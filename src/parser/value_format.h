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
// exists on the tempo field. A THIRD PAIR sits beside it since 2026-09-18
// (architect approval 2026-09-18): format_deviation_cents /
// parse_deviation_cents, the SIGNED cent value a tempo DEVIATION TERM is
// spelled with, which takes its magnitude from the tempo pair so the product
// spells a centesimal value one way.

// Authored-value brackets, the single definition of the legal value
// vocabulary. Tempo and scale carry separate brackets. Tempo (marker
// tempo, sweep-derived base tempo) spans the multiplicatively symmetric
// [0.25, 4.00] (4 = 1/0.25), held as integer cents [25, 400] so every
// bracket comparison — adversarial load-fatal, the flag editor's typed-tempo
// red flash (its candidate canonical line runs through this same strict
// parse, so the editor and the load share one bracket compare), the bare
// Up/Down cent step's CONSTRUCTIVE CLAMP at adjust_tempo_cents (its group
// arm refusing at the same edge instead), the bpm derivation's refusal
// (compute_base_tempo_scale), the iter-bracket commit gate — is an exact
// integer compare. Scale — both the per-marker tempo scale and the global
// settings scale — is tighter: [0.50, 2.0000] (2 = 1/0.5), multiplicatively
// symmetric around 1, because
// realistic scale trims sit near 1 (roughly 0.8-1.2). Scale's floor,
// together with tempo's floor, bounds the resolved tempo-scale product
// below by 0.25 * 0.5 * 0.5 = 1/16 — the bound the target-view whole-frame
// nudge guarantee is computed from. Absurd magnitudes (1e307 tempos, 2^53
// bpm bounds) are adversarial, not use cases: every GUI input surface
// enforces these bounds — the flag and settings editors' red-flash refusals,
// each routed through the very strict parser the load itself uses (the flag
// payload through parse_single_canonical_line, the settings block through the
// whole-file schema load), the bare Up/Down cent step's constructive clamp,
// and the bpm bracket's own parse plus the bpm derivation's refusal — so an
// out-of-bracket value on disk is a state the GUI can never produce, and it
// hard-fails the load (stderr, first error only).
// COMMENT-ONLY CORRECTION, 2026-08-12 (architect approval 2026-08-12; frozen
// file, comments only, no code bytes changed): both enforcer lists above used
// to name a Ctrl+wheel / "constructive wheel clamp" tempo enforcer. That was
// a FOSSIL of a pointer-wheel tempo gesture deleted long ago — there is no
// tempo wheel anywhere in the product; bare Up/Down is the whole tempo-step
// surface and its clamp lives at GuiWarpMarkersOps::adjust_tempo_cents.
inline constexpr int64_t kTempoMinCents = 25;  // marker tempo, derived base tempo
inline constexpr int64_t kTempoMaxCents = 400;
inline constexpr double kScaleMin = 0.5;    // marker scale, settings scale
inline constexpr double kScaleMax = 2.0;
inline constexpr double kBpmMin   = 10.0;   // bpm bracket bounds
inline constexpr double kBpmMax   = 400.0;
inline constexpr int    kBpmBeatsMax  = 9999; // beats stays a positive int, capped
// Signed cent deltas live in [-4.00, +4.00], i.e. [-400, +400] integer cents.
// TWO DOMAINS SHARE THIS ONE WALL (architect approval 2026-09-18): the
// ITERATION BRACKET's bounds, session-only and never serialized, and a TEMPO
// DEVIATION TERM, which is serialized inside the warp payload
// (parse_deviation_cents below). One magnitude for both because they are the
// same question asked twice — how far from a base one authored step may reach
// — and a term's own spelling is the bracket cell's spelling too.
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
// double writer's output for such a cell. Signed cent values — a tempo
// deviation term, an iteration bracket bound — wear an explicit sign and take
// the magnitude from here through format_deviation_cents below (architect
// approval 2026-09-18; the GUI's format_signed_delta_cents, warpmarkers.h, is
// that pair's face on the session-only bracket).
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

// Serialization of a TEMPO DEVIATION TERM (architect approval 2026-09-18) —
// the third pair in this file and the tempo pair's signed sibling. A warp
// marker's payload may spell its tempo as a base plus a CHAIN of deviations,
// `1.23+0.01-0.02`, so a section's main speed stays visible beside each bit's
// departure from it and the chain reads as the history of the decisions that
// got there. The chain is SPELLING ALONE: the marker's tempo_cents is the
// resolved TOTAL and the base is DERIVED at format time as total minus the
// terms' sum (warpmarkers_parse.h), so `1.23+0.01` and `1.24` are the same
// number everywhere a number is read and no render can see the difference.
//
// THE TERM'S SIGN IS MANDATORY, and it is what makes the grammar unambiguous:
// parse_tempo_cents admits digits and one dot and nothing else, so the first
// '+' or '-' in a tempo field is where the base ends and the chain begins,
// and a label — four bytes `x.yz`, never signed — cannot collide with one.
inline constexpr int kMaxTempoDeviationTerms = 8;  // (architect approval
// 2026-09-18) — his number, and it is a READABILITY bound rather than a
// structural one: the whole chain paints on the flag untruncated, and past
// eight terms a flag stops being a thing a musician reads at a glance. The
// walls that bound the VALUE are the three next to it: the spelled base and
// the resolved total each take the tempo bracket, and each term takes
// ±kIterDeltaMaxCents.

// Signed deviation cents -> the exact text: a mandatory sign then the N.NN
// spelling of the magnitude ("+0.01", "-4.00", "+0.00" for zero). The
// magnitude runs through format_tempo_cents above, so the product has ONE
// spelling of a centesimal value and a term cannot drift from a tempo; the
// negative arm hands it the signed value directly, which already prints its
// own '-' and is INT64_MIN-safe.
//
// ZERO IS `+0.00` AND HAS NO SECOND SPELLING. A term that has come back to
// zero STAYS on the chain — it is history, and dropping it would rewrite the
// record — so the writer must be able to spell it, while `-0.00` is refused
// on load: one value, one spelling, the file-format rule this whole header
// exists for.
inline std::string format_deviation_cents(int64_t cents) {
    if (cents < 0) return format_tempo_cents(cents);
    return "+" + format_tempo_cents(cents);
}

// Text -> deviation cents. THE WHOLE JUDGE of one term, so the load and the
// flag editor (whose commit runs the same canonical-line parse) can never
// disagree about what a term is: exactly one leading '+' or '-', then the
// strict N.NN spelling through parse_tempo_cents — which refuses "1.1",
// "1.100", "1", a leading zero and every scientific form — then the two
// value rules, NO NEGATIVE ZERO ("-0.00": zero has one spelling) and the
// magnitude wall ±kIterDeltaMaxCents. Returns true and sets `out` on
// success; returns false and leaves `out` untouched on failure. The caller
// composes the diagnostic (warpmarkers_parse.cpp names the offending text and
// the window together, since one judge earns one sentence).
inline bool parse_deviation_cents(std::string_view s, int64_t& out) {
    if (s.empty()) return false;
    const char sign = s.front();
    if (sign != '+' && sign != '-') return false;
    int64_t mag = 0;
    if (!parse_tempo_cents(s.substr(1), mag)) return false;
    if (sign == '-' && mag == 0) return false;      // zero has one spelling
    if (mag > kIterDeltaMaxCents) return false;
    out = (sign == '-') ? -mag : mag;
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
    // Values are plain fixed-decimal spellings: reject any ASCII alphabetic
    // byte. Scientific notation adds no precision (a decimal string already
    // parses to the nearest double; "0.1" and "1e-1" are the identical
    // bits), and no writer, current or historical, ever emitted an exponent,
    // so this refuses both exponent spellings ("1E0" / "1e0") and the
    // "inf"/"nan" words in one rule.
    for (char c : s)
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return false;
    double v = 0.0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{}) return false;
    if (res.ptr != s.data() + s.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}
