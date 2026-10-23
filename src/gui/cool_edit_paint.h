#pragma once

#include "cool_edit_derive.h"   // the case ramp's Default bytes, the HLS rule
#include "materials.h"   // the fortepiano chrome's inks (kBoneInk)
#include "render.h"   // GuiRect, GuiColor, palette(), the program's accessors

#include <array>
#include <cairo/cairo.h>
#include <cstdint>

// THE PROGRAM'S PAINTERS — COOL EDIT PRO 2.1'S PANEL (architect 2026-10-09,
// "chrome means chrome, the program is Cool Edit"; program_spec.h's head):
// the button case in its faces, the dark time field, the shadowed panel
// label and the cue's triangle (the band's lines and panes, the tab strip,
// the dock bar and row 8's bars stood here 2026-10-09 to 2026-10-22, the
// simple layout's strokes removed) — THE SAME UNDER EVERY
// CHROME (the canvas column's lanes — the view bar, the ruler, the cues —
// are painted by render_trim_flags, paint_ruler_row and the flag pass out of
// these and the constants below). The lengths
// are program_spec.h's (render.h's program accessors, each a rounded part);
// the measurements are tmp/research/cool_edit/METRICS.md's, one capture px a
// W, and the approved mock is tmp/mocks/cool_edit/mock_CE_S3_GRID_4.png
// (its generator mock_ce.py the construction these painters follow).
//
// THE COLORS ARE TWO KINDS. THE PANEL'S are the palette's: the `face` (the
// wood's mean since 2026-10-22, the brass's 2026-10-21 to 2026-10-22) and
// the tones Cool Edit derives from it
// (cool_edit_derive.h, the COOL EDIT BLOCK of palette()) — the panel
// label's swapping pair among them — and SINCE 2026-10-21 EVERY SURFACE TONE
// IS A MATERIAL (architect, the fortepiano chrome; materials.h): the
// painters below hand their tones to paint_cell_rect and paint_relief_frame,
// whose surface chokepoint lays each on the caller's material scope — the
// Face as the cover as drawn, every other tone the material's flat stroke —
// the band, the canvas column's margins and the program ring, row 8 and its
// clock's ring on the walnut, the marker lane
// between the ring's columns, the ruler's ground and the two foot lines on
// the brass, the wood's ring round the column (2026-10-22, materials.h's
// REGIONS); THE BUTTON CASE is
// the ebony's (its ramp the cover under Cool Edit's case span, its lines
// flat, materials::paint_case_ramp and case_lines). THE CONSTANTS below are
// what Cool Edit draws whatever its preset — the case's black outline and
// the down face's outer black, the view bar's field — and the fortepiano
// chrome's own inks (materials.h's INKS: the one bone ink kBoneInk on the
// words and the flags, the time fields' own digits on their black ground),
// flat, never a material.
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
// capture px) and the down face's black / highlight ring; the time field's
// ring (row 8's clock's the program ring's tones); the view bar's span
// (render_trim_flags); the program ring. A two-line staircase that is no ring
// — the cue's and the playhead head's
// 9-7-5-3-1 rows — is drawn as its diagonal (their painters say how). A
// one-tone line stays square: the case's black outline, the down face's
// inner shadow line. The dots
// are cells (a dot is not a line). (Every
// line square-joined as the capture's pixels show stood the morning of
// 2026-10-09; git history.)
// (NOT DRAWN 2026-10-09..10-21: the face's brushed grain, METRICS §1.4's ±5
// 2-px texture over the ramp; the ebony's own grain rides the ramp since,
// materials.h.)

// -- THE CASE'S CONSTANTS (METRICS §1.3–1.5; 0xRRGGBB, sRGB) -----------------

// THE FACE RAMP (materials::paint_case_ramp, the ebony's): ONE STOP PER W
// ROW OF THE 20-W GLYPH SEAT, never resampled (architect 2026-10-09 ~10:25,
// the seat Cool Edit's own 20): stop i fills the seat's W row i, whose
// device rows are [nearbyint(i·h/20), nearbyint((i+1)·h/20)) of the seat's
// height h, so the rows tile the seat exactly at every scale, each stop a
// band of the ebony cover under its luma ratio (materials.h's CASE,
// 2026-10-21; a flat band of Cool Edit's per-row case tones until then). ON
// A SEAT THAT IS NO SQUARE — a word button's or a key's (2026-10-22,
// paint_ce_case) — the twenty stops tile its own height the same way and
// run across its whole width. THE HIGHLIGHT
// (materials::case_lines) on the top row and left column; THE SHADOW on the
// right column and the bottom row, a ramp from its first to its last tone
// across the glyph seat — down its height on the column, along its width on
// the row — its first tone also the mitre's dark half at the
// top-right and bottom-left; THE SHADOW'S OWN CORNER at the bottom-right
// inside the black line — each a flat stroke off the ebony's Face. Where the highlight meets the shadow Cool Edit's measured
// ADAEAF is the two tones' half-and-half — the mitre, drawn as the diagonal
// (the head's rule), so no tone stands for it.
// THE BLACK LINE on the right and bottom, 000000 on every captured Face.
inline constexpr uint32_t kCeCaseOuter = 0x000000;
// THE PRESSED AND CHECKED RINGS' OUTER BLACK (METRICS §1.5) on the top and
// left. The rest of the rings is the ebony's since 2026-10-21 (architect,
// "the top and left should be darker than the case"): the case's highlight
// on the bottom and right of the outer ring, its last shadow tone on the
// top and left inside it (paint_ce_case; Cool Edit's constant 808080 inner
// and F8F8F8 light, measured on a light gray case, read as a raised white
// frame on the ebony, closed_questions.md).
inline constexpr uint32_t kCeRingOuter = 0x000000;
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
// THE TONES ARE CASE-RELATIVE ON THE EBONY (architect 2026-10-21, "r1", his
// pick off three 1:1 mocks: the constant silhouette read as a light gray
// blob on the dark case), AND SINCE HIS PICK v2 THE SAME NIGHT (~23:05, on
// four renders of the real painter composited into his tablet capture) THEY
// ARE HIS OWN NUMBERS, NOT COOL EDIT'S RATIOS: THE SILHOUETTE's HLS lightness
// is the live case ramp's mean lightness times kCeDisabledShadowRatio, 0.84
// — a measured constant of his own, his pick 2A1E1C on the shipped ebony
// (L .137 over the ramp mean .163) — on the ebony Face's hue and saturation,
// recomputed at every bake (materials::disabled_glyph_tones); THE RIM IS
// BLACK, kCeDisabledInset, a constant (on a dark case a tone darker than the
// silhouette has nowhere to go but black); THE LIGHT COPY STAYS F8F8F8, Cool
// Edit's. THE RULE IT DEPARTS FROM, recorded: Cool Edit's own ratio, 808080's
// L over the Default case ramp's mean (0.502 / 0.809 = 0.620), with the rim
// at the silhouette's times 505050's L over 808080's (0.625) — the glass
// round's rule, which put the silhouette at 1F1615 and the rim at 130D0C on
// the ebony, the rim invisible ("I don't see it so it may be gone"). The
// constant 808080 / 505050 on a dark case retired earlier the same day.
// kCeDisabledShadow stays Cool Edit's measured byte, the record and the
// state's pre-bake default; the static_assert pins the departure.
inline constexpr uint32_t kCeDisabledShadow = 0x808080;
inline constexpr uint32_t kCeDisabledLight  = 0xF8F8F8;
// THE RIM (architect 2026-10-21, his pick v2): black, constant. 505050, his
// pick off the 2026-10-15 series on Cool Edit's light case, until then.
inline constexpr uint32_t kCeDisabledInset  = 0x000000;
// The Default case ramp's mean HLS lightness, over its twenty measured rows —
// Cool Edit's own ratio's denominator, kept for the assert's record.
constexpr double ce_default_case_ramp_mean_l() {
    double sum = 0.0;
    for (const cool_edit_derive::CaseTone& t : cool_edit_derive::kCaseRamp)
        sum += cool_edit_derive::hls_of(t.default_byte).l;
    return sum / static_cast<double>(cool_edit_derive::kCaseRamp.size());
}
// THE SILHOUETTE'S RATIO TO THE LIVE CASE RAMP'S MEAN LIGHTNESS — HIS PICK
// (architect 2026-10-21 ~23:05): 2A1E1C's L .137 over the shipped ebony
// ramp's mean .163. The lifted silhouettes 3B2A27 … 855E58 and the
// ramp-mean 332523 were mocked and not taken (closed_questions.md).
inline constexpr double kCeDisabledShadowRatio = 0.84;
static_assert(ce_default_case_ramp_mean_l() > 0.804 &&
                  ce_default_case_ramp_mean_l() < 0.814 &&
                  cool_edit_derive::hls_of(kCeDisabledShadow).l /
                          ce_default_case_ramp_mean_l() > 0.615 &&
                  cool_edit_derive::hls_of(kCeDisabledShadow).l /
                          ce_default_case_ramp_mean_l() < 0.625 &&
                  kCeDisabledShadowRatio == 0.84 &&
                  kCeDisabledInset == 0x000000,
              "the disabled emboss: his pick 0.84 of the case ramp's mean L "
              "(2A1E1C on the shipped ebony) departing from Cool Edit's "
              "0.620 (808080's L 0.502 over the Default ramp's mean 0.809), "
              "the rim black 000000 (Cool Edit-relative 0.625 retired)");

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
// undone: following Face here would depart from Cool Edit. ON THE BRASS THE
// TICKS AND DIGITS ARE THE CHROME'S ONE INK, the bone kBoneInk (materials.h's
// INKS, architect 2026-10-22; E6D5A8 2026-10-21 to 2026-10-22, the
// near-white before that, Cool Edit's E0E0E0 before).
inline constexpr uint32_t kCeViewBarField   = 0x000000;
inline constexpr uint32_t kCeRulerTick      = kBoneInk;
inline constexpr uint32_t kCeRulerShadow    = 0x000000;

// -- THE CASE ------------------------------------------------------------------

// ONE BUTTON CASE on `r`, in its face: the band's, row 8's and the render
// player's (icon_case_w_px square) and, SINCE 2026-10-22, THE WORD BUTTONS
// AND THE ON-SCREEN KEYBOARD'S KEYS (architect: "buttons in ebony — yes";
// the modal family's controls row and paint_onscreen_keyboard, which the
// case wears on their own boxes, w x h). ONE CONSTRUCTION FOR EVERY BOX: the
// glyph seat is the case less its lines on each axis, gw = r.w − 3 lines
// across and gh = r.h − 3 lines down (gw = gh = the square seat on the
// square cases, every expression there the square's), the lines' places
// below read off it:
//   REST     — the highlight on the top row and left column, the face ramp
//              on the glyph seat, the shadow ramp on the right column and
//              bottom row, the two mitred where they meet, the black line
//              on the right and bottom; the glyph (or a word, a cap) at
//              (+1, +1) lines.
//   DOWN     — PRESSED (a live press) or CHECKED (`lamp`), the same face
//              (Cool Edit draws them alike, the window group's checked cases
//              and the zoom button's press on his captures): black on the
//              top row and left column, the case's last shadow tone inside
//              it, the ramp on the seat one line further in, the case's
//              highlight on the bottom row and right column, mitred against
//              the black (2026-10-21; Cool Edit's 808080 and F8F8F8 until
//              then); the glyph at (+2, +2) — a word or a cap one line
//              right and down of its rest place, the answer below.
//   DISABLED — the face its state gives, the glyph EMBOSSED (the caller's
//              icons::draw_cased_disabled — Cool Edit's own disabled art,
//              its silhouette over a kCeDisabledLight copy of the drawing
//              one icon unit right and down, with the product's rim inside
//              the silhouette's top-left edges — the silhouette the case
//              ramp's mean lightness times kCeDisabledShadowRatio and the
//              rim black since 2026-10-21,
//              icons.h's THE DISABLED GLYPH). A word or a cap on a case
//              is never dead (a word button lives while its dialog
//              stands; a key while its keyboard does).
//   HOT      — none: Cool Edit's toolbar has no hover face (program_spec.h's
//              case_hot_face).
// Answers the glyph's shift in device px (one line when down).
int paint_ce_case(cairo_t* cr, const GuiRect& r, bool down);

// (THE TOOLBAR BAND'S GROUND, ITS PANES AND DARK COLUMNS, THE DOCK BAR AND
// ROW 8'S BARS — paint_ce_band_ground, paint_ce_pane,
// paint_ce_band_dark_column, paint_ce_dock_bar and ce_row8_bar_w_px,
// 2026-10-09 to 2026-10-22 — ARE REMOVED with the strokes they drew,
// architect 2026-10-22, the simple layout: the band and row 8 are the plain
// wood, program_spec.h's head.)

// -- THE TIME FIELD AND THE LABEL -------------------------------------------------

// THE DARK TIME FIELD on `field` (METRICS §5.4): the `dark` line on its
// top row and left column, BLACK inside (materials.h's kTimeFieldGround,
// 2026-10-21; Cool Edit's `mid` tone until then), the `light` line on its
// bottom row and right column, the ring mitred, the two the scope's
// material's: ROW 8'S CLOCK the program ring's `ce_dark` / `ce_hilight` on
// the wood (architect 2026-10-22, the simple layout — the clock's ring), the
// render player's two fields the field's own `ce_field_dark` /
// `ce_field_light`, flat.
// Its digits are the caller's, in the clocks' own E6D5A8 (kTimeFieldDigits,
// materials.h's INKS: the words' ink 2026-10-21, left on the clocks when the
// words took the bone 2026-10-22; the amber F0B030 that morning, Cool Edit's
// never-swapping light text, `ce_field_text`, before).
void paint_ce_time_field(cairo_t* cr, const GuiRect& field, GuiColor dark,
                         GuiColor light);
// A PANEL LABEL (METRICS §6) — row 8's state line, its one reader: `run`
// in THE CHROME'S ONE INK, the bone kBoneInk (2026-10-22; architect 2026-10-21: the GitHub
// message "in plain white on the same wooden surface — inconsistent"; the
// palette's `ce_label` until then) with `ce_label_shadow` under it one line
// right and down — the dark tone on a dark Face, past the label threshold
// Cool Edit's hilight (2026-10-10, cool_edit_derive.h's label_shadow).
void show_ce_label(cairo_t* cr, const text_shape::ShapedRun& run, double x,
                   double baseline);

// (THE TAB STRIP — paint_ce_tab_strip, ce_tab_strip_layout, kCeTabCount and
// ce_tab_growth_px, Cool Edit's Organizer tabs, 2026-10-18 — IS REMOVED with
// the strip, architect 2026-10-22, the simple layout.)

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
