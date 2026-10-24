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
//      the 23-W case, the strip pad (render.h's strip_h_px) — on the ground,
//      no line, bar or border, its outermost cases or buttons FLUSH WITH THE
//      RING'S OUTER EDGES; on the window's foot.
// IN THE HI-FI SKIN (architect 2026-10-23, pv_41): THE PANEL'S MARGINS AND
// THE CONTROLS ROW are the brushed ground (materials.h's GROUND); THE BOX IS
// ONE SUNKEN FLAT FIELD, kFieldGround, under a ONE-DEVICE-PX RING in the
// ring tones on its outer edge (render.h's line rule) — every member's, a
// prompt's question included ("the user shouldn't be typing on a non-flat
// color"); EVERY BUTTON A SQUARE KEY with its legend under it, the word
// buttons' words in capitals (paint_handler.cpp's modal_word_buttons and
// paint_key_legend) (paint_modal_box). (The wood, the ebony word buttons and
// a prompt's face interior, 2026-10-22 to 2026-10-23, are git history.)
// SO THE BOX'S BOTTOM IS DERIVED FROM THE BOTTOM UP (the controls row), and
// it lands on the main window's ring: the box IS the ring's rect. At the
// tablet's 2304 x 1440, 300 % and the strip pad's 13 W: the ring's top slot
// 187–189 (the view bar's, directly under the band's rows 40–186 below the
// 40-row caption), its bottom slot 1290–1292, its column slots 21–23 and
// 2280–2282, the one-device-px ring on the slots' outer edges (rows 187 and
// 1292, columns 21 and 2282), the interior x 22..2281 and y 188..1291 (THE
// RING IS THE ONLY MARGIN, below; the main canvas's own rows 301–1289); the
// controls row 1293–1439, its case rows 1332–1400, its first case's columns
// 21–89 and its last 2214–2282. A DIALOG'S TEXT (THE TEXT INSIDE THE BOX,
// below) 12 px from the ring on both axes: its pen on column 33, its
// ascender line on row 199, its first baseline at y 215 (the "l" 15.9 px
// tall), its wrap ending one past column 2270.
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
// 929–997), the ring's bottom slot 887–889, the interior y 188..888.
//
// THE INTERIOR IS ONE FLAT FIELD FOR EVERY MEMBER (the hi-fi skin, above):
// the lists, a dialog editor's line, a prompt's question and the progress
// modal's sentence alike. (The Field / Face choice by what the surface is,
// ModalBoxGround, 2026-10-14 to 2026-10-23, is git history.)
//
// Geometry here, every rect a fact that asks no standing, and the box's two
// standing predicates beside it (modal_box_dialog_stands, modal_box_stands);
// the painter is paint_modal_box (paint_handler.cpp), and every paint site
// and every hit site of a member reads these rects (on screen is as
// painted).
#include "app_state.h"
#include "render.h"

#include <algorithm>

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
// stack, the same rows lifted onto the keyboard's band. Its seats are row
// 8's (paint_handler.cpp's bottom_row_seats, the one owner): the ring's
// outer columns, the box's x and right edge (modal_box_rect's).
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

// THE RING IS THE ONLY MARGIN (architect 2026-10-23, on the installed
// player: "the amount of spacing there is between the selection, the left
// edge, top edge, and the frame itself … at most one device pixel, since the
// bevel is one pixel. It looks like there's too much space around the
// selection row"; and "if you're going to remove padding from the player,
// then remove a similar amount of padding from the modal text editor, like
// the settings editor"). THE INTERIOR is the box inside its ONE-DEVICE-PX
// ring (hairline_px, the line the painter draws on the slot's outer edge —
// the slot's other rows the field like the interior), and the lists' rows
// stand ONE DEVICE PX OF AIR inside that (modal_box_content_pad_px) on the
// left, the top and the right, so the lit row's bar reaches to one px
// from the ring's columns and the first row stands one px under its top row;
// a dialog's text — a prompt's question, a dialog editor's line — keeps its
// own pad, 12 px at 300 % (THE TEXT INSIDE THE BOX, below). (The interior
// one program line in, its 3-px slot at 300 %, with the list's 1-W pad and
// the text's 7-W inset inside it — 6 px of air round the lit row, 24 before
// the text — stood 2026-10-14 to 2026-10-23.)
inline GuiRect modal_box_interior_rect(const AppState& a, int keyboard_h) {
    const GuiRect box = modal_box_rect(a, keyboard_h);
    const int     lw  = hairline_px();
    if (box.w <= 2 * lw || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
    return GuiRect{box.x + lw, box.y + lw, box.w - 2 * lw, box.h - 2 * lw};
}
// THE CONTENT'S AIR inside the interior (THE RING IS THE ONLY MARGIN,
// above): one device px at every scale, never scaled — it is measured
// against the one-px ring.
inline int modal_box_content_pad_px() { return 1; }

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

// THE TEXT INSIDE THE BOX — a prompt's question, the progress modal's
// sentence and the dialog editors' line (architect 2026-10-14; the pad
// 2026-10-23, on his capture at 300 %, tmp/theme_next/pressed1/pk_cap_05.png:
// the ink 5 px right of the ring and 18 under it — "it looks odd because the
// horizontal padding is very small, but the vertical padding is large"; his
// pick S3, the clock fields' pad): THE INK 12 PX FROM THE RING ON BOTH AXES
// at 300 %, scaled with gui_scale through skin_px (kModalBoxTextPadAt300,
// the clock fields' kTimeFieldPadAt300 the same number by the same road, the
// ring counted in it as the time field counts its own): THE PEN on the ring's
// left column plus the pad, THE WRAP the same pad short of the ring's right
// column, and THE FIRST ROW'S ASCENDER LINE — the face's own "l" ink top,
// gui_font_ascender_ink_px, never the line box's top: the air he sees is the
// ink's — on the ring's top row plus the pad (modal_box_text_baseline0), each
// further row one list row pitch below (the row and its 1-W gap). 12 px at
// 300 %, 6 at 138 %, 14 at 360 %. THE LISTS (the project picker, the render
// player) KEEP THEIR ROWS ("the player's rows look good"): the folder
// overlay's row box, 17 W, at the content's one px under the ring, the cap
// centred on each row's band (redesign_baseline). (The 7-W inset of
// 2026-10-14 — Windows' edit-control inset, mock_modal_settings_osk_v2.png's
// x 45 at 300 % — and the 1-W pad past the content's one px of air that
// replaced it the morning of 2026-10-23, kModalBoxTextPadPx, its first row
// the list's, are retired.)
inline constexpr double kModalBoxTextPadAt300 = 12.0;
inline int modal_box_text_pad_px() {
    return skin_px(kModalBoxTextPadAt300, 1);
}
// THE TEXT'S ROOM in the box `box` (modal_box_rect): x the pen's column, w
// the soft wrap's width (the box less the pad on each side), zero on a box
// too narrow to hold it; y and h the interior's rows, the text's clip.
inline GuiRect modal_box_text_room(const GuiRect& box) {
    const int pad = modal_box_text_pad_px();
    const int lw  = hairline_px();
    if (box.w <= 2 * pad || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
    return GuiRect{box.x + pad, box.y + lw, box.w - 2 * pad, box.h - 2 * lw};
}
// THE FIRST ROW'S BASELINE in the box `box`: the ring's top row plus the pad
// is the ascender line, the baseline the ascender's own height under it,
// rounded at the element.
inline double modal_box_text_baseline0(const GuiFont& font, const GuiRect& box) {
    return static_cast<double>(box.y + modal_box_text_pad_px()) +
           std::nearbyint(gui_font_ascender_ink_px(font));
}
