#include "phase_reset_frame_map_build.h"

#include "engine/engine_geometry.h"  // kN

#include <cstddef>
#include <string>
#include <vector>

std::expected<std::vector<double>, std::string> build_phase_reset_source_frames(
    const std::vector<PhaseResetMarker>& markers, int64_t total_frames) {
    std::vector<double> out;
    out.reserve(markers.size());
    for (size_t i = 0; i < markers.size(); ++i) {
        const auto& m = markers[i];
        if (m.disabled) continue;
        // The authored position is a whole source frame (int64_t); it widens
        // exactly into the double intermediate the derivation consumes.
        const double src_frame = static_cast<double>(m.time_frame);
        if (src_frame > static_cast<double>(total_frames)) {
            return std::unexpected(
                "phase reset time exceeds source length at marker "
                + std::to_string(i));
        }
        // Sub-frame refusal, mirroring the warp column's shape
        // (build_warp_frame_map's src_frame - src_f_prev < 1.0 check). The
        // raw-store rule (marker_store_validate.h) prohibits same-column markers
        // closer than one deepest-zoom pixel of time on both columns — a wider
        // window than this sub-frame check, since the GUI cannot let the user
        // mouse-pick markers on one pixel column apart. This sub-frame
        // in-function check is the breach backstop for hand-edited input
        // reaching the build directly, past the marker parser and the store's
        // enumerator. Input marker times are non-decreasing (the load parser
        // rejects decreasing times; the GUI store is time-sorted), so a pair
        // this close refuses here; the surviving output stays strictly
        // increasing by construction, which the derivation (a constant N/2
        // shift of the input plus participation drops) preserves, so the
        // engine's strict-ascent init validator holds.
        if (!out.empty() && src_frame - out.back() < 1.0) {
            return std::unexpected(
                "phase reset markers under one source frame apart at marker "
                + std::to_string(i));
        }
        out.push_back(src_frame);
    }
    return out;
}

// The parser compiles authored positions for the locked production geometry:
// the engine core keeps N as a runtime parameter, but every driver hands it
// kN, and the artifact math here is pinned to kN (the N/2 query-origin
// correction).
//
// Derivation mapping:
//   Authored source onset S
//     -> deliverable-map target image T = map_source_to_target(S, map),
//        used only for the render-end participation verdict (drop when T
//        lands at or past the map's final anchor target)
//     -> engine query frame E = S - N/2
//
// The N/2 subtraction is a coordinate-domain correction, not a displacement
// of the reset. Canonical PGHI (Prusa & Holighaus, "Phase Vocoder Done
// Right", docs/references/ltfatnote050.pdf) defines the discrete STFT with
// a real-valued analysis window concentrated around the ORIGIN, so an
// analysis position is the window's CENTER. The engine searches phase reset
// frames against source_frame_positions[m], which are origin-centered
// analysis query frames:
//   map_target_to_source(m * R_s) - N/2.
// A reset that should fire with its analysis window centered at authored
// source frame S therefore has query position S - N/2: the authored onset
// sits at the Hann window center by construction. The list returned here
// holds exact doubles in that same query domain; the engine performs the
// quantization against its schedule.
//
// With no target-domain displacement, the inverse map of a position's
// forward image is the position itself, so S - N/2 is emitted directly —
// exact, with no floating-point map round-trip; the forward map call
// serves only the render-end verdict. An authored reset at frame 0 derives
// to query -N/2, exactly the engine's first analysis frame query position:
// legal and inert, since the first frame seeds synthesis phase from
// analysis phase anyway. Strict ascent of the output holds trivially — the
// input source frames are strictly increasing and the emission is a
// constant shift of them; the render-end drop only shortens the list.
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
        out.push_back(source_frame - static_cast<double>(kN / 2));
    }
    return out;
}
