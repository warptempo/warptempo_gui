#pragma once

#include <string>
#include <vector>

// One phase reset marker's serialized form — position plus an optional
// disabled flag. The parser domain consumes this base directly; the
// engine-internal PhaseResetMarker (stft_container.h, synth_frame/src_frame)
// is a different, engine-private type and never co-visible with this one.
struct PhaseResetMarker {
    double time_seconds  = 0.0;
    bool   disabled      = false;
};

// One parse diagnostic. line_number is 1-based; 0 means "file-level, no line".
struct PhaseResetMarkerParseError {
    int         line_number;
    std::string message;
};

// Result of parse_phaseresetmarkers_file. On failure (ok == false) markers is
// empty and errors lists every violation in encounter order. An empty marker
// list with ok == true is valid (an empty or comment-only file).
// had_nonstandard_content is true when the file carried content the canonical
// save would discard (comments or blank lines).
struct PhaseResetMarkersParse {
    std::vector<PhaseResetMarker>           markers;
    std::vector<PhaseResetMarkerParseError> errors;
    bool                                    had_nonstandard_content = false;
    bool                                    ok                      = false;
};

// Parse a .phaseresetmarkers file. Never throws; a missing or unopenable file
// is reported as a file-level error with ok == false. An empty or comment-only
// file parses to an empty marker list with ok == true. This is the canonical
// .phaseresetmarkers reader for both the GUI store and the headless CLI.
PhaseResetMarkersParse parse_phaseresetmarkers_file(const std::string& path);
