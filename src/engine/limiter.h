#pragma once
#include "stft_container.h"
#include <cstdint>
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
    //
    // `engaged_frames` is the limiter's one report of WHERE IT ENGAGED
    // (architect approval 2026-11-05, for the GUI's limiter lamp): the
    // render-frame positions — indices into `render`'s frames, post coords —
    // at which the solver resolved a peak, cap-retired peaks included, one
    // entry per engaged place (duplicates across passes and channels
    // collapsed; the channel is not reported), sorted ascending. Cleared at
    // entry and left EMPTY on every road that applies no attenuation — the
    // already-compliant return, the 0-peaks return — and on cancellation.
    // Reporting is a read of the solver's own `resolved` list after the
    // fact: it changes no arithmetic, no ordering and no I/O, so the render
    // bytes are untouched.
    void process(AudioSTFT& stft, std::vector<float>& render,
                 std::vector<int64_t>& engaged_frames);
};
