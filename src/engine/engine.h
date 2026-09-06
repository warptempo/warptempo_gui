#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "warp_frame_map.h"      // WarpFrameMapSegment

// Parameter struct for the warptempo DSP pipeline. Constructed by the render
// orchestrators (the GUI's do_render and warptempo_cli) and passed to
// run_warptempo_engine().
struct EngineParams {
    // Source audio as interleaved float, sample rate, and channel count. The
    // engine reads this caller-owned interleaved float buffer directly. The
    // buffer must remain valid for the duration of run_warptempo_engine().
    const float* source_audio_samples = nullptr;
    size_t       source_audio_frames  = 0;
    int          source_sample_rate   = 0;
    int          source_channels      = 0;

    // The engine's sole output sink, required non-null (the engine is
    // buffer-out only; encode lives orchestrator-side in the prepost chain).
    // Synthesis appends to this caller-owned vector by resizing it ONCE by the
    // whole emission and interleaving the channels into that new tail in place
    // (architect approval 2026-09-06). The spectral limiter is always applied
    // in place on this buffer after synthesis. The buffer must remain valid
    // through run_warptempo_engine; the caller is responsible for clearing or
    // reserving.
    std::vector<float>* output_buffer = nullptr;

    std::vector<WarpFrameMapSegment> warp_frame_map;  // rounded + precise breakpoints

    // Phase reset position list. Each entry is an exact double position in
    // the engine's origin-centered query domain, produced by the parser's
    // derivation (phase_reset_frame_map_build.h) — trimmed renders carry the
    // prepost trimmer's translated, range-filtered form of that list. Must
    // be strictly increasing; the engine refuses loudly at init otherwise,
    // mirroring the warp-frame-map strict-ascent validation.
    // Quantized exactly once, by Pass 1's llrint against the query schedule.
    std::vector<double> phase_reset_frame_map;

    // Optional precomputed source-frame analysis schedule (one entry per
    // synthesis hop, valued in source sample-frames). When null (the default
    // — and the ONLY full-render path: a recorded asymmetry, full renders
    // never supply one) the engine generates its own schedule from
    // warp_frame_map exactly as always
    // (AudioSTFT::generate_source_frame_positions, byte-untouched). When
    // non-null the engine adopts it VERBATIM as source_frame_positions and
    // skips generation, after a loud shape check: the entry count must equal
    // what generation would produce (the number of t_s values in
    // [0, target_total_frames) stepping R_s) and the entries must be
    // nondecreasing (generation is nondecreasing because the map is monotone;
    // equal neighbours are legal under extreme slow-downs). The field exists
    // for TRIMMED renders: a trimmed render's translated map is a rebuilt
    // object, and re-interpolating it is not provably llrint-identical to the
    // full map at half-integer ties, so the orchestrator derives the trimmed
    // schedule from the FULL map's own evaluations (plan_trim, translated by
    // the integer source cut) and hands it here — schedule identity with the
    // full render then holds by construction rather than by a floating-point
    // coincidence. Caller-owned; must outlive run_warptempo_engine.
    const std::vector<int64_t>* source_frame_schedule = nullptr;

    // The engine renders the supplied warp_frame_map wholesale and is
    // trim-ignorant: a trimmed render is produced by handing it the prepost
    // trimmer's translated map plus the matching offset+length view into the
    // source buffer (plan_trim in src/prepost/trimmer.h); trimmed renders
    // behave exactly like full renders to the engine. The engine derives its
    // output length from the map's last anchor — map-extent emission is pure
    // full-render behavior — and identity-extrapolates past the map end into
    // the final window skirts.
};

// Tristate result so the GUI-thread dispatcher can distinguish cancellation
// (worker observed `cancel_flag` set at a frame boundary) from genuine
// failure (open errors, bad warp_frame_map, etc.).
enum class EngineResult { Success, Failed, Cancelled };

// `cancel_flag` is optional. When non-null, the synthesis loop checks it
// at the top of each frame iteration; if set, the engine returns
// EngineResult::Cancelled without completing the output buffer.
EngineResult run_warptempo_engine(const EngineParams& p,
                                  const std::atomic<bool>* cancel_flag = nullptr);
