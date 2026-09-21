#include "car_transport.h"

#include "input_handler.h"   // run_undo_redo_without_key,
                             // car_play_refused_by_key_gates,
                             // modal_dialog_editor_active

#include <cstdint>
#include <string>
#include <utility>

// The title (the formula, his example and the spelling at the declaration).
std::string car_transport_title_line(const UndoHistory& history) {
    const int64_t u = static_cast<int64_t>(history.undo_stack.size());
    // The live state's number in the session walk's counting: U + 1.
    std::string s = std::to_string(u + 1);
    s += '_';
    if (!history.saved_valid) {
        s += '?';
        return s;
    }
    // to_string spells a negative with its minus and a zero bare, which is the
    // distance's whole spelling.
    s += std::to_string(-static_cast<int64_t>(history.saved_distance));
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
    // ANY TRANSPORT ACT FROM THE CONSOLE CANCELS A PENDING CAR PLAY (his
    // word; the contract and the other clears at the latch's declaration).
    // It sits AHEAD of the switch so every arm is covered by one line —
    // including the arms that return early and the ones that do nothing at
    // all — and the two skips re-arm in their own tails below if their new
    // step is refused for readiness again.
    pending_play_ = PendingCarPlay{};
    using Kind = GuiMediaCommand::Kind;
    switch (cmd.kind) {
        case Kind::Play:
            // PLAY MEANS REOPEN THE STREAM AND START NOTHING (the measured
            // reason and the accepted cost are at the table). UNGATED, and
            // deliberately so: this arm writes no authored, transport or modal
            // state, and its whole purpose — the Bluetooth audio link coming
            // up under the car's fade-in — holds whatever stands on the
            // screen, so admits() has nothing to say about it — and the
            // render player's own arm answers ahead of its prompt guard for
            // that same reason, so both car roads answer a connect alike. The
            // answer is IGNORED: a console key is not a deliberate press at
            // the glass, so a failed reopen raises no card; the next real
            // press meets the launch gates and cards there.
            (void)playback.ensure_device_available_for_play();
            return;
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

// THE CAR'S SPACE, AND IT MEETS SPACE'S OWN GATES (architect 2026-09-17 —
// each car button is its key): past admits() the press asks the HEAD GATES
// on_key asks ahead of bare Space, through the one statement of them
// (GuiInputHandler::car_play_refused_by_key_gates, which carries the order,
// the cards and the two gates it deliberately does not ask), so a flag editor
// or a pointer drag answers the console exactly as it answers the keyboard.
// Then Space's own target-view gate, asked here where on_key asks it ahead of
// toggle_playback: in target view with nothing playing and the preview not
// ready the press is a silent refusal — row 8's process line already carries
// the preview render's `Updating...`, the one-dimensional class
// (messaging.md's silent list). The stop arm never meets it:
// `!playback.is_playing()` is the gate's own term.
void GuiCarTransport::car_toggle() {
    if (!admits()) return;
    if (input_handler.car_play_refused_by_key_gates()) return;
    if (app.active_audio_view == 'T' && !playback.is_playing() &&
        !target_render.preview_ready()) {
        return;
    }
    playback_lifecycle.car_toggle_playback();
}

// THE SKIPS: the chord whole, and then the sound of what it stepped to
// (architect 2026-09-18; the ruling, his complaint and the two rules are at
// the head of car_transport.h). THE STEP'S OWN ANSWER IS THE WHOLE GATE — a
// press that restored nothing plays nothing and leaves a running loop
// running, every refusal ending where it ended before with its own card.
void GuiCarTransport::car_previous() {
    if (!admits()) return;
    if (!input_handler.run_undo_redo_without_key(/*redo=*/false)) return;
    car_play_after_step();
}

void GuiCarTransport::car_next() {
    if (!admits()) return;
    if (!input_handler.run_undo_redo_without_key(/*redo=*/true)) return;
    car_play_after_step();
}

// THE PLAY TAIL (contract at the declaration): the car's play, or the wait
// that stands in for it. Space's own target-view readiness gate is asked here
// in exactly the shape car_toggle asks it — but where the toggle REFUSES on
// it, the tail WAITS: the restore has just triggered the preview and in
// target view it is often already ready, while a state authored in source
// view has never been rendered and needs the worker to come back (rule 2 at
// the head of this file's header). Nothing is carded either way: the refusal
// is the silent one-dimensional class row 8 answers with `Updating...`.
void GuiCarTransport::car_play_after_step() {
    if (app.active_audio_view == 'T' && !target_render.preview_ready()) {
        pending_play_.armed      = true;
        pending_play_.audio_view = app.active_audio_view;
        pending_play_.tab        = app.active_tab_view;
        pending_play_.state_id   = car_transport_title_line(app.history);
        pending_play_.gui_press_count = app.gui_transport_press_count;
        return;
    }
    playback_lifecycle.car_play_playback();
}

// THE PENDING CAR PLAY'S TICK BODY — every clear is enumerated at the latch's
// declaration and every one of them is a state read right here, no timer and
// no window anywhere. (The player's own clear is tick's early arm, above the
// call to this.)
void GuiCarTransport::run_pending_play() {
    if (!pending_play_.armed) return;
    if (!admits()) {
        pending_play_ = PendingCarPlay{};
        return;
    }
    if (app.active_audio_view != pending_play_.audio_view ||
        app.active_tab_view   != pending_play_.tab ||
        car_transport_title_line(app.history) != pending_play_.state_id ||
        app.gui_transport_press_count != pending_play_.gui_press_count) {
        // A later step, a view switch, a tab switch or a save superseded the
        // wait: the state it was for is not the state on screen. Or a GUI
        // transport press landed, played or refused, and that press is the
        // answer to what sounds now (the rule at the latch's declaration).
        pending_play_ = PendingCarPlay{};
        return;
    }
    if (playback.is_playing()) {
        // He started something himself at the glass; the console's wait
        // does not get to talk over it.
        pending_play_ = PendingCarPlay{};
        return;
    }
    // STILL SETTLING: keep waiting, silently. The preview's own line is on
    // row 8 while it runs.
    if (target_render.is_updating()) return;
    // SETTLED, one way or the other: the wait is over whatever came back.
    const bool ready = target_render.preview_ready();
    pending_play_ = PendingCarPlay{};
    if (ready) playback_lifecycle.car_play_playback();
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
    st.title          = car_transport_title_line(app.history);
    // THE ARTIST NAMES THE TAB AND THEN THE VIEW (architect 2026-09-17, from
    // the car: "instead of just T+W it should say A) T+W"): the active A/B
    // tab's own letter, a close parenthesis and a space, then the view pair
    // exactly as the view bar spells it. THE TAB TERM IS THIS LINE'S ALONE —
    // view_pair_label is the VIEW BAR's speller and its four buttons paint
    // `T+W` with no tab in it — so the tab letter is composed here and in no
    // other place.
    st.artist         = std::string(1, app.active_tab_view) + ") " +
                        view_pair_label(app.active_audio_view,
                                        app.active_markers_view);
    // THE CLOCK, AND A LENGTH ONLY WHILE THE LOOP SOUNDS. The session is
    // published PLAYING AT SPEED 1.0 AT ALL TIMES by ruling (the dummy
    // display, the head comment), so the console extrapolates a clock of its
    // own from every push: a duration published at rest would run that clock
    // into the end of a track that is not sounding and stop it there. The
    // render player's item arm draws the same line — its item carries a real
    // length only while it sounds, and its silence track sends -1.
    //
    // ONE GATE SERVES BOTH FIELDS, NOT TWO. A live A/B audition sets
    // transport_session_live as well, and it plays BOUNDED WINDOWS rather than
    // the trim, so it inherits here exactly the approximation its position has
    // always carried; no bit is added to tell a car loop from an audition,
    // the engine exposing no main-thread "is looping" read (only
    // consume_loop_wrap, which is the wrap's own edge).
    st.duration_ms    = -1;
    st.position_ms    = 0;
    const int64_t rate = audio.sample_rate();
    if (st.playing && rate > 0) {
        // ONE PAIR OF BOUNDS FOR BOTH FIELDS, taken once: the trim window is
        // the car loop's whole lap, so its LENGTH is the track's length and
        // the cursor's distance from its begin is the position.
        // Viewport::trim_range answers the ACTIVE DOMAIN's bounds (source
        // frames in source view, the live target domain's through the map in
        // target view, navigation_trim_range).
        const std::pair<int64_t, int64_t> trim = viewport.trim_range();
        // ONE DIVISOR SERVES BOTH, AND NEITHER FORKS ON THE AUDIO VIEW: the
        // render body's per-output-frame increment is source_rate /
        // output_rate whichever buffer is bound (playback_common.h), so the
        // bound buffer's frames are consumed at the source's own rate in
        // either domain.
        const int64_t span = trim.second - trim.first;
        // A DEGENERATE OR CROSSED PAIR PUBLISHES NO LENGTH: the range clamps
        // each side into the domain and deliberately does NOT order them
        // (mid-gesture crossing is free and this runs per tick), so the
        // unknown -1 above stands rather than a negative or zero length the
        // console would have to interpret.
        if (span > 0) st.duration_ms = span * 1000 / rate;
        // The loop clock: the predictor's position less the trim's begin,
        // floored at 0 (a position under the begin is the launch anchor's
        // floor or the frame before a resync lands, both momentary).
        int64_t pos = playback.cursor() - trim.first;
        if (pos < 0) pos = 0;
        st.position_ms = pos * 1000 / rate;
    }
    return st;
}

void GuiCarTransport::tick() {
    if (app.render_player.active) {
        // The player owns the wire while it stands; the falling edge below
        // takes it back. A PENDING CAR PLAY DIES HERE: the head unit has
        // another owner now, and a wait armed before the player opened is not
        // this cluster's to fire behind it.
        pending_play_ = PendingCarPlay{};
        handed_to_player_ = true;
        return;
    }
    // THE LATCH RUNS AHEAD OF THE COMPARATOR, so a play it fires is already
    // in the state this tick derives and pushes: the console's clock and
    // length go live on the same tick the sound starts, with no extra push.
    run_pending_play();
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
