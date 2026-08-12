#pragma once

#include "app_state.h"
#include "audio.h"
#include "playback.h"
#include "viewport.h"

// Playback-orchestration operations, extracted from main.cpp's inline lambdas.
// Owns the GUI-level wrappers around GuiPlayback's mechanism: the one stop body
// (both stop edges and every gesture stop), the modal-open stop that names it,
// toggle play/stop, the audition launch, the keep-alive reseek, follow mode, and
// the speed set. AppState, Viewport and GuiAudio are captured directly.
// GuiPlayback stays a pure mechanism class — these operations live one layer up.
// (No GuiPlatform& member. The only direct platform reach this cluster ever had
// was restore_playhead_to_lsp's top-strip invalidate, deleted with that function
// 2026-07-30; every damage this cluster emits now goes through Viewport.)
struct GuiPlaybackLifecycle {
    AppState&         app;
    const GuiAudio&   audio;
    GuiPlayback&      playback;
    Viewport&         viewport;

    GuiPlaybackLifecycle(AppState&         app_,
                         const GuiAudio&   audio_,
                         GuiPlayback&      playback_,
                         Viewport&         viewport_)
        : app(app_),
          audio(audio_),
          playback(playback_),
          viewport(viewport_) {}

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
    //   * TRIM MUTATIONS STOP, IN BOTH VIEWS: `x` and Shift+X, matching every
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
    // opens. THE CALLER INVENTORY, re-derived by grep 2026-08-09 — SIX sites:
    // GuiSettingsEditor::open (settings_editor.cpp), the `'` load editor,
    // the `m` bpm editor and the history view's COMMIT-TITLE editor
    // (input_key_dispatch.cpp), and the TWO prompt opens
    // (prompt.cpp: unsaved, error notice). It went seven to six on 2026-08-09,
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
    // ONE MODAL OPEN IS NOT A CALLER, and it is a recorded exception rather than
    // a gap: the PASTE_CONFIRM prompt is built outside prompt.cpp
    // (PhaseResetPropagate::open_paste_confirmation) and stops through
    // stop_playback_if_playing directly, which is mechanically this same stop —
    // so the RULE holds for every modal surface even though this function's
    // caller set is not literally every opener.
    // Authoring or answering a dialog over a live audition is the wrong default,
    // and Space is inside each of those surfaces' blocked sets, so playback
    // cannot restart until the surface closes.
    // THE DECISION TABLE lives here, so a new modal surface inherits an ANSWER
    // instead of an absence:
    //   * BOTTOM-STRIP modal surfaces — the four editors and the prompts — STOP.
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
    // at or past the buffer end is a silent no-op.
    void toggle_playback(int64_t launch_offset = 0);
    // THE AUDITION LAUNCH ENTRY: launch the scanner from `frame`, an ABSOLUTE
    // position in the active paint domain, leaving the resting cursor untouched.
    // ONE CALLER CLASS since 2026-07-30 — the waveform SCRUB act (the
    // START half of its stop-then-start), which is also the gesture for
    // previewing a resting region: click inside the span and it auditions from
    // there. That act has ONE press entry since 2026-08-12 (the lower-half
    // plain left press — the bare right full-height entry died with the right
    // button's unbinding), funneled through scrub_act_at, so this stays
    // one caller class. (Space's region left-bound launch was the second caller until the
    // architect dropped it 2026-07-30; Space now always toggles from the playhead.)
    // Delegates
    // to the same launch body as toggle_playback's play edge, so the standing
    // gates apply identically: a frame outside the active view's range — the
    // SONG in source view, the target buffer's domain in target view — or one
    // leaving fewer than two playable frames of remainder, is a silent no-op —
    // exactly Space's conventions. A live session never
    // launches (defensive; the caller reaches here only with
    // playback stopped — a scrub act over a live session STOPS it and returns).
    void scrub_launch_at(int64_t frame);
    void set_playback_speed(float s);

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
    // The shared launch body (contract at the definition): validate an
    // absolute paint-domain launch position, seed
    // the scanner, and start the audio. Returns whether it launched. Callers
    // (toggle_playback's play edge, scrub_launch_at) run the defensive
    // follow-override clear before delegating.
    bool launch_playback_from(int64_t launch_pos);
};
