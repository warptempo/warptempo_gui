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
        // THE TEMPO ARM. The target rule proved the marker is a live OWNER, so
        // the seed is its authored cents (never a resolved projection: a pass
        // is refused outright, so there is nothing to walk back to) and
        // `tempo_inherits` is already false — the motion writes the value and
        // touches no other field, which is what keeps `tempo_scale` exactly
        // where the flag editor left it.
        const std::vector<GuiWarpMarker>& mv = app.warpmarkers.markers();
        if (marker >= static_cast<int>(mv.size())) return false;
        st.start_value = mv[static_cast<size_t>(marker)].tempo_cents;
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
        const int64_t cur = mv[static_cast<size_t>(idx)].tempo_cents;
        const int64_t cents = tempo_cent_step_landing(cur, target - cur);
        // Asked BEFORE marker_mut, which bumps the store generation on call:
        // a step that lands where it already stands must cost nothing at all,
        // not a spurious generation bump and the cache rebuild behind it.
        if (cents == cur) return;
        if (GuiWarpMarker* m = app.warpmarkers.marker_mut(idx))
            m->tempo_cents = cents;
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
    // four pixels of travel. The gesture is a member of displayed_basis_frozen
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
    // store, a no-op entry that both undo and redo restore invisibly. The
    // compare is the one FIELD this gesture writes.
    const std::vector<GuiWarpMarker>& mv = app.warpmarkers.markers();
    if (st.marker < 0 || st.marker >= static_cast<int>(mv.size()) ||
        st.marker >= static_cast<int>(st.pre_drag_snapshot.size()))
        return;
    if (mv[static_cast<size_t>(st.marker)].tempo_cents ==
        st.pre_drag_snapshot[static_cast<size_t>(st.marker)].tempo_cents)
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
    // four pixels of travel.
    viewport.invalidate_waveform_area();
    // AND THE TEMPO WRITE'S SHARED TAIL — the target-view re-warp, the focus's
    // re-land on its post-write image and the preview trigger — through the
    // ONE body the two keyboard step arms call (warp_tempo_write_tail,
    // warpmarkers_ops.h). The step and the drag write the same field, so they
    // owe the same tail and must not spell it twice.
    warp_tempo_write_tail(app, audio, viewport, target_render);
}
