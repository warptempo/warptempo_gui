#pragma once

#include "render.h"   // GuiRect, GuiColor, palette(), the program's accessors

#include <array>
#include <cairo/cairo.h>
#include <cstdint>

// THE PROGRAM'S PAINTERS — COOL EDIT PRO 2.1'S PANEL (architect 2026-10-09,
// "chrome means chrome, the program is Cool Edit"; program_spec.h's head):
// the button case in its faces, the toolbar band's lines and panes and
// grooves, the dock bar, row 8's grippers and end bars, the dark time field,
// the shadowed panel label and the cue's triangle — THE SAME UNDER EVERY
// CHROME (the canvas column's lanes — the view bar, the ruler, the cues —
// are painted by render_trim_flags, paint_ruler_row and the flag pass out of
// these and the constants below). The lengths
// are program_spec.h's (render.h's program accessors, each a rounded part);
// the measurements are tmp/research/cool_edit/METRICS.md's, one capture px a
// W, and the approved mock is tmp/mocks/cool_edit/mock_CE_S3_GRID_4.png
// (its generator mock_ce.py the construction these painters follow).
//
// THE COLORS ARE TWO KINDS. THE PANEL'S are the palette's: the `face` role
// and the tones Cool Edit derives from it (cool_edit_derive.h, the COOL EDIT
// BLOCK of palette()), so a new Face moves the whole panel — the time
// field's digits and the panel label's swapping pair among them, and since
// 2026-10-10 THE BUTTON CASE'S face ramp, highlight, shadow ramp and corner
// (the per-row case tones, cool_edit_derive.h's head with the fit's worst
// errors; the default Face's within 2 of the bytes it replaced). THE
// CONSTANTS below are what Cool Edit draws whatever its preset: the case's
// black outline and the pressed and checked rings, the ruler's ink and the
// view bar's field.
// THE DIAGONAL RULE REACHES THE PROGRAM (architect 2026-10-09 ~12:40: "any
// jagged staircase of pixel lines, even if it's only two lines, we take as
// an intention of a diagonal, so that when they're scaled they don't look
// like a pixel that's been scaled up"; the chrome's mitre of 2026-10-06,
// paint_relief_frame's head, render.cpp): THE CAPTURE'S SQUARE PIXEL CORNER
// IS THE PERIOD'S LIMIT AND THE DIAGONAL THE ARTIST'S INTENT. Wherever a
// light tone meets a dark one at a ring's corner, the corner block is split
// along the 45-degree diagonal from the ring's outer corner to its inner one
// through paint_relief_frame (the light square L, the dark mitred L over it,
// antialiased by cairo): the case's highlight / shadow ring (Cool Edit's
// ADAEAF junction pixel is the two tones' half-and-half, the diagonal at one
// capture px) and the down face's black / F8F8F8 ring; the pane's two-line
// edge, the groove's dark column its outer bottom-right (Cool Edit's one mid
// pixel atop the groove is that ring's corner at one capture px); the dock
// bar's and the end bar's light / mid rings; the time field's ring; the
// view bar's span (render_trim_flags). A two-line staircase that is no ring
// — the gripper's etched pairs, the cue's and the playhead head's
// 9-7-5-3-1 rows — is drawn as its diagonal (their painters say how). A
// one-tone line stays square: the case's black outline, the down face's
// 808080 line, the dock bar's dark row, the end bar's dark column, the
// band's edge columns. The dots are cells (a dot is not a line). (Every
// line square-joined as the capture's pixels show stood the morning of
// 2026-10-09; git history.)
// NOT DRAWN: the face's brushed grain (METRICS §1.4: a ±5 2-px texture over
// the ramp, a texture and not a tone — the ramp is drawn smooth).

// -- THE CASE'S CONSTANTS (METRICS §1.3–1.5; 0xRRGGBB, sRGB) -----------------

// THE FACE RAMP (palette().ce_case_ramp, the case tones): ONE STOP PER W ROW
// OF THE 20-W GLYPH SEAT, never resampled (architect 2026-10-09 ~10:25, the
// seat Cool Edit's own 20): stop i fills the seat's W row i, whose device
// rows are [nearbyint(i·g/20), nearbyint((i+1)·g/20)) of the seat's g, so the
// rows tile the seat exactly at every scale, each stop a flat band of its
// bytes. THE HIGHLIGHT (ce_case_highlight) on the top row and left column;
// THE SHADOW on the right column and the bottom row, a ramp from
// ce_case_shadow_first to ce_case_shadow_last across the glyph seat, its
// first tone also the mitre's dark half at the top-right and bottom-left;
// THE SHADOW'S OWN CORNER (ce_case_corner) at the bottom-right inside the
// black line. Where the highlight meets the shadow Cool Edit's measured
// ADAEAF is the two tones' half-and-half — the mitre, drawn as the diagonal
// (the head's rule), so no tone stands for it.
// THE BLACK LINE on the right and bottom, 000000 on every captured Face.
inline constexpr uint32_t kCeCaseOuter = 0x000000;
// THE PRESSED AND CHECKED RINGS (METRICS §1.5, constant over all five
// presets): black on the top and left outside, 808080 inside it, F8F8F8 on
// the bottom and right.
inline constexpr uint32_t kCeRingOuter = 0x000000;
inline constexpr uint32_t kCeRingMid   = 0x808080;
inline constexpr uint32_t kCeRingLight = 0xF8F8F8;
// THE DISABLED GLYPH'S TWO TONES (architect 2026-10-15; icons.h's THE
// DISABLED GLYPH), measured off Cool Edit Pro 2.1's own disabled art —
// BITMAP 221's toolbar strips, each glyph's right 20x20 half drawn on the
// C0C0C0 button face: the silhouette in 808080 over a F8F8F8 copy one px
// right and down, byte for byte on B84 (Play), B8 (Undo), B87 (Stop) and
// B88 (Pause), and on most of the Edit view's B100-B138 — two tones, no dark
// copy. Constants, as the rings are: face-independent. The product's offset
// is the bitmap px read as the glyph's own line, ONE ICON UNIT (the drawing's
// 1 / 48), not one Windows px (architect 2026-10-15).
// THE INSET RIM, THE PRODUCT'S OWN THIRD TONE (architect 2026-10-15 evening),
// not Cool Edit's: the silhouette's pixels the light copy does not reach —
// a one-unit band INSIDE the silhouette along its top-left edges — in
// kCeDisabledInset, the shadow line falling down and to the right as the
// pressed case goes. Inside, never a dark copy peeking out up-left, which
// grew the glyph. The tone his pick off a 70 / 60 / 50 / 40 / 30 / 20 / 10 /
// 00 series judged on the glass the same evening.
inline constexpr uint32_t kCeDisabledShadow = 0x808080;
inline constexpr uint32_t kCeDisabledLight  = 0xF8F8F8;
inline constexpr uint32_t kCeDisabledInset  = 0x505050;

// -- THE CANVAS COLUMN'S CONSTANTS (METRICS §3, §4; 2026-10-09) ---------------
// The bytes Cool Edit draws whatever its preset: THE VIEW BAR'S FIELD, black
// (§3); THE RULER'S TICKS AND DIGITS, E0E0E0 (§4.4, face-independent in all
// five presets), the digits' (+1, +1) SHADOW black (§4.4 and §6: "≈ black,
// observed down to 0D0708", darker than the panel labels' dark tone). (Cool
// Edit's period-2 cursor column over the view bar's span, the playhead's
// color and 000000 alternating, is not drawn: architect 2026-10-09 ~14:30.)
// THE PLAYHEAD'S OWN COLOR — the head and its dots in the canvas — is the
// palette's `playhead_stem`
// since 2026-10-09 ~11:50, Cool Edit's Curs FFFF00 its default in every
// palette (palette_file.h's role table), as the cues' two colors are the
// palette's `cue` and `range`; the triangles' shadow is the derived
// `ce_cue_shadow`. THE RULER'S INK IS NO TONE OF FACE AND NEVER SWAPS
// (architect 2026-10-10, "we follow Cool Edit's theming";
// METRICS_PRESETS_1010.md §2d, §2e): its ticks are E0E0E0 byte for byte on
// all nine Faces captured that day, the light and engraved ones included,
// and its digits' cores the same ink over the same black shadow — the
// ruler's ground is the dark `mid` at every Face, so Cool Edit keeps it
// light where the panel's text and label swap. Not a Face tone left
// undone: following Face here would depart from Cool Edit.
inline constexpr uint32_t kCeViewBarField   = 0x000000;
inline constexpr uint32_t kCeRulerTick      = 0xE0E0E0;
inline constexpr uint32_t kCeRulerShadow    = 0x000000;

// -- THE CASE ------------------------------------------------------------------

// ONE BUTTON CASE on `r` (icon_case_w_px square), in its face:
//   REST     — the highlight on the top row and left column, the face ramp
//              on the glyph seat, the shadow ramp on the right column and
//              bottom row, the two mitred where they meet, the black line
//              on the right and bottom; the glyph at (+1, +1) lines.
//   DOWN     — PRESSED (a live press) or CHECKED (`lamp`), the same face
//              (Cool Edit draws them alike, the window group's checked cases
//              and the zoom button's press on his captures): black on the
//              top row and left column, 808080 inside it, the ramp on the
//              seat one line further in, F8F8F8 on the bottom row and right
//              column, mitred against the black; the glyph at (+2, +2).
//   DISABLED — the face its state gives, the glyph EMBOSSED (the caller's
//              icons::draw_cased_disabled — Cool Edit's own disabled art,
//              its silhouette in kCeDisabledShadow over a kCeDisabledLight
//              copy of the drawing one icon unit right and down, with the
//              product's kCeDisabledInset rim inside the silhouette's
//              top-left edges, icons.h's THE DISABLED GLYPH).
//   HOT      — none: Cool Edit's toolbar has no hover face (program_spec.h's
//              case_hot_face).
// Answers the glyph's shift in device px (one line when down).
int paint_ce_case(cairo_t* cr, const GuiRect& r, bool down);

// -- THE TOOLBAR BAND ----------------------------------------------------------

// THE BAND'S GROUND on the lane `band` (icon_row_h_px tall, the lane's whole
// width): the dark outline on its first row, the RECESS tone over every row
// between, the light line on its last row — Cool Edit's band where no pane
// stands. The panes and the grooves' dark columns are laid over it.
void paint_ce_band_ground(cairo_t* cr, const GuiRect& band);
// ONE PANE over the band's ground, its columns [x0, x1) FROM ITS LIGHT LEFT
// COLUMN THROUGH THE GROOVE'S DARK COLUMN AFTER IT (2026-10-09 ~12:40): a
// two-line raised edge from the band's second row through its last row but
// one (the light last row is the ground's) — the outer ring the light top
// row and left column against the dark column and the dark foot row, the
// inner ring the face against the mid right column and the mid foot row,
// each ring mitred, the face between. So the groove reads mid | dark |
// light, the dark column the left pane's outer edge and the light the next
// pane's (METRICS §1.2).
void paint_ce_pane(cairo_t* cr, const GuiRect& band, int x0, int x1);
// A LONE DARK COLUMN at `x`, one line wide, from the band's second row
// through its last row but one, square: the band's left edge and the column
// before the right-anchored panes, each the outline left of a pane's light
// column with no pane of its own to the left (every other dark column is a
// pane's own, paint_ce_pane).
void paint_ce_band_dark_column(cairo_t* cr, const GuiRect& band, int x);

// -- THE DOCK BAR AND ROW 8 -----------------------------------------------------

// THE DOCK BAR on `bar` (dock_bar_h_px tall, the lane's width): the light
// row across its top and the light left column against the mid right column
// and the mid row, the ring mitred, the face rows within, and the dark row
// across its foot.
void paint_ce_dock_bar(cairo_t* cr, const GuiRect& bar);
// THE GRIPPER at column `x` (five lines wide): two etched double lines —
// light, mid, a face line, light, mid — the light ones from the reach above
// the case's top `case_y` to the reach below its bottom, the mid ones one
// line lower, each pair's two-line step at either end drawn as its diagonal
// (the painter's cut).
void paint_ce_gripper(cairo_t* cr, int x, int case_y, int case_h);
inline int ce_gripper_w_px() {
    return kProgramSpec.gripper_cols * program_line_px();
}
// THE END BAR at column `x` over the row's content band [y, y + h): a light
// column and a light top row over its face against a mid column and a mid
// row along its foot, the ring mitred, and a dark column the band's height
// right of it.
void paint_ce_end_bar(cairo_t* cr, int x, int y, int h);
inline int ce_end_bar_w_px() {
    return 3 * program_line_px() + scaled_px(kProgramSpec.end_bar_face_px);
}

// -- THE TIME FIELD AND THE LABEL -------------------------------------------------

// THE DARK TIME FIELD on `field` (METRICS §5.4): the field-dark line on its
// top row and left column, the mid tone inside, the field-light line on its
// bottom row and right column, the ring mitred. Its digits are the caller's,
// in the palette's `ce_field_text` (the never-swapping light text: the
// field's ground is dark at every Face, cool_edit_derive.h's head).
void paint_ce_time_field(cairo_t* cr, const GuiRect& field);
// A PANEL LABEL (METRICS §6): `run` in the palette's `ce_label` with
// `ce_label_shadow` under it one line right and down — the label tone over
// the dark tone on a dark Face, and past the label threshold Cool Edit's
// ENGRAVED pair, the dark tone over the hilight (2026-10-10,
// cool_edit_derive.h's label_ink / label_shadow).
void show_ce_label(cairo_t* cr, const text_shape::ShapedRun& run, double x,
                   double baseline);

// -- THE CUE'S TRIANGLE ---------------------------------------------------------

// COOL EDIT'S POINT-CUE MASK (METRICS §4.2, §4.3), THE CUES' AND THE
// PLAYHEAD HEAD'S ONE PAINTER: a staircase of five rows of one quantum u
// (cue_unit_px) from `top` — 9, 7, 5, 3, 1 quanta — each row's SHADOW one
// quantum right of it. DRAWN AS THE STAIRCASE'S DIAGONALS (architect
// 2026-10-09 ~12:40, the head's rule) AND CENTRED ON THE STEM (~14:35: "at
// every scale, the marker triangle must be fully centered on the stem"): the
// stem is ONE DEVICE PX at window column `col` (render.h's waveform_line_px,
// "on waveform → unscaled"), so the axis is its centre s + 0.5 and ONE
// ANTIALIASED TRIANGLE in `color` has the vertices (s + 0.5 − 4.5u, top),
// (s + 0.5 + 4.5u, top) and (s + 0.5, top + 5u) — its two sides mirror
// images about the stem at every gui_scale, u odd or even — over THE SAME
// TRIANGLE ONE QUANTUM RIGHT in `ce_cue_shadow`, painted first (Cool Edit's
// shadow one pixel right of every row is that offset). Its painted columns
// are [s − ⌊9u / 2⌋, s + ⌊(11u + 2) / 2⌋) — the vertices' outer edges,
// s + 0.5 − 4.5u and s + 0.5 + 5.5u with the shadow, taken to whole device
// columns (cue_triangle_half_w_px and cue_triangle_reach_right_px, render.h,
// the one owner every extent reads); the caller's clip cuts it. (Aliased
// rows of cells stood until the rule reached the program, 2026-10-09; the
// apex on the u-wide cell's centre, col + u / 2, until ~14:35.)
// `shadow` is the shadow's color: `ce_cue_shadow` for a live cue and the
// playhead's head, the faded `ce_off_cue_shadow` for a disabled cue
// (2026-10-10, cool_edit_derive.h's faded look). Where the target allows it
// the two fills are one cached coverage composited by cairo's own arithmetic
// (2026-10-13, the same pixels; CueTriangleCoverage, cool_edit_paint.cpp).
// One triangle alone (the playhead's head): a one-triangle CueTrianglePass.
void paint_ce_cue_triangle(cairo_t* cr, int col, int top, GuiColor color,
                           GuiColor shadow);

struct CueTriangleCoverage;   // cool_edit_paint.cpp, the stamp's coverage

// THE TRIANGLE PASS (2026-10-13, a cost cut that paints the same pixels): the
// cues' triangles, one consecutive run on one context (paint_cues, render.cpp,
// left to right over every label), share ONE PREPARED STAMP TARGET instead of
// each taking its own. begin() takes direct_pixel_target once — the surface
// flushed, the clip's rectangle list copied, the translation read — and looks
// the coverage up once at the quantum read once; triangle() stamps as
// paint_ce_cue_triangle's road does and GROWS ONE DIRTY BOX; end() marks that
// box dirty once.
// THE CALLER'S CONTRACT: between begin() and end() nothing changes the
// context's clip, matrix, operator or group (each cairo road below saves and
// restores its own), so the one target holds the clip list and translation
// each triangle's own direct_pixel_target would have answered; and nothing
// else asks the coverage cache, whose one entry the pass holds (the quantum
// and the rows do not change within a paint).
// THE IDENTITY RESTS ON: the same coverage bytes, the same cue_lerp8x4, the
// same stamp order — the shadow then the color, triangles in call order — the
// same clip list, the same cue_fill_pixel words of the same colors, and THE
// HAND-OVER where a cairo fill falls inside the run (a box the clip cuts, the
// split triangle, a refused target taking every triangle to cairo): before
// the fill the stamped box is marked dirty and emptied, after it the surface
// is flushed before the next stamp. The direct writes and cairo's fills land
// in the one image buffer in the pass's own call order, each handed over
// before the other road reads it — the per-triangle mark and flush they had,
// at the only seams where the other road runs.
class CueTrianglePass {
public:
    void begin(cairo_t* cr);
    // The ordinary triangle: the stamp where the target was taken and the
    // box is uncut (cue_triangle_box_uncut), else cairo's two fills,
    // antialias DEFAULT.
    void triangle(int col, int top, GuiColor color, GuiColor shadow);
    // THE SPLIT TRIANGLE of the `h` view's changed pair (architect
    // 2026-10-10 / 2026-10-11; render_history_diff_flags through paint_cues):
    // the same triangle and shadow, its axis at window x `axis` — a stem's
    // centre col + 0.5, or on a nudged pair the midpoint between its two
    // stems' centres, which may fall on a column boundary — SPLIT ON THE
    // VERTICAL THROUGH ITS APEX (the stem's centre rule above): the half left
    // of the axis in `left`, the half right of it in `right`, antialiased,
    // the axis column's coverage shared. Always cairo's road (its group's
    // arithmetic is not the stamp's), inside the hand-over.
    void split(double axis, int top, GuiColor left, GuiColor right,
               GuiColor shadow);
    void end();

private:
    void fallback_begin();
    void fallback_end();
    cairo_t*                   cr_         = nullptr;
    DirectPixelTarget          target_{};
    bool                       direct_     = false;
    const CueTriangleCoverage* cov_        = nullptr;
    int                        u_          = 0;
    GuiRect                    dirty_{};
    bool                       dirty_any_  = false;
    bool                       flush_owed_ = false;
};
