#include "phase_reset_frame_map_build.h"

#include "engine/engine_geometry.h"  // kN
#include "time_format.h"             // format_timestamp

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

// Terminal message strings in this file carry sentence-initial capitals
// (architect approval 2026-08-02, the terminal capitalization pass —
// text-only, otherwise byte-identical output). The refusal below is a
// standalone message after the CLI's program-name prefix, so it takes the
// capital at its definition; it also appears mid-message in the GUI render
// pipeline's "Render error: %s", the accepted cost recorded for the six
// GUI-painted refusals (warp_frame_map_build.cpp).
std::expected<std::vector<double>, std::string> build_phase_reset_source_frames(
    const std::vector<PhaseResetMarker>& markers, long sample_rate,
    int64_t total_frames) {
    // sample_rate is display-only: it renders the collapse line's timestamp
    // as format_timestamp(frame / sr).
    const double sr_d = static_cast<double>(sample_rate);
    std::vector<double> out;
    out.reserve(markers.size());
    // Whether out.back()'s timestamp has already printed its collapse line —
    // a coincident group of any size collapses with ONE line.
    bool reported_back = false;
    for (size_t i = 0; i < markers.size(); ++i) {
        const auto& m = markers[i];
        if (m.disabled) continue;
        // The authored position is a whole source frame (int64_t); it widens
        // exactly into the double intermediate the derivation consumes.
        // The wall is total - 1, the same compare as the gesture sites and
        // the load guard: both marker columns share the warp column's wall
        // (a reset in the last source frame has nothing left to re-ground,
        // and total-1 keeps every marker inside the playhead's [0, total-1]
        // domain). A reset at exactly total frames — previously legal — is
        // adversarial and refuses here as the breach backstop.
        const double src_frame = static_cast<double>(m.time_frame);
        if (src_frame > static_cast<double>(total_frames - 1)) {
            return std::unexpected(
                "Phase reset time exceeds source length at marker "
                + std::to_string(i));
        }
        // Exact-coincidence collapse, the phase-reset sibling of the warp
        // resolver's stage-2 normalization: authored positions are whole
        // source frames, so sub-frame spacing IS exact equality, and
        // equal-frame enabled resets are one event — the group collapses to
        // one reset with one stderr line per collapsed timestamp, printed
        // on every resolve. Input marker times are non-decreasing (the load
        // parser rejects decreasing times; the GUI store is time-sorted),
        // so equal frames are adjacent and the surviving output stays
        // strictly increasing by construction, which the derivation (a
        // constant N/2 shift of the input plus participation drops)
        // preserves, so the engine's strict-ascent init validator holds.
        if (!out.empty() && src_frame == out.back()) {
            if (!reported_back) {
                std::fprintf(stderr,
                    "Coincident phase reset markers at %s collapse to one reset\n",
                    format_timestamp(src_frame / sr_d).c_str());
                reported_back = true;
            }
            continue;
        }
        out.push_back(src_frame);
        reported_back = false;
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
// legal and inert. THE INERT RANGE IS WIDER THAN FRAME 0, AND THE REASON IS
// NOT THE ONE THIS COMMENT GAVE (recorded 2026-09-02 from the truthfulness
// deep dive's item D, measured; architect approval 2026-09-02, comment-only):
// the engine's first TWO schedule frames read from before sample 0 and its
// analysis guard leaves such a frame WHOLE-ZERO, so frames 0 and 1 seed from
// ZEROS rather than from analysis phase, and every reset authored before the
// third schedule frame's centre — source frame 2048 at unity tempo,
// map_target_to_source(N/2) in general — places on one of them and seeds
// nothing at all. The engine-side record, the history and the accepted cost
// are at stft_container.h's output-timing contract and its analysis guard.
// Strict ascent of the output holds trivially — the
// input source frames are strictly increasing and the emission is a
// constant shift of them; the render-end drop only shortens the list.
std::vector<double> derive_phase_reset_frame_map(
        const std::vector<double>& source_frames,
        const std::vector<WarpFrameMapSegment>& deliverable_map) {
    if (deliverable_map.empty()) return {};
    // Bound: the deliverable map's own final anchor target, compared exactly
    // in the double target domain — a reset whose target image lands at or
    // past the deliverable's last sample has nothing to protect and drops.
    // (Program-written input can no longer reach the bound itself: both
    // marker columns wall at total - 1, so only a hand-edited artifact puts
    // a reset on the final anchor.) Quantization to the engine's integer
    // output length never enters this verdict.
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
