#include "ab_audition.h"

#include "input_handler.h"   // run_center_command, the `c` command's one owner

#include <cmath>
#include <cstdint>

// The A/B audition's operations (contracts at the header). The state's model
// and edge inventory are at GuiAuditionSequence, app_state.h.

int64_t GuiAbAudition::audition_span_frames() const {
    return static_cast<int64_t>(std::nearbyint(
        static_cast<double>(audio.sample_rate()) * kAuditionMs / 1000.0));
}

namespace {
// The preview gate is the SAME predicate Space's play edge reads
// (GuiTargetRender::preview_ready), and the frame gate is the launch body's own
// (playback_launch_playable) asked ahead of time for a tab that is not active
// yet — the switch itself takes no gate, so the other tab's frame must be
// judged here or not at all. In source view the predicate has no preview term
// and only the two-frame remainder gate answers. THE DEVICE IS NOT ASKED HERE:
// it is not a per-tab fact, so start() asks it once for the pair, ahead of
// this. File-static so the member gate, start()'s carded preflight and the
// PLAY face's ask-ahead (ab_audition_preflight_ok below) read one spelling.
bool tab_launch_ready_impl(const AppState& app, const GuiAudio& audio,
                           const GuiPlayback& playback,
                           const GuiTargetRender& target_render,
                           int64_t frame) {
    if (app.active_audio_view == 'T' && !target_render.preview_ready()) {
        return false;
    }
    return playback_launch_playable(app, playback, audio.total_frames(), frame);
}

// THE OTHER TAB'S PLAYHEAD, read through its own ViewState and clamped
// exactly as switch_active_tab_view_to will clamp it on entry
// (clamp_playhead_to_live_domain reads the live domain, which both tabs
// share), so the verdict here is the verdict the launch would reach there.
int64_t other_tab_playhead(const AppState& app, const GuiAudio& audio) {
    const char other = (app.active_tab_view == 'A') ? 'B' : 'A';
    const ViewState& other_tab = (other == 'A') ? app.tab_a : app.tab_b;
    return clamp_playhead_to_live_domain(other_tab.playhead_cursor_sample,
                                         app, audio);
}
}  // namespace

bool GuiAbAudition::tab_launch_ready(int64_t frame) const {
    return tab_launch_ready_impl(app, audio, playback, target_render, frame);
}

// THE PLAY FACE'S ASK-AHEAD (architect 2026-08-30, the twin rule; the
// contract and the one reader are at the declaration, app_state.h): start()'s
// press-time gates asked without acting, in start()'s own order — the device
// once for the pair, then both tabs' launch readiness. The running-sequence
// arm is deliberately not here — and since 2026-08-31 it is not a refusal at
// all but the act's STOP: a standing sequence is transport_session_live, which
// the face reads ahead of this and answers with its lit STOP glyph, so a press
// on that face always does something. It sits beside start() so the two read as
// one; a gate added there must be added here.
bool ab_audition_preflight_ok(const AppState& app, const GuiAudio& audio,
                              const GuiPlayback& playback,
                              const GuiTargetRender& target_render) {
    if (playback.device_unavailable()) return false;
    return tab_launch_ready_impl(app, audio, playback, target_render,
                                 app.playhead_cursor_sample) &&
           tab_launch_ready_impl(app, audio, playback, target_render,
                                 other_tab_playhead(app, audio));
}

void GuiAbAudition::start() {
    using Phase = GuiAuditionSequence::Phase;
    // A SEQUENCE ALREADY RUNNING: THE PRESS STOPS IT (architect 2026-08-31,
    // superseding the 2026-08-30 "An audition is already running" card). The
    // chord is a TOGGLE like the transport it rides: Shift+Space starts the act
    // and Shift+Space ends it, exactly as bare Space already does through the
    // same body — and the Play button, which wears STOP for the act's whole
    // duration (redesign_button_glyph_swapped reads this same `phase !=
    // Idle`), now AGREES WITH ITS FACE on its shift-click and long press too.
    // `phase` is the act's one running bit in BOTH halves, so this test covers
    // a standing rest as it covers a live play and needs no second term.
    //
    // THE ONE STOP BODY, no second road: this is the body bare Space's fork
    // reaches for the identical case (GuiPlaybackLifecycle::toggle_playback's
    // play/stop fork, whose transport-live term is this same phase test), and
    // it is EXACTLY RIGHT for a rest — its clear of the sequence sits ahead of
    // its own nothing-to-do guard, so it ends the act and then early-returns
    // having moved no cursor and damaged nothing. The press is therefore a
    // caller of clearing owner (1), not a new owner (the inventory at
    // GuiAuditionSequence, app_state.h). No card — the silence stops, which is
    // the answer — and no audition is launched by this press.
    if (app.audition_sequence.phase != Phase::Idle) {
        playback_lifecycle.stop_playback_if_playing();
        return;
    }
    // THE DEVICE IS THE PREFLIGHT'S FIRST QUESTION (2026-08-30), ahead of both
    // tab gates and ahead of the first `c`: the act's four plays all end at the
    // launch body's own device check, so a dead device refuses the whole act —
    // but it would refuse it AFTER this body had zoomed one tab, switched to
    // the other and zoomed that one too, which is exactly the "a refused act
    // moves nothing" promise the gates below are here to keep. It is not a
    // per-tab fact (nothing will sound on either), so it is asked once, before
    // the pair is read at all. The sentence is the launch body's own literal,
    // spelled once at playback_lifecycle.h; this arm returns, so the belt down
    // there never adds a second card to the press.
    if (playback.device_unavailable()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kPlaybackDeviceUnavailableCard);
        return;
    }
    const char home  = app.active_tab_view;
    const char other = (home == 'A') ? 'B' : 'A';
    // THE OTHER TAB'S PLAYHEAD comes from the file's one spelling above
    // (other_tab_playhead — the clamp is the switch's own, so the verdict
    // here is the verdict the launch would reach there); the active tab's is
    // the live cursor.
    const int64_t other_playhead = other_tab_playhead(app, audio);
    // BOTH TABS ARE GATED BEFORE THE FIRST SWITCH — and, since 2026-08-30, so
    // is the device above: an act that could only half-run must not move the
    // user off his tab and then fall silent, and the promise holds only if
    // EVERY reason the launch body can refuse for has been asked before the
    // first camera write. Those reasons are the device and the playable
    // predicate, which is what this pair and the check above are between them.
    // AND THE GATE SAYS SO, ONCE, FOR EITHER TAB (architect 2026-08-30): the
    // act is one act over a pair, so which half of the pair could not play is
    // not the answer — that it cannot run is. The Play button's shift-click
    // and long press reach this same body, so the one card answers every
    // road. (The sentence deliberately does not name the target preview: in
    // target view the readiness gate is the preview's, and a Space pressed
    // for itself already says that in its own words.)
    if (!tab_launch_ready(app.playhead_cursor_sample) ||
        !tab_launch_ready(other_playhead)) {
        notifications.notify(
            AppState::NotificationClass::Normal,
            "One of the two tabs has nothing to play from here");
        return;
    }
    // Step 0: THE `c` COMMAND ON THIS TAB, before the switch carries the band
    // away — refresh_active_tab_view_from_app pushes the live zoom into the
    // leaving tab's slot, so the level set here is the level this tab is
    // restored at when the act switches back (and re-run there, the ruling
    // asking for it on every arrival). It runs AFTER the two gates above, so a
    // refused act moves no camera, and BEFORE the sequence exists, so its land
    // clears nothing that stands (the header's ordering rule). The gates above
    // therefore judged the cursor as it RESTED: `c` can only move it onto this
    // tab's own focus, which the cursor already sits on after every route that
    // sets one, and a play that refused after such a move would simply end the
    // act — the interrupt rule's own answer.
    apply_working_zoom();
    // Step 1: the ordinary tab switch (the stop of any live audition, the
    // selection clear, the band swap, the coincidence auto-select and the
    // synchronous rebuild are all its own). Not Ctrl+Tab's trigger() tail —
    // the header says why.
    active_views.switch_active_tab_view_to(other);
    // And `c` on the tab just entered, IN THE SWITCH'S OWN FRAME: no paint has
    // happened between the two lines, so the flip and the working zoom land
    // together. Still ahead of the launch, so the play below starts from the
    // cursor `c` left (a no-op land after a switch — the header says why).
    apply_working_zoom();
    // Step 2's first play, launched STRAIGHT AWAY — no rest precedes it. The
    // architect's rest is between SOUNDS and nothing sounded before this one
    // (the constants' own note, app_state.h). A refusal here is unreachable in
    // practice (the frame was just validated against the same domain) and
    // would end the act on the other tab, the interrupt rule's own answer.
    launch_phase(Phase::OtherFirst, home);
}

void GuiAbAudition::advance_after_natural_end(
    const GuiAuditionSequence& ended) {
    using Phase = GuiAuditionSequence::Phase;
    // The tab is where the act left it whenever `ended` is non-Idle: any
    // switch by the user in between went through the one stop body, which
    // would have cleared the phase the tick then read. So no tab check is
    // owed before the switch back below.
    switch (ended.phase) {
        case Phase::Idle:
            return;
        case Phase::OtherFirst:
            arm_rest(Phase::OtherSecond, ended.home_tab, kAuditionPairGapMs);
            return;
        case Phase::OtherSecond:
            // Step 3: back to the starting tab, then the SWITCH REST before
            // step 4's first play. The switch runs at the natural end, ahead
            // of the rest, so the user sees the tab flip at once and then
            // hears the silence on the tab about to play — and it must run
            // first for a second reason: switch_active_tab_view_to takes the
            // one stop body, which clears the sequence, so a rest armed ahead
            // of it would be wiped.
            active_views.switch_active_tab_view_to(ended.home_tab);
            // `c` on the tab just re-entered, in that same switch's frame —
            // and BEFORE the arm below, which is the second half of this
            // arm's ordering rule: the command can clear the sequence, so it
            // must not run after a rest has been written (the header).
            apply_working_zoom();
            arm_rest(Phase::HomeFirst, ended.home_tab, kAuditionSwitchGapMs);
            return;
        case Phase::HomeFirst:
            arm_rest(Phase::HomeSecond, ended.home_tab, kAuditionPairGapMs);
            return;
        case Phase::HomeSecond:
            // The act's own end: the stop body the tick just took left the
            // sequence Idle, the tab is the starting one, and both playheads
            // are where they were. Nothing to arm — no rest trails the act.
            return;
    }
}

void GuiAbAudition::arm_rest(GuiAuditionSequence::Phase phase, char home_tab,
                             int gap_ms) {
    // THE REST HALF'S NON-IDLE WRITE. Nothing is playing at this point (the
    // tick took the stop body just before calling the advance), so the act
    // stands on this state alone until fire_if_due launches it or an interrupt
    // clears it.
    app.audition_sequence.phase         = phase;
    app.audition_sequence.home_tab      = home_tab;
    app.audition_sequence.waiting       = true;
    app.audition_sequence.launch_due_ms = monotonic_ms() + gap_ms;
}

void GuiAbAudition::apply_working_zoom() {
    // THE `c` COMMAND, WHOLE, through its one owner — the working zoom, the
    // centering and the focused re-land all decided there, so this cluster
    // holds no zoom knowledge of its own and a retune of `c` reaches the
    // audition for free. THE CAMERA IS NEVER A MOVEMENT: `c` with nothing
    // focused writes no playhead, so it hides no trim overlay and clears
    // nothing; with a focus it lands, and that land is the one write this
    // whole act can make to a resting cursor (the header's two paragraphs
    // carry the case and the ordering the three call sites obey).
    if (input != nullptr) input->run_center_command();
}

void GuiAbAudition::fire_if_due() {
    using Phase = GuiAuditionSequence::Phase;
    GuiAuditionSequence& seq = app.audition_sequence;
    // One compare on an idle tick; the clock is read only past it.
    if (seq.phase == Phase::Idle || !seq.waiting) return;
    if (monotonic_ms() < seq.launch_due_ms) return;
    const Phase phase    = seq.phase;
    const char  home_tab = seq.home_tab;
    // END THE REST BEFORE THE LAUNCH IS ATTEMPTED, and read the two fields
    // above first: this is the FIRED edge, the rest's phase (waiting = true)
    // giving way to the play's (waiting = false), and launch_phase writes the
    // play's phase itself before it launches and clears it again on a
    // refusal. The clear here still matters for the one refusal launch_phase
    // takes BEFORE its write — the preview gate — which must leave the act
    // ENDED rather than a due rest re-tried on every tick from here on. (The
    // launch body clears nothing since 2026-09-01; the user launches' clear is
    // launch_playback_from's, which the act's road never passes.)
    clear_audition_sequence(app);
    launch_phase(phase, home_tab);
}

bool GuiAbAudition::launch_phase(GuiAuditionSequence::Phase phase,
                                 char home_tab) {
    // RE-ASK THE PREVIEW GATE at every launch, not only at the press: this
    // launch has its own precondition and does not inherit the press-time
    // verdict, and a refusal here simply ends the act, the interrupt rule's own
    // answer. The window it was first written for — a target-view mutation
    // landing after a play's natural end and before the tick observes it — is
    // now closed at the source instead: GuiTargetRender::trigger() clears the
    // sequence UNCONDITIONALLY on its target-view path, so such a mutation ends
    // the act outright and the tick hands the advance an Idle phase rather than
    // reaching this line against a stale or re-bound preview. The frame gate is
    // the launch body's.
    if (app.active_audio_view == 'T' && !target_render.preview_ready()) {
        return false;
    }
    // From the active tab's RESTING playhead — read, never written.
    const int64_t start = app.playhead_cursor_sample;
    // THE PLAY HALF'S NON-IDLE WRITE, AHEAD OF THE LAUNCH (architect
    // 2026-09-01; it followed a true return until then, the launch body's head
    // clear having wiped whatever stood): the launch body reads the act off
    // this standing phase — its seed fork asks centered_pin_engaged, which
    // answers false while the act stands, so the play seeds through follow's
    // arm as the act requires — and the body clears no sequence on this road
    // (launch_bounded_audition runs none; the user launches' clear is
    // launch_playback_from's). The sequence is Idle at this line by both
    // callers' construction — start() past its running guard, fire_if_due past
    // its clear — so this is a write onto Idle, not a re-arm. Nothing between
    // here and the launch body paints or dispatches, so the phase standing a
    // call earlier reaches no face and no fork. waiting = false is the whole
    // difference from arm_rest's write: the phase named here is SOUNDING, not
    // owed. Both `c` calls the act makes ahead of its first play precede this
    // write (start), and the switch-back call precedes the arm (the advance),
    // so the header's ordering rule holds unchanged.
    app.audition_sequence.phase         = phase;
    app.audition_sequence.home_tab      = home_tab;
    app.audition_sequence.waiting       = false;
    app.audition_sequence.launch_due_ms = 0;
    if (!playback_lifecycle.launch_bounded_audition(start,
                                                    audition_span_frames())) {
        // A REFUSED LAUNCH ENDS THE ACT, as it always did — the tab staying
        // where it is, the interrupt rule's own answer. The one spelling of
        // "the act is over" is used, so the clear's side effect runs: the
        // phase written above is non-Idle, so the derivation memory's cursor
        // term is voided. From fire_if_due that void already stood (its own
        // clear ended a standing rest); from start() it is new to this path
        // and inert in effect — the act's tab switch just changed the
        // memory's tab term without a paint between, so the next engaged
        // pre-paint was already due.
        clear_audition_sequence(app);
        return false;
    }
    return true;
}
