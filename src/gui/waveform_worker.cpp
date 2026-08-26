#include "waveform_worker.h"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

GuiWaveformWorker::GuiWaveformWorker() = default;

GuiWaveformWorker::~GuiWaveformWorker() {
    shutdown();
}

bool GuiWaveformWorker::init() {
    completion_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (completion_fd_ < 0) {
        std::fprintf(stderr,
            "warptempo_gui: eventfd() failed for waveform worker: %s\n",
            std::strerror(errno));
        return false;
    }
    stop_worker_.store(false);
    worker_ = std::thread(&GuiWaveformWorker::worker_loop, this);
    return true;
}

void GuiWaveformWorker::shutdown() {
    if (worker_.joinable()) {
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

void GuiWaveformWorker::dispatch(WaveformJob job, DoneCallback on_done) {
    if (state_.load() != static_cast<int>(State::Idle)) {
        std::fprintf(stderr,
            "warptempo_gui: Waveform worker dispatch while busy "
            "(state=%d) — request dropped\n",
            state_.load());
        return;
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_job_ = std::move(job);
        on_done_     = std::move(on_done);
        cancel_flag_.store(false);
        state_.store(static_cast<int>(State::Running));
    }
    cv_.notify_one();
}

void GuiWaveformWorker::request_cancel() {
    cancel_flag_.store(true);
}

bool GuiWaveformWorker::is_busy() const {
    const int s = state_.load();
    return s == static_cast<int>(State::Running) ||
           s == static_cast<int>(State::CompletionPending);
}

void GuiWaveformWorker::wait_until_idle() {
    using clock = std::chrono::steady_clock;
    // The expiry path is unsafe by design: the caller (the paint path's
    // force_synchronous_waveform_rebuild) takes over the cache surfaces from
    // under any live job. This bound exists only as a last-resort hang guard,
    // and is sized far above the two-render worst case so it does not fire in
    // practice.
    const auto deadline = clock::now() + std::chrono::milliseconds(1000);
    while (true) {
        const int s = state_.load();
        if (s == static_cast<int>(State::Idle)) return;
        if (s == static_cast<int>(State::CompletionPending)) {
            // Drain the completion synchronously. The eventfd counter is
            // left set; the platform layer's poll handler will read it
            // and call on_completion_event() again, which is a no-op once
            // state_ is back to Idle.
            on_completion_event();
            continue;
        }
        // Running: ask the worker to skip its render and try again.
        cancel_flag_.store(true);
        if (clock::now() >= deadline) {
            std::fprintf(stderr,
                "warptempo_gui: Waveform worker did not become idle within "
                "1000ms; proceeding anyway\n");
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void GuiWaveformWorker::worker_loop() {
    while (true) {
        WaveformJob job;
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

        // Run the render unless cancelled before we start. Cancellation
        // is also re-checked after the render so a cancel issued during
        // the render still gets reported (last_ok_=false) to the
        // completion handler.
        bool ok = false;
        if (!cancel_flag_.load() && job.surface && job.audio) {
            render_waveform_to_cache_surface(
                job.surface,
                job.area_w,
                job.area_h,
                job.inset_px,
                *job.audio,
                job.vp_start,
                job.painter_spp,
                job.magnification_level,
                job.warp_frame_map.empty() ? nullptr : &job.warp_frame_map);
            ok = !cancel_flag_.load();
        }
        last_ok_ = ok;

        state_.store(static_cast<int>(State::CompletionPending));
        signal_completion();
    }
}

void GuiWaveformWorker::signal_completion() {
    if (completion_fd_ < 0) return;
    const uint64_t one = 1;
    ssize_t n = ::write(completion_fd_, &one, sizeof(one));
    if (n != static_cast<ssize_t>(sizeof(one))) {
        std::fprintf(stderr,
            "warptempo_gui: Waveform worker eventfd write failed: %s\n",
            std::strerror(errno));
    }
}

void GuiWaveformWorker::on_completion_event() {
    if (state_.load() != static_cast<int>(State::CompletionPending)) {
        return;
    }

    const bool ok = last_ok_;
    DoneCallback cb;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cb = std::move(on_done_);
        on_done_ = nullptr;
        state_.store(static_cast<int>(State::Idle));
    }
    if (cb) cb(ok);
}
