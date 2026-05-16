#pragma once

#include "app_state.h"
#include "async_renderer.h"
#include "audio.h"
#include "playback.h"
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

    GuiTargetRender(AppState&         app_,
                    const GuiAudio&   audio_,
                    GuiAsyncRenderer& async_renderer_,
                    GuiPlayback&      playback_,
                    Viewport&         viewport_)
        : app(app_),
          audio(audio_),
          async_renderer(async_renderer_),
          playback(playback_),
          viewport(viewport_) {}

    // Output-affecting mutation hook. Called from every site that mutates
    // engine input (markers, phase resets, trim, output-affecting settings,
    // undo/redo apply, tab switch, view-domain toggle into target). No-op
    // in source view. Idempotent under rapid repeat calls — each call
    // cancels the previous in-flight target render.
    void trigger();

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

    // Target-view entry hook. Called from sites that *enter* target view
    // (S→T toggle, render-view exit while view_domain==Target). If the
    // target buffer is current (is_dirty_ == false and frames > 0),
    // rebinds playback to the existing buffer with no dispatch. Otherwise
    // falls through to trigger() to cancel-clear-dispatch a fresh render.
    // No-op in source view. Distinct from trigger() in semantics:
    // trigger() is "an edit happened, the buffer is now stale";
    // ensure_ready() is "we just entered target view, is the buffer
    // current?".
    void ensure_ready();

    // Wholesale-invalidate hook for file_loader's two buffer-clear sites
    // (load_file success branch and revert_to_blank). Sets the dirty bit
    // unconditionally; callers separately zero target_buffer / frames /
    // start_frame in the same block. A subsequent T-entry will dispatch
    // against the new source rather than re-binding to whatever was in
    // the cleared buffer's prior life. Replaced by cancel_for_load() in
    // the follow-up brief.
    void mark_dirty() { is_dirty_ = true; }

private:
    // Construct and dispatch the target RenderRequest. Caller must
    // have verified the worker is idle and we are in target view.
    void dispatch_render_now();

    // Target render on_done. Rebinds playback to the (now-populated)
    // target_buffer on Success; logs cancellation. Always clears
    // queue_progress_text and re-pumps pending_ (a fresh trigger() may
    // have arrived during render).
    void on_render_done(RenderOutcome outcome);

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
    // mutation) and by mark_dirty() (file_loader's wholesale source
    // invalidation). Cleared inside on_render_done's success branch
    // after the rebind, where the buffer is known to match the
    // engine input that produced it. Initial value true: at
    // construction the buffer is empty and no render has run, so the
    // first ensure_ready() must dispatch.
    bool is_dirty_  = true;
};
