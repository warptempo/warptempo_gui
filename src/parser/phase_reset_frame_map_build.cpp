#include "phase_reset_frame_map_build.h"

#include <cstddef>
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
        // Adjacent exact duplicates collapse to one. Input marker times are
        // non-decreasing (the load parser rejects decreasing times; the GUI
        // store is time-sorted), and two enabled markers with exactly equal
        // times are one authored event, so the collapse makes this output
        // strictly increasing — which the derivation chain (strictly
        // monotone map interpolation, constant subtractions, participation
        // drops) preserves, so the engine's strict-ascent init validator
        // holds by construction. Exact equality only, no epsilon: two
        // distinct doubles remain two resets no matter how close — the
        // engine consumes same-hop placements together by design.
        if (!out.empty() && src_frame == out.back()) continue;
        out.push_back(src_frame);
    }
    return out;
}

// The parser compiles authored positions for the locked production geometry:
// the engine core keeps N as a runtime parameter, but every driver hands it
// kN, and the artifact math here is pinned to kN (the N/2 query-origin
// correction) and to the phase_reset_offset_samples derived from kRs.
//
// Derivation mapping:
//   Authored source onset S
//     -> deliverable-map target onset T (bounded by the map's final anchor)
//     -> target-domain anticipation D = T - phase_reset_offset_samples
//     -> engine query frame E = map_target_to_source(D, map) - N/2
//
// The final N/2 subtraction is a coordinate-domain correction. The engine
// searches phase reset frames against source_frame_positions[m], which are
// origin-centered analysis query frames:
//   map_target_to_source(m * R_s) - N/2.
// The list returned here holds exact doubles in that same query domain; the
// engine performs the quantization against its schedule.
//
// The lead-in dropzone: a reset whose anticipation D would fall before the
// render's own start is dropped, not clamped forward. Dropping lets the
// natural PGHI heap propagation continue undisturbed through the opening
// stretch; an authored reset this close to the render's start means the
// marker and its following segment, up to the next reset, are not the
// passage in focus. The dropzone is deliberately not gated at parse time or
// in the GUI — it is render-relative and warp-dependent, so no
// authoring-time check could be correct, and the silent per-render drop is
// the intended contract. (map_source_to_target clamps a pre-map source
// position to the first pair's target rather than going negative, so the
// dropzone is also what removes any reset ahead of the map's origin.)
std::vector<double> derive_phase_reset_frame_map(
        const std::vector<double>& source_frames,
        const std::vector<WarpFrameMapSegment>& deliverable_map) {
    if (deliverable_map.empty()) return {};
    // Bound: the deliverable map's own final anchor target, compared exactly
    // in the double target domain — a reset at source EOF sits exactly on it
    // and drops (a point event beyond the deliverable's last sample).
    // Quantization to the engine's integer output length never enters this
    // verdict.
    const double render_target_end = deliverable_map.back().tgt_frame;
    std::vector<double> out;
    out.reserve(source_frames.size());
    for (const double source_frame : source_frames) {
        const double authored_target =
            map_source_to_target(source_frame, deliverable_map);
        if (authored_target >= render_target_end) continue;
        const double anticipated_target =
            authored_target - static_cast<double>(phase_reset_offset_samples);
        if (anticipated_target < 0.0) continue;   // lead-in dropzone
        const double engine_source =
            map_target_to_source(anticipated_target, deliverable_map)
            - static_cast<double>(kN / 2);
        out.push_back(engine_source);
    }
    return out;
}
