#pragma once

#include "app_state.h"
#include "undo.h"
#include "viewport.h"

class GuiAudio;
struct GuiTargetRender;

// Marker reposition drag — THE FLAG'S HORIZONTAL DRAG, one of the plain
// flag drag's TWO gestures (architect 2026-09-10). A plain flag press runs
// its click act AT THE PRESS and arms a pending; motion past the shared
// threshold begins the move, the press's single-select already naming the
// grabbed marker (the contract is at PendingMarkerPress, app_state.h). THE
// CROSSING FORKS ON THE VALUE DRAG'S POSTURE and this gesture is the other
// half: while the VALUE DRAG IS ARMED (the lamp, a one-shot, or grid
// iterations standing; value_drag_armed / value_drag_posture, app_state.h,
// 2026-10-11 — the phase reset view holds the lamp dark outside grid
// iterations) the flag's plain drag is the VERTICAL one in every view
// (ValueDragOps, value_drag.{h,cpp}) and this one does not begin at all, on
// any flag — "we never allow multi-axis dragging; flags move up and down or
// not at all" (architect 2026-09-10). With the lamp DARK this one is the
// flag's plain drag wherever the view lets it author (S+W, and T+P with grid
// iterations dark); where the view refuses it — T+W, the home-view binding,
// and T+P under grid iterations, the lock — the crossing begins NOTHING
// (horizontal_flag_drag_refused, app_state.h): a posture the view decided
// changed a tempo unnoticed when he dragged sideways in T+W.
// Shared by BOTH COLUMNS and dispatched on app.active_markers_view (begin)
// and app.drag.drag_mode (commit). It moves ONE marker: groups are never moved (architect 2026-07-29 —
// the doctrine is at the head of position_nudge.h). It
// lives in its own translation unit because it is the one cross-kind
// gesture: the per-kind authoring and selection-shift operations stay in
// GuiWarpMarkersOps and GuiPhaseResetMarkersOps.
//
// Under the frozen-coord regime, motion writes app.drag.moveable_times only — the live per-list
// stores stay untouched until commit_drag does the write-back.
//
// NO TARGET-VIEW TEMPO DRAG (architect 2026-07-29: "the tempo drag was an
// Ableton-parity nicety; the keyboard step is just as good"): a horizontal
// drag that solves a tempo through hypothetical map builds stays unproposed.
// THE POINTER'S TEMPO GESTURE IS THE VALUE DRAG (architect 2026-10-11), a
// different gesture in every way that ruling turned on: it is LAMP-GATED (a
// one-shot spent by the drag it arms, the lamp permanently lit under grid
// iterations), it is a VERTICAL STEP of the very value the arrows step,
// through the arrows' own landing owner, and it carries NO map arithmetic at
// all: no bisection, no hypothetical builds, no predecessor walk, no group
// seed. Its whole contract is in value_drag.{h,cpp}. His reason was the
// hand: without it there is no pointer road to the base tempo, and the
// Up/Down ladder is cumbersome on glass.
// NO Selection DEPENDENCY: the three bodies below neither read nor write
// app.selected_markers. Naming the drag's subject is the CLICK ACT's, at its
// one site (the arming press, run_marker_click_act) — the reasoning is at
// begin_drag. A second owner for one invariant is wrong, so do not wire it
// to "assert" a selection this gesture does not own.
struct MarkerDragOps {
    AppState&           app;
    const GuiAudio&     audio;
    Viewport&           viewport;
    Undo&               undo;
    GuiTargetRender&    target_render;

    MarkerDragOps(AppState&        app_,
                  const GuiAudio&  audio_,
                  Viewport&        viewport_,
                  Undo&            undo_,
                  GuiTargetRender& target_render_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          undo(undo_),
          target_render(target_render_) {}

    bool begin_drag(int hit, double mouse_x);
    void apply_drag_motion(double raw_delta);
    void commit_drag();
};
