#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

struct FrameMapSegment {
    // Frame-position breakpoints carried at full precision; the dense warp
    // schedule interpolates these and rounds only at the final per-frame source
    // position. A collinear (redundant) breakpoint lies exactly on the segment
    // line and is idempotent. Integer frame values are produced on demand
    // (llrint) at the few sites that need them.
    double src_frame;
    double tgt_frame;
};

inline double map_source_to_target(double src_frame, const std::vector<FrameMapSegment>& map) {
    if (map.empty()) return src_frame;
    if (src_frame <= map.front().src_frame) return map.front().tgt_frame;
    // Strictly monotonic src_frame, so the owning segment is found by binary
    // search: i is the last segment with src_frame <= query.
    auto it = std::upper_bound(
        map.begin(), map.end(), src_frame,
        [](double q, const FrameMapSegment& s) { return q < s.src_frame; });
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
inline double map_target_to_source(double tgt_frame, const std::vector<FrameMapSegment>& map) {
    if (map.empty()) return tgt_frame;
    if (tgt_frame <= map.front().tgt_frame) return map.front().src_frame;
    // Strictly monotonic tgt_frame, so the owning segment is found by binary
    // search, mirroring map_source_to_target.
    auto it = std::upper_bound(
        map.begin(), map.end(), tgt_frame,
        [](double q, const FrameMapSegment& s) { return q < s.tgt_frame; });
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

// --- Map-file readers (header-only, dependency-free) -----------------------
// Inverses of the parser's write_frame_map / write_reset_map. They
// live here, not in the parser's map_output.cpp, so the engine-only
// warptempo_engine driver can read both artifacts while linking
// libwarptempo_engine alone (no parser archive). The formats are trivial
// whitespace-separated numeric text, specified at each writer in
// map_output.cpp; keep these in lockstep with those writers.
//
// The readers validate line shape only. Value-domain and ordering conformance
// is the writers' contract: build_maps and the trimmed-artifact derivation
// emit finite, non-negative, strictly ascending values with a first target of
// exactly zero by construction. Ordering is not left as an assumed
// precondition downstream: the engine refuses loudly at init on a
// non-monotonic frame map or an out-of-order phase reset list (the two
// validators in src/engine/engine.cpp), so a hand-edited artifact that breaks
// the ordering contract fails the render instead of producing silently wrong
// bytes. The readers themselves do not police it.
//
// .warpframemap: one "src_frame tgt_frame" line per segment (space-separated;
// the writer emits precise double breakpoints at up to 17 significant digits;
// a leading 0 0 anchor is present unless dropped at write). Blank /
// whitespace-only lines are skipped. Any malformed line (non-numeric, missing
// field, or trailing garbage) fails the whole read (std::nullopt), so a
// truncated or corrupt file never feeds the engine a partial map. A
// missing/unopenable file is also std::nullopt.
inline std::optional<std::vector<FrameMapSegment>>
read_frame_map(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    std::vector<FrameMapSegment> segs;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        std::istringstream ls(line);
        double s = 0.0, t = 0.0;
        if (!(ls >> s >> t)) return std::nullopt;
        std::string extra;
        if (ls >> extra) return std::nullopt;  // trailing garbage
        segs.push_back(FrameMapSegment{s, t});
    }
    return segs;
}

// .resetmap: one undisplaced source-frame integer per line, in file order.
// Blank / whitespace-only lines skipped; any malformed line (non-numeric,
// missing field, or trailing garbage) fails the whole read. The file carries
// only active resets (the writer's caller drops disabled markers), so there
// is no '#'/disabled syntax to handle. A missing/unopenable file is
// std::nullopt; an empty-but-readable file yields an empty list (a valid
// "no resets" render input).
inline std::optional<std::vector<int64_t>>
read_reset_map(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    std::vector<int64_t> frames;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        std::istringstream ls(line);
        long long f = 0;
        if (!(ls >> f)) return std::nullopt;
        std::string extra;
        if (ls >> extra) return std::nullopt;  // trailing garbage
        frames.push_back(static_cast<int64_t>(f));
    }
    return frames;
}
