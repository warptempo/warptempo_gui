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
    // tempo_drag_predecessor: store predicate + walk. Returns the GROUP's
    // predecessor store index for a drag on marker `hit`, or -1 when
    // ineligible. Coincident groups act as ONE item (architect 2026-07-22): the
    // predecessor is the nearest marker at a STRICTLY earlier frame (walk
    // backward past the equal-frame run of same-frame siblings), so dragging
    // ANY member of a stack drags the stack as one. That predecessor must be an
    // enabled tempo OWNER by its own authored payload and NOT itself a
    // coincident-collapse member (a surviving exact-frame group of 2+ enabled
    // markers resolves to one synthetic 1.00 owner, so its authored tempo is
    // render-inert — tested via the normalization-red set). The old zero-span
    // rejection is now structural: the walk lands a strictly-earlier frame, so
    // L_src >= 1 always. A marker FOLLOWING a stack still never arms — its
    // predecessor IS a collapse member, caught by the red-set test; and a
    // marker at the store's earliest frame (no strictly-earlier marker) returns
    // -1.
    int tempo_drag_predecessor(int hit) const;
    bool begin_tempo_drag(int hit);
    void apply_tempo_drag_motion(int mouse_x);
    void end_tempo_drag();     // release / lost button: finalize
    void cancel_tempo_drag();  // Esc / Ctrl+Q: restore the grab tempo
};
