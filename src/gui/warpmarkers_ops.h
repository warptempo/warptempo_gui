#pragma once

#include "app_state.h"
#include "playback_lifecycle.h"
#include "selection.h"
#include "undo.h"
#include "viewport.h"

#include <utility>

class GuiAudio;
struct GuiTargetRender;

// Warp-authoring cluster. Covers the basic authoring operations (drop /
// delete / toggle / adjust) and the selection-shift cluster (nudge /
// jump-to-playhead and their shared bounds helper). stop_playback_if_playing
// is reached through playback_lifecycle. The reposition drag is no longer
// here: it is the one cross-kind gesture and lives in MarkerDragOps in
// marker_drag.{h,cpp}.
struct GuiWarpMarkersOps {
    AppState&             app;
    const GuiAudio&       audio;
    Viewport&             viewport;
    Selection&            selection;
    Undo&                 undo;
    GuiPlaybackLifecycle& playback_lifecycle;
    GuiTargetRender&   target_render;

    GuiWarpMarkersOps(AppState&             app_,
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

    void drop_marker(double time_seconds, bool inherit);
    void drop_marker_at_playhead();
    void drop_inherit_marker_at_playhead();
    void delete_selected_marker();
    void force_delete_selected_marker();
    void toggle_inherits();
    void toggle_disabled();
    void adjust_tempo(double delta);
    std::pair<double, double> compute_selection_delta_bounds(bool& ok);
    bool apply_selection_shift(double raw_delta);
    void nudge_selected_markers(int direction);
    void jump_selection_to_playhead();
};
