#include "phaseresetmarkers_ops.h"

#include "audio.h"
#include "engine/engine_geometry.h"  // kN
#include "group_position_nudge.h"  // the shared group position-nudge flesh
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
    const int                 hint_last = app.last_selected_marker;
    GuiPhaseResetMarker nm;
    nm.time_frame = drop_frame;
    const int new_idx = app.phaseresetmarkers.insert_marker(std::move(nm));
    selection.set_single_selection(new_idx);
    undo.push_undo_phase_reset(std::move(pre_state), hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    // Match drop_marker: re-affirm the playhead on the new phase reset. The
    // playhead-drop create path (`s`) authors at the playhead, so this is a
    // no-op there; the target-view lead-in create path (Alt+S) authors N/2
    // BEFORE the playhead, so the playhead lands on the seeded reset. This is a
    // drop consequence (the reset is created for the playhead), not a selection
    // sync.
    const int64_t sample = source_frame_to_active_domain(app, audio, drop_frame);
    viewport.move_playhead_to(sample);
    target_render.trigger();
}

void GuiPhaseResetMarkersOps::drop_phase_reset_at_playhead() {
    if (audio.sample_rate() <= 0) return;
    // Playhead drops produce integer-valued frame positions.
    const int64_t src_frame =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);
    drop_phase_reset_at_position(static_cast<double>(src_frame));
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
// — walls, undo, selection — unchanged; only the
// seed frame is offset. The gesture is gated to target view (input_handler.cpp),
// where the overlay/lead-in exist.
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
    const int                 hint_last = app.last_selected_marker;
    // Capture the selected resets' active-domain positions BEFORE the store
    // mutation, so a multi-marker delete DEMOTES down to the region spanning
    // them (a DROP former of the selection<->highlight coupling — the delete
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
    // Demote a multi-marker delete to the spanning region — session scratch,
    // OUTSIDE undo (undoing the delete restores the markers while the region
    // stays). A single deleted reset is a point, not a span, so it forms no
    // region (the sliver rule's spirit; 2-marker + positive-span is the gate).
    // The waveform damage below covers the region paint.
    if (del_positions.size() >= 2) {
        const auto [lo, hi] = std::minmax_element(del_positions.begin(),
                                                  del_positions.end());
        if (*hi > *lo) {
            app.region.active     = true;
            app.region.a_frame    = *lo;
            app.region.b_frame    = *hi;
            // The delete demotion drops the deleted markers, so this region is
            // FREE — tempo gestures skip it.
            app.region.provenance = RegionProvenance::Free;
        }
    }
    undo.push_undo_phase_reset(std::move(pre_state), hint_last);
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
    const int                 hint_last = app.last_selected_marker;
    bool changed = false;
    for (int idx : app.selected_markers) {
        GuiPhaseResetMarker* m = app.phaseresetmarkers.marker_mut(idx);
        if (!m) continue;
        m->disabled = !m->disabled;
        changed = true;
    }
    if (!changed) return;
    undo.push_undo_phase_reset(std::move(pre_state), hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

// Nudge the selected phase resets by exactly one on-screen pixel column per
// press (GROUP, architect 2026-07-23 — a 2+ selection nudges rigidly, the
// keyboard sibling of the group position drag; a singleton is the degenerate
// case). Direction: -1 for earlier, +1 for later. Symmetric with
// nudge_selected_markers — the FOCUSED reset is the pixel-column ANCHOR
// (stepped_anchor_frame — the one-column-per-press derivation and its numeric
// rationale live in the comment there), and every OTHER selected member rides the
// anchor's uniform target-domain
// delta D as a COMPUTED position (snap(inv(fwd(orig_k) + D)) through the mapped
// domain, never re-column-snapped per member). Both columns share
// painted_column_of_source_frame / authored_frame_at_column, so the anchored
// column is the painted one and every committed value is a whole source frame.
//
// Wall semantics are one regime, GROUP-strict, against this column's absolute
// range (zero / total_frames - 1 — the single marker EOF wall both columns share;
// see drop_phase_reset_at_position for the ruling): this gesture runs in phase
// reset's TARGET home view only (the home-view binding, architect 2026-07-22), so
// the all-or-nothing silent refusal applies to the WHOLE group — if ANY member's
// proposal (the anchor's column snap or any rider's rigid position) leaves the
// range the whole press refuses (no per-member skip; the wall is hit before
// anything moves — the group step's max-strict precedent). A singleton is
// bit-for-bit the pre-group refusal. The integer walls win over the pixel grid.
// (The old source-view clamp arm died with the binding — there is no per-view
// refuse-vs-clamp split left.) Crossing a neighbor is legal and goes through the
// reorder-and-remap path below; the render boundary collapses an exact-equal
// group to one event.
void GuiPhaseResetMarkersOps::nudge_selected_phase_resets(int direction) {
    // Shared guard prologue: loading / empty-audio refusal, playback stop first,
    // empty/no-focus refusals, the coalesce verdict, the geometry refusals, and
    // the stale-index belt (the playhead-follows-focused / lead-in rationale lives
    // at the declaration). Refuses silently, navigation-class.
    const GroupNudgePrologue pro = group_position_nudge_prologue(
        app, audio, playback_lifecycle, undo, GestureKind::PhaseResetNudge,
        static_cast<int>(app.phaseresetmarkers.markers().size()));
    if (!pro.ok) return;
    const bool merge = pro.merge;
    // GROUP nudge (architect 2026-07-23): a 2+ selection nudges RIGIDLY, the
    // keyboard sibling of the group position drag — NO collapse_to_focused. Phase
    // resets carry no tempo, so there is no inherit/tempo analog; the FOCUSED
    // reset (app.last_selected_marker) is the pixel-anchored ANCHOR and every
    // other selected member rides its uniform delta D (the rigid-group
    // convention — computed positions, never re-column-snapped per member). A
    // singleton degenerates to the anchor alone.
    const auto& tv = app.phaseresetmarkers.markers();
    const int   f  = pro.focused;   // validated in [0, tv.size()) by the prologue

    // The anchoring map is the DISPLAYED paint basis —
    // displayed_or_live_target_map, the SAME map the stem/flag painter reads —
    // so the ANCHOR moves by exactly the commanded pixel column against WHAT IS
    // PAINTED, even inside a worker publish window where the displayed map lags
    // the live one. Phase resets author in their TARGET home view only (the
    // home-view binding, architect 2026-07-22), a mapped (non-identity) domain,
    // so the wall policy is one regime — GROUP all-or-nothing silent refusal:
    // if ANY member's proposal leaves the absolute range the WHOLE press refuses
    // (the wall is hit before anything moves — the group's max-strict precedent).
    // The former source-view clamp arm was unreachable under the binding and is
    // gone.
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const int64_t reset_wall = audio.total_frames() - 1;

    // (1) The ANCHOR steps one painted column (stepped_anchor_frame; its committed
    // frame is the pixel anchor). A refusal here refuses the whole press.
    const int64_t orig_f = tv[f].time_frame;
    const int64_t committed_f =
        stepped_anchor_frame(app, audio, map, orig_f, direction);
    if (committed_f < 0 || committed_f > reset_wall) return;
    // (2) The uniform ACTIVE-domain (target) delta from the anchor's step, in the
    // mapped domain: D = fwd(committed_f) - fwd(orig_f).
    const double D = map_source_to_target(static_cast<double>(committed_f), map)
                   - map_source_to_target(static_cast<double>(orig_f),      map);

    // (3) Every member's proposal: the anchor commits its column snap; every
    // OTHER member is the rigid computed position snap(inv(fwd(orig_k) + D)) — the
    // ONE double-to-authored route, never re-column-snapped. THE ONE refusal is
    // the EXACT INTEGER wall belt on the SNAPPED result (t_new outside
    // [0, reset_wall]) — walls are exact integer compares, never a float compare
    // (the exact-wall-reach doctrine forbids an epsilon-fragile pre-filter). To
    // make that belt honest the inverse must NOT clamp: map_target_to_source pins
    // any query at/below the map's first target breakpoint to source frame 0,
    // which would HIDE a below-start rigid proposal at 0 and pass the belt (the
    // round-1 trap). unclamped_target_to_source instead extends segment 0's slope
    // backward, so a truly-outside proposal reaches a negative (or past-wall)
    // source double, snaps to an out-of-range integer, and refuses; a proposal
    // whose exact rigid result rounds ONTO a wall snaps to 0/reset_wall and passes
    // (equality is legal). ANY out-of-range member refuses the whole press. The
    // ANCHOR keeps its own post-snap check (committed_f from authored_frame_at_column
    // DEFINES D, so a low-clamped anchor still yields a rigid D matching its actual
    // displayed move — the singleton behavior, unchanged by ruling).
    std::vector<std::pair<int, int64_t>> proposals;
    proposals.reserve(app.selected_markers.size());
    for (int idx : app.selected_markers) {
        int64_t t_new;
        if (idx == f) {
            t_new = committed_f;
        } else {
            const double tgt_prop =
                map_source_to_target(static_cast<double>(tv[idx].time_frame),
                                     map) + D;
            t_new = snap_authored_frame(
                unclamped_target_to_source(tgt_prop, map));
            if (t_new < 0 || t_new > reset_wall) return;
        }
        proposals.emplace_back(idx, t_new);
    }
    bool any_changed = false;
    for (const auto& [idx, t_new] : proposals) {
        if (t_new != tv[idx].time_frame) { any_changed = true; break; }
    }
    if (!any_changed) return;
    std::vector<GuiPhaseResetMarker> pre_state =
        app.phaseresetmarkers.markers();
    const int                 hint_last = app.last_selected_marker;
    // Identity hints: the WHOLE group in PRE-reorder snapshot coordinates (the
    // diff matcher is identity-blind for a translated group).
    std::vector<int> touched_snapshot(app.selected_markers.begin(),
                                      app.selected_markers.end());
    for (const auto& [idx, t_new] : proposals) {
        GuiPhaseResetMarker* m =
            app.phaseresetmarkers.marker_mut(idx);
        if (!m) continue;
        m->time_frame = t_new;
    }
    // A nudge may cross a neighbor; restore time order and remap the index-shaped
    // state (the whole group's selection follows to the new slots).
    remap_marker_indices_after_reorder(
        app, reorder_markers_by_time(app.phaseresetmarkers.markers_mut()));
    std::vector<int> touched_live(app.selected_markers.begin(),
                                  app.selected_markers.end());
    // Coalesce a rapid burst: the first press pushed the pre-burst snapshot with
    // the group hints; a continuation press skips the redundant push and REFRESHES
    // the surviving entry's touched_live to this press's post-reorder indices
    // (touched_snapshot stays the first press's pre-burst coordinates). The
    // post-mutation re-record happens in the shared tail.
    if (merge) {
        undo.note_coalesced_commit();
        undo.refresh_coalesced_touched_live(std::move(touched_live));
    } else {
        undo.push_undo_phase_reset(std::move(pre_state), hint_last,
                                   std::move(touched_snapshot),
                                   std::move(touched_live));
    }
    // Shared commit tail: stem lateral-gesture pin, record/dirty/invalidate,
    // playhead follow (committed_f is reorder-independent; the target home maps),
    // SelectionExtent region follow, and the view-independent target trigger.
    // Ordering rationale at the declaration.
    finish_group_position_nudge(app, audio, viewport, undo,
                                GestureKind::PhaseResetNudge, committed_f,
                                target_render);
}
