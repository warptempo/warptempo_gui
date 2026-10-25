#pragma once

// THE RENDERER (architect 2026-10-06: one renderer for every drawing) —
// the bundled SVG drawings (icons.h), parsed at launch and rasterised
// through RESVG (resvg's C API, Apache-2.0 OR MIT), built from one pinned
// source on both devices (android/deps/85_resvg.sh, CMakeLists.txt), scalable
// and antialiased — and THE MATERIALS' PICTURES (below), a PNG or JPEG
// decoded by resvg itself and scaled by an area filter here. This file is a
// thin wrapper: the parse, the size, one render per (drawing, device px), the
// materials' decode and filter, the channel order cairo wants. The one file
// that includes <resvg.h> is svg_icon.cpp.
//
// THE ERROR RULE: the launch refuses ONLY MALFORMED SVG — resvg's parse error
// (not well-formed XML, no root <svg>, no usable size) is the validation
// doctrine's class (1) at the load boundary, the first error only, hard fail
// naming the file (icons::load_set carries it to gui_main's one stderr line
// and no window). A construct resvg does not draw is SKIPPED SILENTLY, as
// SVG's own error handling prescribes, so the launch cannot vouch for a
// drawing's picture: a drawing is checked by eye when it is added.
// rasterise() cannot fail.
//
// THREADS: a parsed tree is immutable and a render writes only the caller's
// surface, so any thread may render any tree; the icons parse and render on
// the main thread alone (every draw is the painter's), and the materials'
// bake runs raster_info and raster_regions on three threads at once, one
// material each (materials.h's launch road, 2026-10-21) — each call parses
// its own wrapper and touches no shared state.
//
// GUI-ONLY, like icons.cpp: warptempo_cli never carries this TU nor resvg.

#include <cairo/cairo.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct resvg_render_tree;   // resvg.h's opaque tree

namespace svg_icon {

// One parsed SVG file. A value type: copies share the one immutable tree,
// which the last copy frees (resvg_tree_destroy). A re-raster at a new scale
// is one more render of the same tree, never a re-parse.
struct Document {
    std::shared_ptr<resvg_render_tree> tree;
};

// Parse one file's whole bytes. THE ERROR ARM'S PRODUCER is a bundled file
// resvg refuses (a build defect, caught at launch): "not well-formed SVG
// (resvg error N)", N the library's own code; the caller prefixes the file.
// The options are resvg's defaults: every bundled drawing names its own inks
// (icons.h's head).
std::expected<Document, std::string> parse(std::string_view bytes);

// The drawing at `px` device px a side (px > 0) into a NEW ARGB32 surface the
// caller owns (cairo_surface_destroy): scaled uniformly by the smaller of
// px / its width and px / its height and centred (a square drawing fills the
// cell), the offset unsnapped, the antialiasing resvg's.
cairo_surface_t* rasterise(const Document& doc, int px);

// -- THE MATERIALS' ROAD (architect 2026-10-21; since 2026-10-23 the hi-fi
// skin's IMAGE ROAD, a `Ground` or `Marker` picture beside the config,
// materials.h) --------------------------------------------------------------
//
// A MATERIAL is a raster file, PNG or JPEG, decoded through resvg (the bytes
// wrapped in a one-<image> SVG as a base64 data URL, rendered at the
// identity, nearest — resvg's raster-images feature decodes both kinds on
// both devices; the tablet's cairo has no PNG reader) and, where it must be
// scaled, SCALED BY AN AREA FILTER (Lanczos-3, separable, PIL's
// construction). A material is a RECTANGLE, OPAQUE and LARGE: only the
// regions the caller keeps are ever resampled, from ONE decode of their
// source span, and past the image's border the filter reads the border's own
// pixels (a material has no transparent margin to fade into).

// The file's kind and pixel size, read off its header. THE ERROR ARM'S
// PRODUCER is a material that is neither a PNG (its signature and IHDR) nor
// a JPEG (FF D8 and a start-of-frame marker
// before the scan) — a drop defect, caught at launch, the caller prefixing
// the file.
struct RasterInfo {
    uint32_t w    = 0;
    uint32_t h    = 0;
    bool     jpeg = false;
};
std::expected<RasterInfo, std::string> raster_info(std::string_view bytes);

// One region [x0, x0 + w) x [y0, y0 + h) of the image DRAWN AT k (device px
// per source px), in the drawn image's pixels.
struct RasterRegion {
    int x0 = 0, y0 = 0, w = 0, h = 0;
};
// EACH REGION INTO A NEW opaque ARGB32 surface w x h the caller owns, sRGB as
// the file holds it, from ONE resvg decode of the regions' union source span:
// the two passes in double in a fixed order (byte-reproducible:
// -ffp-contract=off is on for every build; the kernel stretched by 1 / k on a
// reduction), the
// horizontal pass on demand over a few rows at a time (the intermediate never
// the span), each tap's index clamped into the image, std::nearbyint at each
// channel; AT k = 1 EXACTLY a region is the decoded pixels themselves, its
// rows copied straight out of the decode with no pass at all (a bundled
// cover's bake, materials.h; every region then inside the image). ONE
// RENDER, NEVER ONE PER REGION (2026-10-21): resvg decodes the whole file at
// every render, ~20 ms for a 1920 x 1080 PNG and ~31 ms for 2304 x 1440 on
// the laptop, against ~7 / ~11 ms for filling the whole span, so the regions
// share one render of their union's bounding box. The image's alpha is not
// read: a material is opaque. The error arm's producer is resvg's refusal of
// the wrapper (the code, parse's line).
std::expected<std::vector<cairo_surface_t*>, std::string> raster_regions(
    std::string_view bytes, const RasterInfo& info, double k,
    std::span<const RasterRegion> regions);

// THE WINDOW'S COPY (architect 2026-10-08, display_transform.h's head): an
// icon's own inks are sRGB, as every authored color, so on the tablet's
// Display-P3 window each pixel of a raster is converted IN PLACE once, at
// raster time — cairo's premultiplied word un-premultiplied, converted and
// premultiplied again (display_transform::display_premultiplied, which keeps
// a neutral's word whole); off a P3 window it touches nothing. The
// conversion keeps every pixel's alpha (icons::boxed_ink reads it).
void convert_to_display(cairo_surface_t* s);

} // namespace svg_icon
