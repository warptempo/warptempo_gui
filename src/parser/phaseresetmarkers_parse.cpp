#include "phaseresetmarkers_parse.h"

#include "parse_text_util.h"
#include "time_format.h"

#include <cstring>
#include <expected>
#include <fstream>

namespace {

using warptempo_parse::strip_bom;

bool starts_with(const std::string& s, const char* pfx) {
    const size_t n = std::strlen(pfx);
    return s.size() >= n && s.compare(0, n, pfx) == 0;
}

// Parse the single timestamp token into a time in seconds. The token must
// be exactly nine characters of MM:SS.mmm; a mode token (trailing |peak /
// |heap / |pass) or any trailing characters after the timestamp are parse
// errors carrying an upgrade diagnostic.
std::expected<double, std::string> parse_timestamp_token(
    const std::string& token) {
    if (token.find('|') != std::string::npos) {
        return std::unexpected<std::string>("phase-reset mode tokens removed; "
                  "strip the trailing |peak/|heap/|pass");
    }
    if (token.size() < 9 || !is_valid_timestamp_format(token.substr(0, 9))) {
        return std::unexpected<std::string>(
            "expected MM:SS.mmm timestamp: " + token);
    }
    if (token.size() > 9) {
        return std::unexpected<std::string>(
            "trailing characters after timestamp: " + token);
    }

    return parse_timestamp(token);
}

// Parse "[#]MM:SS.mmm" into a PhaseResetMarker. Returns the marker on
// success; on failure, returns a one-line diagnostic. The caller has
// already rejected any whitespace on the line, so a legacy multi-token line
// (an i/d status code or a displaced_frame token) fails there with the
// no-whitespace error before reaching here. Trim flags (b= / e=) are
// warp-only; encountering one on a phase reset line is a parse error so the
// migration requirement surfaces.
std::expected<PhaseResetMarker, std::string> parse_line(const std::string& raw) {
    PhaseResetMarker out;

    if (starts_with(raw, "b=") || starts_with(raw, "e=")) {
        return std::unexpected<std::string>("phase_reset trim flags not supported; "
                  "move b= / e= to a warp marker");
    }

    std::string token = raw;
    if (!token.empty() && token[0] == '#') {
        out.disabled = true;
        token.erase(0, 1);
    }

    auto parsed_time = parse_timestamp_token(token);
    if (!parsed_time)
        return std::unexpected(std::move(parsed_time.error()));
    out.time_seconds = *parsed_time;
    return out;
}

} // namespace

std::expected<std::vector<PhaseResetMarker>, std::string>
parse_phaseresetmarkers_file(const std::string& path) {
    auto fail = warptempo_parse::prefix_line_error;
    std::vector<PhaseResetMarker> markers;

    std::ifstream f(path);
    if (!f.is_open())
        return std::unexpected("cannot open file: " + path);

    std::vector<std::string> raw_lines;
    {
        std::string line;
        while (std::getline(f, line)) raw_lines.push_back(std::move(line));
    }
    if (!raw_lines.empty()) strip_bom(raw_lines.front());

    double last_time = -1.0;

    for (size_t idx = 0; idx < raw_lines.size(); ++idx) {
        const int line_number = static_cast<int>(idx + 1);
        const std::string& raw = raw_lines[idx];

        if (raw.empty()) continue;

        // Hash-comment convention: a first-byte '#' that is not followed by a
        // valid nine-character timestamp marks a comment line and is skipped;
        // a '#' that does prefix a valid timestamp is a disabled marker and
        // falls through to the strict parse.
        if (raw[0] == '#' &&
            !(raw.size() >= 10 && is_valid_timestamp_format(raw.substr(1, 9)))) {
            continue;
        }

        // Marker lines are byte-exact canonical: any space, tab, or CR
        // anywhere on the line is a hard, line-numbered parse error. The
        // former trimming and whitespace tokenization were legacy-format
        // residue.
        if (raw.find_first_of(" \t\r") != std::string::npos) {
            return fail(line_number, "no whitespace allowed in canonical line");
        }

        auto parsed = parse_line(raw);
        if (!parsed)
            return fail(line_number, std::move(parsed.error()));
        PhaseResetMarker m = std::move(*parsed);
        const double eff = m.time_seconds;
        // A reset at time zero parses and loads; it is inert at render because
        // the near-start dropzone drops it at dispatch.
        if (last_time >= 0.0 && eff <= last_time)
            return fail(line_number,
                "time_seconds not strictly increasing: " + format_timestamp(eff));
        last_time = eff;
        markers.push_back(std::move(m));
    }

    return markers;
}
