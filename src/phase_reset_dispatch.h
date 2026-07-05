#pragma once

#include "frame_map.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

// Maps one authored (undisplaced) phase-reset source frame into the
// engine's origin-centered query domain for a given render (full or
// trim-windowed). Returns std::nullopt when the reset does not apply to
// this render: authored outside the rendered window, or its lead-in
// anticipation (target_offset_samples, i.e. phase_reset_offset_samples)
// would fall before the window's own start. Dropping in that last case,
// rather than clamping forward, lets the natural PGHI heap propagation
// continue undisturbed through the opening stretch; a trim (or, on a
// full render, an authored reset) placed this close to the render's own
// start means the marker and its following segment, up to the next
// reset, are not the passage in focus for this render. This near-start
// dropzone is deliberately not gated at parse time or in the GUI -- it
// is render-relative and warp-dependent (the same authored marker can be
// droppable in one trim window and live in another), so no
// authoring-time check could be correct, and the silent per-render drop
// is the intended contract.
inline std::optional<int64_t> phase_reset_dispatch_frame_target_domain(
        int64_t source_frame,
        const std::vector<FrameMapSegment>& full_map,
        const std::vector<FrameMapSegment>& engine_map,
        int64_t window_offset_samples,
        int64_t render_target_frames,
        int64_t target_offset_samples,
        int64_t engine_query_origin_offset_samples) {
    const double authored_target_full =
        map_source_to_target(static_cast<double>(source_frame), full_map);
    const double authored_target_window =
        authored_target_full - static_cast<double>(window_offset_samples);

    if (authored_target_window < 0.0) return std::nullopt;
    if (authored_target_window >= static_cast<double>(render_target_frames)) {
        return std::nullopt;
    }

    const double dispatch_target =
        authored_target_window - static_cast<double>(target_offset_samples);
    if (dispatch_target < 0.0) return std::nullopt;

    // Dispatch mapping:
    //   Authored source onset S
    //     -> full-map target onset T
    //     -> rendered-window target onset W = T - window_offset_samples
    //     -> target-domain anticipation D = W - phase_reset_offset_samples
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
    return static_cast<int64_t>(std::llrint(engine_source));
}

inline std::vector<int64_t> phase_reset_dispatch_frames_target_domain(
        const std::vector<int64_t>& source_frames,
        const std::vector<FrameMapSegment>& full_map,
        const std::vector<FrameMapSegment>& engine_map,
        int64_t window_offset_samples,
        int64_t render_target_frames,
        int64_t target_offset_samples,
        int64_t engine_query_origin_offset_samples) {
    std::vector<int64_t> out;
    out.reserve(source_frames.size());
    for (const int64_t source_frame : source_frames) {
        if (auto f = phase_reset_dispatch_frame_target_domain(
                source_frame, full_map, engine_map,
                window_offset_samples, render_target_frames,
                target_offset_samples, engine_query_origin_offset_samples)) {
            out.push_back(*f);
        }
    }
    return out;
}
