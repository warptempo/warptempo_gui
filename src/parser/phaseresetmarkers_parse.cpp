#include "phaseresetmarkers_parse.h"

#include "parse_text_util.h"
#include "time_format.h"

#include <expected>
#include <fstream>

namespace {

using warptempo_parse::strip_bom;

// Parse "[#]MM:SS.mmm" into a PhaseResetMarker. Returns the marker on
// success; on failure, returns a one-line diagnostic. The caller has
// already rejected any whitespace on the line, so the token reaching here is
// non-empty and whitespace-free. The canonical grammar is an optional
// leading '#' meaning disabled, then exactly nine characters forming a valid
// MM:SS.mmm timestamp and nothing else; anything else fails with the generic
// timestamp error.
std::expected<PhaseResetMarker, std::string> parse_line(const std::string& raw) {
    PhaseResetMarker out;

    std::string token = raw;
    if (!token.empty() && token[0] == '#') {
        out.disabled = true;
        token.erase(0, 1);
    }

    if (token.size() != 9 || !is_valid_timestamp_format(token)) {
        return std::unexpected<std::string>(
            "expected MM:SS.mmm timestamp: " + token);
    }
    out.time_seconds = parse_timestamp(token);
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
