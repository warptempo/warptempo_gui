#pragma once

#include "materials.h"   // the skin's inks and the key's constants
#include "render.h"   // GuiRect, GuiColor, palette(), the program's accessors

#include <array>
#include <cairo/cairo.h>
#include <cstdint>
#include <optional>

// THE PROGRAM'S PAINTERS (architect 2026-10-09, "chrome means chrome, the
// program is Cool Edit"; program_spec.h's head): the time field, the
// cue's chip and the coverage stamp (the well's regions — the trim bar, the ruler, the marker
// lane — are painted by render_trim_flags, paint_ruler_row and the flag
// pass). The lengths are program_spec.h's and render.h's (each a rounded
// part); the colors the installed theme's — the flat look's tones
// (materials.h's THE WELL) and the palette's, every one read from palette(). THE PLAYHEAD'S OWN COLORS are the palette's
// `playhead_head` — the head in the ruler, Live's cyan 03C3D5 since
// 2026-11-01 — and `playhead_line` — the line's one static tone in the
// canvas, Default's 81E1EA, white half way to the head (2026-11-12) —
// palette_file.h's role table, as the
// marker's chip is the palette's `cue_fill` (the accent, 2026-11-13) in
// its `cue_outline` (2026-11-05), a
// disabled one's `inactive_cue` in its `inactive_cue_outline` (2026-11-12).
// THE TRIANGLES ARE SMOOTH-EDGED AT THEIR EXACT PLACES (architect 2026-11-14,
// send J, reopening 2026-10-26's aliased staircase: "I think the chips
// should also go anti-aliased"): the cue's chip and the playhead's head are
// one drawing, two 45° triangles stamped at exact coverage
// (CoverageStampPass::chip, below).

// (THE KEY — every case of the band, row 8, the controls rows, the word
// buttons, the scrub's thumb and the on-screen keyboard — is the flat
// look's, materials.h's paint_key, since 2026-11-01.)

// (THE QUAVER'S PAINTER — paint_quaver, the mock's vector, the
// caption's icon and the lists' row glyph for the morning of 2026-10-23 — is
// retired with his pick; the title bar's and the lists' note is Breeze's
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
// and the 39-px height derived from them, 2026-11-04, the unit). ON ITS REAL
// EDGES (architect 2026-11-15, the no-grid arc): `field` is an exact rect —
// its rows centered exactly on the key face beside it, half a row over the
// face above and below where their parities differ (paint_handler.cpp's
// time_field_rect) — filled at exact coverage over the strip's ground.
void paint_ce_time_field(cairo_t* cr, const GuiRectF& field);

// (THE TAB STRIP — paint_ce_tab_strip, ce_tab_strip_layout, kCeTabCount and
// ce_tab_growth_px, Cool Edit's Organizer tabs, 2026-10-18 — IS REMOVED with
// the strip, architect 2026-10-22, the simple layout.)

// -- THE CUE'S CHIP ---------------------------------------------------------------

// THE CUE'S AND THE PLAYHEAD HEAD'S ONE DRAWING (architect 2026-11-14 night,
// send J, on the mock's moved staircase: "That would make them the odd one
// out. It'd make them snap in and out of sharpness. You'd have a sharp chip
// next to a blurrier one ... I think the chips should also go
// anti-aliased" — and, to the apex standing exactly on the stem's center,
// "Agree on both."): A SMOOTH-EDGED DOWN-POINTING TRIANGLE IN ITS ONE-W
// OUTLINE AT EVERY POSITION, the landing's included, ITS APEX EXACTLY ON THE
// MARK'S EXACT x — the stem's center, the playhead line's center (reopening
// 2026-11-06's "it should point at the right column", the apex one cell
// right of the stem's center, and 2026-10-26's aliased staircase). The
// geometry is render.h's cue block (cue_chip_h_px, cue_chip_fill_drop_px,
// cue_chip_fill_h_px), with H the chip's height and yb THE BOTTOM EDGE of
// the row the apex stands on:
//   THE OUTER, in the outline's color: apex (x, yb), height and half-base H
//     — its sides at 45°, the envelope of the staircase it replaces and its
//     area;
//   THE FILL, in the fill's color, over it: the outer inset by one W across
//     each of its three edges — its top edge one W under the outer's, its
//     apex W · √2 above the outer's (a 45° side moved one W across itself
//     moves √2 W along a row), so its height and half-base are
//     H − W − W · √2.
// A chip with no `outline` is the fill's triangle alone, at the same place.
// NO SHADOW (architect 2026-11-02, the well's part of the flat look). The
// colors are resolve_flag_face's for a cue (render.cpp, the one table: the
// fill `cue_fill` in the `cue_outline`, a disabled chip's `inactive_cue` in
// the `inactive_cue_outline`) and the `playhead_head` in the `cue_outline`
// for the playhead's head (the same chip, mock k1, 2026-11-04).
// PAINTED THROUGH THE COVERAGE STAMP (CoverageStampPass::chip, below): each
// triangle's exact coverage of each pixel (coverage_of_triangle,
// coverage_stamp.h), the outer's then the fill's over it, straight into the
// target — the flag cache's surface for the lane's cues (one pass for the
// whole lane, paint_cues, render.cpp), the backbuffer for the ruler's head.
// THE PAINTED EXTENT is [x − H, x + H]: for a mark on column `col` (x within
// half a px of it) the pixel columns [col − H − 1, col + H + 1) — render.h's
// cue_chip_paint_reach_px, every cull's and damage box's; the hit boxes
// keep their own extent (cue_chip_hit_left_px / cue_chip_hit_right_px).
// One chip alone (the playhead's head): a one-chip pass.
void paint_ce_cue_chip(cairo_t* cr, double x, double yb, GuiColor fill,
                       std::optional<GuiColor> outline = std::nullopt);

// -- THE COVERAGE STAMP ---------------------------------------------------------

// THE CHROME'S COVERAGE STAMP (architect 2026-11-14, send J: "the
// anti-aliasing I would like to commit to that as well, the full
// anti-aliasing"; "I would expect snapping nothing to integers and leaving
// them rounding to yield decent results"): the lanes' marks placed by time
// stand at their exact places (displayed_x_at, warp_frame_map_view.h), so
// their edges are REAL, and each is written STRAIGHT INTO THE CONTEXT'S
// TARGET under its clip boxes — every pixel the shape touches takes the
// shape's opaque word blended over the word it holds, at THE EXACT AREA of
// the pixel the shape covers (coverage_stamp.h owns both arithmetics:
// coverage_of_rect, blend_word). A pixel covered whole takes the word
// itself, so a shape on whole edges writes what a whole-cell fill writes.
// NO CAIRO PATH PER MARK (architect 2026-11-14, "Agree to all decisions."):
// the lanes repaint on every gesture frame with marks by the hundred, and a
// path per mark is cairo's tessellation and span compositing per mark.
// A PIXEL COVERED WHOLE TAKES THE WORD BY A STORE, THE SAME BYTES AS THE
// BLEND'S (blend_word at a == 1 is the source word, its own identity), so
// the stamp fills each row's whole-covered run — a rect's interior columns,
// a triangle's between its two slanted edges — without the blend's
// arithmetic, and blends only the pixels an edge crosses; a pixel the shape
// does not reach is not touched (the blend at a == 0 is the target's word).
// WHAT A PASS READS ONCE (the same bytes by construction: each is a pure
// function of its input and of state no paint changes — the scale, the
// installed display transform): THE CHIP'S THREE LENGTHS, at the pass's
// first chip (a pass of rects alone never reads them), and A COLOR'S WORD,
// kept for the last four colors the pass was handed — a lane's chips
// alternate an outline and a fill, the enabled pair and the disabled pair
// interleaved where both stand, so its hundreds of triangles convert four
// colors once each.
// THE PASS: begin() takes the direct target once
// (direct_pixel_target, render.h: the surface flushed, the clip's boxes
// copied — a clip region's boxes are disjoint, so no pixel is blended twice
// by one shape — the translation read), each shape grows one dirty box,
// end() marks it once. THE CALLER'S CONTRACT: between
// begin() and end() nothing changes the context's clip, matrix, operator or
// group, and no cairo drawing falls between the shapes (the pass's cairo
// road, where the target is refused, keeps its own save / restore).
// WHERE THE TARGET IS REFUSED every shape of the pass is filled through
// cairo as a path at cairo's default antialiasing — the same geometry at
// cairo's own coverage steps, NOT BYTE-IDENTICAL to the stamp on a pixel an
// edge crosses (identical on whole edges). The road's conditions are
// render.h's THE DIRECT ROAD.
// THE SHAPES: a rect with real edges — the selection stem's head segment
// (GuiPaintHandler::paint_marker_stem_heads) and the ruler's ticks
// (paint_ruler_row); THE ROUNDED RECT, the trim bar's pill
// (render_trim_flags; architect 2026-11-17, session 140); the
// down-pointing 45° triangle; and THE CHIP, two of them (THE CUE'S CHIP
// above: the lane's cues, paint_cues, and the ruler's head).
class CoverageStampPass {
public:
    void begin(cairo_t* cr);
    // THE RECT [x0, x1] x [y0, y1], real edges in the context's user space,
    // in `color` (an authored color; the word cue_fill_pixel makes of it):
    // nothing where x1 <= x0 or y1 <= y0.
    void rect(double x0, double y0, double x1, double y1, GuiColor color);
    // THE ROUNDED RECT [x0, x1] x [y0, y1], real edges, its left corners
    // quarter disks of radius `rl` and its right ones of `rr` (0 a square
    // corner; each at most half the rect's width and height —
    // coverage_of_rounded_rect's shape, coverage_stamp.h), in `color`:
    // nothing where x1 <= x0 or y1 <= y0.
    void rounded_rect(double x0, double y0, double x1, double y1, double rl,
                      double rr, GuiColor color);
    // THE DOWN-POINTING 45° TRIANGLE of apex (ax, ay) and height `h` in the
    // context's user space (coverage_of_triangle's shape, coverage_stamp.h),
    // in `color`: nothing where h <= 0.
    void triangle(double ax, double ay, double h, GuiColor color);
    // THE CHIP at the mark's exact x, its apex on the row edge `yb` (THE
    // CUE'S CHIP above): the outer triangle in `outline` where given, the
    // fill's over it.
    void chip(double x, double yb, GuiColor fill,
              std::optional<GuiColor> outline);
    void end();

private:
    void grow_dirty(const GuiRect& box);
    // The color's word (cue_fill_pixel), from the pass's kept colors.
    uint32_t word_of(GuiColor color);
    cairo_t*          cr_        = nullptr;
    DirectPixelTarget target_{};
    bool              direct_    = false;
    GuiRect           dirty_{};
    bool              dirty_any_ = false;
    // The kept colors and their words, the oldest replaced first.
    struct KeptWord {
        bool     set = false;
        GuiColor color{};
        uint32_t word = 0;
    };
    static constexpr int kKeptWords = 4;
    KeptWord          kept_[kKeptWords]{};
    int               kept_next_ = 0;
    // The chip's lengths (render.h's cue block), read at the first chip.
    bool              chip_read_   = false;
    double            chip_h_      = 0.0;
    double            chip_drop_   = 0.0;
    double            chip_fill_h_ = 0.0;
};
