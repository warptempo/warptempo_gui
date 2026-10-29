#pragma once

// THE FOLDER OVERLAY — the LIST PANEL (architect design 2026-08-28; the
// keyboard slot's band until 2026-10-14, the modal family's box since,
// modal_box.h). ONE WIDGET, FIVE CONTENTS: the RENDER PLAYER's output folders
// and their wavs, the OPEN PROJECT PICKER's valid project folders, THE
// SETTINGS PAGE's thirteen keys as `key=value` lines under two group rows
// (architect 2026-10-28, on a mock he verified on the glass: "yes" to the
// list, "keep" to the group rows), and THE VALUE LIST, one listed key's
// values, the page's row for that key's content — since 2026-10-25 the
// twenty built-in waveform presets by display name (architect: "it would be
// nice to have some presets, and maybe a drop down would make it quicker,
// because I don't like having to type it in every time. We rejected drop
// downs because it was too much work to make them Windows identical, but now
// we don't have the responsibility" — "brief it as a scrolling list", "Yes";
// this widget as it stands, no new chrome), and since 2026-10-28 night the
// same for `true_colors`, `noise`, `noise_level` and `noise_crossfade_s`
// (kSettingsValueListKeys, settings_editor.h) — a SUB-PAGE of the settings
// page, whose Cancel, Esc and commit return to the page on that key's row
// and whose opener key closes the whole panel (architect 2026-10-28 night,
// GuiInputHandler::return_to_settings_page); and THE TRIM PRESETS LIST
// (architect 2026-10-28 night), the piece's named trim windows
// (trim_presets_file.h), each row the preset's name alone
// (build_trim_presets_rows), a row's tap applying
// its pair to the active tab's trim and framing it with the list standing,
// its controls row the render player's shape — the highlight pair, Save,
// Save As, Delete and Close, one cluster flush right (his word: "we would be
// breaking symmetry if we did it differently").
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
// the trim presets since 2026-10-28 night); and the page's INERT GROUP rows,
// a heading in the dead key's print, which the highlight never stands on
// (row_is_inert, the walk below). A LISTING IN WHICH NO ROW WEARS A GLYPH
// starts its text at the glyph's own column, not past an empty icon column
// (architect 2026-10-28 night; the painter's names' column,
// paint_folder_overlay) (a glyph-less TEXT kind went with
// the AV Sync Stats panel on 2026-09-30, and an UP row, `..`, was a kind
// until 2026-09-01, when
// the player moved inside `tmp/` and going up became a button on its modal
// row) — painted in THE MODAL FAMILY'S BOX (architect 2026-10-14,
// modal_box.h, the one owner of the shape: "overlays/modals can take over the
// area from the trim bar on down, with just the bottom new border
// remaining"; the simple layout 2026-10-22): from THE VIEW BAR'S TOP ROW
// down, one sunken box wearing the program ring, and under it the CONTROLS
// ROW on row 8's rows (the modal dialog's, paint_modal_dialog) — so EVERY
// LANE FROM THE VIEW BAR TO ROW 8 is under it, and THE ICON ROW STANDS ABOVE
// IT, as the main window paints it, the icon row
// CARRYING THE `h` VIEW'S OWN PARTITION (architect 2026-09-03 evening: "leave
// File open, because Quit should still be enabled — everything else like what
// we do with history ... Leave that for the player, the picker and the AV
// stats"; the File menu that ruling kept live retired with the menu row
// 2026-10-21, Quit the window manager's close on the laptop, Android's Back on the tablet, and Ctrl+Q): every icon-row button —
// the view pairs' four included — greys through redesign_button_enabled's
// first arm (each content's opener key excepted, its own closer:
// Play Renders 2026-10-13, Open Project and Settings 2026-10-25, Settings
// over the page and the value list it swaps in 2026-10-28, Trim Presets over
// its list 2026-10-28 night — is_folder_overlay_opener). The waveform's own
// passes paint nothing
// (onscreen_keyboard::waveform_paint_area, whose gate reads both tenants and
// whose clip reads the STANDING one's own rect, and which this band reduces
// to a zero-height rect, its top above the canvas's) while the lanes it
// covers paint and are covered. THE BOX'S RING IS THE BAND'S ONE EDGE; the
// FIRST ROW STANDS AT THE BOX'S TOP, one pad under the ring (the folder-name
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
// SCROLLS when a listing outgrows the band — the wheel on plastic, the
// band's own drag on glass, and the keyboard's row walk keeping the focused
// row visible. No header row, no columns, no scrollbar: the offset is the
// whole scroll state, and an over-long listing simply scrolls. A LISTING
// OPENS WITH ITS SEATED ROW CENTERED in the band, as close to center as the
// listing's ends allow (scroll_row_centered, architect 2026-10-26); every
// later walk moves the offset by the least amount (scroll_row_into_view).
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
//     SHORT LISTING LEAVES THE REST OF THE BOX AS GROUND and a long one
//     scrolls. The fixed height retired the growing half of the day's
//     earlier ceiling ruling (which read "it grows with its content up to
//     that cap, then scrolls") the same day, the panel having jumped under
//     the pointer as the listings changed size; the rest of it stands, and
//     only WHERE the band starts has moved — the waveform's midpoint until
//     2026-09-02, the icon row's foot from 2026-09-09 and the view bar's top
//     row since 2026-10-14. The content's height
//     stays the SCROLL CLAMP's input and is nothing else's.
//   * THE ROW IS WINDOWS' LIST ROW (architect 2026-10-02): 17 Windows px
//     tall at the normal face, as Windows' list boxes with 16-px icons, the
//     roster's 16-px glyph seated at the toolbar case's 3-px lead and centred
//     in the row's height, one Windows px between rows (the list's focus
//     frame stands in it). It was the icon row's BUTTON, the toolbar case's
//     height, from 2026-08-28 ("we've gone for the button analogy"; "the
//     buttons are good enough size for my finger"). A
//     NAME TOO LONG FOR THE LINE RUNS OFF THE EDGE — no wrap, no ellipsis
//     ("project and file names will be short"), the painter clipping it to
//     the row. (The AV Sync Stats panel's text rows stood at a monospace
//     line's pitch, the row geometry's one fork, from 2026-09-03 until the
//     panel's deletion on 2026-09-30.)
// The panel's own authored numbers are the four below.
//
// THE FACES ARE WINDOWS' LIST VIEW (architect 2026-10-02, the ladder at the
// painter; the field 2026-10-06): the box's interior is THE FIELD GROUND, the
// field pair, "like a file explorer, not popping out like a rising bevel" —
// a surface you pick from (the modal box's one flat field, modal_box.h) — inside the
// canvas column's sunken ring since 2026-10-14 (the plain sunken two-line
// edge round the keyboard slot's band before it). IT COVERS EVERY CONTENT,
// this one widget being every
// Windows list in the product (the player's folders and wavs, the project
// picker's projects, the settings page's keys, the value list's values, the
// trim presets); the controls row under it — the transport, the scrub,
// the buttons — stands on the face. A
// resting row paints no fill at all, and the highlighted row is a flat fill
// in the theme's selected pair (render.h's palette block), focused or not.
// No hover face and NO ALTERNATING ROWS.
//
// THE ROWS ARE CHROME (the timing doctrine at GuiInputHandler::on_key): a
// row press ARMS —
// the same press may become the band's scroll drag, so the row's identity is
// not certain at the press — and A MOTIONLESS LIFT HIGHLIGHTS THE ROW AND
// THEN OPENS IT. A CLICK ACTIVATES (architect 2026-08-29, over the
// 2026-08-28 "a single click highlights; opening is the double-click or
// Enter"): Plasma's single-click, which he runs, and GNOME's touch agree, and
// the double-click surface went with the ruling — the second press has no
// second meaning left to carry. The arm's whole state is
// AppState::FolderOverlayPress; a shift or ctrl press on a row is a consumed
// no-op, and no row reads a hold. WHAT THE OPEN ACT MEANS IS THE OWNER'S (a lift
// highlights under every owner — the picker has no field beside the band):
// under the PLAYER an open enters a folder or plays a wav; under
// the PROJECT PICKER it reopens the row's project; under the SETTINGS PAGE
// it closes the page and opens the settings editor prefilled on the row's
// line, whose OK and Cancel reopen the page on that row (2026-10-28 night,
// GuiInputHandler::handle_settings_editor_key), or on the row of a key with
// a value list swaps that list in (settings_page_commit, 2026-10-28); under
// the VALUE LIST
// it commits the row's value to the listed key and returns to the settings
// page (value_list_commit, 2026-10-25, four keys more 2026-10-28 night; the
// return 2026-10-28 night); under the TRIM PRESETS LIST it applies the row's
// preset to the active tab's trim, the list standing (trim_presets_apply,
// 2026-10-28 night). AN
// INERT ROW HAS NO OPEN and no
// highlight: its motionless lift is a consumed nothing
// (finish_folder_overlay_release asks row_is_inert). Enter on the highlight is
// the keyboard's own click and reaches the same fork, which is at
// the press router and the key routers, never here — the panel knows rows,
// not projects.

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
// THE ROW IS WINDOWS' LIST ROW (the ruling above): the panel owns its row
// height, its pad and its row gap; the row's boxed glyph and its name stand
// at the painter's own constants (paint_handler.cpp's kListGlyphInkAt300,
// kListGlyphXAt300, kListNameXAt300, 2026-10-23).

// THE PANEL'S OUTER INSET, its ONE authored margin — the margin between the
// band's edge and the rows, at the top, the bottom and both sides.
// pcmanfm-qt's compact view keeps 1 px between an item and the frame and the
// architect's ruling lets it "grow proportionally with the taller rows"; 1
// Windows px since the unit's change (the laptop pixel's 2 re-authored,
// architect 2026-10-02), and it scales like every other authored length.
inline constexpr double kPanelPadPx = 1.0;
// The gap between two rows, the panel's own since the roster's buttons began
// to touch (architect 2026-10-02): 1 Windows px, the laptop pixel's 2-px
// button gap re-authored.
inline constexpr double kRowGapPx = 1.0;

// THE ROW BOX: 17 Windows px (kRowHeightPx — the laptop pixel's 32-px
// button box until 2026-10-02, then the 22-px toolbar case for that day's
// first hours), the panel's row gap, the row's boxed glyph (the painter's,
// 2026-10-23; Windows' small 16-px icon until then). A folder row wears
// icons::Icon::FolderRow and a wav row icons::Icon::WavRow; a plain row and
// a group row wear none, and a listing of them alone starts its text at the
// glyph's column (paint_folder_overlay's names' column, 2026-10-28 night).
// (The glyph-less
// TEXT kind went with the AV Sync Stats panel on 2026-09-30, and the UP kind,
// which wore the folder glyph because it named a folder, with the player's
// move inside `tmp/` on 2026-09-01.) The accessor keeps the name it had while
// a row was a wide button.
inline constexpr double kRowHeightPx = 17.0;
inline int button_row_height_px() { return scaled_px(kRowHeightPx, 1); }
inline int button_row_gap_px()    { return scaled_px(kRowGapPx, 1); }
// (Its one reader since 2026-10-23 is the notification stack's margin,
// notifications.cpp: the rows stand at row_inset_px below.)
inline int pad_px()               { return scaled_px(kPanelPadPx, 1); }
// THE ROWS' INSET INSIDE THE BOX'S INTERIOR — left, top and right, and the
// listing's foot: THE CONTENT'S ONE DEVICE PX OF AIR (modal_box.h's THE RING
// IS THE ONLY MARGIN, architect 2026-10-23: "at most one device pixel, since
// the bevel is one pixel"), so the lit row's bar stands one px from the
// ring's columns and the first row one px under its top row. (kPanelPadPx's
// 1 W inside the interior's 1-W slot stood 2026-10-14 to 2026-10-23.)
inline int row_inset_px()         { return modal_box_content_pad_px(); }
// (THE ROW'S GLYPH AT WINDOWS' SMALL 16-W ICON, its 3-W lead and its 6-W
// gap to the name stood 2026-10-02 to 2026-10-23; the boxed glyph's
// geometry is the painter's since, paint_handler.cpp's kListGlyph*.)

// (THE BAND OWNS NO LINE OF ITS OWN — its one edge is the modal family's
// ring, modal_box.h.)

// -- Standing ----------------------------------------------------------------

// DOES THE PANEL STAND? THE OWNER TAG DECIDES and this is the panel's own
// name for that one question (the predicate itself is folder_overlay_stands,
// app_state.h — it has to live there because that header's own predicates
// read it: the slot's two tenants are named from both ends). It takes no
// platform term, unlike the
// keyboard's: the panel serves the pointer and the finger alike, so it stands
// on both backends. EVERY paint site and EVERY hit site asks this and nothing
// else.
inline bool stands(const AppState& a) {
    return folder_overlay_stands(a);
}

// -- The surface's rect ------------------------------------------------------

// The content's whole height: the pad at both ends, every row and the gaps
// between them. Zero for an empty listing.
// IT SIZES NOTHING (the band's height is fixed since 2026-08-28): its ONE
// consumer is the scroll ceiling below, which is how far the content runs
// past the band.
inline int content_height_px(const AppState& a) {
    const int n = static_cast<int>(a.folder_overlay.rows.size());
    if (n <= 0) return 0;
    return 2 * row_inset_px() + n * button_row_height_px() +
           (n - 1) * button_row_gap_px();
}

// THE BAND: THE MODAL FAMILY'S PANEL (architect 2026-10-14, modal_box.h —
// the box wearing the program ring and its margins of ground, from the view
// bar's top row to the controls row's top), a FIXED height
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
// fifteen rows, and every other value list its key's whole domain).
// THE KEYBOARD NEVER STANDS UNDER IT (the record at onscreen_keyboard::stands),
// so its stack is the unlifted one, keyboard height 0.
inline GuiRect surface_rect(const AppState& a) {
    return modal_panel_rect(a, /*keyboard_h=*/0);
}

// The rows' band: THE BOX'S INTERIOR, inside the canvas column's ring
// (modal_box_interior_rect; the list's ground the box's flat field). Every
// geometry below reads this and not the surface — the rows,
// the scroll ceiling, the keep-visible walk, the painter's row-walk clip and
// row_at's containment — so a row can never paint over the ring or be
// pressed through it. THE FIRST ROW STANDS AT THE BOX'S TOP, one device px
// under the ring (row_inset_px).
// (THE FOLDER NAME ABOVE THE PLAYLIST — the viewed folder's name in one list
// row and an etched pair under it, the render player's alone, 2026-10-10 to
// 2026-10-14 — IS REMOVED, architect 2026-10-14: "it was icon padding but no
// icon, the dividing line sits awkwardly". If it is ever wanted back, the one
// road that broke no pattern was the folder name in the caption's title (no
// caption of the app's own since 2026-10-24). The
// car's MediaSession artist line keeps its own folder name, the playing
// item's, GuiRenderPlayer's.)
inline GuiRect content_rect(const AppState& a) {
    return modal_box_interior_rect(a, /*keyboard_h=*/0);
}

// -- The scroll state --------------------------------------------------------

// The scroll offset's ceiling: how much of the content lies past the band.
inline int max_scroll_px(const AppState& a) {
    const GuiRect band = content_rect(a);
    return std::max(0, content_height_px(a) - band.h);
}

// THE ONE CLAMP of the scroll offset onto [0, max_scroll_px]. Every writer
// (the wheel, the drag, the row walk, the listing rebuild) calls it after its
// write, so a listing that shrank under a standing offset lands honestly.
inline void clamp_scroll(AppState& a) {
    a.folder_overlay.scroll_px =
        std::clamp(a.folder_overlay.scroll_px, 0, max_scroll_px(a));
}

// -- The rows ----------------------------------------------------------------

// Row `index`'s PAINTED rect under the live scroll offset — full band width
// less the rows' inset on both sides, at the button's row pitch (the painter
// and the press router read their rects from here and nowhere else). It may
// lie partly or wholly outside the band; the painter clips and the hit test
// asks the band first.
inline GuiRect row_rect(const AppState& a, int index) {
    const GuiRect band = content_rect(a);
    const int h = button_row_height_px();
    const int y = band.y + row_inset_px() + index * (h + button_row_gap_px()) -
                  a.folder_overlay.scroll_px;
    return GuiRect{band.x + row_inset_px(), y, band.w - 2 * row_inset_px(), h};
}

// THE ONE WALK OVER THE ROWS, and the reason paint and hit cannot drift: both
// go through it. `fn(index, row, rect)` is called for every row in listing
// order with its painted rect under the live scroll offset.
template <class Fn>
inline void for_each_row(const AppState& a, Fn&& fn) {
    const int n = static_cast<int>(a.folder_overlay.rows.size());
    for (int i = 0; i < n; ++i) {
        fn(i, a.folder_overlay.rows[static_cast<size_t>(i)], row_rect(a, i));
    }
}

// The row under (x, y), or -1 — a point outside the ROWS' BAND (content_rect,
// the box's interior) answers -1 whatever row's rect would contain it, so a
// row scrolled out of the band cannot be pressed through the ring or the
// controls row below. It is the content rect and not the surface because the
// surface is the band's outer CLAIM (the press router's own test, one call
// up) while the rows live in the content — the box inside its ring.
// The painter clips its row walk to this same rect, so paint and hit agree on
// every pixel of the band.
inline int row_at(const AppState& a, int x, int y) {
    const GuiRect band = content_rect(a);
    if (!rect_contains(band, x, y)) return -1;
    int hit = -1;
    for_each_row(a, [&](int i, const AppState::FolderOverlayRow&,
                        const GuiRect& r) {
        if (hit < 0 && rect_contains(r, x, y)) hit = i;
    });
    return hit;
}

// Scroll so row `index` lies wholly inside the band (its inset respected) —
// the keyboard row walk's owner of "keep the focused row visible", moving the
// offset by the least amount that does it and nothing when the row already
// shows. Clamps.
inline void scroll_row_into_view(AppState& a, int index) {
    const int n = static_cast<int>(a.folder_overlay.rows.size());
    if (index < 0 || index >= n) return;
    const GuiRect band = content_rect(a);
    const GuiRect r    = row_rect(a, index);
    const int top      = band.y + row_inset_px();
    const int bottom   = band.y + band.h - row_inset_px();
    if (r.y < top) {
        a.folder_overlay.scroll_px -= (top - r.y);
    } else if (r.y + r.h > bottom) {
        a.folder_overlay.scroll_px += (r.y + r.h) - bottom;
    }
    clamp_scroll(a);
}

// THE OPEN'S LANDING (architect 2026-10-26: "normally you'd expect a drop
// down list to center it if possible, or get as close to center"): the
// offset that puts row `index`'s center on the band's center, the
// difference rounded at the element, then the one clamp — so a row near
// either end of the listing lands as close to center as the ends allow, and a
// listing shorter than the band never scrolls. The five open-time seats
// call it after their seat (the project picker's, the settings page's and
// the value list's builds, the trim presets list's open, the render
// player's every rebuild, each a
// fresh listing — the page's first key row, row 1, lands at the band's top,
// the clamp ruling a centered row 1 out, and a value list's return seats the
// page on that key's row); the
// keyboard walk and the pointer's seats keep scroll_row_into_view's
// least-move rule (an arrow step scrolls by one row, every list widget's
// rule). Returns whether the offset moved.
inline bool scroll_row_centered(AppState& a, int index) {
    const int n = static_cast<int>(a.folder_overlay.rows.size());
    if (index < 0 || index >= n) return false;
    const int     before = a.folder_overlay.scroll_px;
    const GuiRect band   = content_rect(a);
    const GuiRect r      = row_rect(a, index);
    a.folder_overlay.scroll_px += static_cast<int>(std::nearbyint(
        (r.y + r.h / 2.0) - (band.y + band.h / 2.0)));
    clamp_scroll(a);
    return a.folder_overlay.scroll_px != before;
}

// -- The highlight and the scroll, the mechanics every content drives --------
//
// THE DAMAGE IS THE CALLER'S: each of the movers answers whether the band's
// pixels changed and writes none of them, so the panel needs no Viewport and
// no include of one — the player damages through its own helper, the
// picker's four contents through the input handler's. Nothing else about the highlight lives
// anywhere else: what the band MEANS is the owner's, where it can sit is
// here.

// IS ROW `index` INERT? (architect 2026-10-28, the settings page's group
// rows: "keep") — a Group row, THE ONE PREDICATE every refusal of the
// highlight and the open asks: set_highlight below will not seat one, the
// walk steps over them (move_highlight_target), and the row press's
// motionless lift on one is a consumed nothing
// (finish_folder_overlay_release). Its press still arms, the same press
// being the band's scroll drag; the painter keeps it unlit
// (paint_folder_overlay). False outside the listing.
inline bool row_is_inert(const AppState& a, int index) {
    const int n = static_cast<int>(a.folder_overlay.rows.size());
    if (index < 0 || index >= n) return false;
    return a.folder_overlay.rows[static_cast<size_t>(index)].kind ==
           AppState::FolderOverlayRow::Kind::Group;
}

// Seat the highlight on `index`, clamped into the listing (-1 for an empty
// one), scrolling the band to keep it visible. Returns whether the band
// changed. AN INERT ROW IS NEVER SEATED (row_is_inert): a seat on one is
// refused whole, the band unchanged — every seat names an actionable row
// by construction (the builders' seats, the walk's landing, a lift on an
// acting row), so the refusal is the rule's statement, not a road.
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
        const int before = ov.scroll_px;
        scroll_row_into_view(a, to);
        if (ov.scroll_px != before) changed = true;
    }
    return changed;
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
inline int walk_origin_row(const AppState& a) {
    return a.folder_overlay.highlight_row < 0 ? 0
                                              : a.folder_overlay.highlight_row;
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
inline int move_highlight_target(const AppState& a, int delta) {
    const int n = static_cast<int>(a.folder_overlay.rows.size());
    if (n <= 0) return -1;
    const int landing = std::clamp(walk_origin_row(a) + delta, 0, n - 1);
    const int step    = delta < 0 ? -1 : +1;
    for (int to = landing; to >= 0 && to < n; to += step)
        if (!row_is_inert(a, to)) return to;
    for (int to = landing - step; to >= 0 && to < n; to -= step)
        if (!row_is_inert(a, to)) return to;
    return -1;
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
inline bool move_highlight_actionable(const AppState& a, int delta) {
    const int to = move_highlight_target(a, delta);
    return to >= 0 && to != a.folder_overlay.highlight_row;
}

// Scroll the band by `rows` rows (the wheel's detent step) at the button
// row's pitch, clamped. Returns whether the offset moved.
inline bool scroll_rows(AppState& a, int rows) {
    const int before = a.folder_overlay.scroll_px;
    a.folder_overlay.scroll_px +=
        rows * (button_row_height_px() + button_row_gap_px());
    clamp_scroll(a);
    return a.folder_overlay.scroll_px != before;
}

} // namespace folder_overlay
