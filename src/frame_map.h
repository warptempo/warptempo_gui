#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

struct FrameMapSegment {
    size_t src_frame;
    size_t tgt_frame;
};

inline double map_source_to_target(size_t src_frame, const std::vector<FrameMapSegment>& map) {
    if (map.empty()) return static_cast<double>(src_frame);
    if (src_frame <= map.front().src_frame) return map.front().tgt_frame;
    // Strictly monotonic src_frame (engine-validated; GUI builder emits
    // strictly increasing segments), so the owning segment is found by
    // binary search: i is the last segment with src_frame <= query.
    auto it = std::upper_bound(
        map.begin(), map.end(), src_frame,
        [](size_t q, const FrameMapSegment& s) { return q < s.src_frame; });
    const size_t i = static_cast<size_t>(it - map.begin()) - 1;
    if (i < map.size() - 1) {
        double src_dur = static_cast<double>(map[i+1].src_frame - map[i].src_frame);
        double tgt_dur = static_cast<double>(map[i+1].tgt_frame - map[i].tgt_frame);
        double offset = static_cast<double>(src_frame - map[i].src_frame);
        return map[i].tgt_frame + (offset * (tgt_dur / src_dur));
    }
    const auto& last = map.back();
    return last.tgt_frame + (src_frame - last.src_frame);
}

// Inverse of map_source_to_target: piecewise-linear interpolation over
// the same segment list, mapping a target-frame query back to its
// source-frame position. Used by the GUI's target view to translate
// per-column target-frame ranges into source-frame ranges for the
// shared waveform paint. Symmetric edge cases: clamp to the first
// segment for queries before the frame_map's tgt start; identity past
// the last segment; empty map degenerates to identity. The owning
// segment is found by binary search over the strictly monotonic tgt axis.
inline double map_target_to_source(size_t tgt_frame, const std::vector<FrameMapSegment>& map) {
    if (map.empty()) return static_cast<double>(tgt_frame);
    if (tgt_frame <= map.front().tgt_frame) return map.front().src_frame;
    auto it = std::upper_bound(
        map.begin(), map.end(), tgt_frame,
        [](size_t q, const FrameMapSegment& s) { return q < s.tgt_frame; });
    const size_t i = static_cast<size_t>(it - map.begin()) - 1;
    if (i < map.size() - 1) {
        double src_dur = static_cast<double>(map[i+1].src_frame - map[i].src_frame);
        double tgt_dur = static_cast<double>(map[i+1].tgt_frame - map[i].tgt_frame);
        double offset = static_cast<double>(tgt_frame - map[i].tgt_frame);
        return map[i].src_frame + (offset * (src_dur / tgt_dur));
    }
    const auto& last = map.back();
    return last.src_frame + (tgt_frame - last.tgt_frame);
}
