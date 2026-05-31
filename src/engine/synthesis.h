#pragma once
#include "stft_container.h"
#include <complex>
#include <cstddef>
#include <functional>

class Synthesis {
public:
    void process(AudioSTFT& stft);

    // Sibling of process() that routes OLA output to a caller-owned
    // interleaved-float vector instead of an on-disk wav. Same OLA driver
    // (synthesize_full); no sf_open, no PeakLimiter. The write_cb appends
    // n_frames * channels floats to *output_buffer per chunk. The vector
    // ends up with exactly (num_synthesis_frames * R_s + N/2 - R_s) *
    // channels floats appended, channel-interleaved within each frame
    // (same layout the file path emits).
    void process_to_buffer(AudioSTFT& stft, std::vector<float>* output_buffer);

    // Write an already-rendered, spectral-limited buffer to the output wav,
    // running the time-domain peak limiter as an always-after backstop first.
    // Used by the Spectral disk path, where Pass 2 rendered into memory and
    // Pass 3's spectral limiter ran on that buffer in place. Output format is
    // 24-bit PCM (matches the non-None format decision in process()).
    void write_render_to_file(AudioSTFT& stft, const std::vector<float>& render);

    // Shared synthesis helper. Runs full OLA synthesis from frame 0 using
    // stft.attenuation_map. write_cb is invoked with per-iteration chunks (each
    // at most R_s frames; initial iterations contribute less during the N/2
    // start-trim, final flush is up to N - R_s frames). If spectra_cache is
    // non-null, it is filled with M[k]*exp(j*theta[k]) per (frame, channel, bin)
    // as a flat array of size num_frames * channels * (N/2+1).
    //
    // When a phase reset marker's synth_frame is encountered, theta_prev is reset
    // from the current phi_prev so that synthesis phase realigns with the
    // source at that frame.
    //
    // show_progress=true: print live progress % via \r, terminating with 100%.
    // pass_label: full prefix printed before each progress tick (caller provides
    //   trailing dots/space to reach the pipeline's 45-char status column).
    static void synthesize_full(
        AudioSTFT& stft,
        std::complex<float>* spectra_cache,
        std::function<void(const float*, size_t)> write_cb,
        bool show_progress,
        const char* pass_label);
};
