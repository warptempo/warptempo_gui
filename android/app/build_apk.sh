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
#   0. debug keystore (keytool)         5. aapt2 compile (res/) + link
#   1. assets (the four font files,         (manifest + res + assets)
#      the bundled theme files and
#      the icon sets)
#   2. cmake configure                  6. zip the .so (-0) + classes.dex in
#   3. cmake build (the .so)            7. zipalign -P 16
#   4. javac -> d8 (the Java sliver)    8. apksigner sign  9. verify

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
# cert): an APK signed by any other key will not install over the app, and
# uninstalling it to make room wipes the tablet's projects. So A MISSING
# KEYSTORE IS NEVER MINTED SILENTLY -- after a botched restore that would
# yield exactly such an APK. The script stops instead and names the restore
# from the backup the runbook keeps ($WARPTEMPO_KEYSTORE_BACKUP; docs/INSTALL.md,
# The signing key). A fresh key is minted only on an explicit
# WT_NEW_KEYSTORE=1, which means a brand-new app identity (a fresh install, the
# old one's data gone).
if [ -f "$KEYSTORE" ]; then
    wt_say "keystore present: $KEYSTORE"
elif [ "${WT_NEW_KEYSTORE:-}" != "1" ]; then
    wt_die "no keystore at $KEYSTORE -- restore it, do not mint one: the app's
    identity is this key, and an APK signed by a new one cannot install over
    the app (uninstalling wipes the tablet's projects). Restore it from your
    backup, \$WARPTEMPO_KEYSTORE_BACKUP:
        mkdir -p \"$(dirname "$KEYSTORE")\"
        cp \"\$WARPTEMPO_KEYSTORE_BACKUP\" \"$KEYSTORE\"
    (docs/INSTALL.md, The signing key). Only for a brand-new app identity:
    WT_NEW_KEYSTORE=1 bash $0"
else
    wt_say "WT_NEW_KEYSTORE=1: creating a NEW debug keystore: $KEYSTORE"
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
# THE PRODUCT'S FOUR FONT FILES: Nimbus Sans Regular and Bold, Tahoma and
# Tahoma Bold (architect 2026-10-06; the list and its order are gui_font.h's
# kGuiFontFiles), copied from the repository's own fonts/
# (architect 2026-10-02) -- the very files the Linux executable compiles in,
# so both devices paint from the same bytes and the build depends on no
# installed font package. They are what gui_font_bundled.cpp builds the
# product's faces from, and android_main ABORTS if one is missing -- a font
# that failed to install would otherwise paint silently in cairo's default
# face. They are stored (-0 otf -0 ttf at aapt2 link) so
# AAsset_getBuffer hands FreeType a pointer straight into the mapped APK.
#
# The copies land under the build tree, which .gitignore already ignores
# (`build*/`); the tracked originals are fonts/'s.
FONT_DIR="$APPDIR/../../fonts"
for f in NimbusSans-Regular.otf NimbusSans-Bold.otf tahoma.ttf tahomabd.ttf; do
    [ -f "$FONT_DIR/$f" ] || wt_die "missing $FONT_DIR/$f (the repository's fonts/)"
    cp -f "$FONT_DIR/$f" "$ASSETS/$f"
    wt_say "asset: $f ($(stat -c%s "$ASSETS/$f") bytes)"
done
# THE BUNDLED THEME FILES (architect 2026-10-05): the repository's generated
# assets/themes/*.theme (tools/theme_catalog/gen_theme_files.py), every one,
# into the package's assets/themes/, which aapt2 link's -A packs as the APK's
# `themes/` asset directory. The app copies them into its own themes/ folder at
# every launch before reading it (theme_file.h; GuiPlatform::
# bundled_theme_files lists and reads them, platform_android.cpp). An empty
# folder is a build defect and stops the script here, as a missing face does.
THEME_DIR="$APPDIR/../../assets/themes"
mkdir -p "$ASSETS/themes"
shopt -s nullglob
THEME_FILES=("$THEME_DIR"/*.theme)
shopt -u nullglob
[ "${#THEME_FILES[@]}" -gt 0 ] || wt_die "no .theme files under $THEME_DIR (tools/theme_catalog/gen_theme_files.py writes them)"
cp -f "${THEME_FILES[@]}" "$ASSETS/themes/"
wt_say "assets: ${#THEME_FILES[@]} theme files"
# THE BUNDLED ICON SETS (architect 2026-10-06): each folder under the
# repository's assets/icons/ (the Tango set, assets/icons/tango/) copied whole
# as SVG into the package's assets/icons/<set>/, which aapt2 link's -A packs
# (deflated) as the APK's `icons/<set>/` asset directory. The app reads them in
# place at launch, never copying them out (icons.h's load_svg_set;
# GuiPlatform::bundled_icon_files, platform_android.cpp). An empty set folder is
# a build defect and stops the script here.
ICON_ROOT="$APPDIR/../../assets/icons"
ICON_SETS=0
for SET_DIR in "$ICON_ROOT"/*/; do
    SET_NAME="$(basename "$SET_DIR")"
    shopt -s nullglob
    SET_FILES=("$SET_DIR"*.svg)
    shopt -u nullglob
    [ "${#SET_FILES[@]}" -gt 0 ] || wt_die "no .svg files under $SET_DIR"
    mkdir -p "$ASSETS/icons/$SET_NAME"
    cp -f "${SET_FILES[@]}" "$ASSETS/icons/$SET_NAME/"
    wt_say "assets: icon set $SET_NAME, ${#SET_FILES[@]} files"
    ICON_SETS=$((ICON_SETS + 1))
done
[ "$ICON_SETS" -gt 0 ] || wt_die "no icon set under $ICON_ROOT"

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
# ONE class, com.warptempo.gui.MainActivity: the full-screen window (both
# system bars hidden, architect 2026-10-01), the car's MediaSession and the
# system clipboard -- the Java-only needs, each with no NDK
# surface (the class's head comment carries each one's reasoning). Every later
# Java need joins that class as a method, so this step is built to compile a
# TREE, not a file.
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

# --- 5. aapt2 compile + link ----------------------------------------------
# res/ holds EXACTLY THE LAUNCHER ICON (the manifest's android:icon): the
# adaptive-icon XML res/mipmap-anydpi-v26/ic_launcher.xml and its two PNG
# layers per density, the caption's drawing (assets/icons/tango/AppIcon.svg,
# Tango's audio-x-generic) over the caption's navy ground, written by
# tools/app_icon/gen_app_icon.sh, rendered once and committed (that XML's head
# comment states the sizes; nothing here renders). Every GUI pixel is painted
# by cairo, the roster's icons included (the Tango set's SVG assets above,
# read through the app's own subset reader, svg_icon.cpp); the app declares no
# @string, no style, no res/values. aapt2 compile turns the
# directory into res.zip, which link takes as a positional input.
# (targetSdk stays 34 rather than opting out of Android 15's edge-to-edge
# enforcement with the windowOptOutEdgeToEdgeEnforcement theme attribute: the
# compile step exists now, but that attribute also needs a res/values style,
# which the app still does not have, and stepping back to 34 is the same result
# with no theme machinery -- 00_env.sh owns the reasoning, 36 included.)
#
# -I is the INSTALLED platform jar (android-$WT_PLATFORM_SDK) and
# --target-sdk-version is the manifest's own number ($WT_TARGET_SDK). They are
# deliberately different and 00_env.sh states both: the runtime gates behaviour
# on the stamped target, never on the jar the app was compiled against.
wt_say "aapt2 compile (res/)"
"$WT_BUILD_TOOLS/aapt2" compile --dir "$APPDIR/res" -o "$PKGDIR/res.zip"
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
    -0 otf \
    -0 ttf \
    --auto-add-overlay \
    "$PKGDIR/res.zip"

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
wt_say "VERIFY 4/5 -- DT_NEEDED is exactly the NDK stable-ABI set (nothing that would have to ship beside the app)"
# The set, compared exactly (this allowlist is its one statement; NOTES.md
# records how it was measured): an extra entry or a missing one fails the
# build. libnativewindow is in it since the frame-rate pin
# (ANativeWindow_setFrameRate lives there, not in libandroid); the git stack
# added nothing, zlib being libgit2's bundled copy.
wt_check_dt_needed "$STAGING/lib/$WT_ABI/$LIBNAME" \
    libc.so libdl.so libm.so libaaudio.so libandroid.so libnativewindow.so \
    liblog.so \
    || wt_die "DT_NEEDED of $LIBNAME is not the allowlist (above)"
"$READELF" -d "$STAGING/lib/$WT_ABI/$LIBNAME" | grep -E "SONAME" || true

echo
wt_say "VERIFY 5/5 -- the launchable activity is the sliver, the launcher icon is aboard, and so is classes.dex"
"$WT_BUILD_TOOLS/aapt2" dump badging "$APK" | grep -E "launchable-activity|application-label:|application-icon"
unzip -l "$APK" | grep -E "classes.dex|$LIBNAME"

echo
wt_say "APK: $APK ($(stat -c%s "$APK") bytes)"
wt_say "package $PKG / $PKG$ACTIVITY / $WT_ABI / minSdk $WT_API targetSdk $WT_TARGET_SDK"
wt_say "install: adb install -r $APK"
wt_say "launch:  adb shell am start -n $PKG/$ACTIVITY"
