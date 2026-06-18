#include "playback_lifecycle.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

// Gesture-stop: called at the top of any handler that will move the
// cursor (keys, button press, Ctrl+wheel, undo/redo, tab switch).
// Stops the audio thread and restores the split-playhead invariant:
// when the scanner is inactive its sample equals the cursor's sample.
// The cursor is not touched here — the caller is about to commit a new
// cursor position. The scanner's last-painted column must be invalidated
// here regardless of what the caller does next: the caller cares about
// the cursor, but the scanner has its own visible identity that this
// function is responsible for tearing down.
void GuiPlaybackLifecycle::stop_playback_if_playing() {
    if (!playback.is_playing() && !app.playhead_scanner_active) return;
    const double scanner_px = scanner_pixel_x(app, audio);
    const double cursor_px  = playhead_pixel_x(app, audio);
    playback.stop();
    app.playhead_scanner_active = false;
    app.playhead_scanner_sample = app.playhead_cursor_sample;
    viewport.invalidate_playhead_columns(scanner_px, cursor_px);
    viewport.invalidate_timestamp_area();
    app.follow_overridden_for_session = false;
}

// End scanner motion and restore the invariant. Used by Space/Enter to
// stop and by natural end-of-playback. The cursor never moved during
// playback (the predictor only writes the scanner), so the only work
// here is to deactivate the scanner and snap it back onto the cursor.
// Invalidate the span between the scanner's last-painted column and the
// cursor's column so both repaint cleanly.
void GuiPlaybackLifecycle::restore_playhead_to_lsp() {
    const double scanner_px = scanner_pixel_x(app, audio);
    const double cursor_px  = playhead_pixel_x(app, audio);
    viewport.invalidate_playhead_columns(scanner_px, cursor_px);
    viewport.invalidate_timestamp_area();
    const GuiRect ts = top_strip_area(app);
    gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
    app.playhead_scanner_active = false;
    app.playhead_scanner_sample = app.playhead_cursor_sample;
    app.follow_overridden_for_session = false;
}

// Space-bar: start/stop playback. Playback runs from the cursor to
// trim_end (or total_frames if no e= marker). Pressing space with the
// cursor at or past trim-end is a silent no-op. Space-to-stop sends
// the scanner back to the cursor (the cursor is the launch point by
// definition under the split-playhead model — no separate stash).
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
// defeat that purpose. The persistent app.playback_speed is left
// untouched; toggling back to source view restores it naturally
// because set_playback_speed refuses writes in target view.
void GuiPlaybackLifecycle::toggle_playback() {
    if (playback.is_playing()) {
        playback.stop();
        restore_playhead_to_lsp();
        return;
    }
    // Defensive: clear any stale override from an unhandled stop path so
    // it can't survive into the new playback session.
    app.follow_overridden_for_session = false;
    int64_t start;
    int64_t end;
    // Launch position for the visible scanner, always in the active
    // PAINT domain (full-target-frame in target view; source/render-frame
    // otherwise) — the same domain as viewport_start_sample and as the
    // value the pre-paint hook maintains during playback. Distinct from
    // `start`, which in target view is a BUFFER-LOCAL index for
    // playback.play(). Assigning the buffer-local `start` to the scanner
    // here leaks a wrong-domain value into follow_scroll_if_needed below,
    // which compares it against the full-domain viewport and, with a
    // non-zero target_buffer_start_frame (trim set), wrongly judges the
    // scanner offscreen and yanks the viewport to file/trim start.
    int64_t scanner_launch;
    if (app.active_audio_view == 'T' &&
        !app.render_view_enabled) {
        // Target view: the target buffer is the live playback source.
        // Refuse if no successful target render has populated it yet
        // — Space's outer gate in input_handler.cpp already checks this,
        // but stay defensive here so a future caller can't slip through.
        if (app.target_buffer_frames <= 0) return;
        const int64_t bias = app.target_buffer_start_frame;
        const int64_t local = app.playhead_cursor_sample - bias;
        // Playhead outside the target buffer's target-domain extent
        // is a silent no-op. Mirrors the "playhead at or past trim_end
        // is a silent no-op" pattern below for source view.
        if (local < 0) return;
        if (local >= app.target_buffer_frames) return;
        start = local;
        end   = app.target_buffer_frames;
        // Full-target-frame launch = buffer-local start + buffer bias,
        // i.e. exactly the validated cursor position.
        scanner_launch = local + bias;
    } else {
        end = viewport.trim_end_sample();
        if (app.playhead_cursor_sample >= end) return;
        // Cursor outside the trim region (either side) is a silent no-op.
        // For unset trim, trim_begin_sample() is 0, so this never bites.
        if (app.playhead_cursor_sample < viewport.trim_begin_sample()) return;
        // Cursor is now guaranteed in [trim_begin, trim_end).
        start = app.playhead_cursor_sample;
        // Source/render view: paint domain == playback domain, so the
        // scanner launch is the same value as start.
        scanner_launch = start;
    }
    app.playhead_scanner_sample = scanner_launch;
    app.playhead_scanner_active = true;
    // If the cursor is offscreen at play press, left-edge-align the
    // viewport on the cursor before the scanner issues forth. Follow
    // mode's same-shape check is sufficient regardless of whether the
    // user has follow mode toggled on, so always run it on press.
    viewport.follow_scroll_if_needed();
    const bool force_one_x =
        app.render_view_enabled || app.active_audio_view == 'T';
    playback.set_speed(force_one_x ? 1.0f : app.playback_speed);
    playback.play(start, end);
}

// Click-keep-alive: reseek a live playback session to `sample` without
// the stop-and-restart visual glitch. Mirrors toggle_playback's domain
// translation (target-view buffer-local frame; source/render-view direct
// trim_end_sample()) but is always-on rather than toggling. Called from
// the press and motion handlers during a playhead-drag when playback was
// alive at press time. Out-of-range positions in target view fall back to
// stop — in-range-only semantics. No follow-scroll at
// the reseek site: the user's click is a positional intent that takes
// precedence over visual centering (unlike Space's start-of-listening).
void GuiPlaybackLifecycle::reseek_keeping_alive(int64_t sample) {
    if (app.active_audio_view == 'T' && !app.render_view_enabled) {
        if (app.target_buffer_frames <= 0) { playback.stop(); return; }
        const int64_t bias  = app.target_buffer_start_frame;
        const int64_t local = sample - bias;
        if (local < 0 || local >= app.target_buffer_frames) {
            playback.stop();
            return;
        }
        playback.play(local, app.target_buffer_frames);
        return;
    }
    playback.play(sample, viewport.trim_end_sample());
}

// Helpers for Shift+<digit> speed selection. Refused silently in target
// view: target view's audio is the warped target buffer, and adding
// a playback-speed multiplier on top would defeat the premise
// (the audible result must match the authored warp, not the
// warp scaled by an extra factor). The persistent app.playback_speed
// is left untouched so a T→S round trip restores whatever rate the
// user last set in source view. This is the only path that writes
// app.playback_speed, so the refusal here is sufficient.
void GuiPlaybackLifecycle::set_playback_speed(float s) {
    if (app.active_audio_view == 'T') return;
    app.playback_speed = s;
    playback.set_speed(s);
    // Speed change without resync would cause a backward cursor jump:
    // the predictor would retroactively apply the new speed to the
    // entire elapsed-since-anchor period.
    if (playback.is_playing()) playback.resync_predictor();
}
