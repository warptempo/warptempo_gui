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
# THE REFERENCE CANVAS (2026-10-13) is the same command with WT_CANVAS=cpu: it
# builds in android/app/build-android-cpu/ and stages warptempo-cpu.apk there
# (the canvas block below).
#
# Idempotent: the packaging tree is wiped first. The CMake build tree is NOT
# wiped -- a re-run is an incremental rebuild, which is the whole point of
# having one -- so `rm -rf android/app/build-android` is the clean-build gesture.
#
# Pipeline (the spike's, generalized; the Java steps are the sliver's, and
# hasCode=true since it landed):
#   0. debug keystore (keytool)         5. the launcher icon rendered (resvg + magick)
#   1. assets (the font files and the      into a staged res/; aapt2 compile
#      icons)                                 + link (manifest + res + assets)
#   2. cmake configure                  6. zip the .so (-0) + classes.dex in
#   3. cmake build (the .so)            7. zipalign -P 16
#   4. javac -> d8 (the Java sliver)    8. apksigner sign  9. verify

set -euo pipefail

APPDIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
. "$APPDIR/../toolchain/00_env.sh"

# THE CANVAS (2026-10-13; CMakeLists.txt's WARPTEMPO_CANVAS owns the
# rule): WT_CANVAS=gl (the default, the product) or cpu (the reference build
# the screencap comparison installs in turn). The cpu variant builds in its
# own tree, "<build dir>-cpu" (configure.sh derives the same one), and stages
# its own APK, warptempo-cpu.apk, so both exist side by side; the two share
# the package name and the key, so installing either replaces the other.
CANVAS="${WT_CANVAS:-gl}"
case "$CANVAS" in
    gl|cpu) ;;
    *) wt_die "WT_CANVAS must be gl or cpu, not '$CANVAS'" ;;
esac
BUILD="${WT_ANDROID_BUILD_DIR:-$APPDIR/build-android}"
APKNAME="warptempo.apk"
if [ "$CANVAS" = cpu ]; then
    BUILD="$BUILD-cpu"
    APKNAME="warptempo-cpu.apk"
fi
PKGDIR="$BUILD/package"
ASSETS="$PKGDIR/assets"
STAGING="$PKGDIR/staging"
LIBNAME="libwarptempo_gui.so"
JAVADIR="$APPDIR/java"
CLASSES="$PKGDIR/classes"
DEXDIR="$PKGDIR/dex"
PKG="com.warptempo.gui"
ACTIVITY=".MainActivity"
APK="$BUILD/$APKNAME"

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

# resvg rasterises the launcher icon's drawing and ImageMagick 7's magick
# frames it at every build (step 5); both checked here, before any work, so a
# host without one stops at once.
command -v resvg >/dev/null \
    || wt_die "resvg not found: the launcher icon is rendered at every build -- install resvg (Arch: resvg)"
command -v magick >/dev/null \
    || wt_die "magick not found: the launcher icon is rendered at every build -- install ImageMagick 7 (Arch: imagemagick)"

rm -rf "$PKGDIR"
mkdir -p "$ASSETS" "$STAGING/lib/$WT_ABI" "$CLASSES" "$DEXDIR"

# --- 1. assets ------------------------------------------------------------
# THE PRODUCT'S FONT FILES: Liberation Sans and Liberation Sans Bold
# (TrueType; architect 2026-10-12, the one face), THE NAMES READ OUT OF
# gui_font.h's kGuiFontFiles initializer, the one list (CMakeLists.txt's
# font step reads the same), copied from the repository's own fonts/
# (architect 2026-10-02) -- the very files the Linux executable compiles in,
# so both devices paint from the same bytes and the build depends on no
# installed font package. They are what gui_font_bundled.cpp builds the
# product's faces from, and android_main ABORTS if one is missing -- a font
# that failed to install would otherwise paint silently in cairo's default
# face. They are stored (-0 ttf at aapt2 link, one flag per
# suffix the list spells) so AAsset_getBuffer hands FreeType a pointer
# straight into the mapped APK.
#
# The copies land under the build tree, which .gitignore already ignores
# (`build*/`); the tracked originals are fonts/'s.
FONT_DIR="$APPDIR/../../fonts"
FONT_LIST_SRC="$APPDIR/../../src/gui/gui_font.h"
FONT_FILES=$(sed -n '/kGuiFontFiles\[kGuiFontFileCount\] = {/,/^};/p' "$FONT_LIST_SRC" |
             grep -o '"[^"]*"' | tr -d '"')
[ -n "$FONT_FILES" ] || wt_die "no font names read from $FONT_LIST_SRC's kGuiFontFiles"
for f in $FONT_FILES; do
    [ -f "$FONT_DIR/$f" ] || wt_die "missing $FONT_DIR/$f (the repository's fonts/)"
    cp -f "$FONT_DIR/$f" "$ASSETS/$f"
    wt_say "asset: $f ($(stat -c%s "$ASSETS/$f") bytes)"
done
# THE BUNDLED ICONS (architect 2026-10-06; ONE FLAT FOLDER since 2026-10-19,
# the drop road, icons.h's head): every `.svg` and `.png` of the repository's
# assets/icons/ copied into the package's assets/icons/, which aapt2 link's -A
# packs (deflated) as the APK's `icons/` asset directory -- a dropped PNG rides
# the next build with no other step. A REPEAT IS A SYMLINK in the repository
# (icons.h's kIconNames: AppIcon.svg -> Quaver.svg and four more); cp -L copies
# the file it names, so the APK holds regular files alone. The app reads them
# in place at launch, never copying them out (icons.h's load_set;
# GuiPlatform::bundled_icon_files, platform_android.cpp). An empty folder is a
# build defect and stops the script here.
ICON_DIR="$APPDIR/../../assets/icons"
shopt -s nullglob
ICON_FILES=("$ICON_DIR"/*.svg "$ICON_DIR"/*.png)
shopt -u nullglob
[ "${#ICON_FILES[@]}" -gt 0 ] || wt_die "no .svg or .png files under $ICON_DIR"
mkdir -p "$ASSETS/icons"
cp -fL "${ICON_FILES[@]}" "$ASSETS/icons/"
wt_say "assets: icons, ${#ICON_FILES[@]} files"
# (THE BUNDLED MATERIALS -- the fortepiano chrome's three covers under
# assets/tiles/, 2026-10-21 to 2026-10-23 -- are no longer packed: the hi-fi
# skin's ground is procedural, baked at launch (materials.h's head); a
# `tiles/` folder beside the device config is the one image road.)

# --- 2/3. configure + build ----------------------------------------------
# WT_CANVAS is in the environment, so configure.sh reads the same canvas and
# configures the same tree.
WT_CANVAS="$CANVAS" bash "$APPDIR/configure.sh"
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

# --- 5. the launcher icon, then aapt2 compile + link -----------------------
# res/ holds EXACTLY THE LAUNCHER ICON (the manifest's android:icon): the
# adaptive-icon XML res/mipmap-anydpi-v26/ic_launcher.xml, tracked, and its two
# PNG layers per density, RENDERED HERE AT EVERY BUILD (architect 2026-10-19:
# the icon must follow a drop with no step -- the one-shot generator and its
# committed PNGs retired) into a staged copy of res/ under the package tree,
# which aapt2 compiles. THE DRAWING is AppIcon's file in assets/icons/ by the
# drop road's rule (icons.h's kIconNames: `AppIcon.png` if one is dropped,
# else `AppIcon.svg`), read through its link: THE QUAVER since 2026-10-23,
# AppIcon.svg -> Quaver.svg, the caption's own icon (architect 2026-10-23:
# "the new note glyph on the caption's color"; the conductor's baton,
# AppIcon.png -> Warp.png, 2026-10-18 to 2026-10-23). THE FRAMINGS (architect
# 2026-10-12, "two framings is normal": Android's adaptive mask demands a
# full-bleed square, Linux desktops take any shape -- the Linux icon is the
# bare drawing, packaging/warptempo_gui.svg, a link into the repository,
# docs/INSTALL.md): the FOREGROUND the drawing fitted to the 52 dp art square
# (architect 2026-10-16, his pick off a 60 / 56 / 52 / 48 / 44 / 40 dp
# series on the glass: the 64 dp art filled ~86 % of the launcher's visible
# tile and nearly touched its corners) -- an SVG rendered by RESVG, the
# product's own renderer (svg_icon.cpp), at the art's height, its width the
# viewBox's (the quaver's 34 x 40: 45 / 67 / 89 / 133 / 177 px wide, resvg
# rounding up), never rasterised small and upscaled (magick's own delegate
# renders the SVG at its 34 x 40 user size and blurs at -resize); a dropped
# PNG resized by ImageMagick's Lanczos filter (the GUI's own filter family)
# into the square -- centered on the transparent 108 dp canvas; the
# BACKGROUND that canvas FLAT IN THE CAPTION'S GROUND #141415 (architect
# 2026-10-23; render.h's kCaptionGround), one color over the whole square,
# which the launcher's mask cuts to its shape (#FFFFFF from 2026-10-17,
# Windows Storm's caption start #800080 from 2026-10-16, the Cool Edit
# case's 20 face-ramp bands under Midnight's Face from 2026-10-12 before
# it). Per density f, the 52 dp art A = 52f px on the 108 dp canvas C = 108f
# px (ic_launcher.xml's head holds the table and the safe-zone arithmetic).
# Byte-stable: -strip drops the date chunks. Every GUI pixel is painted by cairo, the roster's icons
# included (assets/icons/'s SVGs rasterised by resvg, its PNGs decoded by
# resvg and scaled by an area filter, svg_icon.cpp; each copied by cairo); the
# app declares no @string, no style, no res/values. aapt2 compile turns the
# staged directory into res.zip, which link takes as a positional input.
APP_ICON="$ICON_DIR/AppIcon.png"
[ -f "$APP_ICON" ] || APP_ICON="$ICON_DIR/AppIcon.svg"
[ -f "$APP_ICON" ] || wt_die "missing $ICON_DIR/AppIcon.svg (the launcher icon's drawing, a link to Quaver.svg)"
LAUNCHER_GROUND="#141415"   # render.h's kCaptionGround
RES_STAGE="$PKGDIR/res"
cp -r "$APPDIR/res" "$RES_STAGE"
for row in mdpi:52:108 hdpi:78:162 xhdpi:104:216 xxhdpi:156:324 xxxhdpi:208:432; do
    IFS=: read -r density art canvas <<< "$row"
    dir="$RES_STAGE/mipmap-$density"
    mkdir -p "$dir"
    case "$APP_ICON" in
        *.svg) resvg -h "$art" "$APP_ICON" -c ;;
        *)     magick "$APP_ICON" -filter Lanczos -resize "${art}x${art}" png32:- ;;
    esac | magick - \
        -background none -gravity center -extent "${canvas}x${canvas}" -strip \
        -define png:compression-level=9 -define png:compression-filter=5 \
        "PNG32:$dir/ic_launcher_foreground.png"
    magick -size "${canvas}x${canvas}" "xc:$LAUNCHER_GROUND" -strip \
        -define png:compression-level=9 -define png:compression-filter=5 \
        "PNG32:$dir/ic_launcher_background.png"
    wt_say "launcher icon: mipmap-$density, art ${art} px on ${canvas} px"
done
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
"$WT_BUILD_TOOLS/aapt2" compile --dir "$RES_STAGE" -o "$PKGDIR/res.zip"
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
# added nothing, zlib being libgit2's bundled copy. THE SET IS PER CANVAS
# (2026-10-13): the cpu canvas's is the seven below; the gl canvas adds
# libEGL.so and libGLESv3.so, its two NDK stable-ABI libraries
# (CMakeLists.txt's canvas block) — nine.
DT_SET=(libc.so libdl.so libm.so libaaudio.so libandroid.so libnativewindow.so
        liblog.so)
if [ "$CANVAS" = gl ]; then
    DT_SET+=(libEGL.so libGLESv3.so)
fi
wt_check_dt_needed "$STAGING/lib/$WT_ABI/$LIBNAME" "${DT_SET[@]}" \
    || wt_die "DT_NEEDED of $LIBNAME is not the $CANVAS canvas's allowlist (above)"
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
