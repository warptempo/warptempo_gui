#include "playback_lifecycle.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

// Gesture-stop: called at the top of any handler that will move the
// cursor (keys, button press, undo/redo, tab switch).
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
    app.playhead_scanner_restore_pending = false;
    app.playhead_scanner_endpoint_painted = false;
    app.playhead_scanner_active = false;
    app.playhead_scanner_sample = app.playhead_cursor_sample;
    viewport.invalidate_playhead_columns(scanner_px, cursor_px);
    viewport.invalidate_timestamp_area();
    app.follow_overridden_for_session = false;
}

void GuiPlaybackLifecycle::hold_natural_end_scanner(int64_t endpoint_sample) {
    const double old_px = scanner_pixel_x(app, audio);
    app.playhead_scanner_sample = endpoint_sample;
    app.playhead_scanner_active = true;
    app.playhead_scanner_restore_pending = true;
    app.playhead_scanner_endpoint_painted = false;
    const double new_px = scanner_pixel_x(app, audio);
    viewport.invalidate_playhead_columns(old_px, new_px);
    viewport.invalidate_timestamp_area();
    const GuiRect ts = top_strip_area(app);
    gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
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
    app.playhead_scanner_restore_pending = false;
    app.playhead_scanner_endpoint_painted = false;
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
// (rebound by GuiTargetRender's completion path on Success, with the
// buffer's domain offset travelling with the bind). The playhead in
// target view is a full-target-frame coordinate, and playback's whole
// public API speaks the bound buffer's domain (playback.h), so the
// bounds pass straight through: [domain_begin(), domain_end()) is the
// target buffer's full-target-frame extent and play() takes the
// validated cursor position unchanged. Speed is forced to 1.0 here
// (sibling case to render view): target view's whole purpose is that
// audio plays the user's authored warp, so an extra speed multiplier
// on top would defeat that purpose. The persistent app.playback_speed
// is left untouched; toggling back to source view restores it
// naturally because set_playback_speed refuses writes in target view.
void GuiPlaybackLifecycle::toggle_playback() {
    if (playback.is_playing()) {
        playback.stop();
        restore_playhead_to_lsp();
        return;
    }
    // Defensive: clear any stale override from an unhandled stop path so
    // it can't survive into the new playback session.
    app.follow_overridden_for_session = false;
    app.playhead_scanner_restore_pending = false;
    app.playhead_scanner_endpoint_painted = false;
    // Both `start` (playback.play()'s launch bound) and the scanner's
    // launch position below are in the active PAINT domain
    // (full-target-frame in target view; source/render-frame otherwise)
    // — playback's API takes domain coordinates, so the same value
    // serves both, and follow_scroll_if_needed compares the scanner
    // against the full-domain viewport with no wrong-domain leak.
    int64_t start;
    int64_t end;
    if (app.active_audio_view == 'T' &&
        !app.render_view.enabled) {
        // Target view: the target buffer is the live playback source.
        // Refuse if no successful target render has populated it yet
        // — Space's outer gate in input_handler.cpp already checks this,
        // but stay defensive here so a future caller can't slip through.
        // (Mode logic — "has a successful target render populated the
        // buffer" — not domain math; the domain range policy follows.)
        if (app.target_buffer_frames <= 0) return;
        // Playhead outside the bound target buffer's target-domain extent
        // is a silent no-op. Mirrors the "playhead at or past trim_end
        // is a silent no-op" pattern below for source view.
        if (app.playhead_cursor_sample < playback.domain_begin()) return;
        if (app.playhead_cursor_sample >= playback.domain_end()) return;
        start = app.playhead_cursor_sample;
        end   = playback.domain_end();
    } else {
        end = viewport.trim_end_sample();
        if (app.playhead_cursor_sample >= end) return;
        // Cursor outside the trim region (either side) is a silent no-op.
        // For unset trim, trim_begin_sample() is 0, so this never bites.
        // Equal or INVERTED bounds (legal at-rest states; render refuses,
        // authoring never guards) make [begin, end) empty, so the two
        // checks together turn Space into a silent no-op everywhere —
        // playback degrades sanely with no ordering assumption.
        if (app.playhead_cursor_sample < viewport.trim_begin_sample()) return;
        // Cursor is now guaranteed in [trim_begin, trim_end).
        start = app.playhead_cursor_sample;
    }
    // Scanner launch = the validated cursor position, in the paint domain
    // in every view (see the comment above `start`).
    app.playhead_scanner_sample = start;
    app.playhead_scanner_active = true;
    // If the cursor is offscreen at play press, left-edge-align the
    // viewport on the cursor before the scanner issues forth. Follow
    // mode's same-shape check is sufficient regardless of whether the
    // user has follow mode toggled on, so always run it on press.
    viewport.follow_scroll_if_needed();
    const bool force_one_x =
        app.render_view.enabled || app.active_audio_view == 'T';
    playback.set_speed(force_one_x ? 1.0f : app.playback_speed);
    playback.play(start, end);
}

// Click-keep-alive: reseek a live playback session to `sample` without the
// stop-and-restart visual glitch. All three arms mirror toggle_playback's
// range policy: source view against [trim_begin_sample(), trim_end_sample()),
// target and render view against the bound buffer's [domain_begin(),
// domain_end()). `sample` is a paint-domain coordinate, the same domain
// playback's public API speaks in every view. Called from the PRESS handlers
// only (input_pointer.cpp and input_render_view.cpp) during a playhead-drag
// when playback was alive at press time — the motion handlers do not call it.
// An out-of-range position in any arm falls back to a MANUAL stop with
// immediate scanner teardown (stop_playback_if_playing), never the natural-end
// scanner flash. No follow-scroll at the reseek site: the user's click is a
// positional intent that takes precedence over visual centering (unlike
// Space's start-of-listening).
//
// stop_playback_if_playing clears follow_overridden_for_session, but every
// caller sets it back to true immediately AFTER this returns (having already
// run move_playhead_to before), so the reset is a harmless transient — the
// caller owns the override across the reseek.
void GuiPlaybackLifecycle::reseek_keeping_alive(int64_t sample) {
    if (app.active_audio_view == 'T' && !app.render_view.enabled) {
        if (app.target_buffer_frames <= 0) { stop_playback_if_playing(); return; }
        if (sample < playback.domain_begin() ||
            sample >= playback.domain_end()) {
            stop_playback_if_playing();
            return;
        }
        playback.play(sample, playback.domain_end());
        return;
    }
    if (app.render_view.enabled) {
        // The audio device is bound to the entry wav at offset 0, and render
        // view displays that wav's OWN timeline — so [domain_begin(),
        // domain_end()) is [0, entry frames), the WHOLE displayed domain. The
        // playhead always rests inside it, so this bound-buffer range check is
        // defensively unreachable in practice; it is kept as the shared shape
        // of the three reseek arms (a stray out-of-range value falls back to a
        // clean manual stop rather than a bad play()).
        if (sample < playback.domain_begin() ||
            sample >= playback.domain_end()) {
            stop_playback_if_playing();
            return;
        }
        playback.play(sample, playback.domain_end());
        return;
    }
    // Source view: enforce the trim window with in-range-only semantics,
    // mirroring the two arms above. Equal or crossed trim bounds make
    // [trim_begin, trim_end) empty; such bounds exist only transiently — a
    // commit that would REST them crossed or equal forces modal resolution
    // (the trim remedy deletes both bounds), so an empty window survives only
    // mid-gesture, or loaded from disk before the defect series resolves on
    // the first tick after load. Whenever the window is empty every live
    // reseek stops — the same sane degradation as Space's silent no-op. This
    // guard also means play() below can never be reached with an empty range
    // from this site, closing the play() early-return trap (end_sample <=
    // start_sample returns early WITHOUT clearing the playing flag) at its
    // only reseek exposure.
    if (sample < viewport.trim_begin_sample() ||
        sample >= viewport.trim_end_sample()) {
        stop_playback_if_playing();
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
