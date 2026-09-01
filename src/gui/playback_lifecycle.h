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

// THE DEAD DEVICE'S SENTENCE, ONE SPELLING AND THREE SITES (2026-08-30): the
// one launch body's own device gate — the BELT, which every road that plays
// passes — and the TWO PRE-LAUNCH GATES that decide something before reaching
// it and so must ask the same question first: toggle_playback's target-view
// pre-sum position gate (which would otherwise answer a dead device with the
// POSITION sentence) and GuiAbAudition::start's press-time preflight (which
// would otherwise run `c` and switch tabs before the belt refused). Only one of
// the three fires per press — each returns — so the shared literal is what
// keeps one fact one sentence. It lives here rather than in notifications.h
// because the fact is this cluster's: the reading is GuiPlayback::
// device_unavailable's and every site that raises it is a launch road.
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
    //     stop; each pays it at its own first write — the shared prologue's
    //     collapse arm for a 2+ press, each twin past its wall clamp for a
    //     singleton)
    //     and `c`. The S/T switch `t` stops on its own standing ruling, the audio
    //     domain flipping under the running session.
    //   * GROUP-PRESERVING VALUE STEPS DO NOT STOP: the bare Up/Down tempo cent
    //     step edits values and leaves the selection and its span exactly as they
    //     stood, so the audition plays on under the edit.
    //   * PURE VIEWPORT MOVES DO NOT STOP: bare `0`'s ZOOM-OUT ARM,
    //     PageUp/PageDown, the zoom steps — they move the window onto the audio,
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
    // it does not. The cursor-moving NAVIGATION stops (the bare arrows, Home/End,
    // the Tab family) are older and broader than the rule — they hold under the
    // definition's own contract, a handler about to commit a new cursor position.
    void stop_playback_if_playing();

    // THE MODAL-OPEN PLAYBACK STOP, ONE OWNER (architect 2026-07-28, replacing
    // six hand-spelled stops). Called at the moment a modal surface ACTUALLY
    // opens. THE CALLER INVENTORY, re-derived by grep 2026-08-30 — EIGHT
    // sites: GuiSettingsEditor::open (settings_editor.cpp); in
    // input_key_dispatch.cpp the `h` view's `'` LOAD CONFIRMATION
    // (history_load_in_place — the history picker it replaced on 2026-08-29
    // was this caller before it), the `m`
    // bpm editor (handle_mode_keys), the history view's COMMIT-TITLE editor
    // (open_history_commit_editor), the MEASURE PASTE-OFFSET editor
    // (open_measure_paste_editor, 2026-08-20) and the OPEN PROJECT PICKER
    // (open_project_picker, 2026-08-27 as a prompt, field-less since
    // 2026-08-28) — the last two had joined without this list moving, which
    // this re-grep corrects; the ONE prompt open
    // (prompt.cpp: unsaved — the error notice's was the second until that
    // prompt kind retired whole on 2026-08-30, which is what takes the count
    // from nine to eight); and THE RENDER PLAYER's open
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
    // audition and open nothing.
    // The count had held across the day's earlier change by coincidence: the
    // commit-title editor replaced the history commit confirmation, so one
    // caller left prompt.cpp as another arrived in input_key_dispatch.cpp.
    // TWO MODAL OPENS ARE NOT CALLERS, and each is a recorded exception rather
    // than a gap: the PASTE_CONFIRM prompt is built outside prompt.cpp
    // (PhaseResetPropagate::open_paste_confirmation) and stops through
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
    //   * DIALOG modal surfaces — the four dialog editors, the prompts and
    //     the picker, all painted as the bottom row's modal since
    //     2026-08-13 — STOP.
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
    // at or past the buffer end is a no-op that says "There is nothing left to
    // play from here" (the pre-sum gate's card, since 2026-08-30).
    // THE FORK IS "TRANSPORT-LIVE", NOT playback.is_playing(): a REST of the A/B
    // audition takes the stop arm too, the act being one transport session from
    // its first play to its last (the ruling and its face argument at the
    // definition).
    void toggle_playback(int64_t launch_offset = 0);
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
    // leaving fewer than two playable frames of remainder, is a no-op that
    // says so on the launch body's own card — exactly Space's conventions,
    // this being the one launch road with no outer gate of its own. A live session never
    // launches (defensive; the caller reaches here only with
    // playback stopped — a scrub act over a live session STOPS it and returns).
    void scrub_launch_at(int64_t frame);
    // THE BOUNDED AUDITION (architect 2026-08-26), the A/B audition's play:
    // launch the scanner from `start` — an ABSOLUTE position in the active
    // paint domain — and play `span` frames, the session's end being
    // `start + span` clamped to the active view's own end (the song's in
    // source view, the bound preview buffer's in target) rather than that end
    // itself. NOTHING LOOPS still — the rule has exactly ONE sanctioned
    // exception since 2026-08-28, the render player's REPEAT ONE lamp
    // (render_player.h), which is not this body's and reaches no project
    // audio: this is a discrete play to ITS end, the
    // natural-end teardown is its one terminal, and the resting cursor is
    // untouched exactly as under Space — the same launch body, the same
    // gates (playback_launch_playable, so a start at or past the domain end
    // or leaving fewer than two frames refuses), the same follow behaviour,
    // the same scanner. Returns whether it launched; the refusals are the
    // launch body's own two cards, which the audition's press-time preflight
    // has already asked ahead of the act (so a refusal here is unreachable in
    // practice and would merely end the act).
    // ONE CALLER: GuiAbAudition::launch_phase (ab_audition.cpp), which owns the
    // sequence this play is one step of and re-arms the sequence only on
    // true. A live session never launches (the caller always arrives stopped —
    // the tick's natural end or the act's own tab switch precede every call).
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

    // Set follow mode to `desired`. Shared by the bare-`f` toggle (which passes
    // !app.follow_mode) and the settings editor's `follow=` commit (which passes
    // the parsed value) so the two stay one implementation. An off→on edge
    // during live playback clears the manual-pan suppression and resyncs so
    // follow resumes paging, not just the one initial jump; with playback
    // stopped (the settings editor is modal, so its open stopped playback) the
    // edge branch is inert and this is a plain field set.
    void set_follow_mode(bool desired);

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
    // scrub_launch_at) run the defensive follow-override clear before
    // delegating. Since 2026-08-26 this is a thin caller of
    // launch_playback_window below, the end being the only thing it adds.
    bool launch_playback_from(int64_t launch_pos);
    // THE ONE LAUNCH BODY FOR THE PROJECT'S AUDIO (contract at the
    // definition): validate `start`, seed the scanner, and play [start, end).
    // Every launch OF THE PROJECT'S WAVEFORM ends here — the view-end launch
    // above and the bounded audition — so the gates, the scanner seed, the
    // follow check and the launch damage are written once. It also ends the
    // A/B audition sequence at its head (the launch-body clear, the edge
    // inventory at GuiAuditionSequence): a launch is a fresh session, whoever
    // asked.
    // THE PRODUCT HAS A SECOND LAUNCH BODY SINCE 2026-08-28, and it is
    // recorded here as well as at its own head: the RENDER PLAYER's
    // (GuiRenderPlayer::play_wav / toggle_pause / seek_to, render_player.h)
    // plays a decoded render over ITS OWN buffer and calls playback.play
    // directly, because everything this body seeds belongs to the project's
    // waveform, which the player does not display — the resting playhead
    // does not move, the scanner never runs, and the item's domain is the
    // buffer's own. The two share the ONE STOP BODY above, which carries the
    // player's fork.
    bool launch_playback_window(int64_t start, int64_t end);
};
