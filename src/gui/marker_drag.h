#pragma once

#include "app_state.h"
#include "selection.h"
#include "undo.h"
#include "viewport.h"

class GuiAudio;
struct GuiTargetRender;

// Marker reposition drag — THE ONLY POINTER MARKER GESTURE, and the single
// plain-flag-drag fine-tuning gesture
// (a plain flag press single-selects and arms it; motion past the shared
// threshold begins the move), shared by the warp and phase reset views and
// dispatched on app.active_markers_view (begin) and app.drag.drag_mode
// (commit). It moves ONE marker: groups are never moved (architect 2026-07-29 —
// the doctrine is at the head of group_position_nudge.h). It
// lives in its own translation unit because it is the one cross-kind
// gesture: the per-kind authoring and selection-shift operations stay in
// GuiWarpMarkersOps and GuiPhaseResetMarkersOps. Under the frozen-coord
// regime, motion writes app.drag.moveable_times only — the live per-list
// stores stay untouched until commit_drag does the write-back.
//
// The TARGET-VIEW TEMPO DRAG lived here too and is DELETED (architect 2026-07-29,
// with its keyboard twin the bare Left/Right tempo-image step): "the tempo drag
// was an Ableton-parity nicety; the keyboard step is just as good", and then the
// step went with it. THE TEMPO SURFACE IS THE BARE UP/DOWN CENT STEP ONLY
// (GuiWarpMarkersOps::adjust_tempo_cents, singleton and group, in both ruled
// contexts including W+target). Gone with them: TempoDragState and its whole
// pointer plumbing, PendingTempoDrag, tempo_drag_predecessor's eligibility walk,
// TempoGroupSeed with the deduped participant set and its walled pin, the group
// label wall, and the exact monotone bisection over hypothetical map builds. Do
// not re-propose a pointer tempo gesture: this is a ruling, not a gap.
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
};
