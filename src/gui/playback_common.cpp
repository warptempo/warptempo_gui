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
// see "eventually" (cursor, speed). The exception is the `playing` flag:
// playback_publish_play stores it with release ordering as the last step of its
// publish block, and the backend's callback loads it with acquire ordering at
// its gate, so a callback that sees playing == true is guaranteed to see the
// range and pending-start stores that preceded the release. The sample buffer
// is read-only from the audio thread's point of view, and its address lives
// across the device's entire life.
//
// Speed changes are applied at buffer granularity. A set_speed() call
// between callback invocations is picked up on the next fill; the speed
// stays constant within one fill, avoiding mid-buffer rate artefacts.

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
                           float* const* channel_buffers,
                           int64_t stride,
                           int64_t frame_count,
                           int channel_count) {
    const int    src_channels = state.channels;
    const double speed        = static_cast<double>(
                                    state.speed_x1000.load(std::memory_order_relaxed))
                                / 1000.0;
    const uint32_t output_rate = state.output_rate.load(std::memory_order_relaxed);
    // The device asks for output-rate frames; fold the output-to-source rate
    // ratio into the existing fractional source read increment.
    const double increment = output_rate == 0
        ? 0.0
        : speed * static_cast<double>(state.source_rate) / static_cast<double>(output_rate);
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

    // Pick up any pending restart position published by play(). Clearing it
    // atomically lets us idempotently absorb the latest restart without a
    // lock. See the state doc on pending_start.
    const int64_t pending =
        state.pending_start.exchange(-1, std::memory_order_relaxed);
    if (pending >= 0) {
        state.fractional_cursor = static_cast<double>(pending);
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
    state.cursor.store(new_cur, std::memory_order_relaxed);
    if (natural_end) {
        state.playing.store(false, std::memory_order_release);
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
    state.end_sample.store(0, std::memory_order_relaxed);
    state.speed_x1000.store(1000, std::memory_order_relaxed);
    state.playing.store(false, std::memory_order_relaxed);
    state.pending_start.store(-1, std::memory_order_relaxed);
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
    state.cursor.store(start_sample, std::memory_order_relaxed);
    state.end_sample.store(end_sample, std::memory_order_relaxed);
    state.pending_start.store(start_sample, std::memory_order_relaxed);

    // Anchor the predictor to start_sample directly: the audio thread may
    // not yet have processed pending_start, so the cursor atomic still
    // reflects the previous session. The anchor pair is written and read only
    // on the main thread, and each 8-byte aligned load is atomic on the target,
    // so there is no torn read; the predictor tolerates only bounded staleness
    // (the anchor lagging real playback between resyncs), which self-corrects
    // at the next resync.
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    state.anchor_sample.store(start_sample, std::memory_order_relaxed);
    state.anchor_ns.store(now_ns, std::memory_order_relaxed);
    state.playing.store(true, std::memory_order_release);
    return true;
}

void playback_resync_predictor(GuiPlaybackState& state) {
    const int64_t cur = state.cursor.load(std::memory_order_relaxed);
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    state.anchor_sample.store(cur, std::memory_order_relaxed);
    state.anchor_ns.store(now_ns, std::memory_order_relaxed);
}

void playback_set_speed(GuiPlaybackState& state, float speed) {
    if (speed < 0.10f) speed = 0.10f;
    if (speed > 1.00f) speed = 1.00f;
    // x1000 in DOUBLE, rounded with std::nearbyint — the project's rounding
    // rule. NOT an audio change: the speed vocabulary is the tenths presets
    // (kPlaybackSpeedPresets, 0.1 .. 1.0), whose x1000 products all lie within
    // a float ulp of an integer and nowhere near a tie, so the stored word is
    // identical to what the old float `std::lround(speed * 1000.0f)` produced.
    const int32_t v = static_cast<int32_t>(
        std::nearbyint(static_cast<double>(speed) * 1000.0));
    state.speed_x1000.store(v, std::memory_order_relaxed);
}

bool playback_is_playing(const GuiPlaybackState& state) {
    // ACQUIRE, not relaxed: this load pairs with the audio thread's release
    // store of playing = false at the natural end (playback_render_block), which
    // the callback makes after its last read of the borrowed sample buffer. The
    // conditional-stop sites (target_render.cpp's ensure_ready and
    // rebind_to_source) skip stop()'s quiescence fence on a false read, so
    // the acquire is what orders that final callback's buffer reads before
    // anything the caller mutates afterwards. Free on the target — x86 loads
    // already carry acquire ordering; the tightening is formal.
    return state.playing.load(std::memory_order_acquire);
}

int64_t playback_cursor(GuiPlaybackState& state) {
    // Every internal value below (integer cursor, predictor anchor, end/total
    // clamps) is buffer-local; the bound buffer's domain offset is added once
    // at each return, so the reported position is a domain coordinate
    // (playback.h head comment).
    const int64_t off = state.domain_offset;
    if (!state.playing.load(std::memory_order_relaxed)) {
        return state.cursor.load(std::memory_order_relaxed) + off;
    }
    // Device suspended: the audio thread is holding position, so the playhead
    // holds honestly at the integer cursor. Re-anchoring continuously through
    // the outage makes resume extrapolate from the held position and wall-clock
    // now, with no forward jump or snap-back in either direction.
    if (state.output_rate.load(std::memory_order_relaxed) == 0) {
        const int64_t cur = state.cursor.load(std::memory_order_relaxed);
        const int64_t now_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        state.anchor_sample.store(cur, std::memory_order_relaxed);
        state.anchor_ns.store(now_ns, std::memory_order_relaxed);
        return cur + off;
    }
    const int64_t a_sample = state.anchor_sample.load(std::memory_order_relaxed);
    const int64_t a_ns     = state.anchor_ns.load(std::memory_order_relaxed);
    if (a_ns == 0) return a_sample + off;  // before first anchor
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const int64_t elapsed_ns = now_ns - a_ns;
    if (elapsed_ns <= 0) return a_sample + off;
    const double speed = static_cast<double>(
        state.speed_x1000.load(std::memory_order_relaxed)) / 1000.0;
    const int64_t sr = static_cast<int64_t>(state.source_rate);
    const double advance_samples =
        static_cast<double>(elapsed_ns) * 1e-9 * speed * static_cast<double>(sr);
    int64_t predicted = a_sample + static_cast<int64_t>(advance_samples);
    const int64_t end = state.end_sample.load(std::memory_order_relaxed);
    if (predicted > end) predicted = end;
    if (predicted > state.total_frames) predicted = state.total_frames;
    return predicted + off;
}

double playback_cursor_precise(const GuiPlaybackState& state) {
    // Mirrors playback_cursor branch-for-branch but returns the pre-truncation
    // double and applies the same end/total clamps as doubles, so it agrees with
    // cursor() exactly at the clamped window end and is floor()-consistent with
    // it elsewhere (advance_samples >= 0 while playing forward). The domain
    // offset is added once at each return, matching cursor()'s domain.
    const double off = static_cast<double>(state.domain_offset);
    if (!state.playing.load(std::memory_order_relaxed)) {
        return static_cast<double>(
                   state.cursor.load(std::memory_order_relaxed)) + off;
    }
    // Device suspended: hold at the integer cursor exactly as cursor() reports.
    // No re-anchor here — this is a side-effect-free reader; cursor(), called
    // alongside it on the main thread, owns the continuous suspended re-anchor.
    if (state.output_rate.load(std::memory_order_relaxed) == 0) {
        return static_cast<double>(
                   state.cursor.load(std::memory_order_relaxed)) + off;
    }
    const int64_t a_sample = state.anchor_sample.load(std::memory_order_relaxed);
    const int64_t a_ns     = state.anchor_ns.load(std::memory_order_relaxed);
    if (a_ns == 0) return static_cast<double>(a_sample) + off;
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const int64_t elapsed_ns = now_ns - a_ns;
    if (elapsed_ns <= 0) return static_cast<double>(a_sample) + off;
    const double speed = static_cast<double>(
        state.speed_x1000.load(std::memory_order_relaxed)) / 1000.0;
    const int64_t sr = static_cast<int64_t>(state.source_rate);
    const double advance_samples =
        static_cast<double>(elapsed_ns) * 1e-9 * speed * static_cast<double>(sr);
    double predicted = static_cast<double>(a_sample) + advance_samples;
    const double end = static_cast<double>(
        state.end_sample.load(std::memory_order_relaxed));
    if (predicted > end) predicted = end;
    const double total = static_cast<double>(state.total_frames);
    if (predicted > total) predicted = total;
    return predicted + off;
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
    // natural-end release store is what orders that last callback's buffer
    // reads before the assignments. Free on the target; the tightening is
    // formal.
    if (state.playing.load(std::memory_order_acquire)) {
        std::fprintf(stderr,
            "warptempo_gui: rebind_buffer called while playing — refusing "
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
    state.anchor_sample.store(0, std::memory_order_relaxed);
    state.anchor_ns.store(0, std::memory_order_relaxed);
}
