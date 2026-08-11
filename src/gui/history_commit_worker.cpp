#include "history_commit_worker.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/eventfd.h>
#include <unistd.h>
#include <utility>

GuiHistoryCommitWorker::GuiHistoryCommitWorker() = default;

GuiHistoryCommitWorker::~GuiHistoryCommitWorker() {
    shutdown();
}

bool GuiHistoryCommitWorker::init() {
    completion_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (completion_fd_ < 0) {
        std::fprintf(stderr,
            "warptempo_gui: eventfd() failed for the checkpoint worker: %s\n",
            std::strerror(errno));
        return false;
    }
    stop_worker_.store(false);
    worker_ = std::thread(&GuiHistoryCommitWorker::worker_loop, this);
    return true;
}

void GuiHistoryCommitWorker::shutdown() {
    if (worker_.joinable()) {
        // NO CANCEL, BY DESIGN — the loop below finishes the checkpoint it is
        // running and only then sees the stop flag, so the join waits it out.
        // The act's git steps are children that must not be abandoned part-way,
        // and the user's own state was written to disk before the act was
        // dispatched at all.
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

void GuiHistoryCommitWorker::dispatch(GuiHistoryCommitJob job,
                                      DoneCallback         on_done) {
    // The caller serializes: a second act is refused while one is in flight
    // (the key's admission and the button's face read the same bit). Arriving
    // here busy is a programming error — say so and drop, never race.
    if (state_.load() != static_cast<int>(State::Idle)) {
        std::fprintf(stderr,
            "warptempo_gui: Checkpoint worker dispatch while busy "
            "(state=%d) — the request was dropped\n",
            state_.load());
        return;
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_job_ = std::move(job);
        on_done_     = std::move(on_done);
        state_.store(static_cast<int>(State::Running));
    }
    cv_.notify_one();
}

bool GuiHistoryCommitWorker::is_busy() const {
    const int s = state_.load();
    return s == static_cast<int>(State::Running) ||
           s == static_cast<int>(State::CompletionPending);
}

void GuiHistoryCommitWorker::worker_loop() {
    while (true) {
        GuiHistoryCommitJob job;
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
            job = std::move(*pending_job_);
            pending_job_.reset();
        }

        // THE ACT ITSELF, unchanged and whole (history_diff.h): the three
        // writes, the pathspec-scoped commit, the push, and every stderr line
        // about them — which now print from this thread, which is fine, they
        // are the same lines in the same order.
        last_outcome_ = commit_history_checkpoint(
            job.repo_root, job.project_directory, job.base_name,
            job.projects_repo, job.bytes, job.title);

        state_.store(static_cast<int>(State::CompletionPending));
        signal_completion();
    }
}

void GuiHistoryCommitWorker::signal_completion() {
    if (completion_fd_ < 0) return;
    const uint64_t one = 1;
    ssize_t n = ::write(completion_fd_, &one, sizeof(one));
    if (n != static_cast<ssize_t>(sizeof(one))) {
        std::fprintf(stderr,
            "warptempo_gui: Checkpoint worker eventfd write failed: %s\n",
            std::strerror(errno));
    }
}

void GuiHistoryCommitWorker::on_completion_event() {
    if (state_.load() != static_cast<int>(State::CompletionPending)) {
        // Spurious wakeup or platform race — nothing to do.
        return;
    }

    const GuiHistoryCommitOutcome outcome = last_outcome_;
    DoneCallback cb;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cb = std::move(on_done_);
        on_done_ = nullptr;
        state_.store(static_cast<int>(State::Idle));
    }
    if (cb) cb(outcome);
}
