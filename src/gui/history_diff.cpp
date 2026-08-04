#include "history_diff.h"

#include "app_state.h"
#include "frame_format.h"
#include "phaseresetmarkers.h"
#include "settings_io.h"
#include "warpmarkers.h"
#include "warpmarkers_parse.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

// THE REPO IS A FIXED ABSOLUTE PATH. This is a single-laptop product with no
// portability shims — the same footing as the labwc/1920x1080 target
// assumptions — so the corpus lives where it lives and there is no fallback
// search, no environment variable, and no walk up from the binary. A missing
// path or a failing git there is UNAVAILABLE, reported once and dropped.
constexpr const char* kRepoRoot = "/home/b/.warptempo/warptempo_gui";

// The architect's ruled depth: the newest 20 commits touching the sidecars.
constexpr int kCommitDepth = 20;

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
// `HEAD` is the spelling because it needs no name — it is whatever this clone
// has checked out — and the same word serves the tip listing, the walk and the
// push destination, so the three cannot come to mean different things. The
// projects_repo guard is unaffected either way: it asks which REPOSITORY this
// clone is, not how fresh it is.
constexpr const char* kBranchRef = "HEAD";

// The three sidecars a source carries, in no significant order. A directory
// matches if it holds ANY of them under the source's base name — the
// architect's checkpoints are complete sets, but a partial one should still be
// found rather than silently missed, and a file the directory lacks is simply
// the empty-then-side case the diff already handles.
constexpr const char* kSidecarExtensions[] = {
    ".warpmarkers", ".phaseresetmarkers", ".settings"};

// Pathological-input guards for the line diff. The real files are tens to
// a few hundred lines, so both are unreachable in practice; they exist so a
// hand-edited or corrupt blob can never make the DP table an allocation
// hazard. kMaxDiffCells is the binding one — the table is (n+1)*(m+1)
// cells and 16M of them is 64 MB, well past anything real. Crossing either
// cap degrades that file to whole-file-replaced (every `then` line removed,
// every `now` line added), which is a truthful if coarse answer.
constexpr std::size_t kMaxDiffLines = 10000;
constexpr std::size_t kMaxDiffCells = 16u * 1024u * 1024u;

// The one settings key this mode displays. Matched as a whole-line PREFIX, so
// `gui_scale=100` — which contains "scale=" but does not start with it — is
// correctly not the scale line.
constexpr std::string_view kScaleKeyPrefix = "scale=";

// ---------------------------------------------------------------------------
// git plumbing
// ---------------------------------------------------------------------------

// Run `git -C <repo> <args...>` and capture its stdout.
//
// THE ONLY SUBCOMMANDS THIS ENTRY POINT EVER PASSES ARE `log`, `show`,
// `ls-tree`, `rev-parse`, `status` AND `remote get-url` (rev-parse joined
// 2026-08-04 with the adopt-from-commit path's spelling resolution, status the
// same day with the commit act's pre-flight probe) — all of them reads, and that
// constraint is meant to stay checkable by reading the call sites below rather
// than by trusting a runtime guard. THE MUTATING SUBCOMMANDS HAVE THEIR OWN
// ENTRY POINT, run_git_mutate directly below, which is the whole point of there
// being two: the fence is which function a call site names.
//
// argv exec, NEVER system() and never a shell: the committed directory names
// carry spaces ("550 - 1") and so do the sidecar base names, and every one of
// those bytes reaches git as one argv element with no quoting rules in
// between.
//
// The child's stderr goes to /dev/null. A `show` of a path a commit does not
// carry is an ordinary answer here, not a fault worth printing.
//
// SUCCESS IS "RAN AND PRODUCED OUTPUT", and the emptiness half is load-bearing
// rather than sloppy: main() sets SIGCHLD to SIG_IGN so the kernel auto-reaps
// the fire-and-forget audio players, which also means waitpid() here normally
// fails with ECHILD and the exit status is simply not obtainable. The status is
// still honoured when it does arrive (a future session leaving the default
// disposition), and emptiness carries the rest. Both callers want exactly that
// reading anyway: a log with no commits is unavailable, and a `show` of a
// missing path and of an empty committed file both mean "no bytes on the then
// side".
bool run_git_capture(const std::vector<std::string>& args, std::string& out) {
    out.clear();

    std::vector<std::string> full;
    full.reserve(args.size() + 3);
    full.emplace_back("git");
    full.emplace_back("-C");
    full.emplace_back(kRepoRoot);
    for (const std::string& a : args) full.push_back(a);

    std::vector<char*> argv;
    argv.reserve(full.size() + 1);
    for (std::string& s : full) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);

    int fds[2];
    if (pipe(fds) != 0) return false;

    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (pid == 0) {
        // Child: stdout to the pipe, stderr discarded, then exec immediately.
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        close(fds[1]);
        const int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execvp("git", argv.data());
        _exit(127);
    }

    close(fds[1]);
    char buf[4096];
    for (;;) {
        const ssize_t n = read(fds[0], buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) break;
        if (errno == EINTR) continue;
        break;
    }
    close(fds[0]);

    int   status = 0;
    pid_t w      = 0;
    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR);
    if (w == pid && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) {
        out.clear();
        return false;
    }

    return !out.empty();
}

// Run `git -C <repo> <args...>` for a MUTATING subcommand, and hand back the
// first line git said about it.
//
// THE MUTATING INVENTORY IS `add`, `commit` AND `push` — the commit act's three
// steps, in that order — AND THAT ACT IS THIS FUNCTION'S ONLY CALLER. Nothing
// else in the product runs a git subcommand that changes a file, a ref or the
// index.
//
// SUCCESS IS NOT DECIDED HERE, which is the one real difference from the capture
// helper above. That helper's "ran and produced output" reading is WRONG for
// these: a failed push produces plenty of output and a successful one may
// produce none, and the exit status is not obtainable either — main() sets
// SIGCHLD to SIG_IGN, so waitpid normally fails with ECHILD (the same regime the
// capture helper documents). So this reports only that the child was STARTED,
// and the caller decides the outcome by OBSERVING THE REPOSITORY afterwards:
// HEAD moved for the commit, the remote-tracking ref caught up for the push.
// That is a question about the repository rather than about the child, and it
// is answerable with the plumbing we have.
//
// STDOUT AND STDERR SHARE ONE PIPE, so `first_line` is git's own first non-empty
// line whichever stream it chose (`commit` reports on stdout, `push` on stderr).
// STDIN IS /dev/null: a GUI must never sit blocked forever behind a credential
// prompt, and the push's call site pairs this with ssh's batch mode for the same
// reason — a non-interactive failure that says so in one line is the honest
// alternative to a frozen window.
bool run_git_mutate(const std::vector<std::string>& args,
                    std::string&                    first_line) {
    first_line.clear();

    std::vector<std::string> full;
    full.reserve(args.size() + 3);
    full.emplace_back("git");
    full.emplace_back("-C");
    full.emplace_back(kRepoRoot);
    for (const std::string& a : args) full.push_back(a);

    std::vector<char*> argv;
    argv.reserve(full.size() + 1);
    for (std::string& s : full) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);

    int fds[2];
    if (pipe(fds) != 0) return false;

    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (pid == 0) {
        // Child: BOTH output streams to the pipe, stdin to /dev/null, then exec.
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        if (dup2(fds[1], STDERR_FILENO) < 0) _exit(127);
        close(fds[1]);
        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        execvp("git", argv.data());
        _exit(127);
    }

    close(fds[1]);
    std::string out;
    char        buf[4096];
    for (;;) {
        const ssize_t n = read(fds[0], buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) break;
        if (errno == EINTR) continue;
        break;
    }
    close(fds[0]);

    // Reap where the disposition allows it; the status is deliberately unread
    // (see above), and under SIG_IGN this simply fails with ECHILD.
    int   status = 0;
    pid_t w      = 0;
    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR);
    (void)w;
    (void)status;

    // The first non-empty line, trailing whitespace off. Spelled out here rather
    // than through the text helpers below so the two subprocess entry points
    // stay adjacent — the fence reads better than four saved lines would.
    for (std::size_t i = 0; i <= out.size();) {
        const std::size_t nl  = out.find('\n', i);
        const std::size_t end = (nl == std::string::npos) ? out.size() : nl;
        std::string       line = out.substr(i, end - i);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                 line.back() == '\t')) {
            line.pop_back();
        }
        if (!line.empty()) {
            first_line = std::move(line);
            break;
        }
        if (nl == std::string::npos) break;
        i = nl + 1;
    }
    return true;
}

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

// Trailing whitespace off a captured git line (`remote get-url` ends in '\n').
std::string trim_trailing_ws(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' ||
                          s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    return s;
}

struct LineDiff {
    std::vector<std::string> added;    // on `now` only
    std::vector<std::string> removed;  // on `then` only
    bool                     degraded = false;
};

// Classic LCS over whole lines with exact byte equality, in tree because the
// files are tiny and a diff library would be a dependency bought for nothing.
// A common prefix and suffix are peeled first, which is what keeps the DP table
// small on the ordinary case (one edited line in a long sorted file).
LineDiff diff_lines(const std::string& then_text, const std::string& now_text) {
    LineDiff out;
    const std::vector<std::string> a = split_lines(then_text);
    const std::vector<std::string> b = split_lines(now_text);

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
        for (std::size_t i = lo; i < a_hi; ++i) out.removed.push_back(a[i]);
        for (std::size_t j = lo; j < b_hi; ++j) out.added.push_back(b[j]);
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
            out.added.push_back(b[lo + j - 1]);
            --j;
        } else {
            out.removed.push_back(a[lo + i - 1]);
            --i;
        }
    }
    std::reverse(out.added.begin(), out.added.end());
    std::reverse(out.removed.begin(), out.removed.end());
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
    // The parse succeeded, so the '|' is there; the guard is defensive.
    out.tempo_token =
        (pipe == std::string::npos) ? std::string() : line.substr(pipe + 1);
    return true;
}

// The phase reset column has NO callable per-line entry point — its parser's
// parse_line lives in an anonymous namespace, whole-file only — so this
// mirrors it exactly rather than relaxing anything: no whitespace anywhere on
// the line, an optional leading '#' meaning disabled, then the CANONICAL
// authored frame spelling (parse_authored_frame, frame_format.h) and nothing
// else. A byte-empty line has no frame and is refused here just as it is at
// load; there is no blank-line or comment concept in the grammar.
bool extract_phase_reset_entry(const std::string&         line,
                               GuiHistoryPhaseResetEntry& out) {
    if (line.find_first_of(" \t\r") != std::string::npos) return false;
    std::string_view t(line);
    out.disabled = false;
    if (!t.empty() && t.front() == '#') {
        out.disabled = true;
        t.remove_prefix(1);
    }
    return parse_authored_frame(t, out.frame);
}

// Pair a removal and an addition sharing one frame into a change, leaving the
// unpaired remainder in place. Coincident markers are legal on both columns,
// so the pairing is positional per frame — the first unclaimed addition at
// that frame — and any surplus on either side stays a plain add or remove.
// File order survives: neither surviving list is re-sorted.
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

// The directory part of a committed path. Every path this module considers has
// passed the `projects/` prefix test, so there is always a separator and the
// answer is never empty — the repo-root case the tree-wide match had to model
// went with that match.
std::string directory_of(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return (slash == std::string::npos) ? std::string()
                                        : path.substr(0, slash);
}

// The committed paths in a NUL-separated `ls-tree -z --name-only` listing that
// sit UNDER `projects/` and whose BASENAME is
// `<base_name>.<one of the three extensions>`.
//
// The folder test is a plain prefix compare and it is the ONLY geography here:
// everything past it is the basename rule. Both callers take it — init's tip
// listing and each commit's own tree — so "the corpus lives under projects/" is
// one rule read in one place rather than a claim the walk and the per-commit
// resolution could come to disagree about.
//
// The listing is NUL-separated rather than newline-separated on purpose: git
// quotes paths containing unusual bytes when it writes them one per line
// (core.quotePath defaults to true), and a source name is free UTF-8 under
// this product's own text ruling, so a line-based read would hand back a
// C-quoted spelling that matches nothing. `-z` disables the quoting entirely
// and emits each path verbatim, which is also why a name may safely contain
// anything except NUL.
std::vector<std::string> sidecar_paths_in_listing(const std::string& listing,
                                                  const std::string& base_name) {
    std::vector<std::string> hits;
    for (const std::string& path : split_on(listing, '\0')) {
        if (path.empty()) continue;
        if (path.size() <= kProjectsPrefix.size() ||
            std::string_view(path).substr(0, kProjectsPrefix.size()) !=
                kProjectsPrefix) {
            continue;
        }
        const std::size_t slash = path.rfind('/');
        const std::string leaf =
            (slash == std::string::npos) ? path : path.substr(slash + 1);
        for (const char* ext : kSidecarExtensions) {
            if (leaf == base_name + ext) {
                hits.push_back(path);
                break;
            }
        }
    }
    return hits;
}

// The single directory carrying this source's sidecars, judged over the paths
// above. Returns it (possibly "" for the repo root) via `out_dir`, or false
// with `reason` set. Multiplicity is judged BY DIRECTORY: the three files are
// expected to sit together, so three hits in one directory is the ordinary
// success and a directory carrying only one of them still matches.
bool sole_directory_of(const std::vector<std::string>& paths,
                       const std::string&              base_name,
                       std::string&                    out_dir,
                       std::string&                    reason) {
    std::vector<std::string> dirs;
    for (const std::string& p : paths) {
        std::string d = directory_of(p);
        if (std::find(dirs.begin(), dirs.end(), d) == dirs.end()) {
            dirs.push_back(std::move(d));
        }
    }

    if (dirs.empty()) {
        reason = "No file committed under 'projects/' is named '" + base_name +
                 ".warpmarkers' or its two siblings";
        return false;
    }
    if (dirs.size() > 1) {
        std::sort(dirs.begin(), dirs.end());
        std::string list;
        for (std::size_t i = 0; i < dirs.size(); ++i) {
            if (i != 0) list += ", ";
            list += "'" + dirs[i] + "'";
        }
        reason = "Sidecars named '" + base_name +
                 ".*' are committed in more than one directory: " + list;
        return false;
    }
    out_dir = dirs.front();
    return true;
}

// ---------------------------------------------------------------------------
// the projects-home guard
// ---------------------------------------------------------------------------

// Reduce a repository spelling to bare host/path so the settings key and the
// clone's own remote can be compared as the same thing:
//
//   git@github.com:warptempo/warptempo_gui.git  ->  github.com/warptempo/warptempo_gui
//   https://github.com/warptempo/warptempo_gui  ->  github.com/warptempo/warptempo_gui
//   ssh://git@github.com/warptempo/x.git/       ->  github.com/warptempo/x
//
// A scheme goes, userinfo goes, an scp-style host:path colon becomes the path
// separator it means, and a trailing `.git` and any trailing slashes go. An
// explicit PORT is the one spelling this does not model (`host:22/path` would
// read the port as a path component) — no such remote exists here and adding
// the case would buy nothing but a branch to be wrong in.
std::string normalize_repo_url(const std::string& raw) {
    std::string s = trim_trailing_ws(raw);

    const std::size_t scheme = s.find("://");
    if (scheme != std::string::npos) s = s.substr(scheme + 3);

    const std::size_t first_slash = s.find('/');
    const std::size_t at          = s.find('@');
    if (at != std::string::npos &&
        (first_slash == std::string::npos || at < first_slash)) {
        s = s.substr(at + 1);
    }

    // scp-style `host:path` — only when no '/' precedes the ':'.
    const std::size_t colon = s.find(':');
    if (colon != std::string::npos) {
        const std::size_t slash = s.find('/');
        if (slash == std::string::npos || colon < slash) s[colon] = '/';
    }

    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".git") == 0) {
        s.erase(s.size() - 4);
    }
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

// ---------------------------------------------------------------------------
// per-commit snapshot
// ---------------------------------------------------------------------------

// One commit's committed path for each of the three sidecars, empty where that
// commit carries none. Indexed to match kSidecarExtensions.
struct GuiHistoryCommitPaths {
    std::string path[3];
};

// Resolve where this commit kept the sidecars, from THAT COMMIT'S OWN TREE —
// which is what replaces knowing the era's directory name. ONE subprocess per
// commit, not one per file: the whole listing is fetched once and all three
// extensions are picked out of it.
//
// A commit whose tree carries the base name in more than one directory (a
// stray copy — the tip tree's ambiguity is already refused at init) resolves to
// the directory holding the MOST of the three, ties broken lexicographically,
// so the answer is deterministic rather than listing-order dependent.
GuiHistoryCommitPaths resolve_commit_paths(const std::string& sha,
                                           const std::string& base_name) {
    GuiHistoryCommitPaths out;
    std::string           listing;
    if (!run_git_capture({"ls-tree", "-r", "-z", "--name-only", sha},
                         listing)) {
        return out;
    }
    const std::vector<std::string> hits =
        sidecar_paths_in_listing(listing, base_name);
    if (hits.empty()) return out;

    std::vector<std::string> dirs;
    std::vector<int>         counts;
    for (const std::string& p : hits) {
        const std::string d  = directory_of(p);
        const auto        it = std::find(dirs.begin(), dirs.end(), d);
        if (it == dirs.end()) {
            dirs.push_back(d);
            counts.push_back(1);
        } else {
            ++counts[static_cast<std::size_t>(it - dirs.begin())];
        }
    }
    std::size_t best = 0;
    for (std::size_t i = 1; i < dirs.size(); ++i) {
        if (counts[i] > counts[best] ||
            (counts[i] == counts[best] && dirs[i] < dirs[best])) {
            best = i;
        }
    }
    const std::string& chosen = dirs[best];

    for (const std::string& p : hits) {
        if (directory_of(p) != chosen) continue;
        for (std::size_t e = 0; e < 3; ++e) {
            const std::string leaf = base_name + kSidecarExtensions[e];
            if (p.size() >= leaf.size() &&
                p.compare(p.size() - leaf.size(), leaf.size(), leaf) == 0) {
                out.path[e] = p;
            }
        }
    }
    return out;
}

// The committed bytes at one resolved path, or empty when this commit carries
// no such file. `git show <rev>:<path>` takes a LITERAL committed path —
// relative to the repo root, one argv element, and NOT a pathspec, so nothing
// about it globs or follows a rename; the path came from this commit's own
// tree, so it is already the spelling that commit uses.
std::string read_snapshot_at(const std::string& sha, const std::string& path) {
    if (path.empty()) return {};
    std::string out;
    if (run_git_capture({"show", sha + ":" + path}, out)) return out;
    return {};
}

}  // namespace

bool read_commit_sidecars(const std::string&        spelling,
                          const std::string&        base_name,
                          GuiHistoryCommitSidecars& out,
                          std::string&              reason) {
    out    = GuiHistoryCommitSidecars{};
    reason.clear();

    if (spelling.empty()) {
        reason = "no commit was named";
        return false;
    }
    if (base_name.empty()) {
        reason = "the source has no sidecar base name";
        return false;
    }

    // `--verify` makes a non-resolving spelling an error rather than an echo of
    // the argument, and `^{commit}` peels whatever resolved to a commit — a tag
    // or a tree spelling that is not one fails here rather than downstream.
    std::string raw;
    if (!run_git_capture({"rev-parse", "--verify", spelling + "^{commit}"},
                         raw)) {
        reason = "'" + spelling + "' does not name a commit in " +
                 std::string(kRepoRoot);
        return false;
    }
    const std::string sha = trim_trailing_ws(raw);
    // Defensive shape check on git's own answer: --verify with the peel suffix
    // yields exactly one full object name, so anything else means the assumption
    // broke rather than that the user typed something odd.
    auto is_hex40 = [](const std::string& s) {
        if (s.size() != 40) return false;
        for (const char c : s) {
            const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            if (!ok) return false;
        }
        return true;
    };
    if (!is_hex40(sha)) {
        reason = "'" + spelling + "' did not resolve to a single commit";
        return false;
    }
    out.sha = sha;

    // That commit's OWN tree decides where the sidecars sit — the same
    // basename match the walk uses, applied to an arbitrary commit.
    const GuiHistoryCommitPaths paths = resolve_commit_paths(sha, base_name);
    out.warpmarkers.path       = paths.path[0];
    out.phaseresetmarkers.path = paths.path[1];
    out.settings.path          = paths.path[2];
    out.warpmarkers.text       = read_snapshot_at(sha, paths.path[0]);
    out.phaseresetmarkers.text = read_snapshot_at(sha, paths.path[1]);
    out.settings.text          = read_snapshot_at(sha, paths.path[2]);
    return true;
}

GuiHistoryNowSide build_history_now_side(const AppState& app) {
    GuiHistoryNowSide out;
    out.warpmarkers_text = format_warpmarkers_text(app.warpmarkers.markers());
    out.phaseresetmarkers_text =
        format_phaseresetmarkers_text(app.phaseresetmarkers.markers());

    // A Ctrl+S runs refresh_active_tab_view_from_app before the writer reads
    // the bands, stashing the live viewport / zoom / playhead / trim into the
    // ACTIVE tab's band. Mirror that stash onto LOCAL copies — the same const
    // overlay the settings editor's autocomplete recall uses — so the bytes
    // match a save exactly while this read mutates nothing. read_only is not
    // mirrored: it lives in the band already, toggled by bare `o`.
    ViewState  eff_a      = app.tab_a;
    ViewState  eff_b      = app.tab_b;
    ViewState& eff_active = (app.active_tab_view == 'B') ? eff_b : eff_a;
    eff_active.viewport_start_sample  = app.viewport_start_sample;
    eff_active.zoom_level             = app.zoom_level;
    eff_active.playhead_cursor_sample = app.playhead_cursor_sample;
    eff_active.trim                   = app.trim;

    const NonEngineSettingsSnapshot gui{
        eff_a, eff_b, app.follow_mode,
        app.active_audio_view, app.active_markers_view, app.active_tab_view,
        app.playback_speed, app.gui_scale, app.audio_player,
        app.projects_repo,
        app.libm_hash, app.libmvec_hash,
        app.fftw3_hash, app.fftw3_threads_hash};
    out.settings_text = format_settings_text(gui, app.engine_settings);
    return out;
}

bool GuiHistoryDiff::init(const AppState& app) {
    available_ = false;
    unavailable_reason_.clear();
    base_name_.clear();
    project_directory_.clear();
    commits_.clear();
    cache_.clear();

    // Every failure arm lands here: one stderr line, and the whole session
    // left in its documented empty shape whatever step got as far as filling
    // in (the tree match runs after the base name is derived, so a late
    // refusal has something to clear).
    auto unavailable = [this](std::string why) {
        unavailable_reason_ = std::move(why);
        base_name_.clear();
        project_directory_.clear();
        commits_.clear();
        std::fprintf(stderr, "warptempo_gui: History is unavailable: %s\n",
                     unavailable_reason_.c_str());
        return false;
    };

    std::error_code ec;
    if (!std::filesystem::is_directory(kRepoRoot, ec) || ec) {
        return unavailable("Repository path is missing: " +
                           std::string(kRepoRoot));
    }

    // THE PROJECTS-HOME GUARD, first because it is a precondition on the whole
    // feature rather than a property of one source. The `projects_repo`
    // setting names WHICH repository is the projects home; the clone at
    // kRepoRoot is only the transport that happens to be on this disk. If the
    // setting has been rebound to another repository, this clone's history is
    // the wrong history, and reading it anyway would answer confidently about
    // the wrong piece of work. Both spellings are normalized to bare host/path
    // first, so a scheme, an scp-style remote or a trailing `.git` never makes
    // a false mismatch.
    //
    // IT ASKS WHICH REPOSITORY, NEVER HOW FRESH: the question is answered from
    // the remote's URL and no ref at all, which is why moving the walk off
    // `origin/main` and onto the local branch (kBranchRef) left this untouched.
    // A clone that has not fetched in a year is still THIS repository, and a
    // checkpoint this product commits and fails to push is still its history.
    std::string remote_raw;
    if (!run_git_capture({"remote", "get-url", "origin"}, remote_raw)) {
        return unavailable("The clone at " + std::string(kRepoRoot) +
                           " has no 'origin' remote");
    }
    const std::string remote_norm = normalize_repo_url(remote_raw);
    const std::string setting_norm = normalize_repo_url(app.projects_repo);
    if (setting_norm.empty()) {
        return unavailable("The projects_repo setting is empty");
    }
    if (setting_norm != remote_norm) {
        return unavailable("The projects_repo setting names '" +
                           app.projects_repo + "' but the clone at " +
                           std::string(kRepoRoot) + " has origin '" +
                           trim_trailing_ws(remote_raw) + "'");
    }

    // The sidecar base name is the source's own stem — the single derivation
    // rule the loader uses when it builds <base>.warpmarkers and its two
    // siblings beside the WAV (file_loader.cpp's companion-file block). The
    // corpus names its files by exactly that, so mirroring the rule is what
    // makes the filename match work on names full of periods and commas.
    if (app.source_audio_path.empty()) {
        return unavailable("No source is loaded");
    }
    base_name_ =
        std::filesystem::path(app.source_audio_path).stem().string();
    if (base_name_.empty()) {
        return unavailable("The source path has no base name: " +
                           app.source_audio_path);
    }

    // THE MATCH IS RESOLVED AGAINST THE COMMITTED TREE, not the working
    // directory: history is what this mode reads, so what is committed on the
    // branch is the thing that decides whether there is any history to read. A
    // sidecar sitting on disk but never committed is correctly no match, and one
    // committed but since deleted from the checkout still is. (The branch is the
    // LOCAL one — kBranchRef's paragraph owns why.)
    std::string tip_listing;
    if (!run_git_capture({"ls-tree", "-r", "-z", "--name-only", kBranchRef},
                         tip_listing)) {
        return unavailable("Could not read the committed tree at " +
                           std::string(kRepoRoot));
    }
    std::string reason;
    if (!sole_directory_of(sidecar_paths_in_listing(tip_listing, base_name_),
                           base_name_, project_directory_, reason)) {
        return unavailable(std::move(reason));
    }

    // The commit walk is ERA-AGNOSTIC BELOW `projects/`: one
    // `:(glob)projects/**/<base>.<ext>` pathspec per sidecar, matching the
    // basename at any depth under that folder — including directly inside it,
    // since `**` matches zero path components as well as many. So a commit that
    // renamed or re-nested the piece's directory is followed with no --follow
    // and no knowledge of what it used to be called, which is the whole point of
    // matching by name; what the folder term adds is that a same-named file
    // OUTSIDE the corpus cannot pull commits into the walk that carry no
    // checkpoint of this piece at all.
    std::vector<std::string> log_args{"log", "-n",
                                      std::to_string(kCommitDepth),
                                      "--format=%H", kBranchRef, "--"};
    for (const char* ext : kSidecarExtensions) {
        log_args.push_back(std::string(":(glob)") +
                           std::string(kProjectsPrefix) + "**/" + base_name_ +
                           ext);
    }
    std::string log_out;
    if (!run_git_capture(log_args, log_out)) {
        return unavailable("No commit touches 'projects/**/" + base_name_ +
                           ".*'");
    }

    for (std::string& sha : split_lines(log_out)) {
        if (!sha.empty()) commits_.push_back(std::move(sha));
    }
    if (commits_.empty()) {
        return unavailable("No commit touches 'projects/**/" + base_name_ +
                           ".*'");
    }

    // The now side is captured once, here: every delta this session hands out
    // is measured against these exact bytes.
    now_ = build_history_now_side(app);
    cache_.resize(commits_.size());
    available_ = true;
    return true;
}

const std::string& GuiHistoryDiff::sha_at(std::size_t index) const {
    static const std::string kNone;
    if (index >= commits_.size()) return kNone;
    return commits_[index];
}

const GuiHistoryCommitDelta* GuiHistoryDiff::delta_at(std::size_t index) {
    if (!available_ || index >= commits_.size()) return nullptr;
    if (cache_[index].has_value()) return &*cache_[index];

    GuiHistoryCommitDelta d;
    d.sha = commits_[index];

    // One tree listing for this commit, then the three blobs out of it — the
    // paths are whatever THIS commit calls them, so a corpus rename anywhere
    // in the walked range costs nothing here.
    const GuiHistoryCommitPaths paths =
        resolve_commit_paths(d.sha, base_name_);
    const std::string then_warp = read_snapshot_at(d.sha, paths.path[0]);
    const std::string then_phase_reset =
        read_snapshot_at(d.sha, paths.path[1]);
    const std::string then_settings = read_snapshot_at(d.sha, paths.path[2]);

    const LineDiff warp_diff = diff_lines(then_warp, now_.warpmarkers_text);
    const LineDiff phase_reset_diff =
        diff_lines(then_phase_reset, now_.phaseresetmarkers_text);
    const LineDiff settings_diff =
        diff_lines(then_settings, now_.settings_text);

    d.warp_lines.added          = static_cast<int>(warp_diff.added.size());
    d.warp_lines.removed        = static_cast<int>(warp_diff.removed.size());
    d.phase_reset_lines.added   = static_cast<int>(phase_reset_diff.added.size());
    d.phase_reset_lines.removed = static_cast<int>(phase_reset_diff.removed.size());
    d.settings_lines.added      = static_cast<int>(settings_diff.added.size());
    d.settings_lines.removed    = static_cast<int>(settings_diff.removed.size());

    // A line that yields no frame drops out of the typed lists entirely —
    // legacy timestamp spellings, hand-edit damage. Adoption is where full
    // parsing gates; a view just shows less.
    for (const std::string& line : warp_diff.added) {
        GuiHistoryWarpEntry e;
        if (extract_warp_entry(line, e)) d.warp_added.push_back(std::move(e));
        else ++d.warp_lines.dropped;
    }
    for (const std::string& line : warp_diff.removed) {
        GuiHistoryWarpEntry e;
        if (extract_warp_entry(line, e)) d.warp_removed.push_back(std::move(e));
        else ++d.warp_lines.dropped;
    }
    for (const std::string& line : phase_reset_diff.added) {
        GuiHistoryPhaseResetEntry e;
        if (extract_phase_reset_entry(line, e)) d.phase_reset_added.push_back(e);
        else ++d.phase_reset_lines.dropped;
    }
    for (const std::string& line : phase_reset_diff.removed) {
        GuiHistoryPhaseResetEntry e;
        if (extract_phase_reset_entry(line, e)) d.phase_reset_removed.push_back(e);
        else ++d.phase_reset_lines.dropped;
    }

    pair_changes_by_frame(
        d.warp_removed, d.warp_added, d.warp_changed,
        [](const GuiHistoryWarpEntry& r, const GuiHistoryWarpEntry& a) {
            GuiHistoryWarpChange c;
            c.frame            = r.frame;
            c.then_tempo_token = r.tempo_token;
            c.now_tempo_token  = a.tempo_token;
            c.then_disabled    = r.disabled;
            c.now_disabled     = a.disabled;
            return c;
        });
    pair_changes_by_frame(
        d.phase_reset_removed, d.phase_reset_added, d.phase_reset_changed,
        [](const GuiHistoryPhaseResetEntry& r,
           const GuiHistoryPhaseResetEntry& a) {
            GuiHistoryPhaseResetChange c;
            c.frame         = r.frame;
            c.then_disabled = r.disabled;
            c.now_disabled  = a.disabled;
            return c;
        });

    d.then_scale_token = scale_token_of(then_settings);
    d.now_scale_token  = scale_token_of(now_.settings_text);
    d.scale_changed    = (d.then_scale_token != d.now_scale_token);

    // One line per commit view, at most. The degraded arm is separate because
    // it says something different: not "some lines were unreadable" but "this
    // file was too large to diff line by line and reads as replaced whole".
    if (warp_diff.degraded || phase_reset_diff.degraded ||
        settings_diff.degraded) {
        std::fprintf(stderr,
                     "warptempo_gui: History diff at %s exceeded the line cap; "
                     "the affected sidecar reads as replaced whole\n",
                     d.sha.c_str());
    }
    const int dropped = d.warp_lines.dropped + d.phase_reset_lines.dropped;
    if (dropped > 0) {
        std::fprintf(stderr,
                     "warptempo_gui: History diff at %s dropped %d unparseable "
                     "line(s)\n",
                     d.sha.c_str(), dropped);
    }

    cache_[index] = std::move(d);
    return &*cache_[index];
}

// ---------------------------------------------------------------------------
// THE COMMIT ACT — the product's one mutating git route
// ---------------------------------------------------------------------------

namespace {

// A spelling's object name, or "" when it does not resolve. `--verify` is what
// makes a missing ref an error rather than an echo of the argument, which is how
// the push check below asks "does this remote-tracking ref exist yet".
std::string resolved_object_name(const std::string& spelling) {
    std::string out;
    if (!run_git_capture({"rev-parse", "--verify", spelling}, out)) return {};
    return trim_trailing_ws(out);
}

std::string short_sha(const std::string& sha) {
    return (sha.size() >= 7) ? sha.substr(0, 7) : sha;
}

}  // namespace

std::string history_checkpoint_title(const std::string& project_directory) {
    const std::size_t slash = project_directory.rfind('/');
    const std::string id    = (slash == std::string::npos)
                                  ? project_directory
                                  : project_directory.substr(slash + 1);
    return "Update " + id;
}

// THE ACT, in order: write the three sidecars into the piece's directory in the
// working tree, stage exactly those three paths, commit them under `Update
// <id>`, push. Every step is fenced to those three paths and to this one
// function.
//
// WHAT THE COMMIT CANNOT CARRY, and why the pathspec is not a nicety: `git
// commit -- <paths>` builds its tree from HEAD plus the named paths and ignores
// everything else the index holds, so foreign staged work in the repository — a
// source edit mid-session, anything at all — can never ride along on a
// checkpoint. The `git add` in front of it exists for one case the pathspec
// commit cannot cover alone: a sidecar the piece's directory did not previously
// carry is UNTRACKED, and a pathspec naming an untracked file is an error rather
// than an addition.
//
// WHAT REMAINS AFTER A FAILURE. The three files are written first and are NEVER
// rolled back: a commit that fails leaves them in the working tree — staged, if
// the `add` got that far — where `git status` shows them and a hand commit can
// still land them, and a WRITE that fails part-way leaves the files it had
// already written standing beside the one it could not. That is the honest shape: the bytes are the user's own state,
// not a temporary, and a failed act that swept them away would destroy the only
// copy of what the user asked to keep. It is also why the write failure and the
// commit failure are different outcomes.
//
// THE IDENTITY IS THE MACHINE'S. Author and committer come from the clone's own
// git config; this program embeds no name, no address and no credential, and the
// push carries none either (the remote's own ssh key or credential helper is the
// whole story). The push runs ssh in BATCH MODE so a key that would prompt fails
// in one line instead of blocking the GUI on a passphrase nothing can type.
GuiHistoryCommitOutcome commit_history_checkpoint(
    const std::string& project_directory, const std::string& base_name,
    const GuiHistoryNowSide& bytes) {
    const std::string title = history_checkpoint_title(project_directory);

    // kSidecarExtensions order, which is what pairs each text with its path.
    const std::string* texts[3] = {&bytes.warpmarkers_text,
                                   &bytes.phaseresetmarkers_text,
                                   &bytes.settings_text};
    std::vector<std::string> paths;
    paths.reserve(3);
    for (std::size_t e = 0; e < 3; ++e) {
        paths.push_back(project_directory + "/" + base_name +
                        kSidecarExtensions[e]);
    }

    // (a) The bytes. Through the same atomic writer a Ctrl+S uses — tmp, fsync,
    // rename — so a checkpoint is never half-written, and into a directory that
    // MUST ALREADY EXIST: nothing here creates one (a piece with no committed
    // history cannot open the mode at all, so its first checkpoint is a manual
    // act by design), and a missing directory simply fails the open below.
    for (std::size_t e = 0; e < 3; ++e) {
        const std::string absolute =
            std::string(kRepoRoot) + "/" + paths[e];
        if (!atomic_write_string_to_path(absolute, *texts[e])) {
            std::fprintf(stderr,
                         "warptempo_gui: Commit failed: could not write '%s'\n",
                         paths[e].c_str());
            return GuiHistoryCommitOutcome::WriteFailed;
        }
    }

    // THE ONE EXPECTED NON-FAILURE, named before it can be mistaken for one: if
    // the bytes just written match what HEAD already carries, `git status` sees
    // nothing and `git commit` would refuse with a message whose first line is
    // "On branch main" — true and useless. This is the state a user reaches by
    // committing twice, and the mode's own empty diff already says so. An
    // unrunnable git reads the same as an empty answer here, which is the
    // capture helper's standing convention and harmless: every read before this
    // line just succeeded, and a commit attempted past a git that cannot run
    // could only fail too.
    std::vector<std::string> status_args{"status", "--porcelain", "--"};
    for (const std::string& p : paths) status_args.push_back(p);
    std::string status_out;
    if (!run_git_capture(status_args, status_out)) {
        std::fprintf(stderr,
                     "warptempo_gui: Nothing to commit: the checkpoint already "
                     "carries these bytes\n");
        return GuiHistoryCommitOutcome::NothingToCommit;
    }

    const std::string before = resolved_object_name(kBranchRef);

    // (b) Stage, then commit — both pathspec-scoped to the same three paths.
    std::string              git_line;
    std::vector<std::string> add_args{"add", "--"};
    for (const std::string& p : paths) add_args.push_back(p);
    if (!run_git_mutate(add_args, git_line)) {
        std::fprintf(stderr, "warptempo_gui: Commit failed: could not run git\n");
        return GuiHistoryCommitOutcome::CommitFailed;
    }

    std::vector<std::string> commit_args{"commit", "-m", title, "--"};
    for (const std::string& p : paths) commit_args.push_back(p);
    if (!run_git_mutate(commit_args, git_line)) {
        std::fprintf(stderr, "warptempo_gui: Commit failed: could not run git\n");
        return GuiHistoryCommitOutcome::CommitFailed;
    }

    // THE VERDICT IS THE REPOSITORY'S, not the child's: HEAD moved or it did
    // not, which needs no exit status and cannot be fooled by output.
    const std::string after = resolved_object_name(kBranchRef);
    if (after.empty() || after == before) {
        std::fprintf(stderr, "warptempo_gui: Commit failed: %s\n",
                     git_line.empty() ? "git reported nothing"
                                      : git_line.c_str());
        return GuiHistoryCommitOutcome::CommitFailed;
    }

    // (c) The push, and the same kind of verdict: a push updates the LOCAL
    // remote-tracking ref as its last act, so `refs/remotes/origin/<branch>`
    // catching up to the new commit is the observation that it landed. The
    // branch NAME is needed only for that ref — the push itself sends `HEAD`,
    // the same branch every read in this module already walks — and a detached
    // HEAD, which has no name and no such ref, simply reports the push as
    // unlanded rather than guessing.
    std::string branch;
    {
        std::string raw;
        if (run_git_capture({"rev-parse", "--abbrev-ref", kBranchRef}, raw)) {
            branch = trim_trailing_ws(raw);
        }
    }
    std::string push_line;
    run_git_mutate({"-c", "core.sshCommand=ssh -o BatchMode=yes", "push",
                    "origin", kBranchRef},
                   push_line);
    const bool pushed =
        !branch.empty() && branch != "HEAD" &&
        resolved_object_name("refs/remotes/origin/" + branch) == after;

    if (pushed) {
        std::fprintf(stderr, "warptempo_gui: Committed and pushed %s \"%s\"\n",
                     short_sha(after).c_str(), title.c_str());
        return GuiHistoryCommitOutcome::Committed;
    }
    std::fprintf(stderr, "warptempo_gui: Committed %s \"%s\"\n",
                 short_sha(after).c_str(), title.c_str());
    std::fprintf(stderr, "warptempo_gui: Push failed: %s\n",
                 push_line.empty() ? "git reported nothing"
                                   : push_line.c_str());
    return GuiHistoryCommitOutcome::CommittedNotPushed;
}
