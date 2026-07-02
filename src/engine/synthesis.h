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
    // (synthesize_full); no file writer, no PeakLimiter. The write_cb appends to
    // *output_buffer. The vector ends up with exactly emit-cap-bounded
    // out_frames * channels floats, channel-interleaved within each frame
    // (same layout the file path emits).
    void process_to_buffer(AudioSTFT& stft, std::vector<float>* output_buffer);

    // Write an already-limited buffer (spectral + peak backstop both applied by
    // the engine in place) to the output wav. Used by the limiter-on disk path,
    // where Pass 2 rendered into memory and Pass 3 ran the limited chain on that
    // buffer. Plain buffer-to-file write — no limiter here. Output format is
    // 24-bit PCM (matches the limiter-on format decision in process()).
    void write_render_to_file(AudioSTFT& stft, const std::vector<float>& render);

    // Shared synthesis helper. Runs full OLA synthesis from frame 0. The
    // per-channel mono streams are interleaved after the channel threads join
    // and write_cb is invoked once with the full interleaved emit.
    //
    // A frame following a phase-reset marker is a seed frame: theta re-grounds
    // to the analysis phase phi so synthesis phase realigns with the source at
    // that frame.
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

// Apply the peak-limiter backstop to `buf` in place. One-shot process+flush, so
// it is sample-for-sample identical to the previous streaming application in
// write_render_to_file. Ceiling = stft.peak_limiter_ceiling_dbfs (hardcoded 0
// dBFS) — a pure clip net above the spectral limiter's -0.3. Called by the
// engine on whichever buffer (disk render or target-view) the limited chain
// fills.
void apply_peak_backstop(AudioSTFT& stft, std::vector<float>& buf);
