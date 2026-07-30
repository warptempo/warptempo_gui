#pragma once

#include "active_views.h"
#include "app_state.h"
#include "playback_lifecycle.h"
#include "selection.h"
#include "viewport.h"

#include <vector>

struct GuiTargetRender;

// HELD-KEY undo coalescing, BY REPEAT IDENTITY. A burst of eligible keyboard
// gestures — THREE of them, re-derived 2026-07-29 when the W+target tempo-IMAGE
// step was deleted with the whole tempo-image family (marker_drag.h): the warp and
// phase-reset position nudges (bare Left/Right in the
// marker lane) and the tempo cent step (bare Up/Down; no wheel route). A burst
// collapses into ONE undo
// entry when it is ONE HELD KEY: the PHYSICAL press pushes the pre-burst
// snapshot, and every SYNTHESIZED REPEAT of that press (GuiInputState::
// synthesized_repeat, set only in GuiPlatform::maybe_fire_repeat) SKIPS its own
// push, so a single Ctrl+Z reverts the whole hold. The visible move,
// reorder/remap, dirty tracking, and the target-view preview stay per-press and
// unchanged — only the redundant history push is suppressed.
//
// A hold therefore coalesces BY CONSTRUCTION, for any compositor at any
// key-repeat delay: there is no wall-clock window and no command counter, so
// nothing here can drift out of sync with the desktop's repeat configuration.
// Rapid MANUAL taps are separately undoable — the literal gesture boundary is
// "did you let go of the key".
//
// "Same target / same tab / same history" all still follow for free, and so does
// "the top of the undo stack is this burst's entry": a synthesized repeat can
// only arrive while the hold is still armed, and the platform's repeat contract
// disarms the hold at every intervening pointer press, key press, and completed
// wheel emission (layer (1), stated at maybe_fire_repeat), so no command can run
// between a burst's physical press and its repeats.
// THE ELIGIBLE KINDS, one per coalescing gesture plus None: the two position
// nudges and the Up/Down cent step (TempoStep, singleton and group — its own kind
// keeps a nudge burst and a tempo burst separate). TempoImageStep was a fourth
// until 2026-07-29 and went caller-less with the tempo-image family's deletion
// (marker_drag.h).
enum class GestureKind {
    None, WarpNudge, PhaseResetNudge, TempoStep
};

// Undo-cluster operations, extracted from main.cpp's inline lambdas.
// The struct holds references to the long-lived state the methods read and
// write; bodies are byte-identical to the originals modulo `this->` access
// on the captured references. clear_hover_popup is reached through
// viewport; stop_playback_if_playing is reached through playback_lifecycle;
// switch_active_tab_view_to is reached through active_views (so do_undo / do_redo
// can restore the originating A/B tab before applying the marker change).
struct Undo {
    AppState&             app;
    Viewport&             viewport;
    Selection&            selection;
    GuiPlaybackLifecycle& playback_lifecycle;
    GuiActiveViews&       active_views;
    GuiTargetRender&   target_render;

    Undo(AppState&             app_,
         Viewport&             viewport_,
         Selection&            selection_,
         GuiPlaybackLifecycle& playback_lifecycle_,
         GuiActiveViews&       active_views_,
         GuiTargetRender&   target_render_)
        : app(app_),
          viewport(viewport_),
          selection(selection_),
          playback_lifecycle(playback_lifecycle_),
          active_views(active_views_),
          target_render(target_render_) {}

    void recompute_dirty();
    // touched_snapshot / touched_live are the position movers' identity hints
    // (UndoEntry — the reposition drag and the two nudges, one marker each):
    // defaulted empty for every other caller, which then uses the
    // diff-based touched-set reconstruction in the post-restore rules.
    void push_undo_warp(std::vector<GuiWarpMarker> pre_state,
                        bool affects_persistence = true,
                        std::vector<int> touched_snapshot = {},
                        std::vector<int> touched_live = {});
    void push_undo_phase_reset(std::vector<GuiPhaseResetMarker> pre_state,
                             std::vector<int> touched_snapshot = {},
                             std::vector<int> touched_live = {});
    // tab_override attributes entries pushed on behalf of a tab the caller is
    // about to switch to. The entry belongs to the edit's semantic tab, not
    // the incidental tab the cursor is in when history is pushed.
    void push_undo_both(std::vector<GuiWarpMarker> warp_pre,
                        std::vector<GuiPhaseResetMarker> phase_reset_pre,
                        char op_mode, char tab_override = 0);
    // Settings-only undo entry. op_mode='S' marks it as settings-class so
    // do_undo / do_redo skip the mode-switch and post-restore-rules
    // dispatch. Markers are captured wholesale at push time (carry-
    // everywhere shape) so the restore is symmetric with marker entries.
    void push_settings_undo(SettingsSnapshot pre_state);
    void apply_post_restore_rules_warp(const UndoEntry& entry,
                                       const std::vector<GuiWarpMarker>& before);
    void apply_post_restore_rules_phase_reset(const UndoEntry& entry,
                                            const std::vector<GuiPhaseResetMarker>& before);
    void do_undo();
    void do_redo();

    // Whether the current eligible gesture press of `kind` coalesces into the
    // burst's existing undo entry. `synthesized_repeat` is the press's own
    // platform bit (GuiInputState::synthesized_repeat), threaded from the key
    // event that reached the handler. When it returns true the caller SKIPS its
    // undo push (and calls note_coalesced_commit for the per-press side
    // effects); either way the caller then calls record_gesture. A const query —
    // callable anywhere before record_gesture, in any order with the handler's
    // own refusals.
    bool coalesce_gesture(GestureKind kind, bool synthesized_repeat) const;
    // Record this eligible press as the burst's latest by KIND. Call after the
    // push / skip.
    void record_gesture(GestureKind kind);
    // Per-press side effects for a coalesced (push-skipped) press: the same
    // hover-popup clear the push_undo_* helpers do, minus the history push.
    void note_coalesced_commit();
    // Refresh the coalesced burst entry's touched_live to a continuation press's
    // LATEST post-reorder indices (the position nudges, which reorder — the
    // tempo step never does). The surviving first-press undo entry keeps its
    // touched_snapshot (the pre-burst snapshot coordinates a restore produces),
    // but its touched_live — the coordinates a redo of the whole coalesced op must
    // re-select — must track the final after-state as later presses reorder
    // further. Repeat identity (the coalesce precondition) guarantees the top of
    // the undo stack IS this burst's entry — no command can run between the
    // physical press that pushed it and the repeats that merge into it; a no-op on
    // an empty stack (defensive).
    void refresh_coalesced_touched_live(std::vector<int> touched_live);

  private:
    // The previous eligible commit's gesture kind — the whole coalescing state,
    // session-only and never serialized. A repeat merges only into a burst of its
    // OWN kind, which is what keeps a nudge burst and a tempo-step burst separate.
    // TWO writers: record_gesture stamps it at each eligible commit, and every
    // route that changes the undo-stack top CLEARS it — the four push helpers plus
    // the do_undo/do_redo restore core (the reason is at coalesce_gesture: it stops
    // a stale stamp from outliving the entry it named).
    GestureKind last_gesture_kind_ = GestureKind::None;

    // Shared authoritative guard for do_undo / do_redo: true when the step
    // would actually act (non-empty source stack, top entry's target tab
    // writable). See the rationale block at the definition.
    bool history_entry_actionable(const std::vector<UndoEntry>& stack) const;
    // Direction-parameterized restore core shared by do_undo / do_redo. Pops
    // `from`, pushes the live-state counter-entry onto `to`, and applies the
    // whole common restore body; saved_distance_delta is +1 for undo, -1 for
    // redo. Callers guard with history_entry_actionable first.
    void restore_history_entry(std::vector<UndoEntry>& from,
                               std::vector<UndoEntry>& to,
                               int saved_distance_delta);
};
