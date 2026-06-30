#include "phase_reset_markers_ops.h"

#include "audio.h"
#include "target_render.h"
#include "time_format.h"
#include "frame_map_view.h"
#include "frame_map.h"

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

// Drop a phase reset marker at `time_seconds`. Rejects creation within
// kMarkerHitHalfPx pixels at current zoom of an existing phase reset marker.
// Selection collapses to the freshly-inserted index. Frame-0 phase alignment
// is implicit by definition and needs no marker to assert it.
void GuiPhaseResetMarkersOps::drop_phase_reset_at_position(double time_seconds) {
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    // Snap before the within-eps dup check so the check and the stored
    // marker both see the grid value (mirrors warp drop_marker).
    time_seconds = snap_to_timestamp_grid(time_seconds);
    const double sr_d = static_cast<double>(sr);
    const double spp  = current_samples_per_pixel(app, audio);
    const double eps = marker_hit_eps_seconds(spp, sr_d);
    // Same source-end defense as the warp drop, kept symmetric across marker
    // kinds. (An out-of-bounds phase reset is render-harmless, but placement
    // stays uniform unless a difference is required.)
    if (time_seconds > static_cast<double>(audio.total_frames()) / sr_d - eps)
        return;
    const auto& tv = app.phase_reset_markers.markers();
    if (reject_if_marker_within_eps(tv, time_seconds, eps, "phase_reset")) return;
    std::vector<GuiPhaseResetMarker> pre_state = app.phase_reset_markers.markers();
    const int                 hint_last = app.last_selected_marker;
    GuiPhaseResetMarker nm;
    nm.time_seconds = time_seconds;
    const int new_idx = app.phase_reset_markers.insert_marker(std::move(nm));
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
    const auto& tv = app.phase_reset_markers.markers();
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(tv.size())) {
            std::fprintf(stderr,
                "warptempo_gui: phase_reset delete rejected: stale selection index\n");
            return;
        }
    }
    std::vector<GuiPhaseResetMarker> pre_state = app.phase_reset_markers.markers();
    const int                 hint_last = app.last_selected_marker;
    for (auto it = app.selected_markers.rbegin();
         it != app.selected_markers.rend(); ++it) {
        app.phase_reset_markers.remove_marker(*it);
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
    std::vector<GuiPhaseResetMarker> pre_state = app.phase_reset_markers.markers();
    const int                 hint_last = app.last_selected_marker;
    bool changed = false;
    for (int idx : app.selected_markers) {
        GuiPhaseResetMarker* m = app.phase_reset_markers.marker_mut(idx);
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
// warp version: nearest non-selected neighbor on each side, intersected,
// with a kMarkerHitHalfPx-pixels-at-current-zoom visual gap enforced via
// eps. No trim clamp — phase resets aren't bounded by trim flags during
// edit. No frame-zero pin either — a phase reset at time 0.0 is legit-
// imately movable, in contrast to warp marker 0.
std::pair<double, double> GuiPhaseResetMarkersOps::compute_phase_reset_delta_bounds(bool& ok) {
    ok = false;
    const auto& tv = app.phase_reset_markers.markers();
    if (app.selected_markers.empty()) return {0.0, 0.0};
    const int sr = audio.sample_rate();
    if (sr <= 0) return {0.0, 0.0};
    const double sr_d = static_cast<double>(sr);
    const double spp  = current_samples_per_pixel(app, audio);
    const double eps = marker_hit_eps_seconds(spp, sr_d);
    const double total_duration =
        static_cast<double>(audio.total_frames()) / sr_d;
    auto bounds = compute_neighbor_walk_bounds(
        tv, app.selected_markers, eps, total_duration);
    ok = true;
    return bounds;
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
        const auto& tv = app.phase_reset_markers.markers();
        for (int idx : app.selected_markers) {
            if (idx < 0 || idx >= static_cast<int>(tv.size())) return;
        }
        const double sr_d = static_cast<double>(sr);
        const auto tmap = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
        const double total_duration =
            static_cast<double>(audio.total_frames()) / sr_d;
        const double eps = 1.0 / sr_d;
        std::vector<std::pair<int, double>> proposals;
        proposals.reserve(app.selected_markers.size());
        for (int idx : app.selected_markers) {
            const double t_src = tv[idx].time_seconds;
            const double t_tgt = map_source_to_target(
                static_cast<size_t>(std::nearbyint(t_src * sr_d)), tmap);
            const double t_tgt_new = t_tgt +
                static_cast<double>(direction) * spp;
            const size_t q = (t_tgt_new < 0.0)
                ? static_cast<size_t>(0)
                : static_cast<size_t>(std::llrint(t_tgt_new));
            const double t_src_new = snap_to_timestamp_grid(
                map_target_to_source(q, tmap) / sr_d);
            proposals.emplace_back(idx, t_src_new);
        }
        bool any_changed = false;
        for (const auto& [idx, t_new] : proposals) {
            if (t_new == tv[idx].time_seconds) continue;
            int prev = idx - 1;
            while (prev >= 0 && app.selected_markers.count(prev)) --prev;
            const double lo = (prev >= 0)
                ? (tv[prev].time_seconds + eps)
                : eps;
            int next = idx + 1;
            const int n = static_cast<int>(tv.size());
            while (next < n && app.selected_markers.count(next)) ++next;
            const double hi = (next < n)
                ? (tv[next].time_seconds - eps)
                : (total_duration - eps);
            if (t_new < lo || t_new > hi) return;
            any_changed = true;
        }
        if (!any_changed) return;
        std::vector<GuiPhaseResetMarker> pre_state =
            app.phase_reset_markers.markers();
        const int                 hint_last = app.last_selected_marker;
        for (const auto& [idx, t_new] : proposals) {
            GuiPhaseResetMarker* m =
                app.phase_reset_markers.marker_mut(idx);
            if (!m) continue;
            m->time_seconds = t_new;
        }
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

    bool ok = false;
    auto [d_min, d_max] = compute_phase_reset_delta_bounds(ok);
    if (!ok) return;
    double delta = delta_s;
    if (delta < d_min) delta = d_min;
    if (delta > d_max) delta = d_max;
    if (delta == 0.0) return;

    std::vector<GuiPhaseResetMarker> pre_state = app.phase_reset_markers.markers();
    const int                 hint_last = app.last_selected_marker;
    for (int idx : app.selected_markers) {
        GuiPhaseResetMarker* m = app.phase_reset_markers.marker_mut(idx);
        if (!m) continue;
        m->time_seconds = snap_to_timestamp_grid(m->time_seconds + delta);
    }
    undo.push_undo_phase_reset(std::move(pre_state), hint_last);
    selection.sync_playhead_to_last_selected(/*edge_follow=*/true);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

// `j` for phase reset view: shift the selection so last_selected lands
// on the playhead. All-or-nothing clamp check.
void GuiPhaseResetMarkersOps::jump_phase_reset_selection_to_playhead() {
    if (app.selected_markers.empty()) return;
    if (app.last_selected_marker < 0) return;
    // Fine-tuning op: collapse the selection to the focused marker, so the
    // anchor and the shifted marker are one and the same — mirroring
    // jump_selection_to_playhead (warpmarkers_ops.cpp).
    app.selected_markers.clear();
    app.selected_markers.insert(app.last_selected_marker);
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    const auto& tv = app.phase_reset_markers.markers();
    if (app.last_selected_marker >= static_cast<int>(tv.size())) return;
    const double anchor_t = tv[app.last_selected_marker].time_seconds;
    // Target view: inverse-translate playhead so the delta lives in
    // source-seconds (matching anchor_t's domain).
    const int64_t ph_src =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);
    const double ph_t     = static_cast<double>(ph_src) /
                            static_cast<double>(sr);
    const double delta    = ph_t - anchor_t;
    if (delta == 0.0) return;

    bool ok = false;
    auto [d_min, d_max] = compute_phase_reset_delta_bounds(ok);
    if (!ok || delta < d_min || delta > d_max) {
        std::fprintf(stderr,
            "warptempo_gui: phase_reset jump rejected: would violate "
            "marker ordering\n");
        return;
    }
    std::vector<GuiPhaseResetMarker> pre_state = app.phase_reset_markers.markers();
    const int                 hint_last = app.last_selected_marker;
    for (int idx : app.selected_markers) {
        GuiPhaseResetMarker* m = app.phase_reset_markers.marker_mut(idx);
        if (!m) continue;
        m->time_seconds = snap_to_timestamp_grid(m->time_seconds + delta);
    }
    undo.push_undo_phase_reset(std::move(pre_state), hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}
