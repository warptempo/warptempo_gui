#!/usr/bin/env bash
# HarfBuzz -- shaping against freetype and nothing else.
#
# Every optional integration hb ships defaults to `auto` and gets picked up the
# moment a .pc is visible, and hb 12+ additionally defaults raster/vector/gpu/
# subset to `enabled` -- a lot of code this product never calls. Two of the
# disables are load-bearing rather than merely thrifty:
#   -Dcairo=disabled    hb's cairo integration exists for hb-view only, and
#                       leaving it auto makes a CYCLE with cairo, which is built
#                       after this.
#   -Dfreetype=enabled  turns a missing freetype into a hard error instead of a
#                       silent drop of hb-ft -- hb-ft is the whole reason this
#                       library is here (text_shape.cpp).
set -euo pipefail
. "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

src="$(wt_src "$HARFBUZZ_TAR" "$HARFBUZZ_URL" "$HARFBUZZ_SHA256" "harfbuzz-$HARFBUZZ_VER")"
build="$(wt_fresh_build_dir harfbuzz)"

meson setup "$build" "$src" "${wt_meson_common[@]}" \
    -Dfreetype=enabled \
    -Dglib=disabled -Dgobject=disabled -Dicu=disabled \
    -Dcairo=disabled -Dchafa=disabled \
    -Dpng=disabled -Dzlib=disabled \
    -Dgraphite2=disabled -Dgraphite=disabled \
    -Dfontations=disabled -Dharfrust=disabled -Dkbts=disabled -Dwasm=disabled \
    -Ddirectwrite=disabled -Dgdi=disabled -Dcoretext=disabled \
    -Dsubset=disabled -Draster=disabled -Dvector=disabled \
    -Dgpu=disabled -Dgpu_demo=disabled \
    -Dutilities=disabled -Dtests=disabled -Dbenchmark=disabled \
    -Ddocs=disabled -Dintrospection=disabled \
    -Dcpp_std=c++17

ninja -C "$build"
ninja -C "$build" install

wt_check_lib libharfbuzz.a
"$WT_PKGCONFIG_WRAPPER" --modversion harfbuzz
[ -f "$WT_PREFIX/include/harfbuzz/hb-ft.h" ] || wt_die "hb-ft.h missing: freetype integration did not build"
