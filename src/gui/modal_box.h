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
// A MODAL TAKES THE AREA FROM THE VIEW BAR'S TOP ROW DOWN (the simple
// layout, architect 2026-10-22, on the mocks tmp/theme_next/out/screens/,
// "Yes, this all looks good"): the caption and the toolbar band above it
// stay as the main window paints them. Across, it runs the window's width,
// as every program lane does. Top to bottom:
//   1. ONE SUNKEN BOX WEARING THE PROGRAM RING — it stands where the ring
//      stands: one program line, `ce_dark` top and left, `ce_hilight` right
//      and bottom, mitred, exactly paint_canvas_column_frame's — in the
//      canvas column's own columns (the column's side margin of wood in from
//      each side, the waveform area's x and width one line out), its top row
//      the view bar's top row, its bottom row the ring's last row;
//   2. directly under it THE CONTROLS ROW, ROW 8'S OWN ROWS — the strip pad,
//      the 23-W case, the strip pad (render.h's strip_h_px) — on the wood,
//      no line, bar or border, its outermost cases or buttons FLUSH WITH THE
//      RING'S OUTER EDGES; on the window's foot.
// THE PANEL AND THE CONTROLS ROW ARE THE WOOD AS DRAWN (architect
// 2026-10-22, "just make the rest wood": materials.h's WOOD); THE FIELDS
// STAY FLAT ("the user shouldn't be typing on a non-flat color") — a dialog
// editor's line, the lists' ground; THE WORD BUTTONS ARE THE EBONY CASE
// (architect 2026-10-22, "buttons in ebony — yes": the band's case on each
// button's own box, its word in the chrome's one ink — materials.h's CASE
// and INKS); a prompt's question stands on the face, the wood
// (paint_modal_box).
// SO THE BOX'S BOTTOM IS DERIVED FROM THE BOTTOM UP (the controls row), and
// it lands on the main window's ring: the box IS the ring's rect. At the
// tablet's 2304 x 1440, 300 % and the strip pad's 13 W: the ring's top row
// 201–203 (the view bar's, directly under the band's rows 54–200), its
// bottom row 1290–1292, its columns 21–23 and 2280–2282, the interior x
// 24..2279 and y 204..1289 (the main canvas's own rows 315–1289); the
// controls row 1293–1439, its case rows 1332–1400, its first case's columns
// 21–89 and its last 2214–2282.
//
// THE ON-SCREEN KEYBOARD STANDS UNDER THE STACK (architect 2026-10-14,
// mock_modal_settings_osk_v2.png; the spacing 2026-10-22): when a dialog
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
// implemented before"): the strip pad under the row's cases plus the
// keyboard band's own edge pad above its first key row — by construction,
// the row standing directly on the band with nothing between. At 300 %: 39
// + 14 = 53 device rows (the row's cases end on 997, the first key row
// starts on 1051), where the dock bar's border under the row made it 15 +
// 18 + 14 = 47 until then. THE ROWS: the keyboard's band 1037–1439 (its
// first key row's top 1051), the controls row 890–1036 (its case rows
// 929–997), the ring's bottom row 887–889, the interior y 204..886.
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

// THE CONTROLS ROW (item 2): row 8's own rows — the lane itself
// (bottom_row_area) on the window's foot, or, with the keyboard under the
// stack, the same rows lifted onto the keyboard's band. Its seats are the
// box's outer columns (modal_box_rect's x and right edge, the ring's outer
// columns), never row 8's leads in from them.
inline GuiRect modal_controls_row_rect(const AppState& a, int keyboard_h) {
    const GuiRect lane = bottom_row_area(a);
    if (lane.w <= 0 || lane.h <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{lane.x, lane.y - std::max(0, keyboard_h), lane.w, lane.h};
}

// THE BOX (item 1), the ring's outer rect: the program ring's outer
// columns across (render.h's program_ring_columns), the view bar's top row
// down to the controls row's top (the ring's last row the row above it). A
// zero rect on a degenerate stack.
inline GuiRect modal_box_rect(const AppState& a, int keyboard_h) {
    const ProgramRingColumns ring = program_ring_columns(a);
    const GuiRect trim  = top_trim_row_area(a);
    const GuiRect row   = modal_controls_row_rect(a, keyboard_h);
    const int     lw    = program_line_px();
    const int     y1    = row.y;
    const GuiRect box{ring.x, trim.y, ring.end - ring.x, y1 - trim.y};
    if (box.w <= 2 * lw || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
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

// THE PANEL: everything the modal takes ABOVE the controls row — the box
// and the wood margins beside it — across the lane, from
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
