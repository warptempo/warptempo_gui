#pragma once

#include "failure.h"
#include "history_diff.h"

#include <functional>
#include <string>
#include <vector>

// THE FOLDER ROAD — THE EXPORTED HISTORY, READ WITHOUT GIT (architect
// 2026-09-17).
//
// The tablet runs no git binary at all (platform-seam.md), so the committed
// half of the `h` view was unreachable there and the session walk was the
// whole of what the glass had. The answer is not a git port: the LAPTOP
// EXPORTS the piece's committed sidecar history as a plain folder and the sync
// script ships it, and this module walks that folder with exactly the
// vocabulary the commit walk already speaks — the same members, the same
// strict load gate, the same deltas, the same `'` load in place.
//
// THE APP FORKS ON THE FOLDER, NOT THE BACKEND. If the source's parent folder
// holds a `history/` directory, the commit walk reads IT and runs no git at
// all — on the tablet and on the laptop alike. There is no backend term, no
// settings key, no lamp and no card at the entry: the folder's presence is the
// whole decision (history_folder_of_source below is the one place it is asked,
// and resolve_history_walk_header asks it AHEAD of the clone derivation). The
// laptop simply never has one, the exporter writing to scratch and pushing.
//
// THE FORMAT — THIS HEADER IS THE CONTRACT and the exporter follows it:
//
//     <project>/history/                    its presence selects this road
//     <project>/history/<seq>_<sha7>/       one member per exported checkpoint
//         <stem>.warpmarkers                the three sidecars under their REAL
//         <stem>.phaseresetmarkers          names (kSidecarExtensions), so the
//         <stem>.settings                   strict loaders are the same ones a
//                                           source's own sidecars meet
//
//   <seq> is decimal digits — the checkpoint's 1-based ordinal in the piece's
//   history, OLDEST FIRST. The exporter zero-pads to four (`0001`); the reader
//   parses the digits NUMERICALLY and enforces no width, so a five-digit
//   history needs no new rule. GAPS ARE LEGAL: a commit the exporter cannot
//   place in one folder is skipped and keeps its number. THE WALK IS NEWEST
//   FIRST — descending `seq`, ties (which the exporter never produces) broken
//   by name bytewise descending — so the order is total and deterministic.
//
//   <sha7> is exactly seven lowercase hex digits, the commit's short SHA. It
//   IS the member's `sha` and its label: short_sha returns a seven-character
//   string unchanged, so HistoryMode::member_label, the walk line and the `'`
//   confirmation spell it with no change of their own.
//
//   NOTHING ELSE IS READ. There is no metadata file, no date and no title: the
//   walk line shows `n/N <sha7>` and a scale clause derived from the sidecars,
//   so a member needs its SHA and nothing more. Files beside the three are
//   not looked at — A RETIRED SIDECAR INCLUDED: a member exported while the
//   set was four carries a `<stem>.magnificationlevelmarkers` beside them, and
//   the member stays eligible on its three, that file never stat'd or read
//   (architect approval 2026-09-23; the git road's own rule, history_diff.h's
//   eligibility paragraph). An entry under `history/` that is not a directory, or whose
//   name is not `<digits>_<7 lowercase hex>`, is NOT A MEMBER — it is skipped
//   with one stderr line naming it and is not counted as hidden, hidden being
//   reserved for an export the strict load refuses. A member the strict load
//   refuses IS hidden and counted, exactly as a load-fatal commit is, and the
//   prefetch drain's counted line says so in the same words (a member is a
//   commit's export).
//
//   FILE MTIMES ARE THE EXPORTER'S BUSINESS — it stamps each file with the
//   commit's date so `adb push --sync` skips an unchanged export — and this
//   reader ignores them entirely.
//
// THE OUTSIDE-OBSERVER ANSWER, because a folder of sidecars sitting inside a
// project invites the question: it is a DERIVATION of the git history
// restricted to one piece — a pure function of the clone, which any clone can
// regenerate — written by the architect's personal script
// `~/.pc/bash/wts_history` (outside this repository, beside `wts`, invoked by
// `wts tt` as a tablet-only pass). The `.git` itself never travels, and a
// bundle would need git on the reader, which is the whole thing the tablet
// does not have. History only grows, so the export never deletes.
//
// SAVE AND COMMIT IS REFUSED ON THIS ROAD, and that is the one thing the two
// roads do not share: an export is a derivation, not a clone — there is
// nothing to commit into. The predicate is GuiHistoryDiff::takes_checkpoint,
// the GUI spells it history_checkpoint_road_available (app_state.h), and the
// key's card is kHistoryFolderNoCheckpoint (history_diff.h). Everything else
// the commit walk does works here: `,`/`.`, both compare readings, the diff
// lane, the selection and bare `v`, the paired march, Ctrl+Tab, and `'`.

// The folder's name under the project. ONE owner.
inline constexpr const char* kHistoryFolderName = "history";

// THE ROAD'S ONE FORK: `<source's parent>/history` when it is a directory,
// else empty. ITS READERS: resolve_history_walk_header (the road decision) and
// read_history_walk_tip (the staleness key) — both in history_diff.cpp — and
// nothing else asks the filesystem this question.
std::string history_folder_of_source(const std::string& source_audio_path);

// One member folder, as its name spells it. `seq` is the LISTING'S ORDER TERM
// and nothing else — the load below reads `path` and `sha7` alone, and spells
// the member in its sentences by the folder's own leaf name — so a caller that
// already holds a member's address (the `'` act) may leave it zero.
struct GuiHistoryFolderMember {
    std::string        path;   // absolute
    unsigned long long seq = 0;
    std::string        sha7;
};

// THE LISTING, newest first (the format's order above). False with `failure`
// set ONLY when the directory could not be LISTED — the read that did not
// answer, and this road's one arm of GuiHistoryScanResult's enumeration; an
// empty folder is an empty vector and TRUE, which is the ruled empty walk.
// Non-member entries are skipped with their stderr line here.
bool list_history_folder_members(const std::string& history_folder,
                                 std::vector<GuiHistoryFolderMember>& out,
                                 GuiFailure& failure);

// THE STRICT WHOLE-SET LOAD OF ONE MEMBER — this road's load gate AND its `'`
// act, load_commit_sidecars_strict's twin over files instead of blobs and one
// predicate for both askers exactly as that one is.
//
// The three files at `<member>/<base_name><ext>` must be REGULAR FILES (a
// missing one refuses naming it, the partial-commit arm's own rule: a load in
// place is a whole-state replace, and some files from the export plus the rest
// from nowhere composes a state no checkpoint ever was); their bytes are read
// whole into the member's blob texts, which are the delta's then side; and the
// three STRICT WHOLE-FILE LOADERS then judge THE FILES THEMSELVES in the
// sibling's order — settings, warp, phase reset. THERE IS
// NO SCRATCH STAGING HERE: the files already sit under their real names, which
// is the one thing this road has that the git one does not. First error only.
//
// `out.sidecars.sha` is the member's sha7 and `out.sidecars.folder` its path.
// Each blob's `path` is the file's PROJECT-RELATIVE spelling,
// `history/<seq>_<sha7>/<file>`, and every refusal names that spelling on the
// display clause and the full path on the diagnostic (path_failure,
// failure.h).
bool load_history_folder_member_strict(const GuiHistoryFolderMember& member,
                                       const std::string&    base_name,
                                       GuiHistoryCommitLoad& out,
                                       GuiFailure&           failure);

// THIS ROAD'S SCAN — scan_history_walk's body past the header, and the same
// callback contract: the listing (a failure ends the run NOT ok with its
// reason), then per member `abandoned()` first, the strict load, `on_member`
// or ++hidden, then `on_done` with the count. No git anywhere in it.
void scan_history_folder_walk(
    const std::string& history_folder, const std::string& base_name,
    const std::function<bool()>&                         abandoned,
    const std::function<void(GuiHistoryCommitSidecars)>& on_member,
    const std::function<void(GuiHistoryScanResult)>&     on_done);
