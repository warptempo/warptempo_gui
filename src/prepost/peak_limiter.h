#pragma once

#include <cstddef>
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
// Length-preserving: total emitted frames equals total input frames. Output
// sample N corresponds to input sample N (the internal lookahead delay is
// absorbed by the end-of-stream drain).

// The peak stage's parameters: 0.0 dBFS ceiling, 0.25 ms attack, 0.5 ms
// release — a pure clip net above the spectral limiter's -0.3 dBFS ceiling.
// They were never settings keys and stay non-settings constants; the always-on
// limiter runs the spectral and peak stages together.
inline constexpr double kPeakLimiterCeilingDbfs = 0.0;
inline constexpr double kPeakLimiterAttackMs    = 0.25;
inline constexpr double kPeakLimiterReleaseMs   = 0.5;

// Apply the peak limiter to an interleaved float buffer in place: one-shot,
// length-preserving, joint-channel envelope, ffmpeg-alimiter-shaped. Called by
// the shared post-engine chain (finish_render in trimmer.h) on whichever buffer
// the render fills — disk render or target view — after the post_trim crop,
// never before it, so the limiter's envelope sees exactly the delivered
// samples. The stage's ceiling/attack/release are the kPeakLimiter* constants
// above, read directly (they are fixed, never per-call flexibility).
void apply_peak_limiter(std::vector<float>& buffer, int channels,
                        int sample_rate);
