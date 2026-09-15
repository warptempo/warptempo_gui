#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

// THE MAGNIFICATION LEVEL MARKER COLUMN (architect 2026-09-15; architect
// approval 2026-09-15 for this frozen touch): a third marker column, carried
// in `<stem>.magnificationlevelmarkers` beside the two render columns, whose
// only reader is the waveform picture. Magnification was a field of the warp
// marker until the same day, which tied a loudness picture to a tempo section
// — a two-measure blast inside one section could not be shown — so it is its
// own column now, decoupled from tempo.
//
// DISPLAY-ONLY AND NOT A RENDER INPUT: no engine stage, no render fingerprint
// and no playback path reads this column; the CLI requires and strictly parses
// the file so a sidecar set stays loadable in both products or neither, and
// renders without it.
struct MagnificationLevelMarker {
    // Authored position: a whole source frame, the other columns' domain and
    // wall ([0, total−1], marker_store_validate.h), serialized through
    // frame_format.h.
    int64_t time_frame = 0;
    // The level this marker sets from its frame on: a count of picture
    // doublings in [0, kMarkerMagnificationMax] (marker_magnification.h, the
    // one range and grammar owner). REQUIRED on every line — there is no blank
    // level and no inheritance; a level-0 marker is how the picture returns
    // to unmagnified.
    uint8_t level      = 0;
    bool    disabled   = false;
};

// Parse a .magnificationlevelmarkers file. Never throws. The phase-reset
// parser's discipline exactly (phaseresetmarkers_parse.h): an empty file — no
// lines at all — yields an empty vector; a blank line is load-fatal; a leading
// '#' is the disabled-marker prefix and not a comment introducer; no
// whitespace or CR anywhere on a line; only DECREASING frames refuse, equal
// frames loading. A line is `[#]<frame position>|<level>` and nothing else:
// the frame in the canonical authored spelling (frame_format.h), one `|`, and
// EXACTLY ONE ASCII digit 0..4 (parse_marker_magnification). On the first
// malformed line, or an unopenable file, returns a one-line diagnostic
// (line-tagged where line-specific) — adversarial and load-fatal in both
// binaries. Canonical reader for the GUI store and the headless CLI.
//
// `path_free_reason` carries the warp column's contract
// (warpmarkers_parse.h): when given, the two refusals that name the path —
// and only those — write their words with no path in them there, while the
// returned string stays the composed sentence.
std::expected<std::vector<MagnificationLevelMarker>, std::string>
parse_magnificationlevelmarkers_file(
    const std::string& path,
    std::optional<std::string>* path_free_reason = nullptr);
