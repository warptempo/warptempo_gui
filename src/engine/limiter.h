#pragma once
#include "stft_container.h"
#include <vector>

// The spectral limiter's fixed parameters: a -0.3 dBFS ceiling and a 0.01 dB
// convergence tolerance. They were never settings keys and never varied
// per-call, so they are non-settings constants read directly by process()
// (mirroring the kPeakLimiter* constants in prepost/peak_limiter.h); the
// always-on spectral stage derives its ceiling and tolerance bands from them.
inline constexpr double kSpectralLimiterCeilingDbfs = -0.3;
inline constexpr double kSpectralLimiterToleranceDb = 0.01;

class Limiter {
public:
    // Post-render spectral limiter. `render` is the finished interleaved-float
    // OLA output (sample 0 = first emitted sample). Limits in
    // place: builds its own N=stft.N STFT of `render`, solves over a local
    // attenuation map, reconstructs, and writes the limited buffer back into
    // `render`. `stft` is read only for src_info (sample rate, channels),
    // N, and cancel_flag; the ceiling/tolerance are the kSpectralLimiter*
    // constants above. The limiter solves over its own local gain map and
    // reads nothing from the PV's 2N-padded grid.
    void process(AudioSTFT& stft, std::vector<float>& render);
};
