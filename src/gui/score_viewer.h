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
// IT PLAYS ITS OWN SOUNDTRACK AND NEVER FOLLOWS THE GUI (architect
// 2026-10-26 night: "the score player is essentially a separate media player.
// It has nothing to do with what's going on in the GUI, other than I would
// manually be keeping them both in sync; there's no mechanism for doing that
// automatically"; the afternoon's "no tolerance, no intelligent sync … it's
// not going to work" stands for the GUI's playhead): the viewer plays the
// score folder's audio.wav (score_audio.h, AppState::score_audio — the
// video's own soundtrack, which pages.txt was cut from), and nothing of the
// GUI's transport moves: its open takes the modal-open stop like every surface
// in the modal family's box (GuiPlaybackLifecycle::stop_playback_for_modal_
// open's table), the project's playhead rests where it was, and the engine is
// rebound to the project's own buffer at the close.
//
// WHAT IT IS AND WHERE IT STANDS: an OVERLAY of the interface class — its
// opener key (the SCORE key, bare `m`) sits lit and closes it, and it stands
// under its own question (RESET's) — painted in THE MODAL FAMILY'S BOX
// (modal_box.h, the geometry's one owner): the box's interior is the page,
// SCALED TO FIT by the smaller of the two ratios and centered on both axes,
// the rest the field ground (paint_modal_dialog's score branch). It is the
// sixth modal owner (AppState::ModalDialogOwner::Score), ranked with the
// player and the picker, and NO TAB SWITCH HAPPENS UNDER IT ("just like with
// other modals"): its router consumes Ctrl+Tab, so the active tab is the
// viewer's for its whole life. Its state is AppState::ScoreViewer and the
// piece's range keys (app_state.h); this struct owns the acts.
//
// TWO FACES OF THE CONTROLS ROW, decided by whether the soundtrack is present
// (loaded or loading, GuiScoreAudio::present; architect 2026-10-26 night:
// "when there's audio, the scrub should just be a media player scrub, just
// like the render player's. The page numbers should not be included at all
// if there's audio. Everything — the playback, the scrub — should be frame
// and/or time based. No pages. Pages are only kept in the background"):
//   * THE MEDIA PLAYER'S, with the soundtrack: PLAY / PAUSE at the left (the
//     render player's transport seat, its key and its legend —
//     score_viewer_play_face), THE SCRUB (the render player's, its domain
//     the viewing range's FRAMES: the thumb at the cursor, a press off the
//     thumb jumping it, a drag CARRYING it and seeking nothing, THE SEEK AT
//     THE LIFT, the position written with it — architect 2026-10-26, "no
//     scrubbing" — and, the score's own, THE SOUND PAUSED WHILE THE SCRUB IS
//     HELD: a live soundtrack paused at the press and playing on from where
//     the thumb dropped at the lift — architect 2026-10-26 ~23:00, "it
//     wouldn't start playing until I commit by dropping the thumb"), THE
//     CLOCK (the
//     render player's two time fields, verbatim: the elapsed time within the
//     range and the range's length), then RESET, START, END and CLOSE flush
//     right. Space plays and pauses, Home / End seek the range's first /
//     last frame, Left / Right do nothing (no pages).
//   * THE PAGE ROAD'S, with no soundtrack (no readable audio.wav beside
//     pages.txt — always so on the PDF road, tools/score_extract.py's PDF
//     input, 2026-10-27): PREV and NEXT (the render player's
//     transport seats), THE PAGE SCRUB (its domain the viewing range's
//     pages; the phase-reset overlays' rule, drag anywhere, step quantized —
//     architect 2026-10-27, "the thumb follows the pen continuously, the
//     page under it changes in steps; the lift commits the page under the
//     thumb": the drag CARRIES the thumb as the media player's does, the page
//     under it shown as the thumb crosses into it, and the lift writes the
//     position), THE PAGE CELL where the player's clock stands ("7 / 15", the
//     shown page's ordinal within the range and the range's count) and the
//     same four keys flush right. Left / Right are PREV / NEXT, Home / End
//     the range's first / last page, Space nothing.
//
// THE TRANSPORT (the media player's face) is the render player's model,
// simplified — IDLE (the engine not on the soundtrack), PAUSED, LIVE
// (AppState::ScoreViewer::transport owns the states): PLAY runs [cursor, the
// range's end) ONCE and stops — no loop, no auto-advance, no car hook
// (car_transport.h stays the trim's). AT THE END IT CONTINUES IF THERE IS A
// NEXT TRACK AND STOPS DEAD WHERE IT IS OTHERWISE (architect 2026-10-26
// ~23:55, the render player's one rule; "if I'm working at the end of the
// song, I don't want it to go back to the beginning every time it hits the
// end"): the soundtrack is one track, so THE NATURAL END RESTS AT THE RANGE'S
// END, PAUSED there so the scrub stays live — the last page shown, the clock
// at the range's length, the thumb at the right — and the PLAY key pressed
// at that rest plays from the range's start (a press always sounds; the
// automatic rewind is what goes); a pause keeps the cursor and shows its
// page. A scrub dropped at the end of a live soundtrack reaches the same
// natural end (the lift plays on from the drop through the live seek's
// clamp, never the PLAY key's end rule: input_pointer.cpp's score-scrub
// block). EVERY
// STOP IS THE ONE STOP BODY'S
// (its score fork parks LIVE at PAUSED with the cursor read first), so the
// fence-before-rebind ordering holds as it does for the player. THE PAGE
// FOLLOWS THE CURSOR: each tick while LIVE the page holding the engine's
// cursor (score_page_for_position over GuiPlayback::cursor(), the one
// position face) is shown when it differs, and at rest every seek and every
// pause shows the page holding the new cursor. A device that goes away mid-play pauses at
// the held cursor with the playback card (the render player's tick's rule).
// THE OPEN lands at the remembered frame clamped into the range, PAUSED, the
// engine rebound to the soundtrack behind the modal stop (the fence) — IDLE
// instead while the soundtrack is still loading or a render run stands (the
// close's re-express would kill the run: load_in_place_render_blocked, the
// render player's own refusal, here a grey PLAY until it ends), the PLAY
// press binding then. THE CLOSE takes the render player's close road
// verbatim where the engine was the viewer's: the stop body's fence, the
// transport down, the mode bit down, the source bound first, then the S/T
// flip's re-express.
//
// THE VIEWING RANGE — `score_first_page` / `score_last_page`, 0 the edge's
// placeholder — is THE TRIM'S CLASS ("that would be how we would trim
// effectively", "no undo, just like with regular trim"): START sets the first
// to the shown page, END the last, RESET (behind its question, "because
// there's no undo") both to 0; no undo entry, no dirty mark, each written
// through to the score viewer's file at once (write_score_viewer_file,
// score_viewer_file.h). THE PAGES STAY THE RANGE'S NAMES on both faces (the
// architect sets them "from the page on screen because that's convenient"),
// and once set the range EXCLUDES EVERYTHING OUTSIDE IT: the soundtrack's
// domain is [the first page's begin, the last page's end) — the scrub's
// span, PLAY's window, the open's clamp — and a range changed under playback
// republishes PLAY's end at the cursor (score_viewer.cpp's range_changed).
// THE QUESTION STANDS OVER THE VIEWER as the load confirmation stands over the
// render player (the model for standing), PAUSING a live soundtrack as the
// render player's Delete question pauses its transport, and it opens with
// CANCEL FOCUSED as that question does (a destructive act with no undo behind
// it), so a bare Enter cancels. The three are AUTHORING and grey on a
// read-only tab and under grid iterations (score_viewer_range_locked);
// viewing, paging, playing and the position's write are not, and stay live
// under both. The shown page is always inside the range, so START and END can
// never cross the pair.
//
// THE REMEMBERED POSITION — ViewState::score_position, the active tab's — is
// the playhead's class, written through to the score viewer's file at once: on
// the page road EVERY PAGE CHANGE writes the new page's `begin` (show_page)
// but a held scrub's, whose page is written at the lift (write_page_position;
// a lost lift writes nothing); under the soundtrack it IS THE CURSOR'S FRAME
// (architect: "it remembers based on frames. The pages are themselves aliases
// to frames: begin is an alias to the first frame of a page, end to the
// last"), written at every seek, pause, natural end and close and at a page
// turn under playback (the turn's write waits for its paint, so the turn is
// never late for it). The open shows the page that holds it, clamped into the
// range (score_page_for_position).
//
// THE PAGES ARE DECODED OFF THE MAIN THREAD (architect 2026-10-26 night: "if
// there's going to be an additional visual lag on the page turning on top of
// the Bluetooth lag, that's not going to work"): the page decoder
// (score_page_worker.h) decodes a page, scales it ONCE to the box's fit size
// and converts it for the window, and the painter blits it 1:1 — NEVER A
// SYNCHRONOUS DECODE, the open's included; the main thread never decodes,
// scales or expands. TWO HOLDINGS, both the decoder's to fill:
//   * THE GRAY STORE (AppState::ScoreViewer::gray; architect 2026-10-26
//     ~22:10: every page of the viewing range "decoded once", held in RAM as
//     8-bit gray — about 2.2 MB a page fit to 1975 x 1111 in the tablet's
//     box, 310 MB for all of K.550, about 80 MB for a delineated movement):
//     each page of the range at the fit box, one byte a pixel, keyed by page
//     and box and not by the conversion bit (the bytes are the authored sRGB
//     page).
//     From the open, and whenever it lacks a page of the range, the feed
//     queues the missing pages AFTER THE CACHE'S WANTS, in the likely order
//     reaching the whole range. THE RANGE SCOPES IT ("start and end reset
//     that cache and drop anything that's outside of it", so the full price
//     is paid only until the movement is delineated), AND THE FEED IS THE
//     SCOPE'S OWNER (architect 2026-10-27): every feed, and the drain ahead
//     of its completion measure, drops every page outside the range as it
//     stands (scope_gray_to_range), so START, END, RESET's confirm and
//     SETUP's typed bounds are all covered with none of them named. It
//     survives the close as the cache does, goes whole at a box change
//     (gui_scale) and with the score folder and the project. No backstop for
//     memory: a failed allocation is std::bad_alloc, the rare non-silent
//     fault.
//   * THE CACHE (AppState::ScoreViewer::cache), the PAINTED pages: the shown
//     page and up to three either side of it in the range — at the tablet's
//     box about 9 MB a page — each made FROM THE STORE where the store holds
//     it (the decoder expands the gray bytes, a few ms against a JPEG decode)
//     and from the file otherwise (the one decode yielding the store's bytes
//     too), asked for in the likely order: forward first while the
//     soundtrack sounds or after NEXT, both ways otherwise. IT SURVIVES THE
//     CLOSE, so a reopen is instant, and goes at a score folder change and
//     with the project.
// A SHOWN PAGE NOT YET DELIVERED SHOWS THE LAST PAINTED PAGE in its stead
// (architect 2026-10-26: "a little flash to black … is not helpful"): the
// painter blits the page ScoreViewer::painted_page names, its cache entry
// kept by the trim while the shown page's is absent, until the shown page
// lands (the tick drains the decoder and damages the interior); with nothing
// painted yet — the open's first frame, a score change — the interior is the
// field ground. A page whose decode fails is one stderr line, the interior the
// field ground, and that page is not asked for again under the same key.
// Every job prints its timing line and the store's completion one more
// (score_page_worker.h's THE TIMING LINE, note_gray_complete).

#include "app_state.h"
#include "audio.h"
#include "display_transform.h"
#include "modal_box.h"
#include "playback.h"
#include "playback_lifecycle.h"
#include "target_render.h"
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

// THE PAGE AT A POSITION (the open's landing, and under the soundtrack the
// page that follows the cursor): the first page whose range
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

// WHICH FACE THE ROW WEARS (the head's TWO FACES): the media player's while
// the soundtrack is present — loaded or loading — the page road's otherwise.
inline bool score_viewer_has_soundtrack(const AppState& a) {
    return a.score_audio.present();
}

// THE RANGE IN FRAMES (the media player's domain, the head's THE VIEWING
// RANGE): [the first page's begin, the last page's end), at pages.txt's rate,
// which is the soundtrack's by its admitted shape (score_audio.h).
// Precondition: a score is loaded.
struct ScoreFrameRange {
    int64_t begin = 0;
    int64_t end   = 0;
    int64_t length() const { return end - begin; }
};
inline ScoreFrameRange score_viewer_frame_range(const AppState& a) {
    const ScoreViewerRange r = score_viewer_range(a);
    return ScoreFrameRange{
        a.score->pages[static_cast<std::size_t>(r.first)].begin,
        a.score->pages[static_cast<std::size_t>(r.last)].end};
}

// THE SOUNDTRACK'S POSITION, the one reader the clock, the scrub's thumb,
// the page follow and the acts share: the engine's cursor while LIVE (through
// GuiPlayback::cursor(), the one position face — its window is inside the
// range), the resting cursor otherwise (the active tab's remembered
// position), clamped into the range.
inline int64_t score_viewer_position(const AppState& a,
                                     const GuiPlayback& playback) {
    const ScoreFrameRange fr = score_viewer_frame_range(a);
    const int64_t pos =
        a.score_viewer.transport == AppState::ScoreViewer::Transport::Live
            ? playback.cursor()
            : active_view_state(a).score_position;
    return std::clamp(pos, fr.begin, fr.end);
}

// THE PLAY KEY'S FACE, NAMING ITS NEXT PRESS (the render player's rule and
// its PlayerPlayFace): PAUSE while LIVE, PLAY otherwise — at rest anywhere,
// the press playing on from the cursor, and from the range's start at its
// end, never a no-op there (architect 2026-10-26: play / pause only, "keep it
// simple"; the end, ~23:55).
inline PlayerPlayFace score_viewer_play_face(const AppState& a) {
    return a.score_viewer.transport == AppState::ScoreViewer::Transport::Live
               ? PlayerPlayFace::Pause
               : PlayerPlayFace::Play;
}

// EACH BUTTON'S TRUTHFUL FACE (the roster's rule: a button greys whenever
// its act would be a no-op): PREV and NEXT at the range's edges; PLAY / PAUSE
// until the soundtrack is loaded, on a device that never came up
// (GuiPlayback::device_absent, the render player's own term: a press reopens
// one that went away), and while the engine is not yet the viewer's under a
// render run (the head's THE OPEN); START at the range's first page, END at
// its last, RESET when the range already is every page, all three under the
// lock; CLOSE never. The act asks the same.
inline bool score_viewer_button_enabled(const AppState& a,
                                        const GuiPlayback& playback,
                                        AppState::ScoreButtonAct act) {
    using Act = AppState::ScoreButtonAct;
    if (!a.score_viewer.active || !a.score) return false;
    const ScoreViewerRange r = score_viewer_range(a);
    const int p = a.score_viewer.page;
    switch (act) {
        case Act::PrevPage: return p > r.first;
        case Act::NextPage: return p < r.last;
        case Act::PlayPause:
            if (!a.score_audio.ready() || playback.device_absent()) return false;
            return a.score_viewer.transport !=
                       AppState::ScoreViewer::Transport::Idle ||
                   !load_in_place_render_blocked(a);
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
// PLAY / PAUSE's is the render player's Play key's, by its face
// (render_player_button_legend, paint_handler.cpp, which the painter asks).
inline const char* score_viewer_button_legend(AppState::ScoreButtonAct act) {
    using Act = AppState::ScoreButtonAct;
    switch (act) {
        case Act::PrevPage:  return "PREV";
        case Act::NextPage:  return "NEXT";
        case Act::PlayPause: return "PLAY";
        case Act::Reset:     return "RESET";
        case Act::SetStart:  return "START";
        case Act::SetEnd:    return "END";
        case Act::Close:     return "CLOSE";
        case Act::None:      break;
    }
    return "";
}

// THE TOOLTIPS: the act's name, Title Case, and the key it binds in the
// roster's and the player's own form (render_player_button_hint); PLAY /
// PAUSE names its face as the player's does; the range keys bind none.
inline const char* score_viewer_button_hint(AppState::ScoreButtonAct act,
                                            PlayerPlayFace face) {
    using Act = AppState::ScoreButtonAct;
    switch (act) {
        case Act::PrevPage: return "Previous Page (Left)";
        case Act::NextPage: return "Next Page (Right)";
        case Act::PlayPause:
            switch (face) {
                case PlayerPlayFace::Pause:      return "Pause (Space)";
                case PlayerPlayFace::OpenFolder:
                case PlayerPlayFace::Play:       break;
            }
            return "Play (Space)";
        case Act::Reset:    return "Reset Range";
        case Act::SetStart: return "Set as Start";
        case Act::SetEnd:   return "Set as End";
        case Act::Close:    return "Close (Esc)";
        case Act::None:     break;
    }
    return "";
}

// THE PAGE SCRUB'S MAPPING (the page road) over the painter's PUBLISHED item
// rect (AppState::ModalDialogGeometry::scrub), the player's travel exactly
// (render_player_scrub_travel_x0 / _usable_span): the range's first page at
// the channel's first column, its last at the last, page i at
// (i − first) / (count − 1) of the travel — the resting thumb's column; the
// inverse rounds to the nearest page, the page under a dragged thumb's
// carried column (architect 2026-10-27, the steps the page changes in). One
// page: the thumb at the left, the scrub dead (the press claim).
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

// THE SOUNDTRACK SCRUB'S MAPPING (the media player's face), the render
// player's (render_player_scrub_x_of / _frame_at) over the range's frames:
// the range's first frame at the channel's first column, its end at the
// last, banker's rounding onto the cells; a zero item or an empty range the
// first column and the first frame.
inline int score_scrub_x_of_frame(const AppState& a, int64_t frame) {
    const GuiRect track = a.modal_dialog.scrub;
    const int x0   = render_player_scrub_travel_x0(track);
    const int span = render_player_scrub_usable_span(track);
    const ScoreFrameRange fr = score_viewer_frame_range(a);
    if (span <= 0 || fr.length() <= 0) return x0;
    const int64_t f = std::clamp(frame, fr.begin, fr.end);
    const double t = static_cast<double>(f - fr.begin) /
                     static_cast<double>(fr.length());
    return x0 + static_cast<int>(std::nearbyint(t * span));
}
inline int64_t score_scrub_frame_at(const AppState& a, int x) {
    const GuiRect track = a.modal_dialog.scrub;
    const int x0   = render_player_scrub_travel_x0(track);
    const int span = render_player_scrub_usable_span(track);
    const ScoreFrameRange fr = score_viewer_frame_range(a);
    if (span <= 0 || fr.length() <= 0) return fr.begin;
    const int cx = std::clamp(x, x0, x0 + span);
    const double t = static_cast<double>(cx - x0) / static_cast<double>(span);
    return fr.begin + static_cast<int64_t>(std::nearbyint(
                          t * static_cast<double>(fr.length())));
}

// THE PAGE THE PAINTER BLITS: the cache's entry for `page` at the fit box
// `box` (the modal box's interior) under the window's conversion bit as it
// stands, or null — not delivered yet; an entry marked failed answers too,
// with no surface (the painter's one read of the cache).
inline const AppState::ScoreViewer::Cached* score_viewer_cached(
        const AppState& a, int page, const GuiRect& box) {
    const GuiScorePageKey key{page, box.w, box.h, display_transform::active()};
    for (const AppState::ScoreViewer::Cached& c : a.score_viewer.cache)
        if (c.key == key) return &c;
    return nullptr;
}

// THE SCORE FOLDER CHANGED (the project load, a typed `score_path=` commit —
// the two writers of AppState::score): the page cache and the gray store
// dropped and the decoder reset (the head's THE PAGES ARE DECODED), and the soundtrack's load restarted
// for the new folder (GuiScoreAudio::start) or stopped with no score. Never
// while the viewer stands: neither caller runs under it.
void score_folder_changed(AppState& app);

struct GuiScoreViewer {
    AppState&             app;
    const GuiAudio&       audio;
    GuiPlayback&          playback;
    Viewport&             viewport;
    GuiPlaybackLifecycle& playback_lifecycle;
    GuiTargetRender&      target_render;
    GuiNotifications&     notifications;

    GuiScoreViewer(AppState& app_, const GuiAudio& audio_,
                   GuiPlayback& playback_, Viewport& viewport_,
                   GuiPlaybackLifecycle& playback_lifecycle_,
                   GuiTargetRender& target_render_,
                   GuiNotifications& notifications_)
        : app(app_),
          audio(audio_),
          playback(playback_),
          viewport(viewport_),
          playback_lifecycle(playback_lifecycle_),
          target_render(target_render_),
          notifications(notifications_) {}

    // THE ONE OPENER — the SCORE key's lift and bare `m` (the dispatch arm,
    // input_key_dispatch.cpp). A piece with no score cards why (the key is
    // grey, so the keyboard alone reaches that refusal); a standing prompt is
    // silent (it veils everything). The folder overlay's contents never stand
    // under it: under them `m` is their routers' silent catch-all and the
    // SCORE key is grey, so no road reaches here (the player's and the
    // picker's own mutual exclusion, input_key_dispatch.cpp's
    // open_project_picker). Takes the modal-open stop, mints the session,
    // lands on the active tab's remembered position (the soundtrack bound,
    // PAUSED, where it can be: the head's THE OPEN), asks the decoder for
    // what the cache and the gray store lack and damages the window.
    bool open();
    // THE ONE CLOSER — CLOSE, Esc, the SCORE key and bare `m`, and the exit
    // road's teardown (GuiPrompt::request_close): the render player's close
    // road where the engine was the viewer's (the head's THE CLOSE), the
    // cursor's frame written through, the scrub's arm dropped, the cache
    // KEPT, the whole window damaged (the roster's faces and the canvas
    // column come back together, so the window is the honest rect).
    void close();

    // -- The page road --------------------------------------------------------
    // THE ONE PAGE CHANGE: `index` clamped into the viewing range; a change
    // is displayed and its position written (display_page, then
    // write_page_position). The same page is a no-op.
    void show_page(int index);
    // ITS TWO HALVES, apart for the held page scrub alone (architect
    // 2026-10-27: the page under the thumb shown live, written at the lift —
    // input_pointer.cpp's score-scrub block): display_page clamps `index`
    // into the viewing range and, where it differs, makes it the shown page
    // and damages the interior and the row, returning whether it changed,
    // nothing written; write_page_position writes the shown page's begin as
    // the active tab's remembered position, through to the file.
    bool display_page(int index);
    void write_page_position();
    // PREV / NEXT (and Left / Right): one page, walled at the range's edges;
    // NEXT biases the decoder forward.
    void step(int delta);
    // Home / End: the range's first / last page — under the soundtrack its
    // first / last frame (seek_to).
    void first_page();
    void last_page();

    // -- The media player's face ---------------------------------------------
    // PLAY / PAUSE and Space: refused, silent, where its key is grey; else
    // pause() where LIVE, play() otherwise.
    void toggle_play();
    // THE PAUSE: LIVE parks at PAUSED through the one stop body, the cursor
    // kept, its page shown and the frame written; a no-op at rest. Its
    // callers are toggle_play, the scrub's press (architect 2026-10-26
    // ~23:00: the sound pauses while the scrub is held) and the tick's dead
    // device.
    void pause();
    // THE PLAY: the engine bound to the soundtrack where it is not yet, and
    // [the cursor, the range's end) played once — from the range's first
    // frame where the cursor rests in its last frame or at its end (THE PLAY
    // KEY AT THE END, the head's THE TRANSPORT). Refused, silent, where the
    // PLAY / PAUSE key is grey; a no-op where LIVE. Its one caller is
    // toggle_play: THE END RULE IS THE KEY'S ALONE.
    void play();
    // THE SCRUB'S PLAY ON (architect 2026-10-27, the render player's
    // resume_at): the soundtrack LIVE from `frame`, clamped as seek_to's live
    // arm clamps — a target in the range's last frame or at its end stops
    // one short of the last frame, so the natural end takes over — and
    // never the PLAY key's end rule; the page follows and the position is
    // written. Refused, silent, where the PLAY / PAUSE key is grey; a no-op
    // where LIVE. Its one caller is the scrub's lift where the press paused
    // a live soundtrack.
    void play_from(int64_t frame);
    // THE SEEK: `frame` clamped into the range; LIVE republishes PLAY's window
    // from there (a target in the range's last frame stops one short of it,
    // the render player's rule, so the natural end takes over), at rest the
    // cursor moves; the page follows either way; the position written
    // through. Its callers are Home / End and the scrub's lift where the
    // press found the soundtrack paused (architect 2026-10-26, "no
    // scrubbing": the drag carries the thumb and seeks nothing; a lift that
    // plays on is play_from's).
    void seek_to(int64_t frame);

    // -- The range ------------------------------------------------------------
    // START / END: the range's first / last bound set to the shown page —
    // each refused, silently, where its key is grey.
    void set_start();
    void set_end();
    // RESET: raises its question (DialogTrigger::SCORE_RESET_CONFIRM, Cancel
    // focused — the head), pausing a live soundtrack first, where its key is
    // live; confirm_reset is the question's OK.
    void raise_reset();
    void confirm_reset();

    // THE RUN LOOP'S TICK (main.cpp's on_tick, every tick, the viewer standing
    // or not): the decoder's results drained into the cache; and while the
    // viewer stands, the soundtrack's arrival or failure repainting the face,
    // the LIVE transport's dead device, natural end, page follow and clock /
    // scrub damage (the render player's tick's shape), a page turn's owed
    // position written once the turn is painted, and the decoder asked for
    // the missing pages.
    void tick();

private:
    // The engine bound to the soundtrack where it is not yet: IDLE to PAUSED
    // behind the stop body's fence. False where it cannot be (not loaded, a
    // render run standing).
    bool bind_soundtrack();
    // The page holding `pos` shown where it differs; the interior damaged.
    // Returns whether the page changed.
    bool follow_cursor(int64_t pos);
    // After START / END / RESET: the cursor clamped into the new range and,
    // LIVE, PLAY's window republished at the cursor to the new end (the gray
    // store's scope is the feed's, scope_gray_to_range).
    void range_changed();
    // THE RESUME ROAD'S ONE LAUNCH: the soundtrack LIVE from `from` to the
    // range's end, the page following, the row damaged. Its two callers have
    // chosen `from` and passed their guards: play() through the PLAY key's
    // end rule, play_from() through the live seek's clamp.
    void play_live_from(int64_t from);
    // The natural end: the fence through the one stop body, the rest at the
    // range's end, written.
    void on_natural_end();
    // The decoder's results into the cache and the gray store; the shown
    // page's arrival damages the interior.
    void drain_decoder();
    // The gray store at the interior's box (dropped whole at a change) and
    // inside the range (scope_gray_to_range), the cache trimmed to the shown
    // page's neighborhood at the current box and conversion (the last painted
    // page kept while the shown one is absent), and the decoder handed the
    // work: the cache's wants in order, then the store's.
    void feed_decoder();
    // THE GRAY STORE'S RANGE SCOPE: every entry outside the range as it
    // stands dropped. Its callers are the feed, every time, and the drain
    // ahead of its completion measure (the head's THE GRAY STORE).
    void scope_gray_to_range();
    // The gray store measured against the range: short, the asking time
    // stamped where unstamped; whole with a stamp, the completion's timing
    // line printed and the stamp cleared.
    void note_gray_complete();
    // Damage: the modal family's whole area, the box's interior, the
    // controls row.
    void damage();
    void damage_interior();
    void damage_row();
};
