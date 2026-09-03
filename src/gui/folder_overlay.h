#pragma once

// THE FOLDER OVERLAY — the keyboard-slot LIST PANEL (architect design
// 2026-08-28). ONE WIDGET, THREE CONTENTS: the RENDER PLAYER's output folders
// and their wavs, the OPEN PROJECT PICKER's valid project folders, and — since
// 2026-09-03 — the AV SYNC STATS PANEL's text lines (Help → AV Sync Stats,
// av_sync_stats.h), the first content whose rows are INERT: no glyph, no
// highlight, no act, the band scrolling and consuming presses under it as it
// does under any content. (A fourth stood for one day, the `h` view's history
// picker; it was retired 2026-08-29 as overengineered, that view's `'` raising
// its confirmation on the viewed member with no list at all — and the TEXT
// KIND it introduced is what the stats panel produces now.) Which one fills it
// is
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
// icon and a name, and TEXT rows, which are a line and nothing else (an UP
// row, `..`, was a fourth kind until 2026-09-01, when
// the player moved inside `tmp/` and going up became a button on its modal
// row) — painted in THE ON-SCREEN KEYBOARD'S OWN BAND:
// full window width, standing from THE WINDOW'S TOP down to the bottom row
// (architect 2026-09-03: "remove top row in file picker/media player —
// kdenlive does not allow ctrl+q during modal so we don't need to either") —
// so EVERY LANE BUT THE BOTTOM ROW is under it, the menu row with its File
// anchor and its view bar included, and the waveform's own passes paint
// nothing (onscreen_keyboard::waveform_paint_area, whose gate reads both
// tenants and whose clip reads the STANDING one's own rect, and which this
// band reduces to a zero-height rect) while the lanes above paint and are
// covered. IT WEARS NO TOP BORDER — there is nothing above it to be framed
// off (the line it had from 2026-08-29 is retired with the ground it
// separated the panel from). The architect's ruling (R3):
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
// whole scroll state, and an over-long listing simply scrolls.
//
// THE BAND IS THE SLOT'S AND THE ROW IS THE BUTTON'S (architect 2026-08-28,
// R31/R33; before that day both were the keyboard's). Two owners, neither
// restated here:
//   * THE BAND takes the SLOT's x, its width and its BOTTOM EDGE — the bottom
//     row's own lane, lifted (keyboard_slot_band, app_state.h, which the
//     keyboard's surface_rect reads too). ITS HEIGHT IS THE CEILING'S WHOLE
//     EXTENT, every time it stands: from THE WINDOW'S TOP
//     (keyboard_slot_max_height_px, the same header) down to the bottom row,
//     whatever the listing's length — architect 2026-08-28, R35: "we should
//     automatically make the height ... so that it's not a fluid height —
//     it's always a fixed height", and 2026-09-03 for where that fixed height
//     starts: "remove top row in file picker/media player". A
//     SHORT LISTING LEAVES THE REST OF THE BAND AS GROUND and a long one
//     scrolls. R35 retired the growing half of R33 (which read "it grows with
//     its content up to that cap, then scrolls") the same day, the panel
//     having jumped under the pointer as the listings changed size; that half
//     stands, and only WHERE the ceiling sits has moved — R33's waveform
//     midpoint until 2026-09-02, row 1's foot for that one day, the window's
//     top since. The content's height stays the SCROLL CLAMP's
//     input and is nothing else's.
//   * THE ROW IS EXACTLY THE ICON ROW'S BUTTON: the same 32px box, the same
//     2px gap between boxes, the same corner radius, the same 22px glyph
//     centred at the box's own (32-22)/2 inset, every number read from
//     render.h's icon-button block where it is measured ("we've gone for the
//     button analogy"; "the buttons are good enough size for my finger"). A
//     NAME TOO LONG FOR THE LINE RUNS OFF THE EDGE — no wrap, no ellipsis
//     ("project and file names will be short"), the painter clipping it to
//     the row.
// The panel's ONE authored number of its own is the outer inset below.
//
// THE PALETTE IS THE FILE MANAGER'S, NOT THE KEYBOARD'S (R32, the ladder at
// the painter and the constants in render.h's palette block): the band's
// ground is kModalFieldGround, a resting row paints no fill at all, and the
// hover, selected and hovered+selected faces are the three kFolderRow*
// values — kdenlive's project bin and pcmanfm-qt's compact view, which agree.
// NO ALTERNATING ROWS.
//
// THE ROWS ARE CHROME (conventions.md's third clause): a row press ARMS —
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
// the PROJECT PICKER it reopens the row's project; under the STATS PANEL there
// is no open and no highlight at all, its rows being inert. Enter on the highlight is
// the keyboard's own click and reaches the same fork, which is at
// the press router and the key routers, never here — the panel knows rows,
// not projects.

// THIS HEADER DELIBERATELY DOES NOT INCLUDE onscreen_keyboard.h, and the
// dependency runs the other way (that header includes this one, for the
// standing tenant's rect in waveform_paint_area): since the rows became
// buttons the panel borrows nothing from the keyboard, and the band the two
// share is app_state.h's.
#include "app_state.h"
#include "render.h"

#include <algorithm>
#include <cstdint>

namespace folder_overlay {

// -- The geometry ------------------------------------------------------------
//
// THE ROW IS THE ICON ROW'S BUTTON (the ruling above): every number below is
// that button's own, read from render.h where it is measured, so a retune of
// the roster carries down here by construction and this file spells no
// literal of its own but the two it owns.

// THE PANEL'S OUTER INSET, its ONE authored number — the margin between the
// band's edge and the rows, at the top, the bottom and both sides.
// pcmanfm-qt's compact view keeps 1 px between an item and the frame and the
// architect's ruling lets it "grow proportionally with the taller rows"; 2
// authored px is that margin at this row height, and it scales like every
// other authored length.
inline constexpr double kPanelPadPx = 2.0;
// The gap between a row's icon and its name, authored, the row's own.
inline constexpr double kRowIconGapPx = 8.0;

// THE ROW BOX: the button's box, the button's between-boxes gap, the button's
// glyph. A folder row wears icons::Icon::Folder and a wav row
// icons::Icon::AudioXWav. NOT EVERY ROW HAS ONE: the glyph-less TEXT kind,
// which went with the history picker on 2026-08-29, is BACK WITH A PRODUCER
// since 2026-09-03 — the AV sync stats panel's every row — and its text starts
// where the glyph would have, at the button's own inset. (The UP kind, which
// wore the folder glyph because it named a folder, went with the player's move
// inside `tmp/` on 2026-09-01 and has no producer.) The BOX is the same
// whatever the kind: a row is a wide button.
inline int row_height_px()   { return scaled_px(kIconBtnPx, 1); }
inline int row_gap_px()      { return scaled_px(kIconBtnGapPx, 1); }
inline int pad_px()          { return scaled_px(kPanelPadPx); }
inline int row_icon_px()     { return scaled_px(kIconGlyphPx, 1); }
inline int row_icon_gap_px() { return scaled_px(kRowIconGapPx); }
// THE GLYPH'S INSET INSIDE THE ROW, on every side: the button's own centring
// of its glyph in its box, (32 - 22) / 2 at 100%, taken from the two scaled
// numbers so it cannot disagree with either. It is the LEFT PAD of the icon in
// the row as well — a row is a wide button — and not the modal word buttons'
// text pad, which belongs to a different surface.
inline int row_icon_inset_px() { return (row_height_px() - row_icon_px()) / 2; }

// (THE BAND'S TOP BORDER IS RETIRED, architect 2026-09-03: "remove the top
// border since now there is nothing above the player". It stood from
// 2026-08-29 — the bottom row's own 1 px kRedesignTabLine, read from that
// row's accessor so the panel's two edges read alike — and its whole reason
// was the ground it ran into above. With the band starting at the window's
// top there is no such ground, and a line along the window edge would frame
// the panel off from nothing. `border_h_px` and the `content_rect` that
// subtracted it are both deleted: THE CONTENT RECT IS THE SURFACE RECT, and
// the rows, the scroll ceiling and the keep-visible walk all read
// surface_rect directly.)

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
// between them. Zero for an empty listing. IT SIZES NOTHING (R35 took the
// band's height off it): its ONE consumer is the scroll ceiling below, which
// is how far the content runs past the band.
inline int content_height_px(const AppState& a) {
    const int n = static_cast<int>(a.folder_overlay.rows.size());
    if (n <= 0) return 0;
    return 2 * pad_px() + n * row_height_px() + (n - 1) * row_gap_px();
}

// THE BAND: the slot's band (its x, its width and its bottom edge — one
// owner, app_state.h) AT THE CEILING'S WHOLE EXTENT, a FIXED height (R35, the
// ruling above). Like the keyboard's own accessor it does not ask whether the
// panel stands.
//
// IT IS THE PANEL'S ONE RECT, and that is what the fixed height bought: the
// band a damage must erase is the band that was painted, so a listing that
// SHRANK leaves nothing standing above a shorter one and there is no second
// "the band at its ceiling" rect to remember to use. (There was one —
// band_damage_rect, whose single reader was the player's listing rebuild — and
// it went with the growing band that gave it its reason: a rect exists iff it
// answers a question, and the two answers are now the same rect.) SINCE
// 2026-09-03 IT IS THE ROWS' BAND TOO: the top border retired with the ground
// above it, and the content rect that subtracted that border went with it, so
// the ground, the damage, the hit test, the rows and the scroll ceiling all
// read this one rect.
//
// A degenerate stack answers a zero-height rect, which the painter and the hit
// test already read as nothing. An EMPTY LISTING is a painted band with no
// rows in it — no content can stand empty anyway (the player refuses with
// "Nothing to play: no renders under tmp/", the project picker
// always lists at least the project that is open, and the stats panel's rows
// are composed rather than enumerated, so it always has some).
inline GuiRect surface_rect(const AppState& a) {
    return keyboard_slot_band(a, keyboard_slot_max_height_px(a));
}

// -- The scroll state --------------------------------------------------------

// The scroll offset's ceiling: how much of the content lies past the band.
inline int max_scroll_px(const AppState& a) {
    const GuiRect band = surface_rect(a);
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
// less the pad on both sides, at the row pitch. It may lie partly or wholly
// outside the band; the painter clips and the hit test asks the band first.
inline GuiRect row_rect(const AppState& a, int index) {
    const GuiRect band = surface_rect(a);
    const int h = row_height_px();
    const int y = band.y + pad_px() + index * (h + row_gap_px()) -
                  a.folder_overlay.scroll_px;
    return GuiRect{band.x + pad_px(), y, band.w - 2 * pad_px(), h};
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

// The row under (x, y), or -1 — a point outside the band answers -1 whatever
// row's rect would contain it, so a row scrolled out of the band cannot be
// pressed through the waveform above or the bottom row below.
inline int row_at(const AppState& a, int x, int y) {
    const GuiRect surf = surface_rect(a);
    if (!rect_contains(surf, x, y)) return -1;
    int hit = -1;
    for_each_row(a, [&](int i, const AppState::FolderOverlayRow&,
                        const GuiRect& r) {
        if (hit < 0 && rect_contains(r, x, y)) hit = i;
    });
    return hit;
}

// Scroll so row `index` lies wholly inside the band (its pad respected) —
// the keyboard row walk's owner of "keep the focused row visible", moving the
// offset by the least amount that does it and nothing when the row already
// shows. Clamps.
inline void scroll_row_into_view(AppState& a, int index) {
    const int n = static_cast<int>(a.folder_overlay.rows.size());
    if (index < 0 || index >= n) return;
    const GuiRect band = surface_rect(a);
    const GuiRect r    = row_rect(a, index);
    const int top      = band.y + pad_px();
    const int bottom   = band.y + band.h - pad_px();
    if (r.y < top) {
        a.folder_overlay.scroll_px -= (top - r.y);
    } else if (r.y + r.h > bottom) {
        a.folder_overlay.scroll_px += (r.y + r.h) - bottom;
    }
    clamp_scroll(a);
}

// -- The highlight and the scroll, the mechanics every content drives --------
//
// THE DAMAGE IS THE CALLER'S: each of the three answers whether the band's
// pixels changed and writes none of them, so the panel needs no Viewport and
// no include of one — the player damages through its own helper, the other
// two contents through the input handler's. Nothing else about the highlight lives
// anywhere else: what the band MEANS is the owner's, where it can sit is
// here.

// Seat the highlight on `index`, clamped into the listing (-1 for an empty
// one), scrolling the band to keep it visible. Returns whether the band
// changed.
inline bool set_highlight(AppState& a, int index) {
    AppState::FolderOverlay& ov = a.folder_overlay;
    const int n  = static_cast<int>(ov.rows.size());
    const int to = n <= 0 ? -1 : std::clamp(index, 0, n - 1);
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

// Move the highlight by `delta` rows, clamped; an empty listing has nothing
// to move and a -1 highlight walks from row 0. Returns whether the band
// changed.
inline bool move_highlight(AppState& a, int delta) {
    const int n = static_cast<int>(a.folder_overlay.rows.size());
    if (n <= 0) return false;
    const int from = a.folder_overlay.highlight_row < 0
                         ? 0 : a.folder_overlay.highlight_row;
    return set_highlight(a, std::clamp(from + delta, 0, n - 1));
}

// Scroll the band by `rows` rows (the wheel's detent step), clamped. Returns
// whether the offset moved.
inline bool scroll_rows(AppState& a, int rows) {
    const int before = a.folder_overlay.scroll_px;
    a.folder_overlay.scroll_px += rows * (row_height_px() + row_gap_px());
    clamp_scroll(a);
    return a.folder_overlay.scroll_px != before;
}

} // namespace folder_overlay
