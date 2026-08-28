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

// The preview gate is the SAME predicate Space's play edge reads
// (GuiTargetRender::preview_ready), and the frame gate is the launch body's own
// (playback_launch_playable) asked ahead of time for a tab that is not active
// yet — the switch itself takes no gate, so the other tab's frame must be
// judged here or not at all. In source view the predicate has no preview term
// and only the two-frame remainder gate answers.
bool GuiAbAudition::tab_launch_ready(int64_t frame) const {
    if (app.active_audio_view == 'T' && !target_render.preview_ready()) {
        return false;
    }
    return playback_launch_playable(app, playback, audio.total_frames(), frame);
}

void GuiAbAudition::start() {
    using Phase = GuiAuditionSequence::Phase;
    // A SEQUENCE ALREADY RUNNING: consumed no-op — never a restart. `phase` is
    // the act's one running bit in BOTH halves, so this test covers a standing
    // rest as it covers a live play and needs no second term. A running PLAIN
    // audition is not a refusal: the switch below stops it through the one stop
    // body, exactly as a Ctrl+Tab over it would.
    if (app.audition_sequence.phase != Phase::Idle) return;
    const char home  = app.active_tab_view;
    const char other = (home == 'A') ? 'B' : 'A';
    // THE OTHER TAB'S PLAYHEAD, read through its own ViewState and clamped
    // exactly as switch_active_tab_view_to will clamp it on entry
    // (clamp_playhead_to_live_domain reads the live domain, which both tabs
    // share), so the verdict here is the verdict the launch would reach
    // there. The active tab's is the live cursor.
    const ViewState& other_tab = (other == 'A') ? app.tab_a : app.tab_b;
    const int64_t other_playhead = clamp_playhead_to_live_domain(
        other_tab.playhead_cursor_sample, app, audio);
    // BOTH TABS ARE GATED BEFORE THE FIRST SWITCH: an act that could only
    // half-run must not move the user off his tab and then fall silent.
    if (!tab_launch_ready(app.playhead_cursor_sample)) return;
    if (!tab_launch_ready(other_playhead)) return;
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
    // above first: launch_phase re-arms the sequence only on success, and a
    // refusal must leave the act ENDED — a due rest left standing would be
    // re-tried on every tick from here on. (The launch body's own head clear
    // would cover the success path, but not the arm the refusal returns
    // before.)
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
    if (!playback_lifecycle.launch_bounded_audition(start,
                                                    audition_span_frames())) {
        return false;
    }
    // THE PLAY HALF'S NON-IDLE WRITE, strictly after the launch body returned
    // true (its head cleared the sequence; this re-arms it for the play now
    // live). waiting = false is the whole difference from arm_rest's write:
    // the phase named here is SOUNDING, not owed.
    app.audition_sequence.phase         = phase;
    app.audition_sequence.home_tab      = home_tab;
    app.audition_sequence.waiting       = false;
    app.audition_sequence.launch_due_ms = 0;
    return true;
}
