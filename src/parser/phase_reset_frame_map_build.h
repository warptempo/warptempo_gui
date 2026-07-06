#pragma once

#include "phaseresetmarkers_parse.h"  // PhaseResetMarker

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

// Pure parser-domain assembly: phase-reset markers -> absolute source-frame
// positions. Drops disabled markers; converts time_seconds to an exact double
// source-frame position (time * sample_rate, no rounding), matching the
// warp-marker time->frame convention in build_warp_frame_map. Refuses an
// enabled reset authored past the source end (strictly greater than
// total_frames; equal is allowed), the producer-side validation layer parallel
// to build_warp_frame_map's past-end check on the warp axis: a phase-reset
// sidecar sitting beside a shorter or replaced source fails loudly here
// instead of the reset silently falling out of dispatch's window drop test.
// Disabled markers are skipped before the check — only resolved markers are
// validated, as in build_warp_frame_map —
// so a disabled past-end reset stays loadable and inert. No ordering check
// lives here: the strict marker parser owns ordering at load, and the
// engine's strict-ascent hardfail covers raw phaseresetframemap inputs that
// bypass the marker parser. The result is the undisplaced authored
// source-frame list used for render-view display, phaseresetframemap
// output, and target-domain dispatch placement; the dispatch mapping
// (phase_reset_dispatch.h) stays in doubles, and quantization to the
// engine's integer query schedule happens inside the engine at placement
// time. There is no resolver cascade sibling to
// resolve_warp_markers_for_render because phase reset markers carry no
// inheritance, labels, or references — a timestamp and a disabled flag are
// the whole grammar.
std::expected<std::vector<double>, std::string> build_phase_reset_frame_map(
    const std::vector<PhaseResetMarker>& markers, long sample_rate,
    int64_t total_frames);
