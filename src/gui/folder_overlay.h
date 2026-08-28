#pragma once

// THE FOLDER OVERLAY — the keyboard-slot LIST PANEL (architect design
// 2026-08-28). ONE WIDGET, TWO CONTENTS: the RENDER PLAYER's output folders
// and their wavs, and the OPEN PROJECT PICKER's valid project folders. Which
// one fills it is AppState::FolderOverlay::owner, and that tag IS the
// standing predicate (folder_overlay_stands, app_state.h; stands() below
// forwards to it). This header is the panel's ONE OWNER of everything that is
// not pixels, a press body or a content: the geometry (the band, the row
// pitch, the scroll clamp), the one walk over the rows, and the highlight and
// scroll mechanics both contents drive.
// The PAINTER lives in paint_handler.cpp beside every other painter
// and the PRESS ROUTER in input_pointer.cpp beside every other press router;
// both walk this file's row table through the one walker below, so paint and
// hit cannot describe different rows. The ROW TABLE ITSELF is
// AppState::folder_overlay (app_state.h), where its every field is described.
//
// WHAT IT IS. A flat list of rows — an UP row (`..`, the parent), FOLDER rows
// and WAV rows — each an icon and a name, painted in THE ON-SCREEN
// KEYBOARD'S OWN BAND: full window width, sitting directly above the bottom
// row over the waveform area's lower part, which the waveform's passes then
// do not paint (onscreen_keyboard::waveform_paint_area, whose gate reads
// both tenants). The architect's ruling (R3): "the overlay sits in the
// on-screen keyboard's place above the bottom strip, replacing the keyboard
// there" — on glass the keyboard would occupy that space, and neither use of
// the panel needs typing. So THE OVERLAY AND THE KEYBOARD NEVER BOTH STAND:
// onscreen_keyboard::stands carries `!folder_overlay::stands` as its third
// term. Unlike the keyboard the panel holds VARIABLE-LENGTH content and
// SCROLLS when a listing outgrows the band — the wheel on plastic, the
// band's own drag on glass, and the keyboard's row walk keeping the focused
// row visible. No header row, no columns, no scrollbar: the offset is the
// whole scroll state, and an over-long listing simply scrolls.
//
// THE GEOMETRY IS THE KEYBOARD'S, READ THROUGH ITS ACCESSORS AND NEVER
// RESTATED: the band is onscreen_keyboard::surface_rect exactly (one owner,
// no second geometry), the row height is the key height, the gap between
// rows the key gap, the inset the keyboard's pad — so about four rows show
// at once and the panel's ground, row height and type are the keyboard's
// (Breeze; the key height is the finger-row precedent). The palette is the
// keyboard's three constants plus the dropdown's hover fill and the modal's
// focus line, every one an existing constant (the ladder is at the painter).
//
// THE ROWS ARE CHROME (conventions.md's third clause): a row press ARMS —
// the same press may become the band's scroll drag — and a motionless lift
// HIGHLIGHTS the row (the band moves onto it and nothing else happens:
// architect 2026-08-28, "a single click highlights"). OPENING a row is the
// DOUBLE-CLICK (the marker flag's own seed shape, the second press acting at
// the press) or Enter on the highlight. The arm's whole state is
// AppState::FolderOverlayPress; a shift or ctrl press on a row is a consumed
// no-op, and no row reads a hold. WHAT THE TWO ACTS MEAN IS THE OWNER'S: under
// the PLAYER a lift only highlights and an open enters a folder, goes up, or
// plays a wav (the Play button and Space act on the highlight too); under the
// PICKER a lift highlights AND writes the row's name into the Open prompt's
// field, and an open runs that prompt's own commit. The fork is at the press
// router and the key router, never here — the panel knows rows, not projects.

#include "app_state.h"
#include "onscreen_keyboard.h"
#include "render.h"

#include <algorithm>
#include <cstdint>

namespace folder_overlay {

// THE ROW GLYPH'S SIZE — the roster's 22 px icon box (the icon row's
// kIconGlyphPx, paint_handler.cpp), restated here as the same authored number
// because that constant is the painter file's own. A folder row wears
// icons::Icon::Folder, a wav row icons::Icon::AudioXWav, the up row the folder
// glyph too (it names a folder).
inline constexpr double kRowIconPx = 22.0;
// The gap between a row's icon and its name, authored.
inline constexpr double kRowIconGapPx = 8.0;

inline int row_icon_px()     { return scaled_px(kRowIconPx, 1); }
inline int row_icon_gap_px() { return scaled_px(kRowIconGapPx); }

// The keyboard's three numbers, read through its accessors (the ruling above).
inline int row_height_px() { return onscreen_keyboard::key_height_px(); }
inline int row_gap_px()    { return onscreen_keyboard::key_gap_px(); }
inline int pad_px()        { return onscreen_keyboard::pad_px(); }

// -- Standing ----------------------------------------------------------------

// DOES THE PANEL STAND? THE OWNER TAG DECIDES and this is the panel's own
// name for that one question (the predicate itself is folder_overlay_stands,
// app_state.h — it has to live there because onscreen_keyboard.h reads it too
// and this header includes that one). It takes no platform term, unlike the
// keyboard's: the panel serves the pointer and the finger alike, so it stands
// on both backends. EVERY paint site and EVERY hit site asks this and nothing
// else.
inline bool stands(const AppState& a) {
    return folder_overlay_stands(a);
}

// -- The surface's rect ------------------------------------------------------

// THE BAND: the keyboard's surface rect, verbatim — one owner. Like that
// accessor it does not ask whether the panel stands.
inline GuiRect surface_rect(const AppState& a) {
    return onscreen_keyboard::surface_rect(a);
}

// -- The scroll state --------------------------------------------------------

// The content's whole height: the pad at both ends, every row and the gaps
// between them. Zero for an empty listing.
inline int content_height_px(const AppState& a) {
    const int n = static_cast<int>(a.folder_overlay.rows.size());
    if (n <= 0) return 0;
    return 2 * pad_px() + n * row_height_px() + (n - 1) * row_gap_px();
}

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

// -- The highlight and the scroll, the mechanics both contents drive ---------
//
// THE DAMAGE IS THE CALLER'S: each of the three answers whether the band's
// pixels changed and writes none of them, so the panel needs no Viewport and
// no include of one — the player damages through its own helper, the picker
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
