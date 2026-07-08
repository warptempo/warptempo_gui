#include "phaseresetmarkers_ops.h"

#include "audio.h"
#include "target_render.h"
#include "time_format.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <utility>
#include <vector>

// Phase reset authoring cluster. Method bodies map onto the original
// main.cpp lambdas via these mechanical rewrites:
//
//   push_undo*                  → undo.push_undo*
//   recompute_dirty             → undo.recompute_dirty
//   sync_playhead_to_last_selected → selection.sync_playhead_to_last_selected
//   invalidate_waveform_area    → viewport.invalidate_waveform_area
//   invalidate_timestamp_area   → viewport.invalidate_timestamp_area
//   invalidate_all              → viewport.invalidate_all
//   move_playhead_to            → viewport.move_playhead_to
//   current_samples_per_pixel   → free function
//   stop_playback_if_playing    → playback_lifecycle.stop_playback_if_playing

// Drop a phase reset marker at `time_seconds`. Placement is bounded only
// by the absolute range; arbitrarily close and exactly-coincident drops
// are legal (the render boundary owns degeneracy). Selection collapses to
// the freshly-inserted index. Frame-0 phase alignment is implicit by
// definition and needs no marker to assert it.
void GuiPhaseResetMarkersOps::drop_phase_reset_at_position(double time_seconds) {
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    // Snap first so the EOF check sees the stored grid value (mirrors
    // warp drop_marker).
    time_seconds = snap_to_timestamp_grid(time_seconds);
    const double sr_d = static_cast<double>(sr);
    // Recorded asymmetry with the warp drop: the EOF bound differs by
    // column for a structural reason. A warp marker needs at least one
    // source frame of segment to its next breakpoint (build_warp_frame_map
    // refuses sub-frame segments), so warp positions stop one frame short
    // of EOF; a phase reset is a point event, so its wall is
    // total_duration exactly — an at-EOF reset is inert at derivation.
    // Reject only when the snapped time lands strictly past EOF.
    if (time_seconds > static_cast<double>(audio.total_frames()) / sr_d)
        return;
    std::vector<GuiPhaseResetMarker> pre_state = app.phaseresetmarkers.markers();
    const int                 hint_last = app.last_selected_marker;
    GuiPhaseResetMarker nm;
    nm.time_seconds = time_seconds;
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
    const int64_t src_sample = static_cast<int64_t>(std::nearbyint(
        time_seconds * static_cast<double>(sr)));
    const int64_t sample = source_frame_to_active_domain(app, audio, src_sample);
    viewport.move_playhead_to(sample);
    target_render.trigger();
}

void GuiPhaseResetMarkersOps::drop_phase_reset_at_playhead() {
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    const int64_t src_frame =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);
    const double t = static_cast<double>(src_frame) /
                     static_cast<double>(sr);
    drop_phase_reset_at_position(t);
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
                "warptempo_gui: phase_reset delete rejected: stale selection index\n");
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

// Compute (delta_min, delta_max) seconds bounds for shifting the
// currently-selected phase resets by a uniform delta. Same shape as the
// warp version: absolute bounds only — the selection may shift until its
// minimum time reaches zero and its maximum reaches total_duration (a
// phase reset is a point event, so its EOF wall is total_duration
// exactly, one frame past the warp column's wall; an at-EOF reset is
// inert at derivation). Neighbors are not consulted; resets may cross
// and overlap freely and the store reorders after the shift. No trim
// clamp — phase resets aren't bounded by trim flags during edit. No
// frame-zero pin either — a phase reset at time 0.0 is legitimately
// movable, in contrast to warp marker 0.
std::pair<double, double> GuiPhaseResetMarkersOps::compute_phase_reset_delta_bounds(bool& ok) {
    ok = false;
    const auto& tv = app.phaseresetmarkers.markers();
    if (app.selected_markers.empty()) return {0.0, 0.0};
    const int sr = audio.sample_rate();
    if (sr <= 0) return {0.0, 0.0};
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(tv.size())) return {0.0, 0.0};
    }
    const double sr_d = static_cast<double>(sr);
    const double total_duration =
        static_cast<double>(audio.total_frames()) / sr_d;
    double min_t =  std::numeric_limits<double>::infinity();
    double max_t = -std::numeric_limits<double>::infinity();
    for (int idx : app.selected_markers) {
        min_t = std::min(min_t, tv[idx].time_seconds);
        max_t = std::max(max_t, tv[idx].time_seconds);
    }
    ok = true;
    return {0.0 - min_t, total_duration - max_t};
}

// Shift every selected phase reset by the clamped delta, exactly — no grid
// snap; a nudge moves exactly one pixel of time at the current zoom,
// sub-millisecond when a pixel is finer than a millisecond. Marker times
// live in memory at full double precision; persistence rounds to the
// millisecond grid with banker's rounding at save (format_timestamp), so
// a save-then-reload may shift a nudged marker by up to half a
// millisecond. Architect ruling (2026-07-07): gesture fidelity outranks
// the authored-equals-reloaded identity for nudges. Returns whether any
// reset actually moved.
bool GuiPhaseResetMarkersOps::apply_phase_reset_selection_shift(double raw_delta) {
    bool ok = false;
    auto [d_min, d_max] = compute_phase_reset_delta_bounds(ok);
    if (!ok) return false;
    // Each press consults only the bound in its direction of travel: it
    // refuses when there is no room (<= 0) and otherwise caps the step
    // at the wall, preserving creep-to-the-wall. The bounds are the
    // absolute range only (zero / total_duration), so the opposite bound
    // could demand a move only for a reset loaded already outside the
    // range; ignoring it keeps the guarantees: a nudge never moves
    // against the press and never moves more than one pixel. Neighbors
    // are not bounds at all — crossing is legal and the store reorders
    // below.
    const int direction = (raw_delta > 0.0) ? 1 : -1;
    double delta;
    if (direction > 0) {
        if (d_max <= 0.0) return false;
        delta = std::min(raw_delta, d_max);
    } else {
        if (d_min >= 0.0) return false;
        delta = std::max(raw_delta, d_min);
    }
    const auto& mv = app.phaseresetmarkers.markers();
    std::vector<GuiPhaseResetMarker> proposed = mv;
    bool any_changed = false;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(proposed.size())) continue;
        const double t_old = proposed[idx].time_seconds;
        const double t_new = t_old + delta;
        if (t_new == t_old) continue;
        proposed[idx].time_seconds = t_new;
        any_changed = true;
    }
    if (!any_changed) return false;
    app.phaseresetmarkers.markers_mut() = std::move(proposed);
    // A uniform shift can carry the selection past non-selected resets;
    // restore time order and re-point the selection at the moved resets.
    remap_marker_indices_after_reorder(
        app, reorder_markers_by_time(app.phaseresetmarkers.markers_mut()));
    return true;
}

// Nudge selected phase resets by +/- 1 source-pixel of seconds. Direction:
// -1 for earlier, +1 for later. Symmetric with nudge_selected_markers.
//
// Target view interprets nudge visually — see the matching note on
// nudge_selected_markers. Per-marker shifts, all-or-nothing.
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
    const double spp = current_samples_per_pixel(app, audio);

    if (app.active_audio_view == 'T') {
        const auto& tv = app.phaseresetmarkers.markers();
        for (int idx : app.selected_markers) {
            if (idx < 0 || idx >= static_cast<int>(tv.size())) return;
        }
        const double sr_d = static_cast<double>(sr);
        const auto& target_warp_frame_map = target_view_warp_frame_map_cached(
            app, sr, static_cast<long>(audio.total_frames())).warp_frame_map;
        const double total_duration =
            static_cast<double>(audio.total_frames()) / sr_d;
        std::vector<std::pair<int, double>> proposals;
        proposals.reserve(app.selected_markers.size());
        for (int idx : app.selected_markers) {
            const double t_src = tv[idx].time_seconds;
            const double t_tgt = map_source_to_target(
                static_cast<size_t>(std::nearbyint(t_src * sr_d)), target_warp_frame_map);
            const double t_tgt_new = t_tgt +
                static_cast<double>(direction) * spp;
            const size_t q = (t_tgt_new < 0.0)
                ? static_cast<size_t>(0)
                : static_cast<size_t>(std::llrint(t_tgt_new));
            // Exact inverse-mapped destination, full precision; the warp
            // frame map is monotone increasing, so the target-domain
            // direction is the source-domain direction.
            const double t_src_new =
                map_target_to_source(q, target_warp_frame_map) / sr_d;
            proposals.emplace_back(idx, t_src_new);
        }
        // A proposal is refused only when it leaves the absolute range:
        // below zero or above total_duration (the phase reset EOF wall —
        // a point event may sit at EOF exactly; an at-EOF reset is inert
        // at derivation). Crossing a neighbor is legal and goes through
        // the reorder-and-remap path below; the render boundary owns
        // degeneracy.
        bool any_changed = false;
        for (const auto& [idx, t_new] : proposals) {
            if (t_new == tv[idx].time_seconds) continue;
            if (t_new < 0.0 || t_new > total_duration) return;
            any_changed = true;
        }
        if (!any_changed) return;
        std::vector<GuiPhaseResetMarker> pre_state =
            app.phaseresetmarkers.markers();
        const int                 hint_last = app.last_selected_marker;
        for (const auto& [idx, t_new] : proposals) {
            GuiPhaseResetMarker* m =
                app.phaseresetmarkers.marker_mut(idx);
            if (!m) continue;
            m->time_seconds = t_new;
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
        return;
    }

    const double delta_s =
        static_cast<double>(direction) * spp / static_cast<double>(sr);
    if (delta_s == 0.0) return;
    std::vector<GuiPhaseResetMarker> pre_state = app.phaseresetmarkers.markers();
    const int                 hint_last = app.last_selected_marker;
    if (apply_phase_reset_selection_shift(delta_s)) {
        undo.push_undo_phase_reset(std::move(pre_state), hint_last);
        selection.sync_playhead_to_last_selected(/*edge_follow=*/true);
        undo.recompute_dirty();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    }
}
