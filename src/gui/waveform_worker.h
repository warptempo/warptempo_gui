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

// THE PLATE PAIR: the two ARGB32 surfaces ONE waveform job renders under ONE
// fingerprint — the PLAIN plate (the three bands in their plain inks) and the
// REGION plate (the same bands, same reads, same bars, in the three selected
// inks; identical binary alpha and geometry by construction). The paint pass
// blits the plain plate whole and the region plate through the live region
// clip, which is what keeps the trim span out of the fingerprint. The pair is
// ONE VALUE everywhere it rests or moves: WaveformCache (paint_handler.h)
// states the atomic all-or-nothing invariant once and owns every allocation
// and destruction; the job only writes pixels into a pair it was handed. Both
// members are always allocated together, swapped together and destroyed
// together; `complete()` is the one predicate a consumer asks before reading
// either.
struct WaveformPlatePair {
    cairo_surface_t* plain  = nullptr;
    cairo_surface_t* region = nullptr;

    bool complete() const { return plain != nullptr && region != nullptr; }

    void destroy() {
        if (plain) {
            cairo_surface_destroy(plain);
            plain = nullptr;
        }
        if (region) {
            cairo_surface_destroy(region);
            region = nullptr;
        }
    }
};

struct WaveformJob {
    // Fingerprint inputs the worker's render uses. The cache's fp_* fields
    // are updated from these AT THE COMPLETION SWAP, not at dispatch.
    int64_t   vp_start         = 0;
    int64_t   vp_end           = 0;
    // The painter's samples-per-pixel, captured on the GUI thread with the rest
    // of the geometry: the render maps columns on the authoring lattice this
    // defines, and the worker must not re-derive it from live state.
    double    painter_spp      = 0.0;
    int       area_w           = 0;
    int       area_h           = 0;
    bool      target           = false;
    uint64_t  warp_frame_map_hash     = 0;

    // Font-dependent waveform inset (waveform_inset_px()), captured on the GUI
    // thread at dispatch alongside area_w/area_h. The worker must never read
    // g_gui_scale_percent itself — the GUI thread mutates that via
    // set_gui_scale_percent without draining in-flight jobs — so ALL
    // font-derived geometry is snapshotted here for a coherent render.
    int       inset_px         = 0;

    // Frame-map snapshot the worker dereferences during the render. Populated
    // for target view from the memoized target display map (an owned copy taken
    // at job submission, so the worker never races a cache rebuild). Empty in
    // source view. The worker reads — never builds — this.
    std::vector<WarpFrameMapSegment> warp_frame_map;

    // The plate pair to render into. Owned by the cache (the cache's
    // pending-slot pair). The worker only writes pixels; the cache lifecycle
    // owns destroy/recreate. The worker's dispatch gate refuses an incomplete
    // pair whole (see DoneCallback) — it never renders one member alone.
    WaveformPlatePair plates;

    // Audio handle the render reads (see lifetime invariant above). Always
    // main.cpp's single long-lived source audio — the one GuiAudio for the
    // process lifetime. It outlives every in-flight job, so the job needs no
    // shared_ptr keepalive to pin the audio: a raw pointer is sufficient (the
    // former audio-identity / lifetime-pinning axis is retired).
    const GuiAudio* audio = nullptr;
};

class GuiWaveformWorker {
public:
    // ok=true on a clean render of BOTH plates; ok=false on cancellation or on
    // dispatch with an incomplete plate pair (either member null) or null
    // audio — THE ONE ASYNC GATE on the pair (the dispatch paths run no
    // completeness check of their own; a second predicate upstream of this one
    // would be a duplicate). A false result withholds publication and lets the
    // completion handler's dirty-detect redispatch. The completion handler is
    // responsible for the swap-or-redispatch state machine.
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
    // skips the render entirely on either hit. A full render (six band calls,
    // two plates) is short enough that mid-render cancellation isn't
    // necessary — the file-load wait will be at most one render's worth of
    // latency.
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

// Render the waveform into BOTH plates of `plates` from scratch (clears each
// to transparent first, then writes the peak columns of the three band lanes
// — Low, Mid, High, in that fixed z-order — DIRECTLY into each plate buffer,
// the plain inks into the plain plate and the selected inks into the region
// plate; no cairo strokes; see render_waveform's writer contract).
// PRECONDITION: the pair is complete — both members non-null. The two owners
// upstream establish it (the worker's dispatch gate refuses an incomplete pair
// whole; force_synchronous_waveform_rebuild renders neither member unless both
// exist), so this function carries no destination-null arm of its own; only
// its GEOMETRY returns (area, inset) remain. Thread-agnostic: it runs
// either on the waveform worker thread (the async dispatch) or synchronously on
// the GUI thread (force_synchronous_waveform_rebuild), touching only the
// supplied plate pair, the audio handle's peak pyramids (read-only after
// load), the caller's warp_frame_map snapshot, and the job-captured geometry
// scalars (area_w/area_h/inset_px) — no other shared or main-thread state. The
// inset is passed in rather than read via waveform_inset_px() so the render
// touches no gui_scale state: ALL scale-dependent geometry
// is snapshotted on the GUI thread at dispatch, closing the race with a
// mid-render set_gui_scale_percent. Such a job COMPLETES from its coherent
// old-geometry snapshot on either reachable ordering: it may publish before the
// next dirty-detect, or be discarded by a supersede request (a tick's
// maybe_enqueue_waveform_render seeing the changed geometry while the job is
// still in flight fills the supersede slot, and the completion handler discards
// the old pixels and redispatches). When integer waveform geometry changed,
// dirty-detect drives a replacement either way; otherwise none is needed.
void render_waveform_to_cache_surface(
    WaveformPlatePair plates,
    int area_w,
    int area_h,
    int inset_px,
    const GuiAudio& audio,
    int64_t vp_start,
    double  painter_spp,
    const std::vector<WarpFrameMapSegment>* warp_frame_map_or_null);
