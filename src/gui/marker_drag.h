#pragma once

#include "app_state.h"
#include "undo.h"
#include "viewport.h"

class GuiAudio;
struct GuiTargetRender;

// Marker reposition drag — THE FLAG'S HORIZONTAL DRAG, and one of the plain
// flag drag's TWO gestures since 2026-09-10
// (a plain flag press runs its click act AT THE PRESS — 2026-08-17 — and arms
// a pending; motion past the shared threshold begins the move, the press's
// single-select already naming the grabbed marker; the
// contract is at PendingMarkerPress, app_state.h). THE CROSSING FORKS ON THE
// VALUE DRAG LAMP and this gesture is the DARK half: with the lamp lit the
// flag's plain drag is the VERTICAL one (ValueDragOps, value_drag.{h,cpp}) and
// this one does not begin at all, on any flag — "we never allow multi-axis
// dragging; flags move up and down or not at all" (architect 2026-09-10).
// UNDER THE CENTRED PIN IT IS A REVERSE PAN (architect 2026-09-12): while the
// `y` lamp is engaged the view promises a playhead held at the window's centre,
// so the FLAG HOLDS THE CENTRE COLUMN AND THE WAVEFORM SLIDES UNDER IT — the
// marker moving in the music by exactly the hand's travel, the Left/Right nudge
// made continuous. NO SIGN IS FLIPPED AND THE REVERSAL FALLS OUT: the store
// proposal is the ordinary one (the marker's frame follows the pointer) and the
// CAMERA follows the marker, so the picture moves the other way by itself; the
// ends need nothing, the marker clamping at 0 / EOF and the viewport at the
// song's ends, where the flag walks off-centre exactly as the pin does. THE
// WHOLE FORK IS ONE BIT, DragState::camera_follows, stamped at the crossing
// from centered_pin_engaged and read at four sites (its contract carries them);
// off the pin the gesture is unchanged in every particular. THE ARMING PRESS
// IS WHAT LEAVES A LIT PIN TO STAMP: it lands through the carry and defers
// both camera lamps to the edge that knows whether the press was a click
// (PendingMarkerPress, app_state.h), so the pin the user lit survives the
// press that starts this gesture. It is therefore the
// pin's strongest KEEPER rather than a collapser — the drag IS the posture's
// own motion — and it is the displayed-basis freeze's one deliberate
// non-member, the camera moving per motion event.
// Shared by the warp and phase reset views and
// dispatched on app.active_markers_view (begin) and app.drag.drag_mode
// (commit). It moves ONE marker: groups are never moved (architect 2026-07-29 —
// the doctrine is at the head of position_nudge.h). It
// lives in its own translation unit because it is the one cross-kind
// gesture: the per-kind authoring and selection-shift operations stay in
// GuiWarpMarkersOps and GuiPhaseResetMarkersOps. Under the frozen-coord
// regime, motion writes app.drag.moveable_times only — the live per-list
// stores stay untouched until commit_drag does the write-back.
//
// The TARGET-VIEW TEMPO DRAG lived here too and is DELETED (architect 2026-07-29,
// with its keyboard twin the bare Left/Right tempo-image step): "the tempo drag
// was an Ableton-parity nicety; the keyboard step is just as good", and then the
// step went with it. Gone with them: TempoDragState and its whole
// pointer plumbing, PendingTempoDrag, tempo_drag_predecessor's eligibility walk,
// TempoGroupSeed with the deduped participant set and its walled pin, the group
// label wall, and the exact monotone bisection over hypothetical map builds.
// ALL OF THAT STAYS DELETED AND UNPROPOSED — the ruling on THAT gesture is
// unchanged.
//
// WHAT IS SUPERSEDED IS THE SENTENCE THAT FOLLOWED IT, "do not re-propose a
// pointer tempo gesture: this is a ruling, not a gap" (architect 2026-09-10,
// overruling himself): the tempo surface is no longer the bare Up/Down cent
// step alone. THE VALUE DRAG is a pointer tempo gesture, and it is a different
// gesture from the deleted one in every way that made the deletion right — it
// is LAMP-GATED (bare `x`, so it exists only when asked for), it is a VERTICAL
// STEP of the very value the arrows step, through the arrows' own landing
// owner, and it carries NO map arithmetic at all: no bisection, no
// hypothetical builds, no predecessor walk, no group seed. It lives in its own
// unit (value_drag.{h,cpp}), where the whole contract is. His reason was the
// hand: a flag drag moved the marker on both platforms and there was no
// pointer road to the base tempo at all, which is why the Ctrl / Shift Up/Down
// ladder is the only spelling and why it is cumbersome on glass.
// NO Selection DEPENDENCY, deleted with the last write on 2026-08-15: the three
// bodies below neither read nor write app.selected_markers. Naming the drag's
// subject is the CLICK ACT's, at its one site (the arming press,
// run_marker_click_act — press-time again since 2026-08-17) — the
// reasoning is at the deletion in begin_drag. Restoring the edge means restoring
// a second owner for one invariant, so do not re-wire it to "assert" a selection
// this gesture does not own.
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

    bool begin_drag(int hit, int mouse_x);
    void apply_drag_motion(double raw_delta);
    void commit_drag();
};
