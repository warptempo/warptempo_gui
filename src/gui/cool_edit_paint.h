#pragma once

#include "cool_edit_derive.h"   // the case ramp's Default bytes, the HLS rule
#include "materials.h"   // the fortepiano chrome's inks (kWordInk)
#include "render.h"   // GuiRect, GuiColor, palette(), the program's accessors

#include <array>
#include <cairo/cairo.h>
#include <cstdint>

// THE PROGRAM'S PAINTERS — COOL EDIT PRO 2.1'S PANEL (architect 2026-10-09,
// "chrome means chrome, the program is Cool Edit"; program_spec.h's head):
// the button case in its faces, the toolbar band's lines and panes and
// grooves, the tab strip (2026-10-18), the dock bar, row 8's bars
// (2026-10-21), the dark time field, the shadowed panel label and the cue's
// triangle — THE SAME UNDER EVERY
// CHROME (the canvas column's lanes — the view bar, the ruler, the cues —
// are painted by render_trim_flags, paint_ruler_row and the flag pass out of
// these and the constants below). The lengths
// are program_spec.h's (render.h's program accessors, each a rounded part);
// the measurements are tmp/research/cool_edit/METRICS.md's, one capture px a
// W, and the approved mock is tmp/mocks/cool_edit/mock_CE_S3_GRID_4.png
// (its generator mock_ce.py the construction these painters follow).
//
// THE COLORS ARE TWO KINDS. THE PANEL'S are the palette's: the `face` (the
// brass's mean since 2026-10-21) and the tones Cool Edit derives from it
// (cool_edit_derive.h, the COOL EDIT BLOCK of palette()) — the panel
// label's swapping pair among them — and SINCE 2026-10-21 EVERY SURFACE TONE
// IS A MATERIAL (architect, the fortepiano chrome; materials.h): the
// painters below hand their tones to paint_cell_rect and paint_relief_frame,
// whose surface chokepoint lays each on the caller's material scope — the
// Face as the cover as drawn, every other tone the material's flat stroke —
// the band, the tab strip's ground, the dock bar and row 8 on the walnut,
// the tabs and the canvas column's lanes on the brass; THE BUTTON CASE is
// the ebony's (its ramp the cover under Cool Edit's case span, its lines
// flat, materials::paint_case_ramp and case_lines). THE CONSTANTS below are
// what Cool Edit draws whatever its preset — the case's black outline and
// the down face's outer black, the view bar's field — and the fortepiano
// chrome's own inks (materials.h's INKS: the one word ink kWordInk, the
// flags' near-white, the time fields' black ground), flat, never a material.
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
// capture px) and the down face's black / highlight ring; the pane's two-line
// edge, the groove's dark column its outer bottom-right (Cool Edit's one mid
// pixel atop the groove is that ring's corner at one capture px); the dock
// bar's light / mid ring; the time field's ring; the
// view bar's span (render_trim_flags); each tab's top-right block and the
// lit tab's two feet (paint_ce_tab_strip, whose declaration says which way
// each foot runs). A two-line staircase that is no ring
// — the cue's and the playhead head's
// 9-7-5-3-1 rows — is drawn as its diagonal (their painters say how). A
// one-tone line stays square: the case's black outline, the down face's
// inner shadow line, the band's edge columns, the lit tab's drop shadow; row
// 8's bars' areas are sunken rings, every corner mitred (2026-10-21,
// paint_row8_joins, paint_handler.cpp). The dots
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
// device rows are [nearbyint(i·g/20), nearbyint((i+1)·g/20)) of the seat's
// g, so the rows tile the seat exactly at every scale, each stop a band of
// the ebony cover under its luma ratio (materials.h's CASE, 2026-10-21; a
// flat band of Cool Edit's per-row case tones until then). THE HIGHLIGHT
// (materials::case_lines) on the top row and left column; THE SHADOW on the
// right column and the bottom row, a ramp from its first to its last tone
// across the glyph seat, its first tone also the mitre's dark half at the
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
// and F8F8F8 light, measured on a light grey case, read as a raised white
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
// THE THREE ARE CASE-RELATIVE ON THE EBONY (architect 2026-10-21, "r1", his
// pick off three 1:1 mocks: the constant silhouette read as a light grey
// blob on the dark case): the bytes above are the MEASUREMENT, on Cool Edit's
// Default case, and what the product paints is their RATIO taken onto the
// live case — THE SILHOUETTE's HLS lightness the live case ramp's mean
// lightness times kCeDisabledShadowRatio (808080's L over the Default ramp's
// mean, 0.502 / 0.809), THE RIM's the silhouette's times kCeDisabledInsetRatio
// (505050's L over 808080's, 0.625), both on the ebony Face's hue and
// saturation; THE LIGHT COPY STAYS F8F8F8, Cool Edit's. Recomputed at every
// bake (materials::disabled_glyph_tones; about 1F1615 and 130D0C on the
// shipped ebony). The constant 808080 / 505050 on a dark case retired the
// same day.
inline constexpr uint32_t kCeDisabledShadow = 0x808080;
inline constexpr uint32_t kCeDisabledLight  = 0xF8F8F8;
inline constexpr uint32_t kCeDisabledInset  = 0x505050;
// The Default case ramp's mean HLS lightness, over its twenty measured rows.
constexpr double ce_default_case_ramp_mean_l() {
    double sum = 0.0;
    for (const cool_edit_derive::CaseTone& t : cool_edit_derive::kCaseRamp)
        sum += cool_edit_derive::hls_of(t.default_byte).l;
    return sum / static_cast<double>(cool_edit_derive::kCaseRamp.size());
}
inline constexpr double kCeDisabledShadowRatio =
    cool_edit_derive::hls_of(kCeDisabledShadow).l / ce_default_case_ramp_mean_l();
inline constexpr double kCeDisabledInsetRatio =
    cool_edit_derive::hls_of(kCeDisabledInset).l /
    cool_edit_derive::hls_of(kCeDisabledShadow).l;
static_assert(ce_default_case_ramp_mean_l() > 0.804 &&
                  ce_default_case_ramp_mean_l() < 0.814 &&
                  kCeDisabledShadowRatio > 0.615 && kCeDisabledShadowRatio < 0.625 &&
                  kCeDisabledInsetRatio > 0.624 && kCeDisabledInsetRatio < 0.626,
              "the disabled emboss's ratios: 808080 (L 0.502) over the "
              "Default case ramp's mean L 0.809, 0.620; 505050 over 808080, "
              "0.625");

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
// TICKS AND DIGITS ARE THE CHROME'S ONE INK, kWordInk (materials.h's INKS,
// architect 2026-10-21, "they should be lower contrast"; the near-white
// earlier that day, Cool Edit's E0E0E0 before).
inline constexpr uint32_t kCeViewBarField   = 0x000000;
inline constexpr uint32_t kCeRulerTick      = kWordInk;
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
//              top row and left column, the case's last shadow tone inside
//              it, the ramp on the seat one line further in, the case's
//              highlight on the bottom row and right column, mitred against
//              the black (2026-10-21; Cool Edit's 808080 and F8F8F8 until
//              then); the glyph at (+2, +2).
//   DISABLED — the face its state gives, the glyph EMBOSSED (the caller's
//              icons::draw_cased_disabled — Cool Edit's own disabled art,
//              its silhouette over a kCeDisabledLight copy of the drawing
//              one icon unit right and down, with the product's rim inside
//              the silhouette's top-left edges, the silhouette and rim the
//              case's tones by kCeDisabledShadowRatio / kCeDisabledInsetRatio
//              since 2026-10-21, icons.h's THE DISABLED GLYPH).
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
// ROW 8'S BAR'S WIDTH (architect 2026-10-21; program_spec.h's row-8 bar
// fields, where the frame of record and the placement stand): FOUR COLUMNS —
// a light column, the bar's face, a mid and a dark column. The bars and
// their joins — each two-tone corner they leave against the dock bar's and
// the border's lines mitred, the area between two bars a sunken two-line
// ring — are paint_row8_joins' (paint_handler.cpp), which owns the rule.
inline int ce_row8_bar_w_px() {
    return 3 * program_line_px() + scaled_px(kProgramSpec.row8_bar_face_px);
}

// -- THE TIME FIELD AND THE LABEL -------------------------------------------------

// THE DARK TIME FIELD on `field` (METRICS §5.4): the field-dark line on its
// top row and left column, BLACK inside (materials.h's kTimeFieldGround,
// 2026-10-21; Cool Edit's `mid` tone until then), the field-light line on
// its bottom row and right column, the ring mitred, its two lines the
// caller's scope's tones (row 8's the walnut's, the render player's flat).
// Its digits are the caller's, in the fortepiano chrome's one ink
// (kTimeFieldDigits = kWordInk since 2026-10-21; the amber F0B030 that
// morning, Cool Edit's never-swapping light text, `ce_field_text`, before).
void paint_ce_time_field(cairo_t* cr, const GuiRect& field);
// A PANEL LABEL (METRICS §6) — row 8's state line, its one reader: `run`
// in THE CHROME'S ONE INK, kWordInk (architect 2026-10-21: the GitHub
// message "in plain white on the same wooden surface — inconsistent"; the
// palette's `ce_label` until then) with `ce_label_shadow` under it one line
// right and down — the dark tone on a dark Face, past the label threshold
// Cool Edit's hilight (2026-10-10, cool_edit_derive.h's label_shadow).
void show_ce_label(cairo_t* cr, const text_shape::ShapedRun& run, double x,
                   double baseline);

// -- THE TAB STRIP (architect 2026-10-18) ---------------------------------------

// COOL EDIT'S ORGANIZER TAB STRIP, top lane 4 (render.h's tab strip block;
// program_spec.h's tab fields, where the law — his Wine capture
// tmp/Screenshot_2026-10-18_16-07-45.png and METRICS §2.3 — and the mocks of
// record, tmp/tabs_mock/tabs_v11_A.png / _B.png and their generator
// make11.py, the construction this painter follows, are named). Two tabs,
// "Tab A" and "Tab B", the lit one the active tab. In W, from the lane's top
// and its first column, THE PAGE EDGE (the program frame's inner
// neighbour):
//   THE GROUND — THE WALNUT AS DRAWN over the lane's first 20 W (the dock's
//     recess until 2026-10-21; materials.h's WOOD: the plank, never the
//     band's void), then THE PAGE'S TOP LIGHT LINE, one line in the hilight
//     tone, the walnut's flat stroke; THE TABS ARE THE BRASS; under the
//     inactive tabs it is THE TAB LINE (ce_tab_line), ONE RUN from one W
//     before the first inactive footprint to one W after the last, across
//     the gaps (with the active tab last, the line under and past its shadow
//     is the plain hilight — the lone tab-line cell Cool Edit's own rule
//     leaves under the shadow, x 87 on the capture, ruled an artifact).
//   A FOOTPRINT per tab, the first one W in from the page edge, each its
//     label's CELL — its shaped advance, width_px, never its ink — plus
//     14 W: its two edge columns and `tab_pad_px` either side of the cell,
//     so the cell is centered in it (architect 2026-10-18 ~20:20: a Windows
//     tab control pads its text cell; Cool Edit's 6 / 5 on the ink is MS
//     Sans Serif's trailing blank column, a bitmap face's width, never
//     copied) — the footprints
//     `tab_gap_px` apart (every length its own scaled_px part,
//     program_spec.h's tab fields, architect 2026-10-18 ~19:55).
//   AN INACTIVE TAB on its footprint from row 4 to the recess's last row
//     (16 W): its top row and left column in ce_tab_light, its right column
//     in ce_tab_dark, the body inside them the brass under Cool Edit's
//     measured 15 stops as a luma ratio (Default's kTabBodies row, one stop
//     per W row, never resampled — kCaseRamp's rule; materials.h's METAL,
//     materials::paint_tab_body).
//   THE ACTIVE TAB one W wider each side and from row 2 (18 W), its face
//     the panel's Face running down THROUGH THE PAGE LINE (the tab merges
//     into the page), its left column in the hilight running through the
//     page line too, its top row the hilight, its right column the mid tone
//     and beside it THE SHADOW on the right growth's columns, the page-dark
//     tone (ce_page_dark), Cool Edit's one drop shadow — SQUARE, NO MITRE,
//     from one line under the
//     tab's top (the strip's ground in the cell above it) to the recess's
//     last row,
//     the page line running under it.
//     So the gap beside it closes to one W, and the first tab active
//     stands flush on the page edge.
//   THE LABELS in the body face, THE INK (kWordInk) over the words' drop
//     shadow at
//     (+d, +d) device px, lit or unlit (materials.h's INKS, 2026-10-21; no
//     shadow, the program text and Cool Edit's ink until then), no hover
//     face (Cool Edit's
//     F6F6F6 hover does not apply, program_spec.h's case_hot_face), the cap
//     band centred by redesign_baseline in the tab's body under its top
//     light row — 15 W inactive, 17 W active, so the active label lands one
//     W higher by the rule alone — THE RUN'S ORIGIN AT THE CELL'S LEFT
//     EDGE, `tab_pad_px` past the footprint's left column, lit or unlit,
//     never shifted by the first glyph's bearing (the ink lands where the
//     face's bearings put it): the lit frame grows ROUND THE FIXED LABEL,
//     so its left pad reads one W more than its right, the raised frame's
//     two-column right edge — Windows' own construction, Cool Edit's 7 / 5
//     (2026-10-18 ~20:20). A DEAD TAB — the render player or the picker standing —
//     wears Windows' DSS_DISABLED emboss on its label (show_embossed_run,
//     the disabled-word rule, its light copy the hilight), its body and
//     edges unchanged (architect 2026-10-18 ~19:05). A TAB HAS NO PRESSED
//     FACE (Cool Edit's tabs show none; redesign_button_is_tab).
// THE CORNERS (the diagonal rule, this file's head): each tab's TOP-RIGHT
// block — its light top row against its right column (the mid tone active,
// ce_tab_dark inactive) — the standard mitre, one line square (never spread
// over the shadow column, which is no part of a mitre). THE ACTIVE TAB'S TWO
// FEET on the page-line row are ONE RING'S TWO BOTTOM CORNERS, each the
// standard mitre, antialiased: the LEFT FOOT the hilight column against the
// tab line arriving from the left, a bottom-right corner — the diagonal from
// the cell's top left to its bottom right, the column's tone above it, the
// line's below — and none when nothing arrives (the first tab active, the
// frame line beside it); THE RIGHT FOOT ITS MIRROR, the mid column against
// the page line running on to the right, a bottom-left corner — the
// diagonal from the cell's bottom left (outer) to its top right (inner), the
// mid tone above-left of it, the line's tone below-right: the tone the page
// line shows in the cell right of the foot, under the shadow (the hilight
// there by the ground's rule above). COOL EDIT DRAWS NO CONNECTING PIXEL AT
// THAT ONE CORNER (on the capture the face stands under the mid column, x
// 86 on row 111, and the line begins under the shadow, x 87), and THE
// PRODUCT IMAGINES IT (architect 2026-10-18 ~19:55: "imagine that there's a
// pixel connecting it, just like every other corner … draw a mitred corner
// there, just as we would normally with the same angle, just as the bottom
// left has, but in mirrored form, because it's on the opposite side").
// ONE GEOMETRY FOR PAINT AND HIT: ce_tab_strip_layout answers each tab's
// painted box (the press target the roster publishes, paint_tab_strip in
// paint_handler.cpp) and its label's origin; the painter takes it whole.
inline constexpr int kCeTabCount = 2;
// The lit tab's growth each side, one W, its own rounded part (program_spec.h's
// tab_growth_px): the left one widens its face, the right one is its shadow.
inline int ce_tab_growth_px() {
    return scaled_px(kProgramSpec.tab_growth_px);
}
struct CeTabStripLayout {
    // Each tab's painted box: the lit tab its grown box through the page
    // line, an unlit tab its footprint from its top row to the recess's last.
    std::array<GuiRect, kCeTabCount> tab{};
    // Each label's pen origin (its cell's left edge, `tab_pad_px` past the
    // footprint's left column, lit or unlit) and baseline.
    std::array<double, kCeTabCount> label_x{};
    std::array<double, kCeTabCount> baseline{};
};
// The layout on the lane `lane` (top_tab_strip_area) for the shaped labels
// `labels` in the body face `font`, tab `active` lit.
CeTabStripLayout ce_tab_strip_layout(
    const GuiRect& lane, const GuiFont& font,
    const std::array<const text_shape::ShapedRun*, kCeTabCount>& labels,
    int active);
// The strip on `lane` from `layout`, tab `active` lit and each tab's label
// live or embossed by `enabled`.
void paint_ce_tab_strip(
    cairo_t* cr, const GuiRect& lane, const CeTabStripLayout& layout,
    const std::array<const text_shape::ShapedRun*, kCeTabCount>& labels,
    int active, const std::array<bool, kCeTabCount>& enabled);

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
