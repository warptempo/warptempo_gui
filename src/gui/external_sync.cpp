#include "external_sync.h"

#include "render_output_naming.h"   // render_staging_path, the product's one
                                    // staging spelling (rule 3 at the head)

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <sys/eventfd.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

// The act (the layout, the mirror and its four rules are stated whole at the
// head of external_sync.h) and its worker thread. Nothing below states a rule
// of its own; each site names the rule it is serving.

namespace {

// The two failure lines, and both name a PATH and then the system's own words
// verbatim. `read_failure` is rule 1's — every enumeration and every status
// that answers with an error_code ends here — and it names the path that could
// not be read. `copy_failure` names the DESTINATION side rather than the
// source: every way a write fails is a destination-side one — a read-only
// mount, a full stick, a permission the app was not granted — and on the
// tablet the first copy IS the plain-open() probe under the All-files
// permission, whose whole diagnostic is the `/storage/<uuid>/...` path it was
// refused. A staged copy names the staging path it was writing and its rename
// names the final one, so the line names the path the call itself failed on.
std::string read_failure(const std::filesystem::path& p,
                         const std::error_code&       ec) {
    return "Cannot read '" + p.string() + "': " + ec.message();
}

std::string copy_failure(const std::filesystem::path& to,
                         const std::error_code&       ec) {
    return "Could not copy '" + to.string() + "': " + ec.message();
}

std::string remove_failure(const std::filesystem::path& p,
                           const std::error_code&       ec) {
    return "Could not remove '" + p.string() + "': " + ec.message();
}

// ONE NON-THROWING DIRECTORY WALK, used by every listing in the act below.
// Every std::filesystem call in this file takes its error_code overload, and a
// range-for over a directory_iterator does not (its increment throws), so the
// increment is spelled out here once.
//
// IT ANSWERS RULE 1: the walk either runs to the end of the listing or reports
// the refusal line, and the callback reports its own the same way (any
// non-empty answer stops the walk at once). `optional_root` true is the rule's
// ONE carve-out — an ENOENT on `dir` ITSELF is an empty listing rather than a
// fault, which is what makes an unrendered deliverable, a project with no
// `tmp/` and a batch folder that is simply gone say "nothing to copy" instead
// of stopping the act. Nothing else is ever excused: an entry that vanishes
// mid-walk, a directory that cannot be opened, an iterator that stops half way
// are all faults, because a partial listing would make good files on the
// volume look unwanted.
using WalkFn =
    std::function<std::optional<std::string>(const std::filesystem::directory_entry&)>;

std::optional<std::string> walk_directory(const std::filesystem::path& dir,
                                          bool                         optional_root,
                                          const WalkFn&                fn) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
        if (optional_root && ec == std::errc::no_such_file_or_directory)
            return std::nullopt;
        return read_failure(dir, ec);
    }
    const std::filesystem::directory_iterator end;
    while (it != end) {
        if (std::optional<std::string> fault = fn(*it)) return fault;
        it.increment(ec);
        if (ec) return read_failure(dir, ec);
    }
    return std::nullopt;
}

} // namespace

// The volume rule's shared half (the contract is at the declaration). IT
// TOUCHES NO FILESYSTEM: the candidates are already the backend's answer, and
// the counting below can only be wrong if the discovery that produced them
// was.
std::expected<std::filesystem::path, std::string> sole_removable_volume(
        std::vector<std::filesystem::path> candidates) {
    std::sort(candidates.begin(), candidates.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
                  return a.filename().string() < b.filename().string();
              });
    // ONE MOUNT POINT IS ONE VOLUME however many times it was named: a mount
    // table can carry the same path twice (a remount, an overmount), and
    // counting those as several would refuse a stick that is plainly there.
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());

    if (candidates.empty())
        return std::unexpected(std::string("No removable volume mounted"));
    if (candidates.size() > 1) {
        std::string list;
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (i != 0) list += ", ";
            list += candidates[i].filename().string();
        }
        return std::unexpected("Several removable volumes mounted: " + list);
    }
    return candidates.front();
}

GuiExternalSyncOutcome run_external_sync(const GuiExternalSyncJob& job) {
    GuiExternalSyncOutcome out;
    const std::filesystem::path dest = job.volume / job.project_name;

    // EVERY REFUSAL LEAVES THROUGH HERE: the line the act composed becomes the
    // status line and the stderr line together, `ok` stays false, and nothing
    // below the refusal runs. On the tablet the stderr half reaches logcat
    // without a word of Android in this file, stderr being redirected onto the
    // log at the file descriptor (platform_android.cpp).
    auto refuse = [&out](std::string line) -> GuiExternalSyncOutcome {
        out.ok      = false;
        out.message = std::move(line);
        std::fprintf(stderr, "warptempo_gui: %s\n", out.message.c_str());
        return out;
    };

    // -- THE SET ------------------------------------------------------------
    //
    // Built first and whole, so the mirror's two halves read one description
    // of what belongs on the volume: the copies drive the writes, and their
    // destination paths ARE the kept set the deletions consult (rule 4 keeps
    // by identity, so the kept set is a list of real paths and never a list of
    // spellings).
    struct Copy {
        std::filesystem::path from;
        std::filesystem::path to;
    };
    std::vector<Copy>                  copies;
    std::vector<std::filesystem::path> kept_dirs;    // `<dest>/<batch>`
    std::vector<std::filesystem::path> kept_files;   // every copy's own `to`,
                                                     // filled once below

    std::error_code ec;
    if (!job.deliverable.empty()) {
        // Rule 1: an unrendered deliverable is ENOENT and is nothing to copy;
        // any other answer stops the act before a single deletion.
        const bool present =
            std::filesystem::is_regular_file(job.deliverable, ec);
        if (ec && ec != std::errc::no_such_file_or_directory)
            return refuse(read_failure(job.deliverable, ec));
        if (present) {
            copies.push_back(
                {job.deliverable, dest / job.deliverable.filename()});
        }
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
    if (auto fault = walk_directory(
            job.batch_root, true,
            [&](const std::filesystem::directory_entry& de)
                    -> std::optional<std::string> {
                std::error_code de_ec;
                const bool is_dir = de.is_directory(de_ec);
                if (de_ec) return read_failure(de.path(), de_ec);
                if (is_dir) batches.push_back(de.path());
                return std::nullopt;
            })) {
        return refuse(*fault);
    }
    std::sort(batches.begin(), batches.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
                  return a.filename().string() < b.filename().string();
              });
    for (const std::filesystem::path& batch : batches) {
        std::vector<std::filesystem::path> wavs;
        if (auto fault = walk_directory(
                batch, true,
                [&](const std::filesystem::directory_entry& de)
                        -> std::optional<std::string> {
                    std::error_code de_ec;
                    const bool is_file = de.is_regular_file(de_ec);
                    if (de_ec) return read_failure(de.path(), de_ec);
                    if (!is_file) return std::nullopt;
                    if (de.path().extension() != ".wav") return std::nullopt;
                    wavs.push_back(de.path());
                    return std::nullopt;
                })) {
            return refuse(*fault);
        }
        if (wavs.empty()) continue;   // an empty cell earns no folder on the volume
        std::sort(wavs.begin(), wavs.end(),
                  [](const std::filesystem::path& a,
                     const std::filesystem::path& b) {
                      return a.filename().string() < b.filename().string();
                  });
        const std::filesystem::path batch_dest = dest / batch.filename();
        kept_dirs.push_back(batch_dest);
        for (const std::filesystem::path& wav : wavs)
            copies.push_back({wav, batch_dest / wav.filename()});
    }

    // THE KEPT FILES ARE THE COPIES' OWN DESTINATIONS, taken off the one list
    // rather than accumulated beside it: what the act writes and what it keeps
    // cannot then disagree.
    for (const Copy& c : copies) kept_files.push_back(c.to);

    // -- WHAT THE ACT MAY TOUCH, ASKED BEFORE IT TOUCHES ANYTHING -----------
    //
    // Rule 2, and it runs to completion before the first create_directories:
    // the project's folder on the volume, every kept batch folder and every
    // kept destination file must be the real directory or the real file it is
    // about to be written into, or nothing at all. A name that is anything
    // else — a symbolic link above all — is a REFUSAL AND NOT A DELETION: what
    // a foreign link at one of our names means is the user's to decide, not
    // this act's.
    enum class DestKind { Directory, RegularFile };
    auto claim_destination = [](const std::filesystem::path& p,
                                DestKind kind) -> std::optional<std::string> {
        std::error_code st_ec;
        const std::filesystem::file_status st =
            std::filesystem::symlink_status(p, st_ec);
        // Nothing there is the ordinary case: the act creates it below.
        if (st.type() == std::filesystem::file_type::not_found)
            return std::nullopt;
        if (st_ec) return read_failure(p, st_ec);
        if (std::filesystem::is_symlink(st))
            return "'" + p.string() + "' is a symbolic link";
        if (kind == DestKind::Directory && !std::filesystem::is_directory(st))
            return "'" + p.string() + "' is not a directory";
        if (kind == DestKind::RegularFile &&
            !std::filesystem::is_regular_file(st))
            return "'" + p.string() + "' is not a regular file";
        return std::nullopt;
    };
    if (auto bad = claim_destination(dest, DestKind::Directory))
        return refuse(*bad);
    for (const std::filesystem::path& d : kept_dirs) {
        if (auto bad = claim_destination(d, DestKind::Directory))
            return refuse(*bad);
    }
    for (const Copy& c : copies) {
        if (auto bad = claim_destination(c.to, DestKind::RegularFile))
            return refuse(*bad);
    }

    // -- THE COPIES, FIRST --------------------------------------------------
    //
    // The project's folder and each batch folder are created as they are
    // needed; create_directories is content with a directory that already
    // exists, and the checks above have just proved that each of these names
    // is a real directory or is free. A failure here is the same class as a
    // failed copy and reports the same way, since it is the same write to the
    // same volume.
    std::filesystem::create_directories(dest, ec);
    if (ec) return refuse(copy_failure(dest, ec));
    for (const std::filesystem::path& batch_dest : kept_dirs) {
        std::filesystem::create_directories(batch_dest, ec);
        if (ec) return refuse(copy_failure(batch_dest, ec));
    }
    for (const Copy& c : copies) {
        // Rule 3: the bytes land on the staging sibling and only a COMPLETE
        // copy is renamed onto the final name, so the file this act fails to
        // replace is the file the user still has. A stale staging file from an
        // interrupted act is OURS BY NAME and goes first — with remove rather
        // than a truncating open, so a link left at that name is removed as a
        // link and never written through (rule 2). overwrite_existing is on
        // the staging name and on no other.
        //
        // vfat, the deployed filesystem: rename over an existing name replaces
        // it in one directory operation, which is the atomicity this act
        // assumes and all it needs — the previous file is either wholly there
        // or wholly replaced.
        const std::filesystem::path staging(render_staging_path(c.to.string()));
        std::filesystem::remove(staging, ec);
        if (ec) return refuse(remove_failure(staging, ec));
        std::filesystem::copy_file(
            c.from, staging, std::filesystem::copy_options::overwrite_existing,
            ec);
        if (ec) return refuse(copy_failure(staging, ec));
        std::filesystem::rename(staging, c.to, ec);
        if (ec) return refuse(copy_failure(c.to, ec));
    }

    // -- THE DELETIONS, AFTER -----------------------------------------------
    //
    // THE SCOPE IS `dest` AND TWO LEVELS DEEP, which is exactly the shape the
    // copies above write: a name at the top is either the deliverable or a
    // batch folder, and a name inside a kept batch folder is either one of its
    // wavs or not ours. Anything else — a stray file at the top, a folder that
    // is no longer a batch, a `peaks/` or a sidecar left inside a cell, a
    // whole subtree, a staging file left by an interrupted act at a name this
    // set no longer carries — goes with remove_all, which is bounded to the
    // entry it was handed. THE VOLUME ROOT AND EVERY OTHER PROJECT'S FOLDER
    // ARE UNTOUCHED BY CONSTRUCTION, and by rule 2 that is now literally true:
    // no path here is composed above `dest`, every entry is classified by its
    // symlink_status so a link is never traversed, and remove_all itself
    // deletes the contents of the entry it was handed and then the entry as if
    // by POSIX remove() — its recursion does not follow directory symlinks
    // (follow_directory_symlink is not set), so a link goes as a link.
    //
    // WHAT IS KEPT IS KEPT BY IDENTITY (rule 4): each entry is compared with
    // std::filesystem::equivalent against the paths the copies just wrote,
    // never against their spellings, so the case-insensitive volume keeps the
    // file it just received under whatever spelling its directory entry
    // carries. The comparison is a plain O(n·m) over the set — a dozen or so
    // names against a dozen or so entries, on a walk that is already doing a
    // stat per entry — and equivalent's own error (an entry that vanished
    // between the listing and the compare) is a refusal, rule 1. THE STAGING
    // NAMES ARE UNKEPT BY CONSTRUCTION: the kept set holds final names only,
    // and `<name>.wav.tmp` is a different file from `<name>.wav`.
    auto kept_by_identity =
        [](const std::filesystem::path&              entry,
           const std::vector<std::filesystem::path>& kept,
           bool& answer) -> std::optional<std::string> {
        answer = false;
        for (const std::filesystem::path& k : kept) {
            std::error_code eq_ec;
            if (std::filesystem::equivalent(entry, k, eq_ec)) {
                answer = true;
                return std::nullopt;
            }
            if (eq_ec) return read_failure(entry, eq_ec);
        }
        return std::nullopt;
    };
    auto remove_subtree =
        [](const std::filesystem::path& p) -> std::optional<std::string> {
        std::error_code rm_ec;
        std::filesystem::remove_all(p, rm_ec);
        if (rm_ec) return remove_failure(p, rm_ec);
        return std::nullopt;
    };
    // A LINK GOES AS A LINK, its target untouched: nothing outside `dest` is
    // reached even to be looked at. A link at a KEPT name never arrives here —
    // the checks above refused the act over one — so every link this pass sees
    // is one the set does not carry.
    auto remove_link =
        [](const std::filesystem::path& p) -> std::optional<std::string> {
        std::error_code rm_ec;
        std::filesystem::remove(p, rm_ec);
        if (rm_ec) return remove_failure(p, rm_ec);
        return std::nullopt;
    };

    if (auto fault = walk_directory(
            dest, false,
            [&](const std::filesystem::directory_entry& de)
                    -> std::optional<std::string> {
                std::error_code st_ec;
                const std::filesystem::file_status st = de.symlink_status(st_ec);
                if (st_ec) return read_failure(de.path(), st_ec);
                if (std::filesystem::is_symlink(st)) return remove_link(de.path());
                if (std::filesystem::is_directory(st)) {
                    bool kept = false;
                    if (auto bad = kept_by_identity(de.path(), kept_dirs, kept))
                        return bad;
                    if (!kept) return remove_subtree(de.path());
                    return walk_directory(
                        de.path(), false,
                        [&](const std::filesystem::directory_entry& fe)
                                -> std::optional<std::string> {
                            std::error_code fe_ec;
                            const std::filesystem::file_status fst =
                                fe.symlink_status(fe_ec);
                            if (fe_ec) return read_failure(fe.path(), fe_ec);
                            if (std::filesystem::is_symlink(fst))
                                return remove_link(fe.path());
                            bool file_kept = false;
                            if (auto bad = kept_by_identity(fe.path(), kept_files,
                                                            file_kept))
                                return bad;
                            if (!file_kept) return remove_subtree(fe.path());
                            return std::nullopt;
                        });
                }
                bool kept = false;
                if (auto bad = kept_by_identity(de.path(), kept_files, kept))
                    return bad;
                if (!kept) return remove_subtree(de.path());
                return std::nullopt;
            })) {
        return refuse(*fault);
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
