#pragma once
#include "stft_container.h"
#include <complex>
#include <cstddef>
#include <functional>

class Synthesis {
public:
    // Route OLA output to a caller-owned interleaved-float vector — the
    // engine's only emission path (buffer-out only; encode lives
    // orchestrator-side in the prepost chain). Runs synthesize_full with an
    // append write_cb; the vector ends up with exactly emit-cap-bounded
    // out_frames * channels floats, channel-interleaved within each frame.
    void process_to_buffer(AudioSTFT& stft, std::vector<float>* output_buffer);

    // Shared synthesis helper. Runs full OLA synthesis from frame 0. The
    // per-channel mono streams are interleaved after the channel threads join
    // and write_cb is invoked once with the full interleaved emit.
    //
    // The frame selected by a phase-reset placement is a seed frame: theta
    // re-grounds to the analysis phase phi, realigning synthesis phase with
    // the source at the authored position (that frame's analysis window
    // centers on it).
    //
    // show_progress=true: print live progress % via \r, terminating with 100%.
    // pass_label: full prefix printed before each progress tick (caller provides
    //   trailing dots/space to reach the pipeline's 45-char status column).
    static void synthesize_full(
        AudioSTFT& stft,
        std::function<void(const float*, size_t)> write_cb,
        bool show_progress,
        const char* pass_label);
};
