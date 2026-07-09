#pragma once

#include "warpmarkers_parse.h"        // WarpMarker
#include "phaseresetmarkers_parse.h"  // PhaseResetMarker

#include <cstddef>
#include <string>
#include <vector>

// Raw-store marker defect enumeration across both columns.
//
// The rule: any two markers of the SAME column under one deepest-zoom pixel of
// time apart hardfail at render — disabled markers included, stacked phase
// resets included. The purpose is GUI pickability: it prevents states where the
// GUI cannot let the user mouse-pick one marker apart from another, and two
// markers closer than one deepest-zoom pixel share a pixel column that no zoom
// can split. It is not engine protection (the engine is indifferent to disabled
// stacks and same-hop resets). Cross-column and trim-vs-marker coincidence stay
// legal — they impair no picking, so they are not enumerated here.
//
// One deepest-zoom pixel of time: the coincidence window. Two same-column
// markers closer than this cannot be mouse-picked apart at any zoom, so they
// hardfail at render. The value is kZoomMsPerPixel[1] (src/gui/main.cpp,
// 0.625 ms/pixel, the deepest manual zoom) expressed in seconds; single-target
// software, so the authoring rule pins the number here as the source of truth
// and the zoom table comment points back.
inline constexpr double kCoincidenceWindowSeconds = 0.625 / 1000.0;
//
// enumerate_marker_store_defects is the shared surface consumed by the CLI
// listing (which prints every defect) and, later, the GUI modal walk. The
// render-boundary owners remain the authoritative refusers beneath it:
// resolve_warp_markers_for_render (via validate_first_marker_render_grammar),
// build_warp_frame_map, build_phase_reset_source_frames, and
// validate_trim_frames. The past-end tests and the first-marker call
// deliberately mirror those owners' exact comparisons — the past-end tests
// mirror each column's build check, the first-marker call is the same
// validator — so a state this enumerator reports clean on those predicates is a
// state the owners accept, and divergence there is a bug. The coincidence
// predicate is the deliberate exception: it is WIDER than the owners' sub-frame
// spacing refusals (kCoincidenceWindowSeconds is one deepest-zoom pixel of
// time, far wider than one source frame), and the width is the safe direction —
// a raw-clean store still renders (the wider raw rule implies the owners'
// narrower sub-frame checks pass), while some renderable stores now hardfail by
// design, because the rule guards mouse-pickability, not the engine.

enum class MarkerDefectKind {
    FirstMarkerGrammar,
    CoincidentGroup,
    PastEof,
    DanglingLabelRef
};

struct MarkerDefect {
    MarkerDefectKind    kind;
    char                column;        // 'W' warp, 'P' phase reset
    double              time_seconds;  // chronological anchor
    std::vector<size_t> indices;       // store indices, ascending
    std::string         message;       // display string shared by the CLI
                                       // stderr lines and the GUI modal text
};

// Scan the raw authored marker stores (both columns) and return every
// render-invalidating authoring defect as a structured, chronologically sorted
// list. The input lists are time-sorted (the GUI store is sorted at rest and
// the file parsers hard-fail decreasing times). Messages are lowercase,
// matching the parser error strings; timestamps go through format_timestamp.
std::vector<MarkerDefect> enumerate_marker_store_defects(
    const std::vector<WarpMarker>&       warp_markers,
    const std::vector<PhaseResetMarker>& phase_resets,
    long sample_rate, long total_frames);
