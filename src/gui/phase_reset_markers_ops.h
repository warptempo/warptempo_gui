#pragma once

#include "app_state.h"
#include "playback_lifecycle.h"
#include "selection.h"
#include "undo.h"
#include "viewport.h"

#include <cstdint>

class GuiAudio;
struct GuiTargetIteration;

// X.7.4: phase reset authoring cluster, extracted from main.cpp's inline
// lambdas. clear_hover_popup is reached through viewport;
// stop_playback_if_playing through playback_lifecycle.
struct GuiPhaseResetMarkersOps {
    AppState&             app;
    const GuiAudio&       audio;
    Viewport&             viewport;
    Selection&            selection;
    Undo&                 undo;
    GuiPlaybackLifecycle& playback_lifecycle;
    GuiTargetIteration&   target_iteration;

    GuiPhaseResetMarkersOps(AppState&             app_,
                            const GuiAudio&       audio_,
                            Viewport&             viewport_,
                            Selection&            selection_,
                            Undo&                 undo_,
                            GuiPlaybackLifecycle& playback_lifecycle_,
                            GuiTargetIteration&   target_iteration_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          undo(undo_),
          playback_lifecycle(playback_lifecycle_),
          target_iteration(target_iteration_) {}

    void drop_phase_reset_at_position(double time_seconds);
    void drop_phase_reset_at_playhead();
    void delete_selected_phase_reset();
    void toggle_phase_reset_disabled();
    std::pair<double, double> compute_phase_reset_delta_bounds(bool& ok);
    void nudge_selected_phase_resets(int direction);
    void jump_phase_reset_selection_to_playhead();
};
