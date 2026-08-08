#include "async_renderer.h"

#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <sys/eventfd.h>
#include <unistd.h>

GuiAsyncRenderer::GuiAsyncRenderer() = default;

GuiAsyncRenderer::~GuiAsyncRenderer() {
    shutdown();
}

bool GuiAsyncRenderer::init() {
    completion_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (completion_fd_ < 0) {
        std::fprintf(stderr,
            "warptempo_gui: eventfd() failed for async renderer: %s\n",
            std::strerror(errno));
        return false;
    }
    stop_worker_.store(false);
    worker_ = std::thread(&GuiAsyncRenderer::worker_loop, this);
    return true;
}

void GuiAsyncRenderer::shutdown() {
    if (worker_.joinable()) {
        // If a job is in flight, raise cancel so the worker exits promptly.
        session_cancel_->store(true);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_worker_.store(true);
        }
        cv_.notify_all();
        worker_.join();
    }
    if (completion_fd_ >= 0) {
        ::close(completion_fd_);
        completion_fd_ = -1;
    }
}

void GuiAsyncRenderer::dispatch(RenderRequest req, DoneCallback on_done) {
    // The GUI must serialize submissions: only dispatch when idle. Programming
    // error if asserted otherwise; we log and drop rather than racing.
    if (state_.load() != static_cast<int>(State::Idle)) {
        std::fprintf(stderr,
            "warptempo_gui: Async renderer dispatch while busy "
            "(state=%d) — request dropped\n",
            state_.load());
        return;
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_req_ = std::move(req);
        on_done_     = std::move(on_done);
        // Fresh per-session token; the old one is never reset, so any writer
        // thread still holding a copy keeps a stable, truthful view of its
        // own session's cancellation.
        session_cancel_ = std::make_shared<std::atomic<bool>>(false);
        state_.store(static_cast<int>(State::Running));
    }
    cv_.notify_one();
}

void GuiAsyncRenderer::request_cancel() {
    // When this fires during CompletionPending (render already finished,
    // completion event not yet consumed), setting the token can at most drop
    // that completed session's still-running cache insert — a benign cache
    // miss, in the safe direction; the deliverable itself has already
    // published or been refused by the pipeline's own gates.
    session_cancel_->store(true);
}

bool GuiAsyncRenderer::current_session_cancelled() const {
    // Resting false between sessions: dispatch installs a fresh token, so an
    // idle dispatcher reports the LAST session's verdict until the next
    // dispatch replaces it. The one caller pairs this with is_busy(), where
    // "the current session" is the live one by the single-in-flight contract.
    return session_cancel_->load();
}

bool GuiAsyncRenderer::is_busy() const {
    const int s = state_.load();
    return s == static_cast<int>(State::Running) ||
           s == static_cast<int>(State::CompletionPending);
}

void GuiAsyncRenderer::worker_loop() {
    while (true) {
        RenderRequest req;
        std::shared_ptr<const std::atomic<bool>> cancel_token;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this]() {
                return stop_worker_.load() ||
                       state_.load() == static_cast<int>(State::Running);
            });
            if (stop_worker_.load() &&
                state_.load() != static_cast<int>(State::Running)) {
                return;
            }
            req = std::move(*pending_req_);
            pending_req_.reset();
            // Copy this session's cancel token under the same lock that
            // took the request; see the member comment for the ownership
            // rule (GUI thread writes the member, this is the only
            // cross-thread read).
            cancel_token = session_cancel_;
        }

        // Execute the render synchronously on this worker thread.
        RenderOutcome outcome = do_render(req, std::move(cancel_token));
        last_outcome_ = outcome;

        state_.store(static_cast<int>(State::CompletionPending));
        signal_completion();
    }
}

void GuiAsyncRenderer::signal_completion() {
    if (completion_fd_ < 0) return;
    const uint64_t one = 1;
    ssize_t n = ::write(completion_fd_, &one, sizeof(one));
    if (n != static_cast<ssize_t>(sizeof(one))) {
        std::fprintf(stderr,
            "warptempo_gui: Async renderer eventfd write failed: %s\n",
            std::strerror(errno));
    }
}

void GuiAsyncRenderer::on_completion_event() {
    if (state_.load() != static_cast<int>(State::CompletionPending)) {
        // Spurious wakeup or platform race — nothing to do.
        return;
    }

    const RenderOutcome outcome = last_outcome_;
    DoneCallback cb;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cb = std::move(on_done_);
        on_done_ = nullptr;
        state_.store(static_cast<int>(State::Idle));
    }
    if (cb) cb(outcome);
}
