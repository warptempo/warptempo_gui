#pragma once

#include "phaseresetmarkers.h"
#include "settings_file.h"
#include "warpmarkers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct AppState;
struct UndoEntry;
class GuiHistoryPrefetch;

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
// holds files by that name belongs to this piece's history, whatever that folder
// is called and however deeply it nests. WHERE THE PIECE ITSELF LIVES IS A
// SEPARATE AND SIMPLER QUESTION since 2026-08-09: it is the folder the SOURCE is
// in, required to be under `projects/`, which is where the next checkpoint is
// written — so the folder decides what is WRITTEN and the name decides what is
// SEEN, and a folder renamed or made fresh today still walks back through every
// checkpoint the piece ever had. The commit walk uses a `projects/`-rooted
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
// that the commit exists. One spelling for both uses — the walk and the push's
// destination — so "the branch" cannot mean two things. (It was three until
// 2026-08-09; the header's tip listing went with the three-arm resolution.)
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
// The diff runs THEN -> NOW, older side to newer side, and WHICH TWO SIDES is
// the compare mode's answer (GuiHistoryCompare below). In the CUMULATIVE
// reading the now side is the LIVE IN-MEMORY STATE serialized through the save
// writers' own string halves — byte-identical to what a Ctrl+S would land at
// this instant, built with no file existing anywhere and no disk touched — so an
// ADDED entry is one the session has and the commit did not, and a REMOVED entry
// is one the commit had and the session dropped. In the ITERATIVE reading the
// viewed checkpoint is the OLD side and the NEXT-NEWER item is the new one — the
// next eligible walk member, or the same live now side at the newest index — and
// added/removed read the same way one step forward.
//
// THE WALK IS LOAD-GATED (architect 2026-08-04): membership in the walk is THE
// LOAD-IN-PLACE GATE ITSELF — load_commit_sidecars_strict below, the exact
// resolution + scratch staging + three strict frozen loaders the `'` act runs,
// ONE predicate with no relaxed variant — so every checkpoint the mode can step
// to is one it can load. A candidate that refuses (a missing sidecar, a parse
// refusal, an ambiguous per-commit path resolution) leaves the walk, counted on
// one stderr line at the end of the scan. That is what makes both sides of every
// diff loader-clean text with all three files present: there is no missing-file
// case, no unparseable line, and no legacy leniency arm anywhere in the diff
// model — the architect's no-legacy rule (the program never imports leniently;
// an old-format checkpoint is hand-edited, never tolerated).
//
// THE WALK IS UNCAPPED AND PREFETCHED (architect 2026-08-07, retiring the ruled
// depth of 20). Two facts follow from the gate above: it costs a strict
// whole-set load PER CANDIDATE, and that cost has to be paid before a walk
// member exists at all — which is what made `h` stall, and what the depth cap
// was really buying. So the git half moved OFF the entry and ONTO A BACKGROUND
// WORKER that runs at startup (GuiHistoryPrefetch, history_prefetch.h): the same
// steps in the same order, uncapped, STREAMING each eligible member to the main
// thread as it passes the gate. GuiHistoryDiff BINDS to that store rather than
// building a list of its own, so a visit costs no git at all in the ordinary
// case and the walk may GROW while the view stands.
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

// THE TWO COMPARE MODES (architect 2026-08-05). A checkpoint can be read
// against two different "other sides", and the view offers both.
//
// WHICH ONE IS SHOWING IS A SESSION PREFERENCE, not view state and not a tab:
// AppState::history_cumulative is the bit (its contract lives at that field),
// bare `u` and row 4's Cumulative button toggle it, and it is OFF — iterative —
// at program start and KEPT ACROSS VISITS thereafter, since the mode's own
// struct is reset whole at both edges and this deliberately is not in it. Row 3
// selects the WALK SOURCE while the view stands ("Remote" / "Local"), and
// Ctrl+Tab cycles those sources; neither touches the reading.
//
//   ITERATIVE — the viewed checkpoint as the OLD side, against THE NEXT-NEWER
//   ITEM as the new one. It compares FORWARD, toward now: the next-newer
//   ELIGIBLE WALK MEMBER for every index but the newest, and — at the newest
//   index, which has no committed successor — THE FROZEN LIVE NOW SIDE. So it
//   answers "what happened after this checkpoint", one step at a time, which is
//   the question a walk backwards through the history is asking at each stop. It
//   is the state the PROGRAM starts in (the bit's own initializer) — not a
//   per-visit default: a session that has turned cumulative on stays there until
//   `u` turns it back or the program closes.
//
//   CUMULATIVE — the viewed checkpoint against the frozen live now side, the
//   reading the mode shipped with. It answers "how does what I have now differ
//   from that checkpoint?", which is what a restore or a load-in-place is about.
//
// THE COLOR GRAMMAR IS ONE RULE ACROSS BOTH, because the NEWER side is always
// the now side of the diff: green `[+]` is what the newer side has, red `[-]`
// what the older side had and the newer dropped. In cumulative the newer side is
// the session; in iterative it is the next step forward — the newer checkpoint,
// or the session itself at the newest index.
//
// THE TWO READINGS COINCIDE AT THE NEWEST INDEX, deliberately and by
// construction (architect 2026-08-05, superseding his walk-parent pairing of
// earlier the same day): both are the newest checkpoint against the live state
// there, so a session freshly loaded right after a commit shows a BLANK lane in
// BOTH readings — which is the architect's stated purpose for the forward
// direction. Neither the lane nor any reader needs to know; they are simply the
// same delta, computed and cached twice.
//
// EVERY INDEX HAS A FORWARD PARTNER, so there is no empty-delta special case and
// no "the end of the walk has nothing to compare against" arm anywhere: the
// walk's newest end is where the live state is, and the walk's OLDEST end is a
// perfectly ordinary index whose partner is the member one newer.
//
// THE NEXT-NEWER MEMBER MAY SPAN COMMITS THE LOAD GATE HID, and that is the
// walk's own honesty rather than a defect here: walk membership is the strict
// whole-set load (the gate at the file head), so an ineligible commit is not
// steppable, not loadable and not shown — and a delta against the nearest
// checkpoint the mode CAN show is the only delta whose two sides are both things
// the user can reach. (It applies between COMMITTED neighbours only; the newest
// index's partner is the live state, which nothing can hide.)
enum class GuiHistoryCompare {
    Iterative,
    Cumulative,
};

// WHICH WALK THE VIEW IS READING (architect 2026-08-07). The view grew a SECOND
// WALK SOURCE beside the committed history: THE SESSION'S OWN UNDO/REDO
// TIMELINE. Row 3's two tabs select THIS axis and only this one — "Remote" and
// "Local" (2026-08-08); the OTHER axis, the reading, is row 4's Cumulative
// toggle. For one day the row carried the product of the two as four tabs, and
// the architect split them apart again.
//
//   COMMIT — the checkpoint walk this mode was built on: the piece's committed
//   sidecar history, matched by name under `projects/` (the file head owns the
//   whole model). Its members cost git, its membership is the strict load gate,
//   and it is the DEFAULT at every entry.
//
//   LOCAL — the session's own UNDO/REDO TIMELINE, read with the SAME machinery
//   over different states: each member is one STATE of that timeline — a redo
//   entry's, the live one, or an undo entry's snapshots — serialized through the
//   save writers' own string halves, and the same compute_commit_delta types the
//   difference. No git, no files, nothing on disk — the walk is what this
//   session has done and what it can still redo, in the vocabulary the commit
//   walk already speaks.
//
// THE DELTA VOCABULARY IS IDENTICAL on both, which is what makes one lane serve
// them: the six entry vectors plus `scale`. Viewport, trim and the rest of the
// GUI band never appear, because they were never undoable in the first place.
enum class GuiHistoryWalkSource {
    Commit,
    Local,
};

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

// One commit's whole answer. Every commit that gets one is a walk member, and
// walk membership is the strict whole-set load (the gate at the file head), so
// the lists below are typed out of loader-clean text on both sides — there is
// no per-commit "no answer" state and no line accounting beside them (the
// Ambiguous display machinery and the dropped-line counters died with the
// gate, 2026-08-04: an ambiguous or unparseable commit never enters the walk).
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
    // delta (title, bpm, the whole per-tab view-state band, the session prefs)
    // is deliberately invisible here, so the mode shows authoring
    // history rather than session bookkeeping. The tokens are the `scale=`
    // value text VERBATIM from each side, empty where that side carried no
    // `scale=` line at all.
    std::string then_scale_token;
    std::string now_scale_token;
    bool        scale_changed = false;

    // NOTHING DIFFERS — the whole delta in one word, and the ONE predicate for
    // it. It lives on the type rather than at a consumer so that "empty" cannot
    // come to mean two things: every member above is a term, so a member added
    // here is a term here too, at the one place a reader is already looking.
    //
    // ITS READER IS THE COMMIT ACT'S FACE (AppState::HistoryMode::head_delta_-
    // empty, app_state.h): the mode asks it once, of the NEWEST checkpoint AND
    // ALWAYS IN THE CUMULATIVE READING, to decide whether there is anything to
    // commit at all — the act commits the LIVE state, so "nothing to
    // checkpoint" is live-vs-newest whatever the lane happens to be
    // displaying. The vocabulary is
    // exactly this struct's — the two marker columns and `scale` — which is why
    // a settings-only drift the mode never displays reads as empty here too (the
    // asymmetry is recorded at the field and in github-recheck.md).
    bool is_empty() const {
        return warp_added.empty() && warp_removed.empty() &&
               warp_changed.empty() && phase_reset_added.empty() &&
               phase_reset_removed.empty() && phase_reset_changed.empty() &&
               !scale_changed;
    }

    // WHERE THE DELTA LIES — [lo, hi] over ALL SIX entry vectors, in SOURCE
    // frames (the domain every sidecar line is authored in). Returns false when
    // no entry carries a frame at all, which is both the empty delta and the
    // `scale`-only one: a scale change has no frame, so it contributes no term
    // and cannot be framed. Both callers read that as "the whole song".
    //
    // IT IS THE WHOLE DELTA, NEVER THE PAINTED HALF, and deliberately so: its
    // readers describe the CHECKPOINT, not the lane. The history view zooms to
    // this span on a plain trim-bar DOUBLE-click
    // (GuiInputHandler::frame_viewed_commit_diff_span), and the trim bar
    // displays it for as long as the view stands (GuiPaintHandler::paint_trim)
    // — and a span that shrank when the user pressed `p` would make a view
    // switch, whose whole purpose is reviewing the OTHER half of this same
    // delta, move the viewport out from under him. It also reads the delta
    // directly rather than AppState::HistoryMode::flags, which is paint-cache
    // output on a once-per-tick cadence and deliberately EMPTY for a frame
    // after every mode edge.
    bool frame_span(int64_t& lo, int64_t& hi) const;
};

// The three files' exact current bytes — what Ctrl+S would write at this
// instant, and the fixed side of every diff. Built in memory only.
struct GuiHistoryNowSide {
    std::string warpmarkers_text;
    std::string phaseresetmarkers_text;
    std::string settings_text;
};

// THE SETTINGS WRITER'S GUI HALF, CAPTURED — every non-engine key's value, held
// by VALUE so it can outlive the read (NonEngineSettingsSnapshot borrows a
// ViewState pair and two strings, which is a call-shaped type, not a storable
// one). It is opaque here because its ViewState members belong to app_state.h,
// which this header is included BY; the definition and the one overlay rule live
// in the .cpp.
//
// IT EXISTS FOR THE LOCAL WALK. The commit walk's two sides are both files, but a
// local delta compares two states of THIS session, and both of them have to be
// spelled with the SAME GUI half or every navigation the view performs would show
// up as a settings difference. So the capture happens once, at the mode's entry,
// and both sides of every local delta are formatted through it.
struct GuiHistoryGuiSide;

// Capture it off the live state. The active tab's band takes the save path's
// pre-write stash (viewport / zoom / playhead / trim) on a LOCAL copy, exactly
// as the settings editor's autocomplete recall does, so the bytes match a save
// while this const read mutates nothing.
std::shared_ptr<const GuiHistoryGuiSide> capture_history_gui_side(
    const AppState& app);

// One settings file's bytes: the captured GUI half plus whichever engine
// settings the caller is describing. The live state's own text is this with
// app.engine_settings; a local walk member's is this with that member's entry's
// (the LIVE member takes the now side's own text and never reaches this).
std::string format_history_settings_text(const GuiHistoryGuiSide& gui,
                                         const EngineSettings&    engine);

// Serialize the live state through the three writers' own string halves
// (format_warpmarkers_text / format_phaseresetmarkers_text /
// format_settings_text). The settings half goes through the two owners above,
// so the now side and every local walk member are spelled by one rule.
GuiHistoryNowSide build_history_now_side(const AppState& app);

// THE TYPED LINE DIFF OF ONE PAIR OF SIDES — the whole delta computation, taken
// off the walk position so that every reading of every walk runs the identical
// mechanism over different texts. The commit walk's cumulative reading hands it
// the viewed commit's snapshots and the frozen now side; its iterative reading
// hands it that commit's and THE NEXT-NEWER ITEM's; the LOCAL walk hands it two
// serialized undo states, or one of them and that same frozen now side. Nothing
// in it knows which, which is what makes four readings one answer-maker rather
// than four.
//
// `sha` is always the VIEWED member's, and EMPTY on the local walk: an undo
// entry has no name, and the corner reads the emptiness rather than inventing
// one (on the Local tab the corner shows `n/N` alone).
GuiHistoryCommitDelta compute_commit_delta(const std::string& sha,
                                           const std::string& then_warp,
                                           const std::string& then_phase_reset,
                                           const std::string& then_settings,
                                           const std::string& now_warp,
                                           const std::string& now_phase_reset,
                                           const std::string& now_settings);

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
// The sidecar directory is the one THIS COMMIT TOUCHED for the base name, the
// same rule the walk uses, so a commit from before a corpus rename reads with no
// knowledge of what the directory used to be called. It takes no session
// directory to break ties with and had one until 2026-08-09: a commit that
// touches TWO directories, or NONE, simply refuses — one may belong to another
// piece entirely and a whole-state replace out of it would be the worst kind of
// confident wrong (resolve_commit_paths owns the rules and why the session's own
// directory stopped being an answer).
//
// A resolved commit that carries none of the three is NOT a failure here — every
// blob comes back with an empty path and the CALLER decides what a missing
// sidecar means. In practice the one caller is load_commit_sidecars_strict
// below, which refuses on any missing file — for the `'` act and the walk's
// membership gate alike.
//
// EVERY BLOB IT DOES RETURN IS WHOLE. A `git show` that could not run yields an
// empty string, and an empty sidecar is a valid file both marker loaders accept,
// so this cross-checks each read against the byte count the tree listing states
// and refuses on any disagreement. That is what keeps "the commit's own three
// sidecars" a true description of the load-in-place's input rather than a
// hope.
//
// False with `reason` set when the spelling does not resolve to a commit, when
// it CHANGED none of this piece's sidecars, when it changed them in more than
// one directory, or when a blob could not be read whole. Nothing here writes
// anything: this is `rev-parse`, then a `show` for the touched directory, then
// `ls-tree` and three `show`s for the blobs — six children on the happy path,
// and TWO where the evidence refuses, the tree listing being asked only once a
// directory has been named.
bool read_commit_sidecars(const std::string&         spelling,
                          const std::string&         base_name,
                          GuiHistoryCommitSidecars&  out,
                          std::string&               reason);

// One commit's three sidecars READ AND PARSED WHOLE — what the strict gate
// below produced when it passed. The parsed halves are what the `'` act
// applies; the raw sidecars are what the walk keeps as each member's then
// side, so a delta costs no further git.
struct GuiHistoryCommitLoad {
    GuiHistoryCommitSidecars         sidecars;
    SettingsFile                     settings;
    std::vector<GuiWarpMarker>       warp_markers;
    std::vector<GuiPhaseResetMarker> phase_reset_markers;
};

// THE STRICT WHOLE-SET LOAD — the load-in-place gate, and since 2026-08-04 THE
// WALK'S MEMBERSHIP TEST, one predicate for both askers by the architect's
// ruling (no second predicate, no relaxed variant anywhere).
//
// The sequence is the `'` act's own validation, whole: read_commit_sidecars
// resolves the spelling and reads the three blobs out of that commit's own
// tree (size-cross-checked); a commit missing ANY of the three refuses (a
// partial checkpoint can neither be loaded in place nor walked to); the bytes
// are then staged through an RAII scratch directory and judged by the three
// STRICT WHOLE-FILE LOADERS themselves — read_settings_file,
// GuiWarpMarkers::load, GuiPhaseResetMarkers::load, all frozen-parser entry
// points that take a PATH — because a GUI-side scanner over the strings would
// be a SECOND GRAMMAR beside the strict one, which is precisely what this gate
// exists to avoid. Staging the bytes is the cheap way to keep the loaders
// themselves as the only judges.
//
// False with `reason` set (one line naming the cause; the committed path and
// the SHA where they apply, never the scratch path — the scratch is an
// implementation detail nothing outside this call can act on). The `'` act
// prints the reason; the walk's init counts refusals silently and reports one
// total. Nothing here writes anywhere but the scratch, which is removed on
// every exit.
bool load_commit_sidecars_strict(const std::string&    spelling,
                                 const std::string&    base_name,
                                 GuiHistoryCommitLoad& out,
                                 std::string&          reason);

// -- THE WALK'S GIT HALF, TAKEN OFF THE SESSION (2026-08-07) ----------------
//
// Everything below this line up to GuiHistoryDiff is what init() used to do
// inline and what the prefetch worker now does off-thread. It lives HERE, in
// the module that owns every git call, so the worker file owns only threading:
// the read/write fence stays "which function a call site names", and the
// prefetch names none of the mutating one.

// WHERE THE PIECE LIVES, or why it cannot be found — the walk's cheap half: the
// projects-home guard, the source's base-name derivation and the source's own
// folder, and no strict load anywhere. Its only git is the guard's two `remote
// get-url` reads (the tip-tree match went with the three-arm resolution on
// 2026-08-09). `unavailable_reason` carries the one line the mode prints when it
// refuses, in the exact shape it always had.
//
// `project_directory` IS THE SOURCE'S OWN PARENT FOLDER, and that is the whole
// rule (architect 2026-08-09): repo-relative, no trailing slash, required to lie
// strictly under the clone's `projects/`, and existing by construction because
// the source is in it. A source anywhere else REFUSES the view — the header's
// one source-side refusal, whose fix is a file move. The definition owns the law
// and the accepted trade.
struct GuiHistoryWalkHeader {
    bool        ok = false;
    std::string unavailable_reason;
    std::string base_name;
    std::string project_directory;
};

// Run that cheap half. TWO CALLERS, deliberately: the prefetch worker at the
// head of every run, and GuiHistoryDiff::init when a visit opens before the
// worker's header has arrived — so an entry refusal is the same answer computed
// in the same place whichever thread asks. Its only git is the projects-home
// guard's two `remote get-url` reads — the tip-tree listing went with the
// three-arm resolution on 2026-08-09, so where the piece lives is now answered
// from the source path and the filesystem alone. It writes nothing.
GuiHistoryWalkHeader resolve_history_walk_header(
    const std::string& source_audio_path, const std::string& projects_repo);

// THE WALKED BRANCH'S TIP, full SHA, empty when it cannot be read. It is the
// prefetch store's STALENESS key: a run describes the repository as of one tip,
// and an entry that finds the tip moved kicks a fresh run rather than trusting
// the old one.
std::string read_history_branch_tip_sha();

// HOW A SCAN RUN ENDED — the DONE callback's whole payload, and the header's own
// ok-plus-reason shape reused because the question is the same one: did this
// half of the walk ANSWER, and if not, what does the mode print when it refuses.
//
// `ok` FALSE IS A READ THAT DID NOT ANSWER, never a history that is empty. The
// two are the arc's whole reason for this type: an empty history is the ruled
// empty success — the view opens at `0/0` and Save and Commit is live — while a
// read this program could not get an answer out of knows NOTHING about the
// piece's history, and reading that silence as "no checkpoints" would establish
// an empty walk, latch the head delta commit-worthy and let the act publish
// against a baseline nobody ever read.
//
// WHICH IS WHY THE EMPTY VERDICT RESTS ON AN OUTPUT-SHAPED WITNESS and not on a
// silent `log`: exit codes are unreadable here, so a `log` that ran and found
// nothing and a `log` that failed both say nothing at all. `rev-list --count`
// prints "0" — bytes git printed — and that is the ruled empty history.
//
// WHAT ENDS A RUN NOT OK — THE ONE ENUMERATION, every other site pointing here
// (re-derived from scan_history_walk 2026-08-09): the count capture could not
// run; the count answered nothing, non-digits, or more digits than a count can
// have; the count was positive and the `log` then said nothing; a `log` line was
// not a full object name; or the number of lines did not EQUAL the count. Every
// one of them is two reads of one history disagreeing, or one read that never
// answered — never a history that is empty. (A per-CANDIDATE failure is not on
// this list and never ends the run: it hides that commit on the counted line's
// terms, the walk's own load gate doing what it always did.) A failed run is a
// terminal matter under the sanctioned-use ruling: the mode refuses entry with
// `unavailable_reason` on one stderr line and stays refused until an ordinary
// re-kick (the tip moving, a checkpoint completing, another source) runs a scan
// that answers.
struct GuiHistoryScanResult {
    bool        ok = true;
    std::string unavailable_reason;  // set iff !ok
    int         hidden = 0;          // the counted stderr line's number
};

// ONE PREFETCH RUN, WHOLE — the header, the UNCAPPED `git log` over the same
// `:(glob)projects/**/<base>.<ext>` pathspecs, and per candidate the strict
// whole-set load gate, in that order.
//
// IT REPORTS THROUGH CALLBACKS RATHER THAN RETURNING A LIST, which is what makes
// the streaming possible: `on_header` fires once (with `ok` false and the reason
// set when the piece cannot be found at all), `on_member` once per ELIGIBLE
// commit in walk order (newest first) carrying the snapshots the gate read, and
// `on_done` once at the end with a GuiHistoryScanResult — whether the run's own
// reads ANSWERED, the refusal reason when they did not, and the HIDDEN count,
// the counted stderr line's number and the only number a run reports. Ineligible
// candidates produce no member callback.
//
// A HEADER REFUSAL ENDS `ok`, deliberately: the header carries its own reason and
// init reads it from there, so the DONE says only that nothing more is coming.
// GuiHistoryScanResult's own comment ENUMERATES what ends a run NOT ok, and is
// the one site that does.
//
// `abandoned` IS ASKED BETWEEN CANDIDATES, and nowhere else: a superseding kick
// or a shutdown wants this run to stop, and a candidate boundary is the finest
// grain that costs nothing (the strict load of one commit's three tiny files).
// `on_done` fires on the abandoned path too, so the caller's own bookkeeping has
// one shape.
//
// EVERY CALLBACK RUNS ON THE CALLING THREAD. This function touches no AppState
// and no shared state of its own — its whole input is the two strings — so the
// worker may run it while the main thread reads the store the callbacks filled.
void scan_history_walk(
    const std::string& source_audio_path, const std::string& projects_repo,
    const std::function<bool()>&                        abandoned,
    const std::function<void(GuiHistoryWalkHeader)>&    on_header,
    const std::function<void(GuiHistoryCommitSidecars)>& on_member,
    const std::function<void(GuiHistoryScanResult)>&     on_done);

// The session object: A BINDING TO THE PREFETCH STORE'S WALK (2026-08-07,
// superseding the list this used to build for itself at init) — each member
// carries the three sidecar snapshots the load gate read on the worker — with
// each commit's delta computed lazily on first request and cached thereafter PER
// (INDEX, COMPARE), so stepping back over a commit already visited costs nothing
// in either reading and no delta ever runs git at all.
//
// THE WALK MAY GROW UNDER A LIVE SESSION, which is the streaming's whole point:
// a visit opened while the scan is still running sees members appended in walk
// order (newest first, so every arrival is OLDER than everything already there)
// and answers a larger commit_count() from one tick to the next. Nothing an
// existing index means changes when that happens — `n/N` grows, the `,` wall
// moves outward, and the lane repaints because the flag cache's fingerprint
// carries the count.
class GuiHistoryDiff {
public:
    // BIND THE VISIT TO THE PREFETCH STORE and freeze the live now side. The
    // git half of this call is gone (the store's worker ran it, or is running
    // it): what is left is the header — taken from the store, or COMPUTED
    // SYNCHRONOUSLY HERE by resolve_history_walk_header when the visit opens
    // before the worker's own header has arrived, two git calls and no strict
    // load — plus the now-side capture and the delta caches.
    //
    // Returns available(). TWO FAMILIES OF FAILURE, one stderr line and nothing
    // else either way. THE HEADER'S: the repo root missing, no `origin` remote, a
    // `projects_repo` that is empty or that names a different repository than
    // this clone's FETCH url or than any of its effective PUSH urls, and a source
    // that is not in a folder under `projects/` — which repository, and where
    // this piece lives. (The committed-tree questions went with the three-arm
    // resolution on 2026-08-09: there is no tip listing, no sole-directory
    // judgment and no ambiguity refusal left in the header.) AND
    // THE SCAN'S: the bound run's `git log` capture could not run, so this
    // program has not read the piece's history at all (GuiHistoryScanResult).
    // Both are questions that went UNANSWERED; neither is a fact about how many
    // checkpoints there turned out to be.
    //
    // MEMBERSHIP IS NEVER A REFUSAL (architect 2026-08-09). A walk with no
    // eligible commit is AVAILABLE whether its scan is still streaming or has
    // FINISHED empty: the view opens at `0/0` over a blank lane, populating live
    // in the first case and resting in the second. The two terminal zeros this
    // once refused on — no commit touching the sidecars, and every touching
    // commit refusing the strict load — are ordinary now, and the reasoning is
    // at the site in the definition. Which is exactly why the scan failure had
    // to become its own answer: with emptiness no longer refusing, an unread
    // history would otherwise have passed for a read one.
    bool init(const AppState& app, const GuiHistoryPrefetch& prefetch);

    bool               available() const { return available_; }
    const std::string& unavailable_reason() const { return unavailable_reason_; }

    // How many eligible commits the bound store has DELIVERED so far. It only
    // ever grows within a visit.
    std::size_t commit_count() const;

    // THE BOUND RUN IS FINISHED AND CARRIES NOTHING — the walk's terminal empty
    // state, and a LEGAL standing one since 2026-08-09: the view opens on it at
    // `0/0` over a blank lane rather than refusing, so this is the difference
    // between "there is no eligible checkpoint" and "one may still arrive".
    //
    // ONE READER, GuiInputHandler::measure_history_head_delta, which answers
    // COMMIT-WORTHY on a true — with no eligible baseline there is by definition
    // everything to checkpoint, and the conservative greyed face it wears while
    // a scan is still streaming would otherwise be permanent here. It asks
    // through the binding rather than the store directly so a generation
    // mismatch answers the same cold "no" the member list does.
    //
    // A RUN THAT FAILED ANSWERS FALSE, on the same reasoning turned around: it
    // finishes with an empty deque too, and an unread history must not be worth
    // committing against. init refuses such a run outright, so what this term
    // covers is a run failing WHILE THE VIEW STANDS.
    bool walk_finished_empty() const;

    // Full 40-char SHA, newest first. Empty for an out-of-range index.
    const std::string& sha_at(std::size_t index) const;

    // The commit's delta IN ONE OF THE TWO READINGS (GuiHistoryCompare above),
    // computed on first call and cached per (index, compare) — the two answers
    // are independent and each is asked repeatedly, so one cache slot per pair
    // is what keeps a compare switch as free as a step back. Returns nullptr
    // for an out-of-range index or an unavailable session. The returned pointer
    // stays valid for the session's lifetime: the cache is a DEQUE per reading
    // (2026-08-07, replacing the vector sized once at init), and a deque's
    // push_back — which is the only way membership grows — never moves the
    // elements already in it. So a walk that grows under a live session appends
    // empty slots and invalidates no pointer this ever handed out.
    //
    // EVERY INDEX ANSWERS IN BOTH READINGS — no index is a special case, the
    // iterative reading's forward partner being the next-newer walk member or,
    // at the newest index, the live now side. The two readings COINCIDE at that
    // newest index and are cached separately there, one delta computed twice.
    //
    // AND THE ITERATIVE PAIRING IS APPEND-STABLE BY CONSTRUCTION, which is what
    // lets a growing walk leave the cache alone: index i pairs with i−1 (or the
    // live now side at 0), both of which are NEWER than i — and every arrival is
    // older than everything already delivered, so no member ever appears between
    // an index and its forward partner. A cached delta stays the right answer.
    const GuiHistoryCommitDelta* delta_at(std::size_t         index,
                                          GuiHistoryCompare   compare);

    // What init() resolved: the source's sidecar base name, and the piece's
    // DIRECTORY (e.g. "projects/550 - 1"), repo-relative — the folder the SOURCE
    // is in. Both are empty when unavailable, and available() is the thing to
    // test. The directory is ALWAYS strictly under `projects/` and therefore
    // never empty on an available session, which is what lets the commit act
    // write its checkpoint into a directory this string names, and it always
    // EXISTS, the source being in it.
    const std::string& sidecar_base_name() const { return base_name_; }
    const std::string& project_directory() const { return project_directory_; }

    // THE FROZEN NOW SIDE, for the visit's OTHER walk. The local walk measures
    // against the same three strings this session captured at init(), and it
    // takes them from here rather than building a second set: one capture per
    // visit is what makes "the two walks agree about now" structural instead of
    // a coincidence of two calls made a microsecond apart. Empty until init()
    // succeeds, which is also the only state a caller can reach it in — an
    // unavailable session never opens the view.
    const GuiHistoryNowSide& now_side() const { return now_; }

private:
    // The bound store's walk, or an empty one. It ANSWERS EMPTY FOR A
    // GENERATION MISMATCH, which is the defensive half of the binding: a run
    // superseded under a live session would have swapped the deque out from
    // under these indices, and reading a stale generation as "no members" is the
    // cold answer the lane already knows how to draw. It is not a reachable
    // state — a kick while the mode stands is DEFERRED to the exit
    // (GuiInputHandler::kick_history_prefetch), so a visit's generation is fixed
    // for its whole life.
    const std::deque<GuiHistoryCommitSidecars>& members() const;

    bool              available_ = false;
    std::string       unavailable_reason_;
    std::string       base_name_;
    std::string       project_directory_;
    GuiHistoryNowSide now_;

    // THE WALK ITSELF LIVES IN THE PREFETCH STORE (history_prefetch.h), not
    // here: this is a binding, and the generation is which run it bound to.
    const GuiHistoryPrefetch* store_            = nullptr;
    unsigned long long        store_generation_ = 0;

    // One slot per (commit, compare mode), filled on first request and grown to
    // match membership as the store delivers. A DEQUE per reading, because
    // push_back leaves every element already in it exactly where it is, which is
    // delta_at's pointer-stability contract under a growing walk. Indexed by the
    // enum's own value.
    //
    // THE FOURTH READING DID NOT LAND HERE (2026-08-07), which retires this
    // slot's old "adding a third reading is a one-line change" note: the LOCAL
    // walk offers the same two readings over ANOTHER SOURCE (GuiHistoryLocalWalk
    // below), so what the view grew was a second source with its own two-slot
    // cache — not a third compare mode. A genuine third READING would still be
    // the one-line change the array shape suggests, in both classes.
    std::array<std::deque<std::optional<GuiHistoryCommitDelta>>, 2> cache_;
};

// THE LOCAL WALK — THE SESSION'S OWN UNDO/REDO TIMELINE, READ AS A WALK
// (architect 2026-08-07: "the local history feature will be helpful for
// understanding undo/redo history"). It is GuiHistoryDiff's formula over
// different members, and deliberately nothing more: same members-newest-first
// indexing, same two readings, same forward pairing, same delta type, same
// painter, same lane.
//
// A MEMBER IS ONE STATE OF THE TIMELINE — not one undo entry (architect
// 2026-08-08, superseding the undo-stack-only walk). With U = the undo stack's
// size and R = the redo stack's, BOTH CAPTURED AT init, the walk carries
// N = U + R + 1 members, newest first: every state Ctrl+Z and Ctrl+Shift+Z can
// reach, plus the one the session is standing in.
//   * k < R — A FUTURE STATE, `redo_stack[k]`. Redo counter-entries are pushed
//     as the user undoes, so redo_stack[0] is the FURTHEST future and
//     redo_stack.back() the nearest: the vector already runs newest-first, and
//     a counter-entry's snapshots are the state a redo would restore.
//   * k == R — THE LIVE MEMBER, the session's current state. The walk already
//     holds it as the frozen now side, so nothing is captured or serialized for
//     it; its three texts are that side's own.
//   * k > R — A PAST STATE, `undo_stack[U + R - k]`, whose snapshots are THE
//     STATE BEFORE THE EVENT THAT ENTRY RECORDS (k = R+1 is the stack's top,
//     one Ctrl+Z away; k = N-1 is undo_stack[0], the state at file open).
// Both stacks carry all three snapshot pieces — the carry-everywhere shape at
// UndoEntry, which restore_history_entry reproduces field for field on the
// counter-entry it pushes — so serializing them through the save writers' own
// string halves gives the same three loader-clean texts a commit member carries,
// and the diff needs no second grammar and no second reader.
//
// SO THE MEMBERS ARE THE TIMELINE'S STATES, newest first, and every adjacent
// pair (k, k−1) brackets exactly one event. THE WALK IS THEREFORE NEVER EMPTY:
// a session that has authored nothing is N = 1, the live state alone, reading
// `1/1` and comparing against itself (architect 2026-08-08: "once we've reached
// the undo history, even if there is nothing in it, we are comparing one state
// against an identical state"). `0/0` on a Local tab means the walk was never
// initialized, and nothing else.
//
// WHICH MAKES THE ITERATIVE READING EXACTLY ONE EVENT — THE COMMIT WALK'S OWN
// FORWARD PAIRING, verbatim: member k against member k−1, the next-newer
// member, and at k == 0 against ITSELF, nothing being newer than the furthest
// future state. That self-pair is the blank lane the commit walk shows at its
// newest index right after a commit. At the LIVE member the delta is the change
// the next REDO would apply; at the first past member it is what Ctrl+Z would
// revert.
//
// THE CUMULATIVE READING IS "how does my session differ", so it measures
// against THE LIVE MEMBER always — and for a FUTURE member THE SIDES SWAP: the
// live state is the THEN side and the future state the NOW side, the future
// state being the newer of the two. That is what keeps THE NEWER SIDE IS GREEN
// one exceptionless colour rule across the whole timeline instead of a rule with
// a redo exception. At the live member itself both sides are the same state, so
// the lane is blank there in both readings.
//
// (THE REDO STACK WAS DELIBERATELY EXCLUDED until 2026-08-08, on the reasoning
// that a walk is what is BEHIND you and that including redo would need a second
// index origin and a signed position. The architect superseded that with the
// complaint the states model answers — set a marker, move it, Ctrl+Z, open the
// view and it read `1/1` about a session holding three states — and the
// objection went with it: there is ONE index origin here, the newest state, and
// the live state is a member like any other.)
//
// THE TIMELINE IS FROZEN WHILE THE VIEW STANDS, and the premise is derived
// rather than hoped: every route that could push, pop or evict an entry on
// EITHER stack is either consumed by the mode's two allowlists or closes the
// view as part of itself (AppState::HistoryMode owns that derivation — the same
// one that keeps the frozen now side honest), and it covers the redo stack by
// the same argument, redo's only writers being push (which clears it) and the
// two restores. IT IS EXCEPTIONLESS since 2026-08-07: the one admitted producer
// it shipped with — the S->T view switch's iteration-bracket push — is gone with
// the ruling that ITERATION MODE IS TARGET-LEGAL, so entering target view writes
// no store at all, and the mode bit cannot toggle in here either (`i` is not on
// the keyboard allowlist). So both sizes are captured at init and the caches are
// plain vectors sized once, and the past members index FROM THE BOTTOM of the
// undo stack, which is what would make an append harmless if one ever returned:
// a captured position keeps naming the entry it named and the new entry is
// simply not in the walk. The premise stands on that derivation alone — nothing
// at runtime enforces it. member_at re-reads BOTH stacks' SIZES on every ask,
// but that is a bounds precondition on the subscript, not a check of the
// premise: a stack shorter than its captured size answers a blank lane rather
// than being read at indices that now mean other events. (A per-entry PUSH
// SERIAL verifying each captured position lived for one day of 2026-08-07,
// written for the kCap-eviction shape the admitted push could reach; the
// architect deleted it with that producer — member_at states the rule. Do not
// re-propose it.)
//
// A MEMBER PAIR THAT SERIALIZES IDENTICALLY SHOWS A BLANK LANE, which is honest
// rather than a gap: an `affects_persistence == false` event (the iteration
// bracket's session-only snapshot) changes nothing any sidecar would carry, so
// there is nothing for a delta to say about the pair that brackets it — the same
// blank the commit walk shows for a checkpoint whose content matches its
// neighbour.
class GuiHistoryLocalWalk {
public:
    // BIND TO THE LIVE SESSION at the mode's entry: BOTH stacks' sizes are
    // captured, the GUI half of the settings writer is captured (one snapshot
    // for BOTH sides of every delta — the reason it is captured at all is at
    // GuiHistoryGuiSide), and `now` is the visit's frozen now side, taken from
    // GuiHistoryDiff::now_side() so the two walks measure against one capture —
    // and so the LIVE MEMBER costs this walk nothing but the copy it already
    // makes.
    //
    // `app` IS RETAINED as the stacks' owner. It outlives every visit (it is the
    // one long-lived object in the program) and the entries it hands back are
    // read only through the frozen sizes above.
    void init(const AppState& app, const GuiHistoryNowSide& now);

    // How many STATES the timeline carries — U + R + 1, the `n/N` denominator
    // the corner shows on the Local tab, and fixed for the visit. It is never zero once init has
    // run against a session: a session that has authored nothing answers 1, the
    // live state alone. Zero is the UNINITIALIZED answer (no visit has bound this
    // walk), which is the only way a Local tab can read `0/0`.
    std::size_t entry_count() const { return count_; }

    // WHERE THE SESSION STANDS in the walk — the live member's index, which is
    // the captured redo count. The ONE entry owner opens the view here rather
    // than at 0, because 0 is the furthest FUTURE state and this is the state on
    // screen; with no redo entries the two coincide. Zero on an uninitialized
    // walk, which no reader reaches (the entry owner calls init first).
    std::size_t live_index() const { return redo_count_; }

    // The member's delta IN ONE OF THE TWO READINGS, computed on first call and
    // cached per (index, compare) exactly as the commit walk's is. Returns
    // nullptr for an out-of-range index, an uninitialized walk, or a stack whose
    // size no longer matches the capture (the frozen-timeline premise, checked
    // rather than assumed). The returned pointer is stable for the visit: the
    // caches are sized once at init and neither stack can grow under them.
    const GuiHistoryCommitDelta* delta_at(std::size_t       index,
                                          GuiHistoryCompare compare);

    // ONE MEMBER'S STATE, TYPED — the three pieces a load-in-place puts back
    // into the live session, handed out as pointers into the state that is
    // already there (a stack entry's snapshots, or the live stores and engine
    // block for THE LIVE MEMBER). It is deliberately NOT the member's three
    // TEXTS: those are the DIFF's medium, and round-tripping typed state
    // through them to load it would put the strict parsers in a path that
    // needs no grammar at all.
    //
    // ONE CONSUMER — the Local tab's `'` load-in-place
    // (GuiInputHandler::load_history_local_entry_in_place). It COPIES all three
    // before it writes anything, which is what keeps the identity load (loading
    // the live member) from assigning a store to itself.
    //
    // THE POINTERS LIVE AS LONG AS THE VISIT DOES and no longer: they name the
    // frozen stacks and the live stores, so any route that ends the visit —
    // the load-in-place's own close included — invalidates them.
    struct MemberState {
        const std::vector<GuiWarpMarker>*       warp_markers        = nullptr;
        const std::vector<GuiPhaseResetMarker>* phase_reset_markers = nullptr;
        const EngineSettings*                   engine_settings     = nullptr;
    };
    // EMPTY is member_at's nullptr in optional's spelling and means the same
    // thing: an out-of-range index, an uninitialized walk, or a stack shorter
    // than its capture (the bounds precondition). It is the blank-lane state,
    // and a live Local tab cannot reach it — the walk is bound before the mode
    // goes up and every step clamps — so the one consumer treats it as a
    // refusal rather than acting on a guess.
    std::optional<MemberState> member_state(std::size_t index) const;

private:
    // One member's three texts, serialized on first ask and kept. Lazy for the
    // reason the commit walk's deltas are: a visit typically reads a handful of
    // members out of a timeline that may hold hundreds, and formatting all of
    // them at `h` would be exactly the entry stall the prefetch arc removed. (The
    // LIVE member is the one that costs nothing either way — it copies the three
    // frozen now-side strings — and it takes the same lazy path rather than a
    // case of its own.)
    struct Member {
        bool        built = false;
        std::string warpmarkers_text;
        std::string phaseresetmarkers_text;
        std::string settings_text;
    };
    const Member* member_at(std::size_t index);

    // THE BOUNDS PRECONDITION, shared by the two member readers: the walk is bound,
    // the index is in range, and NEITHER stack has shrunk below its capture.
    // False means "answer nothing at all" rather than subscript a stack whose
    // indices may now name other events (member_at's own comment owns the
    // reasoning; this is where the three tests live).
    bool member_readable(std::size_t index) const;
    // THE INDEX -> STACK ENTRY MAPPING, the class comment's three arms in one
    // place: nullptr for THE LIVE MEMBER (k == R, which has no entry — its state
    // is the session's own), the redo stack below it, the undo stack above it.
    // Precondition: member_readable(index), so this never bounds-checks.
    const UndoEntry* entry_at(std::size_t index) const;

    const AppState*                          app_        = nullptr;
    // The two captured sizes and the member count they imply — U, R and
    // N = U + R + 1. `count_` is what entry_count answers and what bounds every
    // index; the two halves are what member_at maps an index through.
    std::size_t                              undo_count_ = 0;
    std::size_t                              redo_count_ = 0;
    std::size_t                              count_      = 0;
    std::shared_ptr<const GuiHistoryGuiSide> gui_;
    GuiHistoryNowSide                        now_;
    std::vector<Member>                      members_;
    std::array<std::vector<std::optional<GuiHistoryCommitDelta>>, 2> cache_;
};

// -- THE COMMIT ACT — the product's one mutating git route ------------------
//
// What the mode reads, it can now also WRITE: while the history mode stands,
// Ctrl+S saves the piece and commits the live authoring state into its directory
// in the projects repository (the mode bit selects the command, exactly as the
// iteration bit selects the sweep; the chord was Ctrl+Alt+R until 2026-08-08,
// when the architect moved the act onto the chord its first step already is).
// The GUI half — the COMMIT-TITLE EDITOR that
// asks for the message (2026-08-07, superseding the confirmation prompt), THE
// ORDINARY SAVE THAT RUNS FIRST (2026-08-04: the act is "Save and Commit", and a
// failed save refuses it before this module is reached at all), the CLOSE that
// ends the view once THE SAVE has landed (2026-08-07, superseding the
// checkpoint-in-the-repository partition), the dispatch onto the background
// worker and the CRITICAL SLOT its failures write (architect 2026-08-09,
// replacing the acknowledge notice they used to raise) — lives at
// GuiInputHandler::run_history_commit; what lives here is the act itself.

// THE DEFAULT commit message: `Update <id>`, where the id is the piece
// directory's own leaf name ("projects/550 - 1" -> "Update 550 - 1").
//
// SINCE 2026-08-07 IT IS A PREFILL RATHER THAN THE MESSAGE ITSELF (architect,
// superseding "the message is derived, not chosen"): the act opens a
// commit-title editor seeded with this, and whatever the user leaves in that
// buffer is the title the act carries. This stays the ONE owner of the default
// spelling, and it has exactly one reader — the editor's opener.
std::string history_checkpoint_title(const std::string& project_directory);

// HOW FAR THE ACT GOT — six answers over ONE sanctioned path (the act's own head
// in the .cpp owns the model; this says what each value means to the caller).
//
// WriteFailed — NOTHING REACHED THE REPOSITORY. The three sidecars could not be
// written, or the act refused before writing them at all: a DETACHED HEAD is
// unsanctioned use and throws here, since there is no branch to publish onto.
//
// CommitFailed — git made no checkpoint THE ACT CAN STAND BEHIND, which is not
// quite the same as "no commit exists", and the difference splits its arms in
// two.
//   BEFORE THE COMMIT, nothing has been asked of git but reads, so the three
//   files are written and sitting in the working tree where `git status` shows
//   them and a hand `git commit` finishes them: an unusable `git status`, the
//   mid-act branch-mismatch tripwire, and a branch tip that could not be read
//   before committing.
//   AT OR AFTER THE COMMIT, a commit MAY exist that this verdict cannot see. The
//   commit that reported failure is usually one that made nothing — but a
//   commit that landed and then hung in `post-commit` past the deadline lands
//   here too, tip moved and all (git moves HEAD before running the hook). The
//   commit that reported nothing and MOVED NO TIP made nothing, and that is what
//   the tip compare is for: the child's exit status is unreadable here, so a
//   moved tip is the only proof a commit happened, and a rejecting pre-commit
//   hook or an identity/signing failure is caught by its absence. And a tip that
//   could not be read AFTER committing says nothing either way.
// SO THE TERMINAL SHOWS WHICH, and that is the ruled model rather than a gap:
// `git status` and `git log` answer in one look, a hand commit finishes an
// unfinished one, and the next act's clean arm re-observes whatever was left.
//
// NothingToCommit — THE CLEAN, IN-SYNC ENDING: the bytes just written are what
// the branch already carries AND the remote-tracking ref carries the branch.
// Committed and published already, nothing to do. It is the one clean ending
// beside Committed and the caller treats the two alike — including clearing a
// standing failure report, which is how a push made IN THE TERMINAL is
// recognized by the next act.
//
// Unconfirmed — THE ACT COULD NOT ESTABLISH ITS ANSWER, in three shapes: the
// paths are clean but the branch is BEHIND its remote (unpushed commits — the
// terminal's job under this model); the paths are clean and the remote could not
// be read; or a checkpoint WAS committed and the remote-tracking ref could not
// be read to say whether the push arrived. An unanswerable question is never a
// yes, and never clears a standing report.
//
// CommittedNotPushed — the checkpoint is in the local branch and the push did
// not land: the guard refused the destination, the push reported failure, or —
// the observed arm — the remote-tracking ref was SEEN not to carry the
// checkpoint afterwards, which is what catches a push that exited nonzero while
// still looking like success to the subprocess layer. The fix is `git push` in
// the terminal; the next act's clean arm observes it and takes the report down.
enum class GuiHistoryCommitOutcome {
    WriteFailed,
    NothingToCommit,
    Unconfirmed,
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
// `title` IS THE COMMIT MESSAGE, and the caller's (the commit-title editor's
// buffer, seeded from history_checkpoint_title). It is written and never read
// back: the act matches on nothing, the content-signature attribution that once
// did having gone with the graded machinery (2026-08-09).
//
// IT RUNS ON A BACKGROUND WORKER SINCE 2026-08-07 (GuiHistoryCommitWorker),
// which changes nothing in this body: every argument is a value the caller
// captured on the main thread, this function reads no shared state, and its
// stderr lines print from the worker thread in the same order they always did.
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
// SUCCESS IS AN OBSERVATION; EVERY OTHER ANSWER IS THE TRANSPORT'S (2026-08-09,
// with the strict model). `Committed` is claimed only when the remote-tracking
// ref is SEEN to carry the checkpoint — silence is never read as a yes, and an
// unanswerable verify is `Unconfirmed`. The failures, by contrast, are the
// child's own account: a commit or a push that git reported as failing IS the
// failure, with the recorded cost that a commit landing under a hung
// `post-commit` hook reads as `CommitFailed`. The act's head in the .cpp owns
// the ruling, the seven steps and the accepted consequences.
//
// IT CREATES NO DIRECTORY AND NEEDS NONE: `project_directory` is the folder the
// SOURCE is sitting in, so it exists by construction. The first checkpoint of a
// brand-new piece is still an ordinary act of this view — put the piece in its
// own folder under `projects/` and Save and Commit does the rest — exactly as
// the first checkpoint after a schema change is.
GuiHistoryCommitOutcome commit_history_checkpoint(
    const std::string& project_directory, const std::string& base_name,
    const std::string& projects_repo, const GuiHistoryNowSide& bytes,
    const std::string& title);
