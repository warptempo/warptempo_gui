#pragma once

// THE SCORE VIEWER (architect 2026-10-26) — the movement's sheet music ONE
// PAGE AT A TIME, "like the media player, with the page centered, zoomed to
// fit onto the available area". Each movement's project names its score
// folder through `score_path` (four movements may share one; a pages.txt in
// the piece's own `score/` needs no naming), the score viewer's own file
// (score_viewer_file.h) holding it with the range and the positions; the
// folder is tools/score_extract.py's output, read at the load
// (score_pages.h). The
// viewer's page numbers are pages.txt's, never the printed ones.
//
// NOTHING FOLLOWS THE AUDIO (architect: "no tolerance, no intelligent sync …
// it's not going to work"): no sync, no follow, no playback under it — its
// open takes the modal-open stop like every surface in the modal family's
// box (GuiPlaybackLifecycle::stop_playback_for_modal_open's table), and Space
// binds nothing in it.
//
// WHAT IT IS AND WHERE IT STANDS: an OVERLAY of the interface class — its
// opener key (the SCORE key, bare `m`) sits lit and closes it, and it stands
// under its own question (RESET's) — painted in THE MODAL FAMILY'S BOX
// (modal_box.h, the geometry's one owner): the box's interior is the page,
// SCALED TO FIT by the smaller of the two ratios and centered on both axes,
// the rest the field ground (paint_modal_dialog's score branch); the
// CONTROLS ROW under it carries, left to right, PREV and NEXT (the render
// player's transport seats), THE PAGE SCRUB (the player's scrub, its domain
// the viewing range's pages), THE PAGE CELL where the player's clock stands
// ("7 / 15", the shown page's ordinal within the range and the range's
// count) and, flush right, RESET, START, END and CLOSE. Its state is
// AppState::ScoreViewer and the piece's range keys (app_state.h); this
// struct owns the acts. It is the sixth modal owner
// (AppState::ModalDialogOwner::Score), ranked with the player and the
// picker, and NO TAB SWITCH HAPPENS UNDER IT ("just like with other
// modals"): its router consumes Ctrl+Tab, so the active tab is the viewer's
// for its whole life.
//
// THE VIEWING RANGE — `score_first_page` / `score_last_page`, 0 the edge's
// placeholder — is THE TRIM'S CLASS ("that would be how we would trim
// effectively", "no undo, just like with regular trim"): START sets the first
// to the shown page, END the last, RESET (behind its question, "because
// there's no undo") both to 0; no undo entry, no dirty mark, each written
// through to the score viewer's file at once (write_score_viewer_file,
// score_viewer_file.h). THE QUESTION STANDS OVER THE VIEWER as the load confirmation
// stands over the render player (the model for standing), and it opens with
// CANCEL FOCUSED as the render player's Delete question does (the model for
// the focus: a destructive act with no undo behind it), so a bare Enter
// cancels. The three are AUTHORING and grey on a read-only tab and under
// grid iterations (score_viewer_range_locked); viewing, paging and the
// position's write are not, and stay live under both. The shown page is
// always inside the range, so START and END can never cross the pair.
//
// THE REMEMBERED POSITION — ViewState::score_position, the active tab's — is
// the playhead's class: EVERY PAGE CHANGE writes the new page's `begin`
// (show_page), through to the score viewer's file at once, and the open shows the page that holds it, clamped into the
// range (score_page_for_position).
//
// THE PAGES ARE DECODED THROUGH THE MATERIALS' ROAD (svg_icon.h: raster_info,
// raster_regions at k = 1 — the decoded pixels — and convert_to_display for
// the P3 window, a page being an authored sRGB picture like every raster),
// the shown page synchronously at its change and its two neighbors in the
// range one per run-loop tick after it has been painted (tick): MEASURED on
// the laptop 2026-10-26 over the K.550 folder's 2304 x 1296 PNGs, 47 to 55 ms
// a page, file read included, so three are about 150 ms, over the 100 ms a
// synchronous trio may cost. AT MOST THREE ARE KEPT, dropped at the close. A
// page whose decode fails here (its file was checked at the load) is a rare
// non-silent fault: one stderr line, the interior stays the field ground, and
// that page is not decoded again while the viewer stands.

#include "app_state.h"
#include "playback_lifecycle.h"
#include "viewport.h"

#include <cstdint>

// THE VIEWING RANGE, RESOLVED: page INDICES into `score->pages`, `first` to
// `last` inclusive, never crossing. A stored 0 is the edge (the first page,
// the last page); a stored number past the page count (a regenerated
// pages.txt) clamps to the last page — the display-scratch convention the
// viewport and the playhead follow. Precondition: a score is loaded.
struct ScoreViewerRange {
    int first = 0;
    int last  = 0;
    int count() const { return last - first + 1; }
};
inline ScoreViewerRange score_viewer_range(const AppState& a) {
    const int n = a.score->count();
    const auto edge = [n](int64_t stored, int at_zero) {
        if (stored <= 0) return at_zero;
        return static_cast<int>(std::min<int64_t>(stored, n)) - 1;
    };
    ScoreViewerRange r;
    r.first = edge(a.score_first_page, 0);
    r.last  = edge(a.score_last_page, n - 1);
    // The score viewer's file never yields a crossed nonzero pair (its
    // reader takes the defaults for one, score_range_crossed) and the keys
    // never write one, so this holds by construction; a first past the page count beside a
    // last of 0 resolves to the last page on both.
    if (r.first > r.last) r.first = r.last;
    return r;
}

// THE PAGE AT A POSITION (the open's landing): the first page whose range
// holds `pos` or lies after it — a frame no page holds (a fade's gap, a
// regenerated pages.txt) landing on the nearest page at or after it — else
// the range's last, then clamped into the range.
inline int score_page_for_position(const AppState& a, int64_t pos) {
    const ScoreViewerRange r = score_viewer_range(a);
    int at = r.last;
    for (int i = 0; i < a.score->count(); ++i) {
        if (a.score->pages[static_cast<std::size_t>(i)].end > pos) {
            at = i;
            break;
        }
    }
    return std::clamp(at, r.first, r.last);
}

// THE RANGE KEYS' LOCK: START, END and RESET author the piece's range, so a
// read-only active tab and grid iterations refuse them — the trim gestures'
// sibling made authoring by the architect's ruling (the head) — through the
// one lock predicate (authoring_locked).
inline bool score_viewer_range_locked(const AppState& a) {
    return authoring_locked(a);
}

// EACH BUTTON'S TRUTHFUL FACE (the roster's rule: a button greys whenever
// its act would be a no-op): PREV and NEXT at the range's edges; START at
// the range's first page, END at its last, RESET when the range already is
// every page, all three under the lock; CLOSE never. The act asks the same.
inline bool score_viewer_button_enabled(const AppState& a,
                                        AppState::ScoreButtonAct act) {
    using Act = AppState::ScoreButtonAct;
    if (!a.score_viewer.active || !a.score) return false;
    const ScoreViewerRange r = score_viewer_range(a);
    const int p = a.score_viewer.page;
    switch (act) {
        case Act::PrevPage: return p > r.first;
        case Act::NextPage: return p < r.last;
        case Act::Reset:
            return !score_viewer_range_locked(a) &&
                   !(r.first == 0 && r.last == a.score->count() - 1);
        case Act::SetStart:
            return !score_viewer_range_locked(a) && p != r.first;
        case Act::SetEnd:
            return !score_viewer_range_locked(a) && p != r.last;
        case Act::Close: return true;
        case Act::None:  break;
    }
    return false;
}

// THE KEYS' PRINTED LEGENDS (the hi-fi skin's, in capitals under the key).
inline const char* score_viewer_button_legend(AppState::ScoreButtonAct act) {
    using Act = AppState::ScoreButtonAct;
    switch (act) {
        case Act::PrevPage: return "PREV";
        case Act::NextPage: return "NEXT";
        case Act::Reset:    return "RESET";
        case Act::SetStart: return "START";
        case Act::SetEnd:   return "END";
        case Act::Close:    return "CLOSE";
        case Act::None:     break;
    }
    return "";
}

// THE TOOLTIPS: the act's name, Title Case, and the key it binds in the
// roster's and the player's own form (render_player_button_hint); the range
// keys bind none.
inline const char* score_viewer_button_hint(AppState::ScoreButtonAct act) {
    using Act = AppState::ScoreButtonAct;
    switch (act) {
        case Act::PrevPage: return "Previous Page (Left)";
        case Act::NextPage: return "Next Page (Right)";
        case Act::Reset:    return "Reset Range";
        case Act::SetStart: return "Set as Start";
        case Act::SetEnd:   return "Set as End";
        case Act::Close:    return "Close (Esc)";
        case Act::None:     break;
    }
    return "";
}

// THE PAGE SCRUB'S MAPPING over the painter's PUBLISHED item rect
// (AppState::ModalDialogGeometry::scrub), the player's travel exactly
// (render_player_scrub_travel_x0 / _usable_span): the range's first page at
// the channel's first column, its last at the last, page i at
// (i − first) / (count − 1) of the travel; the inverse rounds to the nearest
// page. One page: the thumb at the left, the scrub dead (the press claim).
inline int score_scrub_x_of(const AppState& a, int page) {
    const GuiRect track = a.modal_dialog.scrub;
    const int x0   = render_player_scrub_travel_x0(track);
    const int span = render_player_scrub_usable_span(track);
    const ScoreViewerRange r = score_viewer_range(a);
    if (span <= 0 || r.count() <= 1) return x0;
    const double t = static_cast<double>(page - r.first) /
                     static_cast<double>(r.count() - 1);
    return x0 + static_cast<int>(std::nearbyint(t * span));
}
inline int score_scrub_page_at(const AppState& a, int x) {
    const GuiRect track = a.modal_dialog.scrub;
    const int x0   = render_player_scrub_travel_x0(track);
    const int span = render_player_scrub_usable_span(track);
    const ScoreViewerRange r = score_viewer_range(a);
    if (span <= 0 || r.count() <= 1) return r.first;
    const int cx = std::clamp(x, x0, x0 + span);
    const double t = static_cast<double>(cx - x0) / static_cast<double>(span);
    return r.first + static_cast<int>(
                         std::nearbyint(t * static_cast<double>(r.count() - 1)));
}

// THE SHOWN PAGE'S DECODED SURFACE, or null (not decoded yet, or its decode
// failed) — the painter's one read of the cache.
inline cairo_surface_t* score_viewer_shown_surface(const AppState& a) {
    for (const AppState::ScoreViewer::Decoded& d : a.score_viewer.cache)
        if (d.page == a.score_viewer.page) return d.surface;
    return nullptr;
}

struct GuiScoreViewer {
    AppState&             app;
    Viewport&             viewport;
    GuiPlaybackLifecycle& playback_lifecycle;
    GuiNotifications&     notifications;

    GuiScoreViewer(AppState& app_, Viewport& viewport_,
                   GuiPlaybackLifecycle& playback_lifecycle_,
                   GuiNotifications& notifications_)
        : app(app_),
          viewport(viewport_),
          playback_lifecycle(playback_lifecycle_),
          notifications(notifications_) {}

    // THE ONE OPENER — the SCORE key's lift and bare `m` (the dispatch arm,
    // input_key_dispatch.cpp). A piece with no score cards why (the key is
    // grey, so the keyboard alone reaches that refusal); a standing prompt is
    // silent (it veils everything). The folder overlay's contents never stand
    // under it: under them `m` is their routers' silent catch-all and the
    // SCORE key is grey, so no road reaches here (the player's and the
    // picker's own mutual exclusion, input_key_dispatch.cpp's
    // open_project_picker). Takes the modal-open stop, mints the session,
    // lands on the active tab's remembered position and damages the window.
    bool open();
    // THE ONE CLOSER — CLOSE, Esc, the SCORE key and bare `m`, and the exit
    // road's teardown (GuiPrompt::request_close): the cache dropped, the
    // scrub's arm dropped, the whole window damaged.
    void close();

    // THE ONE PAGE CHANGE: `index` clamped into the viewing range; a change
    // writes the active tab's remembered position (the page's begin),
    // decodes the page if it is not held, drops what the new neighborhood
    // does not need and damages the modal area. The same page is a no-op.
    void show_page(int index);
    // PREV / NEXT (and Left / Right): one page, walled at the range's edges.
    void step(int delta);
    // Home / End: the range's first / last page.
    void first_page();
    void last_page();

    // START / END: the range's first / last bound set to the shown page —
    // each refused, silently, where its key is grey.
    void set_start();
    void set_end();
    // RESET: raises its question (DialogTrigger::SCORE_RESET_CONFIRM, Cancel
    // focused — the head) where
    // its key is live; confirm_reset is the question's OK.
    void raise_reset();
    void confirm_reset();

    // THE RUN LOOP'S TICK: while the viewer stands, ONE missing neighbor of
    // the shown page decoded per call (the head's measured reason), so the
    // shown page's own frame paints first.
    void tick();

private:
    // Decode page `index` into the cache's free slot (the shown page's
    // neighborhood having been made room for); a failure takes its one
    // stderr line and leaves the slot naming the page with no surface.
    void decode_into_cache(int index);
    // Drop every cached page outside {shown − 1, shown, shown + 1}.
    void evict_outside_neighborhood();
    // The modal family's whole area: the page, the scrub, the cell, the keys.
    void damage();
};
