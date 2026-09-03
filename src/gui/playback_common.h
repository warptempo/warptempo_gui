#pragma once
#include "playback.h"

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
// bit 0 playing, the generation above it. The word is process-local — bind
// resets it and nothing serializes it — so the layout is free to close up: a
// natural-end hold rode bit 1 for two days (2026-09-01/02) and left with the
// playback leads on 2026-09-03, and the generation moved back down over it.
constexpr uint64_t kSessionPlayingBit = 1u;
constexpr uint64_t kSessionGenShift   = 1u;
constexpr bool playback_session_playing(uint64_t word) {
    return (word & kSessionPlayingBit) != 0;
}
// The generation a word carries — what the command packet's sequence word and
// the cycle stamp are tagged with (the fields below).
constexpr uint64_t playback_session_generation(uint64_t word) {
    return word >> kSessionGenShift;
}
// A new session's word: the next generation, playing.
constexpr uint64_t playback_session_next(uint64_t word) {
    return ((playback_session_generation(word) + 1) << kSessionGenShift) |
           kSessionPlayingBit;
}
// THE COMMAND PACKET'S SEQUENCE WORD for a generation: the generation shifted
// up one, the low bit the writer's busy bit (the packet's comment below). The
// audio thread accepts a packet only when the word reads exactly this value
// for the generation its gate acquired.
constexpr uint64_t playback_command_seq(uint64_t generation) {
    return generation << 1;
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

    // THE PUBLISHED WINDOW, THE MAIN THREAD'S MIRROR: the session's start and
    // exclusive end as play() published them, buffer-local, written by the
    // publish (and zeroed by bind / rebind) and read by the predictor alone —
    // `start_sample` is its FLOOR and `end_sample` its CLAMP. The floor is a
    // belt: the launch anchor is the publish instant, so `now` is never before
    // it and the extrapolation never runs backward, but the line can then
    // never be drawn below the frame the session was launched on whatever a
    // clock reads.
    // Main thread only, both, like the anchor. THE AUDIO THREAD READS NEITHER
    // (2026-09-01, the third review's packet): the window it renders against
    // is the one it consumed from the command packet below and keeps as its
    // own `active_end`. The mirror stays a MIRROR rather than a read of that
    // private end because the observation's clamp belongs to the PUBLISHED
    // session — the one the anchor and the floor already describe — and the
    // audio thread's end lags a publish by up to one fill (an unseated
    // session), so a pre-seat observation clamped by it would clamp against
    // the previous window; and a main-thread read of a main-thread write
    // needs no ordering argued.
    std::atomic<int64_t> start_sample{0};
    std::atomic<int64_t> end_sample{0};

    // Mutable playback state. `cursor` is the READ position: written once per
    // fill by the render body as the first source frame of the NEXT fill (one
    // whole buffer ahead of the block just rendered, and that block itself
    // one device output latency ahead of the loudspeaker). NOTHING SNAPSHOTS IT
    // DIRECTLY: every consumer, resting reads included, goes through the
    // PREDICTOR — the render player's two pauses call GuiPlayback::cursor()
    // like every other reader — so what this field does here is (a) get
    // clamped at its window's end by the natural-end fill, (b) answer a
    // resting read, and (c) supply the sample half of the suspended anchor.
    // It is NOT itself the running anchor: a resync anchors on the cycle
    // stamp below, which pairs this same position with the instant it enters
    // the port. ON A SUSPENDED DEVICE THE PREDICTOR EXTRAPOLATES NOTHING and
    // the held value is what a resting read gets — one fill (and one output
    // latency) AHEAD of the ear, the accepted shape of a pause the user could
    // hear nothing of anyway.
    std::atomic<int64_t> cursor{0};
    // Free-running cursor predictor anchor: (sample, the steady_clock ns at
    // which the main thread believes that sample is the play position), from
    // which the main thread extrapolates linearly in wall-clock time at the
    // source rate. THE ANCHOR IS A PORT-SIDE INSTANT, NOT A HEARD ONE
    // (architect 2026-09-03): the launch anchors at the PUBLISH instant and a
    // resync at the cycle stamp's port instant, neither carrying the device's
    // output latency, so the painted line runs ahead of the sound by that
    // latency less the display's own — the deliberate shape, chosen at his
    // rig against a compensated build (playback_common.cpp's record above
    // playback_publish_play). ONE STORE (`store_anchor`,
    // playback_common.cpp — every write of these two words lands there, so
    // they are always written together), and its WRITERS, all main thread, in
    // THREE CLASSES: (1) the SUSPENDED HOLD that `observe` derives and its
    // storing reader playback_cursor writes (while output_rate reads 0: the
    // held integer cursor at `now`, so a resume extrapolates from the held
    // position); (2) the RESYNC (playback_resync_predictor: the cycle stamp's
    // pair, so a resync's step is the accumulated steady_clock-vs-interface-
    // clock DRIFT alone — the period-wide phase residual the old `now`
    // anchoring re-rolled is in the stamp); (3) the two RESETS — play()'s
    // publish, (start, the publish instant) — and the zero bind and rebind
    // write, whose `ns == 0` is what the readers' "before first anchor" guard
    // answers. Never written inside the audio callback. Drift between
    // predictor and audio is bounded by time since the last resync ×
    // steady_clock vs sample-clock skew (sub-pixel at typical zoom levels for
    // typical resync intervals).
    std::atomic<int64_t> anchor_sample{0};
    std::atomic<int64_t> anchor_ns{0};

    // THE SESSION WORD — the playing flag and the session generation in ONE
    // atomic, so that the audio thread's terminal
    // write is GENERATION-QUALIFIED by construction (2026-09-01, closing the
    // play()-over-the-last-fill race): bit 0 is PLAYING, the bits above are
    // the generation, incremented by every publish. Layout at the constants
    // below; the readers ask playback_session_playing of a loaded word.
    //
    // THE FLAG (bit 0) is the one release/acquire synchronization point
    // between the two threads, as the bool it replaced was: play()'s publish
    // stores the new word — generation + 1, playing — with release as the last
    // step of its publish block, and the backend's callback loads the word
    // with acquire at its gate, so a callback that sees the bit up sees the
    // command packet that preceded it (the packet's own comment below: what
    // the fill then CONSUMES is qualified by this same generation). stop()
    // lowers the bit in one fetch_and ahead of its quiescence fence; the
    // AAudio disconnect and both shutdowns lower the flag the same way.
    //
    // THE TERMINAL (the render body's natural end) is ONE compare-exchange on
    // this word: expected = THE WORD THE GATE ACQUIRED (generation N,
    // playing — the backend's callback loads the word exactly once, gates on
    // that load, and passes the same value into playback_render_block; the
    // body re-loads nothing), desired = (generation N, not playing). It succeeds
    // only if no publish (a newer generation) and no stop (the bit already
    // down) has written the word since the gate, and a publish landing AFTER
    // a successful exchange simply overwrites it with the new generation's
    // word. So a stale fill can never lower a newer session's flag: before
    // the word, a play() published between the old fill's gate and its
    // natural-end stores let that fill store `playing = false` after the new
    // publish's `true`, stranding the new session's restart behind the
    // silence gate — the new scanner rested at its seed until the tick tore
    // it down.
    // The exchange's success order is RELEASE, pairing with every main-thread
    // acquire load of the word (is_playing, the cursor readers, the rebind
    // check) exactly as the bool's release store did: the fill's last buffer
    // reads and its stamp and cursor stores are ordered before whatever the
    // reader mutates afterwards. WHY THE GATE'S OWN WORD, and not a second
    // load inside the body (the shape until 2026-09-01's second review): a
    // publish landing between the gate and a re-load could be read by a
    // relaxed re-load WITHOUT acquiring its release, so the body rendered
    // against range and pending stores it was not ordered after and could
    // still lower the NEW generation's flag. With the gate's word as the
    // expected value both interleavings simply fail the exchange: a stop or a
    // publish after the gate changes the word, the fill abandons its terminal
    // — having rendered one block of the session it was gated into, UNDER
    // THAT SESSION'S OWN WINDOW (the packet below: a fill consumes no packet
    // but its own generation's, and the end it renders against is its own
    // private copy, which no later publish can move) — and the next callback
    // acquires and seats the new publication. A failed exchange is the audio
    // thread's ONLY reaction; it never retries and never writes the word
    // otherwise.
    //
    // THE TERMINAL ENDS THE SESSION OUTRIGHT: the fill that read past
    // `end_sample` lowers the playing bit and that is the whole of it. The
    // scanner's life is the flag's life — the run loop's tick tears it down
    // on the first read of a lowered flag — so the drawn line stops one
    // output latency BEFORE the sound does, the near end of the same lead the
    // launch has at the far end (the anchor's comment above, and the record
    // at playback_publish_play). A hold that kept the line alive until the
    // last queued frame had been heard rode bit 1 of this word for two days
    // and was rolled back with the leads on 2026-09-03.
    //
    // THE GENERATION never wraps in practice (a publish per press) and is
    // never read as a number by anyone but the publish; it exists so two
    // words that mean "playing" from different sessions can never compare
    // equal. bind resets the whole word (no callback runs there); rebind
    // leaves it alone — only a publish makes a new generation.
    std::atomic<uint64_t> session{0};

    // THE CYCLE STAMP — (stamp_cursor, stamp_ns, stamp_generation): a source
    // position, the steady_clock ns at which that position ENTERS THE OUTPUT
    // PORT, and THE GENERATION OF THE SESSION IT BELONGS TO, published by the
    // audio thread and read by the main thread under a SEQLOCK (stamp_gen: odd
    // while a write is in progress, even between writes — the seqlock's own
    // counter, not the session's generation, which is the third data word).
    // ONE WRITE SITE, in playback_render_block on the audio thread: THE END OF
    // EVERY FILL, (cursor, fill_ns + consumed × 1e9 / output_rate, the fill's
    // generation) — the read cursor with the instant its frame enters the port
    // (the frames this fill consumed lead it in the port buffer). A seat stamp
    // rode beside it while the launch anchored on the audio thread's first
    // fill (2026-09-01/02) and left with the playback leads; the launch
    // anchors at the publish instant again, so nothing waits on a seat.
    // THE GENERATION IS WHOSE STAMP THIS IS: a main thread that reads a stamp
    // whose generation equals the one in the session word it loaded is reading
    // ITS session's stamp, so "is this stamp mine" is one stamp read compared
    // against one word — which is exactly what a resync asks before it anchors
    // (playback_resync_predictor, playback_common.cpp), and its only asker.
    // THE AUDIO THREAD IS ITS ONLY WRITER WHILE A CALLBACK CAN RUN, and BIND
    // is the one other (playback_bind_and_validate, playback_common.cpp), safe
    // precisely because no callback exists there — the backend binds before it
    // opens its device — so bind stores the four words plainly, `stamp_gen`
    // back to an even value. WHY BIND RESETS THEM: it also returns the session
    // generation to 0, and init() is reusable (an idempotent shutdown() at its
    // head), so a stamp left from the previous source would carry a generation
    // the next source's FIRST publish makes again, and a resync taken before
    // that session's first fill would anchor on the OLD source's position.
    // With bind resetting, NO STAMP CARRIES A GENERATION A NEW PUBLISH COULD
    // USE — within a session's life because every publish makes a generation
    // no stamp yet carries, across init()s because the stamp starts over with
    // the word — so a stale stamp is never anchored to.
    //
    // THE SEQLOCK'S ORDERING (the classic shape, all four words atomics so a
    // torn read is impossible and the lock guards only the triple's
    // CONSISTENCY). Writer: gen = odd (relaxed), a release fence, the three
    // data stores (relaxed), gen = even (release). Reader: g1 = gen (acquire)
    // — retry while odd — the three data loads (relaxed), an acquire fence,
    // g2 = gen (relaxed), accept iff g1 == g2. If g1 read the even store, its
    // acquire pairs with that store's release and the data loads see the
    // completed triple. If g1 read an older even value and a write was in
    // flight, then either no data load saw a new store — the old triple, and
    // g1 == g2 accepts it consistently — or one did, in which case that load
    // reads-from a store sequenced after the writer's release fence, the
    // reader's acquire fence synchronizes with it, the odd gen store sequenced
    // before the fence is visible to g2, and g1 != g2 rejects the mix. The
    // writer never waits (the audio thread is never blocked by a reader), the
    // reader spins only across the writer's five stores, and nothing here
    // allocates or calls into the system.
    std::atomic<uint64_t> stamp_gen{0};
    std::atomic<int64_t>  stamp_cursor{0};
    std::atomic<int64_t>  stamp_ns{0};
    std::atomic<uint64_t> stamp_generation{0};

    // THE COMMAND PACKET — (command_start, command_end) under `command_seq`:
    // the window play() publishes for ONE generation, written by the main
    // thread and consumed by the audio thread (2026-09-01, the third review's
    // first finding — until that day the range was two independent atomics
    // and the restart a compare-exchanged `pending_start`, and a fill gated
    // on generation N could consume N+1's restart under N's end, or N's
    // restart under N+1's end, rendering a block that belonged to neither and
    // declaring a natural end at a window it was never published into).
    // `command_seq` IS THE PACKET'S GENERATION: playback_command_seq(N) =
    // N << 1 while N's packet stands complete, with the low bit set while a
    // write is in progress. The publish is the ONE writer: seq = (N << 1) | 1
    // (relaxed), a release fence, the two data stores (relaxed), seq = N << 1
    // (release) — and THEN the session word (N, playing) with release. The
    // fill is the ONE reader, and it reads ONCE, WITHOUT RETRY, and only
    // while its `active_generation` below differs from the generation its
    // gate acquired: g1 = seq (acquire); accept only if g1 ==
    // playback_command_seq(N); the two data loads (relaxed); an acquire
    // fence; g2 = seq (relaxed); accept only if g2 == g1. On acceptance the
    // fill SEATS — `active_generation = N`, `active_end = end`,
    // `fractional_cursor = start` — and on rejection it writes silence and
    // returns without rendering.
    //
    // THE ORDERING, in three lines. (1) A fill gated on N acquired the
    // session word N's publish released AFTER its packet's even store, so it
    // finds seq == N << 1 and N's pair unless a LATER publish has begun
    // overwriting them — and every later publish first sets the busy bit and
    // then, complete, a seq that is not N's. (2) A pair accepted between two
    // equal reads of N's seq is N's: a data load that read a later publish's
    // store reads-from a store sequenced after that publish's release fence,
    // the fill's acquire fence synchronizes with it, and the busy seq stored
    // before that fence is visible to g2, which then differs from g1. (3) So
    // the fill consumes only its own generation's packet and seeds its private
    // window from it; a rejected read means a newer
    // publish is under way, which the NEXT callback's gate acquires and whose
    // packet it then finds complete — the fill never spins on the main
    // thread (which is not real-time and may be preempted mid-write), and a
    // session superseded before any fill could seat it never sounds, which
    // is what superseding means.
    std::atomic<uint64_t> command_seq{0};
    std::atomic<int64_t>  command_start{0};
    std::atomic<int64_t>  command_end{0};

    // AUDIO-THREAD-PRIVATE STATE — written by the render body alone while a
    // callback can run, and by bind / rebind under their no-callback
    // conditions (bind before the device opens, rebind under stop()'s fence).
    //
    // The generation the fill has seated and the window end it renders
    // against, both taken from the packet at the seat. `active_end` is what
    // the natural-end test and the fill-end clamp read — the fill's own copy,
    // so a publish landing mid-fill cannot move the end a running block is
    // rendered against; the main thread's `end_sample` is the published
    // session's mirror (its comment) and is read by nothing here.
    uint64_t active_generation = 0;
    int64_t  active_end        = 0;
    // The fractional source cursor. Tracking the fractional position across
    // buffer boundaries is what prevents per-buffer floor() rounding from
    // compounding into audible drift between audio and visual playhead over
    // long playback. The integer `cursor` is snapshotted from this each
    // buffer for the main thread to read; the seat reseeds it from the
    // packet's start.
    double fractional_cursor = 0.0;
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
// silence if the cursor would pass the fill's window end. Writes the final
// source-cursor back to state.cursor before returning, with the cycle stamp
// beside it; on natural end, also lowers the session word's playing bit — the
// generation-qualified terminal (the field's comment), abandoned
// if a stop or a newer publish has written the word since the gate. Audio
// thread only; the backend's callback loads the session word ONCE with
// acquire, gates on its playing bit, and passes that same value as
// `session_word` — the terminal's expected value AND the generation whose
// command packet alone this fill may consume; the body loads the word again
// nowhere. See playback_write_silence for the stride. ONE CLOCK READ per fill
// (the vDSO's steady_clock, at the fill's top), one no-retry seqlock read of
// the packet while the fill's generation is unseated, one compare-exchange at
// a natural end, the stamp seqlock's stores — no lock, no allocation, no
// syscall.
void playback_render_block(GuiPlaybackState& state,
                           uint64_t session_word,
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
// publish the command packet (the window) under a fresh generation of the
// session word, and anchor the predictor at (start, the publish instant).
// Returns whether
// it published — false means the request was out of range and nothing was
// written, so the backend must not start its device. The backend checks its
// own device readiness BEFORE calling.
bool playback_publish_play(GuiPlaybackState& state, int64_t start_sample,
                           int64_t end_sample);

void   playback_resync_predictor(GuiPlaybackState& state);
bool   playback_is_playing(const GuiPlaybackState& state);

// THE PREDICTOR'S READERS ARE ONE OBSERVATION: the two below are thin faces
// over one main-thread body (`observe`, playback_common.cpp) that loads the
// session word once, reads the clock once, and forms the predicted position
// from the standing anchor — re-anchoring only where the device is suspended
// and the line must hold at the integer cursor. THE STORING READER is
// playback_cursor, which writes the anchor the observation derived (through
// store_anchor) before answering; playback_cursor_precise is the same pure
// function without the write, so it agrees with it whichever is asked first.
int64_t playback_cursor(GuiPlaybackState& state);
double playback_cursor_precise(const GuiPlaybackState& state);
int64_t playback_domain_begin(const GuiPlaybackState& state);
int64_t playback_domain_end(const GuiPlaybackState& state);

// rebind_buffer()'s whole body — no device call in it on either backend
// (playback.h's rebind contract, and the reasoning for the refuse-while-playing
// check, are at the public declaration and repeated at the definition).
void playback_rebind_buffer(GuiPlaybackState& state, const float* samples,
                            int64_t total_frames, int64_t domain_offset);
