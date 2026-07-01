#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "frame_map.h"      // FrameMapSegment

// Parameter struct for the warptempo DSP pipeline. Constructed by the GUI's
// render pipeline and passed to run_warptempo_engine().
struct EngineParams {
    // Source audio as interleaved float, sample rate, and channel count. The
    // engine wraps this in a libsndfile virtual-IO SNDFILE and reads from
    // memory rather than disk. The buffer must remain valid for the
    // duration of run_warptempo_engine().
    const float* source_audio_samples = nullptr;
    size_t       source_audio_frames  = 0;
    int          source_sample_rate   = 0;
    int          source_channels      = 0;

    std::string output_audio_path;

    // Optional in-memory output sink. When non-null, the engine routes
    // synthesis output to this caller-owned vector (append-only via
    // std::vector::insert). When limiter is true the limited chain
    // (spectral -0.3 + peak 0 backstop) is applied in place on this buffer;
    // when false the buffer holds clean synthesis. The existing
    // output_audio_path field is ignored on this path. The buffer must
    // remain valid through run_warptempo_engine; the caller is responsible
    // for clearing or reserving.
    std::vector<float>* output_buffer = nullptr;

    std::vector<FrameMapSegment> frame_map;  // rounded + precise breakpoints

    int    N                          = 4096;
    bool   limiter                    = false;
    double limiter_ceiling_dbfs       = -0.3;   // spectral
    double limiter_tolerance_db       = 0.01;
    double peak_limiter_ceiling_dbfs  = 0.0;
    double peak_limiter_attack_ms     = 0.25;
    double peak_limiter_release_ms    = 0.5;

    // User-curated phase reset frame list (source-frame domain). When non-empty,
    // the engine skips its internal phase reset detection and uses this list
    // verbatim for phase reset positioning. Must be sorted ascending.
    // Typical source: GUI's phase reset view, providing the union of inserted
    // + active-detected (with displacement applied) entries.
    std::vector<int64_t> phase_reset_frames;

    // Output-sample cap. When > 0, the engine emits exactly this many output
    // samples and synthesizes only the frames needed to cover them; the rest of
    // the supplied map is rendered into the discarded tail. When 0 (default),
    // the cap is derived from the map's last anchor (full-render behavior). This
    // is a pure output-length budget, not a trim window: the engine has no
    // notion of trim begin/end source frames. A trimmed render supplies the
    // parser's WindowedFrameMap::emit_sample_cap here.
    int64_t emit_sample_cap = 0;

    // The engine renders the supplied frame_map wholesale and is trim-ignorant:
    // a trimmed render is produced by handing it a pre-sliced sub-map (the
    // parser's slice_frame_map_to_trim_window), not by an internal window.
};

// Tristate result so the GUI-thread dispatcher can distinguish cancellation
// (worker observed `cancel_flag` set at a frame boundary) from genuine
// failure (open errors, bad frame_map, etc.).
enum class EngineResult { Success, Failed, Cancelled };

// `cancel_flag` is optional. When non-null, the synthesis loop checks it
// at the top of each frame iteration; if set, the engine returns
// EngineResult::Cancelled without writing the output wav.
EngineResult run_warptempo_engine(const EngineParams& p,
                                  const std::atomic<bool>* cancel_flag = nullptr);
