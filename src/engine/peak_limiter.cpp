#include "peak_limiter.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kAttFloor = 1e-12;

}  // namespace

PeakLimiter::PeakLimiter(double ceiling_dbfs,
                         double attack_ms,
                         double release_ms,
                         int    sample_rate,
                         int    channels)
    : channels_(channels),
      ceiling_(std::pow(10.0, ceiling_dbfs / 20.0)),
      lookahead_frames_(std::max(1, static_cast<int>(
          std::lround(attack_ms * sample_rate / 1000.0)))),
      release_frames_(std::max(1, static_cast<int>(
          std::lround(release_ms * sample_rate / 1000.0)))),
      ring_(static_cast<std::size_t>(lookahead_frames_) *
            static_cast<std::size_t>(channels), 0.0f),
      ring_size_(lookahead_frames_),
      ring_write_(0),
      ring_count_(0),
      att_(1.0),
      delta_(0.0),
      target_countdown_(-1),
      emit_buf_(static_cast<std::size_t>(channels), 0.0f) {}

void PeakLimiter::emit_frame(
    int ring_idx,
    const std::function<void(const float*, std::size_t)>& write_cb) {
    const float* slot = &ring_[static_cast<std::size_t>(ring_idx) *
                               static_cast<std::size_t>(channels_)];
    for (int c = 0; c < channels_; ++c) {
        double v = static_cast<double>(slot[c]) * att_;
        // Hardclip backstop. Envelope math should keep |v| <= ceiling_;
        // this catches floating-point drift and pathological inputs.
        if (v >  ceiling_) v =  ceiling_;
        if (v < -ceiling_) v = -ceiling_;
        emit_buf_[static_cast<std::size_t>(c)] = static_cast<float>(v);
    }
    write_cb(emit_buf_.data(), 1);
}

void PeakLimiter::advance_envelope() {
    att_ += delta_;
    if (att_ > 1.0) { att_ = 1.0; delta_ = 0.0; }
    if (att_ < kAttFloor) att_ = kAttFloor;
}

void PeakLimiter::process(
    const float* in, std::size_t n_frames,
    const std::function<void(const float*, std::size_t)>& write_cb) {
    for (std::size_t i = 0; i < n_frames; ++i) {
        const float* frame = in + i * static_cast<std::size_t>(channels_);

        // Peak magnitude across channels at this input frame. Joint envelope:
        // a peak in any channel triggers gain reduction on all channels.
        float peak = 0.0f;
        for (int c = 0; c < channels_; ++c) {
            float a = std::fabs(frame[c]);
            if (a > peak) peak = a;
        }

        // Predicted-peak ramp-down. Only adopt if the new candidate delta is
        // steeper than what's currently scheduled — preserves the worst-case
        // attenuation across overlapping peaks in the lookahead window.
        if (static_cast<double>(peak) > ceiling_) {
            double target_att = ceiling_ / static_cast<double>(peak);
            double candidate_delta = (target_att - att_) /
                static_cast<double>(lookahead_frames_);
            if (candidate_delta < delta_) {
                delta_ = candidate_delta;
                target_countdown_ = lookahead_frames_;
            }
        }

        // Emit the about-to-be-evicted oldest frame at current att, then
        // overwrite that slot with the new input. Skipped during the initial
        // lookahead fill (ring not yet full).
        if (ring_count_ == ring_size_) {
            emit_frame(ring_write_, write_cb);
        }
        {
            float* slot = &ring_[static_cast<std::size_t>(ring_write_) *
                                  static_cast<std::size_t>(channels_)];
            for (int c = 0; c < channels_; ++c) slot[c] = frame[c];
            ring_write_ = (ring_write_ + 1) % ring_size_;
            if (ring_count_ < ring_size_) ++ring_count_;
        }

        advance_envelope();

        // After advance: if the countdown just reached zero, att is now sitting
        // at target_att, and the very next emit (ring full → next iter) will be
        // the peak that triggered this ramp. Switch delta to release so that
        // post-emit advance walks att back up to unity over release_frames_.
        if (target_countdown_ > 0) {
            --target_countdown_;
            if (target_countdown_ == 0) {
                delta_ = (1.0 - att_) /
                    static_cast<double>(release_frames_);
                target_countdown_ = -1;
            }
        }
    }
}

void PeakLimiter::flush(
    const std::function<void(const float*, std::size_t)>& write_cb) {
    // Drain remaining ring contents. No new inputs arriving, so no further
    // peak observations; envelope continues with whatever delta is set.
    while (ring_count_ > 0) {
        const int oldest =
            (ring_write_ - ring_count_ + ring_size_) % ring_size_;
        emit_frame(oldest, write_cb);
        --ring_count_;
        advance_envelope();
        if (target_countdown_ > 0) {
            --target_countdown_;
            if (target_countdown_ == 0) {
                delta_ = (1.0 - att_) /
                    static_cast<double>(release_frames_);
                target_countdown_ = -1;
            }
        }
    }
}
