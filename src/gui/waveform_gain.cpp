#include "waveform_gain.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

// --- the column: the caller's, not chosen here ----------------------------
// The measure reads the picture at WORKING ZOOM, the placement-instrument
// zoom, so its column is the device's WORKING COLUMN in source frames,
// nearbyint(2.4 s × rate ÷ the held display width) (working_column_frames,
// app_state.h, its one owner), handed in by GuiAudio::load: 46 on the
// tablet's 2304 px at 44.1 kHz, 50 at 48 kHz (the constants below were
// measured at the laptop's 1920 px column, 55 at 44.1 kHz). At
// the working zoom in source view a plate column then covers exactly one
// analysis column, the plate's columns standing on the column's own
// multiples (the frame-0 lattice, render.h's THE PLATE'S BARS).

// --- the fixed constants -----------------------------------------------------
// THE ANALYSIS HOP is resolution only, and it is the EBU short-term
// loudness's 100 ms, taken as a whole number of columns (96 of the tablet's
// 46 frames at 44.1 kHz, 4416 frames). Measured
// against a 10 ms hop at the laptop's working zoom (55 frames per pixel,
// where 0.1 s is an 80 px linear segment) over the 40th's first movement, the
// two curves differed by 0.028 dB mean, 0.21 dB at p99, 1.47 dB at the
// maximum, 0.10 % of columns over 0.5 dB — indistinguishable, so the coarser
// hop stands (architect 2026-09-23, measured on the peak measure, whose steps
// were the sharper case).
constexpr double kStepSeconds = 0.1;

// No window is audible anywhere: x8 flat (kMinFraction's LEAVE AS IS ruling
// below), clamped by the cap like any other gain.
constexpr double kSilentGain = 8.0;

// --- the constants' reasons --------------------------------------------------
// The ten picture values are HARD-CODED (architect: every project shares
// one frame of reference, so they are set once and left; a retune is a
// recompile, by design — waveform_gain.h). The leveler's five and the
// expander's two were device-config keys, `waveform_gain_*` and
// `waveform_expander_*`, for a tuning phase closed 2026-09-24; the
// compressor's two were `waveform_compressor_threshold_db` and
// `waveform_compressor_ratio`, the phase closed 2026-09-25 and a second one
// closed 2026-09-27 beside the foreground gain's
// `waveform_foreground_gain_db`.
//
// kWindowSeconds 3.0: the EBU short-term loudness length, chosen by
// eye (architect 2026-09-23) for the contrast it gives and for the earlier
// dip before a forte. The 1.5 s that stood before it was the marker regime's
// "finest window whose exposition and repeat still agree", a criterion that
// died with the quantizer. Measured 2026-09-23 on the 40th's first movement
// (on the peak measure): 3 s halves the curve's reversal rate against 1.5 s
// (8 against 16 per minute over a quarter doubling) and moves the first
// tutti's drop about half a second earlier, with the section medians
// unchanged. The earlier dip is musically right: the composer naturally
// writes a dip before a tutti, mastering engineers drop about half a dB
// before a loud section, and the material just before a tutti carries less
// meaning. The window's shape — the half-window ramp on each side of a loud
// entry, the lone accent's halo — is at the header.
constexpr double kWindowSeconds = 3.0;

// kTargetDb -14: measured 2026-09-24 on the 40th's first and second movements,
// each window's peak sits 14.3 / 14.8 dB above its own short-term RMS at the
// median (a 4 dB spread p10..p90), so -14 puts the typical window's peak at the
// lane edge and reproduces the peak leveler's average picture (transient windows
// overshoot, by over 3 dB on one hop in fifteen, clipped flat by the clamp;
// smooth ones sit a little under). It is the streaming standard this 1972
// recording's 2024 mastering was made for (his reading; no coincidence), as his
// material is, or a little under. CLOSED AS A CONSTANT (architect 2026-09-30):
// 406 sources over seven masterings put each mastering's implied target within
// 1.7 dB of it while one album's crest spread reaches 4.1 dB, so no per-piece
// derivation beats it short of grouping a whole set, which the app does not do.
constexpr double kTargetDb = -14.0;

// kGateDb -50: columns whose peak is under it are silence or
// tape hiss (the architect's old gates ran -40..-50), and they are left out of
// the window's mean so a rest does not lower the loudness of the notes around
// it.
constexpr double kGateDb = -50.0;

// kMinFraction 0.25: a window with less audible material than
// this share is silence. Ruled LEAVE AS IS (architect 2026-09-23), together
// with the gate above and the x8 all-silent fallback above: the architect
// never works on a short excerpt or on a file under -50 dBFS, and his oldest
// usable recordings (1970s tape remastered in the 2020s) pass the hiss gate.
// No further criterion.
constexpr double kMinFraction = 0.25;

// kGainMax 16, the cap: four doublings, the old level ladder's top.
constexpr double kGainMax = 16.0;

// THE FLOOR, kGainMin 1: it forbids attenuation. A window louder than
// the target takes x1 rather than being pulled under its own level — its raw
// samples cannot exceed the edge — and every quieter window is raised to the
// target.
constexpr double kGainMin = 1.0;

static_assert(kGainMax >= kGainMin, "the clamp's bounds are ordered");

// THE EXPANDER'S TWO (waveform_gain.h owns the stage), architect 2026-09-24,
// on the leveled scale — dB of a column's leveled peak under the lane edge:
//
// kExpanderThresholdDb -8: the sustain between notes sits at -5..-7 on the
// leveled scale and the quiet passages at -10 and below. The architect
// weighed -4, -3 and -2 against it and returned to -8 with the ratio at 2,
// because it stays closest to the raw picture while accentuating the peaks;
// the higher thresholds made the waveform "emaciated".
constexpr double kExpanderThresholdDb = -8.0;

// kExpanderRatio 2: the ratio the criterion is stated at (the swell before an
// onset roughly doubles). Over 1 by construction: at 1 the curve would be the
// identity, and a zero peak's minus infinity would meet a zero slope.
constexpr double kExpanderRatio = 2.0;
static_assert(kExpanderRatio > 1.0, "the expander reduces under the threshold");

// THE COMPRESSOR'S TWO (waveform_gain.h owns the stage), architect
// 2026-09-25, settled by eye on the laptop at the close of their tuning phase
// when the inner was a solid bar, and reconfirmed by eye 2026-09-27 at the
// close of a second phase with the inner drawn as an outline over the
// outer's ink; on the leveler's own window loudness L in dBFS. THE CRITERION: the gap
// between the inner and the outer bar >= 4.5 dB at the loudest 5 % of hops,
// the quiet parts untouched. The measured map (the product's leveler and
// expander re-derived over the three K550 movements, 2026-09-25, at the
// laptop's 55-frame working column, which the device-relative map of
// 2026-09-27 leaves unchanged, so the map stands byte for byte there; the inner's
// height in lane halves at L's p5 / p25 / p50 / p75 / p95, movement I; all at
// unit gain, before the foreground gain below — the settled half lowers every
// inner height by half and widens every gap by 6.02 dB):
//
//   T -20 R 4   gap at p95 4.7 / 5.0 / 5.2 dB   .07 .11 .21 .38 .39
//   T -22 R 3              5.6 / 6.0 / 6.3      .07 .11 .18 .34 .35
//   T -24 R 2              5.4 / 6.0 / 6.4      .07 .11 .17 .33 .36
//   T -26 R 2              6.4 / 7.0 / 7.4      .07 .11 .15 .30 .32
//   T -26 R 3              8.3 / 8.7 / 9.0      .07 .11 .13 .25 .26
//
// and the pairings T -20 with R <= 3 and T -22 with R 2 break the criterion
// (the outer's fringe returns in the crescendos). In this material the
// leveler never clamps to x1 (L's p95 is -14.8 / -16.1 / -16.7 dBFS), so the
// stage works over L in [-35, -15].
//
// kCompressorThresholdDb -24: with the ratio below, the threshold that holds
// the criterion in all three movements with margin (the smallest gap on any
// open column 4.1 / 4.5 / 4.8 dB, at the crescendo just over T), the inner
// reading pp 0.07, piano 0.11 and a tutti a third of the lane — a monotone
// five-fold span.
constexpr double kCompressorThresholdDb = -24.0;

// kCompressorRatio 2: the gentlest ratio that holds the criterion at that
// threshold; a steeper one shrinks the tuttis ("tiny" at -26 / 3). At least 1
// by construction: 1 would be the identity.
constexpr double kCompressorRatio = 2.0;
static_assert(kCompressorRatio >= 1.0, "the compressor never expands");

// THE FOREGROUND GAIN (waveform_gain.h owns the stage), architect 2026-09-27,
// settled by eye on the laptop at the close of the inner bar's second tuning
// phase: a FLAT LINEAR multiplier on the inner bar alone, folded into every
// hop's inner scale. Drawn as an outline over the outer's own ink, the inner
// at unit gain sat too close to the lane top in the tuttis; the architect
// picked -6.02 dB by eye and asked for the "actual half", so it is written as
// exactly 0.5 and never as a decibel power — the multiply below is exact.
// Under 1, so the inner's scale (c x 1/2 <= 1/2 < 1 <= g) never exceeds the
// outer's — the inner never stands out of the outer in height about the
// centre row (render_waveform_columns' declaration records the one-sided column) —
// and, the raw peaks being at most 1 and the compressor's scale and the
// expander's multiplier at most 1, no inner tip reaches full scale: the inner
// bar is never clipped at the lane's edge, which is why its outline has no
// open end (render_waveform_columns).
constexpr double kForegroundGain = 0.5;
static_assert(kForegroundGain > 0.0 && kForegroundGain < 1.0 &&
                  kForegroundGain <= kGainMin,
              "the inner's scale stays under the outer's, and no inner tip "
              "reaches full scale");

// THE CURVE (waveform_gain.h, THE EXPANDER): the reduction in dB, >= 0 and
// uncapped, for a column whose leveled peak reads `x` dB — 0 at or above the
// threshold, (ratio - 1) dB per dB under it. `x` may be minus infinity (a
// zero peak), which returns plus infinity, the multiplier 0.
double expander_reduction_db(double x) {
    if (x >= kExpanderThresholdDb) return 0.0;
    return (kExpanderRatio - 1.0) * (kExpanderThresholdDb - x);
}

// Reads the column pass's two arrays in place; they outlive it (the
// expander reads the peaks again after the gain is known).
class Analysis {
public:
    Analysis(const std::vector<double>& peaks, const std::vector<double>& mean_squares,
             double gate, double min_fraction)
        : peak_(peaks), ms_(mean_squares),
          n_(static_cast<int64_t>(peak_.size())), gate_(gate), min_fraction_(min_fraction) {}

    // THE SHORT-TERM LOUDNESS of columns [lo, hi) in dBFS (L): 10 log10 of the
    // mean of the AUDIBLE columns' mean squares (a column is audible when its
    // peak is over the gate), in one pass — or nothing when too little of the
    // window is audible. The slice clamps to the song; the audibility
    // threshold reads the UNCLAMPED width, so a window hanging off either end
    // needs as much audible material as a whole one.
    std::optional<double> short_term_loudness(int64_t lo, int64_t hi) const {
        const int64_t b = std::max<int64_t>(0, lo);
        const int64_t e = std::min<int64_t>(n_, hi);
        int64_t audible = 0;
        double  sum_ms  = 0.0;
        for (int64_t i = b; i < e; ++i) {
            if (peak_[static_cast<size_t>(i)] > gate_) {
                ++audible;
                sum_ms += ms_[static_cast<size_t>(i)];
            }
        }
        const int64_t need = std::max<int64_t>(
            1, static_cast<int64_t>(static_cast<double>(hi - lo) * min_fraction_));
        if (audible < need) return std::nullopt;
        // An audible column's peak is over the gate (> 0), so its mean square
        // is positive; the floor only keeps the log finite.
        const double mean = sum_ms / static_cast<double>(audible);
        return 10 * std::log10(std::max(mean, 1e-12));
    }

private:
    const std::vector<double>& peak_;
    const std::vector<double>& ms_;
    int64_t             n_;
    double              gate_;
    double              min_fraction_;
};

// THE LIT SUMMARY'S ONE STORED NUMBER (waveform_gain.h owns the summary):
// a raw sample extreme times a column scale, in double, clamped to [-1, 1]
// (the producer's magnified_tip, render_waveform_columns), then quantized as
// the peak pyramid quantizes (audio.cpp's quantize_unit: nearbyint of
// v x 32767, in double).
int16_t lit_quantized(float raw, double scale) {
    double v = static_cast<double>(raw) * scale;
    if (v < -1.0) v = -1.0;
    if (v >  1.0) v =  1.0;
    return static_cast<int16_t>(std::nearbyint(v * 32767.0));
}

// THE LIT SUMMARY's build (waveform_gain.h owns the structure): the base
// from each working column's per-channel raw extremes (`lo`, `hi`, the
// column pass's) times the column's two scales, then the factor-4 levels,
// each entry the hull of the existing entries under it.
WaveformLitSummary build_lit_summary(const WaveformGainCurve& curve,
                                     const std::vector<float> (&lo)[2],
                                     const std::vector<float> (&hi)[2]) {
    WaveformLitSummary s;
    const int64_t n = static_cast<int64_t>(lo[0].size());
    s.level_begin.push_back(0);
    for (int64_t size = n;;) {
        s.level_begin.push_back(s.level_begin.back() + size);
        if (size <= 1) break;
        size = (size + kWaveformLitSummaryFactor - 1) / kWaveformLitSummaryFactor;
    }
    for (std::vector<WaveformLitEntry>& e : s.entries)
        e.resize(static_cast<size_t>(s.level_begin.back()));
    for (int64_t k = 0; k < n; ++k) {
        const WaveformColumnScales sc = waveform_column_scales(curve, k);
        for (int ch = 0; ch < 2; ++ch) {
            const float mn = lo[ch][static_cast<size_t>(k)];
            const float mx = hi[ch][static_cast<size_t>(k)];
            s.entries[ch][static_cast<size_t>(k)] = WaveformLitEntry{
                lit_quantized(mn, sc.outer), lit_quantized(mx, sc.outer),
                lit_quantized(mn, sc.inner), lit_quantized(mx, sc.inner)};
        }
    }
    const size_t levels = s.level_begin.size() - 1;
    for (std::vector<WaveformLitEntry>& e : s.entries) {
        for (size_t j = 1; j < levels; ++j) {
            const int64_t p0 = s.level_begin[j - 1], p1 = s.level_begin[j];
            const int64_t c1 = s.level_begin[j + 1];
            for (int64_t i = 0; p1 + i < c1; ++i) {
                const int64_t first = p0 + kWaveformLitSummaryFactor * i;
                const int64_t last  = std::min(p1, first + kWaveformLitSummaryFactor);
                WaveformLitEntry h = e[static_cast<size_t>(first)];
                for (int64_t c = first + 1; c < last; ++c) {
                    const WaveformLitEntry& x = e[static_cast<size_t>(c)];
                    h.outer_min = std::min(h.outer_min, x.outer_min);
                    h.outer_max = std::max(h.outer_max, x.outer_max);
                    h.inner_min = std::min(h.inner_min, x.inner_min);
                    h.inner_max = std::max(h.inner_max, x.inner_max);
                }
                e[static_cast<size_t>(p1 + i)] = h;
            }
        }
    }
    return s;
}

}  // namespace

WaveformGainCurve derive_waveform_gain(const float* interleaved, int64_t total_frames,
                                       int sample_rate, int64_t column_frames) {
    if (total_frames <= 0) return {};

    const int64_t col = column_frames;
    assert(col >= 1);

    // THE COLUMN PASS, one read of every sample: each column of `col` frames
    // (the last partial) takes its PEAK, the largest |x| over both channels,
    // and its MEAN SQUARE, the sum of x^2 over its samples (both channels
    // together, accumulated in double) divided by their count — and, for
    // THE LIT SUMMARY (waveform_gain.h), each channel's least and greatest
    // sample, kept beside them until the summary is built.
    const int64_t n = (total_frames + col - 1) / col;
    std::vector<double> peaks(static_cast<size_t>(n));
    std::vector<double> mean_squares(static_cast<size_t>(n));
    std::vector<float> raw_lo[2], raw_hi[2];
    for (int ch = 0; ch < 2; ++ch) {
        raw_lo[ch].resize(static_cast<size_t>(n));
        raw_hi[ch].resize(static_cast<size_t>(n));
    }
    for (int64_t k = 0; k < n; ++k) {
        const int64_t f0 = k * col;
        const int64_t f1 = std::min(total_frames, f0 + col);
        float  peak = 0.0f;
        double sq   = 0.0;
        float  lo[2] = { std::numeric_limits<float>::infinity(),
                         std::numeric_limits<float>::infinity()};
        float  hi[2] = {-std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity()};
        int    ch = 0;   // the interleaved channel of *p
        for (const float* p = interleaved + 2 * f0, *pe = interleaved + 2 * f1; p < pe; ++p) {
            const float a = std::fabs(*p);
            if (a > peak) peak = a;
            const double x = static_cast<double>(*p);
            sq += x * x;
            if (*p < lo[ch]) lo[ch] = *p;
            if (*p > hi[ch]) hi[ch] = *p;
            ch ^= 1;
        }
        peaks[static_cast<size_t>(k)]        = static_cast<double>(peak);
        mean_squares[static_cast<size_t>(k)] = sq / static_cast<double>(2 * (f1 - f0));
        for (int c = 0; c < 2; ++c) {
            raw_lo[c][static_cast<size_t>(k)] = lo[c];
            raw_hi[c][static_cast<size_t>(k)] = hi[c];
        }
    }

    const double cps = static_cast<double>(sample_rate) / static_cast<double>(col);
    const int64_t st = static_cast<int64_t>(std::nearbyint(cps * kStepSeconds));
    assert(st >= 1);
    const double gate = std::pow(10.0, kGateDb / 20);

    const Analysis an(peaks, mean_squares, gate, kMinFraction);

    // THE MEASURE at the analysis hop: each hop's L, or nothing where too
    // little of its window is audible.
    std::vector<std::optional<double>> measured;
    std::vector<int64_t> known;
    {
        const int64_t hw = static_cast<int64_t>(kWindowSeconds * cps / 2);
        for (int64_t i = 0; i < n; i += st) {
            measured.push_back(an.short_term_loudness(i - hw, i + hw));
            if (measured.back()) known.push_back(static_cast<int64_t>(measured.size()) - 1);
        }
    }

    WaveformGainCurve out;
    out.hop_frames = st * col;
    out.gain.resize(measured.size());
    out.inner_scale.resize(measured.size());
    if (known.empty()) {
        // No window is audible anywhere: x8 flat, and the compressor, having
        // no L over any threshold, reduces nothing — the inner scale is the
        // foreground gain alone.
        std::fill(out.gain.begin(), out.gain.end(), std::clamp(kSilentGain, kGainMin, kGainMax));
        std::fill(out.inner_scale.begin(), out.inner_scale.end(), kForegroundGain);
    } else {
        // THE COMPRESSOR'S SLOPE (waveform_gain.h): (1 - 1/R) dB of reduction
        // per dB of L over the threshold; 0 at R = 1, the identity.
        const double slope = 1.0 - 1.0 / kCompressorRatio;
        for (size_t k = 0; k < measured.size(); ++k) {
            // A silent point takes the NEARER known point's L, the earlier on
            // a tie (the retained detector's rule) — and so its gain AND its
            // compressor scale, both read from the one picked L. The
            // experiment's reference script interpolated between the known
            // hops instead; the difference is intentional and visible only on
            // gated-but-nonzero material, true silence painting nothing at any
            // gain.
            size_t pick = k;
            if (!measured[k]) {
                const int64_t kk = static_cast<int64_t>(k);
                const size_t j = static_cast<size_t>(
                    std::lower_bound(known.begin(), known.end(), kk) - known.begin());
                if (j == 0) {
                    pick = static_cast<size_t>(known[0]);
                } else if (j == known.size()) {
                    pick = static_cast<size_t>(known[j - 1]);
                } else {
                    const int64_t before = known[j - 1];
                    const int64_t after  = known[j];
                    pick = static_cast<size_t>((after - kk < kk - before) ? after : before);
                }
            }
            const double level = *measured[pick];
            // THE GAIN, g = 10^((target - L) / 20), then THE CLAMP to
            // [kGainMin, kGainMax]: never attenuated, never past the cap.
            out.gain[k] = std::clamp(std::pow(10.0, (kTargetDb - level) / 20),
                                     kGainMin, kGainMax);
            // THE COMPRESSOR on the UNCLAMPED L (waveform_gain.h): nothing at
            // or under the threshold (10^0 is exactly 1), the slope over it;
            // then THE FOREGROUND GAIN, the exact half.
            const double reduction =
                level > kCompressorThresholdDb ? slope * (level - kCompressorThresholdDb)
                                               : 0.0;
            out.inner_scale[k] = std::pow(10.0, -reduction / 20) * kForegroundGain;
        }
    }

    // THE EXPANDER, after the leveler (waveform_gain.h owns the stage): a
    // static curve on each working column's leveled peak in dB, no state
    // carried from one column to the next.
    out.column_frames = col;
    out.expander_multiplier.resize(static_cast<size_t>(n));
    for (int64_t k = 0; k < n; ++k) {
        const int64_t f0 = k * col;
        const int64_t f1 = std::min(total_frames, f0 + col);
        const double leveled =
            peaks[static_cast<size_t>(k)] * waveform_gain_at(out, (f0 + f1) / 2);
        const double x = leveled > 0.0 ? 20 * std::log10(leveled)
                                       : -std::numeric_limits<double>::infinity();
        // 10^0 is exactly 1, so an open column's multiplier is exact, and
        // 10^-inf is exactly 0.
        out.expander_multiplier[static_cast<size_t>(k)] =
            static_cast<float>(std::pow(10.0, -expander_reduction_db(x) / 20));
    }
    // THE LIT SUMMARY, after the expander (the column's scales read it).
    out.lit_summary = build_lit_summary(out, raw_lo, raw_hi);
    return out;
}

namespace {

// THE PER-HOP READ shared by the two per-hop arrays: linear between the two
// nearest hops, the first and last value held beyond the ends, 1.0 for an
// empty array.
double per_hop_at(const std::vector<double>& v, int64_t hop_frames, int64_t frame) {
    if (v.empty() || hop_frames <= 0) return 1.0;
    if (frame <= 0) return v.front();
    const int64_t k = frame / hop_frames;
    if (k >= static_cast<int64_t>(v.size()) - 1) return v.back();
    const double a = v[static_cast<size_t>(k)];
    const double b = v[static_cast<size_t>(k) + 1];
    const double t = static_cast<double>(frame - k * hop_frames) /
                     static_cast<double>(hop_frames);
    return a + (b - a) * t;
}

}  // namespace

double waveform_gain_at(const WaveformGainCurve& curve, int64_t frame) {
    return per_hop_at(curve.gain, curve.hop_frames, frame);
}

double waveform_inner_scale_at(const WaveformGainCurve& curve, int64_t frame) {
    return per_hop_at(curve.inner_scale, curve.hop_frames, frame);
}

WaveformColumnScales waveform_column_scales(const WaveformGainCurve& curve, int64_t k) {
    assert(k >= 0 && k < static_cast<int64_t>(curve.expander_multiplier.size()));
    const int64_t col    = curve.column_frames;
    const int64_t centre = k * col + col / 2;
    const double  e      = static_cast<double>(curve.expander_multiplier[static_cast<size_t>(k)]);
    return WaveformColumnScales{waveform_gain_at(curve, centre) * e,
                                waveform_inner_scale_at(curve, centre) * e};
}

WaveformLitHull waveform_lit_summary_hull(const WaveformGainCurve& curve, int channel,
                                          int64_t ka, int64_t kb) {
    const WaveformLitSummary& s = curve.lit_summary;
    assert(channel == 0 || channel == 1);
    assert(s.level_begin.size() >= 2 && 0 <= ka && ka <= kb && kb < s.level_begin[1]);
    const WaveformLitEntry* const e = s.entries[channel].data();
    int16_t omin = std::numeric_limits<int16_t>::max();
    int16_t omax = std::numeric_limits<int16_t>::min();
    int16_t imin = std::numeric_limits<int16_t>::max();
    int16_t imax = std::numeric_limits<int16_t>::min();
    const auto take = [&](const WaveformLitEntry& x) {
        omin = std::min(omin, x.outer_min);
        omax = std::max(omax, x.outer_max);
        imin = std::min(imin, x.inner_min);
        imax = std::max(imax, x.inner_max);
    };
    // BOTTOM-UP over the run [l, r) of level j: its unaligned ends taken one
    // entry at a time (at most F - 1 a side), the aligned middle handed up as
    // the next level's [l / F, r / F) — whose entries hold exactly F of
    // these each, r being at most this level's size (the head's exactness).
    constexpr int64_t F = kWaveformLitSummaryFactor;
    int64_t l = ka;
    int64_t r = kb + 1;
    for (size_t j = 0; l < r; ++j) {
        assert(j + 1 < s.level_begin.size());
        const WaveformLitEntry* const level = e + s.level_begin[j];
        while (l % F != 0 && l < r) take(level[l++]);
        while (r % F != 0 && l < r) take(level[--r]);
        l /= F;
        r /= F;
    }
    return WaveformLitHull{static_cast<double>(omin) / 32767.0,
                           static_cast<double>(omax) / 32767.0,
                           static_cast<double>(imin) / 32767.0,
                           static_cast<double>(imax) / 32767.0};
}
