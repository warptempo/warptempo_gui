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
// A MIRROR THAT IS UNSURE DELETES NOTHING. Four rules say what that means and
// they are stated here once, for the whole act; the body below names the rule
// each site is serving and states none of its own.
//
//   1. THE MIRROR DELETES ONLY AGAINST A LISTING IT FINISHED. Every walk and
//      every status in the act carries its error_code and answers it. An
//      OPTIONAL root that is simply absent — a deliverable not rendered yet, a
//      project with no `tmp/`, a batch folder gone since the listing that
//      named it — is ENOENT and is an empty set. ANY OTHER answer, on either
//      side, ends the act with "Cannot read '<path>': <the system's own
//      words>" BEFORE a single deletion, a destination-enumeration error
//      included, which therefore can never report success. EVERY PATH A
//      SENTENCE NAMES IS RELATIVE TO THE MIRROR'S TWO ROOTS (architect
//      2026-08-29, the basename rule — the sentence is one line on a
//      notification card that clips, never a full path): `<volume
//      name>/<path under the volume>` on the stick, the path under the
//      project folder in the project (the one composer is `shown`,
//      external_sync.cpp; the full path is on stderr beside it). A directory that
//      cannot be opened, an iterator that stops half way and a stat that is
//      refused would each make good files on the volume look unwanted, and
//      that is the one mistake a mirror must not make. SO THE DELETION IS TWO
//      PASSES AND CLASSIFIES THE WHOLE DESTINATION BEFORE ITS FIRST REMOVAL:
//      the first pass reads the top level and then each kept batch folder and
//      sorts every entry into kept, unkept link and unkept subtree, answering
//      every listing, status and identity error as above; only once that
//      classification is complete does the second pass remove, in list order.
//      A read fault can therefore never arrive after a deletion has already
//      run, and no directory_iterator is ever live while its own directory is
//      being changed.
//      THE SET THIS MIRRORS IS THE ONE THE DISK ALREADY HOLDS (architect
//      2026-08-29): the render player's listing and the deliverable's publish
//      prune `render/` to the current title's `<title>.wav` and its
//      `.fingerprint` and nothing else (prune_render_folder, renders_dir.h), so
//      the deliverable this act copies and the deletions it makes on the stick
//      are ONE definition with the prune's on disk — a previous title's
//      deliverable is not a file the mirror sweeps off a volume while it still
//      sits in the project.
//   2. NO DESTINATION SYMLINK IS EVER FOLLOWED, which is what makes the scope
//      claim above true by construction rather than lexically. Before anything
//      is created or written, THE VOLUME ITSELF, the project's folder on the
//      volume, every kept batch folder and every kept destination file are read
//      with symlink_status: a name that exists and is not the real directory or
//      real file it is about to be written into REFUSES — "'<path>' is a
//      symbolic link", "'<path>' is not a directory", "'<path>' is not a
//      regular file". THE VOLUME IS THE FIRST NAME ASKED, because every path in
//      the act is composed under it and a link there would aim the whole mirror
//      — its creates, its copies and its removals — at whatever it points to.
//      It is asked on the DISCOVERY side too, where the same link means the
//      same thing: udisks makes real mount points under `/run/media/<user>/`,
//      so an entry there that is a link is a hand's work and REFUSES with the
//      link sentence rather than being counted as a volume or quietly passed
//      over (platform_wayland.cpp), while the tablet's candidates are the
//      process's own mount table's mount points and are real directories by
//      construction (platform_android.cpp). A refusal AND NOT A DELETION,
//      deliberately: what a foreign link at one of our own names means is the
//      user's to decide, not this act's. `create_directories` runs only after
//      those checks pass, and an unkept link is removed AS A LINK and never
//      traversed, so no composed path is written, walked or deleted through
//      one.
//
//      THE CHECKS RUN AT THE ACT'S START AND NOT AGAIN AT EACH USE, AND THAT
//      COST IS ACCEPTED: a name swapped for a link in the seconds between its
//      check and the create, copy or removal that follows would be followed,
//      std::filesystem being path-based and this act having no fd-relative
//      rewrite of it to offer. That swap is a hand on a mounted volume while
//      the act is running — the adversarial class this product never backstops.
//   3. EVERY COPY IS STAGED, the render's own publish shape (the staging
//      spelling is render_staging_path's, render_output_naming.h — the
//      product has one): the bytes land on `<destination>.tmp` and only a
//      COMPLETE copy is renamed onto the final name. A copy that fails, is
//      refused or is interrupted therefore leaves the previous file on the
//      volume whole — the file the act could not replace is the file the user
//      still has. A stale staging file from an interrupted act is ours by name
//      and is removed at the start of that file's copy; the staging names are
//      never in the kept set, so any other one goes with the deletions.
//   4. WHAT IS KEPT IS KEPT BY FILESYSTEM IDENTITY, NOT BY SPELLING. The
//      deletion pass keeps an entry when it IS one of the files the copies
//      just wrote (std::filesystem::equivalent), because the stick is vfat and
//      case-insensitive: a title changed from `My Title` to `my title` writes
//      through the existing `My Title.wav` entry, whose spelling on the volume
//      need not change, and a spelling comparison would then delete the very
//      file this act had just copied.
//
// NOTHING IS SKIPPED AND NOTHING IS RETRIED. Every file in the set is copied
// on every act, and the size-and-mtime skip that suggests itself is
// deliberately not taken: the destination's mtime is the COPYING host's clock,
// this one physical stick is carried between the laptop and the tablet, and
// two clocks a few seconds apart would make a skip rule silently keep a stale
// render. Correctness first — the act is on a worker and costs the user no
// waiting.
//
// THE FIRST FAILURE OF ANY KIND ENDS THE ACT, naming the path it was reading
// or writing and then the system's own words, and WHAT THAT FAILURE LEAVES IS
// EXACTLY THIS AND NOTHING STRONGER:
//
//   (a) NO DELETION RUNS AT ALL unless every copy succeeded AND the
//       destination's classification finished — the removals are the act's
//       last phase and both of those come before it.
//   (b) A FAILURE IN THE COPY PHASE leaves every replacement completed before
//       it standing, each having been its own rename, and leaves the file it
//       failed on holding its previous contents whole (rule 3).
//   (c) A FAILURE IN THE DELETION PHASE leaves the removals made before it
//       done and the rest undone, each removal being its own act; an unkept
//       subtree's own remove_all may stop part-way as well, so that subtree
//       can be left partly gone.
//
// The act is not transactional and rolls nothing back: running it again is the
// whole recovery, and a second act mirrors from wherever the first stopped.
//
// THE ACT AUTHORS NOTHING and touches no AppState: the job below is captured
// whole by value on the GUI thread, and the worker reads only these four
// fields and the filesystem.

// THE VOLUME RULE'S SHARED HALF — THE COUNTING RULE AND NOTHING ELSE, called
// by both backends' own GuiPlatform::removable_volume() with the candidate
// volume roots that backend's discovery found. Exactly one candidate IS the
// volume; ZERO answers "No removable volume mounted" and SEVERAL answers
// "Several removable volumes mounted: a, b" — the candidates' own names
// (each path's filename) in plain byte order, comma-separated, so the user
// reads which ones to unmount. The list is sorted and de-duplicated HERE,
// once, so neither backend does it: two mount-table lines naming one mount
// point are one volume, not several.
//
// HOW A BACKEND DISCOVERS ITS CANDIDATES IS THE BACKEND'S OWN and is stated at
// each definition (platform_wayland.cpp: the directory entries under the
// udisks mount root; platform_android.cpp: the `/storage/<name>` mount points
// in the process's own mount table). SO IS EVERY REFUSAL THAT IS NOT ABOUT
// COUNTING: a root that cannot be read is a permission fault and NOT zero
// volumes, and the backend refuses it out loud with the system's own words
// before it ever reaches here — this half touches no filesystem and so can
// never mistake a listing it was denied for an empty one.
std::expected<std::filesystem::path, std::string> sole_removable_volume(
    std::vector<std::filesystem::path> candidates);

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
    // The project folder itself (the source's parent, project_model.h) —
    // held so the act can name every project-side path relative to it (rule
    // 1's naming clause); nothing is read through it.
    std::filesystem::path project_dir;
};

// The act's verdict, composed on the worker and raised verbatim as a NORMAL
// notification card by the GUI thread (on_external_sync_complete). `ok` false means the act stopped where it stood and
// `message` names the path that stopped it; WHAT IT LEFT BEHIND is the head's
// (a)(b)(c) — no deletion at all unless every copy and the whole destination
// classification succeeded, a copy-phase failure leaving the replacements
// completed before it standing and the failed file's previous contents whole,
// a deletion-phase failure leaving the removals before it done and the rest
// undone.
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
// one runs (it asks is_busy() and answers on a notification card), and a dispatch
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
