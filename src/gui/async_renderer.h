#pragma once

#include "render_pipeline.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
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
    // the worker has exited — if a job is in flight, sets the session cancel
    // token first so the worker exits at the next frame boundary.
    void shutdown();

    // The eventfd the platform layer polls for completion. -1 before init().
    int completion_fd() const { return completion_fd_; }

    // Dispatch a render job. Asserts the worker is idle (no job in flight).
    // Creates a fresh session cancel token (the previous session's token is
    // never reset, so any writer thread still holding a copy keeps a truthful
    // view of its own session's cancellation), stores req and on_done in the
    // job slot, transitions worker_state_ Idle -> Running, notifies the cv.
    // Returns immediately.
    void dispatch(RenderRequest req, DoneCallback on_done);

    // Request cancellation of the in-flight job. Sets the current session's
    // cancel token; the worker passes its copy through do_render, where the
    // engine observes it during synthesis. No-op if no job is in flight.
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

    // True iff the CURRENT session's cancel token is set — "the job the worker
    // is running has been asked to die". The session concept lives here (each
    // dispatch mints a fresh token), so this is the one place that can answer
    // it; every kill route in the product reaches the token through
    // request_cancel above, so no caller has to enumerate them. Its reader is
    // the deferred archival status message's promotion check
    // (tick_promote_render_status), which must not paint a message for a
    // session whose product will be discarded. GUI-thread read of a GUI-thread-
    // written member, the same access shape request_cancel takes; the pointee
    // atomic covers the worker's own reads.
    bool current_session_cancelled() const;

private:
    enum class State : int { Idle, Running, CompletionPending };

    void worker_loop();
    void signal_completion();

    std::thread          worker_;
    std::mutex           mtx_;
    std::condition_variable cv_;

    std::atomic<int>     state_{static_cast<int>(State::Idle)};
    // Per-dispatch cancellation token. dispatch() replaces it with a fresh
    // token for each session and old tokens are NEVER reset, so a copy held
    // by an asynchronous consumer (do_render, and through it the render
    // cache's writer thread) names exactly its own dispatching session: it
    // can neither read false after a later dispatch's reset nor mistake a
    // later session's cancel for its own. Ownership: the shared_ptr member
    // itself is written only on the GUI thread (dispatch, under mtx_); the
    // worker's copy, taken under the same mutex in worker_loop, is the only
    // cross-thread read. The pointee atomic is written by request_cancel /
    // shutdown and read anywhere.
    std::shared_ptr<std::atomic<bool>> session_cancel_ =
        std::make_shared<std::atomic<bool>>(false);
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
