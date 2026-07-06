#pragma once

#include "warp_frame_map.h"

#include <cstdint>
#include <optional>
#include <vector>

// Phase-reset dispatch: authored (undisplaced) source positions -> the
// engine's origin-centered query domain, for a given render (full or
// trim-windowed).
//
// These helpers live in this root header rather than the parser library
// because warptempo_engine dispatches phase resets while linking the engine
// archive alone, with no parser. At the window-restriction stage, the warp
// axis's sibling is the parser-owned slicer
// (slice_warp_frame_map_to_trim_window, parser-owned because no parser-less
// driver slices — trim is not an engine-CLI concept) against
// phase_reset_window_target_frame below: the warp slicer coalesces
// out-of-window breakpoints into boundary anchors because a map is a
// connected piecewise function that must stay defined over every output
// sample, while the phase-reset window verdict drops non-participating
// points because point events have nothing to coalesce into. One stage
// later, the engine-handoff siblings assign_engine_warp_frame_map and
// assign_engine_phase_reset_frame_map are already parallel; the dispatch
// conversion below, one stage further in, has no warp sibling at all,
// because the warp frame map is already the engine's input domain and needs
// only windowing, while a phase-reset position alone crosses three domains —
// authored source, to window target, to engine query.

// Window-participation verdict for one authored (undisplaced) phase-reset
// source position — an exact double source frame. Maps it through the full
// map to its target image and re-anchors to the rendered window's origin.
// Returns the window-domain target frame W, or std::nullopt when the reset
// does not participate in this render: W negative (before the window — the
// instant precedes the deliverable's first sample) or at or past
// render_target_frames (past the emit cap, beyond the deliverable's last
// sample). Shared by the engine dispatch below and the render-view display
// sidecar writer, so display participation and engine dispatch converge on
// the same window-bounds verdict.
inline std::optional<double> phase_reset_window_target_frame(
        double source_frame,
        const std::vector<WarpFrameMapSegment>& full_map,
        int64_t window_offset_samples,
        int64_t render_target_frames) {
    const double authored_target_full =
        map_source_to_target(source_frame, full_map);
    const double authored_target_window =
        authored_target_full - static_cast<double>(window_offset_samples);

    if (authored_target_window < 0.0) return std::nullopt;
    if (authored_target_window >= static_cast<double>(render_target_frames)) {
        return std::nullopt;
    }
    return authored_target_window;
}

// Maps one authored (undisplaced) phase-reset source position — an exact
// double source frame — into the engine's origin-centered query domain
// for a given render (full or trim-windowed). Returns std::nullopt
// when the reset does not apply to this render: outside the rendered
// window (the window-participation verdict above), or its lead-in
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
inline std::optional<double> phase_reset_dispatch_frame_target_domain(
        double source_frame,
        const std::vector<WarpFrameMapSegment>& full_map,
        const std::vector<WarpFrameMapSegment>& engine_map,
        int64_t window_offset_samples,
        int64_t render_target_frames,
        int64_t target_offset_samples,
        int64_t engine_query_origin_offset_samples) {
    const std::optional<double> authored_target_window =
        phase_reset_window_target_frame(
            source_frame, full_map, window_offset_samples,
            render_target_frames);
    if (!authored_target_window) return std::nullopt;

    const double dispatch_target =
        *authored_target_window - static_cast<double>(target_offset_samples);
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
    // The helper returns the exact double in that same query domain; the
    // engine performs the quantization against its schedule.
    const double engine_source =
        map_target_to_source(dispatch_target, engine_map)
        - static_cast<double>(engine_query_origin_offset_samples);
    return engine_source;
}

inline std::vector<double> phase_reset_dispatch_frames_target_domain(
        const std::vector<double>& source_frames,
        const std::vector<WarpFrameMapSegment>& full_map,
        const std::vector<WarpFrameMapSegment>& engine_map,
        int64_t window_offset_samples,
        int64_t render_target_frames,
        int64_t target_offset_samples,
        int64_t engine_query_origin_offset_samples) {
    std::vector<double> out;
    out.reserve(source_frames.size());
    for (const double source_frame : source_frames) {
        if (auto f = phase_reset_dispatch_frame_target_domain(
                source_frame, full_map, engine_map,
                window_offset_samples, render_target_frames,
                target_offset_samples, engine_query_origin_offset_samples)) {
            out.push_back(*f);
        }
    }
    return out;
}
