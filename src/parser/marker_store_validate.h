#pragma once

#include "warpmarkers_parse.h"        // WarpMarker
#include "phaseresetmarkers_parse.h"  // PhaseResetMarker
#include "settings_file.h"            // SettingsTrim

#include <cstddef>
#include <cstdint>
#include <optional>
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
// and the zoom table comment points back. Domain split (deliberate): the
// CONSTANT stays a seconds value because its purpose is pixel pickability and
// zoom is defined in ms per pixel; the COMPARISON in
// marker_store_validate.cpp converts once to frames
// (window_frames = kCoincidenceWindowSeconds * sample_rate) and compares the
// exact int64 frame difference (widened to double) against it, so no seconds
// representation ever touches the stores.
inline constexpr double kCoincidenceWindowSeconds = 0.625 / 1000.0;
//
// enumerate_marker_store_defects is the shared surface consumed by the CLI
// listing (which prints every defect) and the GUI modal walk. The
// render-boundary owners remain the authoritative refusers beneath it:
// resolve_warp_markers_for_render (via validate_first_marker_render_grammar
// and validate_pass_inheritance_source), build_warp_frame_map,
// build_phase_reset_source_frames, and validate_trim_frames. The first-marker
// and pass-after-label-ref calls deliberately mirror those owners' exact
// comparisons — they are the same validators — so a state this enumerator
// reports clean on them is a state the owners accept, and divergence there is
// a bug.
//
// Past-EOF is NOT an enumerated defect: a marker past its column's wall is
// adversarial input — the GUI's gesture walls (marker EOF walls, per-bound trim
// walls) make it uncommittable, and a marker file applies only to the audio it
// was authored against, so a past-EOF position means the audio was swapped
// outside the GUI. It is hard-failed at the load boundary (GUI file_loader /
// CLI, disabled markers included) like a corrupt audio file. The enumerator
// therefore only ever sees stores where every position is inside its wall; the
// render-boundary EOF checks in build_warp_frame_map and
// build_phase_reset_source_frames remain as breach backstops for hand-edited
// artifacts.
//
// The coincidence predicate is the deliberate exception: it is WIDER than the
// owners' sub-frame spacing refusals (kCoincidenceWindowSeconds is one
// deepest-zoom pixel of time, far wider than one source frame), and the width
// is the safe direction — a raw-clean store still renders (the wider raw rule
// implies the owners' narrower sub-frame checks pass), while some renderable
// stores now hardfail by design, because the rule guards mouse-pickability, not
// the engine.

enum class MarkerDefectKind {
    FirstMarkerGrammar,
    CoincidentGroup,
    DanglingLabelRef,
    PassAfterLabelRef
};

struct MarkerDefect {
    MarkerDefectKind    kind;
    char                column;      // 'W' warp, 'P' phase reset
    int64_t             time_frame;  // chronological anchor (source frames)
    std::vector<size_t> indices;     // store indices, ascending
    std::string         message;     // display string shared by the CLI
                                     // stderr lines and the GUI modal text
};

// Scan the raw authored marker stores (both columns) and return every
// render-invalidating authoring defect as a structured, chronologically sorted
// list. The input lists are time-sorted (the GUI store is sorted at rest and
// the file parsers hard-fail decreasing times). Messages are lowercase,
// matching the parser error strings; embedded times are display renderings
// via format_timestamp(frame / sample_rate). `sample_rate` converts the
// coincidence window to frames and formats the message timestamps; it never
// converts the stored positions themselves.
std::vector<MarkerDefect> enumerate_marker_store_defects(
    const std::vector<WarpMarker>&       warp_markers,
    const std::vector<PhaseResetMarker>& phase_resets,
    long sample_rate);

// The adversarial past-EOF load guard (see the past-EOF paragraph above),
// shared by GUI file_loader and CLI so the six wall checks can never
// drift: warp markers (wall total-1), phase reset markers (wall total
// exactly), then tab A trim begin (wall total-1), tab A end (wall total),
// tab B begin, tab B end — first offender only, disabled markers
// included. Every check is a plain integer compare against the stored
// value — literally the same comparison the gesture walls apply, no
// rounding anywhere — so a legal at-the-wall position always passes.
// Returns the first violation's detail string (embedded times are display
// renderings via format_timestamp(frame / sample_rate)); callers add
// their own product prefix. std::nullopt when every position is inside
// its wall.
std::optional<std::string> first_past_eof_wall_defect(
    const std::vector<WarpMarker>&       warp_markers,
    const std::vector<PhaseResetMarker>& phase_resets,
    const SettingsTrim& tab_a_trim,
    const SettingsTrim& tab_b_trim,
    int64_t total_frames,
    long    sample_rate);

// Persisted view-scratch range guard, shared by GUI file_loader and CLI so
// the four wall checks can never drift: tab A viewport start (refuses at or
// past the domain total — a viewport starting on the total's frame shows
// nothing), tab A playhead (refuses strictly past the domain total — the
// playhead may rest on the end exactly), then tab B likewise — first
// offender only, absent keys skipped.
//
// Persisted viewport/playhead positions live in the persisted
// active_audio_view's domain: while 'T' the live view fields carry
// target-frame values and the S/T toggle translates BOTH tabs together, so
// the domain is global to the file and a Ctrl+S from target view under a
// slowing map legitimately writes positions past the SOURCE total. The
// caller therefore selects and passes the domain total: 'S' or absent is
// the source total; 'T' is the deformed target total of the warp frame map
// built from the loaded markers and scale (each product computes it with
// the same math its runtime clamps use). When 'T' is persisted but the map
// cannot build — the marker store carries walkable defects, which load
// intact by design — the caller SKIPS this check entirely: there is no
// target total to wall against, and the runtime viewport clamps own the
// values then. Returns the first violation's detail string; callers add
// their own product prefix. std::nullopt when every present position is
// inside the domain.
std::optional<std::string> first_view_range_defect(
    const SettingsFileTab& tab_a,
    const SettingsFileTab& tab_b,
    int64_t domain_total_frames);
