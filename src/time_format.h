#pragma once

#include <cmath>
#include <cstdio>
#include <regex>
#include <string>

// MM:SS.mmm is a DISPLAY-ONLY rendering. Authored positions are source-frame
// doubles end to end (serialized via frame_format.h); no file format carries
// timestamps. Display surfaces derive their text as
// format_timestamp(frame / sample_rate).

// Validate "MM:SS.mmm" (minutes and seconds 00-59, three-digit milliseconds).
// Retained solely for the standalone sidecar migration tool; there are zero
// call sites in the GUI, parser, and CLI.
inline bool is_valid_timestamp_format(const std::string& s) {
    static const std::regex re("^([0-5][0-9]):([0-5][0-9])\\.[0-9]{3}$");
    return std::regex_match(s, re);
}

// Parse "MM:SS.mmm" to seconds. Caller validates format first. Retained solely
// for the standalone sidecar migration tool; there are zero call sites in the
// GUI, parser, and CLI.
inline double parse_timestamp(const std::string& s) {
    const int min    = std::stoi(s.substr(0, 2));
    const double sec = std::stod(s.substr(3));
    return min * 60.0 + sec;
}

inline std::string format_timestamp(double seconds) {
    if (seconds < 0) seconds = 0;
    // Hard cap: 59:59.999. The format carries two minute digits; movement-length
    // source never reaches this, and the cap keeps the display shape stable.
    // Display-only clamping — no persisted value flows through this function.
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
