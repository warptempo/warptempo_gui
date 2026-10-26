#pragma once

// THE SCORE FOLDER'S TABLE AND ITS READER (architect 2026-10-26, the built-in
// page viewer, score_viewer.h): the pages a project's `score_path` names, one
// row per page with its audio-frame range, read once at the project load
// (file_loader.cpp, beside the sidecars) and at a typed `score_path=` commit
// (settings_editor.cpp), never kept fresh otherwise.
//
// THE GRAMMAR IS tools/score_extract.py'S, its head comment's "THE GRAMMAR OF
// pages.txt" block the one statement of it; this reader cites it and spells
// no second copy. Read here as it stands there, with two readings of the
// code the tool runs (the code is the authority): the `# src` line's text is
// FREE TEXT to the line's end (the tool writes the video's basename verbatim,
// which may carry any byte a file name does), and a page's file name pads its
// number to max(3, the page count's digits) — `page_name`, which pads EVERY
// page of a run of 1000 or more to four.
//
// THE THREE OUTCOMES, by the validation doctrine's classes:
//   * NO FOLDER, OR NO READABLE pages.txt IN IT — no score, the SCORE key
//     greys, ONE stderr line naming the path (class 5, advisory: the folder
//     arrives by the sync script and may lawfully lag the sidecar that names
//     it). `advisory` carries the line's words.
//   * A pages.txt PRESENT BUT OUT OF THE GRAMMAR — a header the grammar does
//     not name or out of its order, a missing `# rate`, a page line out of
//     grammar, page numbers not 1, 2, 3 …, a begin not under its end, a range
//     overlapping its predecessor, zero pages — or a page's file missing: THE
//     PROJECT LOAD HARD-FAILS, the first error only (class 1: the tool writes
//     the file whole and atomically, so a malformed one is hand-damage). The
//     error arm's producer.
//   * A WELL-FORMED FOLDER — the table, every page's file stat'ed present.
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// ONE PAGE: its range in audio frames at the table's rate, begin inclusive,
// end exclusive (the grammar's own words).
struct GuiScorePage {
    int64_t begin = 0;
    int64_t end   = 0;
};

// THE TABLE. `pages` is never empty for a loaded score (zero pages is the
// grammar's refusal); index i is page number i + 1 — THE VIEWER'S PAGE
// NUMBERS ARE pages.txt's, never the printed ones.
struct GuiScorePages {
    std::filesystem::path     folder;
    int64_t                   rate = 0;
    std::vector<GuiScorePage> pages;

    int count() const { return static_cast<int>(pages.size()); }
    // Page index `i`'s file, the tool's own spelling (page_name).
    std::filesystem::path page_file(int i) const;
};

// THE FOLDER A `score_path` NAMES: the value resolved LEXICALLY against the
// project folder (the source's own folder), `./score` or `../550 - 1/score`
// alike; an empty value names none (an empty path).
std::filesystem::path score_folder_of(const std::filesystem::path& project_folder,
                                      const std::string&           score_path);

// THE READ (the head's three outcomes): the error arm is the hard fail's
// sentence, naming the file and, where a line is at fault, its number; an
// engaged table is a score; a disengaged one is no score, `advisory` then
// holding the stderr line's words (the folder or its pages.txt absent or
// unreadable).
struct GuiScoreRead {
    std::optional<GuiScorePages> pages;
    std::string                  advisory;
};
std::expected<GuiScoreRead, std::string> read_score_folder(
    const std::filesystem::path& folder);
