#pragma once

#include "app_state.h"
#include "phase_reset_propagate.h"
#include "platform_wayland.h"
#include "playback_lifecycle.h"
#include "save_ops.h"
#include "viewport.h"

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

    void request_close();
    void activate_response(char k);

    // (NO COMMIT CONFIRMATION HERE ANY MORE. The `h` history view's
    // Save-and-Commit act was guarded by a fourth prompt — HISTORY_COMMIT, one
    // question with `y` and Esc — until 2026-08-07, when the architect replaced
    // it with the COMMIT-TITLE EDITOR: the act asks for the message instead of
    // asking for permission, and a bare Enter over the prefilled default is the
    // old `y`. The prompt kind, this opener and the back-pointer to the input
    // handler that its `y` reached the act through are all deleted; the editor
    // lives with the mode's other machinery, at
    // GuiInputHandler::open_history_commit_editor.)

    // Dismiss-only modal error notice in the bottom strip (ERROR_NOTICE).
    // `text` is displayed verbatim — callers pass the owner's own error
    // string, unmodified. While active it is modal exactly like the other
    // prompts: mouse swallowed, keyboard answers; Esc acknowledges.
    // Covers the environmental and tripwire-class refusals. Callers: the
    // target-view entry gate (its resolve/build chain — the engine-metadata /
    // non-positive-tempo-product class; marker arrangements always enter —
    // the parser resolver normalizes them, and trim plays no part), the
    // iteration-sweep cell-cap refusal, and since 2026-08-07 THE CHECKPOINT
    // ACT'S FAILURE REPORT (the act runs on a worker, so its failure has no
    // gesture of its own to refuse and needs a surface instead).
    // THAT ONE CALLER IS THE ONLY ASYNC ONE — dispatched by no key and no
    // gesture, and therefore past none of the gates that clear the way for the
    // others — so it is the only one that has to make room for itself, which it
    // does in three classes (re-derived 2026-08-09;
    // GuiInputHandler::maybe_open_pending_history_notice owns the partition and
    // the reasoning): it PARKS for a surface in use (a prompt, any of the five
    // editors), CLOSES a transient popup (the open dropdown, with the floating
    // hint), and PARKS for an act in flight (any live pointer gesture — it
    // cannot be closed, gestures having no cancel by ruling, and force-ending
    // one from a worker's clock would commit a drag the user never released).
    // It also drops the two RELEASE-OWNED SCRAPS the force-end finalizer drops,
    // since the prompt gate is about to swallow the release that owned them.
    // Every other caller here rides a gesture or a key whose own gate has
    // already dealt with all of it.
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
