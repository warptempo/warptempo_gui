#pragma once

#include "active_views.h"
#include "app_state.h"
#include "notifications.h"
#include "undo.h"

// Save-pipeline operations, extracted from main.cpp's
// save_markers lambda. Coordinates the three on-disk writes
// (.warpmarkers, .phaseresetmarkers, .settings) and the per-save
// bookkeeping (active-tab snapshot refresh, then Undo::note_saved — the
// history's mark_saved, the coalescing stamp's clear and the dirty-flag
// refold, one tail). The .warpmarkers write is the primary
// target; the .phaseresetmarkers write is a sibling; the .settings
// write is required too, so any of the three failures keeps the save dirty.
//
// NO Viewport REFERENCE any more (2026-08-01): a save paints nothing. Its one
// damage request was the bottom row's dirty-dot cell, and the dot moved to the
// window title, which the compositor repaints.
//
// ONE REFUSAL IS NOT ABOUT THE DATA (2026-08-08): a save is refused outright
// while a Save-and-Commit checkpoint is publishing, because that background act
// writes these same three paths in the coincident projects/<id>/ workflow. The
// term lives at the top of save() — one place, every caller — and the Save
// button's "Committing..." face is its mirror.
//
// A FAILED SAVE SAYS SO HERE, AT THE OWNER (architect 2026-09-02): the three
// write arms and the numeric-locale refusal raise their own normal card, so
// every caller inherits the sentence and none of them composes a second one
// (the two callers that READ the bool decide with it — the quit prompt's Retry
// rung and the checkpoint prelude — and neither cards). That is why the struct
// takes a GuiNotifications, the arrangement every other ops struct here uses:
// the sentence is composed where the fact is.
struct GuiSaveOps {
    AppState&         app;
    Undo&             undo;
    GuiActiveViews&   active_views;
    GuiNotifications& notifications;

    GuiSaveOps(AppState&         app_,
               Undo&             undo_,
               GuiActiveViews&   active_views_,
               GuiNotifications& notifications_)
        : app(app_),
          undo(undo_),
          active_views(active_views_),
          notifications(notifications_) {}

    bool save();
};
