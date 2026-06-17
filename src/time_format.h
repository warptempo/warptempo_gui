#pragma once

#include <cmath>
#include <cstdio>
#include <string>

// Parse "MM:SS.mmm" to seconds. Caller validates format first.
inline double parse_timestamp(const std::string& s) {
    const int min    = std::stoi(s.substr(0, 2));
    const double sec = std::stod(s.substr(3));
    return min * 60.0 + sec;
}

inline std::string format_timestamp(double seconds) {
    if (seconds < 0) seconds = 0;
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
