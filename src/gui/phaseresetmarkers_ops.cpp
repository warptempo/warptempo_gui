#include "phaseresetmarkers_ops.h"

#include "audio.h"
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
// are legal (the render boundary owns degeneracy). Selection collapses to
// the freshly-inserted index. Frame-0 phase alignment is implicit by
// definition and needs no marker to assert it.
void GuiPhaseResetMarkersOps::drop_phase_reset_at_position(double time_frame) {
    if (audio.sample_rate() <= 0) return;
    // Marker creation is a commit: the position funnels through
    // snap_authored_frame like every other gesture commit (mirrors warp
    // drop_marker — every current caller passes an integer-valued playhead
    // frame, so the snap is the uniformity funnel, not a rounding step).
    // The wall check below IS the load guard's comparison, exactly,
    // applied to the snapped value.
    const int64_t drop_frame = snap_authored_frame(time_frame);
    // Both marker columns share the warp column's EOF wall, total - 1 (the
    // old per-column split — warp total-1, phase reset total — is retired).
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
    app.selected_markers.clear();
    app.selected_markers.insert(new_idx);
    app.last_selected_marker = new_idx;
    undo.push_undo_phase_reset(std::move(pre_state), hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    // Match drop_marker: move playhead to the new phase reset. When
    // dropping at the current playhead, this is a no-op.
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
    for (auto it = app.selected_markers.rbegin();
         it != app.selected_markers.rend(); ++it) {
        app.phaseresetmarkers.remove_marker(*it);
    }
    app.selected_markers.clear();
    app.last_selected_marker = -1;
    undo.push_undo_phase_reset(std::move(pre_state), hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

// Toggle the disabled flag on each selected phase reset. Unconditional —
// phase resets have no label-def gating like warp markers do.
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
// Wall semantics per view are unchanged, against this column's absolute
// range (zero / total_frames - 1 — the single marker EOF wall both
// columns now share; see drop_phase_reset_at_position for the ruling):
// target view keeps the all-or-nothing silent refusal, source view keeps
// the clamp (creep-to-the-wall), and the integer walls win over the
// pixel grid.
// Crossing a neighbor is legal and goes through the reorder-and-remap
// path below; the render boundary owns degeneracy.
void GuiPhaseResetMarkersOps::nudge_selected_phase_resets(int direction) {
    if (app.loading || audio.total_frames() <= 0) return;
    playback_lifecycle.stop_playback_if_playing();
    if (app.selected_markers.empty()) return;
    if (app.last_selected_marker < 0) return;
    // Fine-tuning op: collapse the selection to the focused marker,
    // mirroring nudge_selected_markers (warpmarkers_ops.cpp). Phase resets
    // carry no tempo, so there is no inherit/tempo analog to collapse —
    // only nudge and jump. The existing per-marker loop then runs over the
    // singleton.
    app.selected_markers.clear();
    app.selected_markers.insert(app.last_selected_marker);
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    if (current_samples_per_pixel(app, audio) <= 0.0) return;

    const auto& tv = app.phaseresetmarkers.markers();
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(tv.size())) return;
    }
    // Source view anchors through the identity map (empty list); target
    // view through the live cached map — the same map the stem painter
    // reads, so the anchored column is the painted one.
    const bool target_view = (app.active_audio_view == 'T');
    const std::vector<WarpFrameMapSegment> no_map;
    const auto& map = target_view
        ? target_view_warp_frame_map_cached(
              app, sr, static_cast<long>(audio.total_frames())).warp_frame_map
        : no_map;
    const int64_t reset_wall = audio.total_frames() - 1;
    std::vector<std::pair<int, int64_t>> proposals;
    proposals.reserve(app.selected_markers.size());
    for (int idx : app.selected_markers) {
        const int c = painted_column_of_source_frame(
            app, audio, static_cast<double>(tv[idx].time_frame), map);
        int64_t t_new =
            authored_frame_at_column(app, audio, c + direction, map);
        if (target_view) {
            // All-or-nothing silent refusal outside the absolute range.
            if (t_new < 0 || t_new > reset_wall) return;
        } else {
            if (t_new < 0)          t_new = 0;
            if (t_new > reset_wall) t_new = reset_wall;
        }
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
    // the selection at the moved reset before the playhead sync
    // reads last_selected below.
    remap_marker_indices_after_reorder(
        app, reorder_markers_by_time(app.phaseresetmarkers.markers_mut()));
    undo.push_undo_phase_reset(std::move(pre_state), hint_last);
    selection.sync_playhead_to_last_selected(/*edge_follow=*/true);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}
