#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct AppState;

// THE GITHUB RECHECK'S DIFF MODEL — no UI, no keys, no paint.
//
// The architect commits his working checkpoints of a piece into an
// ARCHITECT-ONLY corpus in this repository, as the same three sidecars a
// source WAV carries beside it (`<base>.settings`, `<base>.warpmarkers`,
// `<base>.phaseresetmarkers`, named by the source's own base name). The
// recheck reads that history back: for each of the last commits that touched
// those files, what would change if the committed state were compared against
// what is authored in memory right now.
//
// THE MATCH IS BY FILE NAME WITHIN `projects/` (architect 2026-08-04). The
// repository layout convention is `<repo>/projects/<piece>/`, so that ONE
// folder name is the module's only geography and everything below it is found
// by NAME: the source's sidecar base name is resolved against the COMMITTED
// TREE of the checked-out branch, and whichever directory under `projects/`
// holds files by that name is the piece's home, whatever it is called and
// however deeply it nests. Exactly one directory may carry them — zero or
// several is UNAVAILABLE — and the commit walk uses a `projects/`-rooted
// basename pathspec, so a piece renamed at some point in the walked range is
// still followed with no knowledge of what its folder used to be called. Each
// commit's blobs are then read at the paths THAT commit's own tree gives, the
// same idea applied per era. (The narrowing RETIRED a tree-wide match, and with
// it the pre-`projects/` era's commits: they are legacy-format checkpoints that
// refuse the load-in-place anyway, and a foreign copy of a sidecar name
// elsewhere in the
// tree can no longer make the match ambiguous.)
//
// THE BRANCH IS THE LOCAL ONE, `HEAD`, not `origin/main` — because this module
// WRITES checkpoints now (the commit act below) and a checkpoint whose push
// failed must still be visible history. `origin/main` would hide it until the
// next successful push, which is exactly the moment the user most needs to see
// that the commit exists. One spelling for all three uses — the tip listing,
// the walk, and the push's destination — so "the branch" cannot mean two
// things.
//
// WHICH REPOSITORY IS THE PROJECTS HOME is the `projects_repo` setting's
// answer, not this module's assumption. The guard compares it against this
// clone's `origin` — its FETCH url AND EVERY EFFECTIVE PUSH url (`remote
// get-url` plus `--push --all`, since a configured `pushurl` can send a
// checkpoint somewhere the fetch url never named) — normalized to bare host/path
// on both sides, and refuses on any mismatch: the clone is the transport, and a
// rebound setting must never quietly produce a confident answer out of the wrong
// history, nor publish into one. It runs at TWO SITES for two different
// questions — init(), as the mode's gate, and the commit act immediately before
// its push, as the mutating boundary's own check, because config can move while
// the mode stands (clone_is_projects_home in the .cpp is the one owner). The
// guard asks WHICH REPOSITORY THIS CLONE IS, never how fresh it is — it reads
// remote URLs and no ref at all — so moving the walk from `origin/main` to
// the local branch left it untouched and means exactly what it always did.
//
// The diff runs THEN -> NOW, and the "now" side is the LIVE IN-MEMORY STATE
// serialized through the save writers' own string halves — byte-identical to
// what a Ctrl+S would land at this instant, built with no file existing
// anywhere and no disk touched. So an ADDED entry is one the session has and
// the commit did not; a REMOVED entry is one the commit had and the session
// dropped. A commit missing the file entirely reads as "everything added",
// which is the natural line-diff answer and needs no special case.
//
// THE NOW SIDE IS FROZEN AT init(): the three strings are captured once and
// every cached delta is measured against them, so a session that keeps
// authoring after init keeps seeing the init-moment answer. The mode's entry
// is the natural re-init point.
//
// READ-ONLY BY CONSTRUCTION WITH ONE FENCED EXCEPTION. Every route in this
// module but the commit act runs only `log`, `show`, `ls-tree`, `rev-parse` and
// `remote get-url` and writes no file, no ref and no index entry. THE COMMIT ACT
// (commit_history_checkpoint, below) is the one writer in the product's whole
// git surface: it writes the three sidecars into the piece's directory in the
// working tree and runs `add`, `commit` and `push` — through a SEPARATE
// subprocess entry point (run_git_mutate in the .cpp), so which calls mutate
// stays answerable by reading the call sites rather than by trusting a runtime
// guard. Both entry points use an argv exec with no shell anywhere (the
// committed directory names carry spaces, so shell quoting would be a hazard
// rather than a convenience).

// One warp line resolved out of a diff hunk. The tempo token is the line's own
// payload text past the '|', VERBATIM: the flag displays the sidecar's own
// spelling, never a re-derivation through the typed value and back.
struct GuiHistoryWarpEntry {
    int64_t     frame    = 0;
    std::string tempo_token;
    bool        disabled = false;
};

// A warp line the two sides both carry at the SAME frame with different text —
// the removed line and the added line paired, which is what the double flag
// paints. Both the payload and the disable prefix can differ.
struct GuiHistoryWarpChange {
    int64_t     frame = 0;
    std::string then_tempo_token;
    std::string now_tempo_token;
    bool        then_disabled = false;
    bool        now_disabled  = false;
};

// One phase reset line. The grammar is `[#]<frame position>` — the frame plus
// the disable prefix, and nothing else.
struct GuiHistoryPhaseResetEntry {
    int64_t frame    = 0;
    bool    disabled = false;
};

// A phase reset line present at the same frame on both sides with a different
// disable prefix. THE COLUMNS ARE SYMMETRIC HERE: the phase reset line's
// payload is frame PLUS the disable bit, not the frame alone, so `100` ->
// `#100` is a genuine same-frame change exactly as a warp tempo edit is, and
// it pairs the same way rather than reading as an unrelated remove and add.
struct GuiHistoryPhaseResetChange {
    int64_t frame         = 0;
    bool    then_disabled = false;
    bool    now_disabled  = false;
};

// Raw per-file line accounting, kept beside the typed lists so a later round
// can surface "N unpaintable" if it ever wants to: `added`/`removed` are what
// the line diff produced before any typing, `dropped` is how many of those the
// typed lists could not accept (legacy spellings, hand-edit damage).
struct GuiHistoryLineCounts {
    int added   = 0;
    int removed = 0;
    int dropped = 0;
};

// One commit's whole answer.
struct GuiHistoryCommitDelta {
    std::string sha;

    // THIS COMMIT HAS NO ANSWER, and says so instead of guessing one. Its tree
    // carries the sidecar base name in several directories and none of them is
    // the one this session matched on the branch tip — an older era's
    // `projects/B/song.*` beside today's `projects/A/song.*`, which the walk's
    // basename pathspec pulls into one list. Every list below is EMPTY in that
    // state (the lane paints nothing) and the bottom-strip line names it; a
    // load-in-place of such a commit refuses in read_commit_sidecars.
    // Ambiguity is not a property of the piece, only of the commit: its
    // neighbours in the same walk resolve normally.
    bool ambiguous = false;

    std::vector<GuiHistoryWarpEntry>  warp_added;
    std::vector<GuiHistoryWarpEntry>  warp_removed;
    std::vector<GuiHistoryWarpChange> warp_changed;

    std::vector<GuiHistoryPhaseResetEntry>  phase_reset_added;
    std::vector<GuiHistoryPhaseResetEntry>  phase_reset_removed;
    std::vector<GuiHistoryPhaseResetChange> phase_reset_changed;

    // THE SCALE IS THE ONLY SETTINGS KEY THIS MODE DISPLAYS (architect's
    // ruling) — a recorded asymmetry, not an oversight: every other settings
    // delta (title, bpm, the whole per-tab view-state band, the environment
    // hashes) is deliberately invisible here, so the mode shows authoring
    // history rather than session bookkeeping. The tokens are the `scale=`
    // value text VERBATIM from each side, empty where that side carried no
    // `scale=` line at all.
    std::string then_scale_token;
    std::string now_scale_token;
    bool        scale_changed = false;

    GuiHistoryLineCounts warp_lines;
    GuiHistoryLineCounts phase_reset_lines;
    GuiHistoryLineCounts settings_lines;
};

// The three files' exact current bytes — what Ctrl+S would write at this
// instant, and the fixed side of every diff. Built in memory only.
struct GuiHistoryNowSide {
    std::string warpmarkers_text;
    std::string phaseresetmarkers_text;
    std::string settings_text;
};

// Serialize the live state through the three writers' own string halves
// (format_warpmarkers_text / format_phaseresetmarkers_text /
// format_settings_text). The settings half mirrors the save path's pre-write
// active-tab stash onto LOCAL ViewState copies exactly as the settings
// editor's autocomplete recall does, so the bytes match a save without this
// const read mutating anything.
GuiHistoryNowSide build_history_now_side(const AppState& app);

// ONE SIDECAR AS ONE COMMIT CARRIED IT. `path` is the committed path in THAT
// commit's own tree and is EMPTY when the commit carries no file by that name —
// the distinction the bytes alone cannot make, since a committed empty file and
// an absent one both read as no bytes.
struct GuiHistorySidecarBlob {
    std::string path;
    std::string text;
};

// One commit's three sidecars, read whole.
struct GuiHistoryCommitSidecars {
    // The full 40-char SHA git resolved the caller's spelling to.
    std::string sha;
    GuiHistorySidecarBlob warpmarkers;
    GuiHistorySidecarBlob phaseresetmarkers;
    GuiHistorySidecarBlob settings;
};

// READ ONE COMMIT'S SIDECARS BY SPELLING — the load-in-place-from-a-commit
// path's input
// (GuiInputHandler::load_history_commit_in_place), and the session walk's per-commit
// read generalized off the walk: the cache above is INDEX-keyed and holds
// deltas, while the load-in-place starts from a SHA the user typed, which may
// name a
// commit outside the walked depth or outside the list entirely.
//
// `spelling` is anything `git rev-parse --verify <spelling>^{commit}` resolves —
// a full SHA, a short SHA, a tag or a branch name. The peel suffix is what makes
// the resolution a COMMIT rather than any object, and it doubles as the argv
// hardening: a spelling starting with '-' reaches git as `-foo^{commit}`, which
// matches no option spelling and simply fails to resolve.
//
// The sidecar paths are resolved from THAT COMMIT'S OWN TREE by base name, the
// same era-agnostic rule the walk uses, so a commit from before a corpus rename
// reads with no knowledge of what the directory used to be called.
// `head_directory` is the session's own match (GuiHistoryDiff::project_directory)
// and breaks the one tie that rule can hit: a commit carrying the base name in
// several directories resolves to THIS piece's if it is among them, to a lone
// candidate otherwise, and REFUSES when neither holds — an ambiguous commit may
// belong to another piece entirely and a whole-state replace out of it would be
// the worst kind of confident wrong.
//
// A resolved commit that carries none of the three is NOT a failure here — every
// blob comes back with an empty path and the CALLER decides what a missing
// sidecar means (the load-in-place refuses on one; the display path treats it
// as
// "everything added").
//
// EVERY BLOB IT DOES RETURN IS WHOLE. A `git show` that could not run yields an
// empty string, and an empty sidecar is a valid file both marker loaders accept,
// so this cross-checks each read against the byte count the tree listing states
// and refuses on any disagreement. That is what keeps "the commit's own three
// sidecars" a true description of the load-in-place's input rather than a
// hope.
//
// False with `reason` set when the spelling does not resolve to a commit, when
// the commit is ambiguous, or when a blob could not be read whole. Nothing here
// writes anything: this is `rev-parse`, `ls-tree` and `show`.
bool read_commit_sidecars(const std::string&         spelling,
                          const std::string&         base_name,
                          const std::string&         head_directory,
                          GuiHistoryCommitSidecars&  out,
                          std::string&               reason);

// The session object: the commit list resolved once at init, each commit's
// snapshot and delta computed lazily on first request and cached thereafter,
// so stepping back over a commit already visited costs nothing.
class GuiHistoryDiff {
public:
    // Check the projects-home guard, locate the loaded source's sidecars in
    // the committed tree, and resolve the commit list. Returns available().
    // Every failure path is UNAVAILABLE with one stderr line and no further
    // git work: the repo root missing, no `origin` remote, a `projects_repo`
    // that is empty or that names a different repository than this clone's
    // FETCH url or than any of its effective PUSH urls, no committed file
    // bearing this source's sidecar names, more than one directory bearing
    // them, or no commit touching any of them.
    bool init(const AppState& app);

    bool               available() const { return available_; }
    const std::string& unavailable_reason() const { return unavailable_reason_; }

    std::size_t commit_count() const { return commits_.size(); }

    // Full 40-char SHA, newest first. Empty for an out-of-range index.
    const std::string& sha_at(std::size_t index) const;

    // The commit's delta, computed on first call and cached. Returns nullptr
    // for an out-of-range index or an unavailable session. The returned
    // pointer stays valid for the session's lifetime (the cache never
    // reallocates its elements).
    const GuiHistoryCommitDelta* delta_at(std::size_t index);

    // What init() matched: the source's sidecar base name, and the committed
    // DIRECTORY holding its sidecars on the branch's tip tree (e.g.
    // "projects/550 - 1"). Both are empty when unavailable, and available() is
    // the thing to test. The directory is ALWAYS under `projects/` and
    // therefore never empty on an available session — the match narrowed to
    // that folder in 2026-08-04, which is also what lets the commit act write
    // its checkpoint into a directory this string names.
    const std::string& sidecar_base_name() const { return base_name_; }
    const std::string& project_directory() const { return project_directory_; }

private:
    bool                     available_ = false;
    std::string              unavailable_reason_;
    std::string              base_name_;
    std::string              project_directory_;
    GuiHistoryNowSide        now_;
    std::vector<std::string> commits_;

    // One slot per commit, filled on first request. A deque-free vector of
    // optionals sized once at init, so no element ever moves.
    std::vector<std::optional<GuiHistoryCommitDelta>> cache_;
};

// -- THE COMMIT ACT — the product's one mutating git route ------------------
//
// What the mode reads, it can now also WRITE: while the history mode stands,
// Ctrl+Alt+R commits the live authoring state into the piece's directory in the
// projects repository (the mode bit selects the command, exactly as the
// iteration bit selects the sweep). The GUI half — the confirmation prompt, THE
// ORDINARY SAVE THAT RUNS FIRST (2026-08-04: the act is "Save and Commit", and a
// failed save refuses it before this module is reached at all), the stderr
// register, the session re-init that turns the freshly written checkpoint into
// an empty diff — lives at GuiInputHandler::run_history_commit;
// what lives here is the act itself.

// The commit message this act writes, and the one the prompt shows: `Update
// <id>`, where the id is the piece directory's own leaf name ("projects/550 - 1"
// -> "Update 550 - 1"). ONE OWNER for both readers, so the prompt cannot ask
// about a title the commit does not use.
std::string history_checkpoint_title(const std::string& project_directory);

// HOW FAR THE ACT GOT. The two failures are distinguished because the user's
// next move differs: nothing reached the repository on a write failure, while a
// commit failure leaves three written files sitting in the working tree, visible
// to `git status` and committable by hand. NothingToCommit is neither: the bytes
// already ARE the newest checkpoint (committing twice), so no commit exists to
// make and nothing was left behind. CommittedNotPushed is a SUCCESS for the
// caller's purposes — the checkpoint exists, and the walk (which reads the local
// branch for exactly this reason) shows it — with the push to retry.
enum class GuiHistoryCommitOutcome {
    WriteFailed,
    NothingToCommit,
    CommitFailed,
    CommittedNotPushed,
    Committed,
};

// WRITE THE THREE SIDECARS AND COMMIT THEM. `project_directory` and `base_name`
// are the session's own match (so the destination is the CURRENT era's spelling,
// the directory the branch tip carries the sidecars in) and `bytes` is what the
// three files are to contain. Every step states its own failure on stderr in one
// line, and this returns how far it got; it prints its own success line too, so
// the caller reports nothing.
//
// `projects_repo` is the setting's own value, and it is here because THE PUSH
// CONSUMES THE VALIDATED DESTINATION: the same guard init() runs as the mode's
// gate is asked again at the mutating boundary, and the URL it validates there
// is pinned into the push's own child rather than re-resolved from the mutable
// remote name — so a config changed since the mode opened cannot publish to a
// repository the user never confirmed, and neither can one changed between the
// check and the push. The publication's other two terms are bound the same way:
// the attributed commit is sent BY SHA, and the branch is read once at act start
// — the act's ONLY reading of the symbolic HEAD for a value, with every
// source-side observation after it BOUND to that branch: all but one by naming
// its own `refs/heads/` ref, and the pre-flight `git status` — which takes no ref
// and so cannot be spelled — by verifying its `##` header still names that
// branch and ending the act when it does not. So a checkout mid-act cannot make
// the observation and the publication mean different branches (the .cpp's push
// leg owns all three, with the `-c` mechanics).
//
// EVERY OUTCOME IS A REPOSITORY OBSERVATION, never a transport result: a commit
// that landed under a hung post-commit hook is FOUND and pushed rather than
// reported failed, and a pre-flight that finds the checkpoint already committed
// but not yet pushed PUSHES IT (the retry route for exactly that shape) instead
// of answering NothingToCommit. An observation that could not be MADE is its own
// answer everywhere — never silence read as a yes.
//
// WHAT "FOUND" MEANS IS CONTENT, NOT AUTHORSHIP: the act attributes a commit
// carrying exactly this checkpoint — its title, only these three paths, and
// these exact bytes — and content-equivalent commits are deliberately
// interchangeable, whoever ran the git. The .cpp's find_checkpoint_commit owns
// the full contract and why it is the ruled one.
//
// IT CREATES NO DIRECTORY. A piece with no committed history cannot open the
// mode at all, so there is nothing to bootstrap from here — the first checkpoint
// of a new piece stays a manual act.
GuiHistoryCommitOutcome commit_history_checkpoint(
    const std::string& project_directory, const std::string& base_name,
    const std::string& projects_repo, const GuiHistoryNowSide& bytes);
