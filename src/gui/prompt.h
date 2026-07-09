#pragma once

#include "app_state.h"
#include "file_loader.h"
#include "phase_reset_propagate.h"
#include "platform_wayland.h"
#include "save_ops.h"
#include "viewport.h"

// Prompt state machine, extracted from main.cpp's inline lambdas. Owns the
// unsaved-work dialog and the paste-confirm dialog. Two entry points are
// exposed: request_close_or_revert (called by Ctrl+Q, Ctrl+W, and the WM-close
// callback) and activate_response (called by the keyboard handler when a
// prompt is active). The other two former lambdas (open_unsaved, proceed) are
// private helpers; they have no callers outside this cluster.
//
// save_markers is reached through save_ops; clear_hover_popup
// through viewport. file_loader, viewport, phase_reset_propagate,
// and gui are reached directly.
struct GuiPrompt {
    AppState&            app;
    GuiPlatform&         gui;
    Viewport&            viewport;
    GuiFileLoader&       file_loader;
    PhaseResetPropagate& phase_reset_propagate;
    GuiSaveOps&          save_ops;

    GuiPrompt(AppState&            app_,
              GuiPlatform&         gui_,
              Viewport&            viewport_,
              GuiFileLoader&       file_loader_,
              PhaseResetPropagate& phase_reset_propagate_,
              GuiSaveOps&          save_ops_)
        : app(app_),
          gui(gui_),
          viewport(viewport_),
          file_loader(file_loader_),
          phase_reset_propagate(phase_reset_propagate_),
          save_ops(save_ops_) {}

    void request_close_or_revert(DialogTrigger t);
    void activate_response(char k);

    // Dismiss-only modal error notice in the bottom strip (ERROR_NOTICE).
    // `text` is displayed verbatim — callers pass the owner's own error
    // string, unmodified. While active it is modal exactly like the other
    // prompts: mouse swallowed, keyboard answers; Esc acknowledges.
    // The defect-resolution modal series (DEFECT_RESOLUTION) is the primary
    // surface for authoring defects; this popup is the backstop plus the
    // environmental and settings-choice refusals. Callers: the render
    // dispatch pre-flight (the resolve/build/trim backstop for failures the
    // defect enumerator does not model, the map-format-with-trim refusal —
    // a settings choice, not corruption — and the queued-snapshot backstop)
    // and the target-view validity gate (entry refusal and the
    // invalidating-edit kick, both only when the defect series does not
    // model the failure).
    void open_error_notice(std::string text);

    // Real abandon for an active PASTE_CONFIRM prompt: dismiss the
    // prompt and clear the pending paste anchor. Called from
    // activate_response on Esc, and from the Ctrl+Q / Ctrl+W
    // interception in input_handler so both cancels go through one
    // path (no synthesized Esc keystroke). Safe to call only when a
    // PASTE_CONFIRM prompt is up.
    void cancel_paste_confirmation();

private:
    void open_unsaved(DialogTrigger t);
    void proceed(DialogTrigger t);
};
