#include "playback_lifecycle.h"

#include "render_player.h"   // the stop body's fork publishes the head unit's state

#include <algorithm>
#include <cstdint>

using RenderPlayerTransport = AppState::RenderPlayer::Transport;

// THE LAUNCH'S POSITION REFUSAL IS SILENT AT BOTH ITS SITES (architect
// 2026-08-31): a benign one-dimensional refusal already at its state says
// nothing — the playhead rests visibly at the view's end and the Play button
// greys on that very predicate. The two sites are the one launch body's
// playable gate and toggle_playback's verdict-identical pre-sum twin. The
// DEVICE refusal has its card: a dead device is the one fact the screen
// cannot show.

// Gesture-stop: called by any handler that will move the
// cursor (keys, button press, undo/redo, tab switch) — and by the two
// keyboard TRIM MUTATIONS (2026-07-30), which stop without touching the
// cursor at all.
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
    // THE LIMITER LAMP'S SPAN IS VOIDED HERE, AHEAD OF THE GUARD (architect
    // 2026-11-06): a stop and a launch in one act (the main
    // view's scrub click) leave no tick between to see the play stopped, so
    // the stop itself says a span ends (AppState::limiter_lamp_prev_frame).
    app.limiter_lamp_prev_frame = -1;
    // THE RENDER PLAYER'S FORK, INSIDE THE ONE STOP BODY (2026-08-28): the
    // player's transport is a session over ITS OWN buffer with no scanner and
    // no waveform picture, so its "a session stood" state is
    // `render_player.transport` in its LIVE value (the scanner flag's mirror)
    // and its teardown is that state moved to PAUSED plus the modal row
    // damaged — the play/pause glyph, the clock and the scrub all read it.
    // PAUSED IS WHAT THIS BODY MEANS, AND THIS FORK IS ITS ONE WRITER: a
    // sounding transport that stops is parked where it stopped, at frame 0 as
    // anywhere else — the natural end's rest at the item's end included
    // (architect 2026-10-26 ~23:55) — so the next Play resumes THAT item. The
    // callers that mean IDLE instead — close() and up(), through the unload
    // body — write it after this returns, and open()'s own reset never
    // reaches this fork (the state's whole writer set is at the field,
    // app_state.h).
    // The FENCE is the same: playback.stop() proves the callback
    // is out of the item's buffer before a rebind or a free, exactly as it
    // proves it out of the source's. Every player stop — the pause, the
    // natural end, the Up act's unload, the close, the rebind ahead of the
    // next item — comes here and nowhere else, which is what keeps the
    // keyboard stop rule and the fence-before-rebind ordering one body.
    if (app.render_player.active) {
        if (!playback.is_playing() &&
            app.render_player.transport != RenderPlayerTransport::Live)
            return;
        playback.stop();
        // The device is not touched here or anywhere between plays (the
        // lifecycle block at the head of playback_aaudio.cpp): a player stop is
        // the fence and the transport write alone.
        app.render_player.transport = RenderPlayerTransport::Paused;
        viewport.invalidate_modal_dialog_area();
        // THE HEAD UNIT'S PUSH AT EVERY PLAYER STOP LIVES HERE AND NOWHERE
        // ELSE (2026-08-28): this fork is the one place every player stop passes
        // — the pause, the natural end, the dead device, a launch over a
        // pending device (GuiRenderPlayer::rest_if_launch_pending), the close,
        // the Up act's unload, the rebind ahead of the next item — so the
        // push lives inside it and no caller can forget it. The callers that
        // go on to an item's own push, an "inactive" one or a SILENCE TRACK
        // (play_wav's tail, the close, and the Up act through its root
        // entry's rebuild_rows) supersede this one a moment later, two
        // binder calls where one would do; accepted, the alternative being
        // one push per caller that the next caller forgets — and under up()
        // the supersession is load-bearing rather than merely tidy, this
        // push still carrying the item that act is dropping. The resume
        // point the push reads is the caller's to have written BEFORE
        // calling here (toggle_pause, the tick's dead-device arm and the
        // pending launch's rest do; the natural end writes the item's end
        // first for the same reason).
        if (render_player) render_player->publish_media_state();
        return;
    }
    // THE SCORE VIEWER'S FORK (2026-10-26, score_viewer.h's THE
    // TRANSPORT), the player's shape: while the viewer stands the engine
    // plays the score's soundtrack or nothing — the project's audio stopped at
    // its open — so its "a session stood" state is the viewer's LIVE
    // transport, and its teardown is that transport parked at PAUSED AT THE
    // ENGINE'S CURSOR, read through the one position face before the fence
    // (the resting cursor is the active tab's remembered position, whose
    // write-through is the viewer's act), the controls row damaged for the
    // play key's glyph. Every viewer stop — the pause, the natural end, the
    // dead device, the close — comes here, which keeps the fence-before-
    // rebind ordering one body for the soundtrack too; the natural end
    // rewrites the rest after it.
    if (app.score_viewer.active) {
        using ScoreTransport = AppState::ScoreViewer::Transport;
        if (!playback.is_playing() &&
            app.score_viewer.transport != ScoreTransport::Live)
            return;
        if (app.score_viewer.transport == ScoreTransport::Live)
            active_view_state(app).score_position = playback.cursor();
        playback.stop();
        app.score_viewer.transport = ScoreTransport::Paused;
        viewport.invalidate_rect(bottom_row_area(app));
        return;
    }
    if (!playback.is_playing() && !app.playhead_scanner_active) return;
    // THE DISPLAY KEEP-ALIVE'S ARMING (architect 2026-11-13;
    // the facts and the ruling at kDisplayKeepAliveFastStopMs,
    // playback_lifecycle.h): THIS ARM IS THE ONE SEAT EVERY STOP OF A
    // PROJECT PLAY CROSSES — the user's own (bare Space's stop edge, the
    // car's pause and Stop), the waveform's placement and marker clicks, the
    // scrub's stop half ahead of its relaunch, the natural end and
    // RESTRICT's edge (the tick's two), a modal open and every authoring
    // stop of the keyboard stop rule above — and a stop of a play the user
    // launched within kDisplayKeepAliveFastStopMs opens the brief window
    // past it, whatever stopped it: the panel drops the same after any
    // stop. The render player's and the score viewer's stops returned at
    // their forks above and their plays never stamp; the A/B audition's
    // plays clear the stamp. The stamp is consumed either way, so one play
    // arms at most once.
    if (user_play_launched_ms >= 0) {
        const int64_t now = monotonic_ms();
        if (now - user_play_launched_ms <= kDisplayKeepAliveFastStopMs)
            display_keep_alive_until_ms = now + kDisplayKeepAliveWindowMs;
        user_play_launched_ms = -1;
    }
    playback.stop();
    // THE SCANNER'S OWN RECT AT ITS PAINTED PLACE, THEN THE BIT (architect
    // 2026-11-16: "number 3, narrowing agree";
    // Viewport::invalidate_standing_scanner): the place is read while the
    // bit still stands, from the still-current playhead_scanner_precise on
    // the PLATE basis — the place the last paint drew the line at, by the
    // pre-paint advance's own argument for its OLD place (main.cpp's "OLD
    // PLACE, DERIVED — never stashed"; playback.stop() writes nothing of
    // AppState). A place the viewport or the plate's basis moved since that
    // paint carries the mover's own whole-area damage, a publication's
    // included. With the bit already down nothing of the canvas is the
    // scanner's. The clock cell flips from the scanner's time to the
    // cursor's. The rule and the per-site shape table live at
    // playhead_pixel_x (app_state.h).
    if (app.playhead_scanner_active) viewport.invalidate_standing_scanner();
    app.playhead_scanner_active = false;
    viewport.invalidate_clock_area();
    // THE STOP LEAVES THE FOLLOW LAMP ALONE (architect 2026-09-23): it is a
    // lamp, not a one-shot, so the next play follows while it stays lit
    // (AppState::follow).
}

// The one owner of the modal-open stop. See the declaration for the decision
// table (every modal surface's open stops, the lane editors' included,
// architect 2026-11-20) and for the refusal-gating rule every caller
// observes. Pure delegation by design:
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
// (architect 2026-07-30): every Space — span resting or not — launches from
// here. Space-to-stop just
// DEACTIVATES the scanner (its value fields go stale by contract — no
// snap-back, no separate stash; the cursor is the launch point by
// definition).
//
// The play edge computes the launch position — cursor + launch_offset, the
// offset non-zero only for the target-view lead-in Space auditions — and delegates
// to launch_playback_from, the VIEW-END ENTRY the scrub launch also rides,
// which adds the end and hands both bounds to the one launch body
// (launch_playback_window); the validation, scanner seed, camera term,
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
    // does), and the bottom icon row must never lie about live state. Without
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
        // ONE STOP BODY (architect 2026-07-30): this edge calls the gesture
        // stop like every other stop in the product, which takes the
        // QUIESCENCE FENCE through its own playback.stop() and then
        // deactivates the scanner, damaging its own rect and the clock cell.
        // No top-strip damage is owed: the scanner paints a waveform-area line
        // and no top-strip pixel (the playhead's top-strip half — the ruler's
        // head and the column's marker-lane run beneath it — belongs to the
        // CURSOR alone), and a stop moves no cursor.
        n2_toggle_branch = "stop";
        stop_playback_if_playing();
        return;
    }
    // THE DEVICE IS REOPENED HERE, AHEAD OF THE POSITION (asked 2026-08-30;
    // a REOPEN, architect 2026-09-02: a read alone would card forever after a
    // route drop, so the press reopens through
    // GuiPlayback::ensure_device_available_for_play and cards only when the
    // reopen FAILED). The launch
    // body asks it first for the same reason and would answer this press
    // correctly in source view — but the target arm below decides a POSITION
    // refusal of its own before control could reach the body, so a dead device
    // met at the domain's end would be told the wrong cause. Asked once for
    // both views because the fact is neither view's: nothing will sound. The
    // sentence is the launch body's own — the chooser's
    // (playback_device_refusal_card, playback_lifecycle.h: the pending
    // device's where this press's reopen overran its bound or met a device
    // job in flight, architect 2026-11-26) — and this arm returns, so the
    // belt below never adds a second card to the press (the belt's reopen then
    // finds the stream this one opened, a no-op).
    if (!playback.ensure_device_available_for_play()) {
        n2_toggle_branch = "device refused";
        notifications.notify(AppState::NotificationClass::Normal,
                             playback_device_refusal_card(playback));
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
            n2_toggle_branch = "end refused";
            return;
        }
        launch_pos = app.playhead_cursor_sample + launch_offset;
    }
    // N2's diagnostic (the record at n2_toggle_branch, playback_lifecycle.h).
    n2_toggle_branch = launch_playback_from(launch_pos) ? "launch"
                                                         : "launch refused";
}

// THE RESTRICT GATE'S QUESTION (architect 2026-10-29, AppState::restrict):
// is the active-domain launch position `frame` on the window AS PAINTED —
// displayed_frame_on_screen (warp_frame_map_view.h), the cameras' one
// visibility test, asked on the ITEM basis (item_viewport_basis, the promoted
// mirror of what the last frame painted; this cluster sees no GuiPaintHandler,
// and the mirror equals the plate basis the playheads draw through at every
// committed frame; cold, the live viewport by the basis's own contract, as
// the scrub's click converts). A waveform with no columns has no window to be
// off, so nothing is. TWO READERS, the act and its face: the
// view-end launch entry's gate below and space_launch_would_play's term.
static bool launch_frame_on_painted_window(const AppState& a,
                                           const GuiAudio& audio,
                                           int64_t frame) {
    const ItemViewportBasis b = item_viewport_basis(a, audio);
    if (b.spp <= 0.0 || b.area_w <= 0) return true;
    return displayed_frame_on_screen(static_cast<double>(frame), b.vp_start,
                                     b.spp, b.lattice_origin, b.area_w);
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
// launch position. RESTRICT'S START GATE (2026-10-29) is the last term, on
// that same position, as the view-end entry asks it. It sits here beside
// toggle_playback so the composition
// and the act read as one; a gate added to the press must be added here.
// THE DEVICE TERM IS A READ, NEVER A REOPEN, AND THE READ IS THE
// NEVER-CAME-UP HALF OR THE PENDING DEVICE (2026-09-02, the truthful-buttons
// rule; pending, architect 2026-11-26): the press asks
// ensure_device_available_for_play, which reopens a dead AAudio stream and
// cards only when that fails, so the face asks device_absent_or_pending —
// the device that never came up, and the device whose last call Android has
// not answered, the two states under which a press plays nothing — and
// AGREES with the act: a dropped route on the tablet leaves Play lit, and
// its press reopens and plays exactly as Space does.
bool space_launch_would_play(const AppState& a, const GuiAudio& audio,
                             const GuiPlayback& playback,
                             const GuiTargetRender& target_render,
                             int64_t total_frames) {
    if (playback.device_absent_or_pending()) return false;
    int64_t launch_pos = a.playhead_cursor_sample;
    if (a.active_audio_view == 'T') {
        if (!target_preview_ready(target_render)) return false;
        const int64_t offset = phase_reset_lead_in_launch_offset(a, playback);
        if (a.playhead_cursor_sample >= playback.domain_end() - offset - 1)
            return false;
        launch_pos = a.playhead_cursor_sample + offset;
    }
    if (!playback_launch_playable(a, playback, total_frames, launch_pos))
        return false;
    // RESTRICT'S START GATE, the view-end entry's own term on the same launch
    // position (architect 2026-10-29), so the PLAY face greys where the press
    // would only card.
    return !a.restrict || launch_frame_on_painted_window(a, audio, launch_pos);
}

// The audition launch entry, the scrub act's launch (which always follows its
// stop, the scrub always playing, 2026-09-21): begin the scanner from
// `frame` — an
// absolute active-paint-domain position (the caller hands it in already
// clamped to the live domain) — with the resting cursor, selection, region, and
// hold posture and follow lamp all untouched. The SCANNER, not the cursor, is what the gesture
// drives: the
// scanner fields are meaningful only while active, and this is exactly the
// launches-the-scanner-independently-of-the-cursor consumer that contract
// anticipated. Riding the shared launch body makes an audition launch
// indistinguishable from a cursor Space launch except for the start
// position, so every standing gate applies (contract at
// the header declaration) — and each launch re-captures the end bound freshly,
// the point of the fresh-session semantic.
void GuiPlaybackLifecycle::scrub_launch_at(int64_t frame) {
    // Defensive: a live session never launches — the scrub act runs the one
    // stop body before calling here (architect 2026-09-21), so the caller
    // always arrives stopped. This guard only
    // keeps a future caller from stacking play() over a live run.
    if (playback.is_playing()) return;
    launch_playback_from(frame);
}

// The active view's play end (contract at the declaration).
// WHAT THE END IS SPLITS BY AUDIO VIEW (architect 2026-08-05): TARGET plays to
// the bound preview buffer's end, which IS the trim window — the preview
// render covers that window and nothing else exists to play. SOURCE PLAYS TO
// THE SONG'S END, the trim window not bounding it at all: source playback is
// the source file read directly, so the gating bought nothing there and only
// cost the user the audition past a trim bound he was aiming. The NAVIGATION
// range is untouched by this — Home/End jump to the trim bounds
// (Viewport::trim_range). In source
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
// scrub_launch_at (the scrub's clicked frame). Everything else is the one
// launch body's, below.
bool GuiPlaybackLifecycle::launch_playback_from(int64_t launch_pos) {
    // THE USER-LAUNCH CLEAR (architect 2026-08-26; at this entry,
    // 2026-09-01): every launch of the user's own
    // transport begins a fresh session, so the A/B audition sequence ends
    // here whoever asked and whether or not the body below refuses — any
    // launch DURING ONE OF THE ACT'S RESTS, or inside the sub-tick window
    // after a bounded play's natural end (bare Space reads `phase != Idle` as
    // transport-live and takes the stop side there; the scrub runs the stop
    // body first, 2026-09-21, which clears the act ahead of its own guard, so
    // this clear is its second), can then never be taken for the act's own
    // play when IT ends. It sits ahead of the delegation, so a REFUSED user
    // launch still ends a standing act. THE ROADS INTO THE BODY THAT ARE NOT
    // THE ACT'S are this entry and car_toggle_playback, each with its own
    // clear (the body's third caller is launch_bounded_audition), and the
    // act's own launches need no clear: the
    // act IS the standing phase, written by GuiAbAudition::launch_phase before
    // it calls the body DIRECTLY — the body itself reads no phase and
    // branches by no act; the distinction is structural alone, carried by
    // which entry a launch reached it through. The edge inventory is at
    // GuiAuditionSequence (app_state.h), owner (2).
    clear_audition_sequence(app);
    // RESTRICT'S START GATE (architect 2026-10-29, AppState::restrict): under
    // the lit lamp a launch position off the painted window is REFUSED ON A
    // CARD — the play would start where RESTRICT says no play runs — and the
    // page-in below never rescues it. Ahead of the body, so the refusal
    // writes no scanner and no camera; behind the clear above, so a refused
    // launch still ends a standing act as every user launch does. Space's
    // play edge asked the device first (toggle_playback), so a dead device is
    // still told as itself. THE SCRUB NEVER TRIPS IT: it launches from the
    // column just clicked, a frame on the window by construction. The face
    // asks the same term (space_launch_would_play). The card is one clause
    // and names no key (notifications.h's head).
    if (app.restrict &&
        !launch_frame_on_painted_window(app, audio, launch_pos)) {
        restrictable_play = false;
        notifications.notify(AppState::NotificationClass::Normal,
                             "Playhead is off screen");
        return false;
    }
    // FOLLOW LIT, THE FOLLOW SCROLL (Viewport::follow_scanner, architect
    // 2026-11-15); DARK, PageIn: Space launches from the cursor and the scrub
    // from a clicked column, so both roads start on screen except when the
    // cursor has been left offscreen by a pan — which is exactly what the
    // page-in rescues for the unrestricted play (the restricted one was
    // refused above).
    // THIS ENTRY'S PLAY IS THE RESTRICTABLE ONE (restrictable_play, the
    // contract at the declaration).
    restrictable_play = launch_playback_window(
        launch_pos, active_view_play_end(), kPlaybackNoLoop,
        app.follow ? LaunchCamera::Follow : LaunchCamera::PageIn);
    // THE USER'S PLAY'S START, the display keep-alive's stamp (the two
    // stamps' contract at the declaration): a launch that ran.
    if (restrictable_play) user_play_launched_ms = monotonic_ms();
    return restrictable_play;
}

// THE CAR'S SPACE (contract and the ruling at the declaration; architect
// 2026-09-17). The stop arm is toggle_playback's own fork verbatim — the
// sub-tick disagreement between the scanner bit and the audio thread's flag
// recorded there is this arm's too — and PAUSE IS STOP: the one stop body,
// nothing remembered. Past the fork, the car's play.
void GuiPlaybackLifecycle::car_toggle_playback() {
    if (playback.is_playing() ||
        app.audition_sequence.phase != GuiAuditionSequence::Phase::Idle) {
        stop_playback_if_playing();
        return;
    }
    // The play arm's prologue is toggle_playback's: the device reopened at
    // the press (carding a failed reopen through the same chooser; the
    // launch body's belt then finds the stream this one opened, a no-op). The pre-sum end gate of
    // toggle_playback's target arm has no counterpart here: there is no
    // offset to add, so the start below is an already-formed frame and the
    // launch body's own playable gate is the whole position verdict.
    if (!playback.ensure_device_available_for_play()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             playback_device_refusal_card(playback));
        return;
    }
    // THE LOOP WINDOW IS THE ACTIVE DOMAIN'S TRIM — Viewport::trim_range, the
    // navigation range, the one owner of the window the car loops. Clamped
    // into the bound buffer's own domain as a belt: in source view the buffer
    // is the song and the clamp is an identity; in target view the buffer IS
    // the trim window (the preview covers it and nothing else), so the mapped
    // trim and the buffer's domain agree to the rounding of one map read, and
    // the clamp holds the window inside what can be played.
    auto [begin, end] = viewport.trim_range();
    if (begin < playback.domain_begin()) begin = playback.domain_begin();
    if (end > playback.domain_end())     end   = playback.domain_end();
    // THE START IS THE TRIM'S BEGIN, ALWAYS (architect 2026-09-17): the
    // resting playhead is no term of the car's play — the console's three
    // buttons place no playhead (Previous and Next seek the live play and
    // undo / redo at rest), so a restart is the play button itself: pause,
    // then play, from the top. NO LEAD-IN OFFSET (the declaration).
    // THE USER-LAUNCH CLEAR, owner (2) at GuiAuditionSequence: this entry is
    // the second road into the launch body that is not the act's, and it
    // clears ahead of the delegation, refused or not, exactly as
    // launch_playback_from does. (A standing act is already ended — the
    // fork above ends it, a rest being transport-live — so this reaches the
    // sub-tick window alone, as the view-end entry's clear does.)
    clear_audition_sequence(app);
    // THE CAR'S LOOP IS NEVER RESTRICTED (restrictable_play's exemptions):
    // it starts off screen by design and loops forever.
    restrictable_play = false;
    // A trim under two frames refuses inside the body — playback_launch_
    // playable on the begin — and the publish's own loop belt refuses the same
    // window one layer down; both silent, the benign one-dimensional class.
    //
    // THE CAMERA IS THE FOLLOW LAMP'S (architect 2026-09-18; the ruling at
    // the declaration): this is the one launch in the product that may start
    // OFF SCREEN by design, the trim's begin being a fixed point while the
    // passage under work sits screens downstream of it, so the camera is
    // the user's to ask for — lit, the launch takes the follow scroll
    // (Viewport::follow_scanner) and the pre-paint advance keeps it the
    // instant it succeeds (AppState::follow).
    // THE USER'S PLAY'S START, the display keep-alive's stamp (the two
    // stamps' contract at the declaration): a launch that ran.
    if (launch_playback_window(begin, end, begin,
                               app.follow ? LaunchCamera::Follow
                                          : LaunchCamera::Leave))
        user_play_launched_ms = monotonic_ms();
}

// IS A FRAME PAST THE PAINTED WINDOW'S RIGHT EDGE — RESTRICT'S STOP asked of
// a seek's landing (architect 2026-10-29 for the stop, 2026-11-20 for the
// seek): the column displayed_column_at's one rounding gives the frame, at
// or past the waveform's width, on the ITEM basis (on screen is as painted:
// item_viewport_basis, launch_frame_on_painted_window's basis above, the
// mirror of what the last frame painted). The pre-paint stop asks the same
// column of the scanner's drawn place on the plate basis it paints through;
// a landing is a frame the scanner has not reached yet, so the item basis,
// as the start gate's, answers it. A waveform with no columns has no edge.
static bool frame_past_painted_window_right(const AppState& a,
                                            const GuiAudio& audio,
                                            int64_t frame) {
    const ItemViewportBasis b = item_viewport_basis(a, audio);
    if (b.spp <= 0.0 || b.area_w <= 0) return false;
    return displayed_column_at(static_cast<double>(frame), b.vp_start, b.spp,
                               b.lattice_origin) >= b.area_w;
}

// THE CONSOLE'S SEEK (contract, the walls and his words at the declaration;
// architect 2026-11-20).
//
// THE SPLICE. The seek is ONE NEW PUBLISH over the session that is sounding
// — play() or play_loop() with the launch's own end and loop target and the
// new start, the render player's live seek's road (GuiRenderPlayer::seek_to)
// — and that is what keeps it clean, by the engine's code: the stream is
// never stopped or restarted (launch_window makes no device call on a
// running stream, playback_aaudio.cpp), stop()'s fence is not taken, and no
// silence is written between the two runs — a fill in flight finishes its
// block of the old run under the old window, and the very next callback's
// gate acquires the new generation and seats its packet at the target
// (playback_render_block's seat), so the output steps from the old read
// position to the new one between two adjacent output frames (save where
// the old once-through run met its own end inside that last block, whose
// tail is then the natural end's silence, as at any end). THE SEAM IS
// THE RAW CUT the loop's own wrap takes (the render body's "clicks accepted"
// ruling) — no fade or ramp, the declick machinery having been considered
// and rejected by ruling (the launch body's playable gate's record).
//
// THE RACES WITH THE AUDIO THREAD, by the command packet's ordering: the
// publish writes a new generation, so a fill in flight at the publish keeps
// its own window to the end of its block. Its LOOP WRAP there is a real wrap
// of what sounded: the count moves, the tick consumes it — its resync finds
// the old generation's stamp and leaves the publish's anchor standing, or
// the new generation's and anchors on it, the floor left at the target
// (playback_resync_predictor lowers it only on a stamp of the new session
// standing below its start) — and its damage and console push cost one
// repaint and one push more. Its NATURAL END there abandons its
// terminal (the word changed under it) and the new run seats; a natural end
// whose terminal landed AFTER this body's is_playing() read and BEFORE the
// publish is overwritten by the publish's new playing word, so the session
// plays on from the target — what the press asked of a play that was still
// sounding when it was pressed. A natural end BEFORE the read answers
// nothing playing and the seek is silent; the tick tears the scanner down.
bool GuiPlaybackLifecycle::seek_live_session_by(int64_t delta_frames) {
    if (delta_frames == 0) return false;
    // A STANDING A/B AUDITION IS NO SEEK'S (the caller drops it; a belt),
    // and nothing playing — the natural end in flight, a dead stream — has
    // no cursor to move.
    if (audition_sequence_standing(app)) return false;
    if (!app.playhead_scanner_active || !playback.is_playing()) return false;
    const LiveWindow w = live_window_;
    // A DEGENERATE OR CROSSED TRIM IS SILENT (Viewport::trim_range clamps
    // each side and does not order them): the walls below read it.
    const auto [trim_begin, trim_end] = viewport.trim_range();
    if (trim_end - trim_begin <= 0) return false;
    // THE ONE POSITION LINE (GuiPlayback::cursor, the predictor at `now`):
    // where the play stands, the place the console's clock and the scanner
    // read.
    const int64_t cur = playback.cursor();
    int64_t target = 0;
    if (w.loop_begin != kPlaybackNoLoop) {
        // THE LOOP. Forward past the end wraps by the loop's own arithmetic,
        // the overshoot carried past the target (the render body subtracts
        // the window's length until the read lies inside it — the remainder
        // here, so a window shorter than the step wraps as often as the
        // loop would); back stops at the target. The window holds at least
        // two frames (the publish's belt refused any shorter loop).
        const int64_t span = w.end - w.loop_begin;
        if (span < 2) return false;
        if (delta_frames > 0) {
            target = cur + delta_frames;
            if (target >= w.end)
                target = w.loop_begin + (target - w.loop_begin) % span;
        } else {
            target = std::max(cur + delta_frames, w.loop_begin);
        }
    } else {
        // ONCE THROUGH. Back to the lower wall: the trim's begin for a
        // session that began inside the trim, else the session's own start.
        const bool began_in_trim = w.start >= trim_begin && w.start < trim_end;
        const int64_t wall = began_in_trim ? trim_begin : w.start;
        if (delta_frames > 0) {
            target = cur + delta_frames;
            // PAST THE WINDOW'S END — fewer than two frames left, the launch
            // body's remainder rule — THE PLAY RUNS ON TO ITS OWN END.
            if (target >= w.end - 1) return false;
            // AND PAST RESTRICT'S EDGE, a restrictable play's own end.
            if (app.restrict && restrictable_play &&
                frame_past_painted_window_right(app, audio, target))
                return false;
        } else {
            target = std::max(cur + delta_frames, wall);
        }
    }
    // The bound buffer's own domain, a belt (both walls lie inside it).
    target = std::max(target, playback.domain_begin());
    if (target == cur) return false;
    // THE SCANNER'S OLD PLACE, erased where the last paint drew it, AHEAD OF
    // THE SEED (the launch body's own shape, Viewport::
    // invalidate_standing_scanner on the plate basis).
    viewport.invalidate_standing_scanner();
    // THE LIMITER LAMP'S SPAN IS VOIDED (AppState::limiter_lamp_prev_frame):
    // the play stands across the jump, so the tick would test the frames it
    // skipped as heard — the render player's live seek's line.
    app.limiter_lamp_prev_frame = -1;
    if (w.loop_begin == kPlaybackNoLoop) {
        playback.play(target, w.end);
    } else {
        playback.play_loop(target, w.end, w.loop_begin);
    }
    // THE SCANNER JUMPS WITH THE CURSOR: the publish anchored the predictor
    // at (target, the publish instant) — the jump's re-anchor (playback.h's
    // design note) — and the seed below is the launch body's, so the next
    // paint draws the line at the target, and the pre-paint advance moves it
    // on from there (its old place the seeded one). Its new place and the
    // clock cell are declared here, as the launch's success tail does; a
    // FOLLOW scroll the next pre-paint takes carries its own whole-area
    // damage. THAT PRE-PAINT RUNS ITS WHOLE ADVANCE even where the frame's
    // position equals the seed (seek_frame_owed, the field's comment): the
    // seek's own frame takes FOLLOW's step.
    app.playhead_scanner_sample  = target;
    app.playhead_scanner_precise = static_cast<double>(target);
    seek_frame_owed = true;
    viewport.invalidate_standing_scanner();
    viewport.invalidate_clock_area();
    return true;
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
    // THE AUDITION IGNORES THE FOLLOW LAMP (architect 2026-09-11;
    // AppState::follow): its four bounded plays are each framed by their own
    // `c`, so a scroll would only fight that framing. The pre-paint follow
    // scroll asks the act's phase and moves for none of its plays.
    const int64_t view_end = active_view_play_end();
    if (start >= view_end - 1) return false;
    const int64_t end = std::min(start + span, view_end);
    // NO SEQUENCE CLEAR ON THIS ROAD (2026-09-01): this entry is the A/B
    // audition's alone (one caller, GuiAbAudition::launch_phase), which
    // arrives with the phase it is launching ALREADY WRITTEN. The clear that
    // every user launch takes is the two user entries' (launch_playback_from
    // and car_toggle_playback, 2026-09-17), the roads into the body
    // that are not the act's.
    // THE AUDITION IS NEVER RESTRICTED (restrictable_play's exemptions): each
    // play is framed by its own `c` and ends at its own span.
    restrictable_play = false;
    // NOR DOES IT ARM THE DISPLAY KEEP-ALIVE (the two stamps' contract at
    // the declaration): its rests keep the panel awake on their own road
    // (main.cpp's "A REST KEEPS THE PAINT CLOCK RUNNING").
    user_play_launched_ms = -1;
    // PageIn: each of the act's four plays is framed by its own `c`
    // (GuiAbAudition), so the start is centred by construction and the term
    // is a no-op here — stated, not defaulted, because the body takes no
    // default (the enum's contract at the declaration).
    return launch_playback_window(start, end, kPlaybackNoLoop,
                                  LaunchCamera::PageIn);
}

// THE ONE LAUNCH BODY: validate `start` — an ABSOLUTE position in the active
// PAINT domain — against the active view's window, seed the scanner, and play
// [start, end), once or looping to `loop_begin` (the parameter's contract at
// the declaration). Returns whether it launched; its two refusals — the dead
// device first (a card, 2026-08-30), then the launch position (silent, the
// rule below). Three callers: the view-end launch above
// (Space's play edge and the scrub, `end` = the view's end), the bounded
// audition (`end` = start + span, clamped) and the car's launch
// (car_toggle_playback, `end` = the trim's end and `loop_begin` its begin,
// 2026-09-17). `camera` is each caller's own word and takes no default —
// who says which is at the enum's declaration (architect 2026-09-18, the
// caller's-word rule; the enum's contract at the declaration). This
// body never writes the
// resting cursor — the scanner is the only playhead it touches, so a launch
// is a pure scanner event and the cursor is untouched by construction.
//
// Target-view branch: the audio device is bound to app.target_buffer
// (rebound by GuiTargetRender's completion path on Success, with the
// buffer's domain offset travelling with the bind). The playhead in
// target view is a full-target-frame coordinate, and playback's whole
// public API speaks the bound buffer's domain (playback.h), so the
// bounds pass straight through: [domain_begin(), domain_end()) is the
// target buffer's full-target-frame extent and play() takes the
// validated launch position unchanged.
// Every view plays at the source's own rate (architect 2026-08-27): there is
// no speed multiplier, so target view hears the authored warp alone.
bool GuiPlaybackLifecycle::launch_playback_window(int64_t start, int64_t end,
                                                  int64_t loop_begin,
                                                  LaunchCamera camera) {
    // THE A/B AUDITION IS NAMED BY ITS STATE HERE, NOT BY ITS ENTRY (architect
    // 2026-09-01): this body clears no sequence. A user launch arrives with
    // the sequence already Idle — the view-end entry launch_playback_from
    // clears it ahead of its delegation — and the act's own launch arrives
    // with the phase it is launching ALREADY STANDING
    // (GuiAbAudition::launch_phase writes it before calling
    // launch_bounded_audition and clears it again on a false return), so
    // `phase != Idle` at this body means exactly "this is one of the act's
    // four plays". Nothing between that write and this body paints
    // or dispatches, so the phase standing one call earlier changes no face
    // and no fork (redesign_button_glyph_swapped and toggle_playback read it
    // at their own times). The edge inventory is
    // at GuiAuditionSequence (app_state.h).
    // Both `start` (playback.play()'s launch bound) and the scanner's
    // launch position below are in the active PAINT domain
    // (full-target-frame in target view; source-frame otherwise)
    // — playback's API takes domain coordinates, so the same value
    // serves both, and the launch's camera (follow_scroll_if_needed,
    // follow_scanner) compares the scanner against the full-domain viewport
    // with no wrong-domain leak.
    //
    // NO LOOPING ON ANY GUI ROAD (architect 2026-07-30, "looping behavior is
    // not that useful. ok to remove all looping", standing for every launch
    // the GUI itself makes): EVERY audition plays once from `start` to `end`
    // and stops there — `end` is the view's end for Space and the scrub
    // and `start + kAuditionMs` for the bounded audition, a different end,
    // the same once-to-its-end play. THE ONE ROAD THAT PASSES A LOOP TARGET
    // IS THE CAR'S (architect 2026-09-17, car_toggle_playback — the head
    // unit's play with the render player closed loops the trim forever):
    // `loop_begin`
    // >= 0 takes the engine's looping face, whose audio-callback wrap, wrap
    // counter and predictor resync live in the packet engine
    // (playback_common.h); kPlaybackNoLoop, every other caller, takes play().
    //
    // EVERY REFUSAL IS THE ONE PREDICATE (playback_launch_playable,
    // app_state.h; its other readers the A/B audition's press-time gate and
    // the PLAY button's face under the truthful-buttons ruling, 2026-08-30,
    // through space_launch_would_play above, which asks it about the launch
    // position the act would actually form, the lead-in offset included).
    // The per-arm reasoning is at the predicate whole: the target arm's
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
    // A DEAD OR ABSENT DEVICE IS REOPENED FIRST, AND A FAILED REOPEN SAYS SO
    // (asked 2026-08-30; a REOPEN, architect 2026-09-02): the
    // launch below would "succeed" — the scanner would seed, the page-in would
    // scroll and play() would return — with nothing coming out of the
    // machine, which is the one refusal the user cannot see for himself. The
    // question is GuiPlayback::ensure_device_available_for_play's: it closes
    // a dead stream and reopens it at the press (play()'s own head check,
    // hoisted — a route drop is undone by the next press instead of carding
    // forever behind a read), and it answers false only when nothing can
    // sound AFTER the reopen, which is
    // the same fact the render player's tick reads through device_unavailable
    // — no bit is added and nothing is latched here. The load's own stderr
    // line stays, but it is printed once at startup and this is every press
    // after it. Ahead of the playable gate so the sentence names the device
    // rather than the position — and for the same reason THE TWO PRE-LAUNCH
    // GATES ASK IT AHEAD OF THIS ONE (2026-08-30: toggle_playback's target
    // pre-sum gate and GuiAbAudition::start's preflight, both of which decide
    // something — a position card, a camera and a tab switch — before control
    // could arrive here). This check is the BELT they leave standing: it is
    // the only gate on the scrub's launch, which has no outer road at all —
    // so the scrub's reopen is this one — and each of the three returns, so
    // exactly one card is raised per press (behind a gate that reopened, this
    // reopen finds the stream standing and is a no-op). The sentence is the
    // chooser's (playback_device_refusal_card, at the header for all three):
    // kPlaybackDeviceUnavailableCard, or the pending device's where the
    // reopen overran its bound or met a device job in flight.
    if (!playback.ensure_device_available_for_play()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             playback_device_refusal_card(playback));
        return false;
    }
    // AND A LAUNCH POSITION WITH NOTHING LEFT REFUSES IN SILENCE (architect
    // 2026-08-31; the rule is at the file head).
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
    // A SCANNER STILL STANDING IS ERASED WHERE IT WAS PAINTED, AHEAD OF THE
    // SEED (architect 2026-11-16; Viewport::
    // invalidate_standing_scanner): a launch inside the sub-tick window
    // between the audio thread's natural end and the tick that tears the
    // scanner down finds the bit up, and its place — read before the seed
    // overwrites playhead_scanner_precise — is the one the last paint drew.
    if (app.playhead_scanner_active) viewport.invalidate_standing_scanner();
    // Scanner launch = the validated launch position, in the paint domain
    // in every view (see the comment above `start`). Seed the continuous
    // position too so the first paint (where the predictor's cur still equals
    // start and the pre-paint hook early-returns) draws at the launch column
    // rather than a stale precise value.
    app.playhead_scanner_sample = start;
    app.playhead_scanner_precise = static_cast<double>(start);
    app.playhead_scanner_active = true;
    // EVERY PLAY STARTS UNSUSPENDED (AppState::follow_suspended): a camera
    // change during the play before this one suspended that play's
    // following, not this one's. Ahead of the camera below, which keeps it
    // as it stands.
    app.follow_suspended = false;
    // THE CAMERA IS THE CALLER'S WORD (architect 2026-09-18; the enum's
    // contract at the declaration). Where the caller says PageIn and the
    // launch position is offscreen, left-edge-align the viewport on it before
    // the scanner issues forth — follow's own page-in has exactly the right
    // shape for it. EVERY GUI ROAD WITH FOLLOW DARK SAYS PageIn AND STARTS
    // ON SCREEN ANYWAY: Space launches from the cursor, the scrub from a
    // column the user just clicked, the A/B audition's plays from a half its
    // own `c` has centred — so the page-in there is a rescue for a cursor a
    // pan has carried offscreen and a no-op otherwise. WITH FOLLOW LIT the
    // two user entries say Follow: THE FOLLOW SCROLL at the launch
    // (Viewport::follow_scanner, the rule's one owner; architect 2026-11-15),
    // a launch position right of the center centered at once. THE
    // CAR'S PLAY IS THE ONE LAUNCH THAT STARTS OFF SCREEN BY DESIGN: its
    // position is the trim's BEGIN, a fixed point the worked passage sits
    // screens downstream of, so it says Leave unless the follow lamp is lit,
    // and the camera stays where the user left it.
    if (camera == LaunchCamera::PageIn) viewport.follow_scroll_if_needed();
    if (camera == LaunchCamera::Follow) viewport.follow_scanner();
    // Damage the scanner's seeded place and the clock cell NOW, in the success
    // tail (strictly after every refusal return above). A launch's visible
    // effect — the scanner line appearing at the launch column and the
    // timestamp readout advancing — otherwise waits for the next tick-driven
    // paint opportunity (the tick heartbeat invalidates the scanner's current
    // place even when the integer predictor has not yet advanced), leaving
    // the PRESS itself damage-less for a whole frame. Damaging at press paints
    // the scanner one frame earlier than the tick heartbeat would — honest
    // damage at the press that caused it.
    //
    // THE SCANNER'S OWN RECT, AFTER THE CAMERA (architect 2026-11-16:
    // "number 3, narrowing agree"; Viewport::invalidate_standing_scanner),
    // not the whole waveform area, which repaints the viewport's lanes down
    // to the waveform's foot and owes the rows above them a check
    // (damage_window_top_rows), the chrome redrawn for pixels that did not
    // change; the GPU draws the frame whole either way (GuiPlatform::present).
    // ON THE PLATE
    // BASIS, the one the scanner is painted through, so it names the pixels
    // even while a plate is in flight (live viewport new, plate old), whose
    // publication damages the viewport's lanes whole itself. A camera that
    // moved (the page-in or the follow scroll above) took its own whole-area
    // damage and synchronous rebuild, so the launch is wide exactly when the
    // camera moved; an offscreen place is an empty rect and declares nothing.
    viewport.invalidate_standing_scanner();
    viewport.invalidate_clock_area();
    // THE LIVE SESSION'S WINDOW, the console's seek's record (live_window_'s
    // contract at the declaration).
    live_window_ = LiveWindow{start, end, loop_begin};
    if (loop_begin == kPlaybackNoLoop) {
        playback.play(start, end);
    } else {
        playback.play_loop(start, end, loop_begin);
    }
    // N2's diagnostic (the record at n2_launch_count, playback_lifecycle.h).
    ++n2_launch_count;
    return true;
}

// The follow key's one chokepoint (contract at the header declaration),
// shared by bare `f` and the icon-row button that synthesizes that chord.
void GuiPlaybackLifecycle::toggle_follow() {
    app.follow = !app.follow;
    if (!app.follow) return;
    // THE RADIO (architect 2026-10-29, AppState::follow): FOLLOW lit puts
    // RESTRICT out — with both lit the scroll below would carry the window
    // along before RESTRICT's stop at the right edge could fire.
    app.restrict = false;
    // THE LIT EDGE RESUMES THE SCROLL AT ONCE: whatever suspended the play
    // in flight is forgiven, and during a play the predictor re-anchors (as
    // every discrete camera move does) and the follow scroll runs
    // (Viewport::follow_scanner, architect 2026-11-15): a scanner right of the
    // center is placed on it, one off screen to the left paged in, one left
    // of the center left to run. At rest the scroll is skipped: the next
    // launch places the camera for itself. The A/B audition's plays are not
    // followed (the pre-paint scroll asks its phase), so the lamp lit during
    // the act waits for the user's next play too.
    app.follow_suspended = false;
    if (playback.is_playing() && !audition_sequence_standing(app)) {
        playback.resync_predictor();
        viewport.follow_scanner();
    }
}

// The release key's one chokepoint (contract at the header declaration).
void GuiPlaybackLifecycle::toggle_device_release() {
    if (playback.device_released()) {
        (void)playback.end_device_release(GuiDeviceReleaseEnd::SecondPress);
    } else {
        stop_playback_if_playing();
        playback.release_device();
    }
    // THE PRESS WHOSE DEVICE CALL DID NOT SETTLE WITHIN THE BOUND SAYS SO
    // (architect 2026-11-26; the card block at the head of
    // playback_lifecycle.h): the dark press's close or the second press's
    // reopen left the device pending — or the press met a job already in
    // flight, under which neither act attempts anything.
    if (playback.device_pending())
        notifications.notify(AppState::NotificationClass::Normal,
                             kPlaybackDeviceNotAnsweringCard);
}

// The return's watch (contract at the header declaration; a gain that meets
// a device job in flight consumes its loss and ends nothing, the
// declaration's last paragraph).
void GuiPlaybackLifecycle::note_window_activation(bool active) {
    if (!active) {
        left_while_released_ = playback.device_released();
        return;
    }
    const bool ends = left_while_released_ && playback.device_released();
    left_while_released_ = false;
    if (ends) (void)playback.end_device_release(GuiDeviceReleaseEnd::Return);
}

// The restrict key's one chokepoint (contract at the header declaration),
// shared by bare `r` and the icon-row button that synthesizes that chord.
void GuiPlaybackLifecycle::toggle_restrict() {
    app.restrict = !app.restrict;
    if (!app.restrict) return;
    // THE RADIO (architect 2026-10-29, AppState::restrict): RESTRICT lit puts
    // FOLLOW out, so the follow scroll stops moving for the play in flight
    // and the tick's stop at the right edge governs it from the next frame.
    app.follow = false;
}

