#!/usr/bin/env bash
# tools/palette/picker: the Warptempo Picker APK, or the laptop check of its core (README.md).
#
#     bash tools/palette/picker/build_picker.sh            # -> tools/palette/picker/build/warptempo_picker.apk
#     bash tools/palette/picker/build_picker.sh --check    # the laptop check: host build of the core + host_check
#
# A STRIPPED VARIANT OF android/app/build_apk.sh, reusing its pieces rather than forking them: the toolchain and SDK
# paths and wt_say / wt_die from android/toolchain/00_env.sh, the static cairo / pixman / FreeType / HarfBuzz of
# android/prebuilt/arm64-v8a, the Roboto faces of fonts/, and the product's debug keystore. No CMake: a handful of
# clang++ calls through the NDK. The device is not involved; there is no adb step here.
#
# Pipeline: 0. the keystore (present, never minted)   1. assets (the Roboto TTF)   2. compile + link the .so
#           3. javac -> d8 (PickerActivity)   4. aapt2 link (manifest + assets; no res/)   5. zip the .so (-0) and
#           classes.dex in   6. zipalign -P 16   7. apksigner sign   8. verify
#
# -ffp-contract=off on every compile, as the product's: the derived role's linear-light mix must be the mock tool's
# double arithmetic step for step (colour.h), never a fused multiply-add.

set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd -- "$HERE/../../.." && pwd)"
BUILD="$HERE/build"
SRC="$HERE/src"
COMMON_SRC=("$SRC/json.cpp" "$SRC/scene.cpp" "$SRC/fonts.cpp" "$SRC/picker.cpp")
CXXSTD=(-std=c++23 -Wall -Wextra -O2 -ffp-contract=off)

if [ "${1:-}" = "--check" ]; then
    # THE LAPTOP CHECK: the portable core against the mock tool's bytes (src/host_check.cpp's head)
    mkdir -p "$BUILD/check"
    # shellcheck disable=SC2046
    g++ "${CXXSTD[@]}" -o "$BUILD/check/host_check" "${COMMON_SRC[@]}" "$SRC/host_check.cpp" \
        $(pkg-config --cflags --libs cairo freetype2)
    python3 "$HERE/check_refs.py" "$REPO/tools/palette/themes/picker.json" "$BUILD/check"
    mkdir -p "$BUILD/check/work"
    "$BUILD/check/host_check" "$REPO/fonts" "$BUILD/check/work" \
        --export "$BUILD/check/multi" "$BUILD/check/multi/expects.txt" \
        --export "$BUILD/check/scene" "$BUILD/check/scene/expects.txt" \
        --export "$BUILD/check/derived" "$BUILD/check/derived/expects.txt" \
        --linmix "$BUILD/check/linmix.bin" \
        --today "$HERE/check_data/tablet_2026-10-04" \
        --late "$HERE/check_data/tablet_2026-10-04_presets" \
        --models "$BUILD/check/models_ref.txt" \
        --presets "$HERE/presets/presets.json" \
        --kept "$HERE/presets"
    exit 0
fi

# shellcheck source=/dev/null
. "$REPO/android/toolchain/00_env.sh"

PKGDIR="$BUILD/package"
ASSETS="$PKGDIR/assets"
STAGING="$PKGDIR/staging"
OBJ="$BUILD/obj"
CLASSES="$PKGDIR/classes"
DEXDIR="$PKGDIR/dex"
LIBNAME="libwarptempo_picker.so"
PKG="com.warptempo.picker"
ACTIVITY=".PickerActivity"
APK="$BUILD/warptempo_picker.apk"

# --- 0. the keystore: the product's, NEVER MINTED --------------------------
# android/app/build_apk.sh's rule: the debug keystore is the app's identity and a missing one is restored, never
# minted. The picker has no identity of its own to protect, but it signs with the same key so the one restore covers
# both, and it never creates one: a missing keystore stops this script (build_apk.sh's message names the restore).
KEYSTORE="${WT_KEYSTORE:-$HOME/.android/debug.keystore}"
KEYALIAS="androiddebugkey"
KEYPASS="android"
[ -f "$KEYSTORE" ] || wt_die "no keystore at $KEYSTORE -- restore it (docs/INSTALL.md, The signing key); this script never mints one"
wt_say "keystore present: $KEYSTORE"

rm -rf "$PKGDIR"
mkdir -p "$ASSETS" "$STAGING/lib/$WT_ABI" "$OBJ" "$CLASSES" "$DEXDIR"

# --- 1. assets: the one face (Roboto Mono retired 2026-10-05) --------------
for f in Roboto-Regular.ttf; do
    [ -f "$REPO/fonts/$f" ] || wt_die "missing $REPO/fonts/$f"
    cp -f "$REPO/fonts/$f" "$ASSETS/$f"
done
wt_say "assets: the Roboto face"

# --- 2. compile + link ------------------------------------------------------
[ -f "$WT_PREFIX/lib/libcairo.a" ] || wt_die "missing $WT_PREFIX/lib/libcairo.a (android/deps/build_all.sh)"
GLUE="$WT_NDK/sources/android/native_app_glue"
INC=(-I"$WT_PREFIX/include/cairo" -I"$WT_PREFIX/include/freetype2" -I"$WT_PREFIX/include/harfbuzz"
     -I"$WT_PREFIX/include/pixman-1" -I"$GLUE")
wt_say "compiling ($CXX)"
"$CC" -O2 -fPIC -ffp-contract=off -Wno-unused-parameter -I"$GLUE" -c "$GLUE/android_native_app_glue.c" -o "$OBJ/glue.o"
OBJS=("$OBJ/glue.o")
for s in "${COMMON_SRC[@]}" "$SRC/main_android.cpp"; do
    o="$OBJ/$(basename "${s%.cpp}").o"
    "$CXX" "${CXXSTD[@]}" -fPIC "${INC[@]}" -c "$s" -o "$o"
    OBJS+=("$o")
done
wt_say "linking $LIBNAME"
"$CXX" -shared -o "$BUILD/$LIBNAME" "${OBJS[@]}" \
    -static-libstdc++ \
    -Wl,--no-undefined -Wl,-z,max-page-size=16384 \
    -u ANativeActivity_onCreate \
    -L"$WT_PREFIX/lib" \
    -Wl,--start-group -lcairo -lpixman-1 -lfreetype -lharfbuzz -Wl,--end-group \
    -landroid -lnativewindow -llog -lm -ldl
cp -f "$BUILD/$LIBNAME" "$STAGING/lib/$WT_ABI/$LIBNAME"
"$STRIP" --strip-unneeded "$STAGING/lib/$WT_ABI/$LIBNAME"
wt_say "linked: $(stat -c%s "$STAGING/lib/$WT_ABI/$LIBNAME") bytes (stripped)"

# --- 3. the Java sliver: javac -> d8 ----------------------------------------
mapfile -t JAVA_SRC < <(find "$HERE/java" -name '*.java' | sort)
"$WT_JDK/bin/javac" -source 11 -target 11 -Xlint:-options -classpath "$WT_ANDROID_JAR" -d "$CLASSES" "${JAVA_SRC[@]}"
"$WT_BUILD_TOOLS/d8" --release --lib "$WT_ANDROID_JAR" --min-api "$WT_API" --output "$DEXDIR" \
    $(find "$CLASSES" -name '*.class' | sort)
[ -f "$DEXDIR/classes.dex" ] || wt_die "d8 produced no classes.dex"
wt_say "classes.dex: $(stat -c%s "$DEXDIR/classes.dex") bytes"

# --- 4. aapt2 link (no res/: no icon, no strings) ---------------------------
"$WT_BUILD_TOOLS/aapt2" link \
    -o "$PKGDIR/base.apk" \
    -I "$WT_ANDROID_JAR" \
    --manifest "$HERE/AndroidManifest.xml" \
    -A "$ASSETS" \
    --min-sdk-version "$WT_API" \
    --target-sdk-version "$WT_TARGET_SDK" \
    --version-code 1 \
    --version-name "1.0" \
    -0 ttf

# --- 5. the .so stored, classes.dex at the root ----------------------------
cp "$PKGDIR/base.apk" "$PKGDIR/unaligned.apk"
( cd "$STAGING" && zip -u -0 -X -q "$PKGDIR/unaligned.apk" "lib/$WT_ABI/$LIBNAME" )
zip -u -j -X -q "$PKGDIR/unaligned.apk" "$DEXDIR/classes.dex"

# --- 6. align, 7. sign -------------------------------------------------------
"$WT_BUILD_TOOLS/zipalign" -f -P 16 4 "$PKGDIR/unaligned.apk" "$PKGDIR/aligned.apk"
"$WT_BUILD_TOOLS/apksigner" sign \
    --v2-signing-enabled true --v3-signing-enabled true \
    --ks "$KEYSTORE" --ks-pass "pass:$KEYPASS" --key-pass "pass:$KEYPASS" --ks-key-alias "$KEYALIAS" \
    --out "$APK" "$PKGDIR/aligned.apk"

# --- 8. verify ---------------------------------------------------------------
wt_say "VERIFY -- apksigner, zipalign -P 16, LOAD alignment, DT_NEEDED, the activity"
"$WT_BUILD_TOOLS/apksigner" verify "$APK"
"$WT_BUILD_TOOLS/zipalign" -c -P 16 4 "$APK"
"$OBJDUMP" -p "$STAGING/lib/$WT_ABI/$LIBNAME" | grep -A1 LOAD | grep -q 'align 2\*\*14' || wt_die "LOAD segments not 16 KB aligned"
wt_check_dt_needed "$STAGING/lib/$WT_ABI/$LIBNAME" libc.so libdl.so libm.so libandroid.so libnativewindow.so liblog.so \
    || wt_die "DT_NEEDED of $LIBNAME is not the NDK stable set above"
"$WT_BUILD_TOOLS/aapt2" dump badging "$APK" | grep -E "^package:|launchable-activity|application-label:"

wt_say "APK: $APK ($(stat -c%s "$APK") bytes)"
wt_say "install: adb install -r $APK"
wt_say "launch:  adb shell am start -n $PKG/$ACTIVITY"
