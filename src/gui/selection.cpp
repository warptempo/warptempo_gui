#include "selection.h"

#include "audio.h"
#include "playback.h"
#include "timemap.h"
#include "frame_map.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

void Selection::repair_last_selected() {
    if (app.last_selected_marker < 0) return;
    if (app.selected_markers.count(app.last_selected_marker)) return;
    if (app.selected_markers.empty()) {
        app.last_selected_marker = -1;
    } else {
        // Pick the largest remaining index (spec: "the largest remaining
        // index in selected_markers, or -1 if empty").
        app.last_selected_marker = *app.selected_markers.rbegin();
    }
}

void Selection::set_single_selection(int idx) {
    app.selected_markers.clear();
    if (idx >= 0) app.selected_markers.insert(idx);
    app.last_selected_marker = (idx >= 0) ? idx : -1;
    // Brief C: a marker-selecting gesture makes Markers the group that
    // Delete / Ctrl+drag act on.
    app.last_sel_group = LastSelGroup::Markers;
    // A fresh single-select in the marker group drops any trim-boundary
    // selection — the two groups are orthogonal, but selecting a marker as
    // the sole selection means trim is no longer selected.
    ViewState& vs = active_view_state(app);
    const bool had_trim = vs.trim_begin_selected || vs.trim_end_selected;
    vs.trim_begin_selected = false;
    vs.trim_end_selected   = false;
    viewport.invalidate_top_strip();
    if (had_trim) viewport.invalidate_waveform_area();
}

void Selection::clear_selection() {
    ViewState& vs = active_view_state(app);
    const bool had_trim = vs.trim_begin_selected || vs.trim_end_selected;
    const bool had_markers =
        !app.selected_markers.empty() || app.last_selected_marker != -1;
    if (!had_trim && !had_markers) return;   // nothing selected anywhere

    app.selected_markers.clear();
    app.last_selected_marker = -1;
    vs.trim_begin_selected = false;
    vs.trim_end_selected   = false;
    app.last_sel_group = LastSelGroup::Markers;

    viewport.invalidate_top_strip();
    // Trim stems live in the stem/waveform-area cache, so repaint it when a
    // trim bound was deselected (amber stem returns from kSelected).
    if (had_trim) viewport.invalidate_waveform_area();
}

bool Selection::toggle_selection_membership(int idx) {
    if (idx < 0) return false;
    app.last_sel_group = LastSelGroup::Markers;
    bool added;
    auto it = app.selected_markers.find(idx);
    if (it == app.selected_markers.end()) {
        app.selected_markers.insert(idx);
        app.last_selected_marker = idx;
        added = true;
    } else {
        app.selected_markers.erase(it);
        if (app.last_selected_marker == idx) repair_last_selected();
        added = false;
    }
    viewport.invalidate_top_strip();
    return added;
}

void Selection::sanitize_selection_after_restore(int n) {
    std::set<int> cleaned;
    for (int idx : app.selected_markers) {
        if (idx >= 0 && idx < n) cleaned.insert(idx);
    }
    app.selected_markers = std::move(cleaned);
    if (!app.selected_markers.count(app.last_selected_marker)) {
        app.last_selected_marker = -1;
    }
}

void Selection::cycle_selection(bool forward) {
    const int sr = audio.sample_rate();
    const bool phase_reset = (app.active_markers_view == 'P');

    // Render-view cycles the displayed render-domain collections; normal
    // authoring cycles the live authoring stores. Mirrors the branch in
    // prune_live_selection. Bind const refs once so the count, frame_of,
    // and is_disabled reads below all index the same vectors.
    const std::vector<GuiWarpMarker>& warp_vec =
        app.render_view_enabled ? app.render_view_markers
                                : app.warpmarkers.markers();
    const std::vector<GuiPhaseResetMarker>& reset_vec =
        app.render_view_enabled ? app.render_view_phase_resets
                                : app.phase_reset_markers.markers();

    const int n = phase_reset
        ? static_cast<int>(reset_vec.size())
        : static_cast<int>(warp_vec.size());
    if (n == 0) return;

    // Helper to read frame-of-index in the active domain. Source view:
    // marker source-frame == active-domain frame (identity). Target view:
    // forward-translate to active-domain so frame_of values are
    // comparable to playhead_cursor_sample / viewport_start_sample below.
    std::vector<FrameMapSegment> tmap;
    if (app.active_audio_view == 'T') {
        tmap = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
    }
    auto frame_of = [&](int i) -> int64_t {
        int64_t src_f;
        if (phase_reset) {
            src_f = static_cast<int64_t>(std::nearbyint(
                reset_vec[i].time_seconds *
                static_cast<double>(sr)));
        } else {
            src_f = static_cast<int64_t>(std::nearbyint(
                warp_vec[i].time_seconds *
                static_cast<double>(sr)));
        }
        return to_domain_frame(app, src_f, tmap);
    };

    // Disabled-skip predicate. Warp side respects label_ref cascade via
    // effective_disabled; phase reset has no cascade and reads the bool.
    auto is_disabled = [&](int i) -> bool {
        if (phase_reset) {
            return reset_vec[i].disabled;
        }
        return effective_disabled(warp_vec, i);
    };

    // Playhead is the sole anchor for cycle direction. Strict inequality
    // ensures a marker exactly at the playhead's frame is not a valid
    // landing — Tab is motion, not confirmation. Disabled markers are
    // skipped as if they were not present in the active mode's list.
    int new_sel = -1;
    const int64_t ph_f = app.playhead_cursor_sample;
    if (forward) {
        for (int i = 0; i < n; ++i) {
            if (frame_of(i) > ph_f && !is_disabled(i)) {
                new_sel = i; break;
            }
        }
    } else {
        for (int i = n - 1; i >= 0; --i) {
            if (frame_of(i) < ph_f && !is_disabled(i)) {
                new_sel = i; break;
            }
        }
    }

    if (new_sel < 0) return;

    // Selection only. Viewport positioning is owned entirely by the sole
    // caller (cycle_marker_focus_with_recenter), which centers the
    // focused marker in one write. A scroll-into-view here would be a
    // redundant intermediate viewport write — overridden by that
    // centering in the same keypress — and the resulting damage,
    // accumulated against a non-final viewport, is what produced the
    // outline-blink / cursor-hop artifact.
    set_single_selection(new_sel);
}

void Selection::select_next_marker() { cycle_selection(true);  }
void Selection::select_prev_marker() { cycle_selection(false); }

void Selection::prune_live_selection() {
    int n = 0;
    if (app.render_view_enabled) {
        n = (app.active_markers_view == 'P')
            ? static_cast<int>(app.render_view_phase_resets.size())
            : static_cast<int>(app.render_view_markers.size());
    } else {
        n = (app.active_markers_view == 'P')
            ? static_cast<int>(app.phase_reset_markers.markers().size())
            : static_cast<int>(app.warpmarkers.markers().size());
    }
    for (auto it = app.selected_markers.begin();
         it != app.selected_markers.end();) {
        if (*it < 0 || *it >= n) {
            it = app.selected_markers.erase(it);
        } else {
            ++it;
        }
    }
    if (app.last_selected_marker < 0 ||
        app.last_selected_marker >= n ||
        !app.selected_markers.count(app.last_selected_marker)) {
        app.last_selected_marker =
            app.selected_markers.empty()
                ? -1
                : *app.selected_markers.rbegin();
    }
}

void Selection::sync_playhead_to_last_selected() {
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    const int last = app.last_selected_marker;
    if (last < 0) return;

    int64_t src_sample = 0;
    if (app.active_markers_view == 'P') {
        const auto& tv = app.phase_reset_markers.markers();
        if (last >= static_cast<int>(tv.size())) return;
        src_sample = static_cast<int64_t>(std::nearbyint(
            tv[last].time_seconds * static_cast<double>(sr)));
    } else {
        const auto& mv = app.warpmarkers.markers();
        if (last >= static_cast<int>(mv.size())) return;
        src_sample = static_cast<int64_t>(std::nearbyint(
            mv[last].time_seconds * static_cast<double>(sr)));
    }
    // Target view: the marker time_seconds is source-domain but the
    // playhead is active-domain. Forward-translate so the playhead
    // lands at the marker's displayed (target-frame) position.
    int64_t target_sample = src_sample;
    if (app.active_audio_view == 'T') {
        const auto tmap = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
        target_sample = to_domain_frame(app, src_sample, tmap);
    }
    jump_playhead_to(target_sample);
}

void Selection::jump_playhead_to(int64_t target_sample) {
    app.playhead_cursor_sample = target_sample;

    const int64_t visible = samples_visible(app, audio);
    const bool offscreen =
        target_sample <  app.viewport_start_sample ||
        target_sample >= app.viewport_start_sample + visible;
    if (offscreen) {
        app.viewport_start_sample = target_sample - visible / 2;
        clamp_viewport_start(app, audio);
    }
    if (playback.is_playing()) playback.resync_predictor();
}
