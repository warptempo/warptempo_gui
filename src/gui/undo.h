#pragma once

#include "app_state.h"
#include "playback_lifecycle.h"
#include "selection.h"
#include "tab_mode.h"
#include "viewport.h"

#include <vector>

// X.7.3: undo-cluster operations, extracted from main.cpp's inline lambdas.
// The struct holds references to the long-lived state the methods read and
// write; bodies are byte-identical to the originals modulo `this->` access
// on the captured references. clear_hover_popup is reached through
// viewport; stop_playback_if_playing is reached through playback_lifecycle;
// switch_active_tab_to is reached through tab_mode (so do_undo / do_redo
// can restore the originating A/B tab before applying the marker change).
struct Undo {
    AppState&             app;
    Viewport&             viewport;
    Selection&            selection;
    GuiPlaybackLifecycle& playback_lifecycle;
    GuiTabMode&           tab_mode;

    Undo(AppState&             app_,
         Viewport&             viewport_,
         Selection&            selection_,
         GuiPlaybackLifecycle& playback_lifecycle_,
         GuiTabMode&           tab_mode_)
        : app(app_),
          viewport(viewport_),
          selection(selection_),
          playback_lifecycle(playback_lifecycle_),
          tab_mode(tab_mode_) {}

    void recompute_dirty();
    void push_undo(std::vector<GuiWarpMarker> pre_state, OpKind op_kind,
                   int hint_last);
    void push_undo_phase_reset(std::vector<GuiPhaseResetMarker> pre_state,
                             OpKind op_kind, int hint_last);
    void push_undo_both(std::vector<GuiWarpMarker> warp_pre,
                        std::vector<GuiPhaseResetMarker> trans_pre,
                        char op_mode, OpKind op_kind, int hint_last);
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
};
