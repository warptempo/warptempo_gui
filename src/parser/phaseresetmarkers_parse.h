#pragma once

#include <expected>
#include <string>
#include <vector>

// One phase reset marker's serialized form — position plus an optional
// disabled flag. The parser domain consumes this base directly; the
// engine-internal PhaseResetMarker (stft_container.h, synth_frame/src_frame)
// is a different, engine-private type and never co-visible with this one.
struct PhaseResetMarker {
    // Authored position: a whole source frame held in a double
    // (integer-valued by grammar — fractional position text is load-fatal;
    // a follow-up retypes to int64_t). Serialized as plain integer text via
    // frame_format.h; timestamps are display-only renderings.
    double time_frame  = 0.0;
    bool   disabled    = false;
};

// Parse a .phaseresetmarkers file. Never throws. Returns the parsed markers on
// success; an empty or comment-only file yields an empty vector. On the first
// malformed line, or an unopenable file, returns a one-line diagnostic
// (line-tagged where line-specific). Canonical reader for the GUI store and the
// headless CLI.
std::expected<std::vector<PhaseResetMarker>, std::string>
parse_phaseresetmarkers_file(const std::string& path);
