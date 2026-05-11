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
        cancel_flag_.store(true);
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
            "warptempo_gui: async renderer dispatch while busy "
            "(state=%d) — request dropped\n",
            state_.load());
        return;
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_req_ = std::move(req);
        on_done_     = std::move(on_done);
        cancel_flag_.store(false);
        state_.store(static_cast<int>(State::Running));
    }
    cv_.notify_one();
}

void GuiAsyncRenderer::request_cancel() {
    cancel_flag_.store(true);
}

bool GuiAsyncRenderer::is_busy() const {
    const int s = state_.load();
    return s == static_cast<int>(State::Running) ||
           s == static_cast<int>(State::CompletionPending);
}

void GuiAsyncRenderer::worker_loop() {
    while (true) {
        RenderRequest req;
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
        }

        // Execute the render synchronously on this worker thread.
        RenderOutcome outcome = do_render(req, &cancel_flag_);
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
            "warptempo_gui: async renderer eventfd write failed: %s\n",
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
