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
    // Synthesis output is appended to this caller-owned vector via
    // std::vector::insert. When limiter is true the spectral limiter is
    // applied in place on this buffer; when false the buffer holds clean
    // synthesis. The buffer must remain valid through run_warptempo_engine;
    // the caller is responsible for clearing or reserving.
    std::vector<float>* output_buffer = nullptr;

    std::vector<WarpFrameMapSegment> warp_frame_map;  // rounded + precise breakpoints

    int    N                          = 4096;
    bool   limiter                    = false;
    double limiter_ceiling_dbfs       = -0.3;   // spectral
    double limiter_tolerance_db       = 0.01;
    // When false, the spectral limiter's stdout header and per-peak residual
    // lines are suppressed (the target-view scrub path); the pass-structure
    // prints are unaffected.
    bool   limiter_verbose            = true;

    // Phase reset position list. Each entry is an exact double position in
    // the engine's origin-centered query domain, produced by the parser's
    // derivation (phase_reset_frame_map_build.h) — trimmed renders carry the
    // prepost trimmer's translated, range-filtered form of that list. Must
    // be strictly increasing; the engine refuses loudly at init otherwise,
    // mirroring the warp-frame-map strict-ascent validation.
    // Quantized exactly once, by Pass 1's llrint against the query schedule.
    std::vector<double> phase_reset_frame_map;

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
