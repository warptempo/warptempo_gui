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
// NO Viewport REFERENCE (2026-08-01): a save paints nothing of its own. Its
// one picture is the Save button greying as the flag falls (architect
// 2026-10-05, SAVE IS THE DIRTY MARK: plain_save_actionable, app_state.h),
// which the roster's per-tick face comparator repaints (main.cpp).
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

    // THE KEYS' PLAIN SAVE (architect 2026-10-05, SAVE IS THE DIRTY MARK):
    // every Ctrl+S road outside the `h` view — on_key's arm, the editors'
    // two modal arms (route_modal_editor_key) and the picker's router —
    // reaches the owner through this, which asks plain_save_actionable
    // (app_state.h, the ruling and its trade-off) first. WITH NOTHING TO
    // SAVE THE PRESS IS SILENT: no write and no card, the button's grey being
    // the answer — the `h` view's own Ctrl+S over an empty head delta is the
    // precedent (notifications.h's one-thing-in-one-place silences). Ctrl+S is
    // a chord the product binds (chord_is_bound), and this arm consumes it,
    // so the unbound-key deduction never sees it. A dirty session takes save()
    // whole, its in-flight card and write cards included. The close prompt's
    // Save answer and the checkpoint act's prelude call save() itself.
    void save_from_key();
};
