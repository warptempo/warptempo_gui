#include "car_transport.h"

#include "input_handler.h"   // run_undo_redo_without_key, modal_dialog_editor_active

#include <cstdint>
#include <string>
#include <utility>

// The title (the formula, his example and the spelling at the declaration).
std::string car_transport_undo_position_line(const UndoHistory& history) {
    const int64_t u = static_cast<int64_t>(history.undo_stack.size());
    const int64_t r = static_cast<int64_t>(history.redo_stack.size());
    if (!history.saved_valid)
        return "?, " + std::to_string(u) + ", ?";
    const int64_t d      = history.saved_distance;
    const int64_t before = -(u + d);
    const int64_t middle = -d;
    const int64_t after  = r - d;
    // to_string spells a negative with its minus and a zero bare, which is
    // the first's and the middle's whole spelling; the third takes a `+`.
    std::string s = std::to_string(before) + ", " + std::to_string(middle) +
                    ", ";
    if (after > 0) s += '+';
    s += std::to_string(after);
    return s;
}

// THE GATE (the terms and their reasons are at the head of car_transport.h).
// The render player never reaches this: main.cpp's hook forks on its mode
// bit ahead of this cluster, so the overlay term below is the picker's and
// the stats panel's.
bool GuiCarTransport::admits() const {
    if (app.prompt.active) return false;
    if (folder_overlay_stands(app)) return false;
    if (input_handler.modal_dialog_editor_active()) return false;
    if (app.history_mode.active) return false;
    return true;
}

void GuiCarTransport::on_media_command(GuiMediaCommand cmd) {
    using Kind = GuiMediaCommand::Kind;
    switch (cmd.kind) {
        case Kind::Play:
        case Kind::Pause:
        case Kind::PlayPause:
            car_toggle();
            return;
        case Kind::Previous:
            car_previous();
            return;
        case Kind::Next:
            car_next();
            return;
        case Kind::Stop:
            // PAUSE IS STOP, so Stop is the same act: the one stop body,
            // which finds nothing to do at rest.
            if (!admits()) return;
            playback_lifecycle.stop_playback_if_playing();
            return;
        case Kind::FocusLost:
        case Kind::FocusLostTransient:
            // Android's one imposed interrupt: a live transport session
            // (the project play or the A/B audition, a rest of it included)
            // takes the one stop body; nothing sounding, nothing written.
            if (!admits()) return;
            if (transport_session_live(app) || playback.is_playing())
                playback_lifecycle.stop_playback_if_playing();
            return;
        case Kind::FastForward:
        case Kind::Rewind:
        case Kind::SeekTo:
        case Kind::FocusGained:
            // Consumed no-ops (the table at the declaration).
            return;
    }
}

// THE CAR'S SPACE. Space's own target-view gate is asked here, ahead of the
// launch, exactly where on_key asks it ahead of toggle_playback: in target
// view with nothing playing and the preview not ready the press is a silent
// refusal — row 8's process line already carries the preview render's
// `Updating...`, the one-dimensional class (messaging.md's silent list). The
// stop arm never meets it: `!playback.is_playing()` is the gate's own term.
void GuiCarTransport::car_toggle() {
    if (!admits()) return;
    if (app.active_audio_view == 'T' && !playback.is_playing() &&
        !target_render.preview_ready()) {
        return;
    }
    playback_lifecycle.car_toggle_playback();
}

void GuiCarTransport::car_previous() {
    if (!admits()) return;
    input_handler.run_undo_redo_without_key(/*redo=*/false);
}

void GuiCarTransport::car_next() {
    if (!admits()) return;
    input_handler.run_undo_redo_without_key(/*redo=*/true);
}

// THE DERIVED STATE (the three lines and the clock at the head comment).
GuiMediaState GuiCarTransport::derive() const {
    GuiMediaState st;
    // The app is running, so the console's buttons must reach it: active
    // always, which the consuming side publishes as PLAYING at speed 1.0 —
    // the dummy display, the player's ruling extended to the transport.
    st.session_active = true;
    // The true transport bit, read on the consuming side for the audio focus
    // alone (as the player's is).
    st.playing        = transport_session_live(app);
    st.album          = app.project_name;
    st.title          = car_transport_undo_position_line(app.history);
    st.artist         = view_pair_label(app.active_audio_view,
                                        app.active_markers_view);
    st.duration_ms    = -1;
    st.position_ms    = 0;
    const int64_t rate = audio.sample_rate();
    if (st.playing && rate > 0) {
        // The loop clock: the predictor's position less the trim's begin,
        // floored at 0 (a position under the begin is the launch anchor's
        // floor or the frame before a resync lands, both momentary).
        int64_t pos = playback.cursor() - viewport.trim_range().first;
        if (pos < 0) pos = 0;
        st.position_ms = pos * 1000 / rate;
    }
    return st;
}

void GuiCarTransport::tick() {
    if (app.render_player.active) {
        // The player owns the wire while it stands; the falling edge below
        // takes it back.
        handed_to_player_ = true;
        return;
    }
    const GuiMediaState st = derive();
    const bool changed =
        !last_pushed_.valid || handed_to_player_ ||
        wrap_epoch_ != last_pushed_.wrap_epoch ||
        st.session_active != last_pushed_.state.session_active ||
        st.playing        != last_pushed_.state.playing ||
        st.title          != last_pushed_.state.title ||
        st.artist         != last_pushed_.state.artist ||
        st.album          != last_pushed_.state.album ||
        st.duration_ms    != last_pushed_.state.duration_ms;
    if (!changed) return;
    gui.publish_media_state(st);
    last_pushed_.valid      = true;
    last_pushed_.state      = st;
    last_pushed_.wrap_epoch = wrap_epoch_;
    handed_to_player_       = false;
}

void GuiCarTransport::note_loop_wrap() {
    ++wrap_epoch_;
}
