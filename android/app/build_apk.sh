#!/usr/bin/env bash
# warptempo_gui: configure -> cross-build -> signed, aligned, verified APK.
# One command, from a clean tree:
#
#     bash android/app/build_apk.sh
#
# THE DEVICE IS NOT INVOLVED and there must be no adb step here: this script's
# whole job is to produce an artefact a device will accept. Installing it is
#
#     adb install -r android/app/build-android/warptempo.apk
#
# Idempotent: the packaging tree is wiped first. The CMake build tree is NOT
# wiped -- a re-run is an incremental rebuild, which is the whole point of
# having one -- so `rm -rf android/app/build-android` is the clean-build gesture.
#
# Pipeline (the spike's, generalized; the Java steps stay absent, hasCode=false
# holding throughout):
#   0. debug keystore (keytool)        4. aapt2 link (manifest + assets)
#   1. assets (the two Liberation TTFs) 5. zip -0 the .so into the APK
#   2. cmake configure                 6. zipalign -P 16
#   3. cmake build (the .so)           7. apksigner sign  8. verify

set -euo pipefail

APPDIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
. "$APPDIR/../toolchain/00_env.sh"

BUILD="${WT_ANDROID_BUILD_DIR:-$APPDIR/build-android}"
PKGDIR="$BUILD/package"
ASSETS="$PKGDIR/assets"
STAGING="$PKGDIR/staging"
LIBNAME="libwarptempo_gui.so"
PKG="com.warptempo.gui"
APK="$BUILD/warptempo.apk"

KEYSTORE="${WT_KEYSTORE:-$HOME/.android/debug.keystore}"
KEYALIAS="androiddebugkey"
KEYPASS="android"

# --- 0. the debug keystore ------------------------------------------------
# The AOSP android/androiddebugkey/android triple, shared with the M2 spike so
# both APKs come off one key. Android identifies an app by (package, signing
# cert): losing this key means uninstall-and-lose-data rather than
# upgrade-in-place, so BACK IT UP.
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

rm -rf "$PKGDIR"
mkdir -p "$ASSETS" "$STAGING/lib/$WT_ABI"

# --- 1. assets ------------------------------------------------------------
# THE TWO LIBERATION FACES AND NOTHING ELSE. They are what gui_font_bundled.cpp
# resolves the product's two families to, there being no fontconfig on the
# platform, and android_main ABORTS if either is missing -- a font that failed
# to install would otherwise paint silently in cairo's default face. They are
# stored (-0 ttf at aapt2 link) so AAsset_getBuffer hands FreeType a pointer
# straight into the mapped APK.
#
# They land under the build tree deliberately: .gitignore already ignores
# `build*/`, so the copied third-party font binaries stay out of the repo with
# no .gitignore edit, on the same reasoning that keeps android/prebuilt/ out.
FONT_DIR="${WT_LIBERATION_DIR:-/usr/share/fonts/liberation}"
for f in LiberationSans-Regular.ttf LiberationMono-Regular.ttf; do
    [ -f "$FONT_DIR/$f" ] ||
        wt_die "missing $FONT_DIR/$f (set WT_LIBERATION_DIR, or pacman -S ttf-liberation)"
    cp -f "$FONT_DIR/$f" "$ASSETS/$f"
    wt_say "asset: $f ($(stat -c%s "$ASSETS/$f") bytes)"
done

# --- 2/3. configure + build ----------------------------------------------
bash "$APPDIR/configure.sh"
wt_say "building $LIBNAME"
cmake --build "$BUILD" -j"$(nproc)"

SO="$BUILD/$LIBNAME"
[ -f "$SO" ] || wt_die "no $SO after the build"
cp -f "$SO" "$STAGING/lib/$WT_ABI/$LIBNAME"
"$STRIP" --strip-unneeded "$STAGING/lib/$WT_ABI/$LIBNAME"
wt_say "linked: $(stat -c%s "$STAGING/lib/$WT_ABI/$LIBNAME") bytes (stripped)"

# --- 4. aapt2 link --------------------------------------------------------
# No res/ dir at all: the app declares no @drawable/@string (every pixel is
# painted by cairo and every icon is an in-tree path), and aapt2 still emits a
# valid binary manifest plus resources.arsc.
wt_say "aapt2 link"
"$WT_BUILD_TOOLS/aapt2" link \
    -o "$PKGDIR/base.apk" \
    -I "$WT_ANDROID_JAR" \
    --manifest "$APPDIR/AndroidManifest.xml" \
    -A "$ASSETS" \
    --min-sdk-version "$WT_API" \
    --target-sdk-version "$WT_TARGET_SDK" \
    --version-code 1 \
    --version-name "2.0" \
    -0 ttf \
    --auto-add-overlay

# --- 5. store the .so -----------------------------------------------------
# -0 (STORED) is REQUIRED for -P 16 to mean anything, and pairs with the
# manifest's extractNativeLibs="false": the loader mmaps the .so out of the APK.
cp "$PKGDIR/base.apk" "$PKGDIR/unaligned.apk"
( cd "$STAGING" && zip -u -0 -X -q "$PKGDIR/unaligned.apk" "lib/$WT_ABI/$LIBNAME" )

# --- 6. align (zipalign BEFORE apksigner, never after) --------------------
wt_say "zipalign -P 16"
"$WT_BUILD_TOOLS/zipalign" -f -P 16 4 "$PKGDIR/unaligned.apk" "$PKGDIR/aligned.apk"

# --- 7. sign --------------------------------------------------------------
# v2 AND v3 explicitly: targetSdk >= 30 makes "v2 or later" mandatory, and at
# minSdk 30 apksigner would otherwise settle for v3 alone.
wt_say "apksigner sign"
"$WT_BUILD_TOOLS/apksigner" sign \
    --v2-signing-enabled true --v3-signing-enabled true \
    --ks "$KEYSTORE" \
    --ks-pass "pass:$KEYPASS" --key-pass "pass:$KEYPASS" \
    --ks-key-alias "$KEYALIAS" \
    --out "$APK" \
    "$PKGDIR/aligned.apk"

# --- 8. verify ------------------------------------------------------------
echo
wt_say "VERIFY 1/4 -- apksigner verify"
"$WT_BUILD_TOOLS/apksigner" verify --verbose "$APK" | head -20

echo
wt_say "VERIFY 2/4 -- zipalign -c -P 16 (alignment survives signing)"
"$WT_BUILD_TOOLS/zipalign" -c -P 16 -v 4 "$APK" | grep -E "$LIBNAME|Verification"

echo
wt_say "VERIFY 3/4 -- every LOAD segment 16 KB aligned (align 2**14)"
"$OBJDUMP" -p "$STAGING/lib/$WT_ABI/$LIBNAME" | grep -A2 LOAD

echo
wt_say "VERIFY 4/4 -- DT_NEEDED (nothing that would have to ship beside the app)"
"$READELF" -d "$STAGING/lib/$WT_ABI/$LIBNAME" | grep -E "NEEDED|SONAME"

echo
wt_say "APK: $APK ($(stat -c%s "$APK") bytes)"
wt_say "package $PKG / android.app.NativeActivity / $WT_ABI / minSdk $WT_API targetSdk $WT_TARGET_SDK"
wt_say "install: adb install -r $APK"
