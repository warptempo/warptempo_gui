#pragma once

// THE ICON RENDERER (architect 2026-10-06: one renderer for every icon set) —
// a bundled set's drawings, parsed at launch and rasterised through RESVG
// (resvg's C API, Apache-2.0 OR MIT), built from one pinned source on both
// devices (android/deps/85_resvg.sh, CMakeLists.txt): scalable and
// antialiased, no bitmap anywhere. This file is a thin wrapper:
// the parse, the size, one render per (drawing, device px), the channel order
// cairo wants. The one file that includes <resvg.h> is svg_icon.cpp.
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

#include <expected>
#include <memory>
#include <string>
#include <string_view>

struct resvg_render_tree;   // resvg.h's opaque tree

namespace svg_icon {

// One parsed file. A value type: copies share the one immutable tree, which
// the last copy frees (resvg_tree_destroy). A re-raster at a new scale is one
// more render of the same tree, never a re-parse.
struct Document {
    std::shared_ptr<resvg_render_tree> tree;
};

// Parse one file's whole bytes. THE ERROR ARM'S PRODUCER is a bundled file
// resvg refuses (a build defect, caught at launch): "not well-formed SVG
// (resvg error N)", N the library's own code; the caller prefixes the set and
// the file.
std::expected<Document, std::string> parse(std::string_view bytes);

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

} // namespace svg_icon
