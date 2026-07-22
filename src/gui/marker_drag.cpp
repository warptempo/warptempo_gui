#include "marker_drag.h"

#include "audio.h"
#include "gui_display_context.h"
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
    // grabbed marker, regardless of any prior multi-selection — each marker is
    // placed deliberately, and group drag does more harm than good. The arming
    // flag press already single-selected the grabbed marker (the click), so the
    // selection is {hit} before the drag begins; apply_drag_motion re-asserts
    // that on first motion so the "a real drag focuses the grabbed marker" rule
    // stays with the drag machinery.
    std::set<int> drag_set;
    drag_set.insert(hit);

    // No first-marker pin, either column: every marker is draggable,
    // including a warp marker at time 0. Whatever arrangement results, the
    // parser resolver normalizes it at render/preview time (ambiguity
    // resolves to tempo 1.00, one stderr line per timestamp), so the GUI
    // never pins the gesture.

    DragState d;
    d.active = true;
    d.drag_mode = phase_reset ? 'P' : 'W';
    d.dragging_markers.assign(drag_set.begin(), drag_set.end());
    d.original_times.reserve(d.dragging_markers.size());
    for (int idx : d.dragging_markers) {
        d.original_times.push_back(t_of(idx));
    }

    // Anchor mouse position — computed at mouse_x in the waveform's X axis,
    // as a plain ACTIVE-domain frame double (one expression, both views; no
    // inverse map). The on_motion delta (mouse_frame -
    // anchor_mouse_time_frame) therefore lives in active-domain frames, and
    // apply_drag_motion carries it into the source domain through the
    // DISPLAYED map's two hops, so the painted flag moves by exactly the
    // pointer's travel.
    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    d.anchor_mouse_time_frame =
        static_cast<double>(app.viewport_start_sample) +
        static_cast<double>(mouse_x - area.x) * spp;

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

    // Viewport clamp: hit is the sole dragged marker, so it alone is clamped
    // to the visible strip so a mouse drag can't push it offscreen, where its
    // precise position would be hidden — on top of the absolute data walls
    // (delta_min/delta_max computed just above). viewport_marker_bounds is
    // active-domain while the walls are source frames, so inverse-translate
    // the edges through the DISPLAYED map (the paint basis the drag's
    // mechanics run on; identity on source view's empty map); the map is
    // monotonic, so the source clamp matches the active-pixel clamp. The
    // grabbed marker is
    // on-screen at grab (the arming flag press hit hit_test_flag, which reports
    // only visible chips against the same displayed map, so the marker's
    // painted column is within the viewport), so
    // these bounds bracket the marker. vp_lo_src <= vp_hi_src always,
    // so [delta_min, delta_max] does not invert from this pair.
    {
        const auto vb = viewport_marker_bounds(app, audio);
        const std::vector<WarpFrameMapSegment>& vdmap =
            displayed_or_live_target_map(app, audio);
        const double vp_lo_src =
            map_target_to_source(static_cast<double>(vb.first), vdmap);
        const double vp_hi_src =
            map_target_to_source(static_cast<double>(vb.second), vdmap);
        const double orig_grabbed = static_cast<double>(t_of(hit));
        const double vp_lb = vp_lo_src - orig_grabbed;
        const double vp_ub = vp_hi_src - orig_grabbed;
        if (vp_lb > d.delta_min) d.delta_min = vp_lb;
        if (vp_ub < d.delta_max) d.delta_max = vp_ub;
    }

    d.moved = false;
    // Seed moveable_times from original_times (int64 frames widening into
    // the free fractional mid-gesture domain). apply_drag_motion writes the
    // displayed-map-anchored, wall-clamped proposal into moveable_times[k]
    // on every motion event (the formula at its header).
    d.moveable_times.assign(d.original_times.begin(), d.original_times.end());
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
    // Pre-drag marker selection snapshot for the Esc/Ctrl+Q cancellation
    // restore: the arming press single-selected the grabbed marker, so this
    // captures {hit}, and a cancel restores exactly that (Esc reverts the drag's
    // position change, not the click's committed selection).
    d.pre_drag_selection = capture_selection_snapshot(app);
    // Coincident-ride verdict, decided ONCE at grab against the grabbed
    // marker's original frame through the Tab placement basis
    // (source_frame_to_active_domain then clamp_playhead_to_live_domain), so
    // a Tab / `c` / alt+flag-click placement is coincident by construction:
    // an exactly-coincident playhead RIDES the marker through the drag (a
    // parked one keeps today's lead-in behavior — see the ruling at
    // DragState). The pre-ride capture feeds the Esc-cancel restore.
    d.playhead_rides =
        app.playhead_cursor_sample ==
        clamp_playhead_to_live_domain(
            source_frame_to_active_domain(app, audio, t_of(hit)), app, audio);
    d.pre_ride_playhead_sample = app.playhead_cursor_sample;
    app.drag = std::move(d);
    viewport.clear_hover_popup();
    return true;
}

// Apply a raw ACTIVE-domain frame delta (mouse-derived: the pointer's
// active-domain position minus the press anchor) to the dragging markers.
// The proposal anchors in the DISPLAYED target domain — dmap =
// displayed_or_live_target_map, the SAME map the DragOverlay flag painter
// walks (empty / identity in source view, where the formula degenerates to
// plain orig + delta):
//
//     proposed_src = map_target_to_source(
//                        map_source_to_target(orig_src, dmap) + delta, dmap)
//
// The painter then shows the flag at fwd(proposed_src) = fwd(orig) + delta —
// the original painted position plus exactly the pointer's travel — so the
// flag tracks the pointer 1:1 BY CONSTRUCTION, under any map, stale or
// fresh, at every song location (no live-map inverse, no slope-ratio slip
// when displayed != live or when the pointer's image and the marker sit in
// different segments). This is the one deliberate revision of the "gesture
// mechanics stay on the live map" rule: the drag's contract is
// pointer-lockstep with the painted flag, so its mechanics run on the
// DISPLAYED map (the paint basis); the commit's store write and the ride's
// final placement remain live-map (post-commit) territory. Walls stay
// integer source frames and win over everything: the proposed source value
// clamps per marker into [orig + delta_min, orig + delta_max].
//
// Writes proposed new times into app.drag.moveable_times — the live
// marker store is NOT mutated. Paint reads moveable_times through the
// DragOverlay so dragged markers paint at their proposed positions while the
// display warp_frame_map is read from the memoized display cache (no per-drag
// copy exists — the cache is stable for the drag's lifetime, the ruling at
// DragState). The live store is updated wholesale in commit_drag.
//
// Symmetric across warp and phase reset: both branches write the same
// statement into the same vector. The waveform cache stays valid
// throughout the drag — viewport / trim / dimensions / view-domain / the
// display warp_frame_map hash don't change — so the invalidation triggers a
// cheap blit of cached pixels with stems, flags, and playhead repainted
// on top. A narrow per-marker rect would be wrong in target view, where
// the dragged marker's proposed position lands at the cursor's pixel
// regardless of which markers surround it.
void MarkerDragOps::apply_drag_motion(double raw_delta) {
    if (!app.drag.active) return;
    // The displayed (paint-basis) map, hoisted out of the loop: one lookup
    // per motion event. Returns a reference to an existing vector, never
    // builds one.
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);

    bool any_changed = false;
    for (size_t k = 0; k < app.drag.dragging_markers.size(); ++k) {
        // Full-precision frame doubles throughout: mid-gesture positions
        // are free — no grid, no snap — so the marker tracks the pointer
        // exactly; commit_drag snaps the release to its painted column's
        // whole frame. Both hops are identity on source view's empty map
        // (proposed = orig + raw_delta there, bit-for-bit).
        const double orig = static_cast<double>(app.drag.original_times[k]);
        // Exact-zero short-circuit: at raw_delta == 0.0 the two-hop
        // inv(fwd(orig)) is NOT IEEE-bitwise orig at interior map points, so a
        // drag wandered exactly back to its press x would fail commit_drag's
        // bit-exact `proposed == original_times[k]` untouched compare and
        // column-snap — a real edit + undo entry from a no-op gesture. Returning
        // orig verbatim restores that untouched contract; a sub-column NON-zero
        // wander keeps the old two-hop value (which column-snaps to the origin's
        // column at commit anyway).
        const double proposed = (raw_delta == 0.0)
            ? orig
            : map_target_to_source(
                  map_source_to_target(orig, dmap) + raw_delta, dmap);
        // Per-marker wall clamp in the SOURCE domain (walls are integer
        // source frames — delta_min/delta_max fold the absolute data walls
        // with the grabbed marker's viewport clamp at begin_drag; the map is
        // monotone, so clamping the source proposal clamps its painted
        // position too).
        double new_t = proposed;
        if (new_t < orig + app.drag.delta_min) new_t = orig + app.drag.delta_min;
        if (new_t > orig + app.drag.delta_max) new_t = orig + app.drag.delta_max;
        if (k >= app.drag.moveable_times.size()) continue;
        if (app.drag.moveable_times[k] == new_t) continue;
        app.drag.moveable_times[k] = new_t;
        any_changed = true;
    }
    if (any_changed) {
        const bool first_motion = !app.drag.moved;
        app.drag.moved = true;
        // Selection focus on the press-to-motion edge: re-assert the single
        // selection on the grabbed marker. The arming flag press already
        // single-selected it, so this is normally a no-op; it lives here so the
        // "a real drag focuses the grabbed marker" rule stays with the drag
        // machinery regardless of how the drag was armed. Delegated to
        // Selection::set_single_selection — the same helper a marker click uses
        // — so the rule lives in one place.
        if (first_motion) {
            selection.set_single_selection(app.drag.hit_marker);
        }
        // Coincident ride, mid-motion: slide the resting cursor playhead to
        // the grabbed marker's live proposed position (the drag is
        // single-marker, so moveable_times[0] is the grabbed one). Target
        // view maps the free double through displayed_or_live_target_map —
        // the SAME basis the DragOverlay paints the flag through, so the
        // playhead tracks the flag in lockstep (mid-motion is paint
        // coherence; commit_drag's two-step placement is the truth).
        // A marker drag can never run under live playback — the arming
        // top-strip flag press stops playback — so the scanner is always
        // inactive here; move_playhead_to's scanner-inactive guard is a
        // belt-and-braces invariant, not a live-playback accommodation. The
        // motion-clamped proposal stays inside the visible strip, so no
        // viewport scroll occurs.
        if (app.drag.playhead_rides && !app.drag.moveable_times.empty()) {
            const double proposed = app.drag.moveable_times[0];
            int64_t sample;
            if (active_display_context(app, audio).domain !=
                GuiDisplayDomain::Source) {
                sample = static_cast<int64_t>(std::nearbyint(
                    map_source_to_target(
                        proposed, displayed_or_live_target_map(app, audio))));
            } else {
                sample = static_cast<int64_t>(std::nearbyint(proposed));
            }
            viewport.move_playhead_to(sample);
        }
        viewport.invalidate_waveform_area();
        viewport.invalidate_top_strip();
    }
}

// Commit the current drag. Caller ensures drag was active. Sets dirty
// only if the markers actually moved. Playhead rule (drag and nudge,
// keyboard/mouse counterparts, share it): a playhead parked OFF the grabbed
// marker stays parked — the marker slides under it and an upstream audition
// point survives the move — while a playhead EXACTLY on the grabbed marker
// at grab time (active-domain equality, the Tab placement basis; see
// begin_drag) RIDES it and lands with it here, so a later Space auditions
// FROM the marker. The ride only keeps the playhead on a marker it already
// sat on; the land routes remain the Tab family, `c`, and the alt-exact
// flag click.
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
    // at — computed against the DISPLAYED map, the exact coordinate system
    // the overlay painted through — and
    // that column time funnels
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
    // The map the overlay painted through: displayed_or_live_target_map —
    // the DISPLAYED map (converged == live at rest; empty / identity in
    // source view), the same basis apply_drag_motion anchored the proposals
    // in, so stored-equals-shown holds at commit even inside a worker
    // publish window where displayed != live. The commit-time RIDE placement
    // below stays on the LIVE map deliberately — placement truth after the
    // re-warp, the Tab basis.
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    for (size_t k = 0; k < app.drag.moveable_times.size(); ++k) {
        const double proposed = app.drag.moveable_times[k];
        // Only positions the drag actually moved snap; an untouched
        // position keeps its stored value bit-exact, so a flag click
        // without motion (and a wander that returns exactly to its
        // origin) commits nothing.
        if (k < app.drag.original_times.size() &&
            proposed == static_cast<double>(app.drag.original_times[k])) {
            committed.push_back(app.drag.original_times[k]);
            continue;
        }
        const int c = painted_column_of_source_frame(
            app, audio, proposed, dmap);
        int64_t t = authored_frame_at_column(app, audio, c, dmap);
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
    // Capture the ride verdict and the grabbed marker's committed frame into
    // locals BEFORE the wholesale DragState reset below discards them. The
    // ride lands regardless of net_changed: a wander-back drag must land the
    // playhead exactly back on the marker (original_times[0]), erasing any
    // mid-motion rounding drift from apply_drag_motion's paint-basis moves.
    const bool playhead_rides = app.drag.playhead_rides &&
        (net_changed ? !committed.empty() : !app.drag.original_times.empty());
    const int64_t ridden_final_frame = !playhead_rides ? 0
        : (net_changed ? committed[0] : app.drag.original_times[0]);
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
    // Coincident ride, commit half (see the header comment): land the ridden
    // playhead on the committed frame through the two-step placement basis.
    // The call runs AFTER the store write + reorder/remap and BEFORE the
    // kick_waveform_sync below, so source_frame_to_active_domain reads the
    // POST-commit map via the generation-keyed display cache and, in target
    // view, the re-warped plate and the ridden playhead land in one frame. A
    // non-riding drag leaves the playhead parked exactly as before.
    if (playhead_rides) {
        viewport.move_playhead_to(
            source_frame_to_active_domain(app, audio, ridden_final_frame));
    }
    // Same discrete-warp_frame_map-change class as drop_marker (see comment
    // there): the commit re-warps the plate, so render it synchronously
    // — the re-warped waveform and the playhead land in one frame.
    if (net_changed && !phase_reset && app.active_audio_view == 'T')
        viewport.kick_waveform_sync();
    if (net_changed) target_render.trigger();
}
