#pragma once

// THE ICON RENDERER (architect 2026-10-06: one renderer for every icon set) —
// a bundled set's drawings, parsed at launch and rasterised through RESVG
// (resvg's C API, Apache-2.0 OR MIT), built from one pinned source on both
// devices (android/deps/85_resvg.sh, CMakeLists.txt): scalable and
// antialiased, no bitmap anywhere — BUT ONE: THE RASTER SEAT (architect
// 2026-10-18, his recorded exception to the set's scalable rule, this file
// alone: Warp's baton, his GIMP cutout of Gemini Flash's drawing, a 2048 x
// 2048 PNG, and its copy AppIcon — "it's already very high res, so we can ship
// it plain and have it scaled by the gui scale and the button casing";
// icons::Icon::Baton). Its PNG is DECODED ONCE, at the load, by resvg itself
// (parse_png) and SCALED BY AN AREA FILTER here, Lanczos-3 from the full
// raster (rasterise), never by resvg's own <image> resampling. This file is a
// thin wrapper: the parse, the size, one render per (drawing, device px), the
// channel order cairo wants. The one file that includes <resvg.h> is
// svg_icon.cpp.
//
// THE ERROR RULE: the launch refuses ONLY MALFORMED SVG — resvg's parse error
// (not well-formed XML, no root <svg>, no usable size) is the validation
// doctrine's class (1) at the load boundary, the first error only, hard fail
// naming the set and the file (icons::load_set carries it to gui_main's
// one stderr line and no window) — AND, for the raster seat, a file that is
// not a PNG or not a square one (parse_png), the same class. A construct resvg does not draw is SKIPPED
// SILENTLY, as SVG's own error handling prescribes, so the launch cannot
// vouch for a drawing's picture: A SET IS CHECKED AT ITS IMPORT, ON A SHEET,
// against rsvg-convert (the reference every bundled set has been judged by).
// rasterise() cannot fail.
//
// THREADS: a parsed tree is immutable and a render writes only the caller's
// surface, so any thread may render any tree; the product parses and renders
// on the main thread alone (every draw is the painter's).
//
// GUI-ONLY, like icons.cpp: warptempo_cli never carries this TU nor resvg.

#include <cairo/cairo.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

struct resvg_render_tree;   // resvg.h's opaque tree

namespace svg_icon {

// One parsed file: EXACTLY ONE OF THE TWO IS ENGAGED — `tree` for an SVG
// file (parse), `raster` for the raster seat (parse_png: its PNG decoded at
// its native N x N into a premultiplied ARGB32 surface, held for the
// process's life — ~16 MB for the 2048 file — so a scale change drops the
// cache of scaled rasters, never the decode). A value type: copies share the
// one immutable tree or surface, which the last copy frees
// (resvg_tree_destroy, cairo_surface_destroy). A re-raster at a new scale is
// one more render of the same tree or one more resample of the same surface,
// never a re-parse.
struct Document {
    std::shared_ptr<resvg_render_tree> tree;
    std::shared_ptr<cairo_surface_t>   raster;
};

// Parse one file's whole bytes. THE ERROR ARM'S PRODUCER is a bundled file
// resvg refuses (a build defect, caught at launch): "not well-formed SVG
// (resvg error N)", N the library's own code; the caller prefixes the set and
// the file. The options are resvg's defaults: every bundled drawing names
// its own inks (icons.h's head).
std::expected<Document, std::string> parse(std::string_view bytes);

// THE RASTER SEAT'S PARSE (architect 2026-10-18; the head): one PNG file's
// whole bytes, decoded once into the Document's `raster`. THE ERROR ARM'S
// PRODUCERS, a bundled file that is not what the seat needs (a build defect,
// caught at launch, the caller prefixing the set and the file): "not a PNG"
// (no PNG signature or no IHDR chunk first), "not a square PNG (W x H)", and
// resvg's refusal of the wrapper (the code, as parse's line). A PNG whose
// body does not decode renders transparent, as an SVG construct resvg does
// not draw is skipped: the set's import sheet is that check.
std::expected<Document, std::string> parse_png(std::string_view bytes);

// The drawing at `px` device px a side (px > 0) into a NEW ARGB32 surface the
// caller owns (cairo_surface_destroy): scaled uniformly by the smaller of
// px / its width and px / its height and centred (a square drawing fills the
// cell), the offset unsnapped, the antialiasing resvg's. THE RASTER SEAT
// fills the px cell the same way (k = px / N device px per source px), its
// samples an AREA FILTER: Lanczos-3, separable, from the full N x N raster
// on premultiplied channels (PIL's Image.LANCZOS construction, the mock the
// architect judged, 2026-10-18), std::nearbyint at each channel.
//
// `shift_x` / `shift_y` TRANSLATE THE DRAWING in its own user units before the
// scale (positive right / down), composed into resvg's one render transform
// (the raster seat's: into the filter's sample positions, one user unit N / 48
// source px — the raster's user space is the 48-unit frame, unit_px);
// a user-unit shift scales with the raster, so it holds at every px. Zero
// leaves the drawing exactly as the file centers it.
//
// `margin_px` widens the SURFACE alone, right and bottom (px + margin_px a
// side): the fit is still the px cell's, so a drawing shifted right and down
// keeps the ink that crosses the cell's edge — the disabled glyph's light
// copy (icons.h's THE DISABLED GLYPH).
cairo_surface_t* rasterise(const Document& doc, int px, double shift_x = 0.0,
                           double shift_y = 0.0, int margin_px = 0);

// ONE USER UNIT OF THE DRAWING in device px at `px` — rasterise's scale (the
// smaller of px / its width and px / its height); px / 48 for every bundled
// file, each a 48-unit root — THE RASTER SEAT'S USER SPACE THE SAME 48-UNIT
// FRAME (px / 48, its N px read as 48 units), so kIconShifts and the disabled
// copy's one-unit translate mean what they mean on every file.
double unit_px(const Document& doc, int px);

// THE EMBOSSED COPY — the disabled glyph (icons.h's THE DISABLED GLYPH,
// architect 2026-10-15): Windows' DSS_DISABLED emboss of the glyph's
// silhouette as Cool Edit draws it, plus the product's own inset rim, built
// from TWO MASKS, each a raster's ALPHA ALONE (its coverage, so antialiased
// edges stay soft; colors are never read): `live`, the glyph's own raster
// (S), and `light`, the same drawing rasterised already translated by its
// offset (L: rasterise's shift and margin — the offset is the drawing's,
// antialiased exactly, never a shifted raster). A NEW surface the caller
// owns, `light`'s size (at least `live`'s), both masks at (0, 0), holding
// three flat tones (each a straight 0xRRGGBB) premultiplied at their
// coverages: `light_rgb` at L·(1 − S), the copy outside the silhouette;
// `shadow_rgb` at min(S, L), the silhouette's body; `inset_rgb` at
// max(0, S − L), the silhouette's pixels the copy does not reach — a rim
// one offset wide INSIDE S along its top-left edges. The footprint is
// S + L·(1 − S), the two-tone emboss's: the rim never grows the glyph.
// std::nearbyint at each channel (the rounding doctrine).
cairo_surface_t* embossed_copy(cairo_surface_t* live, cairo_surface_t* light,
                               uint32_t light_rgb, uint32_t shadow_rgb,
                               uint32_t inset_rgb);

// THE WINDOW'S COPY (architect 2026-10-08, display_transform.h's head): an
// icon's own inks are sRGB, as every authored color, so on the tablet's
// Display-P3 window each pixel of a raster is converted IN PLACE once, at
// raster time — cairo's premultiplied word un-premultiplied, converted and
// premultiplied again (display_transform::display_premultiplied, which keeps
// a neutral's word whole); off a P3 window it touches nothing. The
// conversion keeps every pixel's alpha, so the embossed copy above reads its
// masks off a converted raster as well as an unconverted one; its tones are
// sRGB constants and the copy is converted after (icons.cpp).
void convert_to_display(cairo_surface_t* s);

} // namespace svg_icon
