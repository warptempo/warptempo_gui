#include "playback_lifecycle.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

// Gesture-stop: called at the top of any handler that will move the
// visible playhead (keys, button press, Ctrl+wheel, undo/redo, tab
// switch). Stops the audio thread and keeps the LSP in sync with the
// visible playhead so the next Space-to-play captures the right
// launch position. Does NOT return-to-launch — the gesture is about
// to commit a new playhead position.
void GuiPlaybackLifecycle::stop_playback_if_playing() {
    if (!playback.is_playing() && !app.is_playing) return;
    playback.stop();
    app.is_playing        = false;
    app.last_space_sample = app.playhead_sample;
}

// Snap the visible playhead back to where Space was last pressed and
// refresh the affected regions. Used by both Space-to-stop and natural
// end-of-playback.
void GuiPlaybackLifecycle::restore_playhead_to_lsp() {
    const double old_px = playhead_pixel_x(app, audio);
    app.playhead_sample = app.last_space_sample;
    const double new_px = playhead_pixel_x(app, audio);
    viewport.invalidate_playhead_columns(old_px, new_px);
    viewport.invalidate_timestamp_area();
    // The triangle shares the top strip with any selected-flag
    // highlight; restore jumps can uncover/cover both, so invalidate
    // the flag strip too.
    const GuiRect ts = top_strip_area(app);
    gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
    app.is_playing      = false;
    app.playback_cursor = app.playhead_sample;
}

// Space-bar: start/stop playback. Playback runs from the playhead to
// trim_end (or total_frames if no e= marker). Pressing space with the
// playhead at or past trim-end is a silent no-op. Space-to-stop
// returns the visible playhead to the position where Space-to-play
// was last pressed (return-to-launch).
void GuiPlaybackLifecycle::toggle_playback() {
    if (playback.is_playing()) {
        playback.stop();
        restore_playhead_to_lsp();
        return;
    }
    const int64_t end = viewport.trim_end_sample();
    if (app.playhead_sample >= end) return;
    // Clamp the start position into the trim range in case the playhead
    // is sitting at trim_end - 1 (valid) or somehow slipped.
    const int64_t start = std::max(app.playhead_sample, viewport.trim_begin_sample());
    app.last_space_sample = app.playhead_sample;
    app.playback_cursor = start;
    app.is_playing = true;
    if (app.follow_mode) viewport.follow_scroll_if_needed();
    playback.set_speed(app.render_view_enabled ? 1.0f : app.playback_speed);
    playback.play(start, end);
}

// Helpers for Shift+<digit> speed selection.
void GuiPlaybackLifecycle::set_playback_speed(float s) {
    app.playback_speed = s;
    playback.set_speed(s);
    // Speed change without resync would cause a backward cursor jump:
    // the predictor would retroactively apply the new speed to the
    // entire elapsed-since-anchor period.
    if (playback.is_playing()) playback.resync_predictor();
}
