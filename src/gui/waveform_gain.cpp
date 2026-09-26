#include "waveform_gain.h"

// app_state.h is included here and not in the header for ONE reason: it owns
// the zoom map's two constants, and the column width below is derived from
// them rather than restated. Nothing else is read from
// it; the derivation touches no application state.
#include "app_state.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

// --- the column: derived, not chosen ---------------------------------------
// The measure reads the picture at WORKING ZOOM, the placement-instrument
// zoom, so its column is the working zoom's column in source frames:
//   ms_per_px = kZoomBaseMsPerPx * 2^(kWorkingZoomLevel - 1)   (1.25 ms)
//   COL       = floor(ms_per_px * sample_rate / 1000)
// evaluated in that order: 1.25 * 44100 = 55125 exactly, / 1000 = 55.125
// exactly, floor 55; at 48 kHz, 60.
static_assert(kWorkingZoomLevel == static_cast<double>(static_cast<int>(kWorkingZoomLevel)),
              "the working column's power of two is taken over a whole level");

constexpr double working_zoom_ms_per_px() {
    double ms = kZoomBaseMsPerPx;
    for (int step = 1; step < static_cast<int>(kWorkingZoomLevel); ++step) ms *= 2.0;
    return ms;
}

// Truncation is the floor here: every operand is positive.
constexpr int64_t working_zoom_column_frames(int sample_rate) {
    return static_cast<int64_t>(working_zoom_ms_per_px() * sample_rate / 1000.0);
}
static_assert(working_zoom_column_frames(44100) == 55, "55 source frames per column at 44.1 kHz");
static_assert(working_zoom_column_frames(48000) == 60, "60 source frames per column at 48 kHz");

// --- the fixed constants -----------------------------------------------------
// THE ANALYSIS HOP is resolution only, and it is the EBU short-term
// loudness's 100 ms. Measured against a 10 ms hop at working zoom (1.25 ms/px,
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
// The seven picture values, HARD-CODED 2026-09-24 (architect: every project
// shares one frame of reference, so they are set once and left; a retune is a
// recompile, by design — waveform_gain.h). Until then they were device-config
// keys, `waveform_gain_*` and `waveform_expander_*`, for a tuning phase.
// The compressor's two numbers are not here: they are in their own tuning
// phase and arrive as the derivation's WaveformCompressorParams (the stage and
// the defaults are waveform_gain.h's).
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

// kTargetDb -14: measured 2026-09-24 on the 40th's first and
// second movements, each window's peak against the same window's short-term
// RMS on the samples, the peak sits 14.3 / 14.8 dB above the RMS at the
// median with a 4 dB spread from p10 to p90 — so -14 puts the typical
// window's peak at the lane edge and reproduces the peak leveler's average
// picture, transient-rich windows overshooting the edge (by more than 3 dB on
// about one hop in fifteen, clipped flat by the clamp) and smooth ones
// sitting a little under it. It is also the streaming loudness standard the
// 2024 mastering of this 1972 recording was made for (the architect's
// reading; not a coincidence), and the material he brings in is expected to
// be mastered to it or slightly lower.
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

}  // namespace

WaveformGainCurve derive_waveform_gain(const float* interleaved, int64_t total_frames,
                                       int sample_rate,
                                       const WaveformCompressorParams& compressor) {
    if (total_frames <= 0) return {};

    const int64_t col = working_zoom_column_frames(sample_rate);
    assert(col >= 1);

    // THE COLUMN PASS, one read of every sample: each column of `col` frames
    // (the last partial) takes its PEAK, the largest |x| over both channels,
    // and its MEAN SQUARE, the sum of x^2 over its samples (both channels
    // together, accumulated in double) divided by their count.
    const int64_t n = (total_frames + col - 1) / col;
    std::vector<double> peaks(static_cast<size_t>(n));
    std::vector<double> mean_squares(static_cast<size_t>(n));
    for (int64_t k = 0; k < n; ++k) {
        const int64_t f0 = k * col;
        const int64_t f1 = std::min(total_frames, f0 + col);
        float  peak = 0.0f;
        double sq   = 0.0;
        for (const float* p = interleaved + 2 * f0, *pe = interleaved + 2 * f1; p < pe; ++p) {
            const float a = std::fabs(*p);
            if (a > peak) peak = a;
            const double x = static_cast<double>(*p);
            sq += x * x;
        }
        peaks[static_cast<size_t>(k)]        = static_cast<double>(peak);
        mean_squares[static_cast<size_t>(k)] = sq / static_cast<double>(2 * (f1 - f0));
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
        // no L over any threshold, reduces nothing.
        std::fill(out.gain.begin(), out.gain.end(), std::clamp(kSilentGain, kGainMin, kGainMax));
        std::fill(out.inner_scale.begin(), out.inner_scale.end(), 1.0);
    } else {
        // THE COMPRESSOR'S SLOPE (waveform_gain.h): (1 - 1/R) dB of reduction
        // per dB of L over the threshold; 0 at R = 1, the identity.
        const double slope = 1.0 - 1.0 / compressor.ratio;
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
            // or under the threshold (10^0 is exactly 1), the slope over it.
            const double reduction =
                level > compressor.threshold_db ? slope * (level - compressor.threshold_db)
                                                : 0.0;
            out.inner_scale[k] = std::pow(10.0, -reduction / 20);
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
    return out;
}

double waveform_gain_at(const WaveformGainCurve& curve, int64_t frame) {
    const std::vector<double>& g = curve.gain;
    if (g.empty() || curve.hop_frames <= 0) return 1.0;
    if (frame <= 0) return g.front();
    const int64_t k = frame / curve.hop_frames;
    if (k >= static_cast<int64_t>(g.size()) - 1) return g.back();
    const double a = g[static_cast<size_t>(k)];
    const double b = g[static_cast<size_t>(k) + 1];
    const double t = static_cast<double>(frame - k * curve.hop_frames) /
                     static_cast<double>(curve.hop_frames);
    return a + (b - a) * t;
}

double waveform_inner_scale_at(const WaveformGainCurve& curve, int64_t frame) {
    const std::vector<double>& c = curve.inner_scale;
    if (c.empty() || curve.hop_frames <= 0) return 1.0;
    if (frame <= 0) return c.front();
    const int64_t k = frame / curve.hop_frames;
    if (k >= static_cast<int64_t>(c.size()) - 1) return c.back();
    const double a = c[static_cast<size_t>(k)];
    const double b = c[static_cast<size_t>(k) + 1];
    const double t = static_cast<double>(frame - k * curve.hop_frames) /
                     static_cast<double>(curve.hop_frames);
    return a + (b - a) * t;
}

float waveform_expander_multiplier_over(const WaveformGainCurve& curve, int64_t s0, int64_t s1) {
    const std::vector<float>& m = curve.expander_multiplier;
    if (m.empty() || curve.column_frames <= 0) return 1.0f;
    const int64_t last = static_cast<int64_t>(m.size()) - 1;
    const int64_t k0 = std::clamp<int64_t>(s0 / curve.column_frames, 0, last);
    const int64_t k1 = std::clamp<int64_t>((std::max(s1, s0 + 1) - 1) / curve.column_frames, k0, last);
    float best = m[static_cast<size_t>(k0)];
    for (int64_t k = k0 + 1; k <= k1; ++k) best = std::max(best, m[static_cast<size_t>(k)]);
    return best;
}
