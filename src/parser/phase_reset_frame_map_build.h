#pragma once

#include "phaseresetmarkers_parse.h"  // PhaseResetMarker
#include "warp_frame_map.h"           // WarpFrameMapSegment
#include "engine/engine_geometry.h"   // kN, kRs

#include <cmath>
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
// instead of the reset silently falling out of the derivation's window drop
// test. The two past-end checks are one guard with two instances, kept
// together by ruling: column symmetry outranks the constructibility
// argument for removing either side alone. Disabled markers are skipped
// before the check — only resolved markers are validated, as in
// build_warp_frame_map — so this build refuses only enabled past-end resets,
// as a breach backstop for hand-edited artifacts. A past-end reset is
// unreachable through the GUI (the gesture wall), and one in a hand-edited
// sidecar is load-fatal at the orchestrator (GUI file_loader / CLI, disabled
// markers included) as adversarial input — a reset file applies only to the
// audio it was authored against. Ordering: the load parser rejects only
// DECREASING times (equal times load intact), so the input here is
// non-decreasing; two resets closer than one deepest-zoom pixel of time are
// prohibited by the raw-store rule (marker_store_validate.h) on both columns —
// a wider window than this build's sub-frame check, which refuses only the
// narrower sub-frame pairs as the breach backstop for hand-edited input, so the
// output is strictly increasing by construction, and the engine's strict-ascent
// hardfail still covers raw phaseresetframemap inputs that bypass the marker
// parser. The
// result is the authored (undisplaced) source-frame intermediate consumed by
// render-view display and by derive_phase_reset_frame_map below, which
// compiles it into the engine-domain artifact and engine input. There is no
// resolver cascade sibling to resolve_warp_markers_for_render because phase
// reset markers carry no inheritance, labels, or references — a timestamp
// and a disabled flag are the whole grammar.
std::expected<std::vector<double>, std::string> build_phase_reset_source_frames(
    const std::vector<PhaseResetMarker>& markers, long sample_rate,
    int64_t total_frames);

// Authored -> engine derivation chain: authored (undisplaced) phase-reset
// source positions -> the engine's origin-centered query domain, always
// against the FULL map (the parser knows nothing of trim; the prepost
// trimmer translates and range-filters this chain's output for a trimmed
// render).
//
// The parser is the sole authored-to-engine compiler on both columns.
// Authoring decisions — the phase reset anticipation offset, exactly like
// warp's tempo authoring — never live engine-side: the engine consumes only
// engine-domain inputs, blind to the anticipation
// subtraction, the inverse mapping, and the N/2 query-origin correction
// applied below. The .phaseresetframemap artifact is the engine-input list
// this chain derives; the authored-domain record of reset positions is the
// .phaseresetmarkers file, so each column ends as a markers file (authored
// domain) plus a frame map (engine domain).
//
// The derivation has no warp sibling, because the warp frame map is already
// the engine's input domain, while a phase-reset position alone crosses
// three domains — authored source, to deliverable target, to engine query.

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

// Derive the engine-input phase reset frame map against the deliverable
// map: each authored (undisplaced) source position — an exact double source
// frame — is mapped to its target image, bounded by the map's own final
// anchor target (a reset at or past it lies beyond the deliverable's last
// sample and drops), anticipated by phase_reset_offset_samples in the
// target/output domain (with a lead-in dropzone rather than a clamp), and
// inverse-mapped through the same map into the engine's origin-centered
// query domain. The result stays in exact doubles; quantization to the
// engine's integer query schedule is engine-owned, happening at placement
// time. Non-participating positions are dropped, so the result can be
// shorter than the input. The .phaseresetframemap artifact is this list
// derived against the exact map shipped beside it, so the artifact pair is
// exactly the engine's input, and the in-process render derives its
// engine-input list through this same form — pair and render coincide by
// construction. An empty map yields an empty list (unreachable from program
// paths: the full-map builder always emits the seed anchor; kept so the
// back() access is unconditionally safe, as in derive_midi_tempo_map).
std::vector<double> derive_phase_reset_frame_map(
    const std::vector<double>& source_frames,
    const std::vector<WarpFrameMapSegment>& deliverable_map);
