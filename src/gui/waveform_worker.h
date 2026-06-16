#pragma once

#include "engine/stft_container.h"   // FrameMapSegment

#include <atomic>
#include <cairo/cairo.h>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

class GuiAudio;

// Stage A: layered-paint refactor. The waveform-cache rebuild moves off the
// paint thread onto a dedicated worker. Shape mirrors GuiAsyncRenderer
// byte-for-byte: single std::thread, single-in-flight job, condition-variable
// wakeup, stop-on-shutdown atomic, completion signaled via an eventfd that
// the platform run loop poll()s. The cache lifecycle owns the destination
// surface — the worker writes pixels but never destroys.
//
// The job-payload owns no audio bytes. It carries `const GuiAudio*` whose
// lifetime invariant is: audio outlives all in-flight jobs because (a)
// main.cpp's GuiAudio is constructed before the worker and destroyed after
// gui.shutdown(), and (b) revert_to_blank's `audio = GuiAudio{};` move-
// assignment is preceded by GuiWaveformWorker::wait_until_idle so no
// in-flight job is dereferencing the old audio when the move happens.
struct WaveformJob {
    // Fingerprint inputs the worker's render uses. The cache's fp_* fields
    // are updated from these AT THE COMPLETION SWAP, not at dispatch.
    int64_t   vp_start         = 0;
    int64_t   vp_end           = 0;
    int       area_w           = 0;
    int       area_h           = 0;
    long long audio_gen        = -1;
    bool      target           = false;
    uint64_t  timemap_hash     = 0;

    // Timemap snapshot the worker dereferences during the render. Empty for
    // source view; populated for target view from the value paint-side
    // computed (or app.drag.frozen_timemap during a target-view drag). The
    // worker reads — never builds — this.
    std::vector<FrameMapSegment> frame_map;

    // Surface to render into. Owned by the cache (the cache's pending-slot
    // surface). The worker only writes pixels; the cache lifecycle owns
    // destroy/recreate.
    cairo_surface_t* surface = nullptr;
    int channel_count = 0;     // 1 for mono, 2 for stereo

    // Audio handle (see lifetime invariant above).
    const GuiAudio* audio = nullptr;
};

class GuiWaveformWorker {
public:
    // ok=true on a clean render; ok=false on dispatch with a null surface
    // or null audio. The completion handler is responsible for the
    // swap-or-redispatch state machine.
    using DoneCallback = std::function<void(bool ok)>;

    GuiWaveformWorker();
    ~GuiWaveformWorker();

    GuiWaveformWorker(const GuiWaveformWorker&)            = delete;
    GuiWaveformWorker& operator=(const GuiWaveformWorker&) = delete;

    // Initialize the eventfd and spawn the worker thread. Returns false if
    // eventfd creation fails. Must be called before any dispatch.
    bool init();

    // Stop the worker thread and close the eventfd. Idempotent. Blocks
    // until the worker has exited — if a job is in flight, sets cancel_flag
    // first so the worker observes it before its post-render check.
    void shutdown();

    // The eventfd the platform layer polls for completion. -1 before init().
    int completion_fd() const { return completion_fd_; }

    // Dispatch a render job. Asserts the worker is idle (no job in flight).
    // Resets cancel_flag, stores job and on_done in the slot, transitions
    // state_ Idle -> Running, notifies the cv. Returns immediately.
    void dispatch(WaveformJob job, DoneCallback on_done);

    // Request cancellation of the in-flight job. Sets cancel_flag; the
    // worker observes it once before the render starts and once after, and
    // skips the render entirely on either hit. A 23-37ms worst-case render
    // is short enough that mid-render cancellation isn't necessary — the
    // file-load wait will be at most one render's worth of latency.
    void request_cancel();

    // Called by the platform layer when the completion eventfd fires.
    // Reads the result, transitions state_ CompletionPending -> Idle,
    // invokes the stored on_done callback. The platform layer is
    // responsible for read()ing the eventfd's 8-byte counter to clear it
    // before calling this method.
    void on_completion_event();

    // True while a job is in flight (worker is running OR a completion is
    // pending but on_completion_event hasn't fired yet).
    bool is_busy() const;

    // Block until state_ is Idle. Used by file_loader before swapping the
    // GuiAudio backing buffer to guarantee no in-flight job is reading the
    // soon-to-be-discarded audio. Caps the wait at ~100ms; if the worker
    // hasn't responded by then, logs to stderr and returns.
    void wait_until_idle();

private:
    enum class State : int { Idle, Running, CompletionPending };

    void worker_loop();
    void signal_completion();

    std::thread          worker_;
    std::mutex           mtx_;
    std::condition_variable cv_;

    std::atomic<int>     state_{static_cast<int>(State::Idle)};
    std::atomic<bool>    cancel_flag_{false};
    std::atomic<bool>    stop_worker_{false};

    // Job slot. Written by the GUI thread at dispatch (state Idle->Running);
    // read by the worker after the cv wake. on_done_ is read by the GUI
    // thread at completion (state CompletionPending->Idle).
    std::optional<WaveformJob> pending_job_;
    DoneCallback               on_done_;

    // Completion result. Written by the worker just before signal_completion;
    // read by the GUI thread in on_completion_event.
    bool last_ok_ = false;

    int completion_fd_ = -1;
};

// Render the waveform into `dest` from scratch (clears to transparent
// first, then strokes peaks for `channel_count` channels). Free function so
// both the worker thread and the main thread can reach it (the main thread
// invokes it indirectly only via the worker; this declaration is here for
// callers that need to render synchronously outside the worker).
//
// Stage A: render runs on the worker thread; nothing in this function
// touches main-thread cairo state. perf_counters increments inside
// render_waveform fire from the worker thread when kDebugPerf=true — see
// the comment in render.h.
void render_waveform_to_cache_surface(
    cairo_surface_t* dest,
    int area_w,
    int area_h,
    int channel_count,
    const GuiAudio& audio,
    int64_t vp_start,
    int64_t vp_end,
    const std::vector<FrameMapSegment>* timemap_or_null);
