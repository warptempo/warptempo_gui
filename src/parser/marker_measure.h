#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// THE MARKER MEASURE — one grammar, one split, one validator.
// (architect approval 2026-08-20 — the frozen reopen this header carries; it
// succeeds the deleted marker_comment.h, written under the 2026-08-19 grant.
// THIRD FROZEN REOPEN, architect approval 2026-08-21: the SECTION QUALIFIER
// retired with the score-video sunset — the retirement record is at the
// grammar block below.)
//
// Every warp and phase-reset marker line may carry a MEASURE REFERENCE,
// appended to the otherwise whitespace-free canonical line as:
//
//     <canonical line><space>//<measure>
//
// The separator is UNCHANGED from the comment field this succeeds: the FIRST
// occurrence of " //" on the line — the canonical prefix cannot contain a
// space, so no earlier candidate exists, and everything past it is the
// measure token. The canonical prefix keeps its byte-exact discipline.
//
// THE FREE-UTF-8 BYTE CLASS IS RETIRED (architect 2026-08-20, one day after
// it landed on 2026-08-19). The field was free text for exactly one day and
// was the product's first non-ASCII painted surface; it is now a MEASURE
// REFERENCE, so it sits in the ASCII-grammar
// class that every other structural grammar in the product sits in — one
// canonical spelling per value, the frame_format.h discipline. The retired
// class was: 1..99 bytes, well-formed UTF-8, no control byte, no DEL. It has
// no successor reading and no migration path: no ` //` suffix was ever
// written into the project history, so the tightening strands nothing.
//
// This header is header-only and shared by both binaries — the frame_format.h
// precedent — so the split, the grammar and the canonical spelling have
// exactly one home. THE CONSUMERS, re-derived by grep 2026-08-21 at the
// sunset (the score-video act left the list with its file):
//   * THE TWO FILE PARSERS — warpmarkers_parse.cpp,
//     phaseresetmarkers_parse.cpp: split, then validate.
//   * THE GITHUB RECHECK'S TWO DELTA EXTRACTORS — history_diff.cpp: the same
//     pair. The revert arms reconstitute through the parser's own per-line
//     entry point and so consume this header only through it.
//   * THE MEASURE EDITOR — flag_editor.cpp validates at the commit, and
//     text_editor.h takes its character cap from kMaxMarkerMeasureBytes rather
//     than re-spelling a number.
//   * THE MEASURE PROPAGATE — input_key_dispatch.cpp: it PARSES each clipboard
//     measure, shifts the measure number against kMeasureMaxWhole, and
//     re-spells through format_marker_measure, which is what keeps one
//     spelling on disk.
// None of them mirrors the split, the grammar or the spelling.
//
// ------------------------------------------------------------------------
// THE GRAMMAR — ASCII only, two forms, one canonical spelling per value.
//
// THE SECTION QUALIFIER IS RETIRED (THIRD FROZEN REOPEN, architect approval
// 2026-08-21, the score-video sunset). From 2026-08-20 to 2026-08-21 a direct
// form could carry `<S>:` — `2:12` — to disambiguate a movement whose printed
// numbering restarts (the K.550 menuetto's trio going back to 1); it existed
// for the score-video jump's map lookup, which left the product whole. The
// architect's ruling, recorded here where the section ruling stood: "the
// context will already be a clue" — a repeat or a restart is disambiguated by
// where the marker sits, so the number alone serves. `2:12` is now refused
// like any bad token, and a resolved measure is the plain rational again.
//
//   DIRECT   <M>  or  <M> <n>/<d>
//     M is a decimal integer in [1, kMeasureMaxWhole], no leading zeros, no
//     sign. The optional fraction follows exactly ONE space: n/d with
//     1 <= n < d <= kMeasureMaxDenominator, neither carrying leading zeros,
//     and gcd(n, d) == 1 — so `12 4/8` is REFUSED and its one spelling is
//     `12 1/2`. Meaning: measure M, n/d of the way through it. `10` is the
//     downbeat of measure 10; `12 7/8` is seven eighths through measure 12.
//
//   OFFSET   +<W>  or  +<n>/<d>  or  +<W> <n>/<d>
//     No space after the '+'. W obeys the M rules (so `+0` is refused —
//     offsets are strictly positive), the fraction obeys the fraction rules,
//     and a sub-measure offset's one spelling is the BARE fraction (`+1/2`,
//     never `+0 1/2`). Meaning: this marker's measure is its predecessor's
//     resolved measure plus the offset, in exact rational arithmetic.
//
// Anything else is refused: at the measure editor's commit by red flash, at
// load as ADVERSARIAL (load-fatal, first error only, identically in both
// binaries). The two-category rule holds exactly — a measure that commits in
// the editor loads back, and every refusal here names a state the GUI can
// never produce. The writer emits no suffix for an empty field, so the bare
// ` //` separator can never be written and an empty tail is load-fatal.
//
// THE CRLF TRIPWIRE SURVIVES: no grammar byte here is whitespace other than
// the single fraction space, so a `\r` reaching this validator is refused
// like any other stray byte, and a file that made a round trip through a
// CRLF-writing tool still fails loudly.
//
// ------------------------------------------------------------------------
// '+' RESOLUTION SEMANTICS — stated once, here, the authoritative site.
//
// A RESOLVED MEASURE IS A RATIONAL — the measure number plus its fraction.
// (It was a (section, rational) PAIR from 2026-08-20 to the 2026-08-21
// sunset, under the retired qualifier above.)
//
// A '+' measure resolves against the IMMEDIATE PREDECESSOR marker in the
// SAME column, and only that one — there is no fallback scan to an earlier
// marker. It adds to the predecessor's RESOLVED measure, so CHAINS RESOLVE:
// `12`, `+1`, `+1` resolves the third marker to measure 14, and `12 7/8`
// followed by `+1/4` resolves to `13 1/8`. A chain must bottom out in a
// direct measure; a BROKEN LINK — a predecessor carrying no measure, or a
// predecessor that is itself unresolvable — leaves this marker UNRESOLVED.
//
// AN UNRESOLVED '+' IS STILL VALID. It commits, saves, loads and paints;
// resolution gates the CONSUMERS alone. GRAMMAR IS VALIDITY; RESOLUTION IS
// NOT — the load-lenient, act-strict reading, so re-ordering markers can
// never make a file refuse to load.
//
// This is a DIFFERENT AXIS from the label cascade: it runs predecessor to
// successor down the store, never definition to ref (warpmarkers.h states
// that separation at the no-cascade-resolver ruling).

// The vocabulary clamps (the absurd-value rule): a measure number and an
// offset's whole part both bracket at 99999, a fraction's denominator at 99.
inline constexpr int64_t kMeasureMaxWhole       = 99999;
inline constexpr int64_t kMeasureMaxDenominator = 99;

// (kMeasureMinSection / kMeasureMaxSection stood here from 2026-08-20 to the
// 2026-08-21 sunset, with the retired section qualifier — architect approval
// 2026-08-21.)

// Maximum measure length in BYTES, shared by both binaries. The grammar is
// ASCII, so bytes and characters agree. The longest canonical token is the
// full OFFSET form `+99999 98/99` at 12 bytes — 1 for the sign over the
// 11-byte `99999 98/99`, which is itself the longest DIRECT form. (The
// retired section qualifier briefly made a 14-byte `99:99999 98/99` the
// widest; the cap re-derived down with the 2026-08-21 sunset, architect
// approval the same day.) Nothing longer can be spelled, so this is a tight
// bound rather than a policy cap.
inline constexpr size_t kMaxMarkerMeasureBytes = 12;

// The result of splitting one raw marker line. `prefix` is the canonical
// line the position/payload parsers see; `measure` is the raw measure bytes
// (unvalidated — validate_marker_measure below is the judge); `had_measure`
// distinguishes "no separator on the line" from "separator with an empty
// tail", which is a distinction the load rules need: an EMPTY measure is
// load-fatal, because the writer never emits the bare ` //` suffix and the
// editor's empty commit REMOVES the measure instead of storing one.
//
// The views alias the caller's buffer; they are valid only as long as it is.
struct MarkerMeasureSplit {
    std::string_view prefix;
    std::string_view measure;
    bool             had_measure = false;
};

inline MarkerMeasureSplit split_marker_measure(std::string_view line) {
    MarkerMeasureSplit out;
    const size_t sep = line.find(" //");
    if (sep == std::string_view::npos) {
        out.prefix = line;
        return out;
    }
    out.prefix      = line.substr(0, sep);
    out.measure     = line.substr(sep + 3);
    out.had_measure = true;
    return out;
}

// A parsed measure token. `is_offset` selects the form; `whole` is the
// measure number (direct) or the offset's whole part, ZERO meaning the bare
// fraction form that only an offset may take; `num` is zero when no fraction
// is present, and `den` is then 1. Together the fields spell exactly one
// token, which format_marker_measure below reproduces byte for byte.
// (A `section` field rode here from 2026-08-20 to the 2026-08-21 sunset,
// with the retired qualifier — architect approval 2026-08-21.)
struct MarkerMeasureValue {
    bool    is_offset = false;
    int64_t whole     = 0;
    int64_t num       = 0;
    int64_t den       = 1;
};

namespace marker_measure_detail {

// One canonical unsigned decimal integer: digits only, no leading zeros, in
// [1, max]. `max_digits` bounds the slice before any arithmetic, so nothing
// here can overflow.
inline bool parse_canonical_uint(std::string_view s, int64_t max,
                                 size_t max_digits, int64_t& out) {
    if (s.empty() || s.size() > max_digits) return false;
    if (s.size() > 1 && s[0] == '0') return false;
    int64_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    if (v < 1 || v > max) return false;
    out = v;
    return true;
}

inline int64_t gcd_i64(int64_t a, int64_t b) {
    while (b != 0) {
        const int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

// `<n>/<d>` with 1 <= n < d <= kMeasureMaxDenominator, no leading zeros,
// gcd(n, d) == 1 — the reduced form is the only spelling.
inline bool parse_fraction(std::string_view s, int64_t& num, int64_t& den,
                           std::string& error_out) {
    const size_t bar = s.find('/');
    if (bar == std::string_view::npos) {
        error_out = "malformed measure reference";
        return false;
    }
    if (!parse_canonical_uint(s.substr(0, bar), kMeasureMaxDenominator, 2,
                              num) ||
        !parse_canonical_uint(s.substr(bar + 1), kMeasureMaxDenominator, 2,
                              den)) {
        error_out = "measure fraction out of range (n/d, 1 <= n < d <= " +
                    std::to_string(kMeasureMaxDenominator) + ")";
        return false;
    }
    if (num >= den) {
        error_out = "measure fraction must be proper (n < d)";
        return false;
    }
    if (gcd_i64(num, den) != 1) {
        error_out = "measure fraction must be reduced";
        return false;
    }
    return true;
}

}  // namespace marker_measure_detail

// Parse one measure token into its fields. Returns true on success; on
// failure returns false and sets `error_out` to a one-line diagnostic in the
// readers' voice. This is the grammar's one implementation — the validator,
// the editor's commit and the measure propagate's offset arithmetic all
// enter here, so there is no second reading of the token anywhere.
// (The section-qualifier block that took `<S>:` off the front of a direct
// form retired with the 2026-08-21 sunset — architect approval 2026-08-21; a
// `:` anywhere in the token now falls through the number readers and is
// refused like any other stray byte.)
inline bool parse_marker_measure(std::string_view text,
                                 MarkerMeasureValue& out,
                                 std::string&        error_out) {
    if (text.empty()) {
        error_out = "empty measure after ' //'";
        return false;
    }
    if (text.size() > kMaxMarkerMeasureBytes) {
        error_out = "measure must be at most " +
                    std::to_string(kMaxMarkerMeasureBytes) + " bytes";
        return false;
    }

    MarkerMeasureValue v;
    std::string_view   body = text;
    if (body.front() == '+') {
        v.is_offset = true;
        body.remove_prefix(1);
        if (body.empty()) {
            error_out = "malformed measure reference";
            return false;
        }
    }

    const size_t space = body.find(' ');
    if (space == std::string_view::npos) {
        // One token: a whole number in either form, or — offsets only — a
        // bare fraction, which is the one spelling of a sub-measure offset.
        if (body.find('/') != std::string_view::npos) {
            if (!v.is_offset) {
                error_out = "measure must name a measure number";
                return false;
            }
            if (!marker_measure_detail::parse_fraction(body, v.num, v.den,
                                                       error_out))
                return false;
            v.whole = 0;
        } else {
            if (!marker_measure_detail::parse_canonical_uint(
                    body, kMeasureMaxWhole, 5, v.whole)) {
                error_out = v.is_offset
                                ? "measure offset must be 1.." +
                                      std::to_string(kMeasureMaxWhole)
                                : "measure number must be 1.." +
                                      std::to_string(kMeasureMaxWhole);
                return false;
            }
            v.num = 0;
            v.den = 1;
        }
    } else {
        // Whole part, exactly one space, fraction.
        if (!marker_measure_detail::parse_canonical_uint(
                body.substr(0, space), kMeasureMaxWhole, 5, v.whole)) {
            error_out = v.is_offset ? "measure offset must be 1.." +
                                          std::to_string(kMeasureMaxWhole)
                                    : "measure number must be 1.." +
                                          std::to_string(kMeasureMaxWhole);
            return false;
        }
        if (!marker_measure_detail::parse_fraction(body.substr(space + 1),
                                                   v.num, v.den, error_out))
            return false;
    }

    out = v;
    return true;
}

// The canonical spelling of a parsed value — the writer side of the grammar,
// so a value that round-trips through parse_marker_measure comes back byte
// for byte. The measure propagate re-spells through here after shifting a
// direct measure's whole part, which is what keeps ONE spelling on disk.
// (The section emission retired with the qualifier at the 2026-08-21
// sunset — architect approval 2026-08-21.)
inline std::string format_marker_measure(const MarkerMeasureValue& v) {
    std::string out;
    if (v.is_offset) out += '+';
    if (v.whole > 0) {
        out += std::to_string(v.whole);
        if (v.num > 0) out += ' ';
    }
    if (v.num > 0) {
        out += std::to_string(v.num);
        out += '/';
        out += std::to_string(v.den);
    }
    return out;
}

// THE MEASURE GRAMMAR JUDGE, applied identically at load (both file parsers
// and both history delta extractors) and at the measure editor's commit.
// Every refusal names a state the GUI can never produce, so each is
// adversarial and load-fatal, first error only.
//
// Returns true on success; on failure returns false and sets `error_out` to a
// one-line diagnostic in the readers' voice.
inline bool validate_marker_measure(std::string_view measure,
                                    std::string&     error_out) {
    MarkerMeasureValue parsed;
    return parse_marker_measure(measure, parsed, error_out);
}
