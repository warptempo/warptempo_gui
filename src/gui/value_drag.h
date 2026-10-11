#pragma once

#include "app_state.h"
#include "undo.h"
#include "viewport.h"

class GuiAudio;
struct GuiTargetRender;

// THE FLAG'S VALUE DRAG — the second plain-flag drag and the ONE pointer
// gesture that changes a marker's VALUE (architect 2026-09-10). It runs only
// while THE VALUE DRAG IS ARMED (value_drag_armed): THE VALUE DRAG LAMP lit —
// bare `[` and row 8's Toggle Value Drag, a ONE-SHOT this unit's commit puts
// out — or grid iterations standing, under which it is permanently lit and
// nothing is spent (architect 2026-10-11 ~01:45) (architect 2026-10-11, retiring the
// view posture of 2026-09-13, under which a sideways drag in T+W changed a
// tempo unnoticed; the whole rule is at value_drag_posture, app_state.h) —
// where a plain flag press-and-drag in ANY VIEW, source view included (the
// phase reset view holds the lamp dark outside grid iterations, 2026-10-11
// ~02:05, no target standing there), steps
// the pressed cell's value VERTICALLY and the horizontal marker drag is off
// on every flag: "we never allow multi-axis dragging; flags move up and down
// or not at all." It steps the ONE pressed marker; a multi-selection is not
// carried.
//
// THE PRESS IS THE MARKER DRAG'S: run_marker_click_act runs at the press
// (stop, select, land, address the cell) and arms
// PendingMarkerPress; the crossing of the shared drag threshold forks on the
// posture and begins at most one of the two gestures (with the lamp dark in
// T+W, and in T+P under grid iterations, neither). So this unit owns no press
// path, no selection write and no playhead write — a value change is not a
// movement, and the press already put the playhead where it belongs.
//
// WHAT IT STEPS, and through whose arithmetic: the BASE TEMPO on a warp flag's
// payload and a BOUND on either column's bound cell, each written through the
// ARROWS' OWN LANDING OWNER as a delta from the value the store currently
// holds (tempo_cent_step_landing / iter_bound_step_landing /
// phase_iter_bound_step_landing, app_state.h). So the
// walls are the arrows'
// walls — the tempo bracket, the cell's window and its partner bound — and
// they clamp silently, exactly as a held Up does at the bracket's end. There
// is no second arithmetic anywhere in this file.
//
// A PASS IS A TEMPO TARGET AND THE DRAG CONVERTS IT (architect 2026-09-10:
// "the pass inherits whatever it was and then applies on up and down, so we
// should allow the drag on passes as well"; the payload being a target in
// target view alone from 2026-09-13, the conversion was deleted that morning
// as unreachable and restored that afternoon when the arrow step began
// freezing a pass in target view too; a target in both views since
// 2026-10-11, the lamp's posture). The seed is the arrow step's own
// body — warp_tempo_step_start (warpmarkers_ops.h) — so the value the hand
// walks from is the EFFECTIVE base the flag shows, and the first motion writes
// the step's own three fields: `tempo_inherits` false, the landed cents, and
// the effective scale that keeps the freeze lossless. A LABEL REF and a
// coincident-collapse member are still no target, the arrows' own kind
// refusal ruling, while a DISABLED marker is one, as the singleton arrow step
// steps it (architect 2026-09-13; value_drag_target, app_state.h).
//
// THE TARGET RULE IS NOT HERE EITHER: value_drag_target (app_state.h) answers
// which flags and which cells this gesture may touch, and the CURSOR MAP reads
// that same predicate, so the cue promises exactly the gesture. `begin` asks
// it and refuses in silence — a pointer gesture's non-event is its own answer.
//
// THE UNDO STORY FORKS THREE WAYS AT THE COMMIT, one arm per axis.
//
// THE TEMPO ARM'S ONE UNDO ENTRY IS THE COMMIT'S, never a motion's, which is
// the marker drag's own shape: motion writes the live store and damages the
// top strip, and the release pushes the pre-drag snapshot iff the marker's
// `tempo_inherits`/`tempo_cents` PAIR actually moved — the pair and not the
// cents alone, because a pass dragged back to its own effective base is still
// an owner now, which is the same answer the arrow step's byte-equal pop
// gives (warp_row_fields_differ carries `tempo_inherits`). It is FENCED from
// the tap-coalesce window for free —
// Undo::push_undo_warp clears the coalescing stamp — so an Up tapped straight
// after a drag opens its own entry rather than merging into the drag's, which
// is exactly how commit_drag's entry is fenced (that body records no gesture
// either). THE BOUND ARM PUSHES NOTHING AT ALL: a
// bracket is outside the undo
// domain whole (the iteration lock's own rule), so a bound drag is a session
// act from end to end.
//
// (This is not the deleted TARGET-VIEW TEMPO DRAG coming back. That gesture —
// a map bisection over hypothetical builds, a predecessor-eligibility walk and
// a group seed — was deleted with its keyboard twin on 2026-07-29 and stays
// deleted and unproposed; the record is at the head of marker_drag.h. This is
// a lamp-armed vertical STEP of the same values the arrows step, with no map
// arithmetic anywhere in it, and it is the architect's own ruling of
// 2026-09-10.)
struct ValueDragOps {
    AppState&        app;
    const GuiAudio&  audio;
    Viewport&        viewport;
    Undo&            undo;
    GuiTargetRender& target_render;

    ValueDragOps(AppState&        app_,
                 const GuiAudio&  audio_,
                 Viewport&        viewport_,
                 Undo&            undo_,
                 GuiTargetRender& target_render_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          undo(undo_),
          target_render(target_render_) {}

    // Begin the drag on `marker`'s `cell`, its travel counted from `origin_y`
    // — the window y of the motion that crossed the grab gate, never the
    // press's (ValueDragState::origin_y). False when the target rule refuses
    // (or the index has gone stale), and nothing is written on that path —
    // the caller drops the gesture.
    bool begin(int marker, MarkerCell cell, int origin_y);
    // One motion event's whole effect. Cheap and idempotent inside a step.
    void apply_motion(int mouse_y);
    // End the gesture, pushing the tempo arm's one undo entry iff the
    // marker's tempo pair moved (the rule is above), and SPEND THE LAMP
    // (AppState::value_drag_lamp, 2026-10-11) — a drag began, so the one-shot
    // goes dark (under grid iterations nothing is spent: the mode arms every
    // drag, value_drag_armed). Called by the release, by the lost-button arm and by the
    // force-end finalizer alike — every end of this gesture commits what
    // stands, the standing no-cancel rule.
    void commit();
};
