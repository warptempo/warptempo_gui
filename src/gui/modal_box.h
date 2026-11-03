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
//   1. THE BOX — in the well's own columns (the side margin of the ground in
//      from each side, the waveform area's x and width: render.h's
//      well_columns, the keys' faces' outer columns), its
//      top row the trim lane's first row, its bottom row the well's foot;
//   2. directly under it THE CONTROLS ROW, ROW 8'S OWN ROWS — the strip pad,
//      the cell (key_height tall, render.h's icon_case_h_px), the strip pad
//      (strip_h_px) — on the ground, no line, bar or border, the keys'
//      legends in its bottom pad, its outermost FACES FLUSH WITH THE BOX'S
//      OUTER COLUMNS (their cells one px past; materials.h's THE CELL AND
//      ITS FACE); on the window's foot.
// ONE GROUND, ONE PANE (architect 2026-11-01 night, M6 A — "the list pane
// with no frame" — for every member, M7 / M8 / M11 after it): THE PANEL'S
// MARGINS, THE CONTROLS ROW AND THE BOX'S OWN OUTER ROWS AND COLUMNS are the
// flat ground (materials.h's kGround); THE BOX'S INTERIOR — the box less one
// device px on each side — IS ONE FLAT PANE, materials.h's kPaneFace 363636
// (Live's panel face, the keys' face), with NO FRAME, NO RING, NO SHADOW AND
// NO ROW SEPARATOR, every member's alike: the lists, a prompt's question,
// the progress modal's sentence, a dialog editor's line ("the user
// shouldn't be typing on a non-flat color", 2026-10-23). THE ONE EXCEPTION
// IS THE SCORE VIEWER'S PANE, kScorePaneFace 191919 (below). EVERY BUTTON
// THE FLAT KEY with its glyph on it and its legend under it (2026-11-03), a
// word button's word its glyph's key, its legend in capitals and its
// tooltip (paint_handler.cpp's modal_word_buttons, modal_word_icon and
// paint_key_legend) (paint_modal_box).
// SO THE BOX'S BOTTOM IS DERIVED FROM THE BOTTOM UP (the controls row), and
// it lands on the well's rows of ground. At the tablet's 2304 x 1440, 300 %
// and key_height 33 (its default; the 69-px cell, the strip pad 39 and the
// 72-row logo lane, 2026-11-03): the box's top row 144 (the trim lane's
// first row, the row of ground directly under the band's rows 0–143, the
// logo lane 0–71), its bottom row 1328 (the well's foot) and its columns 24
// and 2279 (the well's first and last, the main canvas's own columns),
// those rows and columns ground, the PANE x 25..2278 and y 145..1327 (the
// main canvas's own rows 240–1327); the controls row 1329–1439, its cell
// rows 1368–1400 (the faces 1369–1399), its first cell's columns 23–91 (the
// face 24–90) and its last 2212–2280 (the face 2213–2279). A DIALOG'S TEXT
// (THE TEXT SEAT, below) on the list rows' seat, 18 px in from the box's
// outer edges: its pen on column 42, its ascender line on row 162, its wrap
// ending one past column 2261, its foot clip one past row 1310.
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
// implemented before"): the strip pad under the row's cases plus the
// keyboard band's own edge pad above its first key row — by construction,
// the row standing directly on the band with nothing between. At 300 %: 39
// + 14 = 53 device rows (at the defaults below the row's cells end on 1048
// and the first key row starts on 1102). THE ROWS, at key_height 33 and the
// keyboard's defaults, osk_key_w 48 and osk_key_h 60 (2026-10-29, the
// compact keyboard's band 352 rows): the keyboard's band 1088–1439 (its
// first key row's top 1102), the controls row 977–1087 (its cell rows
// 1016–1048), the box's bottom row 976 (ground), the pane y 145..975.
//
// Geometry here, every rect a fact that asks no standing, and the box's two
// standing predicates beside it (modal_box_dialog_stands, modal_box_stands);
// the painter is paint_modal_box (paint_handler.cpp), and every paint site
// and every hit site of a member reads these rects (on screen is as
// painted).
#include "app_state.h"
#include "materials.h"      // kPaneFace
#include "palette_file.h"   // kPaletteInk, the score viewer's pane
#include "render.h"

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
// 8's (paint_handler.cpp's bottom_row_seats, the one owner): the box's
// outer columns, its x and right edge (modal_box_rect's).
inline GuiRect modal_controls_row_rect(const AppState& a, int keyboard_h) {
    const GuiRect lane = bottom_row_area(a);
    if (lane.w <= 0 || lane.h <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{lane.x, lane.y - std::max(0, keyboard_h), lane.w, lane.h};
}

// THE BOX (item 1), its outer rect: the well's columns across (render.h's
// well_columns), the trim lane's first row down to the controls row's top
// (the box's last row the well's foot, the row above it). A zero rect on a
// degenerate stack.
inline GuiRect modal_box_rect(const AppState& a, int keyboard_h) {
    const WellColumns well = well_columns(a);
    const GuiRect trim  = top_trim_row_area(a);
    const GuiRect row   = modal_controls_row_rect(a, keyboard_h);
    const int     lw    = hairline_px();   // the ground round the pane
    const int     y1    = row.y;
    const GuiRect box{well.x, trim.y, well.end - well.x, y1 - trim.y};
    if (box.w <= 2 * lw || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
    return box;
}

// THE PANE (architect 2026-10-23, on the installed player: "the amount of
// spacing there is between the selection, the left edge, top edge, and the
// frame itself … at most one device pixel"; the frame's ring retired
// 2026-11-01 night, M6 A, its rows and columns become ground): THE INTERIOR
// is the box less ONE DEVICE PX on each side (hairline_px; the box's outer
// rows and columns the ground), the pane every member stands on, and the
// lists' rows stand ONE DEVICE PX OF AIR inside it
// (modal_box_content_pad_px) on the left, the top and the right, so the lit
// row's bar reaches to one px from the pane's edges and the first row
// stands one px under its top row; a dialog's text — a prompt's question, a
// dialog editor's line — stands on the list rows' text seat (THE TEXT SEAT,
// below).
inline GuiRect modal_box_interior_rect(const AppState& a, int keyboard_h) {
    const GuiRect box = modal_box_rect(a, keyboard_h);
    const int     lw  = hairline_px();
    if (box.w <= 2 * lw || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
    return GuiRect{box.x + lw, box.y + lw, box.w - 2 * lw, box.h - 2 * lw};
}
// THE CONTENT'S AIR inside the pane (THE PANE, above): one device px at
// every scale, never scaled — it is measured against the one-px edge.
inline int modal_box_content_pad_px() { return 1; }

// THE SCORE VIEWER'S PANE — THE ONE DELIBERATE EXCEPTION to kPaneFace
// (architect 2026-11-01 night, M9 A2, "THE SCORE VIEWER IS SETTLED": "not
// lighter, preferably from ableton palette directly"): 191919, Live's ink —
// the palette's canvas-ink tone, palette_file.h's kPaletteInk, the one
// constant reused. THE REASON: the score's page is a picture on a dark mat,
// not a list field. The GPU's field round the page (score_viewer.cpp's
// score_viewer_publish_gpu_page) and the CPU's pane under the score
// viewer's box (paint_modal_dialog's call of paint_modal_box, for a frame
// with no page hole) both read it; the page's fit and centering are
// untouched.
inline constexpr uint32_t kScorePaneFace = kPaletteInk;

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
// seat the line editor's and the settings rows'): kModalBoxTextSeatAt300,
// 18 DEVICE PX AT 300 % IN FROM THE BOX'S OUTER EDGE (modal_box_rect's, the
// ground's column and row round the pane; the seat that stands round the
// 22-px text, back with it 2026-11-03, the retrofit, after the text round's
// 24 of that morning), an AREA scaled with gui_scale through skin_px at the
// element (18 at 300 %, 12 at 200 %, 9 at 150 %).
// Its readers, which cannot drift apart:
//   THE BOX'S TEXT — a prompt's question, the progress modal's sentence and
//     the dialog editors' line, symmetric on all four sides: THE PEN the
//     seat in from the box's left column (column 42 at 300 %), THE WRAP
//     ending the seat short of its right column (one past 2261), THE FIRST
//     ROW'S ASCENDER LINE — the face's own "l" ink top,
//     gui_font_ascender_ink_px, never the line box's top: the air he sees is
//     the ink's — the seat under its top row (row 162; modal_box_text_
//     baseline0), each further row one list row pitch below (the row and
//     its 1-W gap), and THE FOOT the seat over its bottom row (the text
//     room's clip, one past row 1310);
//   THE LISTS' TEXT COLUMN — paint_handler.cpp's kListGlyphXAt300, the
//     glyph's first ink column in a listing with glyph rows and the names' column in a
//     glyph-less one (the settings page, the value lists, the trim presets,
//     the BPM form), the same seat in from the box's left column. (The
//     lists keep their rows: the folder overlay's row box, 51 px, at the
//     content's one px under the pane's top, the cap centred on each row's
//     band, redesign_baseline.)
// (A 12-px pad inside a one-px ring stood 2026-10-23 to 2026-11-02.)
inline constexpr double kModalBoxTextSeatAt300 = 18.0;
inline int modal_box_text_seat_px() {
    return skin_px(kModalBoxTextSeatAt300, 1);
}
// THE TEXT'S ROOM in the box `box` (modal_box_rect): x the pen's column
// (the seat in from its left column), w the soft wrap's width (the box less
// the seat on each side), zero on a box too narrow to hold it; y the
// pane's top row (a row's band rises above the ascender line it seats) and
// the bottom one past the seat over the box's bottom row: the text's clip.
inline GuiRect modal_box_text_room(const GuiRect& box) {
    const int seat = modal_box_text_seat_px();
    const int lw   = hairline_px();
    if (box.w <= 2 * seat || box.h <= lw + seat) return GuiRect{0, 0, 0, 0};
    return GuiRect{box.x + seat, box.y + lw, box.w - 2 * seat,
                   box.h - lw - seat};
}
// THE FIRST ROW'S BASELINE in the box `box`: the seat under the box's top
// row is the ascender line, the baseline the ascender's own height under it,
// rounded at the element.
inline double modal_box_text_baseline0(const GuiFont& font, const GuiRect& box) {
    return static_cast<double>(box.y + modal_box_text_seat_px()) +
           std::nearbyint(gui_font_ascender_ink_px(font));
}
