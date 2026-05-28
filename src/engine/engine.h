#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Tri-state selector for the engine's limiter pass. The 24-bit-PCM output
// format decision is derived from this enum inside the engine (None →
// 32-bit float, Spectral/Peak → 24-bit PCM) — single source of truth.
enum class LimiterMode {
    None,      // no limiter; output is 32-bit float
    Spectral,  // frequency-domain limiter (limiter.cpp)
    Peak,      // time-domain peak limiter (peak_limiter.cpp), inline in synthesis
};

// Phase-propagation model selectable per phase-reset-marker segment. Shared by
// the GUI authoring layer and the engine so both sides reference one
// definition. `Peak` is the Laroche-Dolson identity peak-locking model (the
// historical engine and the global default). `Heap` is the PGHI
// gradient-integration model. `Pass` ("inherit the previous owning marker's
// mode") exists ONLY in the GUI authoring/file domain; it is resolved to a
// concrete Peak/Heap before the engine runs (mirroring how an unresolved
// `pass` tempo never reaches the engine). The engine never sees Pass.
enum class Mode { Peak, Heap, Pass };

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
    // std::vector::insert) and skips the spectral limiter pass — Pass 2
    // (limiter.process) is not invoked. Pass 3 still applies a peak
    // limiter when limiter_mode == Peak (the target render opts in
    // via RenderRequest::force_peak_limiter at the GUI boundary). The
    // existing output_audio_path field is ignored on this path. The
    // buffer must remain valid through run_warptempo_engine; the caller
    // is responsible for clearing or reserving.
    std::vector<float>* output_buffer = nullptr;

    std::vector<std::pair<size_t, size_t>> timemap;  // src_frame, tgt_frame

    int    N                          = 4096;
    int    fftw_threads               = 0;   // 0 = auto
    LimiterMode limiter_mode          = LimiterMode::None;
    double limiter_ceiling_dbfs       = -0.3;   // spectral
    double limiter_tolerance_db       = 0.01;
    int    limiter_num_bands          = 0;
    bool   limiter_diag               = false;
    double peak_limiter_ceiling_dbfs  = -0.3;
    double peak_limiter_attack_ms     = 0.25;
    double peak_limiter_release_ms    = 0.5;

    // User-curated phase reset frame list (source-frame domain). When non-empty,
    // the engine skips its internal phase reset detection and uses this list
    // verbatim for phase reset positioning. Must be sorted ascending.
    // Typical source: GUI's phase reset view, providing the union of inserted
    // + active-detected (with displacement applied) entries.
    std::vector<int64_t> phase_reset_frames;

    // Resolved per-reset phase-propagation model, parallel to
    // phase_reset_frames (same length and order). Each entry is a CONCRETE
    // mode (Peak or Heap; never Pass) — inheritance is resolved GUI-side
    // before dispatch. Default contract: when this list is EMPTY, the engine
    // treats every reset as Peak — an empty modes list is byte-identical to
    // the historical all-peak behavior, which keeps non-GUI callers (e.g. the
    // parser binary) unaffected. When non-empty it must match
    // phase_reset_frames in length and order.
    std::vector<Mode> phase_reset_modes;

    // Phase-propagation mode at synthesis frame 0. Resolved GUI-side from
    // the most recent mode-owning marker at-or-before the render's first
    // source frame (untrimmed: source frame 0; trimmed: trim_begin frame),
    // walking back through `pass` and disabled markers as
    // resolve_inherited_mode does. Defaults to Peak so non-GUI callers
    // (parser binary, future callers) that never set the field reproduce
    // the historical seed and pass Gate 1 byte-identity. The GUI dispatch
    // site also resolves to Peak when no owner precedes, so all-Peak input
    // and untagged input remain byte-identical.
    Mode initial_phase_mode = Mode::Peak;
};

// Tristate result so the GUI-thread dispatcher can distinguish cancellation
// (worker observed `cancel_flag` set at a frame boundary) from genuine
// failure (open errors, bad timemap, etc.).
enum class EngineResult { Success, Failed, Cancelled };

// `cancel_flag` is optional. When non-null, the synthesis loop checks it
// at the top of each frame iteration; if set, the engine returns
// EngineResult::Cancelled without writing the output wav.
EngineResult run_warptempo_engine(const EngineParams& p,
                                  std::vector<int64_t>* out_frame_map = nullptr,
                                  int* out_R_s = nullptr,
                                  const std::atomic<bool>* cancel_flag = nullptr);
