#include "marker_drag.h"

#include "audio.h"
#include "frame_map.h"
#include "frame_map_view.h"
#include "phaseresetmarkers.h"
#include "render.h"
#include "target_render.h"
#include "time_format.h"
#include "warpmarkers.h"
#include "warpmarkers_ops.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <set>
#include <utility>
#include <vector>

bool MarkerDragOps::begin_drag(int hit, int mouse_x) {
    if (hit < 0) return false;
    const int sr = audio.sample_rate();
    if (sr <= 0) return false;
    const bool phase_reset = (app.active_markers_view == 'P');
    const int n = phase_reset
        ? static_cast<int>(app.phaseresetmarkers.markers().size())
        : static_cast<int>(app.warpmarkers.markers().size());
    if (hit >= n) return false;

    const double sr_d = static_cast<double>(sr);
    auto t_of = [&](int idx) -> double {
        if (phase_reset) {
            return app.phaseresetmarkers.markers()[idx].time_seconds;
        }
        return app.warpmarkers.markers()[idx].time_seconds;
    };

    // Drag is a single-marker fine-tuning gesture: it always moves only the
    // grabbed marker, regardless of the current selection — each marker is
    // placed deliberately, and group drag does more harm than good. The
    // selection collapse to {hit} is deferred until motion is observed, so a
    // click without a drag leaves the selection untouched.
    std::set<int> drag_set;
    drag_set.insert(hit);

    // First-marker protection (warp only): the warp marker at frame 0 is
    // the mandatory project anchor and never moves, so refuse index 0 and
    // any effective-time-0 warp marker. Phase resets have no pinned frame-0
    // anchor — the first phase reset marker is optional and freely
    // repositionable — so the pin is skipped in phase reset view. The
    // neighbor-bounds clamp below still keeps a left-edge phase reset
    // strictly after frame 0. Runs before any selection mutation so a
    // refused drag leaves the selection genuinely unchanged.
    if (!phase_reset) {
        for (int idx : drag_set) {
            if (idx == 0 || t_of(idx) == 0.0) {
                std::fprintf(stderr,
                    "warptempo_gui: first warp marker cannot be dragged\n");
                return false;
            }
        }
    }

    DragState d;
    d.active = true;
    d.drag_mode = phase_reset ? 'P' : 'W';
    d.dragging_markers.assign(drag_set.begin(), drag_set.end());
    d.original_times.reserve(d.dragging_markers.size());
    for (int idx : d.dragging_markers) {
        d.original_times.push_back(t_of(idx));
    }

    // Anchor mouse time — computed at mouse_x in the waveform's X axis.
    // Target view: the press position is in target-domain frames; the
    // dragged markers' original_times are source-domain seconds. Convert
    // the anchor to source-domain at the boundary so the on_motion delta
    // (mouse_time - anchor_mouse_time_seconds) lives in source-seconds.
    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    if (app.active_audio_view == 'T') {
        const int64_t anchor_frame_active =
            app.viewport_start_sample +
            static_cast<int64_t>(std::nearbyint(
                static_cast<double>(mouse_x - area.x) * spp));
        const int64_t anchor_frame_src =
            active_domain_to_source_frame(app, audio, anchor_frame_active);
        d.anchor_mouse_time_seconds =
            static_cast<double>(anchor_frame_src) / sr_d;
    } else {
        const double vp_time =
            static_cast<double>(app.viewport_start_sample) / sr_d;
        d.anchor_mouse_time_seconds =
            vp_time + static_cast<double>(mouse_x - area.x) * spp / sr_d;
    }

    // Compute scalar delta_min / delta_max from per-marker neighbor
    // bounds. Correct for both contiguous and non-contiguous drag sets.
    // eps enforces a 4-pixel visual gap at the current zoom — markers
    // never stack even at the tightest clamp. When a selected marker
    // has no neighbor on a side, clamp to [eps, total_duration - eps]
    // so the drag can't leave the audio range.
    const double eps = marker_hit_eps_seconds(spp, sr_d);
    const double total_duration =
        static_cast<double>(audio.total_frames()) / sr_d;

    d.delta_min = -std::numeric_limits<double>::infinity();
    d.delta_max =  std::numeric_limits<double>::infinity();

    for (size_t k = 0; k < d.dragging_markers.size(); ++k) {
        const int idx = d.dragging_markers[k];
        const double orig_t = d.original_times[k];

        // Nearest non-dragged neighbor to the left.
        int prev = idx - 1;
        while (prev >= 0 && drag_set.count(prev)) --prev;
        if (prev >= 0) {
            const double lb = (t_of(prev) + eps) - orig_t;
            if (lb > d.delta_min) d.delta_min = lb;
        } else {
            const double lb = eps - orig_t;
            if (lb > d.delta_min) d.delta_min = lb;
        }

        // Nearest non-dragged neighbor to the right.
        int next = idx + 1;
        while (next < n && drag_set.count(next)) ++next;
        if (next < n) {
            const double ub = (t_of(next) - eps) - orig_t;
            if (ub < d.delta_max) d.delta_max = ub;
        } else {
            const double ub = (total_duration - eps) - orig_t;
            if (ub < d.delta_max) d.delta_max = ub;
        }
    }

    // Viewport clamp: keep the grabbed marker within the visible strip so a
    // mouse drag can't push it offscreen, where its precise position would be
    // hidden. Only the grabbed marker (hit) is clamped; co-dragged markers
    // ride the same scalar delta and stay bound by data / neighbor alone — the
    // same way Ctrl+Shift+drag clamps the grabbed bound but lets its partner
    // run to the clip edge. viewport_marker_bounds is active-domain while the
    // delta lives in source seconds, so inverse-translate the edges; the
    // domain map is monotonic, so the source clamp matches the active-pixel
    // clamp. The grabbed marker is on-screen at grab, so these bounds bracket
    // delta = 0 and never invert [delta_min, delta_max].
    {
        const auto vb = viewport_marker_bounds(app, audio);
        const double vp_lo_src = static_cast<double>(
            active_domain_to_source_frame(app, audio, vb.first))  / sr_d;
        const double vp_hi_src = static_cast<double>(
            active_domain_to_source_frame(app, audio, vb.second)) / sr_d;
        const double orig_grabbed = t_of(hit);
        const double vp_lb = vp_lo_src - orig_grabbed;
        const double vp_ub = vp_hi_src - orig_grabbed;
        if (vp_lb > d.delta_min) d.delta_min = vp_lb;
        if (vp_ub < d.delta_max) d.delta_max = vp_ub;
    }

    d.moved = false;
    // Seed moveable_times from original_times. apply_drag_motion writes
    // moveable_times[k] = original_times[k] + delta on every motion event.
    d.moveable_times = d.original_times;
    // Capture a snapshot of the pre-drag frame_map so paint can route
    // selected-marker positions and target-view waveform through a frozen
    // coordinate system. Both views capture: in source view the segment
    // list is harmless (its forward translation is identity at the
    // relevant input boundaries) and the symmetry lets paint code route
    // through DragOverlay unconditionally. Build may fail (returns empty)
    // — in that case paint walks the identity fallback for the duration
    // of the drag, which is the correct degradation.
    d.frozen_frame_map = build_target_view_frame_map(
        app.warpmarkers.markers(), app.engine_settings.scale,
        sr, static_cast<long>(audio.total_frames()));
    // Capture the pre-drag list state for undo. Commit pushes the
    // active-mode snapshot only when the drag produced a net position
    // change; a drag that returns to its origin is discarded.
    if (phase_reset) {
        d.pre_drag_phase_reset_snapshot = app.phaseresetmarkers.markers();
    } else {
        d.pre_drag_snapshot = app.warpmarkers.markers();
    }
    d.pre_drag_last_selected = app.last_selected_marker;
    d.hit_marker             = hit;
    app.drag = std::move(d);
    viewport.clear_hover_popup();
    return true;
}

// Apply a raw delta (mouse-derived) to the dragging markers, clamped.
// Writes proposed new times into app.drag.moveable_times — the live
// marker store is NOT mutated. Paint reads moveable_times through the
// DragOverlay so dragged markers paint at their proposed positions while
// the frame_map stays frozen at its pre-drag snapshot. The live store is
// updated wholesale in commit_drag.
//
// Symmetric across warp and phase reset: both branches write the same
// statement into the same vector. The waveform cache stays valid
// throughout the drag — viewport / trim / dimensions / view-domain /
// frozen-frame_map-hash don't change — so the invalidation triggers a
// cheap blit of cached pixels with stems, flags, and playhead repainted
// on top. A narrow per-marker rect would be wrong in target view, where
// the dragged marker's proposed position lands at the cursor's pixel
// regardless of which markers surround it.
void MarkerDragOps::apply_drag_motion(double raw_delta) {
    if (!app.drag.active) return;
    double delta = raw_delta;
    if (delta < app.drag.delta_min) delta = app.drag.delta_min;
    if (delta > app.drag.delta_max) delta = app.drag.delta_max;

    bool any_changed = false;
    for (size_t k = 0; k < app.drag.dragging_markers.size(); ++k) {
        const double new_t =
            snap_to_timestamp_grid(app.drag.original_times[k] + delta);
        if (k >= app.drag.moveable_times.size()) continue;
        if (app.drag.moveable_times[k] == new_t) continue;
        app.drag.moveable_times[k] = new_t;
        any_changed = true;
    }
    if (any_changed) {
        const bool first_motion = !app.drag.moved;
        app.drag.moved = true;
        // Selection collapse on the press-to-motion edge: once a drag is
        // real, focus the whole selection on the single grabbed marker.
        // Delegated to Selection::set_single_selection — the same helper a
        // marker click uses — so the rule (drop the rest of the marker
        // selection AND any trim-boundary selection, make Markers the active
        // group) lives in one place. Deferred to first motion so a click
        // without a drag leaves selection untouched.
        if (first_motion) {
            selection.set_single_selection(app.drag.hit_marker);
        }
        viewport.invalidate_waveform_area();
        viewport.invalidate_top_strip();
    }
}

// Commit the current drag. Caller ensures drag was active. Sets dirty
// only if the markers actually moved. Playhead is left in place for
// source-view and phase-reset drags; warp drags in target view
// re-anchor it through the source domain (see the capture/re-anchor
// blocks below).
//
// Write-back step: the live store was untouched throughout motion (the
// proposed positions lived in app.drag.moveable_times and paint read
// them through the DragOverlay). On commit, walk dragging_markers and
// assign each marker's time_seconds from moveable_times before pushing
// the pre-drag snapshot onto the undo stack. Symmetric across warp and
// phase reset: identical statement shape on each side.
void MarkerDragOps::commit_drag() {
    if (!app.drag.active) return;
    const bool phase_reset = (app.drag.drag_mode == 'P');
    // Commit gates on NET change, not on whether motion occurred.
    // app.drag.moved latches true on the first snapped-position change
    // during motion and never clears, so a drag that wanders to a new
    // grid cell and returns to its original snapped position arrives here
    // with moved == true but zero net change. Pushing an undo entry then
    // records a snapshot byte-equal to the live store: a no-op history
    // entry that both undo and redo restore invisibly. original_times and
    // moveable_times are parallel grid-aligned doubles, so an exact
    // compare across the dragged markers is the true "did anything move"
    // test — the same test begin_drag's "if motion landed" note intends.
    bool net_changed = false;
    for (size_t k = 0; k < app.drag.dragging_markers.size(); ++k) {
        if (k >= app.drag.moveable_times.size()) continue;
        if (app.drag.moveable_times[k] != app.drag.original_times[k]) {
            net_changed = true;
            break;
        }
    }
    // Bug fix: the playhead is stored in active-domain frames, so a warp
    // drag that changes the frame_map would silently re-point it at
    // different music. Capture its source-domain anchor against the
    // pre-drag (frozen) map now; after write-back it is re-expressed
    // through the new map below, so the playhead follows the deformation
    // exactly like the marker chips. Phase-reset drags don't touch the
    // frame_map, and in source view the coordinate is already
    // source-domain — both skip.
    bool    reanchor_playhead   = false;
    int64_t playhead_src_anchor = 0;
    if (net_changed && !phase_reset && app.active_audio_view == 'T') {
        playhead_src_anchor = to_source_frame(
            app, app.playhead_cursor_sample, app.drag.frozen_frame_map);
        reanchor_playhead = true;
    }
    // Cascade validation for warp drags. The frozen-coord regime keeps
    // build_maps from running during motion (paint sources from the
    // pre-drag snapshot in app.drag.frozen_frame_map), so a drag end-state
    // that violates the per-segment label_ref final_multiplier ceiling
    // can otherwise land in the live store and leave the next
    // build_target_view_frame_map call returning empty. Construct the
    // proposed post-write warp marker vector, run build_target_view_frame_map
    // against it, and reject the drag on empty result. Phase-reset markers
    // don't participate in label cascade, so this branch is warp-only.
    if (net_changed && !phase_reset) {
        std::vector<GuiWarpMarker> proposed = app.warpmarkers.markers();
        for (size_t k = 0; k < app.drag.dragging_markers.size(); ++k) {
            const int idx = app.drag.dragging_markers[k];
            if (k >= app.drag.moveable_times.size()) continue;
            if (idx < 0 || idx >= static_cast<int>(proposed.size())) continue;
            proposed[idx].time_seconds = app.drag.moveable_times[k];
        }
        if (!proposed_warp_state_valid(
                proposed, app.engine_settings.scale, audio.sample_rate(),
                static_cast<long>(audio.total_frames()))) {
            app.drag = DragState{};
            viewport.invalidate_waveform_area();
            viewport.invalidate_timestamp_area();
            return;
        }
    }
    if (net_changed) {
        for (size_t k = 0; k < app.drag.dragging_markers.size(); ++k) {
            const int idx = app.drag.dragging_markers[k];
            if (k >= app.drag.moveable_times.size()) continue;
            const double new_t = app.drag.moveable_times[k];
            if (phase_reset) {
                GuiPhaseResetMarker* m =
                    app.phaseresetmarkers.marker_mut(idx);
                if (!m) continue;
                m->time_seconds = new_t;
            } else {
                GuiWarpMarker* m = app.warpmarkers.marker_mut(idx);
                if (!m) continue;
                m->time_seconds = new_t;
            }
        }
    }
    std::vector<GuiWarpMarker>    snap_w =
        std::move(app.drag.pre_drag_snapshot);
    std::vector<GuiPhaseResetMarker> snap_t =
        std::move(app.drag.pre_drag_phase_reset_snapshot);
    const int                 hint_last = app.drag.pre_drag_last_selected;
    app.drag = DragState{};
    if (net_changed) {
        if (phase_reset) {
            undo.push_undo_phase_reset(std::move(snap_t), hint_last);
        } else {
            undo.push_undo(std::move(snap_w), hint_last);
        }
        undo.recompute_dirty();
        viewport.invalidate_timestamp_area();
    }
    viewport.invalidate_waveform_area();
    if (reanchor_playhead) {
        viewport.move_playhead_to(
            source_frame_to_active_domain(app, audio, playhead_src_anchor));
    }
    // Same discrete-frame_map-change class as drop_marker (see comment
    // there): the commit re-warps the plate, so render it synchronously
    // — re-warped waveform and re-anchored playhead land in one frame.
    if (net_changed && !phase_reset && app.active_audio_view == 'T')
        viewport.kick_waveform_sync();
    if (net_changed) target_render.trigger();
}
