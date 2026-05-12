#pragma once

#include "app_state.h"
#include "file_loader.h"
#include "phase_reset_propagate.h"
#include "platform_wayland.h"
#include "save_ops.h"
#include "viewport.h"

// X.7.10: prompt state machine, extracted from main.cpp's inline
// lambdas. Owns the unsaved-work dialog and the paste-confirm
// dialog. Two entry points are exposed: request_close_or_revert
// (called by Ctrl+Q, Ctrl+W, and the WM-close callback) and
// activate_response (called by the keyboard handler when a prompt
// is active). The other two former lambdas (open_unsaved, proceed)
// are private helpers; they have no callers outside this cluster.
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

private:
    void open_unsaved(DialogTrigger t);
    void proceed(DialogTrigger t);
};
