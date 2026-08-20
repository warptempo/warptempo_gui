#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct AppState;

// THE SCORE-VIDEO ACT (architect 2026-08-20) — Shift+`/` and the Measure
// button's shift admission open or seek the architect's mpv to the FOCUSED
// marker's resolved measure. It is the seat the measure's `/` arm reserved on
// the day the field was rebranded, and it is the whole reason the free-text
// comment became a grammar.
//
// IT IS PURE NAVIGATION AND IT PAINTS NOTHING. No store write, no undo entry,
// no record_gesture, no damage, no view switch, no playback change — the GUI
// reads three things (the focused marker's measure, the map beside the source,
// the mpv socket) and writes one line to another process. Every refusal on the
// way is a CONSUMED SILENT NO-OP: nothing focused, no measure, an unresolved
// `+` chain, no map, no video, or an mpv that could neither be reached nor
// started. That taxonomy is the act's whole error vocabulary; the only thing it
// ever prints is one stderr line where an mpv it TRIED to drive refused.
//
// NO LIVE SYNC, NO FOLLOW, NO SPEED SYNC (architect 2026-08-20): the act is a
// JUMP. mpv is not driven again until the next press, and the GUI never reads
// mpv's position back — there is no second channel, no poll and no state on
// this side beyond the socket path, which is derived per act rather than held.
//
// ------------------------------------------------------------------------
// THE MAP is <project>/sheet/sheet.map, where <project> is the SOURCE FILE'S
// OWN PARENT FOLDER — the GitHub recheck's folder law (history_diff.h) read
// off the filesystem alone, with no git in it. tools/extract_sheet_map.py is
// its one producer; the whole sheet/ folder is gitignored, local to the host
// that made it, and the GUI never writes a byte of it.
//
// THE VIDEO is <project>/sheet/sheet.webm when it is there, seeked to the map's
// own window-relative time; failing that the map header's `# src` names a file
// under <project>/sheet/src/, seeked to window_start + t, so a map extracted
// from a slice of one long rip can drive that rip untrimmed. Neither present is
// a no-op like every other refusal.

// One map anchor: a time in the video (SECONDS, relative to the map's window
// start) and the measure whose downbeat sits there. Anchors are the tool's
// monotonic first-pass page starts, strictly increasing in both fields.
struct GuiScoreVideoAnchor {
    double  seconds = 0.0;
    int64_t measure = 0;
};

// A parsed sheet.map. `ok` false means the act refuses — a missing file and a
// malformed one are the same answer, deliberately (see the parser's class note
// in score_video.cpp).
// THE WHOLE HEADER IS OPTIONAL, and a headerless map is not a defect: the tool
// writes `# src` / `# window` only for a WINDOWED extraction, a run over a
// whole video having no source offset to record. An absent `# window` therefore
// means window_start 0 — which is the truth for such a map, not a fallback —
// and an absent `# src` simply leaves sheet.webm as the only playable path.
struct GuiScoreVideoMap {
    std::string src;                 // `# src` basename; empty when absent
    double      window_start = 0.0;  // seconds into the `# src` file
    std::vector<GuiScoreVideoAnchor> anchors;
    bool        ok = false;
};

// Read and validate <project_dir>/sheet/sheet.map. Never throws; never writes.
// A file that is absent, unreadable or off the grammar comes back with `ok`
// false and no diagnostic — the act's silent-refusal rule.
GuiScoreVideoMap load_score_video_map(const std::string& project_dir);

// THE ACT. Takes the app CONST because it changes nothing in it: the caller has
// already repaired the focus, and everything from here is a read plus one
// write to another process.
void run_score_video_jump(const AppState& app);
