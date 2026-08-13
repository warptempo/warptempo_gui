#include "marker_drag.h"

#include "audio.h"
#include "gui_display_context.h"
#include "warp_frame_map.h"
#include "warp_frame_map_view.h"
#include "phaseresetmarkers.h"
#include "target_render.h"
#include "warpmarkers.h"

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

bool MarkerDragOps::begin_drag(int hit, int mouse_x) {
    if (hit < 0) return false;
    const int sr = audio.sample_rate();
    if (sr <= 0) return false;
    const bool phase_reset = (app.active_markers_view == 'P');
    if (hit >= active_marker_count(app)) return false;

    auto t_of = [&](int idx) -> int64_t {
        if (phase_reset) {
            return app.phaseresetmarkers.markers()[idx].time_frame;
        }
        return app.warpmarkers.markers()[idx].time_frame;
    };

    // ONE MARKER, ALWAYS — GROUPS ARE NEVER MOVED (architect 2026-07-29,
    // HORIZONTAL MOVEMENT IS A FOCUS ACT; the doctrine and the whole dead rigid
    // group-drag machinery are recorded at the head of position_nudge.h). The
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
    // The selection re-assert does not live here: it runs at the THRESHOLD
    // CROSSING in begin_drag, unconditionally. A wall-saturated drag (the clamped
    // delta pins the proposal — the marker at the EOF wall) never reaches this
    // point, yet it is a real drag that commit lands the playhead for.
    // Playhead follows the marker, mid-motion: slide the resting cursor
    // playhead to the dragged marker's live proposed position. Target
    // view maps through displayed_or_live_target_map — the SAME basis the
    // DragOverlay paints the flag through, so the playhead tracks the flag in
    // lockstep (mid-motion is paint coherence; commit_drag's two-step placement
    // is the truth).
    // THE MAP INPUT IS THE ROUNDED PROPOSAL, which is what makes that lockstep
    // TRUE rather than merely intended (2026-08-01). The painters do not map the
    // free double: frame_to_paint_sample (render.cpp) rounds the source frame
    // FIRST and rounds the map's output second, and painted_column_of_source_-
    // frame_on_basis restates that shape for the gesture-commit helpers. This
    // site mapped the fraction directly, so on a compressing segment the cursor
    // could land a target frame off the flag's own paint sample and paint one
    // column beside the stem it is supposed to be riding — measured at ~0.08% of
    // motion events over a swept zoom range. Rounding here first makes the two
    // one expression; it also matches the COMMIT below, which maps an integer
    // frame by construction, so the ride and its landing now agree too.
    // Reachable through the PHASE-RESET column, whose home view is the target one
    // (a warp drag is source-home by the arming press's authoring gate).
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
            map_source_to_target(std::nearbyint(new_t), dmap)));
    } else {
        sample = static_cast<int64_t>(std::nearbyint(new_t));
    }
    viewport.move_playhead_to(sample);
    // NO REGION WORK, and none is reachable: the arming press single-selected the
    // marker and cleared any resting scratch span at its own site, so a marker
    // drag runs with no
    // region at all. The group live-track that used to re-derive an extent span per
    // motion event died with the group drag (architect 2026-07-29 — groups are
    // never moved; the doctrine is at the head of position_nudge.h).
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
// workflow (parking the playhead upstream) is supplied by the audition
// scrub instead.
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
    // position_nudge.h). Nothing here needs the walls-win D clamp any more:
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
    // Commit gates on NET CHANGE, not on whether motion occurred — the COMMITTED
    // frame against the pre-drag one, which is the only form of the question worth
    // asking (the `moved` latch that used to be tracked beside it was never read
    // and is deleted, 2026-07-29). A drag that wanders and returns exactly to its
    // original position therefore commits nothing: pushing an undo entry there would
    // record a snapshot byte-equal to the live store, a no-op history entry that
    // both undo and redo restore invisibly. The compare runs on the SNAPPED value:
    // a sub-column wander commits nothing, while the whole-frame store keeps
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
        // The position-DRAG commit (both columns). A restore owes no stem bit:
        // stems key on the MARKER (always on, class-colored), never on the
        // selection, so the restore's own damage carries them.
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
        viewport.invalidate_status_chain_area();
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
    // The dragged marker's STEM moves at commit under the full-waveform
    // invalidate_waveform_area above: the drag shifts its frame, so its
    // always-on stem repaints at the committed column (every enabled marker
    // stems since row 5 — nothing here keys on selection).
    // NO REGION WORK, and none is reachable: the arming press single-selected the
    // marker and cleared any resting span, and nothing during the drag forms one.
    // The extent re-derive that used to snap a live-tracked group span back to its
    // resting extent here died with the group drag (architect 2026-07-29 — groups
    // are never moved; the doctrine is at the head of position_nudge.h).
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
