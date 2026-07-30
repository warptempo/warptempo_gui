#include "phaseresetmarkers_ops.h"

#include "audio.h"
#include "engine/engine_geometry.h"  // kN
#include "group_position_nudge.h"  // the shared group position-nudge flesh
#include "input_handler.h"         // clear_region_highlight (the drop's collapse)
#include "target_render.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

// Phase reset authoring cluster: drop / delete / toggle / detect / clear
// operations on the phase reset store, reaching undo, selection, viewport,
// and playback_lifecycle through the struct's reference members.

// Drop a phase reset marker at `time_frame`. Placement is bounded only
// by the absolute range; arbitrarily close and exactly-coincident drops
// are legal (the render boundary collapses an exact-equal group to one
// event). Selection collapses to the freshly-inserted index. Frame-0
// phase alignment is implicit by definition and needs no marker to
// assert it.
void GuiPhaseResetMarkersOps::drop_phase_reset_at_position(double time_frame) {
    if (audio.sample_rate() <= 0) return;
    // Marker creation is a commit: the position funnels through
    // snap_authored_frame like every other gesture commit (mirrors warp
    // drop_marker — every current caller passes an integer-valued playhead
    // frame, so the snap is the uniformity funnel, not a rounding step).
    // The wall check below IS the load guard's comparison, exactly,
    // applied to the snapped value.
    const int64_t drop_frame = snap_authored_frame(time_frame);
    // Both marker columns share the warp column's EOF wall, total - 1;
    // phase reset does not get a total-exact wall.
    // Warp stops one frame short structurally (build_warp_frame_map refuses
    // sub-frame segments); phase reset stops one frame short by ruling: a
    // reset in the last source frame has nothing left to re-ground, and
    // total-1 keeps every marker inside the playhead's [0, total-1] domain
    // so marker gestures and playhead syncs agree exactly.
    if (drop_frame > audio.total_frames() - 1)
        return;
    std::vector<GuiPhaseResetMarker> pre_state = app.phaseresetmarkers.markers();
    GuiPhaseResetMarker nm;
    nm.time_frame = drop_frame;
    const int new_idx = app.phaseresetmarkers.insert_marker(std::move(nm));
    selection.set_single_selection(new_idx);
    undo.push_undo_phase_reset(std::move(pre_state));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    // Match drop_marker: re-affirm the playhead on the new phase reset. The one
    // create path is the lead-in drop below (bare `s` and the empty-lane
    // double-click both take it), which authors N/2 BEFORE the playhead, so the
    // playhead lands back on the seeded reset. This is a drop consequence (the
    // reset is created for the playhead), not a selection sync.
    const int64_t sample = source_frame_to_active_domain(app, audio, drop_frame);
    viewport.move_playhead_to(sample);
    // A DROP IS A POINT COMMAND (architect 2026-07-29, drop_marker's twin —
    // see the fuller statement there): it seats the playhead on the reset it
    // creates and single-selects it, so any resting span ends here,
    // unconditionally and of any provenance. THE PHASE CHOKEPOINT: both entry
    // routes reach this column only through drop_phase_reset_lead_in_at_playhead,
    // whose only act is to call this, so one clear covers both. PAST EVERY
    // REFUSAL: the callers' read-only / home-view gates and the wrapper's own
    // sample-rate test return before this runs, and this function's two refusals
    // — no sample rate, a drop_frame past the EOF wall — return above, before the
    // insert. clear_region_highlight owns its damage.
    clear_region_highlight(app, viewport);
    target_render.trigger();
}

// Target-view lead-in drop: place a phase reset N/2 output samples BEFORE the
// playhead. The OLA/Hann synthesis lead-in makes a reset's output ramp up over
// ~N/2 samples, reaching full scale ~N/2 after its authored frame; offsetting
// by -N/2 places the reset so its full-scale output lands on the playhead (the
// perceived transient). N/2 is measured in the target/output paint domain,
// matching the output-domain phase-reset overlay, then mapped to a source
// frame. kN/2 is an exact integer and the playhead is an integer frame, so the
// offset is plain integer arithmetic (no snap needed); clamped to 0. Reuses
// drop_phase_reset_at_position so the created reset takes the full create path
// — walls, undo, selection, the region collapse — unchanged; only the
// seed frame is offset. This is the phase column's ONLY drop (bare `s` and the
// empty-lane double-click), and both routes gate it on the home-view predicate,
// whose P arm IS target view — where the overlay/lead-in exist.
void GuiPhaseResetMarkersOps::drop_phase_reset_lead_in_at_playhead() {
    if (audio.sample_rate() <= 0) return;
    const int64_t ph =
        std::max<int64_t>(0, app.playhead_cursor_sample - kN / 2);
    const int64_t src_frame = active_domain_to_source_frame(app, audio, ph);
    drop_phase_reset_at_position(static_cast<double>(src_frame));
}

// Delete every selected phase reset. No label/cascade rules — phase resets
// don't have labels. Any selected phase reset is deletable, including one
// that happens to sit at time 0 — frame-0 phase alignment is implicit
// and needs no marker to assert it.
void GuiPhaseResetMarkersOps::delete_selected_phase_reset() {
    if (app.selected_markers.empty()) return;
    const auto& tv = app.phaseresetmarkers.markers();
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(tv.size())) {
            std::fprintf(stderr,
                "warptempo_gui: phase reset delete rejected: stale selection index\n");
            return;
        }
    }
    std::vector<GuiPhaseResetMarker> pre_state = app.phaseresetmarkers.markers();
    // Capture the selected resets' active-domain positions BEFORE the store
    // mutation, so a multi-marker delete DEMOTES down to the region spanning
    // them (a DROP former of the selection<->highlight coupling — the delete
    // drops the selection and forms the region; architect 2026-07-23). Phase deletes run in the
    // target home view, whose map phase resets do not affect, so the pre/post
    // active-domain mapping is identical either way.
    std::vector<int64_t> del_positions;
    del_positions.reserve(app.selected_markers.size());
    for (int idx : app.selected_markers) {
        // Clamp the forward-map image into the live domain: region endpoints
        // hold PLAYABLE frames only (the display-state validator rejects an
        // endpoint >= total and clears the highlight), and an EOF reset's
        // unclamped image can round one past the wall — the land's own helper
        // keeps it a playable frame.
        del_positions.push_back(clamp_playhead_to_live_domain(
            source_frame_to_active_domain(app, audio, tv[idx].time_frame),
            app, audio));
    }
    for (auto it = app.selected_markers.rbegin();
         it != app.selected_markers.rend(); ++it) {
        app.phaseresetmarkers.remove_marker(*it);
    }
    selection.clear_selection();
    // Demote a multi-marker delete to the spanning region — session scratch,
    // OUTSIDE undo (undoing the delete restores the markers while the region
    // stays). A single deleted reset is a point, not a span, so it forms no
    // region (the sliver rule's spirit; 2-marker + positive-span is the gate).
    // The waveform damage below covers the region paint. ORDER: the deselect
    // above runs FIRST, so its membership clear cannot reach the span formed
    // here — and a FREE span is outside that clear's reach regardless (it takes
    // SelectionExtent only).
    if (del_positions.size() >= 2) {
        const auto [lo, hi] = std::minmax_element(del_positions.begin(),
                                                  del_positions.end());
        if (*hi > *lo) {
            app.region.active     = true;
            app.region.a_frame    = *lo;
            app.region.b_frame    = *hi;
            // The delete demotion drops the deleted markers, so this region is
            // FREE — tempo gestures skip it.
            app.region.provenance = RegionProvenance::Free;
        }
    }
    undo.push_undo_phase_reset(std::move(pre_state));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

// Toggle the disabled flag on each selected phase reset. Unconditional —
// phase resets have no label-def gating like warp markers do. This mutates
// each marker in place via marker_mut, whereas the warp column's
// toggle_disabled (warpmarkers_ops.cpp) builds a proposed vector and swaps
// it in; the two are behaviorally equivalent and the style split is a
// cosmetic mirror divergence, accepted.
void GuiPhaseResetMarkersOps::toggle_phase_reset_disabled() {
    if (app.selected_markers.empty()) return;
    std::vector<GuiPhaseResetMarker> pre_state = app.phaseresetmarkers.markers();
    bool changed = false;
    for (int idx : app.selected_markers) {
        GuiPhaseResetMarker* m = app.phaseresetmarkers.marker_mut(idx);
        if (!m) continue;
        m->disabled = !m->disabled;
        changed = true;
    }
    if (!changed) return;
    undo.push_undo_phase_reset(std::move(pre_state));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

// Nudge the selected phase resets by exactly one on-screen pixel column per
// press — EACH of them, on its own column (GROUP, architect 2026-07-23; the 2+
// selection became per-member rather than rigid on 2026-07-29, so this gesture
// is no longer the keyboard sibling of the group position DRAG, which stays
// rigid). Direction: -1 for earlier, +1 for later. Symmetric with
// nudge_selected_markers — every moved reset, lone or member, is its own
// pixel-column anchor (stepped_anchor_frame — the one-column-per-press
// derivation, its numeric rationale, and the spacing consequences the
// per-member rule accepts live in the comment there). Both columns share
// painted_column_of_source_frame, so the anchored column is the painted one and
// every committed value is a whole source frame.
//
// Wall semantics are one regime, GROUP-strict, against this column's absolute
// range (zero / total_frames - 1 — the single marker EOF wall both columns share;
// see drop_phase_reset_at_position for the ruling): this gesture runs in phase
// reset's TARGET home view only (the home-view binding, architect 2026-07-22), so
// the all-or-nothing silent refusal applies to the WHOLE group — if ANY member's
// proposal leaves the range the whole press refuses (no per-member skip and no
// per-member clamp; the wall is hit before anything moves — the group step's
// max-strict precedent), the FOCUSED member included, since 2026-07-29 evaluated
// on each member's own column snap. A singleton is bit-for-bit the pre-group
// refusal. The integer walls win over the pixel grid.
// (The old source-view clamp arm died with the binding — there is no per-view
// refuse-vs-clamp split left.) Crossing a neighbor is legal and goes through the
// reorder-and-remap path below; the render boundary collapses an exact-equal
// group to one event.
void GuiPhaseResetMarkersOps::nudge_selected_phase_resets(
        int direction, bool synthesized_repeat) {
    // Shared guard prologue: loading / empty-audio refusal, playback stop first,
    // empty/no-focus refusals, the coalesce verdict, the geometry refusals, and
    // the stale-index belt (the playhead-follows-focused / lead-in rationale lives
    // at the declaration). Refuses silently, navigation-class.
    const GroupNudgePrologue pro = group_position_nudge_prologue(
        app, audio, playback_lifecycle, undo, GestureKind::PhaseResetNudge,
        synthesized_repeat,
        static_cast<int>(app.phaseresetmarkers.markers().size()));
    if (!pro.ok) return;
    const bool merge = pro.merge;
    // GROUP nudge (architect 2026-07-23): a 2+ selection moves whole — NO
    // collapse_to_focused. Phase resets carry no tempo, so there is no
    // inherit/tempo analog. Since 2026-07-29 it moves PER MEMBER (architect):
    // each selected reset re-snaps to its OWN adjacent painted column, so the
    // painted step is exactly one column for every one of them; the rigid single
    // delta and the "never re-column-snapped per member" convention apply to the
    // pointer group DRAG only now. app.last_selected_marker stays the FOCUS (the
    // playhead follows it), not an anchor the others ride.
    const auto& tv = app.phaseresetmarkers.markers();
    const int   f  = pro.focused;   // validated in [0, tv.size()) by the prologue

    // The anchoring map is the DISPLAYED paint basis —
    // displayed_or_live_target_map, the SAME map the flag/trim painters read —
    // so each moved reset travels exactly the commanded pixel column against
    // WHAT IS PAINTED, even inside a worker publish window where the displayed
    // map lags the live one. Phase resets author in their TARGET home view only
    // (the home-view binding, architect 2026-07-22), a mapped (non-identity)
    // domain, so the wall policy is one regime — GROUP all-or-nothing silent
    // refusal: if ANY member's proposal leaves the absolute range the WHOLE press
    // refuses (the wall is hit before anything moves — the group's max-strict
    // precedent). The former source-view clamp arm was unreachable under the
    // binding and is gone.
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const int64_t reset_wall = audio.total_frames() - 1;

    // (1) EVERY moved reset — the lone one, or each member of a 2+ selection —
    // takes the SAME step: its own currently painted column, plus direction,
    // committed through authored_frame_at_column's target arm (stepped_anchor_frame;
    // the doctrine, the numbers, and the spacing consequences the per-member rule
    // accepts live at its declaration). One painted column per press each, no
    // shared delta: the rigid single-delta form, with its forward map and its
    // unclamped inverse, left this gesture on 2026-07-29 (architect).
    //
    // (2) THE ONE refusal is the EXACT INTEGER wall belt on the SNAPPED result
    // (t_new outside [0, reset_wall]) — walls are exact integer compares, never a
    // float compare (the exact-wall-reach doctrine forbids an epsilon-fragile
    // pre-filter), and a proposal that rounds ONTO a wall passes (equality is
    // legal). ANY out-of-range member refuses the WHOLE press, the focused one
    // included: the max-strict regime this column has always had, now evaluated
    // on each member's own column snap. It also has to stay a refusal rather than
    // becoming a per-member clamp — clamping would pool the clamped members onto
    // the wall frame and merge them permanently; the warp twin adopts the same
    // shape, recorded there as a planner translation of the per-member rule,
    // architect-confirmed 2026-07-29 (live-tested).
    // A SINGLETON is bit-for-bit its long-standing behavior: one snap, one belt.
    const int64_t orig_f = tv[f].time_frame;
    int64_t committed_f = orig_f;   // the focused reset's commit, for the follow
    std::vector<std::pair<int, int64_t>> proposals;
    proposals.reserve(app.selected_markers.size());
    for (int idx : app.selected_markers) {
        const int64_t t_new = stepped_anchor_frame(
            app, audio, map, tv[idx].time_frame, direction);
        if (t_new < 0 || t_new > reset_wall) return;
        if (idx == f) committed_f = t_new;
        proposals.emplace_back(idx, t_new);
    }
    bool any_changed = false;
    for (const auto& [idx, t_new] : proposals) {
        if (t_new != tv[idx].time_frame) { any_changed = true; break; }
    }
    if (!any_changed) return;
    std::vector<GuiPhaseResetMarker> pre_state =
        app.phaseresetmarkers.markers();
    // Identity hints: the WHOLE group in PRE-reorder snapshot coordinates (the
    // diff matcher is identity-blind for a translated group).
    std::vector<int> touched_snapshot(app.selected_markers.begin(),
                                      app.selected_markers.end());
    for (const auto& [idx, t_new] : proposals) {
        GuiPhaseResetMarker* m =
            app.phaseresetmarkers.marker_mut(idx);
        if (!m) continue;
        m->time_frame = t_new;
    }
    // A nudge may cross a neighbor; restore time order and remap the index-shaped
    // state (the whole group's selection follows to the new slots).
    remap_marker_indices_after_reorder(
        app, reorder_markers_by_time(app.phaseresetmarkers.markers_mut()));
    std::vector<int> touched_live(app.selected_markers.begin(),
                                  app.selected_markers.end());
    // Coalesce a held key: the PHYSICAL press pushed the pre-burst snapshot with
    // the group hints; each synthesized repeat skips the redundant push and
    // REFRESHES the surviving entry's touched_live to this press's post-reorder
    // indices (touched_snapshot stays the first press's pre-burst coordinates). The
    // post-mutation re-record happens in the shared tail.
    if (merge) {
        undo.note_coalesced_commit();
        undo.refresh_coalesced_touched_live(std::move(touched_live));
    } else {
        // The phase-reset POSITION NUDGE. A singleton restore's always-on focus
        // stem follows from the selection — no lateral bit.
        undo.push_undo_phase_reset(std::move(pre_state),
                                   std::move(touched_snapshot),
                                   std::move(touched_live));
    }
    // Shared commit tail: record/dirty/invalidate (its full-waveform damage moves
    // the focused singleton's always-on stem), playhead follow (committed_f is
    // reorder-independent; the target home maps), SelectionExtent region follow, and
    // the view-independent target trigger. Ordering rationale at the declaration.
    finish_group_position_nudge(app, audio, viewport, undo,
                                GestureKind::PhaseResetNudge, committed_f,
                                target_render);
}
