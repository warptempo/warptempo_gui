#pragma once

// THE ONE PROPORTIONAL-TEXT SHAPING CHOKEPOINT for the whole GUI.
//
// Every text surface that goes proportional measures AND paints through the
// same ShapedRun: `shape_text_run` produces it, `width_px` is the measurement,
// `show_shaped_run` paints exactly those glyphs at exactly those offsets. That
// single-run rule is the whole point — width, truncation and hit
// geometry are computed from the same positions the pixels come from, so they
// cannot disagree. With a proportional face only a real shaping pass carries
// that property, since kerning, GPOS mark placement and ligature
// substitution all change the advance sum in ways no per-character sum
// reproduces.
//
// A RUN IS LAID OUT ON A GuiFont, the face owner's face at a scale and a size
// (gui_font.h), and the run carries it, so show_shaped_run paints with
// exactly the face that measured it — no caller can hand the painter a
// different one. ONE ROAD AT EVERY SCALE (architect 2026-10-06): the whole
// run shaped by HarfBuzz on the face's scaled font (gui_outline_scaled_font:
// the live face set's file for the use, outlines only), on that font's OWN
// FreeType face (hb-ft), full GPOS/GSUB (the `kern` feature alone
// turned off, over a run of digits: THE DIGITS' RULE below), its own
// advances less the tracking
// (gui_tracking_px, gui_font.h), the four math signs set onto the hyphen's
// axis and the pipe onto the digits' band where the live set lifts them
// (gui_sign_lift_px, gui_font.h) — and
// painted
// through THE RASTER ROAD on the same scaled font (text_raster.h, architect
// 2026-11-15, the no-grid arc): every glyph at its EXACT place, to a 64th
// of a px on both axes, the pen and the baseline taken as the caller hands
// them — a glyph whose own place is whole is the glyph cairo_show_glyphs
// would put down, and a glyph inside a run stands on its own fraction (the
// sum of the unhinted advances before it). The run is seated on a baseline the caller hands in, which its
// seat derived from the face's recorded vertical metrics (gui_font.h).
//
// PRECONDITIONS (stated, not guarded — no error arm without a producer):
//   - the faces are installed (gui_font_install_bundled, before the first
//     paint).
//   - `utf8` is well-formed UTF-8. Free-text fields and the editors carry real
//     UTF-8 (architect 2026-08-02); it arrives already filtered
//     (text_editor::replace_selection is the one incoming boundary) or
//     verbatim from a hand-edited file. A malformed byte lays out as U+FFFD's
//     glyph for that one byte; HarfBuzz consumes arbitrary bytes safely.
//   - NO FALLBACK FACE. A codepoint the face does not cover either
//     shapes to .notdef and paints as the empty box — accepted, in the same
//     class as the no-bidi exclusion below.
//   - The current cairo PATH is preserved by show_shaped_run: the raster
//     road composites a mask and neither reads nor touches it.
//   - The context's matrix is a pure translation (text_raster.h's THE
//     CONTEXT; every product context's).
//
// Runs are single-direction LTR horizontal only: y advances are not modelled,
// and a run's pen walks x alone.

#include "gui_font.h"

#include <cairo/cairo.h>

#include <memory>
#include <string_view>
#include <vector>

// text_raster.h's interval of user x, the cut painter's
// (show_shaped_run_within below).
namespace text_raster { struct XSpan; }

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

// THE DIGITS' RULE — A DIGIT NEVER KERNS AGAINST A DIGIT, ON EVERY TEXT THE
// APP SHAPES (architect 2026-11-27; his question, a history count's even
// digits beside a commit date's pinched ones: "Should we do that everywhere
// or nowhere? Because right now it's very inconsistent.", and his "1 agree"
// to everywhere). In every run, each maximal run of TWO OR MORE ASCII
// DIGITS shapes with the one feature `kern = 0` over its bytes (a HarfBuzz
// feature range, start and end the run's byte indices, which are the
// buffer's clusters: append_glyphs, text_shape.cpp); a run with no such
// stretch shapes with no feature list, the face's every default applied.
// Everything else keeps the face's own GPOS kerning.
//
// WHY THIS IS THE WHOLE OF IT (measured on the bundled files, fontTools and
// HarfBuzz, 2026-11-27): Liberation Sans's ten digits all advance 1139 of
// 2048 per em (0.556 em), Regular and Bold alike, and of each weight's 908
// kerning pairs EXACTLY ONE touches a digit, "11": -152 / 2048 em in
// Regular (0.89 U at 12 U, 1.78 device px on the tablet), -113 / 2048 em in
// Bold (0.66 U at 12 U, 0.99 U at the timestamp's 18 U, 0.55 U at the
// ruler's 10 U). So with `kern` off over the digit runs every digit stands
// on one slot of 0.556 em, every number of n digits is n slots wide, and a
// time never walks as it runs (the time fields' fixed slots, paint_handler.
// cpp's THE TIME FIELDS block, 2026-11-05). THE RANGE TOUCHES NOTHING ELSE:
// the face pairs no digit with a non-digit, so the letters, signs and
// spaces round a run kern exactly as the face kerns them, and a lone digit
// is in no range, there being no pair of it to turn off.
//
// A DEPARTURE FROM GTK (its line in docs/engineering/gtk_deviations.md):
// Pango shapes with the face's kerning and would pinch the "11"; Adwaita
// asks `tnum` on a few widgets and Arc's sheet nowhere, and the face has no
// `tnum`, its digits being tabular already.
//
// Lay `utf8` out on `font`: HarfBuzz on its face less the tracking (LTR,
// script and language guessed from the text, full GPOS/GSUB under the
// digits' rule above). An empty string shapes to an empty run of width 0.
ShapedRun shape_text_run(const GuiFont& font, std::string_view utf8);

// A SHARED HANDLE on one memoized run (memoized_run below): its holders read
// the memo's own run, never a copy of its glyphs, and the run is const behind
// the handle, so no holder can change what another reads.
using SharedRun = std::shared_ptr<const ShapedRun>;

// THE LABEL RUNS' SHAPING MEMO (2026-10-13, a cost cut that paints the same
// pixels), ONE BOUNDED MAP FOR EVERY USE BELOW: THE CUE LABELS on the Label face
// (render.cpp: the cue passes and the cell measurer, measure_iter_cells — the
// tablet's profile of a pinch put 3.5 % of all CPU on shaping the visible
// labels at every flag-cache rebuild) and THE RULER'S TIMESTAMPS on the Small
// face (paint_handler.cpp's paint_ruler_row, whose majors re-shaped the same
// strings at every repaint a pan's damage reached the ruler); the title
// bar's brand and battery + clock runs and the score row's page-cell
// measure shape through it too, and so do THE NOTIFICATION CARDS' sentences
// and their wrapped lines (paint_notifications' layout, which runs under
// every damage rect while a card stands; 2026-11-19). A run is a pure
// function of its font and its text — HarfBuzz on the face's own scaled font,
// the tracking and the lifts all the font's (this file's head, gui_font.h),
// the digits' rule (above) the text's own, and a GuiFont is its face,
// percent and size alone (gui_font.h) — so a run
// is looked up here by the face, the percent, the size and the
// text's bytes, and only a text not yet seen under that key is shaped.
// IDENTITY RESTS ON THAT PREMISE: a hit returns
// the very run shape_text_run returned for the same font and bytes, so the
// caller's show_shaped_run puts down the same glyphs at the same offsets as a
// fresh shape would. THE ANSWER IS A HANDLE (SharedRun), so a hit hands out
// the run without copying its glyph vector — into a cue layout and again into
// each segment, hundreds of labels a rebuild. Keyed in this file because the
// premise is the chokepoint's own and every use already shapes through it.
// THE BOUNDING CLEAR (4096 entries, all at once) IS HARMLESS TO A HOLDER: it
// drops the memo's references alone, and a run a caller still holds lives on
// by ownership until its last handle goes. GUI THREAD ONLY, as the scaled
// font every shape runs on is (gui_outline_scaled_font): every caller paints
// the flag cache, the editor or the ruler on it, and the handles never leave
// that thread.
SharedRun memoized_run(const GuiFont& font, std::string_view utf8);

// Paint `run` with its baseline origin at EXACTLY (x, y) in cairo's current
// source, on the run's own font, under the context's clip: no rounding of
// the pen or of the baseline here or below it (text_raster.h — the ink
// stands where x and y say, to a 64th of a px, and no row or column of it
// is cut at a fraction). Cairo state and the current path are not modified.
// The placement goes through one reused per-thread buffer (text_shape.cpp,
// 2026-10-13), so a paint allocates nothing once that buffer has grown.
// GUI THREAD ONLY — THE THREAD CONTRACT of every paint and ink measure in
// this file (show_shaped_run, show_shaped_run_bytes, ink_left_px,
// ink_right_px; the memo's too, above): the raster road's tables are
// unlocked statics (text_raster.h's THE THREAD CONTRACT, which lists the
// callers and the threads that never reach it).
void show_shaped_run(cairo_t* cr, const ShapedRun& run, double x, double y);

// PAINT ONE BYTE RANGE OF `run` — the glyphs whose cluster begins in
// [byte_begin, byte_end) — EACH AT THE PLACE THE WHOLE RUN'S PLACEMENT AT
// (x, y) GIVES IT (the one fixed arithmetic, never a sub-run laid out
// again), in cairo's current source. THE EDITORS' COLOR SPLIT (architect
// 2026-11-15, the no-grid arc): a selection's edges stand on byte
// boundaries, between glyphs, so the selected bytes and the bytes around
// them are whole glyphs each and take their own ink with no clip at all —
// a line painted as its ranges is the line painted whole, less only cairo's
// saturating sum where two glyphs of different ranges overlap, each then
// laid in its own ink. A range that holds no cluster paints nothing.
void show_shaped_run_bytes(cairo_t* cr, const ShapedRun& run, double x,
                           double y, size_t byte_begin, size_t byte_end);

// PAINT `run` AT EXACTLY (x, y), CUT TO THE VISIBLE INTERVALS OF x
// (text_raster::show_glyphs_within, whose contract this hands on: `visible`
// ascending and disjoint, the run's coverage multiplied column by column by
// the length inside them — the ink clipped at each edge at exact coverage,
// with no cairo clip). The marker lane's labels' painter (render.cpp).
void show_shaped_run_within(cairo_t* cr, const ShapedRun& run, double x,
                            double y, const text_raster::XSpan* visible,
                            int nvisible);

// A RUN'S FIRST INK, in pixels from the run's pen origin to THE REAL LEFT
// EDGE OF ITS INK — the left side bearing of the run as it is painted
// (negative where a glyph overhangs its origin), UNROUNDED (architect
// 2026-11-15, the no-grid arc), as against the origin `width_px` measures
// from; an empty run, or one with no ink, answers 0. Measured on the run's
// OWN positioned glyphs by the raster road's own outlines
// (text_raster::ink_box: each glyph's hinted outline's box in 26.6, the box
// its raster is rendered on — the light hinter leaves every x as designed,
// so this is the design's own edge at the size), so it is the ink
// show_shaped_run puts down — the chokepoint's one-run property, said of
// ink — and a seat that aligns ink to an edge aligns it exactly.
//
// ITS CONSUMERS: THE TITLE BAR (render.h's title bar block): the right group's
// runs and its battery cell spaced off each other's ink, the percent's first
// ink the lamps' seat; and THE TIMESTAMP'S DAMAGE CELL (paint_handler.cpp's
// bottom_left_clock_cell, off timestamp_metrics): the widest-digit
// specimen's advance united with its ink's real edges, so an ink that
// overhangs the pen still lies inside the damage. The time fields themselves
// stand at their pens, each slot its specimen's advance (paint_handler.cpp's
// THE TIME FIELDS block). Every other
// layout wants a reserved CELL, which is `width_px`'s job.
double ink_left_px(const ShapedRun& run);
// A RUN'S LAST INK, in pixels from the run's pen origin to the real right
// edge of its ink, measured the same way (the run's own positioned glyphs'
// outline boxes, unrounded); an empty run answers 0. ITS CONSUMERS: THE
// TITLE BAR'S RIGHT GROUP (render.h's title bar block): the clock run's last
// ink 8 U inside the panel's right edge (paint_handler.cpp's
// logo_legend_layout, which reads ink_left_px for the percent's first ink
// column — the lamps' seat); and THE TIMESTAMP'S DAMAGE CELL
// (paint_handler.cpp's bottom_left_clock_cell): the widest-digit specimen's
// advance united with its ink's real edges, so an ink that runs past the
// advance still lies inside the damage.
double ink_right_px(const ShapedRun& run);

// THE BYTE ADDRESS OF A SHAPED RUN: `byte_count + 1` pen offsets, in pixels
// from the run's origin, one per byte BOUNDARY of the shaped string — index 0
// is 0.0 and index `byte_count` is exactly `run.width_px`, so the vector spans
// the run end to end and every caret position and selection edge is one lookup.
//
// This exists because a proportional run has no advance to divide by: the
// only faithful road from a pixel to a byte is the run's OWN accumulated pen,
// read at the boundaries the glyphs' clusters name. Editors are the
// consumers (caret
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
