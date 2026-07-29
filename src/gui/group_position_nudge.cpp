#include "group_position_nudge.h"

#include "audio.h"
#include "input_handler.h"      // set_region_to_selection_extent (region follow),
                                // clear_region_highlight (singleton collapse)
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
    GestureKind kind, bool synthesized_repeat, int store_size) {
    GroupNudgePrologue r;
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
    // (a) re-stamp this press's kind for the next coalesce test.
    undo.record_gesture(kind);
    // (b) dirty flags.
    undo.recompute_dirty();
    // (c, d) damage. The full-waveform damage here also owns the selected-marker
    // stem's move: a nudge shifts the focused SINGLETON's frame, so its always-on
    // focus stem repaints at the new column on this repaint.
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    // (e) playhead follows the FOCUSED item's committed frame.
    viewport.move_playhead_to(
        source_frame_to_active_domain(app, audio, committed_focused_frame));
    // (f) the region, by the playhead's two forms. A GROUP nudge is a SPAN
    // gesture: an active SelectionExtent highlight FOLLOWS the moved group
    // (MAINTAIN, never CREATE), and a resting TrimWindow highlight is left
    // exactly alone — it still shows the trim correctly, the bounds being source
    // frames that a W+source nudge does not touch and a P+target nudge cannot
    // reach (the warp map is what places them, and phase resets do not warp).
    // A SINGLETON nudge is a POINT command — one flag standing in for the cursor
    // — so it collapses any resting span instead, unconditionally, exactly like
    // the marker click that would have selected that singleton.
    if (app.region.active &&
        app.region.provenance == RegionProvenance::SelectionExtent) {
        set_region_to_selection_extent(app, audio, viewport);
    } else if (app.selected_markers.size() < 2) {
        clear_region_highlight(app, viewport);
    }
    // (g) view-independent target preview.
    target_render.trigger();
}
