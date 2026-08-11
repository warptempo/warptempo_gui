#include "phaseresetmarkers_ops.h"

#include "audio.h"
#include "engine/engine_geometry.h"  // kN
#include "position_nudge.h"  // the shared position-nudge flesh (prologue,
                                  // step, commit tail) + the movement doctrine
#include "input_handler.h"         // clear_region_highlight (the drop's collapse)
#include "target_render.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"

#include <algorithm>
#include <cstdint>
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
    viewport.invalidate_status_row_area();
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
    // unconditionally. THE PHASE CHOKEPOINT: both entry
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
    // THE STALE-INDEX BELT, one policy for every verb that iterates the selection
    // (architect 2026-07-30): a stale member is SILENTLY SKIPPED and the rest of
    // the batch proceeds — the warp delete's twin (fuller statement there). A belt
    // against a sanitization invariant reports nothing and refuses nothing; this
    // arm used to print one stderr line and refuse the WHOLE batch.
    std::vector<int> live_idx;
    live_idx.reserve(app.selected_markers.size());
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(tv.size())) continue;
        live_idx.push_back(idx);
    }
    // Nothing live to delete: no snapshot, no undo entry, no damage.
    if (live_idx.empty()) return;
    std::vector<GuiPhaseResetMarker> pre_state = app.phaseresetmarkers.markers();
    // Descending order so earlier indices stay valid (live_idx is ascending —
    // app.selected_markers is an ordered set and the skip above preserves it).
    for (auto it = live_idx.rbegin(); it != live_idx.rend(); ++it) {
        app.phaseresetmarkers.remove_marker(*it);
    }
    // A DELETE RESTS AN EMPTY SELECTION AND NO REGION (architect 2026-07-30, the
    // warp delete's twin): the demotion that dropped a 2+ delete down to a span
    // over the deleted positions is gone with the SPAN FORM — the region is trim
    // scratch and a delete has nothing to aim `x` at.
    selection.clear_selection();
    undo.push_undo_phase_reset(std::move(pre_state));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.invalidate_status_row_area();
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
    viewport.invalidate_status_row_area();
    target_render.trigger();
}

// Nudge the FOCUSED phase reset by exactly one on-screen pixel column per press.
// Direction: -1 for earlier, +1 for later. Symmetric with nudge_selected_markers
// — the moved reset is its own pixel-column anchor (stepped_anchor_frame — the
// one-column-per-press derivation and its numeric rationale live in the comment
// there). Both columns share painted_column_of_source_frame, so the anchored
// column is the painted one and every committed value is a whole source frame.
//
// HORIZONTAL MOVEMENT IS A FOCUS ACT — GROUPS ARE NEVER MOVED (architect
// 2026-07-29): a 2+ selection COLLAPSES TO ITS FOCUS in the shared prologue (which
// also lands the playhead there), so the body below moves exactly one reset. The
// doctrine and the dead per-member machinery are recorded once at the head of
// position_nudge.h.
//
// Wall semantics against this column's absolute range (zero / total_frames - 1 —
// the single marker EOF wall both columns share; see drop_phase_reset_at_position
// for the ruling): the step's delta is CLAMPED into the reset's own wall headroom,
// so the wall is exactly reachable by a press that would overshoot it. This is the
// UNIFIED WALL POLICY (architect 2026-07-30 — singleton steps clamp, group presses
// refuse whole); the policy and its rationale are stated once at the head of
// position_nudge.h. The phase twin refused the whole press until that ruling; the
// warp twin already clamped. The integer walls win over the pixel grid, and a
// clamped target equal to the current frame writes NOTHING (the silent no-op
// below). Crossing a neighbor is legal and goes through the reorder-and-remap
// path below.
void GuiPhaseResetMarkersOps::nudge_selected_phase_resets(
        int direction, bool synthesized_repeat) {
    // Shared guard prologue: loading / empty-audio refusal, empty/no-focus
    // refusals, the coalesce verdict, the geometry refusals, the focused-index
    // belt, and THE COLLAPSE + LAND that makes this a focus act — which carries
    // the collapse's own playback stop (the playhead-follows / lead-in rationale
    // lives at the declaration). Refuses silently, navigation-class.
    const PositionNudgePrologue pro = position_nudge_prologue(
        app, audio, playback_lifecycle, selection, viewport, undo,
        GestureKind::PhaseResetNudge, synthesized_repeat,
        static_cast<int>(app.phaseresetmarkers.markers().size()));
    if (!pro.ok) return;
    const bool merge = pro.merge;
    // Phase resets carry no tempo, so there is no inherit/tempo analog to the warp
    // twin's value gestures — this column's only nudge is positional.
    const auto& tv = app.phaseresetmarkers.markers();
    const int   f  = pro.focused;   // validated in [0, tv.size()) by the prologue

    // The anchoring map is the DISPLAYED paint basis —
    // displayed_or_live_target_map, the SAME map the flag/trim painters read —
    // so the moved reset travels exactly the commanded pixel column against
    // WHAT IS PAINTED, even inside a worker publish window where the displayed
    // map lags the live one. Phase resets author in their TARGET home view only
    // (the home-view binding, architect 2026-07-22), a mapped (non-identity)
    // domain.
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const int64_t reset_wall = audio.total_frames() - 1;

    // (1) THE STEP: the reset's own currently painted column, plus direction,
    // committed through authored_frame_at_column's target arm
    // (stepped_anchor_frame; the doctrine and the numbers live at its
    // declaration).
    //
    // (2) WALLS WIN BY CLAMPING, in the reset's own wall headroom
    // [0 - orig_f, reset_wall - orig_f] (integer arithmetic, non-empty because
    // every stored reset rests in [0, reset_wall]). Exact integer compares, never
    // a float compare — the exact-wall-reach doctrine forbids an epsilon-fragile
    // pre-filter — and the wall is exactly reachable: a press that would overshoot
    // lands ON it. The warp twin's shape verbatim (the unified wall policy at the
    // head of position_nudge.h).
    const int64_t orig_f = tv[f].time_frame;
    int64_t D = stepped_anchor_frame(app, audio, map, orig_f, direction) - orig_f;
    if (D < -orig_f)              D = -orig_f;
    if (D > reset_wall - orig_f)  D = reset_wall - orig_f;

    // (3) The reset commits orig_f + D. The [0, reset_wall] clamp is a deliberate
    // walls-win belt — provably dead today (this path is all-integer int64 sums,
    // and the headroom clamp above keeps the sum in range), kept as cheap
    // insurance so a future edit to the clamp cannot commit a wall-illegal frame
    // (an out-of-wall authored position would save a load-fatal file). The warp
    // twin carries the same belt for the same reason.
    int64_t committed_f = orig_f + D;
    if (committed_f < 0)          committed_f = 0;
    if (committed_f > reset_wall) committed_f = reset_wall;
    // POST-CLAMP IDENTITY IS A SILENT NO-OP: a press already resting on its wall
    // (or one whose column step resolved to the same frame) writes NOTHING — no
    // undo push, no damage, no playback stop. This is what makes the keyboard stop
    // rule's refusal gating exact for the nudges.
    if (committed_f == orig_f) return;   // saturated / zero-step press
    // THE SINGLETON PRESS'S STOP, past every refusal and immediately ahead of the
    // first write (the keyboard stop rule's refusal gating, at
    // stop_playback_if_playing's declaration in playback_lifecycle.h): a position
    // nudge collapses the selection to point form and takes the playhead with it.
    // On a 2+ press the prologue's collapse arm already stopped; this second call
    // early-returns on the stopped session, so the double call is free.
    playback_lifecycle.stop_playback_if_playing();
    std::vector<GuiPhaseResetMarker> pre_state =
        app.phaseresetmarkers.markers();
    // Identity hint: the nudged reset in PRE-reorder snapshot coordinates (the
    // diff matcher is identity-blind for a column-snapped move — the moved row can
    // land field-identical on another row).
    std::vector<int> touched_snapshot{f};
    if (GuiPhaseResetMarker* m = app.phaseresetmarkers.marker_mut(f))
        m->time_frame = committed_f;
    // A nudge may cross a neighbor; restore time order and remap the index-shaped
    // state (the selection follows its reset to the new slot).
    remap_marker_indices_after_reorder(
        app, reorder_markers_by_time(app.phaseresetmarkers.markers_mut()));
    // touched_live: the nudged reset's POST-reorder live index — read off the
    // selection, which the remap rewrote in place and which is exactly this one
    // reset (the prologue collapsed to it).
    std::vector<int> touched_live(app.selected_markers.begin(),
                                  app.selected_markers.end());
    // Coalesce a held key: the PHYSICAL press pushed the pre-burst snapshot with
    // the identity hints; each synthesized repeat skips the redundant push and
    // REFRESHES the surviving entry's touched_live to this press's post-reorder
    // index (touched_snapshot stays the first press's pre-burst coordinates). The
    // post-mutation re-record happens in the shared tail.
    if (merge) {
        undo.refresh_coalesced_touched_live(std::move(touched_live));
    } else {
        // The phase-reset POSITION NUDGE. A restore owes no stem bit: stems key
        // on the MARKER (always on, class-colored), never on the selection.
        undo.push_undo_phase_reset(std::move(pre_state),
                                   std::move(touched_snapshot),
                                   std::move(touched_live));
    }
    // Shared commit tail: record/dirty/invalidate (its full-waveform damage moves
    // the nudged reset's always-on stem), playhead follow (committed_f is
    // reorder-independent; the target home maps), the point command's region
    // collapse, and the view-independent target trigger. Ordering rationale at the
    // declaration.
    finish_position_nudge(app, audio, viewport, undo,
                                GestureKind::PhaseResetNudge, committed_f,
                                target_render);
}
