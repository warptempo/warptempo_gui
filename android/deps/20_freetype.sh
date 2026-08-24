#!/usr/bin/env bash
# FreeType -- pass 1 of a linear order, harfbuzz DISABLED.
#
# THE TRAP: freetype's `harfbuzz` option is a combo defaulting to `auto`, and on
# a miss with auto/dynamic it falls back to find_library('dl') -- which SUCCEEDS
# on Android -- and compiles in FT_CONFIG_OPTION_USE_HARFBUZZ_DYNAMIC, i.e. a
# runtime dlopen("libharfbuzz.so") that will not exist inside the APK. Passing
# -Dharfbuzz=disabled explicitly is the whole fix, and it is also what breaks
# the freetype<->harfbuzz cycle. Pass 2 (rebuilding freetype against harfbuzz)
# only improves autohinting for complex scripts; this product paints 12pt Latin,
# so the order stays linear and pass 2 is skipped.
#
# Fontconfig does not enter here at all: freetype has zero fontconfig
# involvement (the dependency runs the other way).
set -euo pipefail
. "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

src="$(wt_src "$FREETYPE_TAR" "$FREETYPE_URL" "$FREETYPE_SHA256" "freetype-$FREETYPE_VER")"
build="$(wt_fresh_build_dir freetype)"

meson setup "$build" "$src" "${wt_meson_common[@]}" \
    -Dharfbuzz=disabled \
    -Dbrotli=disabled \
    -Dbzip2=disabled \
    -Dpng=disabled \
    -Dzlib=internal \
    -Dmmap=enabled \
    -Dtests=disabled \
    -Derror_strings=false

ninja -C "$build"
ninja -C "$build" install

wt_check_lib libfreetype.a
# The .pc carries the LIBTOOL version (cairo wants freetype2 >= 23.0.17, a
# libtool number, not a release number). A hand-rolled .pc claiming "2.14.3"
# is what makes cairo say "found 2.14.3 but need >= 23.0.17".
"$WT_PKGCONFIG_WRAPPER" --modversion freetype2
