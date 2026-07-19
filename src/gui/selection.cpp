#include "selection.h"

#include "audio.h"
#include "playback.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

void Selection::repair_last_selected() {
    if (app.last_selected_marker < 0) return;
    if (app.selected_markers.count(app.last_selected_marker)) return;
    if (app.selected_markers.empty()) {
        app.last_selected_marker = -1;
    } else {
        // Pick the largest remaining index in selected_markers (or -1
        // if empty).
        app.last_selected_marker = *app.selected_markers.rbegin();
    }
}

void Selection::set_single_selection(int idx) {
    app.selected_markers.clear();
    if (idx >= 0) app.selected_markers.insert(idx);
    app.last_selected_marker = (idx >= 0) ? idx : -1;
    viewport.invalidate_top_strip();
}

void Selection::clear_selection() {
    if (app.selected_markers.empty() && app.last_selected_marker == -1)
        return;   // nothing selected
    app.selected_markers.clear();
    app.last_selected_marker = -1;
    viewport.invalidate_top_strip();
}

bool Selection::toggle_selection_membership(int idx) {
    if (idx < 0) return false;
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
    const bool phase_reset = (app.active_markers_view == 'P');

    // Bind const refs once so the count, frame_of, and is_disabled reads below
    // all index the same live authoring stores.
    const std::vector<GuiWarpMarker>& warp_vec = app.warpmarkers.markers();
    const std::vector<GuiPhaseResetMarker>& phase_reset_vec =
        app.phaseresetmarkers.markers();

    const int n = phase_reset
        ? static_cast<int>(phase_reset_vec.size())
        : static_cast<int>(warp_vec.size());
    // The Tab walk is markers-only (trim bounds are not cycle stops — trim is
    // outside the selection system). frame_of / is_disabled below are only
    // invoked for indices in [0, n), so n == 0 simply yields no candidate.

    // Helper to read frame-of-index in the active domain. Source view:
    // marker source-frame == active-domain frame (identity). Target view:
    // forward-translate through the display context (the live map) so
    // frame_of values are comparable to playhead_cursor_sample /
    // viewport_start_sample below.
    auto frame_of = [&](int i) -> int64_t {
        int64_t src_f;
        if (phase_reset) {
            src_f = phase_reset_vec[i].time_frame;
        } else {
            src_f = warp_vec[i].time_frame;
        }
        return source_frame_to_active_domain(app, audio, src_f);
    };

    // Disabled-skip predicate. Warp side respects label_ref cascade via
    // effective_disabled; phase reset has no cascade and reads the bool.
    auto is_disabled = [&](int i) -> bool {
        if (phase_reset) {
            return phase_reset_vec[i].disabled;
        }
        return effective_disabled(warp_vec, i);
    };

    // The playhead frame is the sole cycle anchor. Strict frame inequalities
    // in the scan below prevent re-landing on the stop we are standing on;
    // markers sharing one active-domain frame are traversed by the in-group
    // step so every member is Tab-reachable — stacks are legal at rest
    // (same-column coincidences, which the parser resolver normalizes at
    // render/preview time). Disabled markers are skipped as if absent from the
    // active mode's list. Trim is not part of the selection system, so trim
    // bounds are not cycle stops; the walk is markers-only.
    const int64_t ph_f = app.playhead_cursor_sample;

    // Current stop: the last-selected marker when it sits on the playhead frame
    // (a playhead moved elsewhere by a click breaks the equality and disables
    // the in-group step below naturally).
    int cur_marker = -1;
    {
        const int last = app.last_selected_marker;
        if (last >= 0 && last < n && frame_of(last) == ph_f) cur_marker = last;
    }

    // In-group step, tried before the frame scan. When the previous Tab landed
    // on a marker, the caller synced the playhead onto it, so that marker's
    // frame equals ph_f. Advance one place within the shared frame in the cycle
    // direction (ascending index forward, descending backward).
    int land_marker = -1;
    if (cur_marker >= 0) {
        if (forward) {
            for (int i = cur_marker + 1; i < n; ++i) {
                if (frame_of(i) != ph_f) break;   // frame-sorted: group ends
                if (is_disabled(i)) continue;
                land_marker = i; break;
            }
        } else {
            for (int i = cur_marker - 1; i >= 0; --i) {
                if (frame_of(i) != ph_f) break;
                if (is_disabled(i)) continue;
                land_marker = i; break;
            }
        }
    }

    // Frame scan: nearest marker strictly past the playhead in the walk
    // direction. Markers are frame-sorted, so the first in-direction hit is the
    // nearest.
    if (land_marker < 0) {
        if (forward) {
            for (int i = 0; i < n; ++i) {
                if (frame_of(i) > ph_f && !is_disabled(i)) {
                    land_marker = i; break;
                }
            }
        } else {
            for (int i = n - 1; i >= 0; --i) {
                if (frame_of(i) < ph_f && !is_disabled(i)) {
                    land_marker = i; break;
                }
            }
        }
    }

    if (land_marker < 0) return;   // nothing ahead

    // Selection only. Viewport positioning is owned entirely by the sole
    // caller (cycle_marker_focus), which under follow mode centers the focused
    // stop in one write. A scroll-into-view here would be a redundant
    // intermediate viewport write — overridden by that centering in the same
    // keypress — and the resulting damage, accumulated against a non-final
    // viewport, is what produced the outline-blink / cursor-hop artifact.
    set_single_selection(land_marker);
}

void Selection::select_next_marker() { cycle_selection(true);  }
void Selection::select_prev_marker() { cycle_selection(false); }

void Selection::prune_live_selection() {
    const int n = (app.active_markers_view == 'P')
        ? static_cast<int>(app.phaseresetmarkers.markers().size())
        : static_cast<int>(app.warpmarkers.markers().size());
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

void Selection::sync_playhead_to_last_selected(bool edge_follow) {
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    const int last = app.last_selected_marker;
    if (last < 0) return;

    int64_t src_sample = 0;
    if (app.active_markers_view == 'P') {
        const auto& tv = app.phaseresetmarkers.markers();
        if (last >= static_cast<int>(tv.size())) return;
        src_sample = tv[last].time_frame;
    } else {
        const auto& mv = app.warpmarkers.markers();
        if (last >= static_cast<int>(mv.size())) return;
        src_sample = mv[last].time_frame;
    }
    // Target view: the marker time_frame is source-domain but the
    // playhead is active-domain. Forward-translate so the playhead
    // lands at the marker's displayed (target-frame) position.
    const int64_t target_sample =
        source_frame_to_active_domain(app, audio, src_sample);
    if (edge_follow) {
        // Alt+Left/Right marker nudge: the marker stepped one pixel, so move
        // the playhead to it through the same edge-follow path bare Left/Right
        // uses (move_playhead_to), scrolling the viewport at most one pixel to
        // keep the marker just inside the edge. The default path keeps
        // jump_playhead_to's center-on-offscreen, which suits a jump to a
        // possibly-distant marker (undo, navigate) but is jarring for a nudge.
        viewport.move_playhead_to(target_sample);
    } else {
        jump_playhead_to(target_sample);
    }
}

void Selection::jump_playhead_to(int64_t target_sample) {
    // Playhead domain clamp through clamp_playhead_to_live_domain (the
    // domain ruling): the playhead rests in [0, total - 1], the one inclusive
    // authored domain every marker column and both trim bounds share.
    target_sample = clamp_playhead_to_live_domain(target_sample, app, audio);
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
