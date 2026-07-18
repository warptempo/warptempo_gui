#pragma once
#include "stft_container.h"
#include <complex>
#include <cstddef>

class Synthesis {
public:
    // Run full OLA synthesis from frame 0 and append the emit to a caller-owned
    // interleaved-float vector — the engine's only emission path (buffer-out
    // only; encode lives orchestrator-side in the prepost chain). The
    // per-channel mono streams are interleaved after the channel threads join
    // and appended once as the full interleaved emit; the vector grows by
    // exactly emit-cap-bounded out_frames * channels floats, channel-interleaved
    // within each frame.
    //
    // The frame selected by a phase-reset placement is a seed frame: theta
    // re-grounds to the analysis phase phi, realigning synthesis phase with
    // the source at the authored position (that frame's analysis window
    // centers on it).
    //
    // Live progress % is printed via \r, terminating with 100%.
    void process_to_buffer(AudioSTFT& stft, std::vector<float>* output_buffer);
};
