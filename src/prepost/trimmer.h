#pragma once

#include "warp_frame_map.h"  // WarpFrameMapSegment, map_source_to_target/_target_to_source

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

// Premise (architect-ruled). Trim is a transient inspection tool for quickly
// rendering and auditioning a segment — not an archival deliverable. Accepted
// caveats: head phase is wrong until the first phase reset marker; the tail
// may fade over R_s multiples. The model is cut source, rebase maps, render,
// crop — with hop discipline so the trimmed render phase-inverts to null
// against the full render past the first phase reset, through the last kept
// sample (the derivation lives at plan_trim in trimmer.cpp).
//
// Pipeline shape: parser (builds the FULL warp frame map and the FULL phase
// reset frame map, knows nothing of trim) -> pre_trim when a bound is set
// (validate-or-refuse; cut audio view + translated maps) -> engine (analysis,
// PGHI, synthesis, spectral limiter when limiter=true, emits its map's
// extent) -> post_trim (crop to the exact authored window) -> peak limiter
// when limiter=true -> encode to disk and/or hand the buffer to the caller.
// Full renders bypass the trimmer completely — both stages.
//
// Trim is output_format=wav ONLY (ruled): the map formats (warptempo_maps,
// generic_map, midi_map) refuse when a trim bound is set — the GUI dispatch
// preflight raises the popup, do_render's map-format arm refuses on stderr as
// the backstop. The .settings vocabulary is unchanged.
//
// Vocabulary is pre_trim / post_trim, as in pre-/post-processing. One
// computation (plan_trim) yields both stages, so the source view, the
// translated maps, and the crop are consistent by construction.

struct PreTrim {
    // Source-domain view fed to the engine: an offset+length view into
    // the shared sample buffer, no copy.
    int64_t begin_frame = 0;
    int64_t frames      = 0;
    std::vector<WarpFrameMapSegment> warp_frame_map;   // translated
    std::vector<double> phase_reset_frame_map;         // translated
};

struct PostTrim {
    int64_t begin_sample = 0;  // frames into the engine's emitted buffer
    int64_t samples      = 0;  // frames kept
};

struct TrimPlan {
    PreTrim  pre;
    PostTrim post;
};

// Render-boundary trim validation — the sole owner of every trim refusal.
// Authored bounds are exact double source frames (seconds * sample rate, no
// rounding; unset begin is 0, unset end is total_frames). Refusals, in check
// order: end at or before begin (e_src <= b_src); begin at or past the source
// end (b_src >= total_frames); end past the source end (e_src > total_frames);
// target span rounding below one sample (llrint(T_e) - llrint(T_b) < 1, the
// spans' exact double images through the full map). NOTHING else refuses — a
// one-frame fady trim renders; it is not degenerate in the strictest sense,
// which is what is applied. Returns {} when valid, else std::unexpected with
// the concise reason; callers add their own context prefix (the GUI dispatch
// preflight pops it verbatim, the CLI prints it to stderr). plan_trim calls
// this first, so callers routing through plan_trim need no separate call.
std::expected<void, std::string> validate_trim_frames(
    bool has_begin, double begin_seconds,
    bool has_end,   double end_seconds,
    long sample_rate, int64_t total_frames,
    const std::vector<WarpFrameMapSegment>& full_warp_frame_map);

// Compute the trim plan for one render: validates the bounds (above), then
// derives the source cut, the translated warp frame map, the translated and
// range-filtered phase reset frame map, and the output crop, all from one
// geometry (the derivation comment sits at the definition). Inputs are the
// FULL untrimmed warp frame map and the FULL deliverable-form phase reset
// derivation (derive_phase_reset_frame_map of the authored source frames
// against that full map), the authored bounds, and the driver geometry
// (N = kN, R_s = kRs). Refusals are exactly validate_trim_frames'.
//
// Column asymmetry record, the window-restriction stage: the warp column
// keeps a connected map — out-of-window breakpoints drop and the seed /
// closing anchors take their place, because a map is a piecewise function
// that must stay defined over every output sample — while the phase reset
// column range-filters its points away, because point events have nothing to
// coalesce into.
std::expected<TrimPlan, std::string> plan_trim(
    const std::vector<WarpFrameMapSegment>& full_warp_frame_map,
    const std::vector<double>& full_phase_reset_frame_map,
    bool has_begin, double begin_seconds,
    bool has_end,   double end_seconds,
    long sample_rate, int64_t total_frames,
    int N, int R_s);

// Crop the engine's emitted interleaved buffer to the exact authored window:
// drop post.begin_sample frames from the head and keep post.samples frames.
// begin_sample / samples are frame counts, applied per-channel on the
// interleaved buffer.
void apply_post_trim(std::vector<float>& buffer, int channels,
                     const PostTrim& post);

// Orchestrator-side projection refusal, run before the engine allocates —
// the refuse-before-cost property: the engine buffers its full emission in
// memory, so an implausible allocation or an un-finalizable on-disk shape is
// refused before any output-proportional cost is paid.
// engine_output_frames is llrint of the engine map's last anchor target (the
// emission the engine will buffer); encoded_frames is what lands on disk
// (post-crop when trimmed, the same value untrimmed). The RIFF check applies
// only when encoding to disk, against the format the limiter decision selects
// (PCM 24 when limiter, else 32-bit float).
std::expected<void, std::string> validate_render_projection(
    int64_t engine_output_frames, int64_t encoded_frames,
    int channels, bool limiter, bool encode_to_disk);

// The shared post-engine chain — ONE implementation compiled by both
// warptempo_gui (do_render's wav arm) and warptempo_cli, so the CLI stays
// byte-identical to the GUI by construction. Stages, in order:
//   post_trim crop        when post_trim != nullptr (trimmed renders)
//   peak limiter          when limiter (kPeakLimiter* constants)
//   pcm24 decision + sink:
//     output_wav_path set   -> encode to that path (PCM 24 when limiter,
//                              clean 32-bit float otherwise); staging-file
//                              naming and the atomic rename publication stay
//                              orchestrator-owned.
//     output_wav_path empty -> buffer route (target view): quantize the
//                              buffer to the deliverable PCM 24 lattice in
//                              place (limiter-on only), no encode, and hand
//                              the buffer back to the caller as-is.
// Returns {} on success, else the failure message (open/write/close detail);
// callers add their own context prefix.
std::expected<void, std::string> finish_render(
    std::vector<float>& buffer, int channels, int sample_rate,
    bool limiter, const PostTrim* post_trim,
    const std::string& output_wav_path);
