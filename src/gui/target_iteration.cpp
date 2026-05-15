#include "target_iteration.h"

#include "app_state.h"
#include "timemap.h"
#include "engine/stft_container.h"

#include <cmath>
#include <cstdio>
#include <utility>

void GuiTargetIteration::trigger() {
    // Source view: archival renders keep running in the background and
    // playback keeps reading source.wav. Nothing to do here.
    if (app.view_domain != ViewDomain::Target) return;
    // No audio loaded — nothing to render.
    if (audio.total_frames() <= 0)              return;
    if (app.source_audio_path.empty())          return;

    // Freeze playback. Target view's playback model is "every edit halts
    // playback; the user re-presses Space after the update completes".
    // Stop synchronously so the audio callback releases the buffer before
    // we (potentially) swap it underneath.
    if (playback.is_playing()) {
        playback.stop();
        app.is_playing        = false;
        app.last_space_sample = app.playhead_sample;
    }

    // Drop any pending archival batch entries. The current Ctrl+Alt+R /
    // Ctrl+Alt+E / Ctrl+Alt+I / Ctrl+Alt+M flows are sacrificed — the
    // user can re-fire them later. The batch state machine consults
    // queue_cancel_requested at on_batch_entry_complete time and
    // finalizes instead of dispatching the next entry, so setting it
    // here is enough.
    app.queued_renders.clear();
    app.queue_cancel_requested = true;

    // Surface the "updating..." status. The bottom_strip_wide() predicate
    // reads queue_progress_text; "rendering..." (archival) and
    // "updating..." (iteration) share the slot.
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
    dispatch_iteration_now();
}

void GuiTargetIteration::maybe_dispatch_pending() {
    if (!pending_)                  return;
    if (async_renderer.is_busy())   return;
    // Re-validate target view: a target → source toggle between trigger()
    // and the pump may have moved us out of target view. In that case
    // rebind_to_source() already cleared pending_, but be defensive.
    if (app.view_domain != ViewDomain::Target) {
        pending_ = false;
        return;
    }
    dispatch_iteration_now();
}

void GuiTargetIteration::dispatch_iteration_now() {
    pending_   = false;
    in_flight_ = true;

    // The batch state machine's cancel sentinel may still be set from
    // trigger(); the iteration's on_done doesn't read it but the next
    // archival render path needs a clean slate.
    app.queue_cancel_requested = false;

    // Re-stamp the progress text. A cancelled archival's on_done
    // (finalize_render_run) clears the text in its terminal branch,
    // and the iteration dispatch may run in that callback's pumping
    // path. Re-asserting "updating..." here keeps the bottom strip
    // wide for the duration of the actual iteration render.
    app.queue_progress_text = "updating...";
    viewport.invalidate_timestamp_area();

    // Clear the iteration buffer; do_render appends synthesised samples
    // into it via std::vector::insert. The start_frame is also cleared
    // for symmetry — it will be recomputed at on_iteration_done time.
    app.iteration_buffer.clear();
    app.iteration_buffer_frames = 0;
    app.iteration_buffer_target_start_frame = 0;

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
    // *output_buffer instead.
    req.output_buffer = &app.iteration_buffer;
    // Force the peak limiter on. Brick-walling at the user's configured
    // ceiling protects the speakers from spikes the unrendered engine
    // would otherwise emit. This bypasses do_render's trim-derived
    // None / Spectral / Peak selection.
    req.force_peak_limiter = true;

    async_renderer.dispatch(std::move(req),
        [this](RenderOutcome o) { on_iteration_done(o); });
}

void GuiTargetIteration::on_iteration_done(RenderOutcome outcome) {
    in_flight_ = false;

    if (outcome == RenderOutcome::Success) {
        // Cache the buffer's frame count and rebind playback. The buffer
        // is interleaved float; total_frames = size / channels. Use the
        // source's channel count (engine preserves channel count).
        const int ch = audio.channels();
        if (ch > 0) {
            app.iteration_buffer_frames =
                static_cast<int64_t>(app.iteration_buffer.size() /
                                     static_cast<size_t>(ch));
        } else {
            app.iteration_buffer_frames = 0;
        }
        // Capture the full-target-frame coordinate that
        // iteration_buffer[0] represents. With trim set, this is
        // map_source_to_target(trim_begin_frame) against the full-
        // source timemap — the engine rendered only the trim range,
        // so its frame 0 corresponds to the trim's target-frame start.
        // With trim unset, frame 0 of the iteration buffer corresponds
        // to target frame 0 (full-song render). Capture only after
        // iteration_buffer_frames has been set so a failed channel
        // probe doesn't leave start_frame set against an empty buffer.
        app.iteration_buffer_target_start_frame = 0;
        const ViewState& vs = active_view_state(app);
        if (vs.has_trim_begin && app.iteration_buffer_frames > 0 &&
            audio.sample_rate() > 0 && audio.total_frames() > 0) {
            const auto tmap = build_target_view_timemap(
                app, audio.sample_rate(),
                static_cast<long>(audio.total_frames()));
            const int64_t trim_begin_frame = static_cast<int64_t>(
                std::nearbyint(vs.trim_begin_seconds *
                               static_cast<double>(audio.sample_rate())));
            const double tgt = map_source_to_target(
                static_cast<size_t>(trim_begin_frame < 0
                                    ? 0 : trim_begin_frame), tmap);
            app.iteration_buffer_target_start_frame =
                static_cast<int64_t>(std::nearbyint(tgt));
        }
        // Only rebind if we're still in target view. A T→S toggle
        // during render already called rebind_to_source(); we don't
        // want to undo that.
        if (app.view_domain == ViewDomain::Target &&
            app.iteration_buffer_frames > 0) {
            // The trigger always freezes playback before dispatch, so
            // the device should still be stopped here. The rebind helper
            // refuses if the device is somehow playing.
            playback.rebind_buffer(app.iteration_buffer.data(),
                                   app.iteration_buffer_frames);
        }
    } else if (outcome == RenderOutcome::Cancelled) {
        std::fprintf(stderr, "warptempo_gui: iteration cancelled\n");
        // Leave the iteration buffer's frames count at 0 — playback
        // gating checks this to refuse Space.
        app.iteration_buffer.clear();
        app.iteration_buffer_frames = 0;
        app.iteration_buffer_target_start_frame = 0;
    } else {
        std::fprintf(stderr, "warptempo_gui: iteration failed\n");
        app.iteration_buffer.clear();
        app.iteration_buffer_frames = 0;
        app.iteration_buffer_target_start_frame = 0;
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

void GuiTargetIteration::rebind_to_source() {
    // Called from the target → source view toggle. Cancel any in-flight
    // iteration render and clear the pending dispatch — source view's
    // playback reads source.wav, not iteration_buffer.
    if (async_renderer.is_busy() && in_flight_) {
        async_renderer.request_cancel();
    }
    pending_ = false;

    // Clear status if it still says "updating..." (the iteration on_done
    // also clears it but that fires asynchronously).
    if (app.queue_progress_text == "updating...") {
        viewport.invalidate_timestamp_area();
        app.queue_progress_text.clear();
    }

    if (playback.is_playing()) {
        playback.stop();
        app.is_playing = false;
    }
    if (audio.total_frames() > 0) {
        playback.rebind_buffer(audio.samples_ptr(), audio.total_frames());
    }
    // Iteration buffer is no longer the live playback source; null out
    // its target-domain anchor so a future T→S→T round trip starts
    // with a clean slate. iteration_buffer and frames stay populated
    // (cheap; the next trigger() in target view will overwrite them).
    app.iteration_buffer_target_start_frame = 0;
}
