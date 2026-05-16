#pragma once

#include <cstddef>
#include <functional>
#include <vector>

// Time-domain lookahead peak limiter. Replaces the ffmpeg alimiter
// subprocess used on trimmed renders, and is reused by the target render
// path (writes through an in-memory write_cb in that path).
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
    void emit_frame(int ring_idx,
                    const std::function<void(const float*, std::size_t)>& write_cb);
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

    // Single-frame emit scratch buffer (channels_ floats).
    std::vector<float> emit_buf_;
};
