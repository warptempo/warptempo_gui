// The magnification level marker column's reader (architect approval
// 2026-09-15, the frozen touch this file landed under).
#include "magnificationlevelmarkers_parse.h"

#include "frame_format.h"
#include "marker_magnification.h"
#include "parse_text_util.h"

#include <expected>
#include <fstream>
#include <string_view>

namespace {

// Parse "[#]<frame position>|<level>" into a MagnificationLevelMarker. The
// caller has already rejected whitespace. The frame is the authored canonical
// integer (frame_format.h) and the level the one-digit grammar
// (marker_magnification.h); the separator is exactly one `|`, and a missing
// separator or level is refused — every line carries its level.
std::expected<MagnificationLevelMarker, std::string> parse_line(
        const std::string& raw) {
    MagnificationLevelMarker out;

    std::string_view token = raw;
    if (!token.empty() && token[0] == '#') {
        out.disabled = true;
        token.remove_prefix(1);
    }

    const size_t bar = token.find('|');
    if (bar == std::string_view::npos) {
        return std::unexpected<std::string>(
            "expected <frame position>|<level>: " + std::string(token));
    }
    const std::string_view frame_text = token.substr(0, bar);
    const std::string_view level_text = token.substr(bar + 1);

    if (!parse_authored_frame(frame_text, out.time_frame)) {
        return std::unexpected<std::string>(
            "expected frame position: " + std::string(frame_text));
    }
    std::string level_error;
    if (!parse_marker_magnification(level_text, out.level, level_error)) {
        return std::unexpected<std::string>(
            level_error + ": " + std::string(level_text));
    }
    return out;
}

} // namespace

std::expected<std::vector<MagnificationLevelMarker>, std::string>
parse_magnificationlevelmarkers_file(
        const std::string& path,
        std::optional<std::string>* path_free_reason) {
    auto fail = warptempo_parse::prefix_line_error;
    std::vector<MagnificationLevelMarker> markers;

    // The two refusals that name the path publish their words apart from the
    // composed sentence (the warp column's contract, warpmarkers_parse.h).
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
    // badbit alone is a read error mid-file; checked before the walk so a
    // failed read can never yield a silently shortened marker list.
    if (f.bad())
        return path_refusal("read error in file");

    int64_t last_time = -1;

    for (size_t idx = 0; idx < raw_lines.size(); ++idx) {
        const int line_number = static_cast<int>(idx + 1);
        const std::string& raw = raw_lines[idx];

        // Byte-exact canonical lines: no BOM, blank or whitespace tolerance
        // (the writer emits none). A byte-empty line fails parse_line's
        // separator refusal.
        if (raw.find_first_of(" \t\r") != std::string::npos) {
            return fail(line_number, "no whitespace allowed in canonical line");
        }

        auto parsed = parse_line(raw);
        if (!parsed)
            return fail(line_number, std::move(parsed.error()));
        MagnificationLevelMarker m = *parsed;
        // Load rejects only DECREASING times, the phase-reset column's rule:
        // the GUI always saves its time-sorted store, so a decreasing file is
        // a hand edit or corruption, while equal frames load (the GUI's
        // waveform gain profile lets the last in store order win). The
        // past-EOF wall is the orchestrators' (first_past_eof_wall_defect):
        // this parser has no audio duration.
        if (last_time >= 0 && m.time_frame < last_time)
            return fail(line_number,
                "time decreasing: " + format_authored_frame(m.time_frame));
        last_time = m.time_frame;
        markers.push_back(m);
    }

    return markers;
}
