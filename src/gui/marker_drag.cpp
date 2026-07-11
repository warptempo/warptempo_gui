#include "marker_drag.h"

#include "audio.h"
#include "warp_frame_map.h"
#include "warp_frame_map_view.h"
#include "phaseresetmarkers.h"
#include "render.h"
#include "target_render.h"
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

    auto t_of = [&](int idx) -> int64_t {
        if (phase_reset) {
            return app.phaseresetmarkers.markers()[idx].time_frame;
        }
        return app.warpmarkers.markers()[idx].time_frame;
    };

    // Drag is a single-marker fine-tuning gesture: it always moves only the
    // grabbed marker, regardless of the current selection — each marker is
    // placed deliberately, and group drag does more harm than good. The
    // selection collapse to {hit} is deferred until motion is observed, so a
    // click without a drag leaves the selection untouched.
    std::set<int> drag_set;
    drag_set.insert(hit);

    // No first-marker pin, either column: every marker is draggable,
    // including a warp marker at time 0. The first-marker grammar
    // (enabled, tempo-owning numeric marker at exactly frame 0) is a
    // render-boundary rule — validate_first_marker_render_grammar refuses
    // it at render dispatch and at the target-view validity gate — and the
    // default marker created at source load is the recovery tool, so the
    // GUI never pins the gesture.

    DragState d;
    d.active = true;
    d.drag_mode = phase_reset ? 'P' : 'W';
    d.dragging_markers.assign(drag_set.begin(), drag_set.end());
    d.original_times.reserve(d.dragging_markers.size());
    for (int idx : d.dragging_markers) {
        d.original_times.push_back(t_of(idx));
    }

    // Anchor mouse position — computed at mouse_x in the waveform's X axis.
    // Target view: the press position is in target-domain frames; the
    // dragged markers' original_times are source-domain frames. Convert
    // the anchor to source-domain at the boundary so the on_motion delta
    // (mouse_frame - anchor_mouse_time_frame) lives in source frames.
    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    if (app.active_audio_view == 'T') {
        const int64_t anchor_frame_active =
            app.viewport_start_sample +
            static_cast<int64_t>(std::nearbyint(
                static_cast<double>(mouse_x - area.x) * spp));
        const int64_t anchor_frame_src =
            active_domain_to_source_frame(app, audio, anchor_frame_active);
        d.anchor_mouse_time_frame = static_cast<double>(anchor_frame_src);
    } else {
        d.anchor_mouse_time_frame =
            static_cast<double>(app.viewport_start_sample) +
            static_cast<double>(mouse_x - area.x) * spp;
    }

    // Compute scalar delta_min / delta_max from the absolute range only:
    // zero on the left and the marker EOF wall on the right — total_frames
    // minus one source frame for BOTH columns (the per-column split, warp
    // total-1 vs phase reset total, is retired: warp is structural,
    // build_warp_frame_map refuses sub-frame segments; phase reset walls at
    // total-1 by ruling — a reset in the last source frame has nothing left
    // to re-ground, and total-1 keeps every marker inside the playhead's
    // [0, total-1] domain). Exact frame compares — the same comparison the
    // load guard applies. Neighbors do not bound the drag; the marker may
    // cross them freely, and commit_drag reorders the store and remaps the
    // held indices.
    const double total = static_cast<double>(audio.total_frames());
    const double eof_wall = total - 1.0;

    d.delta_min = -std::numeric_limits<double>::infinity();
    d.delta_max =  std::numeric_limits<double>::infinity();

    for (size_t k = 0; k < d.dragging_markers.size(); ++k) {
        const double orig_t = static_cast<double>(d.original_times[k]);
        const double lb = 0.0 - orig_t;
        if (lb > d.delta_min) d.delta_min = lb;
        const double ub = eof_wall - orig_t;
        if (ub < d.delta_max) d.delta_max = ub;
    }

    // Viewport clamp: keep the grabbed marker within the visible strip so a
    // mouse drag can't push it offscreen, where its precise position would be
    // hidden. Only the grabbed marker (hit) is clamped; co-dragged markers
    // ride the same scalar delta and stay bound by the data range alone — the
    // same way Ctrl+Shift+drag clamps the grabbed bound but lets its partner
    // run to the clip edge. viewport_marker_bounds is active-domain while the
    // delta lives in source frames, so inverse-translate the edges; the
    // domain map is monotonic, so the source clamp matches the active-pixel
    // clamp. The grabbed marker is on-screen or within the kMarkerHitHalfPx
    // halo (up to a few pixels past an edge) at grab: when fully on-screen
    // these bounds bracket delta = 0; when grabbed just offscreen through the
    // halo they both sit on one side of 0, so the first motion snaps the item
    // into the visible strip — that is what makes a blind offscreen-halo grab
    // self-correcting. vp_lo_src <= vp_hi_src always, so [delta_min, delta_max]
    // does not invert from this pair.
    {
        const auto vb = viewport_marker_bounds(app, audio);
        const double vp_lo_src = static_cast<double>(
            active_domain_to_source_frame(app, audio, vb.first));
        const double vp_hi_src = static_cast<double>(
            active_domain_to_source_frame(app, audio, vb.second));
        const double orig_grabbed = static_cast<double>(t_of(hit));
        const double vp_lb = vp_lo_src - orig_grabbed;
        const double vp_ub = vp_hi_src - orig_grabbed;
        if (vp_lb > d.delta_min) d.delta_min = vp_lb;
        if (vp_ub < d.delta_max) d.delta_max = vp_ub;
    }

    d.moved = false;
    // Seed moveable_times from original_times (int64 frames widening into
    // the free fractional mid-gesture domain). apply_drag_motion writes
    // moveable_times[k] = original_times[k] + delta on every motion event.
    d.moveable_times.assign(d.original_times.begin(), d.original_times.end());
    // Capture a snapshot of the pre-drag warp_frame_map so paint can route
    // selected-marker positions and target-view waveform through a frozen
    // coordinate system. Both views capture: in source view the segment
    // list is harmless (its forward translation is identity at the
    // relevant input boundaries) and the symmetry lets paint code route
    // through DragOverlay unconditionally. Build may fail (returns empty)
    // — in that case paint walks the identity fallback for the duration
    // of the drag, which is the correct degradation.
    d.frozen_warp_frame_map = build_target_view_warp_frame_map(
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
    d.pre_drag_playhead_sample = app.playhead_cursor_sample;
    app.drag = std::move(d);
    viewport.clear_hover_popup();
    return true;
}

// Apply a raw delta (mouse-derived) to the dragging markers, clamped.
// Writes proposed new times into app.drag.moveable_times — the live
// marker store is NOT mutated. Paint reads moveable_times through the
// DragOverlay so dragged markers paint at their proposed positions while
// the warp_frame_map stays frozen at its pre-drag snapshot. The live store is
// updated wholesale in commit_drag.
//
// Symmetric across warp and phase reset: both branches write the same
// statement into the same vector. The waveform cache stays valid
// throughout the drag — viewport / trim / dimensions / view-domain /
// frozen-warp_frame_map-hash don't change — so the invalidation triggers a
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
        // Full-precision frame double: mouse-derived fractional frames,
        // clamped to the walls above. Mid-gesture positions are free —
        // no grid, no snap — so the marker tracks the pointer exactly;
        // commit_drag snaps the release to its painted column's whole
        // frame.
        const double new_t = app.drag.original_times[k] + delta;
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
// only if the markers actually moved. Motion tracked the playhead onto
// the grabbed marker's proposed position; commit finishes that tracking
// by syncing the playhead onto the committed column-snapped position via
// sync_playhead_to_last_selected — the same call the Ctrl+Left/Right
// nudges end with — so drag and nudge, documented as keyboard/mouse
// counterparts, leave identical marker/playhead state (both columns,
// both views; the sync forward-translates through the live post-commit
// map in target view).
//
// Write-back step: the live store was untouched throughout motion (the
// proposed positions lived in app.drag.moveable_times and paint read
// them through the DragOverlay). On commit, walk dragging_markers and
// assign each marker's time_frame from the column-snapped committed
// times before pushing the pre-drag snapshot onto the undo stack.
// Symmetric across warp and phase reset: identical statement shape on
// each side.
void MarkerDragOps::commit_drag() {
    if (!app.drag.active) return;
    const bool phase_reset = (app.drag.drag_mode == 'P');
    // Commit-time column snap. Mid-gesture positions stay free and
    // fractional (apply_drag_motion); only the commit snaps. The released
    // position is snapped to the time of the pixel column it is PAINTED
    // at — computed against the drag's frozen map, the exact coordinate
    // system the overlay painted through — and that column time funnels
    // through snap_authored_frame (inside authored_frame_at_column), so
    // the stored value is the whole frame of the shown column: stored
    // equals shown, in both views at all zoom levels. With an
    // integer-pixel pointer the column snap is a no-op; with fractional
    // pointer coordinates (touchpads) it moves the value to the column
    // painting already shows. The walls win over the grid: the absolute
    // range (zero / the marker EOF wall, total - 1 for both columns — the
    // per-column split is retired, see begin_drag) is re-applied after the
    // snap, so a wall-clamped commit rests exactly on its wall (the walls
    // are integer frames, so the clamp preserves whole-frame values). The
    // visible-strip clamp composed into delta_min/delta_max during
    // motion, as before.
    const int64_t total    = audio.total_frames();
    const int64_t eof_wall = total - 1;
    std::vector<int64_t> committed;
    committed.reserve(app.drag.moveable_times.size());
    for (size_t k = 0; k < app.drag.moveable_times.size(); ++k) {
        const double proposed = app.drag.moveable_times[k];
        // Only positions the drag actually moved snap; an untouched
        // position keeps its stored value bit-exact, so a Ctrl+click
        // without motion (and a wander that returns exactly to its
        // origin) commits nothing, as before.
        if (k < app.drag.original_times.size() &&
            proposed == static_cast<double>(app.drag.original_times[k])) {
            committed.push_back(app.drag.original_times[k]);
            continue;
        }
        const int c = painted_column_of_source_frame(
            app, audio, proposed, app.drag.frozen_warp_frame_map);
        int64_t t = authored_frame_at_column(app, audio, c,
                                             app.drag.frozen_warp_frame_map);
        if (t < 0)        t = 0;
        if (t > eof_wall) t = eof_wall;
        committed.push_back(t);
    }
    // Commit gates on NET change, not on whether motion occurred.
    // app.drag.moved latches true on the first position change during
    // motion and never clears, so a drag that wanders and returns exactly
    // to its original position arrives here with moved == true but zero
    // net change. Pushing an undo entry then records a snapshot byte-equal
    // to the live store: a no-op history entry that both undo and redo
    // restore invisibly. committed and original_times are parallel int64
    // frames, so an exact integer compare across the dragged markers is the
    // true "did anything move" test. The compare runs on the SNAPPED
    // values: a sub-column wander commits nothing, while the whole-frame
    // store keeps
    // committed == original exactly when the marker returns to its column.
    bool net_changed = false;
    for (size_t k = 0; k < app.drag.dragging_markers.size(); ++k) {
        if (k >= committed.size()) continue;
        if (committed[k] != app.drag.original_times[k]) {
            net_changed = true;
            break;
        }
    }
    if (net_changed) {
        for (size_t k = 0; k < app.drag.dragging_markers.size(); ++k) {
            const int idx = app.drag.dragging_markers[k];
            if (k >= committed.size()) continue;
            const int64_t new_t = committed[k];
            if (phase_reset) {
                GuiPhaseResetMarker* m =
                    app.phaseresetmarkers.marker_mut(idx);
                if (!m) continue;
                m->time_frame = new_t;
            } else {
                GuiWarpMarker* m = app.warpmarkers.marker_mut(idx);
                if (!m) continue;
                m->time_frame = new_t;
            }
        }
        // The drag may have carried the marker across neighbors; restore
        // time order and remap the index-shaped state — the selection
        // (collapsed to the grabbed marker at first motion) follows the
        // marker to its new slot. The drag state's own held indices are
        // remapped too, though they are discarded by the wholesale reset
        // below.
        if (phase_reset) {
            remap_marker_indices_after_reorder(
                app,
                reorder_markers_by_time(app.phaseresetmarkers.markers_mut()));
        } else {
            remap_marker_indices_after_reorder(
                app, reorder_markers_by_time(app.warpmarkers.markers_mut()));
        }
    }
    std::vector<GuiWarpMarker>    snap_w =
        std::move(app.drag.pre_drag_snapshot);
    std::vector<GuiPhaseResetMarker> snap_t =
        std::move(app.drag.pre_drag_phase_reset_snapshot);
    const int                 hint_last = app.drag.pre_drag_last_selected;
    // Capture the moved flag before the wholesale reset: a drag that
    // engaged motion (even a wander back to the origin) tracked the
    // playhead onto the marker during motion and must finish that tracking
    // on release; a Ctrl+click without motion never engaged tracking and
    // must leave the playhead alone. This gate is moved, not net_changed —
    // a wander that returns to its origin still re-syncs (a no-op there,
    // since the playhead already rests on the marker's own frame).
    const bool                drag_moved = app.drag.moved;
    app.drag = DragState{};
    if (net_changed) {
        if (phase_reset) {
            undo.push_undo_phase_reset(std::move(snap_t), hint_last);
        } else {
            undo.push_undo_warp(std::move(snap_w), hint_last);
        }
        undo.recompute_dirty();
        viewport.invalidate_timestamp_area();
        // The flag pack/elision walks visual x order (overlay-effective
        // during a drag, live store at rest) — repaint the top strip so
        // the committed layout, computed from the reordered live store,
        // replaces the overlay-driven one from the last motion.
        viewport.invalidate_top_strip();
    }
    viewport.invalidate_waveform_area();
    // Finish the motion handler's playhead tracking: sync the playhead
    // onto the committed marker. The reorder/remap above kept
    // app.last_selected_marker on the grabbed marker's new slot, so this
    // reads the committed integer time_frame from the live store and, in
    // target view, forward-translates through the live (post-commit) map —
    // the playhead lands on the dragged marker itself, expressed through
    // the new map, subsuming the old source-anchor re-express. It runs
    // BEFORE kick_waveform_sync so the re-warped plate and the moved
    // playhead land in one frame. Pushes no history (like the nudge's
    // sync); undo's own restore re-syncs via its sync_playhead_to_last_selected.
    if (drag_moved) {
        selection.sync_playhead_to_last_selected(/*edge_follow=*/true);
    }
    // Same discrete-warp_frame_map-change class as drop_marker (see comment
    // there): the commit re-warps the plate, so render it synchronously
    // — re-warped waveform and the moved playhead land in one frame.
    if (net_changed && !phase_reset && app.active_audio_view == 'T')
        viewport.kick_waveform_sync();
    if (net_changed) target_render.trigger();
}
