#!/usr/bin/env bash
# M2 spike: clean tree -> signed, aligned, verified APK. One command.
#
# Idempotent by construction: the build tree is wiped first, so a re-run means
# exactly what a first run means. Nothing outside android/spike/build/ is written
# except the debug keystore (~/.android/debug.keystore), created once if absent.
#
# The device is NOT involved. There is no adb step here and there must not be one:
# this script's whole job is to produce an artefact that a device will accept.
#
# Pipeline (research §1.7, with the corrections §1.9/§5.5 make to the Java steps
# -- which this spike does not need at all, hasCode="false" holding throughout):
#   0. debug keystore (keytool)         5. zip -0 the .so into the APK
#   1. assets (fonts + WAV)             6. zipalign -P 16
#   2. compile the .so                  7. apksigner sign
#   3. link check (no undefined syms)   8. verify: apksigner / zipalign -c / LOADs
#   4. aapt2 link (manifest + assets)   9. aapt2 dump of the linked manifest

set -euo pipefail

SPIKE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
. "$SPIKE/../toolchain/00_env.sh"

BUILD="$SPIKE/build"
OBJ="$BUILD/obj"
ASSETS="$BUILD/assets"
STAGING="$BUILD/staging"
LIBNAME="libwarptempo_spike.so"
PKG="com.warptempo.spike"
APK="$BUILD/warptempo-spike.apk"

KEYSTORE="${WT_KEYSTORE:-$HOME/.android/debug.keystore}"
KEYALIAS="androiddebugkey"
KEYPASS="android"

# --- 0. the debug keystore ------------------------------------------------
# The AOSP android/androiddebugkey/android triple, so every tool that assumes a
# debug keystore just works. 20000 days: Android identifies an app by
# (package, signing cert), so losing this key means uninstall-and-lose-data.
if [ -f "$KEYSTORE" ]; then
    wt_say "keystore present: $KEYSTORE"
else
    wt_say "creating debug keystore: $KEYSTORE"
    mkdir -p "$(dirname "$KEYSTORE")"
    "$WT_JDK/bin/keytool" -genkeypair -v \
        -keystore "$KEYSTORE" \
        -alias "$KEYALIAS" -keyalg RSA -keysize 4096 -validity 20000 \
        -storepass "$KEYPASS" -keypass "$KEYPASS" \
        -dname "CN=Android Debug,O=Android,C=US"
fi

# --- clean ----------------------------------------------------------------
rm -rf "$BUILD"
mkdir -p "$OBJ" "$ASSETS" "$STAGING/lib/$WT_ABI"

# --- 1. assets ------------------------------------------------------------
"$SPIKE/tools/gen_assets.sh" "$ASSETS"

# --- 2. compile -----------------------------------------------------------
GLUE_DIR="$WT_NDK/sources/android/native_app_glue"
[ -f "$GLUE_DIR/android_native_app_glue.c" ] || wt_die "no native_app_glue in $WT_NDK"

PKGCONF="$WT_PKGCONFIG_WRAPPER"
[ -x "$PKGCONF" ] || wt_die "missing $PKGCONF (run android/toolchain/40_gen_cross_file.sh)"

DEP_CFLAGS="$("$PKGCONF" --cflags cairo-ft harfbuzz freetype2)"
DEP_LIBS="$("$PKGCONF" --static --libs cairo-ft harfbuzz freetype2)"

# -D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__ is LOAD-BEARING, and neither the
# research doc nor M1 mentions it: without it bionic marks every symbol newer than
# minSdk `strict`, and a strict-unavailable symbol is a hard BUILD ERROR that no
# __builtin_available(android N, *) guard can open. With it, such calls become weak
# references that the guard makes safe -- which is the only way the spike can read
# AAudioStream_getHardwareSampleRate (API 34) from a minSdk-30 build.
COMMON_FLAGS="-fPIC -O3 -ffp-contract=off -Wall -Wextra -DANDROID \
  -D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__ -I$GLUE_DIR $DEP_CFLAGS"

wt_say "compiling native_app_glue (NDK $WT_NDK_RELEASE, stock -- its process_input already drains with a while loop)"
"$CC" $COMMON_FLAGS -std=c11 -c "$GLUE_DIR/android_native_app_glue.c" -o "$OBJ/glue.o"

for src in spike_main spike_audio spike_text spike_storage; do
    wt_say "compiling $src.cpp"
    "$CXX" $COMMON_FLAGS -std=c++23 -c "$SPIKE/src/$src.cpp" -o "$OBJ/$src.o"
done

# --- 3. link --------------------------------------------------------------
# --no-undefined is the "no undefined symbols" check the brief asks for: the link
# FAILS rather than deferring a missing symbol to the device's loader.
# --start/--end-group covers the freetype<->harfbuzz static cycle.
wt_say "linking $LIBNAME"
"$CXX" -shared -o "$STAGING/lib/$WT_ABI/$LIBNAME" \
    "$OBJ/glue.o" "$OBJ/spike_main.o" "$OBJ/spike_audio.o" "$OBJ/spike_text.o" \
    "$OBJ/spike_storage.o" \
    -static-libstdc++ \
    -Wl,--no-undefined -Wl,--as-needed \
    -Wl,--start-group $DEP_LIBS -Wl,--end-group \
    -laaudio -landroid -llog -lm -ldl

"$STRIP" --strip-unneeded "$STAGING/lib/$WT_ABI/$LIBNAME"
wt_say "linked: $(stat -c%s "$STAGING/lib/$WT_ABI/$LIBNAME") bytes"

# --- 4. aapt2 link --------------------------------------------------------
# No res/ dir at all: the spike declares no @drawable/@string, and aapt2 still
# emits a valid binary manifest + resources.arsc. -0 stores the assets, which is
# what lets AAsset_getBuffer hand FreeType a pointer straight into the mapped APK.
wt_say "aapt2 link"
"$WT_BUILD_TOOLS/aapt2" link \
    -o "$BUILD/base.apk" \
    -I "$WT_ANDROID_JAR" \
    --manifest "$SPIKE/AndroidManifest.xml" \
    -A "$ASSETS" \
    --min-sdk-version "$WT_API" \
    --target-sdk-version "$WT_TARGET_SDK" \
    --version-code 1 \
    --version-name "0.1-spike" \
    -0 ttf -0 wav \
    --auto-add-overlay

# --- 5. store the .so -----------------------------------------------------
# -0 (STORED) is REQUIRED for -P 16 to mean anything, and pairs with the
# manifest's extractNativeLibs="false": the loader mmaps the .so out of the APK.
cp "$BUILD/base.apk" "$BUILD/unaligned.apk"
( cd "$STAGING" && zip -u -0 -X -q "$BUILD/unaligned.apk" "lib/$WT_ABI/$LIBNAME" )

# --- 6. align (zipalign BEFORE apksigner, never after) --------------------
wt_say "zipalign -P 16"
"$WT_BUILD_TOOLS/zipalign" -f -P 16 4 "$BUILD/unaligned.apk" "$BUILD/aligned.apk"

# --- 7. sign --------------------------------------------------------------
# v2 AND v3 explicitly. targetSdk >= 30 makes "v2 or later" mandatory, and at
# minSdk 30 apksigner would otherwise settle for v3 alone (correct, but v2 costs
# nothing and removes the question from a device that has to accept this by hand).
wt_say "apksigner sign"
"$WT_BUILD_TOOLS/apksigner" sign \
    --v2-signing-enabled true --v3-signing-enabled true \
    --ks "$KEYSTORE" \
    --ks-pass "pass:$KEYPASS" --key-pass "pass:$KEYPASS" \
    --ks-key-alias "$KEYALIAS" \
    --out "$APK" \
    "$BUILD/aligned.apk"

# --- 8. verify ------------------------------------------------------------
echo
wt_say "VERIFY 1/4 -- apksigner verify"
"$WT_BUILD_TOOLS/apksigner" verify --verbose --print-certs "$APK"
# The default verify runs at the APK's own minSdk (30), where only v3 is
# exercised, so it reports v2 as false even though the v2 block is there. Re-ask
# at 24 to show it: this line is evidence, not a second signature.
echo "  (v2 block presence, verified at min-sdk 24:)"
"$WT_BUILD_TOOLS/apksigner" verify --min-sdk-version 24 --verbose "$APK" | grep "v2 scheme"

echo
wt_say "VERIFY 2/4 -- zipalign -c -P 16 (alignment survives signing)"
"$WT_BUILD_TOOLS/zipalign" -c -P 16 -v 4 "$APK" | grep -E "$LIBNAME|Verification"

echo
wt_say "VERIFY 3/4 -- every LOAD segment 16 KB aligned (align 2**14)"
"$OBJDUMP" -p "$STAGING/lib/$WT_ABI/$LIBNAME" | grep -A2 LOAD

echo
wt_say "VERIFY 4/4 -- DT_NEEDED (nothing that would have to ship beside the app)"
"$READELF" -d "$STAGING/lib/$WT_ABI/$LIBNAME" | grep -E "NEEDED|SONAME"

# --- 9. manifest dump -----------------------------------------------------
echo
wt_say "aapt2 dump badging"
"$WT_BUILD_TOOLS/aapt2" dump badging "$APK"

echo
wt_say "aapt2 dump xmltree (AndroidManifest.xml as linked)"
"$WT_BUILD_TOOLS/aapt2" dump xmltree "$APK" --file AndroidManifest.xml

echo
wt_say "APK: $APK ($(stat -c%s "$APK") bytes)"
wt_say "package $PKG / android.app.NativeActivity / $WT_ABI / minSdk $WT_API targetSdk $WT_TARGET_SDK"
