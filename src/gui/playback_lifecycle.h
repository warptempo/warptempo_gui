#pragma once

#include "app_state.h"
#include "audio.h"
#include "notifications.h"
#include "playback.h"
#include "viewport.h"

// Playback-orchestration operations, extracted from main.cpp's inline lambdas.
// Owns the GUI-level wrappers around GuiPlayback's mechanism: the one stop body
// (both stop edges and every gesture stop), the modal-open stop that names it,
// toggle play/stop, the audition launch, the bounded audition the A/B sequence
// plays (its sequencing is GuiAbAudition's, ab_audition.h — this cluster owns
// the one play), the keep-alive reseek and follow mode.
// AppState, Viewport and GuiAudio are captured directly.
// GuiPlayback stays a pure mechanism class — these operations live one layer up.
// (No GuiPlatform& member. The only direct platform reach this cluster ever had
// was restore_playhead_to_lsp's top-strip invalidate, deleted with that function
// 2026-07-30; every damage this cluster emits now goes through Viewport.)
struct GuiRenderPlayer;

// THE DEAD DEVICE'S SENTENCE, ONE SPELLING AND FOUR SITES (2026-08-30; the
// fourth joined 2026-09-04): the one launch body's own device gate — the BELT,
// which every road that plays passes — and the TWO PRE-LAUNCH GATES that decide
// something before reaching it and so must ask the same question first:
// toggle_playback's target-view
// pre-sum position gate (which would otherwise answer a dead device with the
// POSITION sentence) and GuiAbAudition::start's press-time preflight (which
// would otherwise run `c` and switch tabs before the belt refused). Only one of
// the three fires per press — each returns — so the shared literal is what
// keeps one fact one sentence.
// THE FOURTH SITE IS NOT A PRESS AT ALL and is the one exception to the
// launch-road reading below: GuiAbAudition::fire_if_due, the A/B audition's
// tick, which ends the whole act on device_unavailable and raises this same
// sentence (architect 2026-09-04). It reads rather than reopens, being a tick,
// and it says what the three launch sites say because a device lost mid-act
// and a device that was never there are one fact to the user. It cannot double
// with them: it returns, and the stop body it takes leaves the act Idle, so no
// launch of the act's follows it.
// It lives here rather than in notifications.h
// because the fact is this cluster's: the question is GuiPlayback::
// ensure_device_available_for_play's — THE REOPEN AT THE PRESS (architect
// 2026-09-02): each of the three PRESS sites reopens a dead AAudio stream
// before it asks, and cards only when that reopen failed, JACK answering its
// unchanged read. The reads: the TWO TICKS keep device_unavailable — the
// render player's (a dead stream mid-play must pause) and the A/B audition's
// (a dead stream mid-act must end the act, the fourth site above) — and the
// two PLAY-face predicates (space_launch_would_play,
// ab_audition_preflight_ok) read device_absent, the never-came-up half alone
// — a face is a read, never a reopen, and it must not grey what the press
// would reopen.
inline constexpr const char* kPlaybackDeviceUnavailableCard =
    "Playback is unavailable on this device";

struct GuiPlaybackLifecycle {
    AppState&         app;
    const GuiAudio&   audio;
    GuiPlayback&      playback;
    Viewport&         viewport;
    // THE CARD (2026-08-30, the strictness ruling; ONE sentence since
    // 2026-08-31): the ONE LAUNCH BODY is where a play that will not sound is
    // discovered, and the DEVICE half is what it says — a dead or absent
    // device is the one refusal the screen cannot show for itself. Its former
    // companion, the launch position with nothing left to play from, went
    // SILENT again on 2026-08-31 (a benign one-dimensional refusal already at
    // its state; the record is at the file head).
    GuiNotifications& notifications;
    // THE RENDER PLAYER'S BACK-POINTER (2026-08-28, the car's state push),
    // wired in main.cpp once both exist — the settings editor's and the
    // prompt's own shape, since the player holds this cluster and is built
    // after it. ONE READER: the stop body's player fork, which is the one
    // place every player stop passes and so the one place the head unit's
    // "paused" is published from (the inventory is at
    // GuiRenderPlayer::publish_media_state). Null until wired; the fork tests
    // it.
    GuiRenderPlayer*  render_player = nullptr;

    GuiPlaybackLifecycle(AppState&         app_,
                         const GuiAudio&   audio_,
                         GuiPlayback&      playback_,
                         Viewport&         viewport_,
                         GuiNotifications& notifications_)
        : app(app_),
          audio(audio_),
          playback(playback_),
          viewport(viewport_),
          notifications(notifications_) {}

    // THE GESTURE STOP (teardown contract at the definition).
    // THE KEYBOARD STOP RULE (architect 2026-07-30) — the authoritative statement
    // of WHICH keyboard commands stop a live audition. Other sites state their own
    // class plus a pointer here; no site re-enumerates the commands.
    //   * COLLAPSE-TO-POINT COMMANDS STOP: a command whose act collapses the
    //     selection to its point form takes the playhead with it, so it stops —
    //     both position nudges (the collapse to point form IS the reason they
    //     stop; both pay it once, in the shared prologue past its refusal
    //     verdict — the singleton's wall included — and ahead of the first
    //     write)
    //     and `c`. The S/T switch `t` stops on its own standing ruling, the audio
    //     domain flipping under the running session.
    //   * VALUE STEPS DO NOT STOP: the Up/Down value step on the addressed
    //     cell edits a value. Its tempo arm is GROUP-PRESERVING, leaving the
    //     selection and its span exactly as they stood, so the audition plays
    //     on under the edit; the bound arm collapses
    //     a group to its focus and stops nothing either. The step ladder's
    //     magnitude (bare one cent, three shifted, ten with ctrl) changes the
    //     number and not the class.
    //   * PURE VIEWPORT MOVES DO NOT STOP: bare `0`'s ZOOM-OUT ARM,
    //     PageUp/PageDown — they move the window onto the audio,
    //     not the audio. `0`'s OTHER arm, taken with the zoom already at full
    //     out, IS the `c` command (run_center_command) and stops exactly where
    //     `c` does — inside the land onto the focused stop, so only when one
    //     stands. One command, one answer; `0` adds no rule of its own.
    //   * TRIM MUTATIONS STOP, IN BOTH VIEWS: the sweep and Shift+[, matching every
    //     POINTER trim route (the endcap/bridge drags and the bound-set clicks each
    //     stop at their own commit point). BOTH views, and the rule is unchanged
    //     by the 2026-08-05 playback ungating — what narrowed is only the
    //     rationale's source-view half: a TARGET audition is still playing out the
    //     very window the mutation replaces, while a SOURCE one now runs to the
    //     song's end and never ran against the bounds being moved. The stop stays
    //     there because every trim write parks the playhead at the new trim start
    //     (input_trim.cpp), which is a cursor-moving command by any other name.
    // Every stop in the rule is REFUSAL-GATED (the standing 2026-07-28 rule): it
    // sits past its route's refusals and immediately ahead of that route's first
    // write, so a press that writes nothing stops nothing.
    // THE ARCHITECT'S CONTEXT, recorded because it is what makes the rule cheap:
    // "not stopping audio is not a big priority; audio is constantly being
    // relaunched to audition the impact of decisions."
    // The rule classifies the commands it names and claims nothing about the ones
    // it does not. The cursor-moving NAVIGATION stops (the arrows in every
    // magnitude, Home/End,
    // the Tab family) are older and broader than the rule — they hold under the
    // definition's own contract, a handler about to commit a new cursor position.
    void stop_playback_if_playing();

    // THE MODAL-OPEN PLAYBACK STOP, ONE OWNER (architect 2026-07-28, replacing
    // six hand-spelled stops). Called at the moment a modal surface ACTUALLY
    // opens. THE CALLER INVENTORY, re-derived by grep 2026-09-14 — NINE
    // sites: GuiSettingsEditor::open (settings_editor.cpp); in
    // input_key_dispatch.cpp the `h` view's `'` LOAD CONFIRMATION
    // (history_load_in_place — the history picker it replaced on 2026-08-29
    // was this caller before it), the `m`
    // bpm editor (handle_mode_keys), the history view's COMMIT-TITLE editor
    // (open_history_commit_editor), the OPEN PROJECT PICKER
    // (open_project_picker, 2026-08-27 as a prompt, field-less since
    // 2026-08-28) and the AV SYNC STATS PANEL (open_av_sync_stats); the TWO
    // prompt opens (prompt.cpp: unsaved, and File → Revert's confirmation —
    // the error notice's was a third until that prompt kind retired whole on
    // 2026-08-30); and THE RENDER PLAYER's open
    // (GuiRenderPlayer::open, render_player.cpp — the third modal owner,
    // 2026-08-28; the project's audition ends where the player's transport
    // begins, and the player's own stops all take stop_playback_if_playing
    // through the fork inside it). It had gone seven to six on 2026-08-09,
    // when the render-library advisory prompt was deleted with the whole
    // attestation surface.
    // IT WENT EIGHT TO SEVEN LATER THE SAME DAY: the settings editor's TWO doors
    // — the `;` key (input_handler.cpp) and the Settings DROPDOWN item
    // (input_pointer.cpp), the one route onto that surface that reached no key
    // gate — each carried their own call until the editor gained a READ-ONLY
    // refusal at its opener (a locked tab authors no engine settings), at which
    // point both stops moved INSIDE GuiSettingsEditor::open to sit past that
    // gate. Two callers became one, and the refusal-gating rule below is why: a
    // caller-side stop would have let the dropdown's now-refusable click kill an
    // audition and open nothing. The count did not move back when that
    // refusal left the opener on 2026-09-04 (the lock governs the keys now —
    // GuiSettingsEditor::open): one owner for the two doors is the shape to
    // keep whether or not the opener can refuse.
    // The count had held across the day's earlier change by coincidence: the
    // commit-title editor replaced the history commit confirmation, so one
    // caller left prompt.cpp as another arrived in input_key_dispatch.cpp.
    // TWO MODAL OPENS ARE NOT CALLERS, and each is a recorded exception rather
    // than a gap: the PASTE_CONFIRM prompt is built outside prompt.cpp
    // (PhaseResetPropagate::open_paste_confirmation, its one opener — the
    // magnification level paste was a second from 2026-09-15 until it stopped
    // asking a question on 2026-09-19) and stops through
    // stop_playback_if_playing directly, which is mechanically this same stop;
    // and the render player's LOAD_IN_PLACE_CONFIRM prompt
    // (GuiInputHandler::render_player_load_in_place) PAUSES the player's own
    // transport through GuiRenderPlayer::toggle_pause, which takes that same
    // body through the player's fork — a stop that must keep the resume point
    // the ordinary modal stop would not — so the RULE holds for every modal
    // surface even though this function's caller set is not literally every
    // opener.
    // Authoring or answering a dialog over a live audition is the wrong default,
    // and Space is inside each of those surfaces' blocked sets, so playback
    // cannot restart until the surface closes.
    // THE DECISION TABLE lives here, so a new modal surface inherits an ANSWER
    // instead of an absence:
    //   * DIALOG modal surfaces — the three dialog editors, the prompts, the
    //     picker and (since 2026-09-03) the AV SYNC STATS PANEL, all painted
    //     as the bottom row's modal since 2026-08-13 — STOP. The panel is a
    //     hardware reading rather than an authoring surface, so it was worth
    //     asking whether it owed the stop at all; it takes it because it is
    //     the picker's own shape one content over — a full-window band, a
    //     modal row and a keyboard vocabulary of its own — and because the
    //     LINE it measures against is behind that band, so a session left
    //     playing under it would be running with nothing to watch.
    //   * The TOP-STRIP FLAG EDITOR IS EXEMPT, and that is a DECISION, not an
    //     omission: modality there is CHORDS ONLY (the editor stays pointer- and
    //     wheel-transparent), and editing flag text while listening to the
    //     passage is a workflow the architect uses. Its open site
    //     (GuiFlagEditor::enter_top_flag_edit) carries a pointer back here.
    // REFUSAL-GATED (the standing rule): the stop is the price of an OPEN, so
    // each site calls this only once its own guards have passed — a refused open
    // must leave a listening session untouched. Do NOT hoist a call above a
    // guard ladder.
    // Mechanically this IS stop_playback_if_playing (a modal open needs no
    // teardown the gesture stop does not already do); the separate name is what
    // gives the rule and its one exemption a greppable home. The NON-modal stops
    // (gesture stops, the S/T toggle, the load-in-place mutator's self-guard)
    // keep
    // calling stop_playback_if_playing directly.
    void stop_playback_for_modal_open();

    // (restore_playhead_to_lsp is GONE, architect 2026-07-30 — do not
    // reintroduce it. It named a snap-back that had stopped existing: a stopped
    // scanner is deactivated IMMEDIATELY, its value fields stale by contract, so
    // nothing was ever restored, and the 2026-07-29 deletion of the natural-end
    // follow-scroll tail left it a strict subset of stop_playback_if_playing plus
    // one dead top-strip invalidate — dead because the scanner paints no
    // top-strip pixel and a stop moves no cursor. Its two callers, Space's stop
    // edge and the tick's natural-end branch, now call the gesture stop, so the
    // product has ONE stop body.)
    // launch_offset shifts the SCANNER's launch position (and the play() launch
    // bound) forward in the active paint domain WITHOUT moving the resting
    // cursor, so stop just deactivates the scanner and the cursor is unmoved. Non-zero
    // only for the target-view lead-in audition Space performs when the
    // phase-reset overlay has a subject (start from cursor + N/2);
    // the default 0 keeps plain Space and every other caller byte-identical.
    // The offset is applied only in the target-view branch; the offset launch
    // is re-validated against the target buffer's domain, so an offset landing
    // at or past the buffer end is a SILENT no-op (the pre-sum gate, whose
    // verdict is the launch body's own — architect 2026-08-31, retiring the
    // 2026-08-30 card "There is nothing left to play from here": a benign
    // one-dimensional refusal already at its state says nothing, the playhead
    // resting visibly at the view's end and the Play button greying on that
    // very predicate. The rule is at the head of playback_lifecycle.cpp).
    // THE FORK IS "TRANSPORT-LIVE", NOT playback.is_playing(): a REST of the A/B
    // audition takes the stop arm too, the act being one transport session from
    // its first play to its last (the ruling and its face argument at the
    // definition).
    void toggle_playback(int64_t launch_offset = 0);

    // THE CAR'S SPACE (architect 2026-09-17): the head unit's play/pause with
    // the render player CLOSED, reached from GuiCarTransport::car_toggle and
    // nowhere else. It is THE FORK AND THE PLAY BODY BELOW, split 2026-09-18
    // when the console's skips gained a play tail of their own (Previous and
    // Next undo and redo AND THEN PLAY, car_transport.h): THAT TAIL WANTS THE
    // PLAY ALONE, never the fork. Hearing the state it just stepped to IS the
    // act, so the sound is owed unconditionally; the fork would make it
    // conditional on a transport bit the tail has no business re-deriving —
    // the restore it just ran is what silenced the transport (the restore
    // body's own stop), and any arm of that fork but the play is the feature
    // failing to make a sound. STOP ARM: exactly toggle_playback's — the same transport-
    // live term (a live project play OR a standing A/B audition, a rest of the
    // act stopping as a play does) through the one stop body, and PAUSE IS THE
    // GUI'S STOP: no pause semantics, no resume point — the console's pause
    // runs stop_playback_if_playing as Space's stop does, because the car's
    // transport is to be as close to the GUI's as the car allows and, with
    // the trim narrowed to what he is working on, he goes back to the
    // beginning anyway. PLAY ARM: the window is the ACTIVE DOMAIN'S TRIM,
    // Viewport::trim_range — the navigation range (in target view the trim
    // mapped through the live map, the full window normalized to the whole
    // domain) — and THE PLAY LOOPS IT FOREVER, in every view (his one
    // difference from the GUI's transport), STARTING AT THE TRIM'S BEGIN
    // ALWAYS (architect 2026-09-17): the resting playhead is no term of it,
    // and a restart is the button itself — pause, then play. NO LEAD-IN OFFSET: the phase-reset overlay's N/2 is
    // Space's authoring aid, and the car's play takes none. The gates are
    // toggle_playback's own — the defensive chase clear, the device reopen
    // (carding a failed one) — ahead of the user-launch clear (owner (2) at
    // GuiAuditionSequence: this is the second road into the launch body that
    // is not the act's), the launch through launch_playback_window with the
    // trim's begin as the loop target, and on success the follow lamp's spend
    // (spend_follow_lamp — the car's play IS the next project-audio launch).
    // THE CAMERA STAYS WHERE IT IS UNLESS FOLLOW IS ARMED (architect
    // 2026-09-18): this is the ONE launch that may start OFF SCREEN — the
    // trim's begin is a fixed point and the passage he is working on is
    // habitually a couple of screens downstream of it, so a page-in on every
    // console press cost him a manual recentre per audition. The launch
    // states `LaunchCamera::PageIn` iff app.follow_armed READ AT THE LAUNCH
    // LINE (the spend runs in the success tail behind it, so the lamp still
    // carries the user's arm there) and `LaunchCamera::Leave` otherwise —
    // with the lamp armed, the spend hands the chase the paging it always
    // had; with it dark nothing moves, at the launch or at any loop wrap
    // (the wrap's consumer resyncs the predictor and damages, and writes no
    // camera — main.cpp's tick).
    // A trim under two frames refuses in the body's own playable gate,
    // silently (the benign one-dimensional class: the playhead and the grey
    // say it). The target view's preview-readiness gate is THE CALLER'S on
    // both roads in, as it is on_key's for Space — car_toggle asks it as
    // Space's edge does, and the skips' tail asks it and WAITS on it (the
    // pending car play, car_transport.h).
    //
    // THE LOOP IS A PROPERTY OF THIS LAUNCH, NOT A LAMP: any GUI act that
    // stops (Space, Home / End, a marker touch, a modal open, the S/T flip)
    // or relaunches (the placement click's reseek_keeping_alive, which calls
    // play() once-through; a scrub) ends it, and every GUI launch plays once
    // as it always has. The head unit's Previous / Next are Undo / Redo whole
    // (GuiCarTransport), so a restore stops the loop exactly as Ctrl+Z does
    // (the restore body's own stop) — and since 2026-09-18 a restore that RAN
    // relaunches it here, from the trim's begin again, while a refused step
    // leaves a running loop running.
    void car_toggle_playback();
    // THE CAR'S PLAY, WITHOUT THE FORK (architect 2026-09-18): the toggle
    // above past its stop arm, the whole of it — the defensive chase clear,
    // the device reopen and its card, the trim window and its two clamps, the
    // always-from-the-begin start, the user-launch clear, the looping launch
    // with the camera term, the follow lamp's spend. TWO CALLERS, both the
    // console's: car_toggle_playback's play arm, and the skips' play tail
    // (GuiCarTransport::car_play_after_step — directly, and again from the
    // pending play the tick fires), which wants the play and not the fork for
    // the reason stated above. It asks no transport bit of its own: a live
    // session is the CALLER'S to end, the toggle's stop arm on one road and
    // the restore's own stop body on the other.
    void car_play_playback();
    // THE AUDITION LAUNCH ENTRY: launch the scanner from `frame`, an ABSOLUTE
    // position in the active paint domain, leaving the resting cursor untouched.
    // ONE CALLER CLASS since 2026-07-30 — the waveform SCRUB act (the
    // START half of its stop-then-start), which is also the gesture for
    // previewing a SHOWN trim region overlay: click inside it and it auditions
    // from there, the overlay left standing. That act has ONE entry since 2026-08-13 (the lower-half plain
    // press's MOTIONLESS RELEASE — its press-time dispatch moved to the lift
    // when the waveform's two halves became one surface, and the bare right
    // full-height entry died 2026-08-12 with the right button's unbinding),
    // funneled through scrub_act_at, so this stays one caller class. (Space's region left-bound launch was the second caller until the
    // architect dropped it 2026-07-30; Space now always toggles from the playhead.)
    // Delegates
    // to the same launch body as toggle_playback's play edge, so the standing
    // gates apply identically: a frame outside the active view's range — the
    // SONG in source view, the target buffer's domain in target view — or one
    // leaving fewer than two playable frames of remainder, is a SILENT no-op
    // (the launch body's position gate, whose card retired 2026-08-31) —
    // exactly Space's conventions,
    // this being the one launch road with no outer gate of its own. A DEAD OR
    // ABSENT DEVICE still cards there, being the one refusal the screen cannot
    // show. A live session never
    // launches (defensive; the caller reaches here only with
    // playback stopped — a scrub act over a live session STOPS it and returns).
    void scrub_launch_at(int64_t frame);
    // THE BOUNDED AUDITION (architect 2026-08-26), the A/B audition's play:
    // launch the scanner from `start` — an ABSOLUTE position in the active
    // paint domain — and play `span` frames, the session's end being
    // `start + span` clamped to the active view's own end (the song's in
    // source view, the bound preview buffer's in target) rather than that end
    // itself. NOTHING LOOPS still — the rule has exactly TWO sanctioned
    // exceptions, the render player's REPEAT ONE lamp (2026-08-28,
    // render_player.h, which reaches no project audio) and the CAR'S LOOP OF
    // THE TRIM (2026-09-17, car_toggle_playback above, which reaches this
    // project's audio through a launch of its own) — and NEITHER IS THIS
    // BODY'S: this is a discrete play to ITS end, the
    // natural-end teardown is its one terminal, and the resting cursor is
    // untouched exactly as under Space — the same launch body, the same
    // gates (playback_launch_playable, so a start at or past the domain end
    // or leaving fewer than two frames refuses), the same follow behaviour,
    // the same scanner. Returns whether it launched; the refusals are the
    // launch body's own two — the POSITION gate, silent since 2026-08-31, and
    // the DEAD-DEVICE gate, which still cards — and the audition's press-time
    // preflight has already asked both ahead of the act (so a refusal here is
    // unreachable in practice and would merely end the act).
    // ONE CALLER: GuiAbAudition::launch_phase (ab_audition.cpp), which owns the
    // sequence this play is one step of, WRITES THE PHASE BEFORE CALLING HERE
    // and clears it again on false. A live session never launches (the caller
    // always arrives stopped — the tick's natural end or the act's own tab
    // switch precede every call).
    bool launch_bounded_audition(int64_t start, int64_t span);

    // Reseek the active playback session to a new starting sample, keeping
    // audio alive. The sample is expressed in the active playhead domain
    // (source-domain in source view; target-domain in target view). Handles
    // the target-view target_buffer translation internally. Caller is
    // responsible for the entry-state check — playback alive AND the position
    // actually moving (place_playhead_at_click_column, the ONE caller,
    // compares the sample against the entry playhead); this function
    // unconditionally reseeks when called. The scrub paths no longer come
    // here — a scrub act only stops or launches (scrub_act_at). Samples outside
    // the active view's range — the song in source view, the target buffer's
    // domain in target view — fall back to playback.stop(): keep-alive intent is
    // well-defined for in-range positions only.
    void reseek_keeping_alive(int64_t sample);

    // THE FOLLOW KEY'S ONE CHOKEPOINT, and it TAKES NO VALUE because the
    // subject is not always the same bit (architect 2026-09-11, follow's
    // one-shot): shared by the bare-`f` toggle and the icon-row button that
    // synthesizes that chord, which since the `follow` key left the schema
    // 2026-09-11 are the whole membership.
    // IT FORKS ON THE TRANSPORT. With NO project play in flight it toggles
    // app.follow_armed, the lamp that says "the next play follows" — and
    // nothing else happens, the arming being a promise about a launch that has
    // not happened yet. WITH A PROJECT PLAY IN FLIGHT (playback live and the
    // A/B audition not standing) it toggles app.follow_engaged for that play
    // alone and leaves the lamp untouched: the on edge resyncs the predictor
    // and pages the scanner back into view if it had drifted offscreen — the
    // chase resuming its paging, not just taking one jump — and the off edge
    // writes nothing else. During the A/B AUDITION the lamp is the subject:
    // the act's plays never chase, so the press is about the user's next play.
    // WHAT IT WRITES IS UNCHANGED BY THE LAMP'S 2026-09-12 FACE, which took
    // this very fork: with a project play in flight the face shows that play's
    // chase, so the in-flight arm below now LIGHTS the lamp it does not touch,
    // and the face is what says so (redesign_button_selected, app_state.h).
    void toggle_follow();

private:
    // The active view's PLAY END — the song's end in source view, the bound
    // preview buffer's domain end in target (the split and its ruling are at
    // the launch body's definition). The one owner of that split for the
    // launch family: the view-end launch reads it as its end, the bounded
    // launch as its clamp.
    int64_t active_view_play_end() const;
    // The view-end launch (contract at the definition): validate an absolute
    // paint-domain launch position and play from it to active_view_play_end.
    // Returns whether it launched. Callers (toggle_playback's play edge,
    // scrub_launch_at) run the defensive follow_engaged clear before
    // delegating. Since 2026-08-26 this is a thin caller of
    // launch_playback_window below, adding the end and — since 2026-09-01 —
    // THE USER-LAUNCH CLEAR of the A/B audition sequence ahead of the
    // delegation (owner (2) of the edge inventory at GuiAuditionSequence): a
    // launch of the user's own transport is a fresh session, refused or not,
    // and this entry is the only road into the body that is not the act's.
    // IT IS ALSO WHERE THE FOLLOW LAMP IS SPENT (2026-09-11): its SUCCESS TAIL
    // runs spend_follow_lamp below — app.follow_armed copied into
    // app.follow_engaged and the lamp put out — which is exactly the
    // project-audio launch roads and not the audition's (that road enters the
    // body directly); since 2026-09-17 the car's launch spends it through the
    // same tail from its own entry.
    bool launch_playback_from(int64_t launch_pos);
    // THE FOLLOW LAMP'S SPEND, one body (2026-09-17, factored out of
    // launch_playback_from's success tail when the car's launch became the
    // third project-audio launch road): the lamp says "the next play
    // follows", the caller's play IS that next play, so the arm becomes the
    // chase and the lamp goes out. TWO CALLERS, each in its own success tail
    // and nowhere ahead of a refusal: launch_playback_from (Space's play arm
    // and the scrub) and car_toggle_playback (the head unit's play). The A/B
    // audition's road passes neither. The lamp's writer inventory is at
    // app.follow_armed, app_state.h.
    void spend_follow_lamp();
    // THE LAUNCH'S CAMERA TERM, THE CALLER'S WORD (architect 2026-09-18):
    //   * `PageIn` — if the launch position is offscreen, left-edge-align the
    //     viewport on it before the scanner issues forth
    //     (Viewport::follow_scroll_if_needed, the shape follow's own check
    //     has). Every GUI launch road starts ON SCREEN by construction, so
    //     this is a no-op on the ordinary press and a rescue on the rare one.
    //   * `Leave` — the launch writes no camera at all.
    // NO DEFAULT ARGUMENT: every caller states its own, as MarkerLandingFrame
    // is stated at cycle_marker_focus (selection-model.md's precedent — a
    // camera term that defaults is a camera term nobody reads).
    enum class LaunchCamera { PageIn, Leave };
    // THE ONE LAUNCH BODY FOR THE PROJECT'S AUDIO (contract at the
    // definition): validate `start`, seed the scanner, and play [start, end)
    // — ONCE with `loop_begin` = kPlaybackNoLoop, or wrapping to `loop_begin`
    // forever (the car's loop of the trim, 2026-09-17; the parameter is
    // REQUIRED so every caller states which). Every launch OF THE PROJECT'S
    // WAVEFORM ends here — the view-end launch above, the bounded audition
    // and the car's play — so the gates, the scanner seed, the camera term
    // and the launch damage are written once. It clears no sequence: the
    // view-end entry and the car's entry have already cleared it for a user
    // launch, and the bounded audition arrives with the act's phase already
    // standing — the body itself reads no phase and branches by no act; the
    // distinction between a user launch and the act's own is structural
    // alone, carried by which entry reached it (the contract at the
    // definition).
    // THE PRODUCT HAS A SECOND LAUNCH BODY SINCE 2026-08-28, and it is
    // recorded here as well as at its own head: the RENDER PLAYER's
    // (GuiRenderPlayer::play_wav / toggle_pause / seek_to, render_player.h)
    // plays a decoded render over ITS OWN buffer and calls playback.play
    // directly, because everything this body seeds belongs to the project's
    // waveform, which the player does not display — the resting playhead
    // does not move, the scanner never runs, and the item's domain is the
    // buffer's own. The two share the ONE STOP BODY above, which carries the
    // player's fork.
    bool launch_playback_window(int64_t start, int64_t end, int64_t loop_begin,
                                LaunchCamera camera);
};
