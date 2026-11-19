#pragma once

// THE MODAL FAMILY'S SHAPE — ONE OWNER OF THE GEOMETRY EVERY MODAL READS
// (architect 2026-10-14, on his mocks over tablet captures,
// the player and the picker: "overlays/
// modals can take over the area from the trim bar on down, with just the
// bottom new border remaining"; "use a similar bevel inset as the main GUI
// window, so the modal/overlay isn't flush against the border"). EVERY MODAL
// STANDS IN IT: the render player and the project picker (the folder
// overlay's contents, folder_overlay.h, with the settings page and the
// value list it swaps in; and THE SCORE VIEWER since 2026-10-26, score_viewer.h, its
// page the box's interior), and since the same day's second
// ruling THE PROMPTS AND THE DIALOG EDITORS (his progress and settings-keyboard mocks: the taskbar dialog laid over the window's
// foot, 2026-10-10 to 2026-10-14, is retired — "we should stop doing that"),
// and since that evening THE PROGRESS MODAL, a batch's "Rendering X of Y..."
// (AppState::BatchProgress; the progress mock's own subject).
//
// (INTERFACE VERSUS MODAL, architect 2026-10-14: the player and the picker
// stand in the box but are interface, so what they leave dead greys; the
// prompts, editors and progress modal are modal and grey nothing — the rule
// is recorded at redesign_button_enabled's head, app_state.h.)
//
// A MODAL TAKES THE AREA FROM THE VIEW BAR'S TOP ROW DOWN (the simple
// layout, architect 2026-10-22, on the modal mocks,
// "Yes, this all looks good"): the toolbar band above it (the app's
// caption over it too, until 2026-10-24) stays as the main window paints
// it. Across, it runs the window's width,
// as every program lane does. Top to bottom:
//   1. THE BOX — ITS PANE ON THE PANEL'S COLUMNS (render.h's well_columns,
//      the panel's own since 2026-11-19, send Z: edge to edge; THE EDGE
//      RULE's statement there), its top rows the trim lane's first rows
//      (the line under the top icon row), its bottom rows the well's foot;
//   2. directly under it THE CONTROLS ROW, THE BOTTOM ICON ROW'S OWN ROWS —
//      the inner pad 8 W, the key (key_width wide and key_height tall,
//      render.h's icon_case_w_px / icon_case_h_px), the outer pad 7 W
//      (strip_h_px) — on the body tone, no line, bar or border, its
//      outermost BOXES 6 W INSIDE THE PANEL (render.h's kEdgeInsetW;
//      materials.h's THE KEY);
//   3. under both, THE STATUS BAR on the window's foot, never covered
//      (render.h's status bar block: it keeps its process line under every
//      modal and drops its selection count).
// ONE GROUND, ONE PANE (architect 2026-11-01 night, M6 A — "the list pane
// with no frame" — for every member, M7 / M8 / M11 after it): THE CONTROLS
// ROW AND THE BOX'S OWN OUTER ROWS are the body tone (the palette's `face`,
// `body_face` since 2026-11-19, send Z); THE BOX'S INTERIOR — the box less
// one hairline, one W, above and below — IS ONE FLAT PANE, materials.h's
// `pane_face` (Default's 363636) (Live's panel face, the keys' face), with
// NO FRAME, NO RING, NO SHADOW AND NO ROW SEPARATOR, every member's alike:
// the lists, a prompt's question, the progress modal's sentence, a dialog
// editor's line ("the user shouldn't be typing on a non-flat color",
// 2026-10-23). THE ONE EXCEPTION IS THE SCORE VIEWER'S PANE,
// `score_pane_face` (Default's 191919) (below). EVERY BUTTON ARC'S (send
// Z's stage C, 2026-11-19; materials.h's THE KEY): a glyph key Arc's linked
// button, a word button Arc's text button, its word alone on it and its
// tooltip the word (paint_handler.cpp's modal_word_buttons and
// paint_modal_dialog's button row) (paint_modal_box). (The overlays' own
// look — lists, the centered dialogs, the editors' entry — is send Z's
// stage D.)
// SO THE BOX'S BOTTOM IS DERIVED FROM THE BOTTOM UP (the controls row), and
// it lands on the well's foot. At the tablet's 2304 x 1440, 200 % and the
// keys' defaults, key_width 36 and key_height 28 (the 72 x 56 face, the
// pads 16 and 14 and the 56-row title bar in device px; the hairline one W,
// 2 device px): the box's top rows 142–143 (the trim lane's first rows, the
// line under the band's rows 0–141), its bottom rows 1306–1307 (the well's
// foot), those rows ground, the PANE x 0..2303 (the panel's columns) and y
// 144..1305; the controls row 1308–1393, its face rows 1324–1379, its first
// face's columns 12–83 and its last 2220–2291; the status bar 1394–1439
// under it. A DIALOG'S TEXT (THE TEXT SEAT, below) on the list rows' seat,
// 18 px in from the pane's edges.
//
// THE ON-SCREEN KEYBOARD STANDS UNDER THE STACK (architect 2026-10-14,
// on his settings-keyboard mock; the spacing 2026-10-22): when a dialog
// editor stands on the glass, the keyboard's band (onscreen_keyboard::
// surface_rect — its edge pad on all four sides, the lanes' width, no line
// of its own) stands directly on the window's foot, and the controls row
// stands on the keyboard's band, the box shortened to fit. Every rect below
// takes `keyboard_h`, the band's height when the keyboard stands under the
// stack and 0 otherwise (onscreen_keyboard::modal_keyboard_h_px, whose
// header reads this one; the folder overlay never stands with the keyboard,
// the record at onscreen_keyboard::stands, and passes 0). THE SPACE BETWEEN
// THE CONTROLS ROW'S CASES AND THE KEYBOARD'S TOP KEYS IS THE SUM OF THE TWO
// SIDES' NORMAL PADDINGS (architect 2026-10-22, "the rule that we had
// implemented before"): the outer pad under the row's cases (7 W, send Z)
// plus the keyboard band's own edge pad above its first key row — by
// construction,
// the row standing directly on the band with nothing between; THAT EDGE PAD
// IS THE KEYBOARD'S HEIGHT FLOOR'S ANSWER since 2026-11-08
// (onscreen_keyboard::edge_pad_px: the band's top at or above the lower
// channel's center line, the pads grown in even W), so the box is as much
// shorter as the floor grows it. On the tablet (200 %) at key_height 28 and
// the keyboard's defaults, osk_key_w 24 and osk_key_h 30 (48 x 60 device
// px), the band standing on the bottom icon row's foot, row 1393, over the
// status bar: the line's first row 1042, the natural band's top 1056 — 14
// px short, 7 W, made even 8 — each pad 15 + 8 = 23; THE ROWS: the
// keyboard's band 1040–1393, the controls row 954–1039 (its face rows
// 970–1025), the box's bottom rows 952–953 (ground), the pane y 144..951;
// 14 + 23 = 37 device rows between the row's faces and the first key row
// (1063).
//
// Geometry here, every rect a fact that asks no standing, and the box's two
// standing predicates beside it (modal_box_dialog_stands, modal_box_stands);
// the painter is paint_modal_box (paint_handler.cpp), and every paint site
// and every hit site of a member reads these rects (on screen is as
// painted).
#include "app_state.h"
#include "materials.h"      // the flat look's rules
#include "render.h"         // palette(): `pane_face`, `score_pane_face`

#include <algorithm>
#include <cstdint>

// THE BOX'S DIALOG MEMBERS STAND (architect 2026-10-14): A PROMPT — over any
// surface, the render player's and the picker's lists included, its question
// taking the box for as long as it is asked — A DIALOG EDITOR
// (AppState::dialog_editor_session's set, the settings editor's buffer in
// its three kinds) or THE PROGRESS MODAL (AppState::BatchProgress, a batch's
// "Rendering X of Y..." from its Render press to its terminal). Their box is
// paint_modal_dialog's; the folder overlay's six contents paint their own
// (paint_folder_overlay). (The taskbar dialog's standing predicate,
// chrome_tenant_owns_bottom_row, 2026-10-10 to 2026-10-14, named the prompt
// and the editors less a prompt over the player or the picker, which stood
// on that member's controls row.)
inline bool modal_box_dialog_stands(const AppState& a) {
    return a.prompt.active || a.batch_progress.active ||
           a.dialog_editor_session() != 0;
}

// DOES ANY MEMBER'S BOX STAND — the folder overlay's, a dialog's or, since
// 2026-10-26, THE SCORE VIEWER'S (score_viewer.h: the page in the interior,
// paint_modal_dialog's score branch). The waveform's painted rect is empty
// while it does (onscreen_keyboard::waveform_paint_area): the box covers the
// canvas column whole.
inline bool modal_box_stands(const AppState& a) {
    return folder_overlay_stands(a) || modal_box_dialog_stands(a) ||
           score_viewer_stands(a);
}

// THE CONTROLS ROW (item 2): row 8's own rows — the lane itself
// (bottom_row_area) on the window's foot, or, with the keyboard under the
// stack, the same rows lifted onto the keyboard's band. Its seats are row
// 8's (paint_handler.cpp's bottom_row_seats, the one owner): the well's
// columns, the faces on the pane's outer columns.
inline GuiRect modal_controls_row_rect(const AppState& a, int keyboard_h) {
    const GuiRect lane = bottom_row_area(a);
    if (lane.w <= 0 || lane.h <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{lane.x, lane.y - std::max(0, keyboard_h), lane.w, lane.h};
}

// THE BOX (item 1), its outer rect: across, THE PANEL'S COLUMNS (render.h's
// well_columns, the panel's own since 2026-11-19, send Z: edge to edge), so
// THE PANE stands on the panel's columns (THE EDGE RULE, retold that day:
// the pane held inside the panel; the ground columns that stood one
// hairline outside the well's would stand off the glass); down, the trim
// lane's first row to the controls row's top (the box's last rows the
// well's foot, the rows above it). A zero rect on a degenerate stack.
inline GuiRect modal_box_rect(const AppState& a, int keyboard_h) {
    const WellColumns well = well_columns(a);
    const GuiRect trim  = top_trim_row_area(a);
    const GuiRect row   = modal_controls_row_rect(a, keyboard_h);
    const int     lw    = hairline_px();   // the ground above and below the pane
    const int     y1    = row.y;
    const GuiRect box{well.x, trim.y, well.end - well.x, y1 - trim.y};
    if (box.w <= 2 * lw || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
    return box;
}

// THE PANE (architect 2026-10-23, on the installed player: "the amount of
// spacing there is between the selection, the left edge, top edge, and the
// frame itself … at most one device pixel"; the frame's ring retired
// 2026-11-01 night, M6 A, its rows and columns become ground; ONE W since
// 2026-11-04, the unit): THE INTERIOR is the box less ONE HAIRLINE above
// and below (hairline_px: 2 device px on the tablet at 200 %, 1 at 100 %;
// the box's outer rows the ground), the pane every member stands on —
// across, the panel's own columns (modal_box_rect; send Z, edge to edge),
// 0 .. 2303 on the tablet — and
// the lists' rows stand ONE HAIRLINE OF AIR inside it
// (modal_box_content_pad_px) on the left, the top and the right, so the lit
// row's bar reaches to one W from the pane's edges and the first row stands
// one W under its top row; a dialog's text — a prompt's question, a dialog
// editor's line — stands on the list rows' text seat (THE TEXT SEAT,
// below).
inline GuiRect modal_box_interior_rect(const AppState& a, int keyboard_h) {
    const GuiRect box = modal_box_rect(a, keyboard_h);
    const int     lw  = hairline_px();
    if (box.w <= 2 * lw || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
    return GuiRect{box.x, box.y + lw, box.w, box.h - 2 * lw};
}
// THE CONTENT'S AIR inside the pane (THE PANE, above): ONE HAIRLINE, the
// line rule's W (architect 2026-11-04, the unit, reversing "never scaled":
// every former one-device-px length is one W) — 2 device px on the tablet, 1 at 100 %.
inline int modal_box_content_pad_px() { return hairline_px(); }

// THE SCORE VIEWER'S PANE — THE ONE DELIBERATE EXCEPTION to the box's
// `pane_face` (architect 2026-11-01 night, M9 A2, "THE SCORE VIEWER IS
// SETTLED": "not lighter, preferably from ableton palette directly"): the
// palette's `score_pane_face`, in every theme THE WAVEFORM'S INK (Default's
// 191919, Live's ink; theme_roles.h's derivation), the one tone reused.
// THE REASON: the score's page is a picture on a dark mat, not a list field.
// The GPU's field round the page (score_viewer.cpp's
// score_viewer_publish_gpu_page) and the CPU's pane under the score
// viewer's box (paint_modal_dialog's call of paint_modal_box, for a frame
// with no page hole) both read palette().score_pane_face; the page's fit and
// centering are untouched.

// THE PANEL: everything the modal takes ABOVE the controls row — the box
// and the ground margins beside it — across the lane, from
// the view bar's top row to the controls row's top. The ground a member's
// painter lays (paint_modal_box), the rect its content's damage erases and
// the outer claim of a press on the box.
inline GuiRect modal_panel_rect(const AppState& a, int keyboard_h) {
    const GuiRect box = modal_box_rect(a, keyboard_h);
    const GuiRect row = modal_controls_row_rect(a, keyboard_h);
    if (box.h <= 0 || row.w <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{row.x, box.y, row.w, row.y - box.y};
}

// THE WHOLE AREA A MODAL TAKES: the panel and the controls row under it
// (the keyboard's band below a lifted row is its own). The slot's show and
// hide damage (onscreen_keyboard::slot_damage_rect) and a dialog's surface
// (ModalDialogGeometry::box).
inline GuiRect modal_area_rect(const AppState& a, int keyboard_h) {
    const GuiRect panel = modal_panel_rect(a, keyboard_h);
    const GuiRect row   = modal_controls_row_rect(a, keyboard_h);
    const int     y0    = panel.h > 0 ? panel.y : row.y;
    const int     y1    = row.y + row.h;
    if (row.w <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{row.x, y0, row.w, std::max(0, y1 - y0)};
}

// THE TEXT SEAT — ONE OWNER for the box's text and the lists' text column
// (architect 2026-11-01 night, M8b, "m8b_lineeditor.png APPROVED": the
// editor's line on the list row's seat, "a little more padding on top/left
// (and presumably bottom/right)"; 2026-11-02, M14, "keep" — the question's
// seat the line editor's and the settings rows'): kModalBoxTextSeatW, 9 W
// IN FROM THE PANE'S EDGE (18 device px on the tablet at 200 %;
// modal_box_interior_rect's; the seat that stands round the text, back
// 2026-11-03, the retrofit, after the text round's 24 device px of that
// morning), an AREA scaled with gui_scale through scaled_px at the element
// (18 at 200 %, 9 at 100 %). MEASURED FROM THE PANE (architect 2026-11-04,
// the unit, reversing the 2026-11-03 seat measured from the box: with the
// ring one W the eye's air between the pane's edge and the text would have
// fallen short on the tablet; 9 W from the pane is the whole W nearest the
// 17 device px he approved inside the pane, M8b / M14), on all four sides
// alike.
// Its readers, which cannot drift apart:
//   THE BOX'S TEXT — a prompt's question, the progress modal's sentence and
//     the dialog editors' line, symmetric on all four sides: THE PEN the
//     seat in from the pane's left column (column 42 on the tablet), THE WRAP
//     ending the seat short of its right edge (one past 2261), THE FIRST
//     ROW'S ASCENDER LINE — the face's own "l" ink top,
//     gui_font_ascender_ink_px, never the line box's top: the air he sees is
//     the ink's — the seat under its top row (row 164; modal_box_text_
//     baseline0), each further row one list row pitch below (the row and
//     its gap, folder_overlay.h's kListRowW and button_row_gap_px: 26 + 1
//     = 27 W, 52 + 2 = 54 device rows on the tablet since 2026-11-04
//     night), and THE FOOT the seat over its
//     bottom edge (the text room's clip, one past row 1309);
//   THE LISTS' TEXT COLUMN — paint_handler.cpp's kListGlyphXW, the
//     glyph's first ink column in a listing with glyph rows and the names' column in a
//     glyph-less one (the settings page, the value lists, the trim presets,
//     the BPM form), the same seat in from the pane's left column. (The
//     lists keep their rows: the folder overlay's row box, 52 px, at the
//     content's one W under the pane's top, the cap centred on each row's
//     band, redesign_baseline.)
// (A 12-px pad inside a one-px ring stood 2026-10-23 to 2026-11-02.)
inline constexpr double kModalBoxTextSeatW = 9.0;
inline int modal_box_text_seat_px() {
    return scaled_px(kModalBoxTextSeatW, 1);
}
// THE TEXT'S ROOM in the pane `pane` (modal_box_interior_rect): x the pen's
// column (the seat in from its left column), w the soft wrap's width (the
// pane less the seat on each side), zero on a pane too narrow to hold it; y
// the pane's top row (a row's band rises above the ascender line it seats)
// and the bottom one past the seat over the pane's bottom edge: the text's
// clip.
inline GuiRect modal_box_text_room(const GuiRect& pane) {
    const int seat = modal_box_text_seat_px();
    if (pane.w <= 2 * seat || pane.h <= seat) return GuiRect{0, 0, 0, 0};
    return GuiRect{pane.x + seat, pane.y, pane.w - 2 * seat, pane.h - seat};
}
// THE FIRST ROW'S BASELINE in the pane `pane`: the seat under the pane's top
// row is the ascender line, the baseline the ascender's own height under it
// — EXACT (architect 2026-11-15, the no-grid arc): the face's real "l" ink
// height, unrounded, so the lowercase ink's top stands on the seat's edge.
inline double modal_box_text_baseline0(const GuiFont& font, const GuiRect& pane) {
    return static_cast<double>(pane.y + modal_box_text_seat_px()) +
           gui_font_ascender_ink_px(font);
}
