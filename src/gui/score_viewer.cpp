#include "score_viewer.h"

#include "notifications.h"
#include "score_viewer_file.h"  // the write-through
#include "text_editor.h"   // next_session_id (the one modal counter)

#include <algorithm>
#include <cstdio>
#include <vector>

using Transport = AppState::ScoreViewer::Transport;

namespace {

// THE ONE CARD (bare `m` on a piece whose score_path names no loaded score;
// the key itself is grey there).
constexpr const char* kNoScoreCard = "This piece has no score";

// HOW FAR THE CACHE REACHES either side of the shown page, inside the range
// (the head's THE CACHE): seven pages at most, about 63 MB at the tablet's
// box.
constexpr int kCacheReach = 3;

}  // namespace

void score_folder_changed(AppState& app) {
    AppState::ScoreViewer& sv = app.score_viewer;
    sv.decoder.reset();
    sv.cache.clear();
    sv.asked.clear();
    if (app.score) {
        app.score_audio.start(app.score->folder, app.score->rate);
    } else {
        app.score_audio.shutdown();
    }
}

bool GuiScoreViewer::open() {
    AppState::ScoreViewer& sv = app.score_viewer;
    if (sv.active) return false;
    if (app.prompt.active) return false;
    if (!app.score) {
        notifications.notify(AppState::NotificationClass::Normal, kNoScoreCard);
        return false;
    }
    playback_lifecycle.stop_playback_for_modal_open();
    sv.active         = true;
    sv.session        = text_editor::next_session_id();
    sv.painted_page   = -1;
    sv.scrub          = AppState::ScoreViewer::ScrubDrag{};
    sv.transport      = Transport::Idle;
    sv.painted_cursor = -1;
    sv.position_owed  = false;
    sv.forward        = false;
    sv.asked.clear();
    sv.seen_audio     = app.score_audio.state();
    // THE OPEN LANDS, IT DOES NOT CHANGE A PAGE: the remembered position is
    // read (a position inside a page names that page already) and, under the
    // soundtrack, clamped into the range — the cursor the media player rests
    // at — so the open writes nothing.
    int64_t& rest = active_view_state(app).score_position;
    if (score_viewer_has_soundtrack(app)) {
        const ScoreFrameRange fr = score_viewer_frame_range(app);
        rest = std::clamp(rest, fr.begin, fr.end);
        (void)bind_soundtrack();
    }
    sv.page = score_page_for_position(app, rest);
    feed_decoder();
    viewport.invalidate_all();
    return true;
}

void GuiScoreViewer::close() {
    AppState::ScoreViewer& sv = app.score_viewer;
    if (!sv.active) return;
    // THE RENDER PLAYER'S CLOSE ROAD, its Close arm (unload_item), where the
    // engine was the viewer's: the stop body's fence first — its score fork
    // reads the cursor of a live soundtrack before it parks it — then the
    // transport down, then the mode bit down ahead of the re-express (a
    // target completion resolving inside it must find the viewer gone and
    // rebind, complete_successful_buffer's guard), the source bound first and
    // the S/T flip's own tail fork after it.
    const bool held = sv.transport != Transport::Idle;
    if (held) playback_lifecycle.stop_playback_if_playing();
    sv.transport     = Transport::Idle;
    sv.scrub         = AppState::ScoreViewer::ScrubDrag{};
    sv.position_owed = false;
    // THE CURSOR'S FRAME AT THE CLOSE (the head's THE REMEMBERED POSITION);
    // the page road wrote at its last page change.
    if (score_viewer_has_soundtrack(app)) write_score_viewer_file(app);
    sv.active = false;
    if (held) {
        if (audio.total_frames() > 0) {
            playback.rebind_buffer(GuiBoundAudio::float32(
                audio.samples_ptr(), audio.total_frames(), audio.sample_rate(),
                0));
        }
        if (app.active_audio_view == 'T') {
            target_render.ensure_ready();
        } else {
            target_render.rebind_to_source();
        }
    }
    viewport.invalidate_all();
}

void GuiScoreViewer::show_page(int index) {
    AppState::ScoreViewer& sv = app.score_viewer;
    if (!sv.active || !app.score) return;
    const ScoreViewerRange r = score_viewer_range(app);
    index = std::clamp(index, r.first, r.last);
    if (index == sv.page) return;
    sv.page = index;
    // EVERY PAGE CHANGE WRITES THE POSITION (the head): the active tab's
    // band, the page's own begin — display scratch, nothing dirtied — and
    // the score viewer's file at once.
    active_view_state(app).score_position =
        app.score->pages[static_cast<std::size_t>(index)].begin;
    write_score_viewer_file(app);
    damage_interior();
    damage_row();
}

void GuiScoreViewer::step(int delta) {
    app.score_viewer.forward = delta > 0;
    show_page(app.score_viewer.page + delta);
}

void GuiScoreViewer::first_page() {
    if (!app.score_viewer.active || !app.score) return;
    if (score_viewer_has_soundtrack(app)) {
        seek_to(score_viewer_frame_range(app).begin, /*write=*/true);
        return;
    }
    show_page(score_viewer_range(app).first);
}

void GuiScoreViewer::last_page() {
    if (!app.score_viewer.active || !app.score) return;
    if (score_viewer_has_soundtrack(app)) {
        seek_to(score_viewer_frame_range(app).end - 1, /*write=*/true);
        return;
    }
    show_page(score_viewer_range(app).last);
}

bool GuiScoreViewer::bind_soundtrack() {
    AppState::ScoreViewer& sv = app.score_viewer;
    if (sv.transport != Transport::Idle) return true;
    if (!app.score_audio.ready() || load_in_place_render_blocked(app))
        return false;
    // THE FENCE, then the bind (the render player's play_wav order): nothing
    // sounds under the viewer — its open took the modal stop — so the stop
    // body finds nothing to stop, and the engine's quiescence is the stop
    // that ended the project's last play.
    playback_lifecycle.stop_playback_if_playing();
    playback.rebind_buffer(GuiBoundAudio::pcm16(app.score_audio.samples(),
                                                app.score_audio.frames(),
                                                app.score_audio.rate()));
    sv.transport = Transport::Paused;
    return true;
}

void GuiScoreViewer::toggle_play() {
    AppState::ScoreViewer& sv = app.score_viewer;
    if (!score_viewer_button_enabled(app, playback,
                                     AppState::ScoreButtonAct::PlayPause))
        return;
    if (sv.transport == Transport::Live) {
        // PAUSE: the one stop body, whose score fork reads the engine's
        // cursor into the resting position before the fence and parks the
        // transport at PAUSED; the frame written through.
        playback_lifecycle.stop_playback_if_playing();
        write_score_viewer_file(app);
        damage_row();
        return;
    }
    if (!bind_soundtrack()) return;
    // PLAY: [cursor, the range's end) once; a cursor resting in the range's
    // last frame (the End key's seek) plays from the range's start — a user
    // act, not a loop.
    const ScoreFrameRange fr = score_viewer_frame_range(app);
    if (fr.length() < 2) return;
    int64_t from = score_viewer_position(app, playback);
    if (from >= fr.end - 1) from = fr.begin;
    sv.painted_cursor = -1;
    playback.play(from, fr.end);
    sv.transport = Transport::Live;
    follow_cursor(from);
    damage_row();
}

void GuiScoreViewer::seek_to(int64_t frame, bool write) {
    AppState::ScoreViewer& sv = app.score_viewer;
    if (!sv.active || !app.score || !score_viewer_has_soundtrack(app)) return;
    const ScoreFrameRange fr = score_viewer_frame_range(app);
    const int64_t target = std::clamp(frame, fr.begin, fr.end);
    int64_t& rest = active_view_state(app).score_position;
    if (sv.transport == Transport::Live) {
        // A LIVE RESEEK IS play() OVER THE LIVE SESSION (the render player's
        // seek_to): no stop-and-restart, the window still the range's; a
        // target in its last frame stops one short so the natural end takes
        // over.
        const int64_t live_target = std::min(target, fr.end - 2);
        if (live_target < fr.begin) return;
        sv.painted_cursor = -1;
        playback.play(live_target, fr.end);
        rest = live_target;
    } else {
        rest = target;
    }
    follow_cursor(rest);
    damage_row();
    if (write) write_score_viewer_file(app);
}

void GuiScoreViewer::commit_position() {
    AppState::ScoreViewer& sv = app.score_viewer;
    if (!sv.active || !app.score || !score_viewer_has_soundtrack(app)) return;
    if (sv.transport == Transport::Live)
        active_view_state(app).score_position = playback.cursor();
    write_score_viewer_file(app);
}

bool GuiScoreViewer::follow_cursor(int64_t pos) {
    AppState::ScoreViewer& sv = app.score_viewer;
    const int p = score_page_for_position(app, pos);
    if (p == sv.page) return false;
    sv.page = p;
    damage_interior();
    return true;
}

void GuiScoreViewer::set_start() {
    if (!score_viewer_button_enabled(app, playback,
                                     AppState::ScoreButtonAct::SetStart))
        return;
    app.score_first_page = app.score_viewer.page + 1;
    range_changed();
    write_score_viewer_file(app);
    damage_row();
}

void GuiScoreViewer::set_end() {
    if (!score_viewer_button_enabled(app, playback,
                                     AppState::ScoreButtonAct::SetEnd))
        return;
    app.score_last_page = app.score_viewer.page + 1;
    range_changed();
    write_score_viewer_file(app);
    damage_row();
}

void GuiScoreViewer::range_changed() {
    AppState::ScoreViewer& sv = app.score_viewer;
    if (!score_viewer_has_soundtrack(app)) return;
    // THE RANGE EXCLUDES EVERYTHING OUTSIDE IT (the head): the range narrows
    // around the shown page, which holds the cursor, so the cursor stays
    // inside and only the window's end moves — republished LIVE at the
    // engine's own cursor (play() is safe over a live session), clamped at
    // rest.
    const ScoreFrameRange fr = score_viewer_frame_range(app);
    int64_t& rest = active_view_state(app).score_position;
    if (sv.transport == Transport::Live) {
        if (fr.length() < 2) return;
        const int64_t at = std::clamp(playback.cursor(), fr.begin, fr.end - 2);
        sv.painted_cursor = -1;
        playback.play(at, fr.end);
        rest = at;
    } else {
        rest = std::clamp(rest, fr.begin, fr.end);
    }
    follow_cursor(rest);
}

void GuiScoreViewer::raise_reset() {
    if (app.prompt.active) return;
    if (!score_viewer_button_enabled(app, playback,
                                     AppState::ScoreButtonAct::Reset))
        return;
    // THE QUESTION PAUSES A LIVE SOUNDTRACK (the head): the render player's
    // Delete question's own step, a modal surface opening over its transport.
    if (app.score_viewer.transport == Transport::Live) toggle_play();
    app.prompt.present("Reset the page range to every page?",
                       kPromptOkCancel,
                       DialogTrigger::SCORE_RESET_CONFIRM,
                       PromptInitialFocus::LastButton);
    viewport.invalidate_all();
}

void GuiScoreViewer::confirm_reset() {
    if (!app.score_viewer.active) return;
    app.score_first_page = 0;
    app.score_last_page  = 0;
    range_changed();
    write_score_viewer_file(app);
    damage();
}

void GuiScoreViewer::on_natural_end() {
    // THE FENCE FIRST, through the one stop body (the session word's playing
    // bit is already down; stop() is the quiescence proof), its score fork
    // parking the transport at PAUSED; then THE REST AT THE RANGE'S FIRST
    // FRAME (the head's THE TRANSPORT), its page shown, written.
    playback_lifecycle.stop_playback_if_playing();
    const ScoreFrameRange fr = score_viewer_frame_range(app);
    active_view_state(app).score_position = fr.begin;
    follow_cursor(fr.begin);
    write_score_viewer_file(app);
    damage_row();
}

void GuiScoreViewer::tick() {
    AppState::ScoreViewer& sv = app.score_viewer;
    drain_decoder();
    if (!sv.active || !app.score) return;

    // THE SOUNDTRACK'S ARRIVAL OR FAILURE repaints the face it decides: the
    // PLAY key lights, or a load that failed takes the page road's face.
    const GuiScoreAudio::State audio_state = app.score_audio.state();
    if (audio_state != sv.seen_audio) {
        sv.seen_audio = audio_state;
        damage();
    }

    if (sv.transport == Transport::Live) {
        if (playback.device_unavailable()) {
            // NO DEVICE PAUSES (the render player's tick's rule): the stop
            // body's fork reads the held cursor and parks the transport; the
            // frame written, the one card said. Nothing here retries: the
            // next PLAY press reopens through play().
            playback_lifecycle.stop_playback_if_playing();
            write_score_viewer_file(app);
            damage_row();
            notifications.notify(AppState::NotificationClass::Normal,
                                 kPlaybackDeviceUnavailableCard);
        } else if (!playback.is_playing()) {
            on_natural_end();
        } else {
            // THE PAGE FOLLOWS THE CURSOR, the turn's position owed until the
            // turn is on screen; THE CLOCK AND THE SCRUB track every position
            // change (the render player's damage rule: the two cells the
            // painter published, the row before its first paint).
            const int64_t cur = playback.cursor();
            if (follow_cursor(cur)) sv.position_owed = true;
            if (cur != sv.painted_cursor) {
                sv.painted_cursor = cur;
                const GuiRect clock = app.modal_dialog.clock;
                const GuiRect scrub = app.modal_dialog.scrub;
                if (clock.w > 0 && clock.h > 0) viewport.invalidate_rect(clock);
                if (scrub.w > 0 && scrub.h > 0) viewport.invalidate_rect(scrub);
                if ((clock.w <= 0 || clock.h <= 0) &&
                    (scrub.w <= 0 || scrub.h <= 0))
                    damage_row();
            }
        }
    }

    // A PAGE TURN UNDER PLAYBACK WRITES THE CURSOR'S FRAME (the head), once
    // the turn has been painted, so the write never stands between the tick
    // and the turn's frame.
    if (sv.position_owed && sv.painted_page == sv.page) {
        sv.position_owed = false;
        if (sv.transport == Transport::Live)
            active_view_state(app).score_position = playback.cursor();
        write_score_viewer_file(app);
    }

    feed_decoder();
}

void GuiScoreViewer::drain_decoder() {
    AppState::ScoreViewer& sv = app.score_viewer;
    for (GuiScorePageResult& r : sv.decoder.drain()) {
        AppState::ScoreViewer::Cached c;
        c.key     = r.key;
        c.surface = r.surface;
        c.failed  = r.surface == nullptr;
        // An entry already standing under the key (a result the decoder
        // finished twice across a change of order) is replaced.
        std::erase_if(sv.cache, [&](const AppState::ScoreViewer::Cached& e) {
            return e.key == c.key;
        });
        const bool shown = sv.active && c.key.page == sv.page;
        sv.cache.push_back(std::move(c));
        if (shown) damage_interior();
    }
}

void GuiScoreViewer::feed_decoder() {
    AppState::ScoreViewer& sv = app.score_viewer;
    if (!sv.active || !app.score) return;
    const GuiRect box = modal_box_interior_rect(app, /*keyboard_h=*/0);
    if (box.w <= 0 || box.h <= 0) return;
    const bool convert = display_transform::active();
    const ScoreViewerRange r = score_viewer_range(app);
    const int p = sv.page;
    const auto in_reach = [&](int i) {
        return i >= r.first && i <= r.last && i >= p - kCacheReach &&
               i <= p + kCacheReach;
    };
    // THE CACHE'S REACH (the head's THE CACHE): an entry made for another
    // box, under the other conversion, or outside the shown page's reach
    // goes.
    std::erase_if(sv.cache, [&](const AppState::ScoreViewer::Cached& c) {
        return c.key.box_w != box.w || c.key.box_h != box.h ||
               c.key.convert != convert || !in_reach(c.key.page);
    });
    // THE LIKELY ORDER: the shown page, then forward first while the
    // soundtrack sounds or after NEXT, both ways otherwise.
    std::vector<int> order{p};
    const bool ahead = sv.transport == Transport::Live || sv.forward;
    if (ahead) {
        for (int d = 1; d <= kCacheReach; ++d) order.push_back(p + d);
        for (int d = 1; d <= kCacheReach; ++d) order.push_back(p - d);
    } else {
        for (int d = 1; d <= kCacheReach; ++d) {
            order.push_back(p + d);
            order.push_back(p - d);
        }
    }
    std::vector<GuiScorePageKey> keys;
    std::vector<GuiScorePageJob> jobs;
    for (const int i : order) {
        if (!in_reach(i)) continue;
        const GuiScorePageKey key{i, box.w, box.h, convert};
        const bool held = std::any_of(
            sv.cache.begin(), sv.cache.end(),
            [&](const AppState::ScoreViewer::Cached& c) { return c.key == key; });
        if (held) continue;
        keys.push_back(key);
        jobs.push_back(GuiScorePageJob{key, app.score->page_file(i)});
    }
    if (keys == sv.asked) return;
    sv.asked = keys;
    sv.decoder.request(std::move(jobs));
}

void GuiScoreViewer::damage() {
    viewport.invalidate_rect(modal_area_rect(app, /*keyboard_h=*/0));
}

void GuiScoreViewer::damage_interior() {
    viewport.invalidate_rect(modal_box_interior_rect(app, /*keyboard_h=*/0));
}

void GuiScoreViewer::damage_row() {
    viewport.invalidate_rect(modal_controls_row_rect(app, /*keyboard_h=*/0));
}
