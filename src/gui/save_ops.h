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
// target; the phase-reset file is its sibling; the .settings
// write is required too, so any of the three failures keeps the save dirty.
//
// NO Viewport REFERENCE any more (2026-08-01): a save paints nothing of its
// own. Its one damage request was the bottom row's dirty-mark cell, and that
// damage now belongs to the derive owner instead: Undo::recompute_dirty (the
// note_saved tail below) invalidates the row ON THE TRANSITION alone, so a
// save that actually cleans the flag is repainted by it and one that changes
// nothing costs no repaint. Row 8's `*` is the mark's one surface (2026-09-09,
// the window title's second asterisk deleted with it).
//
// ONE REFUSAL IS NOT ABOUT THE DATA (2026-08-08): a save is refused outright
// while a Save-and-Commit checkpoint is publishing, because that background act
// writes these same three paths in the coincident projects/<id>/ workflow. The
// term lives at the top of save() — one place, every caller — and since
// 2026-09-24 that arm cards kCheckpointPublishing (notifications.h) itself,
// ahead of the three write arms below: the retired "Committing..." button
// label (dead at the 2026-08-12 relayout) and its 2026-09-12 tooltip
// successor both answered with a face alone, and the architect ruled a card
// preferred over a no-op that falsely advertises an action. All seven
// callers — main Ctrl+S, the close prompt's Save answer, the history-entry
// prelude, the two modal-editor Ctrl+S arms, the picker's and the stats
// panel's — reach this one arm and inherit its card without composing their
// own.
//
// A FAILED WRITE SAYS SO HERE TOO, AT THE OWNER (architect 2026-09-02): past
// the checkpoint guard above, the three write arms and the numeric-locale
// refusal each raise their own normal card, so every caller inherits the
// sentence and none of them composes a second one (the two callers that READ
// the bool decide with it — the quit prompt's Retry rung and the checkpoint
// prelude — and neither cards). That is why the struct takes a
// GuiNotifications, the arrangement every other ops struct here uses: the
// sentence is composed where the fact is.
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
