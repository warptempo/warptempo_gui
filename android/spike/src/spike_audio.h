// AAudio playback of the bundled 44.1 kHz WAV -- the spike's audio proof.
//
// THE PATH THIS SPIKE TAKES, and why (research §4.1 offered both):
//   The stream is opened with AAUDIO_UNSPECIFIED sample rate, the granted rate is
//   read back, and the 44.1 kHz source is resampled INTO that rate by the spike
//   itself (plain linear interpolation). Requesting 44100 would also have been
//   allowed for the spike, but it teaches nothing: Google measures an 8x
//   round-trip latency penalty for an app-requested 44.1 k stream, and the number
//   M4 actually needs to know is WHAT RATE THE DEVICE GRANTS. Opening unspecified
//   puts that number on screen. The linear interpolator here is a placeholder for
//   the M4 shim's real resampler, not a draft of it.
#pragma once

#include <aaudio/AAudio.h>

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include "spike_wav.h"

class SpikeAudio {
public:
    ~SpikeAudio();

    // Parse the bundled asset. Safe to call once at startup; the bytes are copied
    // out into floats, so the AAsset may be released afterwards.
    bool load_wav(const void* bytes, size_t size);

    // The tap act: start from the top if idle, stop if playing.
    void toggle();
    void stop();

    bool playing() const { return playing_.load(std::memory_order_relaxed); }

    // One block of on-screen text, newest facts first.
    std::vector<std::string> status_lines() const;

private:
    static aaudio_data_callback_result_t data_cb(AAudioStream* s, void* user, void* audio, int32_t frames);
    static void error_cb(AAudioStream* s, void* user, aaudio_result_t error);

    bool open_locked();
    void close_locked();

    SpikeWav wav_;
    std::string load_error_;

    mutable std::mutex mutex_;
    AAudioStream* stream_ = nullptr;

    // Read back from the stream after open -- never assumed.
    std::atomic<int> stream_rate_{0};
    std::atomic<int> stream_channels_{0};
    std::atomic<int> stream_format_{0};
    std::atomic<int> burst_{0};
    std::atomic<int> buffer_size_{0};
    std::atomic<int> device_id_{0};
    std::atomic<int> perf_mode_{0};
    std::atomic<int> sharing_mode_{0};
    std::atomic<int> hw_rate_{-1};

    std::atomic<bool> playing_{false};
    std::atomic<double> position_{0.0};   // source frames, fractional
    std::atomic<double> ratio_{1.0};      // source frames advanced per stream frame
    std::atomic<int> last_error_{0};
    std::atomic<int> open_result_{0};
    std::atomic<long long> callbacks_{0};
};
