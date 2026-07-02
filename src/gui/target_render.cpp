#include "target_render.h"

#include "app_state.h"
#include "frame_map_view.h"
#include "frame_map.h"
#include <cmath>
#include <cstdio>
#include <utility>

bool GuiTargetRender::target_view_available() const {
    return app.engine_settings.output_format == "wav";
}

void GuiTargetRender::trigger() {
    // Output-affecting mutation hook: the buffer is now stale relative
    // to engine input. Set the bit unconditionally — even in source
    // view — so a later S→T ensure_ready() sees the staleness and
    // dispatches against the accumulated source-view edits rather than
    // re-binding to a buffer that no longer matches the live state.
    is_dirty_ = true;
    if (!target_view_available()) {
        cancel_in_flight_update();
        return;
    }
    // Source view: archival renders keep running in the background and
    // playback keeps reading source.wav. Nothing to do here.
    if (app.active_audio_view != 'T') {
        return;
    }
    // No audio loaded — nothing to render.
    if (audio.total_frames() <= 0 || app.source_audio_path.empty()) {
        return;
    }

    // Freeze playback. Target view's playback model is "every edit halts
    // playback; the user re-presses Space after the update completes".
    // Stop synchronously so the audio callback releases the buffer before
    // we (potentially) swap it underneath.
    if (playback.is_playing()) {
        playback.stop();
        app.playhead_scanner_active = false;
        app.playhead_scanner_restore_pending = false;
        app.playhead_scanner_endpoint_painted = false;
        app.playhead_scanner_sample = app.playhead_cursor_sample;
    }

    // Cancel a running archival batch (Ctrl+Alt+R / Ctrl+Alt+E /
    // Ctrl+Alt+I / BPM-sweep). The batch state machine consults
    // queue_cancel_requested at on_batch_entry_complete time and
    // finalizes instead of dispatching the next entry, so setting it
    // here is enough — no need to touch queued_renders, whose entries
    // a running batch has already moved into batch_.reqs.
    //
    // Must NOT clear app.queued_renders here: that queue holds pending
    // Ctrl+E snapshots, accumulated independently of the live target
    // preview, and has to survive the authoring edits (this trigger())
    // made between Ctrl+E presses.
    app.queue_cancel_requested = true;

    // Surface the target-render status through queue_progress_text.
    // "rendering..." (archival) and "updating..." (target render) share
    // the slot.
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
    if (!target_view_available()) {
        pending_ = false;
        return;
    }
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
    if (!target_view_available() || app.active_audio_view != 'T' ||
        audio.total_frames() <= 0 || app.source_audio_path.empty()) {
        pending_ = false;
        if (app.queue_progress_text == "updating...") {
            viewport.invalidate_timestamp_area();
            app.queue_progress_text.clear();
        }
        return;
    }
    pending_ = false;
    // The batch cancel sentinel may still be set from trigger(); clear it on
    // both the cache-hit and synthesis paths so the next archival render path
    // starts from a clean slate.
    app.queue_cancel_requested = false;

    // Consult the render cache before synthesizing. The only callers
    // (trigger()'s idle branch and maybe_dispatch_pending()) reach here only
    // when the worker is idle, so the GUI thread exclusively owns
    // target_buffer and can fill it from the cache without racing the worker.
    // The fingerprint covers the same engine input build_render_request packs
    // below. Every target-view source carries identical deliverable-lattice
    // samples: fresh limited renders quantize in place at publication, cache
    // hits decode the exact codec roundtrip of those samples, and archival
    // artifact loads decode the same deliverable.
    RenderFileIdentity source_identity;
    if (stat_file_identity(app.source_audio_path, source_identity)) {
        last_fingerprint_ = render_fingerprint(
            app.source_audio_path, source_identity, audio.sample_rate(),
            app.warpmarkers.markers(), app.phaseresetmarkers.markers(),
            app.engine_settings,
            app.trim.has_begin, app.trim.begin_seconds,
            app.trim.has_end,   app.trim.end_seconds);
    } else {
        last_fingerprint_.clear();
    }

    if (!last_fingerprint_.empty() &&
        render_cache.lookup(last_fingerprint_, audio.channels(),
                            audio.sample_rate(), app.target_buffer)) {
        // Hit: target_buffer now holds the cached audio. Mirror
        // on_render_done()'s Success tail with no async render; in_flight_
        // stays false since no worker round trip is pending.
        complete_successful_buffer();
        return;
    }

    if (!last_fingerprint_.empty()) {
        const std::string artifact_candidate =
            compose_sibling_output_path(app.source_audio_path,
                                        app.engine_settings).string();
        // This rung auditions the actual archival deliverable. Fresh renders,
        // cache hits, and archival artifact loads all expose identical
        // deliverable-lattice samples because fresh limited renders quantize
        // in place before cache publication and the codec roundtrip is exact.
        if (fingerprint_sidecar_matches(artifact_candidate, last_fingerprint_) &&
            read_wav_to_float(artifact_candidate, audio.channels(),
                              audio.sample_rate(), app.target_buffer)) {
            complete_successful_buffer();
            std::fprintf(stderr,
                "[warptempo_gui] target view loaded from archival render: %s\n",
                artifact_candidate.c_str());
            return;
        }
    }

    // Miss: synthesize. The remainder is the original dispatch path.
    in_flight_ = true;

    // Re-stamp the progress text. A cancelled archival's on_done
    // (finalize_render_run) clears the text in its terminal branch,
    // and the target render's dispatch may run in that callback's pumping
    // path. Target-render status uses queue_progress_text.
    app.queue_progress_text = "updating...";
    viewport.invalidate_timestamp_area();

    // Clear the target buffer; do_render appends synthesised samples
    // into it via std::vector::insert. The start_frame is also cleared
    // for symmetry — it will be recomputed at on_render_done time.
    app.target_buffer.clear();
    app.target_buffer_frames = 0;
    app.target_buffer_start_frame = 0;

    RenderRequest req = build_render_request(
        app.source_audio_path, app.warpmarkers.markers(),
        app.phaseresetmarkers.markers(), app.engine_settings,
        app.trim.has_begin, app.trim.begin_seconds,
        app.trim.has_end,   app.trim.end_seconds,
        audio.sample_rate());
    // Buffer-output route. do_render skips the on-disk rename, sidecar
    // writes, and the peak-pyramid sidecar; synth samples append into
    // *output_buffer instead. The limited chain (spectral + peak backstop)
    // runs in place on the buffer whenever the global `limiter` toggle is
    // on — the target-view preview gets the same limiting as the disk path.
    req.output_buffer = &app.target_buffer;
    // The process's single RenderCache (this struct's own member, wired from
    // main.cpp). do_render uses it on this path only to queue the writer-thread
    // canonical encode after a successful limited target render.
    req.render_cache = &render_cache;
    req.source_samples = audio.samples_shared();
    req.source_total_frames = audio.total_frames();

    async_renderer.dispatch(std::move(req),
        [this](RenderOutcome o) { on_render_done(o); });
}

void GuiTargetRender::on_render_done(RenderOutcome outcome) {
    in_flight_ = false;

    if (outcome == RenderOutcome::Success) {
        complete_successful_buffer();
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

    if (outcome != RenderOutcome::Success) {
        // Clear status. Match finalize_render_run by invalidating the bottom
        // strip before clearing queue_progress_text; timestamp_invalidate_rect()
        // covers the whole bottom strip.
        viewport.invalidate_timestamp_area();
        app.queue_progress_text.clear();
    }

    // A new trigger() may have fired during render. Pump the pending
    // dispatch now that the worker is idle.
    maybe_dispatch_pending();
}

void GuiTargetRender::complete_successful_buffer() {
    // Cache the buffer's frame count and rebind playback. The buffer is
    // interleaved float; total_frames = size / channels. Use the source's
    // channel count (engine preserves channel count).
    const int ch = audio.channels();
    if (ch > 0) {
        app.target_buffer_frames =
            static_cast<int64_t>(app.target_buffer.size() /
                                 static_cast<size_t>(ch));
    } else {
        app.target_buffer_frames = 0;
    }
    // Capture the full-target-frame coordinate target_buffer[0] represents
    // (0, or the trim-mapped anchor). SoT helper, shared with ensure_ready's
    // clean rebind so a cached re-entry matches.
    recompute_target_buffer_start_frame();
    // Only rebind if we're actually showing target audio. A T->S toggle or a
    // render-view entry during render already cancelled this render and does
    // not want playback bound to target_buffer: rebinding here would clobber
    // source.wav / the render-view buffer. Gate it out by both active_audio_view
    // and render_view.enabled, matching the "actually in target view" idiom.
    // is_dirty_ therefore stays set through a render-view visit, so the next
    // true target-view entry re-renders.
    if (app.active_audio_view == 'T' && !app.render_view.enabled &&
        app.target_buffer_frames > 0) {
        // The trigger always freezes playback before dispatch, so the device
        // should still be stopped here. The rebind helper refuses if the device
        // is somehow playing.
        playback.rebind_buffer(app.target_buffer.data(),
                               app.target_buffer_frames);
        // Buffer now matches the engine input that produced it. Gate the clear
        // behind the rebind path: a render that completed while
        // active_audio_view flipped to Source must not clear the bit, since a
        // later S->T may follow source-view edits whose trigger() set the bit
        // while the render was already in flight.
        is_dirty_ = false;
    }

    // Clear status. Match finalize_render_run by invalidating the bottom strip
    // before clearing queue_progress_text; timestamp_invalidate_rect() covers
    // the whole bottom strip.
    viewport.invalidate_timestamp_area();
    app.queue_progress_text.clear();
}

void GuiTargetRender::recompute_target_buffer_start_frame() {
    // Buffer frame 0 corresponds to target frame 0 for a full-song render;
    // with trim set, to map_source_to_target(trim_begin_frame) against the
    // full-source frame_map, since the engine rendered only the trim range.
    // Compute only after target_buffer_frames is set so a failed/empty buffer
    // does not leave a stale anchor.
    app.target_buffer_start_frame = 0;
    if (app.trim.has_begin && app.target_buffer_frames > 0 &&
        audio.sample_rate() > 0 && audio.total_frames() > 0) {
        const auto& tmap = target_view_map_cached(
            app, audio.sample_rate(),
            static_cast<long>(audio.total_frames())).frame_map;
        const int64_t trim_begin_frame = static_cast<int64_t>(
            std::nearbyint(app.trim.begin_seconds *
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
    if (app.active_audio_view != 'T') {
        return;
    }
    if (!target_view_available()) {
        leave_target_view();
        return;
    }
    // No audio loaded — nothing to do.
    if (audio.total_frames() <= 0 || app.source_audio_path.empty()) {
        return;
    }

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
            app.playhead_scanner_restore_pending = false;
            app.playhead_scanner_endpoint_painted = false;
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

void GuiTargetRender::leave_target_view() {
    if (app.active_audio_view != 'T') {
        cancel_in_flight_update();
        return;
    }

    const double cur_spp = current_samples_per_pixel(app, audio);
    const double ph_px =
        (cur_spp > 0.0)
        ? (static_cast<double>(app.playhead_cursor_sample -
                               app.viewport_start_sample) / cur_spp)
        : 0.0;

    const auto& tmap = target_view_map_cached(
        app, audio.sample_rate(),
        static_cast<long>(audio.total_frames())).frame_map;

    const auto to_source = [&](int64_t s) -> int64_t {
        const size_t q = static_cast<size_t>(s < 0 ? 0 : s);
        return static_cast<int64_t>(
            std::nearbyint(map_target_to_source(q, tmap)));
    };

    const int64_t new_playhead = to_source(app.playhead_cursor_sample);
    {
        ViewState& other = (app.active_tab_view == 'B') ? app.tab_a : app.tab_b;
        const int64_t other_old_ph = other.playhead_cursor_sample;
        const int64_t other_new_ph = to_source(other_old_ph);
        other.playhead_cursor_sample = other_new_ph;
        other.viewport_start_sample += (other_new_ph - other_old_ph);
    }

    app.active_audio_view = 'S';
    app.playhead_cursor_sample = new_playhead;
    app.playhead_scanner_active = false;
    app.playhead_scanner_restore_pending = false;
    app.playhead_scanner_endpoint_painted = false;
    app.playhead_scanner_sample = new_playhead;
    const double new_spp = current_samples_per_pixel(app, audio);
    const double new_vp_d =
        static_cast<double>(new_playhead) - ph_px * new_spp;
    app.viewport_start_sample =
        static_cast<int64_t>(std::nearbyint(new_vp_d));

    clamp_viewport_start(app, audio);
    viewport.clear_hover_popup();
    rebind_to_source();
    viewport.kick_waveform_sync();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
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
    // has output_buffer == nullptr, so archivals write to disk instead of a
    // vector). Either way, no one is touching target_buffer; the synchronous
    // clear is race-free.
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
        app.playhead_scanner_restore_pending = false;
        app.playhead_scanner_endpoint_painted = false;
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
