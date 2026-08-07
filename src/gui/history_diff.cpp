#include "history_diff.h"

#include "app_state.h"
#include "frame_format.h"
#include "history_prefetch.h"
#include "phaseresetmarkers.h"
#include "settings_io.h"
#include "warpmarkers.h"
#include "warpmarkers_parse.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>

// The child's environment is built from ours BEFORE the fork (see
// run_git_mutate), which needs the process environment by name.
extern "C" char** environ;

namespace {

// THE REPO IS A FIXED ABSOLUTE PATH. This is a single-laptop product with no
// portability shims — the same footing as the labwc/1920x1080 target
// assumptions — so the corpus lives where it lives and there is no fallback
// search, no environment variable, and no walk up from the binary. A missing
// path or a failing git there is UNAVAILABLE, reported once and dropped.
constexpr const char* kRepoRoot = "/home/b/.warptempo/warptempo_gui";

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
// `HEAD` is the spelling because it needs no name — it is whatever this clone
// has checked out — and the same word serves the tip listing and the history
// walk, so the mode's two readers cannot come to mean different things.
//
// THE COMMIT ACT DOES NOT USE THIS SPELLING FOR ITS OBSERVATIONS. It reads HEAD
// exactly once, to learn the branch NAME, and every source-side observation it
// makes afterwards is BOUND TO THAT NAME — all but one by naming
// `refs/heads/<that name>` outright, and the one that cannot (the pre-flight
// `git status`, which takes no ref) by VERIFYING that the name is still what git
// has checked out, through the `##` header status_of_paths already demanded as
// its ran-witness. Either way no post-capture observation can answer for another
// branch — because the act both observes and PUBLISHES, and a symbolic HEAD that
// moves under it would let the two name different branches
// (commit_history_checkpoint's `source_ref` owns the reasoning). The read-only
// mode has no such exposure: it publishes nothing, and a checkout under it simply
// shows the branch that is now checked out.
//
// The projects_repo guard is unaffected either way: it asks which REPOSITORY
// this clone is, not how fresh it is.
constexpr const char* kBranchRef = "HEAD";

// The three sidecars a source carries, in no significant order. A directory
// matches if it holds ANY of them under the source's base name — the
// architect's checkpoints are complete sets, but a partial one should still be
// FOUND rather than silently missed: the match answers where the piece lives,
// and the strict load gate is what then refuses a partial commit, at walk
// entry and at the `'` act alike.
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

// HOW LONG A MUTATION MAY HOLD THE GUI. The commit act runs on the Wayland
// thread, so every step of it is a frozen window while it lasts, and the three
// mutating steps are the only git this product runs that can block on something
// other than the local disk: a push talks to a network and can be black-holed by
// a route, a proxy or a credential helper, and any of the three can be delayed
// by a repository hook that never returns. Thirty seconds is chosen against the
// PUSH — the slowest legitimate step, a few kilobytes over ssh, which finishes
// in well under a second on a working link — so the deadline can only fire on
// something that is not going to finish. Expiry KILLS the child (we hold the
// pid) and reports the timeout as the step's failure line; the local `add` and
// `commit` share the constant rather than carrying a tighter one of their own,
// since a hook is exactly what would hang them and a second number would only be
// a second thing to justify.
constexpr int kMutateDeadlineMs = 30000;

// ---------------------------------------------------------------------------
// git plumbing
// ---------------------------------------------------------------------------

// A PATH IS NOT A PATHSPEC. Every argument after `--` on `status`, `add` and
// `commit` is a PATHSPEC, and a pathspec's `*`, `?` and `[...]` are wildcards
// even when the string came from a real committed file — so a source legitimately
// named `take*.wav` would hand `projects/x/take*.settings` to `git add`, which
// matches `take-old.settings` too and would stage and commit a file the
// checkpoint never meant to carry. The `:(literal)` long-form magic (gitglossary,
// "pathspec": "Wildcards in the pattern such as * or ? are treated as literal
// characters") turns the whole remainder back into the exact path it looks like.
//
// The prefix is unconditional rather than applied only to paths that look risky:
// a name with no metacharacter is its own literal, so wrapping it costs nothing
// and leaves no site to forget.
//
// NOT EVERY PATH ARGUMENT NEEDS THIS. `git show <sha>:<path>` takes a literal
// committed path and not a pathspec at all (nothing about it globs), and
// `ls-tree` here is given no path arguments whatsoever — both are left verbatim
// on purpose.
std::string literal_pathspec(const std::string& path) {
    return ":(literal)" + path;
}

// Escape a filesystem-derived string for interpolation into a `:(glob)` pattern,
// which the commit walk needs because its pattern is PART wildcard (the
// `projects/**/` lead) and part literal (this base name) — the one place where
// `:(literal)` cannot serve.
//
// Glob magic matches with wildmatch under FNM_PATHNAME rules, whose metacharacters
// are `*`, `?` and the `[...]` character class, with backslash the escape that
// makes the next byte literal. So those three plus the backslash itself are the
// whole set. A `]` needs none: it is ordinary text unless a bracket expression is
// already open, and the `[` that would open one is escaped here.
std::string escape_glob(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        if (c == '\\' || c == '*' || c == '?' || c == '[') out += '\\';
        out += c;
    }
    return out;
}

// THE EXEC SELF-PIPE — the one witness of "git never ran at all" that survives
// main()'s SIG_IGN SIGCHLD regime, and SHARED BY BOTH subprocess entry points
// below (the capture since 2026-08-04, the mutation since the round-2
// conversions: its own comment used to claim a verdict its implementation could
// not make, and sharing this was cheaper and honester than narrowing the claim).
//
// A pipe is opened with O_CLOEXEC on both ends. The child keeps its write end
// open across everything it does before exec and writes ONE byte on it only if
// execvp/execvpe RETURNS — that is, only if git could not be run at all. A
// successful exec closes the descriptor for it, so the parent's read gets EOF; a
// failed one gets the byte. The read needs NO waitpid and cannot hang: the only
// holder of the write end is the child, and it releases it either by exec'ing or
// by exiting immediately after the write, so the read terminates on every path.
// The child's own pre-exec failures (a dup2 that will not) write the byte too —
// "we did not get to run git" is exactly what they mean.
//
// THE PARENT READS IT AFTER THE OUTPUT PIPE REACHES EOF, which is the point at
// which the child has finished with stdout, so the byte — written before the
// child's own _exit — is already in the pipe by then.
struct ExecProbe {
    int fds[2] = {-1, -1};

    bool open_pipe() { return pipe2(fds, O_CLOEXEC) == 0; }

    // Child side. The read end goes at once; the write end carries the one byte
    // on every path out of the child that is NOT a successful exec.
    void child_close_read() { close(fds[0]); }
    void child_give_up() {
        const char    byte    = 1;
        const ssize_t ignored = write(fds[1], &byte, 1);
        (void)ignored;
        _exit(127);
    }

    void parent_close_write() { close(fds[1]); }

    // Parent side: true when the child said it could not exec. An unreadable
    // answer counts as failure — the safe side of this particular question,
    // since every caller's fallback is to trust a repository observation
    // instead. Closes the read end.
    bool parent_exec_failed() {
        bool failed = false;
        for (;;) {
            char          byte = 0;
            const ssize_t n    = read(fds[0], &byte, 1);
            if (n > 0) {
                failed = true;
                break;
            }
            if (n == 0) break;
            if (errno == EINTR) continue;
            failed = true;
            break;
        }
        close(fds[0]);
        return failed;
    }

    // The one path that does not ask: the mutation's deadline expired, the
    // child was killed, and the answer is already known to be failure.
    void parent_drop() { close(fds[0]); }
};

// Milliseconds on a clock that cannot jump — the deadline's own time base.
long long monotonic_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000 +
           static_cast<long long>(ts.tv_nsec) / 1000000;
}

// WHAT A CAPTURE ANSWERED — a distinction the boolean this used to return could
// not make. "The command failed" and "the command succeeded and said nothing"
// are the same empty string, and two callers are wrong when they are confused:
// a load-in-place would stage a sidecar a FAILED `show` invented as empty and
// replace
// the live store with a state the named commit never held, and the commit act's
// pre-flight would read a FAILED `status` as "nothing to commit" and tell the
// user the checkpoint already carries bytes that are in fact still uncommitted.
//
// Failed covers both shapes of not-having-answered: the fork/exec never ran the
// program at all (detected explicitly — see the self-pipe below), or it ran and
// reported a non-zero status where that status is obtainable.
//
// WHAT `Ran` DOES NOT PROMISE, stated because two callers must know it: under
// main()'s SIG_IGN SIGCHLD the exit status is usually NOT obtainable (waitpid
// returns ECHILD), so `Ran` means "git itself was executed" rather than "git
// succeeded". A git that started and then failed with nothing on stdout — a
// rejected pathspec, a locked index — lands in Ran with an empty string. So a
// caller whose question is "did this succeed" needs a witness IN THE OUTPUT, and
// both of them have one: the commit act's status probe asks with `--branch`,
// whose header line is emitted only on a successful run, and the
// load-in-place
// cross-checks each blob against the byte count the tree listing stated. Neither
// rests on this enum alone; what the enum adds is the case no output-shaped
// witness could ever cover, that git never ran.
enum class GitCapture {
    Failed,
    Ran,  // git was executed; `out` is its whole stdout, possibly empty
};

// Run `git -C <repo> <args...>` and capture its stdout.
//
// THE ONLY SUBCOMMANDS THIS ENTRY POINT EVER PASSES ARE `log`, `show`,
// `ls-tree`, `rev-parse`, `status`, `rev-list`, `diff-tree` AND `remote get-url`
// (rev-parse joined 2026-08-04 with the load-in-place-from-a-commit path's
// spelling
// resolution, status the same day with the commit act's pre-flight probe, and
// rev-list + diff-tree with the same act's ATTRIBUTION verdict — the bounded
// `before..HEAD` walk, each candidate's own changed-path list, and rev-list
// again for the push verdict's containment count) — all of them reads, and that
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
// DID IT RUN, AND WHAT DID IT SAY — two questions, answered separately (see
// GitCapture). main() sets SIGCHLD to SIG_IGN so the kernel auto-reaps the
// fire-and-forget audio players, which also means waitpid() here normally fails
// with ECHILD and the exit status is simply not obtainable; it is still honoured
// when it does arrive (a future session leaving the default disposition), and
// the EXEC SELF-PIPE below covers the case that regime would otherwise hide
// completely.
//
// THE SELF-PIPE covers the case no output-shaped witness could: that git never
// ran. Its mechanism lives at ExecProbe above, which the mutating entry point
// shares.
GitCapture run_git_capture(const std::vector<std::string>& args,
                           std::string&                    out) {
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
    if (pipe(fds) != 0) return GitCapture::Failed;
    ExecProbe probe;
    if (!probe.open_pipe()) {
        close(fds[0]);
        close(fds[1]);
        return GitCapture::Failed;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        probe.parent_close_write();
        probe.parent_drop();
        return GitCapture::Failed;
    }
    if (pid == 0) {
        // Child: stdout to the pipe, stderr discarded, then exec immediately.
        // Every path out of here that is NOT a successful exec says so on the
        // self-pipe first.
        close(fds[0]);
        probe.child_close_read();
        if (dup2(fds[1], STDOUT_FILENO) < 0) probe.child_give_up();
        close(fds[1]);
        const int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execvp("git", argv.data());
        probe.child_give_up();
        _exit(127);  // child_give_up() does not return; the compiler's proof
    }

    close(fds[1]);
    probe.parent_close_write();
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

    // The exec verdict (ExecProbe owns the mechanism).
    const bool exec_failed = probe.parent_exec_failed();

    int   status = 0;
    pid_t w      = 0;
    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR);
    if (exec_failed ||
        (w == pid && (!WIFEXITED(status) || WEXITSTATUS(status) != 0))) {
        out.clear();
        return GitCapture::Failed;
    }

    return GitCapture::Ran;
}

// THE ORDINARY READING — ran AND said something — which is what every caller
// wants whose question is answered by the output itself: a `log` with no commits,
// a `rev-parse` that resolved nothing and an `ls-tree` of a tree with no matching
// path are all "no history here" and all correctly fail this. The two callers
// whose question is NOT the output's content (the load-in-place's blob reads
// and the
// commit act's status pre-flight) call run_git_capture directly and read the
// outcome.
bool git_output(const std::vector<std::string>& args, std::string& out) {
    return run_git_capture(args, out) == GitCapture::Ran && !out.empty();
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
// capture helper documents). So this reports only that the child was STARTED AND
// RAN TO ITS OWN END — false means it could not be STARTED (the shared ExecProbe
// says so; before the round-2 conversion this comment claimed that case while the
// code returned true for it) or that it outlived the deadline below and was
// killed.
//
// AND THE RETURN IS ADVISORY WHATEVER IT SAYS. Every caller decides the OUTCOME
// by OBSERVING THE REPOSITORY afterwards and NEVER by this boolean: the commit
// act's attribution walk for the commit, the remote-tracking ref for the push.
// The rule is not a preference — git updates HEAD BEFORE it runs `post-commit`,
// so a hook that hangs past the deadline gets the child killed here over a
// checkpoint that already exists, and a caller that believed the transport would
// call that landed commit a failure and never push it. What this value is good
// for is DIAGNOSTICS: `first_line` is git's own account of what went wrong, and
// it rides along on the failure the observation reaches.
//
// STDOUT AND STDERR SHARE ONE PIPE, so `first_line` is git's own first non-empty
// line whichever stream it chose (`commit` reports on stdout, `push` on stderr).
//
// NOTHING HERE MAY BLOCK THE GUI WITHOUT END, and three things make that true
// rather than one. STDIN IS /dev/null and the push's call site adds ssh's batch
// mode, which between them turn the ordinary credential question into a
// one-line failure. THE ENVIRONMENT IS NON-INTERACTIVE: `GIT_TERMINAL_PROMPT=0`
// is git's own general answer, covering the askpass and /dev/tty routes an
// HTTPS remote can take that a redirected stdin does not touch. And a DEADLINE
// (kMutateDeadlineMs) covers everything neither of those can reach — a
// black-holed route, a hanging proxy or credential helper, a hook that never
// returns: the read loop polls, and expiry SIGKILLs the child (we hold the pid)
// and returns failure with the timeout as `first_line`.
//
// THE ENVIRONMENT IS BUILT BEFORE THE FORK, which is not a style choice. Between
// fork and exec only async-signal-safe calls are legal, and setenv allocates —
// so the child gets a finished envp to hand straight to execvpe and does no work
// of its own. Any inherited GIT_TERMINAL_PROMPT is dropped rather than shadowed,
// so there is exactly one such entry and no question of which one is read.
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

    // The child's environment: ours, minus any GIT_TERMINAL_PROMPT it carried,
    // plus our own. Built here so the child allocates nothing.
    constexpr std::string_view kPromptKey = "GIT_TERMINAL_PROMPT=";
    std::vector<std::string>   env_storage;
    for (char** e = environ; e != nullptr && *e != nullptr; ++e) {
        const std::string_view entry(*e);
        if (entry.size() >= kPromptKey.size() &&
            entry.substr(0, kPromptKey.size()) == kPromptKey) {
            continue;
        }
        env_storage.emplace_back(*e);
    }
    env_storage.emplace_back(std::string(kPromptKey) + "0");
    std::vector<char*> envp;
    envp.reserve(env_storage.size() + 1);
    for (std::string& s : env_storage) envp.push_back(s.data());
    envp.push_back(nullptr);

    int fds[2];
    if (pipe(fds) != 0) return false;
    ExecProbe probe;
    if (!probe.open_pipe()) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        probe.parent_close_write();
        probe.parent_drop();
        return false;
    }
    if (pid == 0) {
        // Child: BOTH output streams to the pipe, stdin to /dev/null, then exec.
        //
        // ITS OWN PROCESS GROUP FIRST, so the deadline below has something to
        // kill that covers the whole act. Git is not a leaf: a push spawns ssh,
        // and any of the three can run a hook, so killing the git process alone
        // would leave exactly the thing that was hanging — the ssh, the
        // credential helper, the hook — orphaned and still holding whatever it
        // was holding. The child leads the group, so its pid IS the group id and
        // the parent needs no second handle. (setpgid is a bare syscall, which is
        // what makes it legal on this side of the fork.)
        setpgid(0, 0);
        close(fds[0]);
        probe.child_close_read();
        if (dup2(fds[1], STDOUT_FILENO) < 0) probe.child_give_up();
        if (dup2(fds[1], STDERR_FILENO) < 0) probe.child_give_up();
        close(fds[1]);
        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        execvpe("git", argv.data(), envp.data());
        probe.child_give_up();
        _exit(127);  // child_give_up() does not return; the compiler's proof
    }

    close(fds[1]);
    probe.parent_close_write();
    std::string     out;
    char            buf[4096];
    const long long started = monotonic_ms();
    bool            expired = false;
    for (;;) {
        const long long left = kMutateDeadlineMs - (monotonic_ms() - started);
        if (left <= 0) {
            expired = true;
            break;
        }
        struct pollfd pfd{};
        pfd.fd     = fds[0];
        pfd.events = POLLIN;
        const int pr = poll(&pfd, 1, static_cast<int>(left));
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) {
            expired = true;
            break;
        }
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

    if (expired) {
        // The child is still running and still holds whatever it was blocked on.
        // SIGKILL is the right signal here — there is nothing for it to clean up
        // that a half-finished git would not clean up better on its next run,
        // and TERM is what a hung credential helper is most likely already
        // ignoring. THE WHOLE GROUP goes (the negated pid), not the git process
        // alone: see the setpgid above. Under SIG_IGN the kernel reaps it.
        kill(-pid, SIGKILL);
        probe.parent_drop();  // the answer is already known: this is a failure
        first_line = "Timed out after " +
                     std::to_string(kMutateDeadlineMs / 1000) +
                     " seconds and was killed";
        return false;
    }

    // COULD IT BE STARTED AT ALL — the question the exit status cannot answer
    // here, and the one this function's `false` has always claimed to cover.
    if (probe.parent_exec_failed()) {
        first_line = "Git could not be started";
        return false;
    }

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

// The seven-character spelling every user-facing line uses for a commit.
std::string short_sha(const std::string& sha) {
    return (sha.size() >= 7) ? sha.substr(0, 7) : sha;
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

// ONE MATCHED ROW OF A TREE LISTING: where the blob sits and how many bytes the
// object database says it is. THE SIZE IS THE LOAD-IN-PLACE'S CROSS-CHECK —
// `git show`
// hands back a byte string, and a string that came up SHORT (a killed child, a
// read that lost its tail) is otherwise indistinguishable from the file's own
// contents; the tree already knows the true length, so carrying it costs one
// listing flag and closes the gap. -1 means the listing did not state one, which
// no blob row produces.
struct GuiHistoryTreeEntry {
    std::string path;
    long long   size = -1;
};

// The committed rows in a NUL-separated `ls-tree -z -l` listing that sit UNDER
// `projects/` and whose BASENAME is
// `<base_name>.<one of the three extensions>`.
//
// THE LISTING IS `-l`, NOT `--name-only`, so each record is
// `<mode> SP <type> SP <object> SP<pad><size> TAB <path>` and the size arrives
// with the path it belongs to — one subprocess still, one more field parsed.
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
std::vector<GuiHistoryTreeEntry> sidecar_entries_in_listing(
    const std::string& listing, const std::string& base_name) {
    std::vector<GuiHistoryTreeEntry> hits;
    for (const std::string& record : split_on(listing, '\0')) {
        if (record.empty()) continue;
        // The TAB is the one separator the format guarantees cannot occur in the
        // metadata, and a path may legally contain anything but NUL — including
        // a tab — so the FIRST tab is the split and everything past it is path.
        const std::size_t tab = record.find('\t');
        if (tab == std::string::npos) continue;
        const std::string path = record.substr(tab + 1);
        // `<mode> <type> <object> <size>` — the size is the last whitespace-
        // separated field of the head, right-aligned with blanks.
        long long         size      = -1;
        const std::string head      = record.substr(0, tab);
        const std::size_t size_end  = head.find_last_not_of(" ");
        if (size_end != std::string::npos) {
            const std::size_t before = head.find_last_of(" ", size_end);
            const std::string tok =
                (before == std::string::npos)
                    ? head.substr(0, size_end + 1)
                    : head.substr(before + 1, size_end - before);
            // A tree row's size is "-" and a blob's is decimal; only the latter
            // parses, which is exactly the distinction wanted. (`-r` without
            // `-t` lists no trees anyway.)
            if (!tok.empty() &&
                tok.find_first_not_of("0123456789") == std::string::npos) {
                size = std::strtoll(tok.c_str(), nullptr, 10);
            }
        }
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
                GuiHistoryTreeEntry e;
                e.path = path;
                e.size = size;
                hits.push_back(std::move(e));
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
bool sole_directory_of(const std::vector<GuiHistoryTreeEntry>& paths,
                       const std::string&                      base_name,
                       std::string&                            out_dir,
                       std::string&                            reason) {
    std::vector<std::string> dirs;
    for (const GuiHistoryTreeEntry& e : paths) {
        std::string d = directory_of(e.path);
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

// IS THIS CLONE THE CONFIGURED PROJECTS HOME — asked of the clone's FETCH url
// and of EVERY EFFECTIVE PUSH url, both normalized against the setting. False
// with `reason` set names the first disagreement in the user's own spellings.
//
// `git remote get-url origin` answers with the FETCH url, and a push does not
// have to use it: `remote.origin.pushurl` overrides it and may be set more than
// once, so a clone whose fetch url is the configured projects home can still
// publish to a fork, a mirror or an unrelated repository — under a confirmation
// prompt naming the configured one. So both are asked, and every url that comes
// back must normalize equal to the setting. A repo with no pushurl configured
// answers the push query with its fetch url, which makes the fetch-only check a
// strict subset of this one rather than a case beside it.
//
// TWO CALLERS, TWO DIFFERENT QUESTIONS, which is why this is a function rather
// than a step of init(). init() asks it as THE MODE'S GATE — may this session
// read and offer to write this history at all — and the commit act asks it again
// IMMEDIATELY BEFORE THE PUSH, at the MUTATING BOUNDARY: the gate's answer is
// minutes old by then, and `remote.origin.pushurl` is a config value any
// terminal (or any hook this act itself just ran) can change while the mode
// stands.
//
// AND THE SECOND ASKING TAKES ITS ANSWER WITH IT. Asking again close to the push
// only NARROWS the window; what closes it is that the URL validated here is the
// URL the push consumes, so `destination` hands the winning spelling back and
// the push pins it into its own child rather than letting a new git process
// re-resolve the mutable name `origin` (the pin's mechanics are at the push
// site). `destination` is the FIRST effective push URL — the head of the very
// list validated just above, so every candidate destination had to normalize
// equal to the setting before any one of them could be named. An empty list has
// nothing to pin and is refused here rather than left to the push.
//
// THE ONE REWRITE THIS CANNOT SEE, recorded because it is a real hole and a
// pre-existing one: `url.<base>.insteadOf` / `pushInsteadOf` rewrite a URL when
// git USES it, and `remote get-url` reports the raw config value (verified live
// on git 2.55 — get-url printed the configured URL while the push carrying the
// same spelling went to the rewritten one). So a rewrite rule can still move any
// destination this guard blesses, pinned or not. Closing it needs a different
// question asked of the config (an enumeration of the url.* rules), which is a
// mechanism this arc has not been asked for.
bool clone_is_projects_home(const std::string& projects_repo,
                            std::string&       reason,
                            std::string*       destination = nullptr) {
    reason.clear();
    if (destination != nullptr) destination->clear();
    const std::string setting_norm = normalize_repo_url(projects_repo);
    if (setting_norm.empty()) {
        reason = "The projects_repo setting is empty";
        return false;
    }

    std::string remote_raw;
    if (!git_output({"remote", "get-url", "origin"}, remote_raw)) {
        reason = "The clone at " + std::string(kRepoRoot) +
                 " has no 'origin' remote";
        return false;
    }
    if (normalize_repo_url(remote_raw) != setting_norm) {
        reason = "The projects_repo setting names '" + projects_repo +
                 "' but the clone at " + std::string(kRepoRoot) +
                 " has origin '" + trim_trailing_ws(remote_raw) + "'";
        return false;
    }

    std::string push_raw;
    if (!git_output({"remote", "get-url", "--push", "--all", "origin"},
                    push_raw)) {
        reason = "The clone at " + std::string(kRepoRoot) +
                 " states no push URL for 'origin'";
        return false;
    }
    std::string first_push_url;
    for (const std::string& line : split_lines(push_raw)) {
        const std::string one = trim_trailing_ws(line);
        if (one.empty()) continue;
        if (normalize_repo_url(one) != setting_norm) {
            reason = "The projects_repo setting names '" + projects_repo +
                     "' but the clone at " + std::string(kRepoRoot) +
                     " pushes 'origin' to '" + one + "'";
            return false;
        }
        if (first_push_url.empty()) first_push_url = one;
    }
    if (first_push_url.empty()) {
        reason = "The clone at " + std::string(kRepoRoot) +
                 " states no usable push URL for 'origin'";
        return false;
    }
    if (destination != nullptr) *destination = first_push_url;
    return true;
}

// ---------------------------------------------------------------------------
// per-commit snapshot
// ---------------------------------------------------------------------------

// One commit's committed path and blob size for each of the three sidecars,
// empty/-1 where that commit carries none. Indexed to match kSidecarExtensions.
//
// `ambiguous` is a REFUSAL, not a variant of "carries none": this commit's tree
// holds the base name in several directories and none of them is the one HEAD
// matched, so which piece the blobs belong to has no answer. All three paths
// are empty in that state and read_commit_sidecars refuses on it — which the
// walk's load gate counts as ineligibility (an ambiguous commit never enters
// the walk) and the `'` act prints as its own refusal, the arm a pasted
// spelling naming a commit outside the walk keeps live.
struct GuiHistoryCommitPaths {
    std::string path[3];
    long long   size[3] = {-1, -1, -1};
    bool        ambiguous = false;
};

// Resolve where this commit kept the sidecars, from THAT COMMIT'S OWN TREE —
// which is what replaces knowing the era's directory name. ONE subprocess per
// commit, not one per file: the whole listing is fetched once and all three
// extensions are picked out of it.
//
// A COMMIT MAY CARRY THE BASE NAME IN SEVERAL DIRECTORIES, and this is where
// cross-piece confusion would enter if the ambiguity were resolved by a guess.
// The tip tree's ambiguity is refused outright at init, but history is wider than
// the tip: a `projects/B/song.*` that existed in an older era, before B was
// renamed or removed, still sits in those old trees beside today's
// `projects/A/song.*`, and the walk's basename pathspec pulls commits that touched
// either into one list. The old rule — most siblings, ties lexicographic — would
// then silently display B's state as A's and let `'` load it in place.
//
// THE RULE IS HEAD'S OWN DIRECTORY FIRST. `head_directory` is what init matched
// on the tip tree, the one directory this session means by "this piece"; if this
// commit's tree carries the base name there, that is the answer whatever else it
// carries. Failing that, a SINGLE candidate directory is unambiguous and is taken
// (which is every ordinary pre-rename era: the piece sat somewhere else and
// nowhere else). Anything left is genuinely ambiguous and REFUSES, through
// read_commit_sidecars: the walk's load gate drops such a commit at entry —
// it is never shown at all — and the `'` act, whose pasted spelling can name
// a commit outside the walk, refuses it with its own line.
GuiHistoryCommitPaths resolve_commit_paths(const std::string& sha,
                                           const std::string& base_name,
                                           const std::string& head_directory) {
    GuiHistoryCommitPaths out;
    std::string           listing;
    if (!git_output({"ls-tree", "-r", "-z", "-l", sha}, listing)) return out;
    const std::vector<GuiHistoryTreeEntry> hits =
        sidecar_entries_in_listing(listing, base_name);
    if (hits.empty()) return out;

    std::vector<std::string> dirs;
    for (const GuiHistoryTreeEntry& e : hits) {
        const std::string d = directory_of(e.path);
        if (std::find(dirs.begin(), dirs.end(), d) == dirs.end()) {
            dirs.push_back(d);
        }
    }

    std::string chosen;
    if (std::find(dirs.begin(), dirs.end(), head_directory) != dirs.end()) {
        chosen = head_directory;
    } else if (dirs.size() == 1) {
        chosen = dirs.front();
    } else {
        out.ambiguous = true;
        return out;
    }

    for (const GuiHistoryTreeEntry& hit : hits) {
        if (directory_of(hit.path) != chosen) continue;
        for (std::size_t e = 0; e < 3; ++e) {
            const std::string leaf = base_name + kSidecarExtensions[e];
            if (hit.path.size() >= leaf.size() &&
                hit.path.compare(hit.path.size() - leaf.size(), leaf.size(),
                                 leaf) == 0) {
                out.path[e] = hit.path;
                out.size[e] = hit.size;
            }
        }
    }
    return out;
}

// The committed bytes at one resolved path. `git show <rev>:<path>` takes a
// LITERAL committed path — relative to the repo root, one argv element, and NOT a
// pathspec, so nothing about it globs or follows a rename; the path came from
// this commit's own tree, so it is already the spelling that commit uses.
//
// FALSE MEANS THE READ DID NOT HAPPEN — git could not be run, or ran and
// failed. Every caller (read_commit_sidecars' blob reads, the commit act's
// byte confirmation) hands in a path its commit's own tree listed, so there is
// no empty-path case: a missing file is decided BEFORE this read, never read
// as empty bytes. (The lenient everything-added reading the display side once
// had died with the walk's load gate, 2026-08-04.)
bool read_snapshot_at(const std::string& sha, const std::string& path,
                      std::string& out) {
    out.clear();
    return run_git_capture({"show", sha + ":" + path}, out) == GitCapture::Ran;
}

}  // namespace

bool read_commit_sidecars(const std::string&        spelling,
                          const std::string&        base_name,
                          const std::string&        head_directory,
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
    if (!git_output({"rev-parse", "--verify", spelling + "^{commit}"}, raw)) {
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
    const GuiHistoryCommitPaths paths =
        resolve_commit_paths(sha, base_name, head_directory);
    if (paths.ambiguous) {
        reason = "commit " + short_sha(sha) + " carries '" + base_name +
                 ".*' in more than one directory, so which piece it names has "
                 "no answer";
        return false;
    }
    out.warpmarkers.path       = paths.path[0];
    out.phaseresetmarkers.path = paths.path[1];
    out.settings.path          = paths.path[2];

    // THE BYTES, AND THE PROOF THEY ARE ALL OF THEM. A `show` that could not run
    // hands back an empty string, and an empty sidecar is a perfectly VALID whole
    // file in both marker grammars — so without a second witness the
    // load-in-place would
    // stage that emptiness, pass every strict loader, and replace the live store
    // with a state the commit never held. The tree listing is that witness: it
    // stated each blob's true length, so a read that came back short (a killed
    // child, a lost tail) or empty against a non-zero size is caught here rather
    // than believed. A commit the tree says carries nothing has no size to check
    // and reaches the caller's own partial-commit refusal.
    GuiHistorySidecarBlob* blobs[3] = {&out.warpmarkers, &out.phaseresetmarkers,
                                       &out.settings};
    for (std::size_t e = 0; e < 3; ++e) {
        if (paths.path[e].empty()) continue;
        if (!read_snapshot_at(sha, paths.path[e], blobs[e]->text)) {
            reason = "could not read '" + paths.path[e] + "' at commit " +
                     short_sha(sha);
            return false;
        }
        if (paths.size[e] >= 0 &&
            static_cast<long long>(blobs[e]->text.size()) != paths.size[e]) {
            reason = "'" + paths.path[e] + "' at commit " + short_sha(sha) +
                     " read back " + std::to_string(blobs[e]->text.size()) +
                     " bytes where the tree lists " +
                     std::to_string(paths.size[e]);
            return false;
        }
    }
    return true;
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

}  // namespace

// The gate's contract and the reason it is ONE predicate live at the header
// declaration. The body is the `'` act's own validation sequence, moved here
// whole when the walk became load-gated (2026-08-04) so both askers run the
// same bytes.
bool load_commit_sidecars_strict(const std::string&    spelling,
                                 const std::string&    base_name,
                                 const std::string&    head_directory,
                                 GuiHistoryCommitLoad& out,
                                 std::string&          reason) {
    out = GuiHistoryCommitLoad{};
    reason.clear();

    if (!read_commit_sidecars(spelling, base_name, head_directory,
                              out.sidecars, reason)) {
        return false;
    }
    const GuiHistoryCommitSidecars& snap = out.sidecars;

    // A PARTIAL COMMIT IS A REFUSAL: a load-in-place is a whole-state replace,
    // and inheriting two files from the commit and the third from nowhere
    // would compose a state no checkpoint ever was. (For the walk the same
    // refusal is simple ineligibility: a checkpoint that cannot be loaded is
    // not stepped to.)
    auto missing = [&](const char* ext) {
        reason = "commit " + snap.sha + " carries no '" + base_name + ext + "'";
        return false;
    };
    if (snap.warpmarkers.path.empty())       return missing(".warpmarkers");
    if (snap.phaseresetmarkers.path.empty()) return missing(".phaseresetmarkers");
    if (snap.settings.path.empty())          return missing(".settings");

    // THE COMMITTED BYTES REACH THE LOADERS THROUGH A SCRATCH DIRECTORY,
    // because all three whole-file entry points take a PATH and open the file
    // themselves (read_settings_file, GuiWarpMarkers::load,
    // GuiPhaseResetMarkers::load) and all three live in the FROZEN parser, so
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
        reason = "no temporary directory available: " + ec.message();
        return false;
    }
    ScratchDirGuard guard(scratch);
    std::filesystem::create_directories(scratch, ec);
    if (ec) {
        reason = "could not create '" + scratch.string() + "': " + ec.message();
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
        reason = "could not stage '" + blob.path + "' from commit " + snap.sha;
        return false;
    };
    std::filesystem::path settings_file, warp_file, phase_reset_file;
    if (!stage(snap.settings, ".settings", settings_file))       return false;
    if (!stage(snap.warpmarkers, ".warpmarkers", warp_file))     return false;
    if (!stage(snap.phaseresetmarkers, ".phaseresetmarkers",
               phase_reset_file))                                return false;

    // The three STRICT WHOLE-FILE LOADERS are the judges, in the render-entry
    // load-in-place's own order, each refusal naming the committed path and
    // the SHA. First error only, by construction: every arm returns.
    auto settings = read_settings_file(settings_file.string());
    if (!settings) {
        reason = "invalid settings in '" + snap.settings.path + "' at commit " +
                 snap.sha + ": " + settings.error();
        return false;
    }
    out.settings = std::move(*settings);

    {
        GuiWarpMarkers m;
        auto r = m.load(warp_file.string());
        if (!r) {
            reason = "invalid warp markers in '" + snap.warpmarkers.path +
                     "' at commit " + snap.sha + ": " + r.error();
            return false;
        }
        out.warp_markers = m.markers();
    }
    {
        GuiPhaseResetMarkers t;
        auto r = t.load(phase_reset_file.string());
        if (!r) {
            reason = "invalid phase reset markers in '" +
                     snap.phaseresetmarkers.path + "' at commit " + snap.sha +
                     ": " + r.error();
            return false;
        }
        out.phase_reset_markers = t.markers();
    }
    return true;
}

// THE SETTINGS WRITER'S GUI HALF, HELD BY VALUE — the storable form of the
// call-shaped NonEngineSettingsSnapshot (which borrows a ViewState pair and six
// strings). It is opaque in the header because ViewState is app_state.h's and
// this module is included BY that header; nothing outside this file needs its
// shape.
struct GuiHistoryGuiSide {
    ViewState   tab_a;
    ViewState   tab_b;
    bool        follow              = false;
    char        active_audio_view   = 'S';
    char        active_markers_view = 'W';
    char        active_tab_view     = 'A';
    float       playback_speed      = 1.0f;
    int         gui_scale           = 100;
    std::string audio_player;
    std::string projects_repo;
    std::string libm_hash;
    std::string libmvec_hash;
    std::string fftw3_hash;
    std::string fftw3_threads_hash;
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

    gui->follow              = app.follow_mode;
    gui->active_audio_view   = app.active_audio_view;
    gui->active_markers_view = app.active_markers_view;
    gui->active_tab_view     = app.active_tab_view;
    gui->playback_speed      = app.playback_speed;
    gui->gui_scale           = app.gui_scale;
    gui->audio_player        = app.audio_player;
    gui->projects_repo       = app.projects_repo;
    gui->libm_hash           = app.libm_hash;
    gui->libmvec_hash        = app.libmvec_hash;
    gui->fftw3_hash          = app.fftw3_hash;
    gui->fftw3_threads_hash  = app.fftw3_threads_hash;
    return gui;
}

std::string format_history_settings_text(const GuiHistoryGuiSide& gui,
                                         const EngineSettings&    engine) {
    const NonEngineSettingsSnapshot snap{
        gui.tab_a, gui.tab_b, gui.follow,
        gui.active_audio_view, gui.active_markers_view, gui.active_tab_view,
        gui.playback_speed, gui.gui_scale, gui.audio_player,
        gui.projects_repo,
        gui.libm_hash, gui.libmvec_hash,
        gui.fftw3_hash, gui.fftw3_threads_hash};
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

// THE WALK'S CHEAP HALF (the contract is at the declaration). It answers WHERE
// THE PIECE LIVES or why it cannot be found, in two git calls and no strict
// load, and it PRINTS NOTHING: the caller decides whether this is a refusal the
// user is watching for (GuiHistoryDiff::init's one stderr line) or a background
// run's own finding, which the store simply keeps until an entry asks.
GuiHistoryWalkHeader resolve_history_walk_header(
        const std::string& source_audio_path,
        const std::string& projects_repo) {
    GuiHistoryWalkHeader h;

    // Every failure arm lands here: the reason, and the whole header left in
    // its documented empty shape whatever step got as far as filling in (the
    // tree match runs after the base name is derived, so a late refusal has
    // something to clear).
    auto unavailable = [&h](std::string why) {
        h.ok = false;
        h.unavailable_reason = std::move(why);
        h.base_name.clear();
        h.project_directory.clear();
        return h;
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
    //
    // IT VALIDATES EVERY EFFECTIVE PUSH DESTINATION, not the fetch URL alone —
    // through clone_is_projects_home, whose comment owns the whole rule and
    // which the COMMIT ACT asks again immediately before its push. THIS SITE IS
    // THE MODE'S GATE; that one is the mutating boundary. Two askings because
    // the config can move between them, one owner because the question is one.
    std::string guard_reason;
    if (!clone_is_projects_home(projects_repo, guard_reason)) {
        return unavailable(std::move(guard_reason));
    }

    // The sidecar base name is the source's own stem — the single derivation
    // rule the loader uses when it builds <base>.warpmarkers and its two
    // siblings beside the WAV (file_loader.cpp's companion-file block). The
    // corpus names its files by exactly that, so mirroring the rule is what
    // makes the filename match work on names full of periods and commas.
    if (source_audio_path.empty()) {
        return unavailable("No source is loaded");
    }
    h.base_name = std::filesystem::path(source_audio_path).stem().string();
    if (h.base_name.empty()) {
        return unavailable("The source path has no base name: " +
                           source_audio_path);
    }

    // THE MATCH IS RESOLVED AGAINST THE COMMITTED TREE, not the working
    // directory: history is what this mode reads, so what is committed on the
    // branch is the thing that decides whether there is any history to read. A
    // sidecar sitting on disk but never committed is correctly no match, and one
    // committed but since deleted from the checkout still is. (The branch is the
    // LOCAL one — kBranchRef's paragraph owns why.)
    std::string tip_listing;
    if (!git_output({"ls-tree", "-r", "-z", "-l", kBranchRef}, tip_listing)) {
        return unavailable("Could not read the committed tree at " +
                           std::string(kRepoRoot));
    }
    std::string reason;
    if (!sole_directory_of(sidecar_entries_in_listing(tip_listing, h.base_name),
                           h.base_name, h.project_directory, reason)) {
        return unavailable(std::move(reason));
    }

    h.ok = true;
    return h;
}

std::string read_history_branch_tip_sha() {
    std::string out;
    if (!git_output({"rev-parse", "--verify", "--quiet",
                     std::string(kBranchRef) + "^{commit}"}, out)) {
        return std::string();
    }
    // One line, trailing newline and all.
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }
    return out;
}

void scan_history_walk(
        const std::string& source_audio_path, const std::string& projects_repo,
        const std::function<bool()>&                           abandoned,
        const std::function<void(GuiHistoryWalkHeader)>&       on_header,
        const std::function<void(GuiHistoryCommitSidecars)>&   on_member,
        const std::function<void(int candidates, int hidden)>& on_done) {
    GuiHistoryWalkHeader header =
        resolve_history_walk_header(source_audio_path, projects_repo);
    const std::string base_name         = header.base_name;
    const std::string project_directory = header.project_directory;
    const bool        ok                = header.ok;
    on_header(std::move(header));
    if (!ok) {
        // A run whose header refuses is FINISHED, not merely stopped: the DONE
        // is what tells the store there is nothing more coming, and an entry
        // reads the header's own reason rather than these zeroes.
        on_done(0, 0);
        return;
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
    //
    // THE BASE NAME IS ESCAPED INTO THE PATTERN (escape_glob): the `projects/**/`
    // lead is a real wildcard and the name is not, and a source legitimately
    // called `take*.wav` would otherwise widen the walk to every `take<anything>`
    // sidecar in the corpus. This is the one pathspec here that cannot simply be
    // `:(literal)` — glob magic and literal magic are mutually exclusive, and the
    // walk needs the glob half.
    //
    // AND IT IS UNCAPPED (2026-08-07): no `-n` term, so the pathspec walk
    // reaches the piece's first checkpoint. Everything else about it is
    // unchanged.
    const std::string escaped_base = escape_glob(base_name);
    std::vector<std::string> log_args{"log", "--format=%H", kBranchRef, "--"};
    for (const char* ext : kSidecarExtensions) {
        log_args.push_back(std::string(":(glob)") +
                           std::string(kProjectsPrefix) + "**/" + escaped_base +
                           ext);
    }
    std::string log_out;
    std::vector<std::string> candidates;
    if (git_output(log_args, log_out)) {
        for (std::string& sha : split_lines(log_out)) {
            if (!sha.empty()) candidates.push_back(std::move(sha));
        }
    }
    // A `log` that ran and said nothing and a `log` that could not run are the
    // same answer here — no candidate — and the entry's own "No commit touches"
    // line is what states it. The count rides out in the DONE below.
    if (candidates.empty()) {
        on_done(0, 0);
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
    // runs git again.
    //
    // EACH ELIGIBLE MEMBER IS PUBLISHED THE MOMENT IT PASSES (2026-08-07): the
    // gate is the expensive step and it is per candidate, so handing the result
    // over one at a time is what lets a view opened mid-scan show the newest
    // checkpoints while the older ones are still being read. The loop is
    // otherwise the eager one it always was.
    //
    // THE ABANDON CHECK IS THE LOOP'S OWN TOP, and the finest grain that costs
    // nothing: one candidate is a `rev-parse`, an `ls-tree`, three `show`s and
    // three strict loads of tiny files, so a supersede or a quit waits out at
    // most that.
    int hidden = 0;
    int done   = 0;
    for (const std::string& sha : candidates) {
        if (abandoned()) break;
        ++done;
        GuiHistoryCommitLoad load;
        std::string          why;
        if (!load_commit_sidecars_strict(sha, base_name, project_directory,
                                         load, why)) {
            ++hidden;
            continue;
        }
        on_member(std::move(load.sidecars));
    }
    on_done(done, hidden);
}

const std::deque<GuiHistoryCommitSidecars>& GuiHistoryDiff::members() const {
    static const std::deque<GuiHistoryCommitSidecars> kNone;
    if (!store_ || store_->generation() != store_generation_) return kNone;
    return store_->members();
}

std::size_t GuiHistoryDiff::commit_count() const { return members().size(); }

bool GuiHistoryDiff::init(const AppState&           app,
                          const GuiHistoryPrefetch& prefetch) {
    available_ = false;
    unavailable_reason_.clear();
    base_name_.clear();
    project_directory_.clear();
    store_            = nullptr;
    store_generation_ = 0;
    for (std::deque<std::optional<GuiHistoryCommitDelta>>& c : cache_) {
        c.clear();
    }

    // Every failure arm lands here: one stderr line, and the whole session
    // left in its documented empty shape.
    auto unavailable = [this](std::string why) {
        unavailable_reason_ = std::move(why);
        base_name_.clear();
        project_directory_.clear();
        store_            = nullptr;
        store_generation_ = 0;
        std::fprintf(stderr, "warptempo_gui: History is unavailable: %s\n",
                     unavailable_reason_.c_str());
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
    // simply asks the same question on this thread. Two git calls, no strict
    // load: cheap enough to pay at a keystroke, which is exactly why the split
    // is here rather than one step later.
    if (prefetch.has_header()) {
        if (!prefetch.header().ok) {
            return unavailable(prefetch.header().unavailable_reason);
        }
        base_name_         = prefetch.header().base_name;
        project_directory_ = prefetch.header().project_directory;
    } else {
        const GuiHistoryWalkHeader h =
            resolve_history_walk_header(app.source_audio_path,
                                        app.projects_repo);
        if (!h.ok) return unavailable(h.unavailable_reason);
        base_name_         = h.base_name;
        project_directory_ = h.project_directory;
    }

    // THE TWO TERMINAL ZERO CASES, and only a FINISHED scan can state either:
    // no commit touches the sidecars at all, or every one that does refuses the
    // strict load. Both are today's exact lines. A scan still running with no
    // member yet is NOT one of them — the view opens empty and populates — so
    // the refusal is conditioned on done, never on emptiness alone.
    //
    // WHICH LEAVES ONE SHAPE WITH NO REFUSAL, recorded rather than closed: a
    // visit that opened mid-scan whose run then finishes with zero eligible
    // members rests at `0/0` with a blank lane until the user presses `h` again.
    // It is a refusal that arrived too late to be one, and the alternative would
    // be a NEW closer — the view closing itself under the reader — which is a
    // bigger change than the case is worth (it needs a piece with committed
    // sidecars whose every checkpoint refuses the strict load, and an `h` inside
    // the second the scan takes to say so).
    if (prefetch.run_done() && prefetch.members().empty()) {
        return unavailable(
            prefetch.candidate_count() == 0
                ? ("No commit touches 'projects/**/" + base_name_ + ".*'")
                : ("None of the " + std::to_string(prefetch.hidden_count()) +
                   " commit(s) touching 'projects/**/" + base_name_ +
                   ".*' passes the strict sidecar load"));
    }

    // The now side is captured once, here: every delta this session hands out
    // is measured against these exact bytes. (The delta caches are NOT sized
    // here any more — membership grows during a visit, so delta_at grows them.)
    now_ = build_history_now_side(app);
    available_ = true;
    return true;
}

bool GuiHistoryCommitDelta::frame_span(int64_t& lo, int64_t& hi) const {
    bool have = false;
    auto take = [&](int64_t f) {
        if (!have) { lo = f; hi = f; have = true; return; }
        if (f < lo) lo = f;
        if (f > hi) hi = f;
    };
    // All six vectors, both columns, all three kinds — the membership is the
    // struct's own, re-derived from its fields rather than inherited from
    // is_empty's list, which is the same set for a different question.
    for (const GuiHistoryWarpEntry&  e : warp_added)          take(e.frame);
    for (const GuiHistoryWarpEntry&  e : warp_removed)        take(e.frame);
    for (const GuiHistoryWarpChange& c : warp_changed)        take(c.frame);
    for (const GuiHistoryPhaseResetEntry&  e : phase_reset_added)   take(e.frame);
    for (const GuiHistoryPhaseResetEntry&  e : phase_reset_removed) take(e.frame);
    for (const GuiHistoryPhaseResetChange& c : phase_reset_changed) take(c.frame);
    return have;
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
// local walk passes it EMPTY — an undo entry has no name.
GuiHistoryCommitDelta compute_commit_delta(const std::string& sha,
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
    for (const std::string& line : warp_diff.added) {
        GuiHistoryWarpEntry e;
        if (extract_warp_entry(line, e)) d.warp_added.push_back(std::move(e));
    }
    for (const std::string& line : warp_diff.removed) {
        GuiHistoryWarpEntry e;
        if (extract_warp_entry(line, e)) d.warp_removed.push_back(std::move(e));
    }
    for (const std::string& line : phase_reset_diff.added) {
        GuiHistoryPhaseResetEntry e;
        if (extract_phase_reset_entry(line, e)) d.phase_reset_added.push_back(e);
    }
    for (const std::string& line : phase_reset_diff.removed) {
        GuiHistoryPhaseResetEntry e;
        if (extract_phase_reset_entry(line, e)) d.phase_reset_removed.push_back(e);
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
            snap.settings.text, now_.warpmarkers_text,
            now_.phaseresetmarkers_text, now_.settings_text);
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
            snap.settings.text, now_.warpmarkers_text,
            now_.phaseresetmarkers_text, now_.settings_text);
        return &*slots[index];
    }
    const GuiHistoryCommitSidecars& newer = commits[index - 1];
    slots[index] = compute_commit_delta(
        snap.sha, snap.warpmarkers.text, snap.phaseresetmarkers.text,
        snap.settings.text, newer.warpmarkers.text,
        newer.phaseresetmarkers.text, newer.settings.text);
    return &*slots[index];
}

// ---------------------------------------------------------------------------
// the LOCAL walk — the same formula over the undo stack
// ---------------------------------------------------------------------------

void GuiHistoryLocalWalk::init(const AppState&          app,
                               const GuiHistoryNowSide& now) {
    app_   = &app;
    count_ = app.history.undo_stack.size();
    gui_   = capture_history_gui_side(app);
    now_   = now;
    members_.assign(count_, Member{});
    // SIZED ONCE, NEVER GROWN — the frozen-stack premise (the class comment owns
    // it) is exactly what lets these be vectors where the commit walk needs
    // deques: nothing can append a member under a live visit, so no reallocation
    // can move a delta this hands out.
    for (std::vector<std::optional<GuiHistoryCommitDelta>>& c : cache_) {
        c.assign(count_, std::nullopt);
    }
}

// ONE MEMBER'S THREE TEXTS, serialized on first ask. The mapping is the class
// comment's: member index i, newest first, is undo_stack[N-1-i] for the N
// CAPTURED AT INIT, and that entry's snapshots are the state BEFORE the event it
// records.
//
// IT INDEXES FROM THE BOTTOM, WHICH IS WHY A PUSH COULD NOT MOVE A MEMBER — a
// property that mattered while the frozen-stack premise had one hole, the
// admitted S->T VIEW SWITCH's iteration-bracket push. THAT HOLE IS CLOSED AT ITS
// SOURCE (2026-08-07): iteration mode is TARGET-LEGAL, so the S->T edge wipes
// nothing and writes no store, and `i` is not on the mode's keyboard allowlist,
// so the bit cannot move in here either. NO ROUTE PUSHES, POPS OR EVICTS while
// the view stands, so the premise is EXCEPTIONLESS BY CONSTRUCTION and stands on
// that derivation alone. The bottom-indexing stays what it always was — the
// shape that keeps an append harmless if one ever returns.
//
// THE SIZE TERM BELOW IS A BOUNDS PRECONDITION on the subscript this function is
// about to perform, and it predates all of that: a stack shorter than the
// captured count answers NOTHING AT ALL (a blank lane) rather than being read at
// indices that now mean other events.
//
// (A PUSH SERIAL — a per-entry identity the walk captured at init and re-checked
// here, written for the kCap-EVICTION shape the admitted push could reach, where
// the bottom entry goes and every position slides down one while the size holds
// — lived for one day of that same date and was DELETED by the architect once
// that producer went: a producer-less mechanism rather than a granularity change,
// in a feature-complete project. Do not re-propose it.)
const GuiHistoryLocalWalk::Member* GuiHistoryLocalWalk::member_at(
        std::size_t index) {
    if (app_ == nullptr || index >= count_) return nullptr;
    if (app_->history.undo_stack.size() < count_) return nullptr;
    Member& m = members_[index];
    if (m.built) return &m;

    const UndoEntry& e = app_->history.undo_stack[count_ - 1 - index];
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

const GuiHistoryCommitDelta* GuiHistoryLocalWalk::delta_at(
        std::size_t index, GuiHistoryCompare compare) {
    const Member* then_side = member_at(index);
    if (then_side == nullptr) return nullptr;
    std::vector<std::optional<GuiHistoryCommitDelta>>& slots =
        cache_[static_cast<std::size_t>(compare)];
    if (slots[index].has_value()) return &*slots[index];

    // THE NEW SIDE, by the commit walk's own two rules. CUMULATIVE takes the
    // frozen live now side whatever the position — "how far back does this take
    // me". ITERATIVE compares FORWARD: the member one NEWER (index - 1, the list
    // being newest-first), or that same live now side at index 0 — and since the
    // state before event i-1 IS the state after event i, that delta is EVENT i'S
    // OWN CHANGE. Every index has a forward partner here for the same reason it
    // does on the commit walk: the newest end is where the session is.
    const std::string* now_warp     = &now_.warpmarkers_text;
    const std::string* now_phase    = &now_.phaseresetmarkers_text;
    const std::string* now_settings = &now_.settings_text;
    if (compare == GuiHistoryCompare::Iterative && index > 0) {
        const Member* newer = member_at(index - 1);
        // Unreachable (the size check above passed for this same stack, and a
        // smaller index is in range whenever this one is), and stated rather
        // than assumed: the live now side is the honest fallback, being what
        // index 0 pairs with.
        if (newer != nullptr) {
            now_warp     = &newer->warpmarkers_text;
            now_phase    = &newer->phaseresetmarkers_text;
            now_settings = &newer->settings_text;
        }
    }

    // NO SHA: an undo entry has no name, and the corner reads the empty string
    // rather than being told separately (the local tabs show `n/N` alone).
    slots[index] = compute_commit_delta(
        std::string(), then_side->warpmarkers_text,
        then_side->phaseresetmarkers_text, then_side->settings_text,
        *now_warp, *now_phase, *now_settings);
    return &*slots[index];
}

// ---------------------------------------------------------------------------
// THE COMMIT ACT — the product's one mutating git route
// ---------------------------------------------------------------------------

namespace {

// A SPELLING'S OBJECT NAME, WITH THE CAPTURE'S OWN VERDICT BESIDE IT. `--verify`
// is what makes a missing ref an error rather than an echo of the argument,
// which is how the push check below asks "does this remote-tracking ref exist
// yet".
//
// THE TWO RETURNS ARE TWO DIFFERENT FACTS, and keeping them apart here is the
// whole reason this form exists. `object_name` answers WHAT THE REF NAMES and is
// empty when the ref did not resolve; the returned GitCapture answers WHETHER
// THE INVOCATION RAN, which an empty name cannot carry and which no later probe
// can reconstruct — a second question answered now proves the repository speaks
// now, never that an earlier invocation ever executed. resolve_ref_witnessed is
// the consumer that needs both, and its comment owns what it does with them.
GitCapture resolve_ref_capture(const std::string& spelling,
                               std::string&       object_name) {
    std::string      out;
    const GitCapture capture =
        run_git_capture({"rev-parse", "--verify", spelling}, out);
    object_name =
        (capture == GitCapture::Ran) ? trim_trailing_ws(out) : std::string();
    return capture;
}

// The same read for the callers that need only the name, which collapses the two
// answers on purpose: THE EMPTY ANSWER IS TWO ANSWERS — a ref that is absent and
// a read that could not be made — and the callers that read this directly (the
// act's own branch-tip reads) treat both as the same failure by design: no tip,
// no act. A caller that must tell them apart calls resolve_ref_capture, or goes
// through resolve_ref_witnessed which does.
std::string resolved_object_name(const std::string& spelling) {
    std::string name;
    resolve_ref_capture(spelling, name);
    return name;
}

}  // namespace

namespace {

// WHAT `git status` SAYS ABOUT THE THREE CHECKPOINT PATHS, in an answer that
// proves it ran. The three outcomes are the ones the act needs to tell apart, and
// telling them apart is exactly what the boolean capture could not do: "clean"
// and "could not ask" were both the empty string, so a status that failed
// reported the checkpoint as already carrying bytes that were in fact still
// sitting modified in the working tree.
//
// THE WITNESS IS `--branch`. Porcelain v1 with that flag emits a `## <branch>`
// header line FIRST and always, on every successful run — before any entry, and
// on a clean answer as the whole output. A run that failed emits nothing on
// stdout at all. So the header's presence is the proof, the lines after it are
// the answer, and neither is inferred from the other. (The exit status would say
// the same thing and is not available: main()'s SIG_IGN regime, documented at the
// capture helper.)
//
// THE HEADER IS ALSO THE BINDING, which is the second thing `--branch` buys and
// the one that keeps this probe honest about WHICH BRANCH it just answered for.
// `git status` has no ref argument: it compares the working tree against the
// CURRENT symbolic HEAD, and there is no spelling that pins it the way every
// other source-side read of the act names `refs/heads/<captured>`. So the probe
// cannot be pinned — it must be CHECKED, and the header is the check: it names
// the branch git actually compared against, so comparing it to the branch
// captured at act start turns an unpinnable observation into a verified one.
//
// WITHOUT THAT COMPARE THE CLEAN ARM COULD END THE ACT ON ANOTHER BRANCH'S
// ANSWER (reproduced live): captured `main` carries the old sidecar bytes, a
// topic branch already carries the new ones, another terminal checks that topic
// out before the probe, and the act's freshly written bytes match the topic tree
// exactly — so `status` reports CLEAN, the clean arm resolves captured
// `refs/heads/main` and finds `origin/main` caught up with it, and the act
// returns NothingToCommit before the byte confirmation ever runs. Nothing is
// committed, nothing is pushed, captured `main` still lacks the bytes, and the
// user is told the checkpoint already carries them — the one line that tells
// them to stop looking. OtherBranch is that sequence's close.
enum class GuiHistoryPathStatus {
    Unavailable,  // git did not run, or ran and failed
    OtherBranch,  // it ran, but against a branch that is not the captured one
    Clean,        // it ran on the captured branch; the three paths match its tip
    Dirty,        // it ran on the captured branch; at least one differs
};

// THE BRANCH THE `##` HEADER NAMES, in the same spelling `current_branch_name`
// returns — "" for a detached HEAD, so the two answers compare directly.
//
// THE GRAMMAR, verified live against git 2.55 in every shape the act can meet:
// `## main` (no upstream), `## main...origin/main` (with one),
// `## main...origin/main [ahead 1]` / `[behind 1]` / `[ahead 1, behind 2]`,
// `## HEAD (no branch)` (detached), `## No commits yet on main` (unborn).
//
// THE TWO CUTS ARE UNAMBIGUOUS BECAUSE OF WHAT A REFNAME MAY NOT CONTAIN: git
// refuses a branch name holding two consecutive dots or a space
// (`git check-ref-format`'s own rules), so the `...` that introduces the upstream
// and the space that introduces a decoration can never be part of the name
// itself. A name may freely carry single dots and slashes (`feat/a.b.c` parses
// whole, verified), which is why the cut is on `...` and not on the first `.`.
//
// THE DETACHED FORM IS TESTED WHOLE rather than cut, because cutting it would
// yield the literal "HEAD" and turn the ordinary detached act — which proceeds
// and simply publishes nothing — into a spurious mismatch failure.
//
// THE UNBORN FORM IS CUT TO ITS REAL NAME, past the fixed `No commits yet on `
// prefix `git status` always uses for it — verified live, including a slashed/
// dotted name (`No commits yet on feat/a.b.c` parses whole). No further cut is
// applied to the tail: an unborn branch carries no upstream, so `...` cannot
// appear, and `check-ref-format` still refuses a space in the name, so the
// whole remainder is unambiguously the name even if some future git prints a
// trailing decoration this parse has not seen — the safe direction (a
// misparsed decoration folds into the name, not out of it).
//
// EVERY OTHER SHAPE FAILS CLOSED BY CONSTRUCTION, and so does this one, only
// not by returning a name that matches nothing — by returning the TRUE name of
// a branch that cannot be the one this act captured. `current_branch_name`
// (its declaration owns the read) resolves an unborn HEAD to "" exactly as it
// does a detached one — `rev-parse --abbrev-ref HEAD` exits nonzero there, so
// `git_output` reports no answer — so the mode can only ever CAPTURE a real,
// committed branch name at act start (an unborn HEAD is never "the captured
// branch" going in, and the mode cannot open on one either: no committed
// history to diff against). The unborn OtherBranch case this parse now names
// correctly is therefore reachable only by a MID-ACT EXTERNAL SWITCH onto an
// orphan branch after the act's own capture — the same external-terminal
// threat model the mismatch check above exists for — and ordinarily that
// orphan's name cannot equal the captured branch's own, because getting there
// means deleting the captured branch's commit history first, which git
// refuses while it is the one checked out. The one way around that (checking
// out elsewhere, deleting the captured branch, then recreating it as an
// orphan under the identical name, then switching back) is possible but is an
// act of destroying the captured branch's history outright, not a mere
// checkout — at that point the mismatch this function reports is no longer
// the risk worth naming.
std::string branch_of_status_header(const std::string& header) {
    // Past the `##` the caller has already proved is there, plus its separator.
    std::size_t start = 2;
    while (start < header.size() && header[start] == ' ') ++start;
    const std::string rest = trim_trailing_ws(header.substr(start));
    if (rest == "HEAD (no branch)") return {};  // detached: no name, as captured

    static const std::string kUnbornPrefix = "No commits yet on ";
    if (rest.compare(0, kUnbornPrefix.size(), kUnbornPrefix) == 0) {
        return rest.substr(kUnbornPrefix.size());  // unborn: the whole tail
    }

    std::size_t end         = rest.find("...");
    const std::size_t space = rest.find(' ');
    if (space != std::string::npos && (end == std::string::npos || space < end)) {
        end = space;
    }
    return rest.substr(0, end);  // npos takes the whole remainder
}

// `expect_branch` is the branch captured at act start ("" for a detached HEAD);
// `checked_out_branch` comes back with what the header named, so the caller's
// failure line can name both.
GuiHistoryPathStatus status_of_paths(const std::vector<std::string>& pathspecs,
                                     const std::string& expect_branch,
                                     std::string&       checked_out_branch) {
    checked_out_branch.clear();
    std::vector<std::string> args{"status", "--porcelain", "--branch", "--"};
    for (const std::string& p : pathspecs) args.push_back(p);
    std::string out;
    if (run_git_capture(args, out) != GitCapture::Ran) {
        return GuiHistoryPathStatus::Unavailable;
    }
    const std::vector<std::string> lines = split_lines(out);
    if (lines.empty() || lines.front().size() < 2 ||
        lines.front().compare(0, 2, "##") != 0) {
        return GuiHistoryPathStatus::Unavailable;
    }
    checked_out_branch = branch_of_status_header(lines.front());
    if (checked_out_branch != expect_branch) {
        // The entries below this header are about the wrong branch too, so they
        // are not read at all: there is no answer here for the act to use.
        return GuiHistoryPathStatus::OtherBranch;
    }
    for (std::size_t i = 1; i < lines.size(); ++i) {
        if (!lines[i].empty()) return GuiHistoryPathStatus::Dirty;
    }
    return GuiHistoryPathStatus::Clean;
}

// HOW DEEP THE ATTRIBUTION WALK LOOKS. `before..HEAD` is ONE commit in every
// ordinary act — ours — and is longer only when something else committed into
// the same seconds. Ten is far past any shape this single-user laptop produces
// (a concurrent stack deeper than that is not a session this product is designed
// around), and the cap is what keeps the verdict's cost bounded whatever the
// repository does. A checkpoint buried deeper than ten simply is not found: the
// act reports the failure, the commit stands locally, and the pre-flight's
// unpushed arm pushes it on the next attempt.
constexpr int kAttributionWalkDepth = 10;

// The checked-out branch's short name, or "" for a detached HEAD (which has no
// name and no remote-tracking ref).
std::string current_branch_name() {
    std::string raw;
    if (!git_output({"rev-parse", "--abbrev-ref", kBranchRef}, raw)) return {};
    std::string name = trim_trailing_ws(raw);
    if (name == "HEAD") return {};  // detached
    return name;
}

// IS `sha` REACHABLE FROM `tip` — the push's real question, since a commit
// landing on top of the checkpoint makes the remote-tracking ref move to THAT
// commit while the checkpoint rides along underneath as an ancestor.
//
// THREE ANSWERS, BECAUSE "COULD NOT ASK" IS NOT "NO" AND MUST NEVER BE "YES".
// The exit status is unobtainable (main()'s SIG_IGN regime, documented at the
// capture helper), so the answer has to be legible in the OUTPUT — and the
// output shape is the whole design here. The older form asked
// `rev-list --max-count=1 <sha> ^<tip>` and read SILENCE as containment: git
// printing nothing meant reachable. But a run that ran and FAILED prints nothing
// on stdout either, so every failure read as a confirmed containment — the one
// direction this observation must never fail in, since it is what lets the act
// say "pushed" about a checkpoint the remote does not carry, and what lets the
// pre-flight call an unpushed checkpoint done and leave no route that retries.
//
// `--count` INVERTS THAT: a successful run ALWAYS prints a number ("0" when the
// walk is empty, which is exactly containment), so the witness is the number
// itself and an empty or non-numeric answer is a FAILURE, never a yes. Same
// walk, same cost — only the answer is now success-shaped, the same reading
// `status_of_paths` gets from its `##` header.
//
// The caller should still resolve both names first (an unresolvable rev is one
// of the failures this now reports honestly rather than one it hides). What this
// cannot separate is a walk that printed its number AND complained on stderr —
// but the number a completed walk prints is that walk's own answer, and the
// fail-open the callers are exposed to is closed.
enum class GuiHistoryContainment {
    Unavailable,  // the walk did not run, or ran and failed
    Contains,     // it ran; `tip` reaches `sha`
    Missing,      // it ran; it does not
};

GuiHistoryContainment ref_containment(const std::string& tip,
                                      const std::string& sha) {
    std::string out;
    if (run_git_capture({"rev-list", "--count", sha, "^" + tip}, out) !=
        GitCapture::Ran) {
        return GuiHistoryContainment::Unavailable;
    }
    const std::string count = trim_trailing_ws(out);
    if (count.empty()) return GuiHistoryContainment::Unavailable;
    for (const char c : count) {
        if (c < '0' || c > '9') return GuiHistoryContainment::Unavailable;
    }
    return count == "0" ? GuiHistoryContainment::Contains
                        : GuiHistoryContainment::Missing;
}

// WHAT AN EMPTY `rev-parse` ANSWER MEANS — the distinction the tri-state above
// is worth nothing without, since both verdict sites reach the walk through a
// ref NAME that may not resolve at all. `resolved_object_name` returns "" for a
// ref that is genuinely ABSENT (no `origin/<branch>` yet — the state before the
// first push) and for a read that could not be MADE (the capture could not exec,
// the repository could not be read), and those two answers must route
// differently: absent is an honest "the remote does not carry it", unavailable
// is "could not ask" and may never be reported as a no.
//
// THE WITNESS IS A SECOND REF KNOWN TO EXIST. The act runs from a checked-out
// branch, so `refs/heads/<captured>` resolves in any repository that can answer
// at all; asking for it through the same capture right after an empty first read
// turns silence into evidence. If the witness PRINTS, the repository answers
// queries and the first read's emptiness is the target ref's own absence. If the
// witness comes back empty too — a failed exec, an unreadable repository, or the
// captured branch deleted out from under the act — nothing here can tell that
// apart from a broken observation, so the answer is UNAVAILABLE.
//
// WHAT THE WITNESS MAY NOT ADJUDICATE, and the reason the target read comes back
// as a CAPTURE rather than as a bare string: a target probe THAT NEVER RAN is
// Unavailable on the spot, with no witness probe run at all. A witness proves the
// repository answers NOW; it cannot reach backwards and prove that the preceding
// invocation executed, so letting a healthy witness convert a could-not-exec
// target read into "the ref is absent" would manufacture exactly the false
// negative this tri-state exists to prevent (a push route reporting `Push failed`
// over a tracking ref that carries the checkpoint). Only a target read that RAN
// and came back empty is the witness's question — that emptiness is genuinely
// either absence or an unreadable repository, which is what a witness can settle.
//
// THE ONE FALSE UNAVAILABLE, named because it is real: a captured branch deleted
// mid-act makes a healthy repository answer "could not ask". What that costs is a
// fall-through to a push — and the push is NOT refused by the branch's absence,
// which is what makes the routing conservative rather than self-correcting: the
// refspec is `<sha>:refs/heads/<captured>`, an object name the act already holds,
// so git resolves no local branch to send it and the push can create or update
// the REMOTE branch with the local one gone (verified live). The Unavailable
// answer is still the right one — it never claims a containment it could not
// observe, and calling an unreadable repository "absent" would cost a false
// NEGATIVE on the one question this module may never guess at — but the reason it
// is safe is that a redundant push moves nothing at the remote, not that the
// missing branch would stop it.
enum class GuiHistoryRefRead {
    Unavailable,  // the read did not run, or the repository could not answer
    Absent,       // it ran; there is no such ref
    Resolved,     // it ran; `object_name` is what the ref names
};

GuiHistoryRefRead resolve_ref_witnessed(const std::string& spelling,
                                        const std::string& witness,
                                        std::string&       object_name) {
    if (resolve_ref_capture(spelling, object_name) != GitCapture::Ran) {
        return GuiHistoryRefRead::Unavailable;
    }
    if (!object_name.empty()) return GuiHistoryRefRead::Resolved;
    std::string witness_name;
    if (resolve_ref_capture(witness, witness_name) != GitCapture::Ran ||
        witness_name.empty()) {
        return GuiHistoryRefRead::Unavailable;
    }
    return GuiHistoryRefRead::Absent;
}

// DOES `ref` CARRY `sha` — the question both verdict sites actually ask, taken
// from the ref's NAME rather than from an already-resolved tip, because
// RESOLVING it is where the absent/unavailable distinction lives and both
// callers need that distinction carried all the way through. `witness` is the
// ref that proves the repository answers (above). The equality shortcut needs no
// walk at all and is folded in.
//
// Only a demonstrated containment is true here; `unavailable` comes back set
// when the ref could not be READ or the walk could not ANSWER, so a caller can
// say "could not be observed" instead of "no". An ABSENT ref is a plain false: a
// remote-tracking ref that does not exist demonstrably carries nothing.
bool ref_carries(const std::string& ref, const std::string& witness,
                 const std::string& sha, bool& unavailable) {
    unavailable = false;
    std::string tip;
    switch (resolve_ref_witnessed(ref, witness, tip)) {
        case GuiHistoryRefRead::Resolved:
            break;
        case GuiHistoryRefRead::Absent:
            return false;
        case GuiHistoryRefRead::Unavailable:
            unavailable = true;
            return false;
    }
    if (tip == sha) return true;
    switch (ref_containment(tip, sha)) {
        case GuiHistoryContainment::Contains:
            return true;
        case GuiHistoryContainment::Missing:
            return false;
        case GuiHistoryContainment::Unavailable:
            break;
    }
    unavailable = true;
    return false;
}

// DOES THIS COMMIT CHANGE ONLY THE ACT'S OWN PATHS — the scope leg of the
// attribution below, and the one that an ordinary unscoped `git commit -a` fails
// even when it swept the act's staged sidecars up with its own work.
//
// IT IS A SUBSET TEST, NOT AN EQUALITY TEST, and that is the correction the live
// corpus forced: a checkpoint whose warpmarkers happen to be unchanged since the
// last one changes TWO paths, not three (the architect's own 37560ee changed
// exactly two), so demanding all three would refuse the act's most ordinary
// result. Non-empty is required — a commit changing nothing is not the one we
// asked for — and the BYTES leg below is what proves the other sidecars are
// present and current at their unchanged spellings.
//
// `diff-tree` compares the commit against its own first parent and prints
// nothing at all for a merge (no combined diff without -c/-m) or for a root
// commit (no --root), so both fail this leg, which is the right answer for both.
bool commit_touches_only(const std::string&              sha,
                         const std::vector<std::string>& paths) {
    std::string names;
    if (!git_output({"diff-tree", "--no-commit-id", "--name-only", "-z", "-r",
                     sha},
                    names)) {
        return false;
    }
    bool any = false;
    for (const std::string& p : split_on(names, '\0')) {
        if (p.empty()) continue;
        if (std::find(paths.begin(), paths.end(), p) == paths.end()) {
            return false;
        }
        any = true;
    }
    return any;
}

// DOES THIS COMMIT'S TREE CARRY EXACTLY THE BYTES WE WROTE, at all three paths.
// The strongest of the three legs and the one that is a fact about content
// rather than about naming: a commit passing it IS a checkpoint of this state,
// whoever ran the git — which is the whole attribution contract in one line, and
// find_checkpoint_commit's own comment owns why that is the ruled answer rather
// than a shortfall.
//
// Each path is required PRESENT in the commit's own tree with the tree's stated
// byte count, and then read whole and compared. The size cross-check is the
// load-in-place path's own guard reused (`ls-tree -l` states the true length,
// so a short
// read cannot pass as content), and the byte comparison is strictly stronger
// than a size compare on its own; a `show` that could not run yields an empty
// string, which equals our text for no file the writers produce and in any case
// fails the size compare first. A read failure therefore fails CLOSED — the act
// reports a commit it could not attribute, which the pre-flight's unpushed arm
// recovers on the next attempt rather than pushing something unconfirmed.
//
// The tree rows come through sidecar_entries_in_listing, so the paths are found
// under the same `projects/` + base-name rule everything else here uses; the
// act's own destination always satisfies it (an available session's directory is
// always under that folder), and anything that did not would simply fail closed.
bool commit_carries_our_bytes(const std::string&              sha,
                              const std::string&              base_name,
                              const std::vector<std::string>& paths,
                              const std::string* const        texts[3]) {
    std::string listing;
    if (!git_output({"ls-tree", "-r", "-z", "-l", sha}, listing)) return false;
    const std::vector<GuiHistoryTreeEntry> rows =
        sidecar_entries_in_listing(listing, base_name);

    for (std::size_t e = 0; e < 3; ++e) {
        const GuiHistoryTreeEntry* row = nullptr;
        for (const GuiHistoryTreeEntry& candidate : rows) {
            if (candidate.path == paths[e]) {
                row = &candidate;
                break;
            }
        }
        if (row == nullptr) return false;
        if (row->size < 0 ||
            static_cast<std::size_t>(row->size) != texts[e]->size()) {
            return false;
        }
        std::string blob;
        if (!read_snapshot_at(sha, paths[e], blob)) return false;
        if (blob != *texts[e]) return false;
    }
    return true;
}

// FIND THE COMMIT CARRYING THIS CHECKPOINT — the verdict, and the replacement
// for "HEAD moved to a child of before". That older shape asked whether A
// checkpoint appeared; this one asks whether THE INTENDED ONE did, which is a
// different question in exactly the sequences that motivated the concurrency
// model in the first place:
//
//   * THE ACT'S COMMIT IS REJECTED and a concurrent terminal's ordinary unscoped
//     commit lands on `before`, sweeping the already-staged sidecars into it
//     along with unrelated work. The old verdict passed every leg (that commit
//     is a child of before, and the three paths are clean because it included
//     them) and PUSHED IT while reporting it as the checkpoint. Here it fails
//     the scope leg, and — for a title of its own — the message leg.
//   * THE ACT'S COMMIT LANDS and something else commits on top of it before the
//     act looks. The old verdict saw HEAD's parent not equal to `before`, called
//     the act failed and refused to push a checkpoint that existed. Here the
//     walk finds it and the push publishes it by its own name.
//   * THE ORDINARY CASE: one commit in the range, all three legs pass.
//
// WHAT IDENTITY THIS IS, stated plainly because the name it used to carry
// ("our commit") promised more than any leg implements: the legs are the exact
// TITLE, a non-empty subset of the act's three literal paths and NOTHING else,
// and the three resulting blobs byte-equal to what this act wrote. Every one of
// those is a fact about CONTENT. None of them — and no leg here could, short of
// a mechanism the act does not have — establishes WHO ran the git.
//
// AND THAT IS THE RULED CONTRACT, not a gap left open. A commit passing all
// three legs carries exactly the checkpoint this act was asked to publish, under
// exactly the title it was asked to use, touching nothing else; publishing it
// publishes the intended state whoever made it. So content-equivalent commits
// are DELIBERATELY INTERCHANGEABLE, and the byte legs are what make that safe:
// nothing carrying different sidecar state can be substituted, and no foreign
// path can ride along inside one. The alternative was an act-unique message
// trailer, CONSIDERED AND DECLINED — it would add mechanism, and a permanent
// oddity in every checkpoint message, to tell apart commits whose content is
// identical, and the one other producer of this title (the architect's own
// wrapper) is retired for checkpoints as of 2026-08-04.
//
// WHICH MATCH, when the range holds more than one: the FIRST the walk sees,
// which is the newest in `rev-list` order. On the linear history this act
// produces that is the one nearest HEAD, and since the push sends the matched
// commit BY SHA, taking the newest publishes the most of the user's own branch
// while the published tip still carries exactly the intended bytes. (Taking the
// oldest — the first to publish the content — would leave the commits above it
// unpublished for no gain: the content is the same by construction.)
//
// WHICH TIP THE WALK ENDS AT: `source_ref`, the act's captured branch ref, never
// the symbolic `HEAD`. The commit necessarily lands wherever git is checked out
// when it runs, which no argument here can pin — so the RANGE is what binds the
// verdict to the branch this act is publishing on. A checkout mid-act therefore
// makes the walk find nothing, and the act reports a failure having published
// nothing, instead of attributing a commit made on some other branch and pushing
// that branch's ancestry onto the captured one (commit_history_checkpoint's
// branch paragraph owns the whole reasoning).
//
// `observed` is filled with what the walk actually saw whenever the answer is
// "no match", so the failure line names a fact rather than a guess.
std::string find_checkpoint_commit(const std::string&              source_ref,
                                   const std::string&              before,
                                   const std::string&              title,
                                   const std::string&              base_name,
                                   const std::vector<std::string>& paths,
                                   const std::string* const        texts[3],
                                   std::string&                    observed) {
    observed.clear();
    std::string walk;
    if (!git_output({"rev-list",
                     "--max-count=" + std::to_string(kAttributionWalkDepth),
                     before + ".." + source_ref},
                    walk)) {
        // No output is the ordinary "the branch is still `before`" answer — the
        // commit was refused, or it landed on some other branch a checkout put
        // under git; a failed run reads the same way and lands on the same
        // honest verdict, since either way no commit of ours has been
        // demonstrated on the branch this act publishes.
        observed = "the branch did not move";
        return {};
    }

    std::string seen;
    for (const std::string& line : split_lines(walk)) {
        const std::string sha = trim_trailing_ws(line);
        if (sha.empty()) continue;
        if (!seen.empty()) seen += ", ";
        seen += short_sha(sha);

        std::string message;
        if (!git_output({"log", "-1", "--format=%B", sha}, message)) continue;
        const std::vector<std::string> message_lines = split_lines(message);
        if (message_lines.empty() ||
            trim_trailing_ws(message_lines.front()) != title) {
            continue;
        }
        bool other_text = false;
        for (std::size_t i = 1; i < message_lines.size(); ++i) {
            if (!trim_trailing_ws(message_lines[i]).empty()) other_text = true;
        }
        if (other_text) continue;

        if (!commit_touches_only(sha, paths)) continue;
        if (!commit_carries_our_bytes(sha, base_name, paths, texts)) continue;
        return sha;
    }

    observed = seen.empty()
                   ? std::string("the branch did not move")
                   : ("the branch moved to " + seen +
                      ", none of which carries this checkpoint");
    return {};
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
// working tree, stage exactly those three paths, commit them under the caller's
// `title`, push. Every step is fenced to those three paths and to this one
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
//
// THE OBSERVATION DECIDES, NEVER THE TRANSPORT — the rule the whole act is built
// on, stated once here and enforced at each of the three steps below. `git
// commit`'s return, `git add`'s return and `git push`'s return are DIAGNOSTICS;
// what decides each outcome is a question put to the repository afterwards (does
// a commit carrying this checkpoint exist in the branch; does the remote-tracking
// ref carry it, as an answer that could be observed at all). A
// deadline that fires is a killed child, not a rolled-back commit: git moves HEAD
// before it runs `post-commit`, so the one hook shape the deadline exists to
// contain is exactly the one that leaves a landed checkpoint behind a failed
// transport.
GuiHistoryCommitOutcome commit_history_checkpoint(
    const std::string& project_directory, const std::string& base_name,
    const std::string& projects_repo, const GuiHistoryNowSide& bytes,
    const std::string& title) {

    // THE BRANCH, READ ONCE — and this is the ONLY place the act reads the
    // mutable symbolic HEAD for a value. Every later use is this one value: the
    // remote-tracking ref the two verdicts read, the refspec the push writes,
    // and — through `source_ref` below — every source-side observation the act
    // makes. Reading HEAD per site is what let a checkout mid-act have the
    // observation name one branch and the publication another; one read cannot.
    // Empty means a detached HEAD — no name, no remote-tracking ref, no refspec
    // — which each site answers for itself.
    //
    // ONE LATER SITE READS HEAD AGAIN, AND ONLY TO CHECK THIS VALUE: the
    // pre-flight `git status`, which has no ref argument and so always answers
    // for whatever is checked out. Its `##` header is compared against this name
    // and a mismatch ENDS THE ACT (status_of_paths owns the grammar and the
    // sequence). That is not a second source of truth — it is this one being
    // verified still current at the one observation that cannot be pinned.
    const std::string branch = current_branch_name();

    // THE SOURCE REF, which is what makes the capture above worth anything: the
    // act's own before/tip/walk reads all name `refs/heads/<captured>`
    // EXPLICITLY, so none of them can follow a checkout onto another branch.
    // `git commit` itself still commits wherever git is checked out — no
    // argument can pin that — and it does not need to be pinned: if a checkout
    // intervened, the commit lands on the other branch, the walk over THIS
    // ref's range finds nothing, and the act returns CommitFailed having
    // published nothing. The clean arm reasons the same way: a checkout before
    // it makes this read report the captured branch's true state, so the retry
    // pushes that branch's own tip bytes and never the other branch's.
    //
    // A DETACHED HEAD KEEPS THE OLD SPELLING because it has no branch ref to
    // name, and it costs nothing: that arm publishes nothing at all (the push
    // refuses below), so a checkout under it can move what gets COMMITTED but
    // never what gets published.
    const std::string source_ref =
        branch.empty() ? std::string(kBranchRef) : ("refs/heads/" + branch);

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

    // EVERY PATH THAT REACHES GIT AFTER A `--` IS A PATHSPEC, so all three go
    // through literal_pathspec (whose comment owns why) — the pre-flight status
    // probe, the add and the commit, one list built once and used by all three.
    // (The attribution walk below reads PATHS, not pathspecs: `diff-tree
    // --name-only` and `show <sha>:<path>` both speak committed paths.)
    std::vector<std::string> pathspecs;
    pathspecs.reserve(3);
    for (const std::string& p : paths) pathspecs.push_back(literal_pathspec(p));

    auto commit_failed = [](const std::string& why) {
        std::fprintf(stderr, "warptempo_gui: Commit failed: %s\n", why.c_str());
        return GuiHistoryCommitOutcome::CommitFailed;
    };

    // THE PUSH LEG, shared by the two arms that reach it: the ordinary act's
    // tail, and the pre-flight's "already committed, never pushed" retry.
    // `landed` is the checkpoint that must reach the remote for this to count.
    //
    // ALL THREE TERMS OF THE PUBLICATION ARE BOUND HERE — destination, content
    // and branch — and each is bound because the alternative was a name some
    // LATER git process re-resolves for itself, which is a check about one thing
    // and a mutation about another.
    //
    // DESTINATION: the guard runs again here, at the MUTATING BOUNDARY (the
    // second of clone_is_projects_home's two askings, its comment owns the
    // reasoning) — and this act pushes to THE URL THAT GUARD JUST VALIDATED,
    // pinned into the push child's own configuration. Asking again close to the
    // push only narrows the window; a `git push origin` after it still asks a
    // fresh process to resolve `origin`, and `remote.origin.pushurl` is a config
    // value another terminal — or a hook this very act just ran — can move in
    // between. The pin is a PAIR of `-c` settings and needs both halves:
    // `remote.<name>.pushurl` is MULTI-VALUED, so a lone `-c` ADDS a destination
    // to the configured ones rather than replacing them (verified live on git
    // 2.55: with a pushurl in the repo config, one `-c` published to BOTH), and
    // an EMPTY value CLEARS the accumulated list. So: clear, then name the one
    // validated URL — the config the push child resolves has exactly one push
    // destination and it is the checked one. That is also the answer in the
    // multi-pushurl case: the guard validates EVERY configured push URL (all of
    // them had to normalize equal to the setting for the guard to pass at all)
    // and the pin then replaces the whole set with one of them — publishing to
    // one confirmed repository rather than to N, which is the point. The named
    // remote stays in the argv so the remote-tracking ref still updates, which
    // is what the verdict below reads.
    //
    // CONTENT: the refspec sends `landed` BY SHA, never `HEAD`. What was
    // attributed is what gets published — a branch switch or another commit
    // arriving between the attribution walk and the push cannot change it. There
    // is NO force anywhere: an explicit-sha refspec is an ordinary
    // fast-forward-or-refuse push, and a refusal is a plain PushFailed with the
    // checkpoint intact.
    //
    // WHAT THAT MEANS FOR THE UNOBSERVABLE FALL-THROUGH, the clean arm's push
    // over a containment it could not read, stated exactly because "harmless" is
    // two different mechanisms: it is a NO-OP only when the remote tip EQUALS
    // the checkpoint (git answers "Everything up-to-date"). When the remote tip
    // is a DESCENDANT — which this module's own containment definition also
    // calls carrying it — the refspec asks to move the branch BACKWARD onto the
    // checkpoint and git REJECTS it as a non-fast-forward (verified live), which
    // is safe precisely because nothing here forces. A rejected push does NOT
    // update the remote-tracking ref (also verified live), so the verdict below
    // reads exactly what that ref already said and reports accordingly —
    // unconfirmed where it still cannot be read. Either way nothing at the
    // remote is lost or rewound, which is the whole claim this fall-through
    // rests on.
    //
    // The recorded trade of sending the sha: a commit sitting
    // ON TOP of the checkpoint stays unpublished (a whole-branch push would have
    // carried it along), which is the correct cost — this act publishes the
    // checkpoint it made, and the branch's own commits are the user's to push.
    //
    // BRANCH: `branch` is read ONCE at act start — the act's single reading of
    // the symbolic HEAD for a value — and serves the refspec, the
    // remote-tracking ref AND (as `source_ref`) every source-side observation
    // that can be spelled with a ref, with the one that cannot (the pre-flight
    // status) checking its `##` header against this same name and failing the
    // act on a mismatch. So a checkout that happens while the act runs cannot
    // make any two of them name different
    // branches; it also cannot silently redirect the publication, since the
    // content is a sha rather than HEAD, and it cannot smuggle another branch's
    // ancestry in behind that sha, since the walk that produced it read the
    // captured ref (find_checkpoint_commit's range owns that half). A detached
    // HEAD has no branch to name in a refspec and no remote-tracking ref to
    // observe, so it publishes nothing and says so, rather than guessing.
    //
    // THE VERDICT IS THE REPOSITORY'S. A push updates the local remote-tracking
    // ref as its last act (verified live for this explicit-sha refspec shape,
    // which is the one the verdict depends on), so that ref CARRYING `landed` is
    // the observation that it arrived — carrying, not equalling, because a
    // commit landing on top of the checkpoint moves the ref above it. The
    // transport's own return is ignored on purpose: a push that completed and
    // then hung in a post-push hook until the deadline killed it still pushed.
    // An UNOBSERVABLE containment is reported as unconfirmed and never as
    // pushed (ref_containment owns that reading).
    //
    // WHAT THE LOCAL-CHECKPOINT LINE MAY CLAIM. "Still unpushed" is an
    // OBSERVATION, so it is printed only where one was made: `publication_known`
    // carries whether the remote's containment could be read at all — the
    // caller's own reading on the arms that run before the push, this leg's own
    // afterwards — and an unobservable one drops the clause rather than
    // asserting the negative fact the very next line calls unknowable.
    auto push_branch = [&](const std::string& landed, bool already_committed,
                           bool publication_known) {
        auto say_committed = [&](bool known) {
            if (!already_committed) {
                std::fprintf(stderr, "warptempo_gui: Committed %s \"%s\"\n",
                             short_sha(landed).c_str(), title.c_str());
            } else if (known) {
                std::fprintf(stderr,
                             "warptempo_gui: The checkpoint %s \"%s\" is "
                             "committed locally and still unpushed\n",
                             short_sha(landed).c_str(), title.c_str());
            } else {
                std::fprintf(stderr,
                             "warptempo_gui: The checkpoint %s \"%s\" is "
                             "committed locally\n",
                             short_sha(landed).c_str(), title.c_str());
            }
        };

        if (branch.empty()) {
            say_committed(publication_known);
            std::fprintf(stderr,
                         "warptempo_gui: Push refused: HEAD is detached, so "
                         "there is no branch to publish the checkpoint on\n");
            return GuiHistoryCommitOutcome::CommittedNotPushed;
        }

        std::string guard_reason;
        std::string destination;
        if (!clone_is_projects_home(projects_repo, guard_reason, &destination)) {
            say_committed(publication_known);
            std::fprintf(stderr, "warptempo_gui: Push refused: %s\n",
                         guard_reason.c_str());
            return GuiHistoryCommitOutcome::CommittedNotPushed;
        }

        std::string push_line;
        run_git_mutate({"-c", "core.sshCommand=ssh -o BatchMode=yes",
                        // Clear the configured push destinations, then name the
                        // one the guard just validated — both halves required.
                        "-c", "remote.origin.pushurl=",
                        "-c", "remote.origin.pushurl=" + destination,
                        "push", "origin",
                        landed + ":refs/heads/" + branch},
                       push_line);

        bool       unobserved = false;
        const bool pushed = ref_carries("refs/remotes/origin/" + branch,
                                        source_ref, landed, unobserved);

        if (pushed) {
            if (already_committed) {
                std::fprintf(stderr,
                             "warptempo_gui: Pushed the existing checkpoint %s "
                             "\"%s\"\n",
                             short_sha(landed).c_str(), title.c_str());
            } else {
                std::fprintf(stderr,
                             "warptempo_gui: Committed and pushed %s \"%s\"\n",
                             short_sha(landed).c_str(), title.c_str());
            }
            return GuiHistoryCommitOutcome::Committed;
        }
        say_committed(!unobserved);
        if (unobserved) {
            // The push may well have arrived; what failed is the question about
            // it. Saying so is the honest verdict — the checkpoint is intact
            // locally and the retry route pushes it again, which is SAFE
            // whatever the remote already held, though not silent: it is the
            // `Everything up-to-date` no-op only at TIP-EQUALITY, while a remote
            // tip that has since advanced to a DESCENDANT carrying the
            // checkpoint rejects the explicit `<sha>:refs/heads/<branch>`
            // refspec as an ordinary non-fast-forward (verified live). Nothing
            // forces, so neither outcome moves a byte at the remote — which is
            // the whole claim the retry rests on.
            std::fprintf(stderr,
                         "warptempo_gui: Push unconfirmed: could not read "
                         "whether 'origin/%s' carries the checkpoint\n",
                         branch.c_str());
        } else {
            std::fprintf(stderr, "warptempo_gui: Push failed: %s\n",
                         push_line.empty() ? "git reported nothing"
                                           : push_line.c_str());
        }
        return GuiHistoryCommitOutcome::CommittedNotPushed;
    };

    // THE ONE EXPECTED NON-FAILURE, named before it can be mistaken for one: if
    // the bytes just written match what HEAD already carries, `git status` sees
    // nothing and `git commit` would refuse with a message whose first line is
    // "On branch main" — true and useless. This is the state a user reaches by
    // committing twice, and the mode's own empty diff already says so.
    //
    // IT IS A VERDICT ABOUT A STATUS THAT RAN — status_of_paths owns the
    // distinction, and it is the whole difference here. An unrunnable or failing
    // `status` used to read the same as a clean one, which said "the checkpoint
    // already carries these bytes" about three files still sitting modified in
    // the working tree: the most misleading line this act could print, since it
    // tells the user to stop looking. Now only a status that DEMONSTRABLY ran and
    // came back clean is the non-failure.
    //
    // AND IT IS A VERDICT ABOUT THE CAPTURED BRANCH. `git status` takes no ref
    // and compares against whatever HEAD points at, so it is the one source-side
    // observation the act cannot pin by spelling; it is pinned by CHECKING
    // instead, against the `##` header's own branch name (status_of_paths owns
    // the grammar and the sequence a missing compare leaves open). A mismatch is
    // neither Clean nor Dirty — it is no usable answer at all — so it ends the
    // act here, having committed and pushed nothing.
    std::string                checked_out_branch;
    const GuiHistoryPathStatus before_status =
        status_of_paths(pathspecs, branch, checked_out_branch);
    if (before_status == GuiHistoryPathStatus::Unavailable) {
        return commit_failed("could not read 'git status' for the checkpoint "
                             "paths; the written files are still in the working "
                             "tree");
    }
    if (before_status == GuiHistoryPathStatus::OtherBranch) {
        // The written sidecars stay in the working tree exactly as they do after
        // any other failure here (the act's own "what remains after a failure"
        // paragraph owns why they are never rolled back) — visible to `git
        // status` and committable by hand.
        auto branch_phrase = [](const std::string& name) {
            return name.empty() ? std::string("a detached HEAD")
                                : ("'" + name + "'");
        };
        return commit_failed(
            "git has " + branch_phrase(checked_out_branch) +
            " checked out but this act captured " + branch_phrase(branch) +
            ", so nothing was committed or pushed; the written files are still "
            "in the working tree");
    }
    if (before_status == GuiHistoryPathStatus::Clean) {
        // CLEAN IS TWO STATES, NOT ONE, and telling them apart is what makes a
        // retry work. "Committed" and "published" are different facts: the
        // checkpoint can be sitting in the local branch unpushed — the shape a
        // hung post-commit hook leaves behind, since the deadline kills a child
        // over a commit git had ALREADY made — and a bare NothingToCommit there
        // would tell the user everything is done while the remote has none of
        // it, with no route left that would ever retry the push.
        //
        // So: clean AND the remote already carries the branch tip is the
        // ordinary committed-twice non-failure. Clean AND the remote is BEHIND
        // means the work exists locally and only the push is missing, so this
        // arm pushes. It does not ask WHO made that commit and deliberately so
        // — the user may simply have committed from a terminal and left it
        // unpushed, and publishing it is the correct answer whoever made it (the
        // same content-identity contract find_checkpoint_commit states in full).
        // What it DOES confirm first is that the tip carries the checkpoint's
        // bytes, so the line it prints about "the checkpoint" names something
        // real — and what it then publishes is that confirmed commit by sha.
        //
        // THE TIP IS THE CAPTURED BRANCH'S, not the symbolic HEAD's: a checkout
        // mid-act would otherwise have this arm confirm the OTHER branch's bytes
        // and push that branch's tip onto the captured one. Reading the captured
        // ref makes the answer the true state of the branch this act publishes
        // on, whatever git happens to have checked out.
        const std::string head = resolved_object_name(source_ref);
        if (head.empty()) {
            return commit_failed("could not read the branch tip while "
                                 "confirming the existing checkpoint");
        }
        // UNOBSERVABLE IS NOT PUBLISHED, and this arm is the reason that
        // distinction has to exist: "already published" is the answer that ENDS
        // the act, so a containment the walk could not answer must not reach it
        // — that is precisely the reading that turned an unpushed checkpoint
        // into "nothing to commit" and left no route that ever retried. An
        // unobserved containment therefore falls through to the push below,
        // which recovers the checkpoint if the remote did not have it and is
        // refused harmlessly if it did (ref_carries owns the reading, and the
        // push leg's own paragraph owns what that fallback actually does at the
        // remote).
        bool unobserved        = false;
        bool already_published = false;
        if (!branch.empty()) {
            already_published = ref_carries("refs/remotes/origin/" + branch,
                                            source_ref, head, unobserved);
        }

        if (already_published || branch.empty()) {
            // A detached HEAD has no remote-tracking ref to be behind, so it
            // takes the unchanged answer rather than a guess.
            std::fprintf(stderr,
                         "warptempo_gui: Nothing to commit: the checkpoint "
                         "already carries these bytes\n");
            return GuiHistoryCommitOutcome::NothingToCommit;
        }
        if (unobserved) {
            std::fprintf(stderr,
                         "warptempo_gui: Could not read whether 'origin/%s' "
                         "already carries the checkpoint; treating it as "
                         "unpublished\n",
                         branch.c_str());
        }
        if (!commit_carries_our_bytes(head, base_name, paths, texts)) {
            std::fprintf(stderr,
                         "warptempo_gui: Nothing to commit: the checkpoint "
                         "paths are clean, but the branch tip could not be "
                         "confirmed to carry these bytes, so nothing was "
                         "pushed\n");
            return GuiHistoryCommitOutcome::NothingToCommit;
        }
        return push_branch(head, /*already_committed=*/true,
                           /*publication_known=*/!unobserved);
    }

    // BEFORE MUST BE A REAL OBSERVATION. A failed capture here returns "" and an
    // empty string is no starting point for the walk below, which is scoped
    // `before..<source_ref>`. So the failure is the failure. It reads the
    // CAPTURED ref for the same reason the walk does: the range's two ends must
    // name the same branch, and that branch must be the one the push publishes.
    const std::string before = resolved_object_name(source_ref);
    if (before.empty()) {
        return commit_failed("could not read the branch tip before committing");
    }

    // (b) Stage, then commit — both pathspec-scoped to the same three paths.
    //
    // NEITHER TRANSPORT RESULT ENDS THE ACT. run_git_mutate's return is
    // advisory by contract (its comment owns why), so both lines are KEPT for
    // the diagnostics and the act walks on to ask the repository. That matters
    // most exactly where it used to matter least: git updates HEAD BEFORE it
    // runs `post-commit`, so a hook that hangs past the deadline gets the child
    // killed over a checkpoint that already exists, and returning here would
    // have called that landed commit a failure and left it unpushed with no
    // route that ever retried. The `add` is treated the same way for one
    // reason: `git commit -- <paths>` takes the working tree's own contents for
    // those paths, so the add is needed only to make a previously UNTRACKED
    // sidecar known — and if that is what failed, the commit says so itself and
    // the verdict finds nothing. Nothing is lost by asking.
    std::string              add_line;
    std::vector<std::string> add_args{"add", "--"};
    for (const std::string& p : pathspecs) add_args.push_back(p);
    run_git_mutate(add_args, add_line);  // advisory; `add_line` is the diagnostic

    std::string              commit_line;
    std::vector<std::string> commit_args{"commit", "-m", title, "--"};
    for (const std::string& p : pathspecs) commit_args.push_back(p);
    const bool commit_ran = run_git_mutate(commit_args, commit_line);

    // THE VERDICT IS THE REPOSITORY'S, and it identifies THE CHECKPOINT BY
    // CONTENT rather than blessing whatever child of `before` happens to stand
    // (find_checkpoint_commit owns the three legs, the sequences they close, and
    // why content identity — not act identity — is the ruled contract).
    std::string       observed;
    const std::string landed = find_checkpoint_commit(
        source_ref, before, title, base_name, paths, texts, observed);
    if (landed.empty()) {
        std::string why = observed;
        // The transport's own account, where it had one — a hook's rejection
        // message, a "nothing added to commit", or the deadline line — is
        // usually the actual explanation of the observation above, so it rides
        // along whether or not the child reached its own end. The commit's line
        // is preferred over the add's: it is the later step and the one whose
        // failure the walk was looking for.
        std::string transport = commit_line;
        if (transport.empty()) transport = add_line;
        if (!transport.empty()) why += " (git said: " + transport + ")";
        return commit_failed(why);
    }

    // The transport complained but the checkpoint is there — say both, since the
    // anomaly (a hook that hung, a push helper that could not start) is worth
    // knowing about even though it did not stop the act.
    if (!commit_ran && !commit_line.empty()) {
        std::fprintf(stderr,
                     "warptempo_gui: The commit landed as %s despite git "
                     "reporting: %s\n",
                     short_sha(landed).c_str(), commit_line.c_str());
    }

    // (c) The push. `publication_known` is irrelevant on this route — the
    // checkpoint was just made, so the line the flag forks is the "Committed"
    // one, which claims nothing about the remote — and false is the reading that
    // matches: nothing has asked the remote anything yet.
    return push_branch(landed, /*already_committed=*/false,
                       /*publication_known=*/false);
}
