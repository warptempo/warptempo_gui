#include "history_diff.h"

#include "app_state.h"
#include "device_config.h"   // shown_project_path (the card's name for a file)
#include "frame_format.h"
// Every git question this module asks goes through the one git road
// (libgit2 in process); this file keeps the policy.
#include "git_repo.h"
#include "history_prefetch.h"
#include "phaseresetmarkers.h"
#include "settings_io.h"
// marker_effectively_disabled, the one label-cascade owner — a header template
// over the parser marker shape, so reading it here touches no frozen .cpp.
#include "warp_frame_map_build.h"
#include "warpmarkers.h"
#include "warpmarkers_parse.h"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

// THE REPOSITORY ROOT IS DERIVED FROM THE LOADED SOURCE, and there is no
// constant for it (architect 2026-08-11, superseding the fixed absolute path
// that stood here: the product has TWO HOSTS now — an x86 laptop and a Raspberry
// Pi road rig — so the single-laptop premise the path rested on is false, and
// the rig had to replicate the laptop's directory layout by hand for the mode to
// work at all).
//
// THE CLONE YOU OPEN FROM IS THE CLONE THAT COMMITS — the folder law (a piece's
// folder is the folder its source sits in) carried one level up. The owner is
// resolve_repo_root_for_source below — libgit2's discovery from the source's
// parent folder, canonicalized — and the answer TRAVELS AS A VALUE: every git
// question in this file takes its root as a parameter, so there is no fallback
// search, no environment variable, no walk up from the binary and no mutable
// global for the two worker threads to race on. The two ways the derivation can
// refuse — a source in no clone, and a read that could not answer — are recorded
// at that function.

// THE WALK IS UNCAPPED (architect 2026-08-07, retiring the ruled depth of 20).
// The cap existed because the load gate's per-candidate strict load ran at `h`
// and the entry had to finish in a keystroke; the scan runs on a background
// worker now and streams its members, so there is no keystroke to fit inside and
// no reason to hide the older half of a piece's history.

// THE CORPUS FOLDER — the one directory name this module knows (architect
// 2026-08-04). The repository layout convention is `<repo>/projects/<piece>/`;
// everything below this prefix is still matched by NAME, never by folder, so a
// piece may be renamed or nested freely. The trailing slash is part of the
// constant so the prefix test cannot accidentally match a sibling called
// `projects_old/`.
constexpr std::string_view kProjectsPrefix = "projects/";

// WHICH BRANCH THE HISTORY IS. The LOCAL one, not `origin/main`: the commit act
// makes this product a producer of checkpoints, and one whose push failed must
// still be visible history rather than hidden until the next successful push.
// It is spelled `HEAD` — whatever this clone has checked out, needing no name —
// and the same HEAD serves the history WALK and the tip read that keys its
// freshness (GuiGitRepo::walk_head and head_commit), so the mode's readers
// cannot come to mean different things.
//
// THE COMMIT ACT READS HEAD'S BRANCH NAME EXACTLY ONCE, and then names
// `refs/heads/<that name>` at both ends of its push refspec. Reading HEAD again
// at the push would let a checkout mid-act publish onto a branch the act never
// looked at; reading it once cannot. The read-only mode has no such exposure: it
// publishes nothing, and a checkout under it simply shows the branch that is now
// checked out.
//
// The projects_repo guard is unaffected either way: it asks which REPOSITORY
// this clone is, not how fresh it is.

// The sidecars a source carries are the product's one list,
// kSidecarExtensions (sidecar_set.h), whose ORDER indexes this module's
// per-sidecar arrays. A directory matches if it holds ANY of them under the
// source's base name — the architect's checkpoints are complete sets, but a
// partial one should still be FOUND rather than silently missed: the match
// answers where the piece lives, and the strict load gate is what then refuses
// a partial commit, at walk entry and at the `'` act alike. THE MATCH READS
// THE THREE MEMBERS OF THE SET AND NO OTHER NAME: a commit that also carries
// a `.magnificationlevelmarkers` (the set was four from 2026-09-15 until that
// column's deletion) is matched, listed and loaded on its three, that file
// never read — the eligibility paragraph at history_diff.h's head.

// Pathological-input guards for the line diff. The real files are tens to
// a few hundred lines, so both are unreachable in practice; they exist so a
// hand-edited or corrupt blob can never make the DP table an allocation
// hazard. kMaxDiffCells is the binding one — the table is (n+1)*(m+1)
// cells and 16M of them is 64 MB, well past anything real. Crossing either
// cap degrades that file to whole-file-replaced (every `then` line removed,
// every `now` line added), which is a truthful if coarse answer.
constexpr std::size_t kMaxDiffLines = 10000;
constexpr std::size_t kMaxDiffCells = 16u * 1024u * 1024u;

// The one settings key this mode displays. Matched as a whole-line PREFIX, not
// as a substring: a key ENDING in `scale=` would contain this text without
// being this key, and until 2026-08-27 the sidecar shipped exactly such a key
// (`gui_scale=100`, which moved to the per-device config that day). The rule
// outlives its one demonstration.
constexpr std::string_view kScaleKeyPrefix = "scale=";

// ---------------------------------------------------------------------------
// git
// ---------------------------------------------------------------------------
//
// EVERY GIT QUESTION GOES THROUGH GuiGitRepo (git_repo.h), libgit2 in process:
// no child process, no shell, no pathspec grammar and no output to parse. Each
// public entry point below opens its own handle on the clone it is handed —
// never shared across threads (the seam's head owns that rule) — and the scan
// holds one handle for its whole run. The seam's reads write nothing; its
// five mutators have THREE callers, all at the foot of this file: the commit
// act, the GitHub check and the pull.

// ---------------------------------------------------------------------------
// text helpers
// ---------------------------------------------------------------------------

// Split on '\n'. The terminator belongs to the line it ends, so a file whose
// last byte is '\n' — which is every file the writers produce — yields no
// trailing empty element, and an empty file yields no lines at all. A final
// unterminated run (only a hand edit makes one) is still a line.
std::vector<std::string> split_on(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::size_t              i = 0;
    while (i < s.size()) {
        const std::size_t at = s.find(sep, i);
        if (at == std::string::npos) {
            parts.emplace_back(s, i, s.size() - i);
            break;
        }
        parts.emplace_back(s, i, at - i);
        i = at + 1;
    }
    return parts;
}

std::vector<std::string> split_lines(const std::string& s) {
    return split_on(s, '\n');
}

// Trailing whitespace off a configured or stored spelling.
std::string trim_trailing_ws(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' ||
                          s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    return s;
}

// THE DIFF REPORTS LINE POSITIONS, NOT LINE TEXT (2026-09-16, Sol round 16's
// P1): each side stays split, and `added` / `removed` are indices into
// `now_lines` / `then_lines`, in file order. The text is one subscript away;
// the POSITION is what a line's ordinal within its frame's run is derived
// from (run_ordinals below), which is the identity a diff flag carries into
// the revert (the contract at GuiHistoryWarpEntry::ordinal, history_diff.h).
struct LineDiff {
    std::vector<std::string> then_lines;
    std::vector<std::string> now_lines;
    std::vector<std::size_t> added;    // indices into now_lines: on `now` only
    std::vector<std::size_t> removed;  // indices into then_lines: on `then` only
    bool                     degraded = false;
};

// Classic LCS over whole lines with exact byte equality, in tree because the
// files are tiny and a diff library would be a dependency bought for nothing.
// A common prefix and suffix are peeled first, which is what keeps the DP table
// small on the ordinary case (one edited line in a long sorted file).
LineDiff diff_lines(const std::string& then_text, const std::string& now_text) {
    LineDiff out;
    out.then_lines = split_lines(then_text);
    out.now_lines  = split_lines(now_text);
    const std::vector<std::string>& a = out.then_lines;
    const std::vector<std::string>& b = out.now_lines;

    std::size_t lo = 0;
    while (lo < a.size() && lo < b.size() && a[lo] == b[lo]) ++lo;
    std::size_t a_hi = a.size();
    std::size_t b_hi = b.size();
    while (a_hi > lo && b_hi > lo && a[a_hi - 1] == b[b_hi - 1]) {
        --a_hi;
        --b_hi;
    }

    const std::size_t n = a_hi - lo;
    const std::size_t m = b_hi - lo;

    if (n > kMaxDiffLines || m > kMaxDiffLines ||
        (n + 1) * (m + 1) > kMaxDiffCells) {
        // Degrade to whole-file-replaced over the un-peeled middle.
        out.degraded = true;
        for (std::size_t i = lo; i < a_hi; ++i) out.removed.push_back(i);
        for (std::size_t j = lo; j < b_hi; ++j) out.added.push_back(j);
        return out;
    }

    // dp[i][j] = LCS length of a[lo..lo+i) and b[lo..lo+j).
    std::vector<std::uint32_t> dp((n + 1) * (m + 1), 0);
    auto at = [m](std::size_t i, std::size_t j) -> std::size_t {
        return i * (m + 1) + j;
    };
    for (std::size_t i = 1; i <= n; ++i) {
        for (std::size_t j = 1; j <= m; ++j) {
            dp[at(i, j)] = (a[lo + i - 1] == b[lo + j - 1])
                               ? dp[at(i - 1, j - 1)] + 1
                               : std::max(dp[at(i - 1, j)], dp[at(i, j - 1)]);
        }
    }

    // Walk back to the origin, collecting the two difference sets. Ties favour
    // the `now` side so an addition is reported before the removal it sits
    // beside; both lists come out reversed and are flipped at the end, leaving
    // them in file order.
    std::size_t i = n;
    std::size_t j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && a[lo + i - 1] == b[lo + j - 1]) {
            --i;
            --j;
        } else if (j > 0 && (i == 0 || dp[at(i, j - 1)] >= dp[at(i - 1, j)])) {
            out.added.push_back(lo + j - 1);
            --j;
        } else {
            out.removed.push_back(lo + i - 1);
            --i;
        }
    }
    std::reverse(out.added.begin(), out.added.end());
    std::reverse(out.removed.begin(), out.removed.end());
    return out;
}

// EACH LINE'S ORDINAL WITHIN ITS FRAME'S RUN on its own side (2026-09-16) —
// the identity the revert addresses by; the contract is at
// GuiHistoryWarpEntry::ordinal, history_diff.h. Both sides are loader-clean,
// so their frames are non-decreasing and an equal-frame run is contiguous:
// the ordinal is the count of lines at the same frame directly above. `frame_of`
// answers a line's frame or nothing; a line it refuses (unreachable — the
// extractors' own defensive arm) closes the run on both sides of it.
template <typename FrameOf>
std::vector<int> run_ordinals(const std::vector<std::string>& lines,
                              FrameOf                          frame_of) {
    std::vector<int>       out(lines.size(), 0);
    std::optional<int64_t> prev;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::optional<int64_t> frame = frame_of(lines[i]);
        out[i] = (frame && prev && *frame == *prev) ? out[i - 1] + 1 : 0;
        prev   = frame;
    }
    return out;
}

// The `scale=` value text, verbatim, or empty when the side carries no such
// line.
std::string scale_token_of(const std::string& settings_text) {
    for (const std::string& line : split_lines(settings_text)) {
        if (line.size() >= kScaleKeyPrefix.size() &&
            std::string_view(line).substr(0, kScaleKeyPrefix.size()) ==
                kScaleKeyPrefix) {
            return line.substr(kScaleKeyPrefix.size());
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// line -> typed entry
// ---------------------------------------------------------------------------

// The frozen parser's own line-level entry point is the gate: a line this
// module accepts is exactly a line the loader would accept, with no second
// grammar written here to drift from it. The typed result supplies the frame
// and the disable bit; the tempo token is sliced back out of the raw line so
// the flag can show the file's own spelling rather than a round trip through
// the typed value.
bool extract_warp_entry(const std::string& line, GuiHistoryWarpEntry& out) {
    auto parsed = warpmarkers_internal::parse_single_canonical_line(line);
    if (!parsed) return false;
    out.frame    = parsed->time_frame;
    out.disabled = parsed->disabled;
    const std::size_t pipe = line.find('|');
    // The parse succeeded, so the '|' is there; the guard is defensive. The
    // slice is rest-of-line, so any comment rides inside the token —
    // deliberately: the revert rebuilds its line out of exactly this text.
    out.tempo_token =
        (pipe == std::string::npos) ? std::string() : line.substr(pipe + 1);
    return true;
}

// ONE SIDE'S EFFECTIVE-DISABLED VERDICTS, resolved within that side's own
// commit (architect 2026-08-22, the disabled axis carried one layer deeper: a
// label ref with no '#' of its own whose DEFINITION is disabled on the same
// side is effectively disabled there, and until this landed its history flag
// painted full-strength while the same side's live marker dimmed). The side's
// marker lines are parsed IN FILE ORDER into parser-domain WarpMarkers — index
// i of the vector is the i-th marker line — and each index resolves through
// the ONE cascade owner, marker_effectively_disabled (warp_frame_map_build.h).
//
// THE ANSWER IS KEYED BY LINE TEXT because the diff hands the entry loops
// LINES, not indices — and a first-match by text is EXACT even for
// byte-identical duplicate lines (coincident stacks are legal): the cascade
// reads only fields the bytes determine (disabled, label_ref, label_def)
// against the same side vector, so two identical lines carry identical
// verdicts by construction, and which duplicate a lookup lands on cannot
// matter. Empty lines are skipped (split_lines yields one for the trailing
// newline); a '#'-prefixed line is a DISABLED MARKER in this grammar, not a
// comment, and a non-marker line cannot occur at all — the then side passed
// the strict whole-set load and the now side is the writers' own output — so
// the parse's refusal arm below is defensive, and a line it skipped could
// strand no lookup: extract_warp_entry refuses the same line the same way, so
// it produces no entry either.
std::map<std::string, bool> warp_side_effective_disabled(
        const std::string& side_text) {
    const std::vector<std::string> lines = split_lines(side_text);
    std::vector<WarpMarker>         mv;
    std::vector<const std::string*> line_of;
    mv.reserve(lines.size());
    line_of.reserve(lines.size());
    for (const std::string& line : lines) {
        if (line.empty()) continue;
        auto parsed = warpmarkers_internal::parse_single_canonical_line(line);
        if (!parsed) continue;
        mv.push_back(std::move(*parsed));
        line_of.push_back(&line);
    }
    std::map<std::string, bool> out;
    for (std::size_t i = 0; i < mv.size(); ++i) {
        // emplace keeps the first verdict for a duplicate line — identical by
        // the argument above.
        out.emplace(*line_of[i], marker_effectively_disabled(mv, i));
    }
    return out;
}

// The phase reset column has NO callable per-line entry point — its parser's
// parse_line lives in an anonymous namespace, whole-file only — so this
// mirrors it exactly rather than relaxing anything: no whitespace anywhere on
// the line (so a ` //` suffix refuses, as it does on the warp column),
// an optional leading '#' meaning disabled, then the
// CANONICAL authored frame spelling (parse_authored_frame, frame_format.h) and
// nothing else. A byte-empty line has no frame and is refused here just as it
// is at load; comment LINES are not in the grammar. The frame parse is this
// module's own hand-mirror of the loader's, the standing recorded wart.
bool extract_phase_reset_entry(const std::string&         line,
                               GuiHistoryPhaseResetEntry& out) {
    std::string_view t = line;
    if (t.find_first_of(" \t\r") != std::string_view::npos) return false;
    out.disabled = false;
    if (!t.empty() && t.front() == '#') {
        out.disabled = true;
        t.remove_prefix(1);
    }
    return parse_authored_frame(t, out.frame);
}

// Pair a removal and an addition sharing one frame into a change, leaving the
// unpaired remainder in place. Coincident markers are legal on every column,
// so the pairing is positional per frame — the first unclaimed addition at
// that frame — and any surplus on either side stays a plain add or remove.
// File order survives: neither surviving list is re-sorted. THE PAIRING IS
// THE LANE'S, NOT THE REVERT'S (2026-09-16): which removal a double flag shows
// beside which addition is decided here, while WHICH ROW each half names is
// the entry's own ordinal, which `make_change` carries through onto the
// change as its then and now ordinals — so a pair whose halves sit at
// different ordinals of one run (`[100|4, 100|1]` against `[100|1, 100|3]`
// pairs then-0 with now-1) still reverts to the checkpoint's own rows.
// (The phase reset propagate's state paste pairs by its own block walk in
// phase_reset_propagate.cpp and never reaches this function.)
template <typename Entry, typename Change, typename Make>
void pair_changes_by_frame(std::vector<Entry>&  removed,
                           std::vector<Entry>&  added,
                           std::vector<Change>& changed,
                           Make                 make_change) {
    std::vector<bool>  claimed(added.size(), false);
    std::vector<Entry> kept_removed;
    kept_removed.reserve(removed.size());

    for (const Entry& r : removed) {
        std::size_t match = added.size();
        for (std::size_t j = 0; j < added.size(); ++j) {
            if (!claimed[j] && added[j].frame == r.frame) {
                match = j;
                break;
            }
        }
        if (match == added.size()) {
            kept_removed.push_back(r);
            continue;
        }
        claimed[match] = true;
        changed.push_back(make_change(r, added[match]));
    }

    std::vector<Entry> kept_added;
    kept_added.reserve(added.size());
    for (std::size_t j = 0; j < added.size(); ++j) {
        if (!claimed[j]) kept_added.push_back(added[j]);
    }

    removed.swap(kept_removed);
    added.swap(kept_added);
}

// ---------------------------------------------------------------------------
// filename match over the committed tree
// ---------------------------------------------------------------------------

// THE MATCH IS BY FILE NAME WITHIN `projects/`. One folder name is known
// (kProjectsPrefix, the repository's layout convention) and nothing below it is:
// the recheck looks for the committed file whose BASENAME is one of this
// source's three sidecars, wherever under that folder it sits. So a piece's
// directory may be renamed or nested with no era knowledge to keep current,
// while a sidecar name that happens to occur elsewhere in the tree — an
// unrelated copy, a pre-`projects/` era — can no longer make the match
// ambiguous or drag legacy commits into the walk.

// THE PIECE'S SIDECAR, ONE PREDICATE: a committed path UNDER `projects/`
// (at any depth, directly inside it included) whose BASENAME is
// `<base_name>.<one of the three extensions>`. It is the whole of "this piece's
// files" for every git read in the module — the walk, a commit's touched
// paths and its tree listing — so the three cannot come to disagree about what
// they are looking for. The folder test is a plain prefix compare and it is
// the ONLY geography here: everything past it is the basename rule, so a name
// is matched byte for byte whatever it holds (spaces, `*`, free UTF-8).
bool is_piece_sidecar_path(std::string_view path, const std::string& base_name) {
    if (path.size() <= kProjectsPrefix.size() ||
        path.substr(0, kProjectsPrefix.size()) != kProjectsPrefix) {
        return false;
    }
    const std::size_t      slash = path.rfind('/');
    const std::string_view leaf =
        (slash == std::string_view::npos) ? path : path.substr(slash + 1);
    for (const char* ext : kSidecarExtensions) {
        const std::string_view e(ext);
        if (leaf.size() == base_name.size() + e.size() &&
            leaf.substr(0, base_name.size()) == base_name &&
            leaf.substr(base_name.size()) == e) {
            return true;
        }
    }
    return false;
}

// The directory part of a committed path. Every path this module considers has
// passed the `projects/` prefix test, so there is always a separator and the
// answer is never empty.
std::string directory_of(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return (slash == std::string::npos) ? std::string()
                                        : path.substr(0, slash);
}

// THE PIECE'S DIRECTORY: THE SOURCE'S OWN PARENT FOLDER, repo-relative, or empty
// when that folder is not under the clone's `projects/`. It is the WHOLE
// resolution (architect 2026-08-09) and the architect's own workflow read back:
// he makes `projects/550 - 4/` in a file manager and keeps the source WAV
// inside it, so the folder holding the piece IS the piece's folder and there is
// nothing else to work out. Empty is the mode's one source-side refusal, and its
// fix is a file move rather than anything in a terminal.
//
// CONTAINMENT IS DECIDED ON CANONICAL PATHS, never on the spellings: a source
// reached through a symlinked corpus, or named with `..` in the middle, is the
// same file wherever it was typed from, and a naive prefix test on the strings
// would answer no for the first and yes for a `projects/../../elsewhere` that
// leaves the clone entirely. `weakly_canonical` resolves both sides without
// requiring either to exist, and `relative` then does the containment and the
// repo-relative conversion in one step — a path outside the clone comes back
// leading with `..`, which fails the first-component test below like any other
// non-match.
//
// THE PARENT MUST BE STRICTLY BELOW `projects/`, not `projects/` itself: the
// layout convention is one folder per piece, and a source dropped loose in the
// corpus root has no folder of its own to be the answer. It refuses like any
// other source outside the tree, and the fix is the same one — put it in a
// folder.
std::string project_directory_of_source(const std::string& repo_root,
                                        const std::string& source_audio_path) {
    if (source_audio_path.empty() || repo_root.empty()) return std::string();
    std::error_code ec;
    const std::filesystem::path root =
        std::filesystem::weakly_canonical(std::filesystem::path(repo_root), ec);
    if (ec) return std::string();
    // THE SOURCE IS CANONICALIZED WHOLE AND ITS PARENT TAKEN AFTERWARDS, not the
    // other way about: a source named as a bare filename has no parent to
    // canonicalize, and canonicalizing it first is what makes a program launched
    // from inside the project folder answer that folder rather than falling
    // through to a synthesized one beside it.
    const std::filesystem::path source = std::filesystem::weakly_canonical(
        std::filesystem::path(source_audio_path), ec);
    if (ec) return std::string();
    const std::filesystem::path rel =
        std::filesystem::relative(source.parent_path(), root, ec);
    if (ec || rel.empty()) return std::string();

    const std::string_view folder =
        kProjectsPrefix.substr(0, kProjectsPrefix.size() - 1);
    std::filesystem::path::iterator it  = rel.begin();
    std::filesystem::path::iterator end = rel.end();
    if (it == end || std::string_view(it->native()) != folder) {
        return std::string();
    }
    if (++it == end) return std::string();
    // Forward slashes deliberately: this is a repo-relative path in git's own
    // spelling, the exact form directory_of hands back for a committed match, so
    // both arms feed checkpoint_paths the same shape.
    return rel.generic_string();
}

// ---------------------------------------------------------------------------
// the projects-home guard
// ---------------------------------------------------------------------------

// Reduce a repository spelling to bare host/path so the settings key and the
// clone's own remote can be compared as the same thing:
//
//   git@github.com:warptempo/warptempo_projects.git  ->  github.com/warptempo/warptempo_projects
//   https://github.com/warptempo/warptempo_projects  ->  github.com/warptempo/warptempo_projects
//   ssh://git@github.com/warptempo/x.git/            ->  github.com/warptempo/x
//   ssh://git@ssh.github.com:443/warptempo/x.git     ->  github.com/warptempo/x
//
// A scheme goes, userinfo goes, an scp-style host:path colon becomes the path
// separator it means, and a trailing `.git` and any trailing slashes go. In the
// URL FORM (a scheme present) a colon before the first slash is a PORT and goes
// too: the port is how the transport reaches the host, not which repository it
// names. And GitHub's SSH-over-443 host is GitHub (architect 2026-09-27: both
// clones' origin is `ssh://git@ssh.github.com:443/...`, for networks that
// block port 22): `ssh.github.com` serves the same repositories under the same
// names, so it reduces to `github.com` — the one host alias, kGitHubSshHost.
constexpr std::string_view kGitHubSshHost = "ssh.github.com";

std::string normalize_repo_url(const std::string& raw) {
    std::string s = trim_trailing_ws(raw);

    const std::size_t scheme   = s.find("://");
    const bool        url_form = scheme != std::string::npos;
    if (url_form) s = s.substr(scheme + 3);

    const std::size_t first_slash = s.find('/');
    const std::size_t at          = s.find('@');
    if (at != std::string::npos &&
        (first_slash == std::string::npos || at < first_slash)) {
        s = s.substr(at + 1);
    }

    // A colon before the first slash: the URL form's `host:port`, whose port
    // goes, or the scp-style `host:path`, whose colon is the path separator.
    const std::size_t colon = s.find(':');
    if (colon != std::string::npos) {
        const std::size_t slash = s.find('/');
        if (slash == std::string::npos || colon < slash) {
            if (url_form) {
                s.erase(colon, slash == std::string::npos ? std::string::npos
                                                          : slash - colon);
            } else {
                s[colon] = '/';
            }
        }
    }

    const std::size_t host_end = s.find('/');
    if (std::string_view(s).substr(0, host_end) == kGitHubSshHost) {
        s.replace(0, kGitHubSshHost.size(), "github.com");
    }

    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".git") == 0) {
        s.erase(s.size() - 4);
    }
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

// HOW THE CLONE IS NAMED ON A CARD: by its folder name — the one part of its
// canonical absolute path that is not the machine's layout, which the
// basename rule keeps off a card (messaging.md; the full path is the
// diagnostic clause's). A root with no leaf (a filesystem root, unreachable
// from any real clone) falls back to its own spelling rather than to nothing.
std::string clone_name(const std::string& repo_root) {
    const std::string leaf =
        std::filesystem::path(repo_root).filename().string();
    return leaf.empty() ? repo_root : leaf;
}

// IS THIS CLONE THE CONFIGURED PROJECTS HOME — asked of the clone's FETCH url
// and of EVERY EFFECTIVE PUSH url, both normalized against the setting. False
// with `reason` set names the first disagreement in the user's own spellings.
//
// The fetch url (`remote.origin.url`) is not where a push has to go:
// `remote.origin.pushurl` overrides it and may be set more than once, so a
// clone whose fetch url is the configured projects home can still publish to a
// fork, a mirror or an unrelated repository. So both are asked, and every url
// that comes back must normalize equal to the setting. A clone with no pushurl
// configured answers the push question with its fetch url
// (GuiGitRepo::origin_push_urls), which makes the fetch-only check a strict
// subset of this one rather than a case beside it. BOTH ARE READ RAW FROM THE
// CONFIGURATION, never through an `insteadOf` rewrite.
//
// TWO CALLERS, TWO DIFFERENT QUESTIONS, which is why this is a function rather
// than a step of init(). init() asks it as THE MODE'S GATE — may this session
// read and offer to write this history at all — and the commit act asks it again
// IMMEDIATELY BEFORE THE PUSH, at the MUTATING BOUNDARY: the gate's answer is
// minutes old by then, and `remote.origin.pushurl` is a config value any
// terminal can change while the mode stands.
//
// AND THE SECOND ASKING TAKES ITS ANSWER WITH IT. Asking again close to the push
// only NARROWS the window; what closes it is that the URL validated here is the
// URL the push consumes, so `destination` hands the winning spelling back and
// the push sets it on its own remote instance rather than re-resolving the
// mutable name `origin` (GuiGitRepo::push_branch). `destination` is the FIRST
// effective push URL — the head of the very list validated just above, so every
// candidate destination had to normalize equal to the setting before any one of
// them could be named. An empty list has nothing to pin and is refused here
// rather than left to the push. The pinned URL is used verbatim, so a
// `url.<base>.insteadOf` / `pushInsteadOf` rule cannot move the push either:
// what this guard blesses is where the push goes. `fetch_source` is the same
// idea for a FETCH (the GitHub check's and the act's own): the fetch url just
// validated, which the fetch sets on its own remote instance.
//
// ITS REASONS ARE LOWERCASE, like every other reason in this file: both of
// its consumers APPEND (the mode's entry composes "History is unavailable: " and
// the push composes stderr's "Push refused: "), and an appended reason does not
// start a second sentence — the rule is stated once in messaging.md's card
// section, over the product's one statement of the text rules at
// paint_handler.cpp's menu-row block.
// AND EACH IS TWO CLAUSES (GuiFailure, failure.h): the clone's FULL path on
// the diagnostic for stderr, its folder name on the display for the card
// (clone_name above), the setting's own spelling and the remote's URL on
// both — those are not paths on this disk.
bool clone_is_projects_home(const GuiGitRepo&  repo,
                            const std::string& repo_root,
                            const std::string& projects_repo,
                            GuiFailure&        reason,
                            std::string*       destination = nullptr,
                            std::string*       fetch_source = nullptr) {
    reason = GuiFailure{};
    if (destination != nullptr) destination->clear();
    if (fetch_source != nullptr) fetch_source->clear();
    const std::string setting_norm = normalize_repo_url(projects_repo);
    if (setting_norm.empty()) {
        reason = plain_failure("the projects_repo setting is empty");
        return false;
    }
    const std::filesystem::path root(repo_root);
    const std::string shown_root = clone_name(repo_root);

    std::string remote_raw;
    if (!repo.origin_fetch_url(remote_raw) || remote_raw.empty()) {
        reason = path_failure("the clone at ", root, shown_root,
                              " has no 'origin' remote");
        return false;
    }
    if (normalize_repo_url(remote_raw) != setting_norm) {
        reason = path_failure("the projects_repo setting names '" +
                                  projects_repo + "' but the clone at ",
                              root, shown_root,
                              " has origin '" + trim_trailing_ws(remote_raw) +
                                  "'");
        return false;
    }

    std::vector<std::string> push_urls;
    if (!repo.origin_push_urls(push_urls)) {
        reason = path_failure("the clone at ", root, shown_root,
                              " states no push URL for 'origin'");
        return false;
    }
    std::string first_push_url;
    for (const std::string& raw : push_urls) {
        const std::string one = trim_trailing_ws(raw);
        if (one.empty()) continue;
        if (normalize_repo_url(one) != setting_norm) {
            reason = path_failure("the projects_repo setting names '" +
                                      projects_repo + "' but the clone at ",
                                  root, shown_root,
                                  " pushes 'origin' to '" + one + "'");
            return false;
        }
        if (first_push_url.empty()) first_push_url = one;
    }
    if (first_push_url.empty()) {
        reason = path_failure("the clone at ", root, shown_root,
                              " states no usable push URL for 'origin'");
        return false;
    }
    if (destination != nullptr) *destination = first_push_url;
    if (fetch_source != nullptr) *fetch_source = trim_trailing_ws(remote_raw);
    return true;
}

// ---------------------------------------------------------------------------
// per-commit snapshot
// ---------------------------------------------------------------------------

// One commit's committed path for each of the three sidecars, empty where that
// commit carries none. Indexed to match kSidecarExtensions.
//
// `ambiguous` is a REFUSAL, not a variant of "carries none": the commit CHANGED
// this base name in two or more directories, so which piece the blobs belong to
// has no answer (2026-08-09 — no checkpoint this program makes touches two
// folders, so it is somebody's hand commit). `no_touch_evidence` below is its
// sibling and the opposite fact — an answer about the commit versus the absence
// of one. All three paths are empty in either state and read_commit_sidecars
// refuses on both — which the walk's load gate counts as ineligibility (neither
// kind ever enters the walk) and the `'` act prints as its own refusal.
struct GuiHistoryCommitPaths {
    std::string path[kSidecarCount];
    bool        ambiguous = false;
    // The commit named NO directory it touched for this base name. Distinct
    // from `ambiguous` because it is a different fact and deserves a different
    // line: ambiguity is an answer about the commit, this is the absence of one.
    // It is ONE flag for all of its producers (touched_directories_of_commit
    // names them), which are indistinguishable by design and must not be
    // guessed between.
    bool        no_touch_evidence = false;
};

// WHICH DIRECTORIES THIS COMMIT ACTUALLY TOUCHED for the base name — the
// evidence the per-commit path resolution below is built on (2026-08-09).
//
// THE TREE ALONE CANNOT ANSWER "WHICH FOLDER IS THIS COMMIT ABOUT", and that is
// the whole reason this exists: a commit CONTAINS every folder the piece has
// ever lived in, because the checkpoint act commits only its three paths and
// never deletes the folder a piece moved out of. So a commit made from a BACKUP
// copy still carries the original's untouched blobs, and a commit made after a
// rename still carries the pre-rename folder's. Choosing by containment showed
// the wrong folder's state for the first and refused the second as ambiguous;
// choosing by what the commit CHANGED answers both, because a checkpoint changes
// exactly the folder it was made from.
//
// THE QUESTION IS ASKED OF THIS ONE COMMIT (GuiGitRepo::changed_paths: its diff
// against its parent, a root commit against the empty tree), never of a walk
// that could move on to an ancestor. RENAME DETECTION IS NEVER ASKED FOR, so the
// answer does not depend on the clone's `diff.renames`: a `git mv` of a project
// folder done by hand in a terminal answers BOTH the old and the new directory
// and takes the ambiguity arm — unsanctioned, blunt, correct — while the
// sanctioned path costs nothing: a folder renamed in a FILE MANAGER makes no
// commit at all, and the act's next checkpoint commits the three NEW paths and
// deletes nothing, so it answers the new directory alone (the old folder stays
// in the tree, which keeps the pre-rename era's own commits resolvable). A
// DELETION-only commit answers the folder it emptied; a MERGE answers the paths
// that differ from every parent.
//
// AN EMPTY ANSWER IS NOT AN ANSWER — the module's own "no answer", and the
// caller treats it as one. TWO PRODUCERS, INDISTINGUISHABLE BY DESIGN: a commit
// that touched none of these paths (a merge whose differences from its parents
// never coincide on one path is one of them), and a read that failed. Nothing
// downstream can tell them apart and nothing should try: each means this
// program cannot say which folder the commit is about.
//
// A PATH DIRECTLY INSIDE `projects/` refuses the whole answer: the layout is
// one folder per piece, and a sidecar loose in the corpus root belongs to no
// piece's folder (the walk still sees its commit, and the load gate hides it).
//
// IT COSTS ONE DIFF PER CANDIDATE in the prefetch scan, beside the tree listing
// and the three blob reads the load gate already runs. That is the deliberate
// price of ONE resolution owner: the walk and the `'` act reach this through the
// same call, so a member can never display one folder's snapshot and load
// another's.
struct GuiHistoryTouchedDirs {
    // The read ran and named at least one directory, every one strictly below
    // `projects/`. False covers a read that failed, one that named nothing, and
    // one that named a loose sidecar — deliberately together.
    bool                     ok = false;
    // One entry per directory named, in first-seen order. Its SIZE is the whole
    // decision at the caller: one is the answer, more is ambiguity.
    std::vector<std::string> dirs;
};

GuiHistoryTouchedDirs touched_directories_of_commit(const GuiGitRepo&  repo,
                                                    const std::string& sha,
                                                    const std::string& base_name) {
    GuiHistoryTouchedDirs    out;
    std::vector<std::string> paths;
    const auto accept = [&base_name](std::string_view p) {
        return is_piece_sidecar_path(p, base_name);
    };
    if (!repo.changed_paths(sha, accept, paths)) return out;
    for (const std::string& path : paths) {
        const std::string dir = directory_of(path);
        if (dir.size() <= kProjectsPrefix.size()) return GuiHistoryTouchedDirs{};
        if (std::find(out.dirs.begin(), out.dirs.end(), dir) == out.dirs.end()) {
            out.dirs.push_back(dir);
        }
    }
    out.ok = !out.dirs.empty();
    return out;
}

// THE RULE IS THE DIRECTORY THIS COMMIT TOUCHED, AND THERE IS NO OTHER RULE
// (2026-08-09). It superseded "the session's own directory first", which
// preferred the session's folder whenever the commit's TREE carried the base
// name there — which it almost always does, the act never deleting a folder the
// piece has moved out of, so containment answered about folders the commit
// never changed: a checkpoint made from a BACKUP copy displayed the ORIGINAL
// folder's unchanged blobs as its own state, and after a rename the pre-rename
// era's commits carried two candidates and hid as ambiguous. A containment
// FALLBACK for "no touch evidence" went the same day, having laundered silence
// into success: a commit that touched only unrelated files, or a read that
// failed, fell back, found the session's folder in the tree and loaded a
// snapshot the commit is not about.
//
// SO THE EVIDENCE RULES ARE EXHAUSTIVE AND THERE ARE THREE:
//   ONE directory  — that is the answer.
//   TWO OR MORE    — genuinely ambiguous, and no checkpoint this program makes
//                    touches two folders, so it is somebody's hand commit; the
//                    walk hides it on the counted line's terms and `'` refuses.
//   NONE           — NOT AN ANSWER, whatever produced it (the reader's comment
//                    owns the producers). Same hide, same refusal, one message.
//
// The chosen folder's blobs are then read from THIS COMMIT'S OWN TREE, which is
// what replaces knowing the era's directory name: one tree listing per commit,
// all three extensions picked out of it.
GuiHistoryCommitPaths resolve_commit_paths(const GuiGitRepo&  repo,
                                           const std::string& sha,
                                           const std::string& base_name) {
    GuiHistoryCommitPaths out;
    const GuiHistoryTouchedDirs touched =
        touched_directories_of_commit(repo, sha, base_name);
    if (!touched.ok) {
        out.no_touch_evidence = true;
        return out;
    }
    if (touched.dirs.size() > 1) {
        out.ambiguous = true;
        return out;
    }
    // The touched folder must be IN the tree to be read from — it always is for
    // a commit that added or changed files there, and a commit whose only touch
    // was a DELETION leaves nothing to load, which the empty `path` entries
    // below report as the missing sidecar it is (as does a listing that could
    // not be read).
    const std::string& chosen = touched.dirs.front();

    std::vector<std::string> listed;
    if (!repo.tree_paths(sha,
                         [&base_name](std::string_view p) {
                             return is_piece_sidecar_path(p, base_name);
                         },
                         listed)) {
        return out;
    }
    for (const std::string& path : listed) {
        if (directory_of(path) != chosen) continue;
        for (std::size_t e = 0; e < kSidecarCount; ++e) {
            if (path == chosen + "/" + base_name + kSidecarExtensions[e]) {
                out.path[e] = path;
            }
        }
    }
    return out;
}

// READ ONE COMMIT'S SIDECARS through an open handle — read_commit_sidecars'
// body (the contract is at its declaration), shared with the scan, which holds
// one handle for its whole run. `repo_root` names the clone on a card.
bool read_commit_sidecars_in(const GuiGitRepo&         repo,
                             const std::string&        repo_root,
                             const std::string&        spelling,
                             const std::string&        base_name,
                             GuiHistoryCommitSidecars& out,
                             GuiFailure&               failure) {
    out     = GuiHistoryCommitSidecars{};
    failure = GuiFailure{};
    // EVERY REASON HERE IS TWO CLAUSES (GuiFailure, failure.h — 2026-09-02):
    // the committed sidecar paths are REPO-RELATIVE (`projects/<piece>/x.
    // settings` is the blob's whole name, there is no fuller spelling of it)
    // and read alike on both surfaces; the one path on this disk, the clone
    // root, is named in full on the diagnostic and by its folder name on the
    // display.
    auto refuse = [&failure](std::string words) {
        failure = plain_failure(std::move(words));
        return false;
    };
    if (spelling.empty())  return refuse("no commit was named");
    if (base_name.empty()) return refuse("the source has no sidecar base name");

    // THE NAME MUST BE A COMMIT IN THIS CLONE: a full object name the walk
    // handed out, which a history rewritten since (a gc, a force-push fetched
    // in the terminal) can have taken away.
    if (!repo.is_commit(spelling)) {
        failure = path_failure("'" + spelling + "' does not name a commit in ",
                               std::filesystem::path(repo_root),
                               clone_name(repo_root), "");
        return false;
    }
    const std::string& sha = spelling;
    out.sha = sha;

    // That commit's OWN tree decides where the sidecars sit — the same
    // basename match the walk uses, applied to an arbitrary commit.
    const GuiHistoryCommitPaths paths =
        resolve_commit_paths(repo, sha, base_name);
    if (paths.no_touch_evidence) {
        return refuse("commit " + short_sha(sha) +
                      " does not touch this piece's sidecars");
    }
    if (paths.ambiguous) {
        return refuse("commit " + short_sha(sha) + " changed '" + base_name +
                      ".*' in more than one directory, so which piece it "
                      "names has no answer");
    }
    // kSidecarExtensions order, through its named indices (sidecar_set.h):
    // the three named slots below and the blob array after them are that list
    // spelled out, so its size is pinned here.
    static_assert(kSidecarCount == 3);
    out.warpmarkers.path               = paths.path[kSidecarWarp];
    out.phaseresetmarkers.path         = paths.path[kSidecarPhaseReset];
    out.settings.path                  = paths.path[kSidecarSettings];

    // THE BYTES, read whole from the object database. A read that fails is a
    // refusal, never an empty file: an empty sidecar is a VALID whole file in
    // both marker grammars, so an invented emptiness would pass every strict
    // loader and replace the live store with a state the commit never held. A
    // commit the tree says carries nothing reaches the caller's own
    // partial-commit refusal.
    GuiHistorySidecarBlob* blobs[kSidecarCount] = {
        &out.warpmarkers, &out.phaseresetmarkers, &out.settings};
    for (std::size_t e = 0; e < kSidecarCount; ++e) {
        if (paths.path[e].empty()) continue;
        if (!repo.read_blob(sha, paths.path[e], blobs[e]->text)) {
            return refuse("could not read '" + paths.path[e] +
                          "' at commit " + short_sha(sha));
        }
    }
    return true;
}

// OPEN THE CLONE FOR ONE CALL — the public entry points' shared first step. An
// empty root would name no clone at all, so it refuses with the other missing
// inputs rather than being opened.
std::optional<GuiGitRepo> open_clone_for(const std::string& repo_root,
                                         GuiFailure&        failure) {
    if (repo_root.empty()) {
        failure = plain_failure("the source's clone is not known");
        return std::nullopt;
    }
    std::string diag;
    std::optional<GuiGitRepo> repo = GuiGitRepo::open(repo_root, diag);
    if (!repo) {
        failure = path_failure("could not open the clone at ",
                               std::filesystem::path(repo_root),
                               clone_name(repo_root), "");
        failure.diagnostic += " (" + diag + ")";
    }
    return repo;
}

}  // namespace

// The seven-character spelling every user-facing line uses for a commit —
// the contract is at the declaration.
std::string short_sha(const std::string& sha) {
    return (sha.size() >= 7) ? sha.substr(0, 7) : sha;
}

bool read_commit_sidecars(const std::string&        repo_root,
                          const std::string&        spelling,
                          const std::string&        base_name,
                          GuiHistoryCommitSidecars& out,
                          GuiFailure&               failure) {
    out     = GuiHistoryCommitSidecars{};
    failure = GuiFailure{};
    const std::optional<GuiGitRepo> repo = open_clone_for(repo_root, failure);
    if (!repo) return false;
    return read_commit_sidecars_in(*repo, repo_root, spelling, base_name, out,
                                   failure);
}

namespace {

// Removes its directory tree when it falls out of scope, on EVERY exit — the
// refusals, the success, and a throw the allocations below could raise. Its
// one user is the strict whole-set load's scratch staging
// (load_commit_sidecars_strict), where the same guarantee written by hand
// would be one `remove_all` per refusal arm and a leaked directory the first
// time an arm was added without one.
struct ScratchDirGuard {
    std::filesystem::path dir;
    explicit ScratchDirGuard(std::filesystem::path d) : dir(std::move(d)) {}
    ScratchDirGuard(const ScratchDirGuard&)            = delete;
    ScratchDirGuard& operator=(const ScratchDirGuard&) = delete;
    ~ScratchDirGuard() {
        if (dir.empty()) return;
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

// The gate's contract and the reason it is ONE predicate live at the header
// declaration. The body is the `'` act's own validation sequence, moved here
// whole when the walk became load-gated (2026-08-04) so both askers run the
// same bytes; it takes an OPEN HANDLE so the scan's one handle serves every
// candidate, and load_commit_sidecars_strict below is its one-call opener.
bool load_commit_sidecars_strict_in(const GuiGitRepo&     repo,
                                    const std::string&    repo_root,
                                    const std::string&    spelling,
                                    const std::string&    base_name,
                                    GuiHistoryCommitLoad& out,
                                    GuiFailure&           failure) {
    out     = GuiHistoryCommitLoad{};
    failure = GuiFailure{};
    // The two-clause shape is read_commit_sidecars's above; the one path on
    // this disk that any arm below names is the scratch folder, full on the
    // diagnostic and by its leaf on the display.
    auto refuse = [&failure](std::string words) {
        failure = plain_failure(std::move(words));
        return false;
    };

    if (!read_commit_sidecars_in(repo, repo_root, spelling, base_name,
                                 out.sidecars, failure)) {
        return false;
    }
    const GuiHistoryCommitSidecars& snap = out.sidecars;

    // A PARTIAL COMMIT IS A REFUSAL: a load-in-place is a whole-state replace,
    // and inheriting some files from the commit and the rest from nowhere
    // would compose a state no checkpoint ever was. (For the walk the same
    // refusal is simple ineligibility: a checkpoint that cannot be loaded is
    // not stepped to.) A commit that also carries a
    // `.magnificationlevelmarkers` is not partial: that name is no member of
    // the set, so it was never resolved into `snap` and nothing here asks it.
    auto missing = [&](const char* ext) {
        return refuse("commit " + snap.sha + " carries no '" + base_name +
                      ext + "'");
    };
    if (snap.warpmarkers.path.empty())
        return missing(kSidecarExtensions[kSidecarWarp]);
    if (snap.phaseresetmarkers.path.empty())
        return missing(kSidecarExtensions[kSidecarPhaseReset]);
    if (snap.settings.path.empty())
        return missing(kSidecarExtensions[kSidecarSettings]);

    // THE COMMITTED BYTES REACH THE LOADERS THROUGH A SCRATCH DIRECTORY,
    // because all three whole-file entry points take a PATH and open the file
    // themselves (read_settings_file, GuiWarpMarkers::load,
    // GuiPhaseResetMarkers::load) and all three parse through the FROZEN
    // parser, so
    // there is no string-shaped entry to hand a blob to. The alternative — a
    // GUI-side scanner over the strings — would be a SECOND GRAMMAR beside the
    // strict one, which is precisely what this gate exists to avoid; staging
    // the bytes is the cheap way to keep the loaders themselves as the only
    // judges.
    //
    // THE DIRECTORY IS THE CALL'S OWN SCRATCH: the system temp dir, one
    // per-process per-CALL subdirectory, removed on every exit by the guard.
    // NEVER the repository (the walk and the `'` act only ever read it) and
    // NEVER beside the source (the working sidecars are the user's, and a read
    // must not write near them).
    //
    // THE SERIAL IS WHAT MAKES IT PER-CALL RATHER THAN PER-COMMIT (2026-08-07,
    // with the prefetch worker): pid + short sha collided as soon as two THREADS
    // could ask about one commit at the same time — the worker gating a
    // candidate while the main thread runs the `'` act on that same SHA — and
    // the loser's guard would remove the winner's staged files mid-load. The
    // counter is process-wide and atomic, so no two calls anywhere can name one
    // directory.
    static std::atomic<unsigned long long> scratch_serial{0};
    std::error_code   ec;
    const std::string leaf = "warptempo_gui-load-in-place-" +
                             std::to_string(static_cast<long>(::getpid())) +
                             "-" + snap.sha.substr(0, 7) + "-" +
                             std::to_string(scratch_serial.fetch_add(1));
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path(ec) / leaf;
    if (ec) {
        return refuse("no temporary directory available: " + ec.message());
    }
    ScratchDirGuard guard(scratch);
    std::filesystem::create_directories(scratch, ec);
    if (ec) {
        failure = path_failure("could not create ", scratch,
                               scratch.filename().string(),
                               ": " + ec.message());
        return false;
    }

    // Staged under the sidecar's own leaf name, so the loaders see exactly the
    // filename shape they see beside a source. The reason on failure names the
    // COMMITTED path, never the scratch one: the scratch is an implementation
    // detail of this call and nothing the user can act on.
    auto stage = [&](const GuiHistorySidecarBlob& blob, const char* ext,
                     std::filesystem::path& out_path) {
        out_path = scratch / (base_name + ext);
        if (atomic_write_string_to_path(out_path.string(), blob.text)) {
            return true;
        }
        return refuse("could not stage '" + blob.path + "' from commit " +
                      snap.sha);
    };
    std::filesystem::path settings_file, warp_file, phase_reset_file;
    if (!stage(snap.settings, kSidecarExtensions[kSidecarSettings],
               settings_file))                                   return false;
    if (!stage(snap.warpmarkers, kSidecarExtensions[kSidecarWarp],
               warp_file))                                       return false;
    if (!stage(snap.phaseresetmarkers,
               kSidecarExtensions[kSidecarPhaseReset],
               phase_reset_file))                                return false;

    // The three STRICT WHOLE-FILE LOADERS are the judges, in the render-entry
    // load-in-place's own order, each refusal naming the committed path and
    // the SHA. First error only, by construction: every arm returns.
    //
    // THE NAME IN THESE SENTENCES IS THE COMMITTED SIDECAR, NEVER THE SCRATCH
    // FILE (the staging rationale above, and the four-tier review's R-11 rule
    // in failure.h). A loader's open or read refusal names the path it was
    // handed — here the per-call scratch copy, an implementation detail of
    // this call that exists for microseconds and that the user cannot act on
    // — so appending its composed sentence put that temp filename on the card.
    // The loaders publish those refusals' WORDS apart from the path
    // (`path_free_reason`, the granted frozen touch of 2026-09-02) and these
    // arms take the words alone, so the only file any of them names is the
    // committed one, repo-relative, on both clauses; a line-numbered parse
    // error carries no path and its whole sentence is the reason. Both
    // clauses stay the same words, which is why these are plain_failure: no
    // path on this disk reaches them.
    std::optional<std::string> load_reason;
    const auto load_words = [&load_reason](const std::string& composed) {
        return load_reason ? *load_reason : composed;
    };

    auto settings = read_settings_file(settings_file.string(), &load_reason);
    if (!settings) {
        return refuse("invalid settings in '" + snap.settings.path +
                      "' at commit " + snap.sha + ": " +
                      load_words(settings.error()));
    }
    out.settings = std::move(*settings);

    {
        GuiWarpMarkers m;
        auto r = m.load(warp_file.string(), &load_reason);
        if (!r) {
            return refuse("invalid warp markers in '" + snap.warpmarkers.path +
                          "' at commit " + snap.sha + ": " +
                          load_words(r.error()));
        }
        out.warp_markers = m.markers();
    }
    {
        GuiPhaseResetMarkers t;
        auto r = t.load(phase_reset_file.string(), &load_reason);
        if (!r) {
            return refuse("invalid phase reset markers in '" +
                          snap.phaseresetmarkers.path + "' at commit " +
                          snap.sha + ": " + load_words(r.error()));
        }
        out.phase_reset_markers = t.markers();
    }
    return true;
}

}  // namespace

bool load_commit_sidecars_strict(const std::string&    repo_root,
                                 const std::string&    spelling,
                                 const std::string&    base_name,
                                 GuiHistoryCommitLoad& out,
                                 GuiFailure&           failure) {
    out     = GuiHistoryCommitLoad{};
    failure = GuiFailure{};
    const std::optional<GuiGitRepo> repo = open_clone_for(repo_root, failure);
    if (!repo) return false;
    return load_commit_sidecars_strict_in(*repo, repo_root, spelling,
                                          base_name, out, failure);
}

// THE SETTINGS WRITER'S GUI HALF, HELD BY VALUE — the storable form of the
// call-shaped NonEngineSettingsSnapshot (which borrows a ViewState pair and a
// string). It is opaque in the header because ViewState is app_state.h's and
// this module is included BY that header; nothing outside this file needs its
// shape.
struct GuiHistoryGuiSide {
    ViewState   tab_a;
    ViewState   tab_b;
    char        active_audio_view   = 'S';
    char        active_markers_view = 'W';
    char        active_tab_view     = 'A';
};

std::shared_ptr<const GuiHistoryGuiSide> capture_history_gui_side(
        const AppState& app) {
    auto gui = std::make_shared<GuiHistoryGuiSide>();

    // A Ctrl+S runs refresh_active_tab_view_from_app before the writer reads
    // the bands, stashing the live viewport / zoom / playhead / trim into the
    // ACTIVE tab's band. Mirror that stash onto THESE copies — the same const
    // overlay the settings editor's autocomplete recall uses — so the bytes
    // match a save exactly while this read mutates nothing. read_only is not
    // mirrored: it lives in the band already, toggled by bare `o`.
    gui->tab_a = app.tab_a;
    gui->tab_b = app.tab_b;
    ViewState& eff_active =
        (app.active_tab_view == 'B') ? gui->tab_b : gui->tab_a;
    eff_active.viewport_start_sample  = app.viewport_start_sample;
    eff_active.zoom_level             = app.zoom_level;
    eff_active.playhead_cursor_sample = app.playhead_cursor_sample;
    eff_active.trim                   = app.trim;

    gui->active_audio_view   = app.active_audio_view;
    gui->active_markers_view = app.active_markers_view;
    gui->active_tab_view     = app.active_tab_view;
    return gui;
}

std::string format_history_settings_text(const GuiHistoryGuiSide& gui,
                                         const EngineSettings&    engine) {
    const NonEngineSettingsSnapshot snap{
        gui.tab_a, gui.tab_b,
        gui.active_audio_view, gui.active_markers_view, gui.active_tab_view};
    return format_settings_text(snap, engine);
}

GuiHistoryNowSide build_history_now_side(const AppState& app) {
    GuiHistoryNowSide out;
    out.warpmarkers_text = format_warpmarkers_text(app.warpmarkers.markers());
    out.phaseresetmarkers_text =
        format_phaseresetmarkers_text(app.phaseresetmarkers.markers());
    // THROUGH THE TWO OWNERS ABOVE, so the live state's bytes and every LOCAL
    // walk member's are spelled by one rule and can differ in nothing but the
    // engine block — which is the local delta's whole vocabulary anyway.
    out.settings_text = format_history_settings_text(
        *capture_history_gui_side(app), app.engine_settings);
    return out;
}

// THE CLONE THE SOURCE IS IN — the ONE derivation of the repository root
// (architect 2026-08-11; the contract is at the declaration).
//
// libgit2 searches upward from the source's parent folder for the repository
// that holds it (gui_git_discover_root — git's own discovery, stopping at a
// filesystem boundary), which is the clone whose `projects/` the piece must sit
// under and the clone a checkpoint commits into. The source is CANONICALIZED
// WHOLE FIRST for the same reason project_directory_of_source does it that way:
// a source named as a bare filename has no parent to search from, and
// canonicalizing against the working directory is what makes a program
// launched from inside the piece's folder answer that folder's clone.
//
// THE TWO REFUSALS ARE THE DISCOVERY'S TWO NOES. NotAClone — no repository
// above the folder, or one with no work tree — is the ruled NOT-A-CLONE, whose
// fix is a clone or a file move, not a `read_failed`. CouldNotAsk — a search
// libgit2 could not complete — and an answer that is not an existing directory
// are a READ THAT DID NOT ANSWER and say so through `read_failed`, which the
// scan turns into a not-ok run so an unread repository never passes for a read
// one. libgit2's own words ride the diagnostic clause alone.
//
// THE ANSWER IS CANONICALIZED before it leaves, which is what lets every
// consumer — the project-directory containment test above all — compare
// against it without asking again.
GuiHistoryRepoRoot resolve_repo_root_for_source(
        const std::string& source_audio_path) {
    GuiHistoryRepoRoot r;
    if (source_audio_path.empty()) {
        r.reason = plain_failure("no source is loaded");
        return r;
    }

    // Two clauses per refusal (GuiFailure, failure.h — 2026-09-02): the
    // source and its folder in full on the diagnostic, and on the display
    // the source by the project's folder-and-file form (shown_project_path)
    // and a folder by its name.
    std::error_code ec;
    const std::filesystem::path given(source_audio_path);
    const std::filesystem::path source =
        std::filesystem::weakly_canonical(given, ec);
    if (ec) {
        r.read_failed = true;
        r.reason = path_failure("could not resolve the source's own path ",
                                given, shown_project_path(given), "");
        return r;
    }
    const std::string dir = source.parent_path().string();
    if (dir.empty()) {
        r.reason = path_failure("the source is not in a directory: ", given,
                                shown_project_path(given), "");
        return r;
    }
    const std::filesystem::path dir_path(dir);
    const std::string dir_name = dir_path.filename().string();

    std::string toplevel;
    std::string diag;
    const GuiGitRoot found = gui_git_discover_root(dir, toplevel, diag);
    if (found == GuiGitRoot::CouldNotAsk) {
        r.read_failed = true;
        r.reason = path_failure("could not ask git which clone holds ",
                                dir_path, dir_name, "");
        r.reason.diagnostic += " (" + diag + ")";
        return r;
    }
    if (found == GuiGitRoot::NotAClone) {
        // The answer is no. NOT a `read_failed`: a project folder outside
        // every clone is a supported configuration (projects_path need not be
        // under the clone), and the mode's local fallback is what it gets.
        r.reason = path_failure(
            "the source's folder is not inside a git clone: ", dir_path,
            dir_name, "");
        return r;
    }

    const std::filesystem::path root =
        std::filesystem::weakly_canonical(std::filesystem::path(toplevel), ec);
    const bool canonicalized = !ec;
    ec.clear();
    if (!canonicalized || !std::filesystem::is_directory(root, ec) || ec) {
        r.read_failed = true;
        r.reason = two_path_failure(
            "git named ", std::filesystem::path(toplevel),
            clone_name(toplevel), " as the clone holding ", dir_path,
            dir_name, ", which is not a directory");
        return r;
    }

    r.ok   = true;
    r.path = root.string();
    return r;
}

// THE WALK'S CHEAP HALF (the contract is at the declaration). It answers WHICH
// CLONE and WHERE THE PIECE LIVES, or why neither can be found, from the
// clone's discovery and its configuration and no strict load, and it PRINTS
// NOTHING: the caller decides whether
// this is a refusal the user is watching for (GuiHistoryDiff::init's one stderr
// line) or a background run's own finding, which the store simply keeps until an
// entry asks.
GuiHistoryWalkHeader resolve_history_walk_header(
        const std::string& source_audio_path,
        const std::string& projects_repo) {
    GuiHistoryWalkHeader h;

    // Every failure arm lands here: the reason, and the whole header left in
    // its documented empty shape whatever step got as far as filling in (the
    // folder is resolved after the base name is derived, so a late refusal has
    // something to clear).
    //
    // EVERY REASON IN THIS FILE IS LOWERCASE, this walk's, the scan's and
    // resolve_repo_root_for_source's alike: each is consumed ONLY appended
    // ("History is unavailable: <reason>", the one entry owner's card and its
    // stderr twin), and an appended reason does not start a second sentence
    // (messaging.md's card section states the rule). Eleven of them were
    // capitalized until 2026-09-01, when the one that already agreed —
    // `git named '…' as the clone holding '…', which is not a directory` —
    // turned out to be the sibling in the right, and the family moved to it.
    auto unavailable = [&h](GuiFailure why, bool read_failed = false) {
        h.ok = false;
        h.read_failed = read_failed;
        h.unavailable_reason = std::move(why);
        h.repo_root.clear();
        h.base_name.clear();
        h.project_directory.clear();
        return h;
    };

    // THE SIDECAR BASE NAME IS THE SOURCE'S OWN STEM — the one derivation
    // rule the loader uses when it builds <base>.warpmarkers and its two
    // siblings beside the WAV (file_loader.cpp's companion-file block). The
    // corpus names its files by exactly that, so mirroring the rule is what
    // makes the filename match work on names full of periods and commas.
    //
    // THE EMPTY SOURCE REFUSES FIRST, through the clone derivation below, in
    // resolve_repo_root_for_source's own words (`no source is loaded`).
    // THE CLONE FIRST, because every question below it is asked of a repository
    // and there is no repository until this answers (architect 2026-08-11,
    // replacing the compiled-in path's is_directory probe: the root is derived
    // from the loaded source now, so "which clone" is a real question with two
    // real refusals rather than a check on a constant).
    const GuiHistoryRepoRoot root =
        resolve_repo_root_for_source(source_audio_path);
    if (!root.ok) return unavailable(root.reason, root.read_failed);
    h.repo_root = root.path;
    // A clone found but not opened is a read that did not answer, like a
    // derivation that could not ask.
    GuiFailure                      open_reason;
    const std::optional<GuiGitRepo> repo =
        open_clone_for(h.repo_root, open_reason);
    if (!repo) return unavailable(std::move(open_reason), true);

    // THE PROJECTS-HOME GUARD, straight after the clone because it is a
    // precondition on the whole feature rather than a property of one source. The `projects_repo`
    // setting names WHICH repository is the projects home; the clone just derived
    // from the source is only the transport that happens to be on this disk. If
    // the setting has been rebound to another repository, this clone's history is
    // the wrong history, and reading it anyway would answer confidently about
    // the wrong piece of work. Both spellings are normalized to bare host/path
    // first, so a scheme, an scp-style remote or a trailing `.git` never makes
    // a false mismatch.
    //
    // IT ASKS WHICH REPOSITORY, NEVER HOW FRESH: the question is answered from
    // the remote's URL and no ref at all, which is why moving the walk off
    // `origin/main` and onto the local branch (HEAD) left this untouched.
    // A clone that has not fetched in a year is still THIS repository, and a
    // checkpoint this product commits and fails to push is still its history.
    //
    // IT VALIDATES EVERY EFFECTIVE PUSH DESTINATION, not the fetch URL alone —
    // through clone_is_projects_home, whose comment owns the whole rule and
    // which the COMMIT ACT asks again immediately before its push. THIS SITE IS
    // THE MODE'S GATE; that one is the mutating boundary. Two askings because
    // the config can move between them, one owner because the question is one.
    GuiFailure guard_reason;
    if (!clone_is_projects_home(*repo, h.repo_root, projects_repo,
                                guard_reason)) {
        return unavailable(std::move(guard_reason));
    }

    h.base_name = std::filesystem::path(source_audio_path).stem().string();
    if (h.base_name.empty()) {
        const std::filesystem::path given(source_audio_path);
        return unavailable(path_failure("the source path has no base name: ",
                                        given, shown_project_path(given), ""));
    }

    // THE SOURCE'S FOLDER IS THE PROJECT DIRECTORY, AND THAT IS THE WHOLE RULE
    // (architect 2026-08-09). A piece lives in its own folder under the clone's
    // `projects/` with its source inside it, so the folder holding the source is
    // the folder the checkpoints belong in — there is nothing to match, nothing
    // to synthesize and nothing to disambiguate. It replaced a three-arm
    // precedence (a committed tip-tree match, then this, then a synthesized
    // `projects/<base name>`) the same day it was written: the law says in one
    // sentence what the arms said in three, and the arms' apparatus went with
    // them — the tip-tree listing, the sole-directory judgment and its ambiguity
    // refusal, and the act's directory creation, the remaining folder existing
    // by construction because the source is in it.
    //
    // A SOURCE OUTSIDE THAT TREE REFUSES THE VIEW, and the message names the fix
    // because the fix is a file move: this is the corpus's own layout, not a
    // repository operation, and nothing about it belongs in a terminal. LOADING
    // A SOURCE IS UNAFFECTED — this refusal is the history view's alone, and any
    // file anywhere still opens, edits, renders and saves.
    //
    // CONTINUITY RIDES THE BASENAME, NOT THE FOLDER, which is what the deleted
    // committed-match arm used to be the answer to: the walk keeps every commit
    // that touched a file by that name ANYWHERE under `projects/`
    // (is_piece_sidecar_path), so a piece's history follows it, and a folder
    // renamed, re-nested or created fresh today still walks back through every
    // checkpoint the piece ever had. The folder decides where the NEXT
    // checkpoint is written; the name decides what the walk can see.
    //
    // THE ACCEPTED TRADE, architect-ruled: nothing refuses a checkpoint made
    // from a COPY of a piece — a backup folder holding the same source name —
    // and its commits land in that folder and interleave, by basename, into the
    // one walk. It is visible in the view, undoable, and the user's own act;
    // guarding it would be defensive code against a practice the corpus does not
    // have, which the sanctioned-use model rules out.
    h.project_directory =
        project_directory_of_source(h.repo_root, source_audio_path);
    if (h.project_directory.empty()) {
        const std::filesystem::path given(source_audio_path);
        return unavailable(path_failure(
            "the source is not in a folder under 'projects/': ", given,
            shown_project_path(given), ""));
    }

    h.ok = true;
    return h;
}

std::string read_history_walk_tip(const std::string& source_audio_path) {
    // IT DERIVES THE ROOT ITSELF, both its callers asking before any header
    // exists (the declaration owns why). A derivation that refuses, a clone
    // that will not open and an unborn branch all answer the same empty string
    // an unreadable tip does, which is what every caller already handles.
    const GuiHistoryRepoRoot root =
        resolve_repo_root_for_source(source_audio_path);
    if (!root.ok) return std::string();
    std::string                     diag;
    const std::optional<GuiGitRepo> repo = GuiGitRepo::open(root.path, diag);
    if (!repo) return std::string();
    return repo->head_commit();
}

void scan_history_walk(
        const std::string& source_audio_path, const std::string& projects_repo,
        const std::function<bool()>&                           abandoned,
        const std::function<void(GuiHistoryWalkHeader)>&       on_header,
        const std::function<void(GuiHistoryCommitSidecars)>&   on_member,
        const std::function<void(GuiHistoryScanResult)>&       on_done) {
    GuiHistoryWalkHeader header =
        resolve_history_walk_header(source_audio_path, projects_repo);
    const std::string repo_root   = header.repo_root;
    const std::string base_name   = header.base_name;
    const bool        ok          = header.ok;
    const bool        read_failed = header.read_failed;
    const GuiFailure  header_why  = header.unavailable_reason;
    // The header's project_directory is deliberately not copied here: the scan
    // needs the base NAME (the walk's predicate and the load gate) and nothing
    // about where the piece currently lives, each candidate's own touched directory
    // being what resolves its blobs since 2026-08-09. The header still carries
    // it for the checkpoint act, which writes there.
    on_header(std::move(header));
    if (!ok) {
        // A run whose header refuses is FINISHED, not merely stopped: the DONE
        // is what tells the store there is nothing more coming. It ends OK —
        // the run did what it could and the header carries the refusal, which is
        // what init reads; `ok` false is reserved for a read that did not answer
        // (the type's own comment owns the distinction).
        //
        // AND THE ROOT DERIVATION IS EXACTLY SUCH A READ when it could not ask
        // git at all (2026-08-11): the header's `read_failed` carries that one
        // case through to here, so a repository this program never managed to
        // question ends the run NOT ok and can never establish an empty walk.
        // The reason is the header's own, so init — which reads the header
        // first — still prints one line either way.
        GuiHistoryScanResult result;
        if (read_failed) {
            result.ok                = false;
            result.unavailable_reason = header_why;
        }
        on_done(std::move(result));
        return;
    }

    // THE COMMIT WALK IS ERA-AGNOSTIC BELOW `projects/`: it keeps every commit
    // reachable from HEAD that changed a path is_piece_sidecar_path takes — the
    // basename at any depth under that folder, directly inside it included — so
    // a commit that renamed or re-nested the piece's directory is followed with
    // no knowledge of what it used to be called, which is the whole point of
    // matching by name; what the folder term adds is that a same-named file
    // OUTSIDE the corpus cannot pull commits into the walk that carry no
    // checkpoint of this piece at all. The per-commit touched-directory read
    // asks the same predicate, so the walk and the resolution can never
    // disagree about what "this piece's files" means.
    //
    // AND IT IS UNCAPPED (2026-08-07): the walk reaches the piece's first
    // checkpoint.
    //
    // THE VERDICT IS THE WALK'S OWN: a walk that could not be read (a clone
    // that will not open, a HEAD or an object that cannot be read) ends the run
    // NOT ok, while a walk that read and found nothing is the ruled empty
    // success, the view opening at `0/0` with the counted line silent — there
    // is no count to explain, only a piece with no checkpoint behind it yet. The
    // two cannot be confused: the walk runs in this process and answers
    // true-with-nothing or false, never a silence to interpret.
    const auto walk_failed = [&on_done, &base_name](const std::string& diag) {
        GuiHistoryScanResult failed;
        failed.ok = false;
        failed.unavailable_reason = plain_failure(
            "could not read the commit history for 'projects/**/" + base_name +
            ".*'");
        if (!diag.empty()) {
            failed.unavailable_reason.diagnostic += " (" + diag + ")";
        }
        on_done(std::move(failed));
    };
    std::string                     diag;
    const std::optional<GuiGitRepo> repo = GuiGitRepo::open(repo_root, diag);
    if (!repo) {
        walk_failed(diag);
        return;
    }
    std::vector<std::string> candidates;
    if (!repo->walk_head(
            [&base_name](std::string_view p) {
                return is_piece_sidecar_path(p, base_name);
            },
            candidates, diag)) {
        walk_failed(diag);
        return;
    }
    if (candidates.empty()) {
        on_done(GuiHistoryScanResult{});
        return;
    }

    // THE LOAD GATE (architect 2026-08-04): each candidate's eligibility is
    // the load-in-place gate itself — load_commit_sidecars_strict, the exact
    // resolution + staging + three strict loaders the `'` act runs, one
    // predicate — so every commit the walk carries is one the act can load.
    // Anything else (a missing sidecar, a parse refusal, an ambiguous
    // per-commit path resolution) leaves the walk here, counted; the parsed
    // stores the gate produced are discarded, but each eligible commit's
    // SIDECAR SNAPSHOTS ARE KEPT — they are the walk's then sides in both
    // readings, and the NEW sides too in the iterative one wherever its forward
    // partner is a commit rather than the live state, so no delta ever
    // reads git again. The run's one handle serves every candidate.
    //
    // EACH ELIGIBLE MEMBER IS PUBLISHED THE MOMENT IT PASSES (2026-08-07): the
    // gate is the expensive step and it is per candidate, so handing the result
    // over one at a time is what lets a view opened mid-scan show the newest
    // checkpoints while the older ones are still being read.
    //
    // THE ABANDON CHECK IS THE LOOP'S OWN TOP, and the finest grain that costs
    // nothing: one candidate is one diff, one tree listing, three blob reads
    // and three strict loads of tiny files, so a supersede or a quit waits out
    // at most that.
    int hidden = 0;
    for (const std::string& sha : candidates) {
        if (abandoned()) break;
        GuiHistoryCommitLoad load;
        GuiFailure           why;
        if (!load_commit_sidecars_strict_in(*repo, repo_root, sha, base_name,
                                            load, why)) {
            ++hidden;
            continue;
        }
        on_member(std::move(load.sidecars));
    }
    GuiHistoryScanResult done;
    done.hidden = hidden;
    on_done(std::move(done));
}

const std::deque<GuiHistoryCommitSidecars>& GuiHistoryDiff::members() const {
    static const std::deque<GuiHistoryCommitSidecars> kNone;
    if (!store_ || store_->generation() != store_generation_) return kNone;
    return store_->members();
}

std::size_t GuiHistoryDiff::commit_count() const { return members().size(); }

bool GuiHistoryDiff::walk_finished_empty() const {
    // The generation test is members()' own, restated here only because the
    // DONE bit lives on the store rather than in the deque: a store that has
    // moved to another run is describing another walk, and this session's
    // answer about its own is "not finished".
    if (!store_ || store_->generation() != store_generation_) return false;
    // A FAILED RUN IS NOT AN EMPTY HISTORY. It ends DONE with an empty deque
    // like a genuinely empty walk does, and answering true here would latch the
    // head delta commit-worthy off a history nothing ever read. The mode refuses
    // entry on that run anyway (init, below), so this term guards the state a
    // run that fails WHILE THE VIEW STANDS would otherwise reach.
    if (store_->run_failed()) return false;
    return store_->run_done() && store_->members().empty();
}

bool GuiHistoryDiff::init(const AppState&           app,
                          const GuiHistoryPrefetch& prefetch) {
    available_ = false;
    unavailable_reason_ = GuiFailure{};
    repo_root_.clear();
    base_name_.clear();
    project_directory_.clear();
    store_            = nullptr;
    store_generation_ = 0;
    for (std::deque<std::optional<GuiHistoryCommitDelta>>& c : cache_) {
        c.clear();
    }

    // THE NOW SIDE IS CAPTURED FIRST, ABOVE EVERY REFUSAL (2026-09-04), because
    // the visit's OTHER walk needs it even when this one cannot be
    // bootstrapped: a bootstrap the remote walk fails opens the view on the
    // LOCAL walk, whose every member is measured against these three strings.
    // It costs three in-memory formats and no git, so paying it on the refusing
    // path costs the refusal nothing.
    now_ = build_history_now_side(app);

    // Every failure arm lands here: one stderr line, and the whole session
    // left in its documented empty shape (the now side above excepted — it is
    // the local walk's, not the commit walk's).
    auto unavailable = [this](GuiFailure why) {
        unavailable_reason_ = std::move(why);
        repo_root_.clear();
        base_name_.clear();
        project_directory_.clear();
        store_            = nullptr;
        store_generation_ = 0;
        // ONE COMPOSER, TWO READERS (2026-08-30): this line and the card the
        // entry owner raises when init() refuses both read
        // kHistoryUnavailable (history_diff.h) with this same reason
        // appended, so the terminal and the screen cannot come to say
        // different things about one fact.
        std::fprintf(stderr, "warptempo_gui: %s: %s\n", kHistoryUnavailable,
                     unavailable_reason_.diagnostic.c_str());
        return false;
    };

    // BIND FIRST, so the generation is the one the header below describes: the
    // caller has already kicked a fresh run if the store was stale, and nothing
    // can kick another while the mode stands.
    store_            = &prefetch;
    store_generation_ = prefetch.generation();

    // THE HEADER, FROM THE STORE OR COMPUTED HERE. The worker fills it in the
    // first moments of a run, so an entry that lands before it does — a `h`
    // pressed in the second after launch, or right after a staleness kick —
    // simply asks the same question on this thread. A discovery and two config
    // reads, no strict load: cheap enough to pay at a keystroke, which is
    // exactly why the split
    // is here rather than one step later.
    if (prefetch.has_header()) {
        if (!prefetch.header().ok) {
            return unavailable(prefetch.header().unavailable_reason);
        }
        repo_root_         = prefetch.header().repo_root;
        base_name_         = prefetch.header().base_name;
        project_directory_ = prefetch.header().project_directory;
    } else {
        const GuiHistoryWalkHeader h =
            resolve_history_walk_header(app.source_audio_path,
                                        app.projects_repo);
        if (!h.ok) return unavailable(h.unavailable_reason);
        repo_root_         = h.repo_root;
        base_name_         = h.base_name;
        project_directory_ = h.project_directory;
    }

    // A SCAN THAT COULD NOT READ REFUSES, and it is the one thing between the
    // header and availability. WHAT ENDS A RUN NOT OK IS ENUMERATED AT
    // GuiHistoryScanResult (history_diff.h) and nowhere else — several arms, not
    // one, and restating them here is how the two would drift. What matters at
    // this site is the shared meaning: the run did not ANSWER, which is a
    // repository this program cannot ask about rather than a piece with no
    // checkpoints. An unread history must never establish an empty walk, an
    // empty walk being a legal standing state that opens the view and tells Save
    // and Commit there is everything to checkpoint. The failure travels as the
    // store's own recorded reason and prints HERE, on the header refusal's one
    // line and in its exact shape.
    //
    // IT STAYS REFUSED UNTIL A RUN ANSWERS, deliberately: the staleness test is
    // untouched, so a failed run is not re-kicked by pressing `h` again and the
    // recovery is an ordinary re-kick (the branch tip moving, a checkpoint
    // completing, another source) or a relaunch. A clone whose history cannot
    // be read is a broken repository, and the sanctioned-use ruling puts that
    // fix in the terminal rather than behind a retry in here.
    if (prefetch.run_failed()) {
        return unavailable(prefetch.scan_failure_reason());
    }

    // AN EMPTY WALK IS A LEGAL STANDING STATE (architect 2026-08-09), whether
    // the scan is still streaming or has FINISHED with nothing: the view opens
    // at `0/0` over a blank Remote lane, and the blank lane is the honest
    // display of a piece with no eligible checkpoint behind it. Both terminal
    // zeros — no commit touches the sidecars at all, and every touching commit
    // refusing the strict load — open exactly like the mid-scan window does, so
    // emptiness is nowhere a refusal and `done` is nowhere a term.
    //
    // WHAT THE OLD REFUSAL COST is why it went: SAVE AND COMMIT LIVES ONLY
    // INSIDE THIS VIEW, so refusing entry on an empty walk made the one act that
    // can CREATE an eligible member unreachable from the state that has none —
    // a deadlock, and not a theoretical one: RETIRING A SETTINGS KEY EMPTIES
    // EVERY PIECE'S WALK AT A STROKE, every committed sidecar then failing the
    // strict load, so the whole corpus loses the act that would write the first
    // checkpoint under the new schema. The first checkpoint after a schema
    // change is an ordinary in-app act now.
    //
    // AND THE OTHER HALF OF THE BOOTSTRAP CLOSED THE SAME DAY: a piece whose
    // sidecars have never been committed at all opens here too. The header names
    // it a folder rather than refusing — the folder its SOURCE is sitting in,
    // which exists because the source is in it — so there is no piece whose
    // first checkpoint needs a terminal, which is the point of both halves
    // together.
    //
    // THE COUNTED EXPLANATION IS THE PREFETCH'S, at its DONE and in one place
    // (history_prefetch.cpp): the two message strings that stood here died with
    // the refusal rather than becoming informational prints beside it.

    // (THE NOW SIDE IS CAPTURED AT THE HEAD OF THIS BODY since 2026-09-04, the
    // local fallback needing it on the refusing path too. Every delta this
    // session hands out is measured against those exact bytes. The delta caches
    // are NOT sized here — membership grows during a visit, so delta_at grows
    // them.)
    available_ = true;
    return true;
}

const std::string& GuiHistoryDiff::sha_at(std::size_t index) const {
    static const std::string kNone;
    const std::deque<GuiHistoryCommitSidecars>& m = members();
    if (index >= m.size()) return kNone;
    return m[index].sha;
}

// THE TYPED LINE DIFF OF ONE PAIR OF SIDES (the contract is at the
// declaration) — the whole delta computation, taken off the walk position so
// that EVERY reading of EVERY walk runs the identical mechanism over different
// texts. The commit walk's cumulative reading hands it the viewed commit's
// snapshots and the frozen now side; its iterative reading hands it the viewed
// commit's and THE NEXT-NEWER ITEM's — the member one newer, or that same frozen
// now side at the newest index; the LOCAL walk (GuiHistoryLocalWalk) hands it two
// serialized undo states under the same two rules. Nothing here knows which it
// is, which is what makes four readings the same answer to four questions rather
// than four answers.
//
// IT IS NOT FILE-LOCAL ANY MORE (2026-08-07, with the local walk): the second
// walk needs the same mechanism, and a copy of it would be exactly the second
// grammar this module refuses everywhere else.
//
// `sha` is always the VIEWED member's, in both readings: the delta NAMES the
// checkpoint it describes, whichever side of the comparison that checkpoint
// happens to be (the old side in iterative, the old side in cumulative too). The
// local walk passes it EMPTY — a state of the session's own timeline has no
// name.
GuiHistoryCommitDelta compute_commit_delta(
        const std::string& sha,
        const std::string& then_warp,
        const std::string& then_phase_reset,
        const std::string& then_settings,
        const std::string& now_warp,
        const std::string& now_phase_reset,
        const std::string& now_settings) {
    GuiHistoryCommitDelta d;
    d.sha = sha;

    const LineDiff warp_diff = diff_lines(then_warp, now_warp);
    const LineDiff phase_reset_diff =
        diff_lines(then_phase_reset, now_phase_reset);
    const LineDiff settings_diff = diff_lines(then_settings, now_settings);

    // EVERY LINE HERE PARSES: the then side passed the strict whole-set load
    // at init (that is what walk membership means) and the now side is the
    // writers' own output, so the extraction's boolean below is the parse's
    // own optional shape, not a leniency arm — there is no unparseable line
    // to drop and no counter for one (both died with the gate, 2026-08-04).
    //
    // THE DISABLED AXIS SPLITS INTO TEXT AND FACE HERE (architect 2026-08-22),
    // the live lane's own split carried into the delta: the DIM the painter
    // shows is each line's EFFECTIVE verdict resolved within its own side's
    // FULL warp set (warp_side_effective_disabled above — the cascade, so a
    // label ref dims when its same-side definition is disabled), while the
    // TEXT's '#' and the revert's reconstituted line stay the VERBATIM local
    // byte (`disabled`). Each entry looks its own line up in its OWN side's
    // map — added lines in the now side's, removed in the then side's. Phase
    // resets have no cascade, so their local bit IS the effective verdict and
    // their loops below carry nothing extra (the ruling is at
    // GuiHistoryPhaseResetEntry).
    const std::map<std::string, bool> then_warp_effective =
        warp_side_effective_disabled(then_warp);
    const std::map<std::string, bool> now_warp_effective =
        warp_side_effective_disabled(now_warp);
    const auto effective_of = [](const std::map<std::string, bool>& side,
                                 const std::string& line, bool local) {
        // An absent line is unreachable — every diffed line came out of its
        // side's own text — so the local bit is a defensive floor, never a
        // second verdict.
        const auto it = side.find(line);
        return (it != side.end()) ? it->second : local;
    };

    // EVERY ENTRY CARRIES ITS ROW WITHIN ITS FRAME'S RUN (2026-09-16): each
    // side's ordinals are taken once over the side's whole line list through
    // run_ordinals, keyed by the diff's own line positions, and each entry
    // reads its own side's — an added line the now side's, a removed line the
    // then side's. The frame comes off the column's own extractor, so the run
    // is closed exactly where the entry loop below would refuse the line.
    const auto warp_frame_of =
        [](const std::string& line) -> std::optional<int64_t> {
            GuiHistoryWarpEntry e;
            if (!extract_warp_entry(line, e)) return std::nullopt;
            return e.frame;
        };
    const auto phase_reset_frame_of =
        [](const std::string& line) -> std::optional<int64_t> {
            GuiHistoryPhaseResetEntry e;
            if (!extract_phase_reset_entry(line, e)) return std::nullopt;
            return e.frame;
        };
    const std::vector<int> then_warp_ordinal =
        run_ordinals(warp_diff.then_lines, warp_frame_of);
    const std::vector<int> now_warp_ordinal =
        run_ordinals(warp_diff.now_lines, warp_frame_of);
    const std::vector<int> then_phase_reset_ordinal =
        run_ordinals(phase_reset_diff.then_lines, phase_reset_frame_of);
    const std::vector<int> now_phase_reset_ordinal =
        run_ordinals(phase_reset_diff.now_lines, phase_reset_frame_of);

    for (const std::size_t j : warp_diff.added) {
        const std::string&  line = warp_diff.now_lines[j];
        GuiHistoryWarpEntry e;
        if (extract_warp_entry(line, e)) {
            e.ordinal = now_warp_ordinal[j];
            e.effective_disabled =
                effective_of(now_warp_effective, line, e.disabled);
            d.warp_added.push_back(std::move(e));
        }
    }
    for (const std::size_t i : warp_diff.removed) {
        const std::string&  line = warp_diff.then_lines[i];
        GuiHistoryWarpEntry e;
        if (extract_warp_entry(line, e)) {
            e.ordinal = then_warp_ordinal[i];
            e.effective_disabled =
                effective_of(then_warp_effective, line, e.disabled);
            d.warp_removed.push_back(std::move(e));
        }
    }
    for (const std::size_t j : phase_reset_diff.added) {
        GuiHistoryPhaseResetEntry e;
        if (extract_phase_reset_entry(phase_reset_diff.now_lines[j], e)) {
            e.ordinal = now_phase_reset_ordinal[j];
            d.phase_reset_added.push_back(e);
        }
    }
    for (const std::size_t i : phase_reset_diff.removed) {
        GuiHistoryPhaseResetEntry e;
        if (extract_phase_reset_entry(phase_reset_diff.then_lines[i], e)) {
            e.ordinal = then_phase_reset_ordinal[i];
            d.phase_reset_removed.push_back(e);
        }
    }

    pair_changes_by_frame(
        d.warp_removed, d.warp_added, d.warp_changed,
        [](const GuiHistoryWarpEntry& r, const GuiHistoryWarpEntry& a) {
            GuiHistoryWarpChange c;
            c.frame            = r.frame;
            c.then_ordinal     = r.ordinal;
            c.now_ordinal      = a.ordinal;
            c.then_tempo_token = r.tempo_token;
            c.now_tempo_token  = a.tempo_token;
            c.then_disabled    = r.disabled;
            c.now_disabled     = a.disabled;
            c.then_effective_disabled = r.effective_disabled;
            c.now_effective_disabled  = a.effective_disabled;
            return c;
        });
    pair_changes_by_frame(
        d.phase_reset_removed, d.phase_reset_added, d.phase_reset_changed,
        [](const GuiHistoryPhaseResetEntry& r,
           const GuiHistoryPhaseResetEntry& a) {
            GuiHistoryPhaseResetChange c;
            c.frame         = r.frame;
            c.then_ordinal  = r.ordinal;
            c.now_ordinal   = a.ordinal;
            c.then_disabled = r.disabled;
            c.now_disabled  = a.disabled;
            return c;
        });

    // THE SCALE PAIR RIDES THE SAME SUBSTITUTION as the marker columns: then is
    // whichever side is older in this reading, now whichever is newer, so the
    // corner's `Scale: [-]a [+]b` says the same kind of thing in both.
    d.then_scale_token = scale_token_of(then_settings);
    d.now_scale_token  = scale_token_of(now_settings);
    d.scale_changed    = (d.then_scale_token != d.now_scale_token);

    // One line per commit view, at most. The degraded arm is an ALLOCATION
    // guard, not a format leniency: a loader-clean sidecar past the DP caps is
    // still diffed, coarsely, as replaced whole — unreachable on any real
    // corpus file (tens to a few hundred lines).
    if (warp_diff.degraded || phase_reset_diff.degraded ||
        settings_diff.degraded) {
        std::fprintf(stderr,
                     "warptempo_gui: History diff at %s exceeded the line cap; "
                     "the affected sidecar reads as replaced whole\n",
                     d.sha.c_str());
    }

    return d;
}

const GuiHistoryCommitDelta* GuiHistoryDiff::delta_at(
    std::size_t index, GuiHistoryCompare compare) {
    const std::deque<GuiHistoryCommitSidecars>& commits = members();
    if (!available_ || index >= commits.size()) return nullptr;
    std::deque<std::optional<GuiHistoryCommitDelta>>& slots =
        cache_[static_cast<std::size_t>(compare)];
    // GROW TO MEMBERSHIP, never shrink: the walk only ever appends (older
    // commits, arriving from the scan), and push_back leaves every slot already
    // handed out exactly where it is — this deque IS the pointer-stability
    // contract at the declaration.
    while (slots.size() < commits.size()) slots.emplace_back();
    if (slots[index].has_value()) return &*slots[index];

    // THE THEN SIDE IS A SNAPSHOT THE LOAD GATE ALREADY READ, in both readings:
    // walk membership required reading (and strictly loading) all three
    // sidecars, so the walk carries every member's texts and a delta runs no git
    // at all, whichever pair of sides it takes.
    const GuiHistoryCommitSidecars& snap = commits[index];

    if (compare == GuiHistoryCompare::Cumulative) {
        slots[index] = compute_commit_delta(
            snap.sha, snap.warpmarkers.text, snap.phaseresetmarkers.text,
            snap.settings.text,
            now_.warpmarkers_text, now_.phaseresetmarkers_text,
            now_.settings_text);
        return &*slots[index];
    }

    // ITERATIVE COMPARES FORWARD, TOWARD NOW (architect 2026-08-05, superseding
    // the walk-parent pairing of earlier the same day): THEN is the viewed
    // checkpoint and NOW is THE NEXT-NEWER ITEM, so the delta is what happened
    // AFTER this checkpoint, one step at a time.
    //
    // THE NEXT-NEWER ITEM IS THE LIVE STATE AT INDEX 0 and the member one newer
    // otherwise (the list is newest-first, so that is index - 1). So EVERY index
    // has a forward partner and there is no empty-delta arm here at all — the
    // walk's oldest end is an ordinary index, and its newest end is where the
    // session is.
    //
    // WHICH MAKES INDEX 0'S TWO READINGS THE SAME DELTA, deliberately: both are
    // the newest checkpoint against the live now side, so a session freshly
    // loaded right after a commit reads BLANK in both. They are still cached in
    // their own slots — one delta computed twice — rather than aliased, because
    // the coincidence is a property of the pairing, not a rule any reader should
    // have to know.
    //
    // Between COMMITTED neighbours the pairing may span commits the LOAD GATE
    // hid, which is the walk's own honesty: the nearest checkpoint the mode can
    // show is the only one whose delta has two reachable sides (the file head's
    // compare-mode block owns the ruling).
    if (index == 0) {
        slots[index] = compute_commit_delta(
            snap.sha, snap.warpmarkers.text, snap.phaseresetmarkers.text,
            snap.settings.text,
            now_.warpmarkers_text, now_.phaseresetmarkers_text,
            now_.settings_text);
        return &*slots[index];
    }
    const GuiHistoryCommitSidecars& newer = commits[index - 1];
    slots[index] = compute_commit_delta(
        snap.sha, snap.warpmarkers.text, snap.phaseresetmarkers.text,
        snap.settings.text,
        newer.warpmarkers.text, newer.phaseresetmarkers.text,
        newer.settings.text);
    return &*slots[index];
}

// ---------------------------------------------------------------------------
// the LOCAL walk — the same formula over the undo/redo timeline's states
// ---------------------------------------------------------------------------

void GuiHistoryLocalWalk::init(const AppState&          app,
                               const GuiHistoryNowSide& now) {
    app_        = &app;
    undo_count_ = app.history.undo_stack.size();
    redo_count_ = app.history.redo_stack.size();
    // THE +1 IS THE LIVE MEMBER — the state the session is standing in, which is
    // a member of the timeline like any other (the class comment owns the model).
    // It is also why a fresh session answers 1 rather than 0.
    count_      = undo_count_ + redo_count_ + 1;
    gui_        = capture_history_gui_side(app);
    now_        = now;
    members_.assign(count_, Member{});
    // SIZED ONCE, NEVER GROWN — the frozen-timeline premise (the class comment
    // owns it) is exactly what lets these be vectors where the commit walk needs
    // deques: nothing can append a member under a live visit, so no reallocation
    // can move a delta this hands out.
    for (std::vector<std::optional<GuiHistoryCommitDelta>>& c : cache_) {
        c.assign(count_, std::nullopt);
    }
}

// ONE MEMBER'S THREE TEXTS, serialized on first ask. The mapping is the class
// comment's, and it lives at entry_at below rather than here: index k < R is the
// FUTURE state redo_stack[k], k == R is THE LIVE MEMBER (the frozen now side's
// own three texts, nothing serialized), and k > R is the PAST state
// undo_stack[U + R - k], whose snapshots are the state BEFORE the event that
// entry records.
//
// THE PAST ARM INDEXES FROM THE BOTTOM, WHICH IS WHY A PUSH COULD NOT MOVE A
// MEMBER — a property that mattered while the frozen-timeline premise had one
// hole, the admitted S->T VIEW SWITCH's iteration-bracket push. THAT HOLE IS
// CLOSED AT ITS SOURCE (2026-08-07): iteration mode is TARGET-LEGAL, so the S->T
// edge wipes nothing and writes no store, and `i` is not on the mode's keyboard
// allowlist, so the bit cannot move in here either. NO ROUTE PUSHES, POPS OR
// EVICTS ON EITHER STACK while the view stands, so the premise is EXCEPTIONLESS
// BY CONSTRUCTION and stands on that derivation alone. The bottom-indexing stays
// what it always was — the shape that keeps an append harmless if one ever
// returns.
//
// THE TWO SIZE TERMS IN member_readable ARE BOUNDS PRECONDITIONS on the
// subscript this function is about to perform, and they predate all of that: a
// stack shorter than its captured size answers NOTHING AT ALL (a blank lane)
// rather than being read at indices that now mean other events. BOTH are tested
// whichever arm the index takes, because the count that bounds the index is
// built from both.
//
// (A PUSH SERIAL — a per-entry identity the walk captured at init and re-checked
// here, written for the kCap-EVICTION shape the admitted push could reach, where
// the bottom entry goes and every position slides down one while the size holds
// — lived for one day of that same date and was DELETED by the architect once
// that producer went: a producer-less mechanism rather than a granularity change,
// in a feature-complete project. Do not re-propose it.)
bool GuiHistoryLocalWalk::member_readable(std::size_t index) const {
    if (app_ == nullptr || index >= count_) return false;
    if (app_->history.undo_stack.size() < undo_count_) return false;
    if (app_->history.redo_stack.size() < redo_count_) return false;
    return true;
}

const UndoEntry* GuiHistoryLocalWalk::entry_at(std::size_t index) const {
    // THE LIVE MEMBER HAS NO ENTRY: it is the state the session is standing in,
    // held by the live stores themselves and by the frozen now side's texts.
    if (index == redo_count_) return nullptr;
    // A FUTURE state's entry is a redo counter-entry, a PAST state's an undo
    // entry, and the two carry identical fields (the carry-everywhere shape), so
    // one expression reads both.
    return index < redo_count_
        ? &app_->history.redo_stack[index]
        : &app_->history.undo_stack[undo_count_ + redo_count_ - index];
}

const GuiHistoryLocalWalk::Member* GuiHistoryLocalWalk::member_at(
        std::size_t index) {
    if (!member_readable(index)) return nullptr;
    Member& m = members_[index];
    if (m.built) return &m;

    const UndoEntry* entry = entry_at(index);
    if (entry == nullptr) {
        // THE LIVE MEMBER, verbatim from the frozen now side — the same three
        // strings every delta's live side is already made of, so "the member and
        // the now side agree" is an identity here rather than two formattings
        // that had better match.
        m.warpmarkers_text       = now_.warpmarkers_text;
        m.phaseresetmarkers_text = now_.phaseresetmarkers_text;
        m.settings_text          = now_.settings_text;
        m.built                  = true;
        return &m;
    }

    const UndoEntry& e = *entry;
    m.warpmarkers_text       = format_warpmarkers_text(e.snapshot);
    m.phaseresetmarkers_text =
        format_phaseresetmarkers_text(e.phase_reset_snapshot);
    // THE ENGINE BLOCK IS THE ONLY THING AN UNDO ENTRY CARRIES about the
    // settings file, and the captured GUI half is what fills in the rest — the
    // same half the now side was formatted with, so the two sides of every local
    // delta differ in the engine keys or in nothing.
    m.settings_text =
        format_history_settings_text(*gui_, e.settings.engine_settings);
    m.built = true;
    return &m;
}

// ONE MEMBER'S TYPED STATE — the same three arms as the texts above, over the
// state itself. The LIVE MEMBER's is the session's own stores and engine block;
// every other member's is its entry's snapshots, which is exactly what a restore
// of that entry would put back. Nothing is built, cached or serialized here: the
// state already exists, and this only says where.
std::optional<GuiHistoryLocalWalk::MemberState>
GuiHistoryLocalWalk::member_state(std::size_t index) const {
    if (!member_readable(index)) return std::nullopt;
    const UndoEntry* entry = entry_at(index);
    if (entry == nullptr) {
        return MemberState{&app_->warpmarkers.markers(),
                           &app_->phaseresetmarkers.markers(),
                           &app_->engine_settings};
    }
    return MemberState{&entry->snapshot, &entry->phase_reset_snapshot,
                       &entry->settings.engine_settings};
}

const GuiHistoryCommitDelta* GuiHistoryLocalWalk::delta_at(
        std::size_t index, GuiHistoryCompare compare) {
    const Member* member = member_at(index);
    if (member == nullptr) return nullptr;
    std::vector<std::optional<GuiHistoryCommitDelta>>& slots =
        cache_[static_cast<std::size_t>(compare)];
    if (slots[index].has_value()) return &*slots[index];

    // THE PAIR, by the model's two rules (the class comment derives them).
    //
    // ITERATIVE IS THE COMMIT WALK'S FORWARD PAIRING VERBATIM: then = this
    // member, now = the member one NEWER (index - 1, the list being newest
    // first), so the delta is exactly the event the two bracket. At index 0
    // there is nothing newer, so the member pairs WITH ITSELF and
    // compute_commit_delta answers empty — the same blank the commit walk shows
    // at its newest index right after a commit. Computed rather than
    // short-circuited, so there is one pairing expression and no second route to
    // an empty delta.
    //
    // CUMULATIVE MEASURES AGAINST THE LIVE MEMBER, whatever the position — "how
    // does my session differ". For a PAST member (index > R) and for the live
    // member itself that is then = this member, now = live, unchanged. For a
    // FUTURE member (index < R) THE SIDES SWAP — then = live, now = this member
    // — because the future state is the NEWER of the two, and the newer side is
    // green in both readings without an exception.
    const Member* then_side = member;
    const Member* now_side  = nullptr;
    if (compare == GuiHistoryCompare::Iterative) {
        now_side = (index == 0) ? member : member_at(index - 1);
    } else if (index < redo_count_) {
        then_side = member_at(redo_count_);
        now_side  = member;
    } else {
        now_side = member_at(redo_count_);
    }
    // Unreachable: both partners are in range whenever `index` is (index - 1 is
    // smaller, and the live member's index is below the count by construction),
    // and the two size preconditions passed for this same pair of stacks a
    // moment ago. Stated rather than assumed, and answering the blank lane
    // rather than pairing against a side that does not exist.
    if (then_side == nullptr || now_side == nullptr) return nullptr;

    // NO SHA: a timeline state has no name, and the corner reads the empty string
    // rather than being told separately (on the Local tab the corner shows
    // `n/N` alone).
    slots[index] = compute_commit_delta(
        std::string(), then_side->warpmarkers_text,
        then_side->phaseresetmarkers_text, then_side->settings_text,
        now_side->warpmarkers_text, now_side->phaseresetmarkers_text,
        now_side->settings_text);
    return &*slots[index];
}

// ---------------------------------------------------------------------------
// THE COMMIT ACT — the first of the product's three mutating git routes
// (the GitHub check and the pull follow it)
// ---------------------------------------------------------------------------

namespace {

// THE THREE COMMITTED PATHS a piece's checkpoint occupies, in kSidecarExtensions
// order (which is what pairs each path with its text). One owner: the act writes
// them, asks the status about them, stages them and commits them, and all four
// steps must be talking about the same three files.
std::vector<std::string> checkpoint_paths(const std::string& project_directory,
                                          const std::string& base_name) {
    std::vector<std::string> paths;
    paths.reserve(kSidecarCount);
    for (const char* ext : kSidecarExtensions) {
        paths.push_back(project_directory + "/" + base_name + ext);
    }
    return paths;
}

}  // namespace

std::string history_checkpoint_title(const std::string& project_directory) {
    const std::size_t slash = project_directory.rfind('/');
    const std::string id    = (slash == std::string::npos)
                                  ? project_directory
                                  : project_directory.substr(slash + 1);
    return "Update " + id;
}

// THE ACT — ONE SANCTIONED PATH, EACH STEP'S OWN VERDICT, ONE ERROR CLASS.
//
// THE RULING: SANCTIONED USE IS STRICT-EXACT INTENDED USE, AND ANYTHING ELSE
// THROWS AN ERROR THAT IS FIXED IN THE TERMINAL, OUTSIDE THE GUI (architect
// 2026-08-09, superseding the graded machinery of 2026-08-04..09 whole: the
// attribution walk, the retry family, the subject selector, the byte gates and
// the witness grading are all deleted). The projects repository is the app's
// alone (the code lives in its own repository, architect 2026-09-27), the
// corpus is app-written and its history is linear — nothing this act does
// creates a merge — so the act stops distinguishing deviation cases and stops
// trying to recover from them. It is minimal but airtight: it does the one
// thing, and where the repository does not answer the way sanctioned use
// implies, it says so and stops.
//
// EVERY STEP IS DECIDED ON ITS OWN VERDICT, the standard model every git
// front-end uses (architect 2026-09-06: "we prefer parsimony in code"): each of
// GuiGitRepo's calls answers whether it did the thing, and nothing here
// observes the repository afterwards to find out. It runs in this process
// through libgit2 (git_repo.h), so NO HOOK RUNS — no `pre-commit`, no
// `commit-msg`, no `post-commit`, no `pre-push`; the projects repository has
// none, and the act does not look for them.
//
// THE STEPS, each numbered at its own site below:
//   (1) CAPTURE — the branch read ONCE. Detached (or unborn) refuses
//       immediately.
//   (1a) THE GUARD — the projects-home guard, whose validated URLs the fetch
//       and the push consume. A refusal is RemoteRefused.
//   (1b) FETCH FIRST (architect 2026-09-27) — GitHub's newest state, before
//       anything is written: unreachable is RemoteUnreachable, declined is
//       RemoteRefused, and a branch BEHIND the fetched upstream (or diverged
//       from it) is RemoteMoved — fast-forward only, so a checkpoint is never
//       committed on top of a stale base. The prelude save has landed either
//       way; this is the plain save's ending.
//   (2) WRITE the three sidecars.
//   (3) PRE-FLIGHT — one status read over those three paths, which answers
//       BOTH questions the act needs: are the paths dirty, and does the branch
//       OWE ITS UPSTREAM A PUSH (ahead of it, or its upstream branch gone).
//   (4) DIRTY — stage, then commit the three paths under the caller's title;
//       a failure on either is CommitFailed with libgit2's words on stderr.
//   (5) PUSH — iff a commit was just made OR the branch already owed one. A
//       failure is CommittedNotPushed, success is Committed. Clean paths and
//       nothing to publish is NothingToCommit, and the act runs no mutation at
//       all.
//
// THE CLEAN-BUT-OWING ARM PUSHES, which is the standard tool's answer to pending
// commits and the reason the clean arm needs no observation of its own: the
// pre-flight already said the remote has not got what the branch has, so the
// act publishes it rather than reporting a state it declines to fix.
//
// THE ONE ACCEPTED IMPRECISION, recorded rather than worked around: a PUSH that
// fails at the time bound (git_repo.cpp's kServerTimeoutMs) after the server
// had taken it is reported CommittedNotPushed although it landed — the next
// check or act is the correction, finding the branch no longer ahead. A REMOTE
// THAT MOVED IN THE SECONDS SINCE THE ACT'S OWN FETCH (a checkpoint pushed from
// another device) refuses the push as non-fast-forward, CommittedNotPushed
// too — the accepted window; the next check reads Diverged. A FAILED PUSH IS
// RETRIED IN THE APP (architect 2026-09-27, superseding "never by an in-app
// retry" of 2026-08-09): the status reads Ahead, and Ctrl+S in the `h` view
// runs this act again, whose clean-but-owing arm pushes the branch. OUT-OF-APP
// GIT UNDER projects/ is unsanctioned use: a commit racing this act may yield
// a blunt error rather than a graded diagnosis (github-recheck.md carries the
// history of what this replaced).
//
// THE PROJECTS-HOME GUARD STAYS, and it is not an outcome observation — it is
// the FENCE that keeps a checkpoint from publishing to the wrong place. It runs
// at the mutating boundary, first (step 1a), and the fetch and the push go to
// THE URLS IT JUST VALIDATED.
//
// WHAT THE COMMIT CANNOT CARRY: its tree is HEAD's with the three paths laid
// over it and nothing else (GuiGitRepo::commit_paths, `git commit -- <paths>`),
// so foreign staged work in the repository can never ride along on a
// checkpoint. The stage in front of it is what records the working tree's
// bytes for those paths — a sidecar new to its folder included, which is
// untracked until staged.
//
// WHAT REMAINS AFTER A FAILURE. The three files are written first and are NEVER
// rolled back: a commit that fails leaves them in the working tree — staged, if
// the stage got that far — where `git status` shows them and a hand commit can
// still land them, and a WRITE that fails part-way leaves the files it had
// already written standing beside the one it could not. That is the honest
// shape: the bytes are the user's own state, not a temporary, and a failed act
// that swept them away would destroy the only copy of what the user asked to
// keep. It is also why the write failure and the commit failure are different
// outcomes.
//
// THE COMMIT IDENTITY IS THE MACHINE'S: author and committer come from the
// clone's git configuration and the global files (or git's own environment
// variables), and this program embeds no name and no address. THE PUSH'S
// CREDENTIAL IS THE DEPLOY KEY beside the device config, never the account's
// ssh key, and GitHub's host keys are pinned (git_repo.cpp owns both); a
// missing key is a CommittedNotPushed whose stderr line names the path.
//
// `title` IS THE COMMIT MESSAGE and the caller's (the commit-title editor's
// buffer, seeded from history_checkpoint_title). Nothing matches on it, so it is
// written and never read back.
//
// IT CREATES NO DIRECTORY, AND NEEDS NONE: `project_directory` is the folder the
// SOURCE is sitting in (resolve_history_walk_header's one rule), so it exists
// because the file the session is editing is in it. THE FIRST CHECKPOINT OF A
// NEW PIECE IS AN ORDINARY IN-APP ACT — put the piece in its own folder under
// `projects/`, and Save and commit does the rest with no step in a terminal.
GuiHistoryCommitOutcome commit_history_checkpoint(
    const std::string& repo_root, const std::string& project_directory,
    const std::string& base_name, const std::string& projects_repo,
    const GuiHistoryNowSide& bytes, const std::string& title,
    GuiGitHubStatus& github) {
    github = GuiGitHubStatus::Unchecked;

    // THE ONE HANDLE the act runs on, this worker's own. A clone that will not
    // open has taken nothing, which is what WriteFailed says.
    std::string               diag;
    std::optional<GuiGitRepo> repo = GuiGitRepo::open(repo_root, diag);
    if (!repo) {
        std::fprintf(stderr,
                     "warptempo_gui: Checkpoint refused: could not open the "
                     "clone at '%s' (%s)\n",
                     repo_root.c_str(), diag.c_str());
        return GuiHistoryCommitOutcome::WriteFailed;
    }

    // (1) THE BRANCH, READ ONCE — the act's ONLY reading of the mutable symbolic
    // HEAD. It is what the push's refspec names at both ends, and reading HEAD
    // again at the push would let a checkout mid-act publish onto a branch the
    // act never looked at.
    //
    // A DETACHED HEAD IS UNSANCTIONED USE AND THROWS HERE, before anything is
    // written: the act publishes onto a branch, and there is no branch. Nothing
    // has reached the repository, which is what WriteFailed says.
    const std::string branch = repo->head_branch();
    if (branch.empty()) {
        std::fprintf(stderr,
                     "warptempo_gui: Checkpoint refused: HEAD is detached, "
                     "check out a branch in the terminal\n");
        return GuiHistoryCommitOutcome::WriteFailed;
    }

    // (1a) THE GUARD, at the mutating boundary: the clone's remotes must be
    // the configured projects home, and the fetch below and the push at (5)
    // go to the URLs it validates, set on their own remote instances and never
    // re-resolved from the mutable name `origin`.
    GuiFailure  guard_reason;
    std::string destination;
    std::string fetch_source;
    if (!clone_is_projects_home(*repo, repo_root, projects_repo, guard_reason,
                                &destination, &fetch_source)) {
        std::fprintf(stderr, "warptempo_gui: Checkpoint refused: %s\n",
                     guard_reason.diagnostic.c_str());
        github = GuiGitHubStatus::Refused;
        return GuiHistoryCommitOutcome::RemoteRefused;
    }

    // (1b) FETCH FIRST, and refuse before any write if GitHub has moved: the
    // act commits on top of the newest checkpoint or not at all.
    {
        const GuiGitFetch fetched =
            repo->fetch_origin(fetch_source, /*cancel=*/nullptr, diag);
        if (fetched == GuiGitFetch::Refused) {
            std::fprintf(stderr,
                         "warptempo_gui: Checkpoint refused: GitHub refused "
                         "this device (%s)\n",
                         diag.c_str());
            github = GuiGitHubStatus::Refused;
            return GuiHistoryCommitOutcome::RemoteRefused;
        }
        if (fetched != GuiGitFetch::Fetched) {
            std::fprintf(stderr,
                         "warptempo_gui: Checkpoint refused: GitHub cannot be "
                         "reached (%s)\n",
                         diag.c_str());
            github = GuiGitHubStatus::Offline;
            return GuiHistoryCommitOutcome::RemoteUnreachable;
        }
        const GuiGitUpstream up = repo->compare_with_upstream(branch);
        switch (up.reading) {
        case GuiGitUpstream::Reading::Unreadable:
            std::fprintf(stderr,
                         "warptempo_gui: Checkpoint refused: could not compare "
                         "'%s' with its upstream\n",
                         branch.c_str());
            return GuiHistoryCommitOutcome::WriteFailed;
        case GuiGitUpstream::Reading::NoUpstream:
            break;
        case GuiGitUpstream::Reading::Gone:
            github = GuiGitHubStatus::Ahead;
            break;
        case GuiGitUpstream::Reading::Compared:
            if (up.behind > 0) {
                github = (up.ahead > 0) ? GuiGitHubStatus::Diverged
                                        : GuiGitHubStatus::Behind;
                std::fprintf(stderr,
                             "warptempo_gui: Checkpoint refused: GitHub has "
                             "%zu newer checkpoint(s) than '%s'%s\n",
                             up.behind, branch.c_str(),
                             (up.ahead > 0)
                                 ? ", and this device has its own: fast-forward "
                                   "only, so resolve it in a terminal"
                                 : "; pull them first (Ctrl+S in the history "
                                   "view)");
                return GuiHistoryCommitOutcome::RemoteMoved;
            }
            github = (up.ahead > 0) ? GuiGitHubStatus::Ahead
                                    : GuiGitHubStatus::UpToDate;
            break;
        }
    }

    // kSidecarExtensions order, which is what pairs each text with its path.
    const std::string* texts[kSidecarCount] = {
        &bytes.warpmarkers_text, &bytes.phaseresetmarkers_text,
        &bytes.settings_text};
    const std::vector<std::string> paths =
        checkpoint_paths(project_directory, base_name);

    // (2) THE BYTES. Through the same atomic writer a Ctrl+S uses — tmp, fsync,
    // rename — so a checkpoint is never half-written, into a directory that
    // exists because the source is in it. THESE THREE PATHS ARE THE ONES THE
    // PRELUDE SAVE JUST WROTE, always and no longer only in one workflow: the
    // sidecars sit beside the source and the checkpoint sits in the source's own
    // folder, so the coincident double write is now the ONLY case — the same
    // bytes through two atomic renames, deliberately not deduped, and race-free
    // because every other save is locked out for the act's duration (the act's
    // head and github-recheck.md own that reasoning).
    for (std::size_t e = 0; e < kSidecarCount; ++e) {
        const std::string absolute = repo_root + "/" + paths[e];
        if (!atomic_write_string_to_path(absolute, *texts[e])) {
            std::fprintf(stderr,
                         "warptempo_gui: Commit failed: could not write '%s'\n",
                         paths[e].c_str());
            return GuiHistoryCommitOutcome::WriteFailed;
        }
    }

    auto commit_failed = [](const std::string& why) {
        std::fprintf(stderr, "warptempo_gui: Commit failed: %s\n", why.c_str());
        return GuiHistoryCommitOutcome::CommitFailed;
    };
    // libgit2's own account of a failing step, appended where it said anything.
    auto with_git = [](std::string why, const std::string& said) {
        if (!said.empty()) why += " (libgit2 said: " + said + ")";
        return why;
    };

    // (3) THE PRE-FLIGHT, AND IT IS THE ONLY READ THE ACT MAKES. One status
    // read answers both of the act's questions at once — whether the three
    // paths differ from what is committed, and whether the branch owes its
    // upstream a push (GuiGitRepo::status_of owns the reading) — so nothing
    // here has to ask the repository a second question to learn what a
    // mutation did.
    bool                   publication_owed = false;
    const GuiGitPathStatus before_status =
        repo->status_of(paths, publication_owed, diag);
    if (before_status == GuiGitPathStatus::Unavailable) {
        return commit_failed(
            with_git("could not read 'git status' for the checkpoint paths; "
                     "the written files are still in the working tree",
                     diag));
    }

    // (4) DIRTY — stage and commit, both over the same three paths. Reporting
    // the step that actually refused is what puts its own words on stderr.
    bool committed = false;
    if (before_status == GuiGitPathStatus::Dirty) {
        if (!repo->stage_paths(paths, diag)) {
            return commit_failed(
                with_git("git could not stage the checkpoint; the written files "
                         "are still in the working tree",
                         diag));
        }
        if (!repo->commit_paths(paths, title, diag)) {
            return commit_failed(
                with_git("git could not commit the checkpoint; the written "
                         "files are still in the working tree",
                         diag));
        }
        committed = true;
        std::fprintf(stderr, "warptempo_gui: Committed \"%s\"\n",
                     title.c_str());
    }

    // (5) THE PUSH — iff there is something for the remote to receive. A commit
    // just made is the ordinary case; a branch the pre-flight found OWING
    // PUBLICATION is the other, and it is why the clean arm is not an early
    // return: the bytes were already committed (by a previous act whose push
    // failed, or in the terminal) or the upstream branch has gone away under
    // them, and publishing them is exactly what a git front-end does with
    // pending commits.
    if (!committed && !publication_owed) {
        std::fprintf(stderr,
                     "warptempo_gui: Nothing to commit: the checkpoint is "
                     "committed and pushed\n");
        return GuiHistoryCommitOutcome::NothingToCommit;
    }

    // THE DESTINATION IS THE GUARD'S OWN ANSWER (step 1a): this act pushes to
    // THE URL IT VALIDATED, set on the push's own remote instance (never
    // written to the clone's config) so the mutable name `origin` is not
    // resolved again. The named remote still carries the push, so its
    // remote-tracking ref updates, which is what every later reading compares.
    // THE REFSPEC IS THE CAPTURED BRANCH AT BOTH ENDS, never `HEAD` and never a
    // sha: the branch is what the act publishes, and both push arms — the commit
    // it just made and the commits that were already pending — want the same
    // thing sent. Nothing forces.
    if (!repo->push_branch(branch, destination, diag)) {
        // THE PUSH'S REFUSAL IS THE VERDICT — no deploy key, a refused key, a
        // host key off the pin, a remote that has moved, a server-side
        // rejection, the time bound — each in its own words here.
        std::fprintf(stderr, "warptempo_gui: Push failed: %s\n",
                     diag.empty() ? "libgit2 reported nothing" : diag.c_str());
        github = GuiGitHubStatus::Ahead;
        return GuiHistoryCommitOutcome::CommittedNotPushed;
    }
    github = GuiGitHubStatus::UpToDate;

    std::fprintf(stderr, "warptempo_gui: Pushed '%s' to origin\n",
                 branch.c_str());
    return GuiHistoryCommitOutcome::Committed;
}

// ---------------------------------------------------------------------------
// the GitHub check
// ---------------------------------------------------------------------------

const char* github_status_word(GuiGitHubStatus status) {
    switch (status) {
    case GuiGitHubStatus::Unchecked: return nullptr;
    case GuiGitHubStatus::Checking:  return "checking...";
    case GuiGitHubStatus::UpToDate:  return "up to date";
    case GuiGitHubStatus::Ahead:     return "ahead";
    case GuiGitHubStatus::Behind:    return "behind";
    case GuiGitHubStatus::Diverged:  return "diverged";
    case GuiGitHubStatus::Offline:   return "offline";
    case GuiGitHubStatus::Refused:   return "refused";
    }
    return nullptr;
}

// THE CHECK IS ADVISORY: it owns no failure. Every answer is a status, the
// two failing ones printing their cause on one stderr line (the screen's word
// is the state; the terminal carries why), and a check that cannot classify
// answers Unchecked in silence — the `h` entry has already said on stderr why
// that visit has no remote walk.
GuiGitHubStatus check_github(const std::string&       source_audio_path,
                             const std::string&       projects_repo,
                             const std::atomic<bool>& cancel) {
    // THE CLONE AND THE GUARD, through the header's own derivation — the same
    // answer the `h` entry binds to. A guard refusal is Refused (the clone's
    // remotes are not the projects home); any other header refusal has no
    // clone to ask and answers Unchecked.
    const GuiHistoryRepoRoot root =
        resolve_repo_root_for_source(source_audio_path);
    if (!root.ok) return GuiGitHubStatus::Unchecked;
    GuiFailure                open_reason;
    std::optional<GuiGitRepo> repo = open_clone_for(root.path, open_reason);
    if (!repo) return GuiGitHubStatus::Unchecked;
    GuiFailure  guard_reason;
    std::string fetch_source;
    if (!clone_is_projects_home(*repo, root.path, projects_repo, guard_reason,
                                nullptr, &fetch_source)) {
        std::fprintf(stderr, "warptempo_gui: GitHub refused: %s\n",
                     guard_reason.diagnostic.c_str());
        return GuiGitHubStatus::Refused;
    }
    const std::string branch = repo->head_branch();
    if (branch.empty()) return GuiGitHubStatus::Unchecked;

    std::string       diag;
    const GuiGitFetch fetched = repo->fetch_origin(fetch_source, &cancel, diag);
    switch (fetched) {
    case GuiGitFetch::Cancelled:
        return GuiGitHubStatus::Unchecked;
    case GuiGitFetch::Refused:
        std::fprintf(stderr, "warptempo_gui: GitHub refused: %s\n",
                     diag.c_str());
        return GuiGitHubStatus::Refused;
    case GuiGitFetch::Unreachable:
        std::fprintf(stderr, "warptempo_gui: GitHub offline: %s\n",
                     diag.c_str());
        return GuiGitHubStatus::Offline;
    case GuiGitFetch::Fetched:
        break;
    }
    const GuiGitUpstream up = repo->compare_with_upstream(branch);
    switch (up.reading) {
    case GuiGitUpstream::Reading::Unreadable:
    case GuiGitUpstream::Reading::NoUpstream:
        return GuiGitHubStatus::Unchecked;
    case GuiGitUpstream::Reading::Gone:
        return GuiGitHubStatus::Ahead;
    case GuiGitUpstream::Reading::Compared:
        break;
    }
    if (up.ahead > 0 && up.behind > 0) {
        std::fprintf(stderr,
                     "warptempo_gui: GitHub diverged: '%s' is %zu ahead and "
                     "%zu behind its upstream\n",
                     branch.c_str(), up.ahead, up.behind);
        return GuiGitHubStatus::Diverged;
    }
    if (up.behind > 0) return GuiGitHubStatus::Behind;
    if (up.ahead > 0) return GuiGitHubStatus::Ahead;
    return GuiGitHubStatus::UpToDate;
}

// ---------------------------------------------------------------------------
// the pull
// ---------------------------------------------------------------------------

namespace {

// THE STRICT FAST-FORWARD CASE, read from the local refs: the branch, its tip
// and the upstream's remote-tracking tip, strictly behind. `reading` is the
// status the refs show when they are not that case.
GuiHistoryPullPlanVerdict read_pull_refs(const GuiGitRepo&  repo,
                                         std::string&       branch,
                                         std::string&       from_sha,
                                         std::string&       to_sha,
                                         GuiGitHubStatus&   reading) {
    reading = GuiGitHubStatus::Unchecked;
    branch  = repo.head_branch();
    if (branch.empty()) return GuiHistoryPullPlanVerdict::Moved;
    const GuiGitUpstream up = repo.compare_with_upstream(branch);
    switch (up.reading) {
    case GuiGitUpstream::Reading::Unreadable:
        return GuiHistoryPullPlanVerdict::Unreadable;
    case GuiGitUpstream::Reading::NoUpstream:
        return GuiHistoryPullPlanVerdict::Moved;
    case GuiGitUpstream::Reading::Gone:
        reading = GuiGitHubStatus::Ahead;
        return GuiHistoryPullPlanVerdict::Moved;
    case GuiGitUpstream::Reading::Compared:
        break;
    }
    if (up.ahead > 0 || up.behind == 0) {
        reading = (up.ahead > 0 && up.behind > 0) ? GuiGitHubStatus::Diverged
                  : (up.ahead > 0)                ? GuiGitHubStatus::Ahead
                                                  : GuiGitHubStatus::UpToDate;
        return GuiHistoryPullPlanVerdict::Moved;
    }
    reading  = GuiGitHubStatus::Behind;
    from_sha = repo.head_commit();
    to_sha   = up.upstream_sha;
    if (from_sha.empty() || to_sha.empty()) {
        return GuiHistoryPullPlanVerdict::Unreadable;
    }
    return GuiHistoryPullPlanVerdict::Ready;
}

// THE OPEN PIECE'S THREE PATHS, the checkpoint act's own (checkpoint_paths).
bool is_open_piece_path(const std::vector<std::string>& open,
                        const std::string&              path) {
    for (const std::string& p : open) {
        if (p == path) return true;
    }
    return false;
}

}  // namespace

GuiHistoryPullPlanVerdict plan_history_pull(const std::string&  repo_root,
                                            const std::string&  project_directory,
                                            const std::string&  base_name,
                                            GuiHistoryPullPlan& plan,
                                            GuiGitHubStatus&    reading) {
    plan = GuiHistoryPullPlan{};
    std::string                     diag;
    const std::optional<GuiGitRepo> repo = GuiGitRepo::open(repo_root, diag);
    if (!repo) {
        reading = GuiGitHubStatus::Unchecked;
        std::fprintf(stderr,
                     "warptempo_gui: Pull failed: could not open the clone at "
                     "'%s' (%s)\n",
                     repo_root.c_str(), diag.c_str());
        return GuiHistoryPullPlanVerdict::Unreadable;
    }
    const GuiHistoryPullPlanVerdict v = read_pull_refs(
        *repo, plan.branch, plan.from_sha, plan.to_sha, reading);
    if (v != GuiHistoryPullPlanVerdict::Ready) {
        if (v == GuiHistoryPullPlanVerdict::Unreadable) {
            std::fprintf(stderr,
                         "warptempo_gui: Pull failed: could not read the "
                         "branch against its upstream\n");
        }
        return v;
    }
    plan.repo_root         = repo_root;
    plan.project_directory = project_directory;
    plan.base_name         = base_name;
    // DOES THE PULL CHANGE THE OPEN PIECE — the tree-to-tree diff from the tip
    // to the upstream over the piece's three paths, the question's one
    // condition.
    const std::vector<std::string> open =
        checkpoint_paths(project_directory, base_name);
    std::vector<std::string> touched;
    if (!repo->paths_changed_between(
            plan.from_sha, plan.to_sha,
            [&open](std::string_view p) {
                return is_open_piece_path(open, std::string(p));
            },
            touched)) {
        std::fprintf(stderr,
                     "warptempo_gui: Pull failed: could not diff '%s' with "
                     "its upstream\n",
                     plan.branch.c_str());
        return GuiHistoryPullPlanVerdict::Unreadable;
    }
    plan.touches_open_piece = !touched.empty();
    return GuiHistoryPullPlanVerdict::Ready;
}

GuiHistoryPullOutcome run_history_pull(const GuiHistoryPullPlan& plan,
                                       bool                      reload,
                                       std::string&              conflict_piece) {
    conflict_piece.clear();
    std::string               diag;
    std::optional<GuiGitRepo> repo = GuiGitRepo::open(plan.repo_root, diag);
    if (!repo) {
        std::fprintf(stderr,
                     "warptempo_gui: Pull failed: could not open the clone at "
                     "'%s' (%s)\n",
                     plan.repo_root.c_str(), diag.c_str());
        return GuiHistoryPullOutcome::Unreadable;
    }
    // THE REFS AGAIN, at the act: the question may have stood for minutes,
    // and a terminal commit or pull in that time changes what a fast-forward
    // would mean.
    std::string     branch, from_sha, to_sha;
    GuiGitHubStatus reading;
    const GuiHistoryPullPlanVerdict v =
        read_pull_refs(*repo, branch, from_sha, to_sha, reading);
    if (v == GuiHistoryPullPlanVerdict::Unreadable) {
        std::fprintf(stderr,
                     "warptempo_gui: Pull failed: could not read the branch "
                     "against its upstream\n");
        return GuiHistoryPullOutcome::Unreadable;
    }
    if (v != GuiHistoryPullPlanVerdict::Ready || branch != plan.branch ||
        from_sha != plan.from_sha || to_sha != plan.to_sha) {
        std::fprintf(stderr,
                     "warptempo_gui: Pull refused: the branch or its upstream "
                     "moved since the press\n");
        return GuiHistoryPullOutcome::Moved;
    }

    // RELOAD FORCES ALL THREE of the open piece's sidecars, including any the
    // pull does not change, so the reopened screen is exactly the pulled
    // checkpoint and never a mixture with the session's drift; KEEP excludes
    // all three, so their working-tree bytes stay the session's.
    const std::vector<std::string> open =
        checkpoint_paths(plan.project_directory, plan.base_name);
    const std::vector<std::string> none;
    std::string                    conflict_path;
    const GuiGitFastForward ff = repo->fast_forward(
        branch, from_sha, to_sha, reload ? none : open, reload ? open : none,
        conflict_path, diag);
    switch (ff) {
    case GuiGitFastForward::Done:
        std::fprintf(stderr,
                     "warptempo_gui: Pulled '%s' to %s (%s)\n",
                     branch.c_str(), short_sha(to_sha).c_str(),
                     reload ? "this piece reloaded" : "this piece kept");
        return GuiHistoryPullOutcome::Pulled;
    case GuiGitFastForward::NotStarted:
        std::fprintf(stderr, "warptempo_gui: Pull failed: %s\n", diag.c_str());
        return GuiHistoryPullOutcome::Unreadable;
    case GuiGitFastForward::Conflict: {
        // THE PIECE IS NAMED BY ITS FOLDER — `projects/<piece>/...`'s second
        // component — or, for a path outside the corpus, by the path itself.
        const std::string path = conflict_path.empty() ? "?" : conflict_path;
        std::string       piece = path;
        if (path.compare(0, kProjectsPrefix.size(), kProjectsPrefix) == 0) {
            const std::size_t end = path.find('/', kProjectsPrefix.size());
            if (end != std::string::npos) {
                piece = path.substr(kProjectsPrefix.size(),
                                    end - kProjectsPrefix.size());
            }
        }
        conflict_piece = piece;
        std::fprintf(stderr,
                     "warptempo_gui: Pull refused: '%s' has changes not "
                     "committed; nothing was changed (%s)\n",
                     path.c_str(), diag.c_str());
        return GuiHistoryPullOutcome::Conflict;
    }
    case GuiGitFastForward::FilesFailed:
        std::fprintf(stderr,
                     "warptempo_gui: Pull failed: the files were partly "
                     "updated and '%s' did not move (%s)\n",
                     branch.c_str(), diag.c_str());
        return GuiHistoryPullOutcome::FilesFailed;
    case GuiGitFastForward::BranchFailed:
        std::fprintf(stderr,
                     "warptempo_gui: Pull failed: the files were updated but "
                     "'%s' did not move; 'git reset --soft %s' in the "
                     "terminal finishes it (%s)\n",
                     branch.c_str(), short_sha(to_sha).c_str(), diag.c_str());
        return GuiHistoryPullOutcome::BranchFailed;
    }
    return GuiHistoryPullOutcome::Unreadable;
}
