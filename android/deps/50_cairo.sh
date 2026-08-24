#!/usr/bin/env bash
# cairo -- image surface + cairo-ft, nothing else.
#
# cairo-ft WITHOUT fontconfig is a real configuration: CAIRO_HAS_FT_FONT is set
# purely from freetype_dep.found(), every Fc* call site in cairo-ft-font.c sits
# inside a #if CAIRO_HAS_FC_FONT block, and cairo-ft.h guards its
# fontconfig.h include the same way. cairo_ft_font_face_create_for_ft_face()
# -- the one this product calls -- is on the non-fontconfig side of the split.
#
# BUT it must be passed EXPLICITLY: cairo's meson.build makes fontconfig
# required on every system not in ['windows','darwin'], and 'android' is not in
# that list, so `auto` still tries fontconfig here.
#
# -Dpng=disabled only drops the SVG surface and the PNG read/write functions
# (and, as a bonus, boilerplate/ and test/, both png-gated). The IMAGE surface
# is gated on pixman alone. -Dzlib=disabled drops the script/PS/PDF surfaces.
# This is the one flag pair in the stack that no packager exercises -- vcpkg and
# termux both hard-code png+zlib enabled -- so it is the first thing to revert
# (to -Dpng=enabled plus a staged libpng, ~200 KB) if anything here bites.
#
# There is no -Dxml and no -Dwin32 option in 1.18.x; meson hard-errors on an
# unknown option, so a recipe carried over from 1.16/autotools must drop them.
set -euo pipefail
. "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

src="$(wt_src "$CAIRO_TAR" "$CAIRO_URL" "$CAIRO_SHA256" "cairo-$CAIRO_VER")"
build="$(wt_fresh_build_dir cairo)"

meson setup "$build" "$src" "${wt_meson_common[@]}" \
    -Dfreetype=enabled \
    -Dfontconfig=disabled \
    -Dpng=disabled \
    -Dzlib=disabled \
    -Dglib=disabled \
    -Dxlib=disabled -Dxcb=disabled -Dxlib-xcb=disabled \
    -Dquartz=disabled -Ddwrite=disabled \
    -Dtee=disabled -Dlzo=disabled -Dspectre=disabled \
    -Dsymbol-lookup=disabled -Dgtk2-utils=disabled \
    -Dtests=disabled -Dgtk_doc=false

ninja -C "$build"
ninja -C "$build" install

wt_check_lib libcairo.a
"$WT_PKGCONFIG_WRAPPER" --modversion cairo
[ -f "$WT_PREFIX/include/cairo/cairo-ft.h" ] || wt_die "cairo-ft.h missing: the ft font backend did not build"
grep -q '^#define CAIRO_HAS_FT_FONT 1'  "$WT_PREFIX/include/cairo/cairo-features.h" \
    || wt_die "CAIRO_HAS_FT_FONT not set"
grep -q '^#define CAIRO_HAS_IMAGE_SURFACE 1' "$WT_PREFIX/include/cairo/cairo-features.h" \
    || wt_die "CAIRO_HAS_IMAGE_SURFACE not set"
if grep -q '^#define CAIRO_HAS_FC_FONT 1' "$WT_PREFIX/include/cairo/cairo-features.h"; then
    wt_die "CAIRO_HAS_FC_FONT is set -- fontconfig leaked into the build"
fi
wt_say "cairo-features.h: FT_FONT yes, IMAGE_SURFACE yes, FC_FONT no"
