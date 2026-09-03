#include "phaseresetmarkers_parse.h"

#include "frame_format.h"
#include "marker_measure.h"
#include "parse_text_util.h"

#include <expected>
#include <fstream>

namespace {

// Parse "[#]<frame position>" into a PhaseResetMarker. Returns the marker on
// success; on failure, returns a one-line diagnostic. The caller has already
// split off any ` //<measure>` suffix and rejected whitespace in what remains,
// so the token reaching here is whitespace-free and carries no measure
// concept — it stays anonymous. The canonical grammar is an optional
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

    if (!parse_authored_frame(token, out.time_frame)) {
        return std::unexpected<std::string>(
            "expected frame position: " + token);
    }
    return out;
}

} // namespace

std::expected<std::vector<PhaseResetMarker>, std::string>
parse_phaseresetmarkers_file(const std::string& path,
                             std::optional<std::string>* path_free_reason) {
    auto fail = warptempo_parse::prefix_line_error;
    std::vector<PhaseResetMarker> markers;

    // The warp column's own shape, for the warp column's reasons (architect
    // approval 2026-09-02, the granted frozen touch; the rationale is stated
    // once at warpmarkers_parse.cpp's lambda): the two refusals that name the
    // path compose their sentence here and publish the words apart from it,
    // so a card's composer — which already names this file, having handed it
    // in — says the words once and the file once, and the returned string is
    // unchanged by construction.
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

    int64_t last_time = -1;

    for (size_t idx = 0; idx < raw_lines.size(); ++idx) {
        const int line_number = static_cast<int>(idx + 1);
        const std::string& raw = raw_lines[idx];

        // '#' marks a disabled marker and nothing else. parse_line below
        // strips a leading '#', flags the marker disabled, and parses the
        // remainder as a frame position; a '#' line whose remainder is not a
        // valid position is a parse error like any other malformed line —
        // adversarial, load-fatal, first error only. A measure is a SUFFIX on
        // a marker line (below); comment LINES are not part of the grammar.

        // The ` //<measure>` suffix (marker_measure.h) comes off FIRST, so the
        // canonical prefix below keeps its byte-exact discipline untouched and
        // parse_line stays measure-unaware. The measure's own ASCII grammar is
        // judged just as strictly — a CR landing inside a measure is still
        // fatal, so the CRLF corruption tripwire survives the relaxation.
        // (Architect approval 2026-08-20.)
        std::string_view canonical = raw;
        std::string      measure;
        {
            const MarkerMeasureSplit split = split_marker_measure(raw);
            if (split.had_measure) {
                std::string measure_err;
                if (!validate_marker_measure(split.measure, measure_err))
                    return fail(line_number, std::move(measure_err));
                measure.assign(split.measure);
                canonical = split.prefix;
            }
        }

        // Marker lines are byte-exact canonical: no BOM, blank, or whitespace
        // tolerance in the canonical prefix (the writer emits none). Any space,
        // tab, or CR there is a hard, line-numbered parse error, and a
        // byte-empty line fails parse_line's own empty-token refusal below.
        if (canonical.find_first_of(" \t\r") != std::string_view::npos) {
            return fail(line_number, "no whitespace allowed in canonical line");
        }

        auto parsed = parse_line(std::string(canonical));
        if (!parsed)
            return fail(line_number, std::move(parsed.error()));
        PhaseResetMarker m = std::move(*parsed);
        m.measure = std::move(measure);
        const int64_t eff = m.time_frame;
        // A reset at time zero parses, loads, and derives; it lands on the
        // engine's first analysis frame, where synthesis phase is seeded from
        // analysis phase anyway, so it is inert but harmless.
        //
        // Load rejects only DECREASING times. Equal-time (and other closely
        // spaced) markers load deliberately — load stays lenient. At the
        // render boundary, exact-equal enabled resets collapse into one
        // event (one stderr line per group); distinct whole frames render
        // as authored, however closely spaced. Decreasing stays load-fatal
        // as a corruption tripwire — the
        // GUI always saves its time-sorted store, so a decreasing file can only
        // be a hand-edit error or corruption.
        //
        // This parser has no audio duration, so it cannot range-check against
        // the source end; a past-EOF reset is load-fatal at the orchestrator
        // (GUI file_loader / CLI), which hard-fails it as adversarial input (a
        // reset file applies only to the audio it was authored against).
        // build_phase_reset_source_frames keeps its enabled-past-end refusal as
        // the render-boundary breach backstop.
        if (last_time >= 0 && eff < last_time)
            return fail(line_number,
                "time decreasing: " + format_authored_frame(eff));
        last_time = eff;
        markers.push_back(std::move(m));
    }

    return markers;
}
