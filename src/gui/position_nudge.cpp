#include "position_nudge.h"

#include "audio.h"
#include "input_handler.h"      // land_playhead_on_marker (the collapse's
                                // land, which owns the overlay hide)
#include "target_render.h"
#include "warp_frame_map_view.h"  // painted_column_of_source_frame,
                                  // authored_frame_at_column,
                                  // source_frame_to_active_domain

#include <cstdint>
#include <vector>

// The type-free flesh shared by the two position nudges
// (GuiWarpMarkersOps::nudge_selected_markers and
// GuiPhaseResetMarkersOps::nudge_selected_phase_resets). The full doctrine
// (horizontal movement is a focus act; the group-verb doctrine it instances) and
// the step-by-step ordering rationale live at the declarations in
// position_nudge.h; the wall-regime middles stay in each twin verbatim.

PositionNudgePrologue position_nudge_prologue(
    AppState& app, const GuiAudio& audio,
    GuiPlaybackLifecycle& playback_lifecycle, Selection& selection,
    Viewport& viewport, Undo& undo,
    GestureKind kind, bool synthesized_repeat, int store_size) {
    PositionNudgePrologue r;
    if (app.loading || audio.total_frames() <= 0) return r;
    if (app.selected_markers.empty()) return r;
    if (app.last_selected_marker < 0) return r;
    // Undo-coalescing decision, and its PLACEMENT IS LOAD-BEARING rather than
    // incidental since 2026-07-29: a PHYSICAL press
    // INVALIDATES the coalescing stamp inside this call — the derivation is at
    // Undo::coalesce_gesture — so it must run on every press that could otherwise
    // refuse and leave an older burst's stamp standing for a later press to
    // merge into (the reachable flip is the async waveform publish moving the
    // displayed map under the phase twin's wall test between the physical press and
    // that repeat). Hence it sits AHEAD OF EVERYTHING THAT CAN REFUSE ON GEOMETRY OR
    // WALLS: the sample-rate and samples-per-pixel guards below, the focused-index
    // belt, and each twin's own wall regime. What remains AHEAD of it is exactly the
    // trivial state guards (loading / empty audio / empty selection / no focus), and
    // those cannot flip mid-hold — every one of them needs a COMMAND to change, and a
    // command disarms the platform's repeat (layer (1) at maybe_fire_repeat), so no
    // repeat of this hold can survive to find the stamp. Both tempo-step arms are
    // ordered on the same rule. A held key's continuation presses carry
    // synthesized_repeat and merge by identity; a rapid MANUAL re-tap merges too,
    // on the tap arm's fixed window plus its subject test (architect 2026-08-01).
    // THE PLACEMENT SURVIVES THE HYBRID because coalesce_gesture computes its
    // verdict BEFORE it invalidates — so an early call still answers this press
    // correctly and still poisons the stamp for a press that goes on to refuse.
    // The SUBJECT the tap arm compares is read here, PRE-collapse, against what
    // record_gesture stamped POST-collapse in the tail; a steady run of taps has
    // already collapsed, so the two agree from the second press on.
    const bool merge = undo.coalesce_gesture(kind, synthesized_repeat);
    if (audio.sample_rate() <= 0) return r;
    if (current_samples_per_pixel(app, audio) <= 0.0) return r;
    const int focused = app.last_selected_marker;
    if (focused < 0 || focused >= store_size) return r;   // focused stale
    // HORIZONTAL MOVEMENT IS A FOCUS ACT (architect 2026-07-29): a 2+ selection
    // collapses to its focus here, and the playhead lands on that focus — the
    // Ctrl+N shape, the land sitting at the CALLER of collapse_to_focused because
    // the site that hands the marker lane a focus is the site that owes it a land.
    // The step every caller runs after this is therefore always the singleton op.
    // The tail's unconditional hide takes the trim region overlay with it.
    if (app.selected_markers.size() >= 2) {
        // THE COLLAPSE ARM'S STOP — the collapse-to-point class of the keyboard
        // stop rule (architect 2026-07-30, stated at stop_playback_if_playing's
        // declaration, playback_lifecycle.h), placed by that rule's refusal
        // gating: a REAL COLLAPSE is about to happen (the membership replace is a
        // write and the land moves the cursor), so the stop is owed HERE, past
        // every refusal above and immediately ahead of the first write. A
        // singleton press collapses nothing and stops nothing here — its own stop
        // sits in each twin, past the post-clamp identity check. The stop must
        // precede the land, which commits a new cursor position.
        playback_lifecycle.stop_playback_if_playing();
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
    int64_t orig_frame, int direction) {
    const int cf = painted_column_of_source_frame(
        app, audio, static_cast<double>(orig_frame), map);
    return authored_frame_at_column(app, audio, cf + direction, map);
}

void finish_position_nudge(
    AppState& app, const GuiAudio& audio, Viewport& viewport, Undo& undo,
    GestureKind kind, int64_t committed_focused_frame,
    GuiTargetRender& target_render) {
    // (a) re-stamp this press's kind for the next coalesce test.
    undo.record_gesture(kind);
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
    viewport.invalidate_status_bar_area();
    viewport.invalidate_clock_area();
    // (e) playhead follows the nudged marker's committed frame.
    viewport.move_playhead_to(
        source_frame_to_active_domain(app, audio, committed_focused_frame));
    // (f) A POSITION NUDGE HIDES the trim region overlay, unconditionally,
    // exactly like the marker click that would have selected that singleton,
    // and discarding nothing — the trim stands behind it. IT NEEDS NO CALL OF
    // ITS OWN since 2026-08-19: the follow at (e) goes through move_playhead_to
    // and the prologue's collapse through land_playhead_on_marker, and both are
    // movement owners that hide (the rule at clear_region_highlight,
    // input_handler.h). Groups are never
    // moved (the doctrine at the declarations), so there is no extent to
    // maintain here.
    // (g) view-independent target preview.
    target_render.trigger();
}
