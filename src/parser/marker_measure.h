#pragma once

#include "marker_magnification.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// THE MARKER MEASURE — one grammar, one split, one validator.
// (architect approval 2026-08-20 — the frozen reopen this header carries; it
// succeeds the deleted marker_comment.h, written under the 2026-08-19 grant.
// THIRD FROZEN REOPEN, architect approval 2026-08-21: the SECTION QUALIFIER
// retired with the score-video sunset — the retirement record is at the
// grammar block below. FOURTH FROZEN REOPEN, architect approval 2026-09-05:
// THE TWO VOCABULARY CEILINGS CAME DOWN to 999 measures and sixteenths, and
// the byte bound re-derived with them — the record is at the constants
// block below. FIFTH FROZEN REOPEN, architect approval 2026-09-14: THE
// SPACES LEFT THE GRAMMAR — the mixed `+<W> <n>/<d>` and the direct fraction
// `<M> <n>/<d>` are refused, the byte bound re-derived again, and the ` //`
// suffix became the `//<measure>,<magnification>` comment, warp lines only.)
//
// Every WARP marker line may carry a MEASURE REFERENCE, as the left half of
// the line's COMMENT (architect approval 2026-09-14: the comment became
// `//<measure>,<magnification>`, and phase resets lost measures whole):
//
//     <canonical line><space>//<measure>,<magnification>
//
// The separator is UNCHANGED from the comment field this succeeds: the FIRST
// occurrence of " //" on the line — the canonical prefix cannot contain a
// space, so no earlier candidate exists, and everything past it is the
// comment. The comment's split, its comma rule and its three shapes are
// stated once at split_marker_comment below; the magnification half's range
// and grammar live in marker_magnification.h. The canonical prefix keeps its
// byte-exact discipline.
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
// exactly one home. THE CONSUMERS, re-derived by grep 2026-09-14:
//   * THE WARP FILE PARSER — warpmarkers_parse.cpp: parse_single_canonical_line
//     splits the comment and validates both halves; every other reader of a
//     warp sidecar line (the GitHub recheck's delta extractor and its
//     per-side cascade pass, and the revert's reconstitution) enters through
//     that per-line entry point and so consumes this header only through it.
//   * THE MEASURE EDITOR — flag_editor.cpp validates at the commit, and
//     text_editor.h takes its character cap from kMaxMarkerMeasureBytes rather
//     than re-spelling a number.
// None of them mirrors the split, the grammar or the spelling. (The measure
// propagate, which parsed, shifted and re-spelled clipboard measures, was
// deleted whole — architect approval 2026-09-14, comment-only touch. The
// phase-reset parser and its delta extractor left the list the same day, with
// the phase-reset measure.)
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
// NO SPACE ANYWHERE (FIFTH FROZEN REOPEN, architect approval 2026-09-14).
// The direct fraction `<M> <n>/<d>` and the mixed offset `+<W> <n>/<d>` are
// DELETED — refused like any stray byte — so every token is one unbroken run.
//
//   DIRECT   <M>
//     M is a decimal integer in [1, kMeasureMaxWhole], no leading zeros, no
//     sign. Meaning: the downbeat of measure M. A direct token carries no
//     fraction.
//
//   OFFSET   +<W>  or  +<n>/<d>
//     No space after the '+'. W obeys the M rules (so `+0` is refused —
//     offsets are strictly positive). The fraction is PROPER and REDUCED:
//     1 <= n <= d-1, d <= kMeasureMaxDenominator, neither carrying leading
//     zeros, and gcd(n, d) == 1 — so `+4/8` is REFUSED and its one spelling
//     is `+1/2`. An offset is a whole number of measures OR a sub-measure
//     fraction, never both. Meaning: this marker's measure is its
//     predecessor's resolved measure plus the offset, in exact rational
//     arithmetic.
//
// Anything else is refused: at the measure editor's commit by red flash, at
// load as ADVERSARIAL (load-fatal, first error only, identically in both
// binaries). The two-category rule holds exactly — a measure that commits in
// the editor loads back, and every refusal here names a state the GUI can
// never produce. A BLANK measure is not a token: it is the empty left half of
// the comment (split_marker_comment below), which the editor's empty commit
// produces and this grammar never reads.
//
// THE CRLF TRIPWIRE SURVIVES: no grammar byte is whitespace — in the measure,
// the comma or the magnification — so a `\r` reaching the comment is refused
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
// `12`, `+1`, `+1` resolves the third marker to measure 14, and `12`,
// `+1/2`, `+3/4` resolves the third to measure 13 1/4 (a rational — the
// resolved value has no spelling of its own on disk). A chain must bottom out
// in a direct measure; a BROKEN LINK — a predecessor carrying no measure, or a
// predecessor that is itself unresolvable — leaves this marker UNRESOLVED.
//
// AN UNRESOLVED '+' IS STILL VALID. It commits, saves, loads and paints;
// resolution gates the CONSUMERS alone. GRAMMAR IS VALIDITY; RESOLUTION IS
// NOT — the load-lenient, act-strict reading, so re-ordering markers can
// never make a file refuse to load.
//
// THE CHAIN RESOLUTION HAS NO CONSUMER TODAY, and "the consumers" above is
// therefore a future tense (recorded 2026-09-02; architect approval
// 2026-09-02, comment-only). This axis was built for the SCORE-VIDEO
// experiment, sunset whole on 2026-08-21 (marker-ui.md carries the record),
// and no code walks a '+' chain to a rational now: the box paints the
// AUTHORED token and the sidecar round-trips bytes (architect approval
// 2026-09-14, comment-only: the measure paste, the one place a measure was
// taken apart, is deleted). The semantics stay stated here because they ARE
// what `+1/4` means to whoever reads a file, and because a consumer is one
// walk away.
//
// This is a DIFFERENT AXIS from the label cascade: it runs predecessor to
// successor down the store, never definition to ref (warpmarkers.h states
// that separation at the no-cascade-resolver ruling).

// The vocabulary clamps: a measure number and an offset's whole part both
// bracket at kMeasureMaxWhole, a fraction's denominator at
// kMeasureMaxDenominator.
//
// THEY ARE THE SCORE'S BOUNDS, NOT ABSURD-VALUE CLAMPS (FOURTH FROZEN
// REOPEN, architect approval 2026-09-05). They stood at 99999 and 99 from
// the field's 2026-08-20 rebrand, chosen under the absurd-value rule alone —
// wide enough that no real number could reach them, which is all a clamp
// against a stray digit run has to be. The architect ruled them down to what
// music actually spells: "999 measures is more than any sheet music; the
// finest fraction I could place a marker on is a sixteenth". The point is
// truthfulness downstream — the byte bound below, and every editor cap
// derived from it, now advertise exactly what the grammar can commit rather
// than room no one can use.
//
// THE TIGHTENING IS THE COST, and it is paid the way every grammar
// tightening in this header is paid: a sidecar carrying `1000`, `+1000` or a
// denominator past 16 (`1 1/17`) becomes ADVERSARIAL and load-fatal in both
// binaries from this date, and the measure editor refuses the same values at
// its commit, through this one judge. No project history carries such a
// token — the ceilings were never reachable in practice — so the tightening
// strands nothing and needs no migration, the 2026-08-20 rebrand's own
// reasoning.
inline constexpr int64_t kMeasureMaxWhole       = 999;
inline constexpr int64_t kMeasureMaxDenominator = 16;

// (kMeasureMinSection / kMeasureMaxSection stood here from 2026-08-20 to the
// 2026-08-21 sunset, with the retired section qualifier — architect approval
// 2026-08-21.)

// Maximum measure length in BYTES, shared by both binaries. The grammar is
// ASCII, so bytes and characters agree. The longest canonical token is the
// FRACTIONAL OFFSET `+15/16` at 6 bytes — the sign over the widest FRACTION,
// which is five bytes: two digits, the bar, two digits, the denominator
// bounded at kMeasureMaxDenominator and the numerator under it, reduced
// (`15/16`, `12/13` and their kin all reach it). The whole-number forms are
// narrower — `999` at three digits under kMeasureMaxWhole, `+999` at four —
// and no form joins a whole part to a fraction any more. Nothing longer can
// be spelled, so this is a tight bound rather than a policy cap — which is
// the whole reason the ceilings above are the score's and not a clamp's.
// (It read 12 for the 99999/99 ceilings, 14 for the one day the retired
// section qualifier made `99:99999 98/99` the widest, and 10 while the spaced
// `+999 15/16` stood; each re-derivation carries its own grant — architect
// approval 2026-08-21, 2026-09-05 and 2026-09-14.)
inline constexpr size_t kMaxMarkerMeasureBytes = 6;

// THE MARKER COMMENT SPLIT — the comment's ONE owner (FIFTH FROZEN REOPEN,
// architect approval 2026-09-14, generalizing the measure-only split that
// stood here). A warp marker line may end in
//
//     <canonical line><space>//<measure>,<magnification>
//
// with the COMMA REQUIRED, so the comment has exactly three shapes: `//12,3`
// (both fields), `//12,` (a measure alone) and `//,3` (a magnification
// alone). The writer emits NO comment when both fields are blank, so `//,` is
// a state the GUI can never produce. THE STRUCTURAL REFUSALS ARE HERE, each
// adversarial and load-fatal: a comment with no comma (today's measure-only
// `//12` included — no migration), a comment with more than one comma, and
// the empty comment `//,`. The two halves' TOKENS are not judged here —
// validate_marker_measure below and parse_marker_magnification
// (marker_magnification.h) are the judges, and a blank half is never handed
// to either.
//
// `prefix` is the canonical line the position/payload parsers see; `measure`
// and `magnification` are the raw halves (either may be empty, never both
// when `had_comment`); `had_comment` distinguishes "no separator on the line"
// from a comment. Returns true on success; on failure returns false and sets
// `error_out` to a one-line diagnostic in the readers' voice.
//
// The views alias the caller's buffer; they are valid only as long as it is.
struct MarkerCommentSplit {
    std::string_view prefix;
    std::string_view measure;
    std::string_view magnification;
    bool             had_comment = false;
};

inline bool split_marker_comment(std::string_view line,
                                 MarkerCommentSplit& out,
                                 std::string&        error_out) {
    out = MarkerCommentSplit{};
    const size_t sep = line.find(" //");
    if (sep == std::string_view::npos) {
        out.prefix = line;
        return true;
    }
    const std::string_view comment = line.substr(sep + 3);
    const size_t comma = comment.find(',');
    if (comma == std::string_view::npos) {
        error_out = "marker comment must be '//<measure>,<magnification>' "
                    "(missing ',')";
        return false;
    }
    if (comment.find(',', comma + 1) != std::string_view::npos) {
        error_out = "marker comment must carry exactly one ','";
        return false;
    }
    if (comment.size() == 1) {
        error_out = "empty marker comment after ' //'";
        return false;
    }
    out.prefix        = line.substr(0, sep);
    out.measure       = comment.substr(0, comma);
    out.magnification = comment.substr(comma + 1);
    out.had_comment   = true;
    return true;
}

// A parsed measure token. `is_offset` selects the form; `whole` is the
// measure number (direct) or the offset's whole part, ZERO meaning the bare
// fraction form that only an offset may take; `num` is zero when no fraction
// is present, and `den` is then 1. A value with BOTH `whole` and `num`
// nonzero is unrepresentable in the grammar since 2026-09-14 — the parse
// never produces one. Together the fields spell exactly one
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
// and the editor's commit both enter here, so there is no second reading of
// the token anywhere (architect approval 2026-09-14, comment-only).
// (The section-qualifier block that took `<S>:` off the front of a direct
// form retired with the 2026-08-21 sunset — architect approval 2026-08-21; a
// `:` anywhere in the token now falls through the number readers and is
// refused like any other stray byte.)
inline bool parse_marker_measure(std::string_view text,
                                 MarkerMeasureValue& out,
                                 std::string&        error_out) {
    if (text.empty()) {
        error_out = "empty measure";
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

    // One unbroken token: a whole number in either form, or — offsets only —
    // a bare fraction. No space anywhere (architect approval 2026-09-14: the
    // whole-plus-fraction branch that read `W n/d` is deleted), so a space is
    // a stray byte and is named as one.
    if (body.find(' ') != std::string_view::npos) {
        error_out = "measure must contain no spaces";
        return false;
    }
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
                body, kMeasureMaxWhole, 3, v.whole)) {
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

    out = v;
    return true;
}

// The canonical spelling of a parsed value — the writer side of the grammar,
// so a value that round-trips through parse_marker_measure comes back byte
// for byte. It has TWO CALLERS (architect approval 2026-09-14, comment-only),
// both spelling the integer that measure_step_landing (the GUI's app_state.h)
// lands on: the measure VALUE STEP (bare Up/Down and the plain wheel over a
// measure box, GuiWarpMarkersOps::adjust_measure_step) and the VALUE DRAG's
// measure arm (ValueDragOps::apply_motion). It is the grammar's writer half,
// the definition of the ONE spelling on disk.
// (The section emission retired with the qualifier at the 2026-08-21
// sunset — architect approval 2026-08-21.)
// (The whole-plus-fraction arm that emitted `W n/d` is deleted with the
// spaced forms — architect approval 2026-09-14; such a value is unreachable.)
inline std::string format_marker_measure(const MarkerMeasureValue& v) {
    assert(!(v.whole > 0 && v.num > 0));
    std::string out;
    if (v.is_offset) out += '+';
    if (v.whole > 0) {
        out += std::to_string(v.whole);
    } else if (v.num > 0) {
        out += std::to_string(v.num);
        out += '/';
        out += std::to_string(v.den);
    }
    return out;
}

// THE MEASURE GRAMMAR JUDGE, applied identically at load (the warp line
// parser, which every warp sidecar reader enters) and at the measure editor's
// commit.
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

// IS THIS MEASURE STEPPABLE — BLANK or a DIRECT integer (architect approval
// 2026-09-14, landed beside the grammar for the measure value step): an
// integer measure steps by whole measures and a blank one steps to measure 1,
// while an OFFSET — whole or fractional — has no absolute number to step. The
// judge is the grammar itself; nothing here re-reads the token.
inline bool measure_is_steppable(std::string_view measure) {
    if (measure.empty()) return true;
    MarkerMeasureValue parsed;
    std::string        error;
    return parse_marker_measure(measure, parsed, error) && !parsed.is_offset;
}
