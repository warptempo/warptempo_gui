#include "group_position_nudge.h"

#include "audio.h"
#include "input_handler.h"      // set_region_to_selection_extent (region follow)
#include "target_render.h"
#include "warp_frame_map_view.h"  // painted_column_of_source_frame,
                                  // authored_frame_at_column,
                                  // source_frame_to_active_domain

#include <cstdint>
#include <vector>

// The type-free flesh shared by the two group position nudges
// (GuiWarpMarkersOps::nudge_selected_markers and
// GuiPhaseResetMarkersOps::nudge_selected_phase_resets). The full doctrine and
// the step-by-step ordering rationale live at the declarations in
// group_position_nudge.h; the wall-regime middles stay in each twin verbatim.

GroupNudgePrologue group_position_nudge_prologue(
    AppState& app, const GuiAudio& audio,
    GuiPlaybackLifecycle& playback_lifecycle, Undo& undo,
    GestureKind kind, int store_size) {
    GroupNudgePrologue r;
    if (app.loading || audio.total_frames() <= 0) return r;
    // Fine-tuning authoring gesture: stop playback first (an empty-selection
    // press in home view still stops playback today — preserve).
    playback_lifecycle.stop_playback_if_playing();
    if (app.selected_markers.empty()) return r;
    if (app.last_selected_marker < 0) return r;
    // Undo-coalescing decision (a const query); computed before the geometry
    // refusals below, which is today's order in both twins.
    const bool merge = undo.coalesce_gesture(kind);
    if (audio.sample_rate() <= 0) return r;
    if (current_samples_per_pixel(app, audio) <= 0.0) return r;
    // Stale-index belt: every selected index must be in [0, store_size).
    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= store_size) return r;
    }
    const int focused = app.last_selected_marker;
    if (focused < 0 || focused >= store_size) return r;   // focused stale
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

void finish_group_position_nudge(
    AppState& app, const GuiAudio& audio, Viewport& viewport, Undo& undo,
    GestureKind kind, int64_t committed_focused_frame,
    GuiTargetRender& target_render) {
    // (a) stem lateral-gesture pin (see the declaration for the full edge).
    app.stem_pin_marker      = app.last_selected_marker;
    app.stem_pin_command_seq = app.command_seq;
    // (b) re-record with the post-mutation selection for the next coalesce test.
    undo.record_gesture(kind);
    // (c) dirty flags.
    undo.recompute_dirty();
    // (d, e) damage.
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    // (f) playhead follows the FOCUSED item's committed frame.
    viewport.move_playhead_to(
        source_frame_to_active_domain(app, audio, committed_focused_frame));
    // (g) region follow (SelectionExtent only; MAINTAIN, never CREATE).
    if (app.region.active &&
        app.region.provenance == RegionProvenance::SelectionExtent) {
        set_region_to_selection_extent(app, audio, viewport);
    }
    // (h) view-independent target preview.
    target_render.trigger();
}
