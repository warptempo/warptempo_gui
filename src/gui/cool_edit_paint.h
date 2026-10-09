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
// BLOCK of palette()), so a pick of Face moves the whole panel. THE CASE'S
// are CONSTANTS below — Cool Edit's default scheme's bytes as measured,
// never derived ("the buttons did not change color with the preset",
// architect 2026-10-09; the fit that was tried is cool_edit_derive.h's head)
// — and so is the time field's digit ink.
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
// the ramp, a texture and not a tone — the ramp is drawn smooth); the
// LIGHT-FACE TEXT SWAP (cool_edit_derive.h's head).

// -- THE CASE'S CONSTANTS (METRICS §1.3–1.5; 0xRRGGBB, sRGB) -----------------

// THE FACE RAMP — the 20 interior rows' median bytes over ten buttons
// (METRICS §1.4 (b)), ONE STOP PER W ROW OF THE 20-W GLYPH SEAT, never
// resampled (architect 2026-10-09 ~10:25, the seat Cool Edit's own 20):
// stop i fills the seat's W row i, whose device rows are
// [nearbyint(i·g/20), nearbyint((i+1)·g/20)) of the seat's g, so the rows
// tile the seat exactly at every scale, each stop a flat band of its bytes.
inline constexpr std::array<uint32_t, 20> kCeFaceRamp = {
    0xFEFEFE, 0xF9FEFE, 0xF9FAFC, 0xF2F3F6, 0xEBECEE,
    0xE4E5E7, 0xDEDFE1, 0xDDDEE0, 0xD7D8DA, 0xD1D1D3,
    0xCACACB, 0xC4C5C6, 0xC2C3C4, 0xBABBBD, 0xB5B6B7,
    0xAEAFB0, 0xA9A9AB, 0xA7A7A8, 0x9FA0A1, 0x989899,
};
static_assert(kCeFaceRamp.size() ==
              static_cast<std::size_t>(kProgramSpec.glyph_px));
// THE HIGHLIGHT — the case's top row and left column, Cool Edit's FEFEFF …
// FDFEFF … FCFEFF authored as its one middle tone (the brief's ruling; the
// three differ by one level).
inline constexpr uint32_t kCeCaseHighlight = 0xFDFEFF;
// THE SHADOW — the right column and the bottom row, a ramp from its first
// to its last measured tone (5D5E5E … 4C4D4D) across the glyph seat; its
// first tone also the mitre's dark half at the top-right and bottom-left.
inline constexpr uint32_t kCeCaseShadowFirst = 0x5D5E5E;
inline constexpr uint32_t kCeCaseShadowLast  = 0x4C4D4D;
// THE SHADOW'S OWN CORNER (bottom-right, inside the black line). Where the
// highlight meets the shadow (the top-right and bottom-left) Cool Edit's
// measured ADAEAF is the two tones' half-and-half — the mitre, drawn as the
// diagonal (the head's rule), so no constant stands for it.
inline constexpr uint32_t kCeCaseCornerShadow = 0x4B4C4C;
// THE BLACK LINE on the right and bottom.
inline constexpr uint32_t kCeCaseOuter = 0x000000;
// THE PRESSED AND CHECKED RINGS (METRICS §1.5, constant over all five
// presets): black on the top and left outside, 808080 inside it, F8F8F8 on
// the bottom and right.
inline constexpr uint32_t kCeRingOuter = 0x000000;
inline constexpr uint32_t kCeRingMid   = 0x808080;
inline constexpr uint32_t kCeRingLight = 0xF8F8F8;
// THE GLYPH INK ON THE CASE — a BOUND icon drawing's color there (render.h's
// GuiSurface::ProgramCase; Breeze's monochrome glyphs): black, the case's
// face being Cool Edit's light ramp under every palette.
inline constexpr uint32_t kCeCaseInk = 0x000000;
// THE TIME FIELD'S DIGITS (METRICS §5.4, ≈EFF0F0 on the default scheme).
inline constexpr uint32_t kCeFieldText = 0xEFF0F0;

// -- THE CANVAS COLUMN'S CONSTANTS (METRICS §3, §4; 2026-10-09) ---------------
// The bytes Cool Edit draws whatever its preset: THE VIEW BAR'S FIELD, black
// (§3); THE RULER'S TICKS AND DIGITS, E0E0E0 (§4.4, face-independent in all
// five presets), the digits' (+1, +1) SHADOW black (§4.4 and §6: "≈ black,
// observed down to 0D0708", darker than the panel labels' dark tone); and
// the view bar's playhead dots' other color, black (§3: the playhead's
// color and 000000 alternating). THE PLAYHEAD'S OWN COLOR — the head, its
// dots in the canvas and in the view bar — is the palette's `playhead_stem`
// since 2026-10-09 ~11:50, Cool Edit's Curs FFFF00 its default in every
// palette (palette_file.h's role table), as the cues' two colors are the
// palette's `cue` and `range`; the triangles' shadow is the derived
// `ce_cue_shadow`.
inline constexpr uint32_t kCeViewBarField   = 0x000000;
inline constexpr uint32_t kCeRulerTick      = 0xE0E0E0;
inline constexpr uint32_t kCeRulerShadow    = 0x000000;
inline constexpr uint32_t kCeViewBarDot     = 0x000000;

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
//   DISABLED — the face its state gives, the glyph saturated (the caller's
//              icons::draw_cased_disabled — Cool Edit's per-glyph disabled
//              art has no counterpart for the product's icon sets).
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
// in kCeFieldText.
void paint_ce_time_field(cairo_t* cr, const GuiRect& field);
// A PANEL LABEL (METRICS §6): `run` in the label tone with the dark tone
// under it one line right and down, Cool Edit's shadow on a dark face.
void show_ce_label(cairo_t* cr, const text_shape::ShapedRun& run, double x,
                   double baseline);

// -- THE CUE'S TRIANGLE ---------------------------------------------------------

// COOL EDIT'S POINT-CUE MASK (METRICS §4.2, §4.3), THE CUES' AND THE
// PLAYHEAD HEAD'S ONE PAINTER: a staircase of five rows of one quantum u
// (cue_unit_px) from `top`, row r spanning the apex column `col` (window x,
// its own u wide) widened by (4 − r) quanta either side — 9, 7, 5, 3, 1
// quanta — each row's SHADOW one quantum right of it. DRAWN AS THE
// STAIRCASE'S DIAGONALS (architect 2026-10-09 ~12:40, the head's rule): ONE
// ANTIALIASED TRIANGLE in `color` with the vertices (col − 4u, top),
// (col + 5u, top) and (col + u / 2, top + 5u) — the top row's two outer
// ends and the apex cell's bottom centre — over THE SAME TRIANGLE ONE
// QUANTUM RIGHT in `ce_cue_shadow`, painted first (Cool Edit's shadow one
// pixel right of every row is that offset). Its painted columns stay
// [col − 4u, col + 6u) over its five rows (cue_triangle_half_w_px and
// cue_triangle_reach_right_px, render.h); the caller's clip cuts it. (Aliased
// rows of cells stood until the rule reached the program, 2026-10-09.)
void paint_ce_cue_triangle(cairo_t* cr, int col, int top, GuiColor color);
