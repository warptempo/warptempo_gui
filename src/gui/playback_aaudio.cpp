#include "playback.h"

#include "playback_common.h"

#include <aaudio/AAudio.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

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
// (`source_rate / output_rate`, the head of playback_common.h) does the
// 44100 -> whatever-the-device-wants rescaling through the fractional read the
// body already performs.
// The tablet's own speaker grants 48000 and its hardware IS 48 k — 44.1 can
// never be native there — so this ratio is live on every play, and the
// architect's standing ruling is that the audition resample is preview-
// inaudible (nothing vendor-dependent, no second resampler). The ratio is
// recomputed at every open, which is what makes a reopen onto a DIFFERENT
// device (a DAC plugged in after a disconnect) correct without any other edit.
//
// THE STREAM'S LIFECYCLE: OPENED ONCE, STARTED ONCE, NEVER STOPPED BETWEEN
// PLAYS (architect 2026-08-27, on glass — "a little click sound, like a
// keyboard's haptic click" at the start of every audition). STARTING AN AAUDIO
// STREAM IS AUDIBLE: the framework brings the output path up and the device's
// own unmute transient rides out with the first frames. The backend used to
// start the stream at every play() and stop it at every stop(), so every
// audition began with one of those transients — the click. It now starts the
// stream ONCE, at open (init, and the reopen after a disconnect), and stops it
// only where the stream is about to be CLOSED: shutdown, and the dead-stream
// reopen. Both live in close_stream, which holds one of the file's two
// requestStop calls — the other is suspend_stream's, the render player's own
// narrowing recorded below. `start_stream` is idempotent on `started`, so the
// three sites that call it — init's own, the launch gate's
// (ensure_device_available_for_play, where a reopened stream's start is what
// the gate's answer rests on) and play()'s retry for the roads that reach it
// ungated — are one road and an ordinary play makes no device call at all.
//
// THE RENDER PLAYER'S PAUSE IS THE ONE NARROWING OF THAT RULING (architect
// 2026-09-04, after a road test in the car). A stream that stays started while
// the player is paused keeps writing silence into the Bluetooth link, so the
// head unit sees an active player under a session that says paused and
// resolves the contradiction by flipping its display back to playing — after
// which its one toggle button sends the wrong direction forever. So the
// player's rest reaches the device: suspend_stream (below) requests the stream
// stopped and clears `started`, and the next play() starts it again through
// start_stream, which is the same road a reopen takes. It is called from the
// stop body's player fork alone (playback_lifecycle.cpp), so the main window's
// auditions, its Space and the A/B act keep the no-click lifecycle exactly as
// the 2026-08-27 ruling left it. The architect's words for the trade: "all I
// need is for the car and the player to work as expected", the resume
// transient on the tablet's own speaker accepted outright ("I don't use the
// speakers ever — leave it") and the Bluetooth resume delay accepted only as
// the moment the link takes to come back after a pause.
//
// So the transient happens once per session, at launch, with no audition under
// it, and no play() or stop() touches the device's run state at all. BETWEEN
// PLAYS THE STREAM RUNS AND THE CALLBACK WRITES SILENCE — its gate on the
// session word's playing bit
// takes the silence arm and the render body is never reached, so an idle stream
// reads no samples and costs the tablet a few mW of an already screen-on
// device, which is the price the architect accepted for the click. (A
// step-shaped click at the START of a play, from the render body beginning
// mid-waveform at whatever sample value sits there with no ramp, is a
// different thing and is untouched here.)
//
// THREADING, above playback.h's own thread model. Three threads touch this
// file:
//   * MAIN owns the stream handle outright — every AAudio call in this file
//     (open, requestStart, getState, requestStop, close) is made from
//     init/ensure_device_available_for_play/play/stop/shutdown and from
//     nowhere else. There is no mutex and none is needed: no other thread
//     reads `stream`.
//   * The DATA CALLBACK thread runs the engine's render body and touches
//     nothing else — no allocation, no I/O, no locks, no AAudio call.
//   * The ERROR CALLBACK thread writes three atomics and logs. It calls NO
//     AAudio stream function, which is Google's own rule and the reason the
//     spike used a detached thread to do its closing; this backend needs no
//     detached thread because it does not close there at all — the next
//     launch press (ensure_device_available_for_play) or play() closes the
//     dead stream on the main thread and reopens.

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
    //
    // THIS BACKEND REPORTS NO LATENCY FIGURE, and after 2026-09-03 there is
    // nothing on the other side of that to record: neither backend
    // compensates for anything, both run the raw predictor, and the line
    // leads the sound by the route's own latency on both (the ruling and its
    // arithmetic are at playback_common.cpp's record above
    // playback_publish_play). What stays worth saying is WHY no figure would
    // be published here even if one were wanted: in the car the tablet plays
    // over Bluetooth, whose link delay is large and variable, and the figures
    // the framework offers (AAudioStream_getTimestamp, its own latency
    // estimate) do not carry it — they are REPORTED AND UNTRUSTWORTHY on that
    // route rather than absent, so any figure would be a guess that is wrong
    // most of the time. On the tablet's own SPEAKER the latency is by contrast
    // stable and reportable (the 10–25 ms class), so a figure exists for one
    // route and not the other, and an answer that depends on where the sound
    // happens to be going is worse than no answer.
    //
    // WHAT THE EYE GETS ON THIS PLATFORM is smaller than the audio lead,
    // because the display lag runs the other way: the painted position is
    // ahead by L_audio and the pixel arrives L_display late, so the error at
    // light is L_audio − L_display — 10–25 ms against SurfaceFlinger's
    // 22–33 ms, roughly −23…+3 ms net. And IN THE CAR IT COSTS NOTHING AT ALL
    // (recorded 2026-09-02 from the truthfulness deep dive's item I): there
    // the render player stands, and under it the waveform scanner is not
    // sampled or painted (main.cpp's pre-paint hook returns above it) — the
    // only moving picture is the modal row's play-scrub and its clock, neither
    // registered against a waveform.
    GuiPlaybackState state;

    // MAIN THREAD ONLY (the head comment's threading block). `started` is
    // true from the stream's one successful requestStart until the stream is
    // CLOSED — no longer a per-play bit (the lifecycle block at the head).
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

    // Incremented once at the end of every data-callback invocation, playing
    // or silent. stop()'s fence counts it (fence_quiesced) — the JACK
    // backend's own proof, available here only because the stream now stays
    // STARTED between plays and so keeps calling its callback. Written by the
    // callback thread with release, read by the main thread with acquire.
    std::atomic<uint64_t> callback_cycles{0};

    // THE SESSION'S UNDERRUN FLOOR, main thread only. The stream's own counter
    // is cumulative and monotonic for the stream's life, so what a session is
    // worth is a DIFFERENCE — and the subtrahend has to be the count AT THE
    // LAUNCH, not at the previous stop: the stream stays STARTED between plays
    // and keeps issuing silent callbacks (the lifecycle block at the head of
    // this file), so an underrun in an idle interval — the whole stretch
    // before the first play included — belongs to no session at all and must
    // not be charged to the audio the user just heard.
    //
    // -1 IS NO SESSION STANDING, which is the state between a stop and the
    // next launch and the state a fresh stream opens in. THREE WRITERS: play()
    // stamps the launch's count at its tail, report_xrun_count (stop()'s tail)
    // says the difference and voids it back to -1, and close_stream voids it
    // with the stream the counter belongs to. Diagnostic only: no card, no
    // state, no behaviour reads it.
    int32_t xrun_at_launch = -1;
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

    // THE GATE is ONE acquire load of the session word (playback_common.h),
    // the JACK callback's own: the callback gates on its playing bit and
    // hands that same word to the render body as its terminal's expected
    // value and as the one generation whose command packet the fill may
    // consume — loaded here and nowhere later in the burst, so a stop or a
    // publish after this load changes the word and fails the fill's
    // terminal, the fill renders under its own generation's window
    // regardless, and the next callback acquires and seats the new
    // publication.
    const uint64_t session_word =
        impl->state.session.load(std::memory_order_acquire);
    if (!playback_session_playing(session_word)) {
        playback_write_silence(channel_buffers, channel_count,
                               kPlaybackOutputChannels, 0, frames);
        // CONTINUE, never STOP: this arm is what the stream spends most of its
        // life in now (the lifecycle block at the head — the stream stays
        // started between plays), and a stopped-by-the-callback stream would
        // both leave the main thread's `started` flag lying about the device
        // and put the start transient back into the next play. NO SAMPLE IS
        // READ HERE, which is what lets stop()'s fence count callbacks and
        // what makes an idle stream harmless to a buffer the main thread is
        // about to replace.
        impl->callback_cycles.fetch_add(1, std::memory_order_release);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    playback_render_block(impl->state, session_word, channel_buffers,
                          kPlaybackOutputChannels, frames, channel_count);
    impl->callback_cycles.fetch_add(1, std::memory_order_release);
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
    // Four atomic writes, one fence and a log line; NO AAudio call, per the
    // head comment's threading rule. Clearing `output_rate` puts the engine
    // in its SUSPENDED state, which is exactly the behaviour playback.h's
    // graph-suspension clause asks for: the render body would emit silence and
    // hold, and cursor() holds at the last audio position instead of
    // extrapolating. Lowering the session word's playing bit makes
    // is_playing() false, so the run loop's next tick reads this as a natural
    // end and tears the scanner down through the product's one stop body. A
    // fill in flight that reaches its natural end after this finds the word
    // changed under its gate's value and abandons its terminal (the word's
    // comment, playback_common.h), so the lowering this arm performs is the
    // one that stands; and the next reopen (the launch press's, or play()'s
    // own head) closes this stream, and play() publishes a fresh generation,
    // so no session is left stranded behind the lowered flag.
    //
    // THE LATCH IS PUBLISHED BEFORE THE BIT IS LOWERED, WITH A seq_cst FENCE
    // BETWEEN (2026-09-01, the disconnect outranking a concurrent publish):
    // play() publishes a fresh playing generation and then re-checks the
    // latch behind its own seq_cst fence, and the two-case argument at that
    // site rests on this order — a publish store here could otherwise land
    // between play()'s dead check and its publish and be OVERWRITTEN by the
    // fresh generation, leaving `stream_dead` true, `output_rate` 0 and the
    // playing bit up forever (the source view's tick has no device fork to
    // notice). With the latch first, whichever thread's fence is earlier in
    // the fences' total order, the other sees its write: play() either sees
    // the latch and lowers the bit it just published, or its publish is
    // ordered before this fetch_and, which lowers it.
    //
    // NOTHING RECOVERS BY ITSELF: no auto-resume, no reconnect timer, no
    // retry. The next LAUNCH PRESS closes this stream and opens a new one at
    // the new device's own granted rate — the main window's three launch
    // gates through ensure_device_available_for_play, the render player's
    // road through play()'s own head (reopen_stream_if_dead, one body for
    // both). The user presses play again — the same answer the product gives
    // every other rare, loud fault.
    impl->stream_error.store(static_cast<int32_t>(error), std::memory_order_relaxed);
    impl->state.output_rate.store(0, std::memory_order_relaxed);
    impl->stream_dead.store(true, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    impl->state.session.fetch_and(~kSessionPlayingBit, std::memory_order_release);

    std::fprintf(stderr,
        "warptempo_gui: AAudio stream error %d (%s); stream is dead, playback "
        "stopped. The next play press reopens the device.\n",
        static_cast<int>(error), AAudio_convertResultToText(error));
}

// THE STREAM HAS NO CALLBACK LEFT TO COUNT. The fence below waits on callback
// invocations, so it needs one test for the states where none are coming: the
// stream reporting STOPPED — which AAudio reaches only after its callback
// thread has left the callback — and the stream being positively terminal,
// meaning DISCONNECTED / CLOSING / CLOSED or the error callback's `stream_dead`
// latch (the disconnect rule at the error callback: the framework retires the
// callback thread BEFORE that callback runs, so a dead stream has nothing left
// to wait for and waiting would hang forever on a quiesced device). Each of
// these is ALSO a positive proof of quiescence, which is why the fence may
// return on them. Anything else — STARTED, STARTING, a state this build has
// never heard of — is NOT a proof and does not end the wait.
bool fence_state_is_quiesced(const GuiPlayback::Impl& impl,
                             aaudio_stream_state_t state) {
    return state == AAUDIO_STREAM_STATE_STOPPED ||
           state == AAUDIO_STREAM_STATE_DISCONNECTED ||
           state == AAUDIO_STREAM_STATE_CLOSING ||
           state == AAUDIO_STREAM_STATE_CLOSED ||
           impl.stream_dead.load(std::memory_order_acquire);
}

// THE QUIESCENCE FENCE (playback.h's stop() contract: "returns only once the
// callback has quiesced"). IT COUNTS CALLBACK INVOCATIONS, exactly as the JACK
// backend counts process cycles, and for exactly the JACK backend's reason: a
// stream that stays STARTED keeps calling its data callback whether or not
// anything is playing (the lifecycle block at the head of this file), so the
// counter keeps advancing and counting is a proof. It could not be one while
// the stream was stopped between plays — the old fence therefore drove the
// STREAM STATE MACHINE (requestStop, then wait for STOPPED), and that
// requestStop is exactly the click this backend no longer makes.
//
// THE PROOF. stop() lowers the session word's playing bit (seq_cst) before
// calling here. One increment after that write retires a callback that may
// have loaded the word with the bit up before the write became visible; a
// second proves a full callback ran start-to-finish afterwards, and its
// release increment paired with the acquire loads here orders every sample
// read it made before anything the caller mutates once stop() returns. The
// bound is conservative: a callback that sees the bit down takes the silence
// arm and reads no sample at all. Normally about two callback periods — a
// few ms at the granted burst.
//
// THE ESCAPE, and why this cannot hang on a device that has gone away: a
// stream that is dead or positively terminal has no callback left to count, so
// `fence_state_is_quiesced` above ends the wait on those states (it is a proof
// of quiescence in its own right, which is what makes returning on it safe),
// and a stream that was never started has nothing in flight to begin with.
//
// OTHERWISE THE WAIT IS UNBOUNDED, AND HANGING IS THE CONTRACT'S SAFE FAILURE
// MODE — the JACK fence's own choice. A framework that has stopped running
// callbacks on a live stream is a broken environment; freezing the main thread
// here is visible and loud, while a weakened fence lets a rebind, a buffer
// replacement or a shutdown free samples out from under a live audio thread,
// silently and unreproducibly.
void fence_quiesced(GuiPlayback::Impl& impl) {
    if (!impl.stream || !impl.started) return;

    const uint64_t c0 = impl.callback_cycles.load(std::memory_order_acquire);
    while (impl.callback_cycles.load(std::memory_order_acquire) < c0 + 2) {
        if (fence_state_is_quiesced(impl, AAudioStream_getState(impl.stream))) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// Wait out a transition this file asked for, briefly and at most once per
// start. AAudio's stop is a request: the stream passes through STOPPING before
// it is STOPPED, and a start asked during that window can be refused for the
// state alone — which on the player's road would close the device over a pause
// and a resume pressed back to back. The only producer of that window in this
// file is suspend_stream, so the ordinary start (a settled stream, an open one,
// a dead one) reads the state once and waits for nothing. A timeout leaves the
// state where it is and the start takes its own failure arm; the deadline is
// generous because it is never reached in the sequence it exists for.
constexpr int64_t kStateSettleTimeoutNs = 200 * 1000 * 1000;

void settle_stopping_state(GuiPlayback::Impl& impl) {
    if (!impl.stream) return;
    const aaudio_stream_state_t state = AAudioStream_getState(impl.stream);
    if (state != AAUDIO_STREAM_STATE_STOPPING) return;
    aaudio_stream_state_t next = AAUDIO_STREAM_STATE_UNINITIALIZED;
    AAudioStream_waitForStateChange(impl.stream, state, &next,
                                    kStateSettleTimeoutNs);
}

// START THE STREAM, once per open (the lifecycle block at the head). Idempotent
// on `started`, so the init path's start and play()'s own are one road: a start
// refused at init is logged there and RETRIED at the next play, which owns the
// failure arm (it closes the stream, and the next press's reopen brings a
// fresh one).
bool start_stream(GuiPlayback::Impl& impl) {
    if (impl.started) return true;
    if (!impl.stream) return false;
    settle_stopping_state(impl);
    const aaudio_result_t r = AAudioStream_requestStart(impl.stream);
    if (r != AAUDIO_OK) {
        std::fprintf(stderr,
            "warptempo_gui: AAudio requestStart failed (%s); playback "
            "stopped.\n", AAudio_convertResultToText(r));
        return false;
    }
    impl.started = true;
    return true;
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
    // The underrun counter belongs to the STREAM: the next one opens at zero
    // and has had no session on it, so the floor goes with this one (the
    // writers' inventory is at the field). Every stream is opened onto a
    // closed or never-opened Impl — init() shuts down first and the
    // dead-stream reopen closes ahead of its open — so this is the whole seed
    // a fresh stream needs.
    impl.xrun_at_launch = -1;
    // ONE OF THE FILE'S TWO requestStop calls, and it is here because the
    // stream is being CLOSED (shutdown, or the dead-stream reopen) — never
    // between the main window's plays; the other is suspend_stream's, which
    // rests the device under the render player's pause and keeps the stream
    // open for the resume. The
    // close is the stronger fence anyway: it blocks until the callback thread
    // is gone, which is what makes it safe on a stream this function may be
    // stopping while a callback is mid-flight.
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
    impl.callback_cycles.store(0, std::memory_order_relaxed);
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

// THE DEAD-STREAM REOPEN, ONE BODY FOR TWO CALLERS (the disconnect rule at the
// error callback): a dead stream is closed and a dead or null one reopened
// HERE, on the main thread, at the new device's own granted rate. Answers
// whether a stream stands after it. Called at the head of play() — the
// render player's road, which reaches play() with no gate ahead of it — and
// by ensure_device_available_for_play, the main window's three launch gates'
// question (architect 2026-09-02): before it the gates READ the latch ahead
// of play() and so never let play() reopen, leaving the tablet mute behind
// the card after every route drop. A failed reopen leaves playback disabled
// until the next press — no retry, no timer — and, with `device_ready` still
// true, is exactly the "no stream standing" shape device_unavailable reads.
bool reopen_stream_if_dead(GuiPlayback::Impl& impl) {
    if (impl.stream_dead.load(std::memory_order_acquire)) {
        close_stream(impl);
    }
    if (!impl.stream && !open_stream(impl)) return false;
    return true;
}

// THE UNDERRUN COUNT, SAID ONCE PER SESSION THAT HAD ONE (the four-tier
// review's R-18(d), architect 2026-09-02). AAudio's own counter is the only
// evidence this backend has that its buffer — burst × 2, one burst of slack —
// lost a callback to a scheduling hiccup, and until now nothing read it: an
// underrun on this platform was simply an audible tick nobody could account
// for afterwards. So stop() reads it at the tail of its fence, once the
// callback has quiesced and the figure has stopped moving, and prints ONE
// stderr line — which on this backend IS a logcat line, every diagnostic
// going down the redirected fd the log pump drains (platform_android.cpp's
// logcat sink) — WHEN THE SESSION LOST SOMETHING, so a clean session says
// nothing at all.
//
// THE SESSION IS THE INTERVAL FROM THE LAUNCH TO THIS STOP, and that is what
// the launch floor buys (xrun_at_launch, the field's own contract): the stream
// runs and callbacks silence between plays, so subtracting the PREVIOUS STOP's
// figure instead would charge every idle underrun — and the whole interval
// before the first play — to the audio the user just heard. The
// stream-lifetime figure rides in the same line, unsubtracted, because it is
// the one number that says whether this device drops frames at all.
//
// A DIAGNOSTIC AND NOTHING MORE: no notification card, no state cell, no
// engine behaviour. An underrun is already audible, and the product cannot
// grow the buffer without giving up the LOW_LATENCY mode the car's transport
// wants — so the honest answer is to be able to name what happened, not to
// react to it. A negative return (the call unimplemented on a path, an
// invalid stream) is not a count and is dropped.
void report_xrun_count(GuiPlayback::Impl& impl) {
    if (!impl.stream) return;
    // NO SESSION STANDING — a stop with no launch since the last one, or since
    // the stream opened. There is nothing to charge, and the idle stream's own
    // underruns are exactly what must not be said here.
    if (impl.xrun_at_launch < 0) return;
    const int32_t count   = AAudioStream_getXRunCount(impl.stream);
    const int32_t session = count - impl.xrun_at_launch;
    // THE SESSION ENDS HERE whatever the figure, so the floor is voided before
    // the report: a second stop with no play between it and this one says
    // nothing, rather than re-saying this session under a stale floor.
    impl.xrun_at_launch = -1;
    // The counter only ever RISES for a live stream and the floor is never
    // negative (play()'s own clamp), so one test covers the clean session and
    // the non-count alike: a difference at or below zero is either no news or
    // a negative return — the call unimplemented on a path, an invalid stream.
    if (session <= 0) return;
    std::fprintf(stderr,
        "warptempo_gui: AAudio underruns: %d this session, %d since the "
        "stream opened\n",
        static_cast<int>(session), static_cast<int>(count));
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
    // STARTED AT OPEN, so the device's start transient lands here — at launch,
    // under no audition — instead of at the first play (the lifecycle block at
    // the head of this file). The result is deliberately not read: a refused
    // start has already logged, playback is not disabled by it, and play()'s
    // own start arm retries and owns the failure road.
    start_stream(*impl_);
    return true;
}

void GuiPlayback::play(int64_t start_sample, int64_t end_sample) {
    if (!impl_->device_ready) return;

    // THE HEAD CHECK: a dead stream closed and reopened (reopen_stream_if_dead,
    // the one body the launch press's ensure_device_available_for_play also
    // calls). It stays here even though the main window's gates have already
    // run it at the press — the render player's play road has no gate ahead
    // of this call, and the two-case argument below rests on this check
    // standing at the head. A failed reopen returns silently: the gates have
    // carded, and the player's road reads device_unavailable at its tick.
    if (!reopen_stream_if_dead(*impl_)) return;

    // Publish FIRST, start SECOND: the command packet and the session word
    // that releases it are what the callback's acquire gate is waiting to
    // see, and a refused range must not spin the device up at all. The main
    // thread's window mirror, its cursor and the launch anchor — the publish
    // instant, nothing waiting on a seat — are predictor state the callback
    // never reads; a fill consumes only the packet its own generation
    // qualifies.
    if (!playback_publish_play(impl_->state, start_sample, end_sample)) return;

    // THE DISCONNECT OUTRANKS THE PUBLISH (2026-09-01). The dead check at the
    // head of this call and the publish just above are two steps, and the
    // error callback can run between them: its lowering of the playing bit
    // would then be OVERWRITTEN by the fresh generation the publish stored,
    // leaving the latch true, the rate 0 and the bit up on a stream that
    // will never call back — the scanner live forever at the held cursor.
    // So the latch is asked AGAIN here, behind a seq_cst fence, and the
    // error callback stores the latch BEFORE its fetch_and behind a fence of
    // its own. TWO CASES, by the fences' total order: (1) this fence precedes
    // the callback's — then the publish store, sequenced before this fence,
    // is what the callback's fetch_and (sequenced after its fence) modifies,
    // and it lowers the bit this publish set; (2) the callback's fence
    // precedes this one — then the latch store, sequenced before the
    // callback's fence, is visible to this load, and the bit is lowered
    // here. Either way the published bit comes down, and the road taken is
    // the failed start's own: lower the bit, close the dead stream, and the
    // next press reopens. THE THIRD LEG, which the fences do not cover: the
    // callback stores the latch, fences, and is preempted BEFORE its
    // fetch_and while this call sees the latch at its head, closes, reopens
    // and publishes the fresh generation — the late fetch_and would then
    // lower the NEW session's bit. What excludes it is close_stream's
    // AAudioStream_close, which returns only after the callback thread has
    // left (the error callback runs on that thread — the header's own rule
    // that it may call neither requestStop nor close is the self-join it
    // would be), so the delayed fetch_and has landed before the close
    // returns, and so before the reopen and the publish.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (impl_->stream_dead.load(std::memory_order_acquire)) {
        impl_->state.session.fetch_and(~kSessionPlayingBit,
                                       std::memory_order_release);
        close_stream(*impl_);
        return;
    }

    // ORDINARILY A NO-OP, and that is the point of the new lifecycle: the
    // stream was started at open and has been running ever since, so an
    // ordinary play makes no device call at all. Two paths still arrive here
    // with a stopped stream and are started by this call — the reopen just
    // above (the head's, or the launch press's a moment earlier), and an init
    // whose own start was refused — and a refusal here disables the device
    // until the next press reopens it.
    if (!start_stream(*impl_)) {
        impl_->state.session.fetch_and(~kSessionPlayingBit,
                                       std::memory_order_release);
        close_stream(*impl_);
        return;
    }

    // THE SESSION'S UNDERRUN FLOOR, stamped at the launch's own tail because
    // THIS is where the session begins: the stream has been running and
    // callbacking silence since the last stop, and the underruns of that idle
    // interval are not the audio about to be heard (report_xrun_count, above,
    // is the one reader). The stream stands here — start_stream answered true
    // — and a negative return is not a count, so the floor takes zero and the
    // report's own compare drops the pair.
    const int32_t at_launch = AAudioStream_getXRunCount(impl_->stream);
    impl_->xrun_at_launch = at_launch > 0 ? at_launch : 0;
}

void GuiPlayback::resync_predictor() {
    if (!impl_) return;
    playback_resync_predictor(impl_->state);
}

void GuiPlayback::stop() {
    if (!impl_->device_ready) return;
    // THE DEVICE IS NOT TOUCHED HERE. Lower the playing bit, keeping the
    // generation (the session word's comment, playback_common.h: a fill in
    // flight can commit no terminal once this has landed — its exchange
    // expects exactly the word its gate acquired, playing bit up, and this
    // fetch_and changed that word) — then fence on
    // the callback counter (fence_quiesced):
    // the stream keeps running and the callback keeps writing silence, which
    // is the whole of the no-click lifecycle at the head of this file. Then,
    // with the callback quiesced and its counter therefore still, the session's
    // UNDERRUN COUNT is said if the session lost anything since play() stamped
    // its floor (report_xrun_count, above — a logcat line and nothing else).
    impl_->state.session.fetch_and(~kSessionPlayingBit,
                                   std::memory_order_seq_cst);
    fence_quiesced(*impl_);
    report_xrun_count(*impl_);
}

// Rest the device under the render player's pause (the contract at the
// declaration, and the narrowing at the head of this file). The caller is the
// stop body's player fork and it has just returned from stop(), so the session
// word's playing bit is down and the fence has proved the callback out of the
// sample buffer — this adds no fence and needs none.
//
// What it changes is the stream's run state and `started` with it, so the next
// play() reaches start_stream and starts the stream it left stopped, exactly
// as it does for a stream the reopen left standing. Between the two,
// fence_quiesced's `!started` early return is sound for the reason the fence's
// own escape clause gives: a stopped stream has no callback left to count, and
// any callback still retiring reads no sample at all with the playing bit
// down.
//
// A REFUSED STOP LEAVES THE DEVICE AS IT WAS: `started` stays true, the stream
// keeps running and writing silence, and the player behaves as it did before
// this member existed. Nothing here can make the device unavailable — the
// stream, `device_ready` and the disconnect latch are all untouched, which is
// what keeps device_unavailable() and device_absent() meaning what their
// contracts say across a suspension.
void GuiPlayback::suspend_stream() {
    if (!impl_ || !impl_->device_ready || !impl_->stream) return;
    if (!impl_->started) return;
    const aaudio_result_t r = AAudioStream_requestStop(impl_->stream);
    if (r != AAUDIO_OK) {
        std::fprintf(stderr,
            "warptempo_gui: AAudio requestStop failed (%s); the stream keeps "
            "running through the pause.\n", AAudio_convertResultToText(r));
        return;
    }
    impl_->started = false;
}

bool GuiPlayback::is_playing() const {
    return playback_is_playing(impl_->state);
}

// THE THREE WAYS THIS DEVICE CANNOT SOUND (contract at the declaration), and
// they are one answer because they are one fact to the consumer:
//  - `device_ready` false — init() never opened a stream at all, or shutdown()
//    has closed it; play() is its documented silent no-op;
//  - the error callback's `stream_dead` latch — a device that went away under
//    a running stream (a headphone pulled, a Bluetooth route dropped);
//  - no stream standing with `device_ready` still true — what a REFUSED REOPEN
//    leaves behind: the reopen (reopen_stream_if_dead, at a launch press or
//    at play()'s head) closes a dead stream (clearing the latch) and returns
//    when nothing can be opened. That is the same lost device one step later.
// Answering false on any of them would let the render player's tick read the
// silence as a natural end and walk the folder with nothing to play it on.
// A READ ONLY (contract at the declaration): the reopen is the press's, below.
bool GuiPlayback::device_unavailable() const {
    if (!impl_ || !impl_->device_ready) return true;
    return impl_->stream_dead.load(std::memory_order_acquire) ||
           impl_->stream == nullptr;
}

// THE PRESS'S REOPEN AND ITS START (contract at the declaration; architect
// 2026-09-02): the head of play(), hoisted to the launch gates so a dead
// stream is reopened where the main window asks its device question instead
// of read and carded.
// THE QUESTION IS STARTABILITY, NOT EXISTENCE: a stream OBJECT standing is
// not a device that will sound. A freshly reopened stream has `started`
// false, and an init whose own start was refused leaves a non-null unstarted
// one — neither of which device_unavailable can see, since it reads the latch,
// `device_ready` and the null stream alone. If this member answered on the
// object alone, play() would take the start attempt AFTER its publish, and a
// refused start there lowers the playing bit and closes the stream while
// launch_playback_window has already seeded and damaged the scanner and
// returned true: no card, and the next tick reads the lowered bit as a
// natural end and tears the scanner down — a silent false launch, repeatable.
// So the transition happens HERE, before the gate answers: start_stream is
// idempotent on `started`, so a stream already running (the ordinary play)
// makes no device call at all, and only the two stopped paths — this reopen
// and init's refused start — actually request one. A refusal closes the
// stream and answers false, which is the card's own condition.
// The start's own transient (the lifecycle block at the head of this file:
// starting an AAudio stream is audible) is unmoved in practice — it lands at
// the same launch press it always did, microseconds earlier than play()'s.
// The answer is device_unavailable's own reading taken AFTER the reopen — the
// latch cleared by close_stream, the stream standing or not — so the gates
// card exactly when nothing can be opened. A device that never came up
// (`device_ready` false) is not opened by a press: init's failure logged its
// line, play() is the documented silent no-op, and this answers false at once
// as the read does. The error callback may still fire between this reopen and
// play()'s publish; play()'s own head check and its post-publish fence answer
// that, unchanged.
bool GuiPlayback::ensure_device_available_for_play() {
    if (!impl_ || !impl_->device_ready) return false;
    reopen_stream_if_dead(*impl_);
    // A null stream (the reopen just failed) takes this arm too: start_stream
    // answers false on it and close_stream is a no-op there.
    if (!start_stream(*impl_)) {
        close_stream(*impl_);
        return false;
    }
    return !device_unavailable();
}

// THE FACE'S READ (contract at the declaration): the first of
// device_unavailable's three ways alone — init never opened a stream, or
// shutdown() closed it. The latch and the null stream are what a launch press
// reopens, so a face that counted them would grey a button whose press plays.
bool GuiPlayback::device_absent() const {
    return !impl_ || !impl_->device_ready;
}

// WHAT AAUDIO SAYS ABOUT ITSELF, ASKED AT THE MOMENT THE PANEL ASKS (the
// contract at the declaration, playback.h). The granted rate, the burst the
// stream asks for at a time — the PICKUP PHASE'S span — and the buffer it was
// given, all plain getters on the standing stream, on the main thread.
//
// `latency_known` STAYS FALSE, AND THAT IS THE STANDING RECORD RATHER THAN A
// GAP (the Impl block at the head of this file): in the car the tablet plays
// over Bluetooth, whose link delay is large and variable, and the figures the
// framework offers — AAudioStream_getTimestamp and its own estimate — do not
// carry it. A figure that is right on the speaker and wrong on the route the
// panel exists for is worse than no figure, so the panel prints no net line
// here and says which half is missing. No Bluetooth estimate is invented.
GuiAudioStats GuiPlayback::audio_stats() const {
    GuiAudioStats st;
    st.backend = GuiAudioBackendKind::AAudio;
    if (!impl_ || !impl_->device_ready || !impl_->stream) return st;
    st.present     = true;
    st.output_rate =
        static_cast<int>(impl_->state.output_rate.load(std::memory_order_relaxed));
    st.period_frames =
        static_cast<int>(AAudioStream_getFramesPerBurst(impl_->stream));
    st.buffer_frames =
        static_cast<int>(AAudioStream_getBufferSizeInFrames(impl_->stream));
    return st;
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
    impl_->state.session.fetch_and(~kSessionPlayingBit,
                                   std::memory_order_relaxed);
    close_stream(*impl_);  // stops and joins the callback thread
    impl_->device_ready = false;
    playback_clear_binding(impl_->state);
}
