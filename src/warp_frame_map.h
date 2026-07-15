#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

// target_total_frames_for_map is the single owner of the persisted-'T'-domain
// total rule, shared by the GUI target-view cache (which also backs the GUI's
// persisted-view load check), the CLI persisted-view load check, and the
// render-entry `.settings` write clamp, so the rounding and fallback can never
// drift between the products. An empty map returns the source total
// unchanged; otherwise the source total (negative clamped to zero) is mapped
// into the target domain, banker's-rounded, and returned when strictly
// positive, else the source total is the fallback.
inline int64_t target_total_frames_for_map(
    int64_t total_frames, const std::vector<WarpFrameMapSegment>& map) {
    if (map.empty()) return total_frames;
    const double src = static_cast<double>(total_frames < 0 ? 0 : total_frames);
    const int64_t tt =
        static_cast<int64_t>(std::nearbyint(map_source_to_target(src, map)));
    return tt > 0 ? tt : total_frames;
}

// The map artifact WRITERS live in the parser (map_output.cpp), which also
// specifies the on-disk format. No in-tree target reads the artifacts back
// — the engine consumes the in-memory maps directly on every product path
// — so there is no reader here; external consumers parse the trivial
// whitespace-separated numeric text against the writer's contract.
