#pragma once

#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <system_error>

// Serialization of authored VALUES (tempo, scale, bpm bracket bounds, the
// global settings scale). Values are full doubles; this pair is the single
// definition of their on-disk and on-screen number form, the value-domain
// sibling of frame_format.h's position pair. Convention across the
// codebase: tempo-like values print with min_decimals 2, scale-like values
// with min_decimals 4, bpm values with min_decimals 0 (plain shortest).

// Authored-value brackets, the single definition of the legal value
// vocabulary. Normal use is a stretch ratio of roughly 0.60-1.30
// (averaging about 1.10); the bracket widens that band to the
// multiplicatively symmetric [0.25, 4.00] (4 = 1/0.25). Absurd magnitudes
// (1e307 tempos, 2^53 bpm bounds) are adversarial, not use cases: every
// GUI input surface enforces these bounds — editor red-flash, constructive
// wheel clamp, derivation refusal — so an out-of-bracket value on disk is
// a state the GUI can never produce, and it hard-fails the load (stderr,
// first error only).
inline constexpr double kValueMin = 0.25;   // tempo, marker scale, settings scale
inline constexpr double kValueMax = 4.0;
inline constexpr double kBpmMin   = 10.0;   // bpm bracket bounds
inline constexpr double kBpmMax   = 400.0;
inline constexpr int    kBpmBeatsMax  = 9999; // beats stays a positive int, capped
inline constexpr double kIterDeltaMax = 4.0;  // iteration deltas live in [-4.00, +4.00]

// Value double -> shortest round-trip decimal text (std::to_chars general
// form), zero-padded so the fraction carries at least `min_decimals`
// digits (the decimal point is added when absent). Padding keeps the
// familiar authored shapes stable — "1" -> "1.00" at 2, "1.3" -> "1.30",
// while "1.234567" already exceeds the minimum and is untouched — and it
// makes every historical fixed-decimal form re-serialize byte-identically:
// "0.95" parses to the double whose shortest form is "0.95" and pads to
// itself; "1.20" parses to 1.2 and pads back to "1.20". A shortest result
// in exponent form (extreme magnitudes) is used as-is, unpadded.
inline std::string format_value_double(double v, int min_decimals) {
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), v);
    std::string s(buf, res.ptr);
    if (min_decimals <= 0) return s;
    if (s.find('e') != std::string::npos ||
        s.find('E') != std::string::npos) return s;
    const auto dot = s.find('.');
    int frac = 0;
    if (dot == std::string::npos) {
        s += '.';
    } else {
        frac = static_cast<int>(s.size() - dot - 1);
    }
    for (; frac < min_decimals; ++frac) s += '0';
    return s;
}

// Text -> value double. Strict, the same pattern as parse_frame_double:
// the whole field must be consumed and the value must be finite; empty,
// NaN, inf, trailing junk, and a leading '-' are rejected (no authored
// value is negative — grammars that demand strict positivity add their
// own > 0 refusal on top, so a typed zero fails with a pointed message
// rather than a generic parse error). Returns true and sets `out` on
// success; returns false and leaves `out` untouched on failure.
inline bool parse_value_double(std::string_view s, double& out) {
    if (s.empty()) return false;
    // A leading '-' is rejected up front: "-0" would otherwise parse to
    // the negative-zero double, which slips past a > 0 caller check's
    // complement (v <= 0 catches it) only by accident and would
    // round-trip as "-0".
    if (s.front() == '-') return false;
    double v = 0.0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{}) return false;
    if (res.ptr != s.data() + s.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}
