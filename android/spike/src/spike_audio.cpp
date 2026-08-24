#include "spike_audio.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>

#include "spike_log.h"

namespace {

const char* format_name(int f) {
    switch (f) {
        case AAUDIO_FORMAT_PCM_I16: return "PCM_I16";
        case AAUDIO_FORMAT_PCM_FLOAT: return "PCM_FLOAT";
        case AAUDIO_FORMAT_PCM_I24_PACKED: return "PCM_I24_PACKED";
        case AAUDIO_FORMAT_PCM_I32: return "PCM_I32";
        case AAUDIO_FORMAT_INVALID: return "INVALID";
        case AAUDIO_FORMAT_UNSPECIFIED: return "UNSPECIFIED";
        default: return "?";
    }
}

const char* perf_name(int m) {
    switch (m) {
        case AAUDIO_PERFORMANCE_MODE_NONE: return "NONE";
        case AAUDIO_PERFORMANCE_MODE_POWER_SAVING: return "POWER_SAVING";
        case AAUDIO_PERFORMANCE_MODE_LOW_LATENCY: return "LOW_LATENCY";
        default: return "?";
    }
}

const char* sharing_name(int m) {
    switch (m) {
        case AAUDIO_SHARING_MODE_EXCLUSIVE: return "EXCLUSIVE";
        case AAUDIO_SHARING_MODE_SHARED: return "SHARED";
        default: return "?";
    }
}

std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return std::string(buf);
}

}  // namespace

SpikeAudio::~SpikeAudio() {
    std::lock_guard<std::mutex> lock(mutex_);
    close_locked();
}

bool SpikeAudio::load_wav(const void* bytes, size_t size) {
    std::string err;
    if (!spike_wav_parse(static_cast<const uint8_t*>(bytes), size, wav_, err)) {
        load_error_ = err;
        SPIKE_LOGE("wav load failed: %s", err.c_str());
        return false;
    }
    SPIKE_LOGI("wav: %d Hz, %d ch, %d-bit, %zu frames", wav_.sample_rate, wav_.channels,
               wav_.bits, wav_.frames());
    return true;
}

bool SpikeAudio::open_locked() {
    AAudioStreamBuilder* b = nullptr;
    aaudio_result_t r = AAudio_createStreamBuilder(&b);
    if (r != AAUDIO_OK || b == nullptr) {
        open_result_.store(r, std::memory_order_relaxed);
        return false;
    }

    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    // SHARED, not EXCLUSIVE: EXCLUSIVE is refused on the USB path and invites
    // extra disconnects (research §4.1). PERFORMANCE_MODE_NONE for the same
    // reason -- LOW_LATENCY is unavailable over USB and changes nothing about
    // resampling.
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_NONE);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    // The whole point of the probe: ask for nothing, report what is granted.
    AAudioStreamBuilder_setSampleRate(b, AAUDIO_UNSPECIFIED);
    AAudioStreamBuilder_setDeviceId(b, AAUDIO_UNSPECIFIED);
    AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_MEDIA);            // API 28
    AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);  // API 28
    AAudioStreamBuilder_setDataCallback(b, &SpikeAudio::data_cb, this);
    AAudioStreamBuilder_setErrorCallback(b, &SpikeAudio::error_cb, this);

    AAudioStream* s = nullptr;
    r = AAudioStreamBuilder_openStream(b, &s);
    AAudioStreamBuilder_delete(b);
    open_result_.store(r, std::memory_order_relaxed);
    if (r != AAUDIO_OK || s == nullptr) {
        SPIKE_LOGE("openStream failed: %d (%s)", r, AAudio_convertResultToText(r));
        return false;
    }

    // EVERY setter was a request. Read back the truth.
    const int rate = AAudioStream_getSampleRate(s);
    stream_rate_.store(rate, std::memory_order_relaxed);
    stream_channels_.store(AAudioStream_getChannelCount(s), std::memory_order_relaxed);
    stream_format_.store(AAudioStream_getFormat(s), std::memory_order_relaxed);
    device_id_.store(AAudioStream_getDeviceId(s), std::memory_order_relaxed);
    perf_mode_.store(AAudioStream_getPerformanceMode(s), std::memory_order_relaxed);
    sharing_mode_.store(AAudioStream_getSharingMode(s), std::memory_order_relaxed);

    const int burst = AAudioStream_getFramesPerBurst(s);
    burst_.store(burst, std::memory_order_relaxed);
    if (burst > 0) AAudioStream_setBufferSizeInFrames(s, burst * 2);
    buffer_size_.store(AAudioStream_getBufferSizeInFrames(s), std::memory_order_relaxed);

    // AAudioStream_getHardwareSampleRate is __INTRODUCED_IN(34) in the r29 headers
    // and minSdk here is 30, so it is reached as a WEAK reference behind
    // __builtin_available. The delta between it and getSampleRate() is exactly how
    // much framework SRC is still happening under us: worth knowing on the real
    // device, worth nothing if the symbol is absent (hw_rate_ then stays -1).
    //
    // The guard alone is NOT enough: without -D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__
    // bionic marks every newer symbol `strict`, and a strict-unavailable symbol is a
    // hard build error that __builtin_available cannot open. build_apk.sh passes it.
    if (__builtin_available(android 34, *)) {
        hw_rate_.store(AAudioStream_getHardwareSampleRate(s), std::memory_order_relaxed);
    }

    ratio_.store(rate > 0 ? static_cast<double>(wav_.sample_rate) / static_cast<double>(rate) : 1.0,
                 std::memory_order_relaxed);

    stream_ = s;
    SPIKE_LOGI("stream open: %d Hz, %d ch, %s, burst %d, deviceId %d", rate,
               stream_channels_.load(), format_name(stream_format_.load()), burst,
               device_id_.load());
    return true;
}

void SpikeAudio::close_locked() {
    if (!stream_) return;
    AAudioStream* s = stream_;
    stream_ = nullptr;
    AAudioStream_requestStop(s);
    AAudioStream_close(s);
    playing_.store(false, std::memory_order_relaxed);
}

void SpikeAudio::toggle() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_) {
        close_locked();
        return;
    }
    if (wav_.frames() == 0) return;
    position_.store(0.0, std::memory_order_relaxed);
    callbacks_.store(0, std::memory_order_relaxed);
    last_error_.store(0, std::memory_order_relaxed);
    if (!open_locked()) return;
    const aaudio_result_t r = AAudioStream_requestStart(stream_);
    if (r != AAUDIO_OK) {
        SPIKE_LOGE("requestStart failed: %d", r);
        close_locked();
        return;
    }
    playing_.store(true, std::memory_order_relaxed);
}

void SpikeAudio::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    close_locked();
}

aaudio_data_callback_result_t SpikeAudio::data_cb(AAudioStream*, void* user, void* audio,
                                                  int32_t frames) {
    auto* self = static_cast<SpikeAudio*>(user);
    float* out = static_cast<float*>(audio);
    const int nch = self->stream_channels_.load(std::memory_order_relaxed);
    const double ratio = self->ratio_.load(std::memory_order_relaxed);
    const size_t src_frames = self->wav_.frames();
    const float* src = self->wav_.samples.data();

    self->callbacks_.fetch_add(1, std::memory_order_relaxed);

    double pos = self->position_.load(std::memory_order_relaxed);
    bool done = false;

    for (int32_t i = 0; i < frames; ++i) {
        float l = 0.0f;
        float r = 0.0f;
        if (!done) {
            const size_t i0 = static_cast<size_t>(pos);
            if (i0 + 1 >= src_frames) {
                done = true;  // NOTHING LOOPS: the audition ends where the file ends.
            } else {
                const float frac = static_cast<float>(pos - static_cast<double>(i0));
                const float* a = src + i0 * 2;
                const float* b = a + 2;
                l = a[0] + (b[0] - a[0]) * frac;
                r = a[1] + (b[1] - a[1]) * frac;
                pos += ratio;
            }
        }
        float* f = out + static_cast<size_t>(i) * static_cast<size_t>(nch);
        if (nch == 1) {
            f[0] = 0.5f * (l + r);
        } else {
            f[0] = l;
            f[1] = r;
            for (int c = 2; c < nch; ++c) f[c] = 0.0f;
        }
    }

    self->position_.store(pos, std::memory_order_relaxed);
    if (done) {
        self->playing_.store(false, std::memory_order_relaxed);
        return AAUDIO_CALLBACK_RESULT_STOP;
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void SpikeAudio::error_cb(AAudioStream*, void* user, aaudio_result_t error) {
    auto* self = static_cast<SpikeAudio*>(user);
    self->last_error_.store(error, std::memory_order_relaxed);
    self->playing_.store(false, std::memory_order_relaxed);
    // The code is deliberately NOT switched on: a documented Android bug reports
    // TIMEOUT (-885) where DISCONNECTED (-899) is meant, so ANY error means tear
    // down. Stopping and closing must not happen on this thread -- hence a
    // detached one, which is Google's own recipe verbatim.
    std::thread([self] {
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->close_locked();
    }).detach();
}

std::vector<std::string> SpikeAudio::status_lines() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    if (!load_error_.empty()) {
        out.push_back("AAudio: no source -- " + load_error_);
        return out;
    }

    const int rate = stream_rate_.load(std::memory_order_relaxed);
    const int err = last_error_.load(std::memory_order_relaxed);
    const int open_r = open_result_.load(std::memory_order_relaxed);

    std::string state = playing() ? "PLAYING" : (stream_ ? "open/stopped" : "idle");
    out.push_back(fmt("AAudio: %s   source %d Hz %d ch %zu frames", state.c_str(),
                      wav_.sample_rate, wav_.channels, wav_.frames()));
    if (rate > 0) {
        const double ratio = ratio_.load(std::memory_order_relaxed);
        out.push_back(fmt("  granted %d Hz  %d ch  %s  burst %d  buf %d  deviceId %d",
                          rate, stream_channels_.load(), format_name(stream_format_.load()),
                          burst_.load(), buffer_size_.load(), device_id_.load()));
        out.push_back(fmt("  sharing %s  perf %s  hwRate %d",
                          sharing_name(sharing_mode_.load()), perf_name(perf_mode_.load()),
                          hw_rate_.load()));
        out.push_back(fmt("  app-side SRC %s (x%.6f)   callbacks %lld",
                          std::fabs(ratio - 1.0) < 1e-9 ? "OFF" : "ON", ratio,
                          callbacks_.load()));
    } else {
        out.push_back("  no stream opened yet -- tap PLAY WAV");
    }
    if (open_r != AAUDIO_OK && open_r != 0) {
        out.push_back(fmt("  last open result %d (%s)", open_r, AAudio_convertResultToText(open_r)));
    }
    if (err != 0) {
        out.push_back(fmt("  error callback %d (%s) -- stream torn down", err,
                          AAudio_convertResultToText(err)));
    }
    return out;
}
