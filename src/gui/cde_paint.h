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
// colour (cde_select, the trough, the checked face and the armed menu
// entry), the field's two shadows, the title's two and the inactive
// frame's two are the CDE block of roles (theme_file.h); NO HOVER FACE
// anywhere — Motif has none (the deviations doc records the one-hover-face
// rule's unused allowance).
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

// THE ARMED MENU ENTRY (2026-10-08, the NEdit capture
// tmp/Screenshot_2026-10-08_10-14-22.png, a real Motif application under
// NsCDE's Solaris palette): Motif draws an armed cascade button or menu item
// ETCHED IN when XmNenableEtchedInMenu is True, which CDE's sys.resources
// sets — a SUNKEN one-W ring (the bottom shadow on its top row and left
// column, the top shadow on its bottom row and right column) round THE
// SELECT COLOR (XmNselectColor, cde_select: the capture's 3E7C8E is its body
// 4992A7 at 85 %, the arithmetic cde_derive.h proves), the label unpushed in
// the label color. The capture's "File" title armed: the box columns 514-552
// over the bar's body rows 274-296, the ring at 274 / 514 dark and 296 / 552
// light, the fill 3E7C8E inside; its "New" item armed: rows 308-330 across
// the pane's inner width (515-754), the same ring and fill. Filled whole,
// then the ring.
void paint_cde_armed(cairo_t* cr, const GuiRect& r);

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
// tablet's Maximize, which cannot restore) is the same bar through MOTIF'S
// INSENSITIVE STIPPLE (architect 2026-10-08 ~17:45: every disabled glyph the
// stipple; render.h's stipple pair), the box's face showing through every
// other device px — dtwm greys no button of its own, the product's truthful
// rule greys this one, and the window menu's verb greys with it
// (dropdown_item_enabled's Window arm). Drawn after the box.
void paint_cde_caption_glyph(cairo_t* cr, const GuiRect& b, CdeCaptionBox which,
                             bool active, bool enabled);

// -- THE SCROLL BAR (the trim lane, the popup lists' bar) ----------------------

// THE TROUGH — XmScrollBar on the notepad capture (rows 383-395: a sunken
// one-px ring, the bottom shadow on its top and left, the top shadow on its
// bottom and right, THE SELECT COLOUR 9397A5 inside — XmNtroughColor is the
// set's select): the ring on `bar` and cde_select inside it. THE RECT IS
// THE WHOLE BAR'S, ITS ARROWS INCLUDED (architect 2026-10-08 ~19:05, off the
// notepad capture: "today's caps lack that frame") — the popup list's bar
// end to end, and on the trim lane the trim bar alone, the begin cap's
// outer edge to the end cap's, ONE SUNKEN RECTANGLE on the lane's body
// (render_trim_flags). The arrows and the slider stand inside the ring
// (below), on the bar's inner rows.
void paint_cde_trough(cairo_t* cr, const GuiRect& bar);
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
// light) on the cap's box `b` (the trim lane's 13 x 13, the popup bar's —
// the spec's scroll_bar_px, CDE's own 13 since 2026-10-08), THE CAP STANDING
// AT THE BAR'S END INSIDE THE TROUGH'S RING (2026-10-08 ~19:05, the
// architect off the notepad capture: the trough's rect is the whole bar's,
// paint_cde_trough): the triangle's box is the cap less one W on every side
// — the ring's line on the bar's end and its two long sides, and one W of
// trough at the side the slider stands on (the capture's one trough pixel
// between arrow and slider) — 11 x 11 in the 13 (the notepad's left arrow
// x 6-16 inside the ring at x 5, the trough at 17, the slider from 18; the
// Open dialog's up arrow rows 173-183 under the ring's row 172, the trough
// at 184); the three edges one W wide inside it, the dark ones laid first
// so the apex and the light base corner stay light as the pixel art has
// them. PRESSED the face sinks (the edge tones swapped, dtwm's armed
// arrow); no shift.
enum class CdeArrowDir { Left, Right, Up, Down };
void paint_cde_arrow(cairo_t* cr, const GuiRect& b, CdeArrowDir dir,
                     bool pressed);
// THE SAME TRIANGLE IN EXACTLY THE BOX `t` (its base one side of the box,
// its apex the middle of the opposite side, the edges and tones as above) —
// paint_cde_arrow's own drawing, and the playhead's head under cde
// (render.h's playhead head block: Motif's down arrow, 9 x 9 W).
void paint_cde_arrow_triangle(cairo_t* cr, const GuiRect& t, CdeArrowDir dir,
                              bool pressed);

// -- THE SCALE (the scrub, the picker's sliders) -------------------------------

// XmScale: THE TROUGH a sunken one-W ring in the body's tones filled with
// the select colour (the Audio capture's volume scale, x 368-382: the ring
// 5D6069 / DCDEE5 round 9397A5) across `trough`; THE SLIDER a raised box in
// the body on `box` (the thumb's rect, one W inside the trough's ring).
void paint_cde_scale_trough(cairo_t* cr, const GuiRect& trough);
void paint_cde_scale_slider(cairo_t* cr, const GuiRect& box);

// -- THE WINDOW'S FRAME ---------------------------------------------------------

// DTWM'S RESIZE FRAME on the surface's outer `frame_px` band (render.h's
// sizing-frame block, window_frame_px: 5 Windows px under cde, the band
// outside the app's geometry on EVERY window — architect 2026-10-08 ~17:45,
// "the dtwm frame as in the original, on the tablet too", reversing the
// ~15:00 ruling that had hidden it on the tablet; the maximized laptop's and
// the tablet's band stand as the restored window's does, the spec's
// window_frame_maximized): the notepad capture's 5 (rows 0-4 and 396-400) —
// AN OUTER RAISED SHADOW TWO LINES THICK, TWO W OF THE FRAME'S FACE, AN INNER
// SUNKEN SHADOW ONE LINE THICK (2 light + 2 face + 1 dark on the top and
// left, 2 dark + 2 face + 1 light on the bottom and right), every ring
// mitred; in the active title colour and its set's tones while `focused`,
// the body and the inactive ring's tones otherwise (Solaris's inactive frame
// IS the body; the tablet's window is always focused). WITH DTWM'S CORNER
// PIECES (architect 2026-10-08 ~17:45, mock_frame_01, "mock 1 is good"):
// each of the eight runs carries a GROOVE — one line of the bottom shadow,
// then one of the top shadow — whose seam stands the CORNER'S LENGTH from
// the run's outer corner: dtwm's corner is the frame plus the title bar, 5 +
// 19 = 24 px on the capture (the ticks at x 23 / 24 on the top run, x 576 /
// 577 at its right end, y 23 / 24 and 376 / 377 on the sides), 5 + 17 = 22
// W here (frame_px + caption_row_h_px(), 66 device px at 300 %) — the dark
// line across the band from the outer shadow's second line to the inner
// shadow (exclusive), the light one from the same line through the inner
// shadow, the outermost line unbroken, the dark line on the lower x or y of
// the seam at every corner (the capture's own, all eight measured). The hit
// test is not the picture's: window_frame_edges_at's corners reach a caption
// height along the band as before.
void paint_cde_window_frame(cairo_t* cr, int surface_w, int surface_h,
                            int frame_px, bool focused);
