#include "target_render.h"

#include "app_state.h"
#include "engine/engine_geometry.h"
#include "phase_reset_frame_map_build.h"
#include "render_output_naming.h"
#include "trimmer.h"
#include "warp_frame_map_build.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"
#include <chrono>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

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

    // THE PHASE-RESET COMPONENT IS THE SET THAT REACHES THE ENGINE — the
    // mirror of do_render's own fingerprint arm (render_pipeline.cpp). Under a
    // proper sub-window the engine receives plan_trim's translated,
    // range-filtered reset list, so a reset OUTSIDE the window cannot move a
    // byte; keying on the raw store made every out-of-window reset mutation
    // (nudge, drop, delete, disable, drag commit, undo) mint a fresh key, miss
    // the cache, and re-synthesize identical audio — the "Updating..." flash
    // the architect saw. The plan is obtained by CALLING plan_trim, never by
    // re-implementing its head/tail predicates: a hand-rolled margin that
    // disagreed at the boundary would drop a reset the engine actually reads
    // and produce a WRONG-BYTES cache hit, the one unsafe direction (same
    // no-second-arithmetic rule as compute_buffer_start_frame_for below).
    //
    // CROSS-SITE IDENTITY: plan_trim is a pure function of the full warp frame
    // map, the full deliverable-form reset derivation, the authored bounds,
    // total_frames and the fixed geometry (kN/kRs) — and every one of those is
    // the SAME value do_render derives from the request built out of this same
    // live state one statement later in dispatch_render_now. The full map here
    // is the memoized target-view map, which is literally
    // build_warp_frame_map(resolve(...), scale, sample_rate, total_frames) —
    // do_render's construction exactly (the mirror compute_buffer_start_frame_for
    // already relies on), and memoized so no second resolve prints a second
    // set of normalization lines.
    //
    // ARM STRUCTURE, mirroring do_render: a successful plan under a proper
    // sub-window keys on the plan's filtered list; a full window or a plan_trim
    // REFUSAL keys on the full set, because a refusal renders untrimmed by
    // ruling. The refusal string is discarded here — do_render owns the one
    // fallback line per fresh dispatch, and the reuse rungs print theirs from
    // compute_buffer_start_frame_for's verdict; a line from the key derivation
    // would double it. A FAILED map build (empty map, tripwire-class) also
    // keeps the full set: do_render refuses that recipe before it fingerprints
    // anything, so there is no dispatch-side key to disagree with. The
    // sample-rate/total-frames guard mirrors compute_buffer_start_frame_for's;
    // dispatch_render_now is unreachable without loaded audio.
    //
    // ACCEPTED COST (2026-08-01): one plan_trim per keystroke under a proper
    // sub-window, which derives a full source_frame_schedule. That is the same
    // order as the full marker resolve above plus the memoized warp-map build
    // this function already stands on — microseconds against a keypress — and
    // it is what makes the key honest.
    const bool trim_is_sub_window =
        !trim_is_full_window(app.trim, audio.total_frames());
    std::optional<TrimPlan> trim_plan;
    if (trim_is_sub_window &&
        audio.sample_rate() > 0 && audio.total_frames() > 0) {
        const std::vector<WarpFrameMapSegment>& full_warp_frame_map =
            target_view_warp_frame_map_cached(
                app, audio.sample_rate(),
                static_cast<long>(audio.total_frames())).warp_frame_map;
        if (!full_warp_frame_map.empty()) {
            auto plan = plan_trim(
                full_warp_frame_map,
                derive_phase_reset_frame_map(phase_reset_source_frames.value(),
                                             full_warp_frame_map),
                app.trim.begin_frame, app.trim.end_frame,
                audio.total_frames(), kN, kRs);
            if (plan) trim_plan = std::move(*plan);
        }
    }

    return render_fingerprint(
        app.source_audio_path, source_identity, audio.sample_rate(),
        resolved_warp_markers,
        trim_plan ? trim_plan->pre.phase_reset_frame_map
                  : phase_reset_source_frames.value(),
        app.engine_settings,
        // The full window hashes as the old unset state (the one recognition
        // owner, trim_window_is_full — see render_fingerprint's trim block).
        // The BOUNDS bytes stay the authored pair even when plan_trim refused
        // above, exactly as do_render serializes them: accepted conservatism
        // (a fallback recipe misses the equivalent explicit no-trim recipe's
        // entries), recorded at do_render's trim-plan block.
        trim_is_sub_window,
        app.trim.begin_frame, app.trim.end_frame);
}

void GuiTargetRender::trigger() {
    // RUN DETECTION, at the very top so EVERY call counts toward the cadence —
    // ahead of the source-view and no-audio returns and ahead of the dispatch,
    // whose outcome (a synchronous reuse hit or a real render) says nothing
    // about how fast the user is going. Two triggers closer together than the
    // DETECT window ARE a run; one isolated trigger is not, and can never be,
    // since a run only begins at a second event. The label is then HELD for the
    // run's life by the completion clears' deferral, and the run ends on the
    // tick a QUIET window after this stamp (tick_updating_hold). Full rationale
    // at the two constants.
    {
        const std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now();
        // Ask "never triggered" explicitly rather than trusting steady_clock's
        // zero to sit more than a window away from now.
        if (last_trigger_time_ != std::chrono::steady_clock::time_point{} &&
            now - last_trigger_time_ <
                std::chrono::milliseconds{kUpdatingRunDetectMs}) {
            run_active_ = true;
        }
        last_trigger_time_ = now;
    }

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

    // THE A/B AUDITION SEQUENCE ENDS HERE, AHEAD OF THE FREEZE'S GUARD (the
    // target_render clears, the third clearing owner at GuiAuditionSequence,
    // app_state.h): a preview invalidation in target view is an interrupt of the
    // act by ruling, and this freeze deactivates the scanner without the one
    // stop body, so the tick's natural-end branch never sees the session end and
    // could not clear it there.
    //
    // UNCONDITIONAL, ahead of the is_playing() guard below, for the same reason
    // the stop body's own clear sits ahead of ITS guard: the SUB-TICK WINDOW
    // between a bounded play's natural end and the tick that observes it is
    // exactly where a clear behind is_playing() misses — the audio thread has
    // already published false while the phase still stands. A target-view
    // mutation landing in that window (the W+target cent step, the one mutator
    // the keyboard stop rule lets through un-stopped) reaches
    // dispatch_render_now, whose synchronous reuse rungs can rebind a cached
    // preview before the tick runs; preview_ready() would then be true again and
    // the tick would launch the act's NEXT play against the new preview, when
    // the edit was required to end the act. Placing it here also makes the
    // synchronous and asynchronous reuse paths take the same interruption
    // verdict, which is what the ruling means by "every interrupt ends the whole
    // sequence where it stands".
    clear_audition_sequence(app);

    // Freeze playback. Target view's playback model is "every edit halts
    // playback; the user re-presses Space after the update completes".
    // Stop synchronously so the audio callback releases the buffer before
    // we (potentially) swap it underneath. The stop and its teardown stay
    // CONDITIONAL — they are the freeze's own quiescence work, which only a
    // live session owes — while the clear above is not.
    if (playback.is_playing()) {
        playback.stop();
        app.playhead_scanner_active = false;
        // The clock flips from the scanner's time to the resting cursor's on
        // this edge, and nothing else damages row 8's cell on the reachable
        // route (a W+target cent step during a live audition — the one
        // mutator the keyboard stop rule lets through un-stopped): the
        // step's re-land is idempotent (the focused marker's own
        // target-domain position does not move when its tempo changes), and
        // the tick's catch-up branch early-returns once BOTH flags are
        // false, which this write has just made true. Every other stop edge
        // reaches the clock through stop_playback_if_playing; this freeze is
        // its own stop and owes the same repaint. No waveform damage is
        // added: the scanner line's teardown rides the completion's own
        // full repaint moments later, but nothing later repaints the clock.
        // Caller inventory at Viewport::invalidate_clock_area (viewport.h).
        viewport.invalidate_clock_area();
    }

    // A render dispatch kills the running render. Any running archival
    // render (Ctrl+Alt+R, single or iteration sweep / Ctrl+Alt+Shift+R /
    // BPM-sweep) is on some other output;
    // kill it. The batch state machine consults queue_cancel_requested at
    // on_batch_entry_complete time and finalizes instead of dispatching the
    // next entry, so setting it here is enough.
    app.queue_cancel_requested = true;

    // NO STATUS STAMP HERE (architect 2026-08-08). At this point nobody knows
    // yet whether the update will go asynchronous at all: with an idle worker
    // dispatch_render_now often resolves SYNCHRONOUSLY on a reuse rung (the
    // render cache or the archival artifact) and returns with the buffer already
    // rebound. Stamping unconditionally made the label flash on every such
    // reuse — undo/redo A→B→A, the S→T entry with a current fingerprint, an
    // out-of-window phase-reset edit hitting the trim-aware key — for the
    // handful of microseconds until the same call cleared it, and cost the
    // bottom strip two repaints for work that never left the GUI thread. The
    // label is now stamped only where the update really becomes a wait: the
    // busy branch just below, and dispatch_render_now's synthesis miss.
    pending_ = true;
    if (async_renderer.is_busy()) {
        // Worker is mid-render on some other output. Cancel; the existing
        // on_done path will call maybe_dispatch_pending() once the worker
        // exits.
        //
        // An honest wait: a previous render is being cancelled before ours can
        // start, so surface the target-render status. "Rendering..." (archival)
        // and "Updating..." (target render) share the slot — and this stamp is
        // safe against the archival message it may be replacing precisely
        // because the cancel below makes that session dead: the archival label
        // is parked until synthesis is observed, and the promotion refuses on a
        // cancelled session (tick_promote_render_status, which owns the rule).
        stamp_updating();
        async_renderer.request_cancel();
        return;
    }
    // Worker is idle. Dispatch immediately.
    dispatch_render_now();
}

void GuiTargetRender::stamp_updating() {
    // No gate and no clock: a stamp is always honest — it means work just went
    // asynchronous. The calm comes from the HOLD (the completion clears deferring
    // while a run stands), never from refusing to say anything.
    //
    // THE REPEAT-RUN TIMELINE (the HELD cent step in W+target, each repeat
    // killing and redispatching the render — the blink the hold exists for),
    // walked against the live windows (kUpdatingRunDetectMs /
    // kUpdatingRunQuietMs, both 75) and labwc's repeat shape (575 ms delay, then
    // one repeat every 40 ms):
    //   t=0     the physical press. No previous trigger: no run. The dispatch
    //           misses the reuse rungs and stamps here — label shows.
    //   t=40    that render completes; complete_successful_buffer clears the
    //           label immediately, exactly as it always did (no run stands, so
    //           nothing holds it). A single tap ends here, with no linger.
    //   t=575   repeat 1, the compositor's repeat DELAY later. 575 is nowhere
    //           near the DETECT window, so this is still not a run: it takes the
    //           one-off lifecycle in full — stamp, then its own completion
    //           clear. THE ACCEPTED BLINKS ARE ALL BEFORE THIS POINT; nothing
    //           can see a run before its second event, and a held key does not
    //           produce one until the repeats proper start.
    //   t=615   repeat 2, one 40 ms repeat interval after repeat 1 (40 < 75):
    //           run_active_. The dispatch stamps again — label shows.
    //   t=615+  every later repeat arrives another 40 ms on, comfortably inside
    //           the window, and each one kills and redispatches; every
    //           completion and cancellation along the way HOLDS the label. It
    //           stands steady for the rest of the hold, however long the key is
    //           held.
    //   t=X     the key is released, so t=X is the last repeat. Its render lands
    //           under the hold, label still up.
    //   t=X+75  the tick finds a QUIET window with no trigger in it: run over,
    //           and with the work idle it clears + invalidates once
    //           (tick_updating_hold). Had that last render still been running at
    //           X+75, the run would simply have ended there and its own
    //           completion clear — unheld again — would have fired on time.
    // A single slow render also behaves as it always did: one stamp at the
    // start, one clear at the end, and re-entries in between find the label
    // already up and return.
    if (app.queue_progress_text == "Updating...") {
        return;
    }
    app.queue_progress_text = "Updating...";
    viewport.invalidate_status_chain_area();
}

// (THE 2026-08-13 FIELD REPORT — "the Updating.../Rendering... notices are
// gone" — was traced end to end and is NOT a defect in this label's writers,
// its damage or its painter. Its three real causes, and the honest statement
// that the chain's move into the tab row fixed none of them, are recorded once
// at GuiInputHandler::tick_promote_render_status, this label's archival twin.
// The half that belongs HERE: this stamp fires only where the update really
// becomes a wait — the synthesis miss below and trigger()'s busy branch — so
// an update served by either synchronous reuse rung is silent by the
// 2026-08-08 ruling, and a single fast one can be stamped and cleared between
// two compositor frames, the run hold covering repeat runs alone.)

void GuiTargetRender::tick_updating_hold() {
    // THE ONE END THAT WATCHES THE INPUT (the two context-ending clears also
    // drop the run, but they end the CONTEXT, not the gesture). Nothing event-
    // driven could do this job: the last trigger of a run looks exactly like the
    // middle of one when it arrives, so "the user let go" is only observable as
    // a stretch of quiet, which needs a clock somebody reads without being
    // asked. Cheap: one bool test per tick while nothing is running.
    if (!run_active_) {
        return;
    }
    if (std::chrono::steady_clock::now() - last_trigger_time_ <
        std::chrono::milliseconds{kUpdatingRunQuietMs}) {
        return;
    }
    // The counter resets. From here the completion clears are unheld again, so a
    // still-running render's own completion does the ordinary job and this
    // method deliberately does NOT pre-empt it: clearing a label over work that
    // is still in flight would lie in the one direction that matters.
    run_active_ = false;
    if (is_updating()) {
        return;
    }
    // Quiet AND idle: the run's last render already landed under the hold, so
    // its completion clear was the one that got deferred and this is that clear,
    // arriving late. Guarded on the text being OURS — an archival "Rendering..."
    // in the shared slot belongs to a live session and is not ours to erase.
    if (app.queue_progress_text == "Updating...") {
        viewport.invalidate_status_chain_area();
        app.queue_progress_text.clear();
    }
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
        // A CONTEXT-ENDING clear: the target view is gone (or the audio is), so
        // whatever run was in progress is over with it. Reset the run state and
        // clear unheld — a hold only makes sense while the surface it calms is
        // still on screen.
        run_active_ = false;
        if (app.queue_progress_text == "Updating...") {
            viewport.invalidate_status_chain_area();
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
            compute_buffer_start_frame_for(app.trim.begin_frame,
                                           app.trim.end_frame);
        dispatched_buffer_start_frame_ = verdict.start_frame;
        // Reuse skips do_render, whose trim-plan block owns the fallback
        // line on fresh dispatches — so the ruled one-line-per-resolve
        // signal prints here instead. This rung cannot see plan_trim's
        // error string; the verdict names which fallback (a sub-sample span,
        // or the crossed shape kept as a breach mirror — a sub-window can never
        // rest crossed/equal since commit and load reset it to the full window,
        // and past-EOF is adversarial load-fatal), so the reason below matches
        // the orchestrators' vocabulary byte-for-byte. A FULL window flags
        // nothing: rendering untrimmed is its documented meaning, not a
        // fallback.
        if (verdict.trim_fell_back) {
            std::fprintf(stderr,
                "warptempo_gui: %s; rendering untrimmed\n",
                verdict.fallback_reason);
        }
        complete_successful_buffer();
        return;
    }

    // This rung auditions the current-title archival deliverable only — the
    // one in the project's `render/` folder (render_output_directory,
    // render_output_naming.h), composed exactly as do_render composes it, and
    // exactly where warptempo_cli publishes the same deliverable. There is no
    // directory scan and no retitle reuse (every engine field is
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
                compute_buffer_start_frame_for(app.trim.begin_frame,
                                               app.trim.end_frame);
            dispatched_buffer_start_frame_ = verdict.start_frame;
            // Reuse skips do_render's trim-plan block; print the fallback
            // signal here — same rationale as the cache rung above (this
            // is the only other pre-do_render return). The verdict names the
            // fallback reason (a sub-sample span, or the crossed breach
            // mirror); a full window flags nothing.
            if (verdict.trim_fell_back) {
                std::fprintf(stderr,
                    "warptempo_gui: %s; rendering untrimmed\n",
                    verdict.fallback_reason);
            }
            complete_successful_buffer();
            std::fprintf(stderr,
                "warptempo_gui: Target view loaded from archival render: "
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

    // THE MISS IS WHERE THE UPDATE BECOMES A WAIT — both reuse rungs above
    // returned synchronously without touching the label — so this is one of the
    // two sites that stamp it (trigger()'s busy branch is the other), through
    // the one stamp helper.
    //
    // It stamps rather than re-stamps in general, because trigger() often has
    // not stamped at all: it never does on the idle path, and even when its busy
    // branch did, a cancelled archival's on_done (finalize_render_run) clears
    // the text in its terminal branch while the target dispatch runs inside that
    // same callback's pumping path, so the label can be down here even when the
    // busy branch put it up. Mid-run the label is usually already showing, held
    // there by the completion clears' deferral, and the helper's own
    // already-showing return makes this a no-op. Target-render status uses
    // queue_progress_text.
    stamp_updating();

    // Clear the target buffer; do_render appends synthesised samples
    // into it via std::vector::insert. The buffer's domain anchor is stamped
    // below from the request's trim values and travels to the completion rebind
    // through dispatched_buffer_start_frame_.
    app.target_buffer.clear();
    app.target_buffer_frames = 0;

    RenderRequest req = build_render_request(
        app.source_audio_path, app.warpmarkers.markers(),
        app.phaseresetmarkers.markers(), app.engine_settings,
        app.trim.begin_frame, app.trim.end_frame);
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
        compute_buffer_start_frame_for(app.trim.begin_frame,
                                       app.trim.end_frame)
            .start_frame;
    // Buffer-output route. do_render skips the on-disk rename, sidecar
    // writes, and the peak-pyramid sidecar; synth samples append into
    // *output_buffer instead. The post-engine chain runs in place on the
    // buffer: the post_trim crop when a trim PLAN exists (a plan exists for a
    // proper SUB-WINDOW that validated; a FULL window builds no plan at all and
    // renders untrimmed), and the always-on spectral + peak limited
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
        std::fprintf(stderr, "warptempo_gui: Target render cancelled\n");
        // Leave the target buffer's frames count at 0 — playback
        // gating checks this to refuse Space.
        app.target_buffer.clear();
        app.target_buffer_frames = 0;
    } else {
        std::fprintf(stderr, "warptempo_gui: Target render failed\n");
        app.target_buffer.clear();
        app.target_buffer_frames = 0;
    }

    if (outcome != RenderOutcome::Success && !run_active_) {
        // Clear status. Match finalize_render_run by invalidating the status
        // chain before clearing queue_progress_text;
        // invalidate_status_chain_area covers the TAB ROW's lane, which is the
        // label's whole home since 2026-08-13.
        //
        // HELD DURING A RUN: mid-run this branch is the CANCELLED outcome of the
        // render the next trigger just killed, and its successor is already
        // pending — clearing here would blink the label off between two renders
        // of one continuous gesture. The run's own end clears it
        // (tick_updating_hold, once the quiet window passes with the work idle).
        viewport.invalidate_status_chain_area();
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

    // Clear status. Match finalize_render_run by invalidating the status chain
    // before clearing queue_progress_text; invalidate_status_chain_area covers
    // the TAB ROW's lane, the label's whole home since 2026-08-13. GUARDED on a non-empty slot, like the two sibling
    // clears (dispatch_render_now's early refusal and cancel_in_flight_update):
    // the reuse rungs reach this tail with the label NEVER stamped — a
    // synchronous cache or artifact hit resolves without going asynchronous, so
    // nothing was shown — and clearing an already-empty string would cost the
    // bottom strip a repaint for no visible change.
    //
    // HELD DURING A RUN: a completion inside a torrent of triggers is the middle
    // of one continuous gesture, and the next trigger is already on its way, so
    // clearing here is exactly the per-event blink the hold exists to stop. The
    // run's end clears instead (tick_updating_hold, once the quiet window passes
    // with the work idle). Outside a run this is the ordinary immediate clear it
    // has always been.
    if (!run_active_ && !app.queue_progress_text.empty()) {
        viewport.invalidate_status_chain_area();
        app.queue_progress_text.clear();
    }
}

GuiTargetRender::BufferStartVerdict
GuiTargetRender::compute_buffer_start_frame_for(
    int64_t begin_frame, int64_t end_frame) const {
    // Buffer frame 0 corresponds to target frame 0 for a full-song render;
    // for every SURVIVING trim window buffer[0] IS llrint(T_b) by construction
    // — the post_trim crop cut the render at exactly the begin's target image
    // (T_b = that begin frame through the map, exact doubles, the trimmer's own
    // formula) — so the anchor is that same llrint(T_b) in full-target
    // coordinates and the exact authored begin/end display falls out.
    //
    // THE FULL-WINDOW TRANSLATION COMES FIRST (architect 2026-07-30), exactly as
    // at the other two orchestrators: a full window [0, total-1] produces NO
    // trim plan, so the buffer is the full deliverable anchored at 0 — and it is
    // NOT a fallback, so nothing is flagged and no reason string is set. Asking
    // it AHEAD of the crossed / sub-sample diagnostics is what keeps a one-frame
    // source's canonical [0, 0] from being classified as a crossed trim
    // fallback. The recognition is the shared owner trim_window_is_full
    // (settings_file.h); the lone-bound completions that used to live here died
    // with the lone bound.
    //
    // Survival verdict (orchestrator decoupling; rationale at the struct in
    // target_render.h). For a proper SUB-WINDOW, two fallbacks render the FULL,
    // untrimmed buffer with anchor 0, mirroring do_render / the CLI:
    //   - "Trim end at or before trim begin": unreachable from a resting store
    //     (a sub-window can never rest crossed/equal — commit and load reset it
    //     to the full window), kept as the breach mirror of
    //     validate_trim_frames' check ORDER; and
    //   - a SUB-SAMPLE span: llrint(T_e) - llrint(T_b) < 1 (validate_trim_frames'
    //     span rule), reachable for a narrow sub-window under a fast tempo.
    //     Past-EOF is adversarial load-fatal.
    // trim_fell_back carries either outcome to the reuse rungs' diagnostic,
    // and fallback_reason names which one so the printed line matches the
    // orchestrators' vocabulary. Callers pass the trim pair the produced
    // samples embody and stamp the result at production time, so no
    // buffer-frames gate: the buffer may still be empty at the stamp.
    if (!trim_window_is_full(begin_frame, end_frame, audio.total_frames()) &&
        audio.sample_rate() > 0 && audio.total_frames() > 0) {
        const int64_t b = begin_frame;
        const int64_t e = end_frame;
        const auto& target_warp_frame_map = target_view_warp_frame_map_cached(
            app, audio.sample_rate(),
            static_cast<long>(audio.total_frames())).warp_frame_map;
        const int64_t t_begin = std::llrint(map_source_to_target(
            static_cast<double>(b), target_warp_frame_map));
        const int64_t t_end = std::llrint(map_source_to_target(
            static_cast<double>(e), target_warp_frame_map));
        // Mirror validate_trim_frames' check ORDER: e <= b before the span rule,
        // so fallback_reason matches the orchestrator's printed vocabulary (T
        // monotone means e <= b implies the span check would also fire, but with
        // the wrong reason string). Exact source-domain integer compare; a
        // resting store cannot reach it any more, so this is the breach mirror.
        if (e <= b) {
            return {0, true, "Trim end at or before trim begin"};
        }
        if (t_end - t_begin < 1) {
            // Fallback: the FULL deliverable, anchor 0.
            return {0, true,
                    "Trim target span rounds below one output sample"};
        }
        // The anchor is the sub-window begin's target image — llrint(T_b).
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
            // The target_render clears (GuiAuditionSequence's third clearing
            // owner); unreachable in practice for the same reason the stop
            // itself is, and carried so the two facts stay one. It stays BEHIND
            // the guard rather than being hoisted the way trigger()'s was,
            // because no route reaches this branch inside the sub-tick window
            // with an act still standing: the S→T flip takes the one stop body
            // far ahead of it (handle_active_audio_view_toggle), and neither
            // other caller can reach this branch at all with a standing act —
            // maybe_reestablish_target_buffer calls in only when the buffer is
            // dirty or empty, which falls through to trigger() and its
            // unconditional clear, and the end-of-load call runs once at startup
            // with the sequence Idle by construction.
            clear_audition_sequence(app);
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
    // A CONTEXT-ENDING clear (the T→S exit): the target view the label belongs
    // to is going away, so any run goes with it and this clear is unheld.
    run_active_ = false;
    if (app.queue_progress_text == "Updating...") {
        viewport.invalidate_status_chain_area();
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
        // The target_render clears (GuiAuditionSequence's third clearing
        // owner), for the same dead-in-practice reason as ensure_ready's twin,
        // and behind the guard for the same reachability reason stated there:
        // the T→S flip is this method's ONE caller and takes the one stop body
        // ahead of it, so no sub-tick window can carry a standing act to here.
        clear_audition_sequence(app);
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
