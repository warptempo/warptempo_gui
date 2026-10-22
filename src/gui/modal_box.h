#pragma once

// THE MODAL FAMILY'S SHAPE — ONE OWNER OF THE GEOMETRY EVERY MODAL READS
// (architect 2026-10-14, on his mocks over tablet captures,
// tmp/modal_arc/mock_modal_player_v2.png and mock_modal_picker.png: "overlays/
// modals can take over the area from the trim bar on down, with just the
// bottom new border remaining"; "use a similar bevel inset as the main GUI
// window, so the modal/overlay isn't flush against the border"). EVERY MODAL
// STANDS IN IT: the render player and the project picker (the folder
// overlay's two contents, folder_overlay.h), and since the same day's second
// ruling THE PROMPTS AND THE DIALOG EDITORS (mock_modal_progress_v2.png and
// mock_modal_settings_osk_v2.png: the taskbar dialog laid over the window's
// foot, 2026-10-10 to 2026-10-14, is retired — "we should stop doing that"),
// and since that evening THE PROGRESS MODAL, a batch's "Rendering X of Y..."
// (AppState::BatchProgress; mock_modal_progress_v2.png's own subject).
//
// (INTERFACE VERSUS MODAL, architect 2026-10-14: the player and the picker
// stand in the box but are interface, so what they leave dead greys; the
// prompts, editors and progress modal are modal and grey nothing — the rule
// is recorded at redesign_button_enabled's head, app_state.h.)
//
// A MODAL TAKES THE AREA FROM THE VIEW BAR'S TOP ROW DOWN: the top strip
// above it — the tab strip among it — and THE COLUMN'S 5 W AIR under the
// strip stay as the main window
// paints them; the border under row 8 stays visible under it, the window's
// lowest lane. Across, it runs the window's width, as every program lane
// does since 2026-10-21 (inside the program frame's two columns until
// then). Top to bottom:
//   1. ONE SUNKEN BOX wearing THE CANVAS COLUMN'S RING — exactly
//      paint_canvas_column_frame's: one program line, `ce_dark` top and left,
//      `ce_hilight` right and bottom, mitred — standing in the canvas
//      column's own columns (the column's side margin of face in from each
//      side, the waveform area's x and width one line out), its top row the
//      view bar's top row;
//   2. under the box's bottom light row, THE MAIN WINDOW'S 5 W FOOT of face
//      (column_foot_face_h_px);
//   3. THE CONTROLS ROW: row 8's content height (4 W of air, the 23-W case,
//      5 W of air) on the face, NO BARS (row 8's, 2026-10-21) and NO DOCK BAR
//      ABOVE IT (the dock bar's six rows go to the box), its outermost cases
//      or buttons FLUSH WITH THE RING'S OUTER EDGES;
//   4. then THE BORDER UNDER ROW 8, on the window's foot.
// THE PANEL AND THE CONTROLS ROW ARE THE WOOD AS DRAWN (architect
// 2026-10-22, "just make the rest wood": materials.h's WOOD; the brass
// 2026-10-21 to 2026-10-22, the FLAT region before); THE FIELDS STAY FLAT ("the user
// shouldn't be typing on a non-flat color") — a dialog editor's line, the
// lists' ground; THE WORD BUTTONS ARE THE EBONY CASE (architect 2026-10-22,
// "buttons in ebony — yes": the band's case on each button's own box, its
// word in the chrome's one ink — materials.h's CASE and INKS; their flat
// raised faces, "ebony eventually, flat fine for now", 2026-10-21 to
// 2026-10-22); a prompt's question stands on the face, the wood
// (paint_modal_box).
// SO THE BOX'S BOTTOM IS DERIVED FROM THE BOTTOM UP (the border, the
// controls row, the foot), and it lands 6 W below the main window's canvas
// bottom: the mock's picture, intended. At the tablet's 2304 x 1440 and
// 300 %: the ring's top row 234–236 (the view bar's, under the tab strip's
// rows 156–218 and the column's air), its bottom row 1308–1310, its columns
// 21–23 and 2280–2282, the interior x 24..2279 and y 237..1307 (the main
// canvas's own rows end at 1289, its bottom frame row 1290–1292); the foot
// 1311–1325; the controls row 1326–1421, its case rows 1338–1406, its first
// case's columns 21–89 and its last 2214–2282; the border 1422–1439.
//
// THE ON-SCREEN KEYBOARD STANDS UNDER THE STACK (architect 2026-10-14,
// mock_modal_settings_osk_v2.png): when a dialog editor stands on the glass,
// the keyboard's band (onscreen_keyboard::surface_rect — its edge pad on all
// four sides, the lanes' width, no line of its own) stands directly on the
// window's foot, and items 1 to 4 — THE BORDER UNDER ROW 8 WITH THEM — stand
// on the keyboard's band, the box shortened to fit. Every rect below takes
// `keyboard_h`, the band's height
// when the keyboard stands under the stack and 0 otherwise
// (onscreen_keyboard::modal_keyboard_h_px, whose header reads this one; the
// folder overlay never stands with the keyboard, the record at
// onscreen_keyboard::stands, and passes 0). At 300 %: the keyboard's band
// 1037–1439 (its first key row's top 1051), the border 1019–1036, the
// controls row 923–1018 (its case rows 935–1003), the foot 908–922 and the
// ring's bottom row 905–907, the interior y 237..904.
//
// THE INTERIOR'S GROUND SAYS WHAT THE SURFACE IS (ModalBoxGround): THE FIELD
// GROUND, the list's dark field pair, where you pick or type — the render
// player's and the picker's lists, a dialog editor's line; THE FACE where you
// only read — a prompt's question, the progress modal's sentence.
//
// Geometry here, every rect a fact that asks no standing, and the box's two
// standing predicates beside it (modal_box_dialog_stands, modal_box_stands);
// the painter is paint_modal_box (paint_handler.cpp), and every paint site
// and every hit site of a member reads these rects (on screen is as
// painted).
#include "app_state.h"
#include "render.h"

#include <algorithm>

// THE INTERIOR'S GROUND (the head): which of the two the member's surface is.
enum class ModalBoxGround { Field, Face };

inline GuiColor modal_box_ground_color(ModalBoxGround g) {
    return g == ModalBoxGround::Field ? palette().field_ground
                                      : palette().face;
}

// THE BOX'S DIALOG MEMBERS STAND (architect 2026-10-14): A PROMPT — over any
// surface, the render player's and the picker's lists included, its question
// taking the box for as long as it is asked — A DIALOG EDITOR
// (AppState::dialog_editor_session's set, the settings editor and the BPM
// bracket editor) or THE PROGRESS MODAL (AppState::BatchProgress, a batch's
// "Rendering X of Y..." from its Render press to its terminal). Their box is
// paint_modal_dialog's; the folder overlay's two contents paint their own
// (paint_folder_overlay). (The taskbar dialog's standing predicate,
// chrome_tenant_owns_bottom_row, 2026-10-10 to 2026-10-14, named the prompt
// and the editors less a prompt over the player or the picker, which stood
// on that member's controls row.)
inline bool modal_box_dialog_stands(const AppState& a) {
    return a.prompt.active || a.batch_progress.active ||
           a.dialog_editor_session() != 0;
}

// DOES ANY MEMBER'S BOX STAND — the folder overlay's or a dialog's. The
// waveform's painted rect is empty while it does (onscreen_keyboard::
// waveform_paint_area): the box covers the canvas column whole.
inline bool modal_box_stands(const AppState& a) {
    return folder_overlay_stands(a) || modal_box_dialog_stands(a);
}

// THE BORDER UNDER ROW 8 AS THE MODAL STANDS IT (item 4): the lane itself
// (bottom_border_area) on the window's foot, or, with the keyboard under the
// stack, the same six rows lifted onto the keyboard's band.
inline GuiRect modal_border_rect(const AppState& a, int keyboard_h) {
    const GuiRect border = bottom_border_area(a);
    if (keyboard_h <= 0) return border;
    return GuiRect{border.x, border.y - keyboard_h, border.w, border.h};
}

// THE CONTROLS ROW (item 3): row 8's content height standing on the border
// under row 8, across the lane (the program lanes' own x and width,
// bottom_row_area's, the window's since 2026-10-21). Without the keyboard its rows
// are row 8's content band's; its seats are the box's outer columns
// (modal_box_rect's x and right edge), never row 8's pads.
inline GuiRect modal_controls_row_rect(const AppState& a, int keyboard_h) {
    const GuiRect lane   = bottom_row_area(a);
    const GuiRect border = modal_border_rect(a, keyboard_h);
    const int     h      = bottom_row_content_h_px();
    if (lane.w <= 0 || h <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{lane.x, border.y - h, lane.w, h};
}

// THE BOX (item 1), the ring's outer rect: the canvas column's columns
// across, the view bar's top row down to the foot's top (items 2 and 3 taken
// from the controls row up). A zero rect on a degenerate stack.
inline GuiRect modal_box_rect(const AppState& a, int keyboard_h) {
    const GuiRect area  = waveform_area(a);
    const GuiRect trim  = top_trim_row_area(a);
    const GuiRect row   = modal_controls_row_rect(a, keyboard_h);
    const int     lw    = program_line_px();
    const int     y1    = row.y - column_foot_face_h_px();
    const GuiRect box{area.x - lw, trim.y, area.w + 2 * lw, y1 - trim.y};
    if (area.w <= 0 || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
    return box;
}

// THE BOX'S INTERIOR: inside the ring, one program line in on each side —
// the member's ground (ModalBoxGround) and the rect its content clips to.
inline GuiRect modal_box_interior_rect(const AppState& a, int keyboard_h) {
    const GuiRect box = modal_box_rect(a, keyboard_h);
    const int     lw  = program_line_px();
    if (box.w <= 2 * lw || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
    return GuiRect{box.x + lw, box.y + lw, box.w - 2 * lw, box.h - 2 * lw};
}

// THE PANEL: everything the modal takes ABOVE the controls row — the box,
// the face margins beside it and the foot under it — across the lane, from
// the view bar's top row to the controls row's top. The ground a member's
// painter lays (paint_modal_box), the rect its content's damage erases and
// the outer claim of a press on the box.
inline GuiRect modal_panel_rect(const AppState& a, int keyboard_h) {
    const GuiRect box = modal_box_rect(a, keyboard_h);
    const GuiRect row = modal_controls_row_rect(a, keyboard_h);
    if (box.h <= 0 || row.w <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{row.x, box.y, row.w, row.y - box.y};
}

// THE WHOLE AREA A MODAL TAKES: the panel and the controls row under it —
// and, with the keyboard under the stack, the lifted border under them (the
// keyboard's band below is its own). The slot's show and hide damage
// (onscreen_keyboard::slot_damage_rect) and a dialog's surface
// (ModalDialogGeometry::box).
inline GuiRect modal_area_rect(const AppState& a, int keyboard_h) {
    const GuiRect panel = modal_panel_rect(a, keyboard_h);
    const GuiRect row   = modal_controls_row_rect(a, keyboard_h);
    const GuiRect lift  = keyboard_h > 0 ? modal_border_rect(a, keyboard_h)
                                         : GuiRect{0, 0, 0, 0};
    const int     y0    = panel.h > 0 ? panel.y : row.y;
    const int     y1    = lift.h > 0 ? lift.y + lift.h : row.y + row.h;
    if (row.w <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{row.x, y0, row.w, std::max(0, y1 - y0)};
}

// THE TEXT INSIDE THE BOX (architect 2026-10-14, the prompts' question and
// the dialog editors' line; mock_modal_settings_osk_v2.png's x 45 at 300 %):
// its pen 7 W in from the interior's left (and its wrap the same 7 W short
// of the interior's right) — Windows' edit-control inset, the sunken edge's
// two lines and the retired dialog field's 5-W pad, the inset the settings
// choice combo's text took until its removal the same day — on
// THE LIST'S ROWS: the first row the folder overlay's first row
// (folder_overlay.h's row box, 17 W at its 1-W pad under the ring), each
// further row one row pitch (the row and its 1-W gap) below, the cap centred
// on each row's band (redesign_baseline).
inline constexpr double kModalBoxTextInsetPx = 7.0;
inline int modal_box_text_inset_px() {
    return scaled_px(kModalBoxTextInsetPx);
}
