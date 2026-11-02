#pragma once

#include "cool_edit_derive.h"   // the case ramp's Default bytes, the HLS rule
#include "materials.h"   // the skin's inks and the key's constants
#include "render.h"   // GuiRect, GuiColor, palette(), the program's accessors

#include <array>
#include <cairo/cairo.h>
#include <cstdint>

// THE PROGRAM'S PAINTERS (architect 2026-10-09, "chrome means chrome, the
// program is Cool Edit"; program_spec.h's head): the time field and the
// cue's triangle (the canvas column's lanes — the view bar, the ruler, the
// cues — are painted by render_trim_flags, paint_ruler_row and the flag
// pass out of these and the constants below). The lengths are
// program_spec.h's (render.h's program accessors, each a rounded part).
//
// THE COLORS: the flat look's constants (materials.h: the inks, the fields)
// and the two ring tones off the flat ground (materials.h's kRingLight /
// kRingDark, the palette's `ce_hilight` and `ce_dark`), which the well's
// rings still wear until the well's part of the flat arc; every line a ring
// draws is ONE DEVICE PX (hairline_px, render.h's line rule).
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
// since 2026-10-09 ~11:50, Live's cyan 03C3D5 since 2026-11-01
// (palette_file.h's role table), as the cues' one color is the
// palette's `cue`; the triangles' shadow is the derived
// `ce_cue_shadow`. THE RULER'S INK IS NO TONE OF FACE AND NEVER SWAPS
// (architect 2026-10-10, "we follow Cool Edit's theming"):
// its ticks are E0E0E0 byte for byte on
// all nine Faces captured that day, the light and engraved ones included,
// and its digits' cores the same ink over the same black shadow — the
// ruler's ground is the dark `mid` at every Face, so Cool Edit keeps it
// light where the panel's text and label swap. Not a Face tone left
// undone: following Face here would depart from Cool Edit. THE TICKS AND
// DIGITS ARE THE TEXT TONE kTextInk on the flat dark kRulerGround (the bone
// F0F0F0 from 2026-10-23, "the timestamps are good"; the text tone since
// M13, 2026-11-02; render.h's ruler block, the well's part of the flat arc),
// THE TICKS over a ONE-DEVICE-PX black shadow at (+1, +1), THE DIGITS BARE
// (architect 2026-10-25: their shadow "is basically useless because it's so
// dark, it has no effect").
inline constexpr uint32_t kCeViewBarField   = 0x000000;
inline constexpr uint32_t kCeRulerTick      = kTextInk;
inline constexpr uint32_t kCeRulerShadow    = 0x000000;

// (THE KEY — every case of the band, row 8, the controls rows, the word
// buttons, the scrub's thumb and the on-screen keyboard — is the flat
// look's, materials.h's paint_key, since 2026-11-01.)

// (THE QUAVER'S PAINTER — paint_quaver, the mock's vector, the
// caption's icon and the lists' row glyph for the morning of 2026-10-23 — is
// retired with his pick: the quaver is an icon, assets/icons/Quaver.svg,
// icons.h's THE BOXED GLYPHS.)

// -- THE TIME FIELD ---------------------------------------------------------------

// THE TIME FIELD on `field` (architect 2026-10-23, "symmetrical and much
// tighter … white"): BLACK inside (kTimeFieldGround) under a ONE-DEVICE-PX
// SUNKEN RING — `dark` on its top row and left column, `light` on its
// bottom row and right column, mitred — the ring tones off the flat ground
// (the palette's `ce_dark` / `ce_hilight`; the time fields are the well's
// part of the flat arc). Its digits are the caller's, the text tone
// kTextInk, no shadow — every time field in the program face's 22 px,
// row 8's clock (architect 2026-10-26 ~11:30) and the render player's two
// (~12:10, "yes" to symmetry; gui_font.h's table) — centred on both axes (paint_handler.cpp's
// time_field_* accessors own the 40-px height and the 12-px pads).
void paint_ce_time_field(cairo_t* cr, const GuiRect& field, GuiColor dark,
                         GuiColor light);

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
