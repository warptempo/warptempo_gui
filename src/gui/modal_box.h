#pragma once

// THE MODAL FAMILY'S SHAPE — ONE OWNER OF THE GEOMETRY EVERY MODAL READS
// (architect 2026-10-14, on his mocks over tablet captures,
// the player and the picker: "overlays/
// modals can take over the area from the trim bar on down, with just the
// bottom new border remaining"). THE BOX STANDS FOR THE OVERLAYS: the render
// player and the project picker (the folder overlay's contents,
// folder_overlay.h, with the settings page, the value list, the trim presets
// and the BPM form it swaps in) and THE SCORE VIEWER (since 2026-10-26,
// score_viewer.h, its page the box's interior); and for THE THREE PAGES, a
// QUESTION'S, THE PROGRESS PAGE and THE VALUE SUB-PAGE, a line editor's
// (architect 2026-11-20; below).
//
// (INTERFACE VERSUS MODAL, architect 2026-10-14: the player and the picker
// stand in the box but are interface, so what they leave dead greys; the
// prompts, the progress modal and every editor are modal, and under each of
// them the whole roster greys (roster_greyed_whole, architect 2026-11-20) —
// the rule recorded at redesign_button_enabled's head, app_state.h.)
//
// THE BOX TAKES THE VIEW (architect 2026-11-18 / 2026-11-19, the overlays'
// design, its §A): the title bar and the top icon row
// above it stay as the main window paints them, the top icon row wearing
// the face of the page in the view (under an overlay that page's, its opener
// lit; under a question, the progress page or a value sub-page every key
// greyed, a lit one — the overlay's opener, or the opener of the list the
// sub-page was opened from — lit and inert: roster_face_page_owner,
// redesign_button_enabled's first arm, app_state.h). Across, it runs the
// panel's width (render.h's well_columns, edge to edge, architect
// 2026-11-19). Top to bottom:
//   1. THE BOX — its first rows THE LINE UNDER THE TOP ICON ROW (the trim
//      lane's first rows), its last THE LINE OVER THE BOTTOM ICON ROW (the
//      well's foot), both in the line tone `well_border`, and between them
//      THE VIEW, the box's interior, one flat fill — the canvas's tone
//      `ruler_face` (Arc's $base_color, Thunar's list view) for the lists
//      and the three pages, the score viewer's `score_pane_face` its one
//      exception (below); no frame, no ring, no shadow (paint_modal_box,
//      paint_handler.cpp);
//   2. directly under it THE CONTROLS ROW, THE BOTTOM ICON ROW'S OWN ROWS —
//      the pad 7 W, the key (key_width wide and key_height tall,
//      render.h's icon_case_w_px / icon_case_h_px), the pad 7 W
//      (strip_h_px) — on the body tone, its outermost BOXES 6 W INSIDE THE
//      PANEL (render.h's kEdgeInsetW; materials.h's THE KEY). THE ROW IS
//      THE PAGE'S THAT STANDS IN THE VIEW (architect 2026-11-19, his "i
//      take your recommends for all"; the two pages architect 2026-11-20):
//      the overlay's own controls while an overlay stands, A
//      QUESTION'S ANSWERS AND THE PROGRESS PAGE'S CANCEL, Arc's word buttons
//      flush right, while one of those pages stands, in place of whatever
//      row stood there (no timestamp, no tab name, no key, none of an
//      overlay's controls), and the main page's keys and timestamp otherwise
//      (paint_handler.cpp's modal_owns_bottom_row); UNDER EVERY EDITOR — THE
//      VALUE SUB-PAGE'S AMONG THEM — THE ROW IS NOBODY'S: the on-screen
//      keyboard stands over it and it yields (below), the answers the
//      keyboard's Enter and Esc alone;
//   3. under both, THE STATUS BAR on the window's foot (render.h's status
//      bar block: it keeps its process line under an overlay and a question
//      and drops its selection count; BLANK while a batch's progress stands,
//      status_bar_text), covered by the on-screen keyboard under every
//      editor (below).
// At the tablet's 2304 x 1440, 200 % and the keys' defaults (key_width 36,
// key_height 28; the 56-row title bar), the keyboard standing or not: the box's line
// rows 140–141 and 1308–1309, THE VIEW x 0..2303 and y 142..1307 (1166
// rows); the controls row 1310–1393, its face rows 1324–1379, its first
// face's columns 12–83 and its last 2220–2291; the status bar 1394–1439
// under it.
//
// A QUESTION AND THE PROGRESS PAGE ARE FULL PAGES (architect 2026-11-20:
// "we shuold remove the popups - it seems those come from gnome and
// xfce just inherited them (the oversized text is the clue). instead, the
// old prompt eg 'do you want to revert and drop changes?' should come
// back."; "yes, progress window can go back."): while one stands
// (modal_page_stands, app_state.h) THE BOX STANDS and THE VIEW IS ONE FLAT
// PAGE in the lists' tone whatever stood there — the main page's trim bar,
// ruler, marker lane and canvas, the `h` view, an overlay's list or the
// score viewer's page on its mat — with the question alone on it, or the
// progress page's sentence, bar and count, ON THE PAGE MARGIN (below; the
// page's lines modal_page_lines), the answers on the controls row (item 2);
// over an overlay the page stands in place of the overlay's view and row,
// the overlay open under it and back as it stood when the page leaves.
// Neither ever stands with the on-screen keyboard (modal_page_stands' head).
// The painter and the rule's owner: paint_handler.cpp's THE QUESTION PAGE
// AND THE PROGRESS PAGE.
//
// A VALUE'S EDITOR IS A SUB-PAGE (architect 2026-11-20: "a subpage
// where if the user clicks on a value a single line text input is shown ...
// on that page, the only controls are the osk's esc and enter"; the popups
// removed, "it seems those come from gnome and xfce just inherited them"):
// while one of the three line editors stands (the settings editor's buffer:
// a settings value, the trim preset's name, a BPM field —
// AppState::dialog_editor_session) THE BOX STANDS and THE VIEW IS ONE FLAT
// PAGE in the lists' tone with ONE ARC ENTRY alone on it, top-aligned ON
// THE PAGE MARGIN (below; modal_page_lines' `entry`): no label (architect
// 2026-11-22), no title bar, no frame, no shadow, no button, no list behind. The painter and
// the rule's owner: paint_handler.cpp's THE VALUE SUB-PAGE.
//
// THE ON-SCREEN KEYBOARD STANDS OVER THE PAGE, AND NOTHING IS RELAID FOR IT
// (architect 2026-11-20; the rule's one owner onscreen_keyboard.h's
// placement block): every rect below is the lane table's and takes no
// keyboard term. Under a value sub-page THE BOX KEEPS ITS WHOLE VIEW, the
// keyboard over the view's foot, the bottom icon row and the status bar,
// and what the box lays cut at the keyboard's top line (paint_modal_dialog).
// On the tablet at key_height 28: the view y 142..1307 (1166 rows), the
// entry rows 166..221 — far above
// the keyboard's top line at his tuning (osk_key_w 24, osk_key_h 35: the
// keyboard 1048–1439, the view's visible part 142..1047), at the defaults
// (osk_key_h 30: 1098–1439) and at the tallest keyboard the keys' range
// allows (osk_key_h 80: five 160-row key rows, its top line near row 600).
//
// Geometry here, every rect a fact that asks no standing, and the box's
// standing predicate beside it (modal_box_stands); the painter is
// paint_modal_box (paint_handler.cpp), and every paint site and every hit
// site of a member reads these rects (on screen is as painted).
#include "app_state.h"
#include "materials.h"      // the flat look's rules
#include "render.h"         // palette(): `ruler_face`, `score_pane_face`

#include <algorithm>
#include <cmath>
#include <cstdint>

// DOES ANY MEMBER'S BOX STAND — the folder overlay's, THE SCORE VIEWER'S
// (2026-10-26; score_viewer.h: the page in the interior, paint_modal_dialog's
// score branch), A QUESTION'S OR THE PROGRESS PAGE (architect 2026-11-20;
// modal_page_stands, app_state.h: the view is the page) or THE
// VALUE SUB-PAGE, which stands exactly while a line editor of the settings
// editor's buffer does (AppState::dialog_editor_session, the membership's
// one owner; the same ruling). The waveform's painted rect is empty while
// it does (onscreen_keyboard::waveform_paint_area): the box covers the
// canvas column whole, so nothing of a modal floats over the canvas.
inline bool modal_box_stands(const AppState& a) {
    return folder_overlay_stands(a) || a.dialog_editor_session() != 0 ||
           score_viewer_stands(a) || modal_page_stands(a);
}

// THE CONTROLS ROW (item 2): the bottom icon row's own rows, the lane itself
// (bottom_row_area), over the status bar on the window's foot. Its seats are
// the bottom icon row's
// (paint_handler.cpp's bottom_row_seats, the one owner).
inline GuiRect modal_controls_row_rect(const AppState& a) {
    const GuiRect lane = bottom_row_area(a);
    if (lane.w <= 0 || lane.h <= 0) return GuiRect{0, 0, 0, 0};
    return lane;
}

// THE BOX (item 1), its outer rect: across, THE PANEL'S COLUMNS (render.h's
// well_columns, the panel's own since 2026-11-19, send Z: edge to edge);
// down, the trim lane's first row (the line under the top icon row) to the
// controls row's top (its last rows the line over the bottom icon row, the
// well's foot) — the lane table's rows, the on-screen keyboard standing or
// not (the head's rows). A zero rect on a degenerate stack.
inline GuiRect modal_box_rect(const AppState& a) {
    const WellColumns well = well_columns(a);
    const GuiRect trim  = top_trim_row_area(a);
    const GuiRect row   = modal_controls_row_rect(a);
    const int     lw    = hairline_px();   // the box's two line rows
    const GuiRect box{well.x, trim.y, well.end - well.x, row.y - trim.y};
    if (box.w <= 2 * lw || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
    return box;
}

// THE VIEW (architect 2026-11-19, send Z; send AA's stage D2): THE INTERIOR
// is the box less ONE HAIRLINE above and below (hairline_px: 2 device px on
// the tablet at 200 %, 1 at 100 %; those rows the box's lines), across the
// panel's own columns, 0 .. 2303 on the tablet — the lists' rows stand
// across it edge to edge under their head pad (folder_overlay.h; THE PAGE
// MARGIN below), the score viewer's page in it, and the three pages' lines
// on it (y 142..1307; under a value sub-page the keyboard over its foot,
// the head's rows).
inline GuiRect modal_box_interior_rect(const AppState& a) {
    const GuiRect box = modal_box_rect(a);
    const int     lw  = hairline_px();
    if (box.w <= 2 * lw || box.h <= 2 * lw) return GuiRect{0, 0, 0, 0};
    return GuiRect{box.x, box.y + lw, box.w, box.h - 2 * lw};
}

// THE PAGE MARGIN (architect 2026-11-20: "i think we just need a
// little more top and left margin spacing for the other windows, but it
// should come from arc xfce app not just invented. the lists (picker,
// player, presets) would then inherit the spacing so as to make it uniform.
// ok to deviate a little"; on the rule as worded here, "1 agree" / "2
// agree"): ON EVERY PAGE BUT THE MAIN ONE — whatever is laid in the view —
// THE INK STANDS 13 W FROM THE VIEW'S LEFT AND FROM ITS TOP. Two numbers,
// this their one owner:
//   kPageMarginPenW 12 W     a text's PEN from the view's left edge, its ink
//                            then about 13 W in (the face's own left
//                            bearing) — THE XFCE APPEARANCE DIALOG'S INK,
//                            MEASURED 13 on his desktop; and on a page
//                            (not a list, below) NOTHING STANDS CLOSER THAN
//                            THE SAME 12 W TO THE VIEW'S RIGHT EDGE (a text
//                            is cut there);
//   kPageMarginCapTopW 13 W  the first line's CAP TOP under the view's top,
//                            whatever that line's kind — OURS, RULED: GTK
//                            lays its margin to the line box, which leaves
//                            more air above the capitals than left of the
//                            ink ("it looks liek it has more padding above
//                            than on the left"), so the top is measured to
//                            the ink as the left is.
//   kPageLabelGapW 6 W       A LABEL TO THE CONTROL UNDER IT, line box to
//                            box — THE XFCE APPEARANCE DIALOG'S, 6 px
//                            between a label and its control (his "1
//                            agree" on "a label to its control 6 W"): the
//                            progress page's sentence to its bar and the bar
//                            to its count.
// Each a whole W through scaled_px, rounded at the element (24, 26 and 12
// device px on the tablet at 200 %). A FILL ACROSS THE VIEW KEEPS THE VIEW'S
// WHOLE WIDTH (a list's lit row, x 0..2303 on the tablet): the margin seats
// ink, not faces. ITS READERS: the lists — the pad over a list's first row
// that seats its caps (folder_overlay.h's head_pad_px); A LIST'S SIDE SEATS
// ARE THE LIST VIEW'S OWN CONSTANTS, not kPageMarginPenW (architect
// 2026-11-22, sends JJ and KK: every row's drawing box 6 W in, its name at
// 28 W, cut 6 W inside the right edge; paint_handler.cpp's THE LIST VIEW,
// kListDrawingMarginW and kListDrawingGapW) — and the three pages, a question's, the progress page
// and the value sub-page (modal_page_lines below), which keep the 12 W pen;
// the value sub-page's entry box stands kPageMarginPenW from the view's left
// AND its top, a box measured to its border (the cap-top rule is a text
// line's). The score viewer's page is a picture on its mat and takes none.
inline constexpr double kPageMarginPenW    = 12.0;
inline constexpr double kPageMarginCapTopW = 13.0;
inline constexpr double kPageLabelGapW     = 6.0;
inline int page_margin_pen_px()     { return scaled_px(kPageMarginPenW); }
inline int page_margin_cap_top_px() { return scaled_px(kPageMarginCapTopW); }
inline int page_label_gap_px()      { return scaled_px(kPageLabelGapW); }

// THE PAD OVER A PAGE'S FIRST LINE — THE CAP-TOP ARITHMETIC'S ONE DEFINITION
// (architect 2026-11-20; THE PAGE MARGIN's top): the whole device
// px from the view's top to the top of the first line's BOX — a list's first
// row, a page's first line box — that put THAT LINE'S CAP TOP
// kPageMarginCapTopW under the view's top: the margin less the cap top's
// depth inside the box, `baseline_depth_px` (the baseline under the box's
// top, the line-box rule's whole offset) less the face's cap band
// (gui_font_cap_px, the file's own unhinted "H", the metric every placement
// reads). ROUNDED AT THE ELEMENT (std::nearbyint): the baseline's depth
// being whole, rounding the pad is rounding the cap band to whole rows,
// which is what the slight hinter does to the "H", so the painted capitals'
// first row stands at the margin exactly and the line under the pad stands
// on a whole baseline (the reasoning in full at its first reader,
// folder_overlay.h's head_pad_px). Its two readers: a list's head pad and
// the sentence of a question's page and the progress page
// (modal_page_lines; the value sub-page has no line, its entry a box).
inline int page_margin_head_pad_px(const GuiFont& face,
                                   double baseline_depth_px) {
    return static_cast<int>(std::nearbyint(
        static_cast<double>(page_margin_cap_top_px()) -
        (baseline_depth_px - gui_font_cap_px(face))));
}

// THE THREE PAGES' LINES — A QUESTION'S PAGE, THE PROGRESS PAGE AND THE
// VALUE SUB-PAGE, ONE GEOMETRY (architect 2026-11-20; on the mocks:
// the question page TOP-ALIGNED, "progress good", "settings prefer
// toplaigned"; paint_handler.cpp's THE QUESTION PAGE AND THE PROGRESS PAGE
// and THE VALUE SUB-PAGE the painters and the rules' owners). In the view
// (modal_box_interior_rect), every length THE PAGE MARGIN's:
//   THE SENTENCE — a question or the progress page's "Rendering..." — one
//     line of Regular 12 W (GuiFace::Body, the lists' rows' face), its PEN
//     kPageMarginPenW from the view's left, its line box (the face's 14 W,
//     gui_font_line_box_w) under the head pad that seats ITS CAP TOP
//     kPageMarginCapTopW under the view's top (page_margin_head_pad_px), its
//     baseline the line box's ascent under that; CUT kPageMarginPenW inside
//     the view's right edge (`text_end_x`; it never wraps);
//   THE BAR (the progress page's) from the pen's column to the right cut,
//     kPageProgressBarW (6 W) tall, ITS TOP kPageLabelGapW UNDER THE
//     SENTENCE'S LINE BOX;
//   THE COUNT ("3 of 11"; Regular 10 W, GuiFace::Caption, GTK's "small") at
//     the same pen, ITS LINE BOX (13 W) kPageLabelGapW UNDER THE BAR;
//   THE ENTRY (the value sub-page's, alone on its page, no label; Arc's,
//     materials.h's paint_entry) from the pen's column to the right cut —
//     12 W inside the view's left edge to 12 W inside its right —
//     kEntryHeightW (28 W, Arc's) tall, ITS TOP kPageMarginPenW (12 W)
//     UNDER THE VIEW'S TOP, the box at the page margin on both sides of its
//     corner (measured to its border, as its left edge is): a fact of the
//     view alone, whatever the text, so the entry never moves or grows with
//     an edit.
// ON THE TABLET (200 %; the view x 0..2303, y 142..1307): the pen x 24, the
// cut x 2280; the head pad 26 − (22 − 16.51) = 20.51 -> 21 px, so the
// sentence's line box rows 163..190, its baseline 185, the painted caps'
// first row 168; the bar x 24..2279, rows 203..214; the count's line box
// rows 227..252, its baseline 247; the entry x 24..2279, rows 166..221
// (1128 x 28 W), its text's pen x 42 (kEntryPenW).
// `sentence_line` and `count_line` are the two texts' line boxes' rows
// across the view's whole width (every row their ink can stand on: the
// painter's exposure test). `step` IS WHAT A PROGRESS STEP CHANGES: the
// bar's rows through the count's line box across the view's whole width
// (rows 203..252 on the tablet) — the damage
// GuiInputHandler::set_batch_progress_index declares, the sentence and the
// page round it untouched.
inline constexpr double kPageProgressBarW = 6.0;
struct ModalPageLines {
    GuiRect view{0, 0, 0, 0};
    int     pen_x             = 0;
    int     text_end_x        = 0;
    double  sentence_baseline = 0.0;
    GuiRect sentence_line{0, 0, 0, 0};
    GuiRect bar{0, 0, 0, 0};
    double  count_baseline    = 0.0;
    GuiRect count_line{0, 0, 0, 0};
    GuiRect step{0, 0, 0, 0};
    GuiRect entry{0, 0, 0, 0};
};
inline ModalPageLines modal_page_lines(const AppState& a) {
    ModalPageLines l;
    l.view = modal_box_interior_rect(a);
    if (l.view.w <= 0 || l.view.h <= 0) return l;
    const GuiFont body    = gui_font(GuiFace::Body);
    const GuiFont caption = gui_font(GuiFace::Caption);
    const GuiLineBoxW lb  = gui_font_line_box_w(body);
    const GuiLineBoxW cb  = gui_font_line_box_w(caption);
    l.pen_x      = l.view.x + page_margin_pen_px();
    l.text_end_x = l.view.x + l.view.w - page_margin_pen_px();
    const int sentence_top =
        l.view.y +
        page_margin_head_pad_px(body, line_box_baseline(body, 0.0, lb.line()));
    l.sentence_baseline =
        line_box_baseline(body, static_cast<double>(sentence_top), lb.line());
    l.sentence_line =
        GuiRect{l.view.x, sentence_top, l.view.w, scaled_px(lb.line())};
    const int bar_y = sentence_top + l.sentence_line.h + page_label_gap_px();
    const int bar_h = scaled_px(kPageProgressBarW, 1);
    l.bar = GuiRect{l.pen_x, bar_y, std::max(0, l.text_end_x - l.pen_x), bar_h};
    const int count_top = bar_y + bar_h + page_label_gap_px();
    l.count_baseline =
        line_box_baseline(caption, static_cast<double>(count_top), cb.line());
    l.count_line = GuiRect{l.view.x, count_top, l.view.w, scaled_px(cb.line())};
    l.step = GuiRect{l.view.x, bar_y, l.view.w,
                     count_top + l.count_line.h - bar_y};
    l.entry = GuiRect{l.pen_x, l.view.y + page_margin_pen_px(),
                      std::max(0, l.text_end_x - l.pen_x),
                      scaled_px(kEntryHeightW)};
    return l;
}

// THE SCORE VIEWER'S PANE — THE ONE DELIBERATE EXCEPTION to the view's tone
// (architect 2026-11-01 night, M9 A2, "THE SCORE VIEWER IS
// SETTLED": "not lighter, preferably from ableton palette directly"): the
// palette's `score_pane_face`, THE WAVEFORM'S INK (262A33, Arc's
// $header_border; its line of kArcDarkTones, gui_theme.h), the one tone
// reused.
// THE REASON: the score's page is a picture on a dark mat, not a list field.
// The GPU's field round the page (score_viewer.cpp's
// score_viewer_publish_gpu_page) and the CPU's pane under the score
// viewer's box (paint_modal_dialog's call of paint_modal_box, for a frame
// with no page hole) both read palette().score_pane_face; the page's fit
// and centering are untouched. A QUESTION OVER THE VIEWER SHOWS NO MAT: its
// page is the view's own tone, as over every other page (the head).

// THE PANEL: everything the modal takes ABOVE the controls row — THE BOX
// itself, the panel's whole width since send Z (the ground margins beside
// it retired with the well's frame). The fill a member's painter lays
// (paint_modal_box), the rect its content's damage erases and the outer
// claim of a press on the box.
inline GuiRect modal_panel_rect(const AppState& a) {
    return modal_box_rect(a);
}

// THE WHOLE AREA A MODAL TAKES: the box and the controls row under it (the
// on-screen keyboard is its own surface, its damage its own,
// onscreen_keyboard::slot_damage_rect's block).
// The folder overlay's show and hide damage
// (onscreen_keyboard::slot_damage_rect) and the closers'
// (Viewport::invalidate_modal_dialog_area).
inline GuiRect modal_area_rect(const AppState& a) {
    const GuiRect panel = modal_panel_rect(a);
    const GuiRect row   = modal_controls_row_rect(a);
    const int     y0    = panel.h > 0 ? panel.y : row.y;
    const int     y1    = row.y + row.h;
    if (row.w <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{row.x, y0, row.w, std::max(0, y1 - y0)};
}

// (The lists' rows stand edge to edge across the view, their text on THE
// PAGE MARGIN above, and the three pages' lines on it too.)
