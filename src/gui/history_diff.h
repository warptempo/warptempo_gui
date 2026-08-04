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
// THE MATCH IS BY FILE NAME, NEVER BY FOLDER. No directory name appears
// anywhere in this module: the source's sidecar base name is resolved against
// the COMMITTED TREE of origin/main, and whichever directory holds files by
// that name is the project's home, wherever it sits and whatever it is called.
// Exactly one directory may carry them — zero or several is UNAVAILABLE — and
// the commit walk uses a basename pathspec at any depth, so a corpus that was
// renamed at some point in the walked range is followed with no knowledge of
// what it used to be called. Each commit's blobs are then read at the paths
// THAT commit's own tree gives, which is the same idea applied per era.
//
// WHICH REPOSITORY IS THE PROJECTS HOME is the `projects_repo` setting's
// answer, not this module's assumption. init() compares it against the local
// clone's own `origin` remote, normalized to bare host/path on both sides, and
// refuses on a mismatch: the clone is the transport, and a rebound setting
// must never quietly produce a confident answer out of the wrong history.
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
// READ-ONLY BY CONSTRUCTION: the only git subcommands this feature ever runs
// are `log`, `show`, `ls-tree`, `rev-parse` and `remote get-url`, through an
// argv exec with no shell anywhere (the committed directory names carry spaces,
// so shell quoting would be a hazard rather than a convenience). Nothing here
// writes a file, a ref, or the index.

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

// READ ONE COMMIT'S SIDECARS BY SPELLING — the adopt-from-commit path's input
// (GuiInputHandler::adopt_history_commit), and the session walk's per-commit
// read generalized off the walk: the cache above is INDEX-keyed and holds
// deltas, while the adopt starts from a SHA the user typed, which may name a
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
// reads with no knowledge of what the directory used to be called. A resolved
// commit that carries none of the three is NOT a failure here — every blob comes
// back with an empty path and the CALLER decides what a missing sidecar means
// (the adopt refuses on one; the display path treats it as "everything added").
//
// False with `reason` set only when the spelling does not resolve to a commit.
// Nothing here writes anything: this is `rev-parse`, `ls-tree` and `show`.
bool read_commit_sidecars(const std::string&         spelling,
                          const std::string&         base_name,
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
    // that is empty or names a different repository than this clone's origin,
    // no committed file bearing this source's sidecar names, more than one
    // directory bearing them, or no commit touching any of them.
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
    // DIRECTORY holding its sidecars on origin/main (e.g. "projects/550 - 1").
    // Empty when unavailable — and also empty, legitimately, when the sidecars
    // sit at the repository root, which is why available() is the thing to
    // test rather than this string.
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
