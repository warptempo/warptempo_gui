#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "waveform_gain.h"

// Owns an audio file's sample buffer, a fixed-stride min/max peak pyramid
// (int16 cache levels on a powers-of-4 stride ladder) and the waveform
// picture's continuous gain curve. No knowledge of X11, Cairo, or progress UI.
// Synchronous loader with a progress callback that the caller wires up to its
// UI; the gain curve alone is derived after the load on a thread of its own
// (gain_curve_ready below).
class GuiAudio {
public:
    using ProgressCallback = std::function<void(float)>;

    // Number of CACHED pyramid levels (level 0, the raw samples, is not one of
    // them — see num_levels()). The strides themselves live in exactly one
    // place, audio.cpp's kStrides; this is the count both that list and the
    // storage array below are sized by, so the two cannot drift.
    static constexpr int kCacheLevels = 13;

    // Implementation detail of the peak cache. Public only so the cache
    // reader/writer free functions in audio.cpp can name the type.
    // Each PyramidLevel holds a fixed stride, the number of (min,max) pairs
    // covering the source, and per-channel flat int16 storage interleaved as
    // (min0, max0, min1, max1, ...). Quantization: clamp(v,-1,1) * 32767.
    struct PyramidLevel {
        int32_t stride     = 0;
        int64_t pair_count = 0;
        std::vector<std::vector<int16_t>> pairs;  // pairs[ch][2*p..2*p+1]
    };

    // Opens `path` via the in-tree codec library, reads all frames, and builds the pyramid.
    // Returns true on success. On failure, writes a diagnostic to stderr and
    // returns false. `on_progress` is invoked with a value in [0.0, 1.0]
    // periodically during pyramid construction; it may be empty.
    // The picture's gain curve (gain_curve below) is NOT part of the load:
    // load() starts its derivation on a thread of its own once the samples are
    // published and returns without waiting for it (gain_curve_ready below).
    bool load(const std::string& path, const ProgressCallback& on_progress);

    int64_t total_frames()    const { return total_frames_; }
    uint64_t source_load_size()  const { return load_identity_size_; }
    int64_t  source_load_mtime() const { return load_identity_mtime_; }
    int     sample_rate()     const { return sample_rate_; }
    int     channels()        const { return channels_; }

    // Raw interleaved float32 sample buffer. The pointer is valid as long as
    // this GuiAudio instance is alive and no new buffer has been installed. The
    // playback engine reads samples off the audio callback thread; the caller
    // must orchestrate lifetime so the device is stopped before the buffer
    // goes away (see GuiPlayback::shutdown).
    const float* samples_ptr() const { return samples_ ? samples_->data() : nullptr; }

    // Shared handle to the one immutable sample buffer this instance published.
    // A render dispatch copies this handle into its RenderRequest so the
    // worker's handle keeps the buffer alive until the request dies.
    // Contract: the source is loaded ONCE PER PROJECT and the pointed-to vector
    // is never mutated after publish. A GuiAudio is built and torn down inside
    // `run_project` (main.cpp), so an Open Project reopen arrives at a NEW
    // GuiAudio over a new source rather than swapping this buffer under a
    // reader; inside one project nothing loads a source at all (`'` load in
    // place replaces the marker stores and the engine block, never the
    // samples).
    std::shared_ptr<const std::vector<float>> samples_shared() const;

    // Total number of pyramid levels, counting level 0 (raw samples).
    int num_levels() const;

    // The level to read for a column covering `span_samples` SOURCE samples:
    // the coarsest cached level whose stride still fits inside the span, else 0
    // (raw) when the span is finer than the finest stride. Result is always a
    // valid argument for get_peak_range — clamped into [0, num_levels()-1].
    //
    // This is the ONE level-choosing owner, so no caller needs to know the
    // stride ladder. Choosing from the column's own mapped SOURCE span (rather
    // than a viewport-wide estimate) is what bounds the per-column work in
    // target view, where the local source/target slope can reach 16x and a
    // global estimate therefore understates a compressed column's true span by
    // the same factor. Span is a double so a caller can pass the exact
    // fractional mapped width rather than a rounded one.
    //
    // THE RESULTING PER-COLUMN READ BOUND, per channel — UNCONDITIONAL, in
    // BOTH views, for every input the loader accepts:
    //   - a cached level reads AT MOST 5 pairs. The level is chosen so
    //     stride <= span < kReductionFactor*stride, and get_peak_range expands
    //     to whole bins, so it touches ceil(span/stride)+1 <= 5 of them.
    //   - raw (level 0) reads AT MOST 16 samples — one more than the finest
    //     stride, NOT one less. Raw is selected for a span strictly below 16,
    //     but the caller rounds the two endpoints INDEPENDENTLY, which can
    //     widen a sub-16 float span to a 16-sample integer range (0.49 -> 16.48
    //     is a width of 15.99 that rounds to [0, 16)).
    //
    // It holds in TARGET view too, with no scoping or tempo caveat, because
    // get_peak_range clamps end_sample to total_frames_: no column can read
    // past the source however long tempo makes the target timeline or however
    // sharply the local slope concentrates. The source is RIFF-bounded, and the
    // ladder's top rung is sized so that four times it exceeds that bound — see
    // the reach derivation at kStrides for the arithmetic.
    int level_for_span(double span_samples) const;

    // Returns (min, max) over source-sample indices [start_sample, end_sample)
    // on `channel`, read at pyramid `level`. Level 0 is raw samples; levels
    // 1..kCacheLevels select cached min/max pairs on the powers-of-4 stride
    // ladder (see kStrides in audio.cpp). Levels above the deepest cached level
    // clamp to it. Inputs are clamped; an empty range returns (0, 0).
    std::pair<float,float> get_peak_range(int channel,
                                          int level,
                                          int64_t start_sample,
                                          int64_t end_sample) const;

    // THE PICTURE'S CONTINUOUS GAIN over source frames (derive_waveform_gain,
    // waveform_gain.h, which owns the rule), DERIVED OFF THE LOAD PATH
    // (architect 2026-09-24): load() publishes the samples and then starts
    // ONE PLAIN std::thread that holds its own reference to them, derives the
    // curve, prints its own stderr line (`gain_derive=... ms`, which the
    // tablet routes to logcat with every other stderr line) and sets the
    // ready flag. Nothing needs the curve at load — the magnification lamp is
    // dark at every open — so the load does not wait out the ~115 ms the
    // derivation costs on the tablet.
    //
    // THE READY FLAG's one reader outside this class is
    // waveform_magnification_toggle_actionable (warp_frame_map_view.h): the
    // backtick and the lamp's face refuse and
    // grey until it stands, so the lamp cannot be lit before the curve exists.
    // gain_curve() is readable only once gain_curve_ready() has returned true
    // (asserted). Immutable from then on, like the pyramid — so the waveform
    // worker reads it through its job's audio pointer with no owned snapshot.
    // Whether a plate applies it is the gate's (waveform_magnified,
    // warp_frame_map_view.h). Pixels only: no sample and no render input
    // reads it.
    bool gain_curve_ready() const {
        return gain_ && gain_->ready.load(std::memory_order_acquire);
    }
    const WaveformGainCurve& gain_curve() const;

private:
    std::shared_ptr<const std::vector<float>> samples_;
    int64_t            total_frames_    = 0;
    int                sample_rate_     = 0;
    int                channels_        = 0;
    int                render_channels_ = 0;

    // Size and mtime of the source file at the moment its samples were
    // decoded into this buffer, captured through the same stat_file_identity
    // used by the render fingerprint. The render pipeline records these
    // directly as a wav render's fingerprint source identity; the source is
    // immutable for the process lifetime, so there is no on-disk re-stat.
    // load() refuses a source it cannot stat, so both fields are valid
    // whenever a source is loaded.
    // THE STAT PRECEDES THE DECODE, and there is a hair of a window in it
    // (recorded 2026-09-02): if the source file were REPLACED between the stat
    // and wav_read_full's own open, this identity would name the old file
    // while the buffer held the new samples, and a render's fingerprint would
    // then attest the wrong pair. It is startup-only — the identity is
    // captured once per project open and the source is immutable for the rest
    // of the session — and it needs the architect to publish over his own
    // source in that instant, so it is recorded rather than closed with a
    // post-decode re-stat.
    uint64_t load_identity_size_ = 0;
    int64_t  load_identity_mtime_ = 0;

    // The fixed-stride cache levels (powers-of-4 ladder, see kStrides in
    // audio.cpp). Populated either from the on-disk `<basename>.peaks` sidecar
    // or by streaming over the freshly built sample buffer on cache miss.
    std::array<PyramidLevel, kCacheLevels> levels_;

    // THE DERIVATION'S STATE, on the heap so that its address is stable
    // across GuiAudio's moves (the loader builds a GuiAudio and move-assigns
    // it into run_project's, file_loader.cpp): the thread writes `curve` and
    // then `ready`, and touches nothing else but its own copy of the samples'
    // shared_ptr. No cancel, no completion signal, no platform hook, by
    // ruling. THE DESTRUCTOR JOINS the thread before `curve` dies, and it is
    // the only join: it runs when the owning GuiAudio is destroyed (the end of
    // run_project), when a move-assignment replaces the owner's pointer, and
    // when a later load() on the same object replaces it at publish (none
    // does: the loader loads a fresh GuiAudio and moves it in) — so a
    // close waits out at most the rest of the derivation, by design, and
    // nothing the thread touches can die under it.
    //
    // THE HAPPENS-BEFORE CHAIN to every reader of `curve`: the thread writes
    // `curve`, then ready.store(release); a reader's gain_curve_ready()
    // (acquire) that sees true sees the whole curve. The GUI thread's one such
    // read is the lamp's refusal (waveform_magnification_toggle_actionable),
    // which must pass before show_waveform_magnification can be lit; every
    // plate that applies the curve does so only while that lamp is lit
    // (waveform_magnified), on the GUI thread after it, or on the waveform
    // worker through a job the GUI thread submitted after it — the job
    // hand-off being the worker's own synchronization.
    struct GainDerivation {
        WaveformGainCurve curve;
        std::atomic<bool> ready{false};
        std::thread       thread;
        GainDerivation() = default;
        GainDerivation(const GainDerivation&) = delete;
        GainDerivation& operator=(const GainDerivation&) = delete;
        ~GainDerivation() {
            if (thread.joinable()) thread.join();
        }
    };
    std::unique_ptr<GainDerivation> gain_;
};
