#include "position_nudge.h"

#include "audio.h"
#include "input_handler.h"      // clear_region_highlight (the point-command
                                // collapse), land_playhead_on_marker (the
                                // collapse's land)
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
    // Fine-tuning authoring gesture: stop playback first (an empty-selection
    // press in home view still stops playback today — preserve).
    playback_lifecycle.stop_playback_if_playing();
    if (app.selected_markers.empty()) return r;
    if (app.last_selected_marker < 0) return r;
    // Undo-coalescing decision (a const query); computed before the geometry
    // refusals below, which is today's order in both twins. A held key's
    // continuation presses carry synthesized_repeat and merge into the entry the
    // physical press pushed.
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
    // The collapse's own membership replace takes the group's SelectionExtent span
    // (clear_region_on_membership_replace), and the tail's unconditional clear
    // takes anything else resting.
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
    // (c, d) damage. The full-waveform damage here also owns the selected-marker
    // stem's move: a nudge shifts the nudged marker's frame, so its always-on
    // focus stem repaints at the new column on this repaint.
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    // (e) playhead follows the nudged marker's committed frame.
    viewport.move_playhead_to(
        source_frame_to_active_domain(app, audio, committed_focused_frame));
    // (f) A POSITION NUDGE IS A POINT COMMAND — one flag standing in for the
    // cursor — so it collapses any resting span, unconditionally and blind to
    // provenance, exactly like the marker click that would have selected that
    // singleton. Groups are never moved (the doctrine at the declarations), so
    // there is no span to maintain here.
    clear_region_highlight(app, viewport);
    // (g) view-independent target preview.
    target_render.trigger();
}
