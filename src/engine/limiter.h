#pragma once
#include "stft_container.h"
#include <vector>

class Limiter {
public:
    // Post-render spectral limiter. `render` is the finished interleaved-float
    // OLA output (sample 0 = first emitted sample). Limits in
    // place: builds its own N=stft.N STFT of `render`, solves over a local
    // attenuation map, reconstructs, and writes the limited buffer back into
    // `render`. `stft` is read only for src_info (sample rate, channels),
    // limiter_params, N, and cancel_flag; the limiter solves over its own
    // local gain map and reads nothing from the PV's 2N-padded grid.
    void process(AudioSTFT& stft, std::vector<float>& render);
};
