#include "marker_drag.h"

#include "audio.h"
#include "gui_display_context.h"
#include "input_handler.h"       // set_region_to_selection_extent (group-drag commit)
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

    // GROUP DRAG (architect 2026-07-23, REVERSING the retired single-marker-only
    // rule): dragging a member of a 2+ selection moves the WHOLE selection
    // rigidly, file-manager style. The arming plain flag press DEFERRED its
    // single-select + land (PendingMarkerDrag::deferred_click) exactly so the
    // multi-selection is still intact here — otherwise the press would have
    // collapsed it to {hit}. So seed the drag set from ALL selected markers when
    // the grabbed marker belongs to a 2+ selection; else the lone {hit} (the
    // immediate-arm single-marker drag, whose press already single-selected and
    // landed). Defensively skip any selected index outside the active column's
    // [0, n) — the set_region_to_selection_extent convention; stale indices
    // cannot arise mid-session, but the guard is cheap and keeps the parallel
    // vectors sound. selected_markers is a sorted set, so dragging_markers stays
    // ascending; grabbed_k (below) records hit's slot in it.
    std::set<int> drag_set;
    if (app.selected_markers.size() >= 2 &&
        app.selected_markers.count(hit) > 0) {
        for (int idx : app.selected_markers) {
            if (idx >= 0 && idx < n) drag_set.insert(idx);
        }
    } else {
        drag_set.insert(hit);
    }

    // No first-marker pin, either column: every marker is draggable,
    // including a warp marker at time 0. Whatever arrangement results, the
    // parser resolver normalizes it at render/preview time (ambiguity
    // resolves to tempo 1.00, one stderr line per timestamp), so the GUI
    // never pins the gesture.

    DragState d;
    d.active = true;
    d.drag_mode = phase_reset ? 'P' : 'W';
    d.dragging_markers.assign(drag_set.begin(), drag_set.end());
    // grabbed_k: hit's slot in the ascending drag vectors — the delta anchor,
    // the playhead-follow / land target, and (single-marker) the re-asserted
    // selection. Linear find; a single-marker drag lands 0.
    d.grabbed_k = 0;
    for (size_t k = 0; k < d.dragging_markers.size(); ++k) {
        if (d.dragging_markers[k] == hit) {
            d.grabbed_k = static_cast<int>(k);
            break;
        }
    }
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

    // Compute scalar delta_min / delta_max as the INTERSECTION of each dragged
    // member's absolute range: zero on the left and the marker EOF wall on the
    // right — total_frames minus one source frame for BOTH columns (the
    // per-column split, warp total-1 vs phase reset total, is retired: warp is
    // structural, build_warp_frame_map refuses sub-frame segments; phase reset
    // walls at total-1 by ruling — a reset in the last source frame has nothing
    // left to re-ground, and total-1 keeps every marker inside the playhead's
    // [0, total-1] domain). Exact frame compares — the same comparison the load
    // guard applies. This ONE shared scalar is what makes a GROUP drag stop AS A
    // UNIT: in source view (warp's home, identity map) every member clamps at the
    // same delta simultaneously, so the group stays rigid at the 0 / total-1
    // walls. Neighbors do not bound the drag; members may cross each other and
    // their neighbors freely, and commit_drag reorders the store and remaps the
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

    // Viewport clamp: GRABBED-ONLY, deliberately. Only the grabbed marker (hit)
    // is clamped to the visible strip so a mouse drag can't push it offscreen,
    // where its precise position — the one being authored — would be hidden. The
    // OTHER group members ride the rigid delta and MAY travel offscreen: blind
    // MOTION of a group held by one on-screen member is fine, exactly the trim
    // pair drag's recorded ruling — the offscreen ruling forbids blind GRABS, not
    // blind motion of a set held by its handle. The clamp sits on top of the
    // absolute data walls (delta_min/delta_max computed just above).
    // viewport_marker_bounds is active-domain while the walls are source frames,
    // so inverse-translate the edges through the DISPLAYED map (the paint basis
    // the drag's mechanics run on; identity on source view's empty map); the map
    // is monotonic, so the source clamp matches the active-pixel clamp. The
    // grabbed marker is on-screen at grab (the arming flag press hit
    // hit_test_flag, which reports only visible chips against the same displayed
    // map, so the marker's painted column is within the viewport), so these
    // bounds bracket it. vp_lo_src <= vp_hi_src always, so [delta_min, delta_max]
    // does not invert from this pair.
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
    // restore. For a single-marker drag the arming press single-selected the
    // grabbed marker, so this captures {hit}; for a GROUP drag the arming press
    // DEFERRED its single-select, so this captures the whole intact
    // multi-selection — and a cancel restores exactly that (Esc reverts the
    // drag's position change, not any click's selection: a deferred click never
    // committed one).
    d.pre_drag_selection = capture_selection_snapshot(app);
    // Playhead follows the GRABBED marker (see the ruling at DragState): a
    // single-marker press already landed the playhead on it; a group member's
    // deferred press did not, so the first motion's focus + follow tows it onto
    // the grabbed marker. Either way the pre-ride capture feeds the Esc-cancel
    // restore (always applied).
    d.pre_ride_playhead_sample = app.playhead_cursor_sample;
    // Region as it rests at grab, for the Esc-cancel restore. Only a group drag
    // can capture an ACTIVE region here — a single-marker press landed the
    // playhead at press, whose standing region clear dissolved any highlight
    // before begin_drag ran — so for a single-marker drag this is a no-op by
    // construction. apply_drag_motion live-tracks an active region to the moving
    // group's extent; commit_drag re-derives it from the post-commit store.
    d.pre_drag_region = app.region;
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
// DISPLAYED map (the paint basis); the commit's store write and the playhead's
// final placement remain live-map (post-commit) territory. The live consumer
// of the non-identity case is the PHASE reset drag in target view: under the
// home-view binding (architect 2026-07-22) a WARP marker drag authors in
// source view, where the displayed map is identity and both hops degenerate
// to orig + delta, while a phase reset drag authors in target view against a
// real (non-identity) displayed map — where a live-map inverse would slip at
// the slope ratio, and the displayed basis does not. Walls stay
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
        // position too). In SOURCE view (warp's home, identity map) the shared
        // delta bounds clamp every group member at the same delta, so the group
        // stays exactly rigid. In TARGET view (phase resets) the per-member
        // source clamp against the SHARED intersection bounds guarantees no
        // member ever crosses a wall, but near a wall a member may PIN while the
        // others keep moving — a slight squash of the group in the displayed
        // domain. ACCEPTED (the wall is the hard invariant; rigidity is the
        // soft one).
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
        // Selection focus on the press-to-motion edge. A SINGLE-marker drag
        // re-asserts the single selection on the grabbed marker (normally a
        // no-op — the arming press already single-selected it — kept here so
        // the "a real drag focuses the grabbed marker" rule stays with the drag
        // machinery). A GROUP drag must NOT collapse the multi-selection;
        // instead it FOCUSES the grabbed marker without touching membership
        // (Selection::focus_without_collapse — sets last_selected + dissolves
        // the shift anchor, no size crossing so no size-2 overlay damage), so
        // the whole group stays selected and the lane-text run / bottom readout
        // track the grabbed member.
        if (first_motion) {
            if (app.drag.dragging_markers.size() <= 1) {
                selection.set_single_selection(app.drag.hit_marker);
            } else {
                selection.focus_without_collapse(app.drag.hit_marker);
            }
        }
        // Playhead follows the marker, mid-motion: slide the resting cursor
        // playhead to the GRABBED marker's live proposed position
        // (moveable_times[grabbed_k] — [0] for a single-marker drag). Target
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
        const int gk = app.drag.grabbed_k;
        if (gk >= 0 && gk < static_cast<int>(app.drag.moveable_times.size())) {
            const double proposed = app.drag.moveable_times[gk];
            int64_t sample;
            if (target_domain) {
                sample = static_cast<int64_t>(std::nearbyint(
                    map_source_to_target(
                        proposed, displayed_or_live_target_map(app, audio))));
            } else {
                sample = static_cast<int64_t>(std::nearbyint(proposed));
            }
            viewport.move_playhead_to(sample);
        }
        // Region live-tracking (group drag only — a single-marker press landed
        // at press, whose standing region clear dissolved any highlight before
        // begin_drag ran, so app.region.active is false here for those). Retrack
        // the active region to the moving group's live extent: min/max over all
        // moveable_times (SCAN — cheap and assumption-free; the two-hop is
        // monotone so order actually survives, but the per-member wall clamps can
        // tie values and a scan needs no such proof), each endpoint mapped
        // exactly as the playhead follow above (identity in source view, through
        // displayed_or_live_target_map otherwise — the paint basis, mid-motion
        // being paint coherence), std::nearbyint, then clamp_playhead_to_live_
        // domain (region endpoints hold PLAYABLE live-domain frames, the standing
        // invariant every former clamps through). invalidate_waveform_area below
        // already covers the repaint. When the region is inactive, touch nothing
        // — a programmatic multi-select without a region gains none from a drag.
        if (app.region.active && !app.drag.moveable_times.empty()) {
            int64_t lo = 0, hi = 0;
            bool have = false;
            for (double proposed : app.drag.moveable_times) {
                int64_t pos;
                if (target_domain) {
                    pos = static_cast<int64_t>(std::nearbyint(
                        map_source_to_target(
                            proposed,
                            displayed_or_live_target_map(app, audio))));
                } else {
                    pos = static_cast<int64_t>(std::nearbyint(proposed));
                }
                pos = clamp_playhead_to_live_domain(pos, app, audio);
                if (!have) { lo = hi = pos; have = true; }
                else { if (pos < lo) lo = pos; if (pos > hi) hi = pos; }
            }
            if (have) {
                app.region.a_frame = lo;
                app.region.b_frame = hi;
            }
        }
        viewport.invalidate_waveform_area();
        viewport.invalidate_top_strip();
    }
}

// Commit the current drag. Caller ensures drag was active. Sets dirty
// only if the markers actually moved. Playhead rule (drag and nudge,
// keyboard/mouse counterparts, share it): the playhead follows the GRABBED
// marker UNCONDITIONALLY (architect 2026-07-23, reversing the 2026-07-20
// decoupling) — the grabbed member (grabbed_k) is the follow / land target.
// For a single-marker drag the arming click already landed the playhead on it;
// for a GROUP drag the deferred press did not, so the drag's first motion tows
// the playhead onto the grabbed member — either way it lands with the grabbed
// marker here, so a later Space auditions FROM it. Every marker click is a land
// route; Tab and `c` additionally recenter / re-zoom. The lead-in workflow
// (parking the playhead upstream) is supplied by the coming scrub surface
// instead.
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
    // below stays on the LIVE map deliberately — post-commit placement truth,
    // the Tab basis.
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
    // Identity hints for the post-restore selection, filled only on a real
    // (net_changed) drag. The undo diff matcher cannot recover the touched set
    // when a translated group or a column-snapped drag lands rows FIELD-IDENTICAL
    // at another marker's position, so name the touched markers explicitly:
    // touched_snapshot = their indices in the PRE-DRAG snapshot (the pre-reorder
    // store indices, valid in the snapshot a restore produces), touched_live =
    // their POST-reorder indices in the committed (after) store.
    std::vector<int> touched_snapshot;
    std::vector<int> touched_live;
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
        // Pre-reorder: the store was untouched in order during motion and the
        // write above changed only time_frame values, so dragging_markers still
        // holds the pre-drag store indices — the pre-drag snapshot's coordinates.
        touched_snapshot = app.drag.dragging_markers;
        // The drag may have carried markers across neighbors; restore
        // time order and remap the index-shaped state — the selection (the
        // single grabbed marker, or the whole group of a group drag) follows
        // the markers to their new slots. The drag state's own held indices are
        // remapped too, though they are discarded by the wholesale reset
        // below (grabbed_k, a vector position not a store index, needs no
        // remap — see app_state.cpp).
        if (phase_reset) {
            remap_marker_indices_after_reorder(
                app,
                reorder_markers_by_time(app.phaseresetmarkers.markers_mut()));
        } else {
            remap_marker_indices_after_reorder(
                app, reorder_markers_by_time(app.warpmarkers.markers_mut()));
        }
        // Post-remap: remap_marker_indices_after_reorder rewrote dragging_markers
        // in place to the reordered (after) store's indices.
        touched_live = app.drag.dragging_markers;
    }
    // Capture the GRABBED marker's committed frame into a local BEFORE the
    // wholesale DragState reset below discards it — the grabbed_k slot, not [0]:
    // a group drag's grabbed member may sit anywhere in the ascending vectors,
    // and the playhead lands on IT (== [0] for a single-marker drag). The land
    // runs regardless of net_changed: a wander-back drag must land the playhead
    // exactly back on the grabbed marker (original_times[grabbed_k]), erasing any
    // mid-motion rounding drift from apply_drag_motion's paint-basis moves. The
    // bounds guards cover a degenerate drag with no moveable marker and a
    // defensive out-of-range grabbed_k.
    const int gk = app.drag.grabbed_k;
    const bool land_playhead = net_changed
        ? (gk >= 0 && gk < static_cast<int>(committed.size()))
        : (gk >= 0 && gk < static_cast<int>(app.drag.original_times.size()));
    const int64_t ridden_final_frame = !land_playhead ? 0
        : (net_changed ? committed[gk] : app.drag.original_times[gk]);
    std::vector<GuiWarpMarker>    snap_w =
        std::move(app.drag.pre_drag_snapshot);
    std::vector<GuiPhaseResetMarker> snap_t =
        std::move(app.drag.pre_drag_phase_reset_snapshot);
    const int                 hint_last = app.drag.pre_drag_last_selected;
    app.drag = DragState{};
    if (net_changed) {
        if (phase_reset) {
            undo.push_undo_phase_reset(std::move(snap_t), hint_last,
                                       std::move(touched_snapshot),
                                       std::move(touched_live));
        } else {
            undo.push_undo_warp(std::move(snap_w), hint_last,
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
    // Region re-derive (GROUP drag only — a single-marker drag never has an
    // active region here, its press cleared it). apply_drag_motion live-tracked
    // the region to the moving group during motion; snap it back to the RESTING
    // extent now, re-derived from the POST-commit, reordered/remapped selection
    // (still the whole group — the drag focused it without collapsing
    // membership). Runs REGARDLESS of net_changed: a wander-back group drag also
    // moved the region live and must restore it to the resting extent.
    // set_region_to_selection_extent is the same Direction-B owner the
    // multi-select clicks use; here it MAINTAINS an already-active highlight
    // through the store mutation rather than creating one. app.region survives
    // the DragState reset (it lives on AppState), so this reads the tracked-live
    // active flag correctly.
    if (app.region.active) {
        set_region_to_selection_extent(app, audio, viewport);
    }
    // No synchronous re-warp at commit: a marker drag can no longer change the
    // displayed target plate. Warp marker drags author in warp's SOURCE home
    // view only (the home-view binding, architect 2026-07-22 — the arming flag
    // press gates on active_column_authoring_allowed, and the view cannot toggle
    // mid-gesture since every key but Esc/Ctrl+Q is swallowed while a drag is
    // active), where the source waveform has no map-dependent plate; and a phase
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
    // (the owner-only rule the target-view Alt+Up/Down step already applies).
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

// Begin the tempo drag at the threshold crossing. Captures the grab tempo,
// the pre-drag store snapshot (the one undo entry's payload), the selection
// snapshot, and the grab playhead (the Esc-cancel restore). Returns false
// (gesture dropped) only on a defensive eligibility re-check failure.
bool MarkerDragOps::begin_tempo_drag(int hit) {
    // The walk computes the GROUP's predecessor once; -1 is the ineligibility
    // signal (a defensive re-check — the arm already ran the walk). Flowing the
    // walked index here keeps predecessor == marker - 1 from being assumed
    // anywhere downstream.
    const int pred = tempo_drag_predecessor(hit);
    if (pred < 0) return false;
    const auto& mv = app.warpmarkers.markers();

    TempoDragState d;
    d.active      = true;
    d.marker      = hit;
    d.predecessor = pred;
    d.grab_cents  = mv[pred].tempo_cents;
    d.pre_drag_snapshot      = mv;
    d.pre_drag_last_selected = app.last_selected_marker;
    // Pre-drag selection for the Esc / Ctrl+Q cancellation restore: the
    // arming press single-selected the dragged marker, so this captures
    // {hit} (the marker-drag cancel shape).
    d.pre_drag_selection = capture_selection_snapshot(app);
    // Playhead follows the dragged marker (see the ruling at DragState): the
    // arming plain marker click landed the playhead on this marker, so each
    // step re-lands it on the post-commit image. The marker's source frame
    // never changes here — only its image moves. The pre-ride capture feeds
    // the Esc-cancel restore (always applied).
    d.pre_ride_playhead_sample = app.playhead_cursor_sample;
    app.tempo_drag = std::move(d);
    viewport.clear_hover_popup();
    return true;
}

// One motion event: invert the pointer's target-domain position to the
// predecessor tempo that places the dragged marker's image nearest it,
// quantize to integer cents, clamp into the tempo bracket, and — when the
// candidate differs from the stored value — commit it live with a
// synchronous re-warp (the tempo-step precedent: every step is a committed
// value edit with displayed == live restored synchronously at the step
// boundary). Vertical motion is ignored; the solve is ABSOLUTE (pointer x ->
// tempo), so no press anchor exists and the threshold-crossing event needs
// no catch-up fold.
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

    // T(P) from the live memoized map — constant across the drag's steps
    // (the constancy argument in the header comment), recomputed per event
    // by choice. Live, not displayed: each step commits and re-warps
    // synchronously, so displayed == live at every step boundary and the
    // live cache IS the current paint basis (an empty-map cold fallback is
    // identity, matching paint's own fallback).
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
        // shorter, the slope convention above), so the zero/negative-span
        // limit pins constructively to the bracket's MAX end.
        candidate = kTempoMaxCents;
    } else {
        // Invert delta_tgt = L_src / (tempo * s) for tempo at span == T_des
        // - T(P), then enter the integer-cents domain the way the bpm
        // derivation does (compute_base_tempo_scale): nearbyint on the
        // double cents count, bracket-clamped BEFORE the int64 cast so an
        // overflowing count (a denormal span) can never reach the cast —
        // the cents entry is a GUI-side gesture proposal funneling into the
        // int64 store field, the value-domain sibling of
        // snap_authored_frame. Constructive clamp, not refusal: far-right
        // travel (tempo toward 0) pins to the MIN end, near-left to the MAX.
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

    // The both-unchanged skip: an event whose quantized candidate equals the
    // stored value commits nothing — this is what makes the flag move in
    // discrete cent-grid jumps (the Ableton snap; our grid is the cents
    // grid) and bounds the sync-render cost to one per cent step.
    if (candidate == mv[pi].tempo_cents) return;

    GuiWarpMarker* p = app.warpmarkers.marker_mut(pi);
    if (!p) return;
    // Tempo only: no time change, so the store's ascending order is
    // untouched and no reorder/remap runs. The predecessor's iteration
    // bracket is left in place — tempo changes never clear a bracket, the
    // standing rule both tempo-authoring surfaces follow.
    p->tempo_cents = candidate;
    const bool first_commit = !app.tempo_drag.moved;
    app.tempo_drag.moved = true;
    // First committed step: re-assert the single selection on the dragged
    // marker (normally a no-op — the arming press already single-selected
    // it), the reposition drag's first-motion rule kept with the drag
    // machinery.
    if (first_commit) selection.set_single_selection(mi);
    // Synchronous re-warp, exactly adjust_tempo_cents' target-view tail:
    // kick_waveform_sync reclamps zoom/viewport first (a tempo change moves
    // the target total) and rebuilds plate + stem/flag caches inline, with
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
    // The sync render's damage stops at the waveform's bottom edge; the
    // bottom strip is the one surface it does not cover (the dragged
    // marker's pass/ref readout resolves through the predecessor, and a
    // ridden playhead moves the timestamp).
    viewport.invalidate_timestamp_area();
}

// Release / lost-button finalize. The final synchronous re-warp already ran
// on the last committed step, so this only settles history: a NET change
// (final cents != grab cents) pushes the ONE undo entry from the pre-drag
// snapshot, recomputes dirty, and fires the deferred target preview trigger;
// a drag that wandered back to its grab value (the store already holds
// grab_cents — steps only write on change) pushes nothing. No GestureKind /
// coalesce wiring: mouse drags are coalesce-ineligible by standing rule,
// exactly like commit_drag above.
void MarkerDragOps::end_tempo_drag() {
    if (!app.tempo_drag.active) return;
    const auto& mv = app.warpmarkers.markers();
    const int pi = app.tempo_drag.predecessor;
    const bool net_changed =
        pi >= 0 && pi < static_cast<int>(mv.size()) &&
        mv[pi].tempo_cents != app.tempo_drag.grab_cents;
    std::vector<GuiWarpMarker> pre_state =
        std::move(app.tempo_drag.pre_drag_snapshot);
    const int hint_last = app.tempo_drag.pre_drag_last_selected;
    app.tempo_drag = TempoDragState{};
    if (net_changed) {
        undo.push_undo_warp(std::move(pre_state), hint_last);
        undo.recompute_dirty();
        // The dirty dot and readouts live in the bottom strip; the flag/lane
        // surfaces already repainted with the last step's sync render.
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    }
}

// Esc / Ctrl+Q cancel: restore the GRAB tempo (one store write + one
// synchronous re-warp), the pre-drag SelectionSnapshot, and the grab playhead,
// exactly the marker-drag cancel shape. No undo entry and no preview trigger:
// the restored store equals the pre-drag state the last trigger already
// previewed.
void MarkerDragOps::cancel_tempo_drag() {
    if (!app.tempo_drag.active) return;
    restore_selection_snapshot(app, app.tempo_drag.pre_drag_selection);
    const int pi = app.tempo_drag.predecessor;
    bool restored = false;
    if (pi >= 0 && pi < static_cast<int>(app.warpmarkers.markers().size()) &&
        app.warpmarkers.markers()[pi].tempo_cents !=
            app.tempo_drag.grab_cents) {
        GuiWarpMarker* p = app.warpmarkers.marker_mut(pi);
        if (p) {
            p->tempo_cents = app.tempo_drag.grab_cents;
            restored = true;
        }
    }
    if (restored) viewport.kick_waveform_sync();
    viewport.move_playhead_to(app.tempo_drag.pre_ride_playhead_sample);
    app.tempo_drag = TempoDragState{};
    viewport.invalidate_waveform_area();
    viewport.invalidate_top_strip();
    viewport.invalidate_timestamp_area();
}
