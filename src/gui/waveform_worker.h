#pragma once

#include "warp_frame_map.h"   // WarpFrameMapSegment

#include <atomic>
#include <cairo/cairo.h>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
// <memory> is intentionally not included: the job carries a raw GuiAudio*,
// no shared_ptr (the source audio is process-immortal and needs no keepalive).

class GuiAudio;

// The waveform-cache rebuild runs off the paint thread on a dedicated worker.
// Shape mirrors GuiAsyncRenderer byte-for-byte: single std::thread,
// single-in-flight job, condition-variable wakeup, stop-on-shutdown atomic,
// completion signaled via an eventfd that the platform run loop poll()s. The
// cache lifecycle owns the destination surface — the worker writes pixels but
// never destroys.
//
// The job-payload owns no audio bytes. It carries `const GuiAudio*` whose
// lifetime invariant is: audio outlives all in-flight jobs because main.cpp's
// GuiAudio is constructed before the worker and destroyed after
// gui.shutdown(), and the source is loaded once at launch and never replaced
// in-session, so no move-assignment ever races a live job. The only
// synchronous drain (GuiWaveformWorker::wait_until_idle) is the paint path's
// force_synchronous_waveform_rebuild, which takes over the cache surfaces
// before a one-shot rebuild.
struct WaveformJob {
    // Fingerprint inputs the worker's render uses. The cache's fp_* fields
    // are updated from these AT THE COMPLETION SWAP, not at dispatch.
    int64_t   vp_start         = 0;
    int64_t   vp_end           = 0;
    int       area_w           = 0;
    int       area_h           = 0;
    bool      target           = false;
    uint64_t  warp_frame_map_hash     = 0;

    // Frame-map snapshot the worker dereferences during the render. Populated
    // for target view from the memoized target display map (an owned copy taken
    // at job submission, so the worker never races a cache rebuild). Empty in
    // source view. The worker reads — never builds — this.
    std::vector<WarpFrameMapSegment> warp_frame_map;

    // Source-domain fallback spans the plate colors kAccent (an owned copy
    // taken at job submission from the memoized cache). Source-domain, so the
    // same set is used in both views — the worker classifies each column by the
    // source position it already computes. Empty when nothing normalizes.
    std::vector<std::pair<int64_t, int64_t>> fallback_spans;

    // Surface to render into. Owned by the cache (the cache's pending-slot
    // surface). The worker only writes pixels; the cache lifecycle owns
    // destroy/recreate.
    cairo_surface_t* surface = nullptr;
    int channel_count = 0;     // always 2 (stereo-only sources)

    // Audio handle the render reads (see lifetime invariant above). Always
    // main.cpp's single long-lived source audio — the one GuiAudio for the
    // process lifetime. It outlives every in-flight job, so the job needs no
    // shared_ptr keepalive to pin the audio: a raw pointer is sufficient (the
    // former audio-identity / lifetime-pinning axis is retired).
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

    // Block until state_ is Idle. Used by the paint path's
    // force_synchronous_waveform_rebuild to take over the cache surfaces
    // before a one-shot synchronous rebuild, guaranteeing no in-flight job
    // touches them underneath it. Caps the wait at ~1000ms; if the worker
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
// Render runs on the worker thread; nothing in this function
// touches main-thread cairo state.
// fallback_spans_or_null (source-domain, sorted, non-overlapping) colors each
// column kAccent when the source position it reads falls inside a span,
// kWaveform otherwise. Null / empty paints the whole plate kWaveform.
void render_waveform_to_cache_surface(
    cairo_surface_t* dest,
    int area_w,
    int area_h,
    int channel_count,
    const GuiAudio& audio,
    int64_t vp_start,
    int64_t vp_end,
    const std::vector<WarpFrameMapSegment>* warp_frame_map_or_null,
    const std::vector<std::pair<int64_t, int64_t>>* fallback_spans_or_null
        = nullptr);
