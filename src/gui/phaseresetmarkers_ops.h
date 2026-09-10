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
    // `step_columns` is the press's signed PAINTED-COLUMN count (±1 bare, ±3
    // shifted, ±10 with ctrl since 2026-08-31 — the step ladder, one owner at
    // arrow_step_magnitude in gui_input.h), the warp twin's own parameter.
    GuiOpRefusal nudge_selected_phase_resets(int step_columns,
                                             bool synthesized_repeat);
    // THE VERTICAL ARROWS' SECOND STEP BODY ON THIS COLUMN (2026-09-09), the
    // twin of GuiWarpMarkersOps::adjust_iter_bound_cents clause for clause in
    // the HOP domain: it steps one bound of the focused reset's iteration
    // bracket — `side` Lower or Upper, the addressed cell the dispatch forks
    // on (AppState::addressed_cell) — by `delta_hops` through the same arrow
    // ladder, with a 2+ selection taking the all-or-nothing group arm. Its
    // predicates are the shared four (app_state.h, the bound step's block,
    // each forking on the live column inside its own body). IT RECORDS NOTHING,
    // the warp twin's own ruling (architect 2026-09-10): no undo entry, no
    // coalescing kind and no dirty re-derive, so no `synthesized_repeat`
    // either. Its write goes through phase_iter_bound_step_write
    // (app_state.h), which is where a pair landing on two zeroes clears back
    // to the blank bracket. It changes no map and no position: no render
    // trigger, no re-land, no damage but the top strip's. Never stops
    // playback, for the tempo step's own reason.
    GuiOpRefusal adjust_iter_bound_hops(MarkerCell side, int delta_hops);

   private:
    // Group bound step (2+ selection): all-or-nothing over the survivors, the
    // warp twin's shape (adjust_iter_bound_cents_group).
    GuiOpRefusal adjust_iter_bound_hops_group(MarkerCell side, int delta_hops);
};
