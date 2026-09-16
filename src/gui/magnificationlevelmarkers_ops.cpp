#include "magnificationlevelmarkers_ops.h"

#include "audio.h"
#include "position_nudge.h"  // the shared position-nudge flesh (prologue,
                             // step, commit tail) + the movement doctrine
#include "warp_frame_map_view.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// THE MAGNIFICATION LEVEL AUTHORING CLUSTER — the phase-reset cluster's bodies
// over the third store, with this column's two deltas stated once at the
// header: NO GuiTargetRender (a level is display-only, so no act here may
// dispatch a preview) and THE PICTURE'S OWN GAIN KICK in its place. Every body
// below captures the gain hash before its store write and hands it to
// kick_waveform_sync_if_gain_changed after; the comments here carry only what
// is this column's.

namespace {

// THE LOCK'S SENTENCE, the warp value step's own body in this cluster (its
// twin is warp_value_step_lock_refusal, warpmarkers_ops.cpp): the tab's
// sentence on a read-only tab, grid iterations' under the lit lamp — the two
// sentences the keyboard gate's cards carry for the same two states. (Grid
// iterations never lights on this column, so the second arm is a belt that
// states the lock's shape rather than a reachable refusal.)
const char* magnification_level_lock_refusal(const AppState& app) {
    if (active_view_state(app).read_only) return kTabReadOnlyCard;
    if (app.iteration_mode_enabled)       return kIterationLockCard;
    return nullptr;
}

}  // namespace

// The contract is at the declaration. This is drop_phase_reset_at_position's
// body over the third store, with the LEVEL COPY in place of that column's
// lead-in fork.
void GuiMagnificationLevelMarkersOps::drop_magnification_level_at_position(
        double time_frame) {
    if (audio.sample_rate() <= 0) return;
    // Marker creation is a commit: the position funnels through
    // snap_authored_frame like every other gesture commit, the two siblings'
    // rule verbatim.
    const int64_t drop_frame = snap_authored_frame(time_frame);
    // THE MARKER EOF WALL, total - 1 — the ONE wall all three columns share
    // (its ruling is at drop_phase_reset_at_position). SILENT AND WITH NO
    // PRODUCER, the twins' finding in this column's terms: the drop authors AT
    // the playhead's own source frame and the playhead rests in [0, total-1]
    // by every writer's clamp, so no press can reach it. An error arm exists
    // iff a producer exists (validation_topology.md).
    if (drop_frame > audio.total_frames() - 1) return;
    // THE PICTURE'S BEFORE-HASH, captured ahead of the store write (the
    // cluster's rule at its header). A drop copies the level already in force,
    // so the profile is normally unmoved and the kick at the tail renders
    // nothing — but a drop LANDING ON AN EXISTING MARKER'S FRAME can move it
    // (the builder's "the last enabled row of equal frames wins"), and that is
    // exactly the case the hash catches without a rule of its own.
    const uint64_t prior_gain_hash = viewport.waveform_gain_hash();
    std::vector<GuiMagnificationLevelMarker> pre_state =
        app.magnificationlevelmarkers.markers();
    GuiMagnificationLevelMarker nm;
    nm.time_frame = drop_frame;
    // THE LEVEL IN FORCE AT THAT FRAME, off the one owner shared with the gain
    // profile's builder (magnification_level_in_force,
    // magnificationlevelmarkers.h): the new marker restates the level its
    // section already carries, so the drop is a boundary to author FROM rather
    // than a change to the picture.
    nm.level = magnification_level_in_force(pre_state, drop_frame);
    const int new_idx =
        app.magnificationlevelmarkers.insert_marker(std::move(nm));
    selection.set_single_selection(new_idx);
    // THE IDENTITY HINT, the two drops' twin (the contract and the producer
    // enumeration are at restore_touched_indices, app_state.h): the new row is
    // absent from this entry's snapshot, so the UNDO side names nothing and the
    // REDO side names the inserted row.
    undo.push_undo_magnification_level(std::move(pre_state),
                                       /*touched_snapshot=*/{},
                                       /*touched_live=*/{new_idx});
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    // A DROP IS A POINT COMMAND (the two twins' rule): it seats the playhead on
    // what it created and single-selects it, so the trim region overlay goes
    // with it through the movement owner. Here the seat is a same-value
    // re-affirm whenever the drop came from the playhead — which is every
    // reachable route — and it is kept for the twins' reason: the seat is a
    // consequence of the drop, not a selection sync.
    const int64_t sample = source_frame_to_active_domain(app, audio, drop_frame);
    viewport.move_playhead_to(sample);
    // AND NO PREVIEW: a level reaches no render input, so this cluster ends
    // where its siblings call target_render.trigger() (the header's rule).
    viewport.kick_waveform_sync_if_gain_changed(prior_gain_hash);
}

// The column's one create body at the playhead. NO LEAD-IN AND NO AUDIO-VIEW
// FORK (architect 2026-09-15): the phase column's kN/2 exists because the
// engine is window-centred and a reset must be seeded that far ahead of the
// point it protects; a magnification level is a PICTURE BOUNDARY and the frame
// the playhead stands on IS the boundary wanted. The column is target-view only
// (active_column_authoring_allowed's 'M' arm), so the cursor is always a mapped
// domain value and the inverse is always asked; it is asked unconditionally
// anyway, being the identity in source view for two compares.
void GuiMagnificationLevelMarkersOps::drop_magnification_level_at_playhead() {
    if (audio.sample_rate() <= 0) return;
    const int64_t src_frame =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);
    drop_magnification_level_at_position(static_cast<double>(src_frame));
}

// Delete every selected magnification level marker — the phase-reset delete's
// body, clause for clause, over this store. No labels and no cascade on this
// column either, so any selected marker is deletable.
void GuiMagnificationLevelMarkersOps::delete_selected_magnification_levels() {
    // SILENT: the Delete dispatch arm cards the composed refusal ahead of this
    // call, and one press owes one card (marker_selection_standing is the
    // atom, app_state.h).
    if (!marker_selection_standing(app)) return;
    const auto& mv = app.magnificationlevelmarkers.markers();
    // THE STALE-INDEX BELT, one policy for every verb that iterates the
    // selection (architect 2026-07-30): a stale member is SILENTLY SKIPPED and
    // the rest of the batch proceeds.
    std::vector<int> live_idx;
    live_idx.reserve(app.selected_markers.size());
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(mv.size())) continue;
        live_idx.push_back(idx);
    }
    if (live_idx.empty()) return;
    const uint64_t prior_gain_hash = viewport.waveform_gain_hash();
    std::vector<GuiMagnificationLevelMarker> pre_state =
        app.magnificationlevelmarkers.markers();
    // Descending so earlier indices stay valid (live_idx is ascending — the
    // selection is an ordered set and the skip above preserves it).
    for (auto it = live_idx.rbegin(); it != live_idx.rend(); ++it) {
        app.magnificationlevelmarkers.remove_marker(*it);
    }
    // A DELETE RESTS AN EMPTY SELECTION (architect 2026-07-30, the family's
    // rule).
    selection.clear_selection();
    // THE IDENTITY HINT, the two deletes' twin: live_idx names the deleted rows
    // in the PRE-delete store, which is this entry's snapshot, so an undo
    // re-selects the rows it put back; the live side names nothing.
    undo.push_undo_magnification_level(std::move(pre_state),
                                       /*touched_snapshot=*/std::move(live_idx),
                                       /*touched_live=*/{});
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.kick_waveform_sync_if_gain_changed(prior_gain_hash);
}

// Toggle the disabled flag on each selected magnification level marker.
// Unconditional — this column has no label-def gating either — and a DISABLED
// MARKER IS INVISIBLE TO THE PICTURE, the gain profile's builder walking
// straight past it, which is what makes this toggle a picture act and so a
// gain-kick one.
void GuiMagnificationLevelMarkersOps::toggle_magnification_level_disabled() {
    // SILENT for the delete's reason: the Ctrl+D dispatch arm cards the
    // composed refusal ahead of this call.
    if (!marker_selection_standing(app)) return;
    const uint64_t prior_gain_hash = viewport.waveform_gain_hash();
    std::vector<GuiMagnificationLevelMarker> pre_state =
        app.magnificationlevelmarkers.markers();
    bool changed = false;
    for (int idx : app.selected_markers) {
        GuiMagnificationLevelMarker* m =
            app.magnificationlevelmarkers.marker_mut(idx);
        if (!m) continue;
        m->disabled = !m->disabled;
        changed = true;
    }
    if (!changed) return;
    undo.push_undo_magnification_level(std::move(pre_state));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    viewport.kick_waveform_sync_if_gain_changed(prior_gain_hash);
}

// Nudge the FOCUSED magnification level marker by the press's own count of
// on-screen pixel columns — the phase-reset twin's body verbatim over this
// store, with its own GestureKind and its own picture tail.
//
// HORIZONTAL MOVEMENT IS A FOCUS ACT — GROUPS ARE NEVER MOVED (architect
// 2026-07-29): a 2+ selection COLLAPSES TO ITS FOCUS in the shared prologue
// (which also lands the playhead there), so the body below moves exactly one
// marker. The doctrine is recorded once at the head of position_nudge.h.
//
// Wall semantics against the shared [0, total_frames - 1] marker range, the
// UNIFIED WALL POLICY (singleton steps clamp): the step's delta is CLAMPED into
// the marker's own headroom, so the wall is exactly reachable and a clamped
// target equal to the current frame writes NOTHING. Crossing a neighbour is
// legal and goes through the reorder-and-remap path below — and on this column
// a crossing can change the PICTURE even where nothing else does, two adjacent
// boundaries swapping which level holds between them, which the tail's gain
// kick carries.
GuiOpRefusal
GuiMagnificationLevelMarkersOps::nudge_selected_magnification_levels(
        int step_columns, bool synthesized_repeat) {
    // Shared guard prologue: the WHOLE refusal set as one predicate (the Left /
    // Right buttons' own marker_nudge_actionable, which reads the ACTIVE
    // column's store — this one), then the coalesce verdict, then THE COLLAPSE
    // + LAND that makes this a focus act. ITS refusals say NOTHING, the twins'
    // rule and for their reason (GuiOpRefusal, warpmarkers_ops.h).
    const PositionNudgePrologue pro = position_nudge_prologue(
        app, audio, playback_lifecycle, selection, viewport, undo,
        GestureKind::MagnificationLevelNudge, synthesized_repeat, step_columns);
    if (!pro.ok) return std::nullopt;
    const bool merge = pro.merge;
    const auto& mv = app.magnificationlevelmarkers.markers();
    const int   f  = pro.focused;   // validated in [0, mv.size()) by the prologue

    const int64_t orig_f = mv[f].time_frame;

    // THE WALL-REGIME MIDDLE, through the shared landing owner
    // (position_nudge_landing, position_nudge.h — the painted-column step, the
    // headroom clamp and the walls-win belt are all argued there). The
    // anchoring basis is the DISPLAYED map, so the moved marker travels exactly
    // the commanded pixel column against WHAT IS PAINTED; this column authors
    // in TARGET view alone, a mapped (non-identity) domain, exactly as the
    // phase-reset twin's home is.
    int64_t committed_f =
        position_nudge_landing(app, audio, orig_f, step_columns);
    // POST-CLAMP IDENTITY IS A SILENT NO-OP, the twins' rule verbatim: a press
    // already resting on its wall writes NOTHING — no undo push, no damage, no
    // playback stop — and says nothing, the unmoved flag being its own answer.
    if (committed_f == orig_f)
        return std::nullopt;
    const uint64_t prior_gain_hash = viewport.waveform_gain_hash();
    std::vector<GuiMagnificationLevelMarker> pre_state =
        app.magnificationlevelmarkers.markers();
    // Identity hint: the nudged marker in PRE-reorder snapshot coordinates (the
    // diff matcher is identity-blind for a column-snapped move).
    std::vector<int> touched_snapshot{f};
    if (GuiMagnificationLevelMarker* m =
            app.magnificationlevelmarkers.marker_mut(f))
        m->time_frame = committed_f;
    // A nudge may cross a neighbour; restore time order and remap the
    // index-shaped state (the selection follows its marker to the new slot).
    remap_marker_indices_after_reorder(
        app,
        reorder_markers_by_time(app.magnificationlevelmarkers.markers_mut()));
    // touched_live: the nudged marker's POST-reorder live index — read off the
    // selection, which the remap rewrote in place and which is exactly this one
    // marker (the prologue collapsed to it).
    std::vector<int> touched_live(app.selected_markers.begin(),
                                  app.selected_markers.end());
    // Coalesce a held key or button: the burst's OPENER pushed the pre-burst
    // snapshot with the identity hints, and each synthesized repeat behind it
    // REFRESHES the surviving entry's touched_live (the two-surface rule is
    // stated once at Undo::coalesce_gesture).
    if (merge) {
        undo.refresh_coalesced_touched_live(std::move(touched_live));
    } else {
        undo.push_undo_magnification_level(std::move(pre_state),
                                           std::move(touched_snapshot),
                                           std::move(touched_live));
    }
    // Shared commit tail: record/dirty/invalidate, playhead follow, the
    // at-working recentre, and the point command's region collapse. IT PASSES
    // A NULL GuiTargetRender — this column reaches no render input, so the
    // tail's (h) is skipped and the PICTURE's kick takes its place below (the
    // pointer's contract is at finish_position_nudge's declaration).
    finish_position_nudge(app, audio, viewport, undo,
                          GestureKind::MagnificationLevelNudge, merge,
                          committed_f, /*target_render=*/nullptr);
    viewport.kick_waveform_sync_if_gain_changed(prior_gain_hash);
    return std::nullopt;
}

// -- THE VALUE STEP'S LEVEL BODY (architect 2026-09-15) ---------------------
//
// The contract is at the declaration. It mirrors the measure step clause for
// clause — the leading refusal block the face reads, the lock asked HERE
// (because the plain wheel over an M flag reaches this body past no keyboard
// gate of its own), the wall asked through the directional face AHEAD OF THE
// COALESCE STAMP and silent, the write, one undo entry per burst, the
// byte-equal pop through record_gesture and the dirty re-derive — with TWO
// differences that are this column's: the step is SINGLETON AND GROUP with a
// per-member clamp (the header argues it, and there is no collapse to the focus
// here for that reason), and the tail owes the PICTURE a gain kick where the
// measure step owes nothing at all.
GuiOpRefusal GuiMagnificationLevelMarkersOps::adjust_magnification_level_step(
        int64_t delta, bool synthesized_repeat) {
    if (!magnification_level_step_actionable(app))
        return "Select a magnification level marker to change its level";
    if (const char* refusal = magnification_level_lock_refusal(app))
        return refusal;
    // THE WALL, silent and ahead of the stamp: Down with every selected marker
    // at 0, Up with every one at the maximum. An accepted step that lands where
    // it stands, so it spends the selection as the tempo step's wall does
    // (selection_consumed, app_state.h). NO KIND REFUSAL EXISTS ON THIS COLUMN:
    // every magnification level marker carries a level of its own — there is no
    // pass, no label ref and no offset form to refuse on — so the wall is the
    // whole of what the directional face answers.
    if (!magnification_level_step_direction_actionable(app, delta)) {
        selection_consumed(app);
        return std::nullopt;
    }
    const bool merge = undo.coalesce_gesture(
        GestureKind::MagnificationLevelStep, synthesized_repeat);
    const uint64_t prior_gain_hash = viewport.waveform_gain_hash();
    const std::vector<GuiMagnificationLevelMarker>& mv_const =
        app.magnificationlevelmarkers.markers();
    std::vector<GuiMagnificationLevelMarker> proposed = mv_const;
    bool changed = false;
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(proposed.size())) continue;
        GuiMagnificationLevelMarker& m = proposed[static_cast<std::size_t>(idx)];
        // A DISABLED MARKER STEPS, as it does under the tempo step and the
        // value drag: the disable bit says what the PICTURE reads, not what the
        // arrows may author.
        const int64_t landed = magnification_level_step_landing(
            static_cast<int64_t>(m.level), delta);
        if (landed == static_cast<int64_t>(m.level)) continue;   // its own wall
        m.level = static_cast<uint8_t>(landed);
        changed = true;
    }
    // Unreachable past the directional face above (which answers true exactly
    // when some member moves); kept as the family's no-change belt is, and
    // silent (GuiOpRefusal's contract).
    if (!changed) return std::nullopt;
    // THE ACCEPTED PATH SPENDS THE SELECTION, past the belt, so nothing reaches
    // this line but a step that moved a level (selection_consumed, app_state.h,
    // where the accepted-path rule and the whole inventory live).
    selection_consumed(app);
    std::vector<GuiMagnificationLevelMarker> pre_state = mv_const;
    app.magnificationlevelmarkers.markers_mut() = std::move(proposed);
    if (!merge) undo.push_undo_magnification_level(std::move(pre_state));
    // Settle the burst, POST-mutation: the stamp, or — on a merged press that
    // stepped the level back to the burst entry's own snapshot — the
    // BYTE-EQUAL POP of that entry (the rule is at Undo::record_gesture).
    undo.record_gesture(GestureKind::MagnificationLevelStep, merge);
    undo.recompute_dirty();
    // The marker lane repaints its digits, and the PICTURE repaints its gain:
    // nothing moved, so no stem moved and no image moved, but the section this
    // marker opens now carries another level.
    viewport.invalidate_top_strip();
    viewport.kick_waveform_sync_if_gain_changed(prior_gain_hash);
    return std::nullopt;
}
