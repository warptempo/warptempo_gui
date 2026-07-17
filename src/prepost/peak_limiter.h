#pragma once

#include <cstddef>
#include <functional>
#include <vector>

// Time-domain lookahead peak limiter — the prepost pipeline's final limiting
// stage, applied to the engine's emitted buffer (after the post_trim crop on
// trimmed renders). It sits outside the
// engine: src/engine/ is pure DSP (analysis, PGHI, synthesis, spectral
// limiter, map-extent emission), and the peak stage runs orchestrator-side
// through apply_peak_limiter below. It always runs (its internal hard clipper
// included), the pure clip net above the spectral limiter.
//
// Algorithm: single-stage lookahead with predicted-peak ramp-down and
// exponential release. Follows ffmpeg alimiter's core shape with shorter
// time constants tuned for sparse rogue OLA overshoots, not loudness
// maximization. Joint-channel envelope (one envelope across all channels
// per frame) preserves the stereo image during limiting events.
//
// Length-preserving: total emitted frames over process() + flush() equals
// total input frames. Output sample N corresponds to input sample N (the
// internal lookahead delay is absorbed by flush() at end-of-stream).
class PeakLimiter {
public:
    PeakLimiter(double ceiling_dbfs,
                double attack_ms,
                double release_ms,
                int    sample_rate,
                int    channels);

    // Process `n_frames` of interleaved input samples. Emits limited
    // output via write_cb in chunks (zero during the initial lookahead
    // fill, frame-for-frame thereafter).
    void process(const float* in, std::size_t n_frames,
                 const std::function<void(const float*, std::size_t)>& write_cb);

    // Drain the remaining `lookahead_frames` samples from the internal
    // ring buffer through write_cb. Call once at end-of-stream.
    void flush(const std::function<void(const float*, std::size_t)>& write_cb);

private:
    void emit_frame(int ring_idx);
    void advance_envelope();

    int    channels_;
    double ceiling_;        // linear, e.g. 10^(-0.3/20) for -0.3 dBFS
    int    lookahead_frames_;
    int    release_frames_;

    // Ring buffer of pending input frames. Size = lookahead_frames_ * channels_.
    std::vector<float> ring_;
    int                ring_size_;     // == lookahead_frames_
    int                ring_write_;    // next write position (frame index)
    int                ring_count_;    // current fill level (frames)

    // Envelope state. att in (0, 1].
    double att_;
    double delta_;
    int    target_countdown_;  // frames until att should reach target_att_

    // Per-call accumulation buffer: interleaved limited output, one
    // process()/flush() call's worth. Flushed to write_cb once per call.
    std::vector<float> out_accum_;
};

// The peak stage's parameters: 0.0 dBFS ceiling, 0.25 ms attack, 0.5 ms
// release — a pure clip net above the spectral limiter's -0.3 dBFS ceiling.
// They were never settings keys and stay non-settings constants; the always-on
// limiter runs the spectral and peak stages together.
inline constexpr double kPeakLimiterCeilingDbfs = 0.0;
inline constexpr double kPeakLimiterAttackMs    = 0.25;
inline constexpr double kPeakLimiterReleaseMs   = 0.5;

// Apply the peak limiter to an interleaved float buffer in place: one-shot
// process + flush, length-preserving, sample-for-sample the class's streaming
// application. Called by the shared post-engine chain (finish_render in
// trimmer.h) on whichever buffer the render fills — disk render or target
// view — after the post_trim crop, never before it, so the limiter's envelope
// sees exactly the delivered samples. The stage's ceiling/attack/release are
// the kPeakLimiter* constants above, read directly (they are fixed, never
// per-call flexibility).
void apply_peak_limiter(std::vector<float>& buffer, int channels,
                        int sample_rate);
