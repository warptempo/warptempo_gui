#pragma once

#include "notifications.h"
#include "app_state.h"
#include "async_renderer.h"
#include "audio.h"
#include "playback.h"
#include "render_cache.h"
#include "render_pipeline.h"
#include "viewport.h"

#include <chrono>
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

// THE "Updating..." RUN HOLD (architect 2026-08-08), and its two windows. Both
// read nothing but the wall-clock spacing of output-affecting triggers, whatever
// produced them — which is what lets one rule cover key repeat, a pointer drag
// and manual input with no input classification anywhere.
//
// THE LABEL IS HELD FOR A RUN'S WHOLE LIFE AND IS NEVER SUPPRESSED. Once a run
// stands, the work-completion clears stop firing — a mid-run render that
// completes, or is killed and redispatched by the next trigger, leaves the label
// standing — so a torrent of triggers shows one steady label instead of a blink
// per event. When the input goes quiet the run ends on the tick and the label
// clears once the work is idle. A SINGLE isolated trigger never starts a run and
// behaves exactly as it always did: stamp at the dispatch, clear at the
// completion, no hold and no linger. The one accepted blink is the gap between
// the first trigger's completion and the second trigger — unavoidable, since
// nothing can know a run has begun until its second event arrives.
//
// TWO NAMES SO THEY TUNE INDEPENDENTLY: they answer different questions ("is
// this a run?" and "is the run over?") and today they happen to share a value,
// which is coincidence, not coupling — change one without the other freely.

// DETECT: a trigger arriving within this of the previous one makes a RUN. It
// covers the MACHINE-CADENCE producers with headroom and nothing else — roughly
// TWICE the architect's measured 40 ms key-repeat interval (labwc repeatRate 25
// per second) and far above a drag's per-pointer-frame cadence (~8-16 ms) —
// while deliberately EXCLUDING leisurely manual tapping, which is a series of
// distinct actions and keeps the one-off lifecycle.
inline constexpr long long kUpdatingRunDetectMs = 75;

// QUIET: this long with no trigger ends the run ("the action stopped").
//
// IT MATCHES DETECT BECAUSE WHAT WE MEASURE IS NOT THE DEVICE CADENCE — it is
// the gap between two triggers as PROCESSED on the GUI thread, which is
// meanwhile dispatching and cancelling a synthesis per keystroke with every core
// busy. The loop batches under that load, so a steady 40 ms device cadence can
// present here as a 60-80 ms processed gap; a quiet window tighter than the
// jitter band would expire between two events of one continuous hold, end the
// run mid-gesture and re-introduce exactly the blink the hold exists to remove.
// 75 carries that headroom while keeping the run's cost imperceptible: it is the
// only extra time a run's FINAL label can stay up compared with a one-off (and
// only when the last render finishes before the window expires — if it outlives
// the window the run ends first and the ordinary completion clear fires with
// zero added time).
inline constexpr long long kUpdatingRunQuietMs = 75;

// Target-view live target render orchestrator. Owns the cancel-restart
// dispatch helper called from every output-affecting mutation site (marker
// edits, phase reset edits, trim hotkeys, settings commits, undo/redo, tab
// switches, S→T view toggle). The helper:
//
//   - No-ops in source view. Source view's playback continues to read
//     source.wav across archival renders unchanged.
//   - In target view: stops playback, requests cancellation of an active
//     batch/queue run when needed, and dispatches a fresh render to
//     app.target_buffer (the always-on limiter applies). The dispatch
//     is deferred until the worker is idle (the existing on_done callback paths
//     pump pending target renders through maybe_dispatch_pending()).
//     The queue_progress_text="Updating..." label is NOT stamped here: it is
//     stamped only where the update actually goes asynchronous, through the one
//     stamp helper (stamp_updating below). trigger() does own the label's RUN
//     detection, though — every call stamps the trigger clock at its top.
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
    // A FAILED preview is an event the user was not watching, so it is a
    // notification card (2026-08-29) beside its stderr line; this is the one
    // push chokepoint. A CANCELLED preview is the product's own doing and
    // says nothing.
    GuiNotifications& notifications;

    GuiTargetRender(AppState&         app_,
                    const GuiAudio&   audio_,
                    GuiAsyncRenderer& async_renderer_,
                    GuiPlayback&      playback_,
                    Viewport&         viewport_,
                    RenderCache&      render_cache_,
                    GuiNotifications& notifications_)
        : app(app_),
          audio(audio_),
          async_renderer(async_renderer_),
          playback(playback_),
          viewport(viewport_),
          render_cache(render_cache_),
          notifications(notifications_) {}

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
    // pending behind the cancellation of a prior render. Used by the two
    // Space handlers (input_handler.cpp) in target view to refuse
    // Space-to-play while an update is in progress.
    bool is_updating() const {
        return pending_ || in_flight_;
    }

    // THE PREVIEW'S READINESS FOR A LAUNCH — the one owner of "target view has
    // audio to play right now": no update in flight or pending (the bound
    // buffer is stale by definition while one is) AND a populated target
    // buffer (no successful preview render yet in this session means the bind
    // would play stale source-domain samples). THREE READERS: Space's play
    // edge (input_handler.cpp, where Space-to-stop is honored first), the A/B
    // audition's press-time gate (GuiAbAudition, which asks it for BOTH tabs
    // before its first switch and again at every launch) and, since
    // 2026-08-30, the play/stop button's enabled face (redesign_button_enabled
    // through the trampoline target_preview_ready, target_render.cpp — the
    // truthful-buttons ruling greys Play at rest in target view with no
    // preview to play, exactly where Space's edge refuses). The
    // per-position gate — the launch frame against the bound domain — is the
    // launch body's own (playback_launch_playable) and is deliberately not
    // folded in: this answers about the buffer, that about a frame in it.
    bool preview_ready() const {
        return !is_updating() && app.target_buffer_frames > 0;
    }

    // WOULD THE LIVE TRIM PAIR FALL BACK? — the archival commands' read of the
    // verdict below, and the reason it is public. Ctrl+Alt+R and
    // Ctrl+Alt+Shift+R build their request from the LIVE stores and the LIVE
    // trim, so this answers exactly what the worker's own plan_trim will decide
    // for that request; the two chords card kTrimFallbackCard on it at the
    // press (input_key_dispatch.cpp, where the whole rationale sits) rather
    // than leaving the outcome to do_render's worker-thread stderr line. It
    // asks compute_buffer_start_frame_for and nothing else — THE ONE VERDICT
    // OWNER on this thread, so no second arithmetic implementation exists to
    // drift from the orchestrators'.
    //
    // View-independent, so a source-view press answers as truthfully as a
    // target-view one: the map it consults (target_view_warp_frame_map_cached)
    // is keyed on the warp store's generation, the scale and the audio
    // identity, never on active_audio_view, and snapshot_current_authoring_state
    // already reads it from source view for the same reason.
    bool trim_would_fall_back() const;

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

    // Per-iteration hook for the "Updating..." label's RUN HOLD: the end that
    // watches the INPUT (the two context-ending clears drop a run too, but they
    // end the context rather than the gesture). Wired from main.cpp's on_tick,
    // where the reason for that site rather than another is stated. Cheap and
    // total: it returns immediately unless a run stands, and it is the clear a
    // run's own held completions defer to. Detecting the end on a clock rather
    // than at an event is what makes "the user let go" observable at all — the
    // last trigger of a run is indistinguishable from the middle of one when it
    // arrives.
    void tick_updating_hold();

private:
    // Construct and dispatch the target RenderRequest. Caller must
    // have verified the worker is idle and we are in target view.
    void dispatch_render_now();

    // Target render on_done. Rebinds playback to the (now-populated)
    // target_buffer on Success; logs cancellation. Always clears
    // queue_progress_text and re-pumps pending_ (a fresh trigger() may
    // have arrived during render).
    void on_render_done(RenderOutcome outcome);

    // THE ONE WRITER of the "Updating..." label. Both async stamp sites —
    // trigger()'s pending-behind-a-busy-worker branch and
    // dispatch_render_now()'s synthesis miss — call this instead of assigning
    // the text; the reuse rungs resolve synchronously and never call it at all.
    // It stamps whenever the label is not already showing, unconditionally: the
    // run machinery works by HOLDING the label against the completion clears,
    // never by refusing a stamp, so nothing here consults the run state or any
    // clock (rationale at the two run constants above).
    void stamp_updating();

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
    // start). The FULL window [0, total-1] renders untrimmed and anchors 0
    // exactly like the old unset state, and that is NOT a fallback —
    // trim_fell_back stays false there. It is true only for a proper SUB-WINDOW
    // that plan_trim refuses: a target span rounding below one output sample
    // (reachable), or "Trim end at or before trim begin" (a breach shape now —
    // a sub-window cannot rest crossed) — either way the render is the FULL,
    // untrimmed deliverable. This
    // verdict is the one GUI-thread read that must mirror the orchestrators' own
    // completion-then-refusal outcome, and it cannot see the worker's plan — so
    // compute_buffer_start_frame_for re-derives the
    // survival test from the same exact-double images through the same map,
    // once, and the anchor and the diagnostic both consume that single
    // result (no second arithmetic implementation). fallback_reason names
    // WHICH fallback the reuse-rung diagnostic reports — the crossed pair (a
    // breach shape now that a sub-window can never rest crossed) or a sub-sample
    // span — so its line mirrors the orchestrators' vocabulary
    // byte-for-byte; it points at a string literal (static lifetime), set
    // alongside trim_fell_back, and stays null when the render is trimmed or
    // full.
    //
    // trim_fell_back HAS THREE READERS (2026-09-02, deep dive item L): the two
    // reuse rungs' stderr lines, the PREVIEW'S CARD at the top of
    // dispatch_render_now (kTrimFallbackCard on this flag's rising edge, the
    // one outermost site every target-view dispatch passes through) and the
    // ARCHIVAL CHORDS' card through trim_would_fall_back above. Only the
    // stderr readers consult fallback_reason: the card names the outcome, not
    // the producer.
    struct BufferStartVerdict {
        int64_t     start_frame = 0;
        bool        trim_fell_back = false;
        const char* fallback_reason = nullptr;
    };

    // Compute the verdict above for an explicit trim pair. Takes BOTH bounds
    // because it must mirror the orchestrators' full-window translation and
    // their ambiguous-trim fallback: a FULL window [0, total-1] renders
    // untrimmed and anchors 0 with NO fallback flagged (it is the documented
    // default, not a refusal — recognized FIRST, ahead of the fallback
    // diagnostics, so a one-frame source's canonical [0, 0] cannot be misread as
    // crossed), and a proper sub-window anchors its begin's target image unless
    // it takes the one reachable refusal — a target span rounding below one
    // output sample — which anchors 0 too (rule mirror at the definition). No
    // buffer-frames gate: callers stamp the origin at production time, when the
    // buffer may still be empty. The stamp rests in
    // dispatched_buffer_start_frame_ below and travels to GuiPlayback with each
    // bind.
    BufferStartVerdict compute_buffer_start_frame_for(int64_t begin_frame,
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

    // THE PREVIEW CARD'S EDGE (architect 2026-09-02, deep dive item L): the
    // trim_fell_back verdict of the PREVIOUS dispatch, so dispatch_render_now
    // cards kTrimFallbackCard on the RISING edge alone — it fell back now and
    // did not at the dispatch before it.
    //
    // WHY AN EDGE HERE AND A PLAIN PRESS COUNT ON THE ARCHIVAL CHORDS: the
    // ruling is one card per deliberate act, and a preview dispatch is not one.
    // trigger() fires from every output-affecting mutation — a held arrow's
    // synthesized repeats, a marker drag's per-motion commits, a settings
    // commit, undo/redo, a tab switch — and each one re-dispatches under a
    // trim window that has not changed. The notification layer stacks
    // duplicates for everything that is not a held input's own repeat
    // (notifications.h), and these dispatches reach it off the render road
    // rather than through on_key, so they would read that bit false and stack
    // one card per keystroke. The edge answers the only thing the user has not
    // already been told: that the window he has just made is one the render
    // cannot honor.
    //
    // It is the LAST DISPATCH'S verdict and nothing more — the acts that skip a
    // dispatch entirely (ensure_ready's clean path, a T->S flip) leave it
    // standing on purpose, exactly as dispatched_buffer_start_frame_ keeps the
    // anchor of the production that is actually bound.
    bool last_dispatch_trim_fell_back_ = false;

    // THE RUN STATE, the whole of it — two fields. last_trigger_time_ is written
    // by trigger()'s top alone and read by trigger() and tick_updating_hold;
    // run_active_ is written by those two plus the two CONTEXT-ENDING clears
    // (dispatch_render_now's early refusal and cancel_in_flight_update, which
    // reset it because the surface a hold would calm is going away) and read by
    // the two WORK-COMPLETION clears, whose deferral is the hold itself.
    //
    // NOT THE LOADERS: file_loader owns the same status slot for its own
    // "Loading..." line and assigns it directly, touching neither field. That
    // costs nothing — a run left standing across a load simply finds the slot
    // already empty at its next quiet tick, where the "is the text ours" guard
    // makes the late clear a no-op, and the run state resets there as usual.
    //
    // last_trigger_time_ is stamped at the TOP of EVERY trigger() call, before
    // any of its early returns and whatever the dispatch turns out to do: a
    // reuse-resolving trigger is still part of the user's cadence, and a run
    // made of nothing but reuse hits simply holds a label that was never shown
    // (a no-op that costs one compare per completion). steady_clock: monotonic,
    // immune to a wall-clock step — the same idiom the undo tap window measures
    // on (undo.h's last_gesture_time_), deliberately reused rather than
    // introducing a second kind of clock for a second millisecond window. The
    // default-constructed epoch value would only be consulted before the first
    // trigger, so the never-triggered case is asked explicitly rather than
    // leaning on how far steady_clock's zero happens to be from now.
    std::chrono::steady_clock::time_point last_trigger_time_{};
    // True from the moment a second trigger arrives within kUpdatingRunDetectMs
    // of the previous one until kUpdatingRunQuietMs of quiet ends it on the tick.
    // While it stands, the two work-completion clears hold their fire — that
    // deferral IS the hold. A single isolated trigger never sets it, which is
    // what keeps one-shot behavior byte-identical to the pre-hold shape.
    bool run_active_ = false;

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
