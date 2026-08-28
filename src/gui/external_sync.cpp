#include "external_sync.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <sys/eventfd.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

// The act (the layout and the mirror are stated whole at the head of
// external_sync.h) and its worker thread.

namespace {

// ONE NON-THROWING DIRECTORY WALK, used by both halves below. Every
// std::filesystem call in this file takes its error_code overload, and a
// range-for over a directory_iterator does not (its increment throws), so the
// increment is spelled out here once. A directory that cannot be opened or
// cannot be walked to the end is simply the entries seen so far: the copy half
// then mirrors what it found, and the delete half removes only what it found,
// both of which are the extra-files-never-fewer side of the order.
void walk_directory(
        const std::filesystem::path& dir,
        const std::function<void(const std::filesystem::directory_entry&)>& fn) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) return;
    const std::filesystem::directory_iterator end;
    while (it != end) {
        fn(*it);
        it.increment(ec);
        if (ec) return;
    }
}

// The failure line, and it names the DESTINATION path rather than the source:
// every way this act fails is a destination-side one — a read-only mount, a
// full stick, a permission the app was not granted — and on the tablet the
// first copy IS the plain-open() probe under the All-files permission, whose
// whole diagnostic is the `/storage/<uuid>/...` path it was refused. The
// system's own words follow, verbatim.
std::string copy_failure(const std::filesystem::path& to,
                         const std::error_code&       ec) {
    return "Could not copy '" + to.string() + "': " + ec.message();
}

} // namespace

// The volume rule's shared half (the contract is at the declaration).
std::expected<std::filesystem::path, std::string> sole_removable_volume(
        const std::filesystem::path&    root,
        const std::vector<std::string>& excluded) {
    std::vector<std::string> names;
    walk_directory(root, [&](const std::filesystem::directory_entry& de) {
        std::error_code de_ec;
        if (!de.is_directory(de_ec)) return;
        const std::string name = de.path().filename().string();
        if (std::find(excluded.begin(), excluded.end(), name) != excluded.end())
            return;
        names.push_back(name);
    });
    std::sort(names.begin(), names.end());

    if (names.empty())
        return std::unexpected(std::string("No removable volume mounted"));
    if (names.size() > 1) {
        std::string list;
        for (size_t i = 0; i < names.size(); ++i) {
            if (i != 0) list += ", ";
            list += names[i];
        }
        return std::unexpected("Several removable volumes mounted: " + list);
    }
    return root / names.front();
}

GuiExternalSyncOutcome run_external_sync(const GuiExternalSyncJob& job) {
    GuiExternalSyncOutcome out;
    const std::filesystem::path dest = job.volume / job.project_name;

    // -- THE SET ------------------------------------------------------------
    //
    // Built first and whole, so the mirror's two halves read one description
    // of what belongs on the volume: `copies` drives the writes, `kept_files`
    // and `kept_dirs` drive the deletions.
    struct Copy {
        std::filesystem::path from;
        std::filesystem::path to;
    };
    std::vector<Copy>     copies;
    std::set<std::string> kept_files;  // "<name>" and "<batch>/<name>"
    std::set<std::string> kept_dirs;   // the batch folder names

    std::error_code ec;
    if (!job.deliverable.empty() &&
        std::filesystem::is_regular_file(job.deliverable, ec)) {
        const std::string name = job.deliverable.filename().string();
        copies.push_back({job.deliverable, dest / name});
        kept_files.insert(name);
    }

    // THE BATCH FOLDERS, walked off the batch root itself rather than through
    // GuiRendersDir: that walk holds AppState and belongs to the GUI thread,
    // and what this act wants is simpler than what the `'` load editor wants —
    // every directory under `tmp/` and every `.wav` directly inside it, with no
    // opinion about the `N_tag` and `NN` spellings the dispatchers write. The
    // order is plain byte order on both levels, the product's one order for
    // names on disk (project_model.h), so a sync's copies run in the order the
    // folders read.
    std::vector<std::filesystem::path> batches;
    walk_directory(job.batch_root, [&](const std::filesystem::directory_entry& de) {
        std::error_code de_ec;
        if (de.is_directory(de_ec)) batches.push_back(de.path());
    });
    std::sort(batches.begin(), batches.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
                  return a.filename().string() < b.filename().string();
              });
    for (const std::filesystem::path& batch : batches) {
        const std::string batch_name = batch.filename().string();
        std::vector<std::filesystem::path> wavs;
        walk_directory(batch, [&](const std::filesystem::directory_entry& de) {
            std::error_code de_ec;
            if (!de.is_regular_file(de_ec)) return;
            if (de.path().extension() != ".wav") return;
            wavs.push_back(de.path());
        });
        if (wavs.empty()) continue;   // an empty cell earns no folder on the volume
        std::sort(wavs.begin(), wavs.end(),
                  [](const std::filesystem::path& a,
                     const std::filesystem::path& b) {
                      return a.filename().string() < b.filename().string();
                  });
        kept_dirs.insert(batch_name);
        for (const std::filesystem::path& wav : wavs) {
            const std::string name = wav.filename().string();
            copies.push_back({wav, dest / batch_name / name});
            kept_files.insert(batch_name + "/" + name);
        }
    }

    // -- THE COPIES, FIRST --------------------------------------------------
    //
    // The project's folder and each batch folder are created as they are
    // needed; create_directories is content with a directory that already
    // exists. A failure here is the same class as a failed copy and reports
    // the same way, since it is the same write to the same volume.
    std::filesystem::create_directories(dest, ec);
    if (ec) {
        out.message = copy_failure(dest, ec);
        std::fprintf(stderr, "warptempo_gui: %s\n", out.message.c_str());
        return out;
    }
    for (const std::string& batch_name : kept_dirs) {
        std::filesystem::create_directories(dest / batch_name, ec);
        if (ec) {
            out.message = copy_failure(dest / batch_name, ec);
            std::fprintf(stderr, "warptempo_gui: %s\n", out.message.c_str());
            return out;
        }
    }
    for (const Copy& c : copies) {
        std::filesystem::copy_file(
            c.from, c.to, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            // THE FIRST FAILURE ENDS THE ACT and the deletions below do not
            // run: a mirror that lost a file it then failed to replace is
            // worse than one carrying a stale copy. On the tablet this line is
            // the All-files permission's own answer — and it reaches logcat
            // without a word of Android in this file, stderr being redirected
            // onto the log at the file descriptor (platform_android.cpp).
            out.message = copy_failure(c.to, ec);
            std::fprintf(stderr, "warptempo_gui: %s\n", out.message.c_str());
            return out;
        }
    }

    // -- THE DELETIONS, AFTER -----------------------------------------------
    //
    // THE SCOPE IS `dest` AND TWO LEVELS DEEP, which is exactly the shape the
    // copies above write: a name at the top is either the deliverable or a
    // batch folder, and a name inside a kept batch folder is either one of its
    // wavs or not ours. Anything else — a stray file at the top, a folder that
    // is no longer a batch, a `peaks/` or a sidecar left inside a cell, a
    // whole subtree — goes with remove_all, which is bounded to the entry it
    // was handed and so can never climb out of the project's own folder. The
    // volume root and every other project's folder on it are untouched by
    // construction: nothing here ever names a path above `dest`.
    std::string  removal_failure;
    auto remove_entry = [&](const std::filesystem::path& p) {
        if (!removal_failure.empty()) return;
        std::error_code rm_ec;
        std::filesystem::remove_all(p, rm_ec);
        if (rm_ec) {
            removal_failure =
                "Could not remove '" + p.string() + "': " + rm_ec.message();
        }
    };
    walk_directory(dest, [&](const std::filesystem::directory_entry& de) {
        std::error_code de_ec;
        const std::string name = de.path().filename().string();
        if (de.is_directory(de_ec)) {
            if (kept_dirs.find(name) == kept_dirs.end()) {
                remove_entry(de.path());
                return;
            }
            walk_directory(de.path(),
                           [&](const std::filesystem::directory_entry& fe) {
                               const std::string rel =
                                   name + "/" + fe.path().filename().string();
                               if (kept_files.find(rel) == kept_files.end())
                                   remove_entry(fe.path());
                           });
            return;
        }
        if (kept_files.find(name) == kept_files.end()) remove_entry(de.path());
    });
    if (!removal_failure.empty()) {
        out.message = std::move(removal_failure);
        std::fprintf(stderr, "warptempo_gui: %s\n", out.message.c_str());
        return out;
    }

    out.ok = true;
    out.message = "Synchronized " + std::to_string(copies.size()) +
                  (copies.size() == 1 ? " file to " : " files to ") +
                  dest.string();
    return out;
}

// ---------------------------------------------------------------------------
// The worker (the contract is at the declaration)
// ---------------------------------------------------------------------------

GuiExternalSyncWorker::GuiExternalSyncWorker() = default;

GuiExternalSyncWorker::~GuiExternalSyncWorker() {
    shutdown();
}

bool GuiExternalSyncWorker::init() {
    completion_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (completion_fd_ < 0) {
        std::fprintf(stderr,
            "warptempo_gui: eventfd() failed for the synchronization worker: "
            "%s\n",
            std::strerror(errno));
        return false;
    }
    stop_worker_.store(false);
    worker_ = std::thread(&GuiExternalSyncWorker::worker_loop, this);
    return true;
}

void GuiExternalSyncWorker::shutdown() {
    if (worker_.joinable()) {
        // NO CANCEL, BY DESIGN — the loop below finishes the act it is running
        // and only then sees the stop flag, so the join waits it out. A
        // copy_file abandoned part-way would leave a truncated wav on the
        // volume, which is the one thing the copies-then-deletions order does
        // not already make harmless.
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

void GuiExternalSyncWorker::dispatch(GuiExternalSyncJob job,
                                     DoneCallback       on_done) {
    // The caller serializes: a second act is refused on the status line while
    // one is in flight. Arriving here busy is a programming error — say so and
    // drop, never race.
    if (state_.load() != static_cast<int>(State::Idle)) {
        std::fprintf(stderr,
            "warptempo_gui: Synchronization worker dispatch while busy "
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

bool GuiExternalSyncWorker::is_busy() const {
    const int s = state_.load();
    return s == static_cast<int>(State::Running) ||
           s == static_cast<int>(State::CompletionPending);
}

void GuiExternalSyncWorker::worker_loop() {
    while (true) {
        GuiExternalSyncJob job;
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

        last_outcome_ = run_external_sync(job);

        state_.store(static_cast<int>(State::CompletionPending));
        signal_completion();
    }
}

void GuiExternalSyncWorker::signal_completion() {
    if (completion_fd_ < 0) return;
    const uint64_t one = 1;
    ssize_t n = ::write(completion_fd_, &one, sizeof(one));
    if (n != static_cast<ssize_t>(sizeof(one))) {
        std::fprintf(stderr,
            "warptempo_gui: Synchronization worker eventfd write failed: %s\n",
            std::strerror(errno));
    }
}

void GuiExternalSyncWorker::on_completion_event() {
    if (state_.load() != static_cast<int>(State::CompletionPending)) {
        // Spurious wakeup or platform race — nothing to do.
        return;
    }

    GuiExternalSyncOutcome outcome = last_outcome_;
    DoneCallback cb;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cb = std::move(on_done_);
        on_done_ = nullptr;
        state_.store(static_cast<int>(State::Idle));
    }
    if (cb) cb(std::move(outcome));
}
