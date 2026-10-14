#pragma once

// THE MODAL FAMILY'S SHAPE — ONE OWNER OF THE GEOMETRY EVERY MODAL READS
// (architect 2026-10-14, on his mocks over tablet captures,
// tmp/modal_arc/mock_modal_player_v2.png and mock_modal_picker.png: "overlays/
// modals can take over the area from the trim bar on down, with just the
// bottom new border remaining"; "use a similar bevel inset as the main GUI
// window, so the modal/overlay isn't flush against the border"). The render
// player and the project picker stand in it since that day (the folder
// overlay's two contents, folder_overlay.h); the prompts, the dialog editors,
// the keyboard and the progress modal are to follow (until then the taskbar
// dialog, onscreen_keyboard.h's bottom-overlays block, carries them).
//
// A MODAL TAKES THE AREA FROM THE VIEW BAR'S TOP ROW DOWN: the top strip
// above it and THE COLUMN'S 5 W AIR under the band stay as the main window
// paints them; the border under row 8 and the program frame's bottom row stay
// visible under it. Across, it runs inside the program frame's two columns,
// as every program lane does. Top to bottom:
//   1. ONE SUNKEN BOX wearing THE CANVAS COLUMN'S RING — exactly
//      paint_canvas_column_frame's: one program line, `ce_dark` top and left,
//      `ce_hilight` right and bottom, mitred — standing in the canvas
//      column's own columns (the column's side margin of face in from each
//      side, the waveform area's x and width one line out), its top row the
//      view bar's top row;
//   2. under the box's bottom light row, THE MAIN WINDOW'S 5 W FOOT of face
//      (column_foot_face_h_px);
//   3. THE CONTROLS ROW: row 8's content height (4 W of air, the 23-W case,
//      5 W of air) on the face, no grippers, no end bars and NO DOCK BAR
//      ABOVE IT (the dock bar's six rows go to the box), its outermost cases
//      or buttons FLUSH WITH THE RING'S OUTER EDGES;
//   4. then the border under row 8 and the program frame's bottom row,
//      unchanged.
// SO THE BOX'S BOTTOM IS DERIVED FROM THE BOTTOM UP (the frame's line, the
// border, the controls row, the foot), and it lands 6 W below the main
// window's canvas bottom: the mock's picture, intended. At the tablet's
// 2304 x 1440 and 300 %: the ring's top row 231–233 and bottom row
// 1305–1307, its columns 21–23 and 2280–2282, the interior x 24..2279 and
// y 234..1304 (the main canvas's own rows end at 1286, its bottom frame row
// 1287–1289); the foot 1308–1322; the controls row 1323–1418, its case rows
// 1335–1403, its first case's columns 21–89 and its last 2214–2282; the
// border 1419–1436 and the frame's line 1437–1439.
//
// THE INTERIOR'S GROUND SAYS WHAT THE SURFACE IS (ModalBoxGround): THE FIELD
// GROUND, the list's dark field pair, where you pick — the render player's
// and the picker's lists; THE FACE where you only read.
//
// Geometry only here, every rect a fact that asks no standing; the painter
// is paint_modal_box (paint_handler.cpp), and every paint site and every hit
// site of a member reads these rects (on screen is as painted).
#include "app_state.h"
#include "render.h"

// THE INTERIOR'S GROUND (the head): which of the two the member's surface is.
enum class ModalBoxGround { Field, Face };

inline GuiColor modal_box_ground_color(ModalBoxGround g) {
    return g == ModalBoxGround::Field ? palette().field_ground
                                      : palette().face;
}

// THE CONTROLS ROW (item 3): row 8's content height standing on the border
// under row 8, across the lane one frame line in from each side (the program
// lanes' own x and width, bottom_row_area's). Its rows are row 8's content
// band's; its seats are the box's outer columns (modal_box_rect's x and
// right edge), never row 8's pads.
inline GuiRect modal_controls_row_rect(const AppState& a) {
    const GuiRect lane   = bottom_row_area(a);
    const GuiRect border = bottom_border_area(a);
    const int     h      = bottom_row_content_h_px();
    if (lane.w <= 0 || h <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{lane.x, border.y - h, lane.w, h};
}

// THE BOX (item 1), the ring's outer rect: the canvas column's columns
// across, the view bar's top row down to the foot's top (items 2 and 3 taken
// from the controls row up). A zero rect on a degenerate stack.
inline GuiRect modal_box_rect(const AppState& a) {
    const GuiRect area  = waveform_area(a);
    const GuiRect trim  = top_trim_row_area(a);
    const GuiRect row   = modal_controls_row_rect(a);
    const int     lw    = program_line_px();
    const int     y1    = row.y - column_foot_face_h_px();
    const GuiRect box{area.x - lw, trim.y, area.w + 2 * lw, y1 - trim.y};
    if (area.w <= 0 || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
    return box;
}

// THE BOX'S INTERIOR: inside the ring, one program line in on each side —
// the member's ground (ModalBoxGround) and the rect its content clips to.
inline GuiRect modal_box_interior_rect(const AppState& a) {
    const GuiRect box = modal_box_rect(a);
    const int     lw  = program_line_px();
    if (box.w <= 2 * lw || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
    return GuiRect{box.x + lw, box.y + lw, box.w - 2 * lw, box.h - 2 * lw};
}

// THE PANEL: everything the modal takes ABOVE the controls row — the box,
// the face margins beside it and the foot under it — across the lane, from
// the view bar's top row to the controls row's top. The ground a member's
// painter lays (paint_modal_box), the rect its content's damage erases and
// the outer claim of a press on the box.
inline GuiRect modal_panel_rect(const AppState& a) {
    const GuiRect box = modal_box_rect(a);
    const GuiRect row = modal_controls_row_rect(a);
    if (box.h <= 0 || row.w <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{row.x, box.y, row.w, row.y - box.y};
}

// THE WHOLE AREA A MODAL TAKES: the panel and the controls row under it —
// the slot's show and hide damage (onscreen_keyboard::slot_damage_rect).
inline GuiRect modal_area_rect(const AppState& a) {
    const GuiRect panel = modal_panel_rect(a);
    const GuiRect row   = modal_controls_row_rect(a);
    if (panel.h <= 0) return row;
    return GuiRect{row.x, panel.y, row.w, row.y + row.h - panel.y};
}
