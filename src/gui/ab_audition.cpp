#include "ab_audition.h"

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
    // A SEQUENCE ALREADY RUNNING: consumed no-op — never a restart. A running
    // PLAIN audition is not a refusal: the switch below stops it through the
    // one stop body, exactly as a Ctrl+Tab over it would.
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
    // Step 1: the ordinary tab switch (the stop of any live audition, the
    // selection clear, the band swap, the coincidence auto-select and the
    // synchronous rebuild are all its own). Not Ctrl+Tab's trigger() tail —
    // the header says why.
    active_views.switch_active_tab_view_to(other);
    // Step 2's first play. A refusal here is unreachable in practice (the
    // frame was just validated against the same domain) and would end the act
    // on the other tab, the interrupt rule's own answer.
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
            launch_phase(Phase::OtherSecond, ended.home_tab);
            return;
        case Phase::OtherSecond:
            // Step 3: back to the starting tab, then step 4's first play.
            active_views.switch_active_tab_view_to(ended.home_tab);
            launch_phase(Phase::HomeFirst, ended.home_tab);
            return;
        case Phase::HomeFirst:
            launch_phase(Phase::HomeSecond, ended.home_tab);
            return;
        case Phase::HomeSecond:
            // The act's own end: the stop body the tick just took left the
            // sequence Idle, the tab is the starting one, and both playheads
            // are where they were. Nothing to launch.
            return;
    }
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
    // THE ONE NON-IDLE WRITE, strictly after the launch body returned true
    // (its head cleared the sequence; this re-arms it for the play now live).
    app.audition_sequence.phase    = phase;
    app.audition_sequence.home_tab = home_tab;
    return true;
}
