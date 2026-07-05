#pragma once

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// --- Phase-reset-frame-map file reader (header-only, dependency-free) ------
// Inverse of the parser's write_phase_reset_frame_map. It lives here, not in
// the parser's map_output.cpp, so the engine-only warptempo_engine driver can
// read the artifact while linking libwarptempo_engine alone (no parser
// archive); read_warp_frame_map (warp_frame_map.h) is the warp-axis sibling.
// The format is trivial whitespace-separated numeric text, specified at the
// writer in map_output.cpp; keep this in lockstep with that writer.
//
// The reader validates line shape only. Ordering conformance is the writers'
// contract, and it is not left as an assumed precondition downstream: the
// engine refuses loudly at init on an out-of-order phase reset list
// (validate_phase_reset_frame_map_ordered in src/engine/engine.cpp), so a
// hand-edited artifact that breaks the ordering contract fails the render
// instead of producing silently wrong bytes.
//
// .phaseresetframemap: one undisplaced source-frame double per line, in file
// order (the writer emits up to 17 significant digits, so the value
// round-trips exactly; whole-frame positions carry no decimal point, so old
// integer-format files parse unchanged). Blank / whitespace-only lines
// skipped; any malformed line (non-numeric, missing field, or trailing
// garbage) fails the whole read. The file carries only active resets (the
// writer's caller drops disabled markers), so there is no '#'/disabled
// syntax to handle. A missing/unopenable file is std::nullopt; an
// empty-but-readable file yields an empty list (a valid "no resets" render
// input).
inline std::optional<std::vector<double>>
read_phase_reset_frame_map(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    std::vector<double> frames;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        std::istringstream ls(line);
        double f = 0.0;
        if (!(ls >> f)) return std::nullopt;
        std::string extra;
        if (ls >> extra) return std::nullopt;  // trailing garbage
        frames.push_back(f);
    }
    return frames;
}
