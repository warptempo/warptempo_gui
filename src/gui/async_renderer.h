#pragma once

#include "render_pipeline.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

// RenderOutcome is defined in render_pipeline.h.
//
// GuiAsyncRenderer: single-worker, single-in-flight-job dispatcher. Owns a
// std::thread that loops waiting on a condition variable for incoming jobs.
// Completion is signaled to the GUI thread via an eventfd that the platform
// run loop polls; on POLLIN the GUI thread calls on_completion_event(), which
// reads out the result and invokes the user's on_done callback.
//
// The GUI's existing queue-walker serializes submissions, so dispatch() while
// the worker is busy is a programming error (asserted via worker_state_).
class GuiAsyncRenderer {
public:
    using DoneCallback = std::function<void(RenderOutcome)>;

    GuiAsyncRenderer();
    ~GuiAsyncRenderer();

    GuiAsyncRenderer(const GuiAsyncRenderer&)            = delete;
    GuiAsyncRenderer& operator=(const GuiAsyncRenderer&) = delete;

    // Initialize the eventfd and spawn the worker thread. Returns false if
    // eventfd creation fails. Must be called before any dispatch.
    bool init();

    // Stop the worker thread and close the eventfd. Idempotent. Safe to call
    // even if init() failed; safe to call from the destructor. Blocks until
    // the worker has exited — if a job is in flight, sets cancel_flag first
    // so the worker exits at the next frame boundary.
    void shutdown();

    // The eventfd the platform layer polls for completion. -1 before init().
    int completion_fd() const { return completion_fd_; }

    // Dispatch a render job. Asserts the worker is idle (no job in flight).
    // Resets cancel_flag, stores req and on_done in the job slot, transitions
    // worker_state_ Idle -> Running, notifies the cv. Returns immediately.
    void dispatch(RenderRequest req, DoneCallback on_done);

    // Request cancellation of the in-flight job. Sets cancel_flag; the
    // worker passes it through do_render, where the engine observes it during
    // synthesis. No-op if no job is in flight.
    void request_cancel();

    // Called by the platform layer when the completion eventfd fires.
    // Reads the result, transitions worker_state_ CompletionPending -> Idle,
    // invokes the stored on_done callback.
    //
    // The platform layer is responsible for read()ing the eventfd's 8-byte
    // counter to clear it before calling this method.
    void on_completion_event();

    // True while a job is in flight (worker is running OR a completion is
    // pending but on_completion_event hasn't fired yet). Read by the Esc
    // handler to decide whether to deliver a cancel.
    bool is_busy() const;

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
    std::optional<RenderRequest> pending_req_;
    DoneCallback                 on_done_;

    // Completion result. Written by the worker just before signal_completion;
    // read by the GUI thread in on_completion_event.
    RenderOutcome last_outcome_ = RenderOutcome::Failed;

    int completion_fd_ = -1;
};
