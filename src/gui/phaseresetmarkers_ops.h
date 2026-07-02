#pragma once

#include "app_state.h"
#include "playback_lifecycle.h"
#include "selection.h"
#include "undo.h"
#include "viewport.h"

#include <cstdint>

class GuiAudio;
struct GuiTargetRender;

// Phase reset authoring cluster. clear_hover_popup is reached through viewport;
// stop_playback_if_playing through playback_lifecycle.
struct GuiPhaseResetMarkersOps {
    AppState&             app;
    const GuiAudio&       audio;
    Viewport&             viewport;
    Selection&            selection;
    Undo&                 undo;
    GuiPlaybackLifecycle& playback_lifecycle;
    GuiTargetRender&   target_render;

    GuiPhaseResetMarkersOps(AppState&             app_,
                            const GuiAudio&       audio_,
                            Viewport&             viewport_,
                            Selection&            selection_,
                            Undo&                 undo_,
                            GuiPlaybackLifecycle& playback_lifecycle_,
                            GuiTargetRender&   target_render_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          undo(undo_),
          playback_lifecycle(playback_lifecycle_),
          target_render(target_render_) {}

    void drop_phase_reset_at_position(double time_seconds);
    void drop_phase_reset_at_playhead();
    void delete_selected_phase_reset();
    void toggle_phase_reset_disabled();
    std::pair<double, double> compute_phase_reset_delta_bounds(bool& ok);
    void nudge_selected_phase_resets(int direction);
};
