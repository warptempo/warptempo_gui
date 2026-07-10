#include "phaseresetmarkers_parse.h"

#include "frame_format.h"
#include "parse_text_util.h"

#include <expected>
#include <fstream>

namespace {

using warptempo_parse::strip_bom;

// Parse "[#]<frame double>" into a PhaseResetMarker. Returns the marker on
// success; on failure, returns a one-line diagnostic. The caller has
// already rejected any whitespace on the line, so the token reaching here is
// non-empty and whitespace-free. The canonical grammar is an optional
// leading '#' meaning disabled, then an authored source-frame position
// (frame_format.h: a whole frame, finite, non-negative, whole field
// consumed) and nothing else; anything else — a fractional value, the old
// MM:SS.mmm timestamp form — fails with the generic position error.
std::expected<PhaseResetMarker, std::string> parse_line(const std::string& raw) {
    PhaseResetMarker out;

    std::string token = raw;
    if (!token.empty() && token[0] == '#') {
        out.disabled = true;
        token.erase(0, 1);
    }

    if (!parse_frame_double(token, out.time_frame)) {
        return std::unexpected<std::string>(
            "expected frame position: " + token);
    }
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

        // Hash-comment convention: a first-byte '#' whose remaining text does
        // not parse as a frame double marks a comment line and is skipped; a
        // '#' that does prefix a valid frame position is a disabled marker
        // and falls through to the strict parse.
        if (raw[0] == '#') {
            double probe = 0.0;
            if (!parse_frame_double(std::string_view(raw).substr(1), probe)) {
                continue;
            }
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
        const double eff = m.time_frame;
        // A reset at time zero parses and loads; it is inert at render because
        // the near-start dropzone drops it at derivation.
        //
        // Load rejects only DECREASING times. Equal-time (and other closely
        // spaced) markers load deliberately — load stays lenient — but they
        // refuse at the render boundary: the raw-store rule
        // (marker_store_validate.h) prohibits two resets closer than one
        // deepest-zoom pixel of time on both columns, and
        // build_phase_reset_source_frames refuses the sub-frame subset of those
        // pairs as the breach backstop. Decreasing stays load-fatal as a
        // corruption tripwire — the
        // GUI always saves its time-sorted store, so a decreasing file can only
        // be a hand-edit error or corruption.
        //
        // This parser has no audio duration, so it cannot range-check against
        // the source end; a past-EOF reset is load-fatal at the orchestrator
        // (GUI file_loader / CLI), which hard-fails it as adversarial input (a
        // reset file applies only to the audio it was authored against).
        // build_phase_reset_source_frames keeps its enabled-past-end refusal as
        // the render-boundary breach backstop.
        if (last_time >= 0.0 && eff < last_time)
            return fail(line_number,
                "time decreasing: " + format_frame_double(eff));
        last_time = eff;
        markers.push_back(std::move(m));
    }

    return markers;
}
