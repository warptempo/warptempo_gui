#pragma once

// THE RENDERER (architect 2026-10-06: one renderer for every drawing) —
// the bundled SVG drawings (icons.h), parsed at launch and rasterised
// through RESVG (resvg's C API, Apache-2.0 OR MIT), built from one pinned
// source (deps/85_resvg.sh, CMakeLists.txt), scalable
// and antialiased — and THE RASTER ROAD (below), a PNG or JPEG decoded by
// resvg itself at its own size, the score viewer's pages its reader
// (score_page_worker.h). This file is a thin wrapper: the parse (a bound
// drawing's under its color, 2026-11-02), the size, one render per (drawing,
// zoom), the raster decode, the channel order cairo wants.
// The one file
// that includes <resvg.h> is svg_icon.cpp.
//
// THE ERROR RULE: the launch refuses ONLY MALFORMED SVG — resvg's parse error
// (not well-formed XML, no root <svg>, no usable size) is the validation
// doctrine's class (1) at the load boundary, the first error only, hard fail
// naming the file (icons::load_set carries it to gui_main's one stderr line
// and no window). A construct resvg does not draw is SKIPPED SILENTLY, as
// SVG's own error handling prescribes, so the launch cannot vouch for a
// drawing's picture: a drawing is checked by eye when it is added.
// rasterise_zoom() cannot fail.
//
// THREADS: a parsed tree is immutable and a render writes only the caller's
// surface, so any thread may render any tree; the icons parse and render on
// the main thread alone (every draw is the painter's), and the score
// viewer's page decoder runs raster_info and raster_regions on its own
// thread (score_page_worker.h) — each call parses its own wrapper and
// touches no shared state.
//
// GUI-ONLY, like icons.cpp: warptempo_cli never carries this TU nor resvg.

#include <cairo/cairo.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct resvg_render_tree;   // resvg.h's opaque tree

namespace svg_icon {

// One parsed SVG file. A value type: copies share the one immutable tree,
// which the last copy frees (resvg_tree_destroy). A re-raster at a new scale
// is one more render of the same tree, never a re-parse; a bound drawing
// under a new color is a new parse (usvg resolves `currentColor` while it
// builds the tree), icons.cpp holding one per (drawing, color).
struct Document {
    std::shared_ptr<resvg_render_tree> tree;
};

// Parse one file's whole bytes. THE ERROR ARM'S PRODUCER is a bundled file
// resvg refuses (a build defect, caught at launch): "not well-formed SVG
// (resvg error N)", N the library's own code; the caller prefixes the file.
// THE BOUND COLOR (architect 2026-11-02, icons.h's THE BOUND DRAWING): with
// `current_color` (0xRRGGBB, sRGB) the parse hands resvg the user stylesheet
// `svg { color: #RRGGBB; }`, so every `currentColor` in the file resolves to
// it — resvg's own road (resvg_options_set_stylesheet), the bytes untouched;
// with none, resvg's defaults (a `currentColor` resolving to black: the
// load's check parse, and every drawing that names its own inks).
std::expected<Document, std::string> parse(
    std::string_view bytes,
    std::optional<uint32_t> current_color = std::nullopt);

// The drawing at `zoom` device px per user unit, its origin on the surface's
// top-left, into a NEW ARGB32 surface the caller owns (cairo_surface_destroy)
// of the drawing's size times `zoom`, each side rounded up (resvg's own
// `--zoom`, the approved M5 mock's render), the antialiasing resvg's. EVERY
// GLYPH'S RASTER (icons.h's THE INK BOX), whose ink the caller crops.
// Cannot fail.
cairo_surface_t* rasterise_zoom(const Document& doc, double zoom);

// -- THE RASTER ROAD (architect 2026-10-21; the score viewer's pages its
// reader, score_page_worker.h) ---------------------------------------------
//
// A RASTER is a picture file, PNG or JPEG, decoded through resvg (the bytes
// wrapped in a one-<image> SVG as a base64 data URL, rendered at the
// identity, nearest — resvg's raster-images feature decodes both kinds;
// the tablet's cairo has no PNG reader) AT ITS OWN SIZE: no
// scale here (the score pages' fit is the caller's, cairo's,
// score_page_worker.h). A raster is a RECTANGLE and OPAQUE.

// The file's kind and pixel size, read off its header. THE ERROR ARM'S
// PRODUCER is a file that is neither a PNG (its signature and IHDR) nor a
// JPEG (FF D8 and a start-of-frame marker before the scan), the caller
// naming the file.
struct RasterInfo {
    uint32_t w    = 0;
    uint32_t h    = 0;
    bool     jpeg = false;
};
std::expected<RasterInfo, std::string> raster_info(std::string_view bytes);

// One region [x0, x0 + w) x [y0, y0 + h) of the image, in its own pixels.
struct RasterRegion {
    int x0 = 0, y0 = 0, w = 0, h = 0;
};
// EACH REGION INTO A NEW opaque ARGB32 surface w x h the caller owns, sRGB as
// the file holds it, from ONE resvg decode of the regions' union source span:
// a region is the decoded pixels themselves, its rows copied straight out of
// the decode, every region inside the image (the caller's crop; a score
// page's decode is the whole image, score_page_worker.cpp). ONE
// RENDER, NEVER ONE PER REGION (2026-10-21): resvg decodes the whole file at
// every render, ~20 ms for a 1920 x 1080 PNG and ~31 ms for 2304 x 1440 on
// the laptop, against ~7 / ~11 ms for filling the whole span, so the regions
// share one render of their union's bounding box. The image's alpha is not
// read: a raster is opaque. The error arm's producer is resvg's refusal of
// the wrapper (the code, parse's line).
std::expected<std::vector<cairo_surface_t*>, std::string> raster_regions(
    std::string_view bytes, const RasterInfo& info,
    std::span<const RasterRegion> regions);

// THE WINDOW'S COPY (architect 2026-10-08, display_transform.h's head): an
// icon's own inks are sRGB, as every authored color, so on the tablet's
// Display-P3 window each pixel of a raster is converted IN PLACE once, at
// raster time — cairo's premultiplied word un-premultiplied, converted and
// premultiplied again (display_transform::display_premultiplied, which keeps
// a neutral's word whole); off a P3 window it touches nothing. The
// conversion keeps every pixel's alpha.
void convert_to_display(cairo_surface_t* s);

} // namespace svg_icon
