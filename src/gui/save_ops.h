#pragma once

#include "app_state.h"
#include "tab_mode.h"
#include "undo.h"
#include "viewport.h"

// X.7.12: save-pipeline operations, extracted from main.cpp's
// save_markers lambda. Coordinates the three on-disk writes
// (.warpmarkers, .phaseresetmarkers, .settings) and the per-save
// bookkeeping (active-tab snapshot refresh, history mark_saved,
// dirty-flag refold). The .warpmarkers write is the primary
// target; the .phaseresetmarkers write is a sibling; the .settings
// write is best-effort and its failure does not fail the call.
struct GuiSaveOps {
    AppState&     app;
    Undo&         undo;
    GuiTabMode&   tab_mode;
    Viewport&     viewport;

    GuiSaveOps(AppState&     app_,
               Undo&         undo_,
               GuiTabMode&   tab_mode_,
               Viewport&     viewport_)
        : app(app_),
          undo(undo_),
          tab_mode(tab_mode_),
          viewport(viewport_) {}

    bool save();
};
