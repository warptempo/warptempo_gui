#pragma once

#include "app_state.h"
#include "selection.h"
#include "undo.h"
#include "viewport.h"

class GuiAudio;
struct GuiTargetRender;

// Marker reposition drag — the single plain-flag-drag fine-tuning gesture
// (a plain flag press single-selects and arms it; motion past the shared
// threshold begins the move), shared by the warp and phase reset views and
// dispatched on app.active_markers_view (begin) and app.drag.drag_mode
// (commit). It
// lives in its own translation unit because it is the one cross-kind
// gesture: the per-kind authoring and selection-shift operations stay in
// GuiWarpMarkersOps and GuiPhaseResetMarkersOps. Under the frozen-coord
// regime, motion writes app.drag.moveable_times only — the live per-list
// stores stay untouched until commit_drag does the write-back.
struct MarkerDragOps {
    AppState&           app;
    const GuiAudio&     audio;
    Viewport&           viewport;
    Selection&          selection;
    Undo&               undo;
    GuiTargetRender&    target_render;

    MarkerDragOps(AppState&        app_,
                  const GuiAudio&  audio_,
                  Viewport&        viewport_,
                  Selection&       selection_,
                  Undo&            undo_,
                  GuiTargetRender& target_render_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          selection(selection_),
          undo(undo_),
          target_render(target_render_) {}

    bool begin_drag(int hit, int mouse_x);
    void apply_drag_motion(double raw_delta);
    void commit_drag();

    // Target-view TEMPO drag (Ableton-style stretch) — the pointer half of the
    // home-view binding's tempo exception, warp-only and reachable only in W +
    // target view (the arm site in input_pointer.cpp owns the view/read-only
    // gating; TempoDragState in app_state.h carries the model). Unlike the
    // reposition drag above there is no overlay and no deferred commit: each
    // changed integer-cents candidate writes the predecessor's tempo into the
    // live store and re-warps synchronously, and the release only finalizes
    // (undo push + preview trigger on net change).
    //
    // tempo_drag_eligible: store predicate — marker `hit` has a
    // predecessor (hit - 1 in the time-sorted store) that is an enabled tempo
    // OWNER by its own authored payload, sits at least one source frame
    // earlier (an exact-tie predecessor spans zero source frames, so no tempo
    // can move the dragged marker's image — the solve is degenerate
    // everywhere and the press stays a plain select), and is NOT a
    // coincident-collapse member (a surviving exact-frame group of 2+ enabled
    // markers resolves to one synthetic 1.00 owner, so the predecessor's
    // authored tempo is render-inert — tested via the normalization-red set).
    bool tempo_drag_eligible(int hit) const;
    bool begin_tempo_drag(int hit);
    void apply_tempo_drag_motion(int mouse_x);
    void end_tempo_drag();     // release / lost button: finalize
    void cancel_tempo_drag();  // Esc / Ctrl+Q: restore the grab tempo
};
