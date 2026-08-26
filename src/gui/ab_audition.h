#pragma once

#include "active_views.h"
#include "app_state.h"
#include "audio.h"
#include "playback.h"
#include "playback_lifecycle.h"
#include "target_render.h"

#include <cstdint>

// THE A/B AUDITION (architect 2026-08-26) — the operations cluster for the one
// transport act that spans several plays. The model, the phases and the
// complete edge inventory of its state are at GuiAuditionSequence
// (app_state.h); this struct owns the two things that move it: the PRESS-TIME
// ACT (start — Shift+Space, and the play button's shift-click / long press
// through the same chord) and the ADVANCE (advance_after_natural_end — the
// tick's natural-end branch hands it the phase that just ended). The ONE PLAY
// each phase makes is GuiPlaybackLifecycle::launch_bounded_audition, the tab
// switch between plays is GuiActiveViews::switch_active_tab_view_to (the
// ordinary Ctrl+Tab act, selection clear and synchronous rebuild included),
// and the preview readiness gate is GuiTargetRender::preview_ready — nothing
// here plays, switches or renders on its own. NO AUDIO CHANGES: each play is
// an ordinary session over the buffer the view already plays, at the level it
// already plays it; no new audio path, no render.
//
// WHAT THE ACT DOES, in order: (1) switch to the OTHER tab; (2) play
// kAuditionMs from that tab's resting playhead, twice in immediate succession;
// (3) switch back to the tab it started on; (4) play the same span from THAT
// tab's resting playhead, twice. Then it is over and the user adjusts on the
// tab he started from. THE RESTING PLAYHEAD NEVER MOVES: a play drives the
// scanner and a stop moves no cursor (the lifecycle's doctrine), the tick's
// natural-end branch parks nothing, and each switch restores the entering
// tab's own band — so after the whole act both tabs' playheads are exactly
// where they were, and each pair of plays is identical by construction.
//
// SEQUENCING has no timer and no gap: the audio thread ends a play, the tick
// observes it (the same natural-end branch every audition ends in), takes the
// one stop body, and calls the advance, which switches if the phase asks and
// launches the next play synchronously on the GUI thread. The scanner paints
// during each play exactly as it does under Space.
//
// THE SWITCH IS switch_active_tab_view_to ALONE, NOT Ctrl+Tab's trigger()
// tail. Ctrl+Tab re-dispatches the preview for the entering tab's trim; here
// the bound buffer IS what the act plays and BOTH tabs' playheads were
// validated against it before the first switch — the target domain is
// full-target frames on either tab, so the one bound preview plays the right
// audio for both, and a re-dispatch mid-act would take the buffer away under
// the very play about to launch (asynchronously where the two trims differ).
// The recorded cost: an act INTERRUPTED on the other tab leaves the preview
// bound as rendered for the starting tab's trim window until the user's next
// mutation or Ctrl+Tab triggers, exactly as an in-flight re-render leaves it.
//
// INTERRUPTION ends the whole act and leaves the tab where it is — the user is
// already acting, so no return to the starting tab: every path is a clearing
// owner listed at GuiAuditionSequence (the one stop body ahead of its guard,
// the one launch body's head, the three hand-spelled target_render stops).
// Bare Esc is NOT one of them — its five bindings are closed.
//
// REFUSALS, all silent, all at the press, all at start: a sequence already
// running (a second Shift+Space is a consumed no-op; a held key meets the same
// answer, though Space is one-shot in repeat_eligible and no repeat arrives);
// in TARGET VIEW, a preview not ready (GuiTargetRender::preview_ready) or
// EITHER tab's playhead outside the bound buffer's playable range — the other
// tab's read through its own ViewState, clamped exactly as the switch would
// clamp it, WITHOUT switching; in SOURCE VIEW, either playhead at or past the
// two-frame remainder gate. The `h` view consumes the chord at its allowlist
// (playback is removed from the view whole); loading-or-absent audio consumes
// it at on_key's head. A read-only tab admits it (it authors nothing). Undo:
// none. Damage: the tab switch's and the scanner's, nothing new.
struct GuiAbAudition {
    AppState&             app;
    const GuiAudio&       audio;
    GuiPlayback&          playback;
    GuiPlaybackLifecycle& playback_lifecycle;
    GuiActiveViews&       active_views;
    GuiTargetRender&      target_render;

    GuiAbAudition(AppState&             app_,
                  const GuiAudio&       audio_,
                  GuiPlayback&          playback_,
                  GuiPlaybackLifecycle& playback_lifecycle_,
                  GuiActiveViews&       active_views_,
                  GuiTargetRender&      target_render_)
        : app(app_),
          audio(audio_),
          playback(playback_),
          playback_lifecycle(playback_lifecycle_),
          active_views(active_views_),
          target_render(target_render_) {}

    // THE PRESS-TIME ACT (the refusals at the head comment): gate both tabs,
    // switch to the other one, launch the first play. ONE CALLER: on_key's
    // Shift+Space arm (input_handler.cpp), which the play button's shift-click
    // and long press also reach.
    void start();

    // THE ADVANCE: `ended` is the sequence state the tick's natural-end branch
    // read BEFORE taking the one stop body (which clears it). Idle means the
    // play that ended was not the act's — nothing to do. Otherwise launch the
    // next phase, switching back to the starting tab ahead of its first play;
    // HomeSecond ending is the act's own end. ONE CALLER: that branch
    // (main.cpp), the sequence's one advance site.
    void advance_after_natural_end(const GuiAuditionSequence& ended);

private:
    // kAuditionMs at the active domain's sample rate, std::nearbyint — the one
    // conversion site (the target buffer is bound at the source's rate).
    int64_t audition_span_frames() const;
    // Press-time readiness of ONE tab's launch frame: the preview gate in
    // target view, then the launch body's own playable predicate on the frame.
    bool tab_launch_ready(int64_t frame) const;
    // Launch one phase's play from the active tab's resting playhead; on
    // success the sequence is armed with `phase` (the ONE non-Idle writer).
    // Returns whether it launched; a refusal leaves the act ended (the launch
    // body cleared it) and the tab where it is.
    bool launch_phase(GuiAuditionSequence::Phase phase, char home_tab);
};
