#pragma once
#include <atomic>
#include <cstdint>

// THE PORTABLE HALF OF THE PLAYBACK ENGINE. playback.h is the CONTRACT and
// nothing here changes it: this file exists because that contract has two
// implementations — the JACK client (playback.cpp) and the AAudio stream
// (playback_aaudio.cpp) — and all but the device mechanics are the same
// engine in both.
//
// THE SPLIT, stated once:
//   * DEVICE (per backend, in the backend's own file): opening and closing the
//     device, registering the callbacks, the callback's own entry/exit, and
//     stop()'s QUIESCENCE FENCE — the one piece of the contract whose proof is
//     device-shaped: BOTH count callback cycles, and only AAudio adds a
//     terminal-state escape (`fence_state_is_quiesced`, playback_aaudio.cpp);
//     each states its choice at its own site.
//   * PORTABLE (here): the state every backend keeps, the audio-thread RENDER
//     BODY, and the main-thread predictor / domain / bind logic that the public
//     methods are thin wrappers over.
// A backend file therefore holds its device calls plus one-line forwards; a
// change to the ENGINE is a change to this file and reaches both platforms.
//
// THE OUTPUT RATE IS THE ONE THING THE TWO DEVICES DISAGREE ABOUT NUMERICALLY.
// `output_rate` is the rate the device asks its frames at, and the render
// body's per-output-frame source increment is `source_rate / output_rate` — the
// RATE RESCALING AND NOTHING ELSE, with the fractional read it drives as the
// resampler. On JACK that is the graph's rate (44100 on a graph pinned to the
// source, so the ratio is 1.0 and the increment is bare 1); on AAudio it is the
// rate the stream was GRANTED (48000 on the tablet's own speaker, which is 48 k
// hardware — 44.1 can never be native there), so the ratio is 44100/48000 and
// every output frame reads a fractional source position. A rate of 0 is the
// SUSPENDED device in both: the render body emits silence and holds position,
// and cursor() holds at the integer cursor rather than extrapolating
// (playback.h's graph-suspension clause).
//
// THERE IS NO SPEED FACTOR ANY MORE (architect 2026-08-27): the increment
// carried a `speed *` multiplier until that day, authored through the
// `playback_speed` settings key over a tenths-preset vocabulary. The architect
// runs 1.0 everywhere in his one live project, so the key retired whole and the
// factor went with it — the setter, the atomic word it published, and the
// predictor's speed input. What STAYS is the rate ratio above, which is not the
// same thing and never was: AAudio may hand the engine a rate the source is not
// recorded at, and that has to be rescaled whatever the speed.

// Sources are stereo-only (the channels != 2 load refusal), so playback runs
// exactly two output channels on every backend — two JACK ports, or the two
// interleaved lanes of one AAudio buffer.
constexpr int kPlaybackOutputChannels = 2;

// THE SESSION WORD'S LAYOUT (the field's comment, GuiPlaybackState::session):
// bit 0 playing, bit 1 ended naturally, the generation above them.
constexpr uint64_t kSessionPlayingBit = 1u;
constexpr uint64_t kSessionEndedBit   = 2u;
constexpr uint64_t kSessionGenShift   = 2u;
constexpr bool playback_session_playing(uint64_t word) {
    return (word & kSessionPlayingBit) != 0;
}
constexpr bool playback_session_ended(uint64_t word) {
    return (word & kSessionEndedBit) != 0;
}
// A new session's word: the next generation, playing, not ended.
constexpr uint64_t playback_session_next(uint64_t word) {
    return (((word >> kSessionGenShift) + 1) << kSessionGenShift) |
           kSessionPlayingBit;
}

// The engine state. Every backend's GuiPlayback::Impl holds one of these
// beside its own device handles.
struct GuiPlaybackState {
    // The rate the DEVICE asks frames at (the head comment). 0 = suspended.
    // Written by the backend at open/close and, on JACK, from the graph's
    // sample-rate callback; read from both threads.
    std::atomic<uint32_t> output_rate{0};

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
    // other position here stay buffer-local and never read this.
    int64_t      domain_offset = 0;

    // Current range. Updated from the main thread, read from the audio
    // thread. end_sample is exclusive.
    std::atomic<int64_t> end_sample{0};

    // Mutable playback state. `cursor` is the READ position: written once per
    // fill by the render body as the first source frame of the NEXT fill (one
    // whole buffer ahead of the block just rendered, and that block itself
    // `output_latency_frames` ahead of the loudspeaker). It is what the
    // resting readers snapshot — the render player's pause reads its resume
    // point here, the natural-end fill clamps it at `end_sample` — and it
    // is NOT the predictor's anchor: the predictor anchors on the cycle stamp
    // below, which pairs this same position with the instant it enters the
    // port.
    std::atomic<int64_t> cursor{0};
    // Free-running cursor predictor anchor, and EVERY ANCHOR IS A HEARD
    // INSTANT (architect 2026-09-01): the pair is (sample, the steady_clock ns
    // at which that sample LEAVES THE LOUDSPEAKER), and the main thread
    // extrapolates linearly from it in wall-clock time, in both directions —
    // a `now` before the anchor's instant reads the position heard at `now`,
    // floored at `start_sample`, which is how the line RESTS on the launch
    // frame until the first sound and never steps back. FOUR WRITERS, all
    // main thread, one form (`store_anchor`, playback_common.cpp): the launch
    // latch (playback_cursor's first read after the session is seated: the
    // stamp's sample at the stamp's port instant plus the heard offset), every
    // resync (playback_resync_predictor: the same stamp-plus-offset form, so a
    // resync's step is the accumulated steady_clock-vs-interface-clock DRIFT
    // alone — the period-wide phase residual and the latency figure are both
    // in the stamp), the LATENCY EPOCH re-anchor (playback_cursor's advancing
    // arm, the same stamp form again, the moment the live heard offset differs
    // from `anchor_offset_ns` below) and the suspended hold (playback_cursor
    // while output_rate reads 0: the held integer cursor at `now`, so a resume
    // extrapolates from the held position). play() publishes (start, 0) —
    // "await the seat" — and the latch fills the instant in exactly once per
    // session (the monotonicity argument is at that arm). Never written inside
    // the audio callback. Drift between predictor and audio is bounded by time
    // since the last resync × steady_clock vs sample-clock skew (sub-pixel at
    // typical zoom levels for typical resync intervals).
    std::atomic<int64_t> anchor_sample{0};
    std::atomic<int64_t> anchor_ns{0};
    // THE ANCHOR'S EPOCH: the heard offset (ns) the standing anchor was built
    // with — the third word of the anchor, written beside the other two by
    // every writer and main thread only like them. The device's latency figure
    // is ONE FIGURE PER EPOCH, not a session constant: a quantum change moves
    // it mid-session (the buffer-size callback re-reads it), and an anchor
    // built with the old figure would then be extrapolated in the new epoch —
    // the natural-end deadline, which reads the live figure, ending the hold
    // short of (or past) the end the anchor reaches. So the reader compares
    // the live offset to this word on every advancing read and, when they
    // differ, re-anchors from the latest cycle stamp exactly as a resync does
    // — one step of the latency delta, in the frame the figure changed.
    // Unseated sessions need nothing: the latch reads the live figure.
    std::atomic<int64_t> anchor_offset_ns{0};
    // The session's start, buffer-local — play()'s own, written beside the
    // anchor and read by the predictor as its FLOOR: an anchor stamped at a
    // heard instant still in the future extrapolates BACKWARD for a `now`
    // ahead of it, and this is where that extrapolation stops, so the line
    // rests on the launch frame until the first sound is actually heard.
    // Main thread only, like the anchor.
    std::atomic<int64_t> start_sample{0};

    // THE SESSION WORD — the playing flag, the natural-end hold and the
    // session generation in ONE atomic, so that the audio thread's terminal
    // write is GENERATION-QUALIFIED by construction (2026-09-01, closing the
    // play()-over-the-last-fill race): bit 0 is PLAYING, bit 1 is ENDED
    // NATURALLY, the bits above are the generation, incremented by every
    // publish. Layout at the constants below; the readers ask
    // playback_session_playing / _ended of a loaded word.
    //
    // THE FLAG (bit 0) is the one release/acquire synchronization point
    // between the two threads, as the bool it replaced was: play()'s publish
    // stores the new word — generation + 1, playing — with release as the last
    // step of its publish block, and the backend's callback loads the word
    // with acquire at its gate, so a callback that sees the bit up sees the
    // range and pending-start stores that preceded it. stop() lowers both bits
    // in one fetch_and ahead of its quiescence fence; the AAudio disconnect and
    // both shutdowns lower the flag the same way.
    //
    // THE TERMINAL (the render body's natural end) is ONE compare-exchange on
    // this word: expected = the word the fill captured at its top (generation
    // N, playing), desired = (generation N, ended). It succeeds only if no
    // publish (a newer generation) and no stop (the bit already down) has
    // written the word since the fill began, and a publish landing AFTER a
    // successful exchange simply overwrites it with the new generation's word.
    // So a stale fill can never lower a newer session's flag: before the word,
    // a play() published between the old fill's gate and its natural-end
    // stores let that fill store `playing = false` after the new publish's
    // `true`, stranding the new pending_start behind the silence gate — the
    // new scanner rested at its seed until the tick tore it down. The
    // exchange's success order is RELEASE, pairing with every main-thread
    // acquire load of the word (is_playing, the cursor readers, the rebind
    // check) exactly as the bool's release store did: the fill's last buffer
    // reads and its stamp and cursor stores are ordered before whatever the
    // reader mutates afterwards. The capture at the fill's top is a relaxed
    // load ordered by the gate's own acquire on the same word; a publish that
    // lands between the gate and the capture makes the fill the NEW session's
    // (it consumes that pending and its terminal names that generation), and
    // one that lands after the capture makes the exchange fail — the fill
    // abandons the terminal, having rendered a block of the old session into
    // this buffer, and the next callback seats the new one. A failed exchange
    // is the audio thread's ONLY reaction; it never retries and never writes
    // the word otherwise.
    //
    // THE NATURAL-END HOLD (bit 1, architect 2026-09-01) is set by that same
    // exchange — the fill that read past `end_sample` — and read by
    // playback_cursor's not-playing arm, which keeps extrapolating from the
    // anchor (clamped at `end_sample`) while it stands, and by
    // playback_natural_end_holding, which answers true until the last frame
    // that fill consumed has been HEARD (the stamp's port instant plus the
    // heard offset). So the drawn playhead outlives the flag by exactly the
    // output latency and vanishes when the SOUND ends, not `latency + (0,
    // period]` early. The flag still drops at the terminal — is_playing()
    // reads bit 0 alone — so the fence-before-rebind ordering every
    // conditional stop rests on is unchanged, and riding in the same word the
    // hold bit is visible to every reader that saw the flag drop, with no
    // second ordering to argue. THREE CLEARERS, all main thread: play()'s
    // publish (a fresh word, no bits), the backends' stop() (the fetch_and at
    // its head, ahead of the fence — a fill in flight can commit no terminal
    // once the flag is down, its exchange expecting the flag up), and bind /
    // rebind (a new buffer). The audio thread never clears it: a session that
    // ends is over for the callback the moment the flag drops, and the hold is
    // the main thread's picture of the sound still in the device's queue.
    //
    // THE GENERATION never wraps in practice (a publish per press) and is
    // never read as a number by anyone but the publish; it exists so two
    // words that mean "playing" from different sessions can never compare
    // equal. bind resets the whole word (no callback runs there); rebind
    // clears the hold bit alone and keeps the generation.
    std::atomic<uint64_t> session{0};

    // THE DEVICE'S OUTPUT LATENCY IN OUTPUT FRAMES: the time between a frame
    // entering our output port and its leaving the loudspeaker, as the backend
    // reports it. Written by the backend from its own notification threads
    // (JACK: the latency and buffer-size callbacks, and one read after the
    // auto-connect — playback.cpp's refresh_output_latency, which also prints
    // the figure to stderr whenever it changes) and read by the predictor's
    // anchor writers on the main thread as `heard_offset_ns` (playback_common.
    // cpp): `output_latency_frames × 1e9 / output_rate`, 0 while suspended.
    // ONE FIGURE PER EPOCH, re-anchored at the change: a mid-session change
    // is consumed by playback_cursor's epoch check against `anchor_offset_ns`
    // (that field's comment), so no anchor is ever extrapolated with a figure
    // other than the one it was built with.
    // AAUDIO NEVER WRITES IT — the recorded asymmetry (the record is at the
    // AAudio Impl): the tablet's route is Bluetooth in the car, whose latency
    // is large, variable and unreported to the stream, so no figure the
    // framework could report is worth anchoring to, and the tablet's predictor
    // keeps the uncompensated lead (architect 2026-09-01). A device fact, not
    // the binding's: it survives rebinds and is zeroed where the backend zeroes
    // output_rate (clear_after_failed_init, shutdown).
    std::atomic<int64_t> output_latency_frames{0};

    // THE CYCLE STAMP — (stamp_cursor, stamp_ns): a source position and the
    // steady_clock ns at which that position ENTERS THE OUTPUT PORT, published
    // by the audio thread and read by the main thread under a generation
    // SEQLOCK (stamp_gen: odd while a write is in progress, even between
    // writes). TWO WRITE SITES, both in playback_render_block and both on the
    // audio thread, one writer: the SEAT — the fill that absorbs `pending_start`
    // stamps (pending, fill_ns), the instant that fill began, BEFORE it consumes
    // the pending (the compare-exchange that consumes it is the seat's
    // publication: a main thread that reads pending_start == -1 with acquire
    // then reads a stamp at least this new, never the previous fill's) — and
    // THE END OF EVERY FILL, (cursor, fill_ns + consumed × 1e9 / output_rate),
    // the read cursor with the instant its frame enters the port (the frames
    // this fill consumed lead it in the port buffer; on the natural-end fill
    // `consumed` is the count read before `end_sample`, so the stamp names the
    // instant the last sound enters the port exactly). The predictor's anchor
    // writers read it and add the heard offset; playback_natural_end_holding
    // reads it for the hold's end. Never reset: every reader gates on the seat
    // (pending_start == -1) or on the session word's ended bit, each of which
    // a publish, a bind or a rebind puts back, so a stale stamp is never
    // anchored to.
    //
    // THE SEQLOCK'S ORDERING (the classic shape, all three words atomics so a
    // torn read is impossible and the lock guards only the pair's
    // CONSISTENCY). Writer: gen = odd (relaxed), a release fence, the two data
    // stores (relaxed), gen = even (release). Reader: g1 = gen (acquire) —
    // retry while odd — the two data loads (relaxed), an acquire fence, g2 =
    // gen (relaxed), accept iff g1 == g2. If g1 read the even store, its
    // acquire pairs with that store's release and the data loads see the
    // completed pair. If g1 read an older even value and a write was in
    // flight, then either no data load saw a new store — the old pair, and g1
    // == g2 accepts it consistently — or one did, in which case that load
    // reads-from a store sequenced after the writer's release fence, the
    // reader's acquire fence synchronizes with it, the odd gen store sequenced
    // before the fence is visible to g2, and g1 != g2 rejects the mix. The
    // writer never waits (the audio thread is never blocked by a reader), the
    // reader spins only across the writer's four stores, and nothing here
    // allocates or calls into the system.
    std::atomic<uint64_t> stamp_gen{0};
    std::atomic<int64_t>  stamp_cursor{0};
    std::atomic<int64_t>  stamp_ns{0};

    // Audio-thread-only fractional source cursor. Tracking the fractional
    // position across buffer boundaries is what prevents per-buffer floor()
    // rounding from compounding into audible drift between audio and visual
    // playhead over long playback. The integer `cursor` is snapshotted from
    // this each buffer for the main thread to read.
    double fractional_cursor = 0.0;

    // Main thread sets a pending restart position via play(); the audio
    // thread picks it up at the top of its next fill to reseat
    // fractional_cursor without a lock. -1 sentinel means "no pending" — and
    // since 2026-09-01 that sentinel is also THE SEAT'S PUBLICATION: the render
    // body stamps (pending, fill_ns) and only then compare-exchanges the word
    // to -1 (a newer pending landing in between fails the exchange and is
    // stamped and consumed in its turn), so a main-thread acquire load reading
    // -1 after its own publish knows the stamp is its session's. The
    // predictor's launch latch and every resync ask exactly that
    // (playback_seated, playback_common.cpp) before they anchor.
    std::atomic<int64_t> pending_start{-1};
};

// ---------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------

// Zero `frames` output frames beginning at output frame `first`, on every
// channel. THE STRIDE IS THE ONLY THING THE TWO BACKENDS' OUTPUT BUFFERS
// DISAGREE ABOUT: JACK hands the callback one contiguous float buffer PER PORT
// (channel_buffers[c], stride 1), AAudio one INTERLEAVED buffer (channel_buffers
// [c] = base + c, stride = the channel count). Stride 1 takes the contiguous
// memset; an interleaved destination steps. No allocation, no I/O, no locks —
// this runs on the audio thread.
void playback_write_silence(float* const* channel_buffers, int channel_count,
                            int64_t stride, int64_t first, int64_t frames);

// THE RENDER BODY. Copy `frame_count` output frames at the current output rate,
// advancing the cursor. Stops early and fills the remainder with
// silence if the cursor would pass end_sample. Writes the final source-cursor
// back to state.cursor before returning, with the cycle stamp beside it; on
// natural end, also swaps the session word from playing to ended — the
// generation-qualified terminal (the field's comment), abandoned if a newer
// session was published since this fill began. Audio thread only; the
// backend's callback calls this after its own acquire gate on the session
// word. See playback_write_silence for the stride. ONE CLOCK READ per fill
// (the vDSO's steady_clock, at the fill's top), one compare-exchange when a
// pending restart is absorbed, one at a natural end, the seqlock's stores —
// no lock, no allocation, no syscall.
void playback_render_block(GuiPlaybackState& state,
                           float* const* channel_buffers,
                           int64_t stride,
                           int64_t frame_count,
                           int channel_count);

// ---------------------------------------------------------------------------
// Main thread
// ---------------------------------------------------------------------------

// Drop the bound buffer. Called by the two init refusals below, by a
// backend's failed-open cleanup and by shutdown(); the backend owns
// output_rate, which is its device's fact rather than the binding's.
void playback_clear_binding(GuiPlaybackState& state);

// init()'s portable prologue: seed every engine field for a fresh device, then
// validate the source. Returns false (having logged and cleared the binding)
// for a non-stereo source or an invalid buffer — the backend then returns
// false from init() without touching its device. The backend calls this FIRST,
// before it opens anything.
bool playback_bind_and_validate(GuiPlaybackState& state, int sample_rate,
                                int channels, const float* samples,
                                int64_t total_frames, int64_t domain_offset);

// play()'s portable half: translate the domain bounds to buffer-local, clamp,
// and publish the range + restart position + predictor anchor to the audio
// thread. Returns whether it published — false means the request was out of
// range and nothing was written, so the backend must not start its device.
// The backend checks its own device readiness BEFORE calling.
bool playback_publish_play(GuiPlaybackState& state, int64_t start_sample,
                           int64_t end_sample);

void   playback_resync_predictor(GuiPlaybackState& state);
bool   playback_is_playing(const GuiPlaybackState& state);
// The natural-end hold's whole test (the session word's ended bit): the
// session ended at its window's end and the last sound it queued has not yet
// left the loudspeaker. False while playing, false after any stop.
bool   playback_natural_end_holding(const GuiPlaybackState& state);
int64_t playback_cursor(GuiPlaybackState& state);
double playback_cursor_precise(const GuiPlaybackState& state);
int64_t playback_domain_begin(const GuiPlaybackState& state);
int64_t playback_domain_end(const GuiPlaybackState& state);

// rebind_buffer()'s whole body — no device call in it on either backend
// (playback.h's rebind contract, and the reasoning for the refuse-while-playing
// check, are at the public declaration and repeated at the definition).
void playback_rebind_buffer(GuiPlaybackState& state, const float* samples,
                            int64_t total_frames, int64_t domain_offset);
