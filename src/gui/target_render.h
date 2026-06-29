#pragma once

#include "app_state.h"
#include "async_renderer.h"
#include "audio.h"
#include "playback.h"
#include "render_cache.h"
#include "render_pipeline.h"
#include "viewport.h"

// Target-view live target render orchestrator. Owns the cancel-restart
// dispatch helper called from every output-affecting mutation site (marker
// edits, phase reset edits, trim hotkeys, settings commits, undo/redo, tab
// switches, S→T view toggle). The helper:
//
//   - No-ops in source view. Source view's playback continues to read
//     source.wav across archival renders unchanged.
//   - In target view: stops playback, cancels any in-flight render,
//     clears pending batch entries, sets queue_progress_text="updating...",
//     and dispatches a fresh render to app.target_buffer with the peak
//     limiter forced on. The dispatch is deferred until the worker is idle
//     (the existing on_done callback paths pump pending target renders
//     through maybe_dispatch_pending()).
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

    // Target view is a preview of the PGHI wav path only. Non-wav output
    // formats intentionally request sidecar data, so target view and target
    // renders are unavailable for them.
    bool target_view_available() const;

    // Pumped from finalize_render_run / on_batch_entry_complete /
    // render on_done. Dispatches the target render if pending_ is
    // set AND the worker is idle. Idempotent.
    void maybe_dispatch_pending();

    // True iff a target render is currently in flight (worker busy
    // with the target render's request) OR a target-render dispatch is
    // pending behind the cancellation of a prior render. Used by
    // toggle_playback in target view to refuse Space while an update is
    // in progress.
    bool is_updating() const {
        return pending_ || in_flight_;
    }

    // Rebind the playback device's borrowed sample buffer to source.wav.
    // Called when the user toggles target → source. Asserts playback is
    // stopped; callers (the toggle handler) stop playback before calling.
    void rebind_to_source();

    // Cancel an in-flight / pending target render and clear its "updating..."
    // status, without touching playback. Shared by the two paths that leave
    // target view: rebind_to_source (T→S) calls it, and the render-view entry
    // (GuiRenderView::load_render_view_at) calls it directly. No-op when
    // nothing is updating (e.g. render-to-render navigation).
    void cancel_in_flight_update();

    // Target-view entry hook. Called from sites that *enter* target view
    // (S→T toggle, render-view exit while active_audio_view==Target). If the
    // target buffer is current (is_dirty_ == false and frames > 0),
    // rebinds playback to the existing buffer with no dispatch. Otherwise
    // falls through to trigger() to cancel-clear-dispatch a fresh render.
    // No-op in source view. Distinct from trigger() in semantics:
    // trigger() is "an edit happened, the buffer is now stale";
    // ensure_ready() is "we just entered target view, is the buffer
    // current?".
    void ensure_ready();

    // Force target view back to source view, preserving the playhead's screen
    // column and cancelling any target render work. Used when an engine setting
    // change makes target view unavailable.
    void leave_target_view();

    // File-load entry hook. Called by GuiFileLoader before it tears
    // down the live source audio. Sets is_dirty_; clears pending_; if
    // in_flight_, requests worker cancellation and leaves the buffer
    // clear to on_render_done's Cancelled branch (asynchronous); else
    // synchronously clears target_buffer / frames / start_frame.
    // Replaces an earlier inline buffer-clear.
    void cancel_for_load();

private:
    // Construct and dispatch the target RenderRequest. Caller must
    // have verified the worker is idle and we are in target view.
    void dispatch_render_now();

    // Target render on_done. Rebinds playback to the (now-populated)
    // target_buffer on Success; logs cancellation. Always clears
    // queue_progress_text and re-pumps pending_ (a fresh trigger() may
    // have arrived during render).
    void on_render_done(RenderOutcome outcome);

    // Single source of truth for app.target_buffer_start_frame: the
    // full-target-frame coordinate that target_buffer[0] represents. 0 for a
    // full-song (no-trim) render; with trim set, the trim-begin source frame
    // mapped through the target-view frame_map (the engine renders only the trim
    // range, so buffer frame 0 is the trim's target-frame start). Requires
    // app.target_buffer_frames to be set. Called from on_render_done (after a
    // fresh render) and from ensure_ready's clean rebind (so a cached buffer
    // re-entered without a render gets the same anchor it had at render time).
    void recompute_target_buffer_start_frame();

    // Render fingerprint of the most recent dispatch. Computed at the top of
    // dispatch_render_now() and consumed in on_render_done() to key the cache
    // insert, so the buffer is stored under the exact engine input that
    // produced it. Renders are serialized (one in flight; trigger() cancels
    // before re-dispatch), so a Success in on_render_done() always pairs with
    // the fingerprint set at its own dispatch.
    std::vector<uint8_t> last_fingerprint_;

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
    // mutation) and by cancel_for_load() (file_loader's wholesale source
    // invalidation). Cleared inside on_render_done's success branch
    // after the rebind, where the buffer is known to match the
    // engine input that produced it. Initial value true: at
    // construction the buffer is empty and no render has run, so the
    // first ensure_ready() must dispatch.
    bool is_dirty_  = true;
};
