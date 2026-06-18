#pragma once

#include <cmath>
#include <cstdio>
#include <regex>
#include <string>

// Canonical persistence timestamp: MM:SS.mmm, minutes and seconds 00-59,
// three-digit milliseconds. The single definition of the on-disk timestamp
// format; all parsers validate through this. Hard-capped at 59:59.999 — source
// material is single movements, never long enough to need a wider field.
inline bool is_valid_timestamp_format(const std::string& s) {
    static const std::regex re("^([0-5][0-9]):([0-5][0-9])\\.[0-9]{3}$");
    return std::regex_match(s, re);
}

// Parse "MM:SS.mmm" to seconds. Caller validates format first.
inline double parse_timestamp(const std::string& s) {
    const int min    = std::stoi(s.substr(0, 2));
    const double sec = std::stod(s.substr(3));
    return min * 60.0 + sec;
}

inline std::string format_timestamp(double seconds) {
    if (seconds < 0) seconds = 0;
    // Hard cap: 59:59.999. The format carries two minute digits; movement-length
    // source never reaches this, and clamping guarantees a writable, reloadable
    // timestamp rather than an out-of-range MM that the validator rejects.
    if (seconds > 3599.999) seconds = 3599.999;
    long total_ms = static_cast<long>(std::nearbyint(seconds * 1000.0));
    const long m  = total_ms / 60000;
    total_ms     -= m * 60000;
    const long s  = total_ms / 1000;
    const long ms = total_ms - s * 1000;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02ld:%02ld.%03ld", m, s, ms);
    return buf;
}

// Snap `seconds` to the millisecond grid the .settings / .warpmarkers /
// .phaseresetmarkers formats persist at. Defined as the persistence
// round-trip so an authored value equals its own reloaded value exactly.
// Idempotent: snapping an already-gridded value is a no-op.
inline double snap_to_timestamp_grid(double seconds) {
    return parse_timestamp(format_timestamp(seconds));
}
