#include "phaseresetmarkers_ops.h"

#include "audio.h"
#include "engine/engine_geometry.h"  // kN
#include "position_nudge.h"  // the shared position-nudge flesh (prologue,
                                  // step, commit tail) + the movement doctrine
#include "input_handler.h"         // clear_region_highlight (the drop's collapse)
#include "target_render.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

// Phase reset authoring cluster: drop / delete / toggle / detect / clear
// operations on the phase reset store, reaching undo, selection, viewport,
// and playback_lifecycle through the struct's reference members.

// Drop a phase reset marker at `time_frame`. Placement is bounded only
// by the absolute range; arbitrarily close and exactly-coincident drops
// are legal (the render boundary collapses an exact-equal group to one
// event). Selection collapses to the freshly-inserted index. Frame-0
// phase alignment is implicit by definition and needs no marker to
// assert it.
void GuiPhaseResetMarkersOps::drop_phase_reset_at_position(double time_frame) {
    if (audio.sample_rate() <= 0) return;
    // Marker creation is a commit: the position funnels through
    // snap_authored_frame like every other gesture commit (mirrors warp
    // drop_marker — every current caller passes an integer-valued playhead
    // frame, so the snap is the uniformity funnel, not a rounding step).
    // The wall check below IS the load guard's comparison, exactly,
    // applied to the snapped value.
    const int64_t drop_frame = snap_authored_frame(time_frame);
    // Both marker columns share the warp column's EOF wall, total - 1;
    // phase reset does not get a total-exact wall.
    // Warp stops one frame short structurally (build_warp_frame_map refuses
    // sub-frame segments); phase reset stops one frame short by ruling: a
    // reset in the last source frame has nothing left to re-ground, and
    // total-1 keeps every marker inside the playhead's [0, total-1] domain
    // so marker gestures and playhead syncs agree exactly.
    if (drop_frame > audio.total_frames() - 1)
        return;
    std::vector<GuiPhaseResetMarker> pre_state = app.phaseresetmarkers.markers();
    GuiPhaseResetMarker nm;
    nm.time_frame = drop_frame;
    const int new_idx = app.phaseresetmarkers.insert_marker(std::move(nm));
    selection.set_single_selection(new_idx);
    undo.push_undo_phase_reset(std::move(pre_state));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    // Match drop_marker: re-affirm the playhead on the new phase reset. The one
    // create path is the lead-in drop below (bare `s` and the empty-lane
    // double-click both take it), which authors N/2 BEFORE the playhead, so the
    // playhead lands back on the seeded reset. This is a drop consequence (the
    // reset is created for the playhead), not a selection sync.
    const int64_t sample = source_frame_to_active_domain(app, audio, drop_frame);
    viewport.move_playhead_to(sample);
    // A DROP IS A POINT COMMAND (architect 2026-07-29, drop_marker's twin —
    // see the fuller statement there): it seats the playhead on the reset it
    // creates and single-selects it, so any resting span ends here,
    // unconditionally and of any provenance. THE PHASE CHOKEPOINT: both entry
    // routes reach this column only through drop_phase_reset_lead_in_at_playhead,
    // whose only act is to call this, so one clear covers both. PAST EVERY
    // REFUSAL: the callers' read-only / home-view gates and the wrapper's own
    // sample-rate test return before this runs, and this function's two refusals
    // — no sample rate, a drop_frame past the EOF wall — return above, before the
    // insert. clear_region_highlight owns its damage.
    clear_region_highlight(app, viewport);
    target_render.trigger();
}

// Target-view lead-in drop: place a phase reset N/2 output samples BEFORE the
// playhead. The OLA/Hann synthesis lead-in makes a reset's output ramp up over
// ~N/2 samples, reaching full scale ~N/2 after its authored frame; offsetting
// by -N/2 places the reset so its full-scale output lands on the playhead (the
// perceived transient). N/2 is measured in the target/output paint domain,
// matching the output-domain phase-reset overlay, then mapped to a source
// frame. kN/2 is an exact integer and the playhead is an integer frame, so the
// offset is plain integer arithmetic (no snap needed); clamped to 0. Reuses
// drop_phase_reset_at_position so the created reset takes the full create path
// — walls, undo, selection, the region collapse — unchanged; only the
// seed frame is offset. This is the phase column's ONLY drop (bare `s` and the
// empty-lane double-click), and both routes gate it on the home-view predicate,
// whose P arm IS target view — where the overlay/lead-in exist.
void GuiPhaseResetMarkersOps::drop_phase_reset_lead_in_at_playhead() {
    if (audio.sample_rate() <= 0) return;
    const int64_t ph =
        std::max<int64_t>(0, app.playhead_cursor_sample - kN / 2);
    const int64_t src_frame = active_domain_to_source_frame(app, audio, ph);
    drop_phase_reset_at_position(static_cast<double>(src_frame));
}

// Delete every selected phase reset. No label/cascade rules — phase resets
// don't have labels. Any selected phase reset is deletable, including one
// that happens to sit at time 0 — frame-0 phase alignment is implicit
// and needs no marker to assert it.
void GuiPhaseResetMarkersOps::delete_selected_phase_reset() {
    if (app.selected_markers.empty()) return;
    const auto& tv = app.phaseresetmarkers.markers();
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(tv.size())) {
            std::fprintf(stderr,
                "warptempo_gui: phase reset delete rejected: stale selection index\n");
            return;
        }
    }
    std::vector<GuiPhaseResetMarker> pre_state = app.phaseresetmarkers.markers();
    // Capture the selected resets' active-domain positions BEFORE the store
    // mutation, so a multi-marker delete DROPS the selection down to the region
    // spanning them (a DROP former of the selection<->highlight coupling — the delete
    // drops the selection and forms the region; architect 2026-07-23). Phase deletes run in the
    // target home view, whose map phase resets do not affect, so the pre/post
    // active-domain mapping is identical either way.
    std::vector<int64_t> del_positions;
    del_positions.reserve(app.selected_markers.size());
    for (int idx : app.selected_markers) {
        // Clamp the forward-map image into the live domain: region endpoints
        // hold PLAYABLE frames only (the display-state validator rejects an
        // endpoint >= total and clears the highlight), and an EOF reset's
        // unclamped image can round one past the wall — the land's own helper
        // keeps it a playable frame.
        del_positions.push_back(clamp_playhead_to_live_domain(
            source_frame_to_active_domain(app, audio, tv[idx].time_frame),
            app, audio));
    }
    for (auto it = app.selected_markers.rbegin();
         it != app.selected_markers.rend(); ++it) {
        app.phaseresetmarkers.remove_marker(*it);
    }
    selection.clear_selection();
    // Drop the deleted selection down to the spanning region — session scratch,
    // OUTSIDE undo: the delete creates this Free span outside the history entry, and
    // any later undo/redo of it CLEARS any resting region wholesale before deriving
    // the restored selection's own current visual form (the region does not
    // survive undo — it is replaced, not preserved). A single deleted reset is a
    // point, not a span, so it forms no
    // region (the sliver rule's spirit; 2-marker + positive-span is the gate).
    // The waveform damage below covers the region paint. ORDER: the deselect
    // above runs FIRST, so its membership clear cannot reach the span formed
    // here — and a FREE span is outside that clear's reach regardless (it takes
    // SelectionExtent only).
    if (del_positions.size() >= 2) {
        const auto [lo, hi] = std::minmax_element(del_positions.begin(),
                                                  del_positions.end());
        if (*hi > *lo) {
            app.region.active     = true;
            app.region.a_frame    = *lo;
            app.region.b_frame    = *hi;
            // The delete drop-former drops the deleted markers, so this region is
            // FREE — tempo gestures skip it.
            app.region.provenance = RegionProvenance::Free;
        }
    }
    undo.push_undo_phase_reset(std::move(pre_state));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

// Toggle the disabled flag on each selected phase reset. Unconditional —
// phase resets have no label-def gating like warp markers do. This mutates
// each marker in place via marker_mut, whereas the warp column's
// toggle_disabled (warpmarkers_ops.cpp) builds a proposed vector and swaps
// it in; the two are behaviorally equivalent and the style split is a
// cosmetic mirror divergence, accepted.
void GuiPhaseResetMarkersOps::toggle_phase_reset_disabled() {
    if (app.selected_markers.empty()) return;
    std::vector<GuiPhaseResetMarker> pre_state = app.phaseresetmarkers.markers();
    bool changed = false;
    for (int idx : app.selected_markers) {
        GuiPhaseResetMarker* m = app.phaseresetmarkers.marker_mut(idx);
        if (!m) continue;
        m->disabled = !m->disabled;
        changed = true;
    }
    if (!changed) return;
    undo.push_undo_phase_reset(std::move(pre_state));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

// Nudge the FOCUSED phase reset by exactly one on-screen pixel column per press.
// Direction: -1 for earlier, +1 for later. Symmetric with nudge_selected_markers
// — the moved reset is its own pixel-column anchor (stepped_anchor_frame — the
// one-column-per-press derivation and its numeric rationale live in the comment
// there). Both columns share painted_column_of_source_frame, so the anchored
// column is the painted one and every committed value is a whole source frame.
//
// HORIZONTAL MOVEMENT IS A FOCUS ACT — GROUPS ARE NEVER MOVED (architect
// 2026-07-29): a 2+ selection COLLAPSES TO ITS FOCUS in the shared prologue (which
// also lands the playhead there), so the body below moves exactly one reset. The
// doctrine and the dead per-member machinery are recorded once at the head of
// position_nudge.h.
//
// Wall semantics are one regime, STRICT, against this column's absolute range
// (zero / total_frames - 1 — the single marker EOF wall both columns share; see
// drop_phase_reset_at_position for the ruling): this gesture runs in phase reset's
// TARGET home view only (the home-view binding, architect 2026-07-22), and a
// proposal outside the range REFUSES the press silently rather than clamping (the
// warp twin clamps into its headroom instead — one regime per column at its home).
// The integer walls win over the pixel grid.
// (The old source-view clamp arm died with the binding — there is no per-view
// refuse-vs-clamp split left.) Crossing a neighbor is legal and goes through the
// reorder-and-remap path below.
void GuiPhaseResetMarkersOps::nudge_selected_phase_resets(
        int direction, bool synthesized_repeat) {
    // Shared guard prologue: loading / empty-audio refusal, playback stop first,
    // empty/no-focus refusals, the coalesce verdict, the geometry refusals, the
    // focused-index belt, and THE COLLAPSE + LAND that makes this a focus act (the
    // playhead-follows / lead-in rationale lives at the declaration). Refuses
    // silently, navigation-class.
    const PositionNudgePrologue pro = position_nudge_prologue(
        app, audio, playback_lifecycle, selection, viewport, undo,
        GestureKind::PhaseResetNudge, synthesized_repeat,
        static_cast<int>(app.phaseresetmarkers.markers().size()));
    if (!pro.ok) return;
    const bool merge = pro.merge;
    // Phase resets carry no tempo, so there is no inherit/tempo analog to the warp
    // twin's value gestures — this column's only nudge is positional.
    const auto& tv = app.phaseresetmarkers.markers();
    const int   f  = pro.focused;   // validated in [0, tv.size()) by the prologue

    // The anchoring map is the DISPLAYED paint basis —
    // displayed_or_live_target_map, the SAME map the flag/trim painters read —
    // so the moved reset travels exactly the commanded pixel column against
    // WHAT IS PAINTED, even inside a worker publish window where the displayed
    // map lags the live one. Phase resets author in their TARGET home view only
    // (the home-view binding, architect 2026-07-22), a mapped (non-identity)
    // domain.
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const int64_t reset_wall = audio.total_frames() - 1;

    // (1) THE STEP: the reset's own currently painted column, plus direction,
    // committed through authored_frame_at_column's target arm
    // (stepped_anchor_frame; the doctrine and the numbers live at its
    // declaration).
    //
    // (2) THE ONE refusal is the EXACT INTEGER wall belt on the SNAPPED result
    // (t_new outside [0, reset_wall]) — walls are exact integer compares, never a
    // float compare (the exact-wall-reach doctrine forbids an epsilon-fragile
    // pre-filter), and a proposal that rounds ONTO a wall passes (equality is
    // legal).
    const int64_t orig_f = tv[f].time_frame;
    const int64_t committed_f = stepped_anchor_frame(
        app, audio, map, orig_f, direction);
    if (committed_f < 0 || committed_f > reset_wall) return;
    if (committed_f == orig_f) return;   // zero-step press
    std::vector<GuiPhaseResetMarker> pre_state =
        app.phaseresetmarkers.markers();
    // Identity hint: the nudged reset in PRE-reorder snapshot coordinates (the
    // diff matcher is identity-blind for a column-snapped move — the moved row can
    // land field-identical on another row).
    std::vector<int> touched_snapshot{f};
    if (GuiPhaseResetMarker* m = app.phaseresetmarkers.marker_mut(f))
        m->time_frame = committed_f;
    // A nudge may cross a neighbor; restore time order and remap the index-shaped
    // state (the selection follows its reset to the new slot).
    remap_marker_indices_after_reorder(
        app, reorder_markers_by_time(app.phaseresetmarkers.markers_mut()));
    // touched_live: the nudged reset's POST-reorder live index — read off the
    // selection, which the remap rewrote in place and which is exactly this one
    // reset (the prologue collapsed to it).
    std::vector<int> touched_live(app.selected_markers.begin(),
                                  app.selected_markers.end());
    // Coalesce a held key: the PHYSICAL press pushed the pre-burst snapshot with
    // the identity hints; each synthesized repeat skips the redundant push and
    // REFRESHES the surviving entry's touched_live to this press's post-reorder
    // index (touched_snapshot stays the first press's pre-burst coordinates). The
    // post-mutation re-record happens in the shared tail.
    if (merge) {
        undo.note_coalesced_commit();
        undo.refresh_coalesced_touched_live(std::move(touched_live));
    } else {
        // The phase-reset POSITION NUDGE. The restore's always-on focus stem
        // follows from the selection — no lateral bit.
        undo.push_undo_phase_reset(std::move(pre_state),
                                   std::move(touched_snapshot),
                                   std::move(touched_live));
    }
    // Shared commit tail: record/dirty/invalidate (its full-waveform damage moves
    // the nudged reset's always-on stem), playhead follow (committed_f is
    // reorder-independent; the target home maps), the point command's region
    // collapse, and the view-independent target trigger. Ordering rationale at the
    // declaration.
    finish_position_nudge(app, audio, viewport, undo,
                                GestureKind::PhaseResetNudge, committed_f,
                                target_render);
}
