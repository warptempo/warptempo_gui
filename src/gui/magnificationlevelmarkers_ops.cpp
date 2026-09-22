#include "magnificationlevelmarkers_ops.h"

#include "audio.h"
#include "position_nudge.h"  // the shared position-nudge flesh (prologue,
                             // step, commit tail) + the movement doctrine
#include "warp_frame_map_view.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

// THE MAGNIFICATION LEVEL AUTHORING CLUSTER — the phase-reset cluster's bodies
// over the third store, with this column's two deltas stated once at the
// header: NO GuiTargetRender (a level is display-only, so no act here may
// dispatch a preview) and THE PICTURE'S OWN GAIN KICK in its place. Every body
// below but the nudge captures the gain hash before its store write and hands
// it to kick_waveform_sync_if_gain_changed after; THE NUDGE ASKS THE DISPLAYED
// PLATE INSTEAD (architect 2026-09-16, at its tail), because its shared commit
// tail may already have rendered the new gain through the at-working
// held-column move, and a compare against the pre-write hash would render that same
// plate twice. The comments here carry only what is this column's.

namespace {

// THE LOCK'S SENTENCE, asked in this body because the plain wheel over an M
// flag reaches it past no keyboard gate of its own: the tab's
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

// THE LEVEL STEP'S KIND REFUSAL — the contract, the readers and the two-form
// split are at the declaration (app_state.h, the level-step block). The tempo
// pair's bodies over this column's cache (tempo_cent_step_kind_refusal_for /
// tempo_cent_step_kind_refusal, warpmarkers_ops.cpp), less the terms this
// column has no producer for: no view fork (the column exists in source view
// alone), no label-ref arm (no marker here names another), and no enabled
// test of its own — the cache's `collapsed` subset already holds the ENABLED
// members alone (magnification_level_red_flag_set_cached,
// warp_frame_map_view.h), so membership is the whole test and a disabled row
// of a collapsed run steps as on W.
const char* magnification_level_step_kind_refusal_for(const AppState& a,
                                                      int idx) {
    // The index is the MAGNIFICATION LEVEL store's: on the other two columns
    // the pair steps no level (the column gate,
    // magnification_level_step_actionable), so an index there names another
    // list and this refusal has nothing to say.
    if (a.active_markers_view != 'M') return nullptr;
    const auto& mv = a.magnificationlevelmarkers.markers();
    if (idx < 0 || idx >= static_cast<int>(mv.size())) return nullptr;
    const std::set<int>& collapsed =
        magnification_level_red_flag_set_cached(a).collapsed;
    return collapsed.count(idx) ? kCoincidentCollapseStepCard : nullptr;
}

const char* magnification_level_step_kind_refusal(const AppState& a) {
    // A GROUP PRESS IS NOT THIS REFUSAL'S BUSINESS — the group arm's own
    // refusal is the wall scan's, which asks the index form of every member —
    // so the short-circuit stays here, on the focus form, and does not travel
    // into the index body.
    if (a.selected_markers.size() >= 2) return nullptr;
    return magnification_level_step_kind_refusal_for(a, a.last_selected_marker);
}

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
    // nothing — but a drop LANDING ON AN EXISTING ENABLED MARKER'S FRAME can
    // move it (the run then holds two enabled rows and COLLAPSES TO THE NEUTRAL
    // LEVEL 0, magnificationlevelmarkers.h), and that is exactly the case the
    // hash catches without a rule of its own.
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
// the playhead stands on IS the boundary wanted. The column is source-view only
// (active_column_authoring_allowed's 'M' arm, architect 2026-09-16), so the
// cursor IS the source frame and the inverse below is the identity; it is
// asked UNCONDITIONALLY anyway — two compares, and the drop body then cannot
// drift from its siblings' shape (the phase column's drop asks the same
// conversion in the view where it is a real map).
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
// boundaries swapping which level holds between them, which the tail's plate
// check carries.
GuiOpRefusal
GuiMagnificationLevelMarkersOps::nudge_selected_magnification_levels(
        HorizontalArrowStep step, NudgeCamera camera,
        bool synthesized_repeat) {
    // Shared guard prologue: the WHOLE refusal set as one predicate (the Left /
    // Right buttons' own marker_nudge_actionable, which reads the ACTIVE
    // column's store — this one), then the coalesce verdict, then THE COLLAPSE
    // + LAND that makes this a focus act. ITS refusals say NOTHING, the twins'
    // rule and for their reason (GuiOpRefusal, warpmarkers_ops.h).
    const PositionNudgePrologue pro = position_nudge_prologue(
        app, audio, playback_lifecycle, selection, viewport, undo,
        GestureKind::MagnificationLevelNudge, synthesized_repeat, step);
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
    // in SOURCE view alone (architect 2026-09-16), where that basis is the
    // identity — the warp twin's home — while the phase-reset twin's home is
    // a real map; the shared owner asks the displayed basis either way.
    int64_t committed_f =
        position_nudge_landing(app, audio, orig_f, step);
    // POST-CLAMP IDENTITY IS A SILENT NO-OP, the twins' rule verbatim: a press
    // already resting on its wall writes NOTHING — no undo push, no damage, no
    // playback stop — and says nothing, the unmoved flag being its own answer.
    if (committed_f == orig_f)
        return std::nullopt;
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
    // press's camera (NudgeCamera), and the point command's region collapse. IT PASSES
    // A NULL GuiTargetRender — this column reaches no render input, so the
    // tail's (h) is skipped and the PICTURE's repayment takes its place below
    // (the pointer's contract is at finish_position_nudge's declaration).
    finish_position_nudge(app, audio, viewport, undo,
                          GestureKind::MagnificationLevelNudge, merge,
                          orig_f, committed_f, camera,
                          /*target_render=*/nullptr);
    // THE PICTURE IS REPAID ONLY WHERE THE DISPLAYED PLATE IS STALE (architect
    // 2026-09-16, "prefer the correct way"): the M drag's release rule
    // (MarkerDragOps::commit_drag's tail) rather than the cluster's pre-write
    // hash compare. The tail above may already have rendered the new gain —
    // the edge-align of (e) or, under Ctrl, the held-column move
    // (hold_subject_column_after_nudge) may shift the viewport, whose synchronous kick reads the committed
    // store's profile and publishes the displayed fingerprint with it — and a
    // compare against the hash captured BEFORE the write would then render
    // that same plate a second time, synchronously, on every held repeat that
    // moved a breakpoint. Asking the DISPLAYED plate's own gain fingerprint
    // (Viewport::displayed_plate_gain_is_stale — its published gain hash
    // against the live effective profile's) answers both halves at once: a
    // camera move that rendered leaves the fingerprint current and nothing more
    // is owed; a hold whose offset already matched (or a bare press's
    // edge-align that found the subject already on screen) moves no viewport
    // and leaves it stale exactly when the profile moved — at every zoom,
    // since the hold and the edge-align both run there now (architect
    // 2026-09-22, the placement-instrument zoom gate retired). No hash fallback is kept
    // for the unwired case: the predicate is wired in main.cpp ahead of the
    // loop, and the drag's release relies on the same wiring; with no plate
    // displayed it answers false and the tick's dirty-detect renders the first
    // plate, as it does for the drag.
    if (viewport.displayed_plate_gain_is_stale()) viewport.kick_waveform_sync();
    return std::nullopt;
}

// -- THE VALUE STEP'S LEVEL BODY (architect 2026-09-15) ---------------------
//
// The contract is at the declaration. It mirrors the tempo step's singleton
// arm clause for clause — the leading refusal block the face reads, the lock asked HERE
// (because the plain wheel over an M flag reaches this body past no keyboard
// gate of its own), the wall asked AHEAD OF THE COALESCE STAMP through the
// pair's own face, the write, one undo entry per burst, the byte-equal pop
// through record_gesture and the dirty re-derive — with TWO differences that
// are this column's: the step is SINGLETON AND GROUP, forking exactly as the
// TEMPO step forks (architect 2026-09-16 — the singleton's clamp silent, the
// group all-or-nothing and carded; there is no collapse to the focus, a value
// step never being a movement), and the tail owes the PICTURE a gain kick and
// no tempo tail — a level is not a map input.
GuiOpRefusal GuiMagnificationLevelMarkersOps::adjust_magnification_level_step(
        int64_t delta, bool synthesized_repeat) {
    if (!magnification_level_step_actionable(app))
        return "Select a magnification level marker to change its level";
    if (const char* refusal = magnification_level_lock_refusal(app))
        return refusal;
    // THE KIND REFUSAL AND THE WALL, ahead of the stamp and FORKING WHERE THE
    // TEMPO STEP FORKS (architect 2026-09-16). ONE KIND REFUSAL EXISTS ON THIS
    // COLUMN SINCE 2026-09-16, THE COLLAPSE (magnification_level_step_kind_refusal,
    // app_state.h — the tempo step's own collapse refusal read onto this
    // column): an enabled member of a run the picture collapses to level 0
    // refuses on a CARD, because coincidence is never intentional (architect
    // 2026-09-13) and a collapsed member's digit is picture-inert — the run
    // reads as 0 whatever it says. Nothing else refuses on kind: every
    // magnification level marker carries a level of its own — no pass, no
    // label ref, no offset form — so past the collapse the level bracket is
    // the whole of what either arm answers.
    //
    // THE GROUP ARM IS THE TEMPO GROUP'S ALL, replacing the per-member clamp of
    // 2026-09-15: ANY member that cannot take the WHOLE step — a collapsed
    // member, or one that would leave the bracket — refuses the WHOLE press,
    // before any level changes, and IT SAYS SO — a group step would have
    // moved every selected flag's digit, so it is not the one-dimensional
    // already-at-its-state refusal that goes silent, and the sentence names the
    // group rather than the member for the tempo card's reason (what the press
    // needs to know is that the group could not move as a group). The refusal
    // SPENDS NOTHING, the branch-local rule at selection_consumed.
    if (app.selected_markers.size() >= 2) {
        if (!magnification_level_step_group_actionable(app, delta))
            return "One of the selected markers cannot take this magnification "
                   "level change";
    } else {
        // THE SINGLETON'S KIND REFUSAL IS ASKED FIRST, the tempo singleton's
        // order: it runs AHEAD OF THE WALL because the directional face below
        // answers false on both and the wall's exit is silent — asked the
        // other way round, a collapsed member resting on a bracket end would
        // fall into that exit and lose its sentence. Before any mutation, and
        // spending nothing. The plain wheel over an M flag reaches this same
        // line and drops the sentence (run_flag_cell_wheel), silent as the
        // tempo wheel is.
        if (const char* refusal = magnification_level_step_kind_refusal(app))
            return refusal;
        if (!magnification_level_step_direction_actionable(app, delta)) {
            // THE SINGLETON'S CLAMP IS SILENT, the tempo singleton's
            // verbatim: Down with the marker at 0, Up with it at the maximum
            // is an accepted step that lands where it stands — a benign
            // one-dimensional refusal already at its state, the flag's own
            // digit being the one place to glance — so it spends the
            // selection and says nothing (selection_consumed, app_state.h).
            // The kind refusal was asked just above and passed, so what this
            // refuses on is the wall alone.
            selection_consumed(app);
            return std::nullopt;
        }
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
        // A BELT SINCE 2026-09-16, not a branch: the group scan passed means
        // every member takes the WHOLE step, and the singleton arm's face
        // refused a member resting on its wall, so nothing that reaches this
        // loop clamps. It was the per-member wall while the group arm was an
        // ANY.
        if (landed == static_cast<int64_t>(m.level)) continue;
        m.level = static_cast<uint8_t>(landed);
        changed = true;
    }
    // Unreachable past the two arms above (the group scan answers true only
    // when every member moves, the singleton face only when the one member
    // does); kept as the family's no-change belt is, and silent (GuiOpRefusal's
    // contract).
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
