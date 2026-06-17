#include "warpmarkers_ops.h"

#include "audio.h"
#include "render.h"
#include "phase_reset_markers_ops.h"
#include "platform_wayland.h"
#include "target_render.h"
#include "time_format.h"
#include "timemap.h"
#include "frame_map.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

// X.7.5a: warp-authoring cluster. Method bodies are byte-identical to
// the lambdas they replaced in main.cpp, with these mechanical rewrites:
//
//   push_undo, push_undo_phase_reset,
//   push_undo_both                 → undo.push_undo*
//   recompute_dirty                → undo.recompute_dirty
//   sync_playhead_to_last_selected → selection.sync_playhead_to_last_selected
//   invalidate_waveform_area       → viewport.invalidate_waveform_area
//   invalidate_timestamp_area      → viewport.invalidate_timestamp_area
//   invalidate_top_strip           → viewport.invalidate_top_strip
//   move_playhead_to               → viewport.move_playhead_to
//   stop_playback_if_playing       → playback_lifecycle.stop_playback_if_playing
//   clear_hover_popup              → viewport.clear_hover_popup
//                                    (X.7.13 retired the std::function forwarders)
//   resolve_inherited_tempo,
//   resolve_inherited_tempo_scale,
//   current_samples_per_pixel,
//   waveform_area, union_rect,
//   playhead_invalidate_rect       → free functions, no qualifier change
//
// Drag write-back: apply_drag_motion writes proposed positions to
// app.drag.moveable_times only; the per-list live stores stay untouched
// during motion. commit_drag does the write-back step that copies
// moveable_times into the active-list's marker time_seconds before
// pushing the snapshot.

void GuiWarpMarkersOps::drop_marker(double time_seconds, bool inherit) {
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    // Snap before the within-eps dup check so the check and the stored
    // marker both see the grid value.
    time_seconds = snap_to_timestamp_grid(time_seconds);
    const double sr_d = static_cast<double>(sr);
    const double spp  = current_samples_per_pixel(app, audio);
    const double eps  = static_cast<double>(kMarkerHitHalfPx) * spp / sr_d;  // kMarkerHitHalfPx pixels at current zoom
    const auto& mv = app.warpmarkers.markers();
    if (reject_if_marker_within_eps(mv, time_seconds, eps, "warp")) return;
    // Snapshot pre-mutation state for undo. Captured after the dup
    // check so rejected drops don't leave a no-op entry on the stack.
    std::vector<GuiWarpMarker> pre_state = mv;
    const int              hint_last = app.last_selected_marker;
    GuiWarpMarker nm;
    nm.time_seconds    = time_seconds;
    nm.tempo_inherits  = inherit;
    // pass markers carry inert defaults; their effective tempo is
    // resolved live from the marker list at every read site.
    if (inherit) {
        nm.tempo_base  = 1.0;
        nm.tempo_scale = "1.0000";
    } else {
        nm.tempo_base = 1.0;
        nm.tempo_scale.clear();
    }
    const int new_idx = app.warpmarkers.insert_marker(std::move(nm));
    // Newly-dropped marker becomes the sole selection per chunk I.
    app.selected_markers.clear();
    app.selected_markers.insert(new_idx);
    app.last_selected_marker = new_idx;
    undo.push_undo(std::move(pre_state), OpKind::Create, hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();

    // Move the playhead to the new marker for consistency with click-
    // to-select behavior. Done last so invalidations in the helper
    // don't double-paint with the ones above.
    const int64_t src_sample = static_cast<int64_t>(std::nearbyint(
        time_seconds * static_cast<double>(sr)));
    int64_t sample = src_sample;
    if (app.active_audio_view == 'T') {
        const auto tmap_after = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
        sample = to_domain_frame(app, src_sample, tmap_after);
    }
    viewport.move_playhead_to(sample);

    // Discrete frame_map change while target view is displayed: the plate
    // must re-warp. Route this one-shot jump through the synchronous
    // rebuild — the same fix applied to tab cycling (Tab / Shift+Tab /
    // Ctrl+Shift+Tab) and render-view entry — so the re-warped waveform,
    // stems, flags, and playhead all land in one frame instead of
    // flashing across the async worker's rebuild window. Source view
    // skips it: marker edits don't change source-domain waveform pixels.
    if (app.active_audio_view == 'T') viewport.kick_waveform_sync();
    target_render.trigger();
}

void GuiWarpMarkersOps::drop_marker_at_playhead() {
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    std::vector<FrameMapSegment> tmap;
    if (app.active_audio_view == 'T') {
        tmap = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
    }
    const int64_t src_frame =
        to_source_frame(app, app.playhead_cursor_sample, tmap);
    const double t = static_cast<double>(src_frame) /
                     static_cast<double>(sr);
    drop_marker(t, /*inherit=*/false);
}

void GuiWarpMarkersOps::drop_inherit_marker_at_playhead() {
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    std::vector<FrameMapSegment> tmap;
    if (app.active_audio_view == 'T') {
        tmap = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
    }
    const int64_t src_frame =
        to_source_frame(app, app.playhead_cursor_sample, tmap);
    const double t = static_cast<double>(src_frame) /
                     static_cast<double>(sr);
    drop_marker(t, /*inherit=*/true);
}

void GuiWarpMarkersOps::delete_selected_marker() {
    if (app.selected_markers.empty()) return;
    const auto& mv = app.warpmarkers.markers();

    // Validate the batch. Reject the whole operation if any member is
    // the time-0 first marker or has a label_def referenced from outside
    // the selection set.
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(mv.size())) {
            std::fprintf(stderr,
                "warptempo_gui: delete rejected: stale selection index\n");
            return;
        }
        if (mv[idx].time_seconds == 0.0) {
            std::fprintf(stderr,
                "warptempo_gui: cannot delete first warp marker (time 0)\n");
            return;
        }
        if (mv[idx].label_def.empty()) continue;
        std::string refs;
        int ref_count = 0;
        for (size_t i = 0; i < mv.size(); ++i) {
            if (app.selected_markers.count(static_cast<int>(i))) continue;
            if (!mv[i].label_ref.empty() &&
                mv[i].label_ref == mv[idx].label_def) {
                char tbuf[32];
                std::snprintf(tbuf, sizeof(tbuf), "%.3fs",
                              mv[i].time_seconds);
                if (!refs.empty()) refs += ", ";
                refs += tbuf;
                ++ref_count;
            }
        }
        if (ref_count > 0) {
            std::fprintf(stderr,
                "warptempo_gui: cannot delete marker: label '%s' is "
                "referenced at %s\n",
                mv[idx].label_def.c_str(), refs.c_str());
            return;
        }
    }

    // All validations passed — capture snapshot and selection hint
    // before mutating so the undo can restore the pre-delete selection.
    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    const int              hint_last = app.last_selected_marker;
    // Delete in descending order so earlier indices stay valid.
    for (auto it = app.selected_markers.rbegin();
         it != app.selected_markers.rend(); ++it) {
        app.warpmarkers.remove_marker(*it);
    }
    app.selected_markers.clear();
    app.last_selected_marker = -1;
    undo.push_undo(std::move(pre_state), OpKind::Destroy, hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    // Same discrete-frame_map-change class as drop_marker (see comment
    // there): re-warp synchronously in target view.
    if (app.active_audio_view == 'T') viewport.kick_waveform_sync();
    target_render.trigger();
}

// Shift+Delete variant. Auto-cascades label_refs of any selected def
// into the deletion batch, so the user doesn't have to hand-pick each
// ref before deleting the def. With the cascade, the "label is
// referenced from outside the selection" check is unnecessary — every
// ref is now inside the batch by construction.
void GuiWarpMarkersOps::force_delete_selected_marker() {
    if (app.selected_markers.empty()) return;
    const auto& mv = app.warpmarkers.markers();

    std::set<int> expanded = app.selected_markers;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(mv.size())) {
            std::fprintf(stderr,
                "warptempo_gui: delete rejected: stale selection index\n");
            return;
        }
        if (mv[idx].label_def.empty()) continue;
        for (size_t i = 0; i < mv.size(); ++i) {
            if (!mv[i].label_ref.empty() &&
                mv[i].label_ref == mv[idx].label_def) {
                expanded.insert(static_cast<int>(i));
            }
        }
    }

    for (int idx : expanded) {
        if (idx < 0 || idx >= static_cast<int>(mv.size())) {
            std::fprintf(stderr,
                "warptempo_gui: delete rejected: stale selection index\n");
            return;
        }
        if (mv[idx].time_seconds == 0.0) {
            std::fprintf(stderr,
                "warptempo_gui: cannot delete first warp marker (time 0)\n");
            return;
        }
    }

    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    int hint_last = app.last_selected_marker;
    {
        // Prefer focusing undo on the label_def that drove the cascade.
        // Refs were pulled into `expanded` automatically; the def is the
        // action's subject. Search the original selection (not the
        // expanded batch) so only an explicitly selected def wins.
        int def_hint = -1;
        const bool last_is_def =
            app.last_selected_marker >= 0 &&
            app.last_selected_marker < static_cast<int>(mv.size()) &&
            !mv[app.last_selected_marker].label_def.empty() &&
            app.selected_markers.count(app.last_selected_marker);
        if (last_is_def) {
            def_hint = app.last_selected_marker;
        } else {
            for (int idx : app.selected_markers) {
                if (idx >= 0 && idx < static_cast<int>(mv.size()) &&
                    !mv[idx].label_def.empty()) {
                    def_hint = idx;   // app.selected_markers is a std::set,
                    break;            // so iteration is ascending — lowest wins
                }
            }
        }
        if (def_hint >= 0) hint_last = def_hint;
    }
    for (auto it = expanded.rbegin(); it != expanded.rend(); ++it) {
        app.warpmarkers.remove_marker(*it);
    }
    app.selected_markers.clear();
    app.last_selected_marker = -1;
    undo.push_undo(std::move(pre_state), OpKind::Destroy, hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

// Shift+P: convert each selected marker's tempo source. Cache-free —
// the only stored state on a pass marker is `tempo_inherits = true`
// plus inert defaults. Three input cases per marker:
//   - owning   → pass: inert defaults; label_def preserved.
//   - pass     → owning: freeze the resolved tempo/scale at this moment;
//                label_def preserved.
//   - label_ref → pass: clear the ref; inert defaults.
// The first marker is silently skipped (it must own its tempo).
void GuiWarpMarkersOps::toggle_inherits() {
    if (app.selected_markers.empty()) return;
    if (app.last_selected_marker < 0) return;
    // Fine-tuning op: collapse the selection to the focused marker, so the
    // operation (and the resulting selection) targets last_selected only.
    app.selected_markers.clear();
    app.selected_markers.insert(app.last_selected_marker);
    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    const int              hint_last = app.last_selected_marker;
    const auto& mv_const = app.warpmarkers.markers();
    // Single-marker resolve via the canonical parser walk (slice once).
    const std::vector<WarpMarker> resolved_src = slice_to_warp_markers(mv_const);
    bool changed = false;
    for (int idx : app.selected_markers) {
        GuiWarpMarker* m = app.warpmarkers.marker_mut(idx);
        if (!m) continue;
        if (idx == 0) continue;
        if (!m->label_ref.empty()) {
            m->label_ref.clear();
            m->tempo_inherits = true;
            m->tempo_base     = 1.0;
            m->tempo_scale    = "1.0000";
        } else if (m->tempo_inherits) {
            const double resolved_tempo =
                resolve_inherited_tempo(resolved_src, idx);
            const std::string resolved_scale =
                resolve_inherited_tempo_scale(resolved_src, idx);
            m->tempo_inherits = false;
            m->tempo_base     = resolved_tempo;
            m->tempo_scale    = resolved_scale;
        } else {
            m->tempo_inherits = true;
            m->tempo_base     = 1.0;
            m->tempo_scale    = "1.0000";
        }
        changed = true;
    }
    if (!changed) return;
    undo.push_undo(std::move(pre_state), OpKind::Other, hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

// Toggle the disabled flag on each selected marker. Per chunk U patch 3
// the flag is allowed on any marker (cascade still applies only when the
// toggled marker is a label_def).
void GuiWarpMarkersOps::toggle_disabled() {
    if (app.selected_markers.empty()) return;
    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    const int              hint_last = app.last_selected_marker;
    bool changed = false;
    for (int idx : app.selected_markers) {
        GuiWarpMarker* m = app.warpmarkers.marker_mut(idx);
        if (!m) continue;
        m->disabled = !m->disabled;
        changed = true;
    }
    if (!changed) return;
    undo.push_undo(std::move(pre_state), OpKind::Other, hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

// Nudge every selected marker by `delta`. Label refs are silently
// skipped (no tempo to nudge — convert via Shift+P first). Pass markers
// resolve walk-backward to get their starting tempo/scale, then freeze
// to owning at the nudged value. Owning markers nudge in place.
// Clamps to [0.01, 9.99]. Only dirties / invalidates on real change.
void GuiWarpMarkersOps::adjust_tempo(double delta) {
    if (app.selected_markers.empty()) return;
    if (app.last_selected_marker < 0) return;
    // Fine-tuning op: collapse the selection to the focused marker.
    app.selected_markers.clear();
    app.selected_markers.insert(app.last_selected_marker);
    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    const int              hint_last = app.last_selected_marker;
    const auto& mv_const = app.warpmarkers.markers();
    // Single-marker resolve via the canonical parser walk (slice once).
    const std::vector<WarpMarker> resolved_src = slice_to_warp_markers(mv_const);
    bool changed = false;
    for (int idx : app.selected_markers) {
        GuiWarpMarker* m = app.warpmarkers.marker_mut(idx);
        if (!m) continue;
        if (!m->label_ref.empty()) continue;
        double      start_tempo;
        std::string start_scale;
        if (m->tempo_inherits) {
            start_tempo = resolve_inherited_tempo(resolved_src, idx);
            start_scale = resolve_inherited_tempo_scale(resolved_src, idx);
        } else {
            start_tempo = m->tempo_base;
            start_scale = m->tempo_scale;
        }
        double new_tempo = start_tempo + delta;
        if (new_tempo < 0.01) new_tempo = 0.01;
        if (new_tempo > 9.99) new_tempo = 9.99;
        if (!m->tempo_inherits && new_tempo == m->tempo_base) continue;
        m->tempo_inherits = false;
        m->tempo_base     = new_tempo;
        m->tempo_scale    = start_scale;
        changed = true;
    }
    if (!changed) return;
    undo.push_undo(std::move(pre_state), OpKind::Other, hint_last);
    undo.recompute_dirty();
    viewport.invalidate_top_strip();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

bool GuiWarpMarkersOps::begin_drag(int hit, int mouse_x) {
    if (hit < 0) return false;
    const int sr = audio.sample_rate();
    if (sr <= 0) return false;
    const bool phase_reset = (app.active_markers_view == 'P');
    const int n = phase_reset
        ? static_cast<int>(app.phase_reset_markers.markers().size())
        : static_cast<int>(app.warpmarkers.markers().size());
    if (hit >= n) return false;

    const double sr_d = static_cast<double>(sr);
    auto t_of = [&](int idx) -> double {
        if (phase_reset) {
            return app.phase_reset_markers.markers()[idx].time_seconds;
        }
        return app.warpmarkers.markers()[idx].time_seconds;
    };

    // Drag is a single-marker fine-tuning gesture: it always moves only the
    // grabbed marker, regardless of the current selection — each marker is
    // placed deliberately, and group drag does more harm than good. The
    // selection collapse to {hit} is deferred until motion is observed (see
    // pending_collapse_to_hit), so a click without a drag leaves the
    // selection untouched.
    std::set<int> drag_set;
    drag_set.insert(hit);
    bool pending_collapse = true;

    // First-marker protection: refuse index 0 and any effective-time-0
    // marker. Runs before any selection mutation so a refused drag
    // leaves selection genuinely unchanged.
    for (int idx : drag_set) {
        if (idx == 0 || t_of(idx) == 0.0) {
            std::fprintf(stderr, phase_reset
                ? "warptempo_gui: first phase_reset marker cannot be dragged\n"
                : "warptempo_gui: first warp marker cannot be dragged\n");
            return false;
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
        const auto tmap = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
        const int64_t anchor_frame_src =
            to_source_frame(app, anchor_frame_active, tmap);
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
    const double eps = static_cast<double>(kMarkerHitHalfPx) * spp / sr_d;
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
        app, sr, static_cast<long>(audio.total_frames()));
    // Capture the pre-drag list state for undo. Commit pushes the
    // active-mode snapshot if motion landed; otherwise it's discarded.
    if (phase_reset) {
        d.pre_drag_phase_reset_snapshot = app.phase_reset_markers.markers();
    } else {
        d.pre_drag_snapshot = app.warpmarkers.markers();
    }
    d.pre_drag_last_selected = app.last_selected_marker;
    d.hit_marker             = hit;
    d.pending_collapse_to_hit = pending_collapse;
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
void GuiWarpMarkersOps::apply_drag_motion(double raw_delta) {
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
        // group) lives in one place. Deferred to first motion
        // (pending_collapse_to_hit, always armed at begin_drag) so a click
        // without a drag leaves selection untouched.
        if (first_motion && app.drag.pending_collapse_to_hit) {
            selection.set_single_selection(app.drag.hit_marker);
            app.drag.pending_collapse_to_hit = false;
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
void GuiWarpMarkersOps::commit_drag() {
    if (!app.drag.active) return;
    const bool moved = app.drag.moved;
    const bool phase_reset = (app.drag.drag_mode == 'P');
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
    if (moved && !phase_reset && app.active_audio_view == 'T') {
        playhead_src_anchor = to_source_frame(
            app, app.playhead_cursor_sample, app.drag.frozen_frame_map);
        reanchor_playhead = true;
    }
    // Cascade validation for warp drags. The frozen-coord regime keeps
    // build_timemaps from running during motion (paint sources from the
    // pre-drag snapshot in app.drag.frozen_frame_map), so a drag end-state
    // that violates the per-segment label_ref final_multiplier ceiling
    // can otherwise land in the live store and leave the next
    // build_target_view_frame_map call returning empty. Construct the
    // proposed post-write warp marker vector, run build_target_view_frame_map
    // against it, and reject the drag on empty result. Phase-reset markers
    // don't participate in label cascade, so this branch is warp-only.
    if (moved && !phase_reset) {
        std::vector<GuiWarpMarker> proposed = app.warpmarkers.markers();
        for (size_t k = 0; k < app.drag.dragging_markers.size(); ++k) {
            const int idx = app.drag.dragging_markers[k];
            if (k >= app.drag.moveable_times.size()) continue;
            if (idx < 0 || idx >= static_cast<int>(proposed.size())) continue;
            proposed[idx].time_seconds = app.drag.moveable_times[k];
        }
        // Builds from a proposed list, intentionally bypasses the
        // live-state cache.
        const auto tmap = build_target_view_frame_map(
            proposed, app.engine_settings.scale, audio.sample_rate(),
            static_cast<long>(audio.total_frames()));
        if (tmap.empty()) {
            std::fprintf(stderr,
                "warptempo_gui: drag rejected: would violate label "
                "multiplier constraint\n");
            app.drag = DragState{};
            viewport.invalidate_waveform_area();
            viewport.invalidate_timestamp_area();
            return;
        }
    }
    if (moved) {
        for (size_t k = 0; k < app.drag.dragging_markers.size(); ++k) {
            const int idx = app.drag.dragging_markers[k];
            if (k >= app.drag.moveable_times.size()) continue;
            const double new_t = app.drag.moveable_times[k];
            if (phase_reset) {
                GuiPhaseResetMarker* m =
                    app.phase_reset_markers.marker_mut(idx);
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
    if (moved) {
        if (phase_reset) {
            undo.push_undo_phase_reset(std::move(snap_t), OpKind::Move, hint_last);
        } else {
            undo.push_undo(std::move(snap_w), OpKind::Move, hint_last);
        }
        undo.recompute_dirty();
        viewport.invalidate_timestamp_area();
    }
    viewport.invalidate_waveform_area();
    if (reanchor_playhead) {
        const int sr = audio.sample_rate();
        const auto new_map = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
        viewport.move_playhead_to(
            to_domain_frame(app, playhead_src_anchor, new_map));
    }
    // Same discrete-frame_map-change class as drop_marker (see comment
    // there): the commit re-warps the plate, so render it synchronously
    // — re-warped waveform and re-anchored playhead land in one frame.
    if (moved && !phase_reset && app.active_audio_view == 'T')
        viewport.kick_waveform_sync();
    if (moved) target_render.trigger();
}

// Compute (delta_min, delta_max) scalar bounds for shifting the current
// selection set by a uniform delta. Neighbors: for each selected marker,
// the nearest non-selected marker on each side. Trim is purely cosmetic
// and does not constrain edits. Returns (0, 0) if empty or time-0 marker
// present (move forbidden).
std::pair<double, double> GuiWarpMarkersOps::compute_selection_delta_bounds(bool& ok) {
    ok = false;
    const auto& mv = app.warpmarkers.markers();
    if (app.selected_markers.empty()) return {0.0, 0.0};
    const int sr = audio.sample_rate();
    if (sr <= 0) return {0.0, 0.0};
    // Bounds check is needed inline because the frame-zero pin
    // dereferences mv[idx]; the warp wrapper owns this precondition,
    // not the shared helper.
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(mv.size())) return {0.0, 0.0};
        if (idx == 0 || mv[idx].time_seconds == 0.0) return {0.0, 0.0};
    }
    const double sr_d = static_cast<double>(sr);
    const double spp  = current_samples_per_pixel(app, audio);
    const double eps  = static_cast<double>(kMarkerHitHalfPx) * spp / sr_d;  // kMarkerHitHalfPx pixels at current zoom
    const double total_duration =
        static_cast<double>(audio.total_frames()) / sr_d;
    auto bounds = compute_neighbor_walk_bounds(
        mv, app.selected_markers, eps, total_duration);
    ok = true;
    return bounds;
}

// Shift every selected marker by the clamped delta. Returns whether any
// marker actually moved.
bool GuiWarpMarkersOps::apply_selection_shift(double raw_delta) {
    bool ok = false;
    auto [d_min, d_max] = compute_selection_delta_bounds(ok);
    if (!ok) return false;
    double delta = raw_delta;
    if (delta < d_min) delta = d_min;
    if (delta > d_max) delta = d_max;
    if (delta == 0.0) return false;
    for (int idx : app.selected_markers) {
        GuiWarpMarker* m = app.warpmarkers.marker_mut(idx);
        if (!m) continue;
        m->time_seconds = snap_to_timestamp_grid(m->time_seconds + delta);
    }
    return true;
}

// Nudge selected markers by +/- 1 pixel of source time at current zoom.
// direction: -1 for earlier (up/left), +1 for later (down/right).
//
// Brief 3b: target view interprets the nudge visually — each selected
// marker shifts by direction * 1 target-pixel; the resulting source-
// seconds delta per marker depends on the local alpha, so the per-
// marker shifts diverge. Validation walks each marker's proposed new
// source-time against its non-selected source-domain neighbors; the
// nudge is all-or-nothing.
void GuiWarpMarkersOps::nudge_selected_markers(int direction) {
    if (app.loading || audio.total_frames() <= 0) return;
    // Nudges move the playhead (via sync_playhead_to_last_selected).
    // Stop playback first — Ctrl+Left/Right is the only caller path.
    playback_lifecycle.stop_playback_if_playing();
    if (app.selected_markers.empty()) return;
    if (app.last_selected_marker < 0) return;
    // Fine-tuning op: collapse the selection to the focused marker.
    app.selected_markers.clear();
    app.selected_markers.insert(app.last_selected_marker);
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    const double spp = current_samples_per_pixel(app, audio);

    if (app.active_audio_view == 'T') {
        const auto& mv = app.warpmarkers.markers();
        for (int idx : app.selected_markers) {
            if (idx < 0 || idx >= static_cast<int>(mv.size())) return;
            if (idx == 0 || mv[idx].time_seconds == 0.0) return;
        }
        const double sr_d = static_cast<double>(sr);
        const auto tmap = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
        const double total_duration =
            static_cast<double>(audio.total_frames()) / sr_d;
        // Compute proposed new source-times per selected marker, then
        // validate against non-selected source-domain neighbors. eps
        // here is the minimum 1-frame gap; the visual 3-px gap source
        // view enforces doesn't translate uniformly to source-domain
        // under a non-trivial frame_map, so we degrade to strict-monotonic
        // with one-frame headroom.
        const double eps = 1.0 / sr_d;
        std::vector<std::pair<int, double>> proposals;
        proposals.reserve(app.selected_markers.size());
        for (int idx : app.selected_markers) {
            const double t_src = mv[idx].time_seconds;
            const double t_tgt = map_source_to_target(
                static_cast<size_t>(std::llrint(t_src * sr_d)), tmap);
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
            if (t_new == mv[idx].time_seconds) continue;
            int prev = idx - 1;
            while (prev >= 0 && app.selected_markers.count(prev)) --prev;
            const double lo = (prev >= 0)
                ? (mv[prev].time_seconds + eps)
                : eps;
            int next = idx + 1;
            const int n = static_cast<int>(mv.size());
            while (next < n && app.selected_markers.count(next)) ++next;
            const double hi = (next < n)
                ? (mv[next].time_seconds - eps)
                : (total_duration - eps);
            if (t_new < lo || t_new > hi) return;
            any_changed = true;
        }
        if (!any_changed) return;
        std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
        const int              hint_last = app.last_selected_marker;
        for (const auto& [idx, t_new] : proposals) {
            GuiWarpMarker* m = app.warpmarkers.marker_mut(idx);
            if (!m) continue;
            m->time_seconds = t_new;
        }
        undo.push_undo(std::move(pre_state), OpKind::Move, hint_last);
        selection.sync_playhead_to_last_selected();
        undo.recompute_dirty();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
        return;
    }

    const double delta_s =
        static_cast<double>(direction) * spp / static_cast<double>(sr);
    if (delta_s == 0.0) return;
    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    const int              hint_last = app.last_selected_marker;
    if (apply_selection_shift(delta_s)) {
        undo.push_undo(std::move(pre_state), OpKind::Move, hint_last);
        selection.sync_playhead_to_last_selected();
        undo.recompute_dirty();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    }
}

// `j`: move every selected marker so last_selected lands on the playhead.
// All-or-nothing: if any resulting position would violate monotonicity
// or trim, reject the whole operation with a stderr note.
void GuiWarpMarkersOps::jump_selection_to_playhead() {
    if (app.selected_markers.empty()) return;
    if (app.last_selected_marker < 0) return;
    // Fine-tuning op: collapse the selection to the focused marker, so the
    // anchor and the shifted marker are one and the same.
    app.selected_markers.clear();
    app.selected_markers.insert(app.last_selected_marker);
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    const auto& mv = app.warpmarkers.markers();
    if (app.last_selected_marker >= static_cast<int>(mv.size())) return;
    const double anchor_t = mv[app.last_selected_marker].time_seconds;
    // Target view: the playhead is target-domain; the anchor marker's
    // time_seconds is source-domain. Inverse-translate playhead before
    // taking the delta so the resulting shift is source-seconds.
    std::vector<FrameMapSegment> tmap;
    if (app.active_audio_view == 'T') {
        tmap = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
    }
    const int64_t ph_src =
        to_source_frame(app, app.playhead_cursor_sample, tmap);
    const double ph_t =
        static_cast<double>(ph_src) / static_cast<double>(sr);
    const double delta = ph_t - anchor_t;
    if (delta == 0.0) return;

    bool ok = false;
    auto [d_min, d_max] = compute_selection_delta_bounds(ok);
    if (!ok || delta < d_min || delta > d_max) {
        std::fprintf(stderr,
            "warptempo_gui: jump rejected: would violate marker "
            "ordering\n");
        return;
    }
    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    const int              hint_last = app.last_selected_marker;
    for (int idx : app.selected_markers) {
        GuiWarpMarker* m = app.warpmarkers.marker_mut(idx);
        if (!m) continue;
        m->time_seconds = snap_to_timestamp_grid(m->time_seconds + delta);
    }
    undo.push_undo(std::move(pre_state), OpKind::Move, hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}
