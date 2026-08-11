#include "history_prefetch.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/eventfd.h>
#include <unistd.h>
#include <utility>

GuiHistoryPrefetch::GuiHistoryPrefetch() = default;

GuiHistoryPrefetch::~GuiHistoryPrefetch() {
    shutdown();
}

bool GuiHistoryPrefetch::init() {
    completion_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (completion_fd_ < 0) {
        std::fprintf(stderr,
            "warptempo_gui: eventfd() failed for the history prefetch worker: "
            "%s\n",
            std::strerror(errno));
        return false;
    }
    stop_worker_.store(false);
    worker_ = std::thread(&GuiHistoryPrefetch::worker_loop, this);
    return true;
}

void GuiHistoryPrefetch::shutdown() {
    if (worker_.joinable()) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_worker_.store(true);
            // A run in flight reads this the same way it reads a supersede —
            // through the abandon check at its candidate boundary — so the join
            // below waits out at most one commit's strict load. Nothing is left
            // half-done: a scan writes only its own temp scratch, which its own
            // RAII guard removes.
            pending_run_.reset();
        }
        cv_.notify_all();
        worker_.join();
    }
    if (completion_fd_ >= 0) {
        ::close(completion_fd_);
        completion_fd_ = -1;
    }
}

void GuiHistoryPrefetch::kick(std::string source_audio_path,
                              std::string projects_repo) {
    // THE STORE IS CLEARED HERE, ON THIS THREAD, before the worker can push a
    // single message of the new run — which is what makes "the deque a reader
    // holds is either the current generation's or nothing" true without a lock.
    ++generation_seen_;
    header_seen_ = false;
    header_      = GuiHistoryWalkHeader{};
    members_.clear();
    done_    = false;
    running_ = true;
    failed_  = false;
    failure_reason_.clear();
    hidden_  = 0;
    tip_sha_.clear();
    subject_path_ = source_audio_path;
    subject_repo_ = projects_repo;

    {
        std::lock_guard<std::mutex> lk(mtx_);
        // The bump is what abandons whatever is running: the run compares this
        // against the generation it started with, between candidates.
        generation_.store(generation_seen_);
        // Anything the superseded run already queued is now stale by tag, and
        // dropping it here rather than at the drain saves the drain the work of
        // walking messages it will discard.
        queue_.clear();
        pending_run_ = Run{generation_seen_, std::move(source_audio_path),
                           std::move(projects_repo)};
    }
    cv_.notify_one();
}

GuiHistoryPrefetch::DrainResult GuiHistoryPrefetch::drain() {
    DrainResult result;

    std::deque<Message> batch;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        batch.swap(queue_);
    }

    for (Message& m : batch) {
        // A SUPERSEDED RUN'S MESSAGES DIE HERE, provably: the generation is
        // stamped at the producing site inside the run and the store's own
        // counter only ever moves at kick(), on this thread, so a message that
        // does not carry the current number describes a walk nothing is bound
        // to. (kick() also empties the queue, so this is the second of two
        // fences rather than the only one — the one that catches a message the
        // worker pushed after that clear and before it noticed the bump.)
        if (m.generation != generation_seen_) continue;

        switch (m.kind) {
        case Message::Kind::Header:
            header_      = std::move(m.header);
            header_seen_ = true;
            tip_sha_     = std::move(m.tip_sha);
            break;
        case Message::Kind::Member:
            members_.push_back(std::move(m.member));
            ++result.members_appended;
            break;
        case Message::Kind::Done:
            done_              = true;
            running_           = false;
            failed_            = !m.result.ok;
            failure_reason_    = std::move(m.result.unavailable_reason);
            hidden_            = m.result.hidden;
            result.became_done = true;
            // THE COUNTED LINE, once per run and at its end — the same sentence
            // the eager init printed, moved to the moment the count is final.
            // IT PRINTS ON AN EMPTY WALK TOO (2026-08-09, with the terminal-zero
            // refusal's deletion): a run that hid EVERY candidate is exactly
            // when the number explains something, the view opening at `0/0` over
            // a blank lane with no other account of why. It is the ONE
            // explanation the feature offers — the entry's two refusal messages
            // died with the refusal rather than moving here beside it.
            //
            // A FAILED RUN PRINTS NOTHING HERE, and needs no term of its
            // own: every arm that ends a run NOT ok returns before a single
            // candidate has been gated (GuiHistoryScanResult, history_diff.h,
            // enumerates them and is the one site that does), so the hidden
            // count is structurally zero. Its own line is the mode's refusal at
            // GuiHistoryDiff::init, the header refusal's shape exactly — the
            // store records, the entry prints.
            if (hidden_ > 0) {
                std::fprintf(stderr,
                             "warptempo_gui: History hid %d commit(s) whose "
                             "sidecars refuse the strict load\n",
                             hidden_);
            }
            break;
        }
    }
    return result;
}

void GuiHistoryPrefetch::push_message(Message m) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        queue_.push_back(std::move(m));
    }
    signal_ready();
}

void GuiHistoryPrefetch::signal_ready() {
    if (completion_fd_ < 0) return;
    const uint64_t one = 1;
    const ssize_t  n   = ::write(completion_fd_, &one, sizeof(one));
    if (n != static_cast<ssize_t>(sizeof(one))) {
        std::fprintf(stderr,
            "warptempo_gui: History prefetch eventfd write failed: %s\n",
            std::strerror(errno));
    }
}

void GuiHistoryPrefetch::worker_loop() {
    while (true) {
        Run run;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this]() {
                return stop_worker_.load() || pending_run_.has_value();
            });
            if (stop_worker_.load()) return;
            run = std::move(*pending_run_);
            pending_run_.reset();
        }

        const unsigned long long my_gen = run.generation;
        // ABANDON ON EITHER REASON, one predicate: a newer kick has bumped the
        // counter past ours, or the process is going down.
        auto abandoned = [this, my_gen]() {
            return stop_worker_.load() || generation_.load() != my_gen;
        };

        // THE TIP THE RUN IS BUILT AGAINST, read once here — the store's
        // staleness key, carried out with the header so a reader never sees a
        // tip without the walk it describes.
        std::string tip = read_history_branch_tip_sha(run.source_audio_path);

        scan_history_walk(
            run.source_audio_path, run.projects_repo, abandoned,
            [this, my_gen, &tip](GuiHistoryWalkHeader h) {
                Message m;
                m.kind       = Message::Kind::Header;
                m.generation = my_gen;
                m.header     = std::move(h);
                m.tip_sha    = tip;
                push_message(std::move(m));
            },
            [this, my_gen](GuiHistoryCommitSidecars s) {
                Message m;
                m.kind       = Message::Kind::Member;
                m.generation = my_gen;
                m.member     = std::move(s);
                push_message(std::move(m));
            },
            [this, my_gen](GuiHistoryScanResult r) {
                Message m;
                m.kind       = Message::Kind::Done;
                m.generation = my_gen;
                m.result     = std::move(r);
                push_message(std::move(m));
            });
    }
}
