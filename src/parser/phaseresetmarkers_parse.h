#pragma once

#include "marker_measure.h"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

// One phase reset marker's serialized form — position, an optional disabled
// flag, and an optional measure reference. The parser domain consumes this
// base directly; the engine-internal PhaseResetMarker (stft_container.h,
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

    // MEASURE REFERENCE (architect approval 2026-08-20), serialized as the
    // ` //<measure>` suffix past the canonical line — grammar, canonical
    // spelling and byte bound in marker_measure.h. Empty means no measure; the
    // writer emits no suffix for it and the bare suffix is load-fatal.
    //
    // The base-home rationale is the warp column's, stated once at
    // WarpMarker::measure (warpmarkers_parse.h): the field must round-trip
    // through the file and both readers are shared, while nothing past the
    // parser domain — no engine marker, no render fingerprint — ever sees it.
    std::string measure;
};

// Parse a .phaseresetmarkers file. Never throws. Returns the parsed markers on
// success; an empty file — no lines at all — yields an empty vector. A blank
// line is NOT skipped: it is load-fatal like any other malformed line (the
// writer emits none). A leading '#' is the disabled-marker prefix, NOT a
// comment introducer: a '#' line whose remainder is not a valid frame position
// is load-fatal. A measure is a SUFFIX — ` //<measure>` past the canonical
// line, marker_measure.h — and comment LINES do not exist in the grammar; a
// malformed measure (off the grammar, past the byte bound, or the bare
// separator with nothing after it) is GUI-unproducible and load-fatal
// like any other adversarial line. On the first malformed line, or an
// unopenable file, returns a one-line diagnostic (line-tagged where
// line-specific). Canonical reader for the GUI store and the headless CLI.
std::expected<std::vector<PhaseResetMarker>, std::string>
parse_phaseresetmarkers_file(const std::string& path);
