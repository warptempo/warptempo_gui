#include "peak_limiter.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kAttFloor = 1e-12;

// File-local envelope state for one apply_peak_limiter run. Folds what was the
// PeakLimiter class's private state; the class had no consumer beyond this
// single one-shot in-place application, so it collapses to this struct plus the
// process-then-drain sequence below. Emitted frames append directly to the
// caller's `out` vector (no intermediate accumulation buffer, no chunk sink).
struct LimiterState {
    int    channels;
    double ceiling;          // linear, e.g. 10^(-0.3/20) for -0.3 dBFS
    int    lookahead_frames;
    int    release_frames;

    // Ring buffer of pending input frames. Size = lookahead_frames * channels.
    std::vector<float> ring;
    int                ring_size;   // == lookahead_frames
    int                ring_write;  // next write position (frame index)
    int                ring_count;  // current fill level (frames)

    // Envelope state. att in (0, 1].
    double att;
    double delta;
    int    target_countdown;        // frames until att should reach target_att

    void emit_frame(std::vector<float>& out, int ring_idx) const {
        const float* slot = &ring[static_cast<std::size_t>(ring_idx) *
                                  static_cast<std::size_t>(channels)];
        for (int c = 0; c < channels; ++c) {
            double v = static_cast<double>(slot[c]) * att;
            // Hardclip backstop. Envelope math should keep |v| <= ceiling;
            // this catches floating-point drift and pathological inputs.
            if (v >  ceiling) v =  ceiling;
            if (v < -ceiling) v = -ceiling;
            out.push_back(static_cast<float>(v));
        }
    }

    void advance_envelope() {
        att += delta;
        if (att > 1.0) { att = 1.0; delta = 0.0; }
        if (att < kAttFloor) att = kAttFloor;
    }
};

}  // namespace

void apply_peak_limiter(std::vector<float>& buffer, int channels,
                        int sample_rate) {
    // Unity-bypass scan over the untouched buffer (one linear read before any
    // limiter allocation): on an already-compliant buffer (every sample within
    // the ceiling) the limiter's gain never leaves 1.0, so the buffer returns
    // unchanged. Non-finite input cannot reach here — finish_render's buffer
    // contract (trimmer.cpp) refuses any non-finite sample before this runs —
    // so the ceiling compare alone decides the bypass. A nonqualifying buffer
    // pays this extra read.
    const double ceiling = std::pow(10.0, kPeakLimiterCeilingDbfs / 20.0);
    double max_abs = 0.0;
    for (float s : buffer) {
        const double a = std::fabs(static_cast<double>(s));
        if (a > max_abs) max_abs = a;
    }

    if (max_abs <= ceiling) {
        return;  // buffer already within the ceiling; left untouched
    }

    const std::size_t total_frames =
        buffer.size() / static_cast<std::size_t>(channels);

    LimiterState st{
        channels,
        ceiling,
        std::max(1, static_cast<int>(
            std::llrint(kPeakLimiterAttackMs * sample_rate / 1000.0))),
        std::max(1, static_cast<int>(
            std::llrint(kPeakLimiterReleaseMs * sample_rate / 1000.0))),
        {},   // ring, sized below
        0,    // ring_size, set below
        0,    // ring_write
        0,    // ring_count
        1.0,  // att
        0.0,  // delta
        -1,   // target_countdown
    };
    st.ring.assign(static_cast<std::size_t>(st.lookahead_frames) *
                       static_cast<std::size_t>(channels),
                   0.0f);
    st.ring_size = st.lookahead_frames;

    std::vector<float> out;
    out.reserve(buffer.size());

    // Process all frames: for each input frame observe its cross-channel peak,
    // adopt a steeper predicted-peak ramp if warranted, emit the about-to-be-
    // evicted oldest frame at current att, overwrite that slot, advance the
    // envelope, then step the release countdown.
    for (std::size_t i = 0; i < total_frames; ++i) {
        const float* frame =
            buffer.data() + i * static_cast<std::size_t>(channels);

        // Peak magnitude across channels at this input frame. Joint envelope:
        // a peak in any channel triggers gain reduction on all channels.
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c) {
            float a = std::fabs(frame[c]);
            if (a > peak) peak = a;
        }

        // Predicted-peak ramp-down. Only adopt if the new candidate delta is
        // steeper than what's currently scheduled — preserves the worst-case
        // attenuation across overlapping peaks in the lookahead window.
        if (static_cast<double>(peak) > st.ceiling) {
            double target_att = st.ceiling / static_cast<double>(peak);
            double candidate_delta = (target_att - st.att) /
                static_cast<double>(st.lookahead_frames);
            if (candidate_delta < st.delta) {
                st.delta = candidate_delta;
                st.target_countdown = st.lookahead_frames;
            }
        }

        // Emit the about-to-be-evicted oldest frame at current att, then
        // overwrite that slot with the new input. Skipped during the initial
        // lookahead fill (ring not yet full).
        if (st.ring_count == st.ring_size) {
            st.emit_frame(out, st.ring_write);
        }
        {
            float* slot = &st.ring[static_cast<std::size_t>(st.ring_write) *
                                   static_cast<std::size_t>(channels)];
            for (int c = 0; c < channels; ++c) slot[c] = frame[c];
            st.ring_write = (st.ring_write + 1) % st.ring_size;
            if (st.ring_count < st.ring_size) ++st.ring_count;
        }

        st.advance_envelope();

        // After advance: if the countdown just reached zero, att is now sitting
        // at target_att, and the very next emit (ring full → next iter) will be
        // the peak that triggered this ramp. Switch delta to release so that
        // post-emit advance walks att back up to unity over release_frames.
        if (st.target_countdown > 0) {
            --st.target_countdown;
            if (st.target_countdown == 0) {
                st.delta = (1.0 - st.att) /
                    static_cast<double>(st.release_frames);
                st.target_countdown = -1;
            }
        }
    }

    // Drain remaining ring contents. No new inputs arriving, so no further
    // peak observations; envelope continues with whatever delta is set.
    while (st.ring_count > 0) {
        const int oldest =
            (st.ring_write - st.ring_count + st.ring_size) % st.ring_size;
        st.emit_frame(out, oldest);
        --st.ring_count;
        st.advance_envelope();
        if (st.target_countdown > 0) {
            --st.target_countdown;
            if (st.target_countdown == 0) {
                st.delta = (1.0 - st.att) /
                    static_cast<double>(st.release_frames);
                st.target_countdown = -1;
            }
        }
    }

    buffer.swap(out);
}
