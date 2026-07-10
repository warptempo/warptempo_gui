#include "warpmarkers_parse.h"

#include "frame_format.h"
#include "parse_text_util.h"
#include "value_format.h"

#include <algorithm>
#include <cstdio>
#include <expected>
#include <fstream>
#include <map>
#include <regex>
#include <set>

namespace {

using warptempo_parse::strip_bom;

bool is_valid_label_format(const std::string& s) {
    static const std::regex re("^[a-z]\\.[a-z0-9]{2}$");
    return std::regex_match(s, re);
}

// Tempo and scale values are full doubles (parse_value_double: whole field
// consumed, finite, no leading '-') under a strict positivity refusal —
// zero is parseable but refused with a pointed message; negatives and any
// other malformed spelling fail the parse itself — and the authored-value
// bracket for its kind, passed in by the caller (value_format.h: tempo
// fields [kTempoMin, kTempoMax], scale fields [kScaleMin, kScaleMax]).
// Every GUI input surface enforces the bracket, so an out-of-bracket value
// on disk is a state the GUI can never produce: adversarial, load-fatal,
// first error only, exactly like the non-positive refusals. Serializers
// pad the shortest round-trip form (format_value_double: tempo at min 2
// decimals, scale at min 4), so historical fixed-decimal sidecars
// re-serialize byte-identically.
bool parse_positive_value(const std::string& s, double& out,
                          const char* what, double lo, double hi,
                          std::string& error_out) {
    double v = 0.0;
    if (!parse_value_double(s, v)) {
        // parse_value_double rejects a leading '-' outright; a well-formed
        // negative number is still a positivity violation, not gibberish,
        // so it gets the same message a typed zero does.
        double neg_probe = 0.0;
        if (s.size() > 1 && s.front() == '-' &&
            parse_value_double(std::string_view(s).substr(1), neg_probe)) {
            error_out = std::string(what) + " must be positive: " + s;
        } else {
            error_out = std::string("invalid ") + what + " value: " + s;
        }
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
    out = v;
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
// `disabled_in` is plumbed through so the caller can attach a metadata
// flag (`#`) that came from outside the payload. Time and trim flags
// are not handled here.
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
            m.tempo_base     = 1.0;
            m.tempo_scale.reset();
            return true;
        }
        if (is_valid_label_format(payload)) {
            m.label_ref      = payload;
            m.tempo_inherits = false;
            m.tempo_base     = 0.0;
            m.tempo_scale.reset();
            return true;
        }
        // Tempo (numeric, with optional *scale).
        const size_t star = payload.find('*');
        const std::string tempo_part = (star == std::string::npos)
            ? payload : payload.substr(0, star);
        const std::string scale_part = (star == std::string::npos)
            ? std::string() : payload.substr(star + 1);
        double tempo_v = 0.0;
        if (!parse_positive_value(tempo_part, tempo_v, "tempo", kTempoMin, kTempoMax, error_out)) {
            return false;
        }
        std::optional<double> scale_v;
        if (star != std::string::npos) {
            double sv = 0.0;
            if (!parse_positive_value(scale_part, sv, "scale", kScaleMin, kScaleMax, error_out)) {
                return false;
            }
            scale_v = sv;
        }
        m.tempo_inherits = false;
        m.tempo_base     = tempo_v;
        m.tempo_scale    = scale_v;
        return true;
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
        m.tempo_base     = 1.0;
        m.tempo_scale.reset();
        m.label_def      = label_def;
        return true;
    }
    const size_t star = tempo_with_scale.find('*');
    const std::string tempo_part = (star == std::string::npos)
        ? tempo_with_scale : tempo_with_scale.substr(0, star);
    const std::string scale_part = (star == std::string::npos)
        ? std::string() : tempo_with_scale.substr(star + 1);
    double tempo_v = 0.0;
    if (!parse_positive_value(tempo_part, tempo_v, "tempo", kTempoMin, kTempoMax, error_out)) {
        return false;
    }
    std::optional<double> scale_v;
    if (star != std::string::npos) {
        double sv = 0.0;
        if (!parse_positive_value(scale_part, sv, "scale", kScaleMin, kScaleMax, error_out)) {
            return false;
        }
        scale_v = sv;
    }
    m.tempo_inherits = false;
    m.tempo_base     = tempo_v;
    m.tempo_scale    = scale_v;
    m.label_def      = label_def;
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

    // [#]?  <frame double>  |  PAYLOAD
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
    if (!parse_frame_double(std::string_view(t).substr(0, pipe),
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
    if (!raw_lines.empty()) strip_bom(raw_lines.front());

    // ----- Build markers ---------------------------------------------------

    // The first-marker grammar — an enabled, tempo-owning numeric marker at
    // exactly frame 0 — is a render-boundary requirement validated by
    // validate_first_marker_render_grammar (warp_frame_map_build.h), not a
    // load rule. Files violating it (a moved, disabled, pass, or label-ref
    // first marker — or no markers at all: an empty file parses to an empty
    // vector) load intact so the save/reload round trip can never lock the
    // user out; every render path refuses them with a pointed message
    // instead.
    double last_time = -1.0;

    // Track which line first defined each label (for duplicate errors).
    std::set<std::string>      seen_def;
    std::map<std::string, int> seen_def_line;

    for (size_t idx = 0; idx < raw_lines.size(); ++idx) {
        const int line_number = static_cast<int>(idx + 1);
        // Marker lines are byte-exact canonical: any space, tab, or CR
        // anywhere on the line is a hard, line-numbered parse error via
        // parse_single_canonical_line below. Only byte-empty lines are
        // skipped as blanks; a whitespace-only line is an error like any
        // other whitespace. The former trimming was legacy-format residue.
        std::string t = raw_lines[idx];
        if (t.empty()) {
            continue;
        }

        // Hash-comment convention: a first-byte '#' whose text up to the
        // first '|' does not parse as a frame double marks a comment line
        // and is skipped; a '#' that does prefix a valid frame position is
        // a disabled marker and falls through to the strict parse.
        bool line_disabled = false;
        if (!t.empty() && t[0] == '#') {
            const size_t pipe = t.find('|');
            double probe = 0.0;
            if (pipe != std::string::npos &&
                parse_frame_double(std::string_view(t).substr(1, pipe - 1),
                                   probe)) {
                line_disabled = true;
                t.erase(0, 1);
            } else {
                continue;
            }
        }
        if (t.empty()) {
            continue;
        }

        auto parsed = warpmarkers_internal::parse_single_canonical_line(t);
        if (!parsed)
            return fail(line_number, std::move(parsed.error()));
        WarpMarker m = std::move(*parsed);
        if (line_disabled) m.disabled = true;

        // The validated position field's raw text (everything before the
        // '|'), echoed verbatim in the decreasing-time diagnostic.
        const std::string time_raw = t.substr(0, t.find('|'));

        // Load rejects only DECREASING times. Equal-time markers load
        // deliberately: the GUI may author them, so the save/reload round
        // trip must never lock the user out; ordering degeneracy is a render
        // boundary verdict now, refused by build_warp_frame_map's existing
        // "marker segment < 1 frame" error. Decreasing stays load-fatal as a
        // corruption tripwire — the GUI always saves its time-sorted store,
        // so a decreasing file can only be a hand-edit error or corruption.
        if (last_time >= 0.0 && m.time_frame < last_time)
            return fail(line_number,
                "time decreasing: " + time_raw);

        // Cross-marker validation. A pass following a label ref is
        // deliberately accepted: the resolver inherits from the nearest
        // owner on the backward walk, skipping label refs and disabled
        // markers, deterministically. Bad form is the author's concern,
        // not a parse error. A label reference without a matching
        // definition is likewise authorable now (the GUI permits deleting
        // a definition its refs outlive), loads intact, and is refused at
        // the render boundary by build_warp_frame_map.
        if (!m.label_def.empty()) {
            if (seen_def.count(m.label_def))
                return fail(line_number,
                    "duplicate label definition: " + m.label_def +
                    " (first defined at line " +
                    std::to_string(seen_def_line[m.label_def]) + ")");
            seen_def.insert(m.label_def);
            seen_def_line[m.label_def] = line_number;
        }

        // pass markers carry inert defaults (set by parse_new_payload). No
        // cache: their effective tempo is resolved live via walk-backward
        // through the marker list at every read site.

        last_time = m.time_frame;
        markers.push_back(std::move(m));
    }

    return markers;
}
