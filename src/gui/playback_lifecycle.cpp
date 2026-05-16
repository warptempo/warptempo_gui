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
//
// Target-view branch: the audio device is bound to app.target_buffer
// (rebound by GuiTargetRender::on_render_done on Success). The
// playhead in target view is a full-target-frame coordinate; the
// target buffer is indexed [0, target_buffer_frames) and
// represents the target-domain range starting at
// target_buffer_start_frame. Translate the bounds at the
// dispatch site so playback.play()'s frame indices land inside the
// target buffer. Speed is forced to 1.0 here (sibling case to
// render view): target view's whole purpose is that audio plays the
// user's authored warp, so an extra speed multiplier on top would
// defeat the brief. The persistent app.playback_speed is left
// untouched; toggling back to source view restores it naturally
// because set_playback_speed refuses writes in target view.
void GuiPlaybackLifecycle::toggle_playback() {
    if (playback.is_playing()) {
        playback.stop();
        restore_playhead_to_lsp();
        return;
    }
    int64_t start;
    int64_t end;
    if (app.view_domain == ViewDomain::Target &&
        !app.render_view_enabled) {
        // Target view: the target buffer is the live playback source.
        // Refuse if no successful target render has populated it yet
        // — Space's outer gate in input_handler.cpp already checks this,
        // but stay defensive here so a future caller can't slip through.
        if (app.target_buffer_frames <= 0) return;
        const int64_t bias = app.target_buffer_start_frame;
        const int64_t local = app.playhead_sample - bias;
        // Playhead outside the target buffer's target-domain extent
        // is a silent no-op. Mirrors the "playhead at or past trim_end
        // is a silent no-op" pattern below for source view.
        if (local < 0) return;
        if (local >= app.target_buffer_frames) return;
        start = local;
        end   = app.target_buffer_frames;
    } else {
        end = viewport.trim_end_sample();
        if (app.playhead_sample >= end) return;
        // Clamp the start position into the trim range in case the
        // playhead is sitting at trim_end - 1 (valid) or somehow slipped.
        start = std::max(app.playhead_sample, viewport.trim_begin_sample());
    }
    app.last_space_sample = app.playhead_sample;
    app.playback_cursor = start;
    app.is_playing = true;
    if (app.follow_mode) viewport.follow_scroll_if_needed();
    const bool force_one_x =
        app.render_view_enabled || app.view_domain == ViewDomain::Target;
    playback.set_speed(force_one_x ? 1.0f : app.playback_speed);
    playback.play(start, end);
}

// Helpers for Shift+<digit> speed selection. Refused silently in target
// view: target view's audio is the warped target buffer, and adding
// a playback-speed multiplier on top would defeat the brief's whole
// premise (the audible result must match the authored warp, not the
// warp scaled by an extra factor). The persistent app.playback_speed
// is left untouched so a T→S round trip restores whatever rate the
// user last set in source view. This is the only path that writes
// app.playback_speed, so the refusal here is sufficient.
void GuiPlaybackLifecycle::set_playback_speed(float s) {
    if (app.view_domain == ViewDomain::Target) return;
    app.playback_speed = s;
    playback.set_speed(s);
    // Speed change without resync would cause a backward cursor jump:
    // the predictor would retroactively apply the new speed to the
    // entire elapsed-since-anchor period.
    if (playback.is_playing()) playback.resync_predictor();
}
