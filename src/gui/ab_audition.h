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
// switch between plays is Ctrl+Tab's whole road —
// GuiActiveViews::switch_active_tab_view_to (selection clear and synchronous
// rebuild included) and then GuiTargetRender::trigger() — and the preview
// readiness gate is GuiTargetRender::preview_ready — nothing here plays,
// switches or renders on its own. NO NEW AUDIO: each play is an ordinary
// session over the preview the tab plays after a Ctrl+Tab, at the level it
// already plays it; no new audio path. The one render the act causes is the
// switch's own re-dispatch (2026-10-18), Ctrl+Tab's, which in the common case
// is a render-cache hit (each tab's preview cached the first time it played)
// and otherwise a synthesis the next play waits for.
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
// pass): apply_working_zoom runs the `c` key's act — the whole command through
// its one owner, GuiInputHandler::run_center_command, reached through the
// key's own GuiInputHandler::run_center_key_command, never a second zoom
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
// AND EACH CENTRING ARMS THE HOLD POSTURE AS `c` DOES (architect 2026-09-29,
// superseding 2026-09-23's "the act ignores the hold posture";
// AppState::camera_hold): the key's act arms the bit behind its centring, and
// nothing in the act saves or writes it back. The plays and the stops between
// the centrings move no camera and keep it; each switch puts it out and
// derives it on arrival (hold_derived_on_arrival), and the `c` behind the
// switch arms it over that answer. So the act ends with the hold standing on
// the home tab, centred on its playhead at the working zoom, whether or not
// it stood at the press; an act a key ends on the other tab leaves it armed
// there, on the centring the act made, unless the key itself moved the
// playhead or the camera.
//
// ITS ORDERING IS LOAD-BEARING: `c` STOPS PLAYBACK BEFORE IT CENTRES, on both
// its arms (architect 2026-09-29), through the one stop body, which CLEARS THE
// SEQUENCE ahead of its own guard and would silence a play of the act's own;
// and with a focus it reaches land_playhead_on_marker, which clears the
// sequence too (a land is a movement, the inventory at GuiAuditionSequence).
// So every one of the three calls sits in a window where the sequence is
// already Idle and none of the act's plays sounds — the press-time pair
// before the first play's arm (the refusal guard has returned by then; the
// first call's stop ends a play the press found, which the switch behind it
// would end anyway), and the switch-back call after the tick's stop and the
// switch and BEFORE arm_rest. SINCE 2026-10-18 THE RULE IS ONE AT BOTH ENDS:
// start() no longer launches its first play but ARMS it (a rest of zero),
// so the second `c` at the press precedes an arm exactly as the switch-back
// call does, and each switch's trigger() — whose target-view path clears the
// sequence unconditionally — precedes it too, which is the same reason the
// switch itself must precede the arm. A call placed after an arm — or after
// launch_phase's write — would wipe the phase just written, or stop the play
// just launched, and silently end the act.
//
// SEQUENCING IS PACED AND TAKES NO TIMER OF ITS OWN: the audio thread ends a
// play, the tick observes it (the same natural-end branch every audition ends
// in), takes the one stop body, and calls the advance, which switches if the
// phase asks — synchronously on the GUI thread, so the tab flips at once —
// and then ARMS THE REST before the next play (kAuditionPairGapMs inside a
// pair, kAuditionSwitchGapMs across the switch; both at app_state.h, with the
// hand pacing they transcribe). The rest's deadline is sampled by fire_if_due
// on the run loop's existing deadline tick — the one the key-repeat and touch
// region-hold deadlines already ride — so no timer is added and the
// granularity is that tick's (up to one timer period beyond each rest's own
// milliseconds; the first play's rest of zero is that one period). A rest
// after a switch may also outlast its milliseconds while the entering tab's
// preview updates (fire_if_due, 2026-10-18). The scanner paints during each
// play exactly as it does under Space, and during a rest nothing plays at all
// (a rest is not paint-idle: the tick damages the clock cell every tick while
// a rest stands, main.cpp's tick body says why, 2026-10-18) —
// but THE ACT IS ONE TRANSPORT SESSION FROM ITS FIRST PLAY TO ITS LAST
// (architect 2026-08-26; since 2026-10-18 from the press, whose first play
// is armed behind that rest of zero), so a rest is transport-LIVE where the
// user can see or press it: the play/stop button wears its STOP face for the
// act's whole duration and bare Space is the act's stop throughout (the fork
// at GuiPlaybackLifecycle::toggle_playback, the face at
// redesign_button_glyph_swapped, both reading `phase != Idle` beside the
// scanner bit). The alternative would flip the glyph to Play and back three
// times per act, and the transport row must never lie about live state.
// AND SHIFT+SPACE IS THE ACT'S STOP TOO (architect 2026-08-31): the chord is a
// TOGGLE like the transport it rides — start() routes a press that finds a
// standing sequence into that same one stop body instead of refusing, so the
// button's shift-click and long press agree with the STOP face it already
// wears, and the "An audition is already running" card retires with the arm.
//
// THE SWITCH IS CTRL+TAB'S WHOLE ROAD (architect 2026-10-18, on his saved
// state: each tab played alone, the act carded): switch_active_tab_view_to
// and then GuiTargetRender::trigger(), at both of the act's switches, so THE
// PREVIEW FOLLOWS THE TAB as it does after a Ctrl+Tab and each tab plays its
// OWN trim's render. The two tabs share both marker stores, so the target
// audio is one and only the trim differs per tab — but the preview EMBODIES
// the trim (its domain is the trim's crop), so one bound preview cannot play
// a tab whose playhead lies outside the other tab's window. The order at both
// sites is the switch, the trigger, `c`, then the arm (the ordering rule
// above). Where the dispatch goes asynchronous the next play WAITS for it:
// a due rest stands while the preview updates (fire_if_due), so the first
// play after each switch launches against the entering tab's own buffer.
// The trigger is called unconditionally, as Ctrl+Tab calls it, and carries
// Ctrl+Tab's costs with it: in source view it returns after the dirty bit,
// and in target view it cancels a running archival render, as a Ctrl+Tab
// would. An act INTERRUPTED on the other tab therefore leaves that tab's own
// preview bound, exactly as a Ctrl+Tab would have.
//
// THE ACT IS HANDS-OFF (architect 2026-10-28): any press on the window ends
// it ahead of the press's own dispatch — a bound chord, a pointer or touch
// press, the wheel, a touch pan, pinch or region hold — bare Space and
// Shift+Space (and the PLAY key whose lift is one of them) excepted, their
// own dispatch forking on the act to end it (the entries at
// GuiInputHandler::end_audition_for_press).
// INTERRUPTION ends the whole act and leaves the tab where it is — the user is
// already acting, so no return to the starting tab. A REST IS INTERRUPTED
// EXACTLY AS A PLAY IS: no clearing owner tests for live playback on the way
// in (the stop body's clear sits ahead of its own nothing-to-do guard, the
// user launch's ahead of its entry's delegation — launch_playback_from, since
// 2026-09-01), so a stop, a Space or a scrub that lands in one
// of the rests ends the act just as it would mid-play — a Space landing in a
// rest reaching the stop body rather than the launch body since the fork became
// transport-live, which is the same end by the same owner. Every path is a
// clearing owner listed at GuiAuditionSequence, whose inventory is the one
// authoritative copy; the class that matters here is that A PLAYHEAD MOVEMENT
// IS ONE OF THEM (the two movement owners, the rule at
// Viewport::move_playhead_to),
// which is what makes "the resting playhead cannot move under a standing act"
// structural rather than a list of routes — while a TRANSLATION and a RESTORE
// are not, which is what lets the act's own two tab switches run inside it.
// Bare Esc ends it AS A PRESS (2026-10-28, the hands-off rule), not as an Esc
// binding — its eight bindings are closed and unchanged (the card dismissal
// joined them on 2026-08-31; the enumeration is input_handler.cpp's).
// AND A DEVICE LOST MID-ACT IS AN INTERRUPT LIKE ANY OTHER (architect
// 2026-09-04): fire_if_due asks GuiPlayback::device_unavailable on every tick
// the act stands, in a play as in a rest, and ends it through that same one
// stop body with the launch gate's own card. The tick is therefore a CALLER of
// clearing owner (1) rather than a fifth owner, and the tab stays where the act
// had it, exactly as every other interruption leaves it. Until that day the
// tick had no device fork at all and an AAudio disconnect read as the play's
// natural end, so the act advanced one rest and the rest's launch either
// reopened the device or carded at the belt; the ruling replaces that with one
// end and one sentence.
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
// kPlaybackDeviceUnavailableCard) — and since 2026-09-02 the question REOPENS
// a dead AAudio stream first (GuiPlayback::ensure_device_available_for_play,
// the reopen at the press), so it cards only a device that stayed dead after
// the reopen or never came up at all;
// in TARGET VIEW, a preview not ready (GuiTargetRender::preview_ready) or the
// ACTIVE tab's playhead outside the bound buffer's playable range (the launch
// body's own predicate — at the press the bound buffer is this tab's trim's
// render), or the OTHER tab's playhead outside the playable range of the
// domain ITS OWN trim's preview will have (2026-10-18:
// GuiTargetRender::preview_domain_for, the verdict owner the switch's
// dispatch stamps, so the answer is the one the launch reaches after the
// switch), the other tab's read through its own ViewState, clamped exactly as
// the switch would clamp it, WITHOUT switching; in SOURCE VIEW, either
// playhead at or past the two-frame remainder gate against the song end —
// the last two answering in ONE sentence, the act being one act over a pair.
// THE ORDER IS THE PROMISE: every reason the launch body could refuse for is
// asked here, before the first `c` and the switch, so a refused act moves
// nothing, and an admitted act plays unless a render fails (the failure is
// carded on the preview's own road, and the launch it refuses ends the act).
// The `h` view consumes the chord at its allowlist
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
    // run `c` on this tab, switch to the other one (Ctrl+Tab's whole road,
    // trigger() included), run `c` there, ARM the first play behind a rest of
    // zero (2026-10-18; fire_if_due launches it once the entering tab's
    // preview is in hand) — OR, where the press finds the act already standing,
    // STOP it through the one stop body and launch nothing (the toggle,
    // 2026-08-31).
    // ONE CALLER: on_key's Shift+Space arm (input_handler.cpp), which the play
    // button's shift-click and long press also reach.
    void start();

    // THE ADVANCE: `ended` is the sequence state the tick's natural-end branch
    // read BEFORE taking the one stop body (which clears it). Idle means the
    // play that ended was not the act's — nothing to do. Otherwise ARM THE
    // REST before the next phase (it launches nothing itself), switching back
    // to the starting tab ahead of the switch rest — Ctrl+Tab's whole road,
    // trigger() included (2026-10-18); HomeSecond ending is the act's own
    // end. THE SWITCH RUNS FIRST AND THE ARM AFTER IT, because the switch
    // takes the one stop body and that body clears the sequence, as does the
    // trigger's target-view path — the same ordering start() keeps for the
    // first play's arm, and the same ordering the apply_working_zoom that
    // follows the switch needs for its own reason (the head comment). ONE
    // CALLER: that branch (main.cpp), the sequence's one advance site.
    void advance_after_natural_end(const GuiAuditionSequence& ended);

    // THE REST'S DEADLINE SAMPLER AND THE ACT'S DEVICE WATCH: launch the phase
    // the advance armed once its rest has elapsed, or end the whole act where
    // the device has gone away. A no-op unless the act stands, so an idle tick
    // still costs one enum compare — the device read and the clock are both
    // past that, and the device read comes first because it must catch a PLAY
    // as well as a rest (the arm and its reasoning are at the definition;
    // architect 2026-09-04). It reads
    // monotonic_ms() itself rather than taking a `now`, so the caller need not
    // sample a clock on every tick for a state that is almost never standing.
    // A DUE REST STANDS WHILE THE TARGET PREVIEW UPDATES (2026-10-18): the
    // play after a switch waits for the switch's own re-dispatch to land, and
    // the launch is then attempted once.
    // The rest is cleared BEFORE the launch is attempted, so a refusal ahead
    // of launch_phase's own write ends the act (the interrupt rule's own
    // answer) instead of leaving a due rest to retry on every tick;
    // launch_phase then writes the play's phase ahead of its launch and clears
    // it again on a refusal, as every launch of the act's does.
    // ONE CALLER: main.cpp's on_tick, above its playing-only guard — a rest has
    // nothing playing, so it must be sampled ahead of that return — and beside
    // the platform's own deadline samplers on the same timerfd expiry. That
    // placement is what lets the device arm end a stalled PLAY too, and it is
    // what keeps the tick's natural-end branch from advancing behind it: the
    // stop body this arm takes lowers the scanner flag, so the guard the call
    // sits above returns before that branch is reached and the act ends once.
    void fire_if_due();

private:
    // kAuditionMs at the active domain's sample rate, std::nearbyint — the one
    // conversion site (the target buffer is bound at the source's rate).
    int64_t audition_span_frames() const;
    // Press-time readiness of the ACTIVE tab's resting playhead: the preview
    // gate in target view, then the launch body's own playable predicate.
    bool active_tab_launch_ready() const;
    // Press-time readiness of the OTHER tab's playhead (the switch's clamp):
    // in target view against the domain its own trim's preview will have
    // (GuiTargetRender::preview_domain_for, 2026-10-18), in source view the
    // launch body's own predicate. The asymmetry is at the definition.
    bool other_tab_launch_ready() const;
    // Launch one phase's play from the active tab's resting playhead. THE
    // SEQUENCE IS WRITTEN WITH `phase`, waiting = false, BEFORE THE LAUNCH
    // (the play half's non-Idle writer; 2026-09-01 — it followed a true
    // return until then), so the launch body finds the act standing; called
    // only where the sequence is Idle — fire_if_due past its clear, its one
    // caller since start() arms its first play (2026-10-18). Returns whether it
    // launched; a refusal clears the sequence again, so the act is ended and
    // the tab stays where it is.
    bool launch_phase(GuiAuditionSequence::Phase phase, char home_tab);
    // Arm the REST that precedes `phase`: the sequence takes that phase with
    // waiting = true and a deadline `gap_ms` out (the rest half's non-Idle
    // writer). Called from the advance and, since 2026-10-18, from start()
    // for the first play (a gap of zero), and at both only after the tab
    // switch, its trigger() and the `c` that transition owes — the switch's
    // stop body, the trigger's target-view clear and `c`'s stop would each
    // clear what this wrote.
    void arm_rest(GuiAuditionSequence::Phase phase, char home_tab, int gap_ms);
    // THE `c` COMMAND ON THE ACTIVE TAB (architect 2026-08-28; the rule, the
    // same-frame claim and the ordering it owes the sequence are at the head
    // comment). It routes through the key's own act and adds nothing of its
    // own — no zoom arithmetic, no camera write, no hold write — so the three
    // call sites land exactly what the key lands, the hold posture armed
    // included (2026-09-29). Silent if the back-pointer is unwired.
    void apply_working_zoom();
};
