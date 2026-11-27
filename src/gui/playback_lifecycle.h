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
// the one play) and the follow and restrict lamps' toggles — two sticky
// lamps, the device's (architect 2026-11-24: seated from the device key
// `lit_lamps` at every open through these toggles and stored at every edge,
// GuiInputHandler::seat_lit_lamps and main.cpp's refresh_lit_lamps).
// AppState, Viewport and GuiAudio are captured directly.
// GuiPlayback stays a pure mechanism class — these operations live one layer up.
// (No GuiPlatform& member: every damage this cluster emits goes through
// Viewport.)
struct GuiRenderPlayer;

// THE DEAD DEVICE'S SENTENCE, ONE SPELLING AND FOUR SITES (2026-08-30,
// 2026-09-04): the one launch body's own device gate — the BELT,
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
// BESIDE THOSE FOUR: the car's Play
// (car_toggle_playback's play arm, its prologue), the score viewer's tick
// (a dead device mid-soundtrack pauses, the render player's rule) and
// NOISE's wake (GuiInputHandler::wake_device_for_level_case, architect
// 2026-11-24: the level case's tap or drag lifted on an audible level over
// a dead stream reopens and plays nothing, and cards on a refused reopen).
// The bed itself asks no device question — it sounds through whichever
// stream is running, noise.h's head.
// It lives here rather than in notifications.h
// because the fact is this cluster's: the question is GuiPlayback::
// ensure_device_available_for_play's — THE REOPEN AT THE PRESS (architect
// 2026-09-02): each PRESS site (the three launch sites, the car's Play,
// NOISE's wake) reopens a dead AAudio stream before it asks, and
// cards only when that reopen failed. The
// reads: the THREE TICKS keep device_unavailable — the render player's (a
// dead stream mid-play must pause), the A/B
// audition's (a dead stream mid-act must end the act, the fourth site
// above) and the score viewer's (a dead stream under a playing soundtrack
// pauses it and cards this sentence), and NOISE's lift reads it live to
// choose its wake — and the
// two PLAY-face predicates (space_launch_would_play,
// ab_audition_preflight_ok) read device_absent, the never-came-up half alone
// — a face is a read, never a reopen, and it must not grey what the press
// would reopen.
inline constexpr const char* kPlaybackDeviceUnavailableCard =
    "Playback is unavailable on this device";

// THE DISPLAY KEEP-ALIVE, A BRIEF WINDOW AFTER A FAST STOP (architect
// 2026-11-13) — THE ONE OWNER OF ITS FACTS AND ITS RULING.
// THE FACTS (measured 2026-11-13, under the 90 Hz pin): the tablet's panel
// (Galaxy Tab S10 FE) drops 90 -> 30 Hz once no layer has presented for
// 250 ms (SurfaceFlinger's idle timer), and the first frames after it land
// at 30 Hz cadence; neither the window's frame-rate vote (adopt_window) nor
// a preferred display mode outranks the timer, and `min_refresh_rate` is a
// System setting no app may write. The architect saw it as a CATCH — the
// scanner's first beat stepping late — only when tapping play and stop
// frantically on a transient: recorded over 127 plays, 14
// late first frames (41..60 ms against a 27 ms median), 13 of them
// 0.34..1.06 s after the previous Stop, 12 of those after a play that ran
// under 0.15 s; calm use never showed one.
// THE FORMS NOT TAKEN: a keep-alive at every vsync cost ~46 % app / ~53 %
// SurfaceFlinger (the panel pinned at 90 Hz); a permanent
// sparse keep-alive (one frame after GuiPlatform::kDisplayKeepAliveMs
// without a present, about five a second at rest) held 90 Hz but cost about
// 11 % battery.
// THE RULING: the sparse keep-alive, ARMED ONLY BRIEFLY — A STOP LANDING
// WITHIN kDisplayKeepAliveFastStopMs OF ITS PLAY keeps the panel awake for
// kDisplayKeepAliveWindowMs PAST THAT STOP, each such Stop re-arming it;
// calm use never arms it, and the first tap after a rest stays uncovered,
// accepted ("cover a little more than I can physically do ... comfortable
// hitting it as hard and fast as I want"). IT STANDS (architect
// 2026-11-16: "the whole point
// would be that the scanner would be the one thing that gets the special
// treatment. And the keep alives are about the scanner, in particular the AB
// audition and the frantic play press"): the panel kept awake at its pinned
// rate, the scanner's first frames after the Stop land on the beat
// (kFrameBeat, platform_android.cpp), not on the idle 30 Hz panel's late
// first refresh. THE FRAMES IT PRESENTS are the clock cell alone, small
// frames, one per GuiPlatform::kDisplayKeepAliveMs inside the window. TWO CONSTEXPRS, NO
// DEVICE KEY ("I'm not going to be able to test this reliably": he
// recompiles with either raised). The arming is GuiPlaybackLifecycle's (the
// stamp at the user's launch entries, the window at the one stop body's
// project arm, display_keep_alive_wanted), the period and the question the
// platform's (GuiPlatform::display_keep_alive_due), the damage main.cpp's
// tick head. The A/B audition's own rest keep-alive (main.cpp's "A REST
// KEEPS THE PAINT CLOCK RUNNING") is another road, standing under the same
// ruling.
inline constexpr int64_t kDisplayKeepAliveFastStopMs = 1000;
inline constexpr int64_t kDisplayKeepAliveWindowMs   = 1500;

struct GuiPlaybackLifecycle {
    AppState&         app;
    const GuiAudio&   audio;
    GuiPlayback&      playback;
    Viewport&         viewport;
    // THE CARD (2026-08-30, the strictness ruling; ONE sentence,
    // 2026-08-31): the ONE LAUNCH BODY is where a play that will not sound is
    // discovered, and the DEVICE half is what it says — a dead or absent
    // device is the one refusal the screen cannot show for itself. The
    // launch position with nothing left to play from is SILENT (2026-08-31:
    // a benign one-dimensional refusal already at its state; the rule is at
    // playback_lifecycle.cpp's head).
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
    // IS THE PLAY IN FLIGHT ONE RESTRICT GOVERNS? (architect 2026-10-29,
    // AppState::restrict.) RESTRICT stops and gates THE USER'S OWN PLAY OF THE
    // WAVEFORM — Space's play edge and the scrub's — and nothing else, and
    // which ENTRY a launch came through is what says so (the launch body's
    // structural rule: the body branches by no act). Written by the three
    // entries into the one launch body: SET by launch_playback_from (the
    // view-end entry Space and the scrub ride) to whether its launch
    // succeeded; CLEARED by car_toggle_playback's play arm and launch_bounded_audition
    // ahead of their launches. THE TWO EXEMPT: the CAR'S LOOP of the trim
    // starts off screen by design (its begin a fixed point screens upstream
    // of the passage under work) and loops forever, so a stop at the edge
    // would end the console's play the first time it reached it; the A/B
    // AUDITION's four bounded plays are each framed by their own `c` and
    // already end at their span. (The render player and the score viewer play
    // their own buffers and never reach the launch body.) TWO READERS: the
    // stop in main.cpp's pre_paint_advance, and the console's seek
    // (seek_live_session_by: under a lit RESTRICT a restrictable play's
    // forward landing past the window's right edge is its silent no-op).
    // Meaningful only while a project play runs; its value at rest is never
    // read.
    bool restrictable_play = false;
    // THE SEEK'S FRAME IS OWED ITS ADVANCE (2026-11-20): SET by
    // seek_live_session_by on a seek that moved, its one
    // writer of true; READ AND CLEARED by the next pre-paint advance
    // (main.cpp's pre_paint_advance, its one reader, at its top, whatever
    // that frame then does), so the bit lives for one frame. What it buys:
    // the seek seeds the scanner at its landing and the publish floors the
    // predictor there, so the due frame's integer position can equal the
    // seed and the advance's "nothing moved" early return would skip
    // FOLLOW's step (and RESTRICT's stop) for that frame — the old camera
    // painted with the new clock and the line off screen. With the bit the
    // seek's own frame runs the advance's whole body. No other road sets it:
    // a launch, a loop wrap and a stop leave it alone.
    bool seek_frame_owed = false;
    // THE DISPLAY KEEP-ALIVE'S TWO STAMPS (architect 2026-11-13;
    // the facts and the ruling at kDisplayKeepAliveFastStopMs above), both
    // monotonic_ms() (app_state.h, the application layer's one clock).
    // `user_play_launched_ms` is THE USER'S PLAY'S START: written by the two
    // user entries into the launch body on a launch that ran —
    // launch_playback_from (bare Space's play edge, whoever produced the
    // chord — the PLAY key's lift, the on-screen keyboard — and the scrub)
    // and car_toggle_playback's play arm (the car's play) — set to -1 by the A/B
    // audition's launch (the act's plays never arm), and CONSUMED (-1) by
    // the one stop body's project arm, which is the one seat every stop of a
    // project play crosses: there a stop landing within
    // kDisplayKeepAliveFastStopMs of the stamp sets
    // `display_keep_alive_until_ms` to that stop + kDisplayKeepAliveWindowMs.
    // NOTHING ELSE WRITES THE DEADLINE, so once it passes the tick asks the
    // platform nothing and the app presents nothing at rest. Per project
    // session, as this cluster is: a reopen starts with neither standing.
    int64_t user_play_launched_ms       = -1;
    int64_t display_keep_alive_until_ms = 0;
    // IS THE BRIEF WINDOW OPEN AT `now_ms` (monotonic_ms())? The tick head's
    // one question before it asks the platform's (main.cpp).
    bool display_keep_alive_wanted(int64_t now_ms) const {
        return now_ms < display_keep_alive_until_ms;
    }
    // N2, THE STOP THAT PLAYS (architect 2026-11-17: "add the two lines - i
    // assume these will run only when the button face shows stop and the
    // button actually does a play?"): a diagnostic for N2, to be removed once
    // N2 is understood. `n2_launch_count` counts the one launch body's
    // launches that ran; `n2_toggle_branch` names the exit toggle_playback
    // took last (the PLAY key's lift resets it before its dispatch). Their
    // readers are the two stderr lines alone: the PLAY key's lift
    // (finish_chrome_press_release) and the media roads (main.cpp).
    uint64_t    n2_launch_count  = 0;
    const char* n2_toggle_branch = "not reached";

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
    //     write). The S/T switch `t` stops on its own standing ruling, the audio
    //     domain flipping under the running session.
    //   * THE CENTRE COMMAND STOPS, ALWAYS (architect 2026-09-29): `c` and every
    //     road that runs its body (the Center button, the paired marches' steps,
    //     Ctrl+J, the A/B audition's opening of each half) stop before they
    //     centre, a focus standing or not, at the command's one owner
    //     (run_center_command), so the centring is on the resting playhead and
    //     the hold the key and the audition arm stands on it. Follow, not `c`, keeps the
    //     scanner in view.
    //   * MARKER MUTATIONS STOP, IN BOTH VIEWS (architect 2026-10-14, "we
    //     want to aggressively stop audio"): every act that writes either
    //     marker store — the warp or the phase-reset one — inside the undo
    //     domain stops a live play. The classes: the drops; the deletes; the
    //     status toggles (Ctrl+D, Ctrl+N's pass/owner conversion); the flatten;
    //     the Up/Down tempo step, singleton and group, and its pointer hand
    //     the value drag; the marker drag; the flag editor's payload commit;
    //     both phase-reset pastes (the placement paste and the state paste,
    //     which flips disabled flags); the position nudges; undo and redo; and
    //     the whole-store rewrites (the history revert, load in place). Each
    //     pays once, at the act's own body — the one place every road onto that
    //     act passes, key, button and pointer alike — past its refusals and
    //     ahead of its first store write; where an earlier stop already
    //     stands on the road (the marker press's click act ahead of either
    //     flag drag, the paste confirmation's open, the restore body, the
    //     load in place's self-guard) that stop is the act's. NOT in the
    //     class: the SESSION-ONLY ITERATION STATE — the bound cells' step, drag
    //     and typed commit, the tie toggle, the mode's wipe — and the BPM
    //     form's owner fields. None of them is a map input or a render
    //     input, so none changes what plays (the bracket feeds the grid
    //     iterations' own sidecars alone, AppState::iteration_mode_enabled),
    //     and they stand outside the undo domain for that same reason.
    //   * VALUE STEPS STOP (architect 2026-10-14): the Up/Down tempo step on
    //     the addressed cell writes the warp store, so it is a marker mutation
    //     and stops, its group arm as well as its singleton — the
    //     group-preserving shape of the tempo arm buys no play-on. The step is
    //     bare Up / Down alone, one cent a press. The BOUND arm stops
    //     nothing, being iteration state (the bullet above).
    //   * PURE VIEWPORT MOVES DO NOT STOP: bare `0`'s ZOOM-OUT ARM,
    //     PageUp/PageDown, the zoom gestures and the zoom step (`=` / `-`,
    //     2026-10-27) — they move the window onto the audio,
    //     not the audio. `0`'s RESTORE (architect 2026-09-23) puts the stamped
    //     playhead back and so stops as Home / End do, only when that playhead
    //     differs from the resting one; at a ceiling nothing stamped `0` is a
    //     silent no-op and stops nothing.
    //   * TRIM MUTATIONS STOP, IN BOTH VIEWS: the sweep and Reset Trim, matching every
    //     POINTER trim route (the endcap/bridge drags and the bound-set clicks each
    //     stop at their own commit point). BOTH views: a TARGET audition is
    //     playing out the very window the mutation replaces; a SOURCE one runs
    //     to the song's end and never ran against the bounds being moved, and
    //     stops on the rule's own both-views symmetry (no trim write moves the
    //     playhead, architect 2026-11-07, input_trim.cpp's head).
    //   * A POINTER OR TOUCH PLAYHEAD PLACEMENT STOPS (architect 2026-10-06):
    //     a press, click or drag on the main view that places the
    //     playhead (the waveform's upper-half click act, the ruler's and the
    //     marker lane's empty-stretch placement clicks) is the one way to stop
    //     a play without the Stop button, since no other waveform-area act
    //     stops it. It pays once, at the placement body
    //     (place_playhead_at_click_column), past the gutter refusal and ahead
    //     of the first write. The armed trim sweep's begin is not in the
    //     class (architect 2026-11-07: it seats no playhead); the sweep stops a
    //     play as a TRIM MUTATION, at its first accepted write (the bullet
    //     above). NOT in the class: the
    //     audition scrub (the lower half's click act and the render player's
    //     scrub — "play from here" restarts the audio itself), pans and zooms
    //     (they move no playhead), and the car transport.
    // Every stop in the rule is REFUSAL-GATED (the standing 2026-07-28 rule): it
    // sits past its route's refusals and immediately ahead of that route's first
    // write, so a press that writes nothing stops nothing.
    // THE ARCHITECT'S CONTEXT, recorded because it is what makes the rule cheap:
    // "not stopping audio is not a big priority; audio is constantly being
    // relaunched to audition the impact of decisions."
    // The rule classifies the commands it names and claims nothing about the ones
    // it does not. The cursor-moving NAVIGATION stops (the arrows in every
    // magnitude, Home/End,
    // the Tab family) are broader than the rule — they hold under the
    // definition's own contract, a handler about to commit a new cursor position.
    void stop_playback_if_playing();

    // THE MODAL-OPEN PLAYBACK STOP, ONE OWNER (architect 2026-07-28). Called
    // at the moment a modal surface ACTUALLY opens. THE CALLER INVENTORY —
    // SIXTEEN sites: THE LANE
    // EDITORS' TWO OPENS (flag_editor.cpp: GuiFlagEditor::enter_text_edit,
    // the flag editor's one open chokepoint, and enter_iter_bound_edit, the
    // iteration bound editor's; architect 2026-11-20); in
    // input_key_dispatch.cpp THE TRIM PRESETS LIST (open_trim_presets), and
    // its name editor under Save As and Rename
    // (open_trim_preset_name_editor) and the BPM form's line
    // editor (bpm_form_commit), each of those two a dialog editor opened
    // over a closed picker; THE
    // PROGRESS MODAL's open (GuiInputHandler::open_batch_progress,
    // input_render_dispatch.cpp, the batch's Render press);
    // GuiSettingsEditor::open (settings_editor.cpp); in
    // input_key_dispatch.cpp the `h` view's `'` LOAD CONFIRMATION
    // (history_load_in_place), THE BPM FORM (open_bpm_form,
    // the Ctrl+B arm's tail and its line editor's return, whose open rides
    // the stop already taken), the OPEN PROJECT PICKER
    // (open_project_picker), THE SETTINGS PAGE (open_settings_page, the
    // picker's second content, whose rows for the listed keys swap a value
    // list in under the stop already taken) and the PULL confirmation
    // (run_history_pull_press, Cancel / Pull); THE SCORE VIEWER's open
    // (GuiScoreViewer::open, score_viewer.cpp); the TWO
    // prompt opens (prompt.cpp: unsaved, and Revert's confirmation); THE
    // RENDER PLAYER's open (GuiRenderPlayer::open, render_player.cpp — the
    // project's audition ends where the player's transport
    // begins, and the player's own stops all take stop_playback_if_playing
    // through the fork inside it).
    // THE SETTINGS EDITOR'S STOP IS INSIDE GuiSettingsEditor::open, one owner
    // for whatever doors reach it, refusable or not: a caller-side stop
    // ahead of a refusable open would kill an audition and open nothing (the
    // refusal-gating rule below). Its one door is the settings page's rows,
    // the page's own open a caller of its own, so a row's editor open finds
    // playback already stopped.
    // THREE MODAL OPENS ARE NOT CALLERS, and each is a recorded exception
    // rather than a gap: the PASTE_CONFIRM prompt is built outside prompt.cpp
    // (PhaseResetPropagate::open_paste_confirmation, its one opener) and
    // stops through
    // stop_playback_if_playing directly, which is mechanically this same stop;
    // and the render player's two prompts — its LOAD_IN_PLACE_CONFIRM
    // (GuiInputHandler::render_player_load_in_place) and its DELETE question
    // (render_player_delete, input_key_dispatch.cpp) — each PAUSE the
    // player's own transport through GuiRenderPlayer::toggle_pause, which
    // takes that same body through the player's fork — a stop that must keep
    // the resume point the ordinary modal stop would not — so the RULE holds
    // for every modal surface even though this function's caller set is not
    // literally every opener.
    // Authoring or answering a dialog over a live audition is the wrong default,
    // and Space is inside each of those surfaces' blocked sets, so playback
    // cannot restart until the surface closes.
    // THE DECISION TABLE lives here, so a new modal surface inherits an ANSWER
    // instead of an absence:
    //   * THE MODAL FAMILY'S SURFACES — the one dialog editor, the prompts
    //     and the progress modal — and THE TWO OVERLAYS, the picker (all
    //     five of its contents, the project picker, the settings page and
    //     the value list it swaps in, the trim presets list, whose name
    //     editor's open finds playback already stopped, and the BPM form,
    //     whose line editor's likewise) and the
    //     render player (whose own transport begins where the project's
    //     audition ends), and THE SCORE VIEWER (whose own soundtrack
    //     likewise begins where the project's audition ends), all standing
    //     in the modal family's box —
    //     STOP.
    //   * THE LANE EDITORS' OPEN — the flag editor in every kind and the
    //     iteration bound editor — STOPS TOO (architect 2026-11-20:
    //     "play should stop on editor open. i don't use that workflow and
    //     never have"): EVERY EDITOR'S OPEN STOPS, so nothing of the
    //     project's plays while the on-screen keyboard stands — PLAY and the
    //     clock lie under it, and the keyboard is the one live element
    //     (onscreen_keyboard.h's placement block). The stop is this one,
    //     whole: the scanner down, the transport's state, the car's console
    //     (whose admits() takes no command under an editor) and the fast-Stop
    //     keep-alive's arming as for any modal open. Its two sites
    //     (GuiFlagEditor::enter_text_edit, enter_iter_bound_edit) sit past
    //     their opens' guards and the re-open on the live session's own
    //     target, ahead of the select and the land. EDIT's hold, the label
    //     paste, opens no editor and stops only on a changed write, the
    //     keyboard stop rule above (GuiFlagEditor::paste_payload_onto_
    //     selection).
    // REFUSAL-GATED (the standing rule): the stop is the price of an OPEN, so
    // each site calls this only once its own guards have passed — a refused open
    // must leave a listening session untouched. Do NOT hoist a call above a
    // guard ladder.
    // Mechanically this IS stop_playback_if_playing (a modal open needs no
    // teardown the gesture stop does not already do); the separate name is what
    // gives the rule a greppable home. The NON-modal stops
    // (gesture stops, the S/T toggle, the load-in-place mutator's self-guard)
    // keep
    // calling stop_playback_if_playing directly.
    void stop_playback_for_modal_open();

    // launch_offset shifts the SCANNER's launch position (and the play() launch
    // bound) forward in the active paint domain WITHOUT moving the resting
    // cursor, so stop just deactivates the scanner and the cursor is unmoved. Non-zero
    // only for the target-view lead-in audition Space performs when the
    // phase-reset overlay has a subject (start from cursor + N/2);
    // the default 0 keeps plain Space and every other caller byte-identical.
    // The offset is applied only in the target-view branch; the offset launch
    // is re-validated against the target buffer's domain, so an offset landing
    // at or past the buffer end is a SILENT no-op (the pre-sum gate, whose
    // verdict is the launch body's own — architect 2026-08-31: a benign
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
    // nowhere else — THE FORK, THEN THE PLAY. STOP ARM: exactly
    // toggle_playback's — the same transport-live term (a live project play
    // OR a standing A/B audition, a rest of the act stopping as a play does)
    // through the one stop body, and PAUSE IS THE GUI'S STOP: no pause
    // semantics, no resume point — the console's pause runs
    // stop_playback_if_playing as Space's stop does, because the car's
    // transport is to be as close to the GUI's as the car allows and, with
    // the trim narrowed to what he is working on, he goes back to the
    // beginning anyway. PLAY ARM: the window is the ACTIVE DOMAIN'S TRIM,
    // Viewport::trim_range — the navigation range (in target view the trim
    // mapped through the live map, the full window normalized to the whole
    // domain) — and THE PLAY LOOPS IT FOREVER, in every view (his one
    // difference from the GUI's transport), STARTING AT THE TRIM'S BEGIN
    // ALWAYS (architect 2026-09-17): the resting playhead is no term of it,
    // and a restart is the button itself — pause, then play. NO LEAD-IN
    // OFFSET: the phase-reset overlay's N/2 is Space's authoring aid, and the
    // car's play takes none. The gates are toggle_playback's own — the device
    // reopen (carding a failed one) — ahead of the user-launch clear (owner
    // (2) at GuiAuditionSequence: this is the second road into the launch
    // body that is not the act's), and the launch through
    // launch_playback_window with the trim's begin as the loop target.
    // THE CAMERA STAYS WHERE IT IS UNLESS FOLLOW IS LIT (architect
    // 2026-09-18): this is the ONE launch that may start OFF SCREEN — the
    // trim's begin is a fixed point and the passage he is working on is
    // habitually a couple of screens downstream of it, so a page-in on every
    // console press cost him a manual recentre per audition. The launch
    // states `LaunchCamera::Follow` iff the lamp AppState::follow is lit and
    // `LaunchCamera::Leave` otherwise — lit, the follow scroll
    // (Viewport::follow_scanner) places the scanner at the launch and the
    // pre-paint advance keeps it for the whole play (unless a camera change
    // suspends it, AppState::follow_suspended); dark, nothing moves, at the
    // launch or at any loop wrap (the wrap's consumer resyncs the predictor
    // and damages, and writes no camera — main.cpp's tick).
    // A trim under two frames refuses in the body's own playable gate,
    // silently (the benign one-dimensional class: the playhead and the grey
    // say it). The target view's preview-readiness gate is THE CALLER'S, as
    // it is on_key's for Space — car_toggle asks it as Space's edge does.
    //
    // THE LOOP IS A PROPERTY OF THIS LAUNCH, NOT A LAMP: any GUI act that
    // stops (Space, Home / End, a marker touch, a modal open, the S/T flip,
    // a GUI undo or redo — the restore body's own stop) or relaunches (a
    // scrub) ends it, and every GUI launch plays once as it always has. The
    // head unit's Previous / Next SEEK a live loop three seconds back or
    // forward (seek_live_session_by below, architect 2026-11-20) and undo /
    // redo only at rest (GuiCarTransport), so the console's own buttons never
    // restore over the loop: they move its cursor and leave it looping.
    void car_toggle_playback();
    // THE CONSOLE'S SEEK OVER A LIVE SESSION (architect 2026-11-20:
    // "when audio is playing, the rewind and fast forward buttons ... should
    // ... just go back 3 seconds and forward 3 seconds"; "it wraps around on
    // the end, but not at the beginning"): move the playing session's read
    // position by `delta_frames` of the bound buffer — negative back,
    // positive forward — and answer whether it moved. ONE CALLER,
    // GuiCarTransport::car_skip's live arm, which has dropped the A/B
    // audition and asked the play's key gates first.
    // THE ROAD IS A NEW PUBLISH OVER THE LIVE SESSION — the render player's
    // live seek's road (GuiRenderPlayer::seek_to): the same window, the same
    // loop target, a new start (live_window_ below, the launch's record), so
    // the stream keeps running, no stop and no fence, and the next callback
    // seats the new start (the definition owns the splice's argument).
    // THE WALLS (his words above):
    //   * A LOOP (the car's): FORWARD PAST THE WINDOW'S END WRAPS as the loop
    //     does, the overshoot carried past the loop target (end − 1 s + 3 s
    //     lands 2 s after the begin); BACK STOPS AT THE LOOP TARGET, the
    //     trim's begin (no backward wrap).
    //   * ONCE THROUGH (Space's play, the scrub's): BACK STOPS AT THE
    //     SESSION'S LOWER WALL — the trim's begin when the session started
    //     inside the trim, else the session's own start (a source-view play
    //     launched outside the trim never seeks onto ground it did not start
    //     from; that fallback architect 2026-11-20: ruled as built);
    //     FORWARD PAST THE WINDOW'S END IS A SILENT NO-OP — the play runs on
    //     to its own end and the session keeps its once-through window
    //     (architect 2026-11-20: ruled as built; an end there would turn the
    //     next press into a Redo under a repeated press, "I don't like to
    //     feel hurried"). UNDER A LIT RESTRICT a restrictable play's own end
    //     is the painted window's right edge (the pre-paint stop, main.cpp),
    //     so a forward landing past it is the same silent no-op (architect
    //     2026-11-20: ruled as built); a back landing left of the window
    //     runs on into it, as a play does after a pan carried the camera
    //     past it.
    // A seek that cannot move — nothing playing (the natural end in flight
    // before the tick tears the scanner down), already at the back wall, a
    // degenerate or crossed trim, an unknown rate — is SILENT and writes
    // nothing. NOTHING AUTHORED IS WRITTEN: the resting playhead stays where
    // it was, no undo entry, no restrict or follow lamp touched. THE CAMERA
    // is the pre-paint advance's: with FOLLOW lit and not suspended the next
    // frame's follow scroll takes the new place by Viewport::follow_scanner's
    // rule (a place off screen to the left paged in, one right of the center
    // centered, one left of it left to run); with FOLLOW dark nothing moves.
    bool seek_live_session_by(int64_t delta_frames);
    // THE AUDITION LAUNCH ENTRY: launch the scanner from `frame`, an ABSOLUTE
    // position in the active paint domain, leaving the resting cursor untouched.
    // ONE CALLER CLASS — the waveform SCRUB act (the
    // launch that follows its stop in the same click, the scrub always
    // playing), which is also the gesture for
    // previewing the trim window: click inside it and it auditions from
    // there. That act has ONE entry (the lower-half plain press's
    // MOTIONLESS RELEASE, the waveform's two halves being one surface),
    // funneled through scrub_act_at, so this stays one caller class. (Space
    // always toggles from the playhead, architect 2026-07-30.)
    // Delegates
    // to the same launch body as toggle_playback's play edge, so the standing
    // gates apply identically: a frame outside the active view's range — the
    // SONG in source view, the target buffer's domain in target view — or one
    // leaving fewer than two playable frames of remainder, is a SILENT no-op
    // (the launch body's position gate, silent, 2026-08-31) —
    // exactly Space's conventions,
    // this being the one launch road with no outer gate of its own. A DEAD OR
    // ABSENT DEVICE still cards there, being the one refusal the screen cannot
    // show. A live session never
    // launches (defensive; the caller reaches here only with
    // playback stopped — the scrub act runs the one stop body first).
    void scrub_launch_at(int64_t frame);
    // THE BOUNDED AUDITION (architect 2026-08-26), the A/B audition's play:
    // launch the scanner from `start` — an ABSOLUTE position in the active
    // paint domain — and play `span` frames, the session's end being
    // `start + span` clamped to the active view's own end (the song's in
    // source view, the bound preview buffer's in target) rather than that end
    // itself. NOTHING LOOPS — the rule has exactly TWO sanctioned
    // exceptions, the render player's REPEAT ONE lamp (2026-08-28,
    // render_player.h, which reaches no project audio) and the CAR'S LOOP OF
    // THE TRIM (2026-09-17, car_toggle_playback above, which reaches this
    // project's audio through a launch of its own) — and NEITHER IS THIS
    // BODY'S: this is a discrete play to ITS end, the
    // natural-end teardown is its one terminal, and the resting cursor is
    // untouched exactly as under Space — the same launch body, the same
    // gates (playback_launch_playable, so a start at or past the domain end
    // or leaving fewer than two frames refuses), the same scanner — but NO
    // FOLLOW: the pre-paint follow scroll asks the act's phase and moves for
    // none of its plays, each framed by its own `c`. Returns whether it launched; the refusals are the
    // launch body's own two — the POSITION gate, silent (2026-08-31), and
    // the DEAD-DEVICE gate, which still cards — and the audition's press-time
    // preflight has already asked both ahead of the act (so a refusal here is
    // unreachable in practice and would merely end the act).
    // ONE CALLER: GuiAbAudition::launch_phase (ab_audition.cpp), which owns the
    // sequence this play is one step of, WRITES THE PHASE BEFORE CALLING HERE
    // and clears it again on false. A live session never launches (the caller
    // always arrives stopped — the tick's natural end or the act's own tab
    // switch precede every call).
    bool launch_bounded_audition(int64_t start, int64_t span);

    // THE FOLLOW KEY'S ONE CHOKEPOINT (architect 2026-09-23), shared by bare
    // `f`, the icon-row button that synthesizes that chord and the open's
    // seat of the sticky lamp (GuiInputHandler::seat_lit_lamps, architect
    // 2026-11-24: lit from the device key `lit_lamps` at rest, before the
    // first tick) — the lamp AppState::follow's one writer. It flips the
    // lamp. The LIT EDGE also
    // clears AppState::follow_suspended, and during a project play (the A/B
    // audition not standing) resyncs the predictor and takes the follow
    // scroll at once (Viewport::follow_scanner: a scanner right of the
    // center is placed on it, one off screen to the left paged in); the dark
    // edge writes nothing else, the camera staying where the scroll left it. The face repaints through the per-tick comparator,
    // so this mutator owes no damage. Refused in the `h` view by the
    // dispatch (bare `f` is off that mode's allowlist), never here; the view
    // neither puts the lamp out nor runs a play it governs (architect
    // 2026-11-25; the view has no playback), so a lit lamp stands through it
    // and acts again on leaving.
    // THE RADIO (architect 2026-10-29): the lit edge also puts RESTRICT out
    // (AppState::restrict = false); the dark edge leaves it alone.
    void toggle_follow();
    // THE RESTRICT KEY'S ONE CHOKEPOINT (architect 2026-10-29), shared by bare
    // `r`, the icon-row button that synthesizes that chord and the open's
    // seat of the sticky lamp (GuiInputHandler::seat_lit_lamps, toggle_follow's) — the lamp
    // AppState::restrict's one writer beside toggle_follow's lit edge. It
    // flips the lamp; THE RADIO: the lit edge puts FOLLOW out
    // (AppState::follow = false), the dark edge writes nothing else. Lit
    // during a restrictable play whose scanner already stands past the
    // window's right edge, the next pre-paint stops it — the lamp says the
    // play stays in the window, and the stop is the tick's alone. The face
    // repaints through the per-tick comparator, so this mutator owes no
    // damage. Refused in the `h` view by the dispatch (bare `r` is off that
    // mode's allowlist), never here; a lit lamp stands through the view,
    // toggle_follow's rule (architect 2026-11-25).
    void toggle_restrict();

    // THE RELEASE KEY'S ONE CHOKEPOINT (architect 2026-11-26; the rule and
    // its three ends at playback_aaudio.cpp's lifecycle block), THE KEY'S
    // ACT ALONE: the icon-row button's internal chord (is_release_lamp_key,
    // gui_input.h) is its one caller. The open's seat of the sticky lamp is
    // no toggle and does not come here (GuiInputHandler::seat_lit_lamps
    // calls GuiPlayback::release_device itself). DARK, IT RELEASES: whatever plays stopped through the
    // ONE STOP BODY (stop_playback_if_playing: the scanner, the A/B act, a
    // transport's state), then GuiPlayback::release_device — the fence, the
    // close, the drop's state, the fact. LIT, IT IS THE SECOND PRESS:
    // GuiPlayback::end_device_release, the stream reopened and started with
    // no play, no card on a refused reopen (the lamp is the whole surface;
    // the next play's gate cards). The lamp is the backend's fact
    // (GuiPlayback::device_released), mirrored for the face by the tick's
    // settle (GuiInputHandler::settle_device_release_lamp), which damages
    // the key at every edge, so this mutator owes no damage.
    void toggle_device_release();

    // THE APP'S RETURN ENDS A RELEASE (architect 2026-11-26: "Number 1,
    // agree"): the window's focus edge (GuiPlatform's activation hook,
    // main.cpp; APP_CMD_GAINED_FOCUS / LOST_FOCUS), `active` its new value.
    // A LOSS with the lamp lit is remembered; the NEXT GAIN, the lamp still
    // lit, ends the release (GuiPlayback::end_device_release, Return) — the
    // stream reopened with silence before any play. A gain with no loss
    // seen under the lamp ends nothing, so the launch's own first focus over
    // a seated lamp leaves it lit. Every loss of focus counts: Settings and
    // every other screen, the task switcher, the notification shade, the
    // screen dozing (each a LOST_FOCUS, its return the GAINED_FOCUS).
    void note_window_activation(bool active);

private:
    // A FOCUS LOSS SEEN WITH RELEASE LIT, not yet answered by a gain
    // (note_window_activation, its one writer and reader). Per project
    // session, as this cluster is.
    bool left_while_released_ = false;
    // The active view's PLAY END — the song's end in source view, the bound
    // preview buffer's domain end in target (the split and its ruling are at
    // the launch body's definition). The one owner of that split for the
    // launch family: the view-end launch reads it as its end, the bounded
    // launch as its clamp.
    int64_t active_view_play_end() const;
    // The view-end launch (contract at the definition): validate an absolute
    // paint-domain launch position and play from it to active_view_play_end.
    // Returns whether it launched. Callers: toggle_playback's play edge and
    // scrub_launch_at. A thin caller of
    // launch_playback_window below, adding the end and
    // THE USER-LAUNCH CLEAR of the A/B audition sequence ahead of the
    // delegation (owner (2) of the edge inventory at GuiAuditionSequence): a
    // launch of the user's own transport is a fresh session, refused or not,
    // and this entry is the only road into the body that is not the act's.
    bool launch_playback_from(int64_t launch_pos);
    // THE LAUNCH'S CAMERA TERM, THE CALLER'S WORD (architect 2026-09-18):
    //   * `PageIn` — if the launch position is offscreen, left-edge-align the
    //     viewport on it before the scanner issues forth
    //     (Viewport::follow_scroll_if_needed, follow's page-in). Every
    //     GUI launch road starts ON SCREEN by construction, so
    //     this is a no-op on the ordinary press and a rescue on the rare one.
    //     THE RESCUE IS THE UNRESTRICTED PLAY'S (architect 2026-10-29): under
    //     a lit RESTRICT the view-end entry refuses an offscreen launch
    //     position on a card before the body is reached, so no page-in ever
    //     follows the playhead out of the window there. Asked by the
    //     view-end entry with FOLLOW dark and by the A/B audition, never
    //     followed.
    //   * `Follow` — THE FOLLOW SCROLL at the launch (architect 2026-11-15;
    //     Viewport::follow_scanner, the rule's one owner): a launch
    //     position right of the center is centered at once, one off screen
    //     to the left paged in, one left of the center moves nothing. Asked
    //     by the two user entries — the view-end entry and the car's play —
    //     when the lamp AppState::follow is lit.
    //   * `Leave` — the launch writes no camera at all.
    // NO DEFAULT ARGUMENT: every caller states its own, as MarkerLandingFrame
    // is stated at cycle_marker_focus (MarkerLandingFrame's precedent — a
    // camera term that defaults is a camera term nobody reads).
    enum class LaunchCamera { PageIn, Follow, Leave };
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
    // THE PRODUCT HAS A SECOND LAUNCH BODY (2026-08-28), and it is
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

    // THE LIVE SESSION'S WINDOW AS LAUNCHED (architect 2026-11-20, the
    // console's seek): the domain start, exclusive end and loop target
    // (kPlaybackNoLoop for a once-through window) the one launch body handed
    // the engine, WRITTEN BY THAT BODY ALONE, in its success tail, and READ
    // BY seek_live_session_by ALONE, which republishes the same end and loop
    // target from a new start and never writes the record — so `start` stays
    // the launch's own, the once-through session's lower wall where it began
    // outside the trim. The engine's main-thread mirror (playback_common.h)
    // is buffer-local and the predictor's: its floor is the LAST publish's
    // start until a resync on the wrapped read lowers it to the loop target,
    // and its loop target is read by that resync alone, so neither is the
    // launch's own
    // domain window; this cluster made the launch, so the record is the
    // launch's. Meaningful
    // only while a project play runs; its value at rest is never read.
    struct LiveWindow {
        int64_t start      = 0;
        int64_t end        = 0;
        int64_t loop_begin = kPlaybackNoLoop;
    };
    LiveWindow live_window_;
};
