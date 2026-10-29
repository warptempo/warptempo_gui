#pragma once

// THE TRIM PRESETS FILE (architect 2026-10-28 night) — the piece's named trim
// windows, the TRIM key's list (AppState::FolderOverlay::Owner::TrimPresets,
// folder_overlay.h), in a GUI-OWNED DEVICE-LOCAL FILE the project folder
// holds, `<project>/trim/presets.txt`, the score viewer's file's shape
// (score_viewer_file.h): the project folder by score_viewer_project_folder's
// rule, no sidecar, no `.settings` key, nothing in the parser, and the CLI
// reads none of it. THE FILE NEVER TRAVELS AND IS NEVER COMMITTED: the
// projects repository ignores `projects/*/trim/` beside `score/`, and the sync
// script touches nothing of it; the rendering trim itself travels in the
// `.settings` sidecar, so a preset is a device's convenience over it, never
// the piece. The `trim/` folder is invisible to the project model's source
// rule by construction — its walk reads regular files only (project_model.h's
// head).
//
// THE GRAMMAR: one line per preset, LF-terminated, IN THE LIST'S ORDER —
// THE MUSIC'S ORDER (trim_preset_before), the file and the screen never
// disagreeing:
//   <begin> <end> <name>
// begin and end in the canonical frame spelling (parse_authored_frame,
// frame_format.h), one space after each, the NAME THE REST OF THE LINE
// VERBATIM. The pair is the active tab's trim as the store holds it — whole
// source frames, inclusive [0, total-1], begin < end strictly (a resting trim
// is never crossed or equal, input_trim.cpp's head). The name is THE NAME
// GRAMMAR's (trim_preset_name_refusal below): a line the name editor could
// have committed. The list's row shows the name alone
// (build_trim_presets_rows, architect 2026-10-28 night); the list's open seat
// on the preset whose pair is the active tab's trim is how a match shows.
//
// THE READ'S THREE OUTCOMES, at the list's open (read_trim_presets_file),
// the score viewer's non-fatal road ("other users shouldn't need the exact
// same file I use"):
//   * ABSENT — an empty list, no stderr line, no file made.
//   * PRESENT AND WELL-FORMED — its presets, SORTED into the list's order
//     (trim_preset_before): a file in another order (hand-edited) reads and
//     shows sorted, no fault and no stderr line.
//   * PRESENT AND MALFORMED (a line off the grammar, a last line with no LF,
//     a pair outside [0, total-1] or not strictly begin < end, an empty,
//     space-edged, filter-refused or duplicate name, an unreadable file) —
//     ONE stderr line naming the file and the first fault, and the list
//     reads EMPTY. Class 5, advisory: the next change rewrites the file
//     whole from the list on screen.
//
// THE WRITE (write_trim_presets_file): the whole list at every change —
// the name editor's commit, Save's and Delete's answers — atomically, the `trim/`
// folder made at the first write, the list sorted first by its one caller
// (GuiInputHandler::persist_trim_presets), so a preset Save moved is written
// where the screen shows it. Its failure is the caller's to card.

#include "failure.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// ONE PRESET: the trim pair and its name.
struct GuiTrimPreset {
    int64_t     begin = 0;
    int64_t     end   = 0;
    std::string name;
};

// THE LIST'S ORDER (architect 2026-10-28 night, "agree": "chronological
// order, and then by length, shortest first, to disambiguate") — THE MUSIC'S
// ORDER, not the save order: BY BEGIN ASCENDING, THEN BY END ASCENDING (of
// two presets beginning together the shorter first), THEN BY NAME in byte
// order (std::string's compare) for two names on one pair. Names are unique
// (trim_preset_name_refusal), so the order is total. ONE SORT, TWO CALLERS:
// the read (read_trim_presets_file) and the write road
// (GuiInputHandler::persist_trim_presets), every change sorting before it
// writes.
inline bool trim_preset_before(const GuiTrimPreset& a,
                               const GuiTrimPreset& b) {
    if (a.begin != b.begin) return a.begin < b.begin;
    if (a.end != b.end)     return a.end < b.end;
    return a.name < b.name;
}

// `<project_folder>/trim/presets.txt`.
std::filesystem::path trim_presets_file_path(
    const std::filesystem::path& project_folder);

// THE READ (the head's three outcomes) against the piece's `total_frames`,
// the presets answered in the list's order (trim_preset_before); the
// malformed outcome prints its one stderr line here and answers the empty
// list, so the caller has one road.
std::vector<GuiTrimPreset> read_trim_presets_file(
    const std::filesystem::path& project_folder, int64_t total_frames);

// THE NAME GRAMMAR, the one owner the name editor's commit and the reader
// ask: UTF-8 free text the editors' one incoming filter keeps whole
// (text_editor::replace_selection — no LF, no control byte, well-formed, the
// name editor's byte cap), NON-EMPTY, no leading or trailing space, and
// UNIQUE among `presets`. Answers the refusal's reason (one clause, no line
// number), or std::nullopt for a name that commits.
std::optional<std::string> trim_preset_name_refusal(
    std::string_view name, std::span<const GuiTrimPreset> presets);

// THE WRITE (the head): `presets`, the whole file, IN THE LIST'S ORDER (the
// caller sorted them, trim_preset_before; asserted), atomically (tmp + fsync +
// rename, atomic_write_string_to_path), the `trim/` folder created first.
// Answers the failure's two clauses (failure.h), the display naming the file
// as the project's files are named (`trim/presets.txt`), or std::nullopt.
std::optional<GuiFailure> write_trim_presets_file(
    const std::filesystem::path& project_folder,
    std::span<const GuiTrimPreset> presets);
