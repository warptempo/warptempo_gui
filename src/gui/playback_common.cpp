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
// guaranteed to see the command packet that preceded the release — and the
// packet carries the same generation, so the fill consumes only the window
// published for the session it was gated into (the packet's comment,
// playback_common.h); the render body's natural-end terminal is a release
// compare-exchange on the same word, qualified by the generation the word
// carries (the field's comment). The sample buffer is read-only from the
// audio thread's point of view, and its address lives across the device's
// entire life.
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

// THE CYCLE STAMP as read: a position, the instant it enters the port, and
// the generation of the session it belongs to (the fields' comment,
// playback_common.h).
struct CycleStamp {
    int64_t  cursor     = 0;
    int64_t  port_ns    = 0;
    uint64_t generation = 0;
};

// THE CYCLE STAMP'S WRITER — audio thread only (the seqlock's ordering
// argument is at the fields, playback_common.h).
void write_stamp(GuiPlaybackState& state, int64_t cursor, int64_t port_ns,
                 uint64_t generation) {
    const uint64_t g = state.stamp_gen.load(std::memory_order_relaxed);
    state.stamp_gen.store(g + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    state.stamp_cursor.store(cursor, std::memory_order_relaxed);
    state.stamp_ns.store(port_ns, std::memory_order_relaxed);
    state.stamp_generation.store(generation, std::memory_order_relaxed);
    state.stamp_gen.store(g + 2, std::memory_order_release);
}

// THE CYCLE STAMP'S READER — main thread; retries across a write in progress
// and never blocks the writer.
CycleStamp read_stamp(const GuiPlaybackState& state) {
    CycleStamp s;
    for (;;) {
        const uint64_t g1 = state.stamp_gen.load(std::memory_order_acquire);
        if (g1 & 1u) continue;
        s.cursor     = state.stamp_cursor.load(std::memory_order_relaxed);
        s.port_ns    = state.stamp_ns.load(std::memory_order_relaxed);
        s.generation = state.stamp_generation.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (state.stamp_gen.load(std::memory_order_relaxed) == g1) return s;
    }
}

// IS THIS STAMP THE SESSION THIS WORD NAMES — its own fill's, rather than a
// previous session's? A publish makes a generation no stamp yet carries, so a
// stamp taken before the new session's first fill answers false and the
// resync leaves the launch anchor standing. Both reads are the caller's.
bool stamp_is_sessions(const CycleStamp& s, uint64_t session_word) {
    return s.generation == playback_session_generation(session_word);
}

// AN ANCHOR: the two words the predictor extrapolates from — a position and
// the instant the main thread takes that position to be the play position.
struct Anchor {
    int64_t sample = 0;
    int64_t ns     = 0;
};

// THE ANCHOR FORM BUILT OFF A STAMP: the stamped position at the instant it
// enters the output port. The resync stores exactly this — nothing is added
// to the port instant, so the anchor is where the SOUND IS BEING WRITTEN,
// not where it is heard (the anchor's field comment, playback_common.h).
Anchor stamp_anchor(const CycleStamp& s) {
    return Anchor{s.cursor, s.port_ns};
}

// THE ANCHOR'S ONE STORE — every writer named at the fields lands here, so
// the two words are always written together: the observation's suspended
// form through commit_observation, the resync, and the resets at publish,
// bind and rebind. Main thread only.
void store_anchor(GuiPlaybackState& state, const Anchor& a) {
    state.anchor_sample.store(a.sample, std::memory_order_relaxed);
    state.anchor_ns.store(a.ns, std::memory_order_relaxed);
}

// THE EXTRAPOLATION, shared by the two readers: the anchor's position advanced
// by wall-clock time at the SOURCE rate (the cursor is in source frames and
// wall-clock advances it at that rate whatever rate the device runs), then
// floored at the session's start (the line never draws below the frame the
// session was launched on) and clamped at the window's and the buffer's ends.
// Returned as the pre-truncation double; cursor() floors it.
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
    // passed the gate). Its generation is the ONLY command packet this fill
    // may consume.
    const uint64_t generation = playback_session_generation(session_word);
    const int      src_channels = state.channels;
    const uint32_t output_rate = state.output_rate.load(std::memory_order_relaxed);
    // The device asks for output-rate frames; the output-to-source rate ratio
    // IS the fractional source read increment (bare 1.0 whenever the device
    // runs at the source's own rate, which is every JACK graph pinned to it).
    const double increment = output_rate == 0
        ? 0.0
        : static_cast<double>(state.source_rate) / static_cast<double>(output_rate);
    const int64_t total = state.total_frames;

    if (increment == 0.0) {
        playback_write_silence(channel_buffers, channel_count, stride, 0, frame_count);
        // Device unavailable: emit silence and hold the playback position.
        // The command packet is deliberately not consumed here, so a play()
        // issued during the outage is seated by the first fill after the
        // device returns.
        return;
    }

    // THE FILL'S ONE CLOCK READ, at its top: the instant this fill's first
    // frame enters the port, from which the fill-end stamp derives. Its error
    // against the true cycle start is the driver-wakeup-to-callback time, tens
    // of microseconds — a fraction of a frame.
    const int64_t fill_ns = steady_now_ns();

    // THE SEAT: while this generation is unseated, read its command packet
    // ONCE — accepted only under the gate's own generation, complete, and
    // unchanged across the two data loads (the packet's ordering argument,
    // playback_common.h) — and seat it: the private window end and the
    // fractional cursor at the published start. NO RETRY: a rejected read
    // means a newer publish is under way, and the next callback's gate
    // acquires it; a fill that cannot seat its own generation writes silence
    // and returns — that session was superseded before it sounded.
    if (state.active_generation != generation) {
        const uint64_t g1 = state.command_seq.load(std::memory_order_acquire);
        if (g1 == playback_command_seq(generation)) {
            const int64_t start = state.command_start.load(std::memory_order_relaxed);
            const int64_t end   = state.command_end.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (state.command_seq.load(std::memory_order_relaxed) == g1) {
                state.active_generation = generation;
                state.active_end        = end;
                state.fractional_cursor = static_cast<double>(start);
            }
        }
        if (state.active_generation != generation) {
            playback_write_silence(channel_buffers, channel_count, stride, 0,
                                   frame_count);
            return;
        }
    }
    // THE WINDOW END IS THE FILL'S OWN: consumed with the packet at the seat
    // and kept private since, so a publish landing mid-fill cannot move it
    // under the loop below.
    const int64_t end = state.active_end;

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
    // end) — tagged with this fill's generation. It is what a RESYNC anchors
    // on (playback_resync_predictor), which is the whole of its consumption.
    // Stamped ahead of the cursor store and the flag, so a main thread that
    // sees the flag drop (acquire) sees this stamp too.
    write_stamp(state, new_cur,
                fill_ns + n * 1000000000LL / static_cast<int64_t>(output_rate),
                generation);
    state.cursor.store(new_cur, std::memory_order_relaxed);
    if (natural_end) {
        // THE TERMINAL, generation-qualified (the session word's comment):
        // playing -> not playing on exactly the word the gate acquired for
        // this fill, the generation kept so the word never compares equal to
        // another session's. A failed exchange means a newer session was
        // published, or a stop lowered the flag, since the gate: the terminal
        // is ABANDONED — this block of the gated session has sounded under its
        // own window, the new session's packet stands unconsumed, and the next
        // callback acquires and seats it.
        uint64_t expected = session_word;
        state.session.compare_exchange_strong(
            expected, session_word & ~kSessionPlayingBit,
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
    store_anchor(state, Anchor{0, 0});
    state.start_sample.store(0, std::memory_order_relaxed);
    state.end_sample.store(0, std::memory_order_relaxed);
    // The whole session word, generation included, the command packet with it
    // (no packet stands until a publish writes one) AND THE CYCLE STAMP: no
    // callback runs here (the backend binds before it opens its device), which
    // is what admits the audio-thread-private resets and what makes bind the
    // stamp's one non-audio-thread writer — with no concurrent writer and no
    // concurrent reader there is nothing to order against, so the four words
    // are stored plainly and `stamp_gen` goes back to an EVEN value (a write in
    // progress is impossible here), which the next reader accepts at once.
    // THE STAMP MUST BE RESET WITH THE WORD because bind returns the session
    // generation to 0 and init() is reusable (it opens with an idempotent
    // shutdown()): a second init() on the same object for a new source would
    // otherwise leave the old source's generation-1 stamp standing for the new
    // source's own first publish — also generation 1 — for a resync taken
    // before that session's first fill to anchor on, putting the line at the
    // OLD source's position.
    // Together the two resets are what makes the stamp field's claim true:
    // no stamp carries a generation a new publish could use.
    state.session.store(0, std::memory_order_relaxed);
    state.command_seq.store(0, std::memory_order_relaxed);
    state.command_start.store(0, std::memory_order_relaxed);
    state.command_end.store(0, std::memory_order_relaxed);
    state.stamp_gen.store(0, std::memory_order_relaxed);
    state.stamp_cursor.store(0, std::memory_order_relaxed);
    state.stamp_ns.store(0, std::memory_order_relaxed);
    state.stamp_generation.store(0, std::memory_order_relaxed);
    state.active_generation = 0;
    state.active_end        = 0;
    state.fractional_cursor = 0.0;
    state.output_rate.store(0, std::memory_order_relaxed);

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

// THE LINE LEADS THE SOUND, AND THAT IS THE RULING (architect 2026-09-03).
// The predictor anchors HERE, at the publish instant, and extrapolates in
// wall-clock at the source rate from there; nothing is added to that instant
// and nothing is added to the `now` a position is read at. So the painted
// line lights at `press + D` (the compositor's paint-to-light delay) while
// the sound starts at `press + phase + L_audio` — the JACK pickup phase, 0
// to one period, then the device's own output latency — and the line
// therefore runs ahead of the ear by `L_audio − D + phase`, varying with the
// phase from launch to launch. THAT IS THE CHOSEN BEHAVIOUR, not a defect
// left standing: on 2026-09-03 the architect ran a blind comparison at his
// own rig (33 ms measured display lead, 1536 frames = 34.8 ms JACK output
// latency) between this build and a fully compensated one, and chose this
// one — "two continuous streams that we sort of happen to pick up here and
// there — sometimes they match, sometimes they don't, and that's exactly
// what they are". EVERY LAUNCH ROAD SHARES IT: bare Space, the waveform
// scrub, the A/B audition's four bounded plays and the render player's own
// launch all publish through this one body.
//
// COMPENSATION WAS BUILT AND ROLLED BACK (2026-09-01/02, removed
// 2026-09-03). The mechanism — the anchor moved onto the audio thread's
// first fill, the reported output latency added to every anchor under a
// per-epoch re-anchor, a self-measured display lead added to the position
// read, and a natural-end hold keeping the line alive until the last queued
// frame was heard — lives in git history and nowhere else in this tree. What
// SURVIVES it is not compensation: the RESYNC anchors on the cycle stamp,
// (the read cursor, the instant that cursor's frame enters the port), rather
// than on the main thread's `now`, which is an ACCURACY choice for the same
// raw line — it takes the period-wide phase re-roll out of a resync so the
// step is the accumulated drift alone.
//
// AND NO MEASUREMENT IS TAKEN OR PRINTED. The instruments the arc added —
// the JACK port-latency figure with its two callbacks and its stderr line,
// and the Wayland presentation-feedback lead with its own — went with the
// leads on the same ruling: the product does not measure what the user has
// not asked it to measure. The panel that is to offer them on demand is a
// later arc and leaves no stub here.
bool playback_publish_play(GuiPlaybackState& state, int64_t start_sample,
                           int64_t end_sample) {
    if (!state.samples || state.total_frames <= 0) return false;
    // Domain -> buffer-local at the API boundary (playback.h head comment).
    // Everything below — the clamps, the early returns, the command packet
    // this publishes, the main thread's window mirror and cursor beside it,
    // and the launch anchor — is buffer-local, exactly as before the
    // offset moved in here.
    start_sample -= state.domain_offset;
    end_sample   -= state.domain_offset;
    if (start_sample < 0) start_sample = 0;
    if (start_sample >= state.total_frames) return false;
    if (end_sample > state.total_frames) end_sample = state.total_frames;
    if (end_sample <= start_sample) return false;

    // THE NEW SESSION'S GENERATION, read off the word as it stands — the
    // audio thread's only write to this word is its terminal exchange, which
    // either landed before this (an ended old session, overwritten below) or
    // will fail against the word stored below (the field's comment).
    const uint64_t word = playback_session_next(
        state.session.load(std::memory_order_relaxed));
    const uint64_t generation = playback_session_generation(word);

    // THE COMMAND PACKET, written under its sequence word for exactly this
    // generation (the packet's ordering argument, playback_common.h): busy,
    // a release fence, the window, complete. The main thread is the packet's
    // one writer, so the busy store and the complete store are this block's
    // alone; the fill that reads the complete store under the gate's own
    // generation has the whole window.
    state.command_seq.store(playback_command_seq(generation) | 1u,
                            std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    state.command_start.store(start_sample, std::memory_order_relaxed);
    state.command_end.store(end_sample, std::memory_order_relaxed);
    state.command_seq.store(playback_command_seq(generation),
                            std::memory_order_release);

    // THE MAIN THREAD'S MIRROR of the window (the fields' comment) and the
    // integer `cursor`, set here so the main thread sees a consistent
    // snapshot immediately (before the next buffer runs).
    state.start_sample.store(start_sample, std::memory_order_relaxed);
    state.end_sample.store(end_sample, std::memory_order_relaxed);
    state.cursor.store(start_sample, std::memory_order_relaxed);

    // THE LAUNCH ANCHOR IS THE PUBLISH INSTANT (architect 2026-09-03, the
    // record above): (start, now). The line therefore starts moving the
    // moment the press lands, ahead of the first fill and ahead of the sound
    // by the pickup phase plus the device's latency. The anchor pair is
    // written and read only on the main thread, and each 8-byte aligned load
    // is atomic on the target, so there is no torn read; the predictor
    // tolerates only bounded staleness (the anchor lagging real playback
    // between resyncs), which self-corrects at the next resync.
    store_anchor(state, Anchor{start_sample, steady_now_ns()});
    // THE NEW SESSION'S WORD: the next generation, playing — the release that
    // publishes the packet above to the callback's acquire gate. A plain
    // store, for the reason given at the generation's read.
    state.session.store(word, std::memory_order_release);
    return true;
}

// THE RESYNC ANCHORS ON THE CYCLE STAMP — (the read cursor, the instant its
// frame enters the output port) — and NOT on the main thread's `now`, which
// sat anywhere inside the period before that cursor's fill and re-rolled that
// whole phase into the line at every resync. THIS IS ACCURACY, NOT
// COMPENSATION (architect 2026-09-03, the rollback's one survivor from the
// lead arc): nothing is added to the port instant, so the anchor names the
// same raw line the launch does — what the stamp removes is the resync's own
// period-wide step, leaving the accumulated drift alone. A SESSION WHOSE
// FIRST FILL HAS NOT RUN IS LEFT ALONE: the stamp is still the previous
// session's (its generation says so), and the publish's launch anchor is
// exactly right. One stamp read, one word load.
void playback_resync_predictor(GuiPlaybackState& state) {
    const CycleStamp s = read_stamp(state);
    if (!stamp_is_sessions(s, state.session.load(std::memory_order_acquire)))
        return;
    store_anchor(state, stamp_anchor(s));
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
// truncation), the playing bit, and — where the suspended arm derived a fresh
// anchor — that anchor with `re_anchor` set, for a storing reader to write.
// Everything in it came from one load of the session word and one clock read.
struct Observation {
    double predicted = 0.0;
    bool   playing   = false;
    bool   re_anchor = false;
    Anchor anchor;
};

// THE ONE MAIN-THREAD OBSERVATION. Both predictor readers — playback_cursor
// and playback_cursor_precise — are this body behind a face, and the body
// DERIVES ONLY: it stores nothing, handing a fresh anchor back for the
// storing face to write (the declarations' block, playback_common.h), which
// is what lets the precise reader be the same pure function of the same
// atomics with no write, agreeing with the other whichever is asked first.
//
// IT READS THE ANCHOR AND THE CLOCK AND NOTHING ELSE. There is no latency
// term, no display lead and no cycle-stamp read here: the anchor was written
// by the publish (at the publish instant) or by a resync (at a stamp's port
// instant), and the position is that anchor extrapolated to `now` — the raw
// line, ahead of the ear by the device's output latency and the launch's
// pickup phase, which is the ruling (the record above playback_publish_play).
// Keeping the stamp out of the per-frame path is also what keeps playback.h's
// design note literal: the main thread reads the stamp at the resync events
// and nowhere else, so motion between resyncs is a smooth wall-clock line.
Observation observe(const GuiPlaybackState& state) {
    Observation o;
    // ACQUIRE on the session word: it pairs with the audio thread's release
    // exchange at the natural end, so a flag read down is ordered after that
    // fill's last buffer reads and its cursor store — which is the value the
    // resting arm below then answers with.
    const uint64_t word = state.session.load(std::memory_order_acquire);
    o.playing = playback_session_playing(word);
    if (!o.playing) {
        // At rest: the read cursor, where the last fill, the publish or the
        // bind left it.
        o.predicted = static_cast<double>(
            state.cursor.load(std::memory_order_relaxed));
        return o;
    }

    const int64_t now_ns = steady_now_ns();

    if (state.output_rate.load(std::memory_order_relaxed) == 0) {
        // Device suspended: the audio thread is holding position, so the
        // playhead holds honestly at the integer cursor. Re-anchoring
        // continuously through the outage makes resume extrapolate from the
        // held position and wall-clock now, with no forward jump or snap-back
        // in either direction. (Unreachable mid-session on JACK, whose rate
        // callback stores the new nonzero rate; session-ending on AAudio,
        // whose disconnect lowers the flag with the rate.)
        const int64_t cur = state.cursor.load(std::memory_order_relaxed);
        o.anchor    = Anchor{cur, now_ns};
        o.re_anchor = true;
        o.predicted = static_cast<double>(cur);
    } else {
        const Anchor a{
            state.anchor_sample.load(std::memory_order_relaxed),
            state.anchor_ns.load(std::memory_order_relaxed)};
        // BEFORE THE FIRST ANCHOR: bind and rebind leave the pair zeroed, and
        // a play() publishes a real instant with the window, so a zero here
        // means no session has been published against this binding yet — the
        // position is the anchor's own sample, extrapolated from nothing.
        o.predicted = a.ns == 0
            ? static_cast<double>(a.sample)
            : predict_position(state, a.sample, a.ns, now_ns);
    }

    return o;
}

// THE STORING FACES' ONE WRITE: the anchor the observation derived, if any.
void commit_observation(GuiPlaybackState& state, const Observation& o) {
    if (o.re_anchor) store_anchor(state, o.anchor);
}

}  // namespace

int64_t playback_cursor(GuiPlaybackState& state) {
    // THIS FLOOR IS THE ROUNDING RULE'S DECLARED EXCEPTION ON A SAMPLE INDEX,
    // and it is declared here the way the overview lane declares its own
    // (app_state.cpp): the digest classes a sample index as a POINT on the
    // sample grid, taking nearbyint, and every other site in the tree does —
    // but a PLAY POSITION is a CELL. Frame n covers the whole interval a
    // fractional position lands in until n + 1 begins, which is exactly what
    // the render body's own natural-end test says one layer down, so a
    // predicted 12.9 is still frame 12 and floor is what agrees with the ear.
    // Nothing here rounds; the "pre-truncation double" the neighbours name is
    // this same value before the floor.
    //
    // Every internal value (integer cursor, predictor anchor, end/total
    // clamps) is buffer-local; the bound buffer's domain offset is added once
    // at the return, so the reported position is a domain coordinate
    // (playback.h head comment).
    //
    // THE ONE POSITION FACE, painting and resting alike (architect
    // 2026-09-03): a second face that read the same line at a different
    // instant existed while the display lead did — the render player's pause
    // point took it — and went with the lead. There is one line and one
    // reading of it.
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
    return observe(state).predicted +
           static_cast<double>(state.domain_offset);
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
    state.start_sample.store(0, std::memory_order_relaxed);
    state.end_sample.store(0, std::memory_order_relaxed);
    // No packet stands for the new buffer (the next publish writes one) and
    // the audio-thread-private window is reset with it — admitted here, as
    // the fractional cursor's reset always was, because the fence the caller
    // took keeps the callback out of the render body.
    state.command_seq.store(0, std::memory_order_relaxed);
    state.command_start.store(0, std::memory_order_relaxed);
    state.command_end.store(0, std::memory_order_relaxed);
    state.active_generation = 0;
    state.active_end        = 0;
    state.fractional_cursor = 0.0;
    // The zeroed pair is the readers' "before the first anchor" state: nothing
    // has been published against this buffer, so the line rests at 0 until a
    // play() writes a real launch anchor. The session word is left alone —
    // the stop that fenced this rebind lowered the playing bit, and only a
    // publish makes a new generation.
    store_anchor(state, Anchor{0, 0});
}
