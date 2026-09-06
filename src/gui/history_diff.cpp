#include "history_diff.h"

#include "app_state.h"
#include "device_config.h"   // shown_project_path (the card's name for a file)
#include "frame_format.h"
#include "history_prefetch.h"
#include "marker_measure.h"
#include "phaseresetmarkers.h"
#include "settings_io.h"
// marker_effectively_disabled, the one label-cascade owner — a header template
// over the parser marker shape, so reading it here touches no frozen .cpp.
#include "warp_frame_map_build.h"
#include "warpmarkers.h"
#include "warpmarkers_parse.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

// The child's environment is built from ours BEFORE THE SPAWN (see
// run_git_mutate), which needs the process environment by name.
extern "C" char** environ;

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
// resolve_repo_root_for_source below, `git -C <the source's parent> rev-parse
// --show-toplevel` canonicalized, and the answer TRAVELS AS A VALUE: every git
// call in this file takes its root as a parameter, so there is no fallback
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
// `HEAD` is the spelling because it needs no name — it is whatever this clone
// has checked out — and the same word serves the history WALK and the tip read
// that keys its freshness, so the mode's readers cannot come to mean different
// things. (It served the header's tip LISTING too until 2026-08-09, when the
// three-arm folder resolution went and where the piece lives became a question
// about the source path.)
//
// THE COMMIT ACT READS THIS SPELLING EXACTLY ONCE, to learn the branch NAME, and
// then names `refs/heads/<that name>` at both ends of its push refspec. Reading
// HEAD again at the push would let a checkout mid-act publish onto a branch the
// act never looked at; reading it once cannot. (The act's own reads went with the
// observation machinery on 2026-09-06 — it asks git one `status` and decides the
// rest on exit codes — so the name serves the publication alone now.) The
// read-only mode has no such exposure: it publishes nothing, and a checkout under
// it simply shows the branch that is now checked out.
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

// The one settings key this mode displays. Matched as a whole-line PREFIX, not
// as a substring: a key ENDING in `scale=` would contain this text without
// being this key, and until 2026-08-27 the sidecar shipped exactly such a key
// (`gui_scale=100`, which moved to the per-device config that day). The rule
// outlives its one demonstration.
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

// Milliseconds on a clock that cannot jump — the deadline's own time base.
long long monotonic_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000 +
           static_cast<long long>(ts.tv_nsec) / 1000000;
}

// WHAT A GIT INVOCATION ANSWERED — the ONE run verdict BOTH subprocess entry
// points return (2026-09-06, when the mutating runner below started reading the
// exit status it had spent a month ignoring by ruling). "The command failed" and
// "the command succeeded and said nothing" are the same empty string, and two
// callers are wrong when they are confused: a load-in-place would stage a
// sidecar a FAILED `show` invented as empty and replace the live store with a
// state the named commit never held, and the commit act's pre-flight would read
// a FAILED `status` as "nothing to commit" and tell the user the checkpoint
// already carries bytes that are in fact still uncommitted.
//
// THREE ANSWERS, EACH A DIFFERENT FACT ABOUT THE INVOCATION, and the split
// between the first two is the one only this enum can carry.
//
// `Failed` — GIT NEVER RAN, OR RAN AND WAS KILLED. The spawn's own refusal, a
// pipe or file-actions build that never got that far, a `waitpid` that did not
// answer with our own normally-exited child, BIONIC'S EXIT 127
// (run_git_capture's head owns the two spellings of could-not-exec), and — the
// mutating runner's own arm — a child that outlived the deadline and was
// SIGKILLed.
//
// `Exited` — GIT RAN AND REFUSED: it exec'd and exited NONZERO, which is git's
// own verdict on the question it was asked — a `rev-parse` outside a clone
// (128), a rejected pathspec, a locked index, a `commit` with nothing to commit
// (1), a `push` the remote rejected (1), a `pre-commit` hook saying no (1).
//
// WHAT `Ran` PROMISES: GIT WAS EXECUTED AND ITS EXIT STATUS WAS ZERO. The status
// is read at every call, by both entry points — SIGCHLD carries its default
// disposition, so it is always there to read.
//
// THE TWO FACES OF ONE ANSWER, which is what having a single enum for both entry
// points means. A CAPTURE pairs the verdict with `out`, git's stdout: `Ran`
// hands it over, and both other verdicts CLEAR it, since a failed command's
// stdout is never handed on. A MUTATION pairs it with `first_line`, git's first
// non-empty line over both streams, and that line is a DIAGNOSTIC ON EVERY
// VERDICT and never a witness — it is what the failure card carries, while the
// verdict alone says whether the mutation happened.
//
// ONE CAPTURE CALLER TELLS `Failed` FROM `Exited` (re-greped 2026-09-06): the
// ROOT DERIVATION, where "could not ask git which clone holds this folder" and
// "this folder is not inside a clone" are two different things to tell the user,
// and only the first is a read that did not answer. EVERY OTHER READER COMPARES
// AGAINST `Ran` ALONE — the checkpoint act's three mutations included, where a
// git that could not start, one that was killed at the deadline and one that
// refused all mean the same thing, that the step did not happen — so `Exited`
// falls exactly where `Failed` falls for all of them.
//
// WHAT `Ran` DOES NOT PROMISE is anything about a capture's OUTPUT: a `log` with
// no commits, a `rev-parse` that resolved nothing and an `ls-tree` of a tree
// with no matching path all exit zero and print nothing, so `Ran` with an empty
// string is an ordinary answer and not a success. A CAPTURE whose question is
// "did this succeed" needs a witness IN THE OUTPUT, and both of them have one:
// the commit act's status probe asks with `--branch`, whose header line is
// emitted only on a successful run, and the load-in-place cross-checks each blob
// against the byte count the tree listing stated. A MUTATION needs no such
// witness at all: its question is exactly the one the exit status answers.
enum class GitRun {
    Failed,  // git never ran, or ran and was killed
    Exited,  // git ran and exited nonzero
    Ran,     // git ran and exited zero
};

// Run `git -C <repo> <args...>` and capture its stdout.
//
// THE ONLY SUBCOMMANDS THIS ENTRY POINT EVER PASSES ARE `log`, `show`,
// `ls-tree`, `rev-parse`, `status`, `rev-list` AND `remote get-url`
// (re-derived from the invocations 2026-09-06): `log` and `ls-tree` are the
// display walk's own reads, `show` reads a commit's blob, `remote get-url` is
// the projects-home guard, `rev-parse` resolves a spelling — the
// load-in-place-from-a-commit path's and the branch name — `status` is the
// checkpoint act's pre-flight probe, and `rev-list --count` is the walk scan's
// emptiness read.
// `diff-tree` LEFT THE LIST on 2026-08-09 with the attribution walk that was its
// only caller (the act keeps no content signature; its own head owns that
// ruling), and `rev-list <sha> ^<tip>` on 2026-09-06 with the checkpoint act's
// containment observations. All of these are reads, and that
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
// GitRun). Both are answered from the CHILD'S EXIT STATUS and its stdout;
// there is no probe of ours in the child, because there is no code of ours in
// the child.
//
// THE CHILD IS SPAWNED, NEVER FORKED (2026-09-06, and the mutating entry point
// below does the same): this process carries several hundred megabytes of
// virtual address space, and fork() copies its page tables and marks every page
// copy-on-write, so the GUI thread faulted on its own writes once per git call —
// a stutter measurable across the startup walk's prefetch, which runs a git per
// commit. posix_spawnp runs the child under vfork semantics (no page-table copy,
// no COW) and the parent resumes only once the child has exec'd or failed. The
// redirections are a FILE-ACTIONS object the spawn applies for us, which is why
// nothing here has to be async-signal-safe and why the child cannot report
// anything of its own.
//
// COULD-NOT-EXEC HAS TWO SPELLINGS AND ONE VERDICT, and the verdict is what the
// callers read. glibc reports the exec's own failure as posix_spawnp's nonzero
// RETURN, reaping the failed child itself — no pid is handed out and nothing
// here may wait on one. bionic (the tablet) does not: its child `_exit(127)`s,
// so there the verdict is the EXIT STATUS, readable because SIGCHLD carries its
// default disposition and unambiguous because git's own failure codes are 1 and
// 128/129 for the reads below, never 127. Both spellings land on Failed, which
// is the case no output-shaped witness could ever cover: that git never ran —
// the 127 half being mapped at the status read below, the one site that sees it.
//
// `root` IS THE CLONE, and it is a parameter rather than a constant since
// 2026-08-11: it is derived from the loaded source and handed down through every
// caller, which is what lets a second host run the same binary against its own
// checkout.
GitRun run_git_capture(const std::string&              root,
                       const std::vector<std::string>& args,
                       std::string&                    out) {
    out.clear();

    std::vector<std::string> full;
    full.reserve(args.size() + 3);
    full.emplace_back("git");
    full.emplace_back("-C");
    full.emplace_back(root);
    for (const std::string& a : args) full.push_back(a);

    std::vector<char*> argv;
    argv.reserve(full.size() + 1);
    for (std::string& s : full) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);

    // BOTH ORIGINAL ENDS ARE CLOEXEC, which is what lets the child side be a
    // file-actions object with nothing to close by hand: the exec drops them,
    // while the descriptor the adddup2 puts on stdout is a fresh one that
    // carries no such flag and so survives into git.
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return GitRun::Failed;

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(fds[0]);
        close(fds[1]);
        return GitRun::Failed;
    }
    posix_spawnattr_t attr;
    if (posix_spawnattr_init(&attr) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        close(fds[0]);
        close(fds[1]);
        return GitRun::Failed;
    }

    // Stdout to the pipe, stderr to /dev/null (the head says why).
    int rc = posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    if (rc == 0) {
        rc = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO,
                                              "/dev/null", O_WRONLY, 0);
    }
    // POSIX_SPAWN_USEVFORK is BIONIC'S SWITCH: without it bionic forks, which is
    // the whole cost this conversion exists to remove. glibc has used CLONE_VFORK
    // unconditionally since 2.24 and ignores the flag.
    if (rc == 0) {
        rc = posix_spawnattr_setflags(
            &attr, static_cast<short>(POSIX_SPAWN_USEVFORK));
    }

    // The environment is inherited whole; this entry point pins nothing (the
    // mutating one below does, and says why).
    pid_t pid = -1;
    if (rc == 0) {
        rc = posix_spawnp(&pid, "git", &actions, &attr, argv.data(), environ);
    }
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);

    // A failed spawn is glibc's could-not-exec spelling and there is no child to
    // wait for; a failed actions/attr build never got that far. Same verdict.
    if (rc != 0) {
        close(fds[0]);
        close(fds[1]);
        return GitRun::Failed;
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

    // THE STATUS IS THE VERDICT, AND IT SPLITS TWO WAYS. The child is ours and
    // unreaped and SIGCHLD carries its default disposition, so the wait answers
    // with our own pid; zero is `Ran`, and a nonzero exit is `Exited` — git ran
    // and refused, which is an ANSWER and not the absence of one.
    //
    // A STATUS WE COULD NOT READ IS `Failed`, on the same footing as a spawn
    // that never happened: `Ran` promises a zero status and nothing here may
    // promise it blind, and a child that died on a SIGNAL never reached a
    // verdict of its own, so calling it a refusal would put words in git's
    // mouth — the root derivation below turns `Exited` into "this folder is not
    // a clone", and a killed `rev-parse` must never say that.
    //
    // 127 IS BIONIC'S COULD-NOT-EXEC AND IS MAPPED HERE, the one site that sees
    // it (the head owns the two spellings): bionic reports the exec's own
    // failure only through the child's `_exit(127)`, git itself never exits 127
    // for the reads above (its own codes are 1 and 128/129), and glibc never
    // yields it here at all, having reported the failure as posix_spawnp's
    // return before any pid was handed out.
    int   status = 0;
    pid_t w      = 0;
    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR);
    if (w != pid || !WIFEXITED(status)) {
        out.clear();
        return GitRun::Failed;
    }
    const int code = WEXITSTATUS(status);
    if (code == 0) return GitRun::Ran;

    // A failed command's stdout is never handed on, whichever verdict it takes.
    out.clear();
    return (code == 127) ? GitRun::Failed : GitRun::Exited;
}

// THE ORDINARY READING — `Ran` AND said something — which is what every caller
// wants whose question is answered by the output itself: a `log` with no commits,
// a `rev-parse` that resolved nothing and an `ls-tree` of a tree with no matching
// path are all "no history here" and all correctly fail this, as do both
// not-having-answered states (`Failed` and `Exited` alike, which this reading
// deliberately does not tell apart — the two callers that must are below).
// SEVEN CALL SITES (re-derived by grep 2026-09-06): the guard's two
// `remote get-url` reads, the
// per-commit `ls-tree`, two `rev-parse --verify`s, the walk's `log`, and
// `rev-parse --abbrev-ref`. THE WALK'S `log` IS ONE OF THEM AGAIN, and only
// because it is no longer the emptiness verdict: `rev-list --count` decides
// that, so by the time the log runs the count has said there ARE commits and
// silence from it is a contradiction rather than an empty history.
//
// FIVE CALLERS READ run_git_capture DIRECTLY INSTEAD (re-derived by grep
// 2026-09-06 — the count stood at four, five and seven while the list was EDITED
// rather than RE-DERIVED, which is the retell rule's own failure mode caught in
// this very comment; six call sites in the file, this helper being the sixth),
// and they fall in three groups.
//
// ONE ACCEPTS AN EMPTY ANSWER AS CONTENT: the load-in-place's BLOB reads
// (read_snapshot_at), where an empty sidecar is a valid whole file in both marker
// grammars — the tree listing's stated byte length is the second witness there.
//
// THREE JUDGE THE SHAPE of what arrived, which git_output's length test cannot
// do: the commit act's status pre-flight wants its `##` header
// (status_of_paths), the scan's `rev-list --count` wants a number ("0" is bytes,
// not silence), and the touched-directory evidence read wants a NUL-framed set of
// this piece's own sidecar paths, REFUSING an empty answer outright.
//
// ONE WANTS THE RAN-VERSUS-COULD-NOT-RUN BIT ITSELF, which is not a question
// about the output at all: the ROOT DERIVATION, whose two refusals ARE `Failed`
// and `Exited` (a `rev-parse` that could not run against one that ran and told us
// this folder is not in a clone).
bool git_output(const std::string& root, const std::vector<std::string>& args,
                std::string& out) {
    return run_git_capture(root, args, out) == GitRun::Ran && !out.empty();
}

// Run `git -C <repo> <args...>` for a MUTATING subcommand, and answer with the
// same GitRun verdict a capture gives, beside the first line git said about it.
//
// THE MUTATING INVENTORY IS `add`, `commit` AND `push` — the commit act's three
// steps, in that order — AND THAT ACT IS THIS FUNCTION'S ONLY CALLER. Nothing
// else in the product runs a git subcommand that changes a file, a ref or the
// index.
//
// THE EXIT STATUS IS THE ANSWER, WHICH IS THE STANDARD MODEL EVERY GIT FRONT-END
// USES (architect 2026-09-06, superseding the strict model of 2026-08-09 whole:
// "we prefer parsimony in code ... remove git custom and use posix_spawn's
// version"). The status was unreadable when that model was written — SIGCHLD was
// ignored — so the act decided its successes by OBSERVING the repository
// afterwards instead: a moved branch tip for the commit, a remote-tracking ref
// carrying the checkpoint for the push. The spawn conversion made the status
// readable at both entry points, this repository runs no hooks, and the
// observation machinery was the last asymmetry between them; it is deleted, and
// what a caller reads here is what git said about its own run.
//
// SO THE THREE VERDICTS MEAN EXACTLY WHAT THEY MEAN ABOVE (GitRun owns the
// contract): `Ran` is git having exited zero and IS the step having happened,
// `Exited` is git's own refusal — a `commit` with nothing to commit, a
// `pre-commit` hook saying no, an identity or signing failure, a rejected push —
// and `Failed` is git never having run, or having outlived the deadline below
// and been killed. The act treats the two failing verdicts alike; what it wants
// from each is `first_line`.
//
// `first_line` IS A DIAGNOSTIC ON EVERY VERDICT, never a witness: git's own first
// non-empty line, which is what the failure card and the stderr line carry. On
// the deadline arm it is the timeout sentence instead, this function's own words
// being the only account of a run nobody let finish.
//
// STDOUT AND STDERR SHARE ONE PIPE, so `first_line` is git's own first non-empty
// line whichever stream it chose (`commit` reports on stdout, `push` on stderr).
//
// COULD-NOT-EXEC HAS TWO SPELLINGS AND ONE VERDICT HERE TOO, the asymmetry the
// old model recorded rather than patched having gone with it: glibc reports the
// exec's own failure as posix_spawnp's nonzero RETURN, bionic as the child's
// `_exit(127)`, and both land on `Failed` — git's own codes for these three
// subcommands are 1 and 128, never 127. (The tablet, the one bionic host, carries
// no git binary at all and greys the checkpoint act.)
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
// and returns `Failed` with the timeout as `first_line`.
//
// THE ENVIRONMENT IS BUILT BEFORE THE SPAWN, which is not a style choice: the
// child runs no code of ours at all, so there is no side on which a setenv could
// happen — posix_spawnp takes a finished envp and the exec itself installs it.
// Any inherited GIT_TERMINAL_PROMPT is dropped rather than shadowed, so there is
// exactly one such entry and no question of which one is read.
GitRun run_git_mutate(const std::string&              root,
                      const std::vector<std::string>& args,
                      std::string&                    first_line) {
    first_line.clear();

    std::vector<std::string> full;
    full.reserve(args.size() + 3);
    full.emplace_back("git");
    full.emplace_back("-C");
    full.emplace_back(root);
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

    // CLOEXEC on both original ends, as in the capture helper: the exec drops
    // them and the two adddup2'd descriptors are the only ones that reach git,
    // so no descendant of the child — an ssh, a hook — can hold the write end
    // open past its own exit.
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return GitRun::Failed;

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(fds[0]);
        close(fds[1]);
        return GitRun::Failed;
    }
    posix_spawnattr_t attr;
    if (posix_spawnattr_init(&attr) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        close(fds[0]);
        close(fds[1]);
        return GitRun::Failed;
    }

    // BOTH output streams to the pipe, stdin to /dev/null.
    int rc = posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    if (rc == 0) {
        rc = posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    }
    if (rc == 0) {
        rc = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO,
                                              "/dev/null", O_RDONLY, 0);
    }

    // ITS OWN PROCESS GROUP, so the deadline below has something to kill that
    // covers the whole act. Git is not a leaf: a push spawns ssh, and any of the
    // three can run a hook, so killing the git process alone would leave exactly
    // the thing that was hanging — the ssh, the credential helper, the hook —
    // orphaned and still holding whatever it was holding. POSIX_SPAWN_SETPGROUP
    // with a group of 0 makes the child lead its own group, so its pid IS the
    // group id and the parent needs no second handle; and under vfork semantics
    // the parent does not resume until the child has exec'd, so the group is
    // established before any kill of ours can run.
    //
    // POSIX_SPAWN_USEVFORK is bionic's switch — see the capture helper.
    if (rc == 0) {
        rc = posix_spawnattr_setflags(
            &attr, static_cast<short>(POSIX_SPAWN_SETPGROUP |
                                      POSIX_SPAWN_USEVFORK));
    }
    if (rc == 0) rc = posix_spawnattr_setpgroup(&attr, 0);

    pid_t pid = -1;
    if (rc == 0) {
        rc = posix_spawnp(&pid, "git", &actions, &attr, argv.data(),
                          envp.data());
    }
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);

    // COULD IT BE STARTED AT ALL — glibc's own answer, and no child exists to
    // wait for on this road. (Bionic answers with the child's 127 instead, which
    // the status read below maps onto this same verdict.)
    if (rc != 0) {
        close(fds[0]);
        close(fds[1]);
        first_line = "Git could not be started";
        return GitRun::Failed;
    }

    close(fds[1]);
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
        // alone: see the process group above.
        //
        // AND THE PARENT REAPS ITS OWN CHILD. SIGCHLD carries its default
        // disposition, so nothing discards the status for us and an unwaited
        // child would sit as a zombie until the process exits. The kill is
        // immediate, so the wait does not block meaningfully; the status is
        // read only to consume it.
        kill(-pid, SIGKILL);
        int   killed_status = 0;
        pid_t killed_w      = 0;
        do {
            killed_w = waitpid(pid, &killed_status, 0);
        } while (killed_w < 0 && errno == EINTR);
        (void)killed_w;
        (void)killed_status;
        first_line = "Timed out after " +
                     std::to_string(kMutateDeadlineMs / 1000) +
                     " seconds and was killed";
        return GitRun::Failed;
    }

    // THE STATUS IS THE VERDICT, read exactly as the capture helper reads it: the
    // child is ours and unreaped and SIGCHLD carries its default disposition, so
    // the wait answers with our own pid. A status we could not read, a child that
    // died on a SIGNAL and bionic's could-not-exec 127 all land on `Failed`,
    // which is the honest verdict for a run that reached no verdict of its own.
    int   status = 0;
    pid_t w      = 0;
    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR);

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

    if (w != pid || !WIFEXITED(status)) return GitRun::Failed;
    const int code = WEXITSTATUS(status);
    if (code == 0) return GitRun::Ran;
    return (code == 127) ? GitRun::Failed : GitRun::Exited;
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
    // MEASURES ARE PART OF THE GRAMMAR HERE: these are the same on-disk lines
    // the loader accepts, so the measure suffix is accepted too. Refusing it
    // would drop every measured marker on the whitespace refusal and vanish
    // it from the diff lane entirely.
    auto parsed = warpmarkers_internal::parse_single_canonical_line(
        line, /*accept_measure=*/true);
    if (!parsed) return false;
    out.frame    = parsed->time_frame;
    out.disabled = parsed->disabled;
    const std::size_t pipe = line.find('|');
    // The parse succeeded, so the '|' is there; the guard is defensive. The
    // slice is rest-of-line, so any measure suffix rides inside the token —
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
        auto parsed = warpmarkers_internal::parse_single_canonical_line(
            line, /*accept_measure=*/true);
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
// mirrors it exactly rather than relaxing anything: the ` //<measure>` suffix
// off first, then no whitespace anywhere in what remains, an optional leading
// '#' meaning disabled, then the CANONICAL authored frame spelling
// (parse_authored_frame, frame_format.h) and nothing else. A byte-empty line
// has no frame and is refused here just as it is at load; comment LINES are
// not in the grammar.
//
// THE SPLIT IS NOT MIRRORED — it comes from marker_measure.h, the shared
// header written so exactly one spelling of it exists. The frame parse below
// remains this module's own hand-mirror of the loader's, the standing recorded
// wart; the split deliberately does not join it.
bool extract_phase_reset_entry(const std::string&         line,
                               GuiHistoryPhaseResetEntry& out) {
    const MarkerMeasureSplit split = split_marker_measure(line);
    std::string_view t = split.prefix;
    out.measure.clear();
    if (split.had_measure) {
        std::string measure_err;
        if (!validate_marker_measure(split.measure, measure_err)) return false;
        out.measure.assign(split.measure);
    }
    if (t.find_first_of(" \t\r") != std::string_view::npos) return false;
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

// A FULL OBJECT NAME, in git's own lower-case hex spelling. Two readers, and
// both are checking git's answer against the SHAPE it promised rather than
// validating user input: read_commit_sidecars, where `rev-parse --verify` with
// the peel suffix yields exactly one, and the walk's enumeration, where every
// `--format=%H` line is one.
bool is_hex40(const std::string& s) {
    if (s.size() != 40) return false;
    for (const char c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) return false;
    }
    return true;
}

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
// everything past it is the basename rule. ONE CALLER SINCE 2026-08-09 — each
// commit's own tree, at resolve_commit_paths — the header's tip listing having
// gone with the three-arm resolution, since where the piece lives is answered
// from the source path now. It stays a shared rule in shape rather than in
// arithmetic: the walk's pathspecs say the same "under projects/, by basename"
// in git's own grammar (sidecar_glob_pathspecs), and the two must keep agreeing.
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
// ITS SIX REASONS ARE LOWERCASE, like every other reason in this file: both of
// its consumers APPEND (the mode's entry composes "History is unavailable: " and
// the push composes stderr's "Push refused: "), and an appended reason does not
// start a second sentence — the rule is stated once in messaging.md's card
// section, over the product's one statement of the text rules at
// paint_handler.cpp's menu-row block. They were capitalized until 2026-09-01.
// AND EACH IS TWO CLAUSES (GuiFailure, failure.h — 2026-09-02, the four-tier
// review's R-11, the universal shape): the clone's FULL path on the
// diagnostic for stderr, its folder name on the display for the card
// (clone_name below), the setting's own spelling and the remote's URL on
// both — those are not paths on this disk.
bool clone_is_projects_home(const std::string& repo_root,
                            const std::string& projects_repo,
                            GuiFailure&        reason,
                            std::string*       destination = nullptr) {
    reason = GuiFailure{};
    if (destination != nullptr) destination->clear();
    const std::string setting_norm = normalize_repo_url(projects_repo);
    if (setting_norm.empty()) {
        reason = plain_failure("the projects_repo setting is empty");
        return false;
    }
    const std::filesystem::path root(repo_root);
    const std::string shown_root = clone_name(repo_root);

    std::string remote_raw;
    if (!git_output(repo_root, {"remote", "get-url", "origin"}, remote_raw)) {
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

    std::string push_raw;
    if (!git_output(repo_root, {"remote", "get-url", "--push", "--all", "origin"},
                    push_raw)) {
        reason = path_failure("the clone at ", root, shown_root,
                              " states no push URL for 'origin'");
        return false;
    }
    std::string first_push_url;
    for (const std::string& line : split_lines(push_raw)) {
        const std::string one = trim_trailing_ws(line);
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
    return true;
}

// ---------------------------------------------------------------------------
// per-commit snapshot
// ---------------------------------------------------------------------------

// One commit's committed path and blob size for each of the three sidecars,
// empty/-1 where that commit carries none. Indexed to match kSidecarExtensions.
//
// `ambiguous` is a REFUSAL, not a variant of "carries none": the commit CHANGED
// this base name in two or more directories, so which piece the blobs belong to
// has no answer (2026-08-09 — it was a tree-containment judgment against the
// session's own directory until the evidence became commit-local; no checkpoint
// this program makes touches two folders, so it is somebody's hand commit).
// `no_touch_evidence` below is its sibling and the opposite fact — an answer
// about the commit versus the absence of one. All three paths are empty in
// either state and read_commit_sidecars refuses on both — which the walk's load
// gate counts as ineligibility (neither kind ever enters the walk) and the `'`
// act prints as its own refusal, the arm a pasted spelling naming a commit
// outside the walk keeps live.
struct GuiHistoryCommitPaths {
    std::string path[3];
    long long   size[3] = {-1, -1, -1};
    bool        ambiguous = false;
    // The commit named NO directory it touched for this base name. Distinct
    // from `ambiguous` because it is a different fact and deserves a different
    // line: ambiguity is an answer about the commit, this is the absence of one.
    // It is ONE flag for all of its producers (a commit that touches none of
    // these paths, a merge git would not diff, a `show` that ran and failed),
    // which the evidence reader's comment argues are indistinguishable by
    // design and must not be guessed between.
    bool        no_touch_evidence = false;
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
// THE WALK'S THREE PATHSPECS, built once for every asking (2026-08-09, when the
// touched-directory read below became a second consumer). `:(glob)` because the
// `projects/**/` lead is a real wildcard and the base name is NOT — a source
// legitimately called `take*.wav` would otherwise widen every asking to every
// `take<anything>` sidecar in the corpus, which is what escape_glob prevents.
// Glob magic and literal magic are mutually exclusive, so this is the one
// pathspec family in the module that cannot be `:(literal)`.
std::vector<std::string> sidecar_glob_pathspecs(const std::string& base_name) {
    const std::string        escaped = escape_glob(base_name);
    std::vector<std::string> out;
    out.reserve(3);
    for (const char* ext : kSidecarExtensions) {
        out.push_back(std::string(":(glob)") + std::string(kProjectsPrefix) +
                      "**/" + escaped + ext);
    }
    return out;
}

// WHICH DIRECTORIES THIS COMMIT ACTUALLY TOUCHED for the base name — the
// evidence the per-commit path resolution below is built on (2026-08-09).
//
// THE TREE ALONE CANNOT ANSWER "WHICH FOLDER IS THIS COMMIT ABOUT", and that is
// the whole reason this exists: a commit CONTAINS every folder the piece has
// ever lived in, because the checkpoint act is pathspec-scoped and never deletes
// the folder a piece moved out of. So a commit made from a BACKUP copy still
// carries the original's untouched blobs, and a commit made after a rename still
// carries the pre-rename folder's. Choosing by containment showed the wrong
// folder's state for the first and refused the second as ambiguous; choosing by
// what the commit CHANGED answers both, because a checkpoint changes exactly the
// folder it was made from.
//
// IT IS `show`, NOT `log -1`, AND THAT IS THE WHOLE CORRECTNESS OF IT: `-1`
// limits how many commits a WALK reports, not which commit is asked about, so
// `git log -1 <sha> -- <pathspecs>` whose own commit touched nothing matching
// walks on to the nearest ANCESTOR that did and reports ITS paths. Measured: on
// a commit touching only unrelated files, `log -1` answered the previous
// checkpoint's sidecar paths while `show` answered nothing. `git show` names one
// commit and walks no ancestry.
//
// THE RENAME CONFIG IS PINNED IN THE ARGV, `-c diff.renames=false` (the push's
// pinned pushurl is the precedent for injecting config a read must not inherit).
// Rename DETECTION would otherwise make this answer depend on the user's own
// `diff.renames`: measured, a `git mv` of a project folder answers the NEW
// directory alone with detection on and BOTH the old and the new with it off.
// Pinned off is raw adds and deletes with no inference — one answer whatever the
// clone is configured to do — and IT COSTS THE SANCTIONED PATH NOTHING, verified
// end to end: a folder renamed in a FILE MANAGER makes no commit at all, and the
// act's next checkpoint is pathspec-scoped to the three NEW paths, so it ADDS
// them and deletes nothing (the old folder stays in the tree, which is what keeps
// the pre-rename era's own commits resolvable). That commit answers the new
// directory alone under the pin. A `git mv` done by hand in a terminal answers
// two directories and takes the ambiguity arm — unsanctioned, blunt, correct.
//
// `--name-only` WITH `-z` AND AN EMPTY `--format` yields nothing but the matched
// paths, NUL-terminated and unquoted, so `core.quotePath` cannot mangle a UTF-8
// name and the split is the ls-tree reader's own. Measured: a ROOT commit answers
// its own paths (git diffs it against the empty tree), a commit touching TWO
// folders at once answers both, and a DELETION-only commit answers the folder it
// emptied.
//
// AN EMPTY ANSWER IS NOT A MERGE, AND NOT ANY OTHER SINGLE THING — it is the
// module's own "no answer", and the caller treats it as one. THREE PRODUCERS,
// INDISTINGUISHABLE BY DESIGN: a commit that genuinely touched none of these
// paths, a merge whose diff git suppressed (usually — the default merge display
// is a COMBINED diff, so an EVIL merge that changed a sidecar relative to both
// parents does emit the path, which is why silence was never a witness for
// "this is a merge"), and a `show` that ran and FAILED — which the capture
// layer now names `Exited` and this reader folds in with the rest, since it
// takes `!= Ran` and there is nothing different to do about it. Nothing
// downstream can tell the three apart and nothing should try: each means this
// program cannot say which folder the commit is about.
//
// THE ANSWER IS ACCEPTED ONLY IN A WELL-FORMED SHAPE, and the shape is derived
// from what a checkpoint can actually be rather than assumed. THE ACT WRITES ONE
// FOLDER'S THREE SIDECARS AND COMMITS PATHSPEC-SCOPED TO EXACTLY THOSE THREE
// (checkpoint_paths, and the commit argv beside it), SO THE COMMIT TOUCHES A
// NONEMPTY SUBSET OF ONE FOLDER'S THREE — a SUBSET, not the three: `git commit`
// records only what actually changed, and a checkpoint whose phase resets and
// settings came out byte-identical touches ONE file. Measured, not reasoned: an
// ordinary second checkpoint with only the warp markers moved reports exactly
// one path. So "exactly three" would refuse the product's own commonest commit,
// and the rule is instead every record RECOGNIZED, none repeated, and at most a
// folder's three — plus the framing check below, which is what a truncation
// actually breaks.
//
// THE RESIDUAL IS ADVERSARIAL AND ACCEPTED, recorded rather than defended: a
// child that dies exactly ON a record boundary leaves a well-framed PREFIX, so a
// multi-directory answer could still be read as the single directory it began
// with. Reaching it needs an unsanctioned multi-directory commit AND a
// deterministic death at exactly that byte — the hand-broken-repository
// category, which the sanctioned-use ruling leaves alone. NO SECOND READ AND NO
// FURTHER WITNESS is the deliberate stopping point: another child would be
// another thing to disagree with itself.
//
// IT COSTS ONE EXTRA CHILD PER CANDIDATE in the prefetch scan, beside the
// rev-parse, the ls-tree and the three shows the load gate already runs. That is
// the deliberate price of ONE resolution owner: the walk and the `'` act reach
// this through the same call, so a member can never display one folder's
// snapshot and load another's — which is exactly the divergence the containment
// rule produced.
struct GuiHistoryTouchedDirs {
    // The read ran AND came back in the sanctioned shape, naming at least one
    // directory. False covers a capture that could not run, one that answered
    // nothing, and one whose answer was malformed or truncated — deliberately
    // together: the reader's comment owns why those producers are indis-
    // tinguishable and must be one outcome.
    bool                     ok = false;
    // One entry per directory named, in first-seen order. Its SIZE is the whole
    // decision at the caller: one is the answer, more is ambiguity.
    std::vector<std::string> dirs;
};

GuiHistoryTouchedDirs touched_directories_of_commit(const std::string& repo_root,
                                                    const std::string& sha,
                                                    const std::string& base_name) {
    GuiHistoryTouchedDirs out;
    std::vector<std::string> args{"-c",     "diff.renames=false",
                                  "show",   "-z",
                                  "--format=", "--name-only",
                                  sha,      "--"};
    for (std::string& p : sidecar_glob_pathspecs(base_name)) {
        args.push_back(std::move(p));
    }
    std::string raw;
    // The tri-state is read directly rather than through git_output because the
    // helper's "said something" reading would fold a could-not-exec into an
    // empty answer — and here they are the same OUTCOME but the distinction is
    // still not the helper's to make silently.
    if (run_git_capture(repo_root, args, raw) != GitRun::Ran) return out;
    if (raw.empty()) return out;

    // THE FRAMING IS THE TRUNCATION WITNESS, and it is the one this reader can
    // have. A nonempty answer that does not END in a NUL is a record git was
    // still writing when the child died: `-z` terminates every path, so a
    // well-formed stream cannot end any other way. Without this a PREFIX of a
    // multi-directory answer read as a clean single-directory one — the
    // truncated-log defect one layer down, and the reason a nonempty output is
    // no longer trusted on its length alone.
    if (raw.back() != '\0') return out;

    // EVERY RECORD MUST BE ONE OF THIS PIECE'S SIDECARS, at a directory strictly
    // below `projects/`, and no path may repeat — a partial path left by a death
    // inside a record fails the extension match, a stray path fails the shape,
    // and a duplicate is not something git emits. Any deviation is NOT AN ANSWER
    // rather than a smaller one.
    std::vector<std::string> seen;
    for (const std::string& path : split_on(raw, '\0')) {
        // AN EMPTY RECORD IS A DEVIATION LIKE ANY OTHER, not something to skip
        // past: this split yields one for a LEADING NUL or either half of a
        // DOUBLED one, and skipping it would let `<path>\0\0` pass the
        // all-records grammar with the tail test satisfied. An ordinary single
        // trailing NUL produces no trailing element here, so well-formed output
        // never reaches this arm.
        if (path.empty()) return GuiHistoryTouchedDirs{};
        const std::string dir = directory_of(path);
        if (dir.size() <= kProjectsPrefix.size() ||
            std::string_view(dir).substr(0, kProjectsPrefix.size()) !=
                kProjectsPrefix) {
            return GuiHistoryTouchedDirs{};
        }
        bool named = false;
        for (const char* ext : kSidecarExtensions) {
            if (path == dir + "/" + base_name + ext) { named = true; break; }
        }
        if (!named) return GuiHistoryTouchedDirs{};
        if (std::find(seen.begin(), seen.end(), path) != seen.end()) {
            return GuiHistoryTouchedDirs{};
        }
        seen.push_back(path);
        if (std::find(out.dirs.begin(), out.dirs.end(), dir) ==
            out.dirs.end()) {
            out.dirs.push_back(dir);
        }
    }
    if (out.dirs.empty()) return GuiHistoryTouchedDirs{};
    // A DIRECTORY CANNOT CARRY MORE THAN ITS THREE SIDECARS, so more records
    // than three per directory is a shape no repository state produces.
    if (seen.size() > out.dirs.size() * 3) return GuiHistoryTouchedDirs{};
    out.ok = true;
    return out;
}

// THE RULE IS THE DIRECTORY THIS COMMIT TOUCHED, AND THERE IS NO OTHER RULE
// (2026-08-09). It superseded "the session's own directory first", which
// preferred `head_directory` whenever the commit's TREE carried the base name
// there — which it almost always does, the act being pathspec-scoped and never
// deleting a folder the piece has moved out of, so containment answered about
// folders the commit never changed: a checkpoint made from a BACKUP copy
// displayed the ORIGINAL folder's unchanged blobs as its own state, and after a
// rename the pre-rename era's commits carried two candidates and hid as
// ambiguous.
//
// AND THEN THE CONTAINMENT FALLBACK WENT TOO, the same day and for the same
// reason carried one step further: kept as the answer for "no touch evidence",
// it LAUNDERED SILENCE INTO SUCCESS on every shape that produced silence.
// Measured, both: a pasted spelling naming an ordinary commit that touched only
// unrelated files answered nothing, fell back, found `head_directory` in that
// commit's tree and loaded all three blobs — reporting a SUCCESSFUL load of a
// snapshot the commit is not about; and a `show` that RAN AND FAILED is
// Ran-with-empty too, so the same fallback fired with nothing verified at all.
// Under sanctioned use every candidate is an act-made commit touching exactly
// one folder's three files, so the fallback only ever served shapes the model
// does not have — which makes deleting it the conversion rather than guarding it.
//
// SO THE EVIDENCE RULES ARE EXHAUSTIVE AND THERE ARE THREE:
//   ONE directory  — that is the answer.
//   TWO OR MORE    — genuinely ambiguous, and no checkpoint this program makes
//                    touches two folders, so it is somebody's hand commit; the
//                    walk hides it on the counted line's terms and `'` refuses.
//   NONE           — NOT AN ANSWER, whatever produced it (the reader's comment
//                    owns the three indistinguishable producers). Same hide,
//                    same refusal, one message.
// `head_directory` is gone from this function and from the two above it, having
// no other reader; the ls-tree STAYS, its blob sizes being the truncation
// witness read_commit_sidecars checks each `show` against.
//
// THE WALK-SIDE CONSEQUENCE IS NEARLY UNREACHABLE, and that is the point: a
// candidate came off a `log` over these very pathspecs, so it touched one of
// them by construction, and an empty answer there is a CONTRADICTION — object
// damage, or a merge git listed and then would not diff. Hidden, counted, blunt.
GuiHistoryCommitPaths resolve_commit_paths(const std::string& repo_root,
                                           const std::string& sha,
                                           const std::string& base_name) {
    GuiHistoryCommitPaths out;
    const GuiHistoryTouchedDirs touched =
        touched_directories_of_commit(repo_root, sha, base_name);
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
    // below report as the missing sidecar it is.
    const std::string& chosen = touched.dirs.front();

    std::string listing;
    if (!git_output(repo_root, {"ls-tree", "-r", "-z", "-l", sha}, listing)) {
        return out;
    }
    const std::vector<GuiHistoryTreeEntry> hits =
        sidecar_entries_in_listing(listing, base_name);
    if (hits.empty()) return out;

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
bool read_snapshot_at(const std::string& repo_root, const std::string& sha,
                      const std::string& path, std::string& out) {
    out.clear();
    return run_git_capture(repo_root, {"show", sha + ":" + path}, out) ==
           GitRun::Ran;
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

    // AN EMPTY ROOT WOULD MEAN THE WORKING DIRECTORY to `git -C`, which is a
    // silently different repository — so it refuses here with the other two
    // missing inputs rather than being handed to a child.
    if (repo_root.empty()) return refuse("the source's clone is not known");
    if (spelling.empty())  return refuse("no commit was named");
    if (base_name.empty()) return refuse("the source has no sidecar base name");

    // `--verify` makes a non-resolving spelling an error rather than an echo of
    // the argument, and `^{commit}` peels whatever resolved to a commit — a tag
    // or a tree spelling that is not one fails here rather than downstream.
    std::string raw;
    if (!git_output(repo_root, {"rev-parse", "--verify", spelling + "^{commit}"},
                    raw)) {
        failure = path_failure("'" + spelling + "' does not name a commit in ",
                               std::filesystem::path(repo_root),
                               clone_name(repo_root), "");
        return false;
    }
    const std::string sha = trim_trailing_ws(raw);
    // Defensive shape check on git's own answer: --verify with the peel suffix
    // yields exactly one full object name, so anything else means the assumption
    // broke rather than that the user typed something odd.
    if (!is_hex40(sha)) {
        return refuse("'" + spelling + "' did not resolve to a single commit");
    }
    out.sha = sha;

    // That commit's OWN tree decides where the sidecars sit — the same
    // basename match the walk uses, applied to an arbitrary commit.
    const GuiHistoryCommitPaths paths =
        resolve_commit_paths(repo_root, sha, base_name);
    if (paths.no_touch_evidence) {
        return refuse("commit " + short_sha(sha) +
                      " does not touch this piece's sidecars");
    }
    if (paths.ambiguous) {
        return refuse("commit " + short_sha(sha) + " changed '" + base_name +
                      ".*' in more than one directory, so which piece it "
                      "names has no answer");
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
        if (!read_snapshot_at(repo_root, sha, paths.path[e], blobs[e]->text)) {
            return refuse("could not read '" + paths.path[e] +
                          "' at commit " + short_sha(sha));
        }
        if (paths.size[e] >= 0 &&
            static_cast<long long>(blobs[e]->text.size()) != paths.size[e]) {
            return refuse("'" + paths.path[e] + "' at commit " +
                          short_sha(sha) + " read back " +
                          std::to_string(blobs[e]->text.size()) +
                          " bytes where the tree lists " +
                          std::to_string(paths.size[e]));
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
bool load_commit_sidecars_strict(const std::string&    repo_root,
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

    if (!read_commit_sidecars(repo_root, spelling, base_name,
                              out.sidecars, failure)) {
        return false;
    }
    const GuiHistoryCommitSidecars& snap = out.sidecars;

    // A PARTIAL COMMIT IS A REFUSAL: a load-in-place is a whole-state replace,
    // and inheriting two files from the commit and the third from nowhere
    // would compose a state no checkpoint ever was. (For the walk the same
    // refusal is simple ineligibility: a checkpoint that cannot be loaded is
    // not stepped to.)
    auto missing = [&](const char* ext) {
        return refuse("commit " + snap.sha + " carries no '" + base_name +
                      ext + "'");
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
    if (!stage(snap.settings, ".settings", settings_file))       return false;
    if (!stage(snap.warpmarkers, ".warpmarkers", warp_file))     return false;
    if (!stage(snap.phaseresetmarkers, ".phaseresetmarkers",
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

// THE SETTINGS WRITER'S GUI HALF, HELD BY VALUE — the storable form of the
// call-shaped NonEngineSettingsSnapshot (which borrows a ViewState pair and a
// string). It is opaque in the header because ViewState is app_state.h's and
// this module is included BY that header; nothing outside this file needs its
// shape.
struct GuiHistoryGuiSide {
    ViewState   tab_a;
    ViewState   tab_b;
    bool        follow              = false;
    bool        centered            = false;
    bool        center_on_next_marker = true;
    char        active_audio_view   = 'S';
    char        active_markers_view = 'W';
    char        active_tab_view     = 'A';
    int         waveform_magnification_level = 0;
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
    gui->centered            = app.centered_mode;
    gui->center_on_next_marker = app.center_on_next_marker;
    gui->active_audio_view   = app.active_audio_view;
    gui->active_markers_view = app.active_markers_view;
    gui->active_tab_view     = app.active_tab_view;
    gui->waveform_magnification_level = app.waveform_magnification_level;
    return gui;
}

std::string format_history_settings_text(const GuiHistoryGuiSide& gui,
                                         const EngineSettings&    engine) {
    const NonEngineSettingsSnapshot snap{
        gui.tab_a, gui.tab_b, gui.follow, gui.centered,
        gui.center_on_next_marker,
        gui.active_audio_view, gui.active_markers_view, gui.active_tab_view,
        gui.waveform_magnification_level};
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
// `git -C <the source's parent> rev-parse --show-toplevel` asks git itself which
// clone the loaded file is in, which is the clone whose `projects/` the piece
// must sit under and the clone a checkpoint commits into. THE DIRECTORY IS THE
// SOURCE'S PARENT because `-C` takes a directory, and the source is
// CANONICALIZED WHOLE FIRST for the same reason project_directory_of_source does
// it that way: a source named as a bare filename has no parent to hand git, and
// canonicalizing against the working directory is what makes a program launched
// from inside the piece's folder answer that folder's clone.
//
// THE TWO REFUSALS ARE TOLD APART BY THE CAPTURE'S TRI-STATE, and this is the
// one caller that reads all three of its states. A `rev-parse` that RAN AND
// EXITED NONZERO is the ruled NOT-A-CLONE — that is exactly what git does
// outside a repository, measured on git 2.55: exit 128 with nothing on stdout,
// for a folder in no clone and for a `.git` with no work tree alike — and its
// fix is a clone or a file move, not a `read_failed`. One that COULD NOT RUN at
// all, or whose answer is not an existing directory, is a READ THAT DID NOT
// ANSWER and says so through `read_failed`, which the scan turns into a not-ok
// run so an unread repository never passes for a read one.
//
// THERE IS NO EMPTY-OUTPUT ARM, and the reason is that nothing produces one: a
// `--show-toplevel` that exits ZERO has named a work tree, and every shape that
// has none refuses with 128 instead (both measured above). An error arm exists
// iff a producer exists (validation_topology.md), so the arm that stood here
// until 2026-09-06 — written when a nonzero exit was indistinguishable from a
// silent success — is folded into the `Exited` refusal above rather than kept
// as an unreachable second road onto it. Were a future git to print nothing and
// exit zero anyway, the empty spelling falls through to the not-a-directory
// refusal below, which is a read that did not answer: conservative, and the
// direction that never launders silence into a clone.
//
// THE ANSWER IS CANONICALIZED before it leaves: git prints a real absolute path,
// and canonicalizing it once here is what lets every consumer — the
// project-directory containment test above all — compare against it without
// asking again.
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

    // The `-C` here is the SOURCE'S FOLDER rather than a root — this is the one
    // call in the file that runs somewhere it has not been told about, which is
    // the whole point of it.
    std::string out;
    const GitRun capture =
        run_git_capture(dir, {"rev-parse", "--show-toplevel"}, out);
    if (capture == GitRun::Failed) {
        r.read_failed = true;
        r.reason = path_failure("could not ask git which clone holds ",
                                dir_path, dir_name, "");
        return r;
    }
    if (capture == GitRun::Exited) {
        // Git answered, and the answer is no. NOT a `read_failed`: a project
        // folder outside every clone is a supported configuration (projects_path
        // need not be under the clone), and the mode's local fallback is what it
        // gets.
        r.reason = path_failure(
            "the source's folder is not inside a git clone: ", dir_path,
            dir_name, "");
        return r;
    }
    const std::string toplevel = trim_trailing_ws(out);

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
// CLONE and WHERE THE PIECE LIVES, or why neither can be found, in three git
// calls and no strict load, and it PRINTS NOTHING: the caller decides whether
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

    // THE CLONE FIRST, because every question below it is asked of a repository
    // and there is no repository until this answers (architect 2026-08-11,
    // replacing the compiled-in path's is_directory probe: the root is derived
    // from the loaded source now, so "which clone" is a real question with two
    // real refusals rather than a check on a constant).
    const GuiHistoryRepoRoot root =
        resolve_repo_root_for_source(source_audio_path);
    if (!root.ok) return unavailable(root.reason, root.read_failed);
    h.repo_root = root.path;

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
    // `origin/main` and onto the local branch (kBranchRef) left this untouched.
    // A clone that has not fetched in a year is still THIS repository, and a
    // checkpoint this product commits and fails to push is still its history.
    //
    // IT VALIDATES EVERY EFFECTIVE PUSH DESTINATION, not the fetch URL alone —
    // through clone_is_projects_home, whose comment owns the whole rule and
    // which the COMMIT ACT asks again immediately before its push. THIS SITE IS
    // THE MODE'S GATE; that one is the mutating boundary. Two askings because
    // the config can move between them, one owner because the question is one.
    GuiFailure guard_reason;
    if (!clone_is_projects_home(h.repo_root, projects_repo, guard_reason)) {
        return unavailable(std::move(guard_reason));
    }

    // The sidecar base name is the source's own stem — the single derivation
    // rule the loader uses when it builds <base>.warpmarkers and its two
    // siblings beside the WAV (file_loader.cpp's companion-file block). The
    // corpus names its files by exactly that, so mirroring the rule is what
    // makes the filename match work on names full of periods and commas.
    h.base_name = std::filesystem::path(source_audio_path).stem().string();
    if (h.base_name.empty()) {
        const std::filesystem::path given(source_audio_path);
        return unavailable(path_failure("the source path has no base name: ",
                                        given, shown_project_path(given),
                                        ""));
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
    // committed-match arm used to be the answer to: the walk's pathspecs are
    // `:(glob)projects/**/<base>.<ext>`, so a piece's history is every commit
    // that touched a file by that name ANYWHERE under `projects/`, and a folder
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

std::string read_history_branch_tip_sha(const std::string& source_audio_path) {
    // IT DERIVES THE ROOT ITSELF, both its callers asking before any header
    // exists (the declaration owns why). A derivation that refuses answers the
    // same empty string an unreadable tip does, which is what every caller
    // already handles.
    const GuiHistoryRepoRoot root =
        resolve_repo_root_for_source(source_audio_path);
    if (!root.ok) return std::string();

    std::string out;
    if (!git_output(root.path, {"rev-parse", "--verify", "--quiet",
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
        const std::function<void(GuiHistoryScanResult)>&       on_done) {
    GuiHistoryWalkHeader header =
        resolve_history_walk_header(source_audio_path, projects_repo);
    const std::string repo_root   = header.repo_root;
    const std::string base_name   = header.base_name;
    const bool        ok          = header.ok;
    const bool        read_failed = header.read_failed;
    const GuiFailure  header_why  = header.unavailable_reason;
    // The header's project_directory is deliberately not copied here: the scan
    // needs the base NAME (the pathspecs and the load gate) and nothing about
    // where the piece currently lives, each candidate's own touched directory
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
    // The three pathspecs are built by sidecar_glob_pathspecs, which owns the
    // escaping and the reason for it — and which the per-commit touched-directory
    // read shares, so the walk and the resolution can never disagree about what
    // "this piece's files" means.
    //
    // AND IT IS UNCAPPED (2026-08-07): no `-n` term, so the pathspec walk
    // reaches the piece's first checkpoint. Everything else about it is
    // unchanged.
    std::vector<std::string> log_args{"log", "--format=%H", kBranchRef, "--"};
    for (std::string& p : sidecar_glob_pathspecs(base_name)) {
        log_args.push_back(std::move(p));
    }
    // HOW MANY COMMITS TOUCHED THIS PIECE — AND THE WITNESS THAT THE READ RAN.
    // `rev-list --count` prints a number ON SUCCESS, and that is the whole point
    // of asking it (2026-08-09): the capture layer's standing rule is that a
    // success needs an OUTPUT-SHAPED WITNESS — and a bare `log` has none, since
    // git_output collapses "could not run" and "ran and said nothing" into the
    // same false and a piece with no checkpoint says nothing at all. Reading that silence as the ruled
    // empty history would open the view at `0/0` and light Save and commit over
    // a history nothing established was empty. A COUNT cannot be silent: "0" is
    // bytes git printed, and it means the walk ran and found nothing.
    //
    // SO THE VERDICT RESTS ON THE COUNT AND THE ENUMERATION IS ORDINARY. An
    // unparseable or absent answer is the scan's failure arm; "0" is the ruled
    // empty success, the view opening at `0/0` with the counted line silent;
    // anything above zero means the `log` below MUST say something, which is
    // exactly git_output's "ran and said something" reading, so the enumeration
    // uses the helper and a contradiction between the two reads is a failure
    // like any other. One extra child per RUN — not per candidate.
    std::vector<std::string> count_args{"rev-list", "--count", kBranchRef, "--"};
    for (std::string& p : sidecar_glob_pathspecs(base_name)) {
        count_args.push_back(std::move(p));
    }
    std::string count_out;
    long long   candidate_count = -1;
    if (run_git_capture(repo_root, count_args, count_out) == GitRun::Ran) {
        const std::string token = trim_trailing_ws(count_out);
        // ALL DIGITS IS NOT ENOUGH: strtoll saturates at LLONG_MAX on overflow
        // and would hand back a "valid" count for a token of a thousand nines.
        // A malformed answer is malformed however long it is, so the length
        // bound refuses it before the conversion and ERANGE catches whatever the
        // bound would let through. (A real repository's count is a handful of
        // digits; eighteen is already absurd and safely inside the type.)
        constexpr std::size_t kMaxCountDigits = 18;
        if (!token.empty() && token.size() <= kMaxCountDigits &&
            token.find_first_not_of("0123456789") == std::string::npos) {
            errno                = 0;
            const long long v    = std::strtoll(token.c_str(), nullptr, 10);
            if (errno == 0 && v >= 0) candidate_count = v;
        }
    }
    if (candidate_count < 0) {
        GuiHistoryScanResult failed;
        failed.ok = false;
        failed.unavailable_reason = plain_failure(
            "could not read the commit history for 'projects/**/" + base_name +
            ".*'");
        on_done(std::move(failed));
        return;
    }
    // A count of ZERO is the ruled empty success: no candidate, so no member, so
    // a walk that opens the view at `0/0` over a blank lane. Nothing was hidden
    // either, so the DONE carries a zero and the counted line stays silent —
    // there is no count to explain, only a piece with no checkpoint behind it
    // yet.
    if (candidate_count == 0) {
        on_done(GuiHistoryScanResult{});
        return;
    }

    std::string log_out;
    if (!git_output(repo_root, log_args, log_out)) {
        GuiHistoryScanResult failed;
        failed.ok = false;
        // ONE CANONICAL SPELLING, NO PARENTHETICAL PLURAL (2026-09-01, the
        // capitalization sweep): the count went with "commit(s)" — it named a
        // number the user cannot act on, the list having failed.
        failed.unavailable_reason = plain_failure(
            "could not list the commits touching 'projects/**/" + base_name +
            ".*'");
        on_done(std::move(failed));
        return;
    }
    // THE ENUMERATION MUST MATCH THE WITNESS, EXACTLY. A `log` that prints a
    // PREFIX and then dies is the shape this catches, and it is measured rather
    // than imagined: with an older object damaged, `git log --format=%H` printed
    // the newest SHA and exited 128 — a non-empty answer, which alone would have
    // accepted the truncation as a walk silently missing its older half, read
    // afterwards as a piece with fewer checkpoints than it has.
    //
    // THAT PARTICULAR SHAPE IS NOW CAUGHT TWICE, and this check is still the one
    // that must hold. The capture layer reads the child's status, so the
    // measured 128 comes back `Exited` and git_output above already refused it;
    // but a truncation whose STATUS IS ZERO is a producer this side still owns —
    // our own read loop breaks on a pipe error and hands on what it got, and git
    // exits fine behind it. So EVERY line must be a full object name and the
    // COUNT of them must equal the count that witnessed the read. Either failing
    // is a contradiction between two reads of one history, and a contradiction
    // is not an answer.
    std::vector<std::string> candidates;
    bool malformed_line = false;
    for (std::string& sha : split_lines(log_out)) {
        if (sha.empty()) continue;
        if (!is_hex40(sha)) { malformed_line = true; break; }
        candidates.push_back(std::move(sha));
    }
    if (malformed_line ||
        static_cast<long long>(candidates.size()) != candidate_count) {
        GuiHistoryScanResult failed;
        failed.ok = false;
        failed.unavailable_reason = plain_failure(
            "the commit history for 'projects/**/" + base_name +
            ".*' did not enumerate: " + std::to_string(candidate_count) +
            " counted, " + std::to_string(candidates.size()) + " listed");
        on_done(std::move(failed));
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
    for (const std::string& sha : candidates) {
        if (abandoned()) break;
        GuiHistoryCommitLoad load;
        GuiFailure           why;
        if (!load_commit_sidecars_strict(repo_root, sha, base_name,
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
    // simply asks the same question on this thread. Three git calls, no strict
    // load: cheap enough to pay at a keystroke, which is exactly why the split
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
    // completing, another source) or a relaunch. A capture that cannot exec, or
    // whose answer does not arrive in the shape it must, is a broken environment
    // — captures carry no deadline, that being the MUTATING entry point's own
    // fence — and the sanctioned-use ruling puts that fix in the terminal rather
    // than behind a retry in here.
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
    for (const std::string& line : warp_diff.added) {
        GuiHistoryWarpEntry e;
        if (extract_warp_entry(line, e)) {
            e.effective_disabled =
                effective_of(now_warp_effective, line, e.disabled);
            d.warp_added.push_back(std::move(e));
        }
    }
    for (const std::string& line : warp_diff.removed) {
        GuiHistoryWarpEntry e;
        if (extract_warp_entry(line, e)) {
            e.effective_disabled =
                effective_of(then_warp_effective, line, e.disabled);
            d.warp_removed.push_back(std::move(e));
        }
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
            c.then_disabled = r.disabled;
            c.now_disabled  = a.disabled;
            // BOTH SIDES' MEASURES TRAVEL (architect 2026-08-22): the then
            // side for the revert AND the removed half's label, the now side
            // for the added half's label, so a measure-only edit paints two
            // different halves instead of two identical ones (the pair's
            // contract is at GuiHistoryPhaseResetChange).
            c.then_measure  = r.measure;
            c.now_measure   = a.measure;
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
// THE COMMIT ACT — the product's one mutating git route
// ---------------------------------------------------------------------------

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
// stdout at all. So the header's presence is the proof and the lines after it are
// the answer. (The exit status says the same thing and the capture layer does
// read it; the header is asked for anyway, because its SECOND job is one no exit
// status could do — the publication reading below.)
//
// THE HEADER ALSO CARRIES THE PUBLICATION STATE, which is the whole reason this
// act needs no repository observation of its own: the decoration — `[ahead N]`,
// or `[gone]` for an upstream that is configured and no longer there — is git's
// own answer to "does the remote have what this branch has", read out of the
// remote-tracking ref exactly as the deleted containment walk read it, in a probe
// the act was already running. Clean paths and an undecorated upstream is the
// honest all-done; clean paths WITH either decoration is the standard tool's cue
// to push.
//
// (A BRANCH WITH NO UPSTREAM AT ALL carries no decoration — `## main` — so it
// reads as owing nothing and the act reports NothingToCommit over clean paths.
// That is the sanctioned repository's answer for a configuration it does not
// have, and a branch with nowhere to push is not a state this act can improve.
// `[gone]` IS A DIFFERENT STATE and is read as such: an upstream IS configured,
// so there is a destination, and the branch it names is the one thing missing —
// the remote lacks every commit this branch carries, and the push recreates it.)
//
// THE HEADER'S BRANCH NAME IS NOT READ. It was, from 2026-08-09 to 2026-09-06, as
// a TRIPWIRE: `git status` takes no ref, so a checkout in another terminal
// mid-act could have it answer for a branch other than the one the act captured,
// and the header's name was compared against that capture to catch it. The
// compare is deleted with the observation machinery around it — an external
// checkout in the middle of a checkpoint is unsanctioned use, which this act
// meets bluntly rather than diagnosing (the act's head owns the ruling).
enum class GuiHistoryPathStatus {
    Unavailable,  // git did not run, or ran and failed
    Clean,        // it ran; the three paths match the checked-out tip
    Dirty,        // it ran; at least one differs
};

// DOES THE HEADER'S DECORATION SAY THE BRANCH OWES ITS UPSTREAM A PUSH — either
// commits the upstream lacks, or no upstream branch left to lack them?
//
// TWO DECORATIONS ANSWER YES, and they are two shapes of the one question.
// `[ahead N]`: the branch carries commits the upstream ref has not got.
// `[gone]`: the upstream is configured and the branch it names is not there at
// all, so the remote lacks every commit this branch has. Both are publication
// work and the act's one push does both — its explicit
// `refs/heads/<branch>:refs/heads/<branch>` refspec RECREATES a gone branch
// (verified live against a bare remote whose `main` was deleted and pruned).
//
// THE GRAMMAR, verified live against git 2.55 in every shape the act can meet:
// `## main` (no upstream), `## main...origin/main` (with one),
// `## main...origin/main [ahead 1]` / `[behind 1]` / `[ahead 1, behind 2]` /
// `[gone]`, `## HEAD (no branch)` (detached), `## No commits yet on main`
// (unborn).
//
// THE MATCH IS UNAMBIGUOUS BECAUSE OF WHAT A REFNAME MAY NOT CONTAIN: git refuses
// a branch name holding a space (`git check-ref-format`'s own rules), so the
// space that introduces the decoration can never be part of the local or the
// upstream name, and neither ` [ahead ` nor ` [gone]` can appear anywhere else on
// the line. AHEAD AND BEHIND TOGETHER STILL READ AS OWING, which is what the act
// wants: the branch carries commits the remote has not got, whatever else the
// remote also carries, and the push git then refuses is reported with git's own
// words.
bool header_says_publication_owed(const std::string& header) {
    return header.find(" [ahead ") != std::string::npos ||
           header.find(" [gone]") != std::string::npos;
}

// `publication_owed` comes back with the header's own reading (false whenever the
// status could not be used at all).
GuiHistoryPathStatus status_of_paths(const std::string& repo_root,
                                     const std::vector<std::string>& pathspecs,
                                     bool& publication_owed) {
    publication_owed = false;
    std::vector<std::string> args{"status", "--porcelain", "--branch", "--"};
    for (const std::string& p : pathspecs) args.push_back(p);
    std::string out;
    if (run_git_capture(repo_root, args, out) != GitRun::Ran) {
        return GuiHistoryPathStatus::Unavailable;
    }
    const std::vector<std::string> lines = split_lines(out);
    if (lines.empty() || lines.front().size() < 2 ||
        lines.front().compare(0, 2, "##") != 0) {
        return GuiHistoryPathStatus::Unavailable;
    }
    publication_owed = header_says_publication_owed(lines.front());
    for (std::size_t i = 1; i < lines.size(); ++i) {
        if (!lines[i].empty()) return GuiHistoryPathStatus::Dirty;
    }
    return GuiHistoryPathStatus::Clean;
}

// The checked-out branch's short name, or "" for a detached HEAD (which has no
// name and no remote-tracking ref).
std::string current_branch_name(const std::string& repo_root) {
    std::string raw;
    if (!git_output(repo_root, {"rev-parse", "--abbrev-ref", kBranchRef}, raw)) {
        return {};
    }
    std::string name = trim_trailing_ws(raw);
    if (name == "HEAD") return {};  // detached
    return name;
}

// THE THREE COMMITTED PATHS a piece's checkpoint occupies, in kSidecarExtensions
// order (which is what pairs each path with its text). One owner: the act writes
// them, stages them, commits them and asks `git status` about them, and all four
// must be talking about the same three files. (A fourth staged path —
// `<project>/sheet/sheet.map`, the score-video map — rode the add and the
// commit from 2026-08-20 until the 2026-08-21 sunset removed the score system
// whole; the score folder is plain ignored local material again.)
std::vector<std::string> checkpoint_paths(const std::string& project_directory,
                                          const std::string& base_name) {
    std::vector<std::string> paths;
    paths.reserve(3);
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

// THE ACT — ONE SANCTIONED PATH, GIT'S OWN EXIT STATUS, ONE ERROR CLASS.
//
// THE RULING: SANCTIONED USE IS STRICT-EXACT INTENDED USE, AND ANYTHING ELSE
// THROWS AN ERROR THAT IS FIXED IN THE TERMINAL, OUTSIDE THE GUI (architect
// 2026-08-09, superseding the graded machinery of 2026-08-04..09 whole: the
// attribution walk, the retry family, the subject selector, the byte gates and
// the witness grading are all deleted). The install scripts guarantee ssh and
// git, the wrapper owns every code commit (and excludes projects/), and the
// corpus is app-written — so the act stops distinguishing deviation cases and
// stops trying to recover from them. It is minimal but airtight: it does the one
// thing, and where the repository does not answer the way sanctioned use
// implies, it says so and stops.
//
// THE ACT DECIDES ON GIT'S EXIT STATUS, WHICH IS THE STANDARD MODEL EVERY GIT
// FRONT-END USES (architect 2026-09-06: "we prefer parsimony in code — makes it
// more maintainable, and the policy to always ferret out inconsistencies and
// asymmetries has paid dividends manyfold ... remove git custom and use
// posix_spawn's version"). THE STRICT MODEL IT SUPERSEDES decided every SUCCESS
// on an OBSERVATION of the repository — the branch tip having MOVED after the
// commit, the remote-tracking ref CARRYING the checkpoint after the push — and
// it was born of an inability rather than a design: SIGCHLD was ignored, the
// mutating runner could not read a child's exit status, and an act that cannot
// ask git how it went has to look at what git left behind. The spawn conversion
// made the status readable at both entry points, the capture side already lived
// on it, and this repository runs no hooks — so the observation machinery was
// the last asymmetry between the two runners, and it is gone: the tip reads and
// the tip compare, the containment walk and the push verify, the clean arm's
// containment read and the pre-flight's branch tripwire, all deleted with the
// `Unconfirmed` verdict they produced between them. Every question this act
// asks git now gets an exit status back, so there is nothing left it cannot
// answer.
//
// THE FIVE STEPS, each numbered at its own site below:
//   (1) CAPTURE — the symbolic branch read ONCE. Detached refuses immediately.
//   (2) WRITE the three sidecars.
//   (3) PRE-FLIGHT — one `git status --porcelain --branch` over those three
//       paths, whose `##` header carries BOTH answers the act needs: are the
//       paths dirty, and does the branch OWE ITS UPSTREAM A PUSH (`[ahead N]`,
//       or `[gone]` for an upstream branch that is no longer there).
//   (4) DIRTY — `git add` then `git commit` under the caller's title; a nonzero
//       exit on either is CommitFailed with git's own first line.
//   (5) PUSH — iff a commit was just made OR the branch already owed one. A
//       nonzero exit is CommittedNotPushed, zero is Committed. Clean paths and
//       nothing to publish is NothingToCommit, and the act runs no mutation at
//       all.
//
// THE CLEAN-BUT-OWING ARM PUSHES, which is the standard tool's answer to pending
// commits and the reason the clean arm needs no observation of its own: the
// header already said the remote has not got what the branch has — some of it
// under `[ahead N]`, all of it under `[gone]` — so the act publishes it rather
// than reporting a state it declines to fix. (Under the old model that arm was
// `Unconfirmed` with a stderr line telling the user to push from the terminal.)
//
// THE TWO ACCEPTED IMPRECISIONS, recorded rather than worked around, and they
// are the same shape: a step the DEADLINE killed may well have done its work.
// A PUSH so killed is reported CommittedNotPushed although it may have landed —
// the next act's pre-flight is the correction, its header showing no decoration
// and reporting NothingToCommit — and a COMMIT that landed and then hung in a
// `post-commit` hook past the deadline reads as CommitFailed although the commit
// is there, git having moved HEAD before running the hook. In both the terminal
// shows the truth in one look and the next act re-reads whatever was left; the
// alternative is a repository observation, which is the machinery this model
// exists to be rid of. A FAILED PUSH is
// fixed with `git push` in the terminal, not by an in-app retry. And OUT-OF-APP
// GIT UNDER projects/ is unsanctioned use: a commit racing this act may yield a
// blunt error rather than a graded diagnosis. (github-recheck.md carries the
// history of what this replaced.)
//
// THE PROJECTS-HOME GUARD STAYS, and it is not an outcome observation — it is
// the FENCE that keeps a checkpoint from publishing to the wrong place. It runs
// at the mutating boundary, immediately before the push, and the push goes to
// THE URL IT JUST VALIDATED (step 5 owns the pinning).
//
// WHAT THE COMMIT CANNOT CARRY, and why the pathspec is not a nicety: `git
// commit -- <paths>` builds its tree from HEAD plus the named paths and ignores
// everything else the index holds, so foreign staged work in the repository — a
// source edit mid-session, anything at all — can never ride along on a
// checkpoint. The `git add` in front of it exists for one case the pathspec
// commit cannot cover alone: a file the piece's directory did not previously
// carry is UNTRACKED, and a pathspec naming an untracked file is an error rather
// than an addition. That case has ONE instance — a sidecar written into a
// folder that had none.
//
// WHAT REMAINS AFTER A FAILURE. The three files are written first and are NEVER
// rolled back: a commit that fails leaves them in the working tree — staged, if
// the `add` got that far — where `git status` shows them and a hand commit can
// still land them, and a WRITE that fails part-way leaves the files it had
// already written standing beside the one it could not. That is the honest
// shape: the bytes are the user's own state, not a temporary, and a failed act
// that swept them away would destroy the only copy of what the user asked to
// keep. It is also why the write failure and the commit failure are different
// outcomes.
//
// THE IDENTITY IS THE MACHINE'S. Author and committer come from the clone's own
// git config; this program embeds no name, no address and no credential, and the
// push carries none either (the remote's own ssh key or credential helper is the
// whole story). The push runs ssh in BATCH MODE so a key that would prompt fails
// in one line instead of blocking the GUI on a passphrase nothing can type.
//
// `title` IS THE COMMIT MESSAGE and the caller's (the commit-title editor's
// buffer, seeded from history_checkpoint_title). Nothing matches on it any more
// — the content-signature attribution that did went with the graded machinery —
// so it is written and never read back.
//
// IT CREATES NO DIRECTORY, AND NEEDS NONE: `project_directory` is the folder the
// SOURCE is sitting in (resolve_history_walk_header's one rule), so it exists
// because the file the session is editing is in it. A creation step lived here
// for part of 2026-08-09, while the header could still name a folder that did
// not exist; the law that replaced those arms took it away again. THE FIRST
// CHECKPOINT OF A NEW PIECE IS AN ORDINARY IN-APP ACT — put the piece in its own
// folder under `projects/`, and Save and commit does the rest with no step in a
// terminal.
GuiHistoryCommitOutcome commit_history_checkpoint(
    const std::string& repo_root, const std::string& project_directory,
    const std::string& base_name, const std::string& projects_repo,
    const GuiHistoryNowSide& bytes, const std::string& title) {

    // (1) THE BRANCH, READ ONCE — the act's ONLY reading of the mutable symbolic
    // HEAD. It is what the push's refspec names at both ends, and reading HEAD
    // again at the push would let a checkout mid-act publish onto a branch the
    // act never looked at.
    //
    // A DETACHED HEAD IS UNSANCTIONED USE AND THROWS HERE, before anything is
    // written: the act publishes onto a branch, and there is no branch. Nothing
    // has reached the repository, which is what WriteFailed says.
    const std::string branch = current_branch_name(repo_root);
    if (branch.empty()) {
        std::fprintf(stderr,
                     "warptempo_gui: Checkpoint refused: HEAD is detached, "
                     "check out a branch in the terminal\n");
        return GuiHistoryCommitOutcome::WriteFailed;
    }

    // kSidecarExtensions order, which is what pairs each text with its path.
    const std::string* texts[3] = {&bytes.warpmarkers_text,
                                   &bytes.phaseresetmarkers_text,
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
    for (std::size_t e = 0; e < 3; ++e) {
        const std::string absolute = repo_root + "/" + paths[e];
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
    std::vector<std::string> pathspecs;
    pathspecs.reserve(3);
    for (const std::string& p : paths) pathspecs.push_back(literal_pathspec(p));

    // (The score-video map — `<project>/sheet/sheet.map` — rode the add and the
    // commit as a fourth, existence-guarded pathspec from 2026-08-20 until the
    // 2026-08-21 sunset removed the score system whole; the checkpoint stages
    // the three sidecars and nothing else again.)

    auto commit_failed = [](const std::string& why) {
        std::fprintf(stderr, "warptempo_gui: Commit failed: %s\n", why.c_str());
        return GuiHistoryCommitOutcome::CommitFailed;
    };
    // git's own account of a failing step, appended where it said anything.
    auto with_git = [](std::string why, const std::string& said) {
        if (!said.empty()) why += " (git said: " + said + ")";
        return why;
    };

    // (3) THE PRE-FLIGHT, AND IT IS THE ONLY READ THE ACT MAKES. One `git status`
    // answers both of the act's questions at once — whether the three paths
    // differ from what is committed, and whether the branch owes its upstream a
    // push — so nothing here has to ask the repository a second question to
    // learn what a mutation did (status_of_paths owns the header's grammar).
    bool                       publication_owed = false;
    const GuiHistoryPathStatus before_status =
        status_of_paths(repo_root, pathspecs, publication_owed);
    if (before_status == GuiHistoryPathStatus::Unavailable) {
        return commit_failed("could not read 'git status' for the checkpoint "
                             "paths; the written files are still in the working "
                             "tree");
    }

    // (4) DIRTY — stage and commit, both pathspec-scoped to the same three
    // paths, and BOTH DECIDED ON GIT'S EXIT STATUS. `add` is no longer advisory:
    // it fails only where the commit behind it could not succeed either (a
    // pathspec matching nothing, an unwritable index), and reporting the step
    // that actually refused is what puts git's own sentence on the card.
    bool committed = false;
    if (before_status == GuiHistoryPathStatus::Dirty) {
        std::string              add_line;
        std::vector<std::string> add_args{"add", "--"};
        for (const std::string& p : pathspecs) add_args.push_back(p);
        if (run_git_mutate(repo_root, add_args, add_line) != GitRun::Ran) {
            return commit_failed(
                with_git("git could not stage the checkpoint; the written files "
                         "are still in the working tree",
                         add_line));
        }

        std::string              commit_line;
        std::vector<std::string> commit_args{"commit", "-m", title, "--"};
        for (const std::string& p : pathspecs) commit_args.push_back(p);
        if (run_git_mutate(repo_root, commit_args, commit_line) != GitRun::Ran) {
            return commit_failed(
                with_git("git could not commit the checkpoint; the written "
                         "files are still in the working tree",
                         commit_line));
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

    // THE DESTINATION IS THE GUARD'S OWN ANSWER, pinned into the push child's
    // configuration: the projects-home guard runs here, at the MUTATING
    // BOUNDARY, and this act pushes to THE URL IT JUST VALIDATED. A `git push
    // origin` after the guard would ask a fresh process to resolve `origin`
    // again, and `remote.origin.pushurl` is a config value another terminal — or
    // a hook this very act just ran — can move in between. The pin is a PAIR of
    // `-c` settings and needs both halves: that key is MULTI-VALUED, so a lone
    // `-c` ADDS a destination rather than replacing the configured ones, and an
    // EMPTY value CLEARS the accumulated list. So: clear, then name the one
    // validated URL. The named remote stays in the argv so the remote-tracking
    // ref still updates, which is what the NEXT act's pre-flight reads.
    GuiFailure  guard_reason;
    std::string destination;
    if (!clone_is_projects_home(repo_root, projects_repo, guard_reason,
                                &destination)) {
        std::fprintf(stderr, "warptempo_gui: Push refused: %s\n",
                     guard_reason.diagnostic.c_str());
        return GuiHistoryCommitOutcome::CommittedNotPushed;
    }

    // THE REFSPEC IS THE CAPTURED BRANCH AT BOTH ENDS, never `HEAD` and never a
    // sha: the branch is what the act publishes, and both push arms — the commit
    // it just made and the commits that were already pending — want the same
    // thing sent. Nothing forces.
    const std::string ref = "refs/heads/" + branch;
    std::string       push_line;
    if (run_git_mutate(repo_root,
                       {"-c", "core.sshCommand=ssh -o BatchMode=yes",
                        // Clear the configured push destinations, then name the
                        // one the guard just validated — both halves required.
                        "-c", "remote.origin.pushurl=",
                        "-c", "remote.origin.pushurl=" + destination,
                        "push", "origin", ref + ":" + ref},
                       push_line) != GitRun::Ran) {
        // GIT'S REFUSAL IS THE VERDICT — a rejected refspec, refused
        // credentials, a remote hook saying no — and so is a push the deadline
        // killed, which is one of the act's two accepted imprecisions (its head
        // says why, and why the next act's pre-flight is the correction).
        std::fprintf(stderr, "warptempo_gui: Push failed: %s\n",
                     push_line.empty() ? "git reported nothing"
                                       : push_line.c_str());
        return GuiHistoryCommitOutcome::CommittedNotPushed;
    }

    std::fprintf(stderr, "warptempo_gui: Pushed '%s' to origin\n",
                 branch.c_str());
    return GuiHistoryCommitOutcome::Committed;
}
