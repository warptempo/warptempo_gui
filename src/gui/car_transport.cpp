#include "car_transport.h"

#include "input_handler.h"   // run_undo_redo_without_key,
                             // car_play_refused_by_key_gates,
                             // keyboard_modal_editor_active

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
    // ALWAYS SIGNED, as a sweep cell's hop entry is (format_signed_hops): the
    // sign then the magnitude, a zero carrying '+'.
    const int64_t distance = -static_cast<int64_t>(history.saved_distance);
    s += distance < 0 ? '-' : '+';
    s += std::to_string(distance < 0 ? -distance : distance);
    return s;
}

// THE GATE (the terms and their reasons are at the head of car_transport.h).
// The render player never reaches this: main.cpp's hook forks on its mode
// bit ahead of this cluster, so the overlay term below is the picker's.
bool GuiCarTransport::admits() const {
    if (app.prompt.active) return false;
    // THE PROGRESS MODAL (2026-10-14): a batch's modal stops playback at its
    // open and covers the waveform, so the console starts nothing under it.
    if (app.batch_progress.active) return false;
    if (folder_overlay_stands(app)) return false;
    // THE SCORE VIEWER (2026-10-26): its open stops playback and nothing of
    // the project's plays under it — the engine is its own soundtrack's or
    // silent (score_viewer.h's head, 2026-10-26 night), with no car hook — so
    // the console starts nothing either.
    if (score_viewer_stands(app)) return false;
    // EVERY EDITOR (2026-10-25: an act that takes the keyboard is a modal,
    // keyboard_modal_editor_active — the dialog editors alone until then,
    // the lane editors reaching the helpers' own gates).
    if (input_handler.keyboard_modal_editor_active()) return false;
    if (app.history_mode.active) return false;
    return true;
}

void GuiCarTransport::on_media_command(GuiMediaCommand cmd) {
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
            // press meets the launch gates and cards there. A dead stream's
            // reopen here RESUMES AN AUDIBLE NOISE BED on the new route with
            // a fresh segment (2026-11-05, the route drop's reading;
            // close_dead_stream, playback_aaudio.cpp, noise.h's head): the
            // bed never resumes by itself, and this reopen is a press's.
            (void)playback.ensure_device_available_for_play();
            return;
        case Kind::Pause:
        case Kind::PlayPause:
            car_toggle();
            return;
        case Kind::Previous:
            car_skip(/*forward=*/false);
            return;
        case Kind::Next:
            car_skip(/*forward=*/true);
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
// (notifications.h's silent list). The stop arm never meets it:
// `!playback.is_playing()` is the gate's own term, and a STANDING A/B
// AUDITION passes it to car_toggle_playback's stop fork in every phase,
// OtherFirst waiting on an unrendered preview included — the readiness
// question a launch's alone, as at on_key's twin (2026-10-28).
void GuiCarTransport::car_toggle() {
    if (!admits()) return;
    if (input_handler.car_play_refused_by_key_gates()) return;
    if (!audition_sequence_standing(app) &&
        app.active_audio_view == 'T' && !playback.is_playing() &&
        !target_render.preview_ready()) {
        return;
    }
    playback_lifecycle.car_toggle_playback();
}

// PREVIOUS AND NEXT, THE TWO MODES (architect 2026-11-19 / 20, send CC; the
// ruling, his words, the predicate's argument and the audition's at the head
// of car_transport.h). Past admits(), the fork is transport_session_live:
//   LIVE — a standing A/B audition consumes the press in silence; otherwise
//     the press meets Space's own head gates (a pointer drag cards as Space
//     does) and then THE SEEK, kCarSeekSeconds of the bound buffer at the
//     source rate, whose walls and silent refusals are the lifecycle's
//     (GuiPlaybackLifecycle::seek_live_session_by). A seek that moved bumps
//     the jump epoch, so the next tick re-pushes the console's clock at the
//     new place. NO HISTORY STEP ON THIS ARM, whatever the seek answered.
//   AT REST — undo / redo whole (run_undo_redo_without_key: Ctrl+Z's and
//     Ctrl+Shift+Z's head gates, refusals and cards), and NOTHING PLAYS: the
//     step is the whole press.
void GuiCarTransport::car_skip(bool forward) {
    if (!admits()) return;
    if (transport_session_live(app)) {
        if (audition_sequence_standing(app)) return;
        if (input_handler.car_play_refused_by_key_gates()) return;
        const int64_t rate = audio.sample_rate();
        if (rate <= 0) return;
        const int64_t step = kCarSeekSeconds * rate;
        if (playback_lifecycle.seek_live_session_by(forward ? step : -step))
            ++jump_epoch_;
        return;
    }
    input_handler.run_undo_redo_without_key(/*redo=*/forward);
}

// THE DERIVED STATE (the three lines and the clock at the head comment).
GuiMediaState GuiCarTransport::derive(ClockSide& side) const {
    GuiMediaState st;
    side = ClockSide::None;
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
    // exactly as view_pair_label spells it. THE TAB TERM IS THIS LINE'S ALONE
    // — view_pair_label is the view pair's one speller and carries no tab —
    // so the tab letter is composed here and in no other place.
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
        // CLAMPED INTO [0, the length] where the length is known, so no push
        // carries a position outside the duration it publishes. ON THE CAR'S
        // LOOP NEITHER CLAMP BITES while the trim stands as it was launched
        // on: the loop's window is that trim (GuiPlaybackLifecycle::
        // car_toggle_playback; the publish only clamps its start up and its
        // end down into the buffer), and the predictor is floored at the
        // window's start or loop target and clamped at its end. A ONCE-
        // THROUGH PLAY (Space's, the scrub's; the console's seek over it,
        // GuiPlaybackLifecycle::seek_live_session_by) CAN STAND OUTSIDE THE
        // TRIM — in source view it runs to the song's end — and then each
        // push carries 0 BEFORE THE TRIM'S BEGIN AND THE FULL LENGTH AT ITS END
        // AND BEYOND: the clock keeps the loop's meaning (architect
        // 2026-11-20, send DD, on the clock as built: "it only happens in
        // source view but car is for target view").
        // THE CROSSING IS A PUSH (2026-11-20, send CC's second review,
        // finding A2): the side the cursor stands on — before the begin,
        // inside the trim (both bounds included), past the end — is a term of
        // tick()'s comparator, so the tick that first finds the cursor on
        // another side pushes once: ENTERING THE TRIM pushes the position at
        // that moment (0 plus the tick's few ms), and the console's count is
        // right from there on; LEAVING PAST THE END pushes the held full
        // position. One push per crossing and none while the side stands —
        // the position itself is no term, so a held clamp never storms — and
        // a crossing made by a seek or a wrap rides that jump's own push
        // (one tick, one push). The predictor runs forward between its
        // resyncs, so a side is crossed once per run of the line, never
        // chattered. On the car's loop the cursor never leaves the trim it
        // was launched on (its window that trim, the predictor floored at the
        // loop's start or target and clamped at its end), so no side push adds
        // to its laps; a trim moved under a sounding loop moves the duration,
        // a push of its own. WHAT STAYS
        // UNTRUE: the console extrapolates every push at speed 1.0 (the
        // session published PLAYING at all times, the dummy display's ruling
        // on the Java side), so A HELD POSITION STILL COUNTS ON BY ITSELF
        // THERE — the stretch BEFORE THE ENTRY counts up from 0 until the
        // entry's push sets it back, and PAST THE END the console runs on
        // from the full position — and nothing in this publisher can hold
        // that clock still. The other option above has no such stretch.
        const int64_t raw = playback.cursor() - trim.first;
        int64_t pos = raw;
        if (pos < 0) pos = 0;
        if (span > 0) {
            if (pos > span) pos = span;
            side = raw < 0     ? ClockSide::Before
                 : raw > span  ? ClockSide::Past
                               : ClockSide::Inside;
        }
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
    ClockSide side = ClockSide::None;
    const GuiMediaState st = derive(side);
    const bool changed =
        !last_pushed_.valid || handed_to_player_ ||
        jump_epoch_ != last_pushed_.jump_epoch ||
        side != last_pushed_.side ||
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
    last_pushed_.jump_epoch = jump_epoch_;
    last_pushed_.side       = side;
    handed_to_player_       = false;
}

void GuiCarTransport::note_loop_wrap() {
    ++jump_epoch_;
}
