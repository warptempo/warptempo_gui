#pragma once

#include "app_state.h"
#include "playback_lifecycle.h"
#include "selection.h"
#include "undo.h"
#include "viewport.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

class GuiAudio;
struct GuiWarpMarker;
struct GuiTargetRender;

// Index of the nearest non-disabled marker strictly before `time_frame`,
// or -1 if none. See the definition in warpmarkers_ops.cpp for the full
// doc comment (matches the resolver's walk).
int find_immediate_prior(const std::vector<GuiWarpMarker>& mv,
                          double time_frame);

// Warp-authoring cluster. Covers the basic authoring operations (drop /
// delete / toggle / adjust) and the pixel-column-anchored nudge.
// stop_playback_if_playing is reached through playback_lifecycle. The
// reposition drag is no longer here: it is the one cross-kind gesture and
// lives in MarkerDragOps in marker_drag.{h,cpp}.
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

    void drop_marker(double time_frame, bool inherit,
                      int64_t tempo_cents, std::optional<double> scale);
    void drop_marker_at_playhead();
    void drop_copy_previous_at_playhead();
    void delete_selected_marker();
    void toggle_inherits();
    void toggle_disabled();
    // Steps the focused marker's tempo by `delta_cents` integer cents (one
    // cent per keypress / wheel detent, signed by direction of travel).
    void adjust_tempo_cents(int64_t delta_cents);
    void nudge_selected_markers(int direction);
};
