#include "warpmarkers_ops.h"

#include "audio.h"
#include "warp_frame_map_build.h"
#include "warp_frame_map_view.h"
#include "target_render.h"
#include "warpmarkers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

// Index of the nearest marker strictly before `time_frame` that survives
// into the render, or -1 if none. Uses the same cascade definition as render
// resolution and hover (effective_disabled: a marker is out if its own
// disabled flag is set, or it is an enabled label ref whose target def is
// disabled), so copy-previous copies the previous render-visible tempo
// rather than attributing to a marker the render ignores. `time_frame`
// need not be present in `mv` — drop_copy_previous_at_playhead calls this
// with the prospective drop time before insertion, landing on the same
// slot insert_marker's lower_bound would place the new marker at, one
// step back.
int find_immediate_prior(const std::vector<GuiWarpMarker>& mv,
                          double time_frame) {
    auto it = std::lower_bound(
        mv.begin(), mv.end(), time_frame,
        [](const GuiWarpMarker& a, double t) { return a.time_frame < t; });
    int i = static_cast<int>(it - mv.begin()) - 1;
    while (i >= 0 && effective_disabled(mv, i)) --i;
    return i;
}

void GuiWarpMarkersOps::drop_marker(double time_frame, bool inherit,
                                     double base, std::optional<double> scale) {
    if (audio.sample_rate() <= 0) return;
    // Marker creation is a commit: the position funnels through
    // snap_authored_frame like every other gesture commit, so the stored
    // value is a whole source frame. Every current caller passes an
    // integer-valued playhead frame, so the snap is the uniformity funnel,
    // not a rounding step. The wall check below IS the load guard's
    // comparison, exactly, applied to the snapped value.
    const int64_t drop_frame = snap_authored_frame(time_frame);
    // Structural warp wall: a warp marker's segment runs to the next
    // breakpoint (or to total_frames for the last marker), and
    // build_warp_frame_map refuses sub-frame segments, so a warp position
    // needs at least one source frame of headroom before EOF. That is the
    // only placement bound — markers may sit arbitrarily close to or
    // exactly on an existing marker; ordering degeneracy is the render
    // boundary's to refuse, not this drop's.
    if (drop_frame > audio.total_frames() - 1)
        return;
    const auto& mv = app.warpmarkers.markers();
    GuiWarpMarker nm;
    nm.time_frame    = drop_frame;
    nm.tempo_inherits  = inherit;
    nm.tempo_base      = base;
    nm.tempo_scale     = scale;
    // Snapshot pre-mutation state for undo. Captured after the wall check
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
    const int64_t sample = source_frame_to_active_domain(app, audio, drop_frame);
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
    if (audio.sample_rate() <= 0) return;
    // Playhead drops produce integer-valued frame positions.
    const int64_t src_frame =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);
    drop_marker(static_cast<double>(src_frame),
                /*inherit=*/false, /*base=*/1.0, /*scale=*/std::nullopt);
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
// Falls back to base 1.0 / no typed scale if there is no prior marker
// (possible: the store may be empty or the playhead may sit before the
// first marker — marker zero is an ordinary, deletable marker).
void GuiWarpMarkersOps::drop_copy_previous_at_playhead() {
    if (audio.sample_rate() <= 0) return;
    const int64_t src_frame =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);
    const double t = static_cast<double>(src_frame);
    const auto& mv = app.warpmarkers.markers();
    const int prev_idx = find_immediate_prior(mv, t);
    double                base = 1.0;
    std::optional<double> scale;
    if (prev_idx >= 0 && mv[prev_idx].label_ref.empty()) {
        const MarkerEffective eff = marker_effective(
            slice_to_warp_markers(mv), prev_idx);
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

    // Validate the batch for stale indices only. Any marker is deletable:
    // the first-marker grammar (enabled, tempo-owning numeric marker at
    // exactly frame 0) is a render-boundary rule enforced by
    // validate_first_marker_render_grammar, and deleting a label_def its
    // refs outlive is legal too — dangling refs load and are refused at
    // the render / target-view validity gate with a popup. The default
    // zero marker created at source load plus that gate are the user's
    // tools; the GUI allows every state. Shift+Delete
    // (force_delete_selected_marker) remains the cascade convenience for
    // taking a def's refs along with it.
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(mv.size())) {
            std::fprintf(stderr,
                "warptempo_gui: delete rejected: stale selection index\n");
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
// ref before deleting the def. The cascade is a convenience, not a
// guard: plain Delete on a def is equally legal and simply leaves its
// refs dangling (loads fine; refused at the render boundary). Any
// marker — including one at time 0 — may be in the batch; the
// first-marker grammar is validate_first_marker_render_grammar's
// render-boundary rule, not a gesture gate.
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
// The first marker is togglable like any other: the requirement that the
// marker at frame 0 own its tempo is a render-boundary rule
// (validate_first_marker_render_grammar), and a pass first marker is
// refused there — surfaced by the defect-resolution series at the commit
// funnel, render dispatch, and target-view gate — not at this gesture.
void GuiWarpMarkersOps::toggle_inherits() {
    if (app.selected_markers.empty()) return;
    if (app.last_selected_marker < 0) return;
    // Fine-tuning op: collapse the selection to the focused marker, so the
    // operation (and the resulting selection) targets last_selected only.
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
        if (!m.label_ref.empty()) {
            m.label_ref.clear();
            m.tempo_inherits = true;
            m.tempo_base     = 1.0;
            m.tempo_scale.reset();
        } else if (m.tempo_inherits) {
            const double resolved_tempo =
                resolve_inherited_tempo(resolved_src, idx);
            const std::optional<double> resolved_scale =
                resolve_inherited_tempo_scale(resolved_src, idx);
            m.tempo_inherits = false;
            m.tempo_base     = resolved_tempo;
            m.tempo_scale    = resolved_scale;
        } else {
            m.tempo_inherits = true;
            m.tempo_base     = 1.0;
            m.tempo_scale.reset();
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
    // Any marker may be disabled, including the one at time 0. A disabled
    // first marker violates the first-marker render grammar, which is
    // enforced at the render boundary (validate_first_marker_render_grammar)
    // and surfaced by the defect-resolution series at the commit funnel,
    // render dispatch, and target-view gate, never here.
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

// Nudge every selected marker's tempo along the 0.01 grid. Label refs are
// silently skipped (no tempo to nudge — convert via Ctrl+N first). Pass
// markers resolve walk-backward to get their starting tempo/scale, then
// freeze to owning at the nudged value. Owning markers nudge in place.
// `delta` arrives as a multiple of 0.01 (one per keypress or wheel
// detent); its sign is the direction of travel. The landed gridpoint is
// clamped into the tempo bracket [kTempoMin, kTempoMax]
// (value_format.h). Only dirties / invalidates on real change.
//
// Grid rule: wheel/keyboard authoring lives on the 0.01 grid; typed
// precision is preserved until the wheel touches the value. A value is
// on-grid exactly when it round-trips its own cent index
// (v == std::nearbyint(v * 100.0) / 100.0 — no epsilon nudge); on-grid
// values step exactly 0.01 per notch, off-grid values snap outward to the
// adjacent gridpoint in the direction of travel (up: floor + 1 cents,
// down: ceil - 1 cents) on the first notch.
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
        double                start_tempo;
        std::optional<double> start_scale;
        if (m.tempo_inherits) {
            start_tempo = resolve_inherited_tempo(resolved_src, idx);
            start_scale = resolve_inherited_tempo_scale(resolved_src, idx);
        } else {
            start_tempo = m.tempo_base;
            start_scale = m.tempo_scale;
        }
        // Land on the adjacent 0.01 gridpoint in the direction of travel
        // (see the grid rule in the function comment). The gridpoint index
        // is computed in cents as a double, so extreme typed magnitudes
        // cannot overflow an integer type.
        const double steps = std::nearbyint(delta * 100.0);
        const double v     = start_tempo;
        double cents;
        if (v == std::nearbyint(v * 100.0) / 100.0) {
            cents = std::nearbyint(v * 100.0) + steps;   // on-grid: exact 0.01 steps
        } else if (steps > 0.0) {
            cents = std::floor(v * 100.0) + steps;       // off-grid: snap up first
        } else {
            cents = std::ceil(v * 100.0) + steps;        // off-grid: snap down first
        }
        // Constructive clamp into the authored-value bracket, the same
        // convention font_size uses: the wheel walks to the bracket edge
        // and stops there, rather than refusing. Both edges are exact in
        // cents (kTempoMin*100 = 25, kTempoMax*100 = 400). No finiteness
        // guard is needed: every restable value is in-bracket, so the cent
        // product v * 100.0 cannot leave the finite double domain.
        cents = std::clamp(cents, kTempoMin * 100.0, kTempoMax * 100.0);
        const double new_tempo = cents / 100.0;
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

// Nudge the focused marker by exactly one on-screen pixel column per
// press. direction: -1 for earlier (up/left), +1 for later (down/right).
//
// Pixel-column-anchored: the press reads the marker's currently PAINTED
// column (painted_column_of_source_frame — the stem painter's own math),
// targets the adjacent column, takes that column's time (source view:
// viewport start plus column times samples-per-pixel; target view: the
// column's target-domain time inverse-mapped through the cached map), and
// commits it through snap_authored_frame, so the stored value is a whole
// source frame. The painted move is exactly one column per press,
// unconditionally: the whole-frame rounding error is bounded by
// 0.5 / (source frames per target pixel) of a pixel, and with the
// bracketed value vocabulary (tempo times both scales at least
// 0.25 * 0.5 * 0.5 = 1/16) at the 44100 sample-rate floor the deepest
// zoom gives at least 27.5625 / 16 = 1.72 source frames per target
// pixel, so the error is at most 0.29 px — and each press also always
// advances at least one whole frame. Rounding each press's step
// independently would NOT give this guarantee (whole-frame residue on
// top of an off-grid sub-pixel phase paints occasional 0 or 2 px jumps);
// anchoring to the column grid re-derives the pixel phase every press,
// so nothing accumulates.
//
// Wall semantics per view are unchanged: target view keeps the
// all-or-nothing silent refusal when a proposal leaves the absolute
// range (zero / the warp EOF wall — total_frames minus one source frame,
// because build_warp_frame_map refuses sub-frame segments); source view
// keeps the clamp (creep-to-the-wall), so the walls stay exactly
// reachable by nudge — the walls win over the pixel grid, and every wall
// is an integer frame, so a wall-clamped commit stays whole. Spacing is
// not the GUI's concern: crossing a neighbor is legal and goes through
// the reorder-and-remap path below; the render boundary refuses ordering
// degeneracy.
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
    if (current_samples_per_pixel(app, audio) <= 0.0) return;

    const auto& mv = app.warpmarkers.markers();
    // Stale-index check only: every marker is nudgeable, including the
    // one at time 0 (the first-marker grammar is the render boundary's
    // rule, not a gesture pin).
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(mv.size())) return;
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
    const int64_t warp_wall = audio.total_frames() - 1;
    std::vector<std::pair<int, int64_t>> proposals;
    proposals.reserve(app.selected_markers.size());
    for (int idx : app.selected_markers) {
        const int c = painted_column_of_source_frame(
            app, audio, static_cast<double>(mv[idx].time_frame), map);
        int64_t t_new =
            authored_frame_at_column(app, audio, c + direction, map);
        if (target_view) {
            // All-or-nothing silent refusal outside the absolute range.
            if (t_new < 0 || t_new > warp_wall) return;
        } else {
            if (t_new < 0)         t_new = 0;
            if (t_new > warp_wall) t_new = warp_wall;
        }
        proposals.emplace_back(idx, t_new);
    }
    bool any_changed = false;
    std::vector<GuiWarpMarker> proposed = mv;
    for (const auto& [idx, t_new] : proposals) {
        if (t_new == mv[idx].time_frame) continue;
        proposed[idx].time_frame = t_new;
        any_changed = true;
    }
    if (!any_changed) return;
    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    const int              hint_last = app.last_selected_marker;
    app.warpmarkers.markers_mut() = std::move(proposed);
    // A nudge may cross a neighbor; restore time order and re-point
    // the selection at the moved marker before the playhead sync
    // reads last_selected below.
    remap_marker_indices_after_reorder(
        app, reorder_markers_by_time(app.warpmarkers.markers_mut()));
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
}
