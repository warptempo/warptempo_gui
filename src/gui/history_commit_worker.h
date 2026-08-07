#pragma once

#include "history_diff.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

// THE CHECKPOINT ACT'S BACKGROUND WORKER (architect 2026-08-07).
//
// The Save-and-Commit act runs `git add`, `git commit` and `git push` as child
// processes, and the push in particular is a network act that can take seconds
// — long enough that running it on the GUI thread froze the window over work
// the user has no reason to wait for. THE SAVE IS THE PART THAT MUST BE
// SYNCHRONOUS (it is the user's own bytes, and its failure refuses the act);
// everything after it is repository housekeeping, so it happens here while the
// user keeps working. GuiInputHandler::run_history_commit owns the split and
// states what is captured.
//
// SINGLE JOB IN FLIGHT, structurally: the caller refuses a second act while one
// is running (AppState::history_checkpoint_in_flight, the GUI-side mirror of
// is_busy() below), and a dispatch that arrives busy anyway is a programming
// error this class logs and drops rather than racing.
//
// SHAPED EXACTLY LIKE GuiAsyncRenderer, deliberately: own std::thread, a
// condition variable for the wake, and an eventfd the platform run loop polls,
// whose POLLIN makes the GUI thread call on_completion_event() and run the
// stored callback on the MAIN thread. The one difference is that there is no
// cancel token — the act's steps are git children that must not be abandoned
// half-way, so shutdown() JOINS an in-flight checkpoint instead of interrupting
// it (the state is already saved to disk by then, so waiting loses nothing).
//
// THE JOB IS CAPTURED WHOLE, BY VALUE. The worker touches no AppState, no
// audio, no marker store — only the strings below — so the user may edit,
// render and even load in place while a checkpoint publishes, and what lands is
// what was on screen when the act ran.
struct GuiHistoryCommitJob {
    std::string       project_directory;  // e.g. "projects/550 - 1"
    std::string       base_name;          // the sidecar base name
    std::string       projects_repo;      // the setting's value, verbatim
    std::string       title;              // the commit message (the editor's)
    GuiHistoryNowSide bytes;              // the three sidecar texts to write
};

class GuiHistoryCommitWorker {
public:
    using DoneCallback = std::function<void(GuiHistoryCommitOutcome)>;

    GuiHistoryCommitWorker();
    ~GuiHistoryCommitWorker();

    GuiHistoryCommitWorker(const GuiHistoryCommitWorker&)            = delete;
    GuiHistoryCommitWorker& operator=(const GuiHistoryCommitWorker&) = delete;

    // Create the eventfd and spawn the worker thread. False if the eventfd
    // could not be created (one stderr line of its own). Must run before any
    // dispatch.
    bool init();

    // Stop the worker and close the eventfd. Idempotent, safe after a failed
    // init, and called from the destructor. IT BLOCKS UNTIL AN IN-FLIGHT
    // CHECKPOINT FINISHES — a quit must not abandon a `git commit` mid-child,
    // and the user's own state is already on disk (the act saves first), so the
    // wait costs a moment and never any work.
    void shutdown();

    // The eventfd the platform layer polls for completion. -1 before init().
    int completion_fd() const { return completion_fd_; }

    // Run `job` on the worker. `on_done` fires on the MAIN thread from
    // on_completion_event with the act's own verdict.
    void dispatch(GuiHistoryCommitJob job, DoneCallback on_done);

    // Called by the platform layer when the completion eventfd fires (the
    // platform read()s the counter first, as it does for the render worker).
    void on_completion_event();

    // True from dispatch until the completion event has been consumed.
    bool is_busy() const;

private:
    enum class State : int { Idle, Running, CompletionPending };

    void worker_loop();
    void signal_completion();

    std::thread             worker_;
    std::mutex              mtx_;
    std::condition_variable cv_;

    std::atomic<int>  state_{static_cast<int>(State::Idle)};
    std::atomic<bool> stop_worker_{false};

    // Job slot. Written by the GUI thread at dispatch (Idle -> Running), read
    // by the worker after the cv wake; on_done_ is read back by the GUI thread
    // at completion (CompletionPending -> Idle).
    std::optional<GuiHistoryCommitJob> pending_job_;
    DoneCallback                       on_done_;

    // Written by the worker just before signal_completion, read by the GUI
    // thread in on_completion_event.
    GuiHistoryCommitOutcome last_outcome_ =
        GuiHistoryCommitOutcome::CommitFailed;

    int completion_fd_ = -1;
};
