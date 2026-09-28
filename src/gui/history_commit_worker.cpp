#include "history_commit_worker.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/eventfd.h>
#include <unistd.h>
#include <utility>

namespace {

// THE REPOSITORY LANE — PROCESS-OWNED, OUTLIVING EVERY WORKER (codex round 1
// over the git arc, 2026-09-28). Every job this worker runs holds it for the
// job's WHOLE run, from before the clone is opened until the job's handle is
// closed: the check (a fetch, which writes the remote-tracking refs) and the
// checkpoint (a fetch, the commit, the push). ONE WORKER'S ONE SLOT already
// serializes a session's own jobs; the lane is what serializes them ACROSS
// SESSIONS. A project switch abandons a running check (the class head) and the
// next session's worker starts at once on the same shared clone — and
// libgit2 reports a ref update only AFTER writing it, so the cancel token
// cannot stop the abandoned fetch's LAST write: without the lane, that stale
// write could land after the new session's own fetch had written a newer tip,
// moving `refs/remotes/origin/<branch>` backwards under a status that says
// otherwise, and a pull would then fast-forward only to the stale tip. With
// it, the new session's first job waits ON ITS OWN WORKER THREAD until the
// abandoned one has fully unwound — the GUI thread never takes the lane, so
// quit and a project switch still never wait on a check (the abandon is
// unchanged; a checkpoint in flight is joined as it always was, and a
// checkpoint that is still waiting for the lane is part of that join).
//
// NEVER DESTROYED: a detached check may still hold it while the process runs
// its static destructors at exit, so it is a leaked heap object rather than a
// static with a destructor.
//
// THE PULL DOES NOT TAKE IT, and needs not: it runs on the GUI thread (which
// must never wait), network-free, and it is admitted only on a Behind status
// with the worker idle (open_history_commit_editor's status fork) — a status
// that a job of THIS session wrote after taking the lane, so every earlier
// session's job had already released it, and nothing of this session's is in
// flight. The prefetch worker's reads write nothing and take no lane either.
std::mutex& repository_lane() {
    static std::mutex* lane = new std::mutex;
    return *lane;
}

}  // namespace

GuiHistoryCommitWorker::GuiHistoryCommitWorker() = default;

GuiHistoryCommitWorker::~GuiHistoryCommitWorker() {
    shutdown();
}

bool GuiHistoryCommitWorker::init() {
    const int fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (fd < 0) {
        std::fprintf(stderr,
            "warptempo_gui: eventfd() failed for the checkpoint worker: %s\n",
            std::strerror(errno));
        return false;
    }
    shared_ = std::make_shared<Shared>();
    shared_->completion_fd = fd;
    worker_ = std::thread(&GuiHistoryCommitWorker::worker_loop, shared_);
    return true;
}

int GuiHistoryCommitWorker::completion_fd() const {
    if (!shared_) return -1;
    std::lock_guard<std::mutex> lk(shared_->mtx);
    return shared_->completion_fd;
}

void GuiHistoryCommitWorker::shutdown() {
    if (!shared_) return;
    if (worker_.joinable()) {
        // THE TWO KINDS PART HERE (the class head owns the ruling): a CHECK
        // on the thread is abandoned — cancelled and detached, never waited
        // on — and anything else is joined: a checkpoint runs to its end, and
        // an idle loop or a finished job returns at once.
        bool abandon = false;
        {
            std::lock_guard<std::mutex> lk(shared_->mtx);
            shared_->stop = true;
            if (shared_->state.load() == static_cast<int>(State::Running) &&
                shared_->running_kind == GuiHistoryJobKind::Check) {
                abandon            = true;
                shared_->abandoned = true;
                shared_->cancel.store(true);
            }
        }
        shared_->cv.notify_all();
        if (abandon) {
            std::fprintf(stderr,
                "warptempo_gui: GitHub check abandoned at the session's end\n");
            worker_.detach();
        } else {
            worker_.join();
        }
    }
    // THE FD CLOSES UNDER THE MUTEX, with the block marked abandoned, so a
    // detached thread finishing later can never write into a closed — or
    // reused — descriptor.
    {
        std::lock_guard<std::mutex> lk(shared_->mtx);
        shared_->abandoned = true;
        if (shared_->completion_fd >= 0) {
            ::close(shared_->completion_fd);
            shared_->completion_fd = -1;
        }
    }
    shared_.reset();
    on_done_ = nullptr;
}

void GuiHistoryCommitWorker::dispatch(GuiHistoryCommitJob job,
                                      DoneCallback         on_done) {
    // The callers serialize: nothing dispatches while a job is in flight (the
    // key's admission and the button's face read the in-flight bit and the
    // GitHub status). Arriving here busy is a programming error — say so and
    // drop, never race.
    if (!shared_ ||
        shared_->state.load() != static_cast<int>(State::Idle)) {
        std::fprintf(stderr,
            "warptempo_gui: Checkpoint worker dispatch while busy "
            "(state=%d): the request was dropped\n",
            shared_ ? shared_->state.load() : -1);
        return;
    }

    on_done_ = std::move(on_done);
    {
        std::lock_guard<std::mutex> lk(shared_->mtx);
        shared_->running_kind = job.kind;
        shared_->pending_job  = std::move(job);
        shared_->state.store(static_cast<int>(State::Running));
    }
    shared_->cv.notify_one();
}

bool GuiHistoryCommitWorker::is_busy() const {
    if (!shared_) return false;
    const int s = shared_->state.load();
    return s == static_cast<int>(State::Running) ||
           s == static_cast<int>(State::CompletionPending);
}

void GuiHistoryCommitWorker::worker_loop(std::shared_ptr<Shared> shared) {
    while (true) {
        GuiHistoryCommitJob job;
        {
            std::unique_lock<std::mutex> lk(shared->mtx);
            shared->cv.wait(lk, [&shared]() {
                return shared->stop ||
                       shared->state.load() ==
                           static_cast<int>(State::Running);
            });
            if (shared->state.load() != static_cast<int>(State::Running)) {
                return;  // stopped with nothing to run
            }
            job = std::move(*shared->pending_job);
            shared->pending_job.reset();
        }

        GuiHistoryJobResult result;
        result.kind = job.kind;
        {
            // THE LANE, held for the whole job (repository_lane above owns
            // why). The wait is here, on this thread, never on the GUI's.
            std::lock_guard<std::mutex> lane(repository_lane());
            if (job.kind == GuiHistoryJobKind::Check) {
                // THE CHECK (history_diff.h): fetch and compare. Its cancel
                // token is the abandon's (shutdown above) — and a check
                // abandoned while it waited for the lane has nothing left to
                // ask: it answers Unchecked without opening the clone.
                if (!shared->cancel.load()) {
                    result.github = check_github(job.source_audio_path,
                                                 job.projects_repo,
                                                 shared->cancel);
                }
            } else {
                // THE ACT ITSELF, unchanged and whole (history_diff.h): the
                // fetch first, the three writes, the three-path commit, the
                // push, and every stderr line about them.
                result.outcome = commit_history_checkpoint(
                    job.repo_root, job.project_directory, job.base_name,
                    job.projects_repo, job.bytes, job.title, result.github);
            }
        }

        {
            std::lock_guard<std::mutex> lk(shared->mtx);
            shared->result = result;
            shared->state.store(static_cast<int>(State::CompletionPending));
            // AN ABANDONED BLOCK IS NEVER SIGNALLED: its session is gone, and
            // its descriptor with it (shutdown closes it under this mutex).
            if (!shared->abandoned && shared->completion_fd >= 0) {
                const uint64_t one = 1;
                const ssize_t  n =
                    ::write(shared->completion_fd, &one, sizeof(one));
                if (n != static_cast<ssize_t>(sizeof(one))) {
                    std::fprintf(stderr,
                        "warptempo_gui: Checkpoint worker eventfd write "
                        "failed: %s\n",
                        std::strerror(errno));
                }
            }
            if (shared->abandoned) return;
        }
    }
}

void GuiHistoryCommitWorker::on_completion_event() {
    if (!shared_ ||
        shared_->state.load() != static_cast<int>(State::CompletionPending)) {
        // Spurious wakeup or platform race — nothing to do.
        return;
    }
    GuiHistoryJobResult result;
    {
        std::lock_guard<std::mutex> lk(shared_->mtx);
        result = shared_->result;
        shared_->state.store(static_cast<int>(State::Idle));
    }
    DoneCallback cb = std::move(on_done_);
    on_done_        = nullptr;
    if (cb) cb(result);
}
