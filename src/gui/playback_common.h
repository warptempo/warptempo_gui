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
    // point here, the natural-end compare clamps it at `end_sample` — and it
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
    // frame until the first sound and never steps back. THREE WRITERS, all
    // main thread, one form: the launch latch (playback_cursor's first read
    // after the session is seated: the stamp's sample at the stamp's port
    // instant plus the heard offset), every resync (playback_resync_predictor:
    // the same stamp-plus-offset form, so a resync's step is the accumulated
    // steady_clock-vs-interface-clock DRIFT alone — the period-wide phase
    // residual and the latency constant are both in the stamp) and the
    // suspended hold (playback_cursor while output_rate reads 0: the held
    // integer cursor at `now`, so a resume extrapolates from the held
    // position). play() publishes (start, 0) — "await the seat" — and the latch
    // fills the instant in exactly once per session (the monotonicity argument
    // is at that arm). Never written inside the audio callback. Drift between
    // predictor and audio is bounded by time since the last resync ×
    // steady_clock vs sample-clock skew (sub-pixel at typical zoom levels for
    // typical resync intervals).
    std::atomic<int64_t> anchor_sample{0};
    std::atomic<int64_t> anchor_ns{0};
    // The session's start, buffer-local — play()'s own, written beside the
    // anchor and read by the predictor as its FLOOR: an anchor stamped at a
    // heard instant still in the future extrapolates BACKWARD for a `now`
    // ahead of it, and this is where that extrapolation stops, so the line
    // rests on the launch frame until the first sound is actually heard.
    // Main thread only, like the anchor.
    std::atomic<int64_t> start_sample{0};
    std::atomic<bool>    playing{false};

    // THE NATURAL-END HOLD (architect 2026-09-01): set by the render body
    // beside its `playing = false` at the natural end — the fill that read
    // past `end_sample` — and read by playback_cursor's not-playing arm, which
    // keeps extrapolating from the anchor (clamped at `end_sample`) while it
    // stands, and by playback_natural_end_holding, which answers true until
    // the last frame that fill consumed has been HEARD (the stamp's port
    // instant plus the heard offset). So the drawn playhead outlives `playing`
    // by exactly the output latency and vanishes when the SOUND ends, not
    // `latency + (0, period]` early. `playing` itself is untouched by this bit:
    // is_playing() still drops at the flag, so the fence-before-rebind ordering
    // every conditional stop rests on is unchanged. THREE CLEARERS, all main
    // thread: play()'s publish (a new session), the backends' stop() after
    // their quiescence fence (the one stop body's road, and every other
    // stop's), and bind / rebind (a new buffer). The audio thread never clears
    // it: a session that ends is over for the callback the moment `playing`
    // drops, and the hold is the main thread's picture of the sound still in
    // the device's queue.
    std::atomic<bool>    ended_naturally{false};

    // THE DEVICE'S OUTPUT LATENCY IN OUTPUT FRAMES: the time between a frame
    // entering our output port and its leaving the loudspeaker, as the backend
    // reports it. Written by the backend from its own notification threads
    // (JACK: the latency and buffer-size callbacks, and one read after the
    // auto-connect — playback.cpp's refresh_output_latency, which also prints
    // the figure to stderr whenever it changes) and read by the predictor's
    // anchor writers on the main thread as `heard_offset_ns` (playback_common.
    // cpp): `output_latency_frames × 1e9 / output_rate`, 0 while suspended.
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
    // (pending_start == -1) or on `ended_naturally`, each of which a publish,
    // a bind or a rebind puts back, so a stale stamp is never anchored to.
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
// natural end, also sets state.ended_naturally and clears state.playing.
// Audio thread only; the backend's callback calls this after its own
// `playing` gate. See playback_write_silence for the stride. ONE CLOCK READ
// per fill (the vDSO's steady_clock, at the fill's top), one compare-exchange
// when a pending restart is absorbed, the seqlock's stores — no lock, no
// allocation, no syscall.
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
// The natural-end hold's whole test (the `ended_naturally` field): the
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
