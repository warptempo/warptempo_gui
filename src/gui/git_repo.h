#pragma once

// THE ONE GIT ROAD — libgit2, in process (architect 2026-09-27, replacing the
// git command-line subprocesses the history module spawned until that day).
//
// MECHANISM ONLY. This seam answers git questions about one clone and owns
// libgit2, the push's authentication and its host-key pin; every POLICY
// question — which paths are this piece's sidecars, what the projects-home
// guard compares, which directory a commit is about, the five-step checkpoint
// act and every sentence the user reads — stays in history_diff.cpp, its one
// caller. So the header carries no libgit2 type but the opaque repository
// handle, and <git2.h> is included by git_repo.cpp alone.
//
// THE FENCE IS WHICH FUNCTION A CALL SITE NAMES: GuiGitRepo's reads write no
// file, no ref and no index entry, and the FIVE MUTATORS — stage, commit,
// push, fetch, fast-forward — sit in their own section below with THREE
// callers, each in history_diff.cpp: the checkpoint act
// (commit_history_checkpoint) stages, commits, fetches and pushes; the GitHub
// check (check_github) fetches; the pull (run_history_pull) fast-forwards.
// Nothing else in the product changes a repository.
//
// THREADS. A GuiGitRepo is ONE HANDLE FOR ONE THREAD and is never shared: the
// main thread, the prefetch worker and the checkpoint worker each open their
// own for every question they ask (opening is cheap and every question is
// one-shot), and the prefetch scan holds one handle for its whole run on its
// own thread. Their concurrent access to the one clone is the accepted overlap
// GuiHistoryPrefetch records; libgit2 takes the same lock files git does. The
// network mutators (fetch, push) run on the checkpoint worker alone, one job at
// a time, each job holding the process-owned repository lane
// (history_commit_worker.cpp) for its whole run — so no two of them ever
// overlap on the remote-tracking refs, an abandoned check of a closed session
// included.

#include <atomic>
#include <cstddef>
#include <optional>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct git_repository;

// ONCE PER PROCESS, on the main thread, before any project's workers start
// (gui_main). It initializes libgit2 and sets its four process-wide options,
// each recorded at its site: durable object writes, the connect and
// read/write timeouts that bound a fetch and a push, and owner validation off.
void gui_git_init();

// WHICH CLONE HOLDS `dir` — the three answers the root derivation
// (resolve_repo_root_for_source, history_diff.cpp) tells apart. `Found` sets
// `root` to the clone's work tree, absolute, with no trailing slash. `NotAClone`
// is libgit2's own no: no repository above `dir` (the search stops at a
// filesystem boundary, as git's does), or one with no work tree. `CouldNotAsk`
// is a read that did not answer, `diag` carrying libgit2's words.
enum class GuiGitRoot { Found, NotAClone, CouldNotAsk };
GuiGitRoot gui_git_discover_root(const std::string& dir, std::string& root,
                                 std::string& diag);

// THE TRANSPORT, READ OFF A REMOTE URL BEFORE ANYTHING ELSE IS ASKED OF IT —
// SSH ONLY (architect 2026-09-27; the parse 2026-09-28). True for exactly the
// two spellings an SSH remote has: the URL form `ssh://[user@]host[:port]/path`
// and git's scp-style `user@host:path` (no slash before the first colon, a user
// and a host both named). EVERY OTHER SPELLING IS FALSE — `git://` (git's
// unauthenticated transport: no deploy key, no host-key pin), `file://`,
// `http(s)://`, `git+ssh://` and every other scheme, a bare path (with or
// without a colon after a slash), git's `<transport>::<address>` helpers, an
// scp-style spelling with no user, and anything carrying whitespace or a
// control character. Lexical: nothing is resolved and no host is judged here
// (the host-key pin at the connection is that answer, git_repo.cpp).
//
// TWO ASKERS OF THE ONE PREDICATE: the projects-home guard
// (clone_is_projects_home, history_diff.cpp) asks it of the fetch url and of
// every push url BEFORE it normalizes them — normalization drops the scheme,
// so a `git://` spelling of the right repository would otherwise compare
// equal — and so refuses before the checkpoint act writes anything; and the
// remote session's own prelude asks it again of the one URL a fetch or a push
// is handed, the seam's contract for every URL that reaches libgit2's
// transport selection.
bool gui_git_is_ssh_url(std::string_view url);

// A path predicate over repo-relative paths in git's own spelling (forward
// slashes, no leading slash). The history module's one sidecar predicate
// (is_piece_sidecar_path) is the only one ever handed in.
using GuiGitPathFilter = std::function<bool(std::string_view repo_path)>;

// WHAT THE WORKING TREE SAYS ABOUT A SET OF PATHS against the checked-out
// commit and the index — the checkpoint act's pre-flight answer.
enum class GuiGitPathStatus {
    Unavailable,  // the status could not be read
    Clean,        // every named path matches the checked-out tip
    Dirty,        // at least one differs, or is untracked
};

// HOW THE CHECKED-OUT BRANCH STANDS AGAINST ITS UPSTREAM'S REMOTE-TRACKING
// REF, as of the last fetch (no network: the ref is what the fetch left).
// `Compared` carries the counts and the upstream's commit; `NoUpstream` is a
// branch with none configured (or a detached / unborn HEAD); `Gone` is an
// upstream configured whose remote-tracking ref does not exist; `Unreadable`
// is a read that did not answer.
struct GuiGitUpstream {
    enum class Reading { Compared, NoUpstream, Gone, Unreadable };
    Reading     reading = Reading::Unreadable;
    std::string upstream_sha;  // Compared only, full 40-hex
    std::size_t ahead  = 0;
    std::size_t behind = 0;
};

// HOW A FETCH ENDED. `Refused` is GitHub declining this device — no deploy
// key, the key refused, a host key off the pin, a non-SSH URL — which no
// retry fixes; `Unreachable` is every other failure (DNS, connect, the time
// bound, a dropped session); `Cancelled` is the caller's cancel token.
enum class GuiGitFetch { Fetched, Unreachable, Refused, Cancelled };

// HOW A FAST-FORWARD ENDED (GuiGitRepo::fast_forward). `NotStarted` and
// `Conflict` wrote nothing; the three failures after them stopped with the
// branch where it was, each at its own step so each has its own recovery:
// `FilesFailed` — the working tree partly written (step 1 or 2), the index
// untouched; `IndexFailed` — the working tree written, the index NOT (step 3:
// libgit2 writes the index through a lock file, so a failed write leaves the
// old one whole); `BranchFailed` — the working tree and the index written, the
// branch ref not moved (step 4).
enum class GuiGitFastForward { Done, NotStarted, Conflict, FilesFailed,
                               IndexFailed, BranchFailed };

class GuiGitRepo {
public:
    // OPEN THE CLONE AT `root` exactly (no search upward: the root is the
    // derivation's answer, handed down as a value). Empty with `diag` set
    // when libgit2 cannot open it.
    static std::optional<GuiGitRepo> open(const std::string& root,
                                          std::string&       diag);

    GuiGitRepo(GuiGitRepo&& other) noexcept;
    GuiGitRepo& operator=(GuiGitRepo&& other) noexcept;
    GuiGitRepo(const GuiGitRepo&)            = delete;
    GuiGitRepo& operator=(const GuiGitRepo&) = delete;
    ~GuiGitRepo();

    // ---- reads --------------------------------------------------------------

    // The commit HEAD resolves to, full 40-hex, or "" when there is none (an
    // unborn branch) or it cannot be read.
    std::string head_commit() const;

    // The checked-out branch's short name, or "" for a DETACHED or UNBORN HEAD
    // (neither has a branch the act could publish) or a HEAD that cannot be read.
    std::string head_branch() const;

    // `remote.origin.url` as the CONFIGURATION spells it — never rewritten by
    // an `insteadOf` rule. False when the key is absent: there is no `origin`.
    bool origin_fetch_url(std::string& url) const;

    // EVERY configured `remote.origin.pushurl` in configuration order, raw like
    // the fetch url; with none configured, the fetch url alone, which is where
    // a push without a pushurl goes. False when neither can be read.
    bool origin_push_urls(std::vector<std::string>& urls) const;

    // THE WALK — every commit reachable from HEAD, newest first in git's own
    // default order (reverse chronological), that CHANGED a path `accept`
    // takes: against its one parent; a root commit against the empty tree; a
    // MERGE only when it differs on such a path from EVERY parent, all parents
    // walked (the history is linear by construction — nothing this product
    // does creates a merge — and this is the reader's defence should one
    // arrive). No rename detection. False with `diag` when the walk cannot be
    // read; an empty list is an answer.
    bool walk_head(const GuiGitPathFilter& accept,
                   std::vector<std::string>& shas, std::string& diag) const;

    // THE PATHS THIS ONE COMMIT CHANGED that `accept` takes — against its
    // parent, the empty tree for a root commit, and for a merge the paths that
    // differ from EVERY parent (git's combined-diff reading). A deletion
    // reports the path it removed. No rename detection. False when the commit
    // cannot be read.
    bool changed_paths(const std::string& sha, const GuiGitPathFilter& accept,
                       std::vector<std::string>& paths) const;

    // THE BLOB PATHS IN THIS COMMIT'S TREE that `accept` takes, recursively.
    // False when the tree cannot be read.
    bool tree_paths(const std::string& sha, const GuiGitPathFilter& accept,
                    std::vector<std::string>& paths) const;

    // THE COMMITTED BYTES at `path` in `sha`'s tree, raw (no filter, no
    // attribute applies to a blob read). False when the commit, the path or
    // the blob cannot be read — never an empty success.
    bool read_blob(const std::string& sha, const std::string& path,
                   std::string& bytes) const;

    // Does `sha` — a full 40-hex spelling — name a commit in this clone?
    bool is_commit(const std::string& sha) const;

    // THE PRE-FLIGHT over `paths`, each named LITERALLY (no glob matching):
    // Dirty when any differs from the checked-out commit in the index or the
    // working tree, or is untracked. `publication_owed` answers the second
    // question in the same read — does the checked-out branch OWE ITS UPSTREAM
    // A PUSH: it is AHEAD of the upstream's remote-tracking ref, or the
    // upstream is configured and that ref is GONE. A branch with no upstream,
    // a detached HEAD and an unborn one owe nothing. `diag` carries libgit2's
    // words on Unavailable.
    GuiGitPathStatus status_of(const std::vector<std::string>& paths,
                               bool&                           publication_owed,
                               std::string&                    diag) const;

    // THE BRANCH AGAINST ITS UPSTREAM (GuiGitUpstream above), read from the
    // local refs alone. `branch` is a short name (head_branch's answer).
    GuiGitUpstream compare_with_upstream(const std::string& branch) const;

    // EVERY PATH that differs between two commits' trees, both named by full
    // 40-hex, that `accept` takes — a deletion reporting the path it removed,
    // no rename detection. False when either commit cannot be read.
    bool paths_changed_between(const std::string& from_sha,
                               const std::string& to_sha,
                               const GuiGitPathFilter& accept,
                               std::vector<std::string>& paths) const;

    // ---- THE FENCE: the five mutations. The checkpoint act stages, commits,
    // fetches and pushes; the GitHub check fetches; the pull fast-forwards.
    // -------------------------------------------------------------------------

    // Stage the working tree's `paths` into the index (`git add`). An untracked
    // path becomes tracked. False with `diag`.
    bool stage_paths(const std::vector<std::string>& paths, std::string& diag);

    // COMMIT `paths` AND NOTHING ELSE onto the checked-out branch (`git commit
    // -- <paths>`): the new tree is HEAD's with the index's entries for these
    // paths laid over it, so anything else staged in the index never rides
    // along. A tree equal to HEAD's is refused ("no changes added to commit"),
    // as git refuses it. The message is `title` under git's own whitespace
    // cleanup; author and committer are the machine's (the `GIT_AUTHOR_*` /
    // `GIT_COMMITTER_*` environment, else `user.name` / `user.email` from the
    // clone's configuration and the global files). NO HOOK RUNS. False with
    // `diag`.
    bool commit_paths(const std::vector<std::string>& paths,
                      const std::string& title, std::string& diag);

    // PUSH `refs/heads/<branch>` to the same ref at `destination_url` — the
    // URL the projects-home guard just validated, set on this push alone and
    // never re-resolved from the remote name or rewritten by an `insteadOf`
    // rule — through the remote `origin`, so its remote-tracking ref follows.
    // Never forced: a remote that has moved is refused as non-fast-forward.
    // SSH ONLY (scp-style or ssh://, port 22 or GitHub's port 443), with the
    // DEPLOY KEY beside the device config and GitHub's three published host
    // keys pinned (git_repo.cpp owns all three rules).
    // False with `diag` for every failure, a server-side rejection included.
    // NO HOOK RUNS.
    bool push_branch(const std::string& branch,
                     const std::string& destination_url, std::string& diag);

    // FETCH `origin`'s configured refspec from `source_url` — the URL the
    // projects-home guard just validated, set on this fetch alone like the
    // push's destination — so the remote-tracking refs follow the remote. No
    // tags, no FETCH_HEAD. The push's transport rules exactly: SSH only, the
    // deploy key, the pinned host keys, the time bound. `cancel`, when not
    // null, is asked at every callback libgit2 makes (credentials, the host
    // key, each progress report, each ref update), and a set token ends the
    // fetch there as Cancelled — so a cancelled fetch writes no ref after the
    // callback that saw the token. `diag` carries the cause of every failure.
    GuiGitFetch fetch_origin(const std::string&       source_url,
                             const std::atomic<bool>* cancel,
                             std::string&             diag);

    // FAST-FORWARD the checked-out `branch` from `from_sha` (its tip now) to
    // `to_sha` (a descendant), NETWORK-FREE, in four steps:
    //   1. every path the two trees differ on, but `exclude` and `force`, is
    //      checked out SAFELY: a path whose working-tree file or index entry
    //      differs from `from_sha`'s, or an untracked file in the way, refuses
    //      the whole step before anything is written (Conflict, the first such
    //      path in `conflict_path`);
    //   2. every path in `force` is checked out FORCED, whatever the working
    //      tree holds;
    //   3. the index becomes `to_sha`'s tree, whole;
    //   4. the branch ref moves, only if it still names `from_sha`.
    // A path in `exclude` keeps its working-tree bytes (the index still takes
    // the new tree's entry, so git sees the kept bytes as a change on top).
    // No hook runs.
    GuiGitFastForward fast_forward(const std::string&              branch,
                                   const std::string&              from_sha,
                                   const std::string&              to_sha,
                                   const std::vector<std::string>& exclude,
                                   const std::vector<std::string>& force,
                                   std::string&                    conflict_path,
                                   std::string&                    diag);

private:
    explicit GuiGitRepo(git_repository* repo) : repo_(repo) {}
    git_repository* repo_ = nullptr;
};
