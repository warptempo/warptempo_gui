#pragma once

// THE ICON RENDERER (architect 2026-10-06: one renderer for every icon set) —
// a bundled set's drawings, parsed at launch and rasterised through RESVG
// (resvg's C API, Apache-2.0 OR MIT), built from one pinned source on both
// devices (android/deps/85_resvg.sh, CMakeLists.txt): scalable and
// antialiased, no bitmap anywhere. This file is a thin wrapper:
// the parse (a bound drawing's under its color, 2026-10-10), the size, one
// render per (drawing, device px), the channel order cairo wants. The one file that includes <resvg.h> is svg_icon.cpp.
//
// THE ERROR RULE: the launch refuses ONLY MALFORMED SVG — resvg's parse error
// (not well-formed XML, no root <svg>, no usable size) is the validation
// doctrine's class (1) at the load boundary, the first error only, hard fail
// naming the set and the file (icons::load_svg_set carries it to gui_main's
// one stderr line and no window). A construct resvg does not draw is SKIPPED
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
#include <optional>
#include <string>
#include <string_view>

struct resvg_render_tree;   // resvg.h's opaque tree

namespace svg_icon {

// One parsed file. A value type: copies share the one immutable tree, which
// the last copy frees (resvg_tree_destroy). A re-raster at a new scale is one
// more render of the same tree, never a re-parse; a bound drawing under a new
// color is a new parse (usvg resolves `currentColor` while it builds the
// tree), icons.cpp holding one per (glyph, color).
struct Document {
    std::shared_ptr<resvg_render_tree> tree;
};

// Parse one file's whole bytes. THE ERROR ARM'S PRODUCER is a bundled file
// resvg refuses (a build defect, caught at launch): "not well-formed SVG
// (resvg error N)", N the library's own code; the caller prefixes the set and
// the file. THE BOUND COLOR (2026-10-10, icons.h's BOUND DRAWING): with
// `current_color` (0xRRGGBB, sRGB) the parse hands resvg the user stylesheet
// `svg { color: #RRGGBB; }`, so every `currentColor` in the file resolves to
// it — resvg's own road (resvg_options_set_stylesheet), the bytes untouched;
// with none, resvg's defaults, a `currentColor` resolving to black.
std::expected<Document, std::string> parse(
    std::string_view bytes, std::optional<uint32_t> current_color);

// The drawing at `px` device px a side (px > 0) into a NEW ARGB32 surface the
// caller owns (cairo_surface_destroy): scaled uniformly by the smaller of
// px / its width and px / its height and centred (a square drawing fills the
// cell), the offset unsnapped, the antialiasing resvg's.
cairo_surface_t* rasterise(const Document& doc, int px);

// THE SATURATED COPY — ReactOS's disabled glyph (comctl32's ILS_SATURATE |
// ILS_ALPHA at 192, imagelist.c's saturate_image): a NEW surface the caller
// owns, `live`'s every pixel's colour replaced by .30 R + .59 G + .11 B, its
// alpha kept, then all four channels times 192 / 255. Run directly on
// cairo's premultiplied channels: the luminance is a convex combination, so
// it never exceeds the alpha, and the frame's scale commutes with the
// premultiply. std::nearbyint at each channel (the rounding doctrine).
cairo_surface_t* saturated_copy(cairo_surface_t* live);

// THE SATURATED AND PIXELATED COPY — GTK 2's insensitive icon
// (gtk_default_render_icon's INSENSITIVE arm, gdk_pixbuf_saturate_and_
// pixelate (src, dest, 0.8, TRUE), gdk-pixbuf-util.c): a NEW surface the
// caller owns, each pixel's UNPREMULTIPLIED colour (the pixbuf's) taken to
// the intensity i = .30 R + .59 G + .11 B, then on the pixels whose
// (x + y) is even the grey i / 2 + 127 and on the others each channel's
// saturate 0.2 i + 0.8 v times DARK_FACTOR 0.7, clamped, each level
// truncated to its byte as the pixbuf's uchar assignment does; the alpha
// kept, the result premultiplied again by std::nearbyint. THE CHECKER'S
// CELL IS ONE DEVICE PX (render.h's dither rule: the period technique at
// native resolution), its phase the raster's own corner as the pixbuf's.
cairo_surface_t* saturated_pixelated_copy(cairo_surface_t* live);

// THE STIPPLED COPY — Motif's insensitive pixmap (architect 2026-10-08
// ~17:45, the cde vocabulary; GuiDisabledGlyph::MotifStipple): a NEW surface
// the caller owns, `live`'s pixels kept whole where (x + y) is even and
// cleared (transparent) where it is odd, so the drawing lands through
// Motif's 50 % stipple and the ground shows through the dropped half — no
// color touched, nothing blended. THE CELL IS ONE DEVICE PX, its phase the
// raster's own corner, the drawing's top-left (render.h's stipple pair, the
// words' twin).
cairo_surface_t* stippled_copy(cairo_surface_t* live);

// THE WINDOW'S COPY (architect 2026-10-08, display_transform.h's head): an
// icon's own inks are sRGB, as every authored color, so on the tablet's
// Display-P3 window each pixel of a raster is converted IN PLACE once, at
// raster time — cairo's premultiplied word un-premultiplied, converted and
// premultiplied again (display_transform::display_premultiplied, which keeps
// a neutral's word whole); off a P3 window it touches nothing. The disabled
// copies above are derived from the UNCONVERTED raster (their arithmetic is
// the period's, on sRGB bytes) and converted after (icons.cpp).
void convert_to_display(cairo_surface_t* s);

} // namespace svg_icon
