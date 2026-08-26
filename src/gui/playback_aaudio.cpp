#include "playback.h"

#include "playback_common.h"

#include <aaudio/AAudio.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

// THE AAUDIO DEVICE HALF of playback.h's contract — the Android arm, replacing
// the silent stub the port ran on before there was a sound. The ENGINE is
// playback_common.{h,cpp}, shared verbatim with the JACK backend
// (playback.cpp), so what lives here is one AAudio output stream: opening it,
// its two callbacks, stop()'s quiescence fence, and the disconnect rule.
//
// THIS FILE IS NOT IN THE LINUX TARGET: exactly one of playback.cpp and this
// file is compiled into a given binary, the same one-arm-per-backend rule
// gui_font_fontconfig.cpp / gui_font_bundled.cpp follow.
//
// THE RATE. The stream is opened at AAUDIO_UNSPECIFIED and the GRANTED rate
// becomes the engine's `output_rate`, so the render body's one increment
// (`speed * source_rate / output_rate`, the head of playback_common.h) does the
// 44100 -> whatever-the-device-wants rescaling in the same multiply that does
// the speed rescaling, through the fractional read the body already performs.
// The tablet's own speaker grants 48000 and its hardware IS 48 k — 44.1 can
// never be native there — so this ratio is live on every play, and the
// architect's standing ruling is that the audition resample is preview-
// inaudible (nothing vendor-dependent, no second resampler). The ratio is
// recomputed at every open, which is what makes a reopen onto a DIFFERENT
// device (a DAC plugged in after a disconnect) correct without any other edit.
//
// THREADING, above playback.h's own thread model. Three threads touch this
// file:
//   * MAIN owns the stream handle outright — every AAudio call in this file
//     (open, requestStart, requestStop, waitForStateChange, close) is made
//     from init/play/stop/shutdown and from nowhere else. There is no mutex
//     and none is needed: no other thread reads `stream`.
//   * The DATA CALLBACK thread runs the engine's render body and touches
//     nothing else — no allocation, no I/O, no locks, no AAudio call.
//   * The ERROR CALLBACK thread writes three atomics and logs. It calls NO
//     AAudio stream function, which is Google's own rule and the reason the
//     spike used a detached thread to do its closing; this backend needs no
//     detached thread because it does not close there at all — the next
//     play() closes the dead stream on the main thread and reopens.

namespace {

const char* perf_mode_name(int32_t m) {
    switch (m) {
        case AAUDIO_PERFORMANCE_MODE_NONE:         return "NONE";
        case AAUDIO_PERFORMANCE_MODE_POWER_SAVING: return "POWER_SAVING";
        case AAUDIO_PERFORMANCE_MODE_LOW_LATENCY:  return "LOW_LATENCY";
        default:                                   return "?";
    }
}

const char* sharing_mode_name(int32_t m) {
    switch (m) {
        case AAUDIO_SHARING_MODE_EXCLUSIVE: return "EXCLUSIVE";
        case AAUDIO_SHARING_MODE_SHARED:    return "SHARED";
        default:                            return "?";
    }
}

}  // namespace

struct GuiPlayback::Impl {
    // The portable engine (playback_common.h). Everything else in this struct
    // is AAudio's own.
    GuiPlaybackState state;

    // MAIN THREAD ONLY (the head comment's threading block).
    AAudioStream* stream       = nullptr;
    bool          started      = false;
    // init() bound a source AND opened a device. play()/stop() gate on it
    // exactly as the JACK backend gates on its client being active.
    bool          device_ready = false;

    // Written by the error callback, read by the main thread. `dead` is the
    // disconnect latch: the stream is finished and only a close-and-reopen can
    // produce sound again.
    std::atomic<bool>    stream_dead{false};
    std::atomic<int32_t> stream_error{0};
};

namespace {

aaudio_data_callback_result_t playback_data_callback(AAudioStream* /*stream*/,
                                                     void* user,
                                                     void* audio_data,
                                                     int32_t num_frames) {
    auto* impl = static_cast<GuiPlayback::Impl*>(user);
    if (!impl) return AAUDIO_CALLBACK_RESULT_STOP;

    // ONE INTERLEAVED BUFFER, so the two channels are the same base pointer one
    // float apart and the stride is the channel count
    // (playback_write_silence's contract). init refuses a stream that was not
    // granted exactly two channels, so this pair is always the whole buffer.
    float* out = static_cast<float*>(audio_data);
    float* channel_buffers[kPlaybackOutputChannels] = { out, out + 1 };
    const int     channel_count = impl->state.channels;
    const int64_t frames        = static_cast<int64_t>(num_frames);

    if (!impl->state.playing.load(std::memory_order_acquire)) {
        playback_write_silence(channel_buffers, channel_count,
                               kPlaybackOutputChannels, 0, frames);
        // CONTINUE, never STOP: a stopped-by-the-callback stream would leave
        // the main thread's `started` flag lying about the device. The stream
        // is stopped from stop() alone, which every playback teardown reaches
        // — the natural end included, the run loop's own tick taking that edge
        // within one tick period.
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    playback_render_block(impl->state, channel_buffers,
                          kPlaybackOutputChannels, frames, channel_count);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void playback_error_callback(AAudioStream* /*stream*/, void* user,
                             aaudio_result_t error) {
    auto* impl = static_cast<GuiPlayback::Impl*>(user);
    if (!impl) return;

    // THE DISCONNECT (a headphone or USB DAC pulled, a route change the
    // framework cannot follow). The error code is deliberately NOT switched
    // on: a documented Android bug reports TIMEOUT where DISCONNECTED is
    // meant, so ANY error means the stream is finished.
    //
    // Three atomic stores and a log line; NO AAudio call, per the head
    // comment's threading rule. Clearing `output_rate` puts the engine in its
    // SUSPENDED state, which is exactly the behaviour playback.h's
    // graph-suspension clause asks for: the render body would emit silence and
    // hold, and cursor() holds at the last audio position instead of
    // extrapolating. Clearing `playing` makes is_playing() false, so the run
    // loop's next tick reads this as a natural end and tears the scanner down
    // through the product's one stop body.
    //
    // NOTHING RECOVERS BY ITSELF: no auto-resume, no reconnect timer, no
    // retry. The next play() closes this stream and opens a new one at the new
    // device's own granted rate. The user presses play again — the same answer
    // the product gives every other rare, loud fault.
    impl->stream_error.store(static_cast<int32_t>(error), std::memory_order_relaxed);
    impl->state.output_rate.store(0, std::memory_order_relaxed);
    impl->state.playing.store(false, std::memory_order_release);
    impl->stream_dead.store(true, std::memory_order_release);

    std::fprintf(stderr,
        "warptempo_gui: AAudio stream error %d (%s); stream is dead, playback "
        "stopped. The next play reopens the device.\n",
        static_cast<int>(error), AAudio_convertResultToText(error));
}

// THE QUIESCENCE FENCE (playback.h's stop() contract: "returns only once the
// callback has quiesced"). The JACK backend counts process cycles, which works
// because a JACK client's callback keeps running silently while the client is
// active. AAudio's does not: the data callback stops being called when the
// stream stops, so counting would never advance. The fence here is the STREAM
// STATE MACHINE — requestStop, then wait until the stream reports STOPPED,
// which AAudio reaches only after its callback thread has left the callback.
//
// The wait is UNBOUNDED in the same sense JACK's is: a TIMEOUT return re-waits
// forever rather than giving up, because returning into a buffer the callback
// may still be reading is the one thing the fence exists to prevent. It is not
// unbounded on a DEAD stream, and that is not a loophole: a disconnected
// stream has already had its callback thread retired by the framework before
// the error callback ran, so there is nothing left to wait for and waiting
// would hang forever on a quiesced device.
void fence_stopped(GuiPlayback::Impl& impl) {
    if (!impl.stream) {
        impl.started = false;
        return;
    }
    AAudioStream_requestStop(impl.stream);

    aaudio_stream_state_t now = AAudioStream_getState(impl.stream);
    while (now != AAUDIO_STREAM_STATE_STOPPED &&
           now != AAUDIO_STREAM_STATE_DISCONNECTED &&
           now != AAUDIO_STREAM_STATE_CLOSING &&
           now != AAUDIO_STREAM_STATE_CLOSED &&
           now != AAUDIO_STREAM_STATE_UNINITIALIZED) {
        aaudio_stream_state_t next = AAUDIO_STREAM_STATE_UNINITIALIZED;
        const aaudio_result_t r = AAudioStream_waitForStateChange(
            impl.stream, now, &next, 100 * 1000 * 1000L);  // 100 ms per wait
        if (r == AAUDIO_OK) {
            now = next;
        } else if (r == AAUDIO_ERROR_TIMEOUT) {
            now = AAudioStream_getState(impl.stream);  // re-wait, no deadline
        } else {
            break;  // dead or disconnected: already quiesced
        }
    }
    impl.started = false;
}

void close_stream(GuiPlayback::Impl& impl) {
    if (!impl.stream) {
        impl.started = false;
        return;
    }
    AAudioStream* s = impl.stream;
    impl.stream = nullptr;
    impl.started = false;
    // Suspended for the engine the moment the device leaves (the error
    // callback's own clause), so a cursor read across the close holds.
    impl.state.output_rate.store(0, std::memory_order_relaxed);
    impl.stream_dead.store(false, std::memory_order_relaxed);
    impl.stream_error.store(0, std::memory_order_relaxed);
    AAudioStream_requestStop(s);
    AAudioStream_close(s);  // blocks until the callback thread is gone
}

// Open one output stream and publish its GRANTED rate as the engine's output
// rate. EVERY SETTER IS A REQUEST: the rate, the performance mode and the
// sharing mode are all asked for and read back, and only the format and the
// channel count are load-bearing enough to refuse over — the render body
// writes interleaved stereo floats and has no other shape.
bool open_stream(GuiPlayback::Impl& impl) {
    AAudioStreamBuilder* builder = nullptr;
    aaudio_result_t r = AAudio_createStreamBuilder(&builder);
    if (r != AAUDIO_OK || builder == nullptr) {
        std::fprintf(stderr,
            "warptempo_gui: AAudio stream builder failed (%s); "
            "playback disabled.\n", AAudio_convertResultToText(r));
        return false;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    // SHARED, not EXCLUSIVE: exclusive mode is refused outright on the USB
    // path and invites extra disconnects everywhere, and it buys nothing the
    // engine can use.
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    // LOW_LATENCY is a REQUEST and the granted mode is logged below. It costs
    // nothing when refused (the framework falls back to NONE) and shortens the
    // gap between a play() and its first sound when granted, which is the
    // whole feel of auditioning an edit.
    AAudioStreamBuilder_setPerformanceMode(builder,
                                           AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, kPlaybackOutputChannels);
    // UNSPECIFIED, and the granted rate becomes output_rate: asking for 44100
    // on 48 k hardware only moves the resample into the framework, where it is
    // neither ours to reason about nor better than the render body's own.
    AAudioStreamBuilder_setSampleRate(builder, AAUDIO_UNSPECIFIED);
    AAudioStreamBuilder_setDeviceId(builder, AAUDIO_UNSPECIFIED);
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_MEDIA);
    AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_MUSIC);
    AAudioStreamBuilder_setDataCallback(builder, playback_data_callback, &impl);
    AAudioStreamBuilder_setErrorCallback(builder, playback_error_callback, &impl);

    AAudioStream* stream = nullptr;
    r = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (r != AAUDIO_OK || stream == nullptr) {
        std::fprintf(stderr,
            "warptempo_gui: AAudio openStream failed (%s); playback disabled.\n",
            AAudio_convertResultToText(r));
        return false;
    }

    const int32_t        rate     = AAudioStream_getSampleRate(stream);
    const int32_t        chans    = AAudioStream_getChannelCount(stream);
    const aaudio_format_t format  = AAudioStream_getFormat(stream);
    if (rate <= 0 || chans != kPlaybackOutputChannels ||
        format != AAUDIO_FORMAT_PCM_FLOAT) {
        std::fprintf(stderr,
            "warptempo_gui: AAudio granted an unusable stream "
            "(rate=%d, channels=%d, format=%d; wanted PCM_FLOAT stereo at any "
            "rate); playback disabled.\n",
            static_cast<int>(rate), static_cast<int>(chans),
            static_cast<int>(format));
        AAudioStream_close(stream);
        return false;
    }

    // Two bursts of buffering. One burst underruns on any scheduling hiccup;
    // more than two buys latency the audition does not want.
    const int32_t burst = AAudioStream_getFramesPerBurst(stream);
    if (burst > 0) AAudioStream_setBufferSizeInFrames(stream, burst * 2);

    impl.stream  = stream;
    impl.started = false;
    impl.stream_dead.store(false, std::memory_order_relaxed);
    impl.stream_error.store(0, std::memory_order_relaxed);
    impl.state.output_rate.store(static_cast<uint32_t>(rate),
                                 std::memory_order_relaxed);

    // getHardwareSampleRate is API 34 and minSdk here is 30, so it is a WEAK
    // reference behind __builtin_available (the build passes
    // -D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__, without which a newer symbol
    // is a hard build error no guard can open). Its delta from the granted
    // rate is exactly how much framework resampling is happening UNDER the
    // render body's own — worth knowing in the log, worth nothing if absent.
    int32_t hardware_rate = -1;
    if (__builtin_available(android 34, *)) {
        hardware_rate = AAudioStream_getHardwareSampleRate(stream);
    }

    std::fprintf(stderr,
        "warptempo_gui: Audio backend: AAudio, output_sample_rate=%d, "
        "source_sample_rate=%d, channels=%d\n",
        static_cast<int>(rate), impl.state.source_rate,
        static_cast<int>(chans));
    std::fprintf(stderr,
        "warptempo_gui: AAudio granted: burst=%d, buffer=%d, perf=%s, "
        "sharing=%s, deviceId=%d, hwRate=%d\n",
        static_cast<int>(burst),
        static_cast<int>(AAudioStream_getBufferSizeInFrames(stream)),
        perf_mode_name(AAudioStream_getPerformanceMode(stream)),
        sharing_mode_name(AAudioStream_getSharingMode(stream)),
        static_cast<int>(AAudioStream_getDeviceId(stream)),
        static_cast<int>(hardware_rate));
    return true;
}

}  // namespace

GuiPlayback::GuiPlayback() : impl_(std::make_unique<Impl>()) {}
GuiPlayback::~GuiPlayback() { shutdown(); }

bool GuiPlayback::init(int sample_rate, int channels, const float* samples,
                       int64_t total_frames, int64_t domain_offset) {
    shutdown(); // idempotent

    if (!playback_bind_and_validate(impl_->state, sample_rate, channels,
                                    samples, total_frames, domain_offset)) {
        return false;
    }
    if (!open_stream(*impl_)) {
        playback_clear_binding(impl_->state);
        return false;
    }
    impl_->device_ready = true;
    return true;
}

void GuiPlayback::play(int64_t start_sample, int64_t end_sample) {
    if (!impl_->device_ready) return;

    // A dead stream is closed and reopened HERE, on the main thread, at the
    // new device's own granted rate (the disconnect rule at the error
    // callback). A failed reopen leaves playback silently disabled until the
    // next press — no retry, no timer.
    if (impl_->stream_dead.load(std::memory_order_acquire)) {
        close_stream(*impl_);
    }
    if (!impl_->stream && !open_stream(*impl_)) return;

    // Publish FIRST, start SECOND: the range/anchor/pending-start block is
    // what the callback's acquire gate is waiting to see, and a refused range
    // must not spin the device up at all.
    if (!playback_publish_play(impl_->state, start_sample, end_sample)) return;

    if (!impl_->started) {
        const aaudio_result_t r = AAudioStream_requestStart(impl_->stream);
        if (r != AAUDIO_OK) {
            std::fprintf(stderr,
                "warptempo_gui: AAudio requestStart failed (%s); playback "
                "stopped.\n", AAudio_convertResultToText(r));
            impl_->state.playing.store(false, std::memory_order_release);
            close_stream(*impl_);
            return;
        }
        impl_->started = true;
    }
}

void GuiPlayback::resync_predictor() {
    if (!impl_) return;
    playback_resync_predictor(impl_->state);
}

void GuiPlayback::stop() {
    if (!impl_->device_ready) return;
    impl_->state.playing.store(false, std::memory_order_seq_cst);
    fence_stopped(*impl_);
}

void GuiPlayback::set_speed(float speed) {
    playback_set_speed(impl_->state, speed);
}

bool GuiPlayback::is_playing() const {
    return playback_is_playing(impl_->state);
}

int64_t GuiPlayback::cursor() const {
    if (!impl_) return 0;
    return playback_cursor(impl_->state);
}

double GuiPlayback::cursor_precise() const {
    if (!impl_) return 0.0;
    return playback_cursor_precise(impl_->state);
}

int64_t GuiPlayback::domain_begin() const {
    if (!impl_) return 0;
    return playback_domain_begin(impl_->state);
}

int64_t GuiPlayback::domain_end() const {
    if (!impl_) return 0;
    return playback_domain_end(impl_->state);
}

void GuiPlayback::rebind_buffer(const float* samples, int64_t total_frames,
                                int64_t domain_offset) {
    if (!impl_) return;
    playback_rebind_buffer(impl_->state, samples, total_frames, domain_offset);
}

void GuiPlayback::shutdown() {
    if (!impl_) return;
    impl_->state.playing.store(false, std::memory_order_relaxed);
    close_stream(*impl_);  // stops and joins the callback thread
    impl_->device_ready = false;
    playback_clear_binding(impl_->state);
}
