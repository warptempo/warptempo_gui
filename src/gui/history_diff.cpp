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
// THE ONLY SUBCOMMANDS THIS FEATURE EVER PASSES ARE `log`, `show`, `ls-tree`
// AND `remote get-url` — the recheck reads history and never writes it, and
// that constraint is meant to stay checkable by reading the call sites below
// rather than by trusting a runtime guard.
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

// THE MATCH IS BY FILE NAME, NOT BY FOLDER. The recheck does not know where in
// the tree the corpus lives, and deliberately so: it looks for the committed
// file whose BASENAME is one of this source's three sidecars, wherever that
// file sits. No directory name is hardcoded anywhere in this module, which is
// what makes it survive a corpus rename — past, or future — with no era
// knowledge to keep current.

// The directory part of a committed path, or "" for a file at the repo root
// (a legal place for a sidecar to sit, and its own distinct directory).
std::string directory_of(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return (slash == std::string::npos) ? std::string()
                                        : path.substr(0, slash);
}

std::string display_directory(const std::string& dir) {
    return dir.empty() ? std::string("(repository root)") : dir;
}

// The committed paths in a NUL-separated `ls-tree -z --name-only` listing
// whose BASENAME is `<base_name>.<one of the three extensions>`.
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
        reason = "No committed file on origin/main is named '" + base_name +
                 ".warpmarkers' or its two siblings";
        return false;
    }
    if (dirs.size() > 1) {
        std::sort(dirs.begin(), dirs.end());
        std::string list;
        for (std::size_t i = 0; i < dirs.size(); ++i) {
            if (i != 0) list += ", ";
            list += "'" + display_directory(dirs[i]) + "'";
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
    // directory: history is what this mode reads, so what is committed on
    // origin/main is the thing that decides whether there is any history to
    // read. A sidecar sitting on disk but never committed is correctly no
    // match, and one committed but since deleted from the checkout still is.
    std::string tip_listing;
    if (!run_git_capture({"ls-tree", "-r", "-z", "--name-only", "origin/main"},
                         tip_listing)) {
        return unavailable("Could not read the origin/main tree at " +
                           std::string(kRepoRoot));
    }
    std::string reason;
    if (!sole_directory_of(sidecar_paths_in_listing(tip_listing, base_name_),
                           base_name_, project_directory_, reason)) {
        return unavailable(std::move(reason));
    }

    // The commit walk is FOLDER- AND ERA-AGNOSTIC: one `:(glob)**/<base>.<ext>`
    // pathspec per sidecar, matching the basename at ANY depth — including the
    // repository root, since `**` matches zero path components as well as many
    // (verified against a root-level file: the glob pathspec returns exactly
    // the commit list the plain path does). So a commit that moved the corpus
    // is followed with no --follow and no knowledge of what the directory used
    // to be called, which is the whole point of matching by name.
    std::vector<std::string> log_args{"log", "-n",
                                      std::to_string(kCommitDepth),
                                      "--format=%H", "origin/main", "--"};
    for (const char* ext : kSidecarExtensions) {
        log_args.push_back(std::string(":(glob)**/") + base_name_ + ext);
    }
    std::string log_out;
    if (!run_git_capture(log_args, log_out)) {
        return unavailable("No commit on origin/main touches '" + base_name_ +
                           ".*'");
    }

    for (std::string& sha : split_lines(log_out)) {
        if (!sha.empty()) commits_.push_back(std::move(sha));
    }
    if (commits_.empty()) {
        return unavailable("No commit on origin/main touches '" + base_name_ +
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
