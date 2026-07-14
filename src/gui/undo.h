#pragma once

#include "active_views.h"
#include "app_state.h"
#include "playback_lifecycle.h"
#include "selection.h"
#include "viewport.h"

#include <chrono>
#include <cstdint>
#include <vector>

struct GuiTargetRender;

// Rapid-gesture undo coalescing. A burst of eligible keyboard gestures — warp
// / phase-reset nudges (Ctrl+Left/Right) and tempo cent steps (Ctrl+Up/Down
// and Ctrl+wheel) — that land within kGestureCoalesceMs of each other on the
// SAME gesture-kind AS CONSECUTIVE COMMANDS collapses into ONE undo entry: the
// burst's first press pushes the pre-burst snapshot and every continuation
// press SKIPS its own push, so a single Ctrl+Z reverts the whole burst. The
// visible move, defect validation, reorder/remap, dirty tracking, and the
// target-view preview stay per-press and unchanged — only the redundant
// history push is suppressed. Any intervening command breaks the burst (see
// command-adjacency below), so "same target / same tab / same history" all
// follow for free: changing any of those requires a command in between.
enum class GestureKind { None, WarpNudge, PhaseResetNudge, TempoStep };

// Coalesce window. Presses farther apart than this start a fresh undo entry.
// Named so it is easy to tune.
inline constexpr uint64_t kGestureCoalesceMs = 150;

// The previous eligible commit's coalesce key: its gesture-kind, its steady
// timestamp, and the command_seq it committed at. A later press merges only
// when it is the same kind, inside the window, and the immediately-next command
// (command_seq == this + 1). Session-only, never serialized.
struct GestureCoalesce {
    GestureKind kind        = GestureKind::None;
    uint64_t    last_ms     = 0;  // steady_clock ms of the previous commit
    uint64_t    command_seq = 0;  // app.command_seq at the previous commit
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
    void push_undo_warp(std::vector<GuiWarpMarker> pre_state, int hint_last,
                        bool affects_persistence = true);
    void push_undo_phase_reset(std::vector<GuiPhaseResetMarker> pre_state,
                             int hint_last);
    // tab_override attributes entries pushed on behalf of a tab the caller is
    // about to switch to. The entry belongs to the edit's semantic tab, not
    // the incidental tab the cursor is in when history is pushed.
    void push_undo_both(std::vector<GuiWarpMarker> warp_pre,
                        std::vector<GuiPhaseResetMarker> phase_reset_pre,
                        char op_mode, int hint_last, char tab_override = 0);
    // Settings-only undo entry. op_mode='S' marks it as settings-class so
    // do_undo / do_redo skip the mode-switch and post-restore-rules
    // dispatch. Markers are captured wholesale at push time (carry-
    // everywhere shape) so the restore is symmetric with marker entries.
    void push_settings_undo(SettingsSnapshot pre_state);
    void apply_post_restore_rules_warp(const UndoEntry& entry,
                                       const std::vector<GuiWarpMarker>& before);
    void apply_post_restore_rules_phase_reset(const UndoEntry& entry,
                                            const std::vector<GuiPhaseResetMarker>& before);
    // True when do_undo would actually act (non-empty stack, top entry's
    // target tab writable). do_undo's authoritative guard, and the
    // predicate the defect-resolution [U]ndo offer consults so the option
    // appears only when it will act.
    bool can_undo() const;
    void do_undo();
    void do_redo();

    // Rapid-gesture undo-coalescing state (see GestureCoalesce above).
    GestureCoalesce gesture_coalesce;

    // Whether the current eligible gesture press of `kind` coalesces into the
    // burst's existing undo entry. When it returns true the caller SKIPS its
    // undo push (and calls note_coalesced_commit for the per-press commit
    // funnel); either way the caller then calls record_gesture. Call at handler
    // entry — after the on_key/on_wheel command_seq bump the eligible press
    // itself carries, so its own command is the one adjacency compares.
    bool coalesce_gesture(GestureKind kind) const;
    // Record this eligible press as the burst's latest: its kind, now, and the
    // current app.command_seq. Call after the push / skip.
    void record_gesture(GestureKind kind);
    // Per-press commit funnel for a coalesced (push-skipped) press: the store
    // still changed, so run the same defect-validation flag and hover-popup
    // clear that the push_undo_* helpers do, minus the history push.
    void note_coalesced_commit();
};
