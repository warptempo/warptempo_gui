#include "playback_lifecycle.h"

#include "render_player.h"   // the stop body's fork publishes the head unit's state

#include <algorithm>
#include <cstdint>

using RenderPlayerTransport = AppState::RenderPlayer::Transport;

// THE LAUNCH'S POSITION REFUSAL IS SILENT AT BOTH ITS SITES (architect
// 2026-08-31, retiring the 2026-08-30 sentence "There is nothing left to play
// from here" and its shared constant): a benign one-dimensional refusal
// already at its state says nothing — the playhead rests visibly at the view's
// end and the Play button greys on that very predicate. The two sites (the one
// launch body's playable gate and toggle_playback's verdict-identical pre-sum
// twin) still refuse exactly as before; only the card is gone. The DEVICE
// refusal keeps its card: a dead device is the one fact the screen cannot show.

// Gesture-stop: called by any handler that will move the
// cursor (keys, button press, undo/redo, tab switch) — and, since 2026-07-30, by
// the two keyboard TRIM MUTATIONS, which stop without touching the cursor at all.
// WHICH KEYBOARD COMMANDS STOP is a ruling, stated once at the declaration (the
// keyboard stop rule, playback_lifecycle.h); this comment owns only the teardown.
// Stops the audio thread and DEACTIVATES the scanner. Once inactive the
// scanner's value fields are stale by contract — no consumer reads them — so
// nothing is snapped back.
// The cursor is not touched here — the caller is about to commit a new
// cursor position. The scanner's last-painted pixels must be invalidated
// here regardless of what the caller does next: the caller cares about
// the cursor, but the scanner has its own visible identity that this
// function is responsible for tearing down.
void GuiPlaybackLifecycle::stop_playback_if_playing() {
    // THE A/B AUDITION SEQUENCE ENDS HERE, AHEAD OF THE GUARD (architect
    // 2026-08-26): every caller of this body — a keyboard stop, a modal open,
    // a tab switch, the `h` entry, the S/T flip, a trim write, the scrub's stop
    // half, Space's stop edge, the tick's natural end — is an interrupt of the
    // four-play act by construction, so the clear lives INSIDE the one stop
    // body and no caller can forget it. Ahead of the guard because a stop that
    // finds nothing playing must still end an act that was between plays in
    // the sub-tick window (the natural end observed by the tick's own branch,
    // which reads the phase before calling here and advances after). The
    // complete edge inventory is at GuiAuditionSequence (app_state.h).
    clear_audition_sequence(app);
    // THE RENDER PLAYER'S FORK, INSIDE THE ONE STOP BODY (2026-08-28): the
    // player's transport is a session over ITS OWN buffer with no scanner and
    // no waveform picture, so its "a session stood" state is
    // `render_player.transport` in its LIVE value (the scanner flag's mirror)
    // and its teardown is that state moved to PAUSED plus the modal row
    // damaged — the play/pause glyph, the clock and the scrub all read it.
    // PAUSED IS WHAT THIS BODY MEANS, AND THIS FORK IS ITS ONE WRITER: a
    // sounding transport that stops is parked where it stopped, at frame 0 as
    // anywhere else, so the next Play resumes THAT item. The callers that mean
    // IDLE instead — stop(), the natural end's rest and close() — write it
    // after this returns, and open()'s own reset never reaches this fork (the
    // state's whole writer set is at the field, app_state.h).
    // The FENCE is the same: playback.stop() proves the callback
    // is out of the item's buffer before a rebind or a free, exactly as it
    // proves it out of the source's. Every player stop — the pause, the
    // natural end, the close, the rebind ahead of the next item — comes here
    // and nowhere else, which is what keeps the keyboard stop rule and the
    // fence-before-rebind ordering one body.
    if (app.render_player.active) {
        if (!playback.is_playing() &&
            app.render_player.transport != RenderPlayerTransport::Live)
            return;
        playback.stop();
        app.render_player.transport = RenderPlayerTransport::Paused;
        viewport.invalidate_modal_dialog_area();
        // THE HEAD UNIT'S "PAUSED" IS PUBLISHED HERE AND NOWHERE ELSE
        // (2026-08-28): this fork is the one place every player stop passes
        // — the pause, the natural end, the dead device, the close, the
        // rebind ahead of the next item — so the push lives inside it and no
        // caller can forget it. The callers that go on to a "playing" or an
        // "inactive" push (play_wav's tail, the close) supersede this one a
        // moment later, two binder calls where one would do; accepted, the
        // alternative being one push per caller that the next caller
        // forgets. The resume point the push reads is the caller's to have
        // written BEFORE calling here (toggle_pause and the tick's dead-device
        // arm do; the natural end writes its 0 first for the same reason).
        if (render_player) render_player->publish_media_state();
        return;
    }
    if (!playback.is_playing() && !app.playhead_scanner_active) return;
    playback.stop();
    app.playhead_scanner_active = false;
    // FULL WAVEFORM-AREA DAMAGE (architect 2026-07-30, replacing the narrow
    // scanner/cursor column pair this used to compute on the LIVE viewport).
    // The playheads' pixels are PLATE-registered, so live-basis columns could
    // leave the scanner's last line un-erased through an async publish window;
    // GuiPlaybackLifecycle sees no GuiPaintHandler, so the site takes the
    // widening shape instead of the honest-basis one — a full-area invalidate
    // cannot ride the wrong epoch, and a stop is a discrete once-per-audition
    // event. The rule and the per-site shape table live at playhead_pixel_x
    // (app_state.h).
    viewport.invalidate_waveform_area();
    viewport.invalidate_clock_area();
    app.follow_overridden_for_session = false;
}

// The one owner of the modal-open stop. See the declaration for the decision
// table (which surfaces stop, and why the top-strip flag editor does not) and
// for the refusal-gating rule every caller observes. Pure delegation by design:
// a modal open needs exactly the gesture stop's teardown and nothing more, so
// this stays a name for a rule rather than a second mechanism.
void GuiPlaybackLifecycle::stop_playback_for_modal_open() {
    stop_playback_if_playing();
}

// Space-bar: start/stop playback. Playback runs from the cursor to the active
// view's end — the SONG's end in source view, the preview buffer's (which is the
// trim window's) in target view; the split and its reasoning are at the launch
// body. Pressing space with the cursor at or past that end is a SILENT no-op
// (architect 2026-08-31; the rule is at the file head).
// THE CURSOR IS ALWAYS THE START
// (architect 2026-07-30): the region left-bound launch that used to divert
// Space's play edge to scrub_launch_at is deleted with the SPAN FORM, so every
// Space — span resting or not — launches from here. Space-to-stop just
// DEACTIVATES the scanner (its value fields go stale by contract — no
// snap-back, no separate stash; the cursor is the launch point by
// definition).
//
// The play edge computes the launch position — cursor + launch_offset, the
// offset non-zero only for the target-view lead-in Space auditions — and delegates
// to launch_playback_from, the VIEW-END ENTRY the scrub launch also rides,
// which adds the end and hands both bounds to the one launch body
// (launch_playback_window); the validation, scanner seed, follow behavior,
// and play() all live there. What stays HERE is the cursor-relative
// arithmetic and its overflow-ordered pre-sum gate (a cursor-vs-shifted-bound
// check that must run before the sum exists — see below).
void GuiPlaybackLifecycle::toggle_playback(int64_t launch_offset) {
    // THE PLAY/STOP FORK, and A REST OF THE A/B AUDITION IS ON THE STOP SIDE OF
    // IT (architect 2026-08-26): the act is ONE TRANSPORT SESSION from its first
    // play to its last, so bare Space is its stop throughout, rests included. A
    // rest is transport-live for this fork's purposes because THE FACE SAYS SO
    // — the play/stop button wears the stop glyph for the act's whole duration
    // (redesign_button_glyph_swapped reads the same `phase != Idle` this arm
    // does), and the transport row must never lie about live state. Without
    // this term a press in one of the few rest frames would start a PLAIN
    // audition instead, and the glyph would flip to Play and back three times
    // per act to stay honest about it.
    //
    // THE ORDINARY TERM IS NOT SHARED, though, and that predates this act: the
    // face's is playhead_scanner_active (the GUI-side mirror, cleared by the
    // run-loop tick on natural end — main.cpp) and this arm's is
    // playback.is_playing() (the audio callback's own published flag, set
    // false at the same natural end but read here directly, no tick between).
    // Between the callback's publish and the tick's clear there is a sub-tick
    // window where is_playing() already reads false but the scanner is still
    // active: the face still says Stop while a bare press here — phase also
    // Idle — takes the play arm below instead of the stop arm the face
    // implies. Accepted cost, not a defect of this fork: the two terms track
    // the same event off two different clocks, and the drift is bounded to
    // that one window.
    // stop_playback_if_playing IS EXACTLY RIGHT for the rest case: its clear
    // sits ahead of its own nothing-to-do guard, so it ends the act and then
    // early-returns having moved no cursor and damaged nothing.
    if (playback.is_playing() ||
        app.audition_sequence.phase != GuiAuditionSequence::Phase::Idle) {
        // ONE STOP BODY (architect 2026-07-30): this edge used to hand-spell
        // playback.stop() + restore_playhead_to_lsp() while every other stop in
        // the product called the gesture stop. That second body's only surplus work was a
        // full-width TOP-STRIP invalidate, and it was dead here — the scanner
        // paints a waveform-area line and no top-strip pixel at all (the
        // playhead's top-strip half — the marker-lane head and the stem segment
        // beside it — belongs to the CURSOR alone), and a stop moves no cursor.
        // So the two collapsed onto
        // this one call, which takes the same QUIESCENCE FENCE through its own
        // playback.stop() and then deactivates the scanner and damages the
        // waveform area and the clock cell.
        stop_playback_if_playing();
        return;
    }
    // Defensive: clear any stale override from an unhandled stop path so
    // it can't survive into the new playback session. Runs before any
    // launch validation (the device and pre-sum gates below included), so a
    // refused launch still leaves it cleared; the shared launch body assumes
    // its caller ran it (scrub_launch_at, the other caller, does too).
    app.follow_overridden_for_session = false;
    // THE DEVICE IS ASKED HERE, AHEAD OF THE POSITION (2026-08-30). The launch
    // body asks it first for the same reason and would answer this press
    // correctly in source view — but the target arm below decides a POSITION
    // refusal of its own before control could reach the body, so a dead device
    // met at the domain's end would be told the wrong cause. Asked once for
    // both views because the fact is neither view's: nothing will sound. The
    // sentence is the launch body's own literal, and this arm returns, so the
    // belt below never adds a second card to the press.
    if (playback.device_unavailable()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kPlaybackDeviceUnavailableCard);
        return;
    }
    int64_t launch_pos = app.playhead_cursor_sample;
    if (app.active_audio_view == 'T') {
        // Launch = cursor + launch_offset. The offset is 0 for plain Space and
        // +N/2 for the lead-in audition; the resting cursor is never
        // moved either way, so stop just deactivates the scanner and the
        // cursor is right where it was left.
        //
        // The end check runs on the cursor against the offset-SHIFTED end,
        // BEFORE the sum is formed: cursor + launch_offset can exceed int64
        // for an extreme cursor value, while domain_end() - launch_offset
        // cannot (the offset is 0 or +N/2 at both call sites and domain_end
        // is a modest buffer extent), and a cursor that passes the check
        // bounds the sum below domain_end(). Same verdicts as summing first
        // wherever the sum was defined — and the launch body's own end check
        // (launch_pos >= domain_end() - 1, the identical verdict once the sum
        // exists) is therefore a pre-passed re-check for this caller, load-
        // bearing only for the scrub entry. The extra `- 1` is the two-frame
        // remainder gate (rationale at the launch body's source arm): a
        // launch whose start would leave fewer than two frames before
        // domain_end() no-ops. It does not change the overflow shape — the
        // check still runs on the cursor against the shifted bound before the
        // sum is formed — and domain_end() - launch_offset - 1 cannot
        // underflow into surprise: domain_end() >= 0 and launch_offset is 0
        // or +N/2, so a tiny buffer only drives the difference negative,
        // which merely makes the no-op FIRE (the safe direction). The
        // target-buffer populated check lives in the launch body; running
        // this gate first is verdict-identical (both are pure refusals on
        // the same one sentence, with no state written between them, and
        // domain_end() is well-defined for whatever buffer is bound).
        if (app.playhead_cursor_sample >=
            playback.domain_end() - launch_offset - 1) {
            // THE LAUNCH BODY'S OWN VERDICT ASKED EARLIER, and silent like it
            // (architect 2026-08-31, the file head's rule): a benign
            // one-dimensional refusal already at its state says nothing — the
            // playhead resting at the view's end is the whole answer, and the
            // Play button greys on this very predicate.
            return;
        }
        launch_pos = app.playhead_cursor_sample + launch_offset;
    }
    launch_playback_from(launch_pos);
}

// WOULD A PLAIN Space PLAY FROM THE RESTING CURSOR (architect 2026-08-30;
// the contract and the one reader — the PLAY face — are at the declaration,
// app_state.h). It is the press above asked WITHOUT acting, each term the
// act's own owner in the act's own order: the device (toggle_playback's
// first card), the dispatch arm's target-preview gate (target_preview_ready
// — the Space route's first refusal, which lives at on_key rather than in
// this file), toggle_playback's overflow-ordered end gate against the
// offset-shifted bound (the comment at that gate owns the ordering
// argument), and the one launch predicate on the launch position the act
// would actually form — cursor plus phase_reset_lead_in_launch_offset, the
// lead-in included, which is what makes the face and the key read ONE
// launch position. It sits here beside toggle_playback so the composition
// and the act read as one; a gate added to the press must be added here.
bool space_launch_would_play(const AppState& a, const GuiPlayback& playback,
                             const GuiTargetRender& target_render,
                             int64_t total_frames) {
    if (playback.device_unavailable()) return false;
    if (a.active_audio_view == 'T') {
        if (!target_preview_ready(target_render)) return false;
        const int64_t offset = phase_reset_lead_in_launch_offset(a, playback);
        if (a.playhead_cursor_sample >= playback.domain_end() - offset - 1)
            return false;
        return playback_launch_playable(a, playback, total_frames,
                                        a.playhead_cursor_sample + offset);
    }
    return playback_launch_playable(a, playback, total_frames,
                                    a.playhead_cursor_sample);
}

// The audition launch entry, the scrub act's stop-then-start START
// half: begin the scanner from `frame` — an
// absolute active-paint-domain position (the caller hands it in already
// clamped to the live domain) — with the resting cursor, selection, region, and
// follow all untouched. The SCANNER, not the cursor, is what the gesture
// drives: the
// scanner fields are meaningful only while active, and this is exactly the
// launches-the-scanner-independently-of-the-cursor consumer that contract
// anticipated. Riding the shared launch body makes an audition launch
// indistinguishable from a cursor Space launch except for the start
// position, so every standing gate applies (contract at
// the header declaration) — and each launch re-captures the end bound freshly,
// the point of the fresh-session semantic.
void GuiPlaybackLifecycle::scrub_launch_at(int64_t frame) {
    // Defensive: a live session never launches — a scrub act over a live
    // session STOPS it and returns without reaching here, so the caller always
    // arrives stopped. This guard only
    // keeps a future caller from stacking play() over a live run.
    if (playback.is_playing()) return;
    // The same defensive clear toggle_playback's play edge runs (the launch
    // body assumes its caller ran it): a stale override must not survive into
    // the new session, and a refused launch leaves it cleared exactly as a
    // refused Space does.
    app.follow_overridden_for_session = false;
    launch_playback_from(frame);
}

// The active view's play end (contract at the declaration).
// WHAT THE END IS SPLITS BY AUDIO VIEW (architect 2026-08-05): TARGET plays to
// the bound preview buffer's end, which IS the trim window — the preview
// render covers that window and nothing else exists to play. SOURCE PLAYS TO
// THE SONG'S END, the trim window not bounding it at all: source playback is
// the source file read directly, so the gating bought nothing there and only
// cost the user the audition past a trim bound he was aiming. The NAVIGATION
// range is untouched by this — Home/End still jump to the trim bounds
// (Viewport::trim_range, the shared owner both used to read here). In source
// view the paint domain is source frames and the bound buffer is the source
// file itself, so this is both the domain's end and the buffer's; play()
// clamps its end bound to the bound total besides.
int64_t GuiPlaybackLifecycle::active_view_play_end() const {
    return (app.active_audio_view == 'T') ? playback.domain_end()
                                          : audio.total_frames();
}

// The view-end launch: validate `launch_pos` — an ABSOLUTE position in the
// active PAINT domain — and play from it to the active view's end. Two
// callers: toggle_playback's play edge (cursor + launch_offset) and
// scrub_launch_at (the scrub's clicked frame). Both run the defensive
// follow-override clear before delegating, so it precedes validation exactly
// once either way. Everything else is the one launch body's, below.
bool GuiPlaybackLifecycle::launch_playback_from(int64_t launch_pos) {
    return launch_playback_window(launch_pos, active_view_play_end());
}

// THE BOUNDED AUDITION (contract at the declaration): play `span` frames from
// `start`, the end clamped to the view's own. The pre-sum gate takes the
// shape of toggle_playback's: `start` is tested against the view end BEFORE
// `start + span` is formed, so the sum is only ever formed from a start
// below a modest buffer extent and cannot overflow; the launch body's own
// remainder gate then re-checks the same verdict on the formed start. A
// window that the clamp shortens to fewer than two frames refuses there too
// (the two-frame remainder gate is on `start` against the view end, and a
// start that passes it leaves at least two frames for the clamp to keep).
bool GuiPlaybackLifecycle::launch_bounded_audition(int64_t start,
                                                   int64_t span) {
    // Defensive, scrub_launch_at's own guard: a live session never launches.
    if (playback.is_playing()) return false;
    if (span <= 0) return false;
    // The same defensive clear the other two launch entries run before their
    // own validation (the launch body assumes its caller ran it).
    app.follow_overridden_for_session = false;
    const int64_t view_end = active_view_play_end();
    if (start >= view_end - 1) return false;
    const int64_t end = std::min(start + span, view_end);
    return launch_playback_window(start, end);
}

// THE ONE LAUNCH BODY: validate `start` — an ABSOLUTE position in the active
// PAINT domain — against the active view's window, seed the scanner, and play
// [start, end). Returns whether it launched; its two refusals — the dead
// device first, then the launch position — each say so on a notification card
// (2026-08-30; the "nothing to audition" family, which said nothing until
// then). Two callers: the view-end launch above
// (Space's play edge and the scrub, `end` = the view's end) and the bounded
// audition (`end` = start + span, clamped). This body never writes the
// resting cursor — the scanner is the only playhead it touches, so a launch
// is a pure scanner event and the cursor is untouched by construction.
// Callers run the defensive follow-override clear before delegating.
//
// Target-view branch: the audio device is bound to app.target_buffer
// (rebound by GuiTargetRender's completion path on Success, with the
// buffer's domain offset travelling with the bind). The playhead in
// target view is a full-target-frame coordinate, and playback's whole
// public API speaks the bound buffer's domain (playback.h), so the
// bounds pass straight through: [domain_begin(), domain_end()) is the
// target buffer's full-target-frame extent and play() takes the
// validated launch position unchanged.
// (THE PER-LAUNCH SPEED PUSH IS GONE — architect 2026-08-27. This site forced
// 1.0 in target view and applied app.playback_speed otherwise, on the reasoning
// that target view's whole purpose is to hear the user's authored warp and an
// extra multiplier on top would defeat it. That reasoning outlived the feature:
// the key retired, so every view plays at the source's own rate and there is
// nothing left to force.)
bool GuiPlaybackLifecycle::launch_playback_window(int64_t start, int64_t end) {
    // THE LAUNCH-BODY CLEAR (architect 2026-08-26): every launch begins a
    // fresh session, so the A/B audition sequence ends here whoever asked and
    // whether or not the launch below refuses — a plain Space or a scrub
    // launched in the sub-tick window after a bounded play's natural end can
    // then never be taken for the act's own play when IT ends. The act's own
    // launch passes through this same line and re-arms once this returns true
    // (GuiAbAudition::launch_phase). The edge inventory is at
    // GuiAuditionSequence.
    clear_audition_sequence(app);
    // Both `start` (playback.play()'s launch bound) and the scanner's
    // launch position below are in the active PAINT domain
    // (full-target-frame in target view; source-frame otherwise)
    // — playback's API takes domain coordinates, so the same value
    // serves both, and follow_scroll_if_needed compares the scanner
    // against the full-domain viewport with no wrong-domain leak.
    //
    // NO LOOPING (architect 2026-07-30, "looping behavior is not that useful.
    // ok to remove all looping" — re-ruling the 2026-07-19 loop ruling dead).
    // EVERY audition plays once from `start` to `end` and stops there. The launch
    // verdict, the per-view loop starts, the audio-callback wrap, the wrap
    // counter and its predictor resync are all gone with it; the natural-end
    // teardown is what remains and is what every session now takes. `end` is
    // the view's end for Space and the scrub and `start + kAuditionMs` for the
    // bounded audition — a different end, the same once-to-its-end play.
    //
    // EVERY REFUSAL IS THE ONE PREDICATE (playback_launch_playable,
    // app_state.h — hoisted out of this body 2026-08-15 so the bottom row's
    // PLAY button could read the launch's own refusal; the architect reversed
    // that face arm the same day and the predicate stayed with THIS as its one
    // reader until the A/B audition's press-time gate became the second on
    // 2026-08-26 — a gate reader, not a face — and the PLAY button's face
    // became the third on 2026-08-30 under the truthful-buttons ruling,
    // through space_launch_would_play below since that evening, which asks
    // it about the launch position the act would actually form — the
    // lead-in offset included, the architect reversing the resting-cursor
    // seam). The
    // per-arm reasoning moved to
    // the predicate whole: the target arm's
    // buffer-populated check (which must live on this shared path — the scrub
    // launch arrives with no outer gate), the two-frame remainder gate against
    // the bound buffer's own domain end (a one-frame remainder is an isolated
    // impulse, the audible pop — End+Space is a common slip of the hand this
    // product caters to; deliberate near-end plays stay admitted, and no
    // fade/ramp/declick machinery is added, considered and REJECTED by
    // ruling), the target arm's lower bound, and the source arm's
    // no-lower-gate-but-the-domain's-own rule. A one-frame SOURCE FILE is
    // launch-inert by this gate (its render still works: the trimmer's
    // one-frame-fady-trim latitude is a RENDER latitude, not an audition one).
    // A DEAD OR ABSENT DEVICE IS ASKED FIRST, AND SAYS SO (architect
    // 2026-08-30): the launch below would "succeed" — the scanner would seed,
    // the follow would scroll and play() would return — with nothing coming
    // out of the machine, which is the one refusal the user cannot see for
    // himself. The fact is GuiPlayback::device_unavailable's, the honest
    // reading of "nothing will sound" on both backends and the same one the
    // render player's tick forks on; no bit is added and nothing is latched
    // here. The load's own stderr line stays, but it is printed once at
    // startup and this is every press after it. Ahead of the playable gate
    // so the sentence names the device rather than the position — and for the
    // same reason THE TWO PRE-LAUNCH GATES ASK IT AHEAD OF THIS ONE (2026-08-30:
    // toggle_playback's target pre-sum gate and GuiAbAudition::start's
    // preflight, both of which decide something — a position card, a camera and
    // a tab switch — before control could arrive here). This check is the BELT
    // they leave standing: it is the only gate on the scrub's launch, which has
    // no outer road at all, and each of the three returns, so exactly one card
    // is raised per press. The sentence is kPlaybackDeviceUnavailableCard,
    // spelled once at the header for all three.
    if (playback.device_unavailable()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kPlaybackDeviceUnavailableCard);
        return false;
    }
    // AND A LAUNCH POSITION WITH NOTHING LEFT REFUSES IN SILENCE (architect
    // 2026-08-31, retiring the 2026-08-30 card; the rule is at the file head).
    // A benign one-dimensional refusal already at its state says nothing: the
    // playhead is one mark resting visibly at the view's end, and the Play
    // button greys on this very predicate, so the grey is the message on the
    // roster and the unmoved playhead is the message on the keyboard. Every
    // road that plays — Space, the scrub's audition, the A/B audition's
    // bounded plays — passes through here and none of them said it twice.
    if (!playback_launch_playable(app, playback, audio.total_frames(),
                                  start)) {
        return false;
    }
    // (`end` is the caller's: the view's end from active_view_play_end for
    // the view-end launch, the clamped bounded window for the audition.)
    // Scanner launch = the validated launch position, in the paint domain
    // in every view (see the comment above `start`). Seed the continuous
    // position too so the first paint (where the predictor's cur still equals
    // start and the pre-paint hook early-returns) draws at the launch column
    // rather than a stale precise value.
    app.playhead_scanner_sample = start;
    app.playhead_scanner_precise = static_cast<double>(start);
    app.playhead_scanner_active = true;
    // If the launch position is offscreen at play press, left-edge-align the
    // viewport on it before the scanner issues forth (Space launches from the
    // possibly-offscreen cursor; a scrub click is a visible column by
    // construction, so this no-ops there). Follow mode's same-shape check is
    // sufficient regardless of whether the user has follow mode toggled on,
    // so always run it on press.
    // UNDER THE CENTERED LAMP THE SEED IS THE DERIVATION instead (2026-08-31,
    // R11): the scanner issues forth CENTERED where follow's check would
    // left-edge-align it, so the first frame already holds the invariant
    // rather than taking a visible two-step (align, then the pre-paint's
    // recenter one frame later). Unconditional on the lamp for the same
    // reason the follow-shape check is unconditional here — the launch edges
    // clear the pan suppression before this line, so a new session always
    // starts pinned.
    if (app.centered_mode) viewport.derive_centered_viewport();
    else                   viewport.follow_scroll_if_needed();
    // Damage the waveform area and the clock cell NOW, in the success tail
    // (strictly after every refusal return above). A launch's visible effect —
    // the scanner line appearing at the launch column and the timestamp readout
    // advancing — otherwise waits for the next tick-driven paint opportunity
    // (the tick heartbeat invalidates the scanner's current column even when the
    // integer predictor has not yet advanced), leaving the PRESS itself damage-less
    // for a whole frame. Damaging at press paints the scanner one frame earlier
    // than the tick heartbeat would — honest damage at the press that caused it.
    //
    // FULL waveform-area damage, NOT a narrow launch-column recompute (the
    // "for a rare cleanup, full-area damage beats a clever narrow recompute" shape).
    // A playback launch is a rare, DISCRETE command, and the scanner PAINTS against
    // the plate owner (wf_cache.fp_*), which any narrow-damage basis reachable from
    // here can transiently DIVERGE from: during an async publish window (plate old,
    // live viewport new) and — after a resize — the item-only promote (the tick
    // rebuilds the flag item mirror against the new live width while the
    // scanner keeps painting the old plate until the still-in-flight worker
    // publishes), where a narrow item-basis column would miss the plate-basis
    // scanner and the line would stay invisible until the publish. Full-area
    // damage is ownership-window-proof by construction, at one full repaint per
    // launch keystroke — bounded for a rare command. It also subsumes any column
    // a just-ended session's scanner still had painted (a launch inside the
    // sub-tick window between the audio thread's natural end and the tick that
    // deactivates the scanner), so no stale line can survive a relaunch.
    viewport.invalidate_waveform_area();
    viewport.invalidate_clock_area();
    playback.play(start, end);
    return true;
}

// Click-keep-alive: reseek a live playback session to `sample` without the
// stop-and-restart visual glitch. Both arms mirror the one launch body's
// (launch_playback_window's)
// range policy: source view against [0, total_frames) — the SONG, the trim
// window having stopped bounding source playback 2026-08-05 — and target view
// against the bound buffer's [domain_begin(), domain_end()). `sample` is a
// paint-domain coordinate, the same domain
// playback's public API speaks in every view. ONE call site (re-derived by grep
// 2026-08-12): `place_playhead_at_click_column`, input_pointer.cpp — the
// placement's seat, always with playback alive at call time. That one body
// serves the DEFERRED CLICK ACT at a plain navigation-surface press's
// motionless release (run_nav_click_act, live and `h`-view arms) and the two
// SHIFT formers' presses, every route stop-free by the claim-keyed stop design
// precisely so this reseek can reach a live session. Keep-alive is
// exactly those routes' point
// (reposition the running audition under the freshly-placed cursor without a
// restart glitch). The
// scrub paths never come here: a scrub act over a LIVE session is a pure STOP
// (scrub_act_at — the clicked frame is ignored, and the NEXT click launches a
// fresh session from where it lands), so only a stopped session ever reaches
// scrub_launch_at and no scrub ever repositions a running one.
// Both arms carry the same two-frame remainder gate as the launch body (see
// the rationale at its source arm): a reseek that would leave fewer than two
// playable frames is out of range, so a live-playback click at the last frame
// stops cleanly instead of playing a one-frame impulse — symmetric with Space.
// An out-of-range position in any arm falls back to a MANUAL stop with
// immediate scanner teardown (stop_playback_if_playing). No follow-scroll at
// the reseek site: the reseek repositions without recentering the viewport.
//
// stop_playback_if_playing clears follow_overridden_for_session. The
// placement caller sets it back to true immediately AFTER this returns
// (having already run move_playhead_to before), so the reset is a harmless
// transient there — that caller owns the override across the reseek.
void GuiPlaybackLifecycle::reseek_keeping_alive(int64_t sample) {
    // (NO A/B AUDITION CLEAR HERE: the act's clear is the MOVEMENT OWNER's, one
    // call up. This body's one caller — place_playhead_at_click_column,
    // input_pointer.cpp — runs Viewport::move_playhead_to unconditionally
    // before it, and a movement is exactly what ends the act, so a clear here
    // would be a second spelling of a decision already taken. The complete
    // owner inventory is at GuiAuditionSequence, app_state.h.)
    if (app.active_audio_view == 'T') {
        if (app.target_buffer_frames <= 0) { stop_playback_if_playing(); return; }
        if (sample < playback.domain_begin() ||
            sample >= playback.domain_end() - 1) {
            stop_playback_if_playing();
            return;
        }
        // The window is unchanged; only the immediate resume point moves.
        playback.play(sample, playback.domain_end());
        return;
    }
    // Source view: the range is the SONG, not the trim window (the launch body's
    // ruling — source playback reads the source file directly, so nothing gates
    // it). In-range-only semantics as in the target arm above: a reseek to the
    // last frame (or past the domain) stops rather than playing a one-frame
    // impulse, the same sane degradation as Space's own no-op (which says so
    // on a card at its gate; this reseek is not a press and says nothing). This guard also
    // means play() below can never be reached with an empty range from this site,
    // closing the play() early-return trap (end_sample <= start_sample returns
    // early WITHOUT clearing the playing flag) at its only reseek exposure.
    const int64_t song_end = audio.total_frames();
    if (sample < 0 || sample >= song_end - 1) {
        stop_playback_if_playing();
        return;
    }
    // The window is unchanged, only the resume point moves (see the target arm
    // above).
    playback.play(sample, song_end);
}

// Set follow mode (contract at the header declaration). Shared by the bare-`f`
// toggle and the settings editor's `follow=` commit.
void GuiPlaybackLifecycle::set_follow_mode(bool desired) {
    const bool was_off = !app.follow_mode;
    app.follow_mode = desired;
    if (was_off && app.follow_mode && playback.is_playing()) {
        // Explicit enable clears a prior pan suppression so follow resumes
        // paging, not just the one initial jump — which since 2026-07-30 is this
        // arm's whole purpose, the "manual-pan suppression" it names finally
        // having pan producers (every viewport pan during playback sets the
        // flag; the producer inventory is at its declaration in app_state.h).
        // Land the scanner at the page-turn position if it had drifted
        // offscreen; no-op when it is already in view.
        app.follow_overridden_for_session = false;
        playback.resync_predictor();
        viewport.follow_scroll_if_needed();
    }
}

// Set the centered pin (contract at the header declaration) — the `y` lamp's
// one gesture chokepoint, set_follow_mode's sibling above.
void GuiPlaybackLifecycle::set_centered_mode(bool desired) {
    const bool was_off = !app.centered_mode;
    app.centered_mode = desired;
    if (was_off && app.centered_mode) {
        // THE TOGGLE ITSELF RECENTERS — the invariant starts holding at the
        // press, not at the next playhead change. During live playback the
        // explicit enable clears a prior pan suppression exactly as follow's
        // off→on arm does (the user re-engaged the autonomous mover), and the
        // one-shot jump re-anchors the predictor like every discrete pan.
        if (playback.is_playing()) {
            app.follow_overridden_for_session = false;
            playback.resync_predictor();
        }
        // (The derivation memory is stamped by the body itself — its one
        // writer since codex round C, so the next pre-paint does not re-derive
        // a camera this press just derived. No hand-kept stamp here.)
        viewport.derive_centered_viewport();
    }
    // The off edge writes nothing: the camera stays where the pin left it.
}

// (set_playback_speed IS GONE — architect 2026-08-27, with the
// `playback_speed` key it was the one writer of. It stored the value, pushed it
// to the engine outside target view, and resynced the predictor so the speed
// change could not retroactively rewrite the elapsed-since-anchor period. All
// three concerns died with the feature; the resync events that remain are
// listed at playback.h.)
