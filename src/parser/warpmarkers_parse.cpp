#include "warpmarkers_parse.h"

#include "parse_text_util.h"
#include "time_format.h"

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

// New-format tempo: exactly one integer digit, dot, two decimal digits.
bool is_valid_tempo_format(const std::string& s) {
    static const std::regex re("^[0-9]\\.[0-9]{2}$");
    return std::regex_match(s, re);
}

// New-format scale: exactly one integer digit, dot, four decimal digits.
bool is_valid_scale_format(const std::string& s) {
    static const std::regex re("^[0-9]\\.[0-9]{4}$");
    return std::regex_match(s, re);
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
            m.tempo_scale    = "1.0000";
            return true;
        }
        if (is_valid_label_format(payload)) {
            m.label_ref      = payload;
            m.tempo_inherits = false;
            m.tempo_base     = 0.0;
            m.tempo_scale.clear();
            return true;
        }
        // Tempo (numeric, with optional *scale).
        const size_t star = payload.find('*');
        const std::string tempo_part = (star == std::string::npos)
            ? payload : payload.substr(0, star);
        const std::string scale_part = (star == std::string::npos)
            ? std::string() : payload.substr(star + 1);
        if (!is_valid_tempo_format(tempo_part)) {
            error_out = "tempo must be N.NN format: " + tempo_part;
            return false;
        }
        // The grammar admits no sign and no other spelling of zero, so an
        // exact compare against the zero literal is a total positivity check.
        if (tempo_part == "0.00") {
            error_out = "tempo must be positive: 0.00";
            return false;
        }
        if (star != std::string::npos && !is_valid_scale_format(scale_part)) {
            error_out = "scale must be N.NNNN format: " + scale_part;
            return false;
        }
        if (scale_part == "0.0000") {
            error_out = "scale must be positive: 0.0000";
            return false;
        }
        m.tempo_inherits = false;
        m.tempo_base     = std::stod(tempo_part);
        m.tempo_scale    = scale_part;
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
        m.tempo_scale    = "1.0000";
        m.label_def      = label_def;
        return true;
    }
    const size_t star = tempo_with_scale.find('*');
    const std::string tempo_part = (star == std::string::npos)
        ? tempo_with_scale : tempo_with_scale.substr(0, star);
    const std::string scale_part = (star == std::string::npos)
        ? std::string() : tempo_with_scale.substr(star + 1);
    if (!is_valid_tempo_format(tempo_part)) {
        error_out = "tempo must be N.NN format: " + tempo_part;
        return false;
    }
    // Same positivity check as the no-colon branch: the zero literal is the
    // grammar's only non-positive spelling, so the exact compare is total.
    if (tempo_part == "0.00") {
        error_out = "tempo must be positive: 0.00";
        return false;
    }
    if (star != std::string::npos && !is_valid_scale_format(scale_part)) {
        error_out = "scale must be N.NNNN format: " + scale_part;
        return false;
    }
    if (scale_part == "0.0000") {
        error_out = "scale must be positive: 0.0000";
        return false;
    }
    m.tempo_inherits = false;
    m.tempo_base     = std::stod(tempo_part);
    m.tempo_scale    = scale_part;
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

    // [#]?  MM:SS.SSS  |  PAYLOAD
    if (!t.empty() && t[0] == '#') {
        out.disabled = true;
        t.erase(0, 1);
    }

    if (t.size() < 9 || !is_valid_timestamp_format(t.substr(0, 9))) {
        return std::unexpected<std::string>("invalid time format: " + t.substr(0, std::min<size_t>(9, t.size())));
    }
    out.time_seconds = parse_timestamp(t.substr(0, 9));
    t.erase(0, 9);

    if (t.empty() || t[0] != '|') {
        return std::unexpected<std::string>("expected '|' after timestamp");
    }
    t.erase(0, 1);

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

    bool first_marker_seen = false;
    double last_time       = -1.0;

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

        bool line_disabled = false;
        if (!t.empty() && t[0] == '#') {
            if (t.size() >= 10 && is_valid_timestamp_format(t.substr(1, 9))) {
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

        // `t.substr(0,9)` is a validated timestamp at this point.
        const std::string time_raw = t.substr(0, 9);

        if (!first_marker_seen) {
            // The map builder applies the first surviving marker's tempo from
            // source frame 0, and the inheritance walk has no owner before
            // index 0. A zero marker that is disabled would drop out of the
            // render list and silently shift opening-span tempo ownership to a
            // later marker; a pass or label ref would leave the opening span
            // with no literal owner (a pass's backward walk finds none and
            // silently defaults to 1.0). A label def is banned so no ref can
            // ever target the zero marker, keeping the position-zero cascade
            // family unrepresentable. The def ban is deliberate even though
            // map math would tolerate a zero-positioned def (the marker still
            // owns a concrete tempo): the zero marker is the one marker that
            // can never be deleted or disabled, so a def there would make it a
            // permanent special-case participant in every def-lifecycle path
            // (rename cascade, referenced-def delete gate, def-removal gate),
            // and the author loses nothing — the same label can be defined at
            // any later marker. The grammar therefore
            // requires the zero marker to be an enabled owning plain tempo, and
            // every GUI mutation path guards the same rule.
            if (time_raw != "00:00.000")
                return fail(line_number,
                    "first marker must be 00:00.000 (got " + time_raw +
                    ")");
            if (m.disabled)
                return fail(line_number,
                    "first marker must not be disabled");
            if (m.tempo_inherits)
                return fail(line_number,
                    "first marker must own a numeric tempo (pass not allowed)");
            if (!m.label_ref.empty())
                return fail(line_number,
                    "first marker must not be a label reference");
            if (!m.label_def.empty())
                return fail(line_number,
                    "first marker must not carry a label definition");
            first_marker_seen = true;
        }
        // Load rejects only DECREASING times. Equal-time markers load
        // deliberately: the GUI may author them, so the save/reload round
        // trip must never lock the user out; ordering degeneracy is a render
        // boundary verdict now, refused by build_warp_frame_map's existing
        // "marker segment < 1 frame" error. Decreasing stays load-fatal as a
        // corruption tripwire — the GUI always saves its time-sorted store,
        // so a decreasing file can only be a hand-edit error or corruption.
        if (last_time >= 0.0 && m.time_seconds < last_time)
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

        last_time = m.time_seconds;
        markers.push_back(std::move(m));
    }

    if (!first_marker_seen)
        return std::unexpected(std::string("file contains no markers"));
    return markers;
}
