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
    app.playhead_scanner_precise = static_cast<double>(app.playhead_cursor_sample);
    viewport.invalidate_playhead_columns(scanner_px, cursor_px);
    viewport.invalidate_timestamp_area();
    app.follow_overridden_for_session = false;
}

void GuiPlaybackLifecycle::hold_natural_end_scanner(int64_t endpoint_sample) {
    const double old_px = scanner_pixel_x(app, audio);
    app.playhead_scanner_sample = endpoint_sample;
    app.playhead_scanner_precise = static_cast<double>(endpoint_sample);
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
    app.playhead_scanner_precise = static_cast<double>(app.playhead_cursor_sample);
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
// validated cursor position unchanged. Speed is forced to 1.0 here:
// target view's whole purpose is that
// audio plays the user's authored warp, so an extra speed multiplier
// on top would defeat that purpose. The persistent app.playback_speed
// is left untouched; toggling back to source view restores it naturally
// because every launch re-applies from app.playback_speed (a speed set
// while in target view only stores, taking effect at this launch site).
void GuiPlaybackLifecycle::toggle_playback(int64_t launch_offset) {
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
    // (full-target-frame in target view; source-frame otherwise)
    // — playback's API takes domain coordinates, so the same value
    // serves both, and follow_scroll_if_needed compares the scanner
    // against the full-domain viewport with no wrong-domain leak.
    int64_t start;
    int64_t end;
    // Looping audition verdict, LAUNCH-CAPTURED here (computed once, before the
    // view split) exactly like end_sample: when trim is set (a lone bound
    // defines a window under the completion semantics), the audition loops — on
    // reaching the end the audio callback wraps back to the window begin and
    // keeps playing until the user stops it (Space/Esc/any stop route). Mid-
    // session trim edits do NOT alter the running session (mirroring how a trim
    // edit never moves the running end_sample); the next launch picks up the
    // new state. The per-arm loop start is set below and stashed in
    // app.playback_loop_start_sample so the click-keep-alive reseek preserves
    // this session's captured verdict.
    const bool loop = app.trim.has_begin || app.trim.has_end;
    int64_t loop_start = -1;
    if (app.active_audio_view == 'T') {
        // Target view: the target buffer is the live playback source.
        // Refuse if no successful target render has populated it yet
        // — Space's outer gate in input_handler.cpp already checks this,
        // but stay defensive here so a future caller can't slip through.
        // (Mode logic — "has a successful target render populated the
        // buffer" — not domain math; the domain range policy follows.)
        if (app.target_buffer_frames <= 0) return;
        // Launch = cursor + launch_offset. The offset is 0 for plain Space and
        // +N/2 for the Alt+Space lead-in audition; the resting cursor is never
        // moved either way, so stop snaps the scanner back onto it. Validate the
        // OFFSET launch (not the bare cursor) against the bound target buffer's
        // target-domain extent: a launch outside it — including cursor + N/2 at
        // or past the buffer end — is a silent no-op, nothing to audition.
        // Mirrors the "playhead at or past trim_end is a silent no-op" pattern
        // below for source view.
        //
        // The end check runs on the cursor against the offset-SHIFTED end,
        // BEFORE the sum is formed: cursor + launch_offset can exceed int64
        // for an extreme cursor value, while domain_end() - launch_offset
        // cannot (the offset is 0 or +N/2 at both call sites and domain_end
        // is a modest buffer extent), and a cursor that passes the check
        // bounds the sum below domain_end(). Same verdicts as summing first
        // wherever the sum was defined. The extra `- 1` is the two-frame
        // remainder gate (rationale at the source arm below): a launch whose
        // start would leave fewer than two frames before domain_end() no-ops.
        // It does not change the overflow shape — the check still runs on the
        // cursor against the shifted bound before the sum is formed — and
        // domain_end() - launch_offset - 1 cannot underflow into surprise:
        // domain_end() >= 0 and launch_offset is 0 or +N/2, so a tiny buffer
        // only drives the difference negative, which merely makes the no-op
        // FIRE (the safe direction).
        if (app.playhead_cursor_sample >=
            playback.domain_end() - launch_offset - 1) return;
        start = app.playhead_cursor_sample + launch_offset;
        if (start < playback.domain_begin()) return;
        end   = playback.domain_end();
        // Target view: the bound buffer IS the trim window under the
        // completion semantics, so the loop start is the buffer's domain
        // begin. `loop` is false when trim is unset even though a buffer
        // domain always exists — no loop then.
        if (loop) loop_start = playback.domain_begin();
    } else {
        end = viewport.trim_end_sample();
        // Space requires at least TWO playable frames of remainder: a
        // remainder of one (or less) no-ops silently, joining the "nothing
        // to audition" family. A one-frame session is an isolated impulse —
        // the audible pop — so playing from the End landing spot (End lands
        // the playhead at end - 1) is degenerate, and End+Space is a common
        // slip of the hand this product caters to for non-adversarial use.
        // The End landing frame (end - 1) therefore now no-ops, and a
        // one-frame trim window (begin == end - 1) is Space-inert (its render
        // still works: the trimmer's one-frame-fady-trim latitude is a RENDER
        // latitude, not an audition one). Deliberate near-end plays stay
        // admitted — End, then Left a few times, then Space plays — and
        // start-of-play clicks are normal DAW behaviour, so no fade/ramp/
        // declick machinery is added (considered and REJECTED by ruling).
        if (app.playhead_cursor_sample >= end - 1) return;
        // Cursor outside the trim region (either side) is a silent no-op.
        // For unset trim, trim_begin_sample() is 0, so this never bites.
        // A crossed or equal pair cannot rest — every commit and load
        // auto-clears both bounds — so [begin, end) is well-formed
        // whenever this runs; the two checks simply bound Space to the
        // resting trim window.
        if (app.playhead_cursor_sample < viewport.trim_begin_sample()) return;
        // Cursor is now guaranteed in [trim_begin, trim_end).
        start = app.playhead_cursor_sample;
        // Source view: loop back to the completed trim begin — the same value
        // the lower Space gate compares against (0 for a lone end).
        if (loop) loop_start = viewport.trim_begin_sample();
    }
    // Scanner launch = the validated cursor position, in the paint domain
    // in every view (see the comment above `start`). Seed the continuous
    // position too so the first paint (where the predictor's cur still equals
    // start and the pre-paint hook early-returns) draws at the launch column
    // rather than a stale precise value.
    app.playhead_scanner_sample = start;
    app.playhead_scanner_precise = static_cast<double>(start);
    app.playhead_scanner_active = true;
    // If the cursor is offscreen at play press, left-edge-align the
    // viewport on the cursor before the scanner issues forth. Follow
    // mode's same-shape check is sufficient regardless of whether the
    // user has follow mode toggled on, so always run it on press.
    viewport.follow_scroll_if_needed();
    const bool force_one_x = (app.active_audio_view == 'T');
    playback.set_speed(force_one_x ? 1.0f : app.playback_speed);
    // Stash the captured loop start for the click-keep-alive reseek to reuse
    // (play() decides the real looping window; -1 means this session does not
    // loop). Must be set before play() so a reseek issued right after sees it.
    app.playback_loop_start_sample = loop_start;
    playback.play(start, end, loop_start);
}

// Click-keep-alive: reseek a live playback session to `sample` without the
// stop-and-restart visual glitch. Both arms mirror toggle_playback's
// range policy: source view against [trim_begin_sample(), trim_end_sample()),
// target view against the bound buffer's [domain_begin(),
// domain_end()). `sample` is a paint-domain coordinate, the same domain
// playback's public API speaks in every view. Called from the waveform PRESS
// handlers only (input_pointer.cpp) — the plain / Shift left press that places
// the playhead — when playback was alive at press time; the motion handlers do
// not call it (a region drag never moves the playhead).
// Both arms carry the same two-frame remainder gate as toggle_playback (see
// the rationale at its source arm): a reseek that would leave fewer than two
// playable frames is out of range, so a live-playback click at the last frame
// stops cleanly instead of playing a one-frame impulse — symmetric with Space.
// An out-of-range position in any arm falls back to a MANUAL stop with
// immediate scanner teardown (stop_playback_if_playing), never the natural-end
// scanner flash. No follow-scroll at the reseek site: the reseek repositions
// without recentering the viewport.
//
// stop_playback_if_playing clears follow_overridden_for_session, but every
// caller sets it back to true immediately AFTER this returns (having already
// run move_playhead_to before), so the reset is a harmless transient — the
// caller owns the override across the reseek.
void GuiPlaybackLifecycle::reseek_keeping_alive(int64_t sample) {
    if (app.active_audio_view == 'T') {
        if (app.target_buffer_frames <= 0) { stop_playback_if_playing(); return; }
        if (sample < playback.domain_begin() ||
            sample >= playback.domain_end() - 1) {
            stop_playback_if_playing();
            return;
        }
        // Preserve the running session's launch-captured loop verdict: a
        // looping audition stays looping across a click-keep-alive reseek
        // (the window is unchanged; only the immediate resume point moves).
        playback.play(sample, playback.domain_end(),
                      app.playback_loop_start_sample);
        return;
    }
    // Source view: enforce the trim window with in-range-only semantics,
    // mirroring the two arms above. Equal or crossed trim bounds make
    // [trim_begin, trim_end) empty; such a pair cannot REST any more (the
    // commit auto-clear destroys it), but it exists freely mid-gesture, and
    // whenever the window is empty every live reseek stops — the same sane
    // degradation as Space's silent no-op. This
    // guard also means play() below can never be reached with an empty range
    // from this site, closing the play() early-return trap (end_sample <=
    // start_sample returns early WITHOUT clearing the playing flag) at its
    // only reseek exposure.
    if (sample < viewport.trim_begin_sample() ||
        sample >= viewport.trim_end_sample() - 1) {
        stop_playback_if_playing();
        return;
    }
    // Preserve the running session's launch-captured loop verdict (see the
    // target arm above): the looping window is unchanged, only the resume
    // point moves.
    playback.play(sample, viewport.trim_end_sample(),
                  app.playback_loop_start_sample);
}

// Set follow mode (contract at the header declaration). Shared by the bare-`f`
// toggle and the settings editor's `follow=` commit.
void GuiPlaybackLifecycle::set_follow_mode(bool desired) {
    const bool was_off = !app.follow_mode;
    app.follow_mode = desired;
    if (was_off && app.follow_mode && playback.is_playing()) {
        // Explicit enable overrides a prior manual-pan suppression so follow
        // resumes paging, not just the one initial jump. Land the scanner at
        // the page-turn position if it had drifted offscreen; no-op when it is
        // already in view.
        app.follow_overridden_for_session = false;
        playback.resync_predictor();
        viewport.follow_scroll_if_needed();
    }
}

// Sets the persistent playback speed (the settings editor is the authoring
// surface). In target view the write STORES only: target view's audio is the
// warped target buffer, played at its natural rate, so a playback-speed
// multiplier on top would defeat the premise (the audible result must match
// the authored warp, not the warp scaled by an extra factor). The stored
// value takes effect on the return to source view, where the next playback
// launch re-applies it from app.playback_speed. This is the only path that
// writes app.playback_speed.
void GuiPlaybackLifecycle::set_playback_speed(float s) {
    app.playback_speed = s;
    if (app.active_audio_view == 'T') return;  // target view: store only (see above)
    playback.set_speed(s);
    // Speed change without resync would cause a backward cursor jump:
    // the predictor would retroactively apply the new speed to the
    // entire elapsed-since-anchor period.
    if (playback.is_playing()) playback.resync_predictor();
}
