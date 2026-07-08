#include "warpmarkers_ops.h"

#include "audio.h"
#include "warp_frame_map_build.h"
#include "warp_frame_map_view.h"
#include "target_render.h"
#include "time_format.h"
#include "warpmarkers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Warp-authoring cluster. Method bodies map onto the original main.cpp
// lambdas via these mechanical rewrites:
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
//   resolve_inherited_tempo,
//   resolve_inherited_tempo_scale,
//   current_samples_per_pixel,
//   waveform_area, union_rect,
//   playhead_invalidate_rect       → free functions, no qualifier change

// Index of the nearest marker strictly before `time_seconds` that survives
// into the render, or -1 if none. Uses the same cascade definition as render
// resolution and hover (effective_disabled: a marker is out if its own
// disabled flag is set, or it is an enabled label ref whose target def is
// disabled), so copy-previous copies the previous render-visible tempo
// rather than attributing to a marker the render ignores. `time_seconds`
// need not be present in `mv` — drop_copy_previous_at_playhead calls this
// with the prospective drop time before insertion, landing on the same
// slot insert_marker's lower_bound would place the new marker at, one
// step back.
int find_immediate_prior(const std::vector<GuiWarpMarker>& mv,
                          double time_seconds) {
    auto it = std::lower_bound(
        mv.begin(), mv.end(), time_seconds,
        [](const GuiWarpMarker& a, double t) { return a.time_seconds < t; });
    int i = static_cast<int>(it - mv.begin()) - 1;
    while (i >= 0 && effective_disabled(mv, i)) --i;
    return i;
}

void GuiWarpMarkersOps::drop_marker(double time_seconds, bool inherit,
                                     double base, const std::string& scale) {
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    // Snap before the within-eps dup check so the check and the stored
    // marker both see the grid value.
    time_seconds = snap_to_timestamp_grid(time_seconds);
    const double sr_d = static_cast<double>(sr);
    const double spp  = current_samples_per_pixel(app, audio);
    const double eps = marker_hit_eps_seconds(spp, sr_d);
    // Defend the source end with the same zoom-based eps used for marker
    // spacing, matching the total_duration - eps clamp drag/nudge/jump use.
    // The eps floor (kMarkerHitHalfPx px at max zoom is 2.5 ms) exceeds the
    // millisecond snap grid, so a near-end drop can never snap past
    // total_frames and break the render.
    if (time_seconds > static_cast<double>(audio.total_frames()) / sr_d - eps)
        return;
    const auto& mv = app.warpmarkers.markers();
    if (reject_if_marker_within_eps(mv, time_seconds, eps, "warp")) return;
    GuiWarpMarker nm;
    nm.time_seconds    = time_seconds;
    nm.tempo_inherits  = inherit;
    nm.tempo_base      = base;
    nm.tempo_scale     = scale;
    // Snapshot pre-mutation state for undo. Captured after the dup check
    // so rejected drops don't leave a no-op entry on the stack.
    std::vector<GuiWarpMarker> pre_state = mv;
    const int              hint_last = app.last_selected_marker;
    const int new_idx = app.warpmarkers.insert_marker(std::move(nm));
    // Newly-dropped marker becomes the sole selection.
    app.selected_markers.clear();
    app.selected_markers.insert(new_idx);
    app.last_selected_marker = new_idx;
    undo.push_undo_warp(std::move(pre_state), hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();

    // Move the playhead to the new marker for consistency with click-
    // to-select behavior. Done last so invalidations in the helper
    // don't double-paint with the ones above.
    const int64_t src_sample = static_cast<int64_t>(std::nearbyint(
        time_seconds * static_cast<double>(sr)));
    const int64_t sample = source_frame_to_active_domain(app, audio, src_sample);
    viewport.move_playhead_to(sample);

    // Discrete warp_frame_map change while target view is displayed: the plate
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
    const int64_t src_frame =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);
    const double t = static_cast<double>(src_frame) /
                     static_cast<double>(sr);
    drop_marker(t, /*inherit=*/false, /*base=*/1.0, /*scale=*/"");
}

// `s` (W view): drop an explicit owner that copies the immediate-prior
// marker's effective tempo (base x scale), via the shared resolver also
// used by the hover popup.
// Exception: when the prior marker is a label ref, the copy is skipped and a
// neutral owner (base 1.0 / empty scale) is dropped instead. Copying the
// ref's resolved effective value would freeze a literal of the pre-drop
// value, but inserting this marker re-deforms the ref's own segment so the
// ref's effective value shifts — the new marker would then hold a value the
// ref no longer carries. A 1.00 owner leaves the ref's segment unchanged.
// Falls back to base 1.0 / empty scale if there is no prior marker
// (should not happen given the mandatory time-0 first marker).
void GuiWarpMarkersOps::drop_copy_previous_at_playhead() {
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    const int64_t src_frame =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);
    const double t = static_cast<double>(src_frame) /
                     static_cast<double>(sr);
    const auto& mv = app.warpmarkers.markers();
    const int prev_idx = find_immediate_prior(mv, t);
    double      base  = 1.0;
    std::string scale;
    if (prev_idx >= 0 && mv[prev_idx].label_ref.empty()) {
        const MarkerEffective eff = marker_effective(
            slice_to_warp_markers(mv), prev_idx, sr);
        base  = eff.base;
        scale = eff.scale;
    }
    drop_marker(t, /*inherit=*/false, base, scale);
}

// Deleting an owning marker lets downstream pass markers re-resolve to the
// next earlier owner, the same live re-resolution that disabling an owner
// already produces; no values are frozen on delete.
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
    undo.push_undo_warp(std::move(pre_state), hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    // Same discrete-warp_frame_map-change class as drop_marker (see comment
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
    undo.push_undo_warp(std::move(pre_state), hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    // Same discrete-warp_frame_map-change class as drop_marker (see comment
    // there): re-warp synchronously in target view.
    if (app.active_audio_view == 'T') viewport.kick_waveform_sync();
    target_render.trigger();
}

// Ctrl+N: convert each selected marker's tempo source. Cache-free —
// the only stored state on a pass marker is `tempo_inherits = true`
// plus inert defaults. Three input cases per marker:
//   - owning   → pass: inert defaults; label_def preserved.
//   - pass     → owning: freeze the resolved tempo/scale at this moment;
//                label_def preserved.
//   - label_ref → pass: clear the ref; inert defaults.
// The first marker is excluded: the zero marker must own its tempo (the map
// builder applies it from source frame 0 and the inheritance walk has no owner
// before it), so the whole-batch reject below refuses to toggle it.
void GuiWarpMarkersOps::toggle_inherits() {
    if (app.selected_markers.empty()) return;
    if (app.last_selected_marker < 0) return;
    // Fine-tuning op: collapse the selection to the focused marker, so the
    // operation (and the resulting selection) targets last_selected only.
    app.selected_markers.clear();
    app.selected_markers.insert(app.last_selected_marker);
    const auto& mv_const = app.warpmarkers.markers();
    // Whole-batch reject: the zero marker must own its tempo, so it can never
    // be toggled to a pass.
    for (int idx : app.selected_markers) {
        if (idx >= 0 && idx < static_cast<int>(mv_const.size()) &&
            mv_const[idx].time_seconds == 0.0) {
            std::fprintf(stderr,
                "warptempo_gui: cannot toggle inherit on first warp marker "
                "(time 0)\n");
            return;
        }
    }
    std::vector<GuiWarpMarker> proposed = mv_const;
    // Single-marker resolve via the canonical parser walk (slice once).
    const std::vector<WarpMarker> resolved_src = slice_to_warp_markers(mv_const);
    bool changed = false;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(proposed.size())) continue;
        GuiWarpMarker& m = proposed[idx];
        if (!m.label_ref.empty()) {
            m.label_ref.clear();
            m.tempo_inherits = true;
            m.tempo_base     = 1.0;
            m.tempo_scale    = "1.0000";
        } else if (m.tempo_inherits) {
            const double resolved_tempo =
                resolve_inherited_tempo(resolved_src, idx);
            const std::string resolved_scale =
                resolve_inherited_tempo_scale(resolved_src, idx);
            m.tempo_inherits = false;
            m.tempo_base     = resolved_tempo;
            m.tempo_scale    = resolved_scale;
        } else {
            m.tempo_inherits = true;
            m.tempo_base     = 1.0;
            m.tempo_scale    = "1.0000";
        }
        changed = true;
    }
    if (!changed) return;
    std::vector<GuiWarpMarker> pre_state = mv_const;
    const int              hint_last = app.last_selected_marker;
    app.warpmarkers.markers_mut() = std::move(proposed);
    undo.push_undo_warp(std::move(pre_state), hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

// Toggle the disabled flag on each selected marker. The flag is allowed
// on any marker (cascade still applies only when the toggled marker is a
// label_def).
void GuiWarpMarkersOps::toggle_disabled() {
    if (app.selected_markers.empty()) return;
    const auto& mv_const = app.warpmarkers.markers();
    // Whole-batch reject: the zero marker must own its tempo (the map builder
    // applies it from source frame 0 and the inheritance walk has no owner
    // before it), so it can never be disabled. Symmetric with the delete guard.
    for (int idx : app.selected_markers) {
        if (idx >= 0 && idx < static_cast<int>(mv_const.size()) &&
            mv_const[idx].time_seconds == 0.0) {
            std::fprintf(stderr,
                "warptempo_gui: cannot disable first warp marker (time 0)\n");
            return;
        }
    }
    std::vector<GuiWarpMarker> proposed = mv_const;
    const int              hint_last = app.last_selected_marker;
    bool changed = false;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(proposed.size())) continue;
        proposed[idx].disabled = !proposed[idx].disabled;
        changed = true;
    }
    if (!changed) return;
    std::vector<GuiWarpMarker> pre_state = mv_const;
    app.warpmarkers.markers_mut() = std::move(proposed);
    undo.push_undo_warp(std::move(pre_state), hint_last);
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

// Nudge every selected marker by `delta`. Label refs are silently
// skipped (no tempo to nudge — convert via Ctrl+N first). Pass markers
// resolve walk-backward to get their starting tempo/scale, then freeze
// to owning at the nudged value. Owning markers nudge in place.
// Clamps to [0.01, 9.99]. Only dirties / invalidates on real change.
void GuiWarpMarkersOps::adjust_tempo(double delta) {
    if (app.active_markers_view != 'W') return;
    if (app.selected_markers.empty()) return;
    if (app.last_selected_marker < 0) return;
    // Fine-tuning op: collapse the selection to the focused marker.
    app.selected_markers.clear();
    app.selected_markers.insert(app.last_selected_marker);
    const auto& mv_const = app.warpmarkers.markers();
    std::vector<GuiWarpMarker> proposed = mv_const;
    // Single-marker resolve via the canonical parser walk (slice once).
    const std::vector<WarpMarker> resolved_src = slice_to_warp_markers(mv_const);
    bool changed = false;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(proposed.size())) continue;
        GuiWarpMarker& m = proposed[idx];
        if (!m.label_ref.empty()) continue;
        double      start_tempo;
        std::string start_scale;
        if (m.tempo_inherits) {
            start_tempo = resolve_inherited_tempo(resolved_src, idx);
            start_scale = resolve_inherited_tempo_scale(resolved_src, idx);
        } else {
            start_tempo = m.tempo_base;
            start_scale = m.tempo_scale;
        }
        double new_tempo = start_tempo + delta;
        if (new_tempo < 0.01) new_tempo = 0.01;
        if (new_tempo > 9.99) new_tempo = 9.99;
        if (!m.tempo_inherits && new_tempo == m.tempo_base) continue;
        m.tempo_inherits = false;
        m.tempo_base     = new_tempo;
        m.tempo_scale    = start_scale;
        changed = true;
    }
    if (!changed) return;
    std::vector<GuiWarpMarker> pre_state = mv_const;
    const int              hint_last = app.last_selected_marker;
    app.warpmarkers.markers_mut() = std::move(proposed);
    undo.push_undo_warp(std::move(pre_state), hint_last);
    undo.recompute_dirty();
    viewport.invalidate_top_strip();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
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
    const double eps = marker_hit_eps_seconds(spp, sr_d);
    const double total_duration =
        static_cast<double>(audio.total_frames()) / sr_d;
    auto bounds = compute_neighbor_walk_bounds(
        mv, app.selected_markers, eps, total_duration);
    ok = true;
    return bounds;
}

// Shift every selected marker by the clamped delta, snapping each
// destination directionally to the persistence grid so a nudge press never
// travels less than one pixel. Returns whether any marker actually moved.
bool GuiWarpMarkersOps::apply_selection_shift(double raw_delta) {
    bool ok = false;
    auto [d_min, d_max] = compute_selection_delta_bounds(ok);
    if (!ok) return false;
    double delta = raw_delta;
    if (delta < d_min) delta = d_min;
    if (delta > d_max) delta = d_max;
    if (delta == 0.0) return false;
    const auto& mv = app.warpmarkers.markers();
    std::vector<GuiWarpMarker> proposed = mv;
    bool any_changed = false;
    const int direction = (delta > 0.0) ? 1 : -1;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(proposed.size())) continue;
        const double t_old = proposed[idx].time_seconds;
        double t_new =
            snap_to_timestamp_grid_directional(t_old + delta, direction);
        // Wall case only: the directional snap can overshoot the clamped
        // [d_min, d_max] bound by strictly less than 1 ms. Fall back to the
        // nearest snap of the clamped destination so the selection still
        // creeps flush against the neighbor.
        if ((direction > 0 && t_new > t_old + d_max) ||
            (direction < 0 && t_new < t_old + d_min))
            t_new = snap_to_timestamp_grid(t_old + delta);
        if (t_new == t_old) continue;
        proposed[idx].time_seconds = t_new;
        any_changed = true;
    }
    if (!any_changed) return false;
    app.warpmarkers.markers_mut() = std::move(proposed);
    return true;
}

// Nudge selected markers by +/- 1 pixel of source time at current zoom.
// direction: -1 for earlier (up/left), +1 for later (down/right).
//
// Target view interprets the nudge visually — each selected marker
// shifts by direction * 1 target-pixel; the resulting source-
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
        const auto& target_warp_frame_map = target_view_warp_frame_map_cached(
            app, sr, static_cast<long>(audio.total_frames())).warp_frame_map;
        const double total_duration =
            static_cast<double>(audio.total_frames()) / sr_d;
        // Compute proposed new source-times per selected marker, then
        // validate against non-selected source-domain neighbors. eps
        // here is the minimum 1-frame gap; the visual 3-px gap source
        // view enforces doesn't translate uniformly to source-domain
        // under a non-trivial warp_frame_map, so we degrade to strict-monotonic
        // with one-frame headroom. In target view a pixel delta can
        // translate to a sub-grid source move only when the map is deformed
        // by extreme stretch, which is outside the usage model (real
        // effective speed stays around 2x or below); the directional snap
        // steps such a move up to the 1 ms grid minimum, and when the step
        // cannot fit against a neighbor the designed response is exactly
        // this all-or-nothing hard reject — no finer-grained backstop is
        // wanted.
        const double eps = 1.0 / sr_d;
        std::vector<std::pair<int, double>> proposals;
        proposals.reserve(app.selected_markers.size());
        for (int idx : app.selected_markers) {
            const double t_src = mv[idx].time_seconds;
            const double t_tgt = map_source_to_target(
                static_cast<size_t>(std::nearbyint(t_src * sr_d)), target_warp_frame_map);
            const double t_tgt_new = t_tgt +
                static_cast<double>(direction) * spp;
            const size_t q = (t_tgt_new < 0.0)
                ? static_cast<size_t>(0)
                : static_cast<size_t>(std::llrint(t_tgt_new));
            // Directional snap so a press never travels less than one
            // target pixel; the warp frame map is monotone increasing, so
            // the target-domain direction is the source-domain direction.
            // No wall fallback here: the neighbor validation below rejects
            // all-or-nothing when the full-pixel step cannot fit, which is
            // the designed response.
            const double t_src_new = snap_to_timestamp_grid_directional(
                map_target_to_source(q, target_warp_frame_map) / sr_d,
                direction);
            proposals.emplace_back(idx, t_src_new);
        }
        bool any_changed = false;
        std::vector<GuiWarpMarker> proposed = mv;
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
            if (t_new < lo || t_new > hi) {
                std::fprintf(stderr,
                    "warptempo_gui: nudge rejected: would collide with a "
                    "neighbor\n");
                return;
            }
            proposed[idx].time_seconds = t_new;
            any_changed = true;
        }
        if (!any_changed) return;
        std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
        const int              hint_last = app.last_selected_marker;
        app.warpmarkers.markers_mut() = std::move(proposed);
        undo.push_undo_warp(std::move(pre_state), hint_last);
        selection.sync_playhead_to_last_selected(/*edge_follow=*/true);
        undo.recompute_dirty();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        // Unlike drop and delete, nudge does not route through
        // kick_waveform_sync: nudges arrive at key-repeat rate, and a
        // synchronous plate rebuild per repeat would cost up to the plate's
        // worst-case render time per keypress; the one-frame async lag on
        // a one-pixel map change is imperceptible.
        target_render.trigger();
        return;
    }

    const double delta_s =
        static_cast<double>(direction) * spp / static_cast<double>(sr);
    if (delta_s == 0.0) return;
    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    const int              hint_last = app.last_selected_marker;
    if (apply_selection_shift(delta_s)) {
        undo.push_undo_warp(std::move(pre_state), hint_last);
        selection.sync_playhead_to_last_selected(/*edge_follow=*/true);
        undo.recompute_dirty();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    }
}
