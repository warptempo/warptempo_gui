#include "phaseresetmarkers_ops.h"

#include "audio.h"
#include "engine/engine_geometry.h"  // kN
#include "position_nudge.h"  // the shared position-nudge flesh (prologue,
                                  // step, commit tail) + the movement doctrine
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
    // SILENT, AND IT HAS NO PRODUCER (re-derived 2026-08-30, the warp twin's
    // finding): every drop road authors at or BEFORE the playhead — the
    // target arm subtracts kN/2 output samples, the source arm authors at
    // the cursor exactly, and the playhead rests in [0, total-1] by every
    // writer's clamp — so no press can reach this wall.
    // An error arm exists iff a producer exists (validation_topology.md).
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
    // Match drop_marker: re-affirm the playhead on the new phase reset. The one
    // create path is the drop below (bare `s`, the empty-lane double-click
    // and Shift+S all take it), whose TARGET arm authors kN/2 BEFORE the
    // playhead — this seat lands the playhead back on the seeded reset — and
    // whose SOURCE arm authors at the cursor exactly, the seat then a
    // same-value re-affirm. This is a drop
    // consequence (the
    // reset is created for the playhead), not a selection sync.
    const int64_t sample = source_frame_to_active_domain(app, audio, drop_frame);
    viewport.move_playhead_to(sample);
    // A DROP IS A POINT COMMAND (architect 2026-07-29, drop_marker's twin —
    // see the fuller statement there): it seats the playhead on the reset it
    // creates and single-selects it, so the trim region overlay goes with it —
    // through the SEAT above since 2026-08-19, move_playhead_to being one of the
    // rule's two movement owners (the rule at clear_region_highlight,
    // input_handler.h), so this site's own call is deleted and the answer is
    // unchanged. PAST EVERY
    // REFUSAL: the callers' read-only / home-view gates and the wrapper's own
    // sample-rate test return before this runs, and this function's two refusals
    // — no sample rate, a drop_frame past the EOF wall — return above, before the
    // insert, so a refused drop never reaches the seat.
    target_render.trigger();
}

// The phase column's drop, FORKED ON THE AUDIO VIEW (architect 2026-08-30 —
// the P column authors in BOTH views since that ruling, and the two arms
// seed differently by its own clause: "no lead-in at all, just like no
// offset overlay").
//
// TARGET VIEW — the LEAD-IN drop: place the reset kN/2 OUTPUT samples BEFORE
// the playhead. The OLA/Hann synthesis lead-in makes a reset's output ramp
// up over ~N/2 samples, reaching full scale ~N/2 after its authored frame;
// offsetting by -N/2 places the reset so its full-scale output lands on the
// playhead (the perceived transient). N/2 is measured in the target/output
// paint domain, matching the output-domain phase-reset overlay, then mapped
// to a source frame. kN/2 is an exact integer and the playhead is an integer
// frame, so the offset is plain integer arithmetic (no snap needed); clamped
// to 0.
//
// SOURCE VIEW — NO LEAD-IN AND NO MAP CONVERSION: the reset lands EXACTLY at
// the playhead's source frame. The kN/2 is an OUTPUT-domain length the
// source cursor is not in (subtracting it here would take kN/2 SOURCE frames
// off a source cursor and seat the reset somewhere the lead-in does not
// reach), and the offset's whole aim — the full-scale point on the playhead
// — is only visible where the overlay is: the missing overlay and the
// missing lead-in are the clues you are reading the wrong domain, and a
// misplaced reset is harmless and adjusted in target view. The identity
// domain needs no active_domain_to_source_frame call either — the cursor IS
// a source frame there.
//
// BOTH ARMS reuse drop_phase_reset_at_position, so the created reset takes
// the full create path — the EOF wall (whose no-producer record holds in
// both arms: the source arm authors AT the playhead, inside [0, total-1] by
// every writer's clamp), undo, the single-select, and the playhead seat
// (whose source_frame_to_active_domain is the identity in the source arm,
// making the seat a same-value re-affirm there) — unchanged; only the seed
// frame differs. The routes are three: bare `s` and the empty-lane
// double-click, both P-column routes in EITHER audio view, and SHIFT+S,
// which SWITCHES the session into T+P first
// (GuiInputHandler::drop_phase_reset_in_target_view, input_handler.cpp) and
// so always takes the target arm.
void GuiPhaseResetMarkersOps::drop_phase_reset_lead_in_at_playhead() {
    if (audio.sample_rate() <= 0) return;
    if (app.active_audio_view != 'T') {
        drop_phase_reset_at_position(
            static_cast<double>(app.playhead_cursor_sample));
        return;
    }
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
    // The subject refusal reads its one owner (marker_selection_standing,
    // app_state.h — 2026-08-30, the warp delete's shape; the Delete button's
    // face reads the same fact through marker_selection_verb_actionable).
    // SILENT: the Delete dispatch arm cards the composed refusal ahead of
    // this call, and one press owes one card.
    if (!marker_selection_standing(app)) return;
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
    // A DELETE RESTS AN EMPTY SELECTION (architect 2026-07-30, the warp
    // delete's twin): the demotion that dropped a 2+ delete down to a span over
    // the deleted positions is gone with the SPAN FORM, and there is no span
    // state left for one to write into — the region IS the trim.
    selection.clear_selection();
    undo.push_undo_phase_reset(std::move(pre_state));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    target_render.trigger();
}

// Toggle the disabled flag on each selected phase reset. Unconditional —
// phase resets have no label-def gating like warp markers do. This mutates
// each marker in place via marker_mut, whereas the warp column's
// toggle_disabled (warpmarkers_ops.cpp) builds a proposed vector and swaps
// it in; the two are behaviorally equivalent and the style split is a
// cosmetic mirror divergence, accepted.
void GuiPhaseResetMarkersOps::toggle_phase_reset_disabled() {
    // The subject refusal reads its one owner (marker_selection_standing,
    // app_state.h) and is SILENT for the delete's reason: the Ctrl+D dispatch
    // arm cards the composed refusal ahead of this call.
    if (!marker_selection_standing(app)) return;
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
GuiOpRefusal GuiPhaseResetMarkersOps::nudge_selected_phase_resets(
        int direction, bool synthesized_repeat) {
    // Shared guard prologue: the WHOLE refusal set as one predicate (the Left /
    // Right buttons' own marker_nudge_actionable — the state and geometry
    // guards, the focused-index belt and THE WALL, all of it ahead of the
    // coalesce stamp), then the coalesce verdict, then THE COLLAPSE + LAND that
    // makes this a focus act — which carries the collapse's own playback stop
    // (the playhead-follows / lead-in rationale lives at the declaration). ITS
    // refusals say NOTHING, the warp twin's rule and for its reason
    // (GuiOpRefusal, warpmarkers_ops.h): each is an outer gate's card already,
    // a belt against a kept invariant, or the wall, silent beside its greyed
    // button. `direction` is passed for the wall term alone.
    const PositionNudgePrologue pro = position_nudge_prologue(
        app, audio, playback_lifecycle, selection, viewport, undo,
        GestureKind::PhaseResetNudge, synthesized_repeat, direction);
    if (!pro.ok) return std::nullopt;
    const bool merge = pro.merge;
    // Phase resets carry no tempo, so there is no inherit/tempo analog to the warp
    // twin's value gestures — this column's only nudge is positional.
    const auto& tv = app.phaseresetmarkers.markers();
    const int   f  = pro.focused;   // validated in [0, tv.size()) by the prologue

    const int64_t orig_f = tv[f].time_frame;

    // THE WALL-REGIME MIDDLE, through the shared landing owner since
    // 2026-08-31 — THE WARP TWIN'S OWN CALL, not a copy of its shape
    // (position_nudge_landing, position_nudge.h; the painted-column step, the
    // headroom clamp with its exact integer compares, and the walls-win belt
    // are all argued at its declaration). The anchoring basis is the DISPLAYED
    // map, so the moved reset travels exactly the commanded pixel column
    // against WHAT IS PAINTED even inside a worker publish window; phase
    // resets author in their TARGET home view only (the home-view binding,
    // architect 2026-07-22), a mapped (non-identity) domain, and the EOF wall
    // is the one both columns share. Crossing a neighbor is legal and goes
    // through the reorder-and-remap below.
    const int64_t committed_f =
        position_nudge_landing(app, audio, orig_f, direction);
    // POST-CLAMP IDENTITY IS A SILENT NO-OP: a press already resting on its wall
    // (or one whose column step resolved to the same frame) writes NOTHING — no
    // undo push, no damage, no playback stop. This is what makes the keyboard stop
    // rule's refusal gating exact for the nudges.
    // AND IT IS SILENT AGAIN SINCE 2026-08-31, the warp twin's rule verbatim —
    // a benign one-dimensional refusal already at its state says nothing, the
    // unmoved flag being its own answer; the one-day card "The marker is
    // already at the edge" is retired in both columns.
    // WHAT STILL REACHES IT is the 2+ press whose FOCUS rests on a wall, the
    // warp twin's rule verbatim again: the prologue asks the same landing ahead
    // of the coalesce stamp, so a SINGLETON at its wall refuses there instead.
    if (committed_f == orig_f)
        return std::nullopt;
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
    // Coalesce a held key or button: THE BURST'S OPENER pushed the pre-burst
    // snapshot with the identity hints — a held KEY's own physical press, a
    // held arrow BUTTON's first fire, the two-surface rule stated once at
    // Undo::coalesce_gesture; each synthesized repeat behind it skips the
    // redundant push and REFRESHES the surviving entry's touched_live to this
    // fire's post-reorder index (touched_snapshot stays the opener's pre-burst coordinates). The
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
    return std::nullopt;
}
