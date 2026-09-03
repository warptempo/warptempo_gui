#include "playback.h"

#include "playback_common.h"

#include <jack/jack.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <type_traits>

// THE JACK DEVICE HALF of playback.h's contract. The ENGINE — the render body,
// the predictor, the domain translation and the bind logic — is
// playback_common.{h,cpp}, shared verbatim with the AAudio backend
// (playback_aaudio.cpp); the split and its rationale are stated once at the
// head of playback_common.h. What lives here is the JACK client: opening and
// closing it, its two callbacks (process, sample rate), and stop()'s
// quiescence fence.
//
// THIS BACKEND MEASURES NOTHING (architect 2026-09-03). A latency instrument
// lived here for two days — the port latency ranges read through a latency
// and a buffer-size callback, cached in the engine and printed to stderr on
// every change, to be added to the predictor's anchors — and went with the
// playback leads it was built for: the line is the raw predictor again
// (playback_common.cpp's record above playback_publish_play), and a
// measurement the user did not ask for is not printed. A later AV-sync panel
// reads the ranges ON DEMAND, which is a different shape from a cached
// atomic; nothing is kept standing for it here.
//
// THIS FILE IS NOT IN THE ANDROID TARGET: exactly one of this file and
// playback_aaudio.cpp is compiled into a given binary, the same one-arm-per-
// backend rule gui_font_fontconfig.cpp / gui_font_bundled.cpp follow.

static_assert(std::is_same_v<jack_default_audio_sample_t, float>,
              "JACK default audio sample type must be float");

struct GuiPlayback::Impl {
    // The portable engine (playback_common.h). Everything else in this struct
    // is JACK's own.
    GuiPlaybackState state;

    jack_client_t* client = nullptr;
    bool           client_active = false;
    std::array<jack_port_t*, kPlaybackOutputChannels> ports{};

    // Incremented once at the end of every process callback invocation,
    // playing or silent. stop() uses it as a quiescence fence: after the
    // session word's playing bit is lowered, observing two further increments
    // proves any callback that could have seen the bit up has exited, so the
    // borrowed sample buffer is no longer being read by the audio thread.
    // A DEVICE FACT, not the engine's: a JACK client's process callback keeps
    // running (silent) while the client is active, which is what makes counting
    // cycles a proof. The AAudio backend keeps its own counter and fences the
    // same way, its stream likewise staying started between plays
    // (playback_aaudio.cpp's lifecycle block).
    std::atomic<uint64_t> process_cycles{0};
};

int sample_rate_callback(jack_nframes_t nframes, void* arg) {
    auto* impl = static_cast<GuiPlayback::Impl*>(arg);
    if (impl) {
        impl->state.output_rate.store(static_cast<uint32_t>(nframes),
                                      std::memory_order_relaxed);
    }
    return 0;
}

int process_callback(jack_nframes_t nframes, void* arg) {
    auto* impl = static_cast<GuiPlayback::Impl*>(arg);
    if (!impl) {
        return 0;
    }

    std::array<float*, kPlaybackOutputChannels> channel_buffers{};
    // init refuses channels != 2, so a live client always has exactly the two
    // stereo ports; channel_count == impl->state.channels == 2.
    const int channel_count = impl->state.channels;
    for (int c = 0; c < channel_count; ++c) {
        channel_buffers[c] = static_cast<float*>(
            jack_port_get_buffer(impl->ports[c], nframes));
    }

    // STRIDE 1: JACK hands out one contiguous buffer per port
    // (playback_write_silence's contract). THE GATE is ONE acquire load of
    // the session word (playback_common.h): the callback gates on its playing
    // bit and hands that same word to the render body as its terminal's
    // expected value and as the one generation whose command packet the fill
    // may consume — the word is loaded here and nowhere later in the fill,
    // so a stop or a publish after this load changes the word and fails the
    // fill's terminal, the fill renders under its own generation's window
    // regardless, and the next callback acquires and seats the new
    // publication.
    const uint64_t session_word =
        impl->state.session.load(std::memory_order_acquire);
    if (!playback_session_playing(session_word)) {
        playback_write_silence(channel_buffers.data(), channel_count, 1, 0,
                               static_cast<int64_t>(nframes));
        impl->process_cycles.fetch_add(1, std::memory_order_release);
        return 0;
    }

    playback_render_block(impl->state, session_word, channel_buffers.data(), 1,
                          static_cast<int64_t>(nframes), channel_count);
    impl->process_cycles.fetch_add(1, std::memory_order_release);
    return 0;
}

void clear_after_failed_init(GuiPlayback::Impl& impl) {
    if (impl.client) {
        jack_client_close(impl.client);
        impl.client = nullptr;
    }
    impl.client_active = false;
    impl.state.output_rate.store(0, std::memory_order_relaxed);
    impl.ports.fill(nullptr);
    playback_clear_binding(impl.state);
    impl.process_cycles.store(0, std::memory_order_relaxed);
}

GuiPlayback::GuiPlayback() : impl_(std::make_unique<Impl>()) {}
GuiPlayback::~GuiPlayback() { shutdown(); }

bool GuiPlayback::init(int sample_rate, int channels, const float* samples,
                       int64_t total_frames, int64_t domain_offset) {
    shutdown(); // idempotent

    impl_->process_cycles.store(0, std::memory_order_relaxed);
    impl_->ports.fill(nullptr);
    if (!playback_bind_and_validate(impl_->state, sample_rate, channels,
                                    samples, total_frames, domain_offset)) {
        return false;
    }

    jack_status_t status = static_cast<jack_status_t>(0);
    impl_->client = jack_client_open("warptempo_gui",
                                     JackNoStartServer,
                                     &status);
    if (!impl_->client) {
        std::fprintf(stderr,
            "warptempo_gui: JACK client init failed (status=0x%x); "
            "playback disabled. Verify pipewire-jack is running.\n",
            static_cast<unsigned>(status));
        clear_after_failed_init(*impl_);
        return false;
    }

    impl_->state.output_rate.store(
        static_cast<uint32_t>(jack_get_sample_rate(impl_->client)),
        std::memory_order_relaxed);

    if (jack_set_sample_rate_callback(impl_->client,
                                      sample_rate_callback,
                                      impl_.get()) != 0) {
        std::fprintf(stderr,
            "warptempo_gui: JACK sample-rate callback setup failed; "
            "playback disabled. Verify pipewire-jack is running.\n");
        clear_after_failed_init(*impl_);
        return false;
    }

    if (jack_set_process_callback(impl_->client,
                                  process_callback,
                                  impl_.get()) != 0) {
        std::fprintf(stderr,
            "warptempo_gui: JACK process callback setup failed; "
            "playback disabled. Verify pipewire-jack is running.\n");
        clear_after_failed_init(*impl_);
        return false;
    }

    for (int c = 0; c < channels; ++c) {
        char name[32];
        std::snprintf(name, sizeof(name), "out_%d", c + 1);
        impl_->ports[c] = jack_port_register(impl_->client,
                                             name,
                                             JACK_DEFAULT_AUDIO_TYPE,
                                             JackPortIsOutput,
                                             0);
        if (!impl_->ports[c]) {
            std::fprintf(stderr,
                "warptempo_gui: JACK output port registration failed "
                "(port=%s); playback disabled. Verify pipewire-jack is running.\n",
                name);
            clear_after_failed_init(*impl_);
            return false;
        }
    }

    if (jack_activate(impl_->client) != 0) {
        std::fprintf(stderr,
            "warptempo_gui: JACK client activation failed; playback disabled. "
            "Verify pipewire-jack is running.\n");
        clear_after_failed_init(*impl_);
        return false;
    }
    impl_->client_active = true;

    const char** physical_ports = jack_get_ports(impl_->client,
                                                 nullptr,
                                                 nullptr,
                                                 JackPortIsPhysical | JackPortIsInput);
    if (!physical_ports) {
        std::fprintf(stderr,
            "warptempo_gui: No physical JACK playback ports found; "
            "client is active and can be patched manually.\n");
    } else {
        // sources are stereo-only by the channels != 2 load refusal, so
        // playback.init is only ever called with channels == 2 — only this
        // stereo auto-connect arm runs.
        int connected = 0;
        for (int c = 0; c < channels && physical_ports[c]; ++c) {
            const int r = jack_connect(impl_->client,
                                       jack_port_name(impl_->ports[c]),
                                       physical_ports[c]);
            if (r == 0 || r == EEXIST) {
                ++connected;
            } else {
                std::fprintf(stderr,
                    "warptempo_gui: JACK auto-connect failed (%s -> %s, code=%d); "
                    "patch manually if needed.\n",
                    jack_port_name(impl_->ports[c]), physical_ports[c], r);
            }
        }
        if (connected < channels) {
            std::fprintf(stderr,
                "warptempo_gui: JACK auto-connect: connected %d of %d output ports "
                "(physical sinks exhausted); patch remaining ports manually if needed.\n",
                connected, channels);
        }
        jack_free(physical_ports);
    }

    std::fprintf(stderr,
        "warptempo_gui: Audio backend: JACK direct, graph_sample_rate=%u, "
        "source_sample_rate=%d, channels=%d\n",
        impl_->state.output_rate.load(std::memory_order_relaxed),
        sample_rate, channels);
    return true;
}

void GuiPlayback::play(int64_t start_sample, int64_t end_sample) {
    if (!impl_->client_active) return;
    playback_publish_play(impl_->state, start_sample, end_sample);
}

void GuiPlayback::resync_predictor() {
    if (!impl_) return;
    playback_resync_predictor(impl_->state);
}

void GuiPlayback::stop() {
    if (!impl_->client_active) return;
    // Lower the playing bit, keeping the generation (the session word's
    // comment, playback_common.h): a fill in flight can commit no terminal
    // once this has landed — its exchange expects exactly the word its gate
    // acquired, playing bit up, and this fetch_and changed that word — so a
    // fill that passed its gate before this lowering renders one more block
    // and abandons its terminal.
    impl_->state.session.fetch_and(~kSessionPlayingBit,
                                   std::memory_order_seq_cst);
    // Quiescence fence. At most one process callback is in flight at a
    // time. One increment after the store retires the callback that may
    // have loaded playing before the store became visible; a second
    // increment proves a full callback ran start-to-finish after that
    // straggler exited, and its release increment paired with our
    // acquire loads orders all of its buffer reads before anything the
    // caller mutates after stop() returns. Normally ~2 JACK periods.
    // The wait is deliberately unbounded (architect 2026-08-08): a server
    // that has stopped running callbacks entirely is a broken environment,
    // and hanging visibly beats returning into a buffer the callback can
    // still be reading — callers clear, append to and reallocate the
    // target buffer the moment stop() returns.
    // THE SECOND PRODUCER OF THAT SHAPE, observed 2026-09-01: a LIVE server
    // with a DEAD DOWNSTREAM SINK. The DAC fell off USB with a hub, and the
    // upgraded wireplumber left its sink node in an error state; pipewire
    // itself stayed healthy, so our client opened, activated, auto-connected
    // to the stale ports and was never driven — every JACK-API check passes
    // (`client_active` true, `device_unavailable()` false), the first play
    // published into silence with the line resting on its start frame, and
    // the next stop road slept here forever. THE HANG IS THE INTENDED LOUD
    // SIGNAL for it (the ruling reaffirmed that day: loud error over silent
    // backstop — we cannot fix what we do not know about). The alternative,
    // an IN-FLIGHT FENCE (wait only for a callback already in flight, which
    // carries the same buffer-safety proof and returns at once with nothing
    // in flight), was proposed by codex that night and REJECTED: its only
    // visible effect is in exactly this adversarial case, it cannot be
    // tested here, and it would trade a hang the user notices for silence he
    // must diagnose. THE DIAGNOSIS ROAD: `aplay -l` shows no card for the
    // DAC, and the pipewire user journal shows the sink node going
    // `suspended -> error`; fix the environment and relaunch.
    const uint64_t c0 =
        impl_->process_cycles.load(std::memory_order_acquire);
    while (impl_->process_cycles.load(std::memory_order_acquire) < c0 + 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool GuiPlayback::is_playing() const {
    return playback_is_playing(impl_->state);
}

// THE CLIENT THAT NEVER CAME UP, and on this backend that is the whole
// answer. `client_active` is init()'s own success bit — false when
// jack_client_open, either callback registration, a port or jack_activate
// failed (each logging "playback disabled; verify pipewire-jack is running"),
// and false again after shutdown() — and it is exactly the bit play() and
// stop() gate on, so it is the honest reading of "nothing will sound".
// A LAPTOP WITH NO pipewire-jack IS THE ORDINARY CASE HERE, which is why this
// half is not optional: without it the render player's tick reads its silent
// engine as a natural end every tick and walks the folder.
// THE MID-PLAY LOSS IS NOT RECORDED, deliberately: no jack_on_shutdown
// callback is registered, and the stop() fence above answers a server that has
// stopped running callbacks by hanging — "a broken environment", the
// architect's 2026-08-08 ruling — rather than by latching a state this could
// read. A device that stops mid-play on this platform is the class of rare,
// loud fault the product does not backstop (the wind-down rule); the AAudio
// twin has a real latch because a headphone pull and a Bluetooth drop are
// ordinary events in the car. A CLIENT THAT CAME UP AND IS NEVER DRIVEN (the
// live server over a dead sink, stop()'s second producer above) is that same
// class and reads as AVAILABLE on purpose: it passes every JACK-API check
// there is, and the fence's hang is what says so.
bool GuiPlayback::device_unavailable() const {
    return !impl_ || !impl_->client_active;
}

// THE PRESS'S REOPEN IS THE READ ON THIS BACKEND (contract at the
// declaration): there is nothing to reopen — no latch is ever set here (the
// mid-play loss is not recorded, above), and a client that never came up is
// not brought up by a press (the init failure logged its own line and the
// user relaunches). So the answer is exactly what the gates read before the
// reopen existed, `!device_unavailable()`, and a press on this backend
// changes no state.
bool GuiPlayback::ensure_device_available_for_play() {
    return !device_unavailable();
}

// THE FACE'S READ IS THE SAME BIT ON THIS BACKEND (contract at the
// declaration): device_unavailable is already the never-came-up half alone
// here, so the two reads are one expression.
bool GuiPlayback::device_absent() const {
    return !impl_ || !impl_->client_active;
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
    if (impl_->client) {
        impl_->state.session.fetch_and(~kSessionPlayingBit,
                                       std::memory_order_relaxed);
        if (impl_->client_active) {
            // jack_deactivate returns after the client leaves the graph, so
            // the process callback no longer borrows the sample buffer.
            jack_deactivate(impl_->client);
            impl_->client_active = false;
        }
        jack_client_close(impl_->client);
        impl_->client = nullptr;
    }
    impl_->state.output_rate.store(0, std::memory_order_relaxed);
    impl_->ports.fill(nullptr);
    playback_clear_binding(impl_->state);
}
