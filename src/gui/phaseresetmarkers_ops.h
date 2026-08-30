#pragma once

#include "app_state.h"
#include "playback_lifecycle.h"
#include "warpmarkers_ops.h"   // GuiOpRefusal, the shared refusal-reason
                               // channel (its contract is stated there)
#include "selection.h"
#include "undo.h"
#include "viewport.h"

class GuiAudio;
struct GuiTargetRender;

// Phase reset authoring cluster. Damage and viewport mutation are reached
// through viewport; stop_playback_if_playing through playback_lifecycle.
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

    void drop_phase_reset_at_position(double time_frame);
    void drop_phase_reset_lead_in_at_playhead();
    void delete_selected_phase_reset();
    void toggle_phase_reset_disabled();
    // `synthesized_repeat` is the dispatching key event's platform repeat bit,
    // read only by the undo-coalesce verdict (undo.h).
    // Returns the refusal's own sentence for the dispatcher to card, or
    // std::nullopt for "nothing to say" (GuiOpRefusal, warpmarkers_ops.h —
    // the contract is stated once there).
    GuiOpRefusal nudge_selected_phase_resets(int direction,
                                             bool synthesized_repeat);
};
