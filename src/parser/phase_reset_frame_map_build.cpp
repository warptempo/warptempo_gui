#include "phase_reset_frame_map_build.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

std::expected<std::vector<double>, std::string> build_phase_reset_source_frames(
    const std::vector<PhaseResetMarker>& markers, long sample_rate,
    int64_t total_frames) {
    std::vector<double> out;
    out.reserve(markers.size());
    for (size_t i = 0; i < markers.size(); ++i) {
        const auto& m = markers[i];
        if (m.disabled) continue;
        double src_frame = m.time_seconds * static_cast<double>(sample_rate);
        if (src_frame > static_cast<double>(total_frames)) {
            return std::unexpected(
                "phase reset time exceeds source length at marker "
                + std::to_string(i));
        }
        out.push_back(src_frame);
    }
    return out;
}

std::optional<double> phase_reset_window_target_frame(
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

namespace {

// Maps one authored (undisplaced) phase-reset source position — an exact
// double source frame — into the engine's origin-centered query domain
// for a given render (full or trim-windowed). Returns std::nullopt
// when the reset does not apply to this render: outside the rendered
// window (the window-participation verdict above), or its lead-in
// anticipation (phase_reset_offset_samples) would fall before the
// window's own start. Dropping in that last case, rather than clamping
// forward, lets the natural PGHI heap propagation continue undisturbed
// through the opening stretch; a trim (or, on a full render, an authored
// reset) placed this close to the render's own start means the marker
// and its following segment, up to the next reset, are not the passage
// in focus for this render. This near-start dropzone is deliberately not
// gated at parse time or in the GUI -- it is render-relative and
// warp-dependent (the same authored marker can be droppable in one trim
// window and live in another), so no authoring-time check could be
// correct, and the silent per-render drop is the intended contract.
std::optional<double> phase_reset_engine_query_frame(
        double source_frame,
        const std::vector<WarpFrameMapSegment>& full_map,
        const std::vector<WarpFrameMapSegment>& engine_map,
        int64_t window_offset_samples,
        int64_t render_target_frames) {
    const std::optional<double> authored_target_window =
        phase_reset_window_target_frame(
            source_frame, full_map, window_offset_samples,
            render_target_frames);
    if (!authored_target_window) return std::nullopt;

    const double anticipated_target =
        *authored_target_window
        - static_cast<double>(phase_reset_offset_samples);
    if (anticipated_target < 0.0) return std::nullopt;

    // Derivation mapping:
    //   Authored source onset S
    //     -> full-map target onset T
    //     -> rendered-window target onset W = T - window_offset_samples
    //     -> target-domain anticipation D = W - phase_reset_offset_samples
    //     -> engine query frame E = map_target_to_source(D, engine_map) - N/2
    //
    // The final N/2 subtraction is a coordinate-domain correction. The engine
    // searches phase reset frames against source_frame_positions[m], which are
    // origin-centered analysis query frames:
    //   map_target_to_source(m * R_s) - N/2.
    // The helper returns the exact double in that same query domain; the
    // engine performs the quantization against its schedule.
    const double engine_source =
        map_target_to_source(anticipated_target, engine_map)
        - static_cast<double>(kN / 2);
    return engine_source;
}

}  // namespace

// The parser compiles authored positions for the locked production geometry:
// the engine core keeps N as a runtime parameter, but every driver hands it
// kN, and the artifact math here is pinned to kN (the N/2 query-origin
// correction) and to the phase_reset_offset_samples derived from kRs.
std::vector<double> derive_phase_reset_frame_map(
        const std::vector<double>& source_frames,
        const std::vector<WarpFrameMapSegment>& full_map,
        const std::vector<WarpFrameMapSegment>& engine_map,
        int64_t window_offset_samples,
        int64_t render_target_frames) {
    std::vector<double> out;
    out.reserve(source_frames.size());
    for (const double source_frame : source_frames) {
        if (auto f = phase_reset_engine_query_frame(
                source_frame, full_map, engine_map,
                window_offset_samples, render_target_frames)) {
            out.push_back(*f);
        }
    }
    return out;
}

std::vector<double> derive_phase_reset_frame_map(
        const std::vector<double>& source_frames,
        const std::vector<WarpFrameMapSegment>& deliverable_map) {
    if (deliverable_map.empty()) return {};
    const int64_t render_target_frames =
        static_cast<int64_t>(std::llrint(deliverable_map.back().tgt_frame));
    return derive_phase_reset_frame_map(
        source_frames, deliverable_map, deliverable_map,
        /*window_offset_samples=*/0, render_target_frames);
}
