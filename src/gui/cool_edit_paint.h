#pragma once

#include "materials.h"   // the skin's inks and the key's constants
#include "render.h"   // GuiRect, GuiColor, palette(), the program's accessors

#include <array>
#include <cairo/cairo.h>
#include <cstdint>
#include <optional>

// THE PROGRAM'S PAINTERS (architect 2026-10-09, "chrome means chrome, the
// program is Cool Edit"; program_spec.h's head): the time field and the
// cue's triangle (the well's regions — the trim bar, the ruler, the marker
// lane — are painted by render_trim_flags, paint_ruler_row and the flag
// pass). The lengths are program_spec.h's and render.h's (each a rounded
// part); the colors the installed theme's — the flat look's tones
// (materials.h's THE WELL) and the palette's, every one read from palette(). THE PLAYHEAD'S OWN COLORS are the palette's
// `playhead_head` — the head in the ruler, Live's cyan 03C3D5 since
// 2026-11-01 — and `playhead_line` — the line's one static tone in the
// canvas, Default's 81E1EA, white half way to the head (2026-11-12) —
// palette_file.h's role table, as the
// marker's chip is the palette's `cue_fill` (the text tone, 2026-11-12) in
// its `cue_outline` (2026-11-05), a
// disabled one's `inactive_cue` in its `inactive_cue_outline` (2026-11-12).
// THE TRIANGLES ARE ALIASED AT 1:1 (architect 2026-10-26, "agree": "at this
// size the staircase is the truthful drawing and the antialiasing only
// blurs it"): the cue's chip and the playhead head's rows, their cells one
// device px, are Cool Edit's staircase in whole cells
// (paint_ce_cue_triangle).

// (THE KEY — every case of the band, row 8, the controls rows, the word
// buttons, the scrub's thumb and the on-screen keyboard — is the flat
// look's, materials.h's paint_key, since 2026-11-01.)

// (THE QUAVER'S PAINTER — paint_quaver, the mock's vector, the
// caption's icon and the lists' row glyph for the morning of 2026-10-23 — is
// retired with his pick; the logo lane's and the lists' note is Breeze's
// MusicNote16th since 2026-11-02, icons.h's THE INK BOX.)

// -- THE TIME FIELD ---------------------------------------------------------------

// THE TIME FIELD on `field` (architect 2026-11-01 night, M5m, "add 1px to
// clock and other related fields to make the spacing even"; Live's numeric
// fields, the transport's position and tempo boxes): A FLAT `key_face` (Default's 363636)
// BOX, NO RING. Its digits are the caller's, the text tone `text`, no
// shadow — every time field in the program face's 22 px, row 8's clock
// (architect 2026-10-26 ~11:30), the render player's two (~12:10, "yes" to
// symmetry; gui_font.h's table) and the score viewer's — centred on both
// axes (render.h's time_field_pad_px / time_field_h_px own the 12-px pads
// and the 39-px height derived from them, 2026-11-04, the unit).
void paint_ce_time_field(cairo_t* cr, const GuiRect& field);

// (THE TAB STRIP — paint_ce_tab_strip, ce_tab_strip_layout, kCeTabCount and
// ce_tab_growth_px, Cool Edit's Organizer tabs, 2026-10-18 — IS REMOVED with
// the strip, architect 2026-10-22, the simple layout.)

// -- THE CUE'S TRIANGLE ---------------------------------------------------------

// COOL EDIT'S POINT-CUE MASK, THE CUES' AND THE PLAYHEAD HEAD'S ONE
// PAINTER: THE FILL, a staircase of `rows` rows of whole cells of one unit u
// (cue_triangle_unit_px, render.h) from `top` — 2·rows − 1 cells on the top
// row down to one, the apex row one cell and each row above it two cells
// wider (15, 13, … 1 at the tablet's eight rows at 200 %; 7, 5, … 1 at
// 100 %'s four) —
// CENTRED ON THE STEM'S COLUMN `col` (architect 2026-10-09 ~14:35: "at every
// scale, the marker triangle must be fully centered on the stem"; the stem
// one W on that column since 2026-11-04, straddling the frame since
// 2026-11-06, render.h's waveform_line_px — so on the tablet the apex, the
// column itself, points at the stem's RIGHT pixel: architect 2026-11-06,
// session 67, "it should point at the right column"), in `fill` — AND,
// given an `outline`, THE
// CHIP: a second, larger staircase of the same construction in the
// outline's color painted first, from cue_chip_top_rows() rows above the
// fill's top through cue_chip_apex_rows() rows below its apex (architect
// 2026-11-04, mock g1, "a one-W 111111 outline"; render.h's cue_chip_rows
// owns the construction: 25 x 13 round the fill's 15 x 8 on the tablet). NO
// SHADOW
// (architect 2026-11-02, the well's part of the flat look, "no shadow
// anywhere").
//
// THE CELL IS ONE DEVICE PX AT EVERY SCALE (architect 2026-10-26, on the
// glass: "I think part of the problem is our flags are so much larger than
// Cool Edit's. I think we should make the triangle smaller, and that would
// make colors like the red from Cool Edit less problematic. I think that is
// a large reason why no colors really look right on the GUI." — then "Why
// not simply leave unscaled? That would preserve the stem/flag size
// relation from CEP"; its own number since the stem became one W,
// 2026-11-04: "untouched: the triangle's cell and the ruler's head"): the
// staircase stands in whole device cells beside the stem, centered on the frame's column
// with its apex over the stem's right pixel,
// the playhead's head with them (render.h's cue_triangle_unit_px, which
// names every reader). THE ROW COUNT IS SCALED
// (architect 2026-10-26 ~13:40, on the row count as a per-device number:
// "why density? why not just simple scale? tablet is only real surface.
// later tablets may have higher dpi, but not lower" — and "yes" to the
// planner's proposal): authored 4 W, 8 device rows on the tablet at 200 %
// (architect 2026-11-12, session 114, "shorter triangles, yes": one W under
// mock v4's 5 W "chip 10" of 2026-11-04 night, the apex on the same row),
// rounded at the element like every length off the canvas (render.h's
// cue_triangle_rows, the one accessor) — 8 rows at 200 %, 4 at 100 %, 2 at
// the scale's floor of 50 %.
//
// DRAWN ALIASED, COOL EDIT'S HARD STAIRCASE (architect 2026-10-26 ~10:40,
// "agree" to "at this size the staircase is the truthful drawing and the
// antialiasing only blurs it"; ~13:05 the antialiased envelope's tuning
// key settled "off" on the glass and retired with its arm,
// closed_questions.md): a bitmap in effect,
// every pixel the color whole, no coverage anywhere. `top` is THE FILL'S top
// row. A bare staircase's painted columns are [col − (rows − 1)u, col +
// rows·u) and its rows [top, top + rows·u); an outlined chip's the chip's
// (cue_triangle_half_w_px,
// cue_triangle_reach_right_px, render.h, the one owner every extent reads)
// over [top − t·u, top + (rows + s)·u). The colors are resolve_flag_face's
// for a cue (render.cpp, the one table: the fill `cue_fill` in the
// `cue_outline`,
// a disabled chip's `inactive_cue` in the `inactive_cue_outline`) and the
// `playhead_head` in the `cue_outline`
// for the playhead's head (the same chip, mock k1, 2026-11-04). TWO
// ROADS, ONE
// DRAWING (CueTrianglePass below): the stamp writes the cells' words
// straight into the target where direct_pixel_target takes it, else cairo
// fills the same cells as whole-pixel rectangles, antialias NONE. The
// triangles paint on the CPU — into the flag cache's surface and the
// ruler's cairo context — never through the canvas sink.
// One triangle alone (the playhead's head): a one-triangle CueTrianglePass.
void paint_ce_cue_triangle(cairo_t* cr, int col, int top, GuiColor fill,
                           std::optional<GuiColor> outline = std::nullopt);

// THE TRIANGLE PASS (2026-10-13, a cost cut that paints the same pixels): the
// cues' triangles, one consecutive run on one context (paint_cues, render.cpp,
// left to right over every label), share ONE PREPARED TARGET instead of each
// taking its own. begin() asks direct_pixel_target once — the surface
// flushed, the clip's rectangle list copied, the translation read — and the
// unit and the rows once; triangle() stamps the cells and GROWS ONE DIRTY
// BOX; end() marks that box dirty once. Where the target is refused, every
// triangle of the pass fills through cairo instead, so the two roads never
// meet in one pass.
// THE CALLER'S CONTRACT: between begin() and end() nothing changes the
// context's clip, matrix, operator or group, so the one target holds the
// clip list and translation each triangle's own direct_pixel_target would
// have answered.
// THE IDENTITY RESTS ON (2026-10-26, the cells aliased): an opaque solid
// filled with OVER and antialias NONE over whole-pixel rectangles, under a
// clip of whole-pixel boxes, covers each pixel whole or not at all, and a
// whole coverage stores the source pixel (cairo's in-place span fill,
// _fill_xrgb32_lerp_opaque_spans, its 255 arm) — the word cue_fill_pixel
// writes, the color's 16-bit shorts' high bytes (_cairo_color_compute_shorts,
// color_to_uint32). The stamp writes that word into exactly the cells cairo's
// fill covers, the cells intersected with each clip box (a cut cell is cut
// alike on both roads, a whole pixel being in a box or not), as the fill
// does. AN OUTLINED CHIP IS TWO STAIRCASES ON ONE ROAD: the outline's cells,
// then the fill's over them, each an opaque OVER fill on cairo's road and a
// word overwrite on the stamp's, so the later cells win alike.
class CueTrianglePass {
public:
    void begin(cairo_t* cr);
    // The triangle — the fill from `top`, and the outline's staircase under
    // it where `outline` is given: the stamp where the target was taken,
    // else cairo's fill.
    void triangle(int col, int top, GuiColor fill,
                  std::optional<GuiColor> outline);
    void end();

private:
    void stamp_span(int x0, int x1, int y, uint32_t pixel);
    cairo_t*          cr_        = nullptr;
    DirectPixelTarget target_{};
    bool              direct_    = false;
    int               u_         = 0;
    int               rows_      = 0;
    int               top_rows_  = 0;
    int               chip_rows_ = 0;
    GuiRect           dirty_{};
    bool              dirty_any_ = false;
};
