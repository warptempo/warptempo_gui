#include "marker_drag.h"

#include "audio.h"
#include "gui_display_context.h"
#include "input_handler.h"       // set_region_to_selection_extent (the group tempo
                                 // gestures' extent follow)
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
#include <set>
#include <string>
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

    // ONE MARKER, ALWAYS — GROUPS ARE NEVER MOVED (architect 2026-07-29,
    // HORIZONTAL MOVEMENT IS A FOCUS ACT; the doctrine and the whole dead rigid
    // group-drag machinery are recorded at the head of group_position_nudge.h). The
    // arming press single-selected `hit` and landed the playhead on it — no press
    // defers its click any more — so the drag subject is exactly the grabbed
    // marker. The one-element vectors below are the DragOverlay's storage contract
    // (paint matches a store index against dragging_markers), not a group: slot 0
    // is the dragged marker everywhere in this file.

    // No first-marker pin, either column: every marker is draggable,
    // including a warp marker at time 0. Whatever arrangement results, the
    // parser resolver normalizes it at render/preview time (ambiguity
    // resolves to tempo 1.00, one stderr line per timestamp), so the GUI
    // never pins the gesture.

    DragState d;
    d.active = true;
    d.drag_mode = phase_reset ? 'P' : 'W';
    d.dragging_markers.assign(1, hit);
    d.original_times.assign(1, t_of(hit));

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

    // Compute scalar delta_min / delta_max as the dragged marker's ACTIVE-domain
    // wall headroom (architect 2026-07-23, retiring the earlier source-domain
    // bounds and their target-view squash).
    // The absolute source walls are zero on the left and the marker EOF wall on
    // the right — total_frames minus one source frame for BOTH columns (the
    // per-column split, warp total-1 vs phase reset total, is retired: warp is
    // structural, build_warp_frame_map refuses sub-frame segments; phase reset
    // walls at total-1 by ruling — a reset in the last source frame has nothing
    // left to re-ground, and total-1 keeps every marker inside the playhead's
    // [0, total-1] domain). Map both walls through the SAME displayed map the
    // drag's mechanics run on (fwd = map_source_to_target; identity in source
    // view), giving [fwd(0) − fwd(orig), fwd(eof_wall) − fwd(orig)] in the ACTIVE
    // (pointer-delta) domain: apply_drag_motion clamps the POINTER delta once
    // against it, so the marker's image stops at its wall in either view. Source
    // view is the identity special case (fwd(x) == x), so the active bounds equal
    // the source bounds. Neighbors do not bound the drag; the marker may cross them
    // freely, and commit_drag reorders the store and remaps the held index.
    const double total = static_cast<double>(audio.total_frames());
    const double eof_wall = total - 1.0;
    const std::vector<WarpFrameMapSegment>& vdmap =
        displayed_or_live_target_map(app, audio);
    const double fwd_orig =
        map_source_to_target(static_cast<double>(d.original_times[0]), vdmap);
    d.delta_min = map_source_to_target(0.0, vdmap)      - fwd_orig;
    d.delta_max = map_source_to_target(eof_wall, vdmap) - fwd_orig;

    // Viewport clamp on top of the absolute data walls above, so a mouse drag
    // can't push the marker FURTHER offscreen, where its precise position — the
    // one being authored — would be hidden. Same active domain:
    // viewport_marker_bounds already returns active-domain values,
    // so its edges fold in directly as [vb.first − fwd(orig), vb.second −
    // fwd(orig)] — no inverse translation to source (that dance existed
    // only because the walls were source-domain).
    //
    // WIDEN the pair to include ZERO. The visible-flag iteration keeps flag
    // CENTERS up to half a flag width past either edge (a half-offscreen flag is
    // still hit-testable on its visible part), so a legal grab can have its center
    // OUTSIDE [vb.first, vb.second] — the naive pair would then exclude 0 and the
    // first nonzero motion would jump the marker INWARD against
    // the pointer. The viewport clamp only exists to stop the drag pushing the
    // marker FURTHER out; a marker grabbed half-offscreen may rest where it is and
    // move inward, never further out. So min the lower headroom with 0 and max the
    // upper with 0: the interval always contains 0, the no-motion identity
    // clamp(0) == 0 holds on every reachable grab, and a centered grab (0 already
    // inside) is unaffected.
    {
        const auto vb = viewport_marker_bounds(app, audio);
        double vp_lb = static_cast<double>(vb.first)  - fwd_orig;
        double vp_ub = static_cast<double>(vb.second) - fwd_orig;
        if (vp_lb > 0.0) vp_lb = 0.0;
        if (vp_ub < 0.0) vp_ub = 0.0;
        if (vp_lb > d.delta_min) d.delta_min = vp_lb;
        if (vp_ub < d.delta_max) d.delta_max = vp_ub;
    }

    d.moved = false;
    // Seed moveable_times from original_times (int64 frames widening into
    // the free fractional mid-gesture domain). apply_drag_motion writes the
    // displayed-map-anchored, wall-clamped proposal into moveable_times[0]
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
    d.hit_marker             = hit;
    // NO PRE-GESTURE CAPTURES HERE (all three deleted 2026-07-29): the selection
    // snapshot, the grab-playhead sample and the pre-drag region existed only for
    // an Esc/Ctrl+Q cancel, and POINTER GESTURES HAVE NO CANCEL — the rule and its
    // reasoning are at the drag-modal gate (input_handler.cpp's on_key). The drag
    // still captures the pre-drag STORE above, but that is the undo payload
    // commit_drag pushes, not cancel machinery: undo is what takes a committed drag
    // back. Playhead-follows-marker is unchanged as a live mechanic
    // (apply_drag_motion's follow and commit_drag's land, the ruling at DragState).
    app.drag = std::move(d);
    // Selection re-assert at the THRESHOLD CROSSING, unconditionally (was: the
    // first MOVED motion in apply_drag_motion). It must run after
    // app.drag = std::move(d) so it mutates the
    // live selection, not the moved-from local — the pre-capture ordering rule it
    // also used to carry died with the captures. A NO-OP today by construction: the
    // arming press already single-selected `hit` and landed the playhead on it (no
    // press defers its click since 2026-07-29 — groups are never moved). It stays as
    // the one site that states "a real drag's subject is what it grabbed", and
    // staying at the crossing rather than behind any_changed is what keeps a
    // WALL-SATURATED drag (the clamped delta pins the proposal — the marker at the
    // EOF wall, or the viewport clamp saturated) honest, since commit_drag lands the
    // playhead on the dragged marker regardless of net change.
    selection.set_single_selection(hit);
    viewport.clear_hover_popup();
    return true;
}

// Apply a raw ACTIVE-domain frame delta (mouse-derived: the pointer's
// active-domain position minus the press anchor) to the dragged marker.
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
// DISPLAYED map (the paint basis); the commit's store write and the playhead's
// final placement remain live-map (post-commit) territory. The live consumer
// of the non-identity case is the PHASE reset drag in target view: under the
// home-view binding (architect 2026-07-22) a WARP marker drag authors in
// source view, where the displayed map is identity and both hops degenerate
// to orig + delta, while a phase reset drag authors in target view against a
// real (non-identity) displayed map — where a live-map inverse would slip at
// the slope ratio, and the displayed basis does not. Walls win as an
// active-domain clamp: the POINTER delta is clamped into the marker's own
// headroom [delta_min, delta_max] (begin_drag), so its image stops at its wall in
// either view, plus a plain absolute [0, eof_wall] source-frame backstop for
// fp-safety.
//
// Writes the proposed new time into app.drag.moveable_times[0] — the live
// marker store is NOT mutated. Paint reads moveable_times through the
// DragOverlay so the dragged marker paints at its proposed position while the
// display warp_frame_map is read from the memoized display cache (no per-drag
// copy exists — the cache is stable for the drag's lifetime, the ruling at
// DragState). The live store is updated in commit_drag.
//
// Symmetric across warp and phase reset: both columns write the same
// statement into the same vector. The waveform cache stays valid
// throughout the drag — viewport / trim / dimensions / view-domain / the
// display warp_frame_map hash don't change — so the invalidation triggers a
// cheap blit of cached pixels with stems, flags, and playhead repainted
// on top. A narrow per-marker rect would be wrong in target view, where
// the dragged marker's proposed position lands at the cursor's pixel
// regardless of which markers surround it.
void MarkerDragOps::apply_drag_motion(double raw_delta) {
    if (!app.drag.active) return;
    if (app.drag.moveable_times.empty() ||
        app.drag.original_times.empty()) return;   // degenerate drag (defensive)
    // The displayed (paint-basis) map: one lookup per motion event. Returns a
    // reference to an existing vector, never builds one.
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);

    // Clamp the POINTER delta against the marker's own ACTIVE-domain wall headroom
    // (begin_drag), so its image stops at its wall in either view.
    const double eof_wall = static_cast<double>(audio.total_frames()) - 1.0;
    double clamped = raw_delta;
    if (clamped < app.drag.delta_min) clamped = app.drag.delta_min;
    if (clamped > app.drag.delta_max) clamped = app.drag.delta_max;

    // Full-precision frame doubles throughout: mid-gesture positions
    // are free — no grid, no snap — so the marker tracks the pointer
    // exactly. At commit the proposal snaps to its painted column's whole frame,
    // so this is a paint value, not the committed position. Both hops are identity
    // on source view's empty map (proposed = orig + clamped there, bit-for-bit).
    const double orig = static_cast<double>(app.drag.original_times[0]);
    // Zero-EFFECTIVE-motion short-circuit on the CLAMPED delta, not the raw
    // pointer delta: the two-hop inv(fwd(orig) + clamped) is NOT IEEE-bitwise
    // orig at interior map points even when clamped == 0, so a motion whose
    // pointer delta is nonzero but CLAMPED TO ZERO — the load-bearing case is
    // an OUTWARD drag of a half-offscreen flag, whose contains-zero viewport
    // bound clamps it to 0 — would otherwise write sub-frame residue into
    // moveable_times, miss commit's bit-exact untouched branch, and column-snap
    // a marker that had NO permitted motion (a visually motionless, forbidden
    // drag committing an inward jump). Testing clamped == 0.0 returns orig
    // verbatim ALWAYS. The downstream no-commit chain (no change -> moved never
    // latches -> commit's untouched short-circuit -> the original stands) holds for
    // a drag whose EVERY event was zero-clamped. A drag that moved inward first
    // and then wandered back to a zero-clamped position is different: the
    // return-to-original event correctly latches moved (so the repaint is correct),
    // and it still commits nothing — commit's bit-exact `proposed == original`
    // untouched branch and the net_changed gate see the restored original. So
    // verbatim-on-return is right either way. It subsumes the raw_delta == 0 case
    // (the bounds contain 0, so clamp(0) == 0).
    const double proposed = (clamped == 0.0)
        ? orig
        : map_target_to_source(
              map_source_to_target(orig, dmap) + clamped, dmap);
    // ABSOLUTE source-domain wall backstop, [0, eof_wall] as plain doubles. With
    // the pointer delta already clamped in the active domain upstream this is
    // fp-safety only, so no proposal can rest outside the authored domain; it
    // should never engage beyond fp dust exactly at a wall (where
    // fwd(orig)+clamped == fwd(wall) round-trips to the wall within rounding).
    double new_t = proposed;
    if (new_t < 0.0)      new_t = 0.0;
    if (new_t > eof_wall) new_t = eof_wall;
    if (app.drag.moveable_times[0] == new_t) return;
    app.drag.moveable_times[0] = new_t;
    app.drag.moved = true;
    // The selection re-assert does not live here: it runs at the THRESHOLD
    // CROSSING in begin_drag, unconditionally. A wall-saturated drag (the clamped
    // delta pins the proposal — the marker at the EOF wall) never reaches this
    // point, yet it is a real drag that commit lands the playhead for.
    // Playhead follows the marker, mid-motion: slide the resting cursor
    // playhead to the dragged marker's live proposed position. Target
    // view maps the free double through displayed_or_live_target_map —
    // the SAME basis the DragOverlay paints the flag through, so the
    // playhead tracks the flag in lockstep (mid-motion is paint
    // coherence; commit_drag's two-step placement is the truth).
    // A marker drag can never run under live playback — the arming
    // top-strip flag press stops playback — so the scanner is always
    // inactive here; move_playhead_to only ever writes the cursor
    // field regardless, so this call could not disturb a running
    // scanner even if one existed. The motion-clamped proposal stays
    // inside the visible strip, so no viewport scroll occurs.
    const bool target_domain = active_display_context(app, audio).domain !=
        GuiDisplayDomain::Source;
    int64_t sample;
    if (target_domain) {
        sample = static_cast<int64_t>(std::nearbyint(
            map_source_to_target(new_t, dmap)));
    } else {
        sample = static_cast<int64_t>(std::nearbyint(new_t));
    }
    viewport.move_playhead_to(sample);
    // NO REGION WORK, and none is reachable: the arming press single-selected the
    // marker (a membership replace, which takes any SelectionExtent span) and then
    // cleared any other resting span at its own site, so a marker drag runs with no
    // region at all. The group live-track that used to re-derive an extent span per
    // motion event died with the group drag (architect 2026-07-29 — groups are
    // never moved; the doctrine is at the head of group_position_nudge.h).
    viewport.invalidate_waveform_area();
    viewport.invalidate_top_strip();
}

// Commit the current drag. Caller ensures drag was active. Sets dirty
// only if the marker actually moved. Playhead rule (drag and nudge,
// keyboard/mouse counterparts, share it): the playhead follows the dragged
// marker UNCONDITIONALLY (architect 2026-07-23, reversing the 2026-07-20
// decoupling). The arming click already landed the playhead on it and the
// mid-motion follow slid it along, and this land runs regardless of net change (a
// wall-saturated drag never moved it), so the playhead lands with the marker here,
// matching the selection, and a later Space auditions FROM it. Every marker click
// is a land route; Tab and `c` additionally recenter / re-zoom. The lead-in
// workflow (parking the playhead upstream) is supplied by the scrub surface
// instead.
//
// Write-back step: the live store was untouched throughout motion (the
// proposed position lived in app.drag.moveable_times and paint read
// it through the DragOverlay). On commit the proposal column-snaps
// (pixel-anchored) and is assigned to the marker's time_frame before pushing the
// pre-drag snapshot onto the undo stack. Symmetric across warp and phase reset:
// identical statement shape on each side.
void MarkerDragOps::commit_drag() {
    if (!app.drag.active) return;
    const bool phase_reset = (app.drag.drag_mode == 'P');
    // ONE MARKER, PIXEL-ANCHORED: the proposal snaps to its painted column, so
    // stored equals shown for the pointer-authored flag. The rigid GROUP commit
    // this replaced — the grabbed member's snap folded into a uniform
    // active-domain delta D that every other member rode, with D re-clamped into
    // the group's wall intersection and the grabbed member re-derived from it —
    // died with the group drag (architect 2026-07-29, HORIZONTAL MOVEMENT IS A
    // FOCUS ACT; the doctrine and the full delete list are at the head of
    // group_position_nudge.h). Nothing here needs the walls-win D clamp any more:
    // the snap's only overshoot risk was carrying OTHER members past the wall the
    // group stopped at, and a lone marker's snap is bounded by the integer walls
    // below (which is exactly what a single-marker drag committed before the group
    // arc, bit-for-bit).
    //
    // dmap = displayed_or_live_target_map — the DISPLAYED map (converged == live
    // at rest; empty / identity in source view), the same basis apply_drag_motion
    // anchored the proposal in and the overlay painted through, so
    // stored-equals-shown holds even inside a worker publish window where
    // displayed != live. The commit-time land below stays on the LIVE
    // map deliberately — post-commit placement truth, the Tab basis.
    const int64_t total    = audio.total_frames();
    const int64_t eof_wall = total - 1;
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    // Slot 0 is the dragged marker (begin_drag seeds exactly one). The guard
    // covers a degenerate drag with no moveable marker.
    const bool slot_valid =
        !app.drag.dragging_markers.empty() &&
        !app.drag.moveable_times.empty() &&
        !app.drag.original_times.empty();

    // The commit: bit-exact untouched short-circuit (a wander returning exactly to
    // the press x keeps the original, dodging the two-hop's non-bitwise identity),
    // else the painted column snap through authored_frame_at_column (which funnels
    // the column time through snap_authored_frame, the ONE double-to-authored
    // route) against the displayed map, then the integer walls.
    int64_t committed = 0;
    int64_t original  = 0;
    if (slot_valid) {
        const double proposed = app.drag.moveable_times[0];
        original = app.drag.original_times[0];
        if (proposed == static_cast<double>(original)) {
            committed = original;
        } else {
            const int c = painted_column_of_source_frame(
                app, audio, proposed, dmap);
            int64_t t = authored_frame_at_column(app, audio, c, dmap);
            if (t < 0)        t = 0;
            if (t > eof_wall) t = eof_wall;
            committed = t;
        }
    }
    // Commit gates on NET change, not on whether motion occurred.
    // app.drag.moved latches true on the first position change during
    // motion and never clears, so a drag that wanders and returns exactly
    // to its original position arrives here with moved == true but zero
    // net change. Pushing an undo entry then records a snapshot byte-equal
    // to the live store: a no-op history entry that both undo and redo
    // restore invisibly. The compare runs on the SNAPPED value: a sub-column
    // wander commits nothing, while the whole-frame store keeps
    // committed == original exactly when the marker returns to its column.
    const bool net_changed = slot_valid && committed != original;
    // Identity hints for the post-restore selection, filled only on a real
    // (net_changed) drag. The undo diff matcher cannot recover the touched marker
    // when a column-snapped drag lands the row FIELD-IDENTICAL at another marker's
    // position, so name it explicitly: touched_snapshot = its index in the PRE-DRAG
    // snapshot (the pre-reorder store index, valid in the snapshot a restore
    // produces), touched_live = its POST-reorder index in the committed (after)
    // store. Singleton scope by construction now — groups are never moved.
    std::vector<int> touched_snapshot;
    std::vector<int> touched_live;
    if (net_changed) {
        const int idx = app.drag.dragging_markers[0];
        if (phase_reset) {
            if (GuiPhaseResetMarker* m = app.phaseresetmarkers.marker_mut(idx))
                m->time_frame = committed;
        } else {
            if (GuiWarpMarker* m = app.warpmarkers.marker_mut(idx))
                m->time_frame = committed;
        }
        // Pre-reorder: the store was untouched in order during motion and the
        // write above changed only a time_frame value, so dragging_markers still
        // holds the pre-drag store index — the pre-drag snapshot's coordinates.
        touched_snapshot = app.drag.dragging_markers;
        // The drag may have carried the marker across a neighbor; restore
        // time order and remap the index-shaped state — the selection follows the
        // marker to its new slot. The drag state's own held index is
        // remapped too, though it is discarded by the wholesale reset
        // below (see app_state.cpp).
        if (phase_reset) {
            remap_marker_indices_after_reorder(
                app,
                reorder_markers_by_time(app.phaseresetmarkers.markers_mut()));
        } else {
            remap_marker_indices_after_reorder(
                app, reorder_markers_by_time(app.warpmarkers.markers_mut()));
        }
        // Post-remap: remap_marker_indices_after_reorder rewrote dragging_markers
        // in place to the reordered (after) store's index.
        touched_live = app.drag.dragging_markers;
    }
    // Capture the marker's committed frame into a local BEFORE the
    // wholesale DragState reset below discards it. The land runs regardless of
    // net_changed: a wander-back drag must land the playhead exactly back on the
    // marker's original frame, erasing any mid-motion rounding drift from
    // apply_drag_motion's paint-basis moves.
    const bool land_playhead = slot_valid;
    const int64_t ridden_final_frame = !land_playhead ? 0
        : (net_changed ? committed : original);
    std::vector<GuiWarpMarker>    snap_w =
        std::move(app.drag.pre_drag_snapshot);
    std::vector<GuiPhaseResetMarker> snap_t =
        std::move(app.drag.pre_drag_phase_reset_snapshot);
    app.drag = DragState{};
    if (net_changed) {
        // The position-DRAG commit (both columns). A singleton restore's
        // always-on focus stem follows from the selection — no lateral bit.
        if (phase_reset) {
            undo.push_undo_phase_reset(std::move(snap_t),
                                       std::move(touched_snapshot),
                                       std::move(touched_live));
        } else {
            undo.push_undo_warp(std::move(snap_w),
                                /*affects_persistence=*/true,
                                std::move(touched_snapshot),
                                std::move(touched_live));
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
    // Playhead-follows-marker, commit half (see the header comment): land the
    // playhead on the committed frame through the two-step placement basis. The
    // call runs AFTER the store write + reorder/remap, so
    // source_frame_to_active_domain reads the POST-commit map via the
    // generation-keyed display cache — the shared Tab placement basis
    // (post-commit truth). A warp marker drag authors in the source home view
    // (home-view binding, architect 2026-07-22), where that call is identity, so
    // the playhead lands on the committed frame directly; a phase reset drag in
    // its target home maps through the post-commit map.
    if (land_playhead) {
        viewport.move_playhead_to(
            source_frame_to_active_domain(app, audio, ridden_final_frame));
    }
    // The selected-marker stem's move at commit is owned by the full-waveform
    // invalidate_waveform_area above: the drag shifts the selected marker's frame,
    // so its always-on focus stem repaints at the committed column.
    // NO REGION WORK, and none is reachable: the arming press single-selected the
    // marker and cleared any resting span, and nothing during the drag forms one.
    // The extent re-derive that used to snap a live-tracked group span back to its
    // resting extent here died with the group drag (architect 2026-07-29 — groups
    // are never moved; the doctrine is at the head of group_position_nudge.h).
    // No synchronous re-warp at commit: a marker drag can no longer change the
    // displayed target plate. Warp marker drags author in warp's SOURCE home
    // view only (the home-view binding, architect 2026-07-22 — the arming flag
    // press gates on active_column_authoring_allowed, and the view cannot toggle
    // mid-gesture since every key but the Ctrl+Q hatch is swallowed while a drag
    // is active), where the source waveform has no map-dependent plate; and a phase
    // reset drag never touches the warp map. So the only surviving effect is the
    // view-independent target preview trigger below.
    if (net_changed) target_render.trigger();
}

// -- Target-view tempo drag ----------------------------------------------
//
// The pointer half of the home-view binding's tempo exception (architect
// 2026-07-22): in W view + target view a plain horizontal flag drag on a
// marker whose predecessor owns its tempo stretches the preceding segment
// Ableton-style, by rewriting that PREDECESSOR's tempo_cents. The dragged
// marker's own payload is never touched.
//
// The forward relation, read off build_warp_frame_map (READ-ONLY, frozen):
// the segment marker P owns runs from P's frame to the NEXT marker's frame,
// and its target duration is
//
//     delta_tgt = delta_src / (effective_tempo(P) * settings_scale)
//               = delta_src / (tempo_from_cents(P.tempo_cents)
//                              * P.tempo_scale.value_or(1.0)
//                              * settings_scale)
//
// (pass 2's `target_frame = tgt_f_prev + (delta_src / divisor)` with
// `divisor = effective_tempo(m) * scale`). Tempo DIVIDES the source span, so
// FASTER IS SHORTER: raising P's tempo pulls the dragged marker's image
// left, lowering it pushes the image right.
//
// T(P)-constancy (holds UNLESS a label definition whose tempo materializes
// from P is cited by an earlier enabled reference — see
// tempo_drag_predecessor, which refuses that arm): P's own tempo cannot
// move P's own image. In pass 2, iteration i consumes markers[i]'s tempo to
// emit the anchor at markers[i+1]'s source frame — the anchor AT P's frame
// was emitted by the PRECEDING iteration from upstream tempos only (or is the
// {0,0} seed). Only P's tempo changes during this gesture (positions never
// move, no other value is written), so every upstream segment — and with it
// T(P) — is constant across the drag's steps. The one exception is the
// forward-label coupling: if a label DEFINITION whose tempo materializes from
// P (P's own, directly or through a chain of passes inheriting P) is cited by
// a surviving enabled reference EARLIER than P, pass 1's definition duration
// (derived from P's materialized tempo) feeds that earlier reference in pass
// 2, so writing P's tempo shifts the anchor at P's frame too — the eligibility
// guard excludes that arrangement so the solve here only ever runs with T(P)
// genuinely constant. T(P) is still
// RECOMPUTED per motion
// event from the live memoized map rather than cached as a double at grab:
// it is one binary search, and re-deriving keeps the solve immune to any
// upstream normalization surprise. L_src is likewise re-read from the live
// store per event (two int64 loads; the store's frames are structurally
// untouched by this gesture, but reading the truth is cheaper than proving a
// cached copy can never go stale).

int MarkerDragOps::tempo_drag_predecessor(int hit) const {
    const auto& mv = app.warpmarkers.markers();
    if (hit <= 0 || hit >= static_cast<int>(mv.size())) return -1;
    // Coincident groups act as ONE draggable item (architect 2026-07-22):
    // dragging ANY member of an exact-frame stack stretches against the
    // GROUP's predecessor — the nearest marker at a STRICTLY earlier frame,
    // found by walking backward past the equal-frame run of same-frame
    // siblings (the store is time-sorted at rest, ties legal, so the run is
    // adjacent). An exact-tie sibling is NEVER the predecessor, so every member
    // of a stack arms identically (before this walk only the first-in-store
    // member armed; the others died on the zero-span check against a sibling).
    // No strictly-earlier marker (the dragged marker sits at the store's
    // earliest frame) -> ineligible.
    int j = hit - 1;
    while (j >= 0 && mv[j].time_frame == mv[hit].time_frame) --j;
    if (j < 0) return -1;
    // The predecessor reads its own AUTHORED payload — the question is whether
    // it owns a rewritable tempo, which is payload, not the resolved projection
    // (the owner-only rule the target-view Up/Down tempo step already applies).
    const GuiWarpMarker& p = mv[j];
    if (p.disabled || p.tempo_inherits || !p.label_ref.empty()) return -1;
    // The source span mv[hit] - mv[j] is >= 1 BY CONSTRUCTION of the walk (j
    // sits at a strictly-earlier frame), so the old explicit zero-span
    // rejection is now structural — the degenerate solve can never arm, and an
    // exact-tie sibling is never reached as the predecessor. When same-frame
    // siblings sit AT the dragged marker's frame, the segment j -> that frame
    // is owned by j (the resolver collapses the stack to one 1.00 owner at that
    // frame), and the dragged marker's target image IS the collapse point's
    // image, so the solve's L_src / T(P) relation stays exact.
    //
    // A predecessor in a surviving coincident group owns no rendered segment:
    // the resolver collapses every exact-frame run of 2+ effectively-enabled
    // markers to ONE synthetic plain 1.00 owner, so rewriting the
    // predecessor's tempo_cents would be render-inert (an undo entry and dirty
    // state for zero image/audio motion). Reuse the normalization-red set as
    // the test: it reddens (a) label-ref fallbacks, (b) passes inheriting from
    // a ref, and (c) coincident-collapse members — but the payload checks
    // above have ALREADY rejected pass and ref predecessors, so for the
    // payload-OWNER predecessor here, red-set membership is EXACTLY the
    // coincident-collapse condition. UX-coherent: a flag the user sees painted
    // red never arms a tempo drag. Disabled-coincident stays armable — a
    // disabled sibling drops from the enabled count before the collapse, so an
    // enabled predecessor sharing a frame only with disabled markers keeps its
    // real segment and is not red.
    const std::set<int>& red = warp_red_flag_set_cached(
        app, audio.sample_rate(),
        static_cast<long>(audio.total_frames())).red;
    if (red.count(j)) return -1;
    // Forward-label coupling breaks T(P)-constancy: a definition whose tempo
    // MATERIALIZES from the predecessor P, consumed by an earlier enabled
    // reference, moves P's own image when P's tempo is written. In the builder,
    // pass 1 derives a definition's section target-duration from its
    // (materialized) tempo and pass 2 applies that duration to an earlier
    // forward-declared reference's section, so every anchor from that reference
    // forward — and with it the anchor AT P's frame, i.e. T(P) — shifts when
    // P's tempo is written. The absolute solve holds the OLD T(P) constant
    // while computing the candidate, then the write invalidates it, so the
    // pointer events oscillate the marker around the cursor instead of
    // converging. Refuse the arm — broken arrangements are fixed in warp view.
    //
    // A def materializes from P exactly when marker_effective's owner_idx — the
    // ref-opaque backward walk's own TERMINUS in raw coordinates — equals j.
    // This covers the direct case (P itself carries the def: owner_idx ==
    // idx == j) AND the pass-materialization case (a later pass, alone or
    // through a chain of passes, whose walk skips passes to land on P). The GUI
    // re-derives no walk of its own; owner_idx is the walk result. Cases that
    // do NOT couple fall out because owner_idx != j: a ref fallback or a
    // materialization from a synthetic prior (frame-0 seed, collapsed-group
    // owner) reports owner_idx -1 (its duration is the 1.00 fallback, not P's
    // rate); a def BEFORE P can only resolve to an owner before it. The
    // reference scan is bounded k < j, so a reference at or after the dragged
    // marker (or exactly at P's frame, where P is already a rejected stack)
    // never triggers a refusal — those defs stay draggable.
    //
    // "Effectively enabled" is judged on the store, which slightly OVER-refuses
    // (an earlier ref that would itself die inside a coincident collapse still
    // triggers the refusal): deliberate — an arm refusal is benign, and
    // mirroring the resolver's exact survivor semantics here would re-create
    // resolver logic GUI-side. Duplicate label definitions are load-fatal, so
    // a def's label_def names at most one definition and the string compare is
    // the whole match rule. The resolution slice is built ONCE per arm (the
    // slice/projection mechanics adjust_tempo_cents uses); the per-def cost is
    // arm-time-only and trivial on a store of dozens.
    const std::vector<WarpMarker> resolved = slice_to_warp_markers(mv);
    const long total = static_cast<long>(audio.total_frames());
    for (int d = 0; d < static_cast<int>(mv.size()); ++d) {
        if (mv[d].label_def.empty()) continue;
        if (marker_effective(resolved, d, total).owner_idx != j) continue;
        for (int k = 0; k < j; ++k) {
            if (!marker_effectively_disabled(mv, static_cast<size_t>(k)) &&
                mv[k].label_ref == mv[d].label_def) {
                return -1;
            }
        }
    }
    return j;
}

// Deduped participant-predecessor seeding (participants + walled verdict), the
// ONE owner shared by begin_tempo_drag and step_tempo_image (see the header).
MarkerDragOps::TempoGroupSeed
MarkerDragOps::seed_tempo_group_participants(int hit, int pred) const {
    const auto& mv = app.warpmarkers.markers();
    TempoGroupSeed seed;

    // Build the DEDUPED participant predecessor set over the WHOLE selection
    // (grabbed/focused included; a singleton selection degenerates to {pred}).
    // Two selected members of one coincident stack walk to the SAME predecessor,
    // and consecutive selected markers legitimately contribute each other — the
    // set dedupes both. An INELIGIBLE member (tempo_drag_predecessor < 0 — no
    // strictly-earlier marker, or a disabled/pass/ref/coincident-collapsed
    // predecessor, or forward-label coupling) WALLS the group at zero: for the
    // drag, `walled` pins the delta intersection to [0, 0] so motion produces
    // nothing — the wall hit before it starts (the keyboard step instead maps
    // the verdict to a whole-press refusal, the step-family convention). The
    // grabbed predecessor is inserted unconditionally, so the set is never
    // empty.
    std::set<int> preds;
    for (int m : app.selected_markers) {
        if (m < 0 || m >= static_cast<int>(mv.size())) continue;
        const int p = tempo_drag_predecessor(m);
        if (p < 0) { seed.walled = true; continue; }
        preds.insert(p);
    }
    preds.insert(pred);
    seed.participants.assign(preds.begin(), preds.end());

    // GROUP LABEL WALL — the multi-participant BISECTION's MONOTONICITY GUARANTEE
    // (not an optimization), the max-strict group extension of leg 6. The
    // bisection assumes T(d) is STRICTLY DECREASING in the group delta, but a
    // label REF cited BEFORE the grabbed marker whose definition materializes from
    // a PARTICIPANT breaks that: as d rises the ref's implied effective tempo can
    // leave the [kRefImpliedTempoMin/Max] envelope, the resolver snaps that ref to
    // a plain 1.00 owner, and its section abruptly LENGTHENS — a discontinuous
    // UPWARD jump in T(d). The endpoint-saturation/bisection logic then bounds
    // nothing (codex's legal case: def A@0 owner 1.00, ref A@1000 selected, owner
    // 1.00 @9000, grabbed @10000 selected; participants {0,9000}; an exact
    // interior d=-40 puts the image at 5000, but T(-75)=12000 and T(+300)=8500 so
    // the t_des<=T_hi saturation falsely commits +300 -> image 8500). Leg 6 misses
    // it: its ref scan is bounded to refs earlier than the PREDECESSOR (k < j), and
    // this ref sits later than the frame-0 predecessor but before the grabbed
    // marker. So WALL the whole group at zero when any such coupled ref exists —
    // the standing walls-at-zero convention (a hit-before-it-moves wall). With the
    // wall, every d-dependent term before the grabbed marker is a
    // participant-coupled hyperbola L / ((cents_p + d)*scale) (passes inheriting
    // from a participant ride it continuously; refs before the grabbed marker now
    // have d-INDEPENDENT definitions; the collapse/frame-0-seed normalizations are
    // positional and d-independent), so T(d) is strictly decreasing and the
    // saturation/bisection is sound. GROUP-ONLY: the single-participant path keeps
    // leg 6 alone, which suffices there — nothing sits strictly between
    // pred(grabbed) and the grabbed marker except same-frame siblings, whose
    // sections start AT the grabbed frame and cannot move T(grabbed).
    //
    // Frame-bound (not index-bound): a ref at a frame STRICTLY EARLIER than the
    // grabbed marker's frame can affect T(grabbed); one AT the grabbed frame
    // starts its section there and cannot. The store is time-sorted ascending, so
    // an index walk with the frame compare (break at >= grabbed_frame) is exact.
    // owner_idx is marker_effective's ref-opaque backward-walk terminus (the leg-6
    // machinery), so it covers a def carried directly by a participant AND one a
    // participant materializes through a pass chain; a synthetic-prior owner
    // reports -1 and is excluded. Effectively-enabled is judged on the store (the
    // benign over-refusal leg 6 accepts). The slice is built ONCE.
    if (seed.participants.size() >= 2 && !seed.walled) {
        const std::vector<WarpMarker> resolved = slice_to_warp_markers(mv);
        const long total = static_cast<long>(audio.total_frames());
        const int64_t grabbed_frame = mv[hit].time_frame;
        for (int def = 0;
             def < static_cast<int>(mv.size()) && !seed.walled; ++def) {
            if (mv[def].label_def.empty()) continue;
            const int owner = marker_effective(resolved, def, total).owner_idx;
            if (owner < 0 || !preds.count(owner)) continue;
            for (int k = 0; k < static_cast<int>(mv.size()); ++k) {
                if (mv[k].time_frame >= grabbed_frame) break;  // sorted ascending
                if (!marker_effectively_disabled(mv, static_cast<size_t>(k)) &&
                    mv[k].label_ref == mv[def].label_def) {
                    seed.walled = true;
                    break;
                }
            }
        }
    }
    return seed;
}

// Begin the tempo drag at the threshold crossing. Captures the grab tempo, the
// deduped participant predecessor set + their grab cents (the shared seeding
// helper above — begin consumes its verdict byte-identically, `walled` staying
// a drag concept), and the pre-drag store snapshot (the one undo entry's payload).
// That is the whole capture set: the selection snapshot and grab playhead went
// with the cancel (2026-07-29 — pointer gestures have no cancel).
// Returns false (gesture dropped) only on a defensive eligibility re-check
// failure of the GRABBED marker.
bool MarkerDragOps::begin_tempo_drag(int hit) {
    // The grabbed marker must be eligible (a defensive re-check — the arm already
    // ran the walk); -1 drops the gesture. Flowing the walked index keeps
    // predecessor == marker - 1 from being assumed anywhere downstream.
    const int pred = tempo_drag_predecessor(hit);
    if (pred < 0) return false;
    const auto& mv = app.warpmarkers.markers();

    TempoDragState d;
    d.active      = true;
    d.marker      = hit;
    d.predecessor = pred;

    TempoGroupSeed seed = seed_tempo_group_participants(hit, pred);
    d.walled                   = seed.walled;
    d.participant_predecessors = std::move(seed.participants);
    d.participant_grab_cents.reserve(d.participant_predecessors.size());
    for (int p : d.participant_predecessors)
        d.participant_grab_cents.push_back(mv[p].tempo_cents);

    // The pre-drag STORE, and nothing else: it is the undo payload end_tempo_drag
    // pushes on a net change, not cancel machinery. NO PRE-GESTURE CAPTURES (the
    // selection snapshot, the grab playhead and the pre-drag region all deleted
    // 2026-07-29 with the cancel itself — pointer gestures have no cancel; the rule
    // is at the drag-modal gate in input_handler.cpp). The participants' GRAB CENTS
    // above are NOT part of that deletion: they are live mechanics — the delta
    // lockstep reads them and end_tempo_drag's net-change test compares against
    // them — so an interrupted tempo drag keeps the cents it already wrote and
    // Ctrl+Z is what takes them back.
    d.pre_drag_snapshot      = mv;
    app.tempo_drag = std::move(d);
    // Focus transfer at the THRESHOLD CROSSING, the reposition drag's rule (a
    // real drag focuses the grabbed marker at the crossing, so a walled group
    // that never moves still focuses correctly, and there is exactly ONE
    // focus-transfer site). A singleton re-asserts the single selection (a no-op
    // — the arming press already single-selected it); a group focuses the grabbed
    // marker WITHOUT collapsing membership so the whole selection keeps riding.
    if (app.selected_markers.size() <= 1)
        selection.set_single_selection(hit);
    else
        selection.focus_without_collapse(hit);
    viewport.clear_hover_popup();
    return true;
}

// One motion event: invert the pointer's target-domain position to the GRABBED
// predecessor tempo that places the grabbed marker's image nearest it, producing
// a group cents DELTA, group-stop-clamp it into the intersection of every
// participant's bracket headroom, and — when the clamped delta is nonzero — add
// it to EVERY participant live with ONE synchronous re-warp (the tempo-step
// precedent: a committed value edit with displayed == live restored synchronously
// at the step boundary). Vertical motion is ignored; the solve is ABSOLUTE
// (pointer x -> tempo), so no press anchor exists and the threshold-crossing
// event needs no catch-up fold.
//
// TWO SOLVE PATHS by participant count:
//   - ONE participant (a singleton drag, or a group whose members all share the
//     grabbed predecessor): the CLOSED-FORM absolute solve, exact and bit-for-bit
//     the original. T(P_grabbed) is constant — the sole participant IS
//     pred(grabbed), whose own tempo shapes only the segment AFTER it (so it
//     cannot move its own image), and tempo_drag_predecessor's leg-6
//     forward-label-coupling refusal guarantees it feeds no earlier ref that
//     would. Invert delta_tgt = L_src / (tempo * s), quantize to cents, clamp the
//     delta into the intersection.
//   - MULTIPLE participants: the closed form is an UNSTABLE fixed-point iteration
//     here — a participant UPSTREAM of the grabbed marker moves T(P_grabbed) when
//     written, so re-solving event-by-event can diverge/thrash between the walls
//     (fixed-point derivative magnitude > 1). Solve EXACTLY by MONOTONE
//     BISECTION on the integer group delta d: the objective T(d) = the grabbed
//     marker's target image under the map rebuilt with every participant at
//     current + d is STRICTLY DECREASING in d — raising every participant's tempo
//     only shortens target durations, and the GROUP LABEL WALL in begin_tempo_drag
//     has already refused the one arrangement that would break this (a label ref
//     before the grabbed marker whose def materializes from a participant, whose
//     envelope normalization jumps T(d) upward). So T(d) crosses t_des at most
//     once and bisection over the integer intersection is exact. Each build is the
//     same resolve->build pipeline the live cache uses, over a COPY (the live
//     store is never touched); per event: 2 endpoint builds + up to ~10 midpoint
//     builds (log2 of the <=750-cent range), nothing re-evaluated — trivial.

// The solve CORE (factored from apply_tempo_drag_motion, byte-identical through
// the factor; step_tempo_image is the second caller). Computes the group-stop
// intersection from the participants' CURRENT stored cents + the walled pin,
// then runs the two-path solve above toward `t_des`. Returns false only on a
// hypothetical-build failure inside the bisection (unreachable by
// construction; the caller drops the event/press without committing).
bool MarkerDragOps::solve_tempo_group_delta(
    double t_des, int mi, int pi, const std::vector<int>& participants,
    bool walled, int64_t& out_delta) const {
    const int sr = audio.sample_rate();
    const auto& mv = app.warpmarkers.markers();

    // The grabbed marker's source frame — the objective's fixed evaluation point
    // (tempo-only, so it never moves through the gesture).
    const double grabbed_src = static_cast<double>(mv[mi].time_frame);

    // GROUP-STOP intersection [delta_lo, delta_hi]: the group cents delta must
    // keep EVERY participant inside the bracket, so it lives in the intersection
    // over participants of [kTempoMinCents - cents_p, kTempoMaxCents - cents_p],
    // from CURRENT stored cents (the drag commits as it goes, so current cents
    // track the live headroom). Every participant is currently in-bracket, so the
    // intersection always contains 0 (delta_lo <= 0 <= delta_hi) and is never
    // empty. A WALLED group (an ineligible member) pins it to [0, 0] — the wall
    // is already touching, so nothing moves. Init to the widest a legal cents pair
    // can produce (+/-(max-min)).
    int64_t delta_lo = kTempoMinCents - kTempoMaxCents;   // -(range)
    int64_t delta_hi = kTempoMaxCents - kTempoMinCents;   // +(range)
    for (int pp : participants) {
        if (pp < 0 || pp >= static_cast<int>(mv.size())) continue;
        const int64_t cp = mv[pp].tempo_cents;
        const int64_t lo_p = kTempoMinCents - cp;
        const int64_t hi_p = kTempoMaxCents - cp;
        if (lo_p > delta_lo) delta_lo = lo_p;
        if (hi_p < delta_hi) delta_hi = hi_p;
    }
    if (walled) { delta_lo = 0; delta_hi = 0; }

    int64_t clamped;
    if (participants.size() <= 1) {
        // SINGLE participant: the closed-form ABSOLUTE solve (see the header),
        // exact and bit-for-bit the original. T(P) from the live memoized map,
        // recomputed per event; live, not displayed: each step re-warps
        // synchronously, so displayed == live at every boundary and the live
        // cache IS the current paint basis (an empty-map cold fallback is
        // identity, matching paint's own).
        const TargetWarpFrameMapCache& c = target_view_warp_frame_map_cached(
            app, sr, static_cast<long>(audio.total_frames()));
        const double t_pred = map_source_to_target(
            static_cast<double>(mv[pi].time_frame), c.warp_frame_map);
        const double span  = t_des - t_pred;
        const double l_src = static_cast<double>(
            mv[mi].time_frame - mv[pi].time_frame);   // >= 1 by eligibility
        // The segment's scale composition exactly as the map build resolves it:
        // the settings engine scale times the predecessor's own typed scale
        // (effective_tempo's value_or(1.0) convention). Both factors are
        // bracket-positive, so the divisor below is positive whenever span is.
        const double s = app.engine_settings.scale *
                         mv[pi].tempo_scale.value_or(1.0);
        int64_t candidate;
        if (!(span > 0.0)) {
            // Pointer at or left of T(P): the solve degenerates — a positive
            // source span can only shrink toward zero as tempo grows (faster is
            // shorter), so the zero/negative-span limit pins to the MAX end.
            candidate = kTempoMaxCents;
        } else {
            // Invert delta_tgt = L_src / (tempo * s), then enter the integer-cents
            // domain the way the bpm derivation does (compute_base_tempo_scale):
            // nearbyint on the double cents count, bracket-clamped BEFORE the
            // int64 cast so an overflowing count (a denormal span) never reaches
            // the cast. Constructive clamp, not refusal.
            const double tempo   = l_src / (span * s);
            const double cents_d = std::nearbyint(tempo * 100.0);
            if (cents_d >= static_cast<double>(kTempoMaxCents)) {
                candidate = kTempoMaxCents;
            } else if (cents_d <= static_cast<double>(kTempoMinCents)) {
                candidate = kTempoMinCents;
            } else {
                candidate = static_cast<int64_t>(cents_d);
            }
        }
        clamped = candidate - mv[pi].tempo_cents;
        if (clamped < delta_lo) clamped = delta_lo;
        if (clamped > delta_hi) clamped = delta_hi;
    } else {
        // MULTIPLE participants: EXACT MONOTONE BISECTION (see the header). The
        // objective T(d) rebuilds the whole-song map from a COPY of the live
        // vector with every participant at current + d, through the same
        // resolve->build pipeline the live cache uses; the live store is never
        // mutated during evaluation. Returns false on a build error (UNREACHABLE
        // — positions unchanged, and every candidate d in the intersection keeps
        // cents bracket-clamped positive — but drop the event without committing
        // if it ever fires).
        auto eval_T = [&](int64_t d, double& out) -> bool {
            std::vector<GuiWarpMarker> copy = mv;
            for (int pp : participants) {
                if (pp < 0 || pp >= static_cast<int>(copy.size())) continue;
                copy[pp].tempo_cents = copy[pp].tempo_cents + d;
            }
            std::string err;
            // quiet=true: these are HYPOTHETICAL candidate builds (never-live
            // states probed by the bisection); their normalization lines must not
            // print — the resting store's lines would repeat ~12x per motion event
            // and envelope warnings would fire for probed candidates that never
            // commit. The live re-warp (kick_waveform_sync) stays loud.
            const std::vector<WarpFrameMapSegment> m =
                build_target_view_warp_frame_map(
                    copy, app.engine_settings.scale, sr,
                    static_cast<long>(audio.total_frames()), &err,
                    /*quiet=*/true);
            if (!err.empty()) return false;
            out = map_source_to_target(grabbed_src, m);
            return true;
        };
        // Degenerate range (walled, or a fully-pinned intersection — always 0
        // given the contains-zero property): no search, no builds.
        if (delta_lo >= delta_hi) {
            clamped = delta_lo;
        } else {
            // T is strictly decreasing: delta_lo (smallest d) gives the MAX image,
            // delta_hi (largest d) the MIN. Saturate constructively; else bisect,
            // CARRYING the endpoint values as the bracket narrows so the final
            // straddling pair needs no re-evaluation (2 endpoint builds + up to
            // ~10 midpoint builds — log2 of the <=750-cent range — nothing rebuilt).
            double T_lo, T_hi;
            if (!eval_T(delta_lo, T_lo) || !eval_T(delta_hi, T_hi)) return false;
            if (t_des >= T_lo) {
                clamped = delta_lo;      // want a larger image than achievable
            } else if (t_des <= T_hi) {
                clamped = delta_hi;      // want a smaller image than achievable
            } else {
                // Crossing bracketed: invariant T(lo) >= t_des >= T(hi), lo < hi,
                // with Tlo/Thi retained for both bounds.
                int64_t lo = delta_lo, hi = delta_hi;
                double  Tlo = T_lo, Thi = T_hi;
                while (hi - lo > 1) {
                    const int64_t mid = lo + (hi - lo) / 2;
                    double T_mid;
                    if (!eval_T(mid, T_mid)) return false;
                    if (T_mid >= t_des) { lo = mid; Tlo = T_mid; }
                    else                { hi = mid; Thi = T_mid; }
                }
                // Two straddling integers (values retained): pick the image
                // closer to the pointer.
                clamped = (std::abs(Tlo - t_des) <= std::abs(Thi - t_des))
                              ? lo : hi;
            }
        }
    }
    out_delta = clamped;
    return true;
}

void MarkerDragOps::apply_tempo_drag_motion(int mouse_x) {
    if (!app.tempo_drag.active) return;
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    const auto& mv = app.warpmarkers.markers();
    const int mi = app.tempo_drag.marker;
    const int pi = app.tempo_drag.predecessor;
    if (pi < 0 || mi <= pi || mi >= static_cast<int>(mv.size())) return;
    const GuiRect area = waveform_area(app);
    if (area.w <= 0) return;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;

    // Desired target position under the pointer, through the CURRENT
    // viewport (each step's sync re-warp reclamps zoom/viewport first, so
    // the live viewport is always the painted one). The gesture takes no
    // pointer capture; travel outside the window clamps to the visible
    // strip, like the reposition drag's live tracking clamp.
    int rel = mouse_x - area.x;
    if (rel < 0)       rel = 0;
    if (rel >= area.w) rel = area.w - 1;
    const double t_des = static_cast<double>(app.viewport_start_sample) +
                         static_cast<double>(rel) * spp;

    // The factored solve core (intersection + two-path solve — see
    // solve_tempo_group_delta above); a failed hypothetical build drops the
    // event without committing, exactly the pre-factor inline returns.
    int64_t clamped = 0;
    if (!solve_tempo_group_delta(t_des, mi, pi,
                                 app.tempo_drag.participant_predecessors,
                                 app.tempo_drag.walled, clamped)) {
        return;
    }

    // The both-unchanged skip, generalized: a clamped delta of zero commits
    // nothing — discrete cent-grid jumps (the Ableton snap), one sync render per
    // cent step. A walled group takes this every event.
    if (clamped == 0) return;

    // Apply the SAME clamped delta to EVERY participant (plain integer adds — the
    // structural producer discipline), so they move in cent lockstep. Tempo only:
    // no time change, so the store's ascending order is untouched and no
    // reorder/remap runs. Each participant's iteration bracket is left in place —
    // tempo changes never clear a bracket, the standing rule both tempo-authoring
    // surfaces follow, applied per member.
    for (int pp : app.tempo_drag.participant_predecessors) {
        if (pp < 0 || pp >= static_cast<int>(mv.size())) continue;
        GuiWarpMarker* p = app.warpmarkers.marker_mut(pp);
        if (!p) continue;
        p->tempo_cents = p->tempo_cents + clamped;
    }
    // The focus transfer already ran at the threshold crossing (begin_tempo_drag,
    // the reposition drag's rule), so there is no first-commit re-assert here.
    // NO REGION-PROVENANCE BRANCH and no pre-kick capture pair (both deleted
    // 2026-07-29): a tempo gesture needs a selection, every TrimWindow SETTER
    // clears it, and a Free region rests only beside an empty selection — so a
    // region resting through this drag is the selection's EXTENT by construction
    // (the rule at sync_region_to_trim_window's declaration, input_handler.h). The
    // extent re-derive below therefore runs UNCONDITIONALLY, which is what the
    // captured boolean amounted to: a 2+ selection always re-derived (the kick's
    // live-domain reclamp can wholesale-clear the region, and
    // set_region_to_selection_extent re-activates it), and a singleton drag's
    // re-derive is a no-op the owner itself returns on.
    // Synchronous re-warp, exactly adjust_tempo_cents' target-view tail:
    // kick_waveform_sync reclamps zoom/viewport first (a tempo change moves
    // the target total) and rebuilds plate + flag cache inline, with
    // full-width damage covering the top strip and waveform. Deliberately NO
    // target_render.trigger() here — the preview fires ONCE at gesture end;
    // per-cent triggers would kill/re-dispatch the render worker per step.
    viewport.kick_waveform_sync();
    // Playhead follows the marker: re-land the resting cursor playhead on the
    // marker's post-commit image (the live cache just rebuilt against the
    // committed store — the Tab placement basis, post-commit truth). The image
    // sits at the clamped pointer column, inside the visible strip, so this does
    // not scroll; the arming top-strip press already stopped playback, so there
    // is no live scanner to disturb — and move_playhead_to only ever writes
    // the cursor field, so it could not touch one regardless.
    viewport.move_playhead_to(
        source_frame_to_active_domain(app, audio, mv[mi].time_frame));
    // Region follows the images (architect 2026-07-23): a group tempo edit moves
    // the selected markers' target IMAGES (tempos change, source frames don't), so
    // re-derive the extent to the selection's NEW extent — the one surviving arm
    // (see the derivation above the kick). It re-activates a region the kick may
    // have cleared (set_region_to_selection_extent writes active = true), so the
    // span follows even across a domain shrink, and it is a no-op for a singleton
    // drag. Nothing here runs off the live selection either way: every mechanic
    // below reads the GRAB-TIME capture (marker, predecessor,
    // participant_predecessors, participant_grab_cents, walled), so which markers
    // the drag moves cannot change under it.
    set_region_to_selection_extent(app, audio, viewport);
    // The sync render's damage stops at the waveform's bottom edge; the
    // bottom strip is the one surface it does not cover (the dragged
    // marker's pass/ref readout resolves through the predecessor, and a
    // ridden playhead moves the timestamp).
    viewport.invalidate_timestamp_area();
}

// Release / lost-button finalize. The final synchronous re-warp already ran
// on the last committed step, so this settles history and lands the playhead: a
// NET change (ANY participant's final cents != its grab cents) pushes the ONE
// undo entry from the pre-drag snapshot, recomputes dirty, and fires the deferred
// target preview trigger; a drag that wandered every participant back to its grab
// value (the store already holds them — steps only write on change), or a walled
// group that never moved, pushes nothing. No GestureKind / coalesce wiring: mouse
// drags are coalesce-ineligible by standing rule, exactly like commit_drag above.
//
// The playhead lands on the GRABBED marker's current image UNCONDITIONALLY
// (regardless of net change), mirroring the position drag's commit-land: the
// crossing focused the grabbed member, but a WALLED or zero-commit drag never ran
// the per-event follow, so without a final land focus and the playhead would
// SPLIT — focus on the grabbed marker, the playhead stranded on whichever
// selection member the land put it on at press — and a later Space would audition
// from the wrong marker. For a moved drag this is a same-value repeat of the last
// event's follow (harmless). This land is now the ONLY playhead write any tempo
// drag end performs — the grab-playhead restore it used to share the story with
// died with the cancel.
// No region re-derive here: the region follows the images per changed event in
// apply_tempo_drag_motion, so the last committed event already landed it on the
// final extent (a walled / zero-commit drag never moved the images, so its
// resting region is still the pre-drag extent).
void MarkerDragOps::end_tempo_drag() {
    if (!app.tempo_drag.active) return;
    const auto& mv = app.warpmarkers.markers();
    bool net_changed = false;
    for (size_t k = 0; k < app.tempo_drag.participant_predecessors.size(); ++k) {
        const int pp = app.tempo_drag.participant_predecessors[k];
        if (pp < 0 || pp >= static_cast<int>(mv.size())) continue;
        if (mv[pp].tempo_cents != app.tempo_drag.participant_grab_cents[k]) {
            net_changed = true;
            break;
        }
    }
    // Capture the grabbed index before the wholesale reset discards it.
    const int mi = app.tempo_drag.marker;
    std::vector<GuiWarpMarker> pre_state =
        std::move(app.tempo_drag.pre_drag_snapshot);
    app.tempo_drag = TempoDragState{};
    if (net_changed) {
        // The TEMPO DRAG end. A singleton restore's always-on focus stem follows
        // from the selection — no lateral bit; the restored selection is the
        // TOUCHED (predecessor) row, whose image the re-warp repaints. No
        // touched hints (diff matcher).
        undo.push_undo_warp(std::move(pre_state),
                            /*affects_persistence=*/true,
                            /*touched_snapshot=*/{}, /*touched_live=*/{});
        undo.recompute_dirty();
        // The dirty dot and readouts live in the bottom strip; the flag/lane
        // surfaces already repainted with the last step's sync render.
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    }
    // Unconditional grabbed-marker land (see the header): closes the walled /
    // zero-commit focus-vs-playhead split. The marker's source frame never moved
    // (tempo only), so this is the current post-drag image through the standard
    // placement basis.
    if (mi >= 0 && mi < static_cast<int>(mv.size())) {
        viewport.move_playhead_to(
            source_frame_to_active_domain(app, audio, mv[mi].time_frame));
    }
    // No stem damage at release, on either grab shape. A SINGLE-marker grab IS
    // the singleton selection (its arming press single-selected it), so the
    // always-on focus stem painted throughout the drag and stays painted after
    // it; a GROUP grab normally keeps 2+ selected through the whole gesture, so
    // no stem paints at any point and none appears at release — and nothing can
    // collapse that group mid-drag any more (the coincident clear arm that once
    // could is deleted; the per-event region work is a pure extent re-derive).
    // Either way the release
    // moves nothing (tempo_drag.active going
    // false changes no stem input). A net-changed drag already repainted the stem's
    // moving image via each per-cent-step kick_waveform_sync; a walled / zero-commit
    // drag changed nothing at all. (The former lateral-gesture pin stamp + its
    // appear-damage lived only to survive tempo_drag.active going false without a
    // hover — subsumed by always-on.)
}

// -- W+target bare Left/Right: the tempo drag's keyboard twin -------------
//
// architect 2026-07-24 second pass (BUG + re-rule): W+target collapses to ONE
// MOTION — stretch/squish the waveform — reached by two image-lateral routes,
// the pointer-continuous tempo drag and this one-column-per-press step (plus
// the direct Up/Down tempo step on an owner). The same-day warp position
// nudge's target-view branch (the short-lived "third exception") authored the
// marker's SOURCE position from target view, deforming the map in a way that
// read as waveform truncation, and had no eligibility gate (it ran even where
// the drag refuses, e.g. on a marker following a label ref) — it is deleted;
// warp POSITIONS author in source view only again.
//
// One press steps the FOCUSED marker's IMAGE by one painted column WHERE THE
// CENT GRID ALLOWS — otherwise the minimum directional cent, whose travel can
// span several columns (the minimum-step rule below; e.g. ~8 px on a 1 s span
// at working zoom) — reached by adjusting the (deduped participant)
// predecessors' tempo_cents through the drag's own factored solve (closed-form
// singleton / monotone bisection group) — COLUMN-TARGETED: read the image's
// painted column, target the adjacent column's position, and let the solve place
// the image nearest it, the committed displacement then yielding to the cent grid
// (the minimum-step rule below). Not "pixel-anchored" — that term is reserved
// project-wide for the authored-position stored-equals-shown contract, which this
// cents-authoring gesture deliberately does not carry.
// Left = column - 1 -> image earlier -> predecessor tempo UP (faster is
// shorter); Right the reverse. No viewport gate: SELECTION/FOCUS, not viewport
// visibility, decides whether a keyboard edit can act (Ableton-parity — offscreen
// members of a selection drag as a group, and this step is the drag's keyboard
// twin; the painter-quantized q basis above keeps the offscreen solve honest). The
// solve's bracket-intersection clamp handles the walls constructively, so a press
// at the cents wall lands d == 0 and refuses below.
//
// ELIGIBILITY = the DRAG's, all-or-nothing (the keyboard step-family
// convention from adjust_tempo_cents_group — refuse the WHOLE press, never
// skip): the focused marker must arm (tempo_drag_predecessor >= 0), and the
// shared seeding helper's verdict over the whole selection — the six
// per-member legs AND the group label wall — REFUSES here where the drag
// arms-but-WALLS. Recorded difference: a drag is a held gesture, so walling it
// at zero keeps the hand-feel (the wall was hit before it moved); a keyboard
// press has no gesture to hold — refusal is the step convention. This closes
// the old position-branch hole where a marker after a label ref still nudged.
//
// COMMIT mirrors the tempo STEP's per-press tail (one discrete command), not
// the drag's per-event one: one coalescing-eligible undo entry per press (its
// OWN GestureKind::TempoImageStep, so a burst coalesces with itself and
// nothing else; touched hints = the participant rows, touched_snapshot ==
// touched_live — tempo only, no reorder ever), capture-before-kick region
// decision, ONE synchronous re-warp, post-kick playhead re-land on the FOCUSED
// marker's image, and a per-press preview trigger. The FOCUSED marker's image
// moves here (unlike the Up/Down tempo step, whose stepped marker's own
// image is fixed by construction), so its always-on focus stem rides the
// re-warp's full-waveform damage to its new column.
void MarkerDragOps::step_tempo_image(int direction, bool synthesized_repeat) {
    // Guards (silent refusals, navigation-class). The dispatch site already
    // routes only W+target here; the view test is kept defensive, the shape of
    // adjust_tempo_cents' own view gate. Read-only tabs refuse upstream, and
    // that is this routine's SOLE read-only defense — there is no check here:
    // read_only_key_blocked admits the bare horizontal arrows only while
    // playhead_in_marker_lane is false, so a locked tab holding the selection
    // this routine requires never dispatches them.
    // NO early stop_playback_if_playing (unlike the position nudges): every
    // SUCCESSFUL press reaches target_render.trigger() (the function's last line),
    // and target view's trigger synchronously freezes playback before the buffer
    // re-render (target_render.cpp's "every edit halts playback" model), so the
    // successful command's resting playback state matches the position nudges and
    // only the in-command ordering differs; a REFUSED press (any guard below)
    // leaves a listening session undisturbed — navigation-class, which an early
    // stop would not preserve.
    if (app.active_markers_view != 'W' || app.active_audio_view != 'T') return;
    if (app.loading || audio.total_frames() <= 0) return;
    if (app.selected_markers.empty()) return;
    const int f = app.last_selected_marker;
    const auto& mv = app.warpmarkers.markers();
    if (f < 0 || f >= static_cast<int>(mv.size())) return;   // focused stale
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    // BASIS COHERENCE (codex second-pass round-2 MEDIUM): the adjacent-column
    // target MUST live on the SAME column grid that produced the column, or an
    // off-screen focused marker shears. `cf` comes from
    // painted_column_of_source_frame, whose basis is the PAINTER-QUANTIZED
    // q = nearbyint(spp*W)/W over waveform_area — NOT the logical
    // current_samples_per_pixel. At integer zoom rungs the two coincide; at a
    // fractional rest they differ by design, and mixing them makes the
    // adjacent-column error grow as cf*(spp - q): far enough off-screen it
    // exceeds a pixel and lands t_des on the OPPOSITE side of the image, so the
    // solve returns a wrong-sign (many-cent) delta the d==0 fallback never
    // catches. So derive t_des from q — the exact free function the painter
    // uses, degenerate-geometry guard and all. (The tempo DRAG deliberately
    // differs: its t_des comes from the POINTER, which is clamped on-screen by
    // definition, so the logical spp is its natural basis.)
    const GuiRect area = waveform_area(app);
    const double q = painter_samples_per_pixel(app, audio, area);
    if (q <= 0.0) return;

    // Eligibility, the drag's own: the focused marker arms (its predecessor
    // owns a rewritable, non-collapsed tempo — the six legs), and the seeded
    // group verdict is clean. `walled` maps to REFUSE (see the header comment).
    const int pred = tempo_drag_predecessor(f);
    if (pred < 0) return;
    TempoGroupSeed seed = seed_tempo_group_participants(f, pred);
    if (seed.walled) return;

    // STEP TARGET, COLUMN-TARGETED (the gesture reads the painted column and
    // targets the adjacent column, but the committed displacement yields to the
    // cent grid — not "pixel-anchored", which is reserved for stored-equals-shown
    // position authoring): the focused marker's image under the LIVE memoized map
    // (the drag's solve basis — displayed == live at every command boundary, this
    // gesture's own sync re-warp maintaining it), its painted column, and the
    // adjacent column's target-domain position through the SAME painter-quantized q
    // that produced the column (t_des = viewport_start + column * q — see the
    // basis-coherence note above).
    const TargetWarpFrameMapCache& c = target_view_warp_frame_map_cached(
        app, sr, static_cast<long>(audio.total_frames()));
    const int cf = painted_column_of_source_frame(
        app, audio, static_cast<double>(mv[f].time_frame), c.warp_frame_map);
    const double t_des = static_cast<double>(app.viewport_start_sample) +
                         static_cast<double>(cf + direction) * q;

    // The factored solve core (never walled here — a walled seed refused
    // above). A failed hypothetical build drops the press.
    int64_t d = 0;
    if (!solve_tempo_group_delta(t_des, f, pred, seed.participants,
                                 /*walled=*/false, d)) {
        return;
    }
    // THE MINIMUM-STEP RULE (codex second-pass round-1 HIGH): d == 0 is NOT only
    // the bracket edge — it is the NORMAL quantization result whenever one cent
    // moves the image more than ~2 px. The cent grid is the authored domain's
    // resolution, so a one-pixel image target on a long span is unrepresentable:
    // 44.1 kHz, scale 1, tempo 1.00, working zoom, a 1 s / 800 px predecessor
    // span gives the adjacent-pixel solve 100.125 / 99.875 cents -> nearbyint ->
    // 100 -> d == 0 both directions, forever (each press re-derives from the
    // unchanged column, so nothing accumulates). A directional press therefore
    // commits AT LEAST one cent in the pressed direction (the Up/Down "a step
    // always steps" convention), moving the image by that cent's own pixel
    // distance (for THIS example — 1.00 -> 1.01 over a 1 s span at working zoom,
    // 55.125 frames/px — the segment shortens ~436.6 frames ≈ ~7.9 px, not one
    // pixel; longer spans travel further — the pressed direction always wins,
    // the exact pixel count yielded to the grid).
    // SIGN: d_fallback = -direction — Left (direction -1) targets an
    // earlier column -> shorter segment -> predecessor tempo UP -> +1 cent;
    // Right (direction +1) -> -1 cent (verified against the solve's own
    // faster-is-shorter derivation: tempo = l_src / (span * s), so a smaller
    // span raises the predecessor tempo). ALL-OR-NOTHING against the
    // participants: every participant must have >= 1 cent of bracket headroom in
    // that direction, else the WHOLE press refuses silently — a TRUE bracket wall
    // still refuses (the d==0 edge case remains real, just correctly scoped now).
    // When the SOLVE returns a nonzero d (short spans — one pixel needs many
    // cents), the fallback is never consulted and behavior is unchanged; the
    // fallback only floors the long-span limit. Rejected alternatives, recorded
    // so they are not re-proposed: sub-cent intent accumulation across presses
    // (violates the nothing-accumulates pixel-anchor principle — every press
    // re-derives from the painted state) and allowing the no-op (the reported
    // inertness IS the bug). The minimum-step rule is architect-ratified
    // (2026-07-24).
    if (d == 0) {
        const int64_t d_fallback = -static_cast<int64_t>(direction);
        for (int pp : seed.participants) {
            if (pp < 0 || pp >= static_cast<int>(mv.size())) continue;
            const int64_t c2 = mv[pp].tempo_cents + d_fallback;
            if (c2 < kTempoMinCents || c2 > kTempoMaxCents) return;  // true wall
        }
        d = d_fallback;
    }

    // Coalescing decision before mutation (repeat identity; this route is the only
    // producer of TempoImageStep, so a held key over the same group collapses to one
    // entry — any command ends the hold, so nothing foreign can get between).
    const bool merge =
        undo.coalesce_gesture(GestureKind::TempoImageStep, synthesized_repeat);
    std::vector<GuiWarpMarker> pre_state = mv;
    // Identity hints: the PARTICIPANT rows (the mutated markers — the diff
    // matcher would see them fine, but the hints are the group convention).
    // Tempo only, positions untouched, no reorder ever: touched_snapshot ==
    // touched_live, and the stamped focused index below stays valid.
    std::vector<int> touched = seed.participants;
    // Apply the SAME clamped delta to EVERY participant (plain integer adds —
    // the structural producer discipline). Iteration brackets stay (tempo
    // changes never clear a bracket, per member).
    for (int pp : seed.participants) {
        if (pp < 0 || pp >= static_cast<int>(mv.size())) continue;
        GuiWarpMarker* p = app.warpmarkers.marker_mut(pp);
        if (!p) continue;
        p->tempo_cents = p->tempo_cents + d;
    }
    // The TEMPO-IMAGE STEP. The entry's touched hints are seed.participants (the
    // PREDECESSOR / cents receiver), so a singleton restore selects and lands on
    // the PREDECESSOR, its always-on focus stem following from the selection (no
    // lateral bit; the undo visual follows the TOUCHED row).
    if (merge) undo.note_coalesced_commit();
    else       undo.push_undo_warp(std::move(pre_state),
                                   /*affects_persistence=*/true,
                                   touched, touched);
    undo.record_gesture(GestureKind::TempoImageStep);
    undo.recompute_dirty();
    viewport.invalidate_top_strip();
    viewport.invalidate_timestamp_area();
    // Target tail, the group tempo step's exact shape: one synchronous re-warp
    // (its live-domain reclamp can wholesale-clear a region whose old endpoint fell
    // outside a shrunken target total), then re-derive the extent UNCONDITIONALLY.
    // NO PROVENANCE BRANCH and no pre-kick capture pair (both deleted 2026-07-29):
    // a tempo gesture needs a selection, every TrimWindow SETTER clears it, and a
    // Free region rests only beside an empty selection — so a region resting here is
    // the selection's EXTENT by construction (the rule at
    // sync_region_to_trim_window's declaration, input_handler.h), which leaves
    // nothing to decide before the kick.
    viewport.kick_waveform_sync();
    // Playhead re-land on the FOCUSED marker's post-kick image (the live cache
    // just rebuilt against the committed store — the Tab placement basis,
    // post-commit truth). Its source frame never moved; only the image did.
    viewport.move_playhead_to(
        source_frame_to_active_domain(app, audio, mv[f].time_frame));
    set_region_to_selection_extent(app, audio, viewport);
    // The focused marker's always-on focus stem rides the FOCUSED image's slide
    // on the kick_waveform_sync full-waveform re-warp above — its source frame
    // never moved (tempo only), but the re-warp repaints its moved image column.
    // No pin stamp is needed; the singleton stays selected, so the stem was and
    // remains painting.
    target_render.trigger();
}
