#pragma once

#include "app_state.h"
#include "phase_reset_propagate.h"
#include "platform_wayland.h"
#include "playback_lifecycle.h"
#include "save_ops.h"
#include "viewport.h"

struct GuiInputHandler;

// Prompt state machine, extracted from main.cpp's inline lambdas. Owns the
// unsaved-work dialog and the paste-confirm dialog. Two entry points are
// exposed: request_close (called by Ctrl+Q and the WM-close
// callback) and activate_response (called by the keyboard handler when a
// prompt is active). The other two former lambdas (open_unsaved, proceed) are
// private helpers; they have no callers outside this cluster.
//
// save_markers is reached through save_ops. viewport, phase_reset_propagate,
// and gui are reached directly.
struct GuiPrompt {
    AppState&             app;
    GuiPlatform&          gui;
    Viewport&             viewport;
    PhaseResetPropagate&  phase_reset_propagate;
    GuiSaveOps&           save_ops;
    GuiPlaybackLifecycle& playback_lifecycle;

    GuiPrompt(AppState&             app_,
              GuiPlatform&          gui_,
              Viewport&             viewport_,
              PhaseResetPropagate&  phase_reset_propagate_,
              GuiSaveOps&           save_ops_,
              GuiPlaybackLifecycle& playback_lifecycle_)
        : app(app_),
          gui(gui_),
          viewport(viewport_),
          phase_reset_propagate(phase_reset_propagate_),
          save_ops(save_ops_),
          playback_lifecycle(playback_lifecycle_) {}

    // Back-pointer to the input handler, wired in main.cpp after both are
    // constructed (the input handler holds this prompt by reference, so the
    // cycle is resolved with a pointer set there — the settings editor's and the
    // phase-reset propagate's own arrangement). ONE reader: the history commit
    // prompt's `y`, which runs GuiInputHandler::run_history_commit. Null until
    // the wiring runs, which is before any event can be dispatched.
    GuiInputHandler* input = nullptr;

    void request_close();
    void activate_response(char k);

    // THE HISTORY MODE'S COMMIT CONFIRMATION (HISTORY_COMMIT) — the product's
    // fourth prompt and the only one guarding a write outside this session.
    // `commit_title` is the message the commit will carry, from the act's own
    // owner (history_checkpoint_title) so the question and the deed cannot name
    // different things, and `repo` is the projects_repo setting verbatim. `y`
    // runs the act; Esc abandons with nothing written. The commit message
    // itself is not asked about (architect: it is derived, not chosen).
    void open_history_commit_confirm(const std::string& commit_title,
                                     const std::string& repo);

    // Dismiss-only modal error notice in the bottom strip (ERROR_NOTICE).
    // `text` is displayed verbatim — callers pass the owner's own error
    // string, unmodified. While active it is modal exactly like the other
    // prompts: mouse swallowed, keyboard answers; Esc acknowledges.
    // Covers the environmental and tripwire-class refusals. Callers: the
    // target-view entry gate (its resolve/build chain — the engine-metadata /
    // non-positive-tempo-product class; marker arrangements always enter —
    // the parser resolver normalizes them, and trim plays no part) and the
    // iteration-sweep cell-cap refusal.
    void open_error_notice(std::string text);

    // Load-time render-environment mismatch (ENV_HASH_MISMATCH), advisory
    // only. `changed_list` is the comma+space-joined subset of
    // `libm, libmvec, fftw3, fftw3_threads` whose stored hash mismatched the
    // running environment's. ONE response:
    // 'o' stamps all four stored hashes to the current environment's
    // (history-less, no-dirty GUI-kind state; the next ordinary Ctrl+S
    // persists it). There is no dismiss-without-ack path — Esc is not a
    // response key, so the prompt's key filter swallows it and acknowledging is
    // the only way past the prompt. Never blocks or invalidates a render.
    void open_env_hash_mismatch(const std::string& changed_list);

    // Real abandon for an active PASTE_CONFIRM prompt: dismiss the
    // prompt and clear the pending paste anchor. Called from
    // activate_response on Esc, and from the Ctrl+Q interception in
    // input_handler so both cancels go through one path (no synthesized
    // Esc keystroke). Safe to call only when a
    // PASTE_CONFIRM prompt is up.
    void cancel_paste_confirmation();

private:
    void open_unsaved(DialogTrigger t);
    void proceed(DialogTrigger t);
};
