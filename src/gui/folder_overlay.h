#pragma once

// THE FOLDER OVERLAY — the LIST PANEL (architect design 2026-08-28; the
// keyboard slot's band until 2026-10-14, the modal family's box since,
// modal_box.h). ONE WIDGET, SIX CONTENTS: the RENDER PLAYER's output folders
// and their wavs, the OPEN PROJECT PICKER's valid project folders, THE
// SETTINGS PAGE's twenty-eight keys as `key=value` lines under two group
// rows (architect 2026-10-28, on a mock he verified on the glass: "yes" to
// the list, "keep" to the group rows) — the twenty-six editable keys and,
// among the piece's lines, `scale` and `bpm` as disabled rows (architect
// 2026-10-29) — and THE VALUE LIST, one listed key's
// values, the page's row for that key's content (architect 2026-10-25: "maybe
// a drop down would make it quicker, because I don't like having to type it
// in every time. We rejected drop downs because it was too much work to make
// them Windows identical, but now we don't have the responsibility" — "brief
// it as a scrolling list", "Yes"; this widget as it stands, no new chrome) —
// since 2026-10-28 night for `true_colors`, `noise` and
// `noise_crossfade_s`, since 2026-10-31 for `waveform_grow` and since
// 2026-11-05 for `theme` and since 2026-11-17 for `icons`
// (kSettingsValueListKeys, settings_editor.h; the color
// presets' list of 2026-10-25 went with the presets 2026-11-01, the two
// levels' lists 2026-11-05 with the band's sliders) — a SUB-PAGE of the settings
// page, whose Cancel and commit return to the page on that key's row
// and whose opener key closes the whole panel (architect 2026-10-28 night,
// GuiInputHandler::return_to_settings_page); and THE TRIM PRESETS LIST
// (architect 2026-10-28 night), the piece's named trim windows
// (trim_presets_file.h), each row the preset's name alone
// (build_trim_presets_rows), a row's tap applying
// its pair to the active tab's trim, framing it and closing the list
// (architect 2026-11-19, send CC),
// its controls row the render player's shape — the highlight pair, Save,
// Save As, Delete and Close, one cluster flush right (his word: "we would be
// breaking symmetry if we did it differently"); and THE BPM FORM (architect
// 2026-10-29: "since settings has a multiline page, we should make bpm a
// form like settings"), the settings page's shape — one group row and the
// sweep's three `key=value` lines, `beats=`, `low=` and `high=`, a row's tap
// opening the line editor on it — its controls row RENDER and Cancel in the
// trim list's cluster.
// (The AV Sync Stats panel's inert text lines stood 2026-09-03..30 and went
// with the panel; the `h` view's history picker for one day, retired
// 2026-08-29 as overengineered, that view's `'` raising its confirmation on
// the viewed member with no list at all.) Which one fills it is
// AppState::FolderOverlay::owner, and that tag IS the standing predicate
// (folder_overlay_stands, app_state.h; stands() below forwards to it). This
// header is the panel's ONE OWNER of everything that is not pixels, a press
// body or a content: the geometry (the band, the row pitch, the scroll
// clamp), the one walk over the rows, and the highlight and scroll mechanics
// every content drives.
// The PAINTER lives in paint_handler.cpp beside every other painter
// and the PRESS ROUTER in input_pointer.cpp beside every other press router;
// both walk this file's row table through the one walker below, so paint and
// hit cannot describe different rows. The ROW TABLE ITSELF is
// AppState::folder_overlay (app_state.h), where its every field is described.
//
// WHAT IT IS. A flat list of rows — FOLDER rows and WAV rows, each an
// icon and a name; glyph-less PLAIN rows, a name alone (the value list's
// values since 2026-10-25, the settings page's key lines since 2026-10-28,
// the trim presets since 2026-10-28 night, the BPM form's three lines since
// 2026-10-29); and the INERT rows, which the
// highlight never stands on (row_is_inert, the walk below): the page's and
// the BPM form's GROUP rows, a heading each (Thunar's section header, in
// the text tone, Bold), and the page's DISABLED rows in the dim print
// (`text_dim`), the `scale` and `bpm` lines (architect 2026-10-29). A
// LISTING IN WHICH NO ROW WEARS A DRAWING starts its text at its own
// column, not past an empty icon column
// (architect 2026-10-28 night; the painter's names' column,
// paint_folder_overlay) (a glyph-less TEXT kind went with
// the AV Sync Stats panel on 2026-09-30, and an UP row, `..`, was a kind
// until 2026-09-01, when
// the player moved inside `tmp/` and going up became a button on its modal
// row) — painted in THE MODAL FAMILY'S BOX (architect 2026-10-14,
// modal_box.h, the one owner of the shape: "overlays/modals can take over the
// area from the trim bar on down, with just the bottom new border
// remaining"; the simple layout 2026-10-22): from THE VIEW BAR'S TOP ROW
// down, one box — THE VIEW between two lines, no frame — and under it the CONTROLS
// ROW on row 8's rows (the modal dialog's, paint_modal_dialog) — so EVERY
// LANE FROM THE VIEW BAR TO ROW 8 is under it, and THE ICON ROW STANDS ABOVE
// IT, as the main window paints it, the icon row
// CARRYING THE `h` VIEW'S OWN PARTITION (architect 2026-09-03 evening: "leave
// File open, because Quit should still be enabled — everything else like what
// we do with history ... Leave that for the player, the picker and the AV
// stats"; the File menu that ruling kept live retired with the menu row
// 2026-10-21, Quit being Android's Back): every icon-row button —
// the view pairs' four included — greys through redesign_button_enabled's
// overlay arm (each content's opener key excepted, its own closer:
// Play Renders 2026-10-13, Open Project and Settings 2026-10-25, Settings
// over the page and the value list it swaps in 2026-10-28, Trim Presets over
// its list 2026-10-28 night, BPM over its form 2026-10-29 —
// is_folder_overlay_opener — and the tooltip lamp, live under every content
// since 2026-11-10, overlay_admits_button; a lit key left dead keeps its
// lit face, dimmed, architect 2026-11-11, session 107,
// redesign_button_selected's head). The waveform's own
// passes paint nothing
// (onscreen_keyboard::waveform_paint_area, whose gate reads both tenants and
// whose clip reads the STANDING one's own rect, and which this band reduces
// to a zero-height rect, its top above the canvas's) while the lanes it
// covers paint and are covered. THE VIEW'S EDGE IS THE BAND'S ONE EDGE; the
// FIRST ROW STANDS UNDER THE LIST'S HEAD PAD, which seats its capitals on
// THE PAGE MARGIN's top and scrolls with the rows (head_pad_px below;
// architect 2026-11-20, send EE), no header above it (the folder-name
// header that stood above the player's list 2026-10-10 to 2026-10-14 is
// removed, content_rect's note).
// The architect's ruling (2026-08-28):
// "the overlay sits in the on-screen keyboard's place above the bottom strip,
// replacing the keyboard there" — on glass the keyboard would occupy that
// space, and no use of the panel needs typing. So THE OVERLAY AND THE
// KEYBOARD NEVER BOTH STAND, structurally rather than by a gate term: the
// panel's owners are modes that are not editors (the record is at
// onscreen_keyboard::stands). Unlike the keyboard the panel holds
// VARIABLE-LENGTH content and
// SCROLLS when a listing outgrows the band — the band's own drag, and the
// highlight buttons' row walk keeping the focused row visible. No header
// row, no columns, no scrollbar: the offset is the
// whole scroll state, and an over-long listing simply scrolls. A LISTING
// OPENS WITH ITS SEATED ROW CENTERED in the band, as close to center as the
// listing's ends allow (scroll_row_centered, architect 2026-10-26); every
// later walk moves the offset by the least amount (scroll_row_into_view);
// A TAP ON A ROW MOVES IT NOT AT ALL (seat_tapped_row, architect
// 2026-11-21, send EE).
//
// THE BAND IS THE MODAL FAMILY'S AND THE ROW IS THE LIST'S (architect
// 2026-08-28, the row then the icon row's button; Windows' list row since
// 2026-10-02; the band the keyboard slot's from 2026-08-28 and the modal
// family's box since 2026-10-14). Two owners, neither restated here:
//   * THE BAND is the family's PANEL and its rows live in the BOX'S INTERIOR
//     (modal_box.h: modal_panel_rect, modal_box_interior_rect). ITS HEIGHT IS
//     FIXED, every time it stands, whatever the listing's length — architect
//     2026-08-28: "we should automatically make the height ... so that it's
//     not a fluid height — it's always a fixed height". A
//     SHORT LISTING LEAVES THE REST OF THE VIEW EMPTY and a long one
//     scrolls. The fixed height retired the growing half of the day's
//     earlier ceiling ruling (which read "it grows with its content up to
//     that cap, then scrolls") the same day, the panel having jumped under
//     the pointer as the listings changed size; the rest of it stands, and
//     only WHERE the band starts has moved — the waveform's midpoint until
//     2026-09-02, the icon row's foot from 2026-09-09 and the view bar's top
//     row since 2026-10-14. The content's height
//     stays the SCROLL CLAMP's input and is nothing else's.
//   * THE ROW IS THUNAR'S DETAILS ROW, GIVEN AIR (architect 2026-11-19, send
//     Z: "use tunar"; send AA's stage D2; the heights ours since 2026-11-21,
//     send EE): 24 W, a group heading 28 W, edge to edge, no gap (kListRowW
//     and kListHeadingW below) — Thunar's 20 W of send AA, Windows' list row of
//     2026-10-02 (26 W and one W apart on the unit) before it, the icon
//     row's BUTTON from 2026-08-28 before that ("we've gone for the button
//     analogy"). A
//     NAME TOO LONG FOR THE LINE RUNS OFF THE EDGE — no wrap, no ellipsis
//     ("project and file names will be short"), the painter cutting it THE
//     PAGE MARGIN inside the view's right edge (modal_box.h, 12 W; send
//     EE, 2026-11-20). (The AV Sync Stats panel's text rows stood at a
//     monospace line's pitch, the row geometry's one fork, from 2026-09-03
//     until the panel's deletion on 2026-09-30.)
// The panel's own authored numbers are the two row heights below; the head
// pad is derived (head_pad_px), the margin's two numbers modal_box.h's.
//
// THE FACES ARE THUNAR'S LIST UNDER ARC (architect 2026-11-19, send Z, his
// approval of tmp/sendZ/VIEW/Z_04; send AA's stage D2; the ladder at the
// painter, paint_list_view): the box's interior is THE VIEW, the canvas's
// tone (`ruler_face`, Arc's $base_color 404552), a surface you pick from,
// a line above and below it and no frame. IT COVERS EVERY CONTENT, this one
// widget being every list in the product (the player's folders and wavs,
// the project picker's projects, the settings page's keys, the value
// list's values, the trim presets, the BPM form); the controls row under it
// — the transport, the scrub, the buttons — stands on the body tone. A
// resting row paints no fill at all, and the highlighted row — and the row
// under a press, the same look (Thunar is single-click) — is a flat fill in
// the theme's selection pair across the view's whole width. A ROW A TAP
// WOULD ACT ON HOVERS under the pen (architect 2026-11-21, send EE: "I'd
// like to add hover ... just the same color that I guess the buttons get on
// hover ... Maybe you want to tune it to the background that the list is
// on"): across the same width, `list_hover_face`, the buttons' own hover
// step laid on the view's tone (theme_roles.h's DERIVED block), in at once
// and out over Arc's row transition (the look at paint_handler.cpp's THE
// LIST VIEW; the state, the gate and the fade AppState::FaceFades; the
// inventory at GuiInputHandler::pen_hot_latch_),
// NO ALTERNATING ROWS, no scrollbar. (Live's browser list on the pane,
// 2026-11-01 night, M6 A, and its lighter press row of 2026-11-05 retired
// with send AA's stage D2.)
//
// THE ROWS ARE CHROME (the timing doctrine at GuiInputHandler::on_key): a
// row press ARMS —
// the same press may become the band's scroll drag, so the row's identity is
// not certain at the press — and A MOTIONLESS LIFT HIGHLIGHTS THE ROW AND
// THEN OPENS IT, the highlight seated where the row stands and the list
// not scrolled (seat_tapped_row, architect 2026-11-21, send EE: the press
// never scrolls). A CLICK ACTIVATES (architect 2026-08-29): Plasma's
// single-click, which he runs, and GNOME's touch agree. The arm's whole state is
// AppState::FolderOverlayPress; a shift or ctrl press on a row is a consumed
// no-op, and no row reads a hold. WHAT THE OPEN ACT MEANS IS THE OWNER'S (a lift
// highlights under every owner — the picker has no field beside the band):
// under the PLAYER an open enters a folder or plays a wav; under
// the PROJECT PICKER it reopens the row's project; under the SETTINGS PAGE
// it closes the page and opens the settings editor on the row's key, its
// entry the key's value (2026-11-19, send AA), whose Enter and Esc reopen
// the page on that row (2026-10-28 night,
// GuiInputHandler::handle_settings_editor_key), or on the row of a key with
// a value list swaps that list in (settings_page_commit, 2026-10-28); under
// the VALUE LIST
// it commits the row's value to the listed key and returns to the settings
// page (value_list_commit, 2026-10-25, four keys more 2026-10-28 night, a
// sixth 2026-10-31; the
// return 2026-10-28 night); under the TRIM PRESETS LIST the row's tap
// applies the row's preset to the active tab's trim, frames it and closes
// the list (trim_presets_apply, 2026-10-28 night; the close architect
// 2026-11-19, send CC: "On selecting a trim preset, the overlay should
// close."); under
// the BPM FORM it opens the line editor prefilled
// on the row's line, the form closing until the editor's return
// (bpm_form_commit, 2026-10-29). AN
// INERT ROW HAS NO OPEN and no
// highlight: its motionless lift is a consumed nothing
// (finish_folder_overlay_release asks row_is_inert). THE ROW'S TAP (its
// press-and-lift) IS THE ONE ROAD TO THE OPEN ACT: no Enter reaches a row,
// no on-screen keyboard standing under a list. The fork is at the press
// router (GuiInputHandler::folder_overlay_open_row, its one caller the
// row's release), never here — the panel knows rows, not projects.

// THIS HEADER DELIBERATELY DOES NOT INCLUDE onscreen_keyboard.h, and the
// dependency runs the other way (that header includes this one, for the
// standing tenant's rect in waveform_paint_area): since the rows became
// buttons the panel borrows nothing from the keyboard, and its band is the
// modal family's (modal_box.h).
#include "app_state.h"
#include "modal_box.h"
#include "render.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace folder_overlay {

// -- The geometry ------------------------------------------------------------
//
// THE ROW IS THUNAR'S DETAILS ROW AT 9 pt (architect 2026-11-19, send Z,
// "use tunar", his approval of tmp/sendZ/VIEW/Z_04; send AA's stage D2; the
// design tmp/sendY/P2_design_overlays.md §B at 12 W text, the mock kit's
// list_row / group_heading the reference): the panel owns the two row
// heights, the row's face and baseline (row_face, row_baseline_px) and the
// head pad derived from them (head_pad_px); the row's drawing and its pens
// stand at the painter's own constants (paint_handler.cpp's THE LIST VIEW,
// kListDrawing*) on THE PAGE MARGIN (modal_box.h).

// THE CARDS' FOOT MARGIN (notifications.cpp, its one reader since
// 2026-10-23): ONE W, the line rule's hairline_px (render.h: 2 device px on
// the tablet at 200 %, 1 at 100 % and below). The rows read none of it:
// they stand edge to edge across the view under the head pad (below).
inline int pad_px()               { return hairline_px(); }

// THE TWO ROW HEIGHTS — OURS, architect 2026-11-21 (send EE: "the lists
// look too crowded ... Whenever there's like a single entry text box, that
// looks good ... We don't have to make the list height that tall, but you
// know, I think a little more than what we have now would be good"): A
// LIST ROW kListRowW = 24 W (48 device px on the tablet at 200 %) — a
// little more than Thunar's 20 W, not as tall as the entry's 28 W (the
// value sub-page's, kEntryHeightW); its 14 W line box with 5 W of air above
// and below — and A GROUP HEADING ROW kListHeadingW = 28 W (56 device px),
// the same 4 W over a row (7 + 14 + 7). Each ONE NAMED NUMBER, so his glass
// moves it in one line; each through scaled_px, rounded at the element,
// floored at one. (Thunar's own, measured on his shot at 9 pt, stood from
// send Z / send AA's stage D2 to send EE: the row 20 W, 3 + 14 + 3, and its
// side-pane section header 24 W, 5 + 14 + 5.) EVERYTHING ELSE OF THE LIST
// FOLLOWS FROM THESE TWO: the baseline (row_baseline_px), the head pad
// (head_pad_px), the content's height and so the scroll ceiling, every
// row's rect and so the walks, the centering, the hit test and the damage,
// and the row's drawing, centered in the list row (paint_handler.cpp's
// kListDrawingYW). THE ROWS STAND
// EDGE TO EDGE ACROSS THE VIEW: no inset at its sides, no gap between rows
// (Thunar's list draws none) — so the lit row's bar runs the view's whole
// width — and over the first row THE HEAD PAD (head_pad_px below), the one
// length of the list that is no row. EVERY LIST STANDS ON THEM — the render player's
// folders and wavs, the project picker, the settings page, its value lists,
// the trim presets, the BPM form — the heading the Group rows' (the
// settings page's DEVICE / PIECE, `theme`'s three groups, the BPM form's
// one); every other kind a list row. (The 26 W row of 2026-11-04 night,
// one W apart and one W inside the pane, retired with send AA's stage D2.)
inline constexpr double kListRowW     = 24.0;
inline constexpr double kListHeadingW = 28.0;
inline int list_row_height_px()     { return scaled_px(kListRowW, 1); }
inline int list_heading_height_px() { return scaled_px(kListHeadingW, 1); }
// A ROW'S HEIGHT BY ITS KIND: a Group row the heading's, every other kind
// the list row's.
inline int row_height_px(const AppState::FolderOverlayRow& row) {
    return row.kind == AppState::FolderOverlayRow::Kind::Group
               ? list_heading_height_px()
               : list_row_height_px();
}
// IS A ROW OF THIS KIND INERT (the walk's row_is_inert below asks it of an
// index): a Group or a Disabled row.
inline bool row_kind_is_inert(AppState::FolderOverlayRow::Kind kind) {
    return kind == AppState::FolderOverlayRow::Kind::Group ||
           kind == AppState::FolderOverlayRow::Kind::Disabled;
}

// A ROW'S TEXT SEAT, ONE DEFINITION FOR THE PAINTER AND THE HEAD PAD: its
// FACE by its kind — a Group heading the Bold 12 W, every other kind the
// Regular 12 W (gui_font.h) — and its BASELINE'S DEPTH UNDER THE ROW'S TOP
// by the line-box rule in the row's own height (render.h's
// line_box_baseline: the 14 W line box 5 W down in the 24 W row, +16 W,
// and 7 W down in the 28 W heading, +18 W; one whole-W offset, 32 and 36
// device px on the tablet at 200 %). The painter seats the row's
// text at its real top plus this depth (paint_handler.cpp's
// paint_list_view); the head pad below reads the same depth, so the pad
// cannot seat a baseline the painter does not draw.
inline GuiFace row_face(AppState::FolderOverlayRow::Kind kind) {
    return kind == AppState::FolderOverlayRow::Kind::Group ? GuiFace::Bold
                                                           : GuiFace::Body;
}
inline double row_baseline_px(AppState::FolderOverlayRow::Kind kind) {
    return line_box_baseline(
        gui_font(row_face(kind)), 0.0,
        static_cast<int>(kind == AppState::FolderOverlayRow::Kind::Group
                             ? kListHeadingW
                             : kListRowW));
}

// THE HEAD PAD (architect 2026-11-20, send EE; THE PAGE MARGIN's top,
// modal_box.h, the rule's owner: "the ink stands 13 W from the view's top
// and from its left; the list's head pad follows its first row's kind" —
// "1 agree" / "2 agree"): A PAD OF CONTENT OVER THE FIRST ROW that puts THE
// FIRST LINE'S CAP TOP kPageMarginCapTopW (13 W) UNDER THE VIEW'S TOP at
// scroll 0, WHATEVER THE FIRST ROW'S KIND. THE RULE IS THE CAP TOP AND THE
// PAD IS DERIVED, never an authored number (modal_box.h's
// page_margin_head_pad_px, the arithmetic's one definition, the two pages'
// first line reading it too): the margin's 13 W less the cap
// top's depth inside the first row — that row's baseline (row_baseline_px)
// less its face's cap band (gui_font_cap_px, the file's own unhinted "H",
// the metric every placement reads) — so it FOLLOWS THE FIRST ROW'S KIND: a
// heading's caps stand deeper in its taller row, and its pad is that much
// shorter.
// A WHOLE NUMBER OF DEVICE PX, ROUNDED AT THE ELEMENT (std::nearbyint): the
// pad is a layout length — the rows under it are whole-pixel boxes at rest
// (the hit test, the clip and the damage read them; a lit row's bar fills
// whole rows; the text stands on a whole baseline, where the hinter's whole
// rows are painted as fitted) and the scroll's walls are whole (clamp_scroll).
// The baseline's depth being whole, rounding the pad IS rounding the cap
// band to whole rows, which is what the slight hinter does to the "H"
// (gui_font_bundled.cpp's head), so the painted capitals' first row stands
// at the margin exactly. ON THE TABLET (200 %; the cap band 16.51 px of the
// 24-px em in both files, 17 rows as hinted; the view's top row 142, the
// margin row 168; the rows of send EE, 2026-11-21):
//   a list row first (the picker, the player, the trim presets, a value
//     list without groups): the caps 32 − 16.51 = 15.49 px deep, the pad
//     26 − 15.49 = 10.51 -> 11 px; the first row 153..200, its baseline 185,
//     the painted caps' first row 168 (the unhinted band's top 168.49);
//   a heading first (the settings page, the BPM form, `theme`'s value
//     list): the caps 36 − 16.51 = 19.49 px deep, the pad 6.51 -> 7 px;
//     the heading's row 149..204, its baseline 185, the caps' first row
//     168; the next row 205..252.
// IT IS CONTENT, AND IT SCROLLS WITH THE ROWS (GTK's padding inside the
// scrolled content): the content's height counts it (content_height_px),
// the one walk starts under it (for_each_row), so the scroll ceiling, every
// row's rect, the keep-visible walks, the centering, the painter and the
// hit test follow from this one definition. IT
// PRESSES NOTHING: it lies in no row's rect, so row_at answers -1 on it —
// a press there is claimed by the band and arms nothing, no highlight, no
// open and no scroll drag, exactly as a press on the empty view under a
// short listing's last row (claim_folder_overlay_press, input_pointer.cpp:
// one rule for every point of the view that is no row). NO FOOT PAD: the
// last row may end on the view's foot. An empty listing has none.
inline int head_pad_px(const AppState::FolderOverlay& ov) {
    if (ov.rows.empty()) return 0;
    const AppState::FolderOverlayRow::Kind kind = ov.rows.front().kind;
    return page_margin_head_pad_px(gui_font(row_face(kind)),
                                   row_baseline_px(kind));
}

// (THE BAND OWNS NO LINE OF ITS OWN — the lines above and below the view are
// the modal family's box's outer rows, modal_box.h.)

// -- Standing ----------------------------------------------------------------

// DOES THE PANEL STAND? THE OWNER TAG DECIDES and this is the panel's own
// name for that one question (the predicate itself is folder_overlay_stands,
// app_state.h — it has to live there because that header's own predicates
// read it: the slot's two tenants are named from both ends). EVERY paint
// site and EVERY hit site asks this and nothing else.
inline bool stands(const AppState& a) {
    return folder_overlay_stands(a);
}

// -- The surface's rect ------------------------------------------------------

// The content's whole height: the head pad (head_pad_px, content like the
// rows) and every row's height, top to bottom, no gap and no foot pad. Zero
// for an empty listing.
// IT SIZES NOTHING (the band's height is fixed since 2026-08-28): its ONE
// consumer is the scroll ceiling below, which is how far the content runs
// past the band.
inline int content_height_px(const AppState::FolderOverlay& ov) {
    int h = head_pad_px(ov);
    for (const AppState::FolderOverlayRow& row : ov.rows) h += row_height_px(row);
    return h;
}
inline int content_height_px(const AppState& a) {
    return content_height_px(a.folder_overlay);
}

// THE BAND: THE MODAL FAMILY'S PANEL (architect 2026-10-14, modal_box.h —
// the box, from the line under the top icon row to the line over the bottom
// icon row), a FIXED height
// whatever the listing's length (2026-08-28, the ruling above). Like the
// keyboard's own accessor it does not ask whether the panel stands.
//
// IT IS THE PANEL'S ONE RECT, and that is what the fixed height bought: the
// band a damage must erase is the band that was painted, so a listing that
// SHRANK leaves nothing standing above a shorter one and there is no second
// "the band at its ceiling" rect to remember to use. (There was one —
// band_damage_rect, whose single reader was the player's listing rebuild — and
// it went with the growing band that gave it its reason: a rect exists iff it
// answers a question, and the two answers are now the same rect.) It is what
// the painter grounds, what a damage erases and what the band's OUTER CLAIM
// contains — the press router asks this rect to decide that the band, not the
// waveform under it, owns the press. The controls row under it is the modal
// dialog's (paint_modal_dialog), never this claim's. The rows are painted
// and hit inside the content rect below, the box's interior.
//
// A degenerate stack answers a zero-height rect, which the painter and the hit
// test already read as nothing. An EMPTY LISTING is a painted band with no
// rows in it — two contents can stand empty: the `noise` key's value
// list with no folder under `noise/`, and the trim presets list before its
// first preset is saved or over a file read as malformed (the player refuses with
// "Nothing to play: no renders under tmp/", the project picker
// always lists at least the project that is open, the settings page its
// key rows under two group rows (the editable keys plus the inert `scale` and
// `bpm`), the BPM form its four, and every other value list its key's
// whole domain).
// THE KEYBOARD NEVER STANDS UNDER IT (the record at onscreen_keyboard::stands),
// so the surface is the box's panel over the lanes, nothing of the
// keyboard's to cut from it.
inline GuiRect surface_rect(const AppState& a) {
    return modal_panel_rect(a);
}

// The rows' band: THE BOX'S INTERIOR, THE VIEW (modal_box_interior_rect) —
// from the row under the top icon row's line to the row over the bottom icon
// row's line, the panel's whole width (send Z, edge to edge).
// Every geometry below reads this and not the surface — the rows,
// the scroll ceiling, the keep-visible walk, the painter's row-walk clip and
// row_at's containment — so a row can never paint onto the lines round the
// view or be pressed through them. AT SCROLL 0 THE HEAD PAD STANDS ON THE
// VIEW'S TOP ROWS AND THE FIRST ROW UNDER IT (head_pad_px), no header above.
// (THE FOLDER NAME ABOVE THE PLAYLIST — the viewed folder's name in one list
// row and an etched pair under it, the render player's alone, 2026-10-10 to
// 2026-10-14 — IS REMOVED, architect 2026-10-14: "it was icon padding but no
// icon, the dividing line sits awkwardly". If it is ever wanted back, the one
// road that broke no pattern was the folder name in the caption's title (no
// caption of the app's own since 2026-10-24). The
// car's MediaSession artist line keeps its own folder name, the playing
// item's, GuiRenderPlayer's.)
inline GuiRect content_rect(const AppState& a) {
    return modal_box_interior_rect(a);
}

// -- The scroll state --------------------------------------------------------

// The scroll offset's ceiling over a band `band`: how much of the content
// (the head pad and the rows) lies past it.
inline int max_scroll_px(const AppState::FolderOverlay& ov,
                         const GuiRect& band) {
    return std::max(0, content_height_px(ov) - band.h);
}
inline int max_scroll_px(const AppState& a) {
    return max_scroll_px(a.folder_overlay, content_rect(a));
}

// THE ONE CLAMP of the scroll offset onto [0, max_scroll_px]. Every writer
// (the drag, the row walk, the listing rebuild) calls it after its
// write, so a listing that shrank under a standing offset lands honestly.
// THE OFFSET IS REAL, ITS WALLS WHOLE (architect 2026-11-15, the no-grid
// arc): the finger's real travel moves the list by a real offset, and the
// two ends it stops at are the whole lengths the layout gives.
inline void clamp_scroll(AppState& a) {
    a.folder_overlay.scroll_px =
        std::clamp(a.folder_overlay.scroll_px, 0.0,
                   static_cast<double>(max_scroll_px(a)));
}

// -- The rows ----------------------------------------------------------------

// THE ONE WALK OVER THE ROWS, and the reason paint and hit cannot drift: both
// go through it. `fn(index, row, rect)` is called for every row of `ov` in
// listing order with its painted rect in the band `band` under the scroll
// offset `scroll` — the band's whole width, each row its kind's height
// (row_height_px), THE FIRST ROW UNDER THE HEAD PAD (head_pad_px of `ov`,
// content that scrolls with the rows: at scroll 0 the first row's top is
// the band's top plus the pad), the next row on its foot. A REAL RECT (architect
// 2026-11-15, the no-grid arc): its columns and its height whole, its rows
// where the real scroll offset puts them — the painter fills it through the
// exact fill and seats its drawing and its name on it exactly, the press
// router asks it with the pointer's real place, and a damage site takes its
// hull (render.h). It may lie partly or wholly outside the band; the painter
// clips and the hit test asks the band first. THE VIEW'S FORM (below) walks
// the standing overlay in its own band, as its painter does
// (paint_handler.cpp's paint_list_view).
template <class Fn>
inline void for_each_row(const AppState::FolderOverlay& ov,
                         const GuiRect& band, double scroll, Fn&& fn) {
    const int n = static_cast<int>(ov.rows.size());
    double y = static_cast<double>(band.y + head_pad_px(ov)) - scroll;
    for (int i = 0; i < n; ++i) {
        const AppState::FolderOverlayRow& row = ov.rows[static_cast<size_t>(i)];
        const int h = row_height_px(row);
        fn(i, row,
           GuiRectF{static_cast<double>(band.x), y,
                    static_cast<double>(band.w), static_cast<double>(h)});
        y += static_cast<double>(h);
    }
}
template <class Fn>
inline void for_each_row(const AppState& a, Fn&& fn) {
    for_each_row(a.folder_overlay, content_rect(a), a.folder_overlay.scroll_px,
                 fn);
}

// Row `index`'s PAINTED rect under the live scroll offset (the walk's own
// rect; a zero rect off the listing).
inline GuiRectF row_rect(const AppState::FolderOverlay& ov, const GuiRect& band,
                         double scroll, int index) {
    GuiRectF out{0.0, 0.0, 0.0, 0.0};
    for_each_row(ov, band, scroll,
                 [&](int i, const AppState::FolderOverlayRow&,
                     const GuiRectF& r) {
                     if (i == index) out = r;
                 });
    return out;
}
inline GuiRectF row_rect(const AppState& a, int index) {
    return row_rect(a.folder_overlay, content_rect(a),
                    a.folder_overlay.scroll_px, index);
}

// The row under the pointer's REAL place (x, y), or -1 (half-open against
// each row's real rect, so two rows never share a point) — a point outside
// the ROWS' BAND (content_rect,
// the box's interior, whole) answers -1 whatever row's rect would contain it, so a
// row scrolled out of the band cannot be pressed through the lines round
// the view or the controls row below. THE HEAD PAD IS NO ROW (head_pad_px):
// a point on it answers -1, as a point on the empty view under the last
// row does. It is the content rect and not the
// surface because the surface is the band's outer CLAIM (the press router's
// own test, one call up) while the rows live in the content — the view.
// The painter clips its row walk to this same rect, so paint and hit agree on
// every pixel of the band.
inline int row_at(const AppState& a, double x, double y) {
    const GuiRectF band = content_rect(a);
    if (!rect_contains(band, x, y)) return -1;
    int hit = -1;
    for_each_row(a, [&](int i, const AppState::FolderOverlayRow&,
                        const GuiRectF& r) {
        if (hit < 0 && rect_contains(r, x, y)) hit = i;
    });
    return hit;
}

// DOES ROW `index` HEAD THE LISTING: no acting row stands above it — row 0,
// or the first acting row under the inert rows that open the listing (the
// settings page's and the BPM form's first key line under its heading, a
// grouped value list's first value). It is the highest row a seat or a
// walk can name (set_highlight seats no inert row), so it is where "the
// top of the list" is shown (scroll_showing_row below).
inline bool row_heads_listing(const AppState::FolderOverlay& ov, int index) {
    const int n = static_cast<int>(ov.rows.size());
    if (index < 0 || index >= n) return false;
    for (int i = 0; i < index; ++i)
        if (!row_kind_is_inert(ov.rows[static_cast<size_t>(i)].kind))
            return false;
    return true;
}

// THE LEAST MOVE THAT SHOWS ROW `index` WHOLE in the band `band`, from the
// offset `scroll`, clamped onto the listing's walls there: the offset the
// keyboard row walk's "keep the focused row visible" writes (below).
// THE ROW THAT HEADS THE LISTING SHOWS WITH THE LISTING'S HEAD (architect
// 2026-11-20, send EE, the head pad's rule: bringing the first row into
// view shows the pad too): for row_heads_listing's row the answer is SCROLL
// 0 — the head pad and whatever heading stands over the row in view with
// it, the list as it rests — wherever the row stands whole in the band at
// that offset (always, in a view taller than the listing's head); a seat
// or a walk onto any other row keeps the least move.
inline double scroll_showing_row(const AppState::FolderOverlay& ov,
                                 const GuiRect& band, double scroll,
                                 int index) {
    const int n = static_cast<int>(ov.rows.size());
    if (index >= 0 && index < n) {
        const double top    = static_cast<double>(band.y);
        const double bottom = static_cast<double>(band.y + band.h);
        if (row_heads_listing(ov, index)) {
            const GuiRectF at_head = row_rect(ov, band, 0.0, index);
            if (at_head.y + at_head.h <= bottom) return 0.0;
        }
        const GuiRectF r = row_rect(ov, band, scroll, index);
        if (r.y < top)                scroll -= (top - r.y);
        else if (r.y + r.h > bottom)  scroll += (r.y + r.h) - bottom;
    }
    return std::clamp(scroll, 0.0,
                      static_cast<double>(max_scroll_px(ov, band)));
}

// Scroll so row `index` lies wholly inside the band — the keyboard row
// walk's owner of "keep the focused row visible", moving the offset by the
// least amount that does it and nothing when the row already shows, the
// row that heads the listing bringing the listing's head with it
// (scroll_showing_row). Clamps.
inline void scroll_row_into_view(AppState& a, int index) {
    const int n = static_cast<int>(a.folder_overlay.rows.size());
    if (index < 0 || index >= n) return;
    a.folder_overlay.scroll_px =
        scroll_showing_row(a.folder_overlay, content_rect(a),
                           a.folder_overlay.scroll_px, index);
}

// THE OPEN'S LANDING (architect 2026-10-26: "normally you'd expect a drop
// down list to center it if possible, or get as close to center"): the
// offset that puts row `index`'s center on the band's center EXACTLY
// (architect 2026-11-15, the no-grid arc: where the band's and the row's
// heights differ by an odd number of px the row rests half a px off the
// pixel rows, at exact coverage), then the one clamp — so a row near
// either end of the listing lands as close to center as the ends allow, and a
// listing shorter than the band never scrolls (its head pad on the view's
// top rows; the pad is content, so the row's rect and the walls count it).
// The six open-time seats
// call it after their seat (the project picker's, the settings page's, the
// value list's and the BPM form's builds, the trim presets list's open, the render
// player's every rebuild, each a
// fresh listing — the page's first key row, row 1, lands under its heading
// at the listing's head, scroll 0, the clamp ruling a centered row 1 out,
// and a value list's return seats the page on that key's row); the
// highlight pair's walk keeps scroll_row_into_view's least-move rule (an
// arrow step scrolls by one row, every list widget's rule), and a tap
// moves the offset not at all (seat_tapped_row, architect 2026-11-21).
// Returns whether the offset moved.
inline bool scroll_row_centered(AppState& a, int index) {
    const int n = static_cast<int>(a.folder_overlay.rows.size());
    if (index < 0 || index >= n) return false;
    const double   before = a.folder_overlay.scroll_px;
    const GuiRect  band   = content_rect(a);
    const GuiRectF r      = row_rect(a, index);
    a.folder_overlay.scroll_px +=
        (r.y + r.h / 2.0) -
        (static_cast<double>(band.y) + static_cast<double>(band.h) / 2.0);
    clamp_scroll(a);
    return a.folder_overlay.scroll_px != before;
}

// -- The highlight and the scroll, the mechanics every content drives --------
//
// THE DAMAGE IS THE CALLER'S: each of the movers answers whether the band's
// pixels changed and writes none of them, so the panel needs no Viewport and
// no include of one — the player damages through its own helper, the
// picker's five contents through the input handler's. Nothing else about the highlight lives
// anywhere else: what the band MEANS is the owner's, where it can sit is
// here.

// IS ROW `index` INERT? (architect 2026-10-28, the settings page's group
// rows: "keep"; 2026-10-29, its disabled `scale` and `bpm` lines) — a Group
// or a Disabled row, THE ONE PREDICATE every refusal of the highlight and
// the open asks: set_highlight below will not seat one, the
// walk steps over them (move_highlight_target), and the row press's
// motionless lift on one is a consumed nothing
// (finish_folder_overlay_release). Its press still arms, the same press
// being the band's scroll drag; the painter keeps it unlit
// (paint_folder_overlay). False outside the listing.
// ON A GIVEN PAGE: the same answer over `ov`, the standing overlay's.
inline bool row_is_inert(const AppState::FolderOverlay& ov, int index) {
    const int n = static_cast<int>(ov.rows.size());
    if (index < 0 || index >= n) return false;
    return row_kind_is_inert(ov.rows[static_cast<size_t>(index)].kind);
}
inline bool row_is_inert(const AppState& a, int index) {
    return row_is_inert(a.folder_overlay, index);
}

// Seat the highlight on `index`, clamped into the listing (-1 for an empty
// one), scrolling the band to keep it visible. Returns whether the band
// changed. AN INERT ROW IS NEVER SEATED (row_is_inert): a seat on one is
// refused whole, the band unchanged — every seat names an actionable row
// by construction (the builders' seats, the walk's landing, a lift on an
// acting row), so the refusal is the rule's statement, not a road.
// ITS SCROLL IS THE WALK'S AND THE OPEN-TIME SEATS' (the builders', the
// player's item change), NEVER A TAP'S: a row the pointer tapped is seated
// by seat_tapped_row below.
inline bool set_highlight(AppState& a, int index) {
    AppState::FolderOverlay& ov = a.folder_overlay;
    const int n  = static_cast<int>(ov.rows.size());
    const int to = n <= 0 ? -1 : std::clamp(index, 0, n - 1);
    if (row_is_inert(a, to)) return false;
    bool changed = false;
    if (to != ov.highlight_row) {
        ov.highlight_row = to;
        changed          = true;
    }
    if (to >= 0) {
        const double before = ov.scroll_px;
        scroll_row_into_view(a, to);
        if (ov.scroll_px != before) changed = true;
    }
    return changed;
}

// THE PRESS NEVER SCROLLS (architect 2026-11-21, send EE: "Once I tap on
// something, it's okay if there's a little delay ... There shouldn't be like
// an in-between frame"): THE TAP'S SEAT — the highlight onto row `index`,
// the row the pointer pressed and lifted on, AND THE SCROLL NOT MOVED AT
// ALL. The row under the pen is on screen by definition, partly cut or
// whole as the user left it; the keep-visible walk and the head rule
// (scroll_showing_row) are the UP / DOWN walk's and the open-time seats',
// never a press's, so no frame shows the list moved between the press and
// what the tap opens — the press's own face, the accent on the pressed row,
// the one picture before the act (paint_list_view), as Arc's. Its one
// road: the row's lift (GuiInputHandler::folder_overlay_highlight_row,
// through picker_set_highlight and GuiRenderPlayer::set_highlight). The
// inert row and the clamp as set_highlight's. Returns whether the
// highlight moved.
inline bool seat_tapped_row(AppState& a, int index) {
    AppState::FolderOverlay& ov = a.folder_overlay;
    const int n  = static_cast<int>(ov.rows.size());
    const int to = n <= 0 ? -1 : std::clamp(index, 0, n - 1);
    if (row_is_inert(a, to)) return false;
    if (to == ov.highlight_row) return false;
    ov.highlight_row = to;
    return true;
}

// THE ROW A WALK STEPS FROM: the highlight, or row 0 where the band has no
// seat (an empty listing, or one whose rows have not been seated yet). ONE
// OWNER, TWO READERS as re-greped 2026-09-12 — move_highlight below, the step
// itself, and THE CAR'S PREVIOUS, whose rest arm asks whether the walk is at
// the band's first row before leaving the folder instead
// (GuiRenderPlayer::car_previous). They must not drift: a fork that measured
// from the raw field and a step that measures from this would disagree about
// where a seatless band begins. (Two walk walls and a hint bit read it, and
// the router's bare Up arm with them, for the one day on which the tablet's
// skips walked the band too.)
inline int walk_origin_row(const AppState::FolderOverlay& ov) {
    return ov.highlight_row < 0 ? 0 : ov.highlight_row;
}
inline int walk_origin_row(const AppState& a) {
    return walk_origin_row(a.folder_overlay);
}

// THE ROW A WALK OF `delta` ROWS LANDS ON, or -1 for an empty listing (the
// step's one owner: move_highlight below seats it, and
// move_highlight_actionable asks whether it would change the highlight).
// THE INERT ROWS' ONE SKIP (architect 2026-10-28, the settings page's group
// rows): a landing on an inert row (row_is_inert) steps on past it in the
// walk's direction (a zero delta stepping down) to the first acting row;
// where none lies that way the walk is CLAMPED AT THE ACTING ROWS' END,
// stepping back from the landing to the nearest acting row — so Up from the
// page's first key row stays there, its group row above it never seated.
// A content with no inert row lands where the clamp lands, unchanged. A
// listing of inert rows alone has no producer and answers -1, as an empty
// one does.
// (The page form answers over `ov`, row_is_inert's page form's reason.)
inline int move_highlight_target(const AppState::FolderOverlay& ov,
                                 int delta) {
    const int n = static_cast<int>(ov.rows.size());
    if (n <= 0) return -1;
    const int landing = std::clamp(walk_origin_row(ov) + delta, 0, n - 1);
    const int step    = delta < 0 ? -1 : +1;
    for (int to = landing; to >= 0 && to < n; to += step)
        if (!row_is_inert(ov, to)) return to;
    for (int to = landing - step; to >= 0 && to < n; to -= step)
        if (!row_is_inert(ov, to)) return to;
    return -1;
}
inline int move_highlight_target(const AppState& a, int delta) {
    return move_highlight_target(a.folder_overlay, delta);
}

// Move the highlight by `delta` rows, clamped; an empty listing has nothing
// to move and a -1 highlight walks from row 0 (the origin owner above).
// Returns whether the band changed.
inline bool move_highlight(AppState& a, int delta) {
    const int to = move_highlight_target(a, delta);
    if (to < 0) return false;
    return set_highlight(a, to);
}

// WOULD A WALK OF `delta` ROWS MOVE THE HIGHLIGHT (architect 2026-10-10, the
// render player's Highlight Previous / Next buttons): the walk's landing row
// differs from the row the highlight stands on. False on an empty listing and
// at the wall the walk clamps to; a -1 highlight is actionable, the walk
// starting FROM row 0 (walk_origin_row) and applying its delta exactly as the
// arrow keys do: Down lands on row 1, Up on row 0. It reads the one landing
// owner, so it agrees with the walk about the inert rows: a step that the
// skip clamps back onto the highlight's own row (Up from the settings page's
// first key row) is no step. The band's scroll-into-view
// alone (a highlight already on the wall row but partly out of view) is not
// asked: that is a repaint of the band, not a step of the highlight.
// (The page form answers over `ov`, row_is_inert's page form's reason.)
inline bool move_highlight_actionable(const AppState::FolderOverlay& ov,
                                      int delta) {
    const int to = move_highlight_target(ov, delta);
    return to >= 0 && to != ov.highlight_row;
}
inline bool move_highlight_actionable(const AppState& a, int delta) {
    return move_highlight_actionable(a.folder_overlay, delta);
}

} // namespace folder_overlay
