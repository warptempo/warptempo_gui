#include "value_drag.h"

#include "audio.h"
#include "phaseresetmarkers.h"
#include "target_render.h"
#include "warpmarkers.h"
#include "warpmarkers_ops.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace {

// The bound this drag started from, in the cell's own domain — cents on the
// warp column, hops on the phase one — with a BLANK bracket reading 0 in both
// cells, which is the arrows' own convention (iter_bound_step_landing and its
// phase twin both start a step from blank at 0). ONE spelling for the begin's
// seed and the motion's per-event current value, so the two can never disagree
// about what "the bound right now" is.
int64_t warp_bound_now(const GuiWarpMarker& m, MarkerCell cell) {
    return cell == MarkerCell::Upper ? m.iter_end_cents.value_or(0)
                                     : m.iter_start_cents.value_or(0);
}

int64_t phase_bound_now(const GuiPhaseResetMarker& m, MarkerCell cell) {
    return cell == MarkerCell::Upper ? m.iter_end_hops.value_or(0)
                                     : m.iter_start_hops.value_or(0);
}

}  // namespace

bool ValueDragOps::begin(int marker, MarkerCell cell, int press_y) {
    // THE TARGET RULE, asked of its one owner and asked HERE rather than at the
    // caller, so the crossing and the cursor map cannot answer it differently
    // (value_drag_target, app_state.h). A refusal is SILENT and writes nothing:
    // a pointer gesture's non-event is its own answer, and under a lit lamp a
    // flag that cannot be dragged simply does not drag.
    if (!value_drag_target(app, audio, marker, cell)) return false;

    const char column = app.active_markers_view;
    ValueDragState st;
    st.active  = true;
    st.marker  = marker;
    st.column  = column;
    st.cell    = cell;
    st.press_y = press_y;

    if (cell == MarkerCell::Payload) {
        // THE TEMPO ARM, SEEDED BY THE STEP'S OWN BODY (warp_tempo_step_start,
        // warpmarkers_ops.h): an OWNER answers its authored cents and scale, a
        // PASS the EFFECTIVE base and scale it resolves to, and the motion arm
        // freezes a pass to owning at the stepped value exactly as the arrow
        // step does (architect 2026-09-10: "the pass inherits whatever it was
        // and then applies on up and down"). The seed is one body for the two
        // hands, which is what keeps the pointer and the keyboard from
        // resolving the same pass differently.
        const std::vector<GuiWarpMarker>& mv = app.warpmarkers.markers();
        if (marker >= static_cast<int>(mv.size())) return false;
        // THE SLICE IS THIS GESTURE'S ONE RESOLVE, taken at the begin and
        // never per motion: the walk answers the value the press found, and
        // that anchor is fixed for the drag's life by the same rule that makes
        // the target absolute against the press.
        const WarpTempoStart start = warp_tempo_step_start(
            mv[static_cast<size_t>(marker)], slice_to_warp_markers(mv), marker,
            audio.total_frames());
        st.start_value = start.cents;
        st.start_scale = start.scale;
        // THE UNDO PAYLOAD, captured at the BEGIN because that is where the
        // pre-gesture store still stands: the crossing runs before the first
        // motion writes anything, and nothing can mutate the store between the
        // press and here (the drag-modal gate swallows every chord but the
        // Ctrl+Q hatch, which force-ends this gesture through its own commit).
        // The commit pushes it iff the value actually moved.
        st.pre_drag_snapshot = mv;
    } else if (column == 'P') {
        const std::vector<GuiPhaseResetMarker>& pv =
            app.phaseresetmarkers.markers();
        if (marker >= static_cast<int>(pv.size())) return false;
        st.start_value = phase_bound_now(pv[static_cast<size_t>(marker)], cell);
        // NO SNAPSHOT ON EITHER BOUND ARM, and it is not an omission: a
        // bracket is outside the undo domain whole (architect 2026-09-10 —
        // every push strips both bounds, and the lock refuses everything that
        // would push while the lamp stands), so there is nothing for a bound
        // drag to record and nothing for a Ctrl+Z to take back.
    } else {
        const std::vector<GuiWarpMarker>& mv = app.warpmarkers.markers();
        if (marker >= static_cast<int>(mv.size())) return false;
        st.start_value = warp_bound_now(mv[static_cast<size_t>(marker)], cell);
    }

    app.value_drag = std::move(st);
    return true;
}

void ValueDragOps::apply_motion(int mouse_y) {
    if (!app.value_drag.active) return;
    // ONE STEP PER kValueDragPxPerStep AUTHORED PIXELS, through the product's
    // one scale conversion — a press-road LENGTH scales, exactly as the drag
    // gate, the double-click slack and the touch slop do, so the gesture asks
    // the hand for the same travel at 100% and at 225%. Floored at 1: a
    // degenerate factor must never make the step zero pixels wide.
    const int per_step = scaled_px(kValueDragPxPerStep, 1);
    // TRAVEL IS MEASURED FROM THE PRESS AND UP IS POSITIVE: window y grows
    // downward, so `press_y - mouse_y` is "how far the hand has risen", and
    // the sign of the step count is the sign of the value change.
    const int64_t travel =
        static_cast<int64_t>(app.value_drag.press_y) - mouse_y;
    // C++ integer division TRUNCATES TOWARD ZERO, which is what this gesture
    // wants: the count is SYMMETRIC about the press — the first step up and
    // the first step down are both a full per_step away — so a hand wobbling
    // inside the dead band around its own press writes nothing in either
    // direction. Flooring instead would make the downward half fire a step
    // one pixel below the press.
    const int64_t steps = travel / per_step;
    if (steps == app.value_drag.last_steps) return;
    app.value_drag.last_steps = steps;
    // THE TARGET IS ABSOLUTE AGAINST THE PRESS, never an accumulation of
    // per-event deltas: the hand's position decides the value, so a drag that
    // walks out to a wall and comes back lands exactly where it started rather
    // than somewhere the clamps ate.
    const int64_t target = app.value_drag.start_value + steps;

    const int idx = app.value_drag.marker;
    if (app.value_drag.cell == MarkerCell::Payload) {
        // THE TEMPO WRITE, through the ARROWS' OWN LANDING OWNER as a delta
        // from the value the store holds right now — so the tempo bracket
        // clamps this drag exactly as it clamps a held Up, silently and with
        // no second spelling of "at the bracket edge" anywhere in this file.
        const std::vector<GuiWarpMarker>& mv = app.warpmarkers.markers();
        if (idx < 0 || idx >= static_cast<int>(mv.size())) return;
        // A PASS'S CURRENT VALUE IS THE BEGIN'S SEED, not its stored field: a
        // marker that inherits carries whatever cents the parser left in the
        // struct, and the value the hand is walking is the EFFECTIVE base the
        // begin resolved. Nothing can have written the field in between — the
        // first write below clears `tempo_inherits`, so this arm is live for
        // exactly one motion.
        const bool inherits = mv[static_cast<size_t>(idx)].tempo_inherits;
        const int64_t cur = inherits ? app.value_drag.start_value
                                     : mv[static_cast<size_t>(idx)].tempo_cents;
        const int64_t cents = tempo_cent_step_landing(cur, target - cur);
        // Asked BEFORE marker_mut, which bumps the store generation on call:
        // a step that lands where it already stands must cost nothing at all,
        // not a spurious generation bump and the cache rebuild behind it. A
        // PASS IS THE ONE EXEMPTION and it is the arrow step's own: the freeze
        // to owning is a change even where the cents do not move (a pass whose
        // effective base rests on a bracket edge, dragged toward it), which is
        // why tempo_cent_step_direction_actionable answers TRUE for a pass in
        // both directions and why the loop's skip carries the same `!m.tempo_
        // inherits` term.
        if (!inherits && cents == cur) return;
        if (GuiWarpMarker* m = app.warpmarkers.marker_mut(idx)) {
            // THE STEP'S OWN THREE WRITES, in its own order. On an owner the
            // first and the third are no-ops (the seed's scale IS its scale),
            // so spelling all three costs nothing and keeps the two hands one
            // act rather than two that agree today.
            m->tempo_inherits = false;
            m->tempo_cents    = cents;
            m->tempo_scale    = app.value_drag.start_scale;
        }
    } else if (app.value_drag.column == 'P') {
        const std::vector<GuiPhaseResetMarker>& pv =
            app.phaseresetmarkers.markers();
        if (idx < 0 || idx >= static_cast<int>(pv.size())) return;
        const MarkerCell cell = app.value_drag.cell;
        const int64_t cur = phase_bound_now(pv[static_cast<size_t>(idx)], cell);
        // The hop window and the partner bound, both through the column's own
        // landing owner. It reads the live store and the live map, so it runs
        // ahead of the mutable accessor below.
        const int landing = phase_iter_bound_step_landing(
            app, audio, idx, cell, static_cast<int>(target - cur));
        if (landing == cur) return;
        if (GuiPhaseResetMarker* m = app.phaseresetmarkers.marker_mut(idx))
            phase_iter_bound_step_write(*m, cell, landing);
    } else {
        const std::vector<GuiWarpMarker>& mv = app.warpmarkers.markers();
        if (idx < 0 || idx >= static_cast<int>(mv.size())) return;
        const MarkerCell cell = app.value_drag.cell;
        const GuiWarpMarker& before = mv[static_cast<size_t>(idx)];
        const int64_t cur = warp_bound_now(before, cell);
        const int64_t landing =
            iter_bound_step_landing(before, cell, target - cur);
        if (landing == cur) return;
        // THE COLUMN'S ONE WRITE SITE, which carries the BLANK RULE with it: a
        // pair landing on [0, 0] CLEARS the bracket rather than resting there,
        // so a drag that walks a set bound back through zero on a blank
        // partner wipes the bracket exactly as the arrows' step does. The
        // step-from-blank still starts at [0, 0] and simply cannot come to
        // rest there — its partner walls the landing.
        if (GuiWarpMarker* m = app.warpmarkers.marker_mut(idx))
            iter_bound_step_write(*m, cell, landing);
    }
    // A BOUND WRITE IS A FLAG'S VALUE, so the two camera lamps take the VALUE
    // answer per motion (postures_after_value_change, app_state.h, where the
    // class and the caller inventory live): the walk's framing lamp goes out
    // and the centred pin with it, idempotently, every accepted motion. THE
    // PAYLOAD ARM IS NOT HERE and takes nothing: a tempo answers at the COMMIT,
    // through the tail the keyboard step shares (warp_tempo_write_tail), which
    // is the same class said once for both hands. Reached on the changed path
    // alone — each arm above returns on a landing that moved nothing.
    if (app.value_drag.cell != MarkerCell::Payload)
        postures_after_value_change(app);
    // THE MARKER LANE AND NOTHING ELSE, per motion. The flag's number (or its
    // cell's) is what moved, the store's generation carries the flag cache's
    // rebuild under this damage, and NO MAP IS RE-LAID: in target view the
    // flags keep sitting on the DISPLAYED basis until the commit's own tail
    // re-warps the plate — the marker drag's precedent exactly, where the
    // proposal paints on the frozen basis and the map re-lands at the release.
    // WHAT MAKES THAT TRUE IS NOT THIS FUNCTION'S RESTRAINT (2026-09-10,
    // codex's finding): the write above lands in the LIVE store, so the target
    // map's hash moves with it and the per-tick dirty-detect would dispatch a
    // full waveform render — and publish its map and rebuild the flags — every
    // eight pixels of travel. The gesture is a member of displayed_basis_frozen
    // (app_state.h), which shuts the worker's dispatch AND its publication for
    // the drag's life, and the tick's live-total backstop (main.cpp) is gated
    // on the same bit, so no clamp can page the viewport sideways under a hand
    // moving vertically. Both gates lift at the commit, which clears the state
    // before it runs the tail.
    // The waveform is not damaged here either: a stem reads the marker's
    // CLASS, a tempo can move a marker in and out of the RED set, and the
    // commit is where that one repaint is owed.
    viewport.invalidate_top_strip();
}

void ValueDragOps::commit() {
    if (!app.value_drag.active) return;
    // READ, CLEAR, THEN ACT — the release bodies' standing shape, so the tail
    // below runs with no gesture live (the cursor map and the follow chase
    // both ask any_pointer_gesture_active, and this body's own tail moves the
    // camera through the reseat).
    ValueDragState st = std::move(app.value_drag);
    app.value_drag = ValueDragState{};

    // A BOUND DRAG COMMITS NOTHING AT ALL: every write it made already stands
    // in the store and already damaged the lane, a bracket pushes no undo
    // entry (the iteration lock's rule) and it changes no map, so there is no
    // re-warp, no re-land, no dirty bit and no preview to trigger. The gesture
    // simply ends.
    if (st.cell != MarkerCell::Payload) return;

    // THE TEMPO ARM'S ONE ENTRY, gated on NET CHANGE against the pre-drag
    // snapshot rather than on whether motion occurred — the marker drag's own
    // question, and for its own reason: a drag that wanders and returns to its
    // starting value would otherwise push a snapshot byte-equal to the live
    // store, a no-op entry that both undo and redo restore invisibly.
    //
    // THE COMPARE IS THE PAIR, NOT THE CENTS ALONE: a PASS dragged back to its
    // own effective base is STILL an owner now, and that conversion is a real
    // change the history must be able to take back. The keyboard step answers
    // the same way and it is worth naming why, since the two roads look
    // different: the step pushes at its first press, and its merged burst's
    // byte-equal pop asks warp_row_fields_differ, which carries
    // `tempo_inherits` — so a tap Up then a tap Down on a pass leaves the
    // entry standing there exactly as this test leaves it standing here. The
    // SCALE needs no term of its own: the only write that moves it is the
    // freeze, which moves `tempo_inherits` with it.
    const std::vector<GuiWarpMarker>& mv = app.warpmarkers.markers();
    if (st.marker < 0 || st.marker >= static_cast<int>(mv.size()) ||
        st.marker >= static_cast<int>(st.pre_drag_snapshot.size()))
        return;
    const GuiWarpMarker& now = mv[static_cast<size_t>(st.marker)];
    const GuiWarpMarker& was =
        st.pre_drag_snapshot[static_cast<size_t>(st.marker)];
    if (now.tempo_inherits == was.tempo_inherits &&
        now.tempo_cents == was.tempo_cents)
        return;

    // ONE ENTRY FOR THE WHOLE DRAG, AND IT IS FENCED FROM THE TAP-COALESCE
    // WINDOW WITHOUT A WORD HERE: push_undo_warp clears the coalescing stamp
    // (last_gesture_kind_, undo.cpp), so an Up tapped straight after this
    // release opens its own entry instead of merging into the drag's. That is
    // exactly how MarkerDragOps::commit_drag is fenced — it records no gesture
    // either — and it is why nothing in this unit calls coalesce_gesture or
    // record_gesture: a drag is one deliberate act, not a burst.
    undo.push_undo_warp(std::move(st.pre_drag_snapshot));
    undo.recompute_dirty();
    // THE WAVEFORM, FOR THE STEMS, once: a tempo change can move a marker in
    // or out of the RED set (a value that normalizes to the 1.00 fallback) and
    // the stem carries its class's colour. The motion arm deliberately leaves
    // this to the commit — one plate repaint per gesture rather than one per
    // eight pixels of travel.
    viewport.invalidate_waveform_area();
    // AND THE TEMPO WRITE'S SHARED TAIL — the target-view re-warp, the focus's
    // re-land on its post-write image and the preview trigger — through the
    // ONE body the two keyboard step arms call (warp_tempo_write_tail,
    // warpmarkers_ops.h). The step and the drag write the same field, so they
    // owe the same tail and must not spell it twice.
    warp_tempo_write_tail(app, audio, viewport, target_render);
}
