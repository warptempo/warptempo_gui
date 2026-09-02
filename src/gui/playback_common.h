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
// The generation a word carries — what the command packet's sequence word and
// the cycle stamp are tagged with (the fields below).
constexpr uint64_t playback_session_generation(uint64_t word) {
    return word >> kSessionGenShift;
}
// A new session's word: the next generation, playing, not ended.
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
    // `start_sample` is its FLOOR (an anchor stamped at a heard instant still
    // in the future extrapolates BACKWARD for a `now` ahead of it, and this is
    // where that extrapolation stops, so the line rests on the launch frame
    // until the first sound is actually heard) and `end_sample` its CLAMP.
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
    // `output_latency_frames` ahead of the loudspeaker). It is what the
    // resting readers snapshot — the render player's pause reads its resume
    // point here, the natural-end fill clamps it at its window's end — and it
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
    // frame until the first sound and never steps back. ONE STORE
    // (`store_anchor`, playback_common.cpp — every write of these three words
    // lands there, so they are always written together), and its WRITERS,
    // all main thread, in THREE CLASSES: (1) the FOUR forms `observe` DERIVES
    // and its storing readers — playback_cursor, playback_natural_end_holding
    // and playback_snapshot (the declarations below) — STORE, so the hold's
    // verdict never stands on an anchor of another epoch: the launch latch
    // (the first read after the session is seated: the stamp's sample at the
    // stamp's port instant plus the heard offset), the LATENCY EPOCH
    // re-anchor (the advancing arm, the same stamp form again, the moment the
    // live heard offset differs from `anchor_offset_ns` below), the TERMINAL
    // re-anchor (the first read that sees the ended bit: the terminal stamp
    // itself — (end, the instant the last sound is heard) — so the line's
    // last stretch runs on the stamp the hold's deadline reads and reaches
    // `end` exactly as the hold ends; the third review's second finding) and
    // the suspended hold (while output_rate reads 0: the held integer cursor
    // at `now`, so a resume extrapolates from the held position); (2) the
    // RESYNC (playback_resync_predictor: the stamp-plus-offset form, stored
    // there, so a resync's step is the accumulated steady_clock-vs-interface-
    // clock DRIFT alone — the period-wide phase residual and the latency
    // figure are both in the stamp); (3) the two RESETS — play()'s publish,
    // (start, 0) — "await the seat", the latch filling the instant in exactly
    // once per session (the monotonicity argument is at that arm) — and the
    // zero bind and rebind write. Never written inside the audio callback.
    // Drift between predictor and audio is bounded by time since the last
    // resync × steady_clock vs sample-clock skew (sub-pixel at typical zoom
    // levels for typical resync intervals).
    std::atomic<int64_t> anchor_sample{0};
    std::atomic<int64_t> anchor_ns{0};
    // THE ANCHOR'S EPOCH: the heard offset (ns) the standing anchor was built
    // with — the third word of the anchor, written beside the other two by
    // every writer and main thread only like them. The device's latency figure
    // is ONE FIGURE PER EPOCH, not a session constant: a quantum change moves
    // it mid-session (the buffer-size callback re-reads it), and an anchor
    // built with the old figure would then be extrapolated in the new epoch —
    // the natural-end deadline, which reads the live figure, ending the hold
    // short of (or past) the end the anchor reaches. So the one observation
    // compares the live offset to this word on every advancing read and, when
    // they differ, re-anchors from the latest cycle stamp exactly as a resync
    // does — one step of the latency delta, in the frame the figure changed —
    // BEFORE it forms the hold's verdict from that same offset (the readers'
    // block below). Unseated sessions need nothing: the latch reads the live
    // figure.
    std::atomic<int64_t> anchor_offset_ns{0};

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
    // command packet that preceded it (the packet's own comment below: what
    // the fill then CONSUMES is qualified by this same generation). stop()
    // lowers both bits in one fetch_and ahead of its quiescence fence; the
    // AAudio disconnect and both shutdowns lower the flag the same way.
    //
    // THE TERMINAL (the render body's natural end) is ONE compare-exchange on
    // this word: expected = THE WORD THE GATE ACQUIRED (generation N,
    // playing — the backend's callback loads the word exactly once, gates on
    // that load, and passes the same value into playback_render_block; the
    // body re-loads nothing), desired = (generation N, ended). It succeeds
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
    // stop landing between the gate and a re-load let the body capture (N,
    // no bits) and exchange it to (N, ended) — the hold resurrected behind an
    // explicit stop — and a publish landing there could be read by a relaxed
    // re-load WITHOUT acquiring its release, so the body rendered against
    // range and pending stores it was not ordered after and could still
    // exchange the NEW generation to ended. With the gate's word as the
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
    // THE NATURAL-END HOLD (bit 1, architect 2026-09-01) is set by that same
    // exchange — the fill that read past `end_sample` — and read by
    // the one observation's not-playing arm, which keeps extrapolating from the
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
    // once the flag is down: its exchange expects the word its gate acquired,
    // playing bit up, and the fetch_and changed it), and bind / rebind (a new
    // buffer). The audio thread never clears it: a session that ends is over
    // for the callback the moment the flag drops, and the hold is the main
    // thread's picture of the sound still in the device's queue.
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
    // is consumed by the one observation's epoch check against
    // `anchor_offset_ns` (that field's comment), so no anchor is ever
    // extrapolated with a figure other than the one it was built with.
    // AAUDIO NEVER WRITES IT — the recorded asymmetry (the record is at the
    // AAudio Impl): the tablet's route is Bluetooth in the car, whose latency
    // is large, variable and unreported to the stream, so no figure the
    // framework could report is worth anchoring to, and the tablet's predictor
    // keeps the uncompensated lead (architect 2026-09-01). A device fact, not
    // the binding's: it survives rebinds and is zeroed where the backend zeroes
    // output_rate (clear_after_failed_init, shutdown).
    std::atomic<int64_t> output_latency_frames{0};

    // THE DISPLAY LEAD IN NANOSECONDS (architect 2026-09-02): how far AHEAD of
    // `now` the predictor's POSITION is read, so that the playhead the paint
    // draws from that position lands where the sound will be when the pixel
    // turns into light — the display's own latency, the twin of the output
    // latency above on the far side of the paint. The platform owns the
    // figure (GuiPlatform::display_lead_ns: the Wayland backend measures it
    // per frame through the compositor's presentation feedback, the Android
    // backend answers 0 by ruling), and main.cpp's pre-paint hook writes it
    // here once per painted frame through the setter below, ahead of every
    // predictor read that frame makes. IT REACHES THE POSITION ALONE: the
    // one read site is `observe`'s predict_position call (playback_common.
    // cpp), and the natural-end hold's deadline beside it keeps the bare
    // `now` — a lead on the shared clock would end the hold early by the
    // lead, the sound still in the device's queue. AND IT REACHES ONLY THE
    // FACES THAT PAINT (2026-09-02): that one read site takes `observe`'s
    // `apply_display_lead` fork, which playback_heard_cursor passes false —
    // a RESTING WRITE (the render player's pause point) must record where
    // the ear was, not where the next pixel will be, so it reads the same
    // line at `now`. Main thread only, written
    // and read there, like the anchor; not a device fact of the audio engine,
    // so bind and rebind leave it standing.
    int64_t display_lead_ns = 0;

    // THE CYCLE STAMP — (stamp_cursor, stamp_ns, stamp_generation): a source
    // position, the steady_clock ns at which that position ENTERS THE OUTPUT
    // PORT, and THE GENERATION OF THE SESSION IT BELONGS TO, published by the
    // audio thread and read by the main thread under a SEQLOCK (stamp_gen: odd
    // while a write is in progress, even between writes — the seqlock's own
    // counter, not the session's generation, which is the third data word).
    // TWO WRITE SITES, both in playback_render_block and both on the audio
    // thread, one writer: the SEAT — the fill that consumes the session's
    // command packet (below) stamps (start, fill_ns, that generation), the
    // instant that fill began — and THE END OF EVERY FILL, (cursor, fill_ns +
    // consumed × 1e9 / output_rate, the fill's generation), the read cursor
    // with the instant its frame enters the port (the frames this fill
    // consumed lead it in the port buffer; on the natural-end fill `consumed`
    // is the count read before the window's end, so the stamp names the
    // instant the last sound enters the port exactly). THE GENERATION IS THE
    // SEAT'S PUBLICATION (2026-09-01, the third review — it was
    // `pending_start`'s -1 sentinel for a day): a main thread that reads a
    // stamp whose generation equals the one in the session word it loaded is
    // reading ITS session's stamp, seat or later, so "has the audio thread
    // seated the published session" is one stamp read compared against one
    // word — the predictor's launch latch and every resync ask exactly that
    // before they anchor (`observe` and playback_resync_predictor,
    // playback_common.cpp). The predictor's anchor writers read it and add
    // the heard offset; the hold reads it for its deadline. THE AUDIO THREAD
    // IS ITS ONLY WRITER WHILE A CALLBACK CAN RUN, and BIND is the one other
    // (playback_bind_and_validate, playback_common.cpp), safe precisely
    // because no callback exists there — the backend binds before it opens its
    // device — so bind stores the four words plainly, `stamp_gen` back to an
    // even value. WHY BIND RESETS THEM: it also returns the session generation
    // to 0, and init() is reusable (an idempotent shutdown() at its head), so a
    // stamp left from the previous source would carry a generation the next
    // source's FIRST publish makes again, and the launch latch would accept it
    // as that publish's seat. With bind resetting, NO STAMP CARRIES A
    // GENERATION A NEW PUBLISH COULD USE — within a session's life because
    // every publish makes a generation no stamp yet carries, across init()s
    // because the stamp starts over with the word — so a stale stamp is never
    // anchored to.
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
    // `fractional_cursor = start`, the seat stamp (start, fill_ns, N) —
    // and on rejection it writes silence and returns without a stamp.
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
    // the fill consumes only its own generation's packet, seeds its private
    // window from it, and stamps it with N; a rejected read means a newer
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
// beside it; on natural end, also swaps the session word from playing to
// ended — the generation-qualified terminal (the field's comment), abandoned
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
// and publish the command packet (the window) under a fresh generation of the
// session word, with the predictor anchor awaiting its seat. Returns whether
// it published — false means the request was out of range and nothing was
// written, so the backend must not start its device. The backend checks its
// own device readiness BEFORE calling.
bool playback_publish_play(GuiPlaybackState& state, int64_t start_sample,
                           int64_t end_sample);

void   playback_resync_predictor(GuiPlaybackState& state);
bool   playback_is_playing(const GuiPlaybackState& state);

// set_display_lead_ns's whole body: the field's contract is at
// GuiPlaybackState::display_lead_ns. Main thread only.
void   playback_set_display_lead_ns(GuiPlaybackState& state, int64_t lead_ns);

// THE PREDICTOR'S READERS ARE ONE OBSERVATION (2026-09-01, the epoch
// reconciled ahead of the hold): the four below are thin faces over one
// main-thread body (`observe`, playback_common.cpp) that loads the session
// word once, reads the clock once and the live heard offset once, re-anchors
// FIRST where the standing anchor's epoch differs from that offset, where
// the launch latch or the suspended hold is due, or — the ended bit's first
// read — onto the TERMINAL STAMP itself, and only then forms both the
// predicted position and the natural-end hold's verdict from that one
// stamp/offset pair: through the hold the anchor IS the stamp the deadline
// reads, so the line reaches the window's end exactly as the hold ends. THE
// STORING READERS are playback_cursor, playback_heard_cursor,
// playback_natural_end_holding and playback_snapshot — each writes the anchor
// the observation derived (through store_anchor) before answering;
// playback_cursor_precise is the same pure function without the write, so it
// agrees with them whichever is asked first.
//
// THE OBSERVATION'S ONE PARAMETER is `apply_display_lead`, and it forks the
// POSITION READ alone (`observe`'s own comment, playback_common.cpp): a face
// whose answer becomes PIXELS asks for the lead, because the pixel lights
// after the read; a face whose answer becomes a RESTING WRITE asks without
// it, because a stored position must say where the EAR was. That is the
// whole difference between playback_cursor and playback_heard_cursor.
//
// The natural-end hold's whole test (the session word's ended bit): the
// session ended at its window's end and the last sound it queued has not yet
// left the loudspeaker. False while playing, false after any stop.
bool   playback_natural_end_holding(GuiPlaybackState& state);
int64_t playback_cursor(GuiPlaybackState& state);
// heard_cursor()'s whole body: playback_cursor with the display lead off.
// The contract is at GuiPlayback::heard_cursor (playback.h).
int64_t playback_heard_cursor(GuiPlaybackState& state);
double playback_cursor_precise(const GuiPlaybackState& state);
// The tick's read: the playing bit and the hold's verdict from ONE
// observation (GuiPlaybackSnapshot, playback.h), stored like playback_cursor,
// so a terminal decision taken on the verdict has the reconciled anchor —
// the terminal stamp's, through the hold — under the paint that follows.
GuiPlaybackSnapshot playback_snapshot(GuiPlaybackState& state);
int64_t playback_domain_begin(const GuiPlaybackState& state);
int64_t playback_domain_end(const GuiPlaybackState& state);

// rebind_buffer()'s whole body — no device call in it on either backend
// (playback.h's rebind contract, and the reasoning for the refuse-while-playing
// check, are at the public declaration and repeated at the definition).
void playback_rebind_buffer(GuiPlaybackState& state, const float* samples,
                            int64_t total_frames, int64_t domain_offset);
