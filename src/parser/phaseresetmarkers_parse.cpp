#include "phaseresetmarkers_parse.h"

#include "parse_text_util.h"
#include "time_format.h"

#include <cstring>
#include <expected>
#include <fstream>
#include <sstream>

namespace {

using warptempo_parse::strip_bom;
using warptempo_parse::trim_ws;

bool starts_with(const std::string& s, const char* pfx) {
    const size_t n = std::strlen(pfx);
    return s.size() >= n && s.compare(0, n, pfx) == 0;
}

// Parse the single timestamp token into a time in seconds. The token must
// be exactly nine characters of MM:SS.mmm; a mode token (trailing |peak /
// |heap / |pass), an extra whitespace-separated token, or any trailing
// characters after the timestamp are parse errors carrying an upgrade
// diagnostic.
std::expected<double, std::string> parse_timestamp_token(
    const std::vector<std::string>& toks) {
    if (toks.empty()) {
        return std::unexpected<std::string>("missing timestamp");
    }
    if (toks.size() > 1) {
        return std::unexpected<std::string>("unexpected status code");
    }

    const std::string& token = toks[0];
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
// success; on failure, returns a one-line diagnostic. Files written by
// older builds (carrying an i/d status code or a displaced_frame token)
// are rejected with "unexpected status code" so the upgrade requirement
// surfaces to the user instead of silently misparsing. Trim flags (b= / e=)
// are warp-only; encountering one on a phase reset line is
// a parse error so the migration requirement surfaces.
std::expected<PhaseResetMarker, std::string> parse_line(const std::string& raw) {
    PhaseResetMarker out;
    std::string t = trim_ws(raw);
    if (t.empty()) {
        return std::unexpected<std::string>("empty line");
    }

    if (starts_with(t, "b=") || starts_with(t, "e=")) {
        return std::unexpected<std::string>("phase_reset trim flags not supported; "
                  "move b= / e= to a warp marker");
    }

    if (!t.empty() && t[0] == '#') {
        out.disabled = true;
        t.erase(0, 1);
    }

    std::vector<std::string> toks;
    {
        std::istringstream iss(t);
        std::string tk;
        while (iss >> tk) toks.push_back(std::move(tk));
    }
    if (toks.empty()) {
        return std::unexpected<std::string>("missing timestamp");
    }

    auto parsed_time = parse_timestamp_token(toks);
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
        const std::string t = trim_ws(raw);

        if (t.empty()) continue;

        if (t[0] == '#' &&
            !(t.size() >= 10 && is_valid_timestamp_format(t.substr(1, 9)))) {
            continue;
        }

        auto parsed = parse_line(t);
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
