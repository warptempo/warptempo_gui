#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

// Serialization of authored VALUES (tempo, scale, bpm bracket bounds, the
// global settings scale). Scale, bpm bounds, and the settings scale are
// full doubles; format_value_double / parse_value_double below are the
// single definition of their on-disk and on-screen number form, the
// value-domain sibling of frame_format.h's position pair. Convention
// across the codebase: tempo-like values print with min_decimals 2,
// scale-like values with min_decimals 4, bpm values with min_decimals 0
// (plain shortest). Tempo is the exception: it is 100-based INTEGER CENTS
// held in int64_t everywhere at rest, and the format_tempo_cents /
// parse_tempo_cents pair below is its single serialization owner — the
// exact tempo sibling of frame_format.h's authored pair. The N.NN spelling
// is the text interface only; no scale-style full-double input latitude
// exists on the tempo field.

// Authored-value brackets, the single definition of the legal value
// vocabulary. Tempo and scale carry separate brackets. Tempo (marker
// tempo, sweep-derived base tempo) spans the multiplicatively symmetric
// [0.25, 4.00] (4 = 1/0.25), held as integer cents [25, 400] so every
// bracket comparison — adversarial load-fatal, editor red-flash, the
// Ctrl+wheel constructive clamp, the bpm derivation's refusal, the
// iter-bracket commit gate — is an exact integer compare. Scale — both the
// per-marker tempo scale and the global settings scale — is tighter:
// [0.50, 2.0000] (2 = 1/0.5), multiplicatively symmetric around 1, because
// realistic scale trims sit near 1 (roughly 0.8-1.2). Scale's floor,
// together with tempo's floor, bounds the resolved tempo-scale product
// below by 0.25 * 0.5 * 0.5 = 1/16 — the bound the target-view whole-frame
// nudge guarantee is computed from. Absurd magnitudes (1e307 tempos, 2^53
// bpm bounds) are adversarial, not use cases: every GUI input surface
// enforces these bounds — editor red-flash, constructive wheel clamp,
// derivation refusal — so an out-of-bracket value on disk is a state the
// GUI can never produce, and it hard-fails the load (stderr, first error
// only).
inline constexpr int64_t kTempoMinCents = 25;  // marker tempo, derived base tempo
inline constexpr int64_t kTempoMaxCents = 400;
inline constexpr double kScaleMin = 0.5;    // marker scale, settings scale
inline constexpr double kScaleMax = 2.0;
inline constexpr double kBpmMin   = 10.0;   // bpm bracket bounds
inline constexpr double kBpmMax   = 400.0;
inline constexpr int    kBpmBeatsMax  = 9999; // beats stays a positive int, capped
// Iteration deltas live in [-4.00, +4.00], i.e. [-400, +400] integer cents
// (session-only, never serialized).
inline constexpr int64_t kIterDeltaMaxCents = 400;

// Integer tempo cents -> the tempo double. The ONE cents-to-double
// boundary: the authored tempo domain is integer cents by type, and a
// double tempo exists only past this helper — at the DSP slope product
// (build_warp_frame_map's effective_tempo), the fingerprint's f64 encoding,
// the bpm derivation's scale division, and the label-ref hover multiplier.
// IEEE division is correctly rounded, so cents / 100.0 IS the double
// nearest the exact centesimal value — bit-identical to what
// strtod/from_chars produced for the same value's N.NN text, which is what
// keeps renders and fingerprints byte-stable across the integer-cents
// representation. The N.NN closure is structural now: an off-grid tempo is
// unrepresentable by type, so no producer can leave the grid.
inline double tempo_from_cents(int64_t cents) {
    return static_cast<double>(cents) / 100.0;
}
// No re-entry from the double domain: cents arithmetic stays integer.
double tempo_from_cents(double) = delete;

// Serialization of authored TEMPO — one domain, one pair, the exact
// sibling of frame_format.h's authored position pair. The on-disk spelling
// IS the grammar: exactly the N.NN text format_tempo_cents writes, or
// nothing.

// Tempo cents -> the exact N.NN text: integer part cents/100, dot,
// cents%100 zero-padded to two digits. For every bracket value this emits
// byte-identical text to the historical min-2-padded shortest-round-trip
// writer ("1" -> "1.00", "1.3" -> "1.30"), so existing sidecars re-serialize
// byte-for-byte. Authored tempo is bracket-positive, but the iteration
// sweep's per-cell computed mutations are deliberately unbracketed and can
// go non-positive, so the negative arm prints a leading '-' ("-3.75") —
// text the strict parse then refuses on load, exactly like the historical
// double writer's output for such a cell. Signed session-only deltas have
// their own explicit-sign formatter (format_signed_delta_cents,
// warpmarkers.h).
inline std::string format_tempo_cents(int64_t cents) {
    std::string s;
    uint64_t a;
    if (cents < 0) {
        s += '-';
        a = static_cast<uint64_t>(-(cents + 1)) + 1;  // INT64_MIN-safe
    } else {
        a = static_cast<uint64_t>(cents);
    }
    s += std::to_string(a / 100);
    s += '.';
    const uint64_t frac = a % 100;
    s += static_cast<char>('0' + frac / 10);
    s += static_cast<char>('0' + frac % 10);
    return s;
}

// Text -> tempo cents. Strict: exactly the N.NN spelling — one or more
// integer digits, a single dot, exactly two fraction digits, no sign, no
// exponent, no leading zero unless the integer part is exactly "0" —
// followed by direct digit-to-cents conversion (no strtod, no doubles). A
// digit run whose value would overflow int64 is refused, not wrapped
// (unreachable from in-bracket values; adversarial input earns the same
// spelling refusal). Returns true and sets `out` on success; returns false
// and leaves `out` untouched on failure. Range/bracket checks are the
// caller's (warpmarkers_parse.cpp applies [kTempoMinCents, kTempoMaxCents]
// with its own diagnostics).
inline bool parse_tempo_cents(std::string_view s, int64_t& out) {
    const size_t dot = s.find('.');
    if (dot == std::string_view::npos) return false;              // dot required
    if (s.find('.', dot + 1) != std::string_view::npos) return false; // exactly one dot
    const std::string_view int_part  = s.substr(0, dot);
    const std::string_view frac_part = s.substr(dot + 1);
    if (int_part.empty()) return false;                 // digit before the dot
    if (frac_part.size() != 2) return false;            // exactly two fraction digits
    auto all_digits = [](std::string_view p) {
        for (char c : p)
            if (c < '0' || c > '9') return false;
        return true;
    };
    if (!all_digits(int_part) || !all_digits(frac_part)) return false;
    // No leading zero unless the integer part is exactly "0".
    if (int_part.size() > 1 && int_part.front() == '0') return false;
    constexpr int64_t kMax = std::numeric_limits<int64_t>::max();
    int64_t whole = 0;
    for (char c : int_part) {
        if (whole > (kMax - 9) / 10) return false;      // overflow refused
        whole = whole * 10 + (c - '0');
    }
    if (whole > (kMax - 99) / 100) return false;        // overflow refused
    out = whole * 100 + (frac_part[0] - '0') * 10 + (frac_part[1] - '0');
    return true;
}

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

// Text -> value double. Strict, the same pattern as parse_authored_frame:
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
    // Values are lowercase-only spellings: reject any ASCII uppercase byte
    // (the practical effect is refusing an uppercase exponent like "1E0";
    // lowercase "1e0" stays legal, and the writers never emit exponents).
    for (char c : s)
        if (c >= 'A' && c <= 'Z') return false;
    double v = 0.0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{}) return false;
    if (res.ptr != s.data() + s.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}
