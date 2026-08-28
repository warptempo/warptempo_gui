#pragma once

// THE FOLDER OVERLAY — the keyboard-slot LIST PANEL (architect design
// 2026-08-28). ONE WIDGET, THREE CONTENTS: the RENDER PLAYER's output folders
// and their wavs, the OPEN PROJECT PICKER's valid project folders, and the
// HISTORY PICKER's walk members. Which one fills it is
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
// WHAT IT IS. A flat list of rows — an UP row (`..`, the parent), FOLDER rows,
// WAV rows and TEXT rows (a glyph-less kind, the history picker's) — each an
// icon (or none) and a name, painted in THE ON-SCREEN KEYBOARD'S OWN BAND:
// full window width, sitting directly above the bottom row over the waveform
// area's lower part, which the waveform's passes then do not paint
// (onscreen_keyboard::waveform_paint_area, whose gate reads both tenants and
// whose clip reads the STANDING one's own rect). The architect's ruling (R3):
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
//     keyboard's surface_rect reads too). Its HEIGHT IS ITS CONTENT'S, up to
//     THE CEILING: the panel grows a row at a time until it reaches the
//     MIDDLE OF THE WAVEFORM (keyboard_slot_max_height_px, the same header),
//     and past that it stops growing and scrolls. R33 in one line — "it grows
//     with its content up to that cap, then scrolls" — after the fixed
//     four-row band read too short in use.
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
// the same press may become the band's scroll drag — and a motionless lift
// HIGHLIGHTS the row (the band moves onto it and nothing else happens:
// architect 2026-08-28, "a single click highlights"). OPENING a row is the
// DOUBLE-CLICK (the marker flag's own seed shape, the second press acting at
// the press) or Enter on the highlight. The arm's whole state is
// AppState::FolderOverlayPress; a shift or ctrl press on a row is a consumed
// no-op, and no row reads a hold. WHAT THE OPEN ACT MEANS IS THE OWNER'S (a lift
// highlights under every owner — the pickers have no field beside the band):
// under the PLAYER an open enters a folder, goes up, or plays a wav (the Play
// button and Space act on the highlight too); under the PROJECT PICKER it
// reopens the row's project; under the HISTORY PICKER it loads the row's
// member in place (OK and Enter open the highlight on both). The fork is at
// the press router and the key routers, never here — the panel knows rows,
// not projects or commits.

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
// glyph. A folder row wears icons::Icon::Folder, a wav row
// icons::Icon::AudioXWav, the up row the folder glyph too (it names a folder).
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
// between them. Zero for an empty listing. (Declared ahead of the band, which
// is sized by it.)
inline int content_height_px(const AppState& a) {
    const int n = static_cast<int>(a.folder_overlay.rows.size());
    if (n <= 0) return 0;
    return 2 * pad_px() + n * row_height_px() + (n - 1) * row_gap_px();
}

// THE BAND: the slot's band (its x, its width and its bottom edge — one
// owner, app_state.h), AS TALL AS ITS CONTENT UP TO THE CEILING. Like the
// keyboard's own accessor it does not ask whether the panel stands.
//
// AN EMPTY LISTING ANSWERS A ZERO-HEIGHT RECT and needs no guard of its own:
// no content can stand empty (the player refuses "No renders to play", the
// project picker always lists at least the project that is open, and the
// history picker's opener refuses an empty walk), and the painter and the
// hit test already read a zero rect as nothing.
inline GuiRect surface_rect(const AppState& a) {
    return keyboard_slot_band(
        a, std::min(content_height_px(a), keyboard_slot_max_height_px(a)));
}

// THE BAND'S DAMAGE RECT — the band AT THE CEILING, whatever the listing is:
// the panel grows and shrinks UPWARD from a fixed bottom edge, so an act that
// SHORTENS it (a rebuild into a smaller folder) leaves the departed rows'
// pixels standing above the new band, and a rect sized to the new content
// would not erase them. Every damage that can change the listing takes this
// rect; the damages that only move something INSIDE a standing band — the
// scroll, the highlight, one row's face — take surface_rect or the row's own,
// which is the whole of what they touched.
inline GuiRect band_damage_rect(const AppState& a) {
    return keyboard_slot_band(a, keyboard_slot_max_height_px(a));
}

// -- The scroll state --------------------------------------------------------

// The scroll offset's ceiling: how much of the content lies past the band.
inline int max_scroll_px(const AppState& a) {
    const GuiRect surf = surface_rect(a);
    return std::max(0, content_height_px(a) - surf.h);
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
    const GuiRect surf = surface_rect(a);
    const int h = row_height_px();
    const int y = surf.y + pad_px() + index * (h + row_gap_px()) -
                  a.folder_overlay.scroll_px;
    return GuiRect{surf.x + pad_px(), y, surf.w - 2 * pad_px(), h};
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
    const GuiRect surf = surface_rect(a);
    const GuiRect r    = row_rect(a, index);
    const int top      = surf.y + pad_px();
    const int bottom   = surf.y + surf.h - pad_px();
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
// no include of one — the player damages through its own helper, the pickers
// through the input handler's. Nothing else about the highlight lives
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
