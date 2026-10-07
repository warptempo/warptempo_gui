#pragma once

// THE SVG-SUBSET READER (architect 2026-10-06: "we definitely want that
// capability") — a bundled icon set's drawings, read at launch and drawn
// through cairo on both devices, scalable and antialiased. A small in-tree
// reader rather than an SVG library: the drawings stay SOURCE, and the
// subset is exactly what the bundled files spell (Tango 0.8.90's 48-unit
// Inkscape files, the chrome spec's set, assets/icons/tango/).
//
// THE SUBSET is svg_icon.cpp's tables, each row with its rule: the XML an
// Inkscape file of the period writes (one declaration, comments, double-quoted
// attributes, prefixed foreign elements and attributes skipped), the elements
// svg / g / path / rect / defs / metadata / linearGradient / radialGradient /
// stop, the paint and stroke properties (as presentation attributes or in
// `style`), the matrix / translate / scale transforms and userSpaceOnUse
// gradients reached through an xlink:href chain. THE SUBSET GROWS WITH A
// PRODUCER: a construct joins when a bundled file spells it, its producer
// named at its rule, and is refused until then.
//
// THE ERROR RULE: a bundled set is the program's own, so a file outside the
// subset is a BUILD DEFECT the launch catches — the validation doctrine's
// class (1) at the load boundary, the first error only, hard fail
// (icons::load_svg_set carries it to gui_main's one stderr line and no
// window). parse() resolves everything a paint needs (the cascade, every
// url(#id) and href chain, every `d` dry-run through the one interpreter), so
// rasterise() cannot fail.
//
// GUI-ONLY, like icons.cpp: warptempo_cli never carries this TU.

#include <cairo/cairo.h>

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace svg_icon {

struct Tree;   // the parsed, resolved drawing (svg_icon.cpp)

// One parsed file. A value type: copies share the one immutable tree.
struct Document {
    std::shared_ptr<const Tree> tree;
};

// Parse one file's whole bytes under the subset. THE ERROR ARM'S PRODUCER is a
// bundled file outside the subset (a build defect, caught at launch): the
// construct in the reader's own words ("element <filter> is outside the icon
// subset"); the caller prefixes the set and the file.
std::expected<Document, std::string> parse(std::string_view bytes);

// The drawing's square cell rasterised at `px` device px a side into a NEW
// ARGB32 surface the caller owns (cairo_surface_destroy): the cell scaled by
// px / its width, nothing snapped, the antialiasing cairo's default.
cairo_surface_t* rasterise(const Document& doc, int px);

// THE SATURATED COPY — ReactOS's disabled glyph (comctl32's ILS_SATURATE |
// ILS_ALPHA at 192, imagelist.c's saturate_image): a NEW surface the caller
// owns, `live`'s every pixel's colour replaced by .30 R + .59 G + .11 B, its
// alpha kept, then all four channels times 192 / 255. Run directly on
// cairo's premultiplied channels: the luminance is a convex combination, so
// it never exceeds the alpha, and the frame's scale commutes with the
// premultiply. std::nearbyint at each channel (the rounding doctrine).
cairo_surface_t* saturated_copy(cairo_surface_t* live);

} // namespace svg_icon
