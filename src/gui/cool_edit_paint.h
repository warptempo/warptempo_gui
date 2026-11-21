#pragma once

#include "materials.h"   // the skin's inks and the key's constants
#include "render.h"   // GuiRect, GuiColor, palette(), the program's accessors

#include <array>
#include <cairo/cairo.h>
#include <cstdint>
#include <optional>

// THE PROGRAM'S PAINTERS (architect 2026-10-09, "chrome means chrome, the
// program is Cool Edit"; program_spec.h's head): the cue's chip and the
// coverage stamp (the well's regions — the trim bar, the ruler, the marker
// lane — are painted by render_trim_flags, paint_ruler_row and the flag
// pass). The lengths are program_spec.h's and render.h's (each a rounded
// part); the colors the installed theme's — the flat look's tones
// (materials.h's THE WELL) and the palette's, every one read from palette(). THE PLAYHEAD'S OWN COLORS are the palette's
// `playhead_head` — the head in the ruler, Live's cyan 03C3D5 since
// 2026-11-01 — and `playhead_line` — the line's one static tone in the
// canvas, Default's 81E1EA, white half way to the head (2026-11-12) —
// palette_file.h's role table, as the
// marker's chip is the palette's `cue_fill` (the accent, 2026-11-13), a
// disabled one's `inactive_cue`, no outline (2026-11-19, send AA).
// THE TRIANGLES ARE SMOOTH-EDGED AT THEIR EXACT PLACES (architect 2026-11-14,
// send J, reopening 2026-10-26's aliased staircase: "I think the chips
// should also go anti-aliased"): the cue's chip and the playhead's head are
// one drawing, one 45° triangle stamped at exact coverage
// (CoverageStampPass::chip, below).

// (THE KEY — every key of the band, the bottom icon row and the controls
// rows, the word buttons and the entries — is Arc's button since send Z's
// stage C, 2026-11-19, materials.h's paint_linked_group, paint_word_button
// and paint_entry, on this file's coverage stamp; the on-screen keyboard's
// keys Arc's buttons in one grid and the scrub Arc's scale (render.cpp's
// paint_scale) on the same stamp since send AA.)

// (THE QUAVER'S PAINTER — paint_quaver, the mock's vector, the
// caption's icon and the lists' row glyph for the morning of 2026-10-23 — is
// retired with his pick; the title bar's and the lists' note is Breeze's
// MusicNote16th since 2026-11-02, icons.h's THE INK BOX.)

// (Every time field is plain text, no box: paint_handler.cpp's THE TIME
// FIELDS.)

// (THE TAB STRIP — paint_ce_tab_strip, ce_tab_strip_layout, kCeTabCount and
// ce_tab_growth_px, Cool Edit's Organizer tabs, 2026-10-18 — IS REMOVED with
// the strip, architect 2026-10-22, the simple layout.)

// -- THE CUE'S CHIP ---------------------------------------------------------------

// THE CUE'S AND THE PLAYHEAD HEAD'S ONE DRAWING (architect 2026-11-14 night,
// send J, on the mock's moved staircase: "That would make them the odd one
// out. It'd make them snap in and out of sharpness. You'd have a sharp chip
// next to a blurrier one ... I think the chips should also go
// anti-aliased" — and, to the apex standing exactly on the stem's center,
// "Agree on both."): A SMOOTH-EDGED DOWN-POINTING TRIANGLE AT EVERY
// POSITION, the landing's included, ITS APEX EXACTLY ON THE MARK'S EXACT x
// — the stem's center, the playhead line's center (reopening 2026-11-06's
// "it should point at the right column", the apex one cell right of the
// stem's center, and 2026-10-26's aliased staircase). The geometry is
// render.h's cue block (cue_chip_h_exact_px), with H the chip's height and
// yb THE BOTTOM EDGE of the row the apex stands on: apex (x, yb), height
// and half-base H, its sides at 45°, ALL OF IT THE FILL — NO OUTLINE, THE
// BARE TRIANGLE OF THE NOMINAL ROWS (architect 2026-11-19, send AA,
// RULINGS §8 / §11, fork A 3 of Z_10: "I would consider removing the
// outline of the chips because, for example, the scroll bar has no
// outline"; "I think I prefer the smaller version without the outline"),
// the apex still on the row's bottom edge. NO SHADOW (architect
// 2026-11-02, the well's part of the
// flat look). The color is resolve_flag_face's for a cue (render.cpp, the
// one table: `cue_fill`, a disabled chip's `inactive_cue`) and the
// `playhead_head` for the playhead's head (the same chip, mock k1,
// 2026-11-04).
// PAINTED THROUGH THE COVERAGE STAMP (CoverageStampPass::chip, below): the
// triangle's exact coverage of each pixel (coverage_of_triangle,
// coverage_stamp.h), straight into the
// target — the flag cache's surface for the lane's cues (one pass for the
// whole lane, paint_cues, render.cpp), the backbuffer for the ruler's head.
// THE PAINTED EXTENT is [x − H, x + H]: for a mark on column `col` (x within
// half a px of it) the pixel columns [col − H − 1, col + H + 1) — render.h's
// cue_chip_paint_reach_px, every cull's and damage box's; the hit boxes
// keep their own extent (cue_chip_hit_left_px / cue_chip_hit_right_px).
// One chip alone (the playhead's head): a one-chip pass.
void paint_ce_cue_chip(cairo_t* cr, double x, double yb, GuiColor fill);

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
// kept for the last sixteen colors the pass was handed — a lane's chips
// take one fill each, the enabled one's and the disabled one's interleaved
// where both stand, so its hundreds of triangles convert two colors once
// each; a linked group fading through its looks (every tone a mix of up to
// five, materials.h's StampTone) converts each look's tones once (architect
// 2026-11-20).
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
// (GuiPaintHandler::paint_marker_stem_heads), the ruler's ticks
// (paint_ruler_row), a lit linked key's left seam and a hovering key's two
// seams (paint_linked_group, in a mixed tone through a fade, materials.h's
// StampTone);
// THE ROUNDED RECT, the trim bar's pill
// (render_trim_flags; architect 2026-11-17, session 140; IN THREE BANDS
// with its caps, architect 2026-11-20) and Arc's keys'
// borders, faces and accents (materials.h's paint_linked_group, send Z,
// 2026-11-19); THE ROUNDED RING, a word button's and an entry's border;
// THE ROUNDED RECT CUT, NOISE's progress line (paint_entry); the
// down-pointing 45° triangle; and THE CHIP, one of them (THE CUE'S CHIP
// above: the lane's cues, paint_cues, and the ruler's head).

// (A STAMP'S TONE, StampTone — an authored color or the mix of several as
// painted — is materials.h's, beside the faces' looks it mixes.)

// THE INK OF A TONE OFF THE STAMP (architect 2026-11-20: a face's
// print and drawing fade with it; materials.h's StampTone): the word
// StampTone's rule makes of `tone` — each color converted for the window
// (cue_fill_pixel), weighed, rounded once — and the context's source set to
// it: a plain tone through the palette's chokepoint as ever (set_palette_source,
// byte for byte every unmixed print), a mix as its word's own bytes, already
// the window's. The text road masks the source with its coverage
// (text_raster.h), so a mixed print costs nothing more than a plain one.
uint32_t stamp_tone_word(const StampTone& tone);
void set_stamp_tone_source(cairo_t* cr, const StampTone& tone);

class CoverageStampPass {
public:
    void begin(cairo_t* cr);
    // THE RECT [x0, x1] x [y0, y1], real edges in the context's user space,
    // in `tone` (an authored color, the word cue_fill_pixel makes of it, or
    // several colors' as-painted mix, StampTone): nothing where x1 <= x0 or
    // y1 <= y0.
    void rect(double x0, double y0, double x1, double y1, StampTone tone);
    // THE ROUNDED RECT [x0, x1] x [y0, y1], real edges, its left corners
    // quarter disks of radius `rl` and its right ones of `rr` (0 a square
    // corner; each at most half the rect's height, the two together at most
    // its width — coverage_of_rounded_rect's shape, coverage_stamp.h), in
    // `tone`:
    // nothing where x1 <= x0 or y1 <= y0.
    void rounded_rect(double x0, double y0, double x1, double y1, double rl,
                      double rr, StampTone tone);
    // THE ROUNDED RECT IN THREE BANDS (architect 2026-11-20: the
    // trim bar's pill and its two caps, THE TRIM BAR'S CAPS AND GRABS,
    // render.h): rounded_rect's shape STAMPED ONCE, its columns cut by two
    // vertical lines `c0` <= `c1` into three regions — [x0, c0] in `t0`,
    // [c0, c1] in `t1`, [c1, x1] in `t2` (a line outside the shape empties
    // its region). A pixel one region alone reaches takes rounded_rect's own
    // stamp in that region's tone, byte for byte; a pixel a line crosses
    // takes each region's tone weighed by THE SHAPE'S EXACT AREA in that
    // region's part of the pixel (area_of_rounded_rect_in_rect), laid and
    // rounded once (blend_word_bands, coverage_stamp.h) — EXACT in real
    // arithmetic wherever the line falls, a round end included, so no pixel
    // is laid twice and no seam shows at a line. THE CAIRO ROAD, where the
    // target is refused, fills the shape once per region under the region's
    // rect as a clip, at cairo's own coverage: a pixel a line crosses is laid
    // by two fills there, source-over, a seam the direct road has not.
    void rounded_rect_bands(double x0, double y0, double x1, double y1,
                            double rl, double rr, double c0, double c1,
                            GuiColor t0, GuiColor t1, GuiColor t2);
    // THE ROUNDED RING (send Z, 2026-11-19: a GTK box's border, the word
    // buttons' and the entries'): the rounded rect [x0, x1] x [y0, y1] of
    // radius `r` on all four corners LESS its padding box — `bw` inside it
    // on every side, of radius max(r − bw, 0) — each pixel the outer's
    // coverage less the padding box's (the kit's gtk_box), in `color`.
    void rounded_ring(double x0, double y0, double x1, double y1, double r,
                      double bw, StampTone tone);
    // THE ROUNDED RECT CUT TO A RECT (send Z, 2026-11-19: Arc's entry
    // progress, the entry's inner box cut at the level's real end and to its
    // foot's rows): rounded_rect's shape, left corners `rl`, right `rr`,
    // inside [cx0, cx1] x [cy0, cy1], each pixel the shape's area inside the
    // cut rect over the cell (area_of_rounded_rect_in_rect), in `tone`.
    void rounded_rect_cut(double x0, double y0, double x1, double y1,
                          double rl, double rr, double cx0, double cy0,
                          double cx1, double cy1, StampTone tone);
    // THE DOWN-POINTING 45° TRIANGLE of apex (ax, ay) and height `h` in the
    // context's user space (coverage_of_triangle's shape, coverage_stamp.h),
    // in `color`: nothing where h <= 0.
    void triangle(double ax, double ay, double h, GuiColor color);
    // THE CHIP at the mark's exact x, its apex on the row edge `yb` (THE
    // CUE'S CHIP above): the one triangle of height H in `fill`.
    void chip(double x, double yb, GuiColor fill);
    void end();

private:
    void grow_dirty(const GuiRect& box);
    // THE GENERIC STAMP the ring and the cut take: every pixel of the device
    // box [bx0, bx1) x [by0, by1) inside the clip boxes at the coverage
    // `cov(px, py)` answers, in device px; `skip(py, a, b)` may name a run
    // of columns [a, b) on that row whose coverage is 0.
    template <class Cov, class Skip>
    void stamp_each(int bx0, int by0, int bx1, int by1, uint32_t word,
                    Cov&& cov, Skip&& skip);
    // The color's word (cue_fill_pixel), from the pass's kept colors.
    uint32_t word_of(GuiColor color);
    // A tone's word (StampTone's rule): its color's, or its colors' words'
    // weighed per-channel mix.
    uint32_t word_of(const StampTone& tone);
    // The cairo road's source for a tone: the color through
    // set_palette_source, or the mixed word's own bytes.
    void set_tone_source(const StampTone& tone);
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
    static constexpr int kKeptWords = 16;
    KeptWord          kept_[kKeptWords]{};
    int               kept_next_ = 0;
    // The chip's height (render.h's cue block), read at the first chip.
    bool              chip_read_   = false;
    double            chip_h_      = 0.0;
};
