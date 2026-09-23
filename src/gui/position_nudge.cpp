#include "position_nudge.h"

#include "audio.h"
#include "input_handler.h"      // land_playhead_on_marker (the collapse's
                                // land)
#include "target_render.h"
#include "warp_frame_map_view.h"  // painted_column_of_source_frame,
                                  // authored_frame_at_column,
                                  // source_frame_to_active_domain

#include <cstdint>
#include <vector>

// The type-free flesh shared by the TWO position nudges
// (GuiWarpMarkersOps::nudge_selected_markers and
// GuiPhaseResetMarkersOps::nudge_selected_phase_resets). The
// full doctrine
// (horizontal movement is a focus act; the group-verb doctrine it instances) and
// the step-by-step ordering rationale live at the declarations in
// position_nudge.h. THE WALL-REGIME MIDDLE IS SHARED FLESH TOO since
// 2026-08-31 — position_nudge_landing below, one owner for every column, the
// twins' two verbatim copies collapsed into it when the Left / Right buttons'
// face needed the landing as a const owner to compare against. What stays in
// each twin is its own STORE read and its post-clamp identity no-op; the stop
// lives in the prologue.

PositionNudgePrologue position_nudge_prologue(
    AppState& app, const GuiAudio& audio,
    GuiPlaybackLifecycle& playback_lifecycle, Selection& selection,
    Viewport& viewport, Undo& undo,
    GestureKind kind, bool synthesized_repeat, HorizontalArrowStep step) {
    PositionNudgePrologue r;
    // EVERY REFUSAL HERE IS SILENT, AND EACH FOR ITS OWN REASON (re-derived
    // 2026-08-30 under the strictness ruling "a card for every silent
    // refusal" — none of these is one): the loading / empty-audio pair is the
    // on_key loading gate's card one level up; the empty selection is
    // unreachable, the dispatcher routing an empty selection to the
    // waveform-lane playhead step and never reaching here; the missing focus
    // is a belt against the never-parked rule (the selection layer keeps the
    // focus a member at every mutator); and the geometry terms are belts
    // against degenerate state. An error arm exists iff a producer exists
    // (validation_topology.md), so the reason channel this pair's callers use
    // (GuiOpRefusal, warpmarkers_ops.h) carries nothing from here — and
    // nothing from the WALL either since 2026-08-31, when that one-day card
    // retired into a silence with a greyed button beside it.
    //
    // THE WHOLE REFUSAL SET IS ONE PREDICATE, AND IT IS THE FACE'S OWN
    // (2026-08-31, converting codex round A's MED finding): the seven guards
    // this prologue used to spell — loading, empty audio, empty selection, no
    // focus, a dead sample rate, a degenerate samples-per-pixel, a stale
    // focused index — plus each twin's WALL are exactly the terms of
    // marker_nudge_actionable (app_state.h), the Left / Right buttons' own
    // marker-lane arm, so the press asks the button's question rather than a
    // second spelling of it. The store the predicate reads is the ACTIVE
    // column's, which IS the calling twin's store: the one dispatch that
    // reaches either twin picks it by app.active_markers_view
    // (input_handler.cpp's marker-lane branch), so the index belt is the same
    // belt it always was.
    // AND ITS PLACEMENT AHEAD OF THE COALESCE CALL IS THE POINT: a WALL NO-OP
    // TOUCHES NOTHING (architect's arc logic, planner-ruled 2026-08-31). A
    // physical press invalidates the coalescing stamp inside coalesce_gesture,
    // so a wall test BEHIND that call made a refused KEY press split an undo
    // run while the same press on the now-GREYED button never dispatched and
    // left the run whole — pointer and keyboard coalescing differently at the
    // same wall. The discriminator is the FACE, not the card: a refusal the
    // button greys on runs BEFORE the stamp (neither surface touches it), a
    // refusal that keeps a live face stays behind it (both surfaces poison
    // alike). Every term here greys, so the whole set moved ahead. The
    // supersession of the "an early call must poison for a press that goes on
    // to refuse" clause is recorded at Undo::coalesce_gesture.
    // AND IT IS ASKED OF THIS PRESS'S OWN STEP, not of a bare sign: the
    // predicate takes the step in its unit and the act hands it the one it is
    // about to commit — the same step the FACE hands it, both asking
    // horizontal_arrow_step for the active column (gui_input.h).
    if (!marker_nudge_actionable(app, audio, step)) return r;
    // The undo-coalescing verdict, now the FIRST thing past the refusals. It
    // reads the press's own repeat bit to pick its arm — a held key's
    // continuation presses carry synthesized_repeat and merge by identity, a
    // rapid MANUAL re-tap merges on the tap arm's fixed window plus its
    // subject test (architect 2026-08-01) — and it just has to run before
    // record_gesture stamps the burst in the tail.
    // IT STAYS AHEAD OF THE COLLAPSE, and that is load-bearing: the SUBJECT
    // the tap arm compares is read here, PRE-collapse, against what
    // record_gesture stamped POST-collapse in the tail; a steady run of taps
    // has already collapsed, so the two agree from the second press on.
    const bool merge = undo.coalesce_gesture(kind, synthesized_repeat);
    const int focused = app.last_selected_marker;
    // HORIZONTAL MOVEMENT IS A FOCUS ACT (architect 2026-07-29): a 2+ selection
    // collapses to its focus here, and the playhead lands on that focus — the
    // Ctrl+N shape, the land sitting at the CALLER of collapse_to_focused because
    // the site that hands the marker lane a focus is the site that owes it a land.
    // The step every caller runs after this is therefore always the singleton op.
    // The tail's unconditional hide takes the trim region overlay with it.
    // THE PRESS'S STOP — the collapse-to-point class of the keyboard stop rule
    // (architect 2026-07-30, stated at stop_playback_if_playing's declaration,
    // playback_lifecycle.h), placed by that rule's refusal gating: past
    // marker_nudge_actionable EVERY PRESS ACTS — a 2+ press collapses and lands
    // below, and a singleton's wall was the predicate's last term, so its step
    // moves the marker (a landing equals its origin only at the wall the step
    // points into, at any zoom) — and this is immediately ahead of the first
    // write. ONE stop for both shapes; it precedes the land, which commits a
    // new cursor position.
    playback_lifecycle.stop_playback_if_playing();
    if (app.selected_markers.size() >= 2) {
        selection.collapse_to_focused();
        land_playhead_on_marker(app, audio, viewport, focused);
    }
    r.ok      = true;
    r.merge   = merge;
    r.focused = focused;
    return r;
}

int64_t stepped_anchor_frame(
    const AppState& app, const GuiAudio& audio,
    const std::vector<WarpFrameMapSegment>& map,
    int64_t orig_frame, int step_columns) {
    const int cf = painted_column_of_source_frame(
        app, audio, static_cast<double>(orig_frame), map);
    return authored_frame_at_column(app, audio, cf + step_columns, map);
}

int64_t position_nudge_landing(const AppState& app, const GuiAudio& audio,
                               int64_t orig_frame, HorizontalArrowStep step) {
    // The prologue's geometry refusals, asked here so a caller that has NOT
    // run the prologue — the buttons' face — gets the same answer the press
    // would give: nothing moves (the declaration says why).
    if (audio.total_frames() <= 0 || audio.sample_rate() <= 0) return orig_frame;
    if (current_samples_per_pixel(app, audio) <= 0.0) return orig_frame;
    const int64_t wall = audio.total_frames() - 1;
    int64_t D = 0;
    if (step.unit == HorizontalArrowStep::Unit::Hops) {
        // (1') THE HOP, the phase-reset column's arrow unit: `count` hops
        // of the engine's analysis lattice through the one hop owner the
        // iteration cells land through, under the LIVE map — the render's,
        // because a hop is the render's quantum and not a painted one. It may
        // answer a frame off the piece (below 0 when no window lies that far
        // left, past the end beyond the map's last anchor); the clamp below
        // is what lands it on the wall.
        D = phase_reset_hop_step_frame(orig_frame, step.count,
                                       live_warp_frame_map(app, audio)) -
            orig_frame;
    } else {
        // The anchoring map is the DISPLAYED paint basis —
        // displayed_or_live_target_map, the SAME map the flag/trim painters
        // read — so the moved marker travels exactly the commanded pixel
        // column against WHAT IS PAINTED, even inside a worker publish window
        // where the displayed map lags the live one. In warp's SOURCE home
        // that map is the empty identity map and every commit is a plain
        // integer frame; in phase's TARGET home it is a real map.
        const std::vector<WarpFrameMapSegment>& map =
            displayed_or_live_target_map(app, audio);
        // (1) the commanded painted columns, as a plain integer delta.
        D = stepped_anchor_frame(app, audio, map, orig_frame, step.count) -
            orig_frame;
    }
    // (2) walls win by clamping, in this marker's own headroom.
    if (D < -orig_frame)        D = -orig_frame;
    if (D > wall - orig_frame)  D = wall - orig_frame;
    // (3) the walls-win belt on the sum.
    int64_t committed = orig_frame + D;
    if (committed < 0)     committed = 0;
    if (committed > wall)  committed = wall;
    return committed;
}

// THE MARKER LANE'S WALL TERM FOR THE LEFT / RIGHT BUTTONS (architect
// 2026-08-31, R3): declared in app_state.h, where the face reads it, and
// defined here beside the landing it compares. The order of its terms is the
// PROLOGUE'S OWN, so the face and the press agree at every one of them; the
// full reasoning — why a 2+ selection stays lit, why the geometry guards are
// terms, and why the step the face hands it is the press's own — is at the
// declaration.
bool marker_nudge_actionable(const AppState& a, const GuiAudio& audio,
                             HorizontalArrowStep step) {
    if (a.loading || audio.total_frames() <= 0) return false;
    if (!marker_selection_standing(a)) return false;
    if (!marker_focus_standing(a)) return false;
    if (audio.sample_rate() <= 0) return false;
    if (current_samples_per_pixel(a, audio) <= 0.0) return false;
    const int f = a.last_selected_marker;
    if (f >= active_marker_count(a)) return false;   // the focused-index belt
    // A GROUP PRESS COLLAPSES AND LANDS before any wall is consulted, so it
    // always changes the screen: horizontal movement is a focus act (the
    // doctrine at the head of position_nudge.h) and the collapse is the
    // press's own committed act, not a prelude to the step.
    if (a.selected_markers.size() >= 2) return true;
    // The active column's store through its one selector (app_state.h), which
    // answers each column's own frame.
    const int64_t orig = active_marker_time_frame(a, f);
    return position_nudge_landing(a, audio, orig, step) != orig;
}

void finish_position_nudge(
    AppState& app, const GuiAudio& audio, Viewport& viewport, Undo& undo,
    GestureKind kind, bool merged, int64_t prior_focused_frame,
    int64_t committed_focused_frame, NudgeCamera camera,
    GuiTargetRender& target_render) {
    // (a) settle the burst: re-stamp this press's kind for the next coalesce
    // test, or — on a MERGED press whose mutation returned the stores to the
    // burst entry's own snapshot — POP that entry, the byte-equal pop
    // (Undo::record_gesture owns the rule; `merged` is the verdict the
    // prologue's coalesce_gesture returned for this same press, carried down
    // the twin). It runs POST-mutation because that is where the answer exists.
    undo.record_gesture(kind, merged);
    // (b) dirty flags.
    undo.recompute_dirty();
    // (c, d) damage, and it is FULL: invalidate_waveform_area spans y=0 through
    // the waveform's bottom across the whole window width, so ONE call covers
    // both halves of a moved marker — the flag box (a top-strip pixel, blitted
    // from the flag cache) and its stem (a waveform pixel, painted live from the
    // same pass's stash). That pairing is why the call cannot narrow: row 5 made
    // the stem a waveform pixel, so a strip-only damage here would repaint the
    // flag at its new column while the old stem ink stayed on the plate until
    // some later full-area damage arrived. Verified 2026-08-01 (both nudge twins
    // reach here; the marker-moving routes were re-grepped against this rule and
    // every one of them — drop, delete, the disabled/inherits toggles, both
    // tempo steps, the drag commit, the flag-editor commit, the propagate paste,
    // the render-entry load-in-place and the undo restores — pays the same
    // full call).
    viewport.invalidate_waveform_area();
    viewport.invalidate_clock_area();
    // (e) playhead follows the nudged marker's committed frame through the
    // movement owner. THE SUBJECT'S PRIOR PLACE is captured first, for (f): the
    // focused marker's pre-write frame in the active domain (the identity in
    // source view; a target-view nudge exists only on the P column, which is
    // no map input, so the map translating it is the one it painted under) and
    // the viewport it painted on — (e)'s keep-visible edge-align may scroll
    // that viewport, and nothing between the twin's capture of the frame and
    // here moves it.
    const int64_t prior_subject_sample =
        source_frame_to_active_domain(app, audio, prior_focused_frame);
    const int64_t prior_viewport_start = app.viewport_start_sample;
    viewport.move_playhead_to(
        source_frame_to_active_domain(app, audio, committed_focused_frame));
    // (f) THE CAMERA IS THE PRESS'S (NudgeCamera, gui_input.h — at every zoom
    // and on every column; the hold posture's answer since 2026-09-23,
    // nudge_camera, read at the dispatch before this act and kept across it):
    // with the posture dark the press follows the edge and needs nothing
    // beyond (e)'s own keep-visible edge-align; with it standing the press
    // holds the column — the viewport is placed so the playhead (e)
    // just landed on the nudged marker paints in the column the marker
    // painted in before the nudge, clamped to the waveform's edge columns — at
    // every step, a held key's repeats and a held button's fires included,
    // because each of them runs this tail. This tail is the nudge's CHANGED
    // path (each twin returns on its post-clamp identity no-op before reaching
    // it), so a walled press moves no camera — a 2+ press walled after its
    // collapse keeps the collapse and the land, and nothing more. The hold's
    // rule and body are at Viewport::hold_subject_column_after_nudge.
    if (camera == NudgeCamera::HoldColumn)
        viewport.hold_subject_column_after_nudge(prior_subject_sample,
                                                 prior_viewport_start);
    // (g) A POSITION NUDGE ENDS AN A/B AUDITION, needing no call of its own:
    // the follow at (e) and the prologue's collapse land both go through a
    // movement owner (the rule at Viewport::move_playhead_to, viewport.cpp).
    // Groups are never moved (the doctrine at the declarations), so there is
    // no extent to maintain here.
    // (h) view-independent target preview.
    target_render.trigger();
}
