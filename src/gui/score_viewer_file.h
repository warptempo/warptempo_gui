#pragma once

// THE SCORE VIEWER'S FILE (architect 2026-10-26 night: "score should not be
// load-fatal: other users shouldn't need the exact same file I use") — the
// score's five keys in a GUI-OWNED file the piece's own score folder holds,
// `<project>/score/viewer.txt`, and in no sidecar: the `.settings` schema
// does not know them (settings_file.cpp's key record) and the CLI reads none
// of this. THE FILE NEVER TRAVELS AND IS NEVER COMMITTED: the projects
// repository ignores `projects/*/score/` on both devices, the sync script
// pushes the pages and the audio and never this file, and each device makes
// its own ("not hard to recreate"), the tablet its author. A movement whose
// pages live in another movement's folder gets a `score/` of its own holding
// this file alone.
//
// THE GRAMMAR is the device config's shape (device_config.h's head): one
// `key=value` line each, LF, ASCII keys, lexed by the shared scanner
// (warptempo_settings::scan_key_value_file — split at the first '=', no
// blank, comment or whitespace tolerance, no duplicate, every key required),
// THE FIVE ALWAYS WRITTEN in kScoreViewerFileKeys's order:
//   score_path=<path>          the score folder RELATIVE TO THE PROJECT
//                              FOLDER, resolved lexically against it
//                              (score_folder_of, score_pages.h) — `./score`
//                              for the movement's own, `../550 - 1/score` for
//                              another's — taken verbatim (any bytes the
//                              lexer hands over); EMPTY for no score
//   score_first_page=<n>       THE VIEWING RANGE in pages.txt's own 1-based
//   score_last_page=<n>        page numbers, whole non-negative numbers in
//                              the canonical frame spelling
//                              (parse_authored_frame), 0 THE EDGE (first 0
//                              the first page, last 0 the last); both nonzero
//                              and crossed (first > last) is a pair the GUI
//                              never writes (score_range_crossed). A number
//                              past the page count reads: the viewer clamps
//                              at use (score_viewer_range).
//   tab_a_score_position=<f>   EACH TAB'S REMEMBERED POSITION, an audio frame
//   tab_b_score_position=<f>   at the score's own `# rate` (pages.txt), the
//                              canonical frame spelling, display scratch with
//                              no range check (score_page_for_position clamps,
//                              and the viewer's open under the soundtrack):
//                              under the soundtrack THE CURSOR'S FRAME
//                              (architect 2026-10-26 night, "it remembers
//                              based on frames"), on the page road the shown
//                              page's begin (score_viewer.h's THE REMEMBERED
//                              POSITION)
//
// THE READ'S THREE OUTCOMES, at the project load beside the sidecars
// (read_score_viewer_file, GuiFileLoader::load_file):
//   * ABSENT — the defaults (score_viewer_file_defaults): `./score` iff
//     `<project>/score/pages.txt` exists, else empty; the range 0 / 0; both
//     positions 0. No stderr line (most pieces have no score), and the read
//     creates no file.
//   * PRESENT AND WELL-FORMED — its values.
//   * PRESENT AND MALFORMED (an unknown or missing key, a bad value, a
//     crossed range, an unreadable file) — ONE stderr line naming the file
//     and the first fault, then the ABSENT road. Class 5, advisory: the
//     load goes on, and the next write overwrites the file whole.
//
// WRITE-THROUGH (write_score_viewer_file): every change of any of the five
// writes the whole file at once — the device config's `last_project` road,
// never the save's — from GuiScoreViewer's page change, START, END and
// RESET, and under the soundtrack its seek, pause, natural end, close and
// page turn under playback (score_viewer.cpp), and from the settings
// editor's five typed keys (settings_editor.cpp). No dirty mark and no undo: the range is the trim's
// class, the positions the playhead's. The `score/` folder is created when
// absent. A failed write is one stderr line and nothing else (class 5).

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

struct AppState;

// The five keys in the writer's order (the head's list), the shared
// scanner's required set.
inline constexpr const char* kScoreViewerFileKeys[] = {
    "score_path",
    "score_first_page",
    "score_last_page",
    "tab_a_score_position",
    "tab_b_score_position",
};

// The whole file, typed, in the writer's order.
struct GuiScoreViewerFile {
    std::string score_path;
    int64_t     first_page     = 0;
    int64_t     last_page      = 0;
    int64_t     tab_a_position = 0;
    int64_t     tab_b_position = 0;
};

// `<project_folder>/score/viewer.txt`.
std::filesystem::path score_viewer_file_path(
    const std::filesystem::path& project_folder);

// The project folder the live piece's file lives under: the source's own
// folder (`.` for a bare file name, the loader's convention).
std::filesystem::path score_viewer_project_folder(const AppState& app);

// THE ABSENT ROAD (the head's first outcome).
GuiScoreViewerFile score_viewer_file_defaults(
    const std::filesystem::path& project_folder);

// THE READ (the head's three outcomes); the malformed outcome prints its one
// stderr line here and returns the defaults, so the caller has one road.
GuiScoreViewerFile read_score_viewer_file(
    const std::filesystem::path& project_folder);

// THE RANGE'S ONE CROSS-KEY RULE: both bounds named and the first past the
// last. The reader refuses such a file (the malformed outcome) and the
// settings editor refuses the commit that would write one; the viewer's
// START and END grey before it (score_viewer_button_enabled).
inline bool score_range_crossed(int64_t first, int64_t last) {
    return first != 0 && last != 0 && first > last;
}

// THE PER-KEY GRAMMAR, the one owner both the reader and the settings
// editor's typed commit ask: std::nullopt when `key` is none of the five;
// an error carrying the refusal's reason (no line number) when the value is
// out of its grammar; otherwise the typed value — `text` for `score_path`,
// `i64` for the four numbers. The crossing rule is cross-key and is the
// caller's (score_range_crossed).
struct GuiScoreViewerValue {
    std::string text;
    int64_t     i64 = 0;
};
std::optional<std::expected<GuiScoreViewerValue, std::string>>
validate_score_viewer_value(const std::string& key, const std::string& value);

// THE RECALL: `key`'s live value as the writer writes it, or std::nullopt
// for a key that is none of the five (the settings editor's Tab recall).
std::optional<std::string> recall_score_viewer_value(const AppState&    app,
                                                     const std::string& key);

// THE WRITE-THROUGH (the head): the live five, the whole file, atomically
// (tmp + fsync + rename, atomic_write_string_to_path), the `score/` folder
// created first. A failure prints its one stderr line.
void write_score_viewer_file(const AppState& app);
