#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// THE FOUR DISPLAY LANES of the waveform: the mono sum of the stereo source
// and its three frequency bands. The lane is the display's axis where the
// source's channel used to be — the plate never paints a channel again. The
// bands are DISPLAY-ONLY: they never reach JACK, the render pipeline or the
// disk as audio; playback and every RenderRequest read the untouched stereo
// buffer (samples_ptr / samples_shared). The band split is described at
// build_lane_signals in audio.cpp.
enum class GuiWaveformLane { Sum = 0, Low = 1, Mid = 2, High = 3 };

// Owns an audio file's sample buffer, the four display-lane signals (int16 mono
// sum and its low / mid / high bands, full length) and one fixed-stride min/max
// peak pyramid per lane (int16 cache levels on a powers-of-4 stride ladder,
// identical frame counts and strides across the four lanes). No knowledge of
// X11, Cairo, or progress UI. Synchronous loader with a progress callback that
// the caller wires up to its UI.
class GuiAudio {
public:
    using ProgressCallback = std::function<void(float)>;

    // Number of CACHED pyramid levels (level 0, the raw lane signal, is not one
    // of them — see num_levels()). The strides themselves live in exactly one
    // place, audio.cpp's kStrides; this is the count both that list and the
    // storage array below are sized by, so the two cannot drift.
    static constexpr int kCacheLevels = 13;

    // The lane count, the extent of every per-lane array below; the enum's
    // values are its indices.
    static constexpr int kLaneCount = 4;

    // Implementation detail of the peak cache. Public only so the cache
    // reader/writer free functions in audio.cpp can name the type.
    // Each PyramidLevel holds a fixed stride, the number of (min,max) pairs
    // covering the source — ONE count for all four lanes, since every lane
    // signal is exactly total_frames long — and per-lane flat int16 storage
    // interleaved as (min0, max0, min1, max1, ...), indexed by the lane's enum
    // value. Quantization: int16(nearbyint(clamp(v,-1,1) * 32767)), the one
    // quantizer at audio.cpp's quantize_unit.
    struct PyramidLevel {
        int32_t stride     = 0;
        int64_t pair_count = 0;
        std::vector<std::vector<int16_t>> pairs;  // pairs[lane][2*p..2*p+1]
    };

    // Opens `path` via the in-tree codec library, reads all frames, forms the
    // four lane signals (the mono sum and the two zero-phase lowpass passes
    // that yield the three bands) and obtains the four pyramids — from the
    // `.peaks` sidecar when one matches, otherwise built and then written —
    // all in one blocking phase. The sidecar holds pyramids only, so the
    // filter walk runs either way. Returns true on success. On failure, writes
    // a diagnostic to stderr and returns false, before anything is installed;
    // a cache problem is never a failure, only a rebuild. `on_progress` is
    // invoked with a value in [0.0, 1.0] periodically during the filter walk
    // and then, on a cache miss, the pyramid construction; it may be empty.
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

    // Shared handle to the one launch-time immutable sample buffer. A render
    // dispatch copies this handle into its RenderRequest so the worker's handle
    // keeps the buffer alive until the request dies.
    // Contract: the source is loaded once at launch and the pointed-to vector
    // is never mutated after publish.
    std::shared_ptr<const std::vector<float>> samples_shared() const;

    // Total number of pyramid levels, counting level 0 (the raw lane signal).
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
    // THE RESULTING PER-COLUMN READ BOUND, per lane — UNCONDITIONAL, in BOTH
    // views, for every input the loader accepts, and the SAME for every lane,
    // because the four pyramids share one frame count and one stride ladder
    // (each lane signal is exactly total_frames long, so a level's pair_count
    // is one number for all four):
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
    // on `lane`, read at pyramid `level`. Level 0 is the lane's raw int16
    // signal; levels 1..kCacheLevels select cached min/max pairs on the
    // powers-of-4 stride ladder (see kStrides in audio.cpp). Levels above the
    // deepest cached level clamp to it. Inputs are clamped; an empty range
    // returns (0, 0), and so does a lane value outside the enum (the
    // display-only safe return). The semantics are
    // identical across the four lanes: every lane, at every level, reads the
    // same int16 lattice through the one dequantizer.
    std::pair<float,float> get_peak_range(GuiWaveformLane lane,
                                          int level,
                                          int64_t start_sample,
                                          int64_t end_sample) const;

private:
    std::shared_ptr<const std::vector<float>> samples_;
    int64_t            total_frames_    = 0;
    int                sample_rate_     = 0;
    // The SOURCE's interleave count (2: sources are stereo-only), the stride
    // playback and the mono sum read the float buffer with. It is not a
    // display axis — the display's axis is the lane.
    int                channels_        = 0;

    // Size and mtime of the source file at the moment its samples were
    // decoded into this buffer, captured through the same stat_file_identity
    // used by the render fingerprint. The render pipeline records these
    // directly as a wav render's fingerprint source identity; the source is
    // immutable for the process lifetime, so there is no on-disk re-stat.
    // load() refuses a source it cannot stat, so both fields are valid
    // whenever a source is loaded.
    uint64_t load_identity_size_ = 0;
    int64_t  load_identity_mtime_ = 0;

    // THE LEVEL-0 SIGNALS, one per lane, indexed by the enum value: each is
    // exactly total_frames_ int16 samples on the pyramid's own quantization
    // lattice — the mono sum and its three bands, formed at load and never
    // persisted. The Sum lane is STORED rather than summed from the float
    // buffer on the fly, so that level 0 has one reader for all four lanes
    // and the sum's level-0 extremes sit on the same lattice its cached
    // levels fold from (the cost is one more total_frames-long int16 vector,
    // one third of the three-band signal storage (a quarter of the
    // resulting four-lane storage)).
    std::array<std::vector<int16_t>, kLaneCount> lane_signals_;

    // The fixed-stride cache levels (powers-of-4 ladder, see kStrides in
    // audio.cpp): read from a matching `.peaks` v8 sidecar when one is found
    // at load, or built by folding each lane's int16 signal on a miss (and
    // then written back to disk). Every level's stride and pair_count are
    // shared by the four lanes regardless of which path filled them.
    std::array<PyramidLevel, kCacheLevels> levels_;
};

bool is_peaks_cache_path(const std::string& path);
