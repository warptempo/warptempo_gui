#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

struct WarpFrameMapSegment {
    // Frame-position breakpoints carried at full precision; the dense warp
    // schedule interpolates these and rounds only at the final per-frame source
    // position. A collinear (redundant) breakpoint lies exactly on the segment
    // line and is idempotent. Integer frame values are produced on demand
    // (llrint) at the few sites that need them.
    double src_frame;
    double tgt_frame;
};

// map_source_to_target / map_target_to_source keep their unprefixed names:
// they are generic piecewise interpolators over the warp frame map, and the
// phase reset axis has no interpolation sibling (resets are point events), so
// there is no second column to disambiguate against. Both axes call them.
inline double map_source_to_target(double src_frame, const std::vector<WarpFrameMapSegment>& map) {
    if (map.empty()) return src_frame;
    if (src_frame <= map.front().src_frame) return map.front().tgt_frame;
    // Strictly monotonic src_frame, so the owning segment is found by binary
    // search: i is the last segment with src_frame <= query.
    auto it = std::upper_bound(
        map.begin(), map.end(), src_frame,
        [](double q, const WarpFrameMapSegment& s) { return q < s.src_frame; });
    const size_t i = static_cast<size_t>(it - map.begin()) - 1;
    if (i < map.size() - 1) {
        double src_dur = map[i+1].src_frame - map[i].src_frame;
        double tgt_dur = map[i+1].tgt_frame - map[i].tgt_frame;
        double offset  = src_frame - map[i].src_frame;
        return map[i].tgt_frame + (offset * (tgt_dur / src_dur));
    }
    const auto& last = map.back();
    return last.tgt_frame + (src_frame - last.src_frame);
}

// Inverse of map_source_to_target: piecewise-linear interpolation over the same
// segment list, mapping a target-frame query back to its source-frame position.
// Used by the GUI's target view to translate per-column target-frame ranges into
// source-frame ranges for the shared waveform paint. Clamp to the first segment
// before the tgt start; identity past the last segment; empty map is identity.
inline double map_target_to_source(double tgt_frame, const std::vector<WarpFrameMapSegment>& map) {
    if (map.empty()) return tgt_frame;
    if (tgt_frame <= map.front().tgt_frame) return map.front().src_frame;
    // Strictly monotonic tgt_frame, so the owning segment is found by binary
    // search, mirroring map_source_to_target.
    auto it = std::upper_bound(
        map.begin(), map.end(), tgt_frame,
        [](double q, const WarpFrameMapSegment& s) { return q < s.tgt_frame; });
    const size_t i = static_cast<size_t>(it - map.begin()) - 1;
    if (i < map.size() - 1) {
        double src_dur = map[i+1].src_frame - map[i].src_frame;
        double tgt_dur = map[i+1].tgt_frame - map[i].tgt_frame;
        double offset  = tgt_frame - map[i].tgt_frame;
        return map[i].src_frame + (offset * (src_dur / tgt_dur));
    }
    const auto& last = map.back();
    return last.src_frame + (tgt_frame - last.tgt_frame);
}

// --- Warp-frame-map file reader (header-only, dependency-free) -------------
// Inverse of the parser's write_warp_frame_map. It lives here, not in the
// parser's map_output.cpp, so the engine-only warptempo_engine driver can
// read the artifact while linking libwarptempo_engine alone (no parser
// archive); read_phase_reset_frame_map (phase_reset_frame_map.h) is the
// phase-reset-axis sibling. The format is trivial whitespace-separated
// numeric text, specified at the writer in map_output.cpp; keep this in
// lockstep with that writer.
//
// The reader validates line shape only. Value-domain and ordering conformance
// is the writers' contract: build_warp_frame_map and the trimmed-artifact derivation
// emit finite, non-negative, strictly ascending values with a first target of
// exactly zero by construction. Ordering is not left as an assumed
// precondition downstream: the engine refuses loudly at init on a frame map
// or phase reset list that is not strictly ascending (the two
// validators in src/engine/engine.cpp), so a hand-edited artifact that breaks
// the ordering contract fails the render instead of producing silently wrong
// bytes. The reader itself does not police it.
//
// .warpframemap: one "src_frame tgt_frame" line per segment (space-separated;
// the writer emits precise double breakpoints at up to 17 significant digits;
// a leading 0 0 anchor is present unless dropped at write). Blank /
// whitespace-only lines are skipped. Any malformed line (non-numeric, missing
// field, or trailing garbage) fails the whole read (std::nullopt), so a
// truncated or corrupt file never feeds the engine a partial map. A
// missing/unopenable file is also std::nullopt.
inline std::optional<std::vector<WarpFrameMapSegment>>
read_warp_frame_map(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    std::vector<WarpFrameMapSegment> segs;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        std::istringstream ls(line);
        double s = 0.0, t = 0.0;
        if (!(ls >> s >> t)) return std::nullopt;
        std::string extra;
        if (ls >> extra) return std::nullopt;  // trailing garbage
        segs.push_back(WarpFrameMapSegment{s, t});
    }
    return segs;
}
