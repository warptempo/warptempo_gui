#include "target_render.h"

#include "app_state.h"
#include "timemap.h"
#include "engine/stft_container.h"

#include <cmath>
#include <cstdio>
#include <utility>

void GuiTargetRender::trigger() {
    // Output-affecting mutation hook: the buffer is now stale relative
    // to engine input. Set the bit unconditionally — even in source
    // view — so a later S→T ensure_ready() sees the staleness and
    // dispatches against the accumulated source-view edits rather than
    // re-binding to a buffer that no longer matches the live state.
    is_dirty_ = true;
    // Source view: archival renders keep running in the background and
    // playback keeps reading source.wav. Nothing to do here.
    if (app.active_audio_view != 'T') return;
    // No audio loaded — nothing to render.
    if (audio.total_frames() <= 0)              return;
    if (app.source_audio_path.empty())          return;

    // Freeze playback. Target view's playback model is "every edit halts
    // playback; the user re-presses Space after the update completes".
    // Stop synchronously so the audio callback releases the buffer before
    // we (potentially) swap it underneath.
    if (playback.is_playing()) {
        playback.stop();
        app.playhead_scanner_active = false;
        app.playhead_scanner_sample = app.playhead_cursor_sample;
    }

    // Drop any pending archival batch entries. The current Ctrl+Alt+R /
    // Ctrl+Alt+E / Ctrl+Alt+I / BPM-sweep flows are sacrificed — the
    // user can re-fire them later. The batch state machine consults
    // queue_cancel_requested at on_batch_entry_complete time and
    // finalizes instead of dispatching the next entry, so setting it
    // here is enough.
    app.queued_renders.clear();
    app.queue_cancel_requested = true;

    // Surface the "updating..." status. The bottom_strip_wide() predicate
    // reads queue_progress_text; "rendering..." (archival) and
    // "updating..." (target render) share the slot.
    app.queue_progress_text = "updating...";
    viewport.invalidate_timestamp_area();

    pending_ = true;
    if (async_renderer.is_busy()) {
        // Worker is mid-render. Cancel; the existing on_done path will
        // call maybe_dispatch_pending() once the worker exits.
        async_renderer.request_cancel();
        return;
    }
    // Worker is idle. Dispatch immediately.
    dispatch_render_now();
}

void GuiTargetRender::maybe_dispatch_pending() {
    if (!pending_)                  return;
    if (async_renderer.is_busy())   return;
    // Re-validate target view: a target → source toggle between trigger()
    // and the pump may have moved us out of target view. In that case
    // rebind_to_source() already cleared pending_, but be defensive.
    if (app.active_audio_view != 'T') {
        pending_ = false;
        return;
    }
    dispatch_render_now();
}

void GuiTargetRender::dispatch_render_now() {
    pending_   = false;
    in_flight_ = true;

    // The batch state machine's cancel sentinel may still be set from
    // trigger(); the target render's on_done doesn't read it but the next
    // archival render path needs a clean slate.
    app.queue_cancel_requested = false;

    // Re-stamp the progress text. A cancelled archival's on_done
    // (finalize_render_run) clears the text in its terminal branch,
    // and the target render's dispatch may run in that callback's pumping
    // path. Re-asserting "updating..." here keeps the bottom strip
    // wide for the duration of the actual target render.
    app.queue_progress_text = "updating...";
    viewport.invalidate_timestamp_area();

    // Clear the target buffer; do_render appends synthesised samples
    // into it via std::vector::insert. The start_frame is also cleared
    // for symmetry — it will be recomputed at on_render_done time.
    app.target_buffer.clear();
    app.target_buffer_frames = 0;
    app.target_buffer_start_frame = 0;

    RenderRequest req;
    req.source_audio_path = app.source_audio_path;
    req.markers           = app.warpmarkers.markers();
    req.phase_resets      = app.phase_reset_markers.markers();
    req.engine_settings   = app.engine_settings;
    {
        const ViewState& vs = active_view_state(app);
        req.has_trim_begin  = vs.has_trim_begin;
        req.trim_begin_sec  = vs.trim_begin_seconds;
        req.has_trim_end    = vs.has_trim_end;
        req.trim_end_sec    = vs.trim_end_seconds;
    }
    for (const auto& m : app.phase_reset_markers.markers()) {
        if (m.disabled) continue;
        req.phase_reset_frames.push_back(static_cast<int64_t>(
            std::nearbyint(m.time_seconds *
                           static_cast<double>(audio.sample_rate()))));
    }
    // Buffer-output route. do_render skips the on-disk rename, sidecar
    // writes, and the peak-pyramid sidecar; synth samples append into
    // *output_buffer instead. The limited chain (spectral + peak backstop)
    // runs in place on the buffer whenever the global `limiter` toggle is
    // on — the target-view preview gets the same limiting as the disk path.
    req.output_buffer = &app.target_buffer;

    async_renderer.dispatch(std::move(req),
        [this](RenderOutcome o) { on_render_done(o); });
}

void GuiTargetRender::on_render_done(RenderOutcome outcome) {
    in_flight_ = false;

    if (outcome == RenderOutcome::Success) {
        // Cache the buffer's frame count and rebind playback. The buffer
        // is interleaved float; total_frames = size / channels. Use the
        // source's channel count (engine preserves channel count).
        const int ch = audio.channels();
        if (ch > 0) {
            app.target_buffer_frames =
                static_cast<int64_t>(app.target_buffer.size() /
                                     static_cast<size_t>(ch));
        } else {
            app.target_buffer_frames = 0;
        }
        // Capture the full-target-frame coordinate target_buffer[0]
        // represents (0, or the trim-mapped anchor). SoT helper, shared with
        // ensure_ready's clean rebind so a cached re-entry matches.
        recompute_target_buffer_start_frame();
        // Only rebind if we're actually showing target audio. A T→S toggle or
        // a render-view entry during render already cancelled this render and
        // does not want playback bound to target_buffer: rebinding here would
        // clobber source.wav / the render-view buffer. Gate it out by both
        // active_audio_view and render_view_enabled, matching the "actually in
        // target view" idiom. is_dirty_ therefore stays set through a render-
        // view visit, so the next true target-view entry re-renders — correct,
        // never stale, and consistent with the source-toggle case.
        if (app.active_audio_view == 'T' && !app.render_view_enabled &&
            app.target_buffer_frames > 0) {
            // The trigger always freezes playback before dispatch, so
            // the device should still be stopped here. The rebind helper
            // refuses if the device is somehow playing.
            playback.rebind_buffer(app.target_buffer.data(),
                                   app.target_buffer_frames);
            // Buffer now matches the engine input that produced it.
            // Gate the clear behind the rebind path: a render that
            // completed while active_audio_view flipped to Source must not
            // clear the bit, since a later S→T may follow source-view
            // edits whose trigger() set the bit while the render was
            // already in flight.
            is_dirty_ = false;
        }
    } else if (outcome == RenderOutcome::Cancelled) {
        std::fprintf(stderr, "warptempo_gui: target render cancelled\n");
        // Leave the target buffer's frames count at 0 — playback
        // gating checks this to refuse Space.
        app.target_buffer.clear();
        app.target_buffer_frames = 0;
        app.target_buffer_start_frame = 0;
    } else {
        std::fprintf(stderr, "warptempo_gui: target render failed\n");
        app.target_buffer.clear();
        app.target_buffer_frames = 0;
        app.target_buffer_start_frame = 0;
    }

    // Clear status. Mirrors finalize_render_run: invalidate first (so
    // the wide-strip rect still includes "updating..."'s width), then
    // clear the text.
    viewport.invalidate_timestamp_area();
    app.queue_progress_text.clear();

    // A new trigger() may have fired during render. Pump the pending
    // dispatch now that the worker is idle.
    maybe_dispatch_pending();
}

void GuiTargetRender::recompute_target_buffer_start_frame() {
    // Buffer frame 0 corresponds to target frame 0 for a full-song render;
    // with trim set, to map_source_to_target(trim_begin_frame) against the
    // full-source frame_map, since the engine rendered only the trim range.
    // Compute only after target_buffer_frames is set so a failed/empty buffer
    // does not leave a stale anchor.
    app.target_buffer_start_frame = 0;
    const ViewState& vs = active_view_state(app);
    if (vs.has_trim_begin && app.target_buffer_frames > 0 &&
        audio.sample_rate() > 0 && audio.total_frames() > 0) {
        const auto tmap = build_target_view_frame_map(
            app, audio.sample_rate(),
            static_cast<long>(audio.total_frames()));
        const int64_t trim_begin_frame = static_cast<int64_t>(
            std::nearbyint(vs.trim_begin_seconds *
                           static_cast<double>(audio.sample_rate())));
        const double tgt = map_source_to_target(
            static_cast<size_t>(trim_begin_frame < 0
                                ? 0 : trim_begin_frame), tmap);
        app.target_buffer_start_frame =
            static_cast<int64_t>(std::nearbyint(tgt));
    }
}

void GuiTargetRender::ensure_ready() {
    // Source view does not use target_buffer. Match trigger()'s
    // source-view no-op invariant.
    if (app.active_audio_view != 'T') return;
    // No audio loaded — nothing to do.
    if (audio.total_frames() <= 0)              return;
    if (app.source_audio_path.empty())          return;

    // Clean path: the buffer is current AND non-empty. Rebind playback
    // to it without dispatching a render.
    if (!is_dirty_ && app.target_buffer_frames > 0) {
        // Defensive stop: rebind_buffer refuses if the device is playing.
        // Call sites are expected to have stopped playback already (the
        // S→T toggle handler does, render-view exit's restore_source_audio
        // does), but a future caller that forgets shouldn't get a silent
        // refused-rebind.
        if (playback.is_playing()) {
            playback.stop();
            app.playhead_scanner_active = false;
            app.playhead_scanner_sample = app.playhead_cursor_sample;
        }
        // Restore the playback bias the cached buffer was rendered with.
        // rebind_to_source() (the T→S leg) zeroes target_buffer_start_frame to
        // leave a clean slate, but a clean re-entry rebinds the SAME buffer
        // without a render, so the trim-mapped anchor must be recomputed here or
        // target-view play maps the playhead past the buffer and silently
        // refuses (no audio, no playhead move). When the clean path is taken the
        // trim is unchanged since the render (any trim edit sets is_dirty_), so
        // this reproduces the render's anchor.
        recompute_target_buffer_start_frame();
        playback.rebind_buffer(app.target_buffer.data(),
                               app.target_buffer_frames);
        return;
    }

    // Dirty or empty buffer: dispatch fresh. trigger() re-sets the bit
    // (already true here by construction) and runs the cancel-clear-
    // dispatch sequence. Identical body to the original S→T eager-
    // dispatch path that this method replaces.
    trigger();
}

void GuiTargetRender::cancel_for_load() {
    // file_loader entry hook. Runs on the GUI thread immediately before
    // the live source audio is torn down. Goal: leave the target buffer
    // in a coherent state without racing the async render worker, which
    // may currently be appending samples into *req.output_buffer (the
    // address of app.target_buffer).
    //
    // The buffer is no longer current with the soon-to-be-loaded source,
    // regardless of which branch we take below.
    is_dirty_ = true;
    // Any pending target-render dispatch must not pump into the new
    // source's worker queue. If a cancel-restart was queued behind a
    // busy archival render, clearing the pending bit aborts the queued
    // dispatch before maybe_dispatch_pending() can fire it.
    pending_ = false;

    if (in_flight_) {
        // Worker is mid-target-render, writing into target_buffer. Do NOT
        // touch the buffer fields here — that would race vector::insert
        // on the worker side. Set the cancel flag and let
        // on_render_done's Cancelled branch clear target_buffer /
        // target_buffer_frames / target_buffer_start_frame after the
        // worker exits. The Cancelled branch's status cleanup
        // (invalidate_timestamp_area + queue_progress_text.clear) also
        // covers the "updating..." text.
        async_renderer.request_cancel();
        return;
    }

    // Worker is idle or running an archival render (whose RenderRequest
    // has output_buffer == nullptr — archivals write to disk via
    // libsndfile, not into a vector). Either way, no one is touching
    // target_buffer; the synchronous clear is race-free.
    app.target_buffer.clear();
    app.target_buffer_frames = 0;
    app.target_buffer_start_frame = 0;
}

void GuiTargetRender::cancel_in_flight_update() {
    // The in-flight render is now navigated-away-from work. Cancel it and drop
    // any pending dispatch. The worker clears target_buffer / frames in
    // on_render_done's Cancelled branch after it exits; is_dirty_ stays set (the
    // edit that triggered the render is still unrendered), so the next true
    // target-view entry re-renders. Does not touch playback — callers own the
    // rebind (source.wav for T→S, the render buffer for render-view entry).
    if (async_renderer.is_busy() && in_flight_) {
        async_renderer.request_cancel();
    }
    pending_ = false;
    if (app.queue_progress_text == "updating...") {
        viewport.invalidate_timestamp_area();
        app.queue_progress_text.clear();
    }
}

void GuiTargetRender::rebind_to_source() {
    // Called from the target → source view toggle. Cancel any in-flight target
    // render and drop the pending dispatch — source view's playback reads
    // source.wav, not target_buffer. Shared with the render-view entry path.
    cancel_in_flight_update();

    if (playback.is_playing()) {
        playback.stop();
        app.playhead_scanner_active = false;
    }
    if (audio.total_frames() > 0) {
        playback.rebind_buffer(audio.samples_ptr(), audio.total_frames());
    }
    // Target buffer is no longer the live playback source; null out
    // its target-domain anchor so a future T→S→T round trip starts
    // with a clean slate. target_buffer and frames stay populated
    // (cheap; the next trigger() in target view will overwrite them).
    app.target_buffer_start_frame = 0;
}
