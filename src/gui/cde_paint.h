#pragma once

#include "render.h"   // GuiRect, GuiColor, palette(), scaled_px, relief_line_px

#include <cairo.h>

// THE CDE PAINTERS (2026-10-08, the cde vocabulary — the chrome spec's cde
// arm, chrome_spec.h's kChromeSpecCde, whose head records every length
// against its capture): Solaris 9's dtwm and Motif as the guidebookgallery
// captures draw them (tmp/research/cde_solaris/dl/shots/, the law), drawn
// with MOTIF'S ONE BEVEL AT THE ARCHITECT'S ONE-W RULING (2026-10-08 ~14:10,
// sheet 01: CDE's authentic weight — only dtcalc used Motif's canonical 2):
// every raised face is ONE line of the top shadow on its top and left and
// ONE of the bottom shadow on its bottom and right, a sunken one the same
// swapped, THE TWO TONES MITRED where they meet (paint_relief_frame's rule,
// the product's over Motif's own square-ish corner: XmeDrawShadows gives
// the top row and the left column whole to the top shadow, the bottom row
// and the right column less their first pixel to the bottom — a 1-px
// staircase that is the mitre's own 45 degrees at one px; the notepad
// capture's title buttons, rows 5 / 23). Every painter here is called from
// its win2000 sibling's site on the spec's fork (paint_handler.cpp,
// render.cpp) and reads its lengths from the live spec.
//
// THE TONES ARE WINDOWS' QUARTET IN MOTIF'S SCHEME: the Solaris theme
// (theme_file.h's kGuiThemeCde) sets hilight = the body's top shadow,
// shadow AND dk_shadow = its bottom shadow and light_3d = the body, so the
// base's ONE-LINE families — paint_relief_raised_inner (Hilight / Shadow)
// and paint_relief_sunken_outer (Shadow / Hilight) — ARE Motif's raised and
// sunken bevels in the body, and the win2000 painters that draw those lines
// (the etched separators, the one-line time field, the hot-free flat
// toolbar's pressed line) draw Motif under cde with no fork. What needs a
// painter of its own is below: the tones that are not the body's (the
// caption's set-1 boxes, the text field's set-4 ring, the inactive frame's)
// and the shapes Windows never drew (Motif's beveled arrow, the select-
// colour trough, the scale's slider, dtwm's frame and glyphs). The select
// colour (cde_select, the trough and the checked face), the field's two
// shadows, the title's two and the inactive frame's two are the CDE block
// of roles (theme_file.h); NO HOVER FACE anywhere — Motif has none (the
// deviations doc records the one-hover-face rule's unused allowance).
//
// EVERY COLOUR OPAQUE, NO GRADIENT, NO ROUNDED CORNER, NO BLEND: Motif's
// drawing is solid cells; the antialiasing is the renderer's on the mitres
// and the arrows' edges alone.

// -- THE BEVELS ----------------------------------------------------------------

// ONE RAISED FACE in the body's tones on `r` (the ring alone; the caller
// fills), and one sunken: Windows' one-line families under the Solaris
// scheme, named for the readers here.
void paint_cde_raised(cairo_t* cr, const GuiRect& r);
void paint_cde_sunken(cairo_t* cr, const GuiRect& r);

// THE TEXT FIELD — XmTextField / XmText on the Open dialog capture (x 20,
// rows 58-82: its top row and left column set 4's bottom shadow 99948B, its
// bottom row and right column set 4's top shadow CCC5BA, the cream FFF7E9
// inside): `ground` filled, then the sunken ring in the FIELD'S OWN
// computed shadows (cde_field_bs / cde_field_ts), never the body's — Motif
// shades a widget from its own background. NO FOCUS RING (architect
// 2026-10-08 ~15:00: CDE's 1-W ring in the active-title colour round the
// focused text widget is not drawn — "anachronism ok to fit the screen and
// the Win95 metrics"; the flag editor's no-box rule and the caret are the
// cue).
void paint_cde_field(cairo_t* cr, const GuiRect& r, GuiColor ground);

// -- THE TOOLBAR BUTTON (GuiToolbarStyle::MotifFlat, chrome_spec.h) -----------

// ONE TOOL BUTTON on its case `r`: the body at rest with nothing drawn;
// PRESSED one sunken line, the glyph one W px right and down; CHECKED
// (`lamp`) the select colour inside one sunken line, the glyph unshifted;
// PRESSED AND CHECKED the checked face pushed; DISABLED as the others (the
// glyph's saturate is the caller's). Answers the glyph's shift.
int paint_cde_tool_button(cairo_t* cr, const GuiRect& r, bool lamp,
                          bool pressed);

// -- THE CAPTION (dtwm's title bar; render.h's caption block) ------------------

// THE FOUR BOXES of dtwm's title bar, each a raised one-W ring in COLOUR
// SET 1's tones (cde_title_ts / cde_title_bs) on the active title colour,
// the window-menu button at the lane's left, the title box between, then
// Minimize and Maximize (the notepad capture: four 19 x 19 boxes side by
// side in the 19-row bar; chrome_spec.h's cde head); INACTIVE the body under
// the inactive frame's ring (cde_inactive_ts / _bs — Solaris's 4-colour
// mode makes the inactive frame the body). A PRESSED button sinks (dtwm's
// armed button: the ring swapped), its glyph unmoved.
enum class CdeCaptionBox { WindowMenu, Title, Minimize, Maximize };
void paint_cde_caption_box(cairo_t* cr, const GuiRect& b, CdeCaptionBox which,
                           bool active, bool pressed);
// THE GLYPHS — dtwm's raised bars, read off the notepad capture in the
// 19-px button and fitted to the 17-W box (the spec's head): the window
// menu's bar 10 x 4, Minimize's square 4 x 4, Maximize's square 10 x 10,
// each A RAISED ONE-W RING ROUND THE FACE (the bar's two inner rows and the
// squares' inner cells the title colour), centred in the box by integer
// division of Windows px and rounded at the element; a DISABLED glyph (the
// tablet's Maximize, which cannot restore) keeps its ring in the inactive
// frame's tones on the active colour — dtwm greys no button, the window
// menu's verb greys instead (dropdown_item_enabled's Window arm). Drawn
// after the box.
void paint_cde_caption_glyph(cairo_t* cr, const GuiRect& b, CdeCaptionBox which,
                             bool active, bool enabled);

// -- THE SCROLL BAR (the trim lane, the popup lists' bar) ----------------------

// THE TROUGH — XmScrollBar on the notepad capture (rows 383-395: a sunken
// one-px ring, the bottom shadow on its top and left, the top shadow on its
// bottom and right, THE SELECT COLOUR 9397A5 inside — XmNtroughColor is the
// set's select): the ring on `lane` and cde_select inside it. The arrows
// and the slider stand inside the ring (below), on the lane's inner rows.
void paint_cde_trough(cairo_t* cr, const GuiRect& lane);
// THE SLIDER — the raised box in the body, one W inside the trough's ring
// on the lane's axis (`horizontal`: inset on top and bottom, the ends the
// caller's — the trim lane's body runs to its caps; a vertical bar's inset
// on left and right): the body filled under one raised line.
void paint_cde_slider(cairo_t* cr, const GuiRect& body, bool horizontal);
// THE ARROW — Motif's beveled triangle (XmeDrawArrow as the notepad's bars
// draw it, 11 px in the 13-px bar: the left arrow's apex at the trough's
// inner column, its upper edge the TOP SHADOW, its lower edge and its base
// the BOTTOM SHADOW, the face the body; the right arrow's base light; up's
// left edge light, right edge and base dark; down's base and left edge
// light) on the cap's box `b` (the trim lane's 16 x 16, the popup bar's):
// the triangle fills the box's rows inside the trough's ring and its
// columns less one W of trough at the side the slider stands on (the
// capture's one trough pixel between arrow and slider), the three edges
// one W wide inside it, the dark ones laid first so the apex and the light
// base corner stay light as the pixel art has them. PRESSED the face sinks
// (the edge tones swapped, dtwm's armed arrow); no shift.
enum class CdeArrowDir { Left, Right, Up, Down };
void paint_cde_arrow(cairo_t* cr, const GuiRect& b, CdeArrowDir dir,
                     bool pressed);

// -- THE SCALE (the scrub, the picker's sliders) -------------------------------

// XmScale: THE TROUGH a sunken one-W ring in the body's tones filled with
// the select colour (the Audio capture's volume scale, x 368-382: the ring
// 5D6069 / DCDEE5 round 9397A5) across `trough`; THE SLIDER a raised box in
// the body on `box` (the thumb's rect, one W inside the trough's ring).
void paint_cde_scale_trough(cairo_t* cr, const GuiRect& trough);
void paint_cde_scale_slider(cairo_t* cr, const GuiRect& box);

// -- THE RESTORED LAPTOP'S FRAME -----------------------------------------------

// DTWM'S RESIZE FRAME on the surface's outer `frame_px` band (render.h's
// sizing-frame block: the platform's 4-W hit length, which nothing moves):
// the notepad capture's 5 — 2 light + 2 face + 1 dark on the top and left,
// 2 dark + 2 face + 1 light on the bottom and right (rows 0-4 and 396-400:
// an outer RAISED two-px ring, two px of the frame's face, an inner SUNKEN
// one-px ring) — FITTED TO 4 as 1 + 2 + 1 under the one-W ruling: the
// outer ring one line, two W of face, the inner ring one line; in the
// active title colour and its set's tones while `focused`, the body and
// the inactive ring's tones otherwise (Solaris's inactive frame IS the
// body). THE CORNER PIECES (dtwm's 22-W ticks where the lines break) are
// PHASE 2 ("the laptop sizing frame polish", the brief) and are not drawn.
// HIDDEN ON THE TABLET (architect 2026-10-08 ~15:00, his ruling: the app
// fills the glass, "anachronism ok to fit the screen and the Win95
// metrics"): the tablet's window is maximized and this is never called
// there, as Windows' frame is not.
void paint_cde_window_frame(cairo_t* cr, int surface_w, int surface_h,
                            int frame_px, bool focused);
