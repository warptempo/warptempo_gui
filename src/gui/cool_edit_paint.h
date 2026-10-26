#pragma once

#include "cool_edit_derive.h"   // the case ramp's Default bytes, the HLS rule
#include "materials.h"   // the skin's inks and the key's constants
#include "render.h"   // GuiRect, GuiColor, palette(), the program's accessors

#include <array>
#include <cairo/cairo.h>
#include <cstdint>

// THE PROGRAM'S PAINTERS (architect 2026-10-09, "chrome means chrome, the
// program is Cool Edit"; program_spec.h's head) IN THE HI-FI PANEL SKIN
// (architect 2026-10-23; materials.h's head): THE KEY
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
// an intention of a diagonal"; the mitre's head at paint_hairline_frame,
// render.cpp):
// wherever a light tone meets a dark one at a ring's corner the corner block
// is split along the 45-degree diagonal — the time field's ring, the view
// bar's span, the program ring. A two-line staircase that is no ring — the
// cue's and the playhead head's 9-7-5-3-1 rows — is drawn as its diagonal
// (their painters say how). The dots are cells (a dot is not a line).

// -- THE CANVAS COLUMN'S CONSTANTS (2026-10-09) ---------------
// The bytes Cool Edit draws whatever its preset: THE VIEW BAR'S FIELD, black;
// THE RULER'S TICKS AND DIGITS, E0E0E0 (face-independent in all
// five presets), the digits' (+1, +1) SHADOW black ("≈ black,
// observed down to 0D0708", darker than the panel labels' dark tone). (Cool
// Edit's period-2 cursor column over the view bar's span, the playhead's
// color and 000000 alternating, is not drawn: architect 2026-10-09 ~14:30.)
// THE PLAYHEAD'S OWN COLOR — the head and its dots in the canvas — is the
// palette's `playhead_stem`
// since 2026-10-09 ~11:50, Cool Edit's Curs FFFF00 on every built-in but
// the CEPro 1.2 pair's FFFF6C (palette_file.h's role table), as the cues'
// two colors are the
// palette's `cue` and `range`; the triangles' shadow is the derived
// `ce_cue_shadow`. THE RULER'S INK IS NO TONE OF FACE AND NEVER SWAPS
// (architect 2026-10-10, "we follow Cool Edit's theming"):
// its ticks are E0E0E0 byte for byte on
// all nine Faces captured that day, the light and engraved ones included,
// and its digits' cores the same ink over the same black shadow — the
// ruler's ground is the dark `mid` at every Face, so Cool Edit keeps it
// light where the panel's text and label swap. Not a Face tone left
// undone: following Face here would depart from Cool Edit. IN THE HI-FI SKIN
// THE TICKS AND DIGITS ARE THE BONE kBoneInk on the flat dark kRulerGround
// (architect 2026-10-23, "the timestamps are good"; render.h's ruler block),
// THE TICKS over a ONE-DEVICE-PX black shadow at (+1, +1), THE DIGITS BARE
// (architect 2026-10-25: their shadow "is basically useless because it's so
// dark, it has no effect").
inline constexpr uint32_t kCeViewBarField   = 0x000000;
inline constexpr uint32_t kCeRulerTick      = kBoneInk;
inline constexpr uint32_t kCeRulerShadow    = 0x000000;

// -- THE KEY (architect 2026-10-23; materials.h's KEY constants) ---------------

// THE LED's STATE on a key: none drawn (the scrub's thumb, the on-screen
// keyboard's keys — a typing key carries no lamp), a dead lamp, or lit in
// THE ACCENT (materials::accent_color).
enum class GuiKeyLed { None, Unlit, Lit };

// THE LED'S RECT on the key `r` (the approved 6 x 3 device px at
// 300 %): 2 x 1 W (scaled_px — an area, never a line), its
// left edge 2 W right of the key's left outline column and its bottom row 2
// W above the outline's bottom row — rows y1 − 8 .. y1 − 6 and columns
// x0 + 6 .. x0 + 11 at 300 %, rows y1 − 3 and columns x0 + 3 .. x0 + 5 at
// the laptop's 138 % (x0, y1 the outline's left column and bottom row), the
// mock's own place. `depth` the key's depth in device px, 0, 1 or 2
// (key_depth; paint_key's CHECKED and HELD rows): THE LED RIDES WITH THE
// FACE, `depth` device px right and down (hairline_px; architect 2026-10-23,
// "the LED doesn't seem to be moving … that's not realistic") — at 300 %
// x0 + 7 .. x0 + 12 on rows y1 − 7 .. y1 − 5 checked and x0 + 8 .. x0 + 13
// on rows y1 − 6 .. y1 − 4 held; at 138 % x0 + 4 .. x0 + 6 on row y1 − 2
// checked and x0 + 5 .. x0 + 7 on row y1 − 1 held. Empty on a key too small
// to hold it at rest. The one owner of the seat: the painter and the bake's
// glow read it.
GuiRect key_led_rect(const GuiRect& r, int depth);

// ONE KEY on `r`, w x h (the band's, row 8's and the render player's
// icon_case_w_px square; a word button's or a keyboard key's own box) —
// BLANK, NO GLYPH (architect 2026-10-23: "there's no icons in these
// consoles … just a little label underneath"). THE KEY'S EDGES ARE DEVICE
// PX BY DESIGN, never inherited Windows px (architect 2026-10-23, checked
// on the glass: "deliberately set in device pixels"; render.h's line rule,
// hairline_px): the outline, the light rows, the foot's two rows, the right
// column and the down key's wall are each one device px a row or column at
// every gui_scale; the key's box and the LED are areas (scaled_px).
//   REST     — the flat face kKeyFace; a ONE-DEVICE-PX black outline on all
//              four sides; inside it the light top edge, row 1 kKeyTopLight
//              and row 2 the mean of it and the face; a foot of two rows
//              kKeyFoot (the last two rows inside the outline) and a right
//              column kKeyFoot (the last column inside it) — the key stands
//              proud (paint order: the top rows, the
//              foot, the right column over both).
//   TWO DEPTHS (architect 2026-10-23, on the pressed sheet's K3: "when a
//              button is pressed but I'm not touching it — pushed in but I'm
//              not touching it — it's at a middle depth, and then when I
//              push it in, it goes deeper regardless of whether it was
//              pushed in or not"; "when a button is pressed and I push it
//              in, nothing happens — this will give extra feedback": a
//              hi-fi deck's key travels past where it sticks). THE DOWN KEY
//              inside the unchanged outline: a band of kKeyFoot `depth`
//              device px along the top row and the left column — the hole's
//              wall the sunken cap no longer hides — and the face
//              kKeyFacePressed filling the rest; the light rows, the foot
//              and the right column gone; THE LED `depth` device px right
//              and down with the face (key_led_rect), and whatever the key
//              carries on its face rides with it by the same depth (the
//              keyboard's caps; a printed legend stands on the ground under
//              the key and does not move). key_depth the one derivation.
//   CHECKED  — THE MIDDLE DEPTH, ONE DEVICE PX (the pressed sheet's reading
//              F of 2026-10-23: "one display pixel, because I thought
//              that's what we were doing"), THE LED LIT (architect
//              2026-10-23: "once I memorize the position of the buttons, I
//              should be able to scan with the corner of my vision … by
//              seeing which ones are pushed in"). A lit LED IS a checked
//              key: paint_key reads `led` Lit as checked.
//   HELD     — THE DEEP DEPTH, TWO DEVICE PX, WHATEVER THE CHECKED STATE: a
//              live press arm (the glass's or the mouse's) or a keyboard
//              key under the finger, `pressed`; at the lift the key returns
//              to its checked depth. THE LED LIT while it stands (architect
//              2026-10-23: "I would expect the little light to turn on when
//              pressed"; "make the LEDs flash when pressed. Flash meaning
//              turn on, as long as the button is held"): a key carrying a
//              lamp (Unlit or Lit) is lit for as long as the press arm
//              stands and returns to its state at the lift — the band's,
//              row 8's, the render player's and the word buttons' keys
//              alike (a lampless key, None — the scrub's thumb, the
//              keyboard's keys — stays lampless).
//   DISABLED — the key as its state gives; the caller dims the legend
//              (kDimInk).
//   HOT      — none.
//   THE FACE — FLAT, its tone the one above at every depth (architect
//              2026-10-25; materials.h's THE KEY).
// THE LED (key_led_rect) per `led`. paint_key takes EVERY KEY from the
// bake's patch for its size and its depth — a lit key's glow composited
// round that depth's LED (materials::paint_key_patch; materials.cpp's
// rebuild_key_patches) — so a key at any depth is one blit, and paints the
// key itself (paint_key_plain) where no patch holds it: before the first
// bake, a size the bake did not see. paint_key_at_depth is the same road at
// a depth the caller derived (the on-screen keyboard's, whose lampless keys
// carry a checked state — the Shift arm — paint_key cannot read off an
// LED); paint_key_plain is the painter itself, `depth` the geometry (0 the
// key at rest), the bake's own road to a patch.
int key_depth(bool pressed, bool checked);
void paint_key(cairo_t* cr, const GuiRect& r, bool pressed, GuiKeyLed led);
void paint_key_at_depth(cairo_t* cr, const GuiRect& r, int depth, GuiKeyLed led);
void paint_key_plain(cairo_t* cr, const GuiRect& r, int depth, GuiKeyLed led);

// (THE QUAVER'S PAINTER — paint_quaver, the mock's vector, the
// caption's icon and the lists' row glyph for the morning of 2026-10-23 — is
// retired with his pick: the quaver is an icon, assets/icons/Quaver.svg,
// icons.h's THE BOXED GLYPHS.)

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
// kBoneInk, no shadow (the hi-fi skin draws none, 2026-10-23; the
// panel label's shadow pair stood 2026-10-10 to 2026-10-23).
void show_ce_label(cairo_t* cr, const text_shape::ShapedRun& run, double x,
                   double baseline);

// (THE TAB STRIP — paint_ce_tab_strip, ce_tab_strip_layout, kCeTabCount and
// ce_tab_growth_px, Cool Edit's Organizer tabs, 2026-10-18 — IS REMOVED with
// the strip, architect 2026-10-22, the simple layout.)

// -- THE CUE'S TRIANGLE ---------------------------------------------------------

// COOL EDIT'S POINT-CUE MASK, THE CUES' AND THE
// PLAYHEAD HEAD'S ONE PAINTER: a staircase of five rows of one unit u
// (cue_triangle_unit_px, render.h) from `top` — 9, 7, 5, 3, 1 units — each
// row's SHADOW one unit right of it. THE UNIT IS ONE DEVICE PX AT EVERY
// SCALE (architect 2026-10-26, on the glass: "I think part of the problem is
// our flags are so much larger than Cool Edit's. I think we should make the
// triangle smaller, and that would make colors like the red from Cool Edit
// less problematic. I think that is a large reason why no colors really
// look right on the GUI." — then "Why not simply leave unscaled? That would
// preserve the stem/flag size relation from CEP"): the flag is Cool Edit's
// 9 by 5 pixels with its one-pixel shadow beside the one-device-px stem, at
// 1:1 whatever the gui_scale, the history view's split triangle the same
// (CueTrianglePass::split) and the playhead's head with them (render.h's
// cue_triangle_unit_px, which names every reader). DRAWN AS THE STAIRCASE'S
// DIAGONALS (architect 2026-10-09 ~12:40, the head's rule) AND CENTRED ON THE STEM (~14:35: "at
// every scale, the marker triangle must be fully centered on the stem"): the
// stem is ONE DEVICE PX at window column `col` (render.h's waveform_line_px,
// "on waveform → unscaled"), so the axis is its centre s + 0.5 and ONE
// ANTIALIASED TRIANGLE in `color` has the vertices (s + 0.5 − 4.5u, top),
// (s + 0.5 + 4.5u, top) and (s + 0.5, top + 5u) — its two sides mirror
// images about the stem — over THE SAME
// TRIANGLE ONE UNIT RIGHT in `ce_cue_shadow`, painted first (Cool Edit's
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
// the coverage up once at the triangle's unit read once; triangle() stamps as
// paint_ce_cue_triangle's road does and GROWS ONE DIRTY BOX; end() marks that
// box dirty once.
// THE CALLER'S CONTRACT: between begin() and end() nothing changes the
// context's clip, matrix, operator or group (each cairo road below saves and
// restores its own), so the one target holds the clip list and translation
// each triangle's own direct_pixel_target would have answered; and nothing
// else asks the coverage cache, whose one entry the pass holds (the unit
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
