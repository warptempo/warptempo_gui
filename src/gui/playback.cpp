#include "playback.h"

#include <jack/jack.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <thread>
#include <type_traits>

// Implementation notes
// --------------------
// The audio callback runs on JACK's process thread. Its contract with
// the main thread is through a small set of atomics. Most are relaxed scalar
// reads whose value we want to see "eventually" (cursor, speed). The exception
// is the `playing` flag: play() stores it with release ordering as the last
// step of its publish block, and the callback loads it with acquire ordering
// at its gate, so a callback that sees playing == true is guaranteed to see the
// range and pending-start stores that preceded the release. The sample buffer
// is read-only from the audio thread's point of view, and its address lives
// across the device's entire life.
//
// Speed changes are applied at buffer granularity. A set_speed() call
// between callback invocations is picked up on the next fill; the speed
// stays constant within one fill, avoiding mid-buffer rate artefacts.

// Sources are stereo-only (the channels != 2 load refusal), so playback runs
// exactly two output ports.
constexpr int kJackOutputPorts = 2;

static_assert(std::is_same_v<jack_default_audio_sample_t, float>,
              "JACK default audio sample type must be float");

struct GuiPlayback::Impl {
    jack_client_t* client = nullptr;
    bool           client_active = false;
    std::atomic<uint32_t> jack_rate{0};
    std::array<jack_port_t*, kJackOutputPorts> ports{};

    // Borrowed source buffer.
    const float* samples       = nullptr;
    int64_t      total_frames  = 0;
    int          channels      = 0;
    int          source_rate   = 0;
    // Domain coordinate of buffer frame 0 (playback.h head comment). Stored
    // in the same moment as the `samples` pointer (init / rebind_buffer,
    // under the same refuse-while-playing conditions) so the pair can never
    // be observed inconsistent. Main-thread only: the public API translates
    // domain <-> buffer-local at its boundary; the audio callback and every
    // other Impl position stay buffer-local and never read this.
    int64_t      domain_offset = 0;

    // Current range. Updated from the main thread, read from the audio
    // thread. end_sample is exclusive.
    std::atomic<int64_t> end_sample{0};

    // Mutable playback state.
    std::atomic<int64_t> cursor{0};
    // Free-running cursor predictor anchor. The main thread extrapolates
    // linearly from (anchor_sample, anchor_ns) using wall-clock time.
    // Re-anchored at events of acceptable visible discontinuity (play(),
    // playhead jumps, viewport reflows, speed changes, follow-mode on),
    // and continuously by cursor() while the graph is suspended (jack_rate
    // reads 0) so the playhead holds and resume extrapolates from the held
    // position. Main-thread-only; never inside the audio callback. Drift
    // between predictor and audio is bounded by time since last resync ×
    // steady_clock vs sample-clock skew (sub-pixel at typical zoom levels
    // for typical resync intervals).
    std::atomic<int64_t> anchor_sample{0};
    std::atomic<int64_t> anchor_ns{0};
    std::atomic<int32_t> speed_x1000{1000};  // speed * 1000, so we can store in int
    std::atomic<bool>    playing{false};

    // Audio-thread-only fractional source cursor. Tracking the fractional
    // position across buffer boundaries is what prevents per-buffer floor()
    // rounding from compounding into audible drift between audio and visual
    // playhead over long playback. The integer `cursor` is snapshotted from
    // this each buffer for the main thread to read.
    double fractional_cursor = 0.0;

    // Main thread sets a pending restart position via play(); the audio
    // thread picks it up at the top of its next fill to reseat
    // fractional_cursor without a lock. -1 sentinel means "no pending".
    std::atomic<int64_t> pending_start{-1};

    // Incremented once at the end of every process callback invocation,
    // playing or silent. stop() uses it as a quiescence fence: after the
    // playing flag is lowered, observing two further increments proves any
    // callback that could have seen playing == true has exited, so the
    // borrowed sample buffer is no longer being read by the audio thread.
    std::atomic<uint64_t> process_cycles{0};
};

// Copy N output frames at the current speed, advancing the cursor. Stops
// early and fills the remainder with silence if the cursor would pass
// end_sample. Writes the final source-cursor back to impl->cursor before
// returning; on natural end, also clears impl->playing.
void fill_output(GuiPlayback::Impl& impl,
                 float* const* channel_buffers,
                 jack_nframes_t frame_count,
                 int channel_count) {
    const int    src_channels = impl.channels;
    const double speed        = static_cast<double>(
                                    impl.speed_x1000.load(std::memory_order_relaxed))
                                / 1000.0;
    const uint32_t graph_rate = impl.jack_rate.load(std::memory_order_relaxed);
    // JACK asks for graph-rate frames; fold the graph-to-source rate ratio
    // into the existing fractional source read increment.
    const double increment = graph_rate == 0
        ? 0.0
        : speed * static_cast<double>(impl.source_rate) / static_cast<double>(graph_rate);
    const int64_t end         = impl.end_sample.load(std::memory_order_relaxed);
    const int64_t total       = impl.total_frames;

    if (increment == 0.0) {
        for (int c = 0; c < channel_count; ++c) {
            std::memset(channel_buffers[c], 0, sizeof(float) * frame_count);
        }
        // Graph unavailable: emit silence and hold the playback position.
        // A pending_start restart is deliberately not absorbed here, so a
        // play() issued during the outage is picked up by the first fill
        // after the graph returns.
        return;
    }

    // Pick up any pending restart position published by play(). Clearing it
    // atomically lets us idempotently absorb the latest restart without a
    // lock. See the class doc on pending_start.
    const int64_t pending =
        impl.pending_start.exchange(-1, std::memory_order_relaxed);
    if (pending >= 0) {
        impl.fractional_cursor = static_cast<double>(pending);
    }

    bool natural_end = false;
    // Running fractional source position — monotonically advancing: with
    // looping removed (architect 2026-07-30) there is no re-anchoring inside
    // this loop, so the position is a plain drift-free accumulation of
    // `increment` from the last buffer's carry.
    double pos = impl.fractional_cursor;

    jack_nframes_t n = 0;
    for (; n < frame_count; ++n) {
        const double  floor_pos = std::floor(pos);
        const int64_t src_floor = static_cast<int64_t>(floor_pos);
        // Reaching or passing the window end (or the buffer total) ends the
        // session — the NATURAL END, the only terminal this fill has now that
        // the loop-wrap arm is gone. Fill the remainder with silence and stop.
        if (src_floor >= end || src_floor >= total) {
            natural_end = true;
            for (int c = 0; c < channel_count; ++c) {
                std::memset(channel_buffers[c] + n,
                            0,
                            sizeof(float) * (frame_count - n));
            }
            break;
        }

        const int64_t src_ceil  = src_floor + 1;
        const double  frac      = pos - floor_pos;

        const float* sp_floor = impl.samples +
                                static_cast<size_t>(src_floor) * src_channels;
        const bool ceil_ok = (src_ceil < end && src_ceil < total);
        const float* sp_ceil = ceil_ok
            ? impl.samples + static_cast<size_t>(src_ceil) * src_channels
            : sp_floor;  // last-sample fallback

        for (int c = 0; c < channel_count; ++c) {
            const double a = sp_floor[c];
            const double b = sp_ceil[c];
            channel_buffers[c][n] = static_cast<float>((1.0 - frac) * a + frac * b);
        }

        pos += increment;
    }

    // `pos` is now the actual last read position plus one increment — the
    // drift-free next-buffer starting position.
    impl.fractional_cursor = pos;
    int64_t new_cur = static_cast<int64_t>(std::floor(pos));
    if (new_cur > end)   new_cur = end;
    if (new_cur > total) new_cur = total;
    impl.cursor.store(new_cur, std::memory_order_relaxed);
    if (natural_end) {
        impl.playing.store(false, std::memory_order_release);
    }
}

int sample_rate_callback(jack_nframes_t nframes, void* arg) {
    auto* impl = static_cast<GuiPlayback::Impl*>(arg);
    if (impl) {
        impl->jack_rate.store(static_cast<uint32_t>(nframes),
                              std::memory_order_relaxed);
    }
    return 0;
}

int process_callback(jack_nframes_t nframes, void* arg) {
    auto* impl = static_cast<GuiPlayback::Impl*>(arg);
    if (!impl) {
        return 0;
    }

    std::array<float*, kJackOutputPorts> channel_buffers{};
    // init refuses channels != 2, so a live client always has exactly the two
    // stereo ports; channel_count == impl->channels == 2.
    const int channel_count = impl->channels;
    for (int c = 0; c < channel_count; ++c) {
        channel_buffers[c] = static_cast<float*>(
            jack_port_get_buffer(impl->ports[c], nframes));
    }

    if (!impl->playing.load(std::memory_order_acquire)) {
        for (int c = 0; c < channel_count; ++c) {
            std::memset(channel_buffers[c], 0, sizeof(float) * nframes);
        }
        impl->process_cycles.fetch_add(1, std::memory_order_release);
        return 0;
    }

    fill_output(*impl, channel_buffers.data(), nframes, channel_count);
    impl->process_cycles.fetch_add(1, std::memory_order_release);
    return 0;
}

void clear_after_failed_init(GuiPlayback::Impl& impl) {
    if (impl.client) {
        jack_client_close(impl.client);
        impl.client = nullptr;
    }
    impl.client_active = false;
    impl.jack_rate.store(0, std::memory_order_relaxed);
    impl.ports.fill(nullptr);
    impl.samples       = nullptr;
    impl.total_frames  = 0;
    impl.domain_offset = 0;
    impl.process_cycles.store(0, std::memory_order_relaxed);
}

GuiPlayback::GuiPlayback() : impl_(std::make_unique<Impl>()) {}
GuiPlayback::~GuiPlayback() { shutdown(); }

bool GuiPlayback::init(int sample_rate, int channels, const float* samples,
                       int64_t total_frames, int64_t domain_offset) {
    shutdown(); // idempotent

    impl_->samples       = samples;
    impl_->total_frames  = total_frames;
    impl_->domain_offset = domain_offset;
    impl_->channels      = channels;
    impl_->source_rate   = sample_rate;
    impl_->cursor.store(0, std::memory_order_relaxed);
    impl_->anchor_sample.store(0, std::memory_order_relaxed);
    impl_->anchor_ns.store(0, std::memory_order_relaxed);
    impl_->end_sample.store(0, std::memory_order_relaxed);
    impl_->speed_x1000.store(1000, std::memory_order_relaxed);
    impl_->playing.store(false, std::memory_order_relaxed);
    impl_->pending_start.store(-1, std::memory_order_relaxed);
    impl_->process_cycles.store(0, std::memory_order_relaxed);
    impl_->fractional_cursor = 0.0;
    impl_->jack_rate.store(0, std::memory_order_relaxed);
    impl_->ports.fill(nullptr);

    if (channels != 2) {
        std::fprintf(stderr,
            "warptempo_gui: Unsupported channel count for JACK playback "
            "(channels=%d, stereo sources only); playback disabled.\n",
            channels);
        impl_->samples       = nullptr;
        impl_->total_frames  = 0;
        impl_->domain_offset = 0;
        return false;
    }
    if (sample_rate <= 0 || samples == nullptr || total_frames <= 0) {
        std::fprintf(stderr,
            "warptempo_gui: Invalid playback source "
            "(sample_rate=%d, samples=%s, total_frames=%lld); playback disabled.\n",
            sample_rate, samples ? "non-null" : "null",
            static_cast<long long>(total_frames));
        impl_->samples       = nullptr;
        impl_->total_frames  = 0;
        impl_->domain_offset = 0;
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

    impl_->jack_rate.store(static_cast<uint32_t>(jack_get_sample_rate(impl_->client)),
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
        impl_->jack_rate.load(std::memory_order_relaxed), sample_rate, channels);
    return true;
}

void GuiPlayback::play(int64_t start_sample, int64_t end_sample) {
    if (!impl_->client_active) return;
    if (!impl_->samples || impl_->total_frames <= 0) return;
    // Domain -> buffer-local at the API boundary (playback.h head comment).
    // Everything below — the clamps, the early returns, the published
    // cursor/anchor/pending-start atomics — is buffer-local, exactly as
    // before the offset moved in here.
    start_sample -= impl_->domain_offset;
    end_sample   -= impl_->domain_offset;
    if (start_sample < 0) start_sample = 0;
    if (start_sample >= impl_->total_frames) return;
    if (end_sample > impl_->total_frames) end_sample = impl_->total_frames;
    if (end_sample <= start_sample) return;

    // The range and anchor stores below are relaxed; they are published to the
    // audio callback by the release store on `playing` at the end of this
    // block. The callback gates on an acquire load of `playing`, so observing
    // playing == true establishes a happens-before edge that guarantees it also
    // observes every store sequenced before that release. `playing` is the sole
    // synchronization point.
    //
    // `pending_start` hands off the restart position to the audio thread,
    // which reseats its private `fractional_cursor` at the top of the next
    // fill. The integer `cursor` atomic is set here so the main thread sees
    // a consistent snapshot immediately (before the next buffer runs).
    impl_->cursor.store(start_sample, std::memory_order_relaxed);
    impl_->end_sample.store(end_sample, std::memory_order_relaxed);
    impl_->pending_start.store(start_sample, std::memory_order_relaxed);

    // Anchor the predictor to start_sample directly: the audio thread may
    // not yet have processed pending_start, so the cursor atomic still
    // reflects the previous session. The anchor pair is written and read only
    // on the main thread, and each 8-byte aligned load is atomic on the target,
    // so there is no torn read; the predictor tolerates only bounded staleness
    // (the anchor lagging real playback between resyncs), which self-corrects
    // at the next resync.
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    impl_->anchor_sample.store(start_sample, std::memory_order_relaxed);
    impl_->anchor_ns.store(now_ns, std::memory_order_relaxed);
    impl_->playing.store(true, std::memory_order_release);
}

void GuiPlayback::resync_predictor() {
    if (!impl_) return;
    const int64_t cur = impl_->cursor.load(std::memory_order_relaxed);
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    impl_->anchor_sample.store(cur, std::memory_order_relaxed);
    impl_->anchor_ns.store(now_ns, std::memory_order_relaxed);
}

void GuiPlayback::stop() {
    if (!impl_->client_active) return;
    impl_->playing.store(false, std::memory_order_seq_cst);
    // Quiescence fence. At most one process callback is in flight at a
    // time. One increment after the store retires the callback that may
    // have loaded playing before the store became visible; a second
    // increment proves a full callback ran start-to-finish after that
    // straggler exited, and its release increment paired with our
    // acquire loads orders all of its buffer reads before anything the
    // caller mutates after stop() returns. Bounded by ~2 JACK periods.
    // The timeout covers a stalled or dead server (callbacks stop
    // arriving); proceeding after a warning beats hanging the GUI.
    const uint64_t c0 =
        impl_->process_cycles.load(std::memory_order_acquire);
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(250);
    while (impl_->process_cycles.load(std::memory_order_acquire) < c0 + 2) {
        if (std::chrono::steady_clock::now() >= deadline) {
            std::fprintf(stderr,
                "warptempo_gui: JACK process callback did not quiesce "
                "within 250 ms; proceeding. Buffer operations after this "
                "stop may race a stalled callback.\n");
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void GuiPlayback::set_speed(float speed) {
    if (speed < 0.10f) speed = 0.10f;
    if (speed > 1.00f) speed = 1.00f;
    const int32_t v = static_cast<int32_t>(std::lround(speed * 1000.0f));
    impl_->speed_x1000.store(v, std::memory_order_relaxed);
}

bool GuiPlayback::is_playing() const {
    return impl_->playing.load(std::memory_order_relaxed);
}

int64_t GuiPlayback::cursor() const {
    if (!impl_) return 0;
    // Every internal value below (integer cursor, predictor anchor, end/total
    // clamps) is buffer-local; the bound buffer's domain offset is added once
    // at each return, so the reported position is a domain coordinate
    // (playback.h head comment).
    const int64_t off = impl_->domain_offset;
    if (!impl_->playing.load(std::memory_order_relaxed)) {
        return impl_->cursor.load(std::memory_order_relaxed) + off;
    }
    // Graph suspended: the audio thread is holding position, so the playhead
    // holds honestly at the integer cursor. Re-anchoring continuously through
    // the outage makes resume extrapolate from the held position and wall-clock
    // now, with no forward jump or snap-back in either direction.
    if (impl_->jack_rate.load(std::memory_order_relaxed) == 0) {
        const int64_t cur = impl_->cursor.load(std::memory_order_relaxed);
        const int64_t now_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        impl_->anchor_sample.store(cur, std::memory_order_relaxed);
        impl_->anchor_ns.store(now_ns, std::memory_order_relaxed);
        return cur + off;
    }
    const int64_t a_sample = impl_->anchor_sample.load(std::memory_order_relaxed);
    const int64_t a_ns     = impl_->anchor_ns.load(std::memory_order_relaxed);
    if (a_ns == 0) return a_sample + off;  // before first anchor
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const int64_t elapsed_ns = now_ns - a_ns;
    if (elapsed_ns <= 0) return a_sample + off;
    const double speed = static_cast<double>(
        impl_->speed_x1000.load(std::memory_order_relaxed)) / 1000.0;
    const int64_t sr = static_cast<int64_t>(impl_->source_rate);
    const double advance_samples =
        static_cast<double>(elapsed_ns) * 1e-9 * speed * static_cast<double>(sr);
    int64_t predicted = a_sample + static_cast<int64_t>(advance_samples);
    const int64_t end = impl_->end_sample.load(std::memory_order_relaxed);
    if (predicted > end) predicted = end;
    if (predicted > impl_->total_frames) predicted = impl_->total_frames;
    return predicted + off;
}

double GuiPlayback::cursor_precise() const {
    if (!impl_) return 0.0;
    // Mirrors cursor() branch-for-branch but returns the pre-truncation double
    // and applies the same end/total clamps as doubles, so it agrees with
    // cursor() exactly at the clamped window end and is floor()-consistent with
    // it elsewhere (advance_samples >= 0 while playing forward). The domain
    // offset is added once at each return, matching cursor()'s domain.
    const double off = static_cast<double>(impl_->domain_offset);
    if (!impl_->playing.load(std::memory_order_relaxed)) {
        return static_cast<double>(
                   impl_->cursor.load(std::memory_order_relaxed)) + off;
    }
    // Graph suspended: hold at the integer cursor exactly as cursor() reports.
    // No re-anchor here — this is a side-effect-free reader; cursor(), called
    // alongside it on the main thread, owns the continuous suspended re-anchor.
    if (impl_->jack_rate.load(std::memory_order_relaxed) == 0) {
        return static_cast<double>(
                   impl_->cursor.load(std::memory_order_relaxed)) + off;
    }
    const int64_t a_sample = impl_->anchor_sample.load(std::memory_order_relaxed);
    const int64_t a_ns     = impl_->anchor_ns.load(std::memory_order_relaxed);
    if (a_ns == 0) return static_cast<double>(a_sample) + off;
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const int64_t elapsed_ns = now_ns - a_ns;
    if (elapsed_ns <= 0) return static_cast<double>(a_sample) + off;
    const double speed = static_cast<double>(
        impl_->speed_x1000.load(std::memory_order_relaxed)) / 1000.0;
    const int64_t sr = static_cast<int64_t>(impl_->source_rate);
    const double advance_samples =
        static_cast<double>(elapsed_ns) * 1e-9 * speed * static_cast<double>(sr);
    double predicted = static_cast<double>(a_sample) + advance_samples;
    const double end = static_cast<double>(
        impl_->end_sample.load(std::memory_order_relaxed));
    if (predicted > end) predicted = end;
    const double total = static_cast<double>(impl_->total_frames);
    if (predicted > total) predicted = total;
    return predicted + off;
}

int64_t GuiPlayback::domain_begin() const {
    if (!impl_) return 0;
    return impl_->domain_offset;
}

int64_t GuiPlayback::domain_end() const {
    if (!impl_) return 0;
    return impl_->domain_offset + impl_->total_frames;
}

void GuiPlayback::rebind_buffer(const float* samples, int64_t total_frames,
                                int64_t domain_offset) {
    if (!impl_) return;
    // Caller invariant: the device must be fenced by stop() before rebind.
    // Every caller lives in target_render.cpp, but a live client is NOT
    // guaranteed: playback.init() failure is deliberately non-fatal at source
    // load (no JACK server means the GUI runs playback-disabled), and a
    // target-preview completion then calls this with client_active false. That
    // is harmless — this body performs no JACK calls, the assignments below are
    // exactly the right stash for a client-less session, and there is no
    // re-init path that would ever consume them, so playback simply stays
    // disabled. This flag check is defense in depth for skipped stops; a
    // mid-flight pointer swap would be silent corruption. The refusal keeps the
    // buffer/offset pair consistent: neither is stored.
    if (impl_->playing.load(std::memory_order_relaxed)) {
        std::fprintf(stderr,
            "warptempo_gui: rebind_buffer called while playing — refusing "
            "to swap the audio buffer (would race the callback)\n");
        return;
    }
    impl_->samples       = samples;
    impl_->total_frames  = total_frames;
    impl_->domain_offset = domain_offset;
    impl_->cursor.store(0, std::memory_order_relaxed);
    impl_->end_sample.store(0, std::memory_order_relaxed);
    impl_->pending_start.store(-1, std::memory_order_relaxed);
    impl_->fractional_cursor = 0.0;
    impl_->anchor_sample.store(0, std::memory_order_relaxed);
    impl_->anchor_ns.store(0, std::memory_order_relaxed);
}

void GuiPlayback::shutdown() {
    if (!impl_) return;
    if (impl_->client) {
        impl_->playing.store(false, std::memory_order_relaxed);
        if (impl_->client_active) {
            // jack_deactivate returns after the client leaves the graph, so
            // the process callback no longer borrows the sample buffer.
            jack_deactivate(impl_->client);
            impl_->client_active = false;
        }
        jack_client_close(impl_->client);
        impl_->client = nullptr;
    }
    impl_->jack_rate.store(0, std::memory_order_relaxed);
    impl_->ports.fill(nullptr);
    impl_->samples       = nullptr;
    impl_->total_frames  = 0;
    impl_->domain_offset = 0;
}
