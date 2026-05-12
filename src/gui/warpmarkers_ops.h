#pragma once

#include "app_state.h"
#include "playback_lifecycle.h"
#include "selection.h"
#include "undo.h"
#include "viewport.h"

#include <utility>

class GuiAudio;
class GuiPlatform;

// X.7.5a: warp-authoring cluster, extracted from main.cpp's inline
// lambdas. Covers the basic authoring lambdas (drop / delete / toggle /
// adjust / clear), the drag cluster (begin / apply / commit, mode-aware
// across warp and phase reset lists), and the selection-shift cluster
// (nudge / jump-to-playhead and their shared bounds helper).
// clear_hover_popup is reached through viewport; stop_playback_if_playing
// through playback_lifecycle.
//
// The drag methods stay mode-aware: warp drag is the dominant case and
// phase reset drag was bolted on later. apply_drag_motion's phase reset
// branch reaches the free-function apply_phase_reset_position_delta
// (declared in phase_reset_markers_ops.h). The GuiPlatform reference is for
// apply_drag_motion's gui.invalidate_region calls during drag.
struct GuiWarpMarkersOps {
    AppState&             app;
    const GuiAudio&       audio;
    GuiPlatform&          gui;
    Viewport&             viewport;
    Selection&            selection;
    Undo&                 undo;
    GuiPlaybackLifecycle& playback_lifecycle;

    GuiWarpMarkersOps(AppState&             app_,
                      const GuiAudio&       audio_,
                      GuiPlatform&          gui_,
                      Viewport&             viewport_,
                      Selection&            selection_,
                      Undo&                 undo_,
                      GuiPlaybackLifecycle& playback_lifecycle_)
        : app(app_),
          audio(audio_),
          gui(gui_),
          viewport(viewport_),
          selection(selection_),
          undo(undo_),
          playback_lifecycle(playback_lifecycle_) {}

    void drop_marker(double time_seconds, bool inherit);
    void drop_marker_at_playhead();
    void drop_inherit_marker_at_playhead();
    void delete_selected_marker();
    void force_delete_selected_marker();
    void toggle_inherits();
    void toggle_disabled();
    void adjust_tempo(double delta);
    bool begin_drag(int hit, int mouse_x);
    void apply_drag_motion(double raw_delta);
    void commit_drag();
    std::pair<double, double> compute_selection_delta_bounds(bool& ok);
    bool apply_selection_shift(double raw_delta);
    void nudge_selected_markers(int direction);
    void jump_selection_to_playhead();
};
