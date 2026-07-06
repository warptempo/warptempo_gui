#pragma once

#include "warpmarkers_parse.h"          // WarpMarker
#include "warp_frame_map.h"                  // WarpFrameMapSegment

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

// In-memory map build used by the engine. Math is organized as
// Pass 1 and Pass 2; the full untrimmed map is built unconditionally and trim
// is applied downstream, never here.

struct MidiTempoMapEntry {
    double target_time_sec;
    double multiplier;
};

// Minimal POD the warp_frame_map math needs. The GUI's `GuiWarpMarker` resolves into
// this: tempo_inherits markers are walked back to their nearest owning
// ancestor and their effective tempo_base / tempo_scale are copied forward.
// Disabled markers (and any references to disabled-defined labels) are
// filtered out BEFORE conversion.
struct MarkerForRender {
    double      time_seconds = 0.0;
    double      tempo_base   = 1.0;   // resolved owning tempo; irrelevant for label_ref
    std::string tempo_scale;           // "" or the numeric string after '*'
    std::string label_def;
    std::string label_ref;
};

// Effective tempo a marker resolves to, for display/authoring callers (not
// fed to the render path). See marker_effective() below.
struct MarkerEffective {
    double      base       = 0.0;   // 0.0 means "could not resolve"
    std::string scale;              // "" means no typed scale (treated as 1.0)
    int         source_idx = -1;    // marker this value was taken from; -1
                                    // means no visible source (e.g. a
                                    // first-marker pass resolving to the 1.0
                                    // default)
    bool scale_saturated = false;   // true when the label_ref branch clamped
                                    // the displayed combined scale to the
                                    // 9.9999 typed-field ceiling; display-only,
                                    // the render's ref handling is delta-based
                                    // and unaffected
};

// Returns the built warp frame map on success, or std::unexpected carrying
// the first violated condition (a concise lowercase reason; callers add their
// own context prefix). Does not log. Failure conditions, in check order:
// invalid source audio metadata (sample_rate <= 0 or total_frames <= 0),
// src_frame > total_frames, src_frame - prev_src_frame < 1, tempo <= 0
// (a zero or negative effective product divides by zero or flips sign in the
// segment arithmetic), duplicate label definition, undefined label reference.
// Builds the full untrimmed map unconditionally; trim is applied downstream by
// slice_warp_frame_map_to_trim_window (engine input) and derive_trimmed_artifact_maps
// (external artifacts), never here. Scale participates here and not in
// build_phase_reset_source_frames because scale multiplies tempo, a
// target-duration quantity; phase reset positions are undisplaced source
// instants and have no target-duration component.
std::expected<std::vector<WarpFrameMapSegment>, std::string>
build_warp_frame_map(const std::vector<MarkerForRender>& markers,
                     double scale, long sample_rate, long total_frames);

// Derive the midi tempo map from a finished warp frame map. Each consecutive
// pair (a, b) with a positive target duration contributes an entry at
// a.tgt_frame / sample_rate whose multiplier is
// (b.src_frame - a.src_frame) / (b.tgt_frame - a.tgt_frame); after the walk a
// final entry at map.back().tgt_frame / sample_rate carries the last valid
// multiplier (1.0 when no pair produced one). The > 0 comparison is division
// safety only, not a size threshold — segment target durations have no floor —
// and the last valid multiplier carries across skips. The arithmetic reads the
// map's stored doubles exactly as written: entries are serialized at seventeen
// significant digits, so any reassociation would change deliverable bytes. An
// empty map returns the single entry {0.0, 1.0} (unreachable from program
// paths, since the build always emits the seed anchor; kept so the back()
// access is unconditionally safe).
std::vector<MidiTempoMapEntry> derive_midi_tempo_map(
    const std::vector<WarpFrameMapSegment>& warp_frame_map,
    long sample_rate);

// Source-aware trim-bounds check shared by every trim-taking caller (the GUI
// render dispatch, the parser CLI's trimmed-artifact path, and warptempo_cli's
// startup). An explicit begin must lie strictly inside the source; an explicit
// end may sit exactly at the source end but no further. Order between begin and
// end is guaranteed upstream (the file reader rejects crossed explicit bounds;
// GUI drag authoring preserves order). Returns {} when in range, else
// std::unexpected with the concise reason; callers add their own stderr prefix.
std::expected<void, std::string> validate_trim_frames(
    int64_t begin_frame, int64_t end_frame,
    bool has_begin, bool has_end, int64_t total_frames);

// Resolve each WarpMarker to a MarkerForRender. Callers in the GUI slice
// their GuiWarpMarker store to std::vector<WarpMarker> first (the resolver
// is parser-domain and reads no GUI-only fields). Filters out markers that
// are references to disabled-defined labels and disabled label-definition
// markers (and thereby all refs to them). The inherit walk-back is applied
// here so MarkerForRender carries a concrete tempo_base / tempo_scale —
// same rule as resolve_inherited_tempo. Both the engine-bound render
// pipeline and the target view's warp_frame_map recompute go through this single
// resolver so the visible deformity matches what the engine would emit.
std::vector<MarkerForRender> resolve_warp_markers_for_render(
    const std::vector<WarpMarker>& src);

// Backward inheritance walk over parser-domain markers: from `index`, scan
// earlier markers for the nearest that OWNS its tempo — tempo_inherits ==
// false, not a label reference, and not disabled. Disabled markers are skipped
// because the engine drops them before resolution, so a disabled marker
// contributes no tempo downstream. Returns 1.0 (tempo) / "" (scale) if none is
// found. This is the single canonical inheritance walk: resolve_warp_markers_for_render
// and compute_hover_popup_text both call it, so the popup display always
// matches the tempo the engine resolves.
double resolve_inherited_tempo(const std::vector<WarpMarker>& markers, int index);
std::string resolve_inherited_tempo_scale(
    const std::vector<WarpMarker>& markers, int index);

// Effective (base, scale, source) a marker resolves to, for display/authoring
// callers in hover/popup and marker operation paths. base
// == 0.0 means "could not resolve" (mirrors compute_hover_popup_text's ""
// guards). scale == "" means no typed scale (treated as 1.0 by callers).
// source_idx names the marker the value is visibly taken from:
//   owner     -> idx itself (its own tempo_base / tempo_scale).
//   pass      -> the nearest non-disabled marker strictly before idx (the
//                immediate prior marker the value is inherited from — NOT
//                necessarily the owning marker if there's a chain of passes).
//                base/scale are still the fully-resolved owner values via
//                resolve_inherited_tempo(_scale).
//   label_ref -> the label-definition marker (def_idx); base/scale are the
//                def's effective base and the combined "~=" scale — or the
//                "9.9999" ceiling with scale_saturated set when the combined
//                value is at or above the typed-field ceiling (displayed
//                ">=", a lower bound).
// A pass immediately after a label_ref is legal; source_idx then names the
// ref (the visible immediate prior), base/scale still the resolved owner's.
MarkerEffective marker_effective(const std::vector<WarpMarker>& mv,
                                  int idx, int sample_rate);

// Hover-popup text for a warp marker (the label-ref / pass tempo notice). Pure
// parser-domain string/math — computes the same resolution the engine uses
// when emitting the warpframemap, so the popup matches what will be
// rendered. Pass
// markers emit "= TEMPO (from SOURCE @ TIME)" or "= TEMPO*SCALE (from SOURCE @
// TIME)" (resolved tempo of the nearest prior owning marker; SOURCE is the
// immediate prior marker's own resolved displayed tempo — matching what that
// marker's own popup or flag shows, not its raw stored fields, which are
// inert for a pass or a label_ref — TIME its time_seconds). If SOURCE's own
// resolution is unresolvable (base 0.0), the suffix is dropped entirely and
// the popup shows just the resolved tempo. Label_ref markers emit
// "~= BASE*COMBINED_SCALE (from DEF_BASE:LABEL @ TIME)" (BASE at 2 decimals;
// COMBINED_SCALE = def_scale * multiplier when the def has a typed scale,
// else multiplier, at 4 decimals; DEF_BASE:LABEL and TIME describe the
// label-definition marker). When the combined scale saturates at the 9.9999
// typed-field ceiling the leading "~=" becomes ">=" and COMBINED_SCALE is
// "9.9999" (a lower bound, not an approximation); the same ">=" prefixes a
// pass popup's provenance descriptor when its visible immediate prior is a
// saturated ref. TIME is formatted with format_timestamp
// (time_format.h), the same mm:ss.mmm formatter the rest of the GUI uses.
// Returns "" when the marker does not qualify (owning, missing def,
// malformed). GUI callers slice their GuiWarpMarker store to WarpMarker
// (slice_to_warp_markers) before calling.
std::string compute_hover_popup_text(
    const std::vector<WarpMarker>& mv, int idx, int sample_rate);

// Result of slicing the full untrimmed frame map to a trim window: the
// re-anchored sub-map the engine renders, the output offset of the window
// origin (wbegin * R_s) for the render-view marker sidecar, and the
// output-sample cap the engine truncates the emitted render at (the trim-end
// target, re-anchored). The sub-map extends to the first real anchor at or
// past trim_end_src so the final segment carries the full map's exact slope;
// emit_sample_cap then cuts the output back to the trim boundary.
struct WindowedWarpFrameMap {
    std::vector<WarpFrameMapSegment> warp_frame_map;     // source absolute; target re-anchored to 0
    int64_t                      window_offset_samples = 0;
    int64_t                      emit_sample_cap       = 0;  // trim-end output length
};

// Slice the full untrimmed map to the synthesis-frame window covering
// [trim_begin_src, trim_end_src], reproducing the engine's former internal
// trim window as a standalone sub-map. The window start is selected against the
// dense synthesis-frame schedule (the same generate_source_frame_positions
// logic the engine used), so the slice lands on a synthesis-frame boundary, NOT
// a source-frame boundary. Target is shifted by -wbegin*R_s (a rigid integer
// translation, so interior segment lines are preserved exactly); source stays
// absolute. The sub-map TERMINATES on the first full-map anchor whose source is
// at or past trim_end_src (the anchor closing the segment that contains
// trim_end_src), target-shifted — NOT on a synthetic rounded anchor at
// trim_end_src — so the final segment carries the full map's exact slope. The
// returned emit_sample_cap is the trim-end target minus the window offset; the
// engine emits only up to it, so the few frames between trim_end_src and the
// closing anchor are synthesized into the truncated tail only. The returned map
// must be strictly monotonic in both axes (engine
// validate_warp_frame_map_strictly_ascending rejects it otherwise). Caller invokes this only when a trim bound is set.
WindowedWarpFrameMap slice_warp_frame_map_to_trim_window(
    const std::vector<WarpFrameMapSegment>& full_map,
    int64_t trim_begin_src, int64_t trim_end_src,
    int N, int R_s);

// The external .warpframemap / .miditempomap artifacts for a trimmed
// deliverable.
struct TrimmedArtifactMaps {
    std::vector<WarpFrameMapSegment> warp_frame_map;
    std::vector<MidiTempoMapEntry>   midi_tempo_map;
};

// Derive the trimmed deliverable's warpframemap and miditempomap from the SAME
// window the engine renders, so there is exactly one trim computation in the
// codebase and the artifacts describe the delivered WAV byte-for-byte. Slices
// the full map with slice_warp_frame_map_to_trim_window and reads back its
// window: the warpframemap keeps every window pair strictly inside the emit
// cap and the trim end, then appends the exact (trim_end_src, emit_sample_cap)
// boundary pair; the miditempomap is the full midi tempo map shifted by
// -window_offset into the deliverable-relative time domain, origin at time
// zero, a final no-op event at the end so DAWs learn the track length.
//
// Returns the derived maps on success, or std::unexpected carrying a concise
// lowercase reason (callers add their own context prefix, same contract as
// validate_trim_frames). Refuses the same degenerate window the WAV path refuses
// through assign_engine_warp_frame_map, up front, instead of writing a map that is
// either reader-rejected or engine-misread. The single refusal condition,
// checked immediately after slicing: emit_sample_cap <= 0 — the trim's target
// span is entirely consumed by the hop-aligned window start, so no output
// sample lies between the window start and the trim end; a stored zero cap
// reads back as "uncapped" at the engine boundary, so warptempo_engine fed
// such a map would render a spurious tail. The same refusal covers an empty
// window map, which the slicer returns only with the cap at its default of
// zero.
//
// Artifact convention: the target column is deliverable-relative — the first
// pair's target is exactly zero, the WAV's first sample — while the source
// column stays absolute undisplaced source frames, matching the project-wide
// convention shared by marker files and render-view sidecars.
// The first pair (s, 0) is therefore self-describing: s is the absolute source
// position of the deliverable's first sample, roughly the trim instant plus the
// N/2 analysis margin, hop-quantized.
std::expected<TrimmedArtifactMaps, std::string> derive_trimmed_artifact_maps(
    const std::vector<WarpFrameMapSegment>& full_map,
    const std::vector<MidiTempoMapEntry>&  full_midi_tempo_map,
    int64_t trim_begin_src, int64_t trim_end_src,
    int N, int R_s, long sample_rate);
