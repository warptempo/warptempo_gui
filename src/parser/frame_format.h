#pragma once

#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <system_error>

// Serialization of authored positions. The authored time domain is source-frame
// doubles, fractional legal; these two helpers are the single definition of the
// on-disk number form. Every producer and consumer of an authored position —
// the marker parsers, the settings trim reader, the serializers, the
// render-view sidecar publisher, and the CLI — goes through exactly this pair,
// so persistence round-trips bit-exactly and there is no second representation
// to disagree with the walls. Timestamps (MM:SS.mmm) are display-only
// renderings; see time_format.h.

// Frame double -> shortest round-trip decimal text (std::to_chars general
// form). An integer-valued frame serializes without a fractional part ("0",
// "44100"); fractional frames keep exactly the digits needed to reload the
// identical double.
inline std::string format_frame_double(double frame) {
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), frame);
    return std::string(buf, res.ptr);
}

// Text -> frame double. Strict: the whole field must be consumed, and the
// value must be finite and non-negative. Rejecting NaN, inf, negative, empty,
// and trailing junk is the parser's malformed-position rule — adversarial,
// load-fatal, the same category as a malformed timestamp used to be. Returns
// true and sets `out` on success; returns false and leaves `out` untouched on
// failure.
inline bool parse_frame_double(std::string_view s, double& out) {
    if (s.empty()) return false;
    // A leading '-' is rejected up front: "-0" would otherwise parse to the
    // negative-zero double, which compares equal to 0.0 and would slip past
    // the non-negative check while round-tripping as "-0".
    if (s.front() == '-') return false;
    double v = 0.0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{}) return false;
    if (res.ptr != s.data() + s.size()) return false;
    if (!std::isfinite(v) || v < 0.0) return false;
    out = v;
    return true;
}
