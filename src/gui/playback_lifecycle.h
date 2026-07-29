#pragma once

#include "app_state.h"
#include "audio.h"
#include "playback.h"
#include "platform_wayland.h"
#include "viewport.h"

// Playback-orchestration operations, extracted from main.cpp's inline lambdas.
// Owns the four GUI-level wrappers around GuiPlayback's mechanism: stop on
// gesture, restore the visible playhead at end-of-play, toggle play/stop, and
// apply a new speed. AppState, Viewport, GuiPlatform, and GuiAudio are
// captured directly. GuiPlayback stays a pure mechanism class — these
// operations live one layer up.
struct GuiPlaybackLifecycle {
    AppState&         app;
    const GuiAudio&   audio;
    GuiPlatform&      gui;
    GuiPlayback&      playback;
    Viewport&         viewport;

    GuiPlaybackLifecycle(AppState&         app_,
                         const GuiAudio&   audio_,
                         GuiPlatform&      gui_,
                         GuiPlayback&      playback_,
                         Viewport&         viewport_)
        : app(app_),
          audio(audio_),
          gui(gui_),
          playback(playback_),
          viewport(viewport_) {}

    void stop_playback_if_playing();

    // THE MODAL-OPEN PLAYBACK STOP, ONE OWNER (architect 2026-07-28, replacing
    // six hand-spelled stops). Called at the moment a modal surface ACTUALLY
    // opens, by every site that opens one: the `;` settings editor
    // (input_handler.cpp), the `'` render-commit editor and the `m` bpm editor
    // (input_key_dispatch.cpp), and the three prompt opens (prompt.cpp).
    // Authoring or answering a dialog over a live audition is the wrong default,
    // and Space is inside each of those surfaces' blocked sets, so playback
    // cannot restart until the surface closes.
    // THE DECISION TABLE lives here, so a new modal surface inherits an ANSWER
    // instead of an absence:
    //   * BOTTOM-STRIP modal surfaces — the three editors and the prompts — STOP.
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
    // (gesture stops, the S/T toggle, the adopt mutator's self-guard) keep
    // calling stop_playback_if_playing directly.
    void stop_playback_for_modal_open();

    // End scanner motion: deactivate the scanner and damage its last-painted
    // column. Two callers, one path by rule — Space's stop edge and the tick's
    // natural-end branch — because a stopped scanner is deactivated IMMEDIATELY
    // and no non-playing scanner-validity window exists (contract at the scanner
    // block in app_state.h).
    void restore_playhead_to_lsp();
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
    // TWO GESTURES, one mechanism — the waveform lower half's scrub act (the
    // START half of its stop-then-start) and, since 2026-07-29 (architect),
    // Space's REGION launch, which plays from the span's left bound without
    // writing the cursor precisely because auditions do not move it. Delegates
    // to the same launch body as toggle_playback's play edge, so the standing
    // gates apply identically: a frame outside the trim window / target buffer
    // domain, or one leaving fewer than two playable frames of remainder, is a
    // silent no-op — exactly Space's conventions. A live session never
    // launches (defensive; both callers reach here only with
    // playback stopped — a scrub act over a live session STOPS it and returns,
    // and Space's stop edge goes through toggle_playback).
    void scrub_launch_at(int64_t frame);
    void set_playback_speed(float s);

    // Reseek the active playback session to a new starting sample, keeping
    // audio alive. The sample is expressed in the active playhead domain
    // (source-domain in source view; target-domain in target view). Handles
    // the target-view target_buffer translation internally. Caller is
    // responsible for the entry-state check — playback alive AND the position
    // actually moving (the upper-half placement press, the ONE caller,
    // compares the sample against the entry playhead); this function
    // unconditionally reseeks when called. The scrub paths no longer come
    // here — a scrub act only stops or launches (scrub_act_at). For target
    // view, samples
    // outside the target buffer's range fall back to playback.stop() —
    // keep-alive intent is well-defined for in-range positions only.
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
    // absolute paint-domain launch position, capture the loop verdict, seed
    // the scanner, and start the audio. Returns whether it launched. Callers
    // (toggle_playback's play edge, scrub_launch_at) run the defensive
    // follow-override clear before delegating.
    bool launch_playback_from(int64_t launch_pos);
};
