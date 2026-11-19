#pragma once

// THE MODAL FAMILY'S SHAPE — ONE OWNER OF THE GEOMETRY EVERY MODAL READS
// (architect 2026-10-14, on his mocks over tablet captures,
// the player and the picker: "overlays/
// modals can take over the area from the trim bar on down, with just the
// bottom new border remaining"). THE BOX STANDS FOR THE OVERLAYS: the render
// player and the project picker (the folder overlay's contents,
// folder_overlay.h, with the settings page, the value list, the trim presets
// and the BPM form it swaps in) and THE SCORE VIEWER (since 2026-10-26,
// score_viewer.h, its page the box's interior) — and, as THE ROOM ABOVE THE
// ON-SCREEN KEYBOARD, for a DIALOG EDITOR (below).
//
// (INTERFACE VERSUS MODAL, architect 2026-10-14: the player and the picker
// stand in the box but are interface, so what they leave dead greys; the
// prompts, editors and progress modal are modal and grey nothing — the rule
// is recorded at redesign_button_enabled's head, app_state.h.)
//
// THE BOX TAKES THE VIEW (architect 2026-11-18 / 2026-11-19, send Z;
// tmp/sendY/P2_design_overlays.md §A): the title bar and the top icon row
// above it stay as the main window paints them. Across, it runs the panel's
// width (render.h's well_columns, edge to edge since send Z). Top to bottom:
//   1. THE BOX — its first rows THE LINE UNDER THE TOP ICON ROW (the trim
//      lane's first rows), its last THE LINE OVER THE BOTTOM ICON ROW (the
//      well's foot), both in the line tone `well_border`, and between them
//      THE VIEW, the box's interior, one flat fill — the canvas's tone
//      `ruler_face` (Arc's $base_color, Thunar's list view) for the lists and
//      the dialog editor's room, the score viewer's `score_pane_face` its one
//      exception (below); no frame, no ring, no shadow (paint_modal_box,
//      paint_handler.cpp);
//   2. directly under it THE CONTROLS ROW, THE BOTTOM ICON ROW'S OWN ROWS —
//      the inner pad 8 W, the key (key_width wide and key_height tall,
//      render.h's icon_case_w_px / icon_case_h_px), the outer pad 7 W
//      (strip_h_px) — on the body tone, its outermost BOXES 6 W INSIDE THE
//      PANEL (render.h's kEdgeInsetW; materials.h's THE KEY); EMPTY BODY TONE
//      under a question, a dialog editor and the progress modal (send AA's
//      stage D2, 2026-11-19: their answers are the dialog's own buttons);
//   3. under both, THE STATUS BAR on the window's foot, never covered
//      (render.h's status bar block: it keeps its process line under every
//      modal and drops its selection count).
// At the tablet's 2304 x 1440, 200 % and the keys' defaults (key_width 36,
// key_height 28; the 56-row title bar): the box's line rows 142–143 and
// 1306–1307, THE VIEW x 0..2303 and y 144..1305 (1162 rows); the controls
// row 1308–1393, its face rows 1324–1379, its first face's columns 12–83 and
// its last 2220–2291; the status bar 1394–1439 under it.
//
// THE DIALOGS ARE XFWM'S WINDOWS, CENTERED (architect 2026-11-19, send Z,
// tmp/sendZ/RULINGS.md §5: "Center looks a lot neater and tidier, and it's
// what I'd expect from a modal"; his "keep" on Z_08's true shadow; send
// AA's stage D2): A QUESTION (every prompt) — GTK's message dialog —, A
// DIALOG EDITOR — Thunar's rename dialog — and THE PROGRESS MODAL —
// Thunar's progress dialog — each stand in a window of xfwm's frame (the 28
// W title bar, 4 W borders) with xfwm's own frame shadow under it, floating
// over whatever stands: a question and the progress modal over the main page
// or the standing overlay (the overlay under its own question stays
// standing, its controls row empty), CENTERED ON THE PANEL; a dialog editor
// CENTERED IN THE ROOM ABOVE THE KEYBOARD (below). The window, its client and
// its seats are paint_handler.cpp's (paint_modal_dialog, THE DIALOG WINDOW);
// they take no rect of this file but the room.
//
// THE ON-SCREEN KEYBOARD STANDS AT THE VIEW'S FOOT UNDER A DIALOG EDITOR
// (architect 2026-11-19, send Z: his approval of tmp/sendZ/VIEW/Z_06; the
// design's §E; onscreen_keyboard::surface_rect): its top line and its band
// directly on the line over the bottom icon row, the bottom icon row under
// it EMPTY body tone; THE BOX STANDS ABOVE IT AS THE ROOM — its last rows
// the keyboard's top line, its view the room the editor's window is
// centered in, the page the editor was opened from painted there as it was
// left (the dialog editor's backdrop, AppState::dialog_backdrop). Every rect
// below takes `keyboard_h`, the keyboard's height (its line and its band)
// when it stands under a dialog editor and 0 otherwise
// (onscreen_keyboard::modal_keyboard_h_px; the folder overlay never stands
// with the keyboard, the record at onscreen_keyboard::stands, and passes 0).
// On the tablet at key_height 28 and his keyboard tuning, osk_key_w 24 and
// osk_key_h 35 (48 x 70 device px): the keyboard's line 907–908 and band
// 909–1305 (onscreen_keyboard.h's arithmetic), the box 142–908 and THE ROOM
// y 144..906.
//
// Geometry here, every rect a fact that asks no standing, and the box's two
// standing predicates beside it (modal_box_dialog_stands, modal_box_stands);
// the painter is paint_modal_box (paint_handler.cpp), and every paint site
// and every hit site of a member reads these rects (on screen is as
// painted).
#include "app_state.h"
#include "materials.h"      // the flat look's rules
#include "render.h"         // palette(): `ruler_face`, `score_pane_face`

#include <algorithm>
#include <cmath>
#include <cstdint>

// THE BOX STANDS AS A DIALOG EDITOR'S ROOM (send AA's stage D2, 2026-11-19):
// while A DIALOG EDITOR stands (AppState::dialog_editor_session's set, the
// settings editor's buffer in its three kinds). A QUESTION AND THE PROGRESS
// MODAL TAKE NO BOX since that day: their windows float over what stands.
// (Every prompt, every dialog editor and the progress modal took the box's
// one flat pane from 2026-10-14 until send AA's stage D2; the taskbar
// dialog's standing predicate, chrome_tenant_owns_bottom_row, 2026-10-10 to
// 2026-10-14, named the prompt and the editors.)
inline bool modal_box_dialog_stands(const AppState& a) {
    return a.dialog_editor_session() != 0;
}

// DOES ANY MEMBER'S BOX STAND — the folder overlay's, a dialog editor's room
// or, since 2026-10-26, THE SCORE VIEWER'S (score_viewer.h: the page in the
// interior, paint_modal_dialog's score branch). The waveform's painted rect
// is empty while it does (onscreen_keyboard::waveform_paint_area): the box
// covers the canvas column whole.
inline bool modal_box_stands(const AppState& a) {
    return folder_overlay_stands(a) || modal_box_dialog_stands(a) ||
           score_viewer_stands(a);
}

// THE CONTROLS ROW (item 2): the bottom icon row's own rows, the lane itself
// (bottom_row_area) — IT NEVER LIFTS since send AA's stage D2 (the keyboard
// stands above it, at the view's foot, onscreen_keyboard.h): `keyboard_h`
// is unread, the parameter kept for the family's one signature. Its seats
// are the bottom icon row's (paint_handler.cpp's bottom_row_seats, the one
// owner).
inline GuiRect modal_controls_row_rect(const AppState& a, int keyboard_h) {
    (void)keyboard_h;
    const GuiRect lane = bottom_row_area(a);
    if (lane.w <= 0 || lane.h <= 0) return GuiRect{0, 0, 0, 0};
    return lane;
}

// THE BOX (item 1), its outer rect: across, THE PANEL'S COLUMNS (render.h's
// well_columns, the panel's own since 2026-11-19, send Z: edge to edge);
// down, the trim lane's first row (the line under the top icon row) to the
// controls row's top (its last rows the line over the bottom icon row) — or,
// WITH THE KEYBOARD STANDING (`keyboard_h` > 0), to one line under the
// keyboard's top, so the box's last rows ARE THE KEYBOARD'S TOP LINE
// (onscreen_keyboard::surface_rect's first rows, the same tone painted by
// both). A zero rect on a degenerate stack.
inline GuiRect modal_box_rect(const AppState& a, int keyboard_h) {
    const WellColumns well = well_columns(a);
    const GuiRect trim  = top_trim_row_area(a);
    const GuiRect row   = modal_controls_row_rect(a, keyboard_h);
    const int     lw    = hairline_px();   // the box's two line rows
    const int     y1    = keyboard_h > 0
                              ? well_foot_area(a).y - keyboard_h + lw
                              : row.y;
    const GuiRect box{well.x, trim.y, well.end - well.x, y1 - trim.y};
    if (box.w <= 2 * lw || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
    return box;
}

// THE VIEW (architect 2026-11-19, send Z; send AA's stage D2): THE INTERIOR
// is the box less ONE HAIRLINE above and below (hairline_px: 2 device px on
// the tablet at 200 %, 1 at 100 %; those rows the box's lines), across the
// panel's own columns, 0 .. 2303 on the tablet — the lists' rows stand on
// it edge to edge (folder_overlay.h), the score viewer's page in it, and a
// dialog editor's window centered in it (the room).
inline GuiRect modal_box_interior_rect(const AppState& a, int keyboard_h) {
    const GuiRect box = modal_box_rect(a, keyboard_h);
    const int     lw  = hairline_px();
    if (box.w <= 2 * lw || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
    return GuiRect{box.x, box.y + lw, box.w, box.h - 2 * lw};
}

// THE SCORE VIEWER'S PANE — THE ONE DELIBERATE EXCEPTION to the view's tone
// (architect 2026-11-01 night, M9 A2, "THE SCORE VIEWER IS
// SETTLED": "not lighter, preferably from ableton palette directly"): the
// palette's `score_pane_face`, in every theme THE WAVEFORM'S INK (Default's
// 191919, Live's ink; theme_roles.h's derivation), the one tone reused.
// THE REASON: the score's page is a picture on a dark mat, not a list field.
// The GPU's field round the page (score_viewer.cpp's
// score_viewer_publish_gpu_page) and the CPU's pane under the score
// viewer's box (paint_modal_dialog's call of paint_modal_box, for a frame
// with no page hole — a question over the viewer among them) both read
// palette().score_pane_face; the page's fit and centering are untouched.

// THE PANEL: everything the modal takes ABOVE the controls row (or above the
// keyboard) — THE BOX itself, the panel's whole width since send Z (the
// ground margins beside it retired with the well's frame). The fill a
// member's painter lays (paint_modal_box), the rect its content's damage
// erases and the outer claim of a press on the box.
inline GuiRect modal_panel_rect(const AppState& a, int keyboard_h) {
    return modal_box_rect(a, keyboard_h);
}

// THE WHOLE AREA A MODAL TAKES: the box and the controls row under it (the
// keyboard between them, under a dialog editor, is its own band; at keyboard
// height 0 this area contains it). The slot's show and hide damage
// (onscreen_keyboard::slot_damage_rect) and the closers'
// (Viewport::invalidate_modal_dialog_area).
inline GuiRect modal_area_rect(const AppState& a, int keyboard_h) {
    const GuiRect panel = modal_panel_rect(a, keyboard_h);
    const GuiRect row   = modal_controls_row_rect(a, keyboard_h);
    const int     y0    = panel.h > 0 ? panel.y : row.y;
    const int     y1    = row.y + row.h;
    if (row.w <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{row.x, y0, row.w, std::max(0, y1 - y0)};
}

// XFWM'S FRAME SHADOW'S GEOMETRY (send AA's stage D2, 2026-11-19; the
// dialogs' window, paint_handler.cpp's THE DIALOG WINDOW, owns the rule and
// the paint): SHADOW_RADIUS kXfwmShadowRadiusW (12 W, xfwm4's compositor.c),
// the window's rect grown kXfwmShadowDeltaYW (his shadow_delta_y, −3 W:
// grown 3 W upward). Here, beside the box, because the damage reads the
// reach too (Viewport::invalidate_dialog_editor_box: the room grown by it,
// before an editor's first paint).
inline constexpr double kXfwmShadowRadiusW = 12.0;
inline constexpr double kXfwmShadowDeltaYW = -3.0;
// THE SHADOW'S LENGTHS in device px: r (SHADOW_RADIUS), the kernel's size
// (make_gaussian_map's), the vertical delta and the mask's origin off the
// window's (SHADOW_OFFSET + the delta, C's integer division).
struct XfwmShadowLengths {
    int r = 0, size = 0, delta_y = 0, dx = 0, dy = 0;
};
inline XfwmShadowLengths xfwm_shadow_lengths() {
    XfwmShadowLengths l;
    l.r       = scaled_px(kXfwmShadowRadiusW, 1);
    l.size    = (static_cast<int>(std::ceil(static_cast<double>(l.r) * 3.0)) + 1) & ~1;
    l.delta_y = scaled_px(kXfwmShadowDeltaYW);
    const int off = (-3 * l.r) / 2;
    l.dx = off;
    l.dy = off + l.delta_y;
    return l;
}
// THE MASK'S RECT for the window `win`: make_shadow's swidth x sheight at
// the shadow's origin — every pixel the shadow may touch.
inline GuiRect xfwm_shadow_rect(const GuiRect& win) {
    const XfwmShadowLengths l = xfwm_shadow_lengths();
    return GuiRect{win.x + l.dx, win.y + l.dy, win.w + l.size,
                   win.h + l.size - l.delta_y};
}

// (The lists stand edge to edge on the view, and every dialog's text in
// its own window, paint_handler.cpp's THE DIALOG WINDOW.)
