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
//     device-shaped (JACK counts process cycles, AAudio waits on the stream
//     state machine; each states its choice at its own site).
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

    // Mutable playback state.
    std::atomic<int64_t> cursor{0};
    // Free-running cursor predictor anchor. The main thread extrapolates
    // linearly from (anchor_sample, anchor_ns) using wall-clock time.
    // Re-anchored at events of acceptable visible discontinuity (play(),
    // playhead jumps, viewport reflows, follow-mode on),
    // and continuously by cursor() while the device is suspended (output_rate
    // reads 0) so the playhead holds and resume extrapolates from the held
    // position. Main-thread-only; never inside the audio callback. Drift
    // between predictor and audio is bounded by time since last resync ×
    // steady_clock vs sample-clock skew (sub-pixel at typical zoom levels
    // for typical resync intervals).
    std::atomic<int64_t> anchor_sample{0};
    std::atomic<int64_t> anchor_ns{0};
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
// back to state.cursor before returning; on natural end, also clears
// state.playing. Audio thread only; the backend's callback calls this after
// its own `playing` gate. See playback_write_silence for the stride.
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
int64_t playback_cursor(GuiPlaybackState& state);
double playback_cursor_precise(const GuiPlaybackState& state);
int64_t playback_domain_begin(const GuiPlaybackState& state);
int64_t playback_domain_end(const GuiPlaybackState& state);

// rebind_buffer()'s whole body — no device call in it on either backend
// (playback.h's rebind contract, and the reasoning for the refuse-while-playing
// check, are at the public declaration and repeated at the definition).
void playback_rebind_buffer(GuiPlaybackState& state, const float* samples,
                            int64_t total_frames, int64_t domain_offset);
