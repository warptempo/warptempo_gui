#pragma once

#include "app_state.h"
#include "undo.h"
#include "viewport.h"

class GuiAudio;
struct GuiTargetRender;

// THE FLAG'S VALUE DRAG — the second plain-flag drag and the ONE pointer
// gesture that changes a marker's VALUE (architect 2026-09-10). It runs only
// while the VALUE DRAG lamp stands (bare `x`; the mode's whole contract is at
// AppState::value_drag_enabled), where a plain flag press-and-drag steps the
// pressed cell's value VERTICALLY and the horizontal marker drag is off on
// every flag: "we never allow multi-axis dragging; flags move up and down or
// not at all."
//
// THE PRESS IS THE MARKER DRAG'S: run_marker_click_act runs at the press
// (stop, select, land, address the cell, hide the trim overlay) and arms
// PendingMarkerPress; the crossing of the shared drag threshold forks on the
// lamp and begins exactly one of the two gestures. So this unit owns no press
// path, no selection write and no playhead write — a value change is not a
// movement, and the press already put the playhead where it belongs.
//
// WHAT IT STEPS, and through whose arithmetic: the BASE TEMPO on a warp flag's
// payload and a BOUND on either column's purple cell, each written through the
// ARROWS' OWN LANDING OWNER as a delta from the value the store currently
// holds (tempo_cent_step_landing / iter_bound_step_landing /
// phase_iter_bound_step_landing, app_state.h). So the walls are the arrows'
// walls — the tempo bracket, the cell's window and its partner bound — and
// they clamp silently, exactly as a held Up does at the bracket's end. There
// is no second arithmetic anywhere in this file.
//
// A PASS IS A TEMPO TARGET AND THE DRAG CONVERTS IT (architect 2026-09-10:
// "the pass inherits whatever it was and then applies on up and down, so we
// should allow the drag on passes as well"). The seed is the arrow step's own
// body — warp_tempo_step_start (warpmarkers_ops.h) — so the value the hand
// walks from is the EFFECTIVE base the flag shows, and the first motion writes
// the step's own three fields: `tempo_inherits` false, the landed cents, and
// the effective scale that keeps the freeze lossless. A LABEL REF is still no
// target, having no tempo of its own; in TARGET view a pass is none either,
// the arrows' own kind refusal ruling there (value_drag_target, app_state.h).
//
// THE TARGET RULE IS NOT HERE EITHER: value_drag_target (app_state.h) answers
// which flags and which cells this gesture may touch, and the CURSOR MAP reads
// that same predicate, so the cue promises exactly the gesture. `begin` asks
// it and refuses in silence — a pointer gesture's non-event is its own answer.
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
// either). THE BOUND ARM PUSHES NOTHING AT ALL: a bracket is outside the undo
// domain whole (the iteration lock's own rule), so a bound drag is a session
// act from end to end.
//
// (This is not the deleted TARGET-VIEW TEMPO DRAG coming back. That gesture —
// a map bisection over hypothetical builds, a predecessor-eligibility walk and
// a group seed — was deleted with its keyboard twin on 2026-07-29 and stays
// deleted and unproposed; the record is at the head of marker_drag.h. This is
// a lamp-gated vertical STEP of the same values the arrows step, with no map
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

    // Begin the drag on `marker`'s `cell` at the press's window y. False when
    // the target rule refuses (or the index has gone stale), and nothing is
    // written on that path — the caller drops the gesture.
    bool begin(int marker, MarkerCell cell, int press_y);
    // One motion event's whole effect. Cheap and idempotent inside a step.
    void apply_motion(int mouse_y);
    // End the gesture, pushing the tempo arm's one undo entry iff the
    // marker's tempo pair moved (the rule is above). Called by the release,
    // by the lost-button arm and by the
    // force-end finalizer alike — every end of this gesture commits what
    // stands, the standing no-cancel rule.
    void commit();
};
