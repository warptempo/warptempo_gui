#pragma once

#include <atomic>
#include <condition_variable>
#include <expected>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// SYNCHRONIZE TO EXTERNAL STORAGE (architect 2026-08-27) — the File menu's
// third act and this file is the one home of WHAT GOES ON THE VOLUME and of
// the thread that puts it there. The volume itself is the platform's answer
// (GuiPlatform::removable_volume, platform_wayland.h / platform_android.h:
// the one mounted removable volume, found and never configured); the act
// below is handed that path and never looks for one.
//
// THE LAYOUT, and this is its whole statement — "the earliest unambiguous
// path". `<volume>/<project name>/` holds:
//
//   * `<title>.wav`, the DELIVERABLE, taken straight out of the project's
//     `render/` (the path composed exactly as a render composes it,
//     render_output_naming.h). An ABSENT deliverable is nothing to copy for
//     it and not a refusal: a project whose deliverable has not been rendered
//     yet syncs its batch cells and says so with a count.
//   * each BATCH FOLDER out of the project's `tmp/` AS ITSELF —
//     `<volume>/<project name>/1_bpm/01.wav` — the folder name and the NN
//     numbering verbatim, because `01.wav` only means something inside its
//     batch folder.
//
// So `render/` and `tmp/` themselves do NOT appear on the volume: a folder
// that exists only to hold other wav folders is not needed there. WAV FILES
// ONLY — no sidecars, no `.fingerprint`, no `.peaks`, no `peaks/`: the volume
// is played from, not authored in.
//
// IT IS A MIRROR of exactly that set. After the copies, every file and folder
// under `<volume>/<project name>/` that is not in the set is deleted (the
// architect allowed wiping the stick). THE SCOPE IS THE PROJECT'S OWN FOLDER
// ON THE VOLUME AND NOTHING OUTSIDE IT — never another project's folder,
// never the volume root, which is what lets one stick carry several projects
// and a car head unit's own files beside them. The ORDER is copies first and
// deletions after, so an act interrupted part-way (a pulled stick, a killed
// process) leaves the volume with at most EXTRA files and never fewer.
//
// NOTHING IS SKIPPED AND NOTHING IS RETRIED. Every file in the set is copied
// with overwrite_existing on every act, and the size-and-mtime skip that
// suggests itself is deliberately not taken: the destination's mtime is the
// COPYING host's clock, this one physical stick is carried between the laptop
// and the tablet, and two clocks a few seconds apart would make a skip rule
// silently keep a stale render. Correctness first — the act is on a worker and
// costs the user no waiting. The FIRST failure ends the act with the
// destination path and the system's own words, and the deletions do not run
// after a failed copy (a half-copied mirror must not lose the file it failed
// to replace).
//
// THE ACT AUTHORS NOTHING and touches no AppState: the job below is captured
// whole by value on the GUI thread, and the worker reads only these four
// fields and the filesystem.

// THE VOLUME RULE'S SHARED HALF, called by both backends' own
// GuiPlatform::removable_volume() (platform_wayland.h / platform_android.h,
// which state what `root` and `excluded` are on that machine and why): the ONE
// directory entry directly under `root` whose name is not in `excluded`.
// Exactly one is the volume; ZERO answers "No removable volume mounted" and
// SEVERAL answers "Several removable volumes mounted: a, b" — the names in
// plain byte order, comma-separated, so the user reads which ones to unmount.
// A `root` that does not exist or cannot be walked is zero entries and gets
// the same first sentence: there is nothing mounted there either way. The
// counting rule and its two sentences live HERE, once, so the two backends
// differ only in the facts they hand in.
std::expected<std::filesystem::path, std::string> sole_removable_volume(
    const std::filesystem::path&    root,
    const std::vector<std::string>& excluded);

// One act's whole input, by value.
struct GuiExternalSyncJob {
    // The mounted removable volume's root (the platform's answer).
    std::filesystem::path volume;
    // The project's name — the folder under `projects_path` (project_model.h),
    // and the folder this act owns on the volume.
    std::string           project_name;
    // `<project>/render/<title>.wav`, composed by the caller through the
    // parser's one owner. May not exist; may be empty when no title resolves.
    std::filesystem::path deliverable;
    // `<project>/tmp`, the batch root (renders_dir.h). May not exist.
    std::filesystem::path batch_root;
};

// The act's verdict, composed on the worker and painted verbatim by the GUI
// thread's status line. `ok` false means nothing was mirrored and `message`
// names the file that stopped it.
struct GuiExternalSyncOutcome {
    bool        ok = false;
    std::string message;
};

// THE ACT ITSELF, run on the worker thread below. Non-throwing throughout
// (every std::filesystem call takes its error_code overload); it spawns no
// child process and reads no configuration.
GuiExternalSyncOutcome run_external_sync(const GuiExternalSyncJob& job);

// THE ACT'S BACKGROUND WORKER, shaped exactly like GuiHistoryCommitWorker
// (history_commit_worker.h) and for the same reason: writing tens of megabytes
// onto a USB stick takes seconds, and the GUI thread must not be the thread
// waiting on it. Own std::thread, a condition variable for the wake, and an
// eventfd the platform run loop polls, whose POLLIN makes the GUI thread call
// on_completion_event() and run the stored callback on the MAIN thread.
//
// SINGLE JOB IN FLIGHT, structurally: the caller refuses a second act while
// one runs (it asks is_busy() and answers on the status line), and a dispatch
// that arrives busy anyway is a programming error this class logs and drops
// rather than racing.
//
// NO CANCEL, like the checkpoint worker: shutdown() JOINS an act in flight
// instead of interrupting it. The copies are already the user's intent, and a
// mirror abandoned between its copies and its deletions is exactly the state
// the copies-then-deletions order is chosen to make harmless — but leaving one
// copy_file half-written is not, so the join waits.
class GuiExternalSyncWorker {
public:
    using DoneCallback = std::function<void(GuiExternalSyncOutcome)>;

    GuiExternalSyncWorker();
    ~GuiExternalSyncWorker();

    GuiExternalSyncWorker(const GuiExternalSyncWorker&)            = delete;
    GuiExternalSyncWorker& operator=(const GuiExternalSyncWorker&) = delete;

    // Create the eventfd and spawn the worker thread. False if the eventfd
    // could not be created (one stderr line of its own). Must run before any
    // dispatch.
    bool init();

    // Stop the worker and close the eventfd. Idempotent, safe after a failed
    // init, and called from the destructor. IT BLOCKS UNTIL AN IN-FLIGHT ACT
    // FINISHES, for the reason above.
    void shutdown();

    // The eventfd the platform layer polls for completion. -1 before init().
    int completion_fd() const { return completion_fd_; }

    // Run `job` on the worker. `on_done` fires on the MAIN thread from
    // on_completion_event with the act's own verdict.
    void dispatch(GuiExternalSyncJob job, DoneCallback on_done);

    // Called by the platform layer when the completion eventfd fires (the
    // platform read()s the counter first, as it does for the other workers).
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
    std::optional<GuiExternalSyncJob> pending_job_;
    DoneCallback                      on_done_;

    // Written by the worker just before signal_completion, read by the GUI
    // thread in on_completion_event.
    GuiExternalSyncOutcome last_outcome_;

    int completion_fd_ = -1;
};
