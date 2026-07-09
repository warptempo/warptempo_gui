#pragma once

#include "warpmarkers_parse.h"        // WarpMarker
#include "phaseresetmarkers_parse.h"  // PhaseResetMarker

#include <cstddef>
#include <string>
#include <vector>

// Raw-store marker defect enumeration across both columns.
//
// The rule: any two markers of the SAME column under one source frame apart
// hardfail at render — disabled markers included, stacked phase resets
// included. The purpose is GUI pickability: it prevents states where the GUI
// cannot let the user mouse-pick one marker apart from another. It is not
// engine protection (the engine is indifferent to disabled stacks and
// same-hop resets). Cross-column and trim-vs-marker coincidence stay legal —
// they impair no picking, so they are not enumerated here.
//
// enumerate_marker_store_defects is the shared surface consumed by the CLI
// listing (which prints every defect) and, later, the GUI modal walk. The
// render-boundary owners remain the authoritative refusers beneath it:
// resolve_warp_markers_for_render (via validate_first_marker_render_grammar),
// build_warp_frame_map, build_phase_reset_source_frames, and
// validate_trim_frames. This enumerator's predicates deliberately mirror those
// owners' exact comparisons — the sub-frame `* sample_rate < 1.0` spacing test
// matches build_warp_frame_map's and build_phase_reset_source_frames' shape,
// the past-end tests mirror each column's build check — so that a state this
// enumerator reports clean is a state the owners will render. Divergence here
// is a bug.

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
