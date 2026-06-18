#include "warpmarkers_parse.h"

#include "parse_text_util.h"
#include "time_format.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <expected>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace {

using warptempo_parse::strip_bom;
using warptempo_parse::trim_ws;

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

// Legacy-only: evaluate a sum-of-signed-decimals like "1.23+0.05-0.03".
// Whitespace is stripped. Used on legacy load only; new format rejects
// arithmetic in the tempo column.
double eval_math_string(const std::string& in) {
    std::string s;
    s.reserve(in.size());
    for (char c : in) {
        if (!std::isspace(static_cast<unsigned char>(c))) s.push_back(c);
    }
    double total = 0.0;
    char op = '+';
    size_t i = 0;
    while (i < s.size()) {
        size_t len = 0;
        while (i + len < s.size() &&
               (std::isdigit(static_cast<unsigned char>(s[i + len])) ||
                s[i + len] == '.')) {
            ++len;
        }
        if (len > 0) {
            const double v = std::stod(s.substr(i, len));
            if (op == '+') total += v;
            else if (op == '-') total -= v;
            i += len;
        } else if (s[i] == '+' || s[i] == '-') {
            op = s[i];
            ++i;
        } else {
            break;
        }
    }
    return total;
}

std::vector<std::string> split_pipe(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string seg;
    while (std::getline(ss, seg, '|')) out.push_back(seg);
    return out;
}

bool is_indented_raw(const std::string& raw) {
    return !raw.empty() && (raw[0] == ' ' || raw[0] == '\t');
}

// Parse a new-format payload (the part after the pipe) into a partly-
// populated WarpMarker base — sets tempo/label fields only. Cross-marker
// checks (label_ref existence, label_def uniqueness) are the caller's job.
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
        if (star != std::string::npos && !is_valid_scale_format(scale_part)) {
            error_out = "scale must be N.NNNN format: " + scale_part;
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
    if (star != std::string::npos && !is_valid_scale_format(scale_part)) {
        error_out = "scale must be N.NNNN format: " + scale_part;
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

// Normalize a scale string to canonical N.NNNN form. Used by save() to
// re-emit legacy-loaded scales (which may have had any precision) in the
// new format. If the string can't be parsed, returns it unchanged so the
// data isn't lost — but the next reload will reject it.
std::string normalize_scale_string(const std::string& s) {
    if (s.empty()) return s;
    try {
        const double v = std::stod(s);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4f", v);
        return buf;
    } catch (...) {
        return s;
    }
}

// --- single-line parser -----------------------------------------------------
//
// Parses one canonical line into a WarpMarker, doing line-local validation
// only. Cross-marker checks (label_ref existence, label_def uniqueness,
// time monotonicity) are left to the caller.
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

    // ----- File-level legacy detection ------------------------------------
    //
    // A file is legacy if any line contains the `""""` ditto sentinel.
    // Mixed-format files are not handled — the first save migrates the
    // entire file in one shot.
    bool is_legacy_file = false;
    for (const auto& raw : raw_lines) {
        if (raw.find("\"\"\"\"") != std::string::npos) {
            is_legacy_file = true;
            break;
        }
    }

    // ----- Pass 1: gather defined labels ---------------------------------
    //
    // For both formats. A label reference is only valid if its label
    // appears as a def somewhere in the file. Disabled defs still count.

    std::set<std::string>            defined;
    std::map<std::string, int>       first_def_line;

    for (size_t idx = 0; idx < raw_lines.size(); ++idx) {
        const int line_number = static_cast<int>(idx + 1);
        const std::string& raw = raw_lines[idx];
        if (is_indented_raw(raw)) continue;
        std::string t = trim_ws(raw);
        if (t.empty()) continue;

        if (!t.empty() && t[0] == '#') {
            if (t.size() >= 10 && is_valid_timestamp_format(t.substr(1, 9))) {
                t.erase(0, 1);
            } else {
                continue;
            }
        }
        if (t.empty()) continue;

        if (is_legacy_file) {
            // Legacy: column 3 holds the label def. Truncate at first space
            // so trailing freeform text doesn't end up inside the column.
            const size_t sp = t.find(' ');
            const std::string body =
                (sp == std::string::npos) ? t : t.substr(0, sp);
            const auto cols = split_pipe(body);
            if (cols.size() > 2 && !cols[2].empty()) {
                std::string lbl = cols[2];
                if (!lbl.empty() && lbl[0] == '#') lbl.erase(0, 1);
                if (is_valid_label_format(lbl)) {
                    if (defined.insert(lbl).second) {
                        first_def_line[lbl] = line_number;
                    }
                }
            }
        } else {
            // New format: payload after `|`, optionally containing `:label`.
            const size_t pipe = t.find('|');
            if (pipe == std::string::npos) continue;
            const std::string payload = t.substr(pipe + 1);
            if (payload.find('(') != std::string::npos ||
                payload.find(')') != std::string::npos) {
                continue;
            }
            const size_t colon = payload.find(':');
            if (colon == std::string::npos) continue;
            const std::string lbl = payload.substr(colon + 1);
            if (is_valid_label_format(lbl)) {
                if (defined.count(lbl) == 0) {
                    defined.insert(lbl);
                    first_def_line[lbl] = line_number;
                }
            }
        }
    }

    // ----- Pass 2: build markers -----------------------------------------

    // `have_prev_numeric` gates the legacy `""""` ditto sentinel: ditto can
    // only appear after a numeric tempo. The actual tempo carried forward
    // is no longer recorded — pass markers in the in-memory model carry
    // inert defaults and resolve live via walk-backward instead.
    bool have_prev_numeric = false;
    bool first_marker_seen = false;
    double last_time       = -1.0;

    // Track which line first defined each label (for duplicate errors).
    std::set<std::string>      seen_def_in_pass2;
    std::map<std::string, int> seen_def_line;

    for (size_t idx = 0; idx < raw_lines.size(); ++idx) {
        const int line_number = static_cast<int>(idx + 1);
        const std::string& raw = raw_lines[idx];
        if (is_indented_raw(raw)) {
            continue;
        }
        std::string t = trim_ws(raw);
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

        // ---------- Legacy parse path (load-only, file-level routed) ----
        if (is_legacy_file) {
            const size_t sp = t.find(' ');
            if (sp != std::string::npos) {
                t = t.substr(0, sp);
            }
            const auto cols = split_pipe(t);
            if (cols.size() < 2)
                return fail(line_number, "need at least time|tempo columns");
            const std::string& time_raw = cols[0];

            if (time_raw.size() < 9 ||
                !is_valid_timestamp_format(time_raw.substr(0, 9)))
                return fail(line_number, "invalid time format: " + time_raw);
            const std::string time_initial = time_raw.substr(0, 9);
            double final_time = parse_timestamp(time_initial);
            if (time_raw.size() > 9) {
                final_time += eval_math_string(time_raw.substr(9));
            }

            if (!first_marker_seen) {
                if (time_initial != "00:00.000")
                    return fail(line_number,
                        "first marker must be 00:00.000 (got " + time_initial +
                        ")");
                first_marker_seen = true;
            }
            if (last_time >= 0.0 && final_time <= last_time)
                return fail(line_number,
                    "time not strictly increasing: " + time_initial);

            WarpMarker m;
            m.time_seconds  = final_time;

            const std::string& tempo_raw = cols[1];
            const std::string  label_raw = (cols.size() > 2) ? cols[2]
                                                             : std::string();

            const bool tempo_quoted  = (tempo_raw == "\"\"\"\"");
            const bool tempo_numeric = !tempo_raw.empty() &&
                (std::isdigit(static_cast<unsigned char>(tempo_raw[0])) ||
                 tempo_raw[0] == '.');

            if (tempo_quoted) {
                if (!have_prev_numeric)
                    return fail(line_number,
                        "ditto tempo \"\"\"\" has no preceding numeric tempo");
                m.tempo_inherits = true;
                m.tempo_base     = 1.0;
                m.tempo_scale    = "1.0000";
            } else if (tempo_numeric) {
                const size_t star = tempo_raw.find('*');
                const std::string base_part = (star == std::string::npos)
                    ? tempo_raw : tempo_raw.substr(0, star);
                m.tempo_inherits = false;
                m.tempo_base     = eval_math_string(base_part);
                m.tempo_scale    = (star == std::string::npos)
                    ? std::string() : tempo_raw.substr(star + 1);
                have_prev_numeric = true;
            } else {
                if (!is_valid_label_format(tempo_raw))
                    return fail(line_number,
                        "invalid tempo or label reference: " + tempo_raw);
                if (defined.count(tempo_raw) == 0)
                    return fail(line_number,
                        "reference to undefined label: " + tempo_raw);
                m.label_ref      = tempo_raw;
                m.tempo_inherits = false;
                m.tempo_base     = 0.0;
                m.tempo_scale.clear();
            }

            if (!label_raw.empty()) {
                std::string def = label_raw;
                bool def_disabled = false;
                if (def[0] == '#') {
                    def_disabled = true;
                    def.erase(0, 1);
                }
                if (!is_valid_label_format(def))
                    return fail(line_number,
                        "invalid label definition: " + label_raw);
                if (!m.label_ref.empty())
                    return fail(line_number,
                        "marker cannot be both a label reference and a label "
                        "definition");
                m.label_def = def;
                m.disabled  = def_disabled;
            }
            (void)line_disabled;

            last_time = m.time_seconds;
            markers.push_back(std::move(m));
            continue;
        }

        // ---------- New-format parse path -------------------------------

        auto parsed = warpmarkers_internal::parse_single_canonical_line(t);
        if (!parsed)
            return fail(line_number, std::move(parsed.error()));
        WarpMarker m = std::move(*parsed);
        if (line_disabled) m.disabled = true;

        // `t.substr(0,9)` is a validated timestamp at this point.
        const std::string time_raw = t.substr(0, 9);

        if (!first_marker_seen) {
            if (time_raw != "00:00.000")
                return fail(line_number,
                    "first marker must be 00:00.000 (got " + time_raw +
                    ")");
            first_marker_seen = true;
        }
        if (last_time >= 0.0 && m.time_seconds <= last_time)
            return fail(line_number,
                "time not strictly increasing: " + time_raw);

        // Cross-marker validation.
        if (!m.label_ref.empty() && defined.count(m.label_ref) == 0)
            return fail(line_number,
                "reference to undefined label: " + m.label_ref);
        if (!m.label_def.empty()) {
            if (seen_def_in_pass2.count(m.label_def))
                return fail(line_number,
                    "duplicate label definition: " + m.label_def +
                    " (first defined at line " +
                    std::to_string(seen_def_line[m.label_def]) + ")");
            seen_def_in_pass2.insert(m.label_def);
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
