#pragma once

// THE ONE PROPORTIONAL-TEXT SHAPING CHOKEPOINT for the whole GUI.
//
// Every text surface that goes proportional measures AND paints through the
// same ShapedRun: `shape_text_run` produces it, `width_px` is the measurement,
// `show_shaped_run` paints exactly those glyphs at exactly those offsets. That
// single-run rule is the whole point — width, truncation and hit
// geometry are computed from the same positions the pixels come from, so they
// cannot disagree. It is the proportional successor of the monospace path's
// glyph-count arithmetic, where a character count times one advance was both
// the measurement and a faithful description of the painted row; with a
// proportional face only a real shaping pass carries that property, since
// kerning, GPOS mark placement and ligature substitution all change the
// advance sum in ways no per-character sum reproduces.
//
// A RUN IS LAID OUT ON A GuiFont, the face owner's face at a scale
// (gui_font.h), and the run carries it, so show_shaped_run paints with
// exactly the face that measured it — no caller can hand the painter a
// different one. ONE ROAD AT EVERY SCALE (architect 2026-10-06): the whole
// run shaped by HarfBuzz on the face's scaled font (gui_outline_scaled_font:
// the live face set's file for the use, outlines only), on that font's OWN
// FreeType face (hb-ft), full GPOS/GSUB, its own advances less the tracking
// (gui_tracking_px, gui_font.h), the four math signs set onto the hyphen's
// axis and the pipe onto the digits' band where the live set lifts them
// (gui_sign_lift_px, gui_font.h) — and
// painted
// through cairo_show_glyphs on the same scaled font. The run is seated on a
// baseline the caller hands in, which its seat derived from the face's
// recorded vertical metrics (gui_face_metrics).
//
// PRECONDITIONS (stated, not guarded — no error arm without a producer):
//   - the faces are installed (gui_font_install_bundled, before the first
//     paint on both backends).
//   - `utf8` is well-formed UTF-8. Free-text fields and the editors carry real
//     UTF-8 (architect 2026-08-02); it arrives already filtered
//     (text_editor::replace_selection is the one incoming boundary) or
//     verbatim from a hand-edited file. A malformed byte lays out as U+FFFD's
//     glyph for that one byte; HarfBuzz consumes arbitrary bytes safely.
//   - NO FALLBACK FACE. A codepoint the face does not cover either
//     shapes to .notdef and paints as the empty box — accepted, in the same
//     class as the no-bidi exclusion below.
//   - The current cairo PATH is preserved by show_shaped_run:
//     cairo_show_glyphs neither reads nor touches it.
//
// Runs are single-direction LTR horizontal only: y advances are not modelled,
// and a run's pen walks x alone.

#include "gui_font.h"

#include <cairo/cairo.h>

#include <string_view>
#include <vector>

namespace text_shape {

// One positioned glyph of a shaped run, in pixels, relative to the run's pen
// position. `glyph_index` is the face's GLYPH ID (post-substitution), never a
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

// A shaped run, the font it was laid out on, and its measurement. `width_px`
// is the sum of the x advances — THE width of this text in this font, and the
// only width any caller should use for it.
struct ShapedRun {
    GuiFont                  font;
    std::vector<ShapedGlyph> glyphs;
    double                   width_px = 0.0;
};

// Lay `utf8` out on `font`: HarfBuzz on its face less the tracking (LTR,
// script and language guessed from the text, full GPOS/GSUB). An empty
// string shapes to an empty run of width 0.
ShapedRun shape_text_run(const GuiFont& font, std::string_view utf8);

// Paint `run` with its baseline origin at (x, y) in cairo's current source,
// on the run's own font. Cairo state and the current path are not modified.
void show_shaped_run(cairo_t* cr, const ShapedRun& run, double x, double y);

// A RUN'S FIRST INK, in pixels from the run's pen origin to the left edge of
// its first lit pixel — the left side bearing of the run as cairo will paint
// it (negative where a glyph overhangs its origin), as against the origin
// `width_px` measures from; an empty run answers 0. Measured through cairo's
// own glyph extents (cairo_scaled_font_glyph_extents) on the run's OWN
// positioned glyphs on its own scaled font, so it is the ink show_shaped_run
// puts down — the chokepoint's one-run property, said of ink.
//
// ITS ONE CONSUMER is row 8's STATE LINE (architect 2026-10-08): its first
// ink stands one field pad past the clock's field, the same air the field
// stands in from the lane's edge (paint_bottom_row_buttons_and_clock), so
// the line's origin is pulled back by this bearing — a placement off the
// origin would read wider by whatever bearing the line's first glyph
// carries. Every other layout wants a reserved CELL, which is `width_px`'s
// job. (A two-edged InkExtents stood here 2026-08-30 .. 31 for the state
// cell then butted against the monospace clock; only the left edge has a
// reader now.)
double ink_left_px(const ShapedRun& run);

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
