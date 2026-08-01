#pragma once

// THE ONE PROPORTIONAL-TEXT SHAPING CHOKEPOINT for the whole GUI.
//
// Every text surface that goes proportional measures AND paints through the
// same ShapedRun: `shape_text_run` produces it, `width_px` is the measurement,
// `show_shaped_run` paints exactly those glyphs at exactly those offsets. That
// single-run rule is the whole point — width, truncation, ellipsis and hit
// geometry are computed from the same positions the pixels come from, so they
// cannot disagree. It is the proportional successor of the monospace path's
// glyph-count arithmetic, where a character count times one advance was both
// the measurement and a faithful description of the painted row; with a
// proportional face only a real shaping pass carries that property, since
// kerning, GPOS mark placement and ligature substitution all change the
// advance sum in ways no per-character sum reproduces.
//
// Shaping runs on the cairo scaled font's OWN FreeType face (hb-ft), so the
// glyph ids, hinting size and transform are the ones cairo will render with;
// nothing here selects, substitutes or falls back to a different face. The
// caller owns font policy entirely and hands in whatever cairo_scaled_font_t
// it wants shaped.
//
// PRECONDITIONS (stated, not guarded — no error arm without a producer):
//   - `font` is a FreeType-backed cairo_scaled_font_t in a non-error state.
//     Every cairo font on this platform's Linux/Wayland target is FT-backed,
//     so a non-FT font has no producer here.
//   - `utf8` is well-formed UTF-8. Project input is ASCII-only, and HarfBuzz
//     consumes arbitrary bytes safely in any case, so there is no validation.
//   - `show_shaped_run` is called with the SAME scaled font set on `cr` that
//     the run was shaped with; the glyph ids are that face's, and no other.
//
// Runs are single-direction LTR horizontal only: y advances are not modelled,
// and a run's pen walks x alone.

#include <cairo/cairo.h>

#include <string_view>
#include <vector>

namespace text_shape {

// One positioned glyph of a shaped run, in pixels, relative to the run's pen
// position. `glyph_index` is a FONT GLYPH ID (post-substitution), never a
// character codepoint.
//
// `cluster` is HarfBuzz's own cluster value: the BYTE INDEX into the shaped
// utf8 where this glyph's cluster begins. It is what makes a shaped run
// addressable BY BYTE — which an editor needs and a label does not — and it is
// monotone non-decreasing across an LTR run. Several glyphs may share a cluster
// (a decomposed character) and one glyph may cover several bytes (a ligature or
// a multibyte character); byte_offsets_px below is the one place that turns
// either shape into a per-byte answer.
struct ShapedGlyph {
    unsigned glyph_index = 0;
    unsigned cluster     = 0;
    double   x_offset_px = 0.0;
    double   y_offset_px = 0.0;  // harfbuzz sense: up-positive
    double   x_advance_px = 0.0;
};

// A shaped run and its measurement. `width_px` is the sum of the x advances —
// THE width of this text in this font, and the only width any caller should
// use for it.
struct ShapedRun {
    std::vector<ShapedGlyph> glyphs;
    double                   width_px = 0.0;
};

// Shape `utf8` with `font`'s own FT face: LTR, script and language guessed
// from the text, full GPOS/GSUB (kerning, ligatures, mark placement) — all of
// which cairo's toy text API skips. An empty string shapes to an empty run of
// width 0.
ShapedRun shape_text_run(cairo_scaled_font_t* font, std::string_view utf8);

// Paint `run` with its baseline origin at (x, y), using cairo's current source
// and current scaled font. Cairo state is not modified.
void show_shaped_run(cairo_t* cr, const ShapedRun& run, double x, double y);

// THE BYTE ADDRESS OF A SHAPED RUN: `byte_count + 1` pen offsets, in pixels
// from the run's origin, one per byte BOUNDARY of the shaped string — index 0
// is 0.0 and index `byte_count` is exactly `run.width_px`, so the vector spans
// the run end to end and every caret position and selection edge is one lookup.
//
// This exists because a proportional run has no advance to divide by: the
// monospace path could turn a pixel into a character with one division, and the
// only faithful successor is the run's OWN accumulated pen, read at the
// boundaries the glyphs' clusters name. Editors are the consumers (caret
// placement, selection extents, click-to-byte); labels never need it.
//
// The walk is: accumulate the pen glyph by glyph, and when a glyph's cluster is
// reached, every boundary up to and including it takes the pen's CURRENT value.
// A byte in the MIDDLE of a multi-byte cluster therefore reports its cluster's
// START — a caret cannot land inside an indivisible glyph, which is the correct
// answer and not an approximation. Boundaries past the last glyph take the full
// width. Monotone non-decreasing by construction.
std::vector<double> byte_offsets_px(const ShapedRun& run, size_t byte_count);

} // namespace text_shape
