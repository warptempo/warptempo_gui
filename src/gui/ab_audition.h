#pragma once

#include "active_views.h"
#include "app_state.h"
#include "audio.h"
#include "notifications.h"
#include "playback.h"
#include "playback_lifecycle.h"
#include "target_render.h"

#include <cstdint>

// The `c` command lives on GuiInputHandler and is reached through the
// back-pointer below: input_handler.h includes this header, so the include
// cannot run both ways (the same shape GuiPrompt::input and
// GuiRenderPlayer::input take).
struct GuiInputHandler;

// THE A/B AUDITION (architect 2026-08-26) — the operations cluster for the one
// transport act that spans several plays. The model, the phases and the
// complete edge inventory of its state are at GuiAuditionSequence
// (app_state.h); this struct owns the two things that move it: the PRESS-TIME
// ACT (start — Shift+Space, and the play button's shift-click / long press
// through the same chord), the ADVANCE (advance_after_natural_end — the
// tick's natural-end branch hands it the phase that just ended, and it arms
// the REST before the next play) and the FIRE (fire_if_due — the run loop's
// deadline tick, which ends that rest and launches). The ONE PLAY
// each phase makes is GuiPlaybackLifecycle::launch_bounded_audition, the tab
// switch between plays is GuiActiveViews::switch_active_tab_view_to (the
// ordinary Ctrl+Tab act, selection clear and synchronous rebuild included),
// and the preview readiness gate is GuiTargetRender::preview_ready — nothing
// here plays, switches or renders on its own. NO AUDIO CHANGES: each play is
// an ordinary session over the buffer the view already plays, at the level it
// already plays it; no new audio path, no render.
//
// WHAT THE ACT DOES, in order: (0) run the `c` command on the tab the press
// found; (1) switch to the OTHER tab and run `c` there; (2) play kAuditionMs
// from that tab's resting playhead, twice, resting kAuditionPairGapMs between
// the two; (3) switch back to the tab it started on, run `c` there again, and
// rest kAuditionSwitchGapMs; (4) play the same span from THAT tab's resting
// playhead, twice, resting the pair gap between them. Then it is over
// and the user adjusts on the
// tab he started from. THE RESTING PLAYHEAD NEVER MOVES BY THE ACT'S OWN
// DOING: a play drives the scanner and a stop moves no cursor (the
// lifecycle's doctrine), the tick's natural-end branch parks nothing, and each
// switch restores the entering tab's own band — so after the whole act both
// tabs' playheads are exactly where they were, and each pair of plays is
// identical by construction. The `c` each half opens with is the one thing
// that can write a cursor here, and only as `c` itself writes one: it lands
// the playhead on the tab's FOCUSED marker. After a switch that land is a
// provable no-op (the switch clears the selection and the coincidence
// auto-select re-acquires only a marker already under the cursor), and at the
// press the cursor already rests on the focus — every focus-changing route
// lands it — so in practice both pairs still start where the user left them.
//
// THE WORKING ZOOM OPENS EACH HALF (architect 2026-08-28, from his plastic
// pass): apply_working_zoom runs the `c` command — the whole command through
// its one owner, GuiInputHandler::run_center_command, never a second zoom
// recipe — at the press on the tab the press found, on the other tab the
// moment the act switches to it, and on the starting tab the moment it
// switches back. IT RUNS IN THE SWITCH'S OWN FRAME, immediately after the
// switch body and before the caller returns, so the tab flip and the zoom
// reach the compositor as one paint and the user sees no flicker: both are
// GUI-thread writes inside one run-loop iteration, and each of `c`'s viewport
// writes takes its own synchronous rebuild through kick_waveform_sync, the
// last one deciding the plate that frame. Each half is therefore read at the
// working zoom, centered, whatever level the tab was left at.
//
// ITS ORDERING IS LOAD-BEARING: `c` can reach land_playhead_on_marker, which
// CLEARS THE SEQUENCE unconditionally (a land is a movement, the inventory at
// GuiAuditionSequence), so every one of the three calls sits in a window where
// the sequence is already Idle — the press-time pair before the first launch
// (the refusal guard has returned by then), and the switch-back call after the
// switch and BEFORE arm_rest, which is the same reason the switch itself must
// precede the arm. A call placed after an arm would wipe the rest it just
// armed and silently end the act.
//
// SEQUENCING IS PACED AND TAKES NO TIMER OF ITS OWN: the audio thread ends a
// play, the tick observes it (the same natural-end branch every audition ends
// in), takes the one stop body, and calls the advance, which switches if the
// phase asks — synchronously on the GUI thread, so the tab flips at once —
// and then ARMS THE REST before the next play (kAuditionPairGapMs inside a
// pair, kAuditionSwitchGapMs across the switch; both at app_state.h, with the
// hand pacing they transcribe). The rest's deadline is sampled by fire_if_due
// on the run loop's existing deadline tick — the one the key-repeat and touch
// disambiguation deadlines already ride — so no timer is added and the
// granularity is that tick's (up to one timer period beyond each rest's own
// milliseconds). The scanner paints during each
// play exactly as it does under Space, and during a rest nothing plays at all —
// but THE ACT IS ONE TRANSPORT SESSION FROM ITS FIRST PLAY TO ITS LAST
// (architect 2026-08-26), so a rest is transport-LIVE where the user can see or
// press it: the play/stop button wears its STOP face for the act's whole
// duration and bare Space is the act's stop throughout (the fork at
// GuiPlaybackLifecycle::toggle_playback, the face at
// redesign_button_glyph_swapped, both reading `phase != Idle` beside the
// scanner bit). The alternative would flip the glyph to Play and back three
// times per act, and the transport row must never lie about live state.
// AND SHIFT+SPACE IS THE ACT'S STOP TOO (architect 2026-08-31): the chord is a
// TOGGLE like the transport it rides — start() routes a press that finds a
// standing sequence into that same one stop body instead of refusing, so the
// button's shift-click and long press agree with the STOP face it already
// wears, and the "An audition is already running" card retires with the arm.
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
// already acting, so no return to the starting tab. A REST IS INTERRUPTED
// EXACTLY AS A PLAY IS: no clearing owner tests for live playback on the way
// in (the stop body's clear sits ahead of its own nothing-to-do guard, the
// launch body's at its head), so a stop, a Space or a scrub that lands in one
// of the rests ends the act just as it would mid-play — a Space landing in a
// rest reaching the stop body rather than the launch body since the fork became
// transport-live, which is the same end by the same owner. Every path is a
// clearing owner listed at GuiAuditionSequence, whose inventory is the one
// authoritative copy; the class that matters here is that A PLAYHEAD MOVEMENT
// IS ONE OF THEM (the trim overlay's hide rule's own two movement owners),
// which is what makes "the resting playhead cannot move under a standing act"
// structural rather than a list of routes — while a TRANSLATION and a RESTORE
// are not, which is what lets the act's own two tab switches run inside it.
// Bare Esc is NOT one of them — its five bindings are closed.
//
// THE RUNNING CASE IS NOT A REFUSAL (architect 2026-08-31): a Shift+Space that
// finds `phase != Idle` STOPS the act through the one stop body and launches
// nothing — the same body bare Space's own fork reaches for the same case, so
// the press is a caller of clearing owner (1) and not a new owner. A REST
// COUNTS AS RUNNING, `phase` being the act's one running bit in both halves, so
// the test is the same `phase != Idle` it always was (and the same term the
// transport fork and the play/stop glyph read); a held key meets the same
// answer, though Space is one-shot in repeat_eligible and no repeat arrives.
// No card: the silence stops, which is the answer, and the "An audition is
// already running" sentence retired with the arm that said it.
//
// REFUSALS, all at the press, all at start, and EACH ONE CARDED since
// 2026-08-30 (the strictness ruling: the whole act is silence and a camera
// move, so a refusal that said nothing was indistinguishable from one):
// A DEAD OR ABSENT DEVICE, asked once for the pair ahead of everything below
// it because nothing will sound on either tab (the launch body's own literal,
// kPlaybackDeviceUnavailableCard);
// in TARGET VIEW, a preview not ready (GuiTargetRender::preview_ready) or
// EITHER tab's playhead outside the bound buffer's playable range — the other
// tab's read through its own ViewState, clamped exactly as the switch would
// clamp it, WITHOUT switching; in SOURCE VIEW, either playhead at or past the
// two-frame remainder gate — the last two answering in ONE sentence, the act
// being one act over a pair. THE ORDER IS THE PROMISE: every reason the launch
// body could refuse for is asked here, before the first `c` and the switch, so
// a refused act moves nothing. The `h` view consumes the chord at its allowlist
// (playback is removed from the view whole); loading-or-absent audio consumes
// it at on_key's head. A read-only tab admits it (it authors nothing). SINCE
// THE SAME EVENING the same press-time gates are composed for the PLAY
// button's face by ab_audition_preflight_ok (ab_audition.cpp, beside start)
// — the face's shift-twin term under the twin rule, so a runnable audition
// keeps a refused plain Play lit. Undo:
// none. Damage: the tab switch's and the scanner's, nothing new.
struct GuiAbAudition {
    AppState&             app;
    const GuiAudio&       audio;
    GuiPlayback&          playback;
    GuiPlaybackLifecycle& playback_lifecycle;
    GuiActiveViews&       active_views;
    GuiTargetRender&      target_render;
    // THE ACT'S TWO PRESS-TIME REFUSALS SPEAK (2026-08-30, the strictness
    // ruling): a dead or absent device, and a tab with nothing to play from.
    // Both are start()'s, and start() is the only body here that refuses a
    // PRESS — the advance and the fire answer no key. (The third refusal, a
    // sequence already running, stopped being one on 2026-08-31: that press
    // takes the stop body and says nothing.)
    GuiNotifications&     notifications;
    // Back-pointer to the input handler, wired in main.cpp after both are
    // built (the handler holds this cluster by reference, so it cannot be a
    // constructor argument) — the prompt's own shape (GuiPrompt::input). Its
    // one reader is apply_working_zoom.
    GuiInputHandler*      input = nullptr;

    GuiAbAudition(AppState&             app_,
                  const GuiAudio&       audio_,
                  GuiPlayback&          playback_,
                  GuiPlaybackLifecycle& playback_lifecycle_,
                  GuiActiveViews&       active_views_,
                  GuiTargetRender&      target_render_,
                  GuiNotifications&     notifications_)
        : app(app_),
          audio(audio_),
          playback(playback_),
          playback_lifecycle(playback_lifecycle_),
          active_views(active_views_),
          target_render(target_render_),
          notifications(notifications_) {}

    // THE PRESS-TIME ACT (the refusals at the head comment): gate both tabs,
    // run `c` on this tab, switch to the other one, run `c` there, launch the
    // first play — OR, where the press finds the act already standing, STOP it
    // through the one stop body and launch nothing (the toggle, 2026-08-31).
    // ONE CALLER: on_key's Shift+Space arm (input_handler.cpp), which the play
    // button's shift-click and long press also reach.
    void start();

    // THE ADVANCE: `ended` is the sequence state the tick's natural-end branch
    // read BEFORE taking the one stop body (which clears it). Idle means the
    // play that ended was not the act's — nothing to do. Otherwise ARM THE
    // REST before the next phase (it launches nothing itself), switching back
    // to the starting tab ahead of the switch rest; HomeSecond ending is the
    // act's own end. THE SWITCH RUNS FIRST AND THE ARM AFTER IT, because the
    // switch takes the one stop body and that body clears the sequence — the
    // same ordering the launch used to need, and the same ordering the
    // apply_working_zoom that follows the switch needs for its own reason (the
    // head comment). ONE CALLER: that branch (main.cpp), the sequence's one
    // advance site.
    void advance_after_natural_end(const GuiAuditionSequence& ended);

    // THE REST'S DEADLINE SAMPLER: launch the phase the advance armed once its
    // rest has elapsed. A no-op unless a rest stands and is due, so an idle
    // tick costs one enum compare — the clock is read only past that. It reads
    // monotonic_ms() itself rather than taking a `now`, so the caller need not
    // sample a clock on every tick for a state that is almost never standing.
    // The rest is cleared BEFORE the launch is attempted, so a launch refusal
    // ends the act (the interrupt rule's own answer) instead of leaving a due
    // rest to retry on every tick; a success re-arms the sequence for the play
    // now live, through launch_phase as every launch does.
    // ONE CALLER: main.cpp's on_tick, above its playing-only guard — a rest has
    // nothing playing, so it must be sampled ahead of that return — and beside
    // the platform's own deadline samplers on the same timerfd expiry.
    void fire_if_due();

private:
    // kAuditionMs at the active domain's sample rate, std::nearbyint — the one
    // conversion site (the target buffer is bound at the source's rate).
    int64_t audition_span_frames() const;
    // Press-time readiness of ONE tab's launch frame: the preview gate in
    // target view, then the launch body's own playable predicate on the frame.
    bool tab_launch_ready(int64_t frame) const;
    // Launch one phase's play from the active tab's resting playhead; on
    // success the sequence is armed with `phase`, waiting = false (the play
    // half's non-Idle writer). Returns whether it launched; a refusal leaves
    // the act ended (the launch body cleared it) and the tab where it is.
    bool launch_phase(GuiAuditionSequence::Phase phase, char home_tab);
    // Arm the REST that precedes `phase`: the sequence takes that phase with
    // waiting = true and a deadline `gap_ms` out (the rest half's non-Idle
    // writer). Called only from the advance, and only after any tab switch
    // that transition owes — that switch's stop body would clear what this
    // wrote.
    void arm_rest(GuiAuditionSequence::Phase phase, char home_tab, int gap_ms);
    // THE `c` COMMAND ON THE ACTIVE TAB (architect 2026-08-28; the rule, the
    // same-frame claim and the ordering it owes the sequence are at the head
    // comment). It routes through the command's one owner and adds nothing of
    // its own — no zoom arithmetic, no camera write — so the three call sites
    // land exactly what the key lands. Silent if the back-pointer is unwired.
    void apply_working_zoom();
};
