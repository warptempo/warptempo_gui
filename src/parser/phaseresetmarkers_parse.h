#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

// One phase reset marker's serialized form — position plus an optional
// disabled flag. The parser domain consumes this base directly; the
// engine-internal PhaseResetMarker (stft_container.h, synth_frame)
// is a different, engine-private type and never co-visible with this one.
struct PhaseResetMarker {
    // Authored position: a whole source frame held in an int64_t — a
    // fractional authored position is unrepresentable (fractional position
    // text is load-fatal, and every gesture commit converts through
    // snap_authored_frame). Serialized as plain integer text via
    // frame_format.h; timestamps are display-only renderings.
    int64_t time_frame  = 0;
    bool    disabled    = false;
};

// Parse a .phaseresetmarkers file. Never throws. Returns the parsed markers on
// success; an empty file — no lines, or only blank lines (which are skipped) —
// yields an empty vector. A leading '#' is the disabled-marker prefix, NOT a
// comment: a '#' line whose remainder is not a valid frame position is
// load-fatal like any other malformed line (there is no comment concept in the
// grammar). On the first malformed line, or an unopenable file, returns a
// one-line diagnostic (line-tagged where line-specific). Canonical reader for
// the GUI store and the headless CLI.
std::expected<std::vector<PhaseResetMarker>, std::string>
parse_phaseresetmarkers_file(const std::string& path);
