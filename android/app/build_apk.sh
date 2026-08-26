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
# Pipeline (the spike's, generalized; the Java steps are the sliver's, and
# hasCode=true since it landed):
#   0. debug keystore (keytool)         5. aapt2 link (manifest + assets)
#   1. assets (the two Liberation TTFs)  6. zip the .so (-0) + classes.dex in
#   2. cmake configure                  7. zipalign -P 16
#   3. cmake build (the .so)            8. apksigner sign  9. verify
#   4. javac -> d8 (the Java sliver)

set -euo pipefail

APPDIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
. "$APPDIR/../toolchain/00_env.sh"

BUILD="${WT_ANDROID_BUILD_DIR:-$APPDIR/build-android}"
PKGDIR="$BUILD/package"
ASSETS="$PKGDIR/assets"
STAGING="$PKGDIR/staging"
LIBNAME="libwarptempo_gui.so"
JAVADIR="$APPDIR/java"
CLASSES="$PKGDIR/classes"
DEXDIR="$PKGDIR/dex"
PKG="com.warptempo.gui"
ACTIVITY=".MainActivity"
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
mkdir -p "$ASSETS" "$STAGING/lib/$WT_ABI" "$CLASSES" "$DEXDIR"

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

# --- 4. the Java sliver: javac -> d8 --------------------------------------
# ONE class, com.warptempo.gui.MainActivity, whose whole job is immersive mode
# (Java-only: there is no NDK door to the system bars, and the taskbar owns the
# input of the band the product's transport and modal surface live in). Every
# later Java need joins that class as a method, so this step is built to compile
# a TREE, not a file.
#
# -classpath, NOT -bootclasspath: since JDK 9 the latter is refused unless
# -source/-target is 8 or lower, and the private JDK here is 21. --min-api
# matches the manifest's minSdk so d8 desugars for exactly the floor we ship to.
wt_say "javac (android.jar on the classpath, JDK $("$WT_JDK/bin/javac" -version 2>&1 | cut -d' ' -f2))"
mapfile -t JAVA_SRC < <(find "$JAVADIR" -name '*.java' | sort)
[ "${#JAVA_SRC[@]}" -gt 0 ] || wt_die "no .java under $JAVADIR (the manifest says hasCode=true)"
"$WT_JDK/bin/javac" -source 11 -target 11 -Xlint:-options \
    -classpath "$WT_ANDROID_JAR" \
    -d "$CLASSES" \
    "${JAVA_SRC[@]}"
wt_say "d8 --min-api $WT_API"
"$WT_BUILD_TOOLS/d8" --release \
    --lib "$WT_ANDROID_JAR" \
    --min-api "$WT_API" \
    --output "$DEXDIR" \
    $(find "$CLASSES" -name '*.class' | sort)
[ -f "$DEXDIR/classes.dex" ] || wt_die "d8 produced no classes.dex"
wt_say "classes.dex: $(stat -c%s "$DEXDIR/classes.dex") bytes"

# --- 5. aapt2 link --------------------------------------------------------
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

# --- 6. store the .so, add classes.dex ------------------------------------
# -0 (STORED) is REQUIRED for -P 16 to mean anything, and pairs with the
# manifest's extractNativeLibs="false": the loader mmaps the .so out of the APK.
# classes.dex takes ordinary deflate (nothing aligns it, nothing mmaps it) and
# must sit at the APK ROOT -- hence -j, junking the dex/ path.
cp "$PKGDIR/base.apk" "$PKGDIR/unaligned.apk"
( cd "$STAGING" && zip -u -0 -X -q "$PKGDIR/unaligned.apk" "lib/$WT_ABI/$LIBNAME" )
zip -u -j -X -q "$PKGDIR/unaligned.apk" "$DEXDIR/classes.dex"

# --- 7. align (zipalign BEFORE apksigner, never after) --------------------
wt_say "zipalign -P 16"
"$WT_BUILD_TOOLS/zipalign" -f -P 16 4 "$PKGDIR/unaligned.apk" "$PKGDIR/aligned.apk"

# --- 8. sign --------------------------------------------------------------
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

# --- 9. verify ------------------------------------------------------------
echo
wt_say "VERIFY 1/5 -- apksigner verify"
"$WT_BUILD_TOOLS/apksigner" verify --verbose "$APK" | head -20

echo
wt_say "VERIFY 2/5 -- zipalign -c -P 16 (alignment survives signing)"
"$WT_BUILD_TOOLS/zipalign" -c -P 16 -v 4 "$APK" | grep -E "$LIBNAME|Verification"

echo
wt_say "VERIFY 3/5 -- every LOAD segment 16 KB aligned (align 2**14)"
"$OBJDUMP" -p "$STAGING/lib/$WT_ABI/$LIBNAME" | grep -A2 LOAD

echo
wt_say "VERIFY 4/5 -- DT_NEEDED (nothing that would have to ship beside the app)"
"$READELF" -d "$STAGING/lib/$WT_ABI/$LIBNAME" | grep -E "NEEDED|SONAME"

echo
wt_say "VERIFY 5/5 -- the launchable activity is the sliver, and classes.dex is aboard"
"$WT_BUILD_TOOLS/aapt2" dump badging "$APK" | grep -E "launchable-activity|application-label:"
unzip -l "$APK" | grep -E "classes.dex|$LIBNAME"

echo
wt_say "APK: $APK ($(stat -c%s "$APK") bytes)"
wt_say "package $PKG / $PKG$ACTIVITY / $WT_ABI / minSdk $WT_API targetSdk $WT_TARGET_SDK"
wt_say "install: adb install -r $APK"
wt_say "launch:  adb shell am start -n $PKG/$ACTIVITY"
