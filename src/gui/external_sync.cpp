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

// The act (the layout, the mirror and its five rules are stated whole at the
// head of external_sync.h) and its worker thread. Nothing below states a rule
// of its own; each site names the rule it is serving.

namespace {

// HOW A PATH IS NAMED IN A SENTENCE (the basename rule, architect 2026-08-29:
// a message that carries a path names the file, never a full path — the
// sentence is one line on a notification card that clips): RELATIVE TO THE
// MIRROR'S TWO ROOTS. A path on the stick is `<sync root's last
// component>/<its path under the root>` — that one component leads so the
// reader knows which side failed, the root itself being just that name (it
// was the found VOLUME's folder name until 2026-08-30 and reads exactly the
// same, `sync_path` naming the mount folder the volume used to be:
// `KINGSTON/550 - 1/x.wav`); a path in the project
// is its path under the project folder (the folder itself its own name); and
// anything under neither — nothing in the act composes one — falls back to
// its filename. Lexical, like the paths themselves: every name in the act is
// composed under one of the two roots and no symlink is ever followed.
std::string shown(const GuiExternalSyncJob& job, const std::filesystem::path& p) {
    auto under = [&p](const std::filesystem::path& root,
                      std::string& out) -> bool {
        if (root.empty()) return false;
        const std::filesystem::path rel = p.lexically_relative(root);
        if (rel.empty()) return false;
        const std::string r = rel.generic_string();
        if (r == ".." || r.rfind("../", 0) == 0) return false;
        out = (r == ".") ? std::string() : r;
        return true;
    };
    std::string rel;
    if (under(job.sync_root, rel)) {
        // THE ROOT'S LAST COMPONENT, and a TRAILING SEPARATOR IS STRIPPED to
        // find it: `sync_path` is typed by hand now, and a shell's own
        // completion ends a folder in `/` — `/run/media/b/SANDISK/` has an
        // EMPTY filename() and would leave every sentence naming
        // `/550 - 1/x.wav` with no root at all. The discovered mount
        // point this replaced could never carry one, so this case is the
        // configured path's own (2026-08-30). The path itself is used
        // verbatim; only the display name walks up.
        const std::filesystem::path& root = job.sync_root;
        std::string name = root.filename().string();
        if (name.empty()) name = root.parent_path().filename().string();
        return rel.empty() ? name : name + "/" + rel;
    }
    if (under(job.project_dir, rel)) {
        return rel.empty() ? job.project_dir.filename().string() : rel;
    }
    return p.filename().string();
}

// EVERY FAILURE IN THE ACT IS TWO CLAUSES (GuiFailure, failure.h — the
// universal shape since 2026-09-02, the four-tier review's R-11): the
// DIAGNOSTIC names the FULL path for the stderr line, the DISPLAY names it as
// `shown` names it for the card, and both are composed here from the path
// and the words, never one from the other. (Until that day the act composed
// ONE line with the shown name and printed that same line to stderr, so the
// full path was on neither surface while the header claimed it was on the
// terminal — the claim is true now.) The four composers:
//
// `read_failure` is rule 1's — every enumeration and every status that
// answers with an error_code ends here — and it names the path that could
// not be read, then the system's own words verbatim. `copy_failure` names
// the DESTINATION side rather than the source: every way a write fails is a
// destination-side one — a read-only mount, a full stick, a permission the
// app was not granted — and on the tablet the first copy IS the plain-open()
// probe under the All-files permission, whose whole diagnostic is the
// `/storage/<uuid>/...` path it was refused (the full path is on stderr; the
// card names it under the sync root). A staged copy names the staging path it
// was writing and its rename names the final one, so the line names the path
// the call itself failed on. `remove_failure` is the deletion pass's.
// `kind_failure` is the claims' — "'<path>' is a symbolic link" and its two
// siblings — a sentence with no system words after the name.
GuiFailure read_failure(const GuiExternalSyncJob&    job,
                        const std::filesystem::path& p,
                        const std::error_code&       ec) {
    return path_failure("Cannot read ", p, shown(job, p),
                        ": " + ec.message());
}

GuiFailure copy_failure(const GuiExternalSyncJob&    job,
                        const std::filesystem::path& to,
                        const std::error_code&       ec) {
    return path_failure("Could not copy ", to, shown(job, to),
                        ": " + ec.message());
}

GuiFailure remove_failure(const GuiExternalSyncJob&    job,
                          const std::filesystem::path& p,
                          const std::error_code&       ec) {
    return path_failure("Could not remove ", p, shown(job, p),
                        ": " + ec.message());
}

GuiFailure kind_failure(const GuiExternalSyncJob&    job,
                        const std::filesystem::path& p,
                        const char*                  what_it_is) {
    return path_failure("", p, shown(job, p), what_it_is);
}

// THE DESTINATION'S OWN FOLD (rule 5 at the head), spelled once: an ASCII
// case fold of one directory entry's name. The stick is vfat and
// case-INSENSITIVE, so two desired entries in one destination directory whose
// names fold together are ONE entry there and the second would silently
// replace the first. The fold is ASCII-only and deliberately so: vfat's real
// rule is Unicode and codepage-dependent, this test is the one every host
// agrees on, and a pair it misses is a pair that folds on the stick but not
// here — the same class the act already refuses to guess at, and one the
// architect's own ASCII titles cannot produce.
std::string ascii_folded(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// ONE NON-THROWING DIRECTORY WALK, used by every listing in the act below.
// Every std::filesystem call in this file takes its error_code overload, and a
// range-for over a directory_iterator does not (its increment throws), so the
// increment is spelled out here once.
//
// IT IS THE SIBLING OF for_each_directory_entry (directory_walk.h, the GUI's
// one non-throwing walk owner) AND NOT A DUPLICATE OF IT: this one adds the
// two things the mirror needs and the owner deliberately has not got — a
// callback that REPORTS A FAULT STRING and stops the walk at once, and the
// `optional_root` ENOENT carve-out on `dir` itself. The owner's header carries
// the inventory of which walks route through it and which spell their own.
//
// It serves rule 1: the walk runs to the end of the listing or reports the
// refusal line, the callback reports its own the same way (any non-empty answer
// stops the walk at once), and `optional_root` true is the rule's ENOENT
// carve-out on `dir` itself.
using WalkFn =
    std::function<std::optional<GuiFailure>(const std::filesystem::directory_entry&)>;

std::optional<GuiFailure> walk_directory(const GuiExternalSyncJob&    job,
                                          const std::filesystem::path& dir,
                                          bool                         optional_root,
                                          const WalkFn&                fn) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
        if (optional_root && ec == std::errc::no_such_file_or_directory)
            return std::nullopt;
        return read_failure(job, dir, ec);
    }
    const std::filesystem::directory_iterator end;
    while (it != end) {
        if (std::optional<GuiFailure> fault = fn(*it)) return fault;
        it.increment(ec);
        if (ec) return read_failure(job, dir, ec);
    }
    return std::nullopt;
}

// THE LISTING RULE, ONE FOR BOTH OF THE PROJECT'S OUTPUT FOLDERS: the regular
// `.wav` files DIRECTLY inside `dir`, in plain byte order by filename — the
// product's one order for names on disk (project_model.h) — with no recursion
// and no opinion about how any of them is spelled. TWO CALLERS, `render/` and
// each batch folder under `tmp/`, so the deliverable side and the batch side
// cannot drift into two classifications of the same question.
//
// WHAT IT DROPS FALLS OUT OF THE RULE rather than out of a list of exceptions:
// a directory is not a regular file, and `.fingerprint`, `.peaks` and an
// interrupted act's staging `<name>.wav.tmp` are not `.wav`.
//
// THE CLASSIFICATION IS symlink_status, SO NOTHING IS COPIED THROUGH A LINK
// (rule 2, whose scope is both sides): `is_regular_file` FOLLOWS a link and
// this walk used it until 2026-09-02, so a `render/x.wav` pointing anywhere at
// all was read through and mirrored onto the stick while HELP promised the
// act would refuse it. A LINK WHOSE NAME IS A `.wav` IS ONE THE ACT WOULD
// COPY, and it refuses with the destination side's own sentence; a link with
// any other name is not in the set at all and is simply dropped, exactly as
// the destination side removes an unkept link rather than refusing over it.
// The line that separates them is whether the act would have to FOLLOW the
// link to decide: the extension is lexical, so a non-`.wav` needs no
// following.
//
// Rule 1 throughout: the root is OPTIONAL, so an absent folder is an empty set
// and every other answer — the walk's, and each entry's own status — is the
// refusal line the caller returns.
std::optional<GuiFailure> list_wav_files(
        const GuiExternalSyncJob&           job,
        const std::filesystem::path&        dir,
        std::vector<std::filesystem::path>& out) {
    if (auto fault = walk_directory(
            job, dir, true,
            [&](const std::filesystem::directory_entry& de)
                    -> std::optional<GuiFailure> {
                std::error_code de_ec;
                const std::filesystem::file_status st = de.symlink_status(de_ec);
                if (de_ec) return read_failure(job, de.path(), de_ec);
                const bool is_wav_name = de.path().extension() == ".wav";
                if (std::filesystem::is_symlink(st)) {
                    if (!is_wav_name) return std::nullopt;
                    return kind_failure(job, de.path(), " is a symbolic link");
                }
                if (!std::filesystem::is_regular_file(st)) return std::nullopt;
                if (!is_wav_name) return std::nullopt;
                out.push_back(de.path());
                return std::nullopt;
            })) {
        return fault;
    }
    std::sort(out.begin(), out.end(),
              [](const std::filesystem::path& a,
                 const std::filesystem::path& b) {
                  return a.filename().string() < b.filename().string();
              });
    return std::nullopt;
}

} // namespace

GuiExternalSyncOutcome run_external_sync(const GuiExternalSyncJob& job) {
    GuiExternalSyncOutcome out;
    const std::filesystem::path dest = job.sync_root / job.project_name;

    // EVERY REFUSAL LEAVES THROUGH HERE: the failure the act composed prints
    // its DIAGNOSTIC clause on stderr here, on the worker, and rides the
    // verdict whole so the GUI thread raises its DISPLAY clause on the
    // notification card (on_external_sync_complete); `ok` stays false, and
    // nothing below the refusal runs. On the tablet the stderr half reaches
    // logcat without a word of Android in this file, stderr being redirected
    // onto the log at the file descriptor (platform_android.cpp).
    auto refuse = [&out](GuiFailure failure) -> GuiExternalSyncOutcome {
        out.ok      = false;
        out.failure = std::move(failure);
        std::fprintf(stderr, "warptempo_gui: %s\n",
                     out.failure.diagnostic.c_str());
        return out;
    };

    // -- WHAT THE ACT MAY TOUCH, ASKED BEFORE IT TOUCHES ANYTHING -----------
    //
    // Rule 2. The claim below is every destination name's, and THE SYNC ROOT
    // is the one asked here at the act's head, before the set is even built:
    // every path in the act is composed under it.
    enum class DestKind { SyncRoot, Directory, RegularFile };
    auto claim_destination = [&job](const std::filesystem::path& p,
                                    DestKind kind) -> std::optional<GuiFailure> {
        std::error_code st_ec;
        const std::filesystem::file_status st =
            std::filesystem::symlink_status(p, st_ec);
        if (st.type() == std::filesystem::file_type::not_found) {
            // Nothing there is the ordinary case for a name the act creates
            // below — and it is NOT one for the sync root, which this act
            // never creates: `sync_path` is a folder the user names and this
            // act writes into, so a name that is not there is an unplugged
            // stick or a typo, and it refuses with the sentence a wrong KIND
            // gets (2026-08-30; while the root was FOUND rather than
            // configured this arm had no producer at all, the platform having
            // just answered that the volume was mounted).
            if (kind != DestKind::SyncRoot) return std::nullopt;
            return kind_failure(job, p, " is not a directory");
        }
        if (st_ec) return read_failure(job, p, st_ec);
        if (std::filesystem::is_symlink(st))
            return kind_failure(job, p, " is a symbolic link");
        if (kind != DestKind::RegularFile && !std::filesystem::is_directory(st))
            return kind_failure(job, p, " is not a directory");
        if (kind == DestKind::RegularFile &&
            !std::filesystem::is_regular_file(st))
            return kind_failure(job, p, " is not a regular file");
        return std::nullopt;
    };
    if (auto bad = claim_destination(job.sync_root, DestKind::SyncRoot))
        return refuse(*bad);

    // Rule 2 ON THE SOURCE SIDE: a root this act is about to OPEN is asked
    // with symlink_status before it is opened, so nothing is read through a
    // link either (until 2026-09-02 the rule was destination-scoped and a
    // symlinked `render/` or `tmp/` was walked normally, HELP promising
    // otherwise). ONLY THE LINK REFUSES here: an ABSENT root is the optional
    // root's ENOENT carve-out (rule 1) and belongs to the walk below, and a
    // root that is there but is not a directory keeps the walk's own
    // "Cannot read" line rather than gaining a second sentence for the same
    // fact. The batch folders under `tmp/` are the third source root and are
    // claimed by the walk that finds them, below.
    auto claim_source_root =
        [&job](const std::filesystem::path& p) -> std::optional<GuiFailure> {
        std::error_code st_ec;
        const std::filesystem::file_status st =
            std::filesystem::symlink_status(p, st_ec);
        if (st.type() == std::filesystem::file_type::not_found)
            return std::nullopt;
        if (st_ec) return read_failure(job, p, st_ec);
        if (std::filesystem::is_symlink(st))
            return kind_failure(job, p, " is a symbolic link");
        return std::nullopt;
    };
    if (auto bad = claim_source_root(job.render_root)) return refuse(*bad);
    if (auto bad = claim_source_root(job.batch_root))  return refuse(*bad);

    // -- THE SET ------------------------------------------------------------
    //
    // Built first and whole, so the mirror's two halves read one description
    // of what belongs on the stick: the copies drive the writes, and their
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

    // THE DELIVERABLE FOLDER'S OWN CONTENTS, listed rather than composed from
    // the live title (rule 1's set paragraph): every regular `.wav` directly
    // inside `render/` lands at the top of the project's folder on the stick,
    // under its own name. An absent folder is ENOENT and an empty set — this
    // act creates nothing on the source side — and any other answer stops the
    // act before a single deletion.
    std::vector<std::filesystem::path> deliverables;
    if (auto fault = list_wav_files(job, job.render_root, deliverables))
        return refuse(*fault);
    for (const std::filesystem::path& wav : deliverables)
        copies.push_back({wav, dest / wav.filename()});

    // THE BATCH FOLDERS, walked off the batch root itself rather than through
    // GuiRendersDir: that walk holds AppState and belongs to the GUI thread,
    // and what this act wants is simpler than what the render player wants —
    // every directory under `tmp/` and every `.wav` directly inside it, with no
    // opinion about the `N_tag` and `NN` spellings the dispatchers write. The
    // inner half is `list_wav_files` above, the SAME listing `render/` just
    // took, so the act has one rule for what a wav folder holds; this level
    // adds only the directories. The order is plain byte order on both levels,
    // the product's one order for names on disk (project_model.h), so a sync's
    // copies run in the order the folders read.
    //
    // Rule 2 on the source side, and THIS IS THE THIRD SOURCE ROOT'S CLAIM: a
    // batch folder is a root this act opens, so it is classified with
    // symlink_status (`is_directory` FOLLOWED a link and this walk used it
    // until 2026-09-02). A LINK HERE REFUSES OUTRIGHT, unlike the non-`.wav`
    // link the listing rule drops: whether the act would traverse this one
    // cannot be decided without FOLLOWING it — directory-ness is not lexical
    // the way an extension is — and the act refuses rather than guessing at a
    // name it may be about to walk.
    std::vector<std::filesystem::path> batches;
    if (auto fault = walk_directory(
            job, job.batch_root, true,
            [&](const std::filesystem::directory_entry& de)
                    -> std::optional<GuiFailure> {
                std::error_code de_ec;
                const std::filesystem::file_status st = de.symlink_status(de_ec);
                if (de_ec) return read_failure(job, de.path(), de_ec);
                if (std::filesystem::is_symlink(st))
                    return kind_failure(job, de.path(), " is a symbolic link");
                if (std::filesystem::is_directory(st))
                    batches.push_back(de.path());
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
        if (auto fault = list_wav_files(job, batch, wavs))
            return refuse(*fault);
        if (wavs.empty()) continue;   // an empty cell earns no folder on the stick
        const std::filesystem::path batch_dest = dest / batch.filename();
        kept_dirs.push_back(batch_dest);
        for (const std::filesystem::path& wav : wavs)
            copies.push_back({wav, batch_dest / wav.filename()});
    }

    // THE KEPT FILES ARE THE COPIES' OWN DESTINATIONS, taken off the one list
    // rather than accumulated beside it: what the act writes and what it keeps
    // cannot then disagree.
    for (const Copy& c : copies) kept_files.push_back(c.to);

    // -- TWO DESIRED NAMES THE DESTINATION COULD NOT HOLD APART -------------
    //
    // Rule 5, and it runs BEFORE ANYTHING IS CREATED OR COPIED because there
    // is no honest way to finish an act whose set the destination cannot
    // hold: the project's filesystem is case-SENSITIVE and the stick's is
    // not, so `My Title.wav` and `my title.wav` are two files here and ONE
    // entry there — the second staged rename replaces the first, rule 4's
    // identity test then reads the survivor as either of them and keeps it,
    // and the act reports success (silently) having shipped one of the two
    // and told nobody which. The CLI writes its deliverable with no prune, so
    // a retitle differing only in case produces the pair with nothing
    // adversarial in it.
    //
    // THE TEST IS ON THE DESIRED SET, PER DESTINATION DIRECTORY, and it
    // covers every entry the act wants there — `render/`'s wavs and the batch
    // FOLDERS at the top level, each cell's wavs inside it — because a folder
    // is an entry the destination folds exactly as a file is. The comparison
    // is the plain O(n²) this act already accepts for its identity test, over
    // a set of a few dozen names.
    struct Wanted {
        std::filesystem::path dir;      // the destination directory
        std::string           name;     // the entry it wants there
        std::filesystem::path source;   // the project-side path that wants it
    };
    std::vector<Wanted> wanted;
    for (const std::filesystem::path& d : kept_dirs) {
        wanted.push_back({d.parent_path(), d.filename().string(),
                          job.batch_root / d.filename()});
    }
    for (const Copy& c : copies)
        wanted.push_back({c.to.parent_path(), c.to.filename().string(), c.from});
    for (size_t i = 0; i < wanted.size(); ++i) {
        for (size_t j = i + 1; j < wanted.size(); ++j) {
            if (wanted[i].dir != wanted[j].dir) continue;
            if (ascii_folded(wanted[i].name) != ascii_folded(wanted[j].name))
                continue;
            // The sentence states the FACT rather than the cause, which is
            // what makes it true of the exact tie as well as of the fold (a
            // batch folder named `x.wav` beside a deliverable of that name):
            // whatever brought them together, these two want one entry.
            return refuse(two_path_failure(
                "", wanted[i].source, shown(job, wanted[i].source), " and ",
                wanted[j].source, shown(job, wanted[j].source),
                " would be one file at the destination"));
        }
    }

    // -- THE REST OF WHAT THE ACT MAY TOUCH ---------------------------------
    //
    // Rule 2, the sync root's own claim above having been the first of these:
    // the rest run to completion before the first create_directories.
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
    // same stick.
    std::filesystem::create_directories(dest, ec);
    if (ec) return refuse(copy_failure(job, dest, ec));
    for (const std::filesystem::path& batch_dest : kept_dirs) {
        std::filesystem::create_directories(batch_dest, ec);
        if (ec) return refuse(copy_failure(job, batch_dest, ec));
    }
    for (const Copy& c : copies) {
        // Rule 3: the bytes land on the staging sibling and only a complete
        // copy is renamed onto the final name. A stale staging file goes first
        // through remove rather than a truncating open, so a link left at that
        // name goes as a link and is never written through (rule 2), and
        // overwrite_existing is on the staging name and on no other. On vfat, a
        // rename over an existing name replaces it in one directory operation,
        // which is the atomicity this act assumes and all it needs.
        const std::filesystem::path staging(render_staging_path(c.to.string()));
        std::filesystem::remove(staging, ec);
        if (ec) return refuse(remove_failure(job, staging, ec));
        std::filesystem::copy_file(
            c.from, staging, std::filesystem::copy_options::overwrite_existing,
            ec);
        if (ec) return refuse(copy_failure(job, staging, ec));
        std::filesystem::rename(staging, c.to, ec);
        if (ec) return refuse(copy_failure(job, c.to, ec));
    }

    // -- THE DELETIONS, AFTER -----------------------------------------------
    //
    // THE SCOPE IS `dest` AND TWO LEVELS DEEP, which is exactly the shape the
    // copies above write: a name at the top is either one of `render/`'s wavs
    // or a batch folder, and a name inside a kept batch folder is either one of
    // its wavs or not ours. Anything else — a stray file at the top, a folder that
    // is no longer a batch, a `peaks/` or a sidecar left inside a cell, a whole
    // subtree, a staging file left by an interrupted act at a name this set no
    // longer carries — goes. No path here is composed above `dest` (rule 2,
    // which also carries the links: an unkept link is removed as a link, and
    // remove_all's own recursion does not follow one, follow_directory_symlink
    // being unset).
    //
    // Rule 1: IT IS TWO PASSES. Everything down to `doomed` below only
    // classifies — the top level first and then each kept batch folder, one
    // listing live at a time — and the removals run after that classification
    // has finished, in list order.
    struct Doomed {
        std::filesystem::path path;
        bool                  is_link = false;   // remove(); else remove_all()
    };
    std::vector<Doomed>                doomed;
    std::vector<std::filesystem::path> kept_dirs_present;

    // Rule 4: an entry is kept when it IS one of the paths the copies just
    // wrote (std::filesystem::equivalent) and never when it merely spells like
    // one. The comparison is a plain O(n·m) over the set — a dozen or so names
    // against a dozen or so entries, on a walk already doing a stat per entry —
    // and equivalent's own error is a refusal (rule 1). THE STAGING NAMES ARE
    // UNKEPT BY CONSTRUCTION: the kept set holds final names only, and
    // `<name>.wav.tmp` is a different file from `<name>.wav`.
    auto kept_by_identity =
        [&job](const std::filesystem::path&              entry,
               const std::vector<std::filesystem::path>& kept,
               bool& answer) -> std::optional<GuiFailure> {
        answer = false;
        for (const std::filesystem::path& k : kept) {
            std::error_code eq_ec;
            if (std::filesystem::equivalent(entry, k, eq_ec)) {
                answer = true;
                return std::nullopt;
            }
            if (eq_ec) return read_failure(job, entry, eq_ec);
        }
        return std::nullopt;
    };

    if (auto fault = walk_directory(
            job, dest, false,
            [&](const std::filesystem::directory_entry& de)
                    -> std::optional<GuiFailure> {
                std::error_code st_ec;
                const std::filesystem::file_status st = de.symlink_status(st_ec);
                if (st_ec) return read_failure(job, de.path(), st_ec);
                if (std::filesystem::is_symlink(st)) {
                    doomed.push_back({de.path(), true});
                    return std::nullopt;
                }
                if (std::filesystem::is_directory(st)) {
                    bool kept = false;
                    if (auto bad = kept_by_identity(de.path(), kept_dirs, kept))
                        return bad;
                    if (kept) kept_dirs_present.push_back(de.path());
                    else      doomed.push_back({de.path(), false});
                    return std::nullopt;
                }
                bool kept = false;
                if (auto bad = kept_by_identity(de.path(), kept_files, kept))
                    return bad;
                if (!kept) doomed.push_back({de.path(), false});
                return std::nullopt;
            })) {
        return refuse(*fault);
    }
    // The kept batch folders' own entries, listed after the top level rather
    // than from inside its callback: the classification holds one listing at a
    // time, and each of these folders is one the top level just proved kept.
    for (const std::filesystem::path& batch_dest : kept_dirs_present) {
        if (auto fault = walk_directory(
                job, batch_dest, false,
                [&](const std::filesystem::directory_entry& fe)
                        -> std::optional<GuiFailure> {
                    std::error_code fe_ec;
                    const std::filesystem::file_status fst =
                        fe.symlink_status(fe_ec);
                    if (fe_ec) return read_failure(job, fe.path(), fe_ec);
                    if (std::filesystem::is_symlink(fst)) {
                        doomed.push_back({fe.path(), true});
                        return std::nullopt;
                    }
                    bool file_kept = false;
                    if (auto bad =
                            kept_by_identity(fe.path(), kept_files, file_kept))
                        return bad;
                    if (!file_kept) doomed.push_back({fe.path(), false});
                    return std::nullopt;
                })) {
            return refuse(*fault);
        }
    }

    // THE SECOND PASS, and the act's first removal is here. A LINK GOES AS A
    // LINK, its target untouched, so nothing outside `dest` is reached even to
    // be looked at; a link at a kept name never reaches this list, the claims
    // above having refused the act over one. A failure here leaves the removals
    // before it done and the rest undone — the head's (c).
    for (const Doomed& d : doomed) {
        std::error_code rm_ec;
        if (d.is_link) std::filesystem::remove(d.path, rm_ec);
        else           std::filesystem::remove_all(d.path, rm_ec);
        if (rm_ec) return refuse(remove_failure(job, d.path, rm_ec));
    }

    // A SUCCESSFUL SYNCHRONIZATION SAYS NOTHING (architect 2026-08-30) — the
    // render's own precedent: a render served silently publishes silently. The
    // verdict leaves with `ok` true and an EMPTY message, and
    // on_external_sync_complete raises no card for it; the count sentence that
    // stood here from 2026-08-28 (`Synchronized N file(s) to <path>`) is
    // deleted rather than left unraised, so nothing composes a line no surface
    // shows. THE FAILURES ARE STILL LOUD, on the card and on stderr both
    // (`refuse` above is their one owner).
    out.ok = true;
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
        // stick, which is the one thing the copies-then-deletions order does
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
    // The caller serializes: a second act is refused on a notification card
    // while one is in flight (synchronize_to_external_storage asks is_busy
    // first, so this arm has no reachable producer). Arriving here busy is a
    // programming error — say so and drop, never race.
    if (state_.load() != static_cast<int>(State::Idle)) {
        std::fprintf(stderr,
            "warptempo_gui: Synchronization worker dispatch while busy "
            "(state=%d): the request was dropped\n",
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
