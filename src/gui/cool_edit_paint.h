#pragma once

#include "cool_edit_derive.h"   // the case ramp's Default bytes, the HLS rule
#include "materials.h"   // the skin's inks and the key's constants
#include "render.h"   // GuiRect, GuiColor, palette(), the program's accessors

#include <array>
#include <cairo/cairo.h>
#include <cstdint>

// THE PROGRAM'S PAINTERS (architect 2026-10-09, "chrome means chrome, the
// program is Cool Edit"; program_spec.h's head) IN THE HI-FI PANEL SKIN
// (architect 2026-10-23; materials.h's head, the frames of record): THE KEY
// — every case of the band, row 8, the render player, the dialogs' word
// buttons and the on-screen keyboard — the time field, the panel label and
// the cue's triangle (the canvas column's lanes — the view bar, the ruler,
// the cues — are painted by render_trim_flags, paint_ruler_row and the flag
// pass out of these and the constants below). The lengths are
// program_spec.h's (render.h's program accessors, each a rounded part).
//
// THE COLORS: the skin's constants (materials.h: the key's face, edges and
// LED, the inks, the fields) and the two ring tones off the ground's mean
// (materials::ring_light / ring_dark, the palette's `ce_hilight` and
// `ce_dark`); every line a ring or a key draws is ONE DEVICE PX
// (hairline_px, render.h's line rule). (The ebony case with Cool Edit's
// ramp, its rings and its glyph, 2026-10-09 to 2026-10-23, is git history.)
// THE DIAGONAL RULE REACHES THE PROGRAM (architect 2026-10-09 ~12:40: "any
// jagged staircase of pixel lines, even if it's only two lines, we take as
// an intention of a diagonal"; paint_relief_frame's head, render.cpp):
// wherever a light tone meets a dark one at a ring's corner the corner block
// is split along the 45-degree diagonal — the time field's ring, the view
// bar's span, the program ring. A two-line staircase that is no ring — the
// cue's and the playhead head's 9-7-5-3-1 rows — is drawn as its diagonal
// (their painters say how). The dots are cells (a dot is not a line).

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
// NO BAND CASE WEARS A GLYPH since 2026-10-23 (the keys are blank, their
// legends printed under them), so these tones are the record of the
// emboss icons::draw_disabled still draws for a caller that asks — Cool
// Edit's measured bytes, the rim black (his pick on the ebony of
// 2026-10-21; the case-relative silhouette of that skin is git history).
inline constexpr uint32_t kCeDisabledShadow = 0x808080;
inline constexpr uint32_t kCeDisabledLight  = 0xF8F8F8;
inline constexpr uint32_t kCeDisabledInset  = 0x000000;

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
// undone: following Face here would depart from Cool Edit. IN THE HI-FI SKIN
// THE TICKS AND DIGITS ARE THE BONE kBoneInk over a ONE-DEVICE-PX black
// shadow at (+1, +1) on the flat dark kRulerGround (architect 2026-10-23,
// "the timestamps are good"; render.h's ruler block).
inline constexpr uint32_t kCeViewBarField   = 0x000000;
inline constexpr uint32_t kCeRulerTick      = kBoneInk;
inline constexpr uint32_t kCeRulerShadow    = 0x000000;

// -- THE KEY (architect 2026-10-23; materials.h's KEY constants) ---------------

// THE LED's STATE on a key: none drawn (the scrub's thumb, the on-screen
// keyboard's keys — a typing key carries no lamp), a dead lamp, or lit in
// THE ACCENT (materials::accent_color).
enum class GuiKeyLed { None, Unlit, Lit };

// THE LED'S RECT on the key `r` (the frame of record's 6 x 3 device px at
// 300 %, make_pivot4.key): 2 x 1 W (scaled_px), its left edge 2 W right of
// the key's left outline column and its bottom edge 2 W above the outline's
// bottom row — rows y1 − 9 .. y1 − 7 and columns x0 + 6 .. x0 + 11 at 300 %
// (x0, y1 the outline's left column and bottom row), the mock's own place.
// Empty on a key too small to hold it.
GuiRect key_led_rect(const GuiRect& r);

// ONE KEY on `r`, w x h (the band's, row 8's and the render player's
// icon_case_w_px square; a word button's or a keyboard key's own box) —
// BLANK, NO GLYPH (architect 2026-10-23: "there's no icons in these
// consoles … just a little label underneath"):
//   REST     — the flat face kKeyFace; a ONE-DEVICE-PX black outline on all
//              four sides (hairline_px); inside it the light top edge, row 1
//              kKeyTopLight and row 2 the mean of it and the face; a foot of
//              two rows kKeyFoot (the last two rows inside the outline) and
//              a right column kKeyFoot (the last column inside it) — the key
//              stands proud (make_pivot1.push_buttons' order: the top rows,
//              the foot, the right column over both).
//   PRESSED  — a live press arm (the glass's or the mouse's) or a keyboard
//              key's armed state: the face kKeyFacePressed and the top rows,
//              the foot and the right column painted as that face (the key
//              is down, no edge left standing), the outline and the LED
//              unchanged, nothing shifted.
//   CHECKED  — THE LED LIT, the key otherwise as its press state gives.
//   DISABLED — the key as its state gives; the caller dims the legend
//              (kDimInk).
//   HOT      — none.
// THE LED (key_led_rect) per `led`. paint_key takes a lit key from the
// bake's patch, its glow composited there (materials::paint_lit_key), and
// paints it plain where no patch holds the size; paint_key_plain is the
// painter itself, the bake's own road to a patch.
void paint_key(cairo_t* cr, const GuiRect& r, bool pressed, GuiKeyLed led);
void paint_key_plain(cairo_t* cr, const GuiRect& r, bool pressed, GuiKeyLed led);

// -- THE QUAVER (architect 2026-10-23; his pick of an outline pending) --------

// THE QUAVER — the caption's icon and the lists' row glyph — the one seat
// a picked outline will replace: make_sheet2.vec_mock()'s geometry (option
// 2, the antialiased vector of the mock), in a cell s x h at (x, y), s =
// `size_px` and h = s · 40 / 34 (the caption's 34 x 40 cell; the lists' 22
// x 26 the same proportion): the head an axis-aligned ellipse, rx .22 s and
// ry .15 s, centred at (.35 s, .72 h); the stem 1.6 device px wide (butt
// caps) from the head's centre-right rising .62 s; the flag one cubic from
// the stem's top toward (stem + .22 s, top + .18 s) and on to (stem + .26 s,
// top + .40 s), 1.6 px, round caps — `ink` its one color.
void paint_quaver(cairo_t* cr, double x, double y, double size_px,
                  GuiColor ink);

// -- THE TIME FIELD AND THE LABEL -------------------------------------------------

// THE TIME FIELD on `field` (architect 2026-10-23, "symmetrical and much
// tighter … white"): BLACK inside (kTimeFieldGround) under a ONE-DEVICE-PX
// SUNKEN RING — `dark` on its top row and left column, `light` on its
// bottom row and right column, mitred — the ring tones off the ground's mean
// (the palette's `ce_dark` / `ce_hilight`). Its digits are the caller's, the
// bone kBoneInk at the body's 22 px, centred on both axes (render.h's
// time_field_* accessors own the 40-px height and the 12-px pads).
void paint_ce_time_field(cairo_t* cr, const GuiRect& field, GuiColor dark,
                         GuiColor light);
// A PANEL LABEL — row 8's state line, its one reader: `run` in the bone
// kBoneInk, no shadow (the frame of record draws none, 2026-10-23; the
// panel label's shadow pair stood 2026-10-10 to 2026-10-23).
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
