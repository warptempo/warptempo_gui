#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// THE MARKER MEASURE — one grammar, one split, one validator.
// (architect approval 2026-08-20 — the frozen reopen this header carries; it
// succeeds the deleted marker_comment.h, written under the 2026-08-19 grant.)
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
// REFERENCE for the score-video jump, so it returns to the ASCII-grammar
// class that every other structural grammar in the product sits in — one
// canonical spelling per value, the frame_format.h discipline. The retired
// class was: 1..99 bytes, well-formed UTF-8, no control byte, no DEL. It has
// no successor reading and no migration path: no ` //` suffix was ever
// written into the project history, so the tightening strands nothing.
//
// This header is header-only and shared by both binaries — the frame_format.h
// precedent — so the split, the grammar and the canonical spelling have
// exactly one home. THE CONSUMERS, re-derived by grep 2026-08-20 rather than
// edited in place (COMMENT ONLY, architect approval 2026-08-20 — the same
// same-day grant the grammar block below carries; no code in this file's
// consumers changed for it):
//   * THE TWO FILE PARSERS — warpmarkers_parse.cpp,
//     phaseresetmarkers_parse.cpp: split, then validate.
//   * THE GITHUB RECHECK'S TWO DELTA EXTRACTORS — history_diff.cpp: the same
//     pair. The revert arms reconstitute through the parser's own per-line
//     entry point and so consume this header only through it.
//   * THE MEASURE EDITOR — flag_editor.cpp validates at the commit, and
//     text_editor.h takes its character cap from kMaxMarkerMeasureBytes rather
//     than re-spelling a number.
//   * THE MEASURE PROPAGATE — input_key_dispatch.cpp: it PARSES each clipboard
//     measure, shifts the printed number against kMeasureMaxWhole, and
//     re-spells through format_marker_measure, which is what keeps one
//     spelling on disk and rides the section through untouched.
//   * THE SCORE-VIDEO ACT — score_video.cpp: it parses down a '+' chain for
//     the resolution walk, and reads kMeasureMaxSection when judging a map
//     anchor's qualifier, so the map format and this grammar cannot drift.
// None of them mirrors the split, the grammar or the spelling.
//
// ------------------------------------------------------------------------
// THE GRAMMAR — ASCII only, two forms, one canonical spelling per value.
// (SECOND FROZEN REOPEN, architect approval 2026-08-20: the SECTION QUALIFIER
// below, landed with the printed-number ruling. Every helper this grant
// changed records it at its own site.)
//
// WHAT A MEASURE NAMES, decided 2026-08-20 and the reason the qualifier exists
// at all: it is THE NUMBER THE PAGE PRINTS, never a continuous count derived
// by cross-referencing an edition. A movement whose printed numbering RESTARTS
// mid-way — the K.550 menuetto's trio going back to 1 — would otherwise have
// two bars called `12` and no way to say which. The SECTION is that
// disambiguator: 1 for the movement's opening numbering and +1 at each printed
// restart IN VIDEO ORDER, which is exactly what the map's own sections are
// (tools/extract_sheet_map.py emits the same qualifier on its anchors, so a
// marker's measure and a map line read alike).
//
//   DIRECT   <M>  or  <M> <n>/<d>, either optionally prefixed <S>:
//     M is a decimal integer in [1, kMeasureMaxWhole], no leading zeros, no
//     sign. The optional fraction follows exactly ONE space: n/d with
//     1 <= n < d <= kMeasureMaxDenominator, neither carrying leading zeros,
//     and gcd(n, d) == 1 — so `12 4/8` is REFUSED and its one spelling is
//     `12 1/2`. Meaning: measure M, n/d of the way through it. `10` is the
//     downbeat of measure 10; `12 7/8` is seven eighths through measure 12.
//
//     THE SECTION QUALIFIER is `<S>:` immediately before that spelling, with
//     no space anywhere in it: `2:12`, `2:12 7/8`. S is a decimal integer in
//     [2, kMeasureMaxSection], no leading zeros. A BARE SPELLING IS SECTION 1
//     AND IS ITS ONLY SPELLING — `1:12` is REFUSED as non-canonical, the
//     one-spelling-per-value rule this whole grammar is built on, and `0:` is
//     refused with it (there is no section 0). Almost every piece has one
//     section and spells nothing.
//
//   OFFSET   +<W>  or  +<n>/<d>  or  +<W> <n>/<d>
//     No space after the '+'. W obeys the M rules (so `+0` is refused —
//     offsets are strictly positive), the fraction obeys the fraction rules,
//     and a sub-measure offset's one spelling is the BARE fraction (`+1/2`,
//     never `+0 1/2`). Meaning: this marker's measure is its predecessor's
//     resolved measure plus the offset, in exact rational arithmetic.
//
//     AN OFFSET CARRIES NO SECTION AND CANNOT BE GIVEN ONE (`+2:1` is
//     refused). It is a distance, not a place, and it takes the section of
//     whatever it is measured from — the never-crosses-sections ruling below.
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
// A RESOLVED MEASURE IS A (SECTION, RATIONAL) PAIR since 2026-08-20 — the
// printed number plus its fraction, and the section they are printed in. Both
// halves are needed to name a place in a score whose numbering restarts, and
// consumers must carry both (score_video.h's act interpolates WITHIN a
// section's own anchors for exactly this reason).
//
// A '+' measure resolves against the IMMEDIATE PREDECESSOR marker in the
// SAME column, and only that one — there is no fallback scan to an earlier
// marker. It adds to the predecessor's RESOLVED measure, so CHAINS RESOLVE:
// `12`, `+1`, `+1` resolves the third marker to measure 14, and `12 7/8`
// followed by `+1/4` resolves to `13 1/8`. A chain must bottom out in a
// direct measure; a BROKEN LINK — a predecessor carrying no measure, or a
// predecessor that is itself unresolvable — leaves this marker UNRESOLVED.
//
// AN OFFSET NEVER CROSSES A SECTION (architect 2026-08-20). A chain's section
// is its DIRECT ANCHOR'S, carried forward unchanged through every '+' on top
// of it: `2:12` followed by `+1` resolves to section 2, measure 13, and there
// is no arithmetic anywhere that could carry a number out of one section and
// into the next. That is a RULING about what the field means rather than a
// limitation — a section is a fresh printed numbering, not a continuation, so
// "one bar past the end of section 1" is not a place the score has a name for.
// To address the next section, spell a direct measure in it.
//
// AN UNRESOLVED '+' IS STILL VALID. It commits, saves, loads and paints;
// resolution gates the CONSUMERS alone (the score-video act no-ops silently
// on an unresolved measure). GRAMMAR IS VALIDITY; RESOLUTION IS NOT — the
// load-lenient, act-strict reading, so re-ordering markers can never make a
// file refuse to load.
//
// This is a DIFFERENT AXIS from the label cascade: it runs predecessor to
// successor down the store, never definition to ref (warpmarkers.h states
// that separation at the no-cascade-resolver ruling).

// The vocabulary clamps (the absurd-value rule): a measure number and an
// offset's whole part both bracket at 99999, a fraction's denominator at 99.
inline constexpr int64_t kMeasureMaxWhole       = 99999;
inline constexpr int64_t kMeasureMaxDenominator = 99;

// THE SECTION BRACKET (architect approval 2026-08-20, this header's second
// frozen reopen). The QUALIFIER spells 2..99; section 1 is the bare spelling
// and has no qualifier at all, so the written domain starts at 2 while the
// VALUE domain is [1, 99]. Ninety-nine printed restarts in one movement is far
// past anything a score does — the K.550 menuetto, the case this exists for,
// has two — and the two-digit cap is what keeps the byte bound tight.
inline constexpr int64_t kMeasureMinSection = 2;
inline constexpr int64_t kMeasureMaxSection = 99;

// Maximum measure length in BYTES, shared by both binaries. The grammar is
// ASCII, so bytes and characters agree. The longest canonical token is the
// full qualified direct form `99:99999 98/99` at 14 bytes — 2 + 1 for the
// qualifier over the 11-byte `99999 98/99` — which overtook the longest OFFSET
// (`+99999 98/99`, 12) when the qualifier landed 2026-08-20. Nothing longer
// can be spelled, so this is a tight bound rather than a policy cap.
inline constexpr size_t kMaxMarkerMeasureBytes = 14;

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
//
// `section` (2026-08-20, under the grant) is meaningful on DIRECT forms only
// and rests at 1, which is both the default and the value the bare spelling
// carries — so a reader that never heard of sections sees exactly the old
// meaning. It is left at 1 on an offset and MUST NOT be read there: an offset
// takes its section from what it resolves against, never from itself.
struct MarkerMeasureValue {
    bool    is_offset = false;
    int64_t section   = 1;
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

    // THE SECTION QUALIFIER (architect approval 2026-08-20), taken off the
    // FRONT before the forms below see anything, which is what keeps their
    // arithmetic untouched by the grant: past this block `body` is exactly the
    // token the grammar has always parsed.
    //
    // IT IS DIRECT-ONLY, and the refusal is spelled rather than left to fall
    // through the number readers: an offset carrying `:` would otherwise fail
    // with "measure offset must be 1..99999", which names the wrong problem in
    // a field the editor red-flashes on.
    const size_t colon = body.find(':');
    if (colon != std::string_view::npos) {
        if (v.is_offset) {
            error_out = "measure offset must not name a section";
            return false;
        }
        int64_t section = 0;
        if (!marker_measure_detail::parse_canonical_uint(
                body.substr(0, colon), kMeasureMaxSection, 2, section)) {
            error_out = "measure section must be " +
                        std::to_string(kMeasureMinSection) + ".." +
                        std::to_string(kMeasureMaxSection);
            return false;
        }
        // SECTION 1 HAS ONE SPELLING AND IT IS THE BARE ONE. `1:` parses as a
        // number perfectly well and is refused HERE, on the canonical-spelling
        // rule the whole grammar rests on, not on the bracket.
        if (section < kMeasureMinSection) {
            error_out = "section 1 is spelled without a qualifier";
            return false;
        }
        v.section = section;
        body.remove_prefix(colon + 1);
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
//
// THE SECTION IS EMITTED HERE (2026-08-20, under the grant) and only above 1,
// the bare spelling being section 1's one canonical form. That single rule is
// also what makes the PROPAGATE section-safe for free: it shifts `whole` and
// re-spells, so the section rides through in the struct without the paste ever
// naming it.
inline std::string format_marker_measure(const MarkerMeasureValue& v) {
    std::string out;
    if (v.is_offset) out += '+';
    if (!v.is_offset && v.section >= kMeasureMinSection) {
        out += std::to_string(v.section);
        out += ':';
    }
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
