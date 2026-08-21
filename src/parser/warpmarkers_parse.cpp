// Dead includes removed under grant (architect approval 2026-08-02).
#include "warpmarkers_parse.h"

#include "frame_format.h"
#include "marker_measure.h"
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
// the optional scale through parse_positive_value, then writes the three
// tempo fields. label_def, if any, is the caller's to attach.
bool parse_tempo_with_scale(const std::string& s, WarpMarker& m,
                            std::string& error_out) {
    const size_t star = s.find('*');
    const std::string tempo_part = (star == std::string::npos)
        ? s : s.substr(0, star);
    const std::string scale_part = (star == std::string::npos)
        ? std::string() : s.substr(star + 1);
    int64_t tempo_c = 0;
    if (!parse_tempo_field(tempo_part, tempo_c, error_out)) {
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
            return true;
        }
        if (is_valid_label_format(payload)) {
            m.label_ref      = payload;
            m.tempo_inherits = false;
            m.tempo_cents    = 0;
            m.tempo_scale.reset();
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
// a load check. `accept_measure` selects whether the ` //<measure>` suffix is
// part of the grammar here; the four callers and their answers are at the
// declaration. (Measure grammar: architect approval 2026-08-20.)
std::expected<WarpMarker, std::string> parse_single_canonical_line(
    const std::string& raw_line, bool accept_measure) {

    WarpMarker out{};

    std::string t = raw_line;
    if (t.empty()) return std::unexpected<std::string>("empty line");

    // The measure suffix comes off FIRST, so everything below judges the
    // canonical prefix alone and keeps its byte-exact discipline unchanged —
    // in particular the no-whitespace loop, which is what refuses a ` //` on
    // the callers that pass false.
    if (accept_measure) {
        const MarkerMeasureSplit split = split_marker_measure(t);
        if (split.had_measure) {
            std::string measure_err;
            if (!validate_marker_measure(split.measure, measure_err))
                return std::unexpected(std::move(measure_err));
            out.measure.assign(split.measure);
            t.resize(split.prefix.size());
        }
    }

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
parse_warpmarkers_file(const std::string& path) {
    auto fail = warptempo_parse::prefix_line_error;
    std::vector<WarpMarker> markers;

    std::ifstream f(path);
    if (!f.is_open())
        return std::unexpected("cannot open file: " + path);

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
        return std::unexpected("read error in file: " + path);

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
        // Marker lines are byte-exact canonical up to the measure separator:
        // no BOM, blank, or whitespace tolerance in the canonical prefix (the
        // writer emits none), so any space, tab, or CR there — and a byte-empty
        // line — is a hard, line-numbered parse error via
        // parse_single_canonical_line below. The one relaxation is the
        // ` //<measure>` SUFFIX (marker_measure.h): the split comes off before
        // the prefix is judged, and the measure's own ASCII grammar (bounded at
        // kMaxMarkerMeasureBytes, one canonical spelling per value) is judged
        // just as strictly — a CR landing inside a measure stays fatal, so the
        // CRLF corruption tripwire survives the relaxation intact. (COMMENT
        // ONLY, architect approval 2026-08-20: the bound was spelled `12` here
        // until the section qualifier raised it, so it now names its owner
        // instead of restating a number. No code in this frozen file changed —
        // it consumes the helpers and never re-reads the grammar.)
        std::string t = raw_lines[idx];

        // '#' marks a disabled marker and nothing else. The strict parser
        // (parse_single_canonical_line) strips a leading '#', flags the
        // marker disabled, and parses the remainder exactly as an enabled
        // line would. A '#' line whose position or payload is malformed is a
        // parse error like any other malformed line — adversarial,
        // load-fatal, first error only. A measure is a SUFFIX on a marker
        // line; comment LINES do not exist in the grammar, so a line that is
        // nothing but a ' //' suffix fails the position parse like any other
        // malformed line.
        auto parsed = warpmarkers_internal::parse_single_canonical_line(
            t, /*accept_measure=*/true);
        if (!parsed)
            return fail(line_number, std::move(parsed.error()));
        WarpMarker m = std::move(*parsed);

        // The validated position field's raw text (everything before the '|',
        // past any leading '#'), echoed verbatim in the decreasing-time
        // diagnostic. Measure-inert by construction: it reads to the first
        // '|', and the measure suffix begins past the whole canonical prefix,
        // so no measure byte can reach this slice.
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
        // never lock the user out. The render resolver normalizes it: a pass
        // whose inheritance walk terminates on a surviving enabled label ref
        // resolves to tempo 1.00, with one stderr line per timestamp at every
        // resolve. A label reference without a matching definition is
        // likewise authorable now (the GUI permits deleting a definition its
        // refs outlive), loads intact, and the resolver normalizes the
        // dangling ref to a plain 1.00 owner at its own frame, with its own
        // line.
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
