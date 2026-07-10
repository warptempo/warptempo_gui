#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

// Serialization of frame positions — two domains, two pairs.
//
// AUTHORED domain (format_authored_frame / parse_authored_frame): authored
// positions — marker times on both columns and both trim bounds — are whole
// source frames (~23 microseconds each at 44.1 kHz), held in int64_t and
// serialized as plain integer text. These two helpers are the single
// definition of the on-disk authored position form; every producer and
// consumer of an authored position — the marker parsers, the settings trim
// reader, the serializers, and the CLI — goes through exactly this pair, so
// persistence round-trips bit-exactly and there is no second representation
// to disagree with the walls. Fractional, negative, non-finite, or otherwise
// malformed position text is a state the GUI can never write — adversarial,
// load-fatal at every consumer. In-memory storage is int64_t throughout, so
// a fractional authored position is unrepresentable at rest; every
// exact-compare wall and validator is a plain integer compare. Timestamps
// (MM:SS.mmm) are display-only renderings; see time_format.h.
//
// RENDER-DISPLAY domain (format_render_frame_double /
// parse_render_frame_double): positions in the .renderwarpmarkers /
// .renderphaseresetmarkers display sidecars live on the render's own target
// axis and are generically fractional — a marker re-timed through the warp
// map rarely lands on a whole target frame — so they keep the fractional
// double serialization, sibling of the map artifacts' fractional domain,
// NOT the authored grammar.

// Authored frame -> plain integer text ("0", "44100"). Routing through the
// integer to_chars pins the spelling to plain digits — no scientific form,
// so 600000 prints as "600000", never "6e+05".
inline std::string format_authored_frame(int64_t frame) {
    char buf[32];
    auto res = std::to_chars(buf, buf + sizeof(buf),
                             static_cast<long long>(frame));
    return std::string(buf, res.ptr);
}

// Text -> authored frame (int64_t). Strict: the whole field must be
// consumed, and the value must be finite, non-negative, and a whole frame.
// Rejecting fractional values alongside NaN, inf, negative, empty, and
// trailing junk is the parser's malformed-position rule — adversarial,
// load-fatal, the same category as a malformed timestamp used to be. The
// restriction is on the VALUE, not the spelling: integer-valued forms such
// as "44100.0" or the historical scientific "6e+05" still parse (and
// re-serialize as plain integer text) — the field is decoded as a double
// and converted after the whole-frame check, which is exact far below 2^53.
// Returns true and sets `out` on success; returns false and leaves `out`
// untouched on failure.
inline bool parse_authored_frame(std::string_view s, int64_t& out) {
    if (s.empty()) return false;
    // A leading '-' is rejected up front: "-0" would otherwise parse to the
    // negative-zero double, which compares equal to 0.0 and would slip past
    // the non-negative check.
    if (s.front() == '-') return false;
    double v = 0.0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{}) return false;
    if (res.ptr != s.data() + s.size()) return false;
    if (!std::isfinite(v) || v < 0.0) return false;
    if (v != std::floor(v)) return false;
    out = static_cast<int64_t>(v);
    return true;
}

// Render-display frame double -> shortest round-trip decimal text
// (std::to_chars general form). An integer-valued frame serializes without
// a fractional part ("0", "44100"); fractional frames keep exactly the
// digits needed to reload the identical double.
inline std::string format_render_frame_double(double frame) {
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), frame);
    return std::string(buf, res.ptr);
}

// Text -> render-display frame double. Same whole-field / finite /
// non-negative strictness as the authored parse, but fractions are legal —
// the render-display readers are lenient per line, and a fractional target
// position is this domain's normal shape, not a defect.
inline bool parse_render_frame_double(std::string_view s, double& out) {
    if (s.empty()) return false;
    // Same "-0" rationale as the authored parse above.
    if (s.front() == '-') return false;
    double v = 0.0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{}) return false;
    if (res.ptr != s.data() + s.size()) return false;
    if (!std::isfinite(v) || v < 0.0) return false;
    out = v;
    return true;
}
