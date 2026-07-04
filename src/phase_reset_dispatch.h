#pragma once

#include "frame_map.h"

#include <cmath>
#include <cstdint>
#include <vector>

struct PhaseResetDispatchFrame {
    int64_t authored_source_frame = 0;
    double  authored_target_frame = 0.0;
    double  dispatch_target_frame = 0.0;
    int64_t engine_source_frame   = 0;
    bool    clamped_to_start      = false;
};

inline bool phase_reset_dispatch_frame_target_domain(
        int64_t source_frame,
        const std::vector<FrameMapSegment>& full_map,
        const std::vector<FrameMapSegment>& engine_map,
        int64_t window_offset_samples,
        int64_t render_target_frames,
        int64_t target_offset_samples,
        int64_t engine_query_origin_offset_samples,
        PhaseResetDispatchFrame& out) {
    if (full_map.empty()) return false;
    if (engine_map.empty()) return false;
    if (render_target_frames <= 0) return false;

    const double authored_target_full =
        map_source_to_target(static_cast<double>(source_frame), full_map);
    const double authored_target_window =
        authored_target_full - static_cast<double>(window_offset_samples);

    if (authored_target_window < 0.0) return false;
    if (authored_target_window >= static_cast<double>(render_target_frames)) {
        return false;
    }

    double dispatch_target =
        authored_target_window - static_cast<double>(target_offset_samples);
    bool clamped = false;
    if (dispatch_target < 0.0) {
        dispatch_target = 0.0;
        clamped = true;
    }

    // Dispatch mapping:
    //   Authored source onset S
    //     -> full-map target onset T
    //     -> rendered-window target onset W = T - window_offset_samples
    //     -> target-domain anticipation D = max(0, W - phase_reset_offset_samples)
    //        where phase_reset_offset_samples is passed as target_offset_samples
    //     -> engine query frame E = map_target_to_source(D, engine_map) - N/2
    //
    // The final N/2 subtraction is a coordinate-domain correction. The engine
    // searches phase reset frames against source_frame_positions[m], which are
    // origin-centered analysis query frames:
    //   map_target_to_source(m * R_s) - N/2.
    // The helper must return frames in that same query domain.
    const double engine_source =
        map_target_to_source(dispatch_target, engine_map)
        - static_cast<double>(engine_query_origin_offset_samples);
    const int64_t engine_frame = static_cast<int64_t>(std::llrint(engine_source));

    out.authored_source_frame = source_frame;
    out.authored_target_frame = authored_target_window;
    out.dispatch_target_frame = dispatch_target;
    out.engine_source_frame   = engine_frame;
    out.clamped_to_start      = clamped;
    return true;
}

inline std::vector<int64_t> phase_reset_dispatch_frames_target_domain(
        const std::vector<int64_t>& source_frames,
        const std::vector<FrameMapSegment>& full_map,
        const std::vector<FrameMapSegment>& engine_map,
        int64_t window_offset_samples,
        int64_t render_target_frames,
        int64_t target_offset_samples,
        int64_t engine_query_origin_offset_samples,
        std::vector<PhaseResetDispatchFrame>* placements = nullptr) {
    std::vector<int64_t> out;
    out.reserve(source_frames.size());
    if (placements) placements->clear();

    for (const int64_t source_frame : source_frames) {
        PhaseResetDispatchFrame p;
        if (!phase_reset_dispatch_frame_target_domain(
                source_frame, full_map, engine_map,
                window_offset_samples, render_target_frames,
                target_offset_samples,
                engine_query_origin_offset_samples, p)) {
            continue;
        }
        out.push_back(p.engine_source_frame);
        if (placements) placements->push_back(p);
    }
    return out;
}
