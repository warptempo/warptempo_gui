#pragma once

#include "phaseresetmarkers_parse.h"  // PhaseResetMarker
#include "warp_frame_map.h"           // WarpFrameMapSegment
#include "engine/engine_geometry.h"   // kN, kRs

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

// Pure parser-domain assembly: phase-reset markers -> absolute source-frame
// positions. Drops disabled markers; converts time_seconds to an exact double
// source-frame position (time * sample_rate, no rounding), matching the
// warp-marker time->frame convention in build_warp_frame_map. Total — no
// past-end check, no failure mode. Non-adversarial GUI usage can never
// construct a reset at or past source EOF: the drop and nudge eps-guards in
// phaseresetmarkers_ops.cpp own prevention at the authoring boundary. A
// fabricated or stale sidecar entry at or past EOF (for example a sidecar
// left beside a replaced, shorter source) is a non-participating point that
// the window-participation verdict below drops exactly: the verdict bound is
// the deliverable map's exact final anchor target and map_source_to_target
// is monotonic with identity extrapolation past the last pair, so any source
// position at or past total_frames maps to a window target at or past the
// bound on both the full and the trimmed deliverable map. And the marker
// sidecars are committed to git, so a corrupt or stale marker file is
// mitigated by commit rollback, not by a render-time validation layer:
// prevention belongs at the GUI authoring boundary and enforcement at the
// strict load boundary. Recorded asymmetry: build_warp_frame_map's past-end
// check on the warp axis stays, because a past-end warp marker corrupts the
// map's own construction — the final segment still ends at total_frames, so
// an earlier past-end marker breaks the source column's strict ascent and
// the map is structurally wrong — whereas a past-end reset is a point event
// with no structural consequence, dropped by the verdict like any other
// non-participant. Different consequence class, so different owner. No
// ordering check lives here: the strict marker parser owns ordering at load,
// and the engine's strict-ascent hardfail covers raw phaseresetframemap
// inputs that bypass the marker parser. The result is the authored
// (undisplaced) source-frame intermediate consumed by render-view display
// and by derive_phase_reset_frame_map below, which compiles it into the
// engine-domain artifact and engine input. There is no resolver cascade
// sibling to resolve_warp_markers_for_render because phase reset markers
// carry no inheritance, labels, or references — a timestamp and a disabled
// flag are the whole grammar.
std::vector<double> build_phase_reset_source_frames(
    const std::vector<PhaseResetMarker>& markers, long sample_rate);

// Authored -> engine derivation chain: authored (undisplaced) phase-reset
// source positions -> the engine's origin-centered query domain, for a given
// render (full or trim-windowed).
//
// The parser is the sole authored-to-engine compiler on both columns.
// Authoring decisions — the phase reset anticipation offset, exactly like
// warp's tempo authoring — never live engine-side: the engine and the engine
// CLI consume only engine-domain inputs, blind to the anticipation
// subtraction, the inverse mapping, and the N/2 query-origin correction
// applied below. The .phaseresetframemap artifact is the engine-input list
// this chain derives; the authored-domain record of reset positions is the
// .phaseresetmarkers file, so each column ends as a markers file (authored
// domain) plus a frame map (engine domain).
//
// Sibling-stage asymmetry record. At the window-restriction stage, the warp
// slicer (slice_warp_frame_map_to_trim_window) coalesces out-of-window
// breakpoints into boundary anchors because a map is a connected piecewise
// function that must stay defined over every output sample, while the
// phase-reset window verdict (phase_reset_window_target_frame below) drops
// non-participating points because point events have nothing to coalesce
// into. One stage later, the engine-handoff siblings
// assign_engine_warp_frame_map and assign_engine_phase_reset_frame_map are
// parallel. The derivation itself has no warp sibling at all, because the
// warp frame map is already the engine's input domain and needs only
// windowing, while a phase-reset position alone crosses three domains —
// authored source, to window target, to engine query.

// Phase-reset lead-in expressed in synthesis hops, and its integer sample
// form: the offset-in-hops scaled by the synthesis hop, banker's-rounded to
// the integer sample domain. The lead-in is two synthesis hops; it is not
// authoring-tunable — not a flag, not a settings field, not a variable. The
// offset lives parser-side because the anticipation is an authoring decision
// the engine never sees: the derivation below bakes it into the engine-domain
// list before the engine reads anything.
constexpr double kPhaseResetOffsetHops = 2.0;
inline const int64_t phase_reset_offset_samples = static_cast<int64_t>(
    std::nearbyint(kPhaseResetOffsetHops * static_cast<double>(kRs)));

// Window-participation verdict for one authored (undisplaced) phase-reset
// source position — an exact double source frame. Maps it through the full
// map to its target image and re-anchors to the rendered window's origin.
// Returns the window-domain target frame W, or std::nullopt when the reset
// does not participate in this render: W negative (before the window — the
// instant precedes the deliverable's first sample) or at or past
// render_target_end (beyond the deliverable's last sample). The bound is
// the deliverable map's own final anchor target, compared exactly in the
// double target domain: trimmed maps carry the integral boundary-pair cap
// there, the full map carries the exact source-EOF target, so a reset at
// the trim end or at source EOF drops identically on both forms.
// Quantization to the engine's integer output length is engine-owned and
// never enters this verdict. Shared by the engine-input derivation below
// and the render-view display sidecar writer, so display participation and
// engine input converge on the same window-bounds verdict.
std::optional<double> phase_reset_window_target_frame(
    double source_frame,
    const std::vector<WarpFrameMapSegment>& full_map,
    int64_t window_offset_samples,
    double render_target_end);

// Derive the engine-input phase reset frame map for a given render (full or
// trim-windowed): each authored source position is taken through the window
// verdict above, anticipated by phase_reset_offset_samples in the
// target/output domain (with a lead-in dropzone rather than a clamp), and
// inverse-mapped through engine_map into the engine's origin-centered query
// domain. The result stays in exact doubles; quantization to the engine's
// integer query schedule is engine-owned, happening at placement time.
// Non-participating positions are dropped, so the result can be shorter than
// the input.
std::vector<double> derive_phase_reset_frame_map(
    const std::vector<double>& source_frames,
    const std::vector<WarpFrameMapSegment>& full_map,
    const std::vector<WarpFrameMapSegment>& engine_map,
    int64_t window_offset_samples,
    double render_target_end);

// Deliverable form: derive the .phaseresetframemap artifact against the
// exact deliverable map shipped beside it — full_map and engine_map are both
// the deliverable map, the window offset is zero, and the render bound is
// the deliverable map's last pair's target, exact. The emitted list is
// computed against the exact map shipped beside it, so the artifact pair is
// exactly warptempo_engine's input and an engine fed the pair renders that
// map's geometry exactly. This is the single trimmed derivation everywhere:
// the in-process trimmed render derives its engine-input list through this
// same form against the same deliverable map, so the artifact pair and the
// in-process render coincide by construction. The window verdict against
// a trimmed deliverable map clamps a pre-window source position to window
// target zero (map_source_to_target clamps before the first pair) rather
// than going negative; such a reset still drops, via the anticipation
// dropzone instead of the negative-target test — same drop set, different
// test. An empty map yields an empty list (unreachable from program paths:
// the full-map builder always emits the seed anchor, and every trimmed
// caller refuses the degenerate stored-zero window — whose map the slicer
// leaves unbuilt — before deriving; kept so the back() access is
// unconditionally safe, as in derive_midi_tempo_map).
std::vector<double> derive_phase_reset_frame_map(
    const std::vector<double>& source_frames,
    const std::vector<WarpFrameMapSegment>& deliverable_map);
