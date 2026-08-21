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
// the mpv socket) and writes one COMMAND BATCH to another process — the jump
// itself, the audition's speed, and pitch correction off, three newline-
// delimited JSON commands sent whole on one connection.
//
// THE REFUSALS SPLIT IN TWO, on whether an mpv was involved at all. EVERYTHING
// UP TO THE TRANSPORT IS A CONSUMED SILENT NO-OP — nothing focused, no measure,
// an unresolved `+` chain, no map, no video, a seek outside the accepted time
// domain — because each of those is an ordinary state of the piece and the GUI
// has nothing to report about it. THE TRANSPORT'S TWO FAILURES EACH PRINT ONE
// STDERR LINE: a socket that was REACHED but could not be written to, and an
// mpv that could not be SPAWNED. Both name what was observed and stop there,
// which is the only honest register available — no reply is ever read from
// mpv, so this side never learns what mpv thought of the command and must
// never claim to. Nothing reaches the GUI either way: no chip, no prompt, no
// pixel (the wind-down rule for a rare non-silent fault outside the product's
// own state).
//
// NO LIVE SYNC AND NO FOLLOW (architect 2026-08-20): the act is a JUMP. mpv is
// not driven again until the next press, and the GUI never reads mpv's position
// back — there is no second channel, no poll and no state on this side beyond
// the socket path, which is derived per act rather than held.
//
// THE SPEED RIDES THE JUMP THOUGH, and that is not a sync (architect ruling
// 2026-08-20): each jump SENDS the audition's current rate, and nothing follows
// a later change until the next jump. SOURCE view sends `playback_speed`;
// TARGET view sends 1.0, the rendered preview always playing at natural rate.
// PITCH CORRECTION IS TURNED OFF with it, deliberately and permanently: the
// app's audition is VARISPEED (a fractional-cursor linear-interp fill — pitch
// falls with speed like a tape machine), so matching it means letting mpv's
// pitch fall too. The reasoning lives at the send site; do not "fix" it.

// A MEASURE IS A (SECTION, MEASURE) PLACE, not just a number (2026-08-20). Maps
// record the PRINTED bar numbers, so a score whose numbering restarts mid-way
// has two bars called 12 and the SECTION says which — 1 for the opening
// numbering, +1 at each printed restart in video order. The act resolves the
// marker's measure to a (section, rational) pair and interpolates WITHIN that
// section's own anchors, clamping at the section's first and last, because
// measure numbers are only comparable inside the numbering that printed them.
// A measure naming a section the map does not carry is one more silent no-op.
//
// IT RUNS ON THE GUI THREAD AND ITS COST IS BOUNDED, which is a contract and
// not an implementation detail: the socket work is nonblocking under one
// deadline (kScoreVideoIpcMs, score_video.cpp), so the very worst a press can
// cost the window is that budget even against a peer that owns the socket and
// never services it. The spawn is fire-and-forget and waits on nothing.
//
// ------------------------------------------------------------------------
// THE FOLDER LAW. <project> is the SOURCE FILE'S OWN PARENT FOLDER — the GitHub
// recheck's own rule (history_diff.h) read off the filesystem alone, with no git
// in it — and the piece's score material sits in ONE FLAT FOLDER beside the
// audio:
//
//     <project>/sheet/sheet.map      the map
//     <project>/sheet/<video>        the video, under its original name
//
// THE MAP names the video: its `# src` header carries that basename, the act
// opens exactly that file from this folder, and a map without the line is
// refused (the tool writes it on every run). The GUI never writes a byte of
// either; tools/extract_sheet_map.py is the map's one producer, and the video is
// whatever the architect downloaded. THE MAP IS THE ONLY PART THAT IS COMMITTED
// — .gitignore ignores this folder's children and negates `sheet.map` alone, so
// A CLONE RETAINS THE MAP AND THE JUMP WORKS ONCE THAT CLONE'S OWN COPY OF THE
// VIDEO IS SUPPLIED. Not before: a clone has the map and not the file the map
// NAMES, so every jump there is a silent no-op at the open below until the
// video is put beside it.
//
// THE SEEK IS window_start + t, always: map times are window-relative, so four
// movements' maps address one stable rip by carrying four different windows,
// and a whole-video map simply carries window 0. THERE IS NO SECOND PATH TO TRY
// — a `sheet.webm` cut to the window was preferred here with `sheet/src/`
// behind it until 2026-08-20, and the split retired with the trimming that gave
// it a reason. A missing video is a no-op like every other refusal.

// One map anchor: a time in the video (SECONDS, relative to the map's window
// start), the PRINTED measure whose downbeat sits there, and the SECTION that
// number is printed in. Anchors are the tool's monotonic first-pass page
// starts: time increases strictly across the whole map, while the MEASURE only
// increases within a section — a printed restart takes it back to 1, which is
// the whole reason the section is here.
//
// A map line spells `<seconds>|<measure>` in section 1 and
// `<seconds>|<S>:<measure>` above it, the measure grammar's own qualifier, so a
// map line and a marker's measure field read alike (marker_measure.h).
struct GuiScoreVideoAnchor {
    double  seconds = 0.0;
    int64_t section = 1;
    int64_t measure = 0;
};

// A parsed sheet.map. `ok` false means the act refuses — a missing file and a
// malformed one are the same answer, deliberately (see the parser's class note
// in score_video.cpp).
// `# src` IS REQUIRED and `# window` is not, which follows what each one says:
// the video's name is the one thing a map cannot do without, while a window is a
// fact only a SLICE has — a whole-video map carries none, and window_start 0 is
// then the truth rather than a fallback. `# url` is provenance and is read by
// nobody. Unknown `#` lines are skipped, the format's forward-compatibility
// guarantee (stated at the parser).
struct GuiScoreVideoMap {
    std::string src;                 // `# src` basename; never empty when ok
    double      window_start = 0.0;  // seconds into that video
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
