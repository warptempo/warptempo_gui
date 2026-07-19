#pragma once

#include "app_state.h"
#include "async_renderer.h"
#include "audio.h"
#include "playback.h"
#include "render_cache.h"
#include "render_pipeline.h"
#include "viewport.h"

#include <cstdint>
#include <functional>
#include <vector>

// The live state's would-be render fingerprint: the same render_fingerprint
// computation, over the same inputs (env quartet, source path + load-time
// identity, sample rate, the RESOLVED marker state of both stores, engine
// fields, trim), that a dispatch of the current state performs. The identity
// bytes are the load-time pair (GuiAudio's recorded size/mtime), taken
// directly with no on-disk re-verification: the loaded source is immutable
// for the process lifetime, so the captured identity is authoritative —
// exactly as do_render builds its own fingerprint. Runs its OWN marker
// resolve (this site stands alone — rationale at the definition), so the
// resolver's per-resolve stderr lines print here too for a resting ambiguous
// store — the intended signal. Its sole consumer is the target-view reuse
// rungs (dispatch_render_now).
std::vector<uint8_t> compute_live_render_fingerprint(const AppState& app,
                                                     const GuiAudio& audio);

// Target-view live target render orchestrator. Owns the cancel-restart
// dispatch helper called from every output-affecting mutation site (marker
// edits, phase reset edits, trim hotkeys, settings commits, undo/redo, tab
// switches, S→T view toggle). The helper:
//
//   - No-ops in source view. Source view's playback continues to read
//     source.wav across archival renders unchanged.
//   - In target view: stops playback, requests cancellation of an active
//     batch/queue run when needed. It sets
//     queue_progress_text="updating..." and dispatches a fresh render to
//     app.target_buffer (the always-on limiter applies). The dispatch
//     is deferred until the worker is idle (the existing on_done callback paths
//     pump pending target renders through maybe_dispatch_pending()).
//
// On completion the render on_done rebinds the playback device's
// borrowed pointer to app.target_buffer via GuiPlayback::rebind_buffer
// so the next Space-to-play reads the warped audio.
struct GuiTargetRender {
    AppState&         app;
    const GuiAudio&   audio;
    GuiAsyncRenderer& async_renderer;
    GuiPlayback&      playback;
    Viewport&         viewport;
    RenderCache&      render_cache;

    GuiTargetRender(AppState&         app_,
                    const GuiAudio&   audio_,
                    GuiAsyncRenderer& async_renderer_,
                    GuiPlayback&      playback_,
                    Viewport&         viewport_,
                    RenderCache&      render_cache_)
        : app(app_),
          audio(audio_),
          async_renderer(async_renderer_),
          playback(playback_),
          viewport(viewport_),
          render_cache(render_cache_) {}

    // Output-affecting mutation hook. Called from every site that mutates
    // engine input (markers, phase resets, trim, output-affecting settings,
    // undo/redo apply, tab switch, view-domain toggle into target). No-op
    // in source view. Idempotent under rapid repeat calls — each call
    // cancels the previous in-flight target render.
    void trigger();

    // Pumped from finalize_render_run / on_batch_entry_complete /
    // render on_done. Offers the worker-idle beat to the parked archival
    // command first (dispatch_pending_archival below), then dispatches the
    // target render if pending_ is set AND the worker is still idle.
    // Idempotent.
    void maybe_dispatch_pending();

    // Installed by GuiInputHandler at construction: dispatches the one-slot
    // parked archival command (app.pending_archival) when armed and the
    // worker is idle; returns true iff it dispatched. Consulted at the head
    // of maybe_dispatch_pending, ahead of the pending preview — rationale
    // there. Null only before GuiInputHandler exists, when no key input can
    // have parked a command yet.
    std::function<bool()> dispatch_pending_archival;

    // True iff a target render is currently in flight (worker busy
    // with the target render's request) OR a target-render dispatch is
    // pending behind the cancellation of a prior render. Used by
    // toggle_playback in target view to refuse Space while an update is
    // in progress.
    bool is_updating() const {
        return pending_ || in_flight_;
    }

    // True iff app.target_buffer is potentially stale relative to the current
    // engine input (set by trigger() on any output-affecting mutation, cleared
    // after the completion rebind). Consulted by the archival on_done tail to
    // decide whether the target buffer actually needs re-establishing.
    bool is_dirty() const {
        return is_dirty_;
    }

    // Rebind the playback device's borrowed sample buffer to source.wav.
    // Called when the user toggles target → source. Asserts playback is
    // stopped; callers (the toggle handler) stop playback before calling.
    void rebind_to_source();

    // Cancel an in-flight / pending target render and clear its "updating..."
    // status, without touching playback. Called by rebind_to_source (T→S) when
    // leaving target view. No-op when nothing is updating.
    void cancel_in_flight_update();

    // Target-view entry hook. Called from sites that *enter* target view
    // (the S→T toggle). If the target buffer is current (is_dirty_ == false
    // and frames > 0),
    // rebinds playback to the existing buffer with no dispatch. Otherwise
    // falls through to trigger() to cancel-clear-dispatch a fresh render.
    // No-op in source view. Distinct from trigger() in semantics:
    // trigger() is "an edit happened, the buffer is now stale";
    // ensure_ready() is "we just entered target view, is the buffer
    // current?".
    void ensure_ready();

private:
    // Construct and dispatch the target RenderRequest. Caller must
    // have verified the worker is idle and we are in target view.
    void dispatch_render_now();

    // Target render on_done. Rebinds playback to the (now-populated)
    // target_buffer on Success; logs cancellation. Always clears
    // queue_progress_text and re-pumps pending_ (a fresh trigger() may
    // have arrived during render).
    void on_render_done(RenderOutcome outcome);

    // Shared Success tail for cache hits, archival artifact loads, and worker
    // completions. Cache insertion is owned by the render worker; target_render
    // only consumes lookup/artifact results and binds the completed buffer.
    void complete_successful_buffer();

    // One trim-survival verdict per dispatch, driving BOTH the buffer's
    // domain anchor and the fallback diagnostic. start_frame is the domain
    // offset for the target-buffer playback bind: the full-target-frame
    // coordinate that target_buffer[0] represents — 0 for a full-song
    // (no-trim) render; with a surviving trim, the trim-begin source frame
    // mapped through the target-view warp_frame_map (the engine renders
    // only the trim range, so buffer frame 0 is the trim's target-frame
    // start). A lone bound is COMPLETED to its extreme at the render boundary
    // — (0, X) or (X, total) — and renders its window like any pair, so a lone
    // bound no longer forces a fallback; trim_fell_back is true for one of the
    // two reachable refusal fallbacks: "trim end at or before trim begin"
    // (reachable exactly via a lone END at frame 0 completing to (0, 0)) or a
    // surviving pair/completed window whose target span rounds below one output
    // sample — either way the render is the FULL, untrimmed deliverable. This
    // verdict is the one GUI-thread read that must mirror the orchestrators' own
    // completion-then-refusal outcome, and it cannot see the worker's plan — so
    // compute_buffer_start_frame_for re-derives the
    // survival test from the same exact-double images through the same map,
    // once, and the anchor and the diagnostic both consume that single
    // result (no second arithmetic implementation). fallback_reason names
    // WHICH fallback the reuse-rung diagnostic reports — the crossed (0, 0) case
    // or a sub-sample span — so its line mirrors the orchestrators' vocabulary
    // byte-for-byte; it points at a string literal (static lifetime), set
    // alongside trim_fell_back, and stays null when the render is trimmed or
    // full.
    struct BufferStartVerdict {
        int64_t     start_frame = 0;
        bool        trim_fell_back = false;
        const char* fallback_reason = nullptr;
    };

    // Compute the verdict above for an explicit trim pair. Takes BOTH
    // bounds because it must mirror the orchestrators' ambiguous-trim
    // fallbacks: a lone bound completes to its extreme and renders that
    // window like any pair, so only the two reachable refusals still render
    // the FULL deliverable — the completed (0, 0) crossed case (a lone END
    // at frame 0) and a (possibly completed) pair whose target span rounds
    // below one output sample — anchor 0 in both, not the begin's target
    // image (rule mirror at the definition). No buffer-frames gate: callers
    // stamp the origin at production time, when the buffer may still be
    // empty. The stamp rests in dispatched_buffer_start_frame_ below and
    // travels to GuiPlayback with each bind.
    BufferStartVerdict compute_buffer_start_frame_for(bool has_begin,
                                                      int64_t begin_frame,
                                                      bool has_end,
                                                      int64_t end_frame) const;

    // Render fingerprint of the most recent dispatch. Computed at the top of
    // dispatch_render_now() and used for target-view cache/artifact lookups.
    // The worker computes the same fingerprint for fresh target-route inserts.
    std::vector<uint8_t> last_fingerprint_;

    // Full-target-domain frame coordinate that element zero of app.target_buffer
    // represents for the production currently in flight or most recently
    // completed. Stamped at each production site in dispatch_render_now from the
    // trim values the produced samples embody, and consumed by the completion
    // rebind and ensure_ready's clean rebind. Freezing it at production keeps a
    // mid-flight trim drag — the one gesture that mutates the live authored
    // store before its commit — from mislabeling a buffer with a bound its
    // samples never embodied.
    int64_t dispatched_buffer_start_frame_ = 0;

    // Set true by trigger() when a dispatch is wanted but the worker
    // wasn't idle. Cleared once dispatch_render_now is actually
    // issued.
    bool pending_   = false;
    // Set true at dispatch time, cleared at on_render_done entry.
    // Allows is_updating() to report "still updating" between dispatch
    // and completion.
    bool in_flight_ = false;
    // True iff app.target_buffer is potentially stale relative to the
    // current engine input. Set by trigger() (any output-affecting
    // mutation). Cleared inside on_render_done's success branch
    // after the rebind, where the buffer is known to match the
    // engine input that produced it. Initial value true: at
    // construction the buffer is empty and no render has run, so the
    // first ensure_ready() must dispatch.
    bool is_dirty_  = true;
};
