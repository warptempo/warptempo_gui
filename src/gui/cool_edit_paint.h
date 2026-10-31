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
// (hairline_px, render.h's line rule). (The button case with Cool Edit's
// ramp, its rings and its glyph, 2026-10-09 to 2026-10-23, is git history.)
// THE DIAGONAL RULE REACHES THE PROGRAM (architect 2026-10-09 ~12:40: "any
// jagged staircase of pixel lines, even if it's only two lines, we take as
// an intention of a diagonal"; the mitre's head at paint_hairline_frame,
// render.cpp):
// wherever a light tone meets a dark one at a ring's corner the corner block
// is split along the 45-degree diagonal — the time field's ring, the view
// bar's span, the program ring. THE TRIANGLES ARE ITS ONE EXCEPTION, AT
// 1:1 (architect 2026-10-26, "agree": "at this size the staircase is the
// truthful drawing and the antialiasing only blurs it"): the cue's and the
// playhead head's rows, their cells one device px, are Cool Edit's
// aliased staircase in whole cells (paint_ce_cue_triangle). The dots are
// cells (a dot is not a line).

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
// 138 % (x0, y1 the outline's left column and bottom row), the mock's own
// place, drawn at the laptop's scale of the day. `depth` the key's depth in
// device px, 0, 1 or 2
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
// icon_case_w_px by icon_case_h_px; a word button's or a keyboard key's own box) —
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
//              live press arm (the finger's or the pen's) or an on-screen
//              keyboard key under the finger, `pressed`; at the lift the key returns
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
// bone kBoneInk, no shadow — every time field in the program face's 22 px,
// row 8's clock (architect 2026-10-26 ~11:30) and the render player's two
// (~12:10, "yes" to symmetry; gui_font.h's table) — centred on both axes (paint_handler.cpp's
// time_field_* accessors own the 40-px height and the 12-px pads).
void paint_ce_time_field(cairo_t* cr, const GuiRect& field, GuiColor dark,
                         GuiColor light);
// A PANEL LABEL — row 8's tab name and state line and, since 2026-10-28,
// the logo lane's brand and legend runs (paint_logo_lane), its readers, the
// one painter of the chrome words' bone-over-shadow pair: `run` in
// the bone kBoneInk OVER THE CUE LABEL'S SHADOW, its offset and its color
// (architect 2026-10-26 ~11:30: "the bottom left timestamp and
// tab/github/etc text should take 26 as well, with the same shadow for the
// tab/git/etc text"): the run first in the cue triangle's shadow color
// (`ce_cue_shadow`, palette()) one label quantum right and one down
// (render.h's cue_label_shadow_offset_px — 3 and 3 device px at 300 %),
// then the bone over it. The shadow is ink, never extent.
void show_ce_label(cairo_t* cr, const text_shape::ShapedRun& run, double x,
                   double baseline);

// (THE TAB STRIP — paint_ce_tab_strip, ce_tab_strip_layout, kCeTabCount and
// ce_tab_growth_px, Cool Edit's Organizer tabs, 2026-10-18 — IS REMOVED with
// the strip, architect 2026-10-22, the simple layout.)

// -- THE CUE'S TRIANGLE ---------------------------------------------------------

// COOL EDIT'S POINT-CUE MASK, THE CUES' AND THE PLAYHEAD HEAD'S ONE
// PAINTER: a staircase of `rows` rows of whole cells of one unit u
// (cue_triangle_unit_px, render.h) from `top` — 2·rows − 1 cells on the top
// row down to one, the apex row one cell and each row above it two cells
// wider (15, 13, … 1 at 300 %'s eight rows; 9, 7, 5, 3, 1 at 200 %'s five,
// Cool Edit's own flag) — CENTRED ON THE STEM'S COLUMN `col` (architect
// 2026-10-09 ~14:35: "at every scale, the marker triangle must be fully
// centered on the stem"; the stem one device px there, render.h's
// waveform_line_px), its SHADOW the same cells in `shadow`, painted first.
//
// THE CELL IS ONE DEVICE PX AT EVERY SCALE (architect 2026-10-26, on the
// glass: "I think part of the problem is our flags are so much larger than
// Cool Edit's. I think we should make the triangle smaller, and that would
// make colors like the red from Cool Edit less problematic. I think that is
// a large reason why no colors really look right on the GUI." — then "Why
// not simply leave unscaled? That would preserve the stem/flag size
// relation from CEP"): the staircase stands in whole device cells beside
// the one-device-px stem, the playhead's head with them (render.h's
// cue_triangle_unit_px, which names every reader). THE ROW COUNT IS SCALED
// (architect 2026-10-26 ~13:40, on the row count as a per-device number:
// "why density? why not just simple scale? tablet is only real surface.
// later tablets may have higher dpi, but not lower" — and "yes" to the
// planner's proposal): authored 8 device px at 300 %, the rows the tuning
// key left on the tablet (~13:05, "read the current setting and apply"),
// rounded at the element like every length off the canvas
// (render.h's cue_triangle_rows, the one accessor) — 8 rows at 300 %, 5 at
// 200 % (Cool Edit's 9 by 5, observed at the laptop's 200 % inspection
// scale), 4 at 150 %
// and 138 %, 1 at the scale's floor of 50 %, the stem's own cell.
//
// DRAWN ALIASED, COOL EDIT'S HARD STAIRCASE (architect 2026-10-26 ~10:40,
// "agree" to "at this size the staircase is the truthful drawing and the
// antialiasing only blurs it"; ~13:05 the antialiased envelope's tuning
// key settled "off" on the glass and retired with its arm,
// closed_questions.md): a bitmap in effect,
// every pixel the color or the shadow whole, no coverage anywhere.
//
// THE SHADOW IS ONE LABEL QUANTUM RIGHT AND ONE DOWN (architect 2026-10-26
// ~13:05: the triangle's shadow takes the label's own offset, render.h's
// cue_label_shadow_offset_px — 3 and 3 device px at 300 %, 2 and 2 at
// 200 %, scaled — on every triangle, one rule: the cue's and the
// playhead's head). THE CALLER'S CLIP
// CUTS IT: in the marker lane the triangle stands on the lane's last rows
// and the lane's clip (paint_cues, render.cpp) ends at the lane's last
// row, so the shadow's bottom `offset` rows are lost and it never
// crosses the canvas's top frame row — "if so, that is correct" (the same
// ruling); in the ruler the head's paint is clipped to the head's own rows,
// the ground's last `rows` (paint_ruler_row, paint_handler.cpp), so its
// shadow is cut alike at the ground's bottom row. Its painted columns are
// [col − (rows − 1)u, col + rows·u + offset) (cue_triangle_half_w_px and
// cue_triangle_reach_right_px, render.h, the one owner every extent reads);
// its rows [top, top + rows·u + offset) before the clip.
// `shadow` is the shadow's color: `ce_cue_shadow` for a live cue and the
// playhead's head, the faded `ce_off_cue_shadow` for a disabled cue
// (2026-10-10, cool_edit_derive.h's faded look). TWO ROADS, ONE DRAWING
// (CueTrianglePass below): the stamp writes the cells' words straight into
// the target where direct_pixel_target takes it, else cairo fills the same
// cells as whole-pixel rectangles, antialias NONE. The triangles paint on
// the CPU — into the flag cache's surface and the ruler's cairo context —
// never through the canvas sink.
// One triangle alone (the playhead's head): a one-triangle CueTrianglePass.
void paint_ce_cue_triangle(cairo_t* cr, int col, int top, GuiColor color,
                           GuiColor shadow);

// THE TRIANGLE PASS (2026-10-13, a cost cut that paints the same pixels): the
// cues' triangles, one consecutive run on one context (paint_cues, render.cpp,
// left to right over every label), share ONE PREPARED TARGET instead of each
// taking its own. begin() asks direct_pixel_target once — the surface
// flushed, the clip's rectangle list copied, the translation read — and the
// unit, the rows and the shadow's offset once; triangle() stamps
// the cells and GROWS ONE DIRTY BOX (the shadow's offset right and down
// within it);
// end() marks that box dirty once. Where the target is refused, every
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
// alike on both roads, a whole pixel being in a box or not), the shadow's
// first, offset right and down alike on both roads, and then the color's,
// as the two fills do.
class CueTrianglePass {
public:
    void begin(cairo_t* cr);
    // The triangle: the stamp where the target was taken, else cairo's two
    // fills. (The `h` view's split triangle, two colors about the apex
    // column, 2026-10-10 to 2026-10-26, left when a nudged pair became two
    // cues.)
    void triangle(int col, int top, GuiColor color, GuiColor shadow);
    void end();

private:
    void stamp_span(int x0, int x1, int y, uint32_t pixel);
    cairo_t*          cr_        = nullptr;
    DirectPixelTarget target_{};
    bool              direct_    = false;
    int               u_         = 0;
    int               rows_      = 0;
    int               so_        = 0;   // the shadow's offset, right and down
    GuiRect           dirty_{};
    bool              dirty_any_ = false;
};
