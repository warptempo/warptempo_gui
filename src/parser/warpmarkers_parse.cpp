// Dead includes removed under grant (architect approval 2026-08-02).
#include "warpmarkers_parse.h"

#include "frame_format.h"
#include "parse_text_util.h"
#include "value_format.h"

#include <expected>
#include <fstream>
#include <set>

namespace {

// Label shape is exactly `x.yz`: a lowercase letter, a dot, then two
// lowercase-letter-or-digit characters. ASCII ranges compared directly (no
// locale, no isalpha/isdigit).
bool is_valid_label_format(const std::string& s) {
    auto is_lower = [](char c) { return c >= 'a' && c <= 'z'; };
    auto is_lower_alnum = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    };
    return s.size() == 4 && is_lower(s[0]) && s[1] == '.' &&
           is_lower_alnum(s[2]) && is_lower_alnum(s[3]);
}

// Parse and bracket-check an authored TEMPO field, straight to integer
// cents. The spelling grammar lives once, in parse_tempo_cents
// (value_format.h): exactly the N.NN text — refusing "1.1", "1.100", "1",
// scientific forms, and every other spelling with a grammar-naming error —
// followed by direct digit-to-cents conversion; no strtod, no doubles
// anywhere in the tempo load path. The parsed cents then take the tempo
// bracket [kTempoMinCents, kTempoMaxCents] as an exact integer compare.
// Every GUI input surface enforces the bracket, so an out-of-bracket value
// on disk is a state the GUI can never produce: adversarial, load-fatal,
// first error only. This is a deliberate tempo/scale asymmetry: tempo is
// integer cents pinned to the N.NN spelling, while marker and settings
// scale remain full doubles (min-4 shortest, parse_positive_value below).
bool parse_tempo_field(const std::string& s, int64_t& out,
                       std::string& error_out) {
    int64_t cents = 0;
    if (!parse_tempo_cents(s, cents)) {
        error_out = "tempo must be of the form N.NN: " + s;
        return false;
    }
    // Only "0.00" can land here non-positive (the grammar has no sign);
    // it keeps its pointed positivity message.
    if (cents <= 0) {
        error_out = "tempo must be positive: " + s;
        return false;
    }
    if (cents < kTempoMinCents || cents > kTempoMaxCents) {
        error_out = "tempo must be within [" +
                    format_tempo_cents(kTempoMinCents) + ", " +
                    format_tempo_cents(kTempoMaxCents) + "]: " + s;
        return false;
    }
    out = cents;
    return true;
}

// Parse and wall-check ONE TEMPO DEVIATION TERM (architect approval
// 2026-09-18). The whole verdict — the mandatory sign, the strict N.NN
// magnitude, the no-negative-zero rule and the ±kIterDeltaMaxCents wall —
// belongs to parse_deviation_cents (value_format.h), so the load and the flag
// editor's commit judge a term with one body. ONE JUDGE EARNS ONE SENTENCE:
// unlike parse_tempo_field above, which can say which of the form and the
// bracket it refused on, this names the form and the window together and
// echoes the offending text, which is what a hand-edited chain needs to see.
bool parse_deviation_field(const std::string& s, int64_t& out,
                           std::string& error_out) {
    if (!parse_deviation_cents(s, out)) {
        error_out = "tempo deviation must be a signed N.NN within [" +
                    format_deviation_cents(-kIterDeltaMaxCents) + ", " +
                    format_deviation_cents(kIterDeltaMaxCents) + "]: " + s;
        return false;
    }
    return true;
}

// Parse and range-check an authored SCALE value: full double via
// parse_value_double (whole field consumed, finite, no leading '-') under
// a strict positivity refusal — a typed zero (a canonical spelling like
// "0.0000") parses and gets the pointed positivity message; any other
// refused spelling, negative included, is the plain invalid-value refusal —
// then the scale bracket [kScaleMin, kScaleMax] (value_format.h), then ONE canonical
// spelling: the accepted spelling IS the writer's spelling
// (format_value_double at `canonical_decimals`, min 4 for scale), so "1.2000"
// loads and "1.2" refuses. This is a deliberate tempo/scale asymmetry with
// parse_tempo_field, which pins the exact N.NN spelling through
// parse_tempo_cents.
bool parse_positive_value(const std::string& s, double& out,
                          const char* what, double lo, double hi,
                          int canonical_decimals, std::string& error_out) {
    double v = 0.0;
    if (!parse_value_double(s, v)) {
        error_out = std::string("invalid ") + what + " value: " + s;
        return false;
    }
    if (!(v > 0.0)) {
        error_out = std::string(what) + " must be positive: " + s;
        return false;
    }
    if (v < lo || v > hi) {
        error_out = std::string(what) + " must be within [" +
                    format_value_double(lo, 2) + ", " +
                    format_value_double(hi, 2) + "]: " + s;
        return false;
    }
    if (format_value_double(v, canonical_decimals) != s) {
        error_out = std::string(what) + " must be in canonical spelling: " + s;
        return false;
    }
    out = v;
    return true;
}

// Parse TEMPO[*SCALE] into m's tempo fields (owner form: inherits=false).
// Splits on an optional '*', parses the tempo through parse_tempo_field and
// the optional scale through parse_positive_value, then writes the four
// tempo fields. label_def, if any, is the caller's to attach.
//
// THE TEMPO PART IS A BASE AND A CHAIN (architect approval 2026-09-18): the
// base, then zero to kMaxTempoDeviationTerms signed terms, `1.23+0.01-0.02`.
// The split is unambiguous because parse_tempo_cents admits digits and one
// dot and nothing else, so the FIRST '+' or '-' can only be a term's sign;
// each term then runs from its own sign to the next sign or to the end.
// FOUR WALLS, each with its own sentence: the SPELLED BASE takes the tempo
// bracket (parse_tempo_field, unchanged — its bracket IS the base's wall),
// each TERM takes ±kIterDeltaMaxCents, the COUNT takes the term cap, and the
// RESOLVED TOTAL takes the tempo bracket again. The total is what
// tempo_cents holds; the terms are the spelling beside it, and the base is
// re-derived from the two at format time (warpmarkers_parse.h).
//
// A PASS AND A LABEL REF NEVER REACH THIS BODY: parse_new_payload matches
// them exactly, so `pass+0.01` and `a.aa+0.01` arrive here as numeric
// payloads and are refused as the malformed bases they are — the chain needs
// no rule of its own to keep them plain (architect 2026-09-19: "a pass is
// simply a pass").
bool parse_tempo_with_scale(const std::string& s, WarpMarker& m,
                            std::string& error_out) {
    const size_t star = s.find('*');
    const std::string tempo_part = (star == std::string::npos)
        ? s : s.substr(0, star);
    const std::string scale_part = (star == std::string::npos)
        ? std::string() : s.substr(star + 1);
    const size_t chain = tempo_part.find_first_of("+-");
    const std::string base_part = (chain == std::string::npos)
        ? tempo_part : tempo_part.substr(0, chain);
    int64_t tempo_c = 0;
    if (!parse_tempo_field(base_part, tempo_c, error_out)) {
        return false;
    }
    std::vector<int64_t> terms;
    for (size_t at = chain; at != std::string::npos; ) {
        const size_t next = tempo_part.find_first_of("+-", at + 1);
        const std::string term = (next == std::string::npos)
            ? tempo_part.substr(at) : tempo_part.substr(at, next - at);
        if (terms.size() == static_cast<size_t>(kMaxTempoDeviationTerms)) {
            error_out = "tempo carries at most " +
                        std::to_string(kMaxTempoDeviationTerms) +
                        " deviations: " + tempo_part;
            return false;
        }
        int64_t term_c = 0;
        if (!parse_deviation_field(term, term_c, error_out)) {
            return false;
        }
        terms.push_back(term_c);
        tempo_c += term_c;
        at = next;
    }
    // THE RESOLVED TOTAL TAKES THE TEMPO BRACKET, its own sentence naming the
    // whole tempo part: a chain whose terms are each legal can still walk the
    // sounding tempo out of the authored window, and tempo_cents is what every
    // reader of this marker takes. Base and terms are bracketed, so the sum
    // cannot overflow.
    if (tempo_c < kTempoMinCents || tempo_c > kTempoMaxCents) {
        error_out = "tempo and its deviations must total within [" +
                    format_tempo_cents(kTempoMinCents) + ", " +
                    format_tempo_cents(kTempoMaxCents) + "]: " + tempo_part;
        return false;
    }
    std::optional<double> scale_v;
    if (star != std::string::npos) {
        double sv = 0.0;
        if (!parse_positive_value(scale_part, sv, "scale", kScaleMin, kScaleMax, 4, error_out)) {
            return false;
        }
        scale_v = sv;
    }
    m.tempo_inherits = false;
    m.tempo_cents    = tempo_c;
    m.tempo_scale    = scale_v;
    m.tempo_deviation_cents = std::move(terms);
    return true;
}

// Parse a new-format payload (the part after the pipe) into a partly-
// populated WarpMarker base — sets tempo/label fields only. Cross-marker
// checks (label_def uniqueness, time ordering) are the caller's job;
// label_ref resolvability is not checked at load at all — it is a render
// boundary verdict (build_warp_frame_map).
//
// On success, returns true and the WarpMarker carries the parsed payload.
// On failure, returns false and `error_out` is set.
//
// The `#` disabled flag, the marker time, and trim flags come from outside
// the payload and are the caller's job; they are not handled here.
bool parse_new_payload(const std::string& payload,
                       WarpMarker& m,
                       std::string& error_out) {
    if (payload.empty()) {
        error_out = "empty payload";
        return false;
    }
    if (payload.find('(') != std::string::npos ||
        payload.find(')') != std::string::npos) {
        error_out = "parens are not valid in the new format: " + payload;
        return false;
    }
    if (payload.find(' ') != std::string::npos ||
        payload.find('\t') != std::string::npos) {
        error_out = "whitespace is not valid in the new format: " + payload;
        return false;
    }

    // Split on `:` — at most one colon expected.
    const size_t colon = payload.find(':');
    if (colon != std::string::npos &&
        payload.find(':', colon + 1) != std::string::npos) {
        error_out = "too many colons in payload: " + payload;
        return false;
    }

    if (colon == std::string::npos) {
        // Single part: tempo, pass, or label_ref.
        if (payload == "pass") {
            m.tempo_inherits = true;
            m.tempo_cents    = 100;
            m.tempo_scale.reset();
            m.tempo_deviation_cents.clear();
            return true;
        }
        if (is_valid_label_format(payload)) {
            m.label_ref      = payload;
            m.tempo_inherits = false;
            m.tempo_cents    = 0;
            m.tempo_scale.reset();
            m.tempo_deviation_cents.clear();
            return true;
        }
        // Tempo (numeric, with optional *scale).
        return parse_tempo_with_scale(payload, m, error_out);
    }

    // Two parts: (TEMPO[*SCALE] | pass) : label_def. The three WarpMarker
    // state axes (tempo source, label relationship, disabled) are
    // independent; `pass:LABEL` is the inheriting + label_def combination.
    const std::string tempo_with_scale = payload.substr(0, colon);
    const std::string label_def        = payload.substr(colon + 1);

    if (tempo_with_scale.empty()) {
        error_out = "missing tempo before colon";
        return false;
    }
    if (!is_valid_label_format(label_def)) {
        error_out = "invalid label definition: " + label_def;
        return false;
    }
    if (tempo_with_scale == "pass") {
        m.tempo_inherits = true;
        m.tempo_cents    = 100;
        m.tempo_scale.reset();
        m.tempo_deviation_cents.clear();
        m.label_def      = label_def;
        return true;
    }
    if (!parse_tempo_with_scale(tempo_with_scale, m, error_out)) {
        return false;
    }
    m.label_def = label_def;
    return true;
}

} // namespace

namespace warpmarkers_internal {

// --- single-line parser -----------------------------------------------------
//
// Parses one canonical line into a WarpMarker, doing line-local validation
// only. Cross-marker checks (label_def uniqueness, time ordering) are left
// to the caller; label_ref resolvability is a render boundary verdict, not
// a load check.
//
// THE MEASURE COMMENT LEFT THE GRAMMAR (architect approval 2026-09-16): a
// warp line is the canonical line whole; a line carrying ` //` is adversarial
// and load-fatal — no migration, the architect swept his own projects/. The
// no-whitespace loop below is the refusal: the separator's own space is the
// stray byte it names.
std::expected<WarpMarker, std::string> parse_single_canonical_line(
    const std::string& raw_line) {

    WarpMarker out{};

    std::string t = raw_line;
    if (t.empty()) return std::unexpected<std::string>("empty line");

    // No whitespace anywhere on the line.
    for (char c : t) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            return std::unexpected<std::string>("no whitespace allowed in canonical line");
        }
    }

    // [#]?  <frame position>  |  PAYLOAD
    if (!t.empty() && t[0] == '#') {
        out.disabled = true;
        t.erase(0, 1);
    }

    const size_t pipe = t.find('|');
    if (pipe == std::string::npos) {
        return std::unexpected<std::string>("expected '|' after frame position");
    }
    // The position field is an authored source-frame position
    // (frame_format.h): a whole frame, finite, non-negative, whole field
    // consumed. Anything else — a fractional value, the old MM:SS.mmm
    // timestamp form — is a malformed position and load-fatal.
    if (!parse_authored_frame(std::string_view(t).substr(0, pipe),
                              out.time_frame)) {
        return std::unexpected<std::string>("invalid frame position: " +
                                            t.substr(0, pipe));
    }
    t.erase(0, pipe + 1);

    std::string err;
    if (!parse_new_payload(t, out, err))
        return std::unexpected(std::move(err));
    return out;
}

} // namespace warpmarkers_internal

std::expected<std::vector<WarpMarker>, std::string>
parse_warpmarkers_file(const std::string& path,
                       std::optional<std::string>* path_free_reason) {
    auto fail = warptempo_parse::prefix_line_error;
    std::vector<WarpMarker> markers;

    // THE TWO REFUSALS THAT NAME THE PATH COMPOSE THEIR SENTENCE HERE AND
    // PUBLISH THEIR WORDS APART FROM IT (architect approval 2026-09-02, the
    // granted frozen touch): a GUI card is one clipped line that names a file
    // by its basename, and its composer already names this very file — the
    // one it handed in — so appending the composed sentence printed the path
    // twice, in two spellings, and the history road's card exposed a scratch
    // filename the user cannot act on. Pulling the path back out of the
    // English would be the parsing the two-clause rule forbids (a path may
    // hold a quote or a colon), so the words travel beside the sentence
    // instead. The returned string is unchanged by construction — one
    // composition, both readers — which is what keeps warptempo_cli's own
    // line byte-identical. The reason is PATH-FREE, not path-bearing: its
    // presence is also how a caller tells an open or read refusal (the path
    // is the loader's subject) from a line-numbered parse error (no path in
    // it at all), and the path a caller would name is the one it passed.
    const auto path_refusal = [&](const char* words) {
        if (path_free_reason) *path_free_reason = words;
        return std::unexpected<std::string>(words + (": " + path));
    };

    std::ifstream f(path);
    if (!f.is_open())
        return path_refusal("cannot open file");

    std::vector<std::string> raw_lines;
    {
        std::string line;
        while (std::getline(f, line)) raw_lines.push_back(std::move(line));
    }
    // The getline loop ends on eofbit (normal end of file) or on badbit (a
    // stream read failure mid-file). eofbit+failbit is the ordinary end of a
    // healthy file and parses on; badbit alone is a filesystem or media read
    // error, checked here before the parsing walk so a read that failed after
    // a valid prefix can never yield a silently shortened marker list.
    if (f.bad())
        return path_refusal("read error in file");

    // ----- Build markers ---------------------------------------------------

    // A missing frame-0 tempo owner is not a load rule. Files with no owner
    // at exactly frame 0 (a moved, disabled, pass, or label-ref first marker
    // — or no markers at all: an empty file parses to an empty vector) load
    // intact so the save/reload round trip can never lock the user out. The
    // render resolver (resolve_warp_markers_for_render) normalizes them: a
    // survivor at frame 0 is required, so when none exists it silently
    // prepends a plain enabled 1.00 owner there, and every render path
    // proceeds.
    int64_t last_time = -1;

    // Track which labels have been defined (for duplicate-definition errors).
    std::set<std::string> seen_def;

    for (size_t idx = 0; idx < raw_lines.size(); ++idx) {
        const int line_number = static_cast<int>(idx + 1);
        // Marker lines are byte-exact canonical whole: no BOM, blank, or
        // whitespace tolerance anywhere (the writer emits none), so any
        // space, tab, or CR — and a byte-empty line — is a hard,
        // line-numbered parse error via parse_single_canonical_line below;
        // the CRLF corruption tripwire is that same refusal.
        std::string t = raw_lines[idx];

        // '#' marks a disabled marker and nothing else. The strict parser
        // (parse_single_canonical_line) strips a leading '#', flags the
        // marker disabled, and parses the remainder exactly as an enabled
        // line would. A '#' line whose position or payload is malformed is a
        // parse error like any other malformed line — adversarial,
        // load-fatal, first error only. Comment lines do not exist in the
        // grammar: a line that is nothing but a ' //' fails as any other
        // malformed line does.
        auto parsed = warpmarkers_internal::parse_single_canonical_line(t);
        if (!parsed)
            return fail(line_number, std::move(parsed.error()));
        WarpMarker m = std::move(*parsed);

        // The validated position field's raw text (everything before the '|',
        // past any leading '#'), echoed verbatim in the decreasing-time
        // diagnostic.
        std::string_view pos_view = t;
        if (!pos_view.empty() && pos_view.front() == '#')
            pos_view.remove_prefix(1);
        const std::string time_raw(pos_view.substr(0, pos_view.find('|')));

        // Load rejects only DECREASING times. Equal-time (and other closely
        // spaced) markers load deliberately: the GUI may author them, so the
        // save/reload round trip must never lock the user out. The render
        // resolver (resolve_warp_markers_for_render) normalizes them — a
        // group of 2+ survivors sharing one exact frame collapses to one
        // plain enabled 1.00 owner, with one stderr line per group at every
        // resolve — so any equal-time arrangement renders. Decreasing stays
        // load-fatal as a corruption tripwire — the GUI always saves its
        // time-sorted store, so a decreasing file can only be a hand-edit
        // error or corruption.
        if (last_time >= 0 && m.time_frame < last_time)
            return fail(line_number,
                "time decreasing: " + time_raw);

        // Cross-marker validation. A pass following a label ref loads
        // intact — the GUI may author it and the save/reload round trip must
        // never lock the user out. The render resolver's inheritance walk
        // skips every label ref to the nearest prior tempo owner and copies
        // that owner's literal fields, silently — a ref owns a duration
        // equation, not a rate, so there is no literal for the pass to copy
        // from it (architect approval 2026-09-13). A label reference without
        // a matching definition is likewise authorable now (the GUI permits
        // deleting a definition its refs outlive), loads intact, and the
        // resolver normalizes the dangling ref to a plain 1.00 owner at its
        // own frame, with its own line.
        if (!m.label_def.empty()) {
            if (seen_def.count(m.label_def))
                return fail(line_number,
                    "duplicate label definition: " + m.label_def);
            seen_def.insert(m.label_def);
        }

        // pass markers carry inert defaults (set by parse_new_payload). No
        // cache: their effective tempo is resolved live via walk-backward
        // through the marker list at every read site.

        last_time = m.time_frame;
        markers.push_back(std::move(m));
    }

    return markers;
}
