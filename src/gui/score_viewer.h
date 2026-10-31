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
// other modals"): its veil admits the SCORE key alone and its router the
// SCORE key's chord alone, so the active tab is the viewer's for its whole
// life. Its state is AppState::ScoreViewer and the
// piece's range keys (app_state.h); this struct owns the acts.
//
// TWO FACES OF THE CONTROLS ROW, decided by whether the soundtrack is present
// (loaded or loading, GuiScoreAudio::present; architect 2026-10-26 night:
// "when there's audio, the scrub should just be a media player scrub, just
// like the render player's. The page numbers should not be included at all
// if there's audio. Everything — the playback, the scrub — should be frame
// and/or time based. No pages. Pages are only kept in the background"):
//   * THE MEDIA PLAYER'S, with the soundtrack: BACK, PLAY / PAUSE and FWD
//     at the left, a deck's order (the render player's transport seats, PLAY
//     its key and its legend — score_viewer_play_face — in the second as the
//     player's stands between its two skips), THE SKIPS (architect
//     2026-10-27): BACK and FWD skip the soundtrack 3 s back / forward at its
//     own rate, their Shift-press or long press 10 s (score_viewer_skip_
//     landing, the one landing the act and the face ask) — each a seek_to;
//     THE SCRUB (the render player's, its domain
//     the viewing range's FRAMES: the thumb at the cursor, a press off the
//     thumb jumping it, a drag CARRYING it and seeking nothing, the page
//     holding the frame under it SHOWN LIVE, display only — architect
//     2026-10-27 ~19:45, "the picture follows the thumb, the sound waits for
//     the lift" — THE SEEK AT THE LIFT, the position written with it —
//     architect 2026-10-26, "no scrubbing" — and, the score's own, THE SOUND
//     PAUSED WHILE THE SCRUB IS HELD: a live soundtrack paused at the press
//     and playing on from where the thumb dropped at the lift — architect
//     2026-10-26 ~23:00, "it wouldn't start playing until I commit by
//     dropping the thumb"), THE CLOCK (the render player's two time fields,
//     verbatim: the elapsed time within the range and the range's length,
//     unmoved under a held scrub), then RESET, START, END and CLOSE flush
//     right. The keys are the acts' one road (architect 2026-10-30: no
//     keyboard stands under the viewer).
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
//     same four keys flush right.
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
// pause shows the page holding the new cursor — the one exception a held
// scrub, which shows the page under the dragged thumb until the lift's seek
// lands it (input_pointer.cpp's score-scrub block). A device that goes away
// mid-play pauses at the held cursor with the playback card (the render
// player's tick's rule).
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
// (score_page_worker.h) decodes a page and scales it ONCE to the box's fit
// size — NEVER A SYNCHRONOUS DECODE, the open's included; the main thread
// never decodes or scales. THE GPU PAGE PAINTS IT (architect 2026-10-27, "1
// a": THE PICTURE FOLLOWS THE THUMB, THE SOUND WAITS FOR THE LIFT; the
// measurement and the design at docs/engineering/gpu_canvas_2026_10.md
// §3.13): the box's interior is the frame's hole, cleared on the CPU, and
// the GPU draws the field ground and the page there at every swap from THE
// GRAY STORE'S BYTES, one R8 texture a page (GuiCanvasPage, canvas_sink.h;
// the textures' lifetime at gl_canvas.h): no blit and no canvas upload, so a
// page change costs a frame and nothing more once the range is resident.
// THE GRAY STORE (AppState::ScoreViewer::gray; architect 2026-10-26 ~22:10:
// every page of the viewing range "decoded once", held as 8-bit gray —
// about 2.2 MB a page fit to 1975 x 1111 in the tablet's box, 310 MB for all
// of K.550, about 80 MB for a delineated movement): each page of the range
// at the fit box, one byte a pixel, keyed by page and box (the bytes are the
// authored sRGB page, the window's byte under either conversion bit,
// score_page_worker.h's proof). ITS RAM BYTES LIVE ONLY UNTIL THE PAGE'S
// TEXTURE IS HELD (architect 2026-10-27, the memory fix): once the GL
// canvas's report lists an entry's serial, the entry lets its bytes go and
// stands on, released (GuiScoreGrayPage; settle_gray_on_gpu), so the RAM
// price is the pages decoded and not yet uploaded — while the page stands
// about one upload's worth, the uploads (one a present) outrunning the
// decoder; the pages decoded while it does not stand, which nothing uploads,
// until it stands again — and the GPU's the range's. An entry whose texture
// goes leaves the store with it, and the feed decodes that page again.
// From the open, and whenever it lacks a page of the range, the feed queues
// the missing pages, THE NEAR REACH FIRST (the shown page and up to three
// either side of it in the range) and then the rest, in the likely order
// reaching the whole range: forward first while the soundtrack sounds or
// after NEXT, both ways otherwise. THE RANGE SCOPES IT ("start and end reset
// that cache and drop anything that's outside of it", so the full price is
// paid only until the movement is delineated), AND THE FEED IS THE SCOPE'S
// OWNER (architect 2026-10-27): every feed, and the drain ahead of its
// completion measure, drops every page outside the range as it stands
// (scope_gray_to_range), so START, END, RESET's confirm and SETUP's typed
// bounds are all covered with none of them named. It survives the close, so
// a reopen is instant, goes whole at a box change (gui_scale) and with the
// score folder and the project. No backstop for memory: a failed allocation
// is std::bad_alloc, the rare non-silent fault.
// A SHOWN PAGE NOT YET DELIVERED SHOWS THE LAST PAINTED PAGE in its stead
// (architect 2026-10-26: "a little flash to black … is not helpful"): the
// GPU draws the shown page's texture where it is held, else the last painted
// page's, else the field ground, the field alone for a failed page
// (GuiGlCanvas's draw); with nothing painted yet — the open's first frame, a
// score change — the interior is the field ground. A page whose decode fails
// is one stderr line, the interior the field ground, and that page is not
// asked for again under the same box. painted_page is THE PAGE THE LAST
// PRESENT PUT ON SCREEN, read off the GL canvas's report at every tick
// (take_gpu_page_report) — so the owed position write's gate (painted_page
// == page) waits for the page's own swap; while the store holds the bytes of
// a page the GPU does not, the tick asks a frame (the uploads go one a
// present).

// Every job prints its timing line and the store's completion one more
// (score_page_worker.h's THE TIMING LINE, note_gray_complete).

#include "app_state.h"
#include "audio.h"
#include "modal_box.h"
#include "playback.h"
#include "playback_lifecycle.h"
#include "target_render.h"
#include "viewport.h"

#include <cstdint>
#include <optional>

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

// THE SEEK'S LANDING — where GuiScoreViewer::seek_to(frame) leaves the
// cursor, or nullopt where it refuses (no viewer, no score, no soundtrack;
// LIVE, a range shorter than two frames): `frame` clamped into the range,
// and LIVE no further than one short of the range's last frame (the render
// player's rule, so the natural end takes over). The act and the skips' face
// read this one body, so the face cannot promise a landing the act refuses.
inline std::optional<int64_t> score_viewer_seek_landing(const AppState& a,
                                                        int64_t frame) {
    if (!a.score_viewer.active || !a.score || !score_viewer_has_soundtrack(a))
        return std::nullopt;
    const ScoreFrameRange fr = score_viewer_frame_range(a);
    const int64_t target = std::clamp(frame, fr.begin, fr.end);
    if (a.score_viewer.transport != AppState::ScoreViewer::Transport::Live)
        return target;
    const int64_t live_target = std::min(target, fr.end - 2);
    if (live_target < fr.begin) return std::nullopt;
    return live_target;
}

// THE SKIPS' TWO STEPS (architect 2026-10-27): a press 3 s, its Shift-press
// or long press 10 s, in seconds at THE SOUNDTRACK'S OWN RATE
// (GuiScoreAudio::rate, valid while the soundtrack is present — loaded or
// loading, the media face's condition), nothing converted. The tooltips
// (score_viewer_button_hint, score_viewer_button_shift_hint) spell the two.
inline constexpr int kScoreSkipSeconds     = 3;
inline constexpr int kScoreSkipLongSeconds = 10;

// WHERE A SKIP LANDS — `forward` FWD over BACK, `long_form` the 10 s form:
// the seek's landing (score_viewer_seek_landing) from the cursor
// (score_viewer_position) plus or minus the step. THE EDGE IS seek_to's
// CLAMP: within the last step of the range a resting FWD lands on the
// range's end (the natural end's rest, PLAY there playing the range from its
// start) and a LIVE one one short of the last frame, the natural end taking
// over at once; within the first step BACK lands on the range's first frame.
inline std::optional<int64_t> score_viewer_skip_landing(
        const AppState& a, const GuiPlayback& playback, bool forward,
        bool long_form) {
    if (!a.score_viewer.active || !a.score || !score_viewer_has_soundtrack(a))
        return std::nullopt;
    const int64_t step =
        static_cast<int64_t>(long_form ? kScoreSkipLongSeconds
                                       : kScoreSkipSeconds) *
        static_cast<int64_t>(a.score_audio.rate());
    const int64_t pos = score_viewer_position(a, playback);
    return score_viewer_seek_landing(a, forward ? pos + step : pos - step);
}

// WOULD THIS FORM OF THE SKIP MOVE THE CURSOR, in its own direction: a
// landing past the cursor for FWD, short of it for BACK. The direction is the
// LIVE arm's term: in the range's last two frames a live FWD's landing (one
// short of the last frame) lies behind the cursor, which is no forward skip.
// At rest the clamp is monotone, so "moves" and "moves its way" agree.
inline bool score_viewer_skip_actionable(const AppState& a,
                                         const GuiPlayback& playback,
                                         bool forward, bool long_form) {
    const std::optional<int64_t> landing =
        score_viewer_skip_landing(a, playback, forward, long_form);
    if (!landing) return false;
    const int64_t pos = score_viewer_position(a, playback);
    return forward ? *landing > pos : *landing < pos;
}

// EACH BUTTON'S TRUTHFUL FACE (the roster's rule: a button greys whenever
// its act would be a no-op): PREV and NEXT at the range's edges; BACK and FWD
// where NEITHER form would move the cursor (the roster skips' twin rule,
// playhead_end_jump_actionable asked of both forms) — BACK at the range's
// first frame, FWD at its end (LIVE, its last two frames); PLAY / PAUSE
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
        case Act::SkipBack:
        case Act::SkipForward: {
            const bool forward = act == Act::SkipForward;
            return score_viewer_skip_actionable(a, playback, forward, false) ||
                   score_viewer_skip_actionable(a, playback, forward, true);
        }
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
// The skips print BACK and FWD (architect 2026-10-27), one print for both
// forms, as the roster's START / END do.
inline const char* score_viewer_button_legend(AppState::ScoreButtonAct act) {
    using Act = AppState::ScoreButtonAct;
    switch (act) {
        case Act::PrevPage:  return "PREV";
        case Act::NextPage:  return "NEXT";
        case Act::SkipBack:    return "BACK";
        case Act::SkipForward: return "FWD";
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
// PAUSE names its face as the player's does; the skips name their 3 s
// step and the bare arrow, their 10 s form the modifier line's
// (score_viewer_button_shift_hint); the range keys bind none.
inline const char* score_viewer_button_hint(AppState::ScoreButtonAct act,
                                            PlayerPlayFace face) {
    using Act = AppState::ScoreButtonAct;
    switch (act) {
        case Act::PrevPage: return "Previous Page (Left)";
        case Act::NextPage: return "Next Page (Right)";
        case Act::SkipBack:    return "Skip Back 3 s (Left)";
        case Act::SkipForward: return "Skip Forward 3 s (Right)";
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

// THE MODIFIER LINE (the roster's form, "Press Shift ..."; the player's
// render_player_button_shift_hint): non-empty on exactly the buttons
// score_button_shift_admits names (app_state.h) — the two skips, "Press
// Shift to skip 10 s." — and DROPPING WHERE THE TWO FORMS LAND ON ONE FRAME,
// the roster skips' rule (redesign_button_tooltip's stateful overload): the
// range's last 3 s for FWD, its first 3 s for BACK, and a greyed skip. The
// first line never forks.
inline const char* score_viewer_button_shift_hint(
        const AppState& a, const GuiPlayback& playback,
        AppState::ScoreButtonAct act) {
    using Act = AppState::ScoreButtonAct;
    switch (act) {
        case Act::SkipBack:
        case Act::SkipForward: {
            const bool forward = act == Act::SkipForward;
            if (score_viewer_skip_landing(a, playback, forward, false) ==
                score_viewer_skip_landing(a, playback, forward, true))
                return "";
            return "Press Shift to skip 10 s.";
        }
        case Act::PrevPage:
        case Act::NextPage:
        case Act::PlayPause:
        case Act::Reset:
        case Act::SetStart:
        case Act::SetEnd:
        case Act::Close:
        case Act::None:
            break;
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

// THE PAGE UNDER A DRAGGED THUMB'S CARRIED COLUMN, the one answer of both
// faces (architect 2026-10-27 ~19:45, "the picture follows the thumb, the
// sound waits for the lift"; input_pointer.cpp's score-scrub block): on the
// page road the page the column rounds to (score_scrub_page_at, the steps);
// under the soundtrack the page holding the column's frame
// (score_page_for_position over score_scrub_frame_at), the very frame the
// lift seeks, so the seek's page is the page the drag showed.
inline int score_scrub_page_under(const AppState& a, int x) {
    if (score_viewer_has_soundtrack(a))
        return score_page_for_position(a, score_scrub_frame_at(a, x));
    return score_scrub_page_at(a, x);
}

// THE PAGE STANDS: the viewer stands with a score loaded and is THE LIVE
// MODAL SURFACE — no prompt over it (RESET's question takes the box), read
// through the one ranking (AppState::modal_dialog_live_session) — which is
// paint_modal_dialog's score branch's condition exactly: the box's interior
// is then the frame's hole and the GPU draws the page (GuiCanvasPage::stands,
// canvas_sink.h).
inline bool score_viewer_page_stands(const AppState& a) {
    return a.score_viewer.active && a.score.has_value() &&
           a.modal_dialog_live_session() == a.score_viewer.session;
}

// THE GPU PAGE'S REQUEST (2026-10-27; GuiCanvasPage's
// rule, canvas_sink.h), written at every frame's first damage rect
// (on_redraw's canvas block) from the viewer's state as it stands: whether
// the page stands, the session, the field word, the shown page and the last
// painted page by their store entries' serials, and the store's pages at the
// interior's box in THE FEED'S ORDER (feed_decoder's) as the wanted
// textures, a released entry's with no bytes (it is held:
// settle_gray_on_gpu) — none while the store's box is not the interior's (a
// resize the feed has not caught up with), so the textures go with the
// store.
struct GuiCanvasPage;
void score_viewer_publish_gpu_page(const AppState& app, GuiCanvasPage& out);

// THE SCORE FOLDER CHANGED (the project load, a typed `score_path=` commit —
// the two writers of AppState::score): the gray store dropped and the decoder
// reset (the head's THE PAGES ARE DECODED), and the soundtrack's load restarted
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

    // THE ONE OPENER — the SCORE key's lift, bare `m` (the dispatch arm,
    // input_key_dispatch.cpp). A piece with no score cards why (the key is
    // grey, so only a face painted live inside the frame its act went dead in
    // reaches that refusal); a standing prompt is silent (it veils
    // everything). The folder overlay's contents never stand
    // under it: under them the SCORE key is grey, so no road reaches here (the player's and the
    // picker's own mutual exclusion, input_key_dispatch.cpp's
    // open_project_picker). Takes the modal-open stop, mints the session,
    // lands on the active tab's remembered position (the soundtrack bound,
    // PAUSED, where it can be: the head's THE OPEN), asks the decoder for
    // what the gray store lacks and damages the window.
    bool open();
    // THE ONE CLOSER — CLOSE, the SCORE key (its lift, bare `m`), and the exit
    // road's teardown (GuiPrompt::request_close): the render player's close
    // road where the engine was the viewer's (the head's THE CLOSE), the
    // cursor's frame written through, the scrub's arm dropped, the gray store
    // KEPT, the whole window damaged (the roster's faces and the canvas
    // column come back together, so the window is the honest rect).
    void close();

    // -- The page road --------------------------------------------------------
    // THE ONE PAGE CHANGE: `index` clamped into the viewing range; a change
    // is displayed and its position written (display_page, then
    // write_page_position). The same page is a no-op.
    void show_page(int index);
    // ITS TWO HALVES, apart for the held scrub (architect 2026-10-27 ~19:45,
    // "the picture follows the thumb, the sound waits for the lift" —
    // input_pointer.cpp's score-scrub block): display_page clamps `index`
    // into the viewing range and, where it differs, makes it the shown page
    // and damages the interior and the row, returning whether it changed,
    // nothing written, nothing seeked — the held scrub's on BOTH FACES, under
    // the soundtrack the page holding the frame under the thumb, the cursor
    // and the clock untouched; write_page_position writes the shown page's
    // begin as the active tab's remembered position, through to the file —
    // the page road's lift alone.
    bool display_page(int index);
    void write_page_position();
    // PREV / NEXT: one page, walled at the range's edges; NEXT biases the
    // decoder forward.
    void step(int delta);

    // -- The media player's face ---------------------------------------------
    // PLAY / PAUSE: refused, silent, where its key is grey; else
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
    // through. The landing is score_viewer_seek_landing's, the one body the
    // skips' face asks too. Its callers are the skips and the
    // scrub's lift where the press found the soundtrack paused (architect
    // 2026-10-26, "no scrubbing": the drag carries the thumb and seeks
    // nothing; a lift that plays on is play_from's).
    void seek_to(int64_t frame);
    // THE SKIPS (architect 2026-10-27) — BACK / FWD, their long press the
    // `long_form`: the
    // cursor 3 s back (`forward` false) or forward, 10 s in the long form, at
    // the soundtrack's own rate, through seek_to, so
    // LIVE plays on from the landing and at rest the cursor moves, the page
    // following and the position written. Refused, silent, where this form
    // would not move the cursor its way (score_viewer_skip_actionable, the
    // face's own term); the end rule, the held scrub's pause and the natural
    // end's rest are untouched.
    void skip(bool forward, bool long_form);

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
    // or not): the decoder's results drained, the gray bytes into the gray
    // store, which outlives the viewer's close; and while the viewer stands,
    // the GL canvas's page report taken (take_gpu_page_report), the
    // soundtrack's arrival or failure repainting the face,
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
    // The decoder's results into the gray store; the shown page's arrival
    // damages the interior.
    void drain_decoder();
    // THE GL CANVAS'S PAGE REPORT (GuiCanvasPage, canvas_sink.h): painted_page
    // set to the page the last present put on screen
    // for this session, and, while the page stands, a frame asked
    // (request_frame) while the store at the interior's box holds the bytes
    // of a page the GPU does not — each present uploads at most one, so the
    // asks run until the range is resident.
    void take_gpu_page_report();
    // ONE FRAME ASKED with no CPU pixel to repaint: one device px of the
    // interior, the GL canvas's hole while the page stands (the CPU clears it
    // and the upload leaves it out), so the frame costs the CPU one clipped
    // pass and the GPU its swap.
    void request_frame();
    // The gray store at the interior's box (dropped whole at a change),
    // inside the range (scope_gray_to_range) and settled against the GPU
    // (settle_gray_on_gpu), and the decoder handed the work: the near
    // reach's missing pages in order, then the rest of the range's.
    void feed_decoder();
    // THE GRAY STORE'S RANGE SCOPE: every entry outside the range as it
    // stands dropped. Its callers are the feed, every time, and the drain
    // ahead of its completion measure (the head's THE GRAY STORE).
    void scope_gray_to_range();
    // THE GRAY STORE AGAINST THE GPU (the head's THE GRAY STORE, architect
    // 2026-10-27, the memory fix), read off the GL
    // canvas's report: an entry whose serial is held lets go of its RAM
    // bytes — the store's copy and the request's (GuiCanvasPage::want) —
    // and stays, RELEASED (GuiScoreGrayPage); a released entry whose texture
    // is no longer held leaves the store, so the feed decodes it again from
    // the file. Its caller is the feed, every time, after the range scope.
    void settle_gray_on_gpu();
    // The gray store measured against the range: short, the asking time
    // stamped where unstamped; whole with a stamp, the completion's timing
    // line printed and the stamp cleared.
    void note_gray_complete();
    // Damage: the modal family's whole area, the box's interior, the
    // controls row. WHILE THE PAGE STANDS, THE INTERIOR'S DAMAGE IS A FRAME
    // (request_frame): its pixels are the GPU's, redrawn
    // whole at every swap from the state the frame publishes, and the CPU's
    // share of them is alpha 0, so a page change owes the CPU nothing.
    void damage();
    void damage_interior();
    void damage_row();
};
