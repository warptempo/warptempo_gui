#include "phaseresetmarkers_ops.h"

#include "audio.h"
#include "engine/engine_geometry.h"  // kN
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
            app.region.active  = true;
            app.region.a_frame = *lo;
            app.region.b_frame = *hi;
            // The delete demotion drops the deleted markers, so this region is
            // NOT selection-owned — image-follow gestures skip it.
            app.region.selection_owned = false;
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

// Nudge the focused phase reset by exactly one on-screen pixel column
// per press. Direction: -1 for earlier, +1 for later. Symmetric with
// nudge_selected_markers — the pixel-column-anchored derivation, the
// one-column-per-press guarantee, and its numeric rationale live in the
// comment there; both columns share painted_column_of_source_frame /
// authored_frame_at_column, so the anchored column is the painted one
// and the committed value is a whole source frame.
//
// Wall semantics are one regime now, against this column's absolute range
// (zero / total_frames - 1 — the single marker EOF wall both columns share;
// see drop_phase_reset_at_position for the ruling): this gesture runs in
// phase reset's TARGET home view only (the home-view binding, architect
// 2026-07-22), so the all-or-nothing silent refusal is unconditional — a
// proposal leaving the range refuses wholesale. The integer walls win over
// the pixel grid. (The old source-view clamp arm died with the binding —
// there is no per-view refuse-vs-clamp split left.)
// Crossing a neighbor is legal and goes through the reorder-and-remap
// path below; the render boundary collapses an exact-equal group to one
// event.
void GuiPhaseResetMarkersOps::nudge_selected_phase_resets(int direction) {
    if (app.loading || audio.total_frames() <= 0) return;
    // Stop playback first. Playhead rule, symmetric with
    // nudge_selected_markers (full rationale there): the playhead FOLLOWS the
    // focused marker through the nudge — it steps with it so a later Space
    // auditions FROM the marker (architect 2026-07-23, reversing the 2026-07-20
    // decoupling; the coming scrub surface owns upstream auditioning).
    playback_lifecycle.stop_playback_if_playing();
    if (app.selected_markers.empty()) return;
    if (app.last_selected_marker < 0) return;
    // Undo-coalescing decision. coalesce_gesture keys off command adjacency
    // (app.command_seq, bumped once at the on_key dispatch entry that reached
    // this handler); it just has to run before record_gesture stamps this
    // command below.
    const bool merge = undo.coalesce_gesture(GestureKind::PhaseResetNudge);
    // Phase resets carry no tempo, so there is no inherit/tempo analog to
    // collapse — only nudge and jump; the per-marker loop below then runs
    // over the focused singleton.
    selection.collapse_to_focused();
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    if (current_samples_per_pixel(app, audio) <= 0.0) return;

    const auto& tv = app.phaseresetmarkers.markers();
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(tv.size())) return;
    }
    // The anchoring map is the DISPLAYED paint basis —
    // displayed_or_live_target_map, the SAME map the stem/flag painter reads —
    // so a press moves the reset by exactly the commanded pixel column against
    // WHAT IS PAINTED, even inside a worker publish window where the displayed
    // map lags the live one. Phase resets author in their TARGET home view only
    // (the home-view binding, architect 2026-07-22), a mapped (non-identity)
    // domain, so the wall policy is a single regime — the all-or-nothing silent
    // refusal outside the absolute range — because target is the only view this
    // gesture runs in; the former source-view clamp arm was unreachable under
    // the home-view binding and is gone.
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const int64_t reset_wall = audio.total_frames() - 1;
    std::vector<std::pair<int, int64_t>> proposals;
    proposals.reserve(app.selected_markers.size());
    for (int idx : app.selected_markers) {
        const int c = painted_column_of_source_frame(
            app, audio, static_cast<double>(tv[idx].time_frame), map);
        int64_t t_new =
            authored_frame_at_column(app, audio, c + direction, map);
        // All-or-nothing silent refusal outside the absolute range.
        if (t_new < 0 || t_new > reset_wall) return;
        proposals.emplace_back(idx, t_new);
    }
    bool any_changed = false;
    for (const auto& [idx, t_new] : proposals) {
        if (t_new != tv[idx].time_frame) any_changed = true;
    }
    if (!any_changed) return;
    std::vector<GuiPhaseResetMarker> pre_state =
        app.phaseresetmarkers.markers();
    const int                 hint_last = app.last_selected_marker;
    for (const auto& [idx, t_new] : proposals) {
        GuiPhaseResetMarker* m =
            app.phaseresetmarkers.marker_mut(idx);
        if (!m) continue;
        m->time_frame = t_new;
    }
    // A nudge may cross a neighbor; restore time order and re-point
    // the selection at the moved reset.
    remap_marker_indices_after_reorder(
        app, reorder_markers_by_time(app.phaseresetmarkers.markers_mut()));
    // Stem NUDGE PIN (symmetric with the warp nudge): keep the moved reset's stem
    // visible through the fine-tuning burst. Post-reorder focused index + the
    // current command_seq; the pin dies at the next command.
    app.stem_pin_marker      = app.last_selected_marker;
    app.stem_pin_command_seq = app.command_seq;
    // Playhead follows the marker: keep the playhead on the focused reset,
    // through the post-mutation two-step basis (identical to the warp nudge; the
    // committed t_new is reorder-independent). move_playhead_to owns the
    // clamp and invalidation, writing the cursor field only — playback was
    // stopped above, so the scanner is inactive and stale by contract,
    // untouched either way.
    if (!proposals.empty()) {
        viewport.move_playhead_to(
            source_frame_to_active_domain(app, audio,
                                          proposals.front().second));
    }
    // Coalesce a rapid burst: the first press already pushed the pre-burst
    // snapshot, so a continuation press skips its redundant push and one
    // Ctrl+Z reverts the whole burst. Then re-record with the post-mutation
    // (reordered) selection.
    if (merge) undo.note_coalesced_commit();
    else       undo.push_undo_phase_reset(std::move(pre_state), hint_last);
    undo.record_gesture(GestureKind::PhaseResetNudge);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}
