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

    // A phase reset carries nothing beside its position and its disabled
    // bit (architect approval 2026-09-16, comment only, with the measures
    // feature's deletion). A magnification level is a field of neither this column nor the
    // warp one: the magnification level markers column carries it as its own
    // payload (MagnificationLevelMarker::level,
    // magnificationlevelmarkers_parse.h) (architect approval 2026-09-16).
};

// Parse a .phaseresetmarkers file. Never throws. Returns the parsed markers on
// success; an empty file — no lines at all — yields an empty vector. A blank
// line is NOT skipped: it is load-fatal like any other malformed line (the
// writer emits none). A leading '#' is the disabled-marker prefix, NOT a
// comment introducer: a '#' line whose remainder is not a valid frame position
// is load-fatal. A line is `[#]<frame position>` and nothing else: comment
// LINES do not exist in the grammar and no marker line carries a suffix, so a
// ` //` meets the whitespace refusal and is load-fatal like any other
// adversarial line (architect approval 2026-09-14; the warp column's own
// comment left its grammar 2026-09-16 — architect approval 2026-09-16,
// comment only here; warpmarkers_parse.h).
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
