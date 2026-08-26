#include "audio.h"
#include "render_cache.h"

#include "audio_probe.h"
#include "wav_io.h"

#include <fftw3.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <numbers>
#include <unistd.h>

namespace {

// `.peaks` format (v7) -----------------------------------------------------
//
// TEMPORARY: cache I/O is BYPASSED — neither try_load_cache nor
// write_cache_to_disk is called (the statement of why is at the load site,
// GuiAudio::load). The reader and writer below still describe and implement
// the v7 file exactly; a v7 file on disk is simply not read.
//
// The layout below is version-independent — what the bumps have moved is the
// RUNG COUNT the body header carries in num_levels, never the framing — so this
// description holds for every version the reader has ever accepted; only the
// version field's current value tracks kCacheVersion (below).
//
// 32-byte fixed preamble:
//   off 0  | 8  | private cache magic
//   off 8  | 2  | version (uint16, currently kCacheVersion = 7)
//   off 10 | 2  | flags   (uint16, written 0)
//   off 12 | 8  | source_size  (int64, bytes)
//   off 20 | 8  | source_mtime (int64, nanoseconds)
//   off 28 | 4  | sample_rate  (int32)
//
// Owner discriminator (length-prefixed string, immediately after the
// preamble):
//   4          | owner_len (uint32, bytes; bounded, an over-long value is a
//                miss, not a hard error)
//   owner_len  | source basename WITH extension, exact bytes, case-sensitive
//                (the final path component, e.g. "take.wav"). Distinguishes
//                same-stem sources like take.wav / take.WAV that share
//                size/mtime/rate/frames/channels.
//
// Body header (16 bytes):
//   8 | total_frames    (int64)
//   1 | render_channels (uint8, 1 or 2)
//   1 | num_levels      (uint8, = kNumLevels)
//   6 | reserved (zero)
//
// Then for each level in ascending stride order:
//   4 | stride     (int32)
//   8 | pair_count (int64)
//   ? | int16 data, channel-major: all of channel 0's pairs first
//       (pair_count * 4 bytes), then channel 1 if render_channels == 2.
//       Each pair is (min_int16, max_int16).
//
// Quantization: an existing v7 file on disk was written by the retired
// float `std::lround(v * 32767.0f)` path — clamp to [-1, 1], scale by 32767,
// round half-away-from-zero, in FLOAT. Out-of-range peaks clip at the
// boundary. That is the quantizer this format description covers; the
// current double/nearbyint quantizer (quantize_unit below) never wrote a v7
// file, cache I/O being bypassed (above), and is described only at its own
// definition.

constexpr char     kCacheMagic[8]         = "WTPEAKS";
// Bumped whenever the ladder changes shape: v5 densified it to powers of four,
// v6 extended it to nine rungs, v7 to thirteen. An older file therefore fails
// the version compare and takes the ordinary STALE path — logged, rebuilt,
// never partially accepted and never surfaced to the user as an error.
//
// THE BUMP IS LOAD-BEARING, not bookkeeping: the version compare runs BEFORE
// the level-count compare in try_load_cache, so an old file exits through
// stale() and never reaches the hdr_nl != kNumLevels branch, which would report
// CORRUPT for what is merely an older schema. Any future change to kStrides or
// kNumLevels must bump this in the same commit for that reason.
//
// The rebuild writes a larger sidecar (the pair count is ~1/16 + 1/64 + ... of
// the raw frames instead of ~1/32 + ...): architect-granted 2026-07-26, "ok to
// bump cache file size", the cost of the smoother zoom ladder. Every rung past
// the first costs a quarter of the one before it, so the deep tail is free in
// practice — on 13.2M-frame material the four rungs v7 added hold about 4, 1, 1
// and 1 pairs.
constexpr uint16_t kCacheVersion          = 7;
constexpr int      kStreamFramesPerChunk  = 65536;
// Bounds the owner-discriminator string so a corrupt header is a cheap cache
// miss rather than a memory-pressure event.
constexpr uint32_t kMaxOwnerBytes         = 4096;

// THE STRIDE LADDER — the single authoritative list. Powers of four, so each
// level covers 4x its predecessor: the zoom texture changes in small steps
// instead of the 32x jumps the old 32/1024/32768 ladder made, whose threshold
// crossings visibly re-textured the waveform mid-zoom. Every level after the
// first folds from the PREVIOUS level by kReductionFactor, so the ratio is the
// reduction factor by construction.
//
// WHY IT REACHES 268435456 — the top rung is derived, not picked, and it closes
// the per-column read bound for EVERY accepted input in BOTH views.
//
// A span stays inside the <=5-pair bound while it is under kReductionFactor x
// the top stride (that PRODUCT is what must exceed the worst span, not the top
// stride alone): 4 x 268435456 = 1073741824 = 2^30 frames.
//
// The worst span any read can present is bounded by the SOURCE total, because
// get_peak_range CLAMPS end_sample to total_frames_ before touching a level. So
// however long a target timeline grows under tempo, and however sharply the
// local slope concentrates, a column can never read past the source. And the
// source is RIFF-bounded: the format accepting the most frames per byte is
// 16-bit stereo at 4 bytes/frame, giving under 2^30 frames inside the 4 GiB
// ceiling (strictly under, once headers are counted). Worst span < 2^30 = 4 x
// top stride, hence ceil(span/top) <= 4 and reads stay <= 5 pairs — no view
// scoping, no tempo caveat, no saturation case. The four lanes share the
// ladder and the frame count, so the bound is one statement for all of them.
//
// THE STANDING NEXT KNOB: a level switch still changes both quantization and
// bin alignment, so a crossing can still pop — 4x makes it small, it does not
// make it impossible. If a pop is still visible on the labwc pass, the next
// step is min/max INTERPOLATION across a narrow log-span transition band
// (blending the two levels' values); it is deliberately not built yet, since it
// doubles the peak reads and the denser ladder may well be enough.
constexpr int      kNumLevels             = GuiAudio::kCacheLevels;
constexpr int      kLaneCount             = GuiAudio::kLaneCount;
constexpr int32_t  kStrides[kNumLevels]   = { 16, 64, 256, 1024, 4096, 16384,
                                              65536, 262144, 1048576, 4194304,
                                              16777216, 67108864, 268435456 };
constexpr int      kReductionFactor       = 4;
constexpr double   kQuantScale            = 32767.0;
// Within one lane's pyramid build, the first 95% of the progress budget goes
// to the dominant level-1 pass; the deeper levels fold from in-memory int16
// buffers and finish in microseconds.
constexpr float    kLevel1Share           = 0.95f;
// The load's progress budget splits between its two long phases: the band
// filter walk (FFT convolution over the whole signal) takes the first share,
// the four pyramid builds the rest. Both are O(total_frames) and of the same
// order in wall time, so an even split keeps the bar moving throughout.
constexpr float    kFilterProgressShare   = 0.5f;

static_assert(kStrides[0] > 0, "finest stride must be positive");
// EVERY adjacent rung, not just the endpoints: the builder folds level L from
// level L-1 over exactly kReductionFactor bins and then labels the result with
// the explicit kStrides[L], so any rung that is not its predecessor times the
// reduction factor would ship data whose real stride disagrees with its own
// header. Checking only first-vs-last would let an intermediate edit (say 64 ->
// 80, endpoints untouched) through.
static_assert([] {
    for (int L = 1; L < kNumLevels; ++L) {
        if (kStrides[L] != kStrides[L - 1] * kReductionFactor) return false;
    }
    return true;
}(), "each kStrides rung must be its predecessor times kReductionFactor — the "
     "fold builds every level from the previous one");

// THE ONE QUANTIZER onto the int16 display lattice, shared by every lane
// signal and therefore by every pyramid (the cached levels fold the quantized
// signal and never requantize): clamp to [-1, 1], scale by 32767, round with
// std::nearbyint — the project's rounding rule (banker's under the default
// mode) — in DOUBLE. It replaced a float `std::lround(v * 32767.0f)`
// (half-away-from-zero, a residue from before the rounding rule). The change
// reaches MORE THAN TIES: the old path multiplied in FLOAT, whose product can
// land ON a half that the exact double product is just under (a representable
// float near 1.5259e-5 gives 0.5f in float but 0.49999999953 in double: old 1,
// new 0). Accepted deliberately — the lattice is display-only, no audio path
// reads a pyramid, and every reader either dequantizes stored extrema
// (get_peak_range) or folds them by min/max (build_lane_pyramids), so no
// consumer assumes either rounding rule. A float source value is promoted to
// double BEFORE the multiply by every caller. A band may overshoot ±1 by
// Gibbs-scale amounts and clips at the boundary exactly as an out-of-range
// source peak does.
inline int16_t quantize_unit(double v) {
    if (v < -1.0) v = -1.0;
    if (v >  1.0) v =  1.0;
    return static_cast<int16_t>(std::nearbyint(v * kQuantScale));
}
inline float dequantize_i16(int16_t q) {
    return static_cast<float>(q) / static_cast<float>(kQuantScale);
}

std::shared_ptr<const std::vector<float>>
make_immutable_samples(std::vector<float>&& samples) {
    std::shared_ptr<std::vector<float>> owned =
        std::make_shared<std::vector<float>>(std::move(samples));
    return owned;
}

std::string cache_path_for(const std::string& source) {
    // Sibling of the audio file with the source's extension swapped to
    // `.peaks`. For `song.wav` this is `song.peaks`; legacy double-extension
    // sidecars are obsolete and any such files left on disk are disposable.
    std::filesystem::path p(source);
    p.replace_extension(".peaks");
    return p.string();
}

std::string owner_name_for(const std::string& source) {
    // The source file's final path component WITH extension, exact bytes and
    // case-sensitive, so same-stem siblings (take.wav / take.WAV) that share
    // size/mtime/rate/frames/channels never accept each other's peaks.
    return std::filesystem::path(source).filename().string();
}

std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool stat_size_mtime(const std::string& path, int64_t& size, int64_t& mtime) {
    RenderFileIdentity identity;
    if (!stat_file_identity(path, identity) ||
        identity.size > static_cast<uint64_t>(
                            std::numeric_limits<int64_t>::max())) {
        return false;
    }
    size  = static_cast<int64_t>(identity.size);
    mtime = identity.mtime;
    return true;
}

void reset_levels(std::array<GuiAudio::PyramidLevel, kNumLevels>& levels) {
    for (auto& L : levels) {
        L.stride     = 0;
        L.pair_count = 0;
        L.pairs.clear();
        L.pairs.shrink_to_fit();
    }
}

// A progress callback that reports into the sub-range [lo, hi] of `outer`'s
// [0, 1]; empty stays empty. Captures by value so a mapped callback can be
// mapped again without a lifetime to mind.
GuiAudio::ProgressCallback sub_progress(const GuiAudio::ProgressCallback& outer,
                                        float lo, float hi) {
    if (!outer) return {};
    return [outer, lo, hi](float p) { outer(lo + (hi - lo) * p); };
}

// THE BAND SPLIT ------------------------------------------------------------
//
// The waveform's three bands are a DISPLAY-ONLY decomposition of the mono sum
// m[n] = (L[n] + R[n]) / 2 (double, from the float stereo buffer; halved so an
// in-phase pair cannot exceed full scale). They never reach JACK, the render
// pipeline or the disk as audio — playback and every render read the untouched
// stereo buffer.
//
// The crossovers are hard-coded constants — no settings key, by ruling
// (architect 2026-08-25). kBandLowCrossoverHz = 120 Hz is derived from the
// orchestral instrumentation (the same value the retired automated
// EQ-matching and transient-detection work used; 100 Hz is too low), and
// kBandHighCrossoverHz = 3500 Hz stands on equal-loudness (Fletcher–Munson)
// grounds. DJ software hard-codes the same pair.
// They are DESIGN TARGETS — the finite, windowed kernels realize the
// Linkwitz-Riley curve to within their design error, so at the low crossover
// a pure tone paints at nominally half height in both Low and Mid, and at the
// high crossover in both Mid and High: the -6 dB LR overlap, the intended look.
//
// ZERO PHASE, because "I want to be able to see the waveform as it actually
// looks like if it were decomposed in an ideal sense, in terms of phase": every
// band sits exactly where its component sits in the real signal, shifted by
// not one sample. That is an INDEX-LEVEL contract on the convolution, kept at
// build_lane_signals below (the convolution, the `n + K` publication and the
// accumulator extents; the kernel itself is designed at design_lowpass_kernel).
// The filters are real, even (symmetric)
// kernels of the LR24 (Butterworth-squared) magnitude |H(f)| = 1 / (1 +
// (f/fc)^4); such a kernel's impulse decays as e^(-0.707 * 2 * pi * fc * |t|)
// — 1/e at about 1.9 ms and -60 dB by about 13 ms at 120 Hz, with the pre-ring
// the same tail mirrored into negative time. That smear is the accepted cost
// of zero phase; at 3500 Hz the tail is gone inside a millisecond.
//
// THREE BANDS FROM TWO FILTERS, exactly complementary: with zero-phase
// lowpasses the highpasses are subtractions, so
//     low  = LPlow(m)             (LPlow  = the kBandLowCrossoverHz kernel)
//     high = m - LPhigh(m)        (LPhigh = the kBandHighCrossoverHz kernel)
//     mid  = LPhigh(m) - LPlow(m)
// and low + mid + high == m sample-for-sample BY CONSTRUCTION (to double
// rounding), whatever the kernels are — high and mid come from the two ACTUAL
// lowpass outputs, not from separately designed highpasses. (The pedantic
// difference between this mid and HPlow·LPhigh is the product term
// LPlow·HPhigh, peaking near sqrt(120 * 3500) = 648 Hz at about 1.4e-6.)
constexpr int kBandLowCrossoverHz  = 120;
constexpr int kBandHighCrossoverHz = 3500;
constexpr int kBandKernelHalfMs    = 20;

// EVERY SIZE IS A PURE FUNCTION OF THE SAMPLE RATE AND THE CONSTANTS. The
// product enforces a rate floor and no ceiling, so nothing here may assume a
// rate: a fixed grid would be wrong above ~1.6 MHz and coarsen with rate. The
// arithmetic is TYPED, not transcribed — the constants are integers and the
// rate is an int, so every formula is evaluated in double with an explicit
// cast on each integer operand, and every derived count is carried in int64_t
// until it narrows to FFTW's plan-length int or to size_t.
//
//   K     = nearbyint((kBandKernelHalfMs / 1000) * fs)   kernel half-length
//   L     = 2K + 1                                        taps, h[-K..K]
//   M_des = next_pow2(max(65536, 8L))                     design grid
//   Nfft  = next_pow2(max(65536, 4L))                     convolution FFT
//   B     = Nfft - L + 1                                  input per block
//
// (linear overlap-add needs Nfft >= B + L - 1, which B is defined to satisfy
// with equality). At 44.1 kHz: K = 882, L = 1765, M_des = 65536 (0.67 Hz
// bins), Nfft = 65536, B = 63772 — the intended arithmetic's example, not a
// substitute for it.
struct BandFilterGeometry {
    int64_t K     = 0;
    int64_t L     = 0;
    int64_t M_des = 0;
    int64_t Nfft  = 0;
    int64_t B     = 0;
};

int64_t next_pow2(int64_t v) {
    int64_t p = 1;
    while (p < v) p *= 2;
    return p;
}

BandFilterGeometry band_filter_geometry(int sample_rate) {
    BandFilterGeometry g;
    g.K = static_cast<int64_t>(std::nearbyint(
        (static_cast<double>(kBandKernelHalfMs) / 1000.0) *
        static_cast<double>(sample_rate)));
    g.L     = 2 * g.K + 1;
    g.M_des = next_pow2(std::max<int64_t>(65536, 8 * g.L));
    g.Nfft  = next_pow2(std::max<int64_t>(65536, 4 * g.L));
    g.B     = g.Nfft - g.L + 1;
    return g;
}

// Designs one zero-phase LR24 lowpass kernel by frequency sampling. Returns
// the L taps with tap index i = j + K holding h[j], j in [-K, K].
//
// The real spectrum H[k] = 1 / (1 + (f/fc)^4), f = k * fs / M_des for k in
// [0, M_des/2], imaginary parts zero, goes through a c2r inverse of length
// M_des divided by M_des (FFTW's inverse is unnormalized). The result g is the
// periodic kernel with its centre at index 0 and its negative half wrapped to
// the top, UNWRAPPED with exactly this scope: h[0] = g[0]; for 1 <= j <= K,
// h[j] = g[j] and h[-j] = g[M_des - j] — no modulo, no negative remainder,
// and j = 0 never reads g[M_des]. A Hann window over the kept span,
// w[j] = 0.5 * (1 + cos(pi * j / (K + 1))) (zero just outside ±K, never zero
// at ±K), is symmetric and so preserves the zero phase. The taps are then
// divided by their sum accumulated in ASCENDING index order: DC gain is unity
// to double rounding — there is no bit-exact "sum h = 1" in floating point,
// and m - LP(m) carries no material DC leak, which is all the display needs.
//
// FFTW_ESTIMATE only, single-threaded: the engine's determinism convention
// (a measured plan may differ run to run). The load runs on the GUI thread at
// launch, before any render exists, so the FFTW planner is uncontended here;
// the engine's global plan thread count is pinned to 1 in any case.
std::vector<double> design_lowpass_kernel(int crossover_hz,
                                          int sample_rate,
                                          const BandFilterGeometry& g) {
    const int64_t half_bins = g.M_des / 2;                 // bins [0, half]
    fftw_complex* spec = fftw_alloc_complex(static_cast<size_t>(half_bins + 1));
    double*       grid = fftw_alloc_real(static_cast<size_t>(g.M_des));
    fftw_plan     plan = fftw_plan_dft_c2r_1d(static_cast<int>(g.M_des),
                                              spec, grid, FFTW_ESTIMATE);

    const double fc = static_cast<double>(crossover_hz);
    for (int64_t k = 0; k <= half_bins; ++k) {
        const double f  = static_cast<double>(k) * static_cast<double>(sample_rate) /
                          static_cast<double>(g.M_des);
        const double r2 = (f / fc) * (f / fc);
        spec[k][0] = 1.0 / (1.0 + r2 * r2);               // (f/fc)^4
        spec[k][1] = 0.0;
    }
    fftw_execute(plan);

    const double m_des = static_cast<double>(g.M_des);
    std::vector<double> taps(static_cast<size_t>(g.L), 0.0);
    taps[static_cast<size_t>(g.K)] = grid[0] / m_des;
    for (int64_t j = 1; j <= g.K; ++j) {
        taps[static_cast<size_t>(g.K + j)] = grid[j] / m_des;
        taps[static_cast<size_t>(g.K - j)] = grid[g.M_des - j] / m_des;
    }
    for (int64_t j = 0; j <= g.K; ++j) {
        const double w = 0.5 * (1.0 + std::cos(std::numbers::pi *
                                               static_cast<double>(j) /
                                               static_cast<double>(g.K + 1)));
        taps[static_cast<size_t>(g.K + j)] *= w;
        if (j > 0) taps[static_cast<size_t>(g.K - j)] *= w;
    }
    double sum = 0.0;
    for (int64_t i = 0; i < g.L; ++i) sum += taps[static_cast<size_t>(i)];
    for (double& t : taps) t /= sum;

    fftw_destroy_plan(plan);
    fftw_free(spec);
    fftw_free(grid);
    return taps;
}

// Forms the four lane signals from the stereo buffer: the mono sum and, from
// two zero-phase lowpass passes over it, the three bands — quantized onto the
// int16 display lattice. `samples` is interleaved, `channels` per frame
// (always 2: sources are stereo-only), `total_frames` = N frames.
//
// THE CONVOLUTION — overlap-add through FFTW, doubles throughout, FFTW_ESTIMATE
// and single-threaded as at design_lowpass_kernel. The taps are placed
// CAUSALLY in the kernel FFT buffer: index i = j + K holds h[j], indices
// [L, Nfft) are zero; one r2c per kernel. The signal is ZERO OUTSIDE [0, N)
// (silence outside the file). num_blocks = 1 + (N - 1) / B; block b's input
// is the min(B, N - b*B) samples starting at b*B, zero-padded to Nfft, one
// r2c per block, two spectral products, two c2r inverses each divided by
// Nfft. THE ACCUMULATOR is one zero-initialized full-convolution buffer of
// exactly N + L - 1 doubles per kernel, and block b adds only
// min(Nfft, (N + L - 1) - b*B) of its inverse outputs at offset b*B — never
// the full Nfft from the last block, whose tail would run past the end. The
// two boundary walks: N = 1 runs one block and writes exactly L outputs into
// an L-long accumulator; N = q*B runs exactly q blocks and the final FULL
// block writes all Nfft outputs.
//
// THE INDEX-LEVEL ZERO-PHASE CONTRACT: the full (causal) convolution y_full
// has N + L - 1 samples and the centred result sits at y_full[n + K], so the
// published lowpass is EXACTLY y[n] = y_full[n + K] for n in [0, N).
// Publishing y_full[0, N) instead would delay every band by K samples and
// break the ruling that no band is shifted in time.
//
// Fixed block partition (B), fixed accumulation order: the output is a
// deterministic function of (fs, N, m) and the algorithm. Direct convolution
// is rejected on cost (tens of millions of samples times L taps); this walk
// is on the order of a second for a ten-minute song, single-threaded.
void build_lane_signals(const float* samples,
                        int channels,
                        int64_t total_frames,
                        int sample_rate,
                        const GuiAudio::ProgressCallback& on_progress,
                        std::array<std::vector<int16_t>, kLaneCount>& out) {
    const int64_t N = total_frames;
    const BandFilterGeometry g = band_filter_geometry(sample_rate);
    const std::vector<double> taps_low  =
        design_lowpass_kernel(kBandLowCrossoverHz,  sample_rate, g);
    const std::vector<double> taps_high =
        design_lowpass_kernel(kBandHighCrossoverHz, sample_rate, g);

    // m[n] = (L[n] + R[n]) / 2 in double, read straight off the float buffer
    // wherever it is needed (the block fill and the band formation) rather
    // than stored: the same expression both times, so both see one value.
    const auto mono_at = [&](int64_t n) -> double {
        const float* p = &samples[n * static_cast<int64_t>(channels)];
        return (static_cast<double>(p[0]) + static_cast<double>(p[1])) / 2.0;
    };

    const int64_t spec_bins = g.Nfft / 2 + 1;
    double*       fwd_in    = fftw_alloc_real(static_cast<size_t>(g.Nfft));
    fftw_complex* fwd_out   = fftw_alloc_complex(static_cast<size_t>(spec_bins));
    fftw_plan     plan_fwd  = fftw_plan_dft_r2c_1d(static_cast<int>(g.Nfft),
                                                   fwd_in, fwd_out, FFTW_ESTIMATE);
    fftw_complex* inv_in    = fftw_alloc_complex(static_cast<size_t>(spec_bins));
    double*       inv_out   = fftw_alloc_real(static_cast<size_t>(g.Nfft));
    fftw_plan     plan_inv  = fftw_plan_dft_c2r_1d(static_cast<int>(g.Nfft),
                                                   inv_in, inv_out, FFTW_ESTIMATE);
    fftw_complex* spec_low  = fftw_alloc_complex(static_cast<size_t>(spec_bins));
    fftw_complex* spec_high = fftw_alloc_complex(static_cast<size_t>(spec_bins));

    // The two kernel spectra, causal placement: tap i at buffer index i.
    const auto kernel_spectrum = [&](const std::vector<double>& taps,
                                     fftw_complex* spec) {
        std::fill(fwd_in, fwd_in + g.Nfft, 0.0);
        for (int64_t i = 0; i < g.L; ++i) fwd_in[i] = taps[static_cast<size_t>(i)];
        fftw_execute(plan_fwd);
        for (int64_t k = 0; k < spec_bins; ++k) {
            spec[k][0] = fwd_out[k][0];
            spec[k][1] = fwd_out[k][1];
        }
    };
    kernel_spectrum(taps_low,  spec_low);
    kernel_spectrum(taps_high, spec_high);

    const int64_t full_len = N + g.L - 1;
    std::vector<double> acc_low (static_cast<size_t>(full_len), 0.0);
    std::vector<double> acc_high(static_cast<size_t>(full_len), 0.0);

    const double  nfft_d     = static_cast<double>(g.Nfft);
    const int64_t num_blocks = 1 + (N - 1) / g.B;
    for (int64_t b = 0; b < num_blocks; ++b) {
        const int64_t offset  = b * g.B;
        const int64_t payload = std::min<int64_t>(g.B, N - offset);
        for (int64_t i = 0; i < payload; ++i) fwd_in[i] = mono_at(offset + i);
        std::fill(fwd_in + payload, fwd_in + g.Nfft, 0.0);
        fftw_execute(plan_fwd);

        const int64_t count = std::min<int64_t>(g.Nfft, full_len - offset);
        const auto accumulate = [&](const fftw_complex* spec,
                                    std::vector<double>& acc) {
            for (int64_t k = 0; k < spec_bins; ++k) {
                const double xr = fwd_out[k][0], xi = fwd_out[k][1];
                const double hr = spec[k][0],    hi = spec[k][1];
                inv_in[k][0] = xr * hr - xi * hi;
                inv_in[k][1] = xr * hi + xi * hr;
            }
            fftw_execute(plan_inv);
            for (int64_t i = 0; i < count; ++i) {
                acc[static_cast<size_t>(offset + i)] += inv_out[i] / nfft_d;
            }
        };
        accumulate(spec_low,  acc_low);
        accumulate(spec_high, acc_high);

        if (on_progress) {
            on_progress(static_cast<float>(static_cast<double>(b + 1) /
                                           static_cast<double>(num_blocks)));
        }
    }

    fftw_destroy_plan(plan_fwd);
    fftw_destroy_plan(plan_inv);
    fftw_free(fwd_in);   fftw_free(fwd_out);
    fftw_free(inv_in);   fftw_free(inv_out);
    fftw_free(spec_low); fftw_free(spec_high);

    for (auto& lane : out) lane.assign(static_cast<size_t>(N), 0);
    auto& sig_sum  = out[static_cast<size_t>(GuiWaveformLane::Sum)];
    auto& sig_low  = out[static_cast<size_t>(GuiWaveformLane::Low)];
    auto& sig_mid  = out[static_cast<size_t>(GuiWaveformLane::Mid)];
    auto& sig_high = out[static_cast<size_t>(GuiWaveformLane::High)];

    // TEMPORARY development diagnostic, not a guard: the largest
    // |low + mid + high - m| over the signal, accumulated in the DOUBLE domain
    // right where the three bands are formed — before any clamp, nearbyint or
    // int16 cast, since three independently quantized bands reconstruct m
    // only to the 1/32767 lattice, which is not what this checks. Expected on
    // the order of 1e-12 of full scale or below; printed once after the walk.
    double max_sum_error = 0.0;
    for (int64_t n = 0; n < N; ++n) {
        const double m    = mono_at(n);
        const double lp_l = acc_low [static_cast<size_t>(n + g.K)];   // y[n] = y_full[n + K]
        const double lp_h = acc_high[static_cast<size_t>(n + g.K)];
        const double low  = lp_l;
        const double high = m - lp_h;
        const double mid  = lp_h - lp_l;
        const double err  = std::fabs(low + mid + high - m);
        if (err > max_sum_error) max_sum_error = err;
        const size_t i = static_cast<size_t>(n);
        sig_sum [i] = quantize_unit(m);
        sig_low [i] = quantize_unit(low);
        sig_mid [i] = quantize_unit(mid);
        sig_high[i] = quantize_unit(high);
    }
    std::fprintf(stderr,
                 "warptempo_gui: TEMPORARY band-split check: "
                 "max |low+mid+high - m| = %.3e of full scale\n",
                 max_sum_error);
}

// Pyramid build over the four in-memory lane signals. Every level's stride and
// pair_count are one number for all lanes (each signal is exactly total_frames
// long); per lane, the FINEST level's pairs (stride kStrides[0]) come straight
// from the int16 signal and each deeper level then folds from the one before
// it in memory, so the ladder is built once per level and never re-scans the
// signal. The on_progress callback pumps the compositor during load and is
// invoked at roughly kStreamFramesPerChunk-frame boundaries.
void build_lane_pyramids(const std::array<std::vector<int16_t>, kLaneCount>& signals,
                         int64_t total_frames,
                         const GuiAudio::ProgressCallback& on_progress,
                         std::array<GuiAudio::PyramidLevel, kNumLevels>& out) {
    reset_levels(out);

    int64_t pair_count[kNumLevels];
    pair_count[0] = (total_frames + kStrides[0] - 1) / kStrides[0];
    for (int L = 1; L < kNumLevels; L++) {
        pair_count[L] = (pair_count[L - 1] + kReductionFactor - 1) / kReductionFactor;
    }
    for (int L = 0; L < kNumLevels; L++) {
        out[L].stride     = kStrides[L];
        out[L].pair_count = pair_count[L];
        out[L].pairs.assign(static_cast<size_t>(kLaneCount),
                            std::vector<int16_t>(static_cast<size_t>(2 * pair_count[L])));
    }

    const int64_t prog_denom = total_frames > 0 ? total_frames : 1;
    for (int lane = 0; lane < kLaneCount; lane++) {
        const GuiAudio::ProgressCallback lane_progress = sub_progress(
            on_progress,
            static_cast<float>(lane)     / static_cast<float>(kLaneCount),
            static_cast<float>(lane + 1) / static_cast<float>(kLaneCount));
        const std::vector<int16_t>& sig = signals[static_cast<size_t>(lane)];
        std::vector<int16_t>& finest = out[0].pairs[static_cast<size_t>(lane)];

        int64_t next_progress = kStreamFramesPerChunk;
        for (int64_t p = 0; p < pair_count[0]; p++) {
            const int64_t s0 = p * kStrides[0];
            const int64_t s1 = std::min<int64_t>(s0 + kStrides[0], total_frames);
            int16_t lo = sig[static_cast<size_t>(s0)];
            int16_t hi = lo;
            for (int64_t s = s0 + 1; s < s1; s++) {
                const int16_t v = sig[static_cast<size_t>(s)];
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            finest[static_cast<size_t>(2 * p)]     = lo;
            finest[static_cast<size_t>(2 * p + 1)] = hi;

            if (lane_progress && s1 >= next_progress) {
                const float pr = kLevel1Share *
                    static_cast<float>(s1) / static_cast<float>(prog_denom);
                lane_progress(pr);
                next_progress += kStreamFramesPerChunk;
            }
        }

        // Every deeper level folds by min-of-mins / max-of-maxes over
        // kReductionFactor adjacent pairs of the PREVIOUS level (a level's
        // stride is therefore its predecessor's times kReductionFactor, which
        // is what makes the ladder powers-of-four). Already-quantized int16 in,
        // no requantization, so a folded extreme is bit-identical to the raw
        // extreme it came from.
        for (int L = 1; L < kNumLevels; L++) {
            const int64_t prev_pc = pair_count[L - 1];
            const int64_t cur_pc  = pair_count[L];
            const auto& src = out[L - 1].pairs[static_cast<size_t>(lane)];
            auto&       dst = out[L].pairs[static_cast<size_t>(lane)];
            for (int64_t q = 0; q < cur_pc; q++) {
                const int64_t i0 = q * kReductionFactor;
                const int64_t i1 = std::min<int64_t>(i0 + kReductionFactor, prev_pc);
                int16_t lo = src[static_cast<size_t>(2 * i0)];
                int16_t hi = src[static_cast<size_t>(2 * i0 + 1)];
                for (int64_t i = i0 + 1; i < i1; i++) {
                    const int16_t a = src[static_cast<size_t>(2 * i)];
                    const int16_t b = src[static_cast<size_t>(2 * i + 1)];
                    if (a < lo) lo = a;
                    if (b > hi) hi = b;
                }
                dst[static_cast<size_t>(2 * q)]     = lo;
                dst[static_cast<size_t>(2 * q + 1)] = hi;
            }
        }

        if (lane_progress) lane_progress(1.0f);
    }

    if (on_progress) on_progress(1.0f);
}

// Try to populate `levels` from the disk cache. Returns true on a clean hit.
// On version/header mismatch ("stale") logs and returns false leaving
// `levels` untouched. On corruption (short read, internal mismatch) clears
// `levels` so the caller's rebuild path starts fresh, logs, returns false.
[[maybe_unused]] bool try_load_cache(const std::string& source_path,
                    int64_t total_frames,
                    int     render_channels,
                    int     sample_rate,
                    std::array<GuiAudio::PyramidLevel, kNumLevels>& levels) {

    const std::string cpath = cache_path_for(source_path);
    FILE* f = std::fopen(cpath.c_str(), "rb");
    if (!f) return false;

    int64_t src_size = 0, src_mtime = 0;
    if (!stat_size_mtime(source_path, src_size, src_mtime)) {
        std::fclose(f);
        return false;
    }

    auto stale = [&]() {
        std::fprintf(stderr,
            "warptempo_gui: Peaks cache stale or invalid for %s; rebuilding\n",
            source_path.c_str());
        std::fclose(f);
    };
    auto corrupt = [&]() {
        std::fprintf(stderr,
            "warptempo_gui: Peaks cache corrupt for %s; rebuilding\n",
            source_path.c_str());
        reset_levels(levels);
        std::fclose(f);
    };

    char magic[8];
    if (std::fread(magic, 1, 8, f) != 8 ||
        std::memcmp(magic, kCacheMagic, 8) != 0) {
        stale(); return false;
    }
    uint16_t version = 0, flags = 0;
    if (std::fread(&version, sizeof(version), 1, f) != 1 ||
        version != kCacheVersion) {
        stale(); return false;
    }
    if (std::fread(&flags, sizeof(flags), 1, f) != 1) { stale(); return false; }
    int64_t hdr_size = 0, hdr_mtime = 0;
    if (std::fread(&hdr_size, sizeof(hdr_size), 1, f) != 1 ||
        hdr_size != src_size) {
        stale(); return false;
    }
    if (std::fread(&hdr_mtime, sizeof(hdr_mtime), 1, f) != 1 ||
        hdr_mtime != src_mtime) {
        stale(); return false;
    }
    int32_t hdr_sr = 0;
    if (std::fread(&hdr_sr, sizeof(hdr_sr), 1, f) != 1 ||
        hdr_sr != sample_rate) {
        stale(); return false;
    }

    // Owner discriminator: an over-long length or a byte mismatch is a cache
    // miss (rebuild), never a hard error and never accepting the wrong cache.
    uint32_t hdr_owner_len = 0;
    if (std::fread(&hdr_owner_len, sizeof(hdr_owner_len), 1, f) != 1 ||
        hdr_owner_len > kMaxOwnerBytes) {
        stale(); return false;
    }
    std::string hdr_owner(hdr_owner_len, '\0');
    if (hdr_owner_len > 0 &&
        std::fread(hdr_owner.data(), 1, hdr_owner_len, f) != hdr_owner_len) {
        stale(); return false;
    }
    if (hdr_owner != owner_name_for(source_path)) { stale(); return false; }

    int64_t hdr_total_frames = 0;
    uint8_t hdr_rc = 0, hdr_nl = 0;
    char    reserved[6];
    if (std::fread(&hdr_total_frames, sizeof(hdr_total_frames), 1, f) != 1 ||
        hdr_total_frames != total_frames) {
        stale(); return false;
    }
    if (std::fread(&hdr_rc, sizeof(hdr_rc), 1, f) != 1 ||
        hdr_rc != static_cast<uint8_t>(render_channels)) {
        stale(); return false;
    }
    if (std::fread(&hdr_nl, sizeof(hdr_nl), 1, f) != 1) { stale(); return false; }
    if (std::fread(reserved, 1, 6, f) != 6) { stale(); return false; }
    if (hdr_nl != static_cast<uint8_t>(kNumLevels)) { corrupt(); return false; }

    int64_t expected_pc[kNumLevels];
    expected_pc[0] = (total_frames + kStrides[0] - 1) / kStrides[0];
    for (int L = 1; L < kNumLevels; L++) {
        expected_pc[L] = (expected_pc[L - 1] + kReductionFactor - 1) / kReductionFactor;
    }

    for (int L = 0; L < kNumLevels; L++) {
        int32_t hdr_stride = 0;
        int64_t hdr_pc     = 0;
        if (std::fread(&hdr_stride, sizeof(hdr_stride), 1, f) != 1 ||
            hdr_stride != kStrides[L]) {
            corrupt(); return false;
        }
        if (std::fread(&hdr_pc, sizeof(hdr_pc), 1, f) != 1 ||
            hdr_pc != expected_pc[L]) {
            corrupt(); return false;
        }

        levels[L].stride     = kStrides[L];
        levels[L].pair_count = hdr_pc;
        levels[L].pairs.assign(static_cast<size_t>(render_channels),
                               std::vector<int16_t>(static_cast<size_t>(2 * hdr_pc)));
        for (int ch = 0; ch < render_channels; ch++) {
            const size_t bytes = sizeof(int16_t) * 2 * static_cast<size_t>(hdr_pc);
            if (hdr_pc > 0 &&
                std::fread(levels[L].pairs[ch].data(), 1, bytes, f) != bytes) {
                corrupt(); return false;
            }
        }
    }

    std::fclose(f);
    std::fprintf(stderr, "warptempo_gui: Peaks cache hit for %s\n",
                 source_path.c_str());
    return true;
}

// Write `levels` to <basename>.peaks (the audio file's extension is
// swapped, see `cache_path_for`) using the .tmp + fsync + rename atomic
// pattern. Logs a single stderr line on any failure and returns false.
// Cache write failure is never fatal.
[[maybe_unused]] bool write_cache_to_disk(const std::string& source_path,
                         int64_t total_frames,
                         int     render_channels,
                         int     sample_rate,
                         const std::array<GuiAudio::PyramidLevel, kNumLevels>& levels) {

    const std::string cpath = cache_path_for(source_path);
    const std::string tpath = cpath + ".tmp";

    int64_t src_size = 0, src_mtime = 0;
    if (!stat_size_mtime(source_path, src_size, src_mtime)) {
        std::fprintf(stderr,
            "warptempo_gui: Peaks cache write failed for %s: %s\n",
            source_path.c_str(), std::strerror(errno));
        return false;
    }

    FILE* f = std::fopen(tpath.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr,
            "warptempo_gui: Peaks cache write failed for %s: %s\n",
            source_path.c_str(), std::strerror(errno));
        return false;
    }

    auto fail = [&]() -> bool {
        const int err = errno;
        std::fprintf(stderr,
            "warptempo_gui: Peaks cache write failed for %s: %s\n",
            source_path.c_str(), std::strerror(err));
        std::fclose(f);
        ::unlink(tpath.c_str());
        return false;
    };

    if (std::fwrite(kCacheMagic, 1, 8, f) != 8) return fail();
    const uint16_t version = kCacheVersion;
    const uint16_t flags   = 0;
    if (std::fwrite(&version, sizeof(version), 1, f) != 1) return fail();
    if (std::fwrite(&flags,   sizeof(flags),   1, f) != 1) return fail();
    if (std::fwrite(&src_size,  sizeof(src_size),  1, f) != 1) return fail();
    if (std::fwrite(&src_mtime, sizeof(src_mtime), 1, f) != 1) return fail();
    const int32_t sr32 = static_cast<int32_t>(sample_rate);
    if (std::fwrite(&sr32, sizeof(sr32), 1, f) != 1) return fail();

    // Owner discriminator: length-prefixed source basename WITH extension.
    const std::string owner = owner_name_for(source_path);
    const uint32_t owner_len = static_cast<uint32_t>(owner.size());
    if (std::fwrite(&owner_len, sizeof(owner_len), 1, f) != 1) return fail();
    if (owner_len > 0 &&
        std::fwrite(owner.data(), 1, owner_len, f) != owner_len) return fail();

    const int64_t tf = total_frames;
    if (std::fwrite(&tf, sizeof(tf), 1, f) != 1) return fail();
    const uint8_t rc = static_cast<uint8_t>(render_channels);
    if (std::fwrite(&rc, sizeof(rc), 1, f) != 1) return fail();
    const uint8_t nl = static_cast<uint8_t>(kNumLevels);
    if (std::fwrite(&nl, sizeof(nl), 1, f) != 1) return fail();
    const char zeros[6] = {0,0,0,0,0,0};
    if (std::fwrite(zeros, 1, 6, f) != 6) return fail();

    for (int L = 0; L < kNumLevels; L++) {
        const int32_t stride = levels[L].stride;
        const int64_t pc     = levels[L].pair_count;
        if (std::fwrite(&stride, sizeof(stride), 1, f) != 1) return fail();
        if (std::fwrite(&pc,     sizeof(pc),     1, f) != 1) return fail();
        for (int ch = 0; ch < render_channels; ch++) {
            const size_t bytes = sizeof(int16_t) * 2 * static_cast<size_t>(pc);
            if (pc > 0 &&
                std::fwrite(levels[L].pairs[ch].data(), 1, bytes, f) != bytes) {
                return fail();
            }
        }
    }

    if (std::fflush(f) != 0)         return fail();
    if (::fsync(::fileno(f)) != 0)   return fail();
    if (std::fclose(f) != 0) {
        const int err = errno;
        std::fprintf(stderr,
            "warptempo_gui: Peaks cache write failed for %s: %s\n",
            source_path.c_str(), std::strerror(err));
        ::unlink(tpath.c_str());
        return false;
    }
    if (::rename(tpath.c_str(), cpath.c_str()) != 0) {
        const int err = errno;
        std::fprintf(stderr,
            "warptempo_gui: Peaks cache write failed for %s: %s\n",
            source_path.c_str(), std::strerror(err));
        ::unlink(tpath.c_str());
        return false;
    }
    std::fprintf(stderr, "warptempo_gui: Peaks cache written for %s\n",
                 source_path.c_str());
    return true;
}

} // namespace

bool GuiAudio::load(const std::string& path, const ProgressCallback& on_progress) {
    auto info = audio_probe(path);
    if (!info) {
        std::fprintf(stderr,
                     "warptempo_gui: Could not open '%s': %s\n",
                     path.c_str(), info.error().c_str());
        return false;
    }

    // Sources are stereo-only: GuiFileLoader::load_file (the sole caller of
    // this function) refuses channels != 2 before load runs, so next_channels
    // is always 2 — the interleave stride of the float buffer, which playback
    // and the mono sum read; the display's own axis is the lane.
    const int next_channels        = info->channels;
    const int next_sample_rate     = info->sample_rate;
    RenderFileIdentity next_load_identity;
    if (!stat_file_identity(path, next_load_identity)) {
        std::fprintf(stderr,
                     "warptempo_gui: Could not stat '%s'\n",
                     path.c_str());
        return false;
    }

    auto full = wav_read_full(path);
    if (!full) {
        std::fprintf(stderr,
                     "warptempo_gui: Could not read '%s': %s\n",
                     path.c_str(), full.error().c_str());
        return false;
    }
    std::vector<float> next_samples = std::move(*full);
    const int64_t next_total_frames =
        static_cast<int64_t>(next_samples.size() /
                             static_cast<size_t>(next_channels));

    std::array<std::vector<int16_t>, kLaneCount> next_lanes;
    std::array<PyramidLevel, kCacheLevels>       next_levels;
    reset_levels(next_levels);

    // TEMPORARY: the on-disk peaks cache is BYPASSED in both directions —
    // try_load_cache is not consulted and write_cache_to_disk is not run — so
    // every load builds the lanes and pyramids fresh. The v7 body stores the
    // retired stereo-display pair (one pyramid per channel), which the
    // four-lane implementation must neither consume nor rewrite; the cache is
    // re-enabled only with the four-lane schema.
    if (on_progress) on_progress(0.0f);

    build_lane_signals(next_samples.data(), next_channels, next_total_frames,
                       next_sample_rate,
                       sub_progress(on_progress, 0.0f, kFilterProgressShare),
                       next_lanes);
    build_lane_pyramids(next_lanes, next_total_frames,
                        sub_progress(on_progress, kFilterProgressShare, 1.0f),
                        next_levels);

    // Nothing above can fail once the samples are decoded, so this is the one
    // install point: every refusal returned before it and left this object
    // untouched.
    samples_         = make_immutable_samples(std::move(next_samples));
    total_frames_    = next_total_frames;
    sample_rate_     = next_sample_rate;
    channels_        = next_channels;
    load_identity_size_  = next_load_identity.size;
    load_identity_mtime_ = next_load_identity.mtime;
    lane_signals_    = std::move(next_lanes);
    levels_          = std::move(next_levels);
    return true;
}

std::shared_ptr<const std::vector<float>> GuiAudio::samples_shared() const {
    return samples_;
}

int GuiAudio::num_levels() const {
    if (total_frames_ <= 0) return 0;
    return 1 + kNumLevels;  // level 0 (raw) + the cached ladder
}

int GuiAudio::level_for_span(double span_samples) const {
    const int nl = num_levels();
    if (nl <= 1) return 0;
    // Coarsest cached level whose stride still fits inside the span; below the
    // finest stride there is no useful cached level and the raw samples win.
    // Walking from the top means the first fit is the coarsest, which is what
    // holds a column to at most 5 pair reads (or at most 16 raw samples below
    // the finest stride — see the bound derivation at the declaration).
    int level = 0;
    for (int L = kNumLevels - 1; L >= 0; --L) {
        if (span_samples >= static_cast<double>(kStrides[L])) {
            level = L + 1;
            break;
        }
    }
    if (level > nl - 1) level = nl - 1;
    return level;
}

std::pair<float,float> GuiAudio::get_peak_range(GuiWaveformLane lane,
                                                int level,
                                                int64_t start_sample,
                                                int64_t end_sample) const {
    const std::pair<float,float> empty{0.0f, 0.0f};

    // A lane value outside the enum is the display-only safe return an
    // invalid channel used to take: the empty pair, never a read.
    const int lane_idx = static_cast<int>(lane);
    if (lane_idx < 0 || lane_idx >= kLaneCount) return empty;
    if (start_sample < 0) start_sample = 0;
    if (end_sample > total_frames_) end_sample = total_frames_;
    if (end_sample <= start_sample) return empty;

    if (level <= 0) {
        // The lane's own int16 signal, exactly total_frames_ long whenever
        // total_frames_ is positive (the clamp above keeps every index inside
        // it). Level 0 therefore reads the same lattice the cached levels fold
        // from, for every lane alike — the Sum lane included, whose raw
        // extremes were once the float buffer's exact values; the 1/32767 step
        // is far below a pixel at any plate height.
        const std::vector<int16_t>& sig = lane_signals_[static_cast<size_t>(lane_idx)];
        int16_t qlo = sig[static_cast<size_t>(start_sample)];
        int16_t qhi = qlo;
        for (int64_t s = start_sample + 1; s < end_sample; s++) {
            const int16_t v = sig[static_cast<size_t>(s)];
            if (v < qlo) qlo = v;
            if (v > qhi) qhi = v;
        }
        return { dequantize_i16(qlo), dequantize_i16(qhi) };
    }

    // level >= 1 picks cache level (level - 1); anything beyond the top
    // cached level clamps to it.
    int cache_idx = level - 1;
    if (cache_idx >= kNumLevels) cache_idx = kNumLevels - 1;

    const auto&   data    = levels_[cache_idx];
    if (data.pair_count <= 0) return empty;
    const int64_t stride  = data.stride;
    const auto&   pairs   = data.pairs[static_cast<size_t>(lane_idx)];
    int64_t i0 = start_sample / stride;
    int64_t i1 = (end_sample + stride - 1) / stride;
    if (i1 > data.pair_count) i1 = data.pair_count;
    if (i1 <= i0) return empty;

    int16_t qlo = pairs[static_cast<size_t>(2 * i0)];
    int16_t qhi = pairs[static_cast<size_t>(2 * i0 + 1)];
    for (int64_t i = i0 + 1; i < i1; i++) {
        const int16_t a = pairs[static_cast<size_t>(2 * i)];
        const int16_t b = pairs[static_cast<size_t>(2 * i + 1)];
        if (a < qlo) qlo = a;
        if (b > qhi) qhi = b;
    }
    return { dequantize_i16(qlo), dequantize_i16(qhi) };
}

bool is_peaks_cache_path(const std::string& path) {
    return lowercase(std::filesystem::path(path).extension().string()) == ".peaks";
}
