#include "target_render.h"

#include "app_state.h"
#include "phase_reset_frame_map_build.h"
#include "render_output_naming.h"
#include "warp_frame_map_build.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <utility>

std::vector<uint8_t> compute_live_render_fingerprint(const AppState& app,
                                                     const GuiAudio& audio) {
    // The fingerprint's identity bytes are the load-time pair (GuiAudio's
    // recorded size/mtime), taken directly with no on-disk re-stat: the loaded
    // source is immutable for the process lifetime, so the captured identity is
    // authoritative — exactly as do_render builds its own fingerprint.
    RenderFileIdentity source_identity;
    source_identity.size  = audio.source_load_size();
    source_identity.mtime = audio.source_load_mtime();

    // The fingerprint serializes the RESOLVED marker state (the exact
    // engine inputs), and the only caller — the reuse rungs in
    // dispatch_render_now — has no resolve of its own, so this site runs its
    // own resolve and ACCEPTS the resolver's one-line-per-normalization-
    // per-resolve stderr output (same signal, same store as the render's own
    // resolve; a resting ambiguous store re-printing is the intended
    // ambiguity signal). Cost: one fingerprint = one resolve + serializing a
    // few hundred fields + FNV — microseconds against a keypress; the env
    // component is a per-process constant.
    auto resolved_warp_markers = resolve_warp_markers_for_render(
        slice_to_warp_markers(app.warpmarkers.markers()),
        audio.sample_rate(), static_cast<long>(audio.total_frames()));
    auto phase_reset_source_frames = build_phase_reset_source_frames(
        slice_to_phase_reset_markers(app.phaseresetmarkers.markers()),
        audio.sample_rate(), audio.total_frames());
    // The resolver above returns a plain vector — a total normalizer with no
    // error arm — so it needs no unwrap. The phase-reset assembly keeps its
    // std::expected: its only refusal (a past-EOF reset) cannot reach a live
    // store (gesture walls clamp to total-1 and a past-EOF sidecar is
    // adversarial load-fatal), so .value() makes a breach loud
    // (bad_expected_access -> terminate) rather than silently degrading.
    return render_fingerprint(
        app.source_audio_path, source_identity, audio.sample_rate(),
        resolved_warp_markers, phase_reset_source_frames.value(),
        app.engine_settings,
        app.trim.has_begin, app.trim.begin_frame,
        app.trim.has_end,   app.trim.end_frame);
}

void GuiTargetRender::trigger() {
    // Output-affecting mutation hook: the buffer is now stale relative
    // to engine input. Set the bit unconditionally — even in source
    // view — so a later S→T ensure_ready() sees the staleness and
    // dispatches against the accumulated source-view edits rather than
    // re-binding to a buffer that no longer matches the live state.
    //
    // Triggers are unconditional product-wide — no fingerprint pre-detection
    // on any mutation path (architect ruling 2026-07-17). Mutation sites call
    // this hook even when the mutation provably leaves render identity
    // unchanged (a normalization-inert marker gesture, e.g. a disabled-marker
    // retouch, or an undo/redo restoring only such state): using the GUI during a
    // render is allowed to cancel and re-render — the longest expected render
    // is short, and dispatch_render_now's reuse rungs absorb an
    // identity-unchanged re-derive as a cache hit, so pre-detection would buy
    // nothing but a parallel classification surface. (A fingerprint-gated
    // design existed briefly and was removed by this ruling.)
    is_dirty_ = true;
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
    }

    // A render dispatch kills the running render. Any running archival
    // render (Ctrl+Alt+R / Ctrl+Alt+I / BPM-sweep) is on some other output;
    // kill it. The batch state machine consults queue_cancel_requested at
    // on_batch_entry_complete time and finalizes instead of dispatching the
    // next entry, so setting it here is enough.
    app.queue_cancel_requested = true;

    // Surface the target-render status through queue_progress_text.
    // "rendering..." (archival) and "updating..." (target render) share
    // the slot.
    app.queue_progress_text = "updating...";
    viewport.invalidate_timestamp_area();

    pending_ = true;
    if (async_renderer.is_busy()) {
        // Worker is mid-render on some other output. Cancel; the existing
        // on_done path will call maybe_dispatch_pending() once the worker
        // exits.
        async_renderer.request_cancel();
        return;
    }
    // Worker is idle. Dispatch immediately.
    dispatch_render_now();
}

void GuiTargetRender::maybe_dispatch_pending() {
    if (async_renderer.is_busy())   return;
    // The one-slot parked archival command dispatches ahead of the pending
    // preview: an explicit user command outranks a derived preview. The
    // preview stays pending_ behind the new session — the same
    // defer-behind-a-busy-worker shape as always — and re-derives, or
    // adopts the new session's output through the reuse rungs, when that
    // session ends and re-pumps this method.
    if (dispatch_pending_archival && dispatch_pending_archival()) return;
    if (!pending_)                  return;
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
    if (app.active_audio_view != 'T' ||
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

    // The preview always proceeds from here: the parser resolver normalizes
    // ambiguous marker arrangements to tempo 1.00 at resolve time (one
    // stderr line per timestamp), so there is no store state to refuse; a
    // build failure is tripwire-class and the existing failure path handles
    // it.

    // Consult the render cache before synthesizing. The only callers
    // (trigger()'s idle branch and maybe_dispatch_pending()) reach here only
    // when the worker is idle, so the GUI thread exclusively owns
    // target_buffer and can fill it from the cache without racing the worker.
    // The fingerprint covers the same engine input build_render_request packs
    // below. Every target-view source carries identical deliverable-lattice
    // samples: fresh limited renders quantize in place at publication, cache
    // hits decode the exact codec roundtrip of those samples, and archival
    // artifact loads decode the same deliverable.
    //
    // The identity build lives in compute_live_render_fingerprint, using the
    // load-time-captured source identity with no on-disk re-stat: the loaded
    // source is immutable for the process lifetime, so do_render renders from
    // the in-memory source samples with no source-changed refusal.
    last_fingerprint_ = compute_live_render_fingerprint(app, audio);

    if (render_cache.lookup(last_fingerprint_, audio.channels(),
                            audio.sample_rate(), app.target_buffer)) {
        // Hit: target_buffer now holds the cached audio. Mirror
        // on_render_done()'s Success tail with no async render; in_flight_
        // stays false since no worker round trip is pending. Live state IS the
        // request state on this synchronous rung, so the stamp is exact.
        const BufferStartVerdict verdict =
            compute_buffer_start_frame_for(app.trim.has_begin,
                                           app.trim.begin_frame,
                                           app.trim.has_end,
                                           app.trim.end_frame);
        dispatched_buffer_start_frame_ = verdict.start_frame;
        // Reuse skips do_render, whose trim-plan block owns the fallback
        // line on fresh dispatches — so the ruled one-line-per-resolve
        // signal prints here instead. This rung cannot see plan_trim's
        // error string; the verdict names which fallback (the completed
        // (0, 0) crossed case — reachable only via a lone END at frame 0 —
        // or a sub-sample span; a set pair can never rest crossed/equal since
        // commit and load auto-clear, and past-EOF is adversarial
        // load-fatal), so the reason below matches the orchestrators'
        // vocabulary byte-for-byte.
        if (verdict.trim_fell_back) {
            std::fprintf(stderr,
                "warptempo_gui: %s; rendering untrimmed\n",
                verdict.fallback_reason);
        }
        complete_successful_buffer();
        return;
    }

    // This rung auditions the current-title archival deliverable only —
    // there is no directory scan and no retitle reuse (every engine field is
    // in the key, so a provenance edit changes the fingerprint and simply
    // re-renders). Fresh renders, cache hits, and archival artifact loads all
    // expose identical deliverable-lattice samples because fresh limited
    // renders quantize in place before cache publication and the codec
    // roundtrip is exact.
    ArtifactStatIdentity candidate_identity{};
    const std::string artifact_candidate =
        compose_render_output_path(
            render_output_directory(app.source_audio_path),
            render_output_stem(app.engine_settings))
            .string();
    if (fingerprint_sidecar_matches(artifact_candidate, last_fingerprint_,
                                    &candidate_identity) &&
        read_wav_to_float(artifact_candidate, audio.channels(),
                          audio.sample_rate(), app.target_buffer)) {
        // Identity bind (TOCTOU): re-stat AFTER the read completes and
        // require the exact wav object the sidecar validation saw (dev,
        // inode, size, mtime_ns — the capture at
        // fingerprint_sidecar_matches). A mismatch means a concurrent
        // GUI/CLI publication replaced the wav between validation and
        // read — the buffer may hold another recipe's audio.
        ArtifactStatIdentity post_read{};
        if (stat_artifact_identity(artifact_candidate, post_read) &&
            post_read == candidate_identity) {
            // Live state IS the request state on this synchronous rung,
            // so the stamp is exact.
            const BufferStartVerdict verdict =
                compute_buffer_start_frame_for(app.trim.has_begin,
                                               app.trim.begin_frame,
                                               app.trim.has_end,
                                               app.trim.end_frame);
            dispatched_buffer_start_frame_ = verdict.start_frame;
            // Reuse skips do_render's trim-plan block; print the fallback
            // signal here — same rationale as the cache rung above (this
            // is the only other pre-do_render return). The verdict names the
            // fallback reason (the completed (0, 0) crossed case or a
            // sub-sample span).
            if (verdict.trim_fell_back) {
                std::fprintf(stderr,
                    "warptempo_gui: %s; rendering untrimmed\n",
                    verdict.fallback_reason);
            }
            complete_successful_buffer();
            std::fprintf(stderr,
                "warptempo_gui: target view loaded from archival render: "
                "%s\n",
                artifact_candidate.c_str());
            return;
        }
        // Discard the suspect read and fall through exactly as if the
        // candidate had not matched — silent, byte-identical to a
        // no-candidate miss (the synthesis path below clears the buffer
        // before dispatch; no locks, no retries).
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
    // into it via std::vector::insert. The buffer's domain anchor is stamped
    // below from the request's trim values and travels to the completion rebind
    // through dispatched_buffer_start_frame_.
    app.target_buffer.clear();
    app.target_buffer_frames = 0;

    RenderRequest req = build_render_request(
        app.source_audio_path, app.warpmarkers.markers(),
        app.phaseresetmarkers.markers(), app.engine_settings,
        app.trim.has_begin, app.trim.begin_frame,
        app.trim.has_end,   app.trim.end_frame);
    // Freeze the buffer's domain origin from the same trim values the request
    // was built with. At this instant they equal app.trim, but the request
    // snapshot is immutable while app.trim is not: a trim drag mutates the live
    // authored store before its release commits, so the async completion
    // consumes this stamp rather than recomputing the origin from a store that
    // may have drifted mid-render. No fallback print here even when the
    // verdict says fell-back: this fresh dispatch runs do_render, whose own
    // trim-plan block prints the one line per resolve — printing at this
    // stamp too would double it.
    dispatched_buffer_start_frame_ =
        compute_buffer_start_frame_for(app.trim.has_begin, app.trim.begin_frame,
                                       app.trim.has_end,   app.trim.end_frame)
            .start_frame;
    // Buffer-output route. do_render skips the on-disk rename, sidecar
    // writes, and the peak-pyramid sidecar; synth samples append into
    // *output_buffer instead. The post-engine chain runs in place on the
    // buffer: the post_trim crop when a trim PLAN exists (a plan exists for
    // any set bound whose — possibly completed — window validated; a lone
    // bound completes to its extreme at the render boundary and plans that
    // window like any pair), and the always-on spectral + peak limited
    // chain — the target-view preview gets the same cropping and limiting
    // as the disk path.
    req.output_buffer = &app.target_buffer;
    // The process's single RenderCache (this struct's own member, wired from
    // main.cpp). do_render uses it on this path only to queue the writer-thread
    // canonical encode after a successful limited target render.
    req.render_cache = &render_cache;
    req.source_samples = audio.samples_shared();
    req.source_load_size = audio.source_load_size();
    req.source_load_mtime = audio.source_load_mtime();

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
    } else {
        std::fprintf(stderr, "warptempo_gui: target render failed\n");
        app.target_buffer.clear();
        app.target_buffer_frames = 0;
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
    // Only rebind if we're actually showing target audio. A T->S toggle
    // during render already cancelled this render and does not want playback
    // bound to target_buffer: rebinding here would clobber source.wav. Gate it
    // out by active_audio_view.
    if (app.active_audio_view == 'T' &&
        app.target_buffer_frames > 0) {
        // The trigger always freezes playback before dispatch, so the device
        // should still be stopped here. The rebind helper refuses if the device
        // is somehow playing. The bind carries the buffer's domain offset —
        // the full-target-frame coordinate target_buffer[0] represents (0, or
        // the trim-mapped anchor) — stamped at production time from the trim
        // values these samples embody, so a mid-render trim drift cannot
        // mislabel the bind.
        playback.rebind_buffer(app.target_buffer.data(),
                               app.target_buffer_frames,
                               dispatched_buffer_start_frame_);
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

GuiTargetRender::BufferStartVerdict
GuiTargetRender::compute_buffer_start_frame_for(
    bool has_begin, int64_t begin_frame,
    bool has_end, int64_t end_frame) const {
    // Buffer frame 0 corresponds to target frame 0 for a full-song render;
    // for every SURVIVING trim window buffer[0] IS llrint(T_b) by construction
    // — the post_trim crop cut the render at exactly the (possibly completed)
    // begin's target image (T_b = that begin frame through the map, exact
    // doubles, the trimmer's own formula) — so the anchor is that same
    // llrint(T_b) in full-target coordinates and the exact authored begin/end
    // display falls out. A lone bound is COMPLETED to its extreme here, exactly
    // as the orchestrators do at the render boundary: a missing begin becomes 0
    // (so a lone end anchors 0 because its completed T_b = 0), a missing end
    // becomes total_frames (a lone begin anchors its own T_b). The completion
    // lives only at the render boundary; the store keeps the authored lone
    // bound.
    //
    // Survival verdict (orchestrator decoupling; rationale at the struct in
    // target_render.h). Two reachable fallbacks render the FULL, untrimmed
    // buffer with anchor 0, mirroring do_render / the CLI:
    //   - "trim end at or before trim begin": reachable only via a lone END at
    //     frame 0 completing to (0, 0) (a set pair can never rest crossed/equal:
    //     commit and load auto-clear); and
    //   - a SUB-SAMPLE span: llrint(T_e) - llrint(T_b) < 1 (validate_trim_frames'
    //     span rule), for a set pair or a completed lone window under a fast
    //     tempo. Past-EOF is adversarial load-fatal.
    // trim_fell_back carries either outcome to the reuse rungs' diagnostic,
    // and fallback_reason names which one so the printed line matches the
    // orchestrators' vocabulary. Callers pass the trim pair the produced
    // samples embody and stamp the result at production time, so no
    // buffer-frames gate: the buffer may still be empty at the stamp.
    if ((has_begin || has_end) &&
        audio.sample_rate() > 0 && audio.total_frames() > 0) {
        const int64_t b = has_begin ? begin_frame : 0;
        const int64_t e = has_end ? end_frame : audio.total_frames();
        const auto& target_warp_frame_map = target_view_warp_frame_map_cached(
            app, audio.sample_rate(),
            static_cast<long>(audio.total_frames())).warp_frame_map;
        const int64_t t_begin = std::llrint(map_source_to_target(
            static_cast<double>(b), target_warp_frame_map));
        const int64_t t_end = std::llrint(map_source_to_target(
            static_cast<double>(e), target_warp_frame_map));
        // Mirror validate_trim_frames' check ORDER: e <= b before the span rule,
        // so fallback_reason matches the orchestrator's printed vocabulary for
        // the completed-(0, 0) case (T monotone means e <= b implies the span
        // check would also fire, but with the wrong reason string). Exact
        // source-domain integer compare.
        if (e <= b) {
            return {0, true, "trim end at or before trim begin"};
        }
        if (t_end - t_begin < 1) {
            // Fallback: the FULL deliverable, anchor 0.
            return {0, true,
                    "trim target span rounds below one output sample"};
        }
        // The anchor is the (completed) begin's target image — llrint(T_b),
        // bit-identical to the set-pair expression.
        return {t_begin, false, nullptr};
    }
    return {0, false, nullptr};
}

void GuiTargetRender::ensure_ready() {
    // Source view does not use target_buffer. Match trigger()'s
    // source-view no-op invariant.
    if (app.active_audio_view != 'T') {
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
        // S→T toggle handler does), but a future caller that forgets
        // shouldn't get a silent refused-rebind.
        // THE QUIESCENCE FENCE IS THE STOP EDGES' JOB, not this gate's — which is
        // why this stays CONDITIONAL on is_playing(). Every route that ends playback
        // takes `GuiPlayback::stop()` there: Space's stop edge, the gesture stop
        // (stop_playback_if_playing, which fires on the SCANNER flag too), and — since
        // 2026-07-29 — the tick's natural-end branch, which is what closed the one
        // bypass this conditional could not see (a post-natural-end rebind with both
        // flags false and the fence untaken). Do not make this unconditional: it would
        // pay the fence on every clean re-entry for a race the stop edges already own.
        if (playback.is_playing()) {
            playback.stop();
            app.playhead_scanner_active = false;
        }
        // Restore the domain offset the cached buffer was rendered with.
        // rebind_to_source() (the T→S leg) rebinds the source at offset 0,
        // but a clean re-entry rebinds the SAME target buffer without a
        // render, so the anchor must be handed back to the bind here or
        // target-view play judges the playhead past the buffer's domain and
        // silently refuses (no audio, no playhead move). The buffer's content
        // is unchanged since its last production (is_dirty_ false, frames > 0),
        // so the stamp from that production is exactly this content's origin.
        playback.rebind_buffer(app.target_buffer.data(),
                               app.target_buffer_frames,
                               dispatched_buffer_start_frame_);
        return;
    }

    // Dirty or empty buffer: dispatch fresh. trigger() re-sets the bit
    // (already true here by construction) and runs the
    // cancel-clear-dispatch sequence. Identical body to the original S→T
    // eager-dispatch path that this method replaces.
    trigger();
}

void GuiTargetRender::cancel_in_flight_update() {
    // The in-flight render is now navigated-away-from work. Cancel it and drop
    // any pending dispatch. The worker clears target_buffer / frames in
    // on_render_done's Cancelled branch after it exits; is_dirty_ stays set (the
    // edit that triggered the render is still unrendered), so the next true
    // target-view entry re-renders. Does not touch playback — callers own the
    // rebind (source.wav for T→S).
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
    // source.wav, not target_buffer.
    cancel_in_flight_update();

    // Conditional by design, for the reason stated at ensure_ready's twin above:
    // the rebind_buffer QUIESCENCE FENCE is taken at the stop EDGES (Space, the
    // gesture stop, and the tick's natural-end branch since 2026-07-29), so by the
    // time a toggle reaches here a stopped session has already been fenced.
    if (playback.is_playing()) {
        playback.stop();
        app.playhead_scanner_active = false;
    }
    if (audio.total_frames() > 0) {
        // Domain offset 0: the source is its own domain origin. The target
        // buffer's anchor travels with its own bind, so nothing to null here;
        // a future T→S→T round trip hands ensure_ready's recomputed anchor
        // back to the rebind. target_buffer and frames stay populated
        // (cheap; the next trigger() in target view will overwrite them).
        playback.rebind_buffer(audio.samples_ptr(), audio.total_frames(), 0);
    }
}
