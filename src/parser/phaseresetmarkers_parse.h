#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

// One phase reset marker's serialized form — position and an optional disabled
// flag. The parser domain consumes this base directly; the engine-internal PhaseResetMarker (stft_container.h,
// synth_frame) is a different, engine-private type and never co-visible with
// this one.
struct PhaseResetMarker {
    // Authored position: a whole source frame held in an int64_t — a
    // fractional authored position is unrepresentable (fractional position
    // text is load-fatal, and every gesture commit converts through
    // snap_authored_frame). Serialized as plain integer text via
    // frame_format.h; timestamps are display-only renderings.
    int64_t time_frame  = 0;
    bool    disabled    = false;

    // RECORDED ASYMMETRY: phase resets carry no measure and no magnification
    // (architect 2026-09-14, architect approval 2026-09-14 for this frozen
    // touch). Both are the warp column's alone (WarpMarker::measure and
    // WarpMarker::magnification, warpmarkers_parse.h).
};

// Parse a .phaseresetmarkers file. Never throws. Returns the parsed markers on
// success; an empty file — no lines at all — yields an empty vector. A blank
// line is NOT skipped: it is load-fatal like any other malformed line (the
// writer emits none). A leading '#' is the disabled-marker prefix, NOT a
// comment introducer: a '#' line whose remainder is not a valid frame position
// is load-fatal. A line is `[#]<frame position>` and nothing else: comment
// LINES do not exist in the grammar and phase resets carry no measure
// (PhaseResetMarker), so a ` //` suffix meets the whitespace refusal and is
// load-fatal like any other adversarial line (architect approval 2026-09-14).
// On the first malformed line, or an unopenable file, returns a one-line
// diagnostic (line-tagged where line-specific). Canonical reader for the GUI store and the headless CLI.
//
// `path_free_reason` is the warp column's own out-parameter, with the warp
// column's contract (warpmarkers_parse.h; architect approval 2026-09-02, the
// granted frozen touch): when given, the two refusals that name the path —
// and only those — write their words with no path in them there, while the
// returned string stays the composed sentence, so the CLI's line is
// byte-identical and a card's composer can name the file once, its own way.
std::expected<std::vector<PhaseResetMarker>, std::string>
parse_phaseresetmarkers_file(
    const std::string& path,
    std::optional<std::string>* path_free_reason = nullptr);
