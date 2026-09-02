#include "playback_common.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

// Implementation notes
// --------------------
// The render body runs on the backend's audio thread (JACK's process thread,
// AAudio's data-callback thread). Its contract with the main thread is through
// a small set of atomics. Most are relaxed scalar reads whose value we want to
// see "eventually" (the cursor). The exception is the SESSION WORD (its bit
// 0, the playing flag): playback_publish_play stores it with release ordering
// as the last step of its publish block, and the backend's callback loads it
// with acquire ordering at its gate, so a callback that sees the flag up is
// guaranteed to see the range and pending-start stores that preceded the
// release; the render body's natural-end terminal is a release compare-
// exchange on the same word, qualified by the generation the word carries
// (the field's comment, playback_common.h). The sample buffer is read-only
// from the audio thread's point of view, and its address lives across the
// device's entire life.
//
// (THE SPEED FACTOR AND ITS BUFFER-GRANULARITY NOTE WENT WITH THE
// `playback_speed` KEY — architect 2026-08-27. The rate ratio that stayed is
// read at the top of each fill for the same reason the speed was: the device
// may change it at a reopen, and one value per fill is what keeps a buffer free
// of mid-block rate artefacts.)

namespace {

// THE PREDICTOR'S ONE CLOCK, on both threads: steady_clock is CLOCK_MONOTONIC
// through the vDSO — lock-free and RT-safe, which is what lets the audio
// thread stamp its fills with the same clock the main thread extrapolates in.
int64_t steady_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// THE HEARD OFFSET: how long after a frame enters the output port it leaves
// the loudspeaker, as the backend reported it (`output_latency_frames`, at the
// OUTPUT rate) — the one figure every anchor adds to a port instant, ONE PER
// EPOCH: the backend may move it mid-session, and the reader re-anchors at
// the change (`anchor_offset_ns`, playback_common.h). Zero while suspended,
// and zero on AAudio by that backend's own rule (the field's comment).
int64_t heard_offset_ns(const GuiPlaybackState& state) {
    const uint32_t rate = state.output_rate.load(std::memory_order_relaxed);
    if (rate == 0) return 0;
    const int64_t frames =
        state.output_latency_frames.load(std::memory_order_relaxed);
    return frames * 1000000000LL / static_cast<int64_t>(rate);
}

// THE CYCLE STAMP'S WRITER — audio thread only (the seqlock's ordering
// argument is at the fields, playback_common.h).
void write_stamp(GuiPlaybackState& state, int64_t cursor, int64_t port_ns) {
    const uint64_t g = state.stamp_gen.load(std::memory_order_relaxed);
    state.stamp_gen.store(g + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    state.stamp_cursor.store(cursor, std::memory_order_relaxed);
    state.stamp_ns.store(port_ns, std::memory_order_relaxed);
    state.stamp_gen.store(g + 2, std::memory_order_release);
}

// THE CYCLE STAMP'S READER — main thread; retries across a write in progress
// and never blocks the writer.
void read_stamp(const GuiPlaybackState& state, int64_t& cursor,
                int64_t& port_ns) {
    for (;;) {
        const uint64_t g1 = state.stamp_gen.load(std::memory_order_acquire);
        if (g1 & 1u) continue;
        cursor  = state.stamp_cursor.load(std::memory_order_relaxed);
        port_ns = state.stamp_ns.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (state.stamp_gen.load(std::memory_order_relaxed) == g1) return;
    }
}

// HAS THE AUDIO THREAD SEATED THE PUBLISHED SESSION — i.e. absorbed the
// pending restart and, before that, stamped it (pending_start's comment)? The
// acquire pairs with the render body's release compare-exchange, so a true
// answer orders the seat's stamp before whatever stamp the caller reads next.
bool playback_seated(const GuiPlaybackState& state) {
    return state.pending_start.load(std::memory_order_acquire) == -1;
}

// AN ANCHOR: the three words the predictor extrapolates from — a position,
// the instant it is heard, and the heard offset that instant was built with
// (the anchor's epoch, `anchor_offset_ns`).
struct HeardAnchor {
    int64_t sample    = 0;
    int64_t heard_ns  = 0;
    int64_t offset_ns = 0;
};

// THE ONE ANCHOR FORM, read off the stamp: (the stamped position, its port
// instant plus the heard offset) — the instant that position is heard, with
// the offset it used. The launch latch, every resync and the epoch re-anchor
// store exactly this; the observation's hold deadline is its `heard_ns`. The
// offset is the CALLER'S one read — the observation reads it once for
// everything it forms, the resync once for its anchor — so the instant and
// the epoch word can never disagree about which figure built the anchor.
HeardAnchor heard_anchor_from_stamp(const GuiPlaybackState& state,
                                    int64_t offset_ns) {
    HeardAnchor a;
    int64_t port_ns = 0;
    read_stamp(state, a.sample, port_ns);
    a.offset_ns = offset_ns;
    a.heard_ns  = port_ns + offset_ns;
    return a;
}

// THE ANCHOR'S ONE STORE — the four writers named at the fields all land
// here, so the three words are always written together. Main thread only.
void store_anchor(GuiPlaybackState& state, const HeardAnchor& a) {
    state.anchor_sample.store(a.sample, std::memory_order_relaxed);
    state.anchor_ns.store(a.heard_ns, std::memory_order_relaxed);
    state.anchor_offset_ns.store(a.offset_ns, std::memory_order_relaxed);
}

// THE RESYNC'S RE-ANCHOR: anchor on the latest cycle stamp at its heard
// instant, at the live offset. The launch latch and the epoch re-anchor take
// the same stamp form inside `observe` below, which hands the anchor to its
// storing readers instead of storing here — so every anchor the predictor
// ever rests on is one of these two stores of one form.
void anchor_on_stamp(GuiPlaybackState& state) {
    store_anchor(state, heard_anchor_from_stamp(state, heard_offset_ns(state)));
}

// THE EXTRAPOLATION, shared by the two readers: the anchor's position advanced
// by wall-clock time at the SOURCE rate (the cursor is in source frames and
// wall-clock advances it at that rate whatever rate the device runs), in
// BOTH directions — a `now` ahead of the anchor's heard instant reads the
// position heard at `now` — then floored at the session's start (nothing has
// been heard before the first frame) and clamped at the window's and the
// buffer's ends. Returned as the pre-truncation double; cursor() floors it.
double predict_position(const GuiPlaybackState& state, int64_t a_sample,
                        int64_t a_ns, int64_t now_ns) {
    const double sr = static_cast<double>(state.source_rate);
    const double advance_samples =
        static_cast<double>(now_ns - a_ns) * 1e-9 * sr;
    double predicted = static_cast<double>(a_sample) + advance_samples;
    const double start = static_cast<double>(
        state.start_sample.load(std::memory_order_relaxed));
    if (predicted < start) predicted = start;
    const double end = static_cast<double>(
        state.end_sample.load(std::memory_order_relaxed));
    if (predicted > end) predicted = end;
    const double total = static_cast<double>(state.total_frames);
    if (predicted > total) predicted = total;
    return predicted;
}

}  // namespace

void playback_write_silence(float* const* channel_buffers, int channel_count,
                            int64_t stride, int64_t first, int64_t frames) {
    if (frames <= 0) return;
    for (int c = 0; c < channel_count; ++c) {
        float* dst = channel_buffers[c] + first * stride;
        if (stride == 1) {
            std::memset(dst, 0, sizeof(float) * static_cast<size_t>(frames));
        } else {
            for (int64_t n = 0; n < frames; ++n) dst[n * stride] = 0.0f;
        }
    }
}

void playback_render_block(GuiPlaybackState& state,
                           uint64_t session_word,
                           float* const* channel_buffers,
                           int64_t stride,
                           int64_t frame_count,
                           int channel_count) {
    // `session_word` is THE WORD THE GATE ACQUIRED — the backend's one load,
    // playing bit up — and the exact value the natural-end terminal below
    // must find unchanged; this body never loads the word itself (the field's
    // comment, playback_common.h: a re-load here could adopt a stop's or a
    // newer publish's word and qualify the terminal by a session that never
    // passed the gate).
    const int    src_channels = state.channels;
    const uint32_t output_rate = state.output_rate.load(std::memory_order_relaxed);
    // The device asks for output-rate frames; the output-to-source rate ratio
    // IS the fractional source read increment (bare 1.0 whenever the device
    // runs at the source's own rate, which is every JACK graph pinned to it).
    const double increment = output_rate == 0
        ? 0.0
        : static_cast<double>(state.source_rate) / static_cast<double>(output_rate);
    const int64_t end         = state.end_sample.load(std::memory_order_relaxed);
    const int64_t total       = state.total_frames;

    if (increment == 0.0) {
        playback_write_silence(channel_buffers, channel_count, stride, 0, frame_count);
        // Device unavailable: emit silence and hold the playback position.
        // A pending_start restart is deliberately not absorbed here, so a
        // play() issued during the outage is picked up by the first fill
        // after the device returns.
        return;
    }

    // THE FILL'S ONE CLOCK READ, at its top: the instant this fill's first
    // frame enters the port, from which the seat's stamp and the fill-end
    // stamp both derive. Its error against the true cycle start is the
    // driver-wakeup-to-callback time, tens of microseconds — a fraction of a
    // frame.
    const int64_t fill_ns = steady_now_ns();

    // Pick up any pending restart position published by play(), STAMPING IT
    // FIRST: the seat is (pending, fill_ns) — the restart position and the
    // instant it enters the port — and the compare-exchange that consumes the
    // pending is what publishes the seat to the main thread (pending_start's
    // comment). A newer pending landing between the stamp and the exchange
    // fails the exchange, which hands the newer value back, and the loop
    // stamps and consumes that one instead — so a main thread reading -1 with
    // acquire never reads a stamp older than the seat of the session it
    // published. The loop is bounded by the main thread's own publish rate
    // (one iteration per publish that lands inside this window of a few
    // stores); in the ordinary case it is one load.
    int64_t pending = state.pending_start.load(std::memory_order_relaxed);
    while (pending >= 0) {
        write_stamp(state, pending, fill_ns);
        if (state.pending_start.compare_exchange_strong(
                pending, -1, std::memory_order_release,
                std::memory_order_relaxed)) {
            state.fractional_cursor = static_cast<double>(pending);
            break;
        }
    }

    bool natural_end = false;
    // Running fractional source position — monotonically advancing: with
    // looping removed (architect 2026-07-30) there is no re-anchoring inside
    // this loop, so the position is a plain drift-free accumulation of
    // `increment` from the last buffer's carry.
    double pos = state.fractional_cursor;

    int64_t n = 0;
    for (; n < frame_count; ++n) {
        const double  floor_pos = std::floor(pos);
        const int64_t src_floor = static_cast<int64_t>(floor_pos);
        // Reaching or passing the window end (or the buffer total) ends the
        // session — the NATURAL END, the only terminal this fill has now that
        // the loop-wrap arm is gone. Fill the remainder with silence and stop.
        if (src_floor >= end || src_floor >= total) {
            natural_end = true;
            playback_write_silence(channel_buffers, channel_count, stride,
                                   n, frame_count - n);
            break;
        }

        const int64_t src_ceil  = src_floor + 1;
        const double  frac      = pos - floor_pos;

        const float* sp_floor = state.samples +
                                static_cast<size_t>(src_floor) * src_channels;
        const bool ceil_ok = (src_ceil < end && src_ceil < total);
        const float* sp_ceil = ceil_ok
            ? state.samples + static_cast<size_t>(src_ceil) * src_channels
            : sp_floor;  // last-sample fallback

        for (int c = 0; c < channel_count; ++c) {
            const double a = sp_floor[c];
            const double b = sp_ceil[c];
            channel_buffers[c][n * stride] = static_cast<float>((1.0 - frac) * a + frac * b);
        }

        pos += increment;
    }

    // `pos` is now the actual last read position plus one increment — the
    // drift-free next-buffer starting position.
    state.fractional_cursor = pos;
    int64_t new_cur = static_cast<int64_t>(std::floor(pos));
    if (new_cur > end)   new_cur = end;
    if (new_cur > total) new_cur = total;
    // THE FILL-END STAMP: the read cursor with the instant its frame enters
    // the port — `n` output frames after this fill's first (every frame on a
    // full fill; on the natural-end fill the count read before the window's
    // end, so the stamp names the instant the last sound enters the port and
    // the hold below ends exactly when that sound has been heard). Stamped
    // ahead of the cursor store and the flag: a main thread that sees the
    // flag drop (acquire) sees this stamp.
    write_stamp(state, new_cur,
                fill_ns + n * 1000000000LL / static_cast<int64_t>(output_rate));
    state.cursor.store(new_cur, std::memory_order_relaxed);
    if (natural_end) {
        // THE TERMINAL, generation-qualified (the session word's comment):
        // playing -> ended on exactly the word the gate acquired for this
        // fill. The hold's bit rides the same release as the flag's drop —
        // one word — so a reader that sees the flag down through an acquire
        // load sees the hold set, and the observation's not-playing arm
        // extrapolates on instead of snapping to the read cursor. A failed
        // exchange means a newer session was published, or a stop lowered
        // the flag, since the gate: the terminal is ABANDONED — this block of
        // the gated session has sounded, a new session's pending is still
        // up, and the next callback acquires and seats it.
        uint64_t expected = session_word;
        state.session.compare_exchange_strong(
            expected,
            (session_word & ~kSessionPlayingBit) | kSessionEndedBit,
            std::memory_order_release, std::memory_order_relaxed);
    }
}

void playback_clear_binding(GuiPlaybackState& state) {
    state.samples       = nullptr;
    state.total_frames  = 0;
    state.domain_offset = 0;
}

bool playback_bind_and_validate(GuiPlaybackState& state, int sample_rate,
                                int channels, const float* samples,
                                int64_t total_frames, int64_t domain_offset) {
    state.samples       = samples;
    state.total_frames  = total_frames;
    state.domain_offset = domain_offset;
    state.channels      = channels;
    state.source_rate   = sample_rate;
    state.cursor.store(0, std::memory_order_relaxed);
    state.anchor_sample.store(0, std::memory_order_relaxed);
    state.anchor_ns.store(0, std::memory_order_relaxed);
    state.anchor_offset_ns.store(0, std::memory_order_relaxed);
    state.start_sample.store(0, std::memory_order_relaxed);
    state.end_sample.store(0, std::memory_order_relaxed);
    // The whole session word, generation included: no callback runs here
    // (the backend binds before it opens its device).
    state.session.store(0, std::memory_order_relaxed);
    state.pending_start.store(-1, std::memory_order_relaxed);
    state.fractional_cursor = 0.0;
    state.output_rate.store(0, std::memory_order_relaxed);
    // (output_latency_frames is the backend's device fact, zeroed where it
    // zeroes output_rate; the cycle stamp is never reset — the fields'
    // comments, playback_common.h.)

    if (channels != 2) {
        // The extraction's FIFTH deviation from the old JACK body (android/
        // NOTES.md §11.8 lists the other four): this message dropped the word
        // "JACK", the refusal being the shared engine's on both devices rather
        // than any one device's.
        std::fprintf(stderr,
            "warptempo_gui: Unsupported channel count for playback "
            "(channels=%d, stereo sources only); playback disabled.\n",
            channels);
        playback_clear_binding(state);
        return false;
    }
    if (sample_rate <= 0 || samples == nullptr || total_frames <= 0) {
        std::fprintf(stderr,
            "warptempo_gui: Invalid playback source "
            "(sample_rate=%d, samples=%s, total_frames=%lld); playback disabled.\n",
            sample_rate, samples ? "non-null" : "null",
            static_cast<long long>(total_frames));
        playback_clear_binding(state);
        return false;
    }
    return true;
}

// THE OUTPUT LATENCY IS COMPENSATED ON THE LAPTOP (architect 2026-09-01 — the
// same day it was recorded as accepted and uncompensated, that morning's
// record standing here until the evening's arc replaced it). WHAT IS
// COMPENSATED, and where: (1) THE REPORTED FIGURE — the output port's
// playback latency as the JACK graph propagates it from the sink
// (`output_latency_frames`; on the architect's rig the quantum plus the USB
// sink's headroom, 1536 frames = 34.8 ms at 44100), added to every anchor as
// `heard_offset_ns`; (2) THE SEAT — the launch anchor is no longer the
// publish instant but the instant the audio thread actually began the fill
// that absorbed `pending_start`, so the wait between play() and that fill
// (0 … one period) is out of the lead; (3) THE CYCLE STAMP — every resync
// anchors on (the read cursor, the instant its frame enters the port), so the
// time from the main thread's `now` to the next fill (which used to re-roll
// inside (0, period] at every pan end, page turn or `c`) is out of it too,
// and a resync's step is the accumulated wall-clock DRIFT alone; (4) THE
// EPOCH — the figure is ONE PER EPOCH, not a session constant: a quantum
// change moves it mid-session, and the reader re-anchors from the latest
// stamp the moment the live offset differs from the one its anchor was built
// with (`anchor_offset_ns`), so the cursor and the natural-end deadline never
// mix two figures. The old [latency, latency + period] band is therefore
// gone whole: the line rests on
// the launch frame until the first sound is heard, tracks the ear between
// resyncs to the crystal skew, and — the natural-end hold (the session word's
// ended bit)
// — vanishes when the sound ends. EVERY LAUNCH ROAD SHARES THE COMPENSATION
// as it shared the band: bare Space, the waveform scrub, the A/B audition's
// four bounded plays and the render player's own launch all publish through
// this one body.
//
// WHAT REMAINS, honestly: the DAC's own pipeline — its USB transfer and
// digital-filter delay, a few milliseconds, constant, and outside anything
// the graph reports (the K5 Pro adds no DSP); the DISPLAY's own latency — a
// frame painted at pre-paint time reaches the panel one to two refresh
// periods later (compositor plus scanout, ~16–33 ms at 60 Hz), which the old
// audio lead partly hid, so to a critical eye the line may now read a frame
// BEHIND the sound — the remedy, if he wants one, is a single authored lead
// constant on the PAINT side (the platform already receives the output's
// refresh from wl_output.mode), proposed as a later one-number retune and
// deliberately not part of this arc; and the TABLET, whose predictor keeps
// the whole uncompensated lead by ruling — its AAudio backend reports no
// figure (the asymmetry at that Impl), so only the seat move reaches it.
// A wrong figure shows as the line running ahead (too small) or behind (too
// large) by the error; the JACK backend prints the figure to stderr whenever
// it changes, so a surprising number is visible at launch.
//
// THE PUBLISH ITSELF anchors at (start, 0) — "await the seat" — and the
// observation's latch turns the seat's stamp into the launch anchor once
// (`observe`, below). The `now` read that used to anchor here is gone with
// the lead it produced.
bool playback_publish_play(GuiPlaybackState& state, int64_t start_sample,
                           int64_t end_sample) {
    if (!state.samples || state.total_frames <= 0) return false;
    // Domain -> buffer-local at the API boundary (playback.h head comment).
    // Everything below — the clamps, the early returns, the published
    // cursor/anchor/pending-start atomics — is buffer-local, exactly as
    // before the offset moved in here.
    start_sample -= state.domain_offset;
    end_sample   -= state.domain_offset;
    if (start_sample < 0) start_sample = 0;
    if (start_sample >= state.total_frames) return false;
    if (end_sample > state.total_frames) end_sample = state.total_frames;
    if (end_sample <= start_sample) return false;

    // The range and anchor stores below are relaxed; they are published to the
    // audio callback by the release store of the session word at the end of
    // this block. The callback gates on an acquire load of that word, so
    // observing the playing bit up establishes a happens-before edge that
    // guarantees it also observes every store sequenced before that release.
    // The word is the sole synchronization point.
    //
    // `pending_start` hands off the restart position to the audio thread,
    // which reseats its private `fractional_cursor` at the top of the next
    // fill. The integer `cursor` atomic is set here so the main thread sees
    // a consistent snapshot immediately (before the next buffer runs). A new
    // session ends any natural-end hold the previous one was in — the fresh
    // word below carries no ended bit.
    state.start_sample.store(start_sample, std::memory_order_relaxed);
    state.cursor.store(start_sample, std::memory_order_relaxed);
    state.end_sample.store(end_sample, std::memory_order_relaxed);
    state.pending_start.store(start_sample, std::memory_order_relaxed);

    // Anchor the predictor at (start, AWAIT THE SEAT): the audio thread has
    // not yet absorbed pending_start, so neither the cursor atomic nor the
    // cycle stamp is this session's yet, and an instant taken here would be
    // the publish instant — the lead the compensation removes. anchor_ns == 0
    // makes the reader rest at `start` until playback_seated answers true,
    // then latch the seat's stamp plus the heard offset, once. The anchor pair
    // is written and read only on the main thread, and each 8-byte aligned
    // load is atomic on the target, so there is no torn read; the predictor
    // tolerates only bounded staleness (the anchor lagging real playback
    // between resyncs), which self-corrects at the next resync.
    store_anchor(state, HeardAnchor{start_sample, 0, 0});
    // THE NEW SESSION'S WORD: the next generation, playing. A plain store —
    // the audio thread's only write to this word is its terminal exchange,
    // which either landed before this (an ended old session, overwritten
    // here) or will fail against it (the field's comment). The generation is
    // read off the word as it stands, whichever of those it is.
    state.session.store(
        playback_session_next(state.session.load(std::memory_order_relaxed)),
        std::memory_order_release);
    return true;
}

// THE RESYNC anchors on the cycle stamp: the read cursor at the instant it
// is HEARD (its port instant plus the heard offset) — not at the main
// thread's `now`, which sat anywhere inside the period before that cursor's
// fill and re-rolled the lead by that much at every resync. The anchor's
// instant is in the future by the latency plus the fill's remaining phase;
// the reader extrapolates backward to `now` from it (predict_position), so
// the position it draws next is the one heard now and the resync's step is
// the drift the old anchor had accumulated, nothing else. AN UNSEATED
// SESSION IS LEFT AWAITING ITS SEAT: the stamp is still the previous
// session's, and the publish's (start, 0) anchor is exactly right until the
// latch fills it.
void playback_resync_predictor(GuiPlaybackState& state) {
    if (!playback_seated(state)) return;
    anchor_on_stamp(state);
}

bool playback_is_playing(const GuiPlaybackState& state) {
    // ACQUIRE, not relaxed: this load pairs with the audio thread's release
    // exchange of the session word at the natural end (playback_render_block),
    // which the callback makes after its last read of the borrowed sample
    // buffer. The conditional-stop sites (target_render.cpp's ensure_ready and
    // rebind_to_source) skip stop()'s quiescence fence on a false read, so
    // the acquire is what orders that final callback's buffer reads before
    // anything the caller mutates afterwards. Free on the target — x86 loads
    // already carry acquire ordering; the tightening is formal.
    return playback_session_playing(
        state.session.load(std::memory_order_acquire));
}

namespace {

// WHAT ONE OBSERVATION HANDS BACK: the position (buffer-local, pre-
// truncation), the hold's verdict, the playing bit, and — where an arm
// derived a fresh anchor — that anchor with `re_anchor` set, for a storing
// reader to write. Everything in it came from one load of the session word,
// one clock read and one read of the live heard offset.
struct Observation {
    double      predicted = 0.0;
    bool        playing   = false;
    bool        holding   = false;
    bool        re_anchor = false;
    HeardAnchor anchor;
};

// THE ONE MAIN-THREAD OBSERVATION (2026-09-01, the epoch reconciled AHEAD OF
// THE HOLD). Every predictor reader — playback_cursor, playback_cursor_
// precise, playback_natural_end_holding and playback_snapshot — is this body
// behind a face, and the body DERIVES ONLY: it stores nothing, handing a
// fresh anchor back for the storing faces to write (the declarations' block,
// playback_common.h), which is what lets the precise reader be the same pure
// function of the same atomics with no write, agreeing with the others
// whichever is asked first.
//
// THE EPOCH IS RECONCILED BEFORE ANY ANSWER IS FORMED, and that ordering is
// the point of there being one body (codex's second review of the arc: the
// tick asked the hold with the live offset while the anchor still carried
// the old one — with `ended` set on a 1536-frame anchor and the figure
// dropping to 544 before the next tick, the hold could answer false past
// `stamp + 544/rate`, the tick tore the scanner down, and the epoch arm the
// cursor reader carried never ran — the short-of-end teardown reachable
// again; and the cursor and the hold each took their own offset and stamp
// snapshots, so "one epoch by construction" was false across a notification
// landing between them). Here the live offset is read ONCE, a standing
// anchor built with another figure is replaced from the stamp WITH THIS ONE
// before either the position or the verdict exists, the hold's deadline is
// the stamp's port instant plus this same offset, and `now` is one read —
// so the line and the deadline move together at a latency change, in the
// same read, and a teardown decided on the verdict has the re-anchor under
// it.
//
// The stamp is read at most once, and only by the arms that need it (the
// latch, the epoch change, the hold's deadline) — the ordinary per-frame read
// of a standing anchor never reads it, which keeps playback.h's design note
// literal: the main thread reads the stamp at the resync events and these,
// never per frame, so motion between resyncs stays the smooth wall-clock
// line.
Observation observe(const GuiPlaybackState& state) {
    Observation o;
    // ACQUIRE on the session word: a flag read down that came from the
    // natural-end fill must see that fill's stamp (the render body stores it
    // ahead of its release exchange), or the hold would snap to the read
    // cursor for one paint before extrapolating on; the hold bit rides in
    // the same word, so the playing bit and the hold are one load's answer.
    const uint64_t word = state.session.load(std::memory_order_acquire);
    o.playing = playback_session_playing(word);
    const bool ended = playback_session_ended(word);
    if (!o.playing && !ended) {
        // At rest: the read cursor, where the last fill, the publish or the
        // bind left it.
        o.predicted = static_cast<double>(
            state.cursor.load(std::memory_order_relaxed));
        return o;
    }

    const int64_t now_ns    = steady_now_ns();
    // THE EPOCH THIS OBSERVATION IS IN — the one read of the live figure.
    const int64_t offset_ns = heard_offset_ns(state);
    bool        stamped = false;
    HeardAnchor stamp;
    const auto on_stamp = [&]() -> const HeardAnchor& {
        if (!stamped) {
            stamp   = heard_anchor_from_stamp(state, offset_ns);
            stamped = true;
        }
        return stamp;
    };

    if (state.output_rate.load(std::memory_order_relaxed) == 0) {
        // Device suspended: the audio thread is holding position, so the
        // playhead holds honestly at the integer cursor. Re-anchoring
        // continuously through the outage makes resume extrapolate from the
        // held position and wall-clock now, with no forward jump or snap-back
        // in either direction. The anchor takes the heard-instant form like
        // every other — held cursor at `now` plus the heard offset, which is
        // identically 0 while the rate reads 0. (Unreachable mid-session on
        // JACK, whose rate callback stores the new nonzero rate; session-
        // ending on AAudio, whose disconnect lowers the flag with the rate.)
        const int64_t cur = state.cursor.load(std::memory_order_relaxed);
        o.anchor    = HeardAnchor{cur, now_ns + offset_ns, offset_ns};
        o.re_anchor = true;
        o.predicted = static_cast<double>(cur);
    } else {
        HeardAnchor a{
            state.anchor_sample.load(std::memory_order_relaxed),
            state.anchor_ns.load(std::memory_order_relaxed),
            state.anchor_offset_ns.load(std::memory_order_relaxed)};
        bool awaiting_seat = false;
        if (a.heard_ns == 0) {
            // THE LAUNCH LATCH. Before the seat, the line RESTS at the
            // published start (a.sample). Once the audio thread has stamped
            // and consumed the pending, the anchor becomes the seat's stamp
            // at its heard instant — derived here, written exactly once per
            // session by the first storing reader to see it. NO BACKWARD
            // STEP: before the latch every read returns `start`; after it,
            // the anchor's heard instant is in the future by the heard offset
            // (plus however much of the seating fill has already been stamped
            // past), so predict_position extrapolates backward from it to a
            // position at or below `start` and floors it there — still
            // `start` — and only once `now` passes the instant the first
            // frame is heard does the line move. Both regimes are monotone in
            // `now`, the anchor is written once, and the floor never lets the
            // second dip under the first, so no paint can show a position
            // earlier than the previous paint's. (A stamp the seating fill
            // has already advanced past its seat — the main thread's first
            // read landing after that fill ended — names the same line: the
            // fill-end stamp is the same session's position at its own port
            // instant.)
            if (playback_seated(state)) {
                a           = on_stamp();
                o.anchor    = a;
                o.re_anchor = true;
            } else {
                awaiting_seat = true;
            }
        } else if (a.offset_ns != offset_ns) {
            // THE LATENCY EPOCH (the field's comment, playback_common.h): the
            // figure has moved under a standing anchor — a quantum change on
            // JACK — so re-anchor from the latest cycle stamp exactly as a
            // resync does, and the step the line takes is the latency delta,
            // once. The hold's deadline below is this same stamp at this
            // same offset, so from this read on the two are one epoch.
            a           = on_stamp();
            o.anchor    = a;
            o.re_anchor = true;
        }
        o.predicted = awaiting_seat
            ? static_cast<double>(a.sample)
            : predict_position(state, a.sample, a.heard_ns, now_ns);
    }

    // THE HOLD'S END IS THE STAMP'S: the natural-end fill stamped the instant
    // its last consumed frame entered the port, so the last sound leaves the
    // loudspeaker at that instant plus the heard offset — exact to the fill's
    // wake jitter, no belt needed. With a zero offset (AAudio) the hold ends
    // within the fill's own duration of the flag's drop. The offset is the
    // one read above and `now` the one read above, so the verdict and the
    // position cannot straddle a notification.
    o.holding = ended && now_ns < on_stamp().heard_ns;
    return o;
}

// THE STORING FACES' ONE WRITE: the anchor the observation derived, if any.
void commit_observation(GuiPlaybackState& state, const Observation& o) {
    if (o.re_anchor) store_anchor(state, o.anchor);
}

}  // namespace

bool playback_natural_end_holding(GuiPlaybackState& state) {
    const Observation o = observe(state);
    commit_observation(state, o);
    return o.holding;
}

int64_t playback_cursor(GuiPlaybackState& state) {
    // Every internal value (integer cursor, predictor anchor, end/total
    // clamps) is buffer-local; the bound buffer's domain offset is added once
    // at the return, so the reported position is a domain coordinate
    // (playback.h head comment).
    const Observation o = observe(state);
    commit_observation(state, o);
    return static_cast<int64_t>(std::floor(o.predicted)) + state.domain_offset;
}

double playback_cursor_precise(const GuiPlaybackState& state) {
    // The same observation WITHOUT THE STORE — the pre-truncation double, so
    // it agrees with cursor() exactly at the clamped window end and is
    // floor()-consistent with it elsewhere (the floor at `start` and the two
    // end clamps are integers, so floor commutes with each); the anchor it
    // derives is the one cursor() stores, a pure function of the same
    // atomics. The domain offset is added once, matching cursor()'s domain.
    return observe(state).predicted + static_cast<double>(state.domain_offset);
}

GuiPlaybackSnapshot playback_snapshot(GuiPlaybackState& state) {
    const Observation o = observe(state);
    commit_observation(state, o);
    return GuiPlaybackSnapshot{
        o.playing, o.holding,
        static_cast<int64_t>(std::floor(o.predicted)) + state.domain_offset};
}

int64_t playback_domain_begin(const GuiPlaybackState& state) {
    return state.domain_offset;
}

int64_t playback_domain_end(const GuiPlaybackState& state) {
    return state.domain_offset + state.total_frames;
}

void playback_rebind_buffer(GuiPlaybackState& state, const float* samples,
                            int64_t total_frames, int64_t domain_offset) {
    // Caller invariant: the device must be fenced by stop() before rebind.
    // Every caller lives in target_render.cpp, but a live device is NOT
    // guaranteed: playback.init() failure is deliberately non-fatal at source
    // load (no JACK server means the GUI runs playback-disabled), and a
    // target-preview completion then calls this with no device open. That
    // is harmless — this body performs no device call, the assignments below are
    // exactly the right stash for a device-less session, and there is no
    // re-init path that would ever consume them, so playback simply stays
    // disabled. This flag check is defense in depth for skipped stops; a
    // mid-flight pointer swap would be silent corruption. The refusal keeps the
    // buffer/offset pair consistent: neither is stored.
    // ACQUIRE, for playback_is_playing()'s reason: the role here stays defense in
    // depth — refuse on a true read — but a FALSE read falls straight through to
    // the pointer swap below, so the same pairing with the audio thread's
    // natural-end release exchange is what orders that last callback's buffer
    // reads before the assignments. Free on the target; the tightening is
    // formal.
    if (playback_session_playing(
            state.session.load(std::memory_order_acquire))) {
        std::fprintf(stderr,
            "warptempo_gui: rebind_buffer called while playing, refusing "
            "to swap the audio buffer (would race the callback)\n");
        return;
    }
    state.samples       = samples;
    state.total_frames  = total_frames;
    state.domain_offset = domain_offset;
    state.cursor.store(0, std::memory_order_relaxed);
    state.end_sample.store(0, std::memory_order_relaxed);
    state.pending_start.store(-1, std::memory_order_relaxed);
    state.fractional_cursor = 0.0;
    store_anchor(state, HeardAnchor{0, 0, 0});
    state.start_sample.store(0, std::memory_order_relaxed);
    // A new buffer ends any natural-end hold: the stop that fenced this
    // rebind already cleared it, and a device-less rebind has no hold to end.
    // The generation is kept — only a publish makes a new one.
    state.session.fetch_and(~kSessionEndedBit, std::memory_order_relaxed);
}
