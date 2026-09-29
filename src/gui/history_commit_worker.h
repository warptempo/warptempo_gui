#pragma once

#include "history_diff.h"

#include <atomic>
#include <memory>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

// THE REPOSITORY'S BACKGROUND WORKER — the checkpoint act and the GitHub
// check (architect 2026-08-07; the check 2026-09-27).
//
// The Save-and-Commit act fetches, stages, commits and pushes through libgit2
// (git_repo.h), and the network steps in particular can take seconds — long
// enough that running them on the GUI thread froze the window over work the
// user has no reason to wait for. THE SAVE IS THE PART THAT MUST BE
// SYNCHRONOUS (it is the user's own bytes, and its failure refuses the act);
// everything after it is repository housekeeping, so it happens here while the
// user keeps working. GuiInputHandler::run_history_commit owns the split and
// states what is captured.
//
// ONE WORKER, TWO JOB KINDS. The CHECK (check_github, history_diff.h) fetches
// and compares at every project open, every `h` entry and Ctrl+S in the
// view under `offline`; the CHECKPOINT runs
// the act. Both can write the remote-tracking ref (a fetch, a push), so they
// share this ONE thread and its ONE job slot: two network jobs of one session
// never race for that ref, by construction — and across sessions the
// repository lane (below) keeps an abandoned check and the next session's
// jobs apart. The CHECKPOINT kind alone locks the save out
// (AppState::history_checkpoint_in_flight); a check locks out nothing.
//
// SINGLE JOB IN FLIGHT, structurally: the callers dispatch only while
// is_busy() is false (a checkpoint's admission reads the in-flight bit and the
// GitHub status, which reads Checking while a check runs), and a dispatch
// that arrives busy anyway is a programming error this class logs and drops
// rather than racing.
//
// SHAPED LIKE GuiAsyncRenderer: own std::thread, a condition variable for the
// wake, and an eventfd the platform run loop polls, whose POLLIN makes the GUI
// thread call on_completion_event() and run the stored callback on the MAIN
// thread.
//
// SHUTDOWN TREATS THE TWO KINDS DIFFERENTLY (architect 2026-09-27: quit and a
// project switch NEVER WAIT on a check). A CHECKPOINT in flight is JOINED — its
// git steps must not be abandoned half-way, the user's state is already on
// disk, and the push is bounded by git_repo.cpp's time limit. A CHECK in
// flight is ABANDONED: its cancel token is set (the fetch stops at libgit2's
// next callback, so it starts no ref update after that — one already in
// progress may still land, below) and the thread is DETACHED,
// so a connect hanging on a black-holed route holds nothing. That is safe
// because everything the thread touches lives in a shared block it co-owns
// (Shared below) — never this object — and because an abandoned thread never
// writes the eventfd: the write and the close both happen under the block's
// mutex, the write only while the block is not abandoned. Its answer is
// discarded; the next session checks afresh. What an abandoned check can
// still leave behind is libgit2's own: a fetch cancelled mid-download removes
// its temporary pack, and a ref update in progress at the very instant of the
// cancel lands (libgit2 reports a ref update only after writing it). THAT LAST
// WRITE IS WHY EVERY JOB HOLDS THE PROCESS-OWNED REPOSITORY LANE
// (repository_lane, history_commit_worker.cpp) for its whole run: the lane
// outlives this object and its session, so the next session's first job waits
// on its own worker thread until the abandoned one has fully unwound, and an
// old fetch's tip can never land after a newer one. The GUI thread never takes
// it: quit and a project switch still never wait on a check.
//
// THE JOB IS CAPTURED WHOLE, BY VALUE. The worker touches no AppState, no
// audio, no marker store — only the strings below — so the user may edit,
// render and even load in place while a checkpoint publishes, and what lands is
// what was on screen when the act ran.
enum class GuiHistoryJobKind { Checkpoint, Check };

struct GuiHistoryCommitJob {
    GuiHistoryJobKind kind = GuiHistoryJobKind::Checkpoint;
    // THE CHECK'S TWO INPUTS: the loaded source (the clone is derived from it
    // on the worker, check_github) and the setting the guard compares.
    std::string       source_audio_path;
    // The clone the act runs in, derived from the loaded source and captured on
    // the main thread with everything else (history_diff.h owns the derivation).
    std::string       repo_root;
    std::string       project_directory;  // e.g. "projects/550 - 1"
    std::string       base_name;          // the sidecar base name
    std::string       projects_repo;      // the setting's value, verbatim
    std::string       title;              // the commit message (the editor's)
    GuiHistoryNowSide bytes;              // the three sidecar texts to write
};

// WHAT A JOB ANSWERED: the act's verdict (a Checkpoint's alone) and the GitHub
// status its fetch read (both kinds; Unchecked where there was none).
struct GuiHistoryJobResult {
    GuiHistoryJobKind       kind    = GuiHistoryJobKind::Checkpoint;
    GuiHistoryCommitOutcome outcome = GuiHistoryCommitOutcome::CommitFailed;
    GuiGitHubStatus         github  = GuiGitHubStatus::Unchecked;
};

class GuiHistoryCommitWorker {
public:
    using DoneCallback = std::function<void(GuiHistoryJobResult)>;

    GuiHistoryCommitWorker();
    ~GuiHistoryCommitWorker();

    GuiHistoryCommitWorker(const GuiHistoryCommitWorker&)            = delete;
    GuiHistoryCommitWorker& operator=(const GuiHistoryCommitWorker&) = delete;

    // Create the eventfd and spawn the worker thread. False if the eventfd
    // could not be created (one stderr line of its own). Must run before any
    // dispatch.
    bool init();

    // Stop the worker and close the eventfd. Idempotent, safe after a failed
    // init, and called from the destructor. A CHECKPOINT IN FLIGHT IS WAITED
    // OUT — a quit must not abandon a commit or a push mid-step, and the
    // user's own state is already on disk (the act saves first); A CHECK IN
    // FLIGHT IS ABANDONED and never waited on (the class head owns both).
    void shutdown();

    // The eventfd the platform layer polls for completion. -1 before init().
    int completion_fd() const;

    // Run `job` on the worker. `on_done` fires on the MAIN thread from
    // on_completion_event with the job's own result.
    void dispatch(GuiHistoryCommitJob job, DoneCallback on_done);

    // Called by the platform layer when the completion eventfd fires (the
    // platform read()s the counter first, as it does for the render worker).
    void on_completion_event();

    // True from dispatch until the completion event has been consumed.
    bool is_busy() const;

private:
    enum class State : int { Idle, Running, CompletionPending };

    // EVERYTHING THE THREAD TOUCHES, co-owned by the thread so an abandoned
    // (detached) thread outlives this object safely.
    struct Shared {
        std::mutex              mtx;
        std::condition_variable cv;
        std::atomic<int>        state{static_cast<int>(State::Idle)};
        bool                    stop      = false;  // under mtx
        bool                    abandoned = false;  // under mtx
        std::atomic<bool>       cancel{false};
        std::optional<GuiHistoryCommitJob> pending_job;   // under mtx
        GuiHistoryJobKind       running_kind = GuiHistoryJobKind::Checkpoint;
        GuiHistoryJobResult     result;                   // under mtx
        int                     completion_fd = -1;       // under mtx
    };

    static void worker_loop(std::shared_ptr<Shared> shared);

    std::shared_ptr<Shared> shared_;
    std::thread             worker_;
    // Main thread only: set at dispatch, taken at the completion event.
    DoneCallback            on_done_;
};
