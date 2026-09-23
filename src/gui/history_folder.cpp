#include "history_folder.h"

#include "phaseresetmarkers.h"
#include "settings_file.h"
#include "sidecar_set.h"
#include "warpmarkers.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// THE FOLDER ROAD'S BODY. The format, the fork and the ruling live at the
// header; what is here is the filesystem work and nothing else. No git call
// appears in this file, which is the road's whole point.

namespace {

// THE MEMBER-NAME GRAMMAR, ONE OWNER: `<digits>_<7 lowercase hex>`. Anything
// else under `history/` is not a member (the header's format block owns the
// rule and what happens to the entry).
//
// THE DIGIT BOUND IS THE COMMIT COUNT'S, and for its reason (scan_history_walk,
// history_diff.cpp): strtoull saturates on overflow and would hand back a
// "valid" ordinal for a token of a thousand nines, so the length refuses it
// before the conversion does. A real export's ordinal is four digits.
bool parse_member_name(const std::string& name, unsigned long long& seq,
                       std::string& sha7) {
    constexpr std::size_t kMaxSeqDigits = 18;
    const std::size_t     bar           = name.find('_');
    if (bar == std::string::npos || bar == 0 || bar > kMaxSeqDigits) {
        return false;
    }
    if (name.find_first_not_of("0123456789", 0) < bar) return false;
    const std::string tail = name.substr(bar + 1);
    if (tail.size() != 7) return false;
    if (tail.find_first_not_of("0123456789abcdef") != std::string::npos) {
        return false;
    }
    errno                          = 0;
    const unsigned long long value = std::strtoull(name.c_str(), nullptr, 10);
    if (errno != 0) return false;
    seq  = value;
    sha7 = tail;
    return true;
}

// The member's own leaf name — `<seq>_<sha7>` AS THE EXPORTER SPELLED IT,
// zero padding and all, which is what the project-relative spelling below and
// the staleness tip both want. Reconstructing it from `seq` would drop the
// padding and invent a second spelling of one name.
std::string member_folder_name(const std::string& member_path) {
    return std::filesystem::path(member_path).filename().string();
}

// A file's PROJECT-RELATIVE spelling, `history/<seq>_<sha7>/<file>` — the
// display clause of every refusal here, and each blob's `path`. It is the
// folder-road counterpart of the committed blob path a git member carries, and
// it reads the same way: the part of the path the user can act on, with the
// absolute prefix they already know left off (messaging.md).
std::string shown_member_file(const std::string& member_path,
                              const std::string& file_name) {
    return std::string(kHistoryFolderName) + "/" +
           member_folder_name(member_path) + "/" + file_name;
}

// One file's bytes, whole. False on anything short of that — the file would
// not open, its length could not be taken, or the read came back short — so an
// empty string here always means an empty file, which both marker grammars
// accept as a valid whole one.
bool read_file_text(const std::filesystem::path& path, std::string& out) {
    out.clear();
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamoff end = in.tellg();
    if (end < 0) return false;
    constexpr unsigned long long kMaxSize = static_cast<unsigned long long>(
        std::numeric_limits<std::size_t>::max());
    if (static_cast<unsigned long long>(end) > kMaxSize) return false;
    std::string text(static_cast<std::size_t>(end), '\0');
    in.seekg(0, std::ios::beg);
    if (!text.empty() &&
        !in.read(text.data(), static_cast<std::streamsize>(text.size()))) {
        return false;
    }
    out = std::move(text);
    return true;
}

}  // namespace

std::string history_folder_of_source(const std::string& source_audio_path) {
    if (source_audio_path.empty()) return std::string();

    // THE SOURCE IS CANONICALIZED WHOLE FIRST, resolve_repo_root_for_source's
    // own reason (history_diff.cpp): a source named as a bare filename has no
    // parent folder to hang an export off, and canonicalizing against the
    // working directory is what makes a program launched from inside the
    // piece's folder find that folder's export. A canonicalization that
    // refuses falls back to the path as given rather than answering "no
    // folder" — the is_directory test below is the one that decides.
    std::error_code       ec;
    std::filesystem::path source = std::filesystem::weakly_canonical(
        std::filesystem::path(source_audio_path), ec);
    if (ec) source = std::filesystem::path(source_audio_path);

    const std::filesystem::path parent = source.parent_path();
    if (parent.empty()) return std::string();

    const std::filesystem::path folder = parent / kHistoryFolderName;
    ec.clear();
    if (!std::filesystem::is_directory(folder, ec) || ec) return std::string();
    return folder.string();
}

bool list_history_folder_members(const std::string& history_folder,
                                 std::vector<GuiHistoryFolderMember>& out,
                                 GuiFailure&                          failure) {
    out.clear();
    failure = GuiFailure{};

    const std::filesystem::path folder(history_folder);
    std::error_code             ec;
    std::filesystem::directory_iterator it(folder, ec);
    // THE ONE ARM THAT ENDS A RUN NOT OK on this road: the directory could not
    // be listed at all, which is a read that did not answer and never an empty
    // history (GuiHistoryScanResult, history_diff.h, owns the distinction and
    // enumerates every such arm). An EMPTY folder lists nothing and succeeds —
    // the ruled empty walk, the view opening at `0/0`.
    auto unlistable = [&failure, &folder, &out](const std::error_code& why) {
        out.clear();
        failure = path_failure("could not list the exported history in ",
                               folder, folder.filename().string(),
                               ": " + why.message());
        return false;
    };
    if (ec) return unlistable(ec);

    // THE INCREMENT'S OWN FAILURE IS CHECKED PAST THE LOOP, not inside it: an
    // increment that fails seats the iterator at the end, so the loop simply
    // stops and the error is still in `ec` when it does. Reading a truncated
    // listing as the whole folder would hide exports without saying so.
    const std::filesystem::directory_iterator end;
    for (; it != end; it.increment(ec)) {
        const std::filesystem::path entry = it->path();
        const std::string           name  = entry.filename().string();

        // NOT A MEMBER IS NOT A DEFECT — it is skipped and said out loud, and
        // it is NOT counted as hidden: the counted line is about exports whose
        // sidecars refuse the strict load, and an entry that is not a member
        // folder at all never reached a loader. One stderr line naming it is
        // the whole account.
        std::error_code entry_ec;
        const bool      is_dir = std::filesystem::is_directory(entry, entry_ec);
        unsigned long long seq  = 0;
        std::string        sha7;
        if (!is_dir || entry_ec || !parse_member_name(name, seq, sha7)) {
            std::fprintf(stderr,
                         "warptempo_gui: History ignored '%s' under '%s': not "
                         "a <seq>_<sha> member folder\n",
                         name.c_str(), history_folder.c_str());
            continue;
        }

        GuiHistoryFolderMember m;
        m.path = entry.string();
        m.seq  = seq;
        m.sha7 = std::move(sha7);
        out.push_back(std::move(m));
    }
    if (ec) return unlistable(ec);

    // NEWEST FIRST — descending ordinal, ties broken by name bytewise
    // descending so the order is TOTAL whatever the folder happens to hold.
    // The exporter produces no tie (one ordinal per checkpoint), so the second
    // term is there to make the answer deterministic rather than to serve a
    // case the format has.
    std::sort(out.begin(), out.end(),
              [](const GuiHistoryFolderMember& a,
                 const GuiHistoryFolderMember& b) {
                  if (a.seq != b.seq) return a.seq > b.seq;
                  return member_folder_name(a.path) >
                         member_folder_name(b.path);
              });
    return true;
}

bool load_history_folder_member_strict(const GuiHistoryFolderMember& member,
                                       const std::string&    base_name,
                                       GuiHistoryCommitLoad& out,
                                       GuiFailure&           failure) {
    out     = GuiHistoryCommitLoad{};
    failure = GuiFailure{};

    // EVERY REASON THAT NAMES A FILE IS TWO CLAUSES (GuiFailure, failure.h):
    // the full path on the diagnostic, which is the stderr and logcat surface,
    // and the member-relative spelling on the display, which is one line of a
    // card that clips. Both are composed at this site from the parts, never by
    // reducing one clause to the other. The two arms below name no path at
    // all, so they say the same words on both surfaces, which is what
    // plain_failure is.
    auto refuse = [&failure](std::string words) {
        failure = plain_failure(std::move(words));
        return false;
    };

    // THE CALL'S OWN PRECONDITIONS, read_commit_sidecars's missing-input arms
    // in this road's vocabulary: an empty base name would compose the three
    // paths out of an extension alone, and an empty member path would compose
    // them against the working directory — a silently different folder. They
    // refuse here rather than being handed to a loader.
    if (base_name.empty()) {
        return refuse("the source has no sidecar base name");
    }
    if (member.path.empty()) return refuse("no exported checkpoint was named");

    const std::filesystem::path folder(member.path);
    const std::string           label = member_folder_name(member.path);

    // THE WHOLE SET OR NOTHING, the git twin's own rule and for its reason: a
    // load in place is a whole-state replace, and inheriting some files from
    // the export and the rest from nowhere would compose a state no checkpoint
    // ever was. For the walk the same refusal is simple ineligibility — an
    // export that cannot be loaded is not stepped to. The three are asked in
    // kSidecarExtensions order, so the first missing one is named.
    static_assert(kSidecarCount == 3);
    std::filesystem::path file[kSidecarCount];
    std::string           shown[kSidecarCount];
    for (std::size_t i = 0; i < kSidecarCount; ++i) {
        file[i]  = sidecar_path(folder, base_name, i);
        shown[i] = shown_member_file(member.path,
                                     file[i].filename().string());
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file[i], ec) || ec) {
            failure = path_failure("the exported checkpoint " + label +
                                       " carries no ",
                                   file[i], shown[i], "");
            return false;
        }
    }

    // THE BYTES, WHOLE — the member's then side, which is what every delta of
    // this member is computed from, so no reading of the walk ever comes back
    // to the disk. A read that could not answer refuses here rather than
    // handing an empty string to a loader that would accept it.
    out.sidecars.sha    = member.sha7;
    out.sidecars.folder = member.path;
    GuiHistorySidecarBlob* blob[kSidecarCount] = {
        &out.sidecars.warpmarkers, &out.sidecars.phaseresetmarkers,
        &out.sidecars.settings};
    for (std::size_t i = 0; i < kSidecarCount; ++i) {
        blob[i]->path = shown[i];
        if (!read_file_text(file[i], blob[i]->text)) {
            failure = path_failure("could not read ", file[i], shown[i], "");
            return false;
        }
    }

    // THE THREE STRICT WHOLE-FILE LOADERS ARE THE JUDGES, run on the files
    // THEMSELVES — no scratch anywhere, the export's files already sitting
    // under the very names a source's sidecars wear. (The git twin stages its
    // blobs for exactly this: all three parse entry points are path-only and
    // frozen, and a GUI-side scanner over the strings would be a SECOND
    // GRAMMAR beside the strict one, which is what the gate exists to avoid.)
    // The order is the render-entry load-in-place's own. First error only, by
    // construction: every arm returns.
    //
    // THE LOADERS' WORDS ARE TAKEN APART FROM THEIR PATH (`path_free_reason`),
    // so the file is named once, by this site, in the two spellings the two
    // surfaces want — the loader's own composed sentence would put the
    // absolute path on the card.
    std::optional<std::string> load_reason;
    const auto load_words = [&load_reason](const std::string& composed) {
        return load_reason ? *load_reason : composed;
    };
    auto invalid = [&failure](const char*                  what,
                              const std::filesystem::path& full,
                              const std::string&           shown_path,
                              const std::string&           words) {
        failure = path_failure(std::string("invalid ") + what + " in ", full,
                               shown_path, ": " + words);
        return false;
    };

    auto settings = read_settings_file(file[kSidecarSettings].string(),
                                       &load_reason);
    if (!settings) {
        return invalid("settings", file[kSidecarSettings],
                       shown[kSidecarSettings], load_words(settings.error()));
    }
    out.settings = std::move(*settings);

    {
        GuiWarpMarkers m;
        auto r = m.load(file[kSidecarWarp].string(), &load_reason);
        if (!r) {
            return invalid("warp markers", file[kSidecarWarp],
                           shown[kSidecarWarp], load_words(r.error()));
        }
        out.warp_markers = m.markers();
    }
    {
        GuiPhaseResetMarkers t;
        auto r = t.load(file[kSidecarPhaseReset].string(), &load_reason);
        if (!r) {
            return invalid("phase reset markers", file[kSidecarPhaseReset],
                           shown[kSidecarPhaseReset], load_words(r.error()));
        }
        out.phase_reset_markers = t.markers();
    }
    return true;
}

void scan_history_folder_walk(
        const std::string& history_folder, const std::string& base_name,
        const std::function<bool()>&                         abandoned,
        const std::function<void(GuiHistoryCommitSidecars)>& on_member,
        const std::function<void(GuiHistoryScanResult)>&     on_done) {
    std::vector<GuiHistoryFolderMember> members;
    GuiFailure                          why;
    if (!list_history_folder_members(history_folder, members, why)) {
        GuiHistoryScanResult failed;
        failed.ok                 = false;
        failed.unavailable_reason = std::move(why);
        on_done(std::move(failed));
        return;
    }

    // THE LOAD GATE, the git road's own: each member's eligibility IS the `'`
    // act's validation, so every member the walk carries is one the act can
    // load. A refusal hides that export and is counted; the parsed stores are
    // discarded and the SIDECAR SNAPSHOTS kept, being the walk's then sides in
    // both readings. THE ABANDON CHECK IS THE LOOP'S TOP and the finest grain
    // that costs nothing — one member is three small file reads and three
    // strict parses.
    int hidden = 0;
    for (const GuiHistoryFolderMember& m : members) {
        if (abandoned()) break;
        GuiHistoryCommitLoad load;
        GuiFailure           refusal;
        if (!load_history_folder_member_strict(m, base_name, load, refusal)) {
            ++hidden;
            continue;
        }
        on_member(std::move(load.sidecars));
    }
    GuiHistoryScanResult done;
    done.hidden = hidden;
    on_done(std::move(done));
}
