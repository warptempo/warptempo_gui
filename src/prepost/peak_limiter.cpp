#include "peak_limiter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

constexpr double kAttFloor = 1e-12;

// Deliberate local duplicate of the temporary render_profile helper (avoids a
// cross-directory include from src/prepost into src/engine). True iff
// WARPTEMPO_PROFILE is exactly "1". Removed with the profiling facility.
bool profile_enabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("WARPTEMPO_PROFILE");
        return v && std::strcmp(v, "1") == 0;
    }();
    return enabled;
}

}  // namespace

void apply_peak_limiter(std::vector<float>& buffer, int channels,
                        int sample_rate, double ceiling_dbfs,
                        double attack_ms, double release_ms) {
    // Temporary render profiling: clock the whole stage. The unity-bypass scan
    // below is product behavior (not profile-gated); the profile line reports
    // its max, ceiling, and verdict when enabled.
    const bool prof = profile_enabled();
    std::chrono::steady_clock::time_point prof_t0;
    if (prof) prof_t0 = std::chrono::steady_clock::now();

    // Unity-bypass scan over the untouched buffer (one linear read before any
    // limiter allocation): on an already-compliant buffer (every sample finite
    // and within the ceiling) the limiter's gain never leaves 1.0, so the
    // buffer returns unchanged. A
    // non-finite sample falls through to the limiter (its hardclip backstop
    // owns that breach-class input). A nonqualifying buffer pays this extra read.
    const double ceiling = std::pow(10.0, ceiling_dbfs / 20.0);
    double max_abs = 0.0;
    bool   all_finite = true;
    for (float s : buffer) {
        if (!std::isfinite(s)) all_finite = false;
        const double a = std::fabs(static_cast<double>(s));
        if (a > max_abs) max_abs = a;
    }

    auto emit_profile = [&]() {
        const double wall = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - prof_t0).count();
        const bool bypass = all_finite && (max_abs <= ceiling);
        char line[256];
        std::snprintf(line, sizeof(line),
            "[profile] peak limiter: wall %.3fs max %.5f ceiling %.5f would-bypass %s",
            wall, max_abs, ceiling, bypass ? "yes" : "no");
        std::cerr << line << "\n";
    };

    if (all_finite && max_abs <= ceiling) {
        if (prof) emit_profile();
        return;  // buffer already within the ceiling; left untouched
    }

    const std::size_t total_frames =
        buffer.size() / static_cast<std::size_t>(channels);
    PeakLimiter pl(ceiling_dbfs, attack_ms, release_ms, sample_rate, channels);
    std::vector<float> out;
    out.reserve(buffer.size());
    auto sink = [&](const float* p, std::size_t n) {
        out.insert(out.end(), p, p + n * static_cast<std::size_t>(channels));
    };
    pl.process(buffer.data(), total_frames, sink);
    pl.flush(sink);
    buffer.swap(out);

    if (prof) emit_profile();
}

PeakLimiter::PeakLimiter(double ceiling_dbfs,
                         double attack_ms,
                         double release_ms,
                         int    sample_rate,
                         int    channels)
    : channels_(channels),
      ceiling_(std::pow(10.0, ceiling_dbfs / 20.0)),
      lookahead_frames_(std::max(1, static_cast<int>(
          std::llrint(attack_ms * sample_rate / 1000.0)))),
      release_frames_(std::max(1, static_cast<int>(
          std::llrint(release_ms * sample_rate / 1000.0)))),
      ring_(static_cast<std::size_t>(lookahead_frames_) *
            static_cast<std::size_t>(channels), 0.0f),
      ring_size_(lookahead_frames_),
      ring_write_(0),
      ring_count_(0),
      att_(1.0),
      delta_(0.0),
      target_countdown_(-1) {}

void PeakLimiter::emit_frame(int ring_idx) {
    const float* slot = &ring_[static_cast<std::size_t>(ring_idx) *
                               static_cast<std::size_t>(channels_)];
    for (int c = 0; c < channels_; ++c) {
        double v = static_cast<double>(slot[c]) * att_;
        // Hardclip backstop. Envelope math should keep |v| <= ceiling_;
        // this catches floating-point drift and pathological inputs.
        if (v >  ceiling_) v =  ceiling_;
        if (v < -ceiling_) v = -ceiling_;
        out_accum_.push_back(static_cast<float>(v));
    }
}

void PeakLimiter::advance_envelope() {
    att_ += delta_;
    if (att_ > 1.0) { att_ = 1.0; delta_ = 0.0; }
    if (att_ < kAttFloor) att_ = kAttFloor;
}

void PeakLimiter::process(
    const float* in, std::size_t n_frames,
    const std::function<void(const float*, std::size_t)>& write_cb) {
    out_accum_.clear();
    out_accum_.reserve(n_frames * static_cast<std::size_t>(channels_));
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
            emit_frame(ring_write_);
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

    if (!out_accum_.empty()) {
        write_cb(out_accum_.data(),
                 out_accum_.size() / static_cast<std::size_t>(channels_));
    }
}

void PeakLimiter::flush(
    const std::function<void(const float*, std::size_t)>& write_cb) {
    // Drain remaining ring contents. No new inputs arriving, so no further
    // peak observations; envelope continues with whatever delta is set.
    out_accum_.clear();
    while (ring_count_ > 0) {
        const int oldest =
            (ring_write_ - ring_count_ + ring_size_) % ring_size_;
        emit_frame(oldest);
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

    if (!out_accum_.empty()) {
        write_cb(out_accum_.data(),
                 out_accum_.size() / static_cast<std::size_t>(channels_));
    }
}
