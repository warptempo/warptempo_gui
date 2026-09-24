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

// No window is audible anywhere: x8 flat (the min_fraction default's LEAVE AS
// IS ruling below), clamped by the cap like any other gain.
constexpr double kSilentGain = 8.0;

// --- the defaults' reasons ---------------------------------------------------
// The five tunables are the device config's (WaveformGainParams,
// waveform_gain.h; the grammar and brackets at device_config.h). These are the
// reasons for the DEFAULTS both first-run templates stamp.
//
// `waveform_gain_window_s` 3.0: the EBU short-term loudness length, chosen by
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
//
// `waveform_gain_target_db` -14: measured 2026-09-24 on the 40th's first and
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
//
// `waveform_gain_gate_db` -50: columns whose peak is under it are silence or
// tape hiss (the architect's old gates ran -40..-50), and they are left out of
// the window's mean so a rest does not lower the loudness of the notes around
// it.
//
// `waveform_gain_min_fraction` 0.25: a window with less audible material than
// this share is silence. Ruled LEAVE AS IS (architect 2026-09-23), together
// with the gate above and the x8 all-silent fallback above: the architect
// never works on a short excerpt or on a file under -50 dBFS, and his oldest
// usable recordings (1970s tape remastered in the 2020s) pass the hiss gate.
// No further criterion.
//
// `waveform_gain_max` 16, the cap: four doublings, the old level ladder's top.
//
// THE FLOOR IS FIXED, not a key: it forbids attenuation. A window louder than
// the target takes x1 rather than being pulled under its own level — its raw
// samples cannot exceed the edge — and every quieter window is raised to the
// target.
constexpr double kGainMin = 1.0;

// THE EXPANDER'S DEFAULTS (WaveformExpanderParams, waveform_gain.h, which
// owns the stage; the grammar and brackets at device_config.h), architect
// 2026-09-24, one line each, on the leveled scale — dB of a column's leveled
// peak under the lane edge:
//
// `waveform_expander_threshold_db` -8: the sustain between notes sits at
// -5..-7 on the leveled scale and the quiet passages at -10 and below.
// `waveform_expander_ratio` 1.00: off for a fresh device; the architect
// tunes from 2.00.
// `waveform_expander_range_db` 40, the floor: a rest never falls below a
// visible line.
// `waveform_expander_knee_db` 4: the width of the band where the leveled
// sustain lives, his to tune.
// `waveform_expander_hold_ms` 50: the manual's ceiling is 250 and he expects
// 50-100.
// `waveform_expander_release_ms` 20: a slight fade rather than a cliff.

// THE GAIN COMPUTER (waveform_gain.h, THE EXPANDER): the target reduction in
// dB, >= 0 and capped at the Range, for a column whose leveled peak reads
// `x` dB. `x` may be minus infinity (a zero peak), which takes the full Range
// under any ratio over 1; Ratio 1 returns 0 before any arithmetic, so the
// identity is exact and never meets an infinity.
double expander_target_reduction_db(double x, const WaveformExpanderParams& e) {
    const double over = e.ratio - 1.0;
    if (over <= 0.0) return 0.0;
    const double k  = e.knee_db;
    const double hi = e.threshold_db + k / 2;
    const double lo = e.threshold_db - k / 2;
    double target;
    if (x >= hi) {
        return 0.0;
    } else if (x >= lo) {
        // The quadratic soft knee, its slope running 0 -> over (reached only
        // when k > 0: at k = 0, lo = hi and every x under hi falls below).
        target = over * (hi - x) * (hi - x) / (2 * k);
    } else {
        // The knee's value at its lower edge, over * k / 2, then over dB per
        // dB under it: the output drops `ratio` dB per dB of input.
        target = over * k / 2 + over * (lo - x);
    }
    return std::min(target, e.range_db);
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
                                       int sample_rate, const WaveformGainParams& params,
                                       const WaveformExpanderParams& expander) {
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
    const double gate = std::pow(10.0, params.gate_db / 20);

    const Analysis an(peaks, mean_squares, gate, params.min_fraction);

    // THE MEASURE at the analysis hop, then THE GAIN per hop,
    // g = 10^((target - L) / 20): the window's loudness brought to the target.
    std::vector<double> coarse;
    {
        const int64_t hw = static_cast<int64_t>(params.window_s * cps / 2);
        std::vector<std::optional<double>> raw;
        std::vector<int64_t> known;
        for (int64_t i = 0; i < n; i += st) {
            const std::optional<double> level = an.short_term_loudness(i - hw, i + hw);
            raw.push_back(level ? std::optional<double>(
                                      std::pow(10.0, (params.target_db - *level) / 20))
                                : std::nullopt);
            if (raw.back()) known.push_back(static_cast<int64_t>(raw.size()) - 1);
        }
        coarse.resize(raw.size());
        if (known.empty()) {
            std::fill(coarse.begin(), coarse.end(), kSilentGain);
        } else {
            // A silent point takes the NEARER known point, the earlier on a tie
            // (the retained detector's rule) — its L, and so its gain. The
            // experiment's reference script interpolated between the known hops
            // instead; the difference is intentional and visible only on
            // gated-but-nonzero material, true silence painting nothing at any
            // gain.
            for (size_t k = 0; k < raw.size(); ++k) {
                if (raw[k]) { coarse[k] = *raw[k]; continue; }
                const int64_t kk = static_cast<int64_t>(k);
                const size_t j = static_cast<size_t>(
                    std::lower_bound(known.begin(), known.end(), kk) - known.begin());
                int64_t pick;
                if (j == 0) {
                    pick = known[0];
                } else if (j == known.size()) {
                    pick = known[j - 1];
                } else {
                    const int64_t before = known[j - 1];
                    const int64_t after  = known[j];
                    pick = (after - kk < kk - before) ? after : before;
                }
                coarse[k] = *raw[static_cast<size_t>(pick)];
            }
        }
    }

    // THE CLAMP to [kGainMin, gain_max] (gain_max >= 1 by the config reader's
    // bracket): never attenuated, never past the cap. Nothing else.
    WaveformGainCurve out;
    out.hop_frames = st * col;
    out.gain.resize(coarse.size());
    for (size_t k = 0; k < coarse.size(); ++k)
        out.gain[k] = std::clamp(coarse[k], kGainMin, params.gain_max);

    // THE EXPANDER, after the leveler (waveform_gain.h owns the stage): one
    // walk over the working columns in time order. Each column's level is its
    // leveled peak in dB, its target the gain computer's; ATTACK 0 opens the
    // gate the instant a column exceeds the threshold and re-arms the HOLD;
    // once the hold has run out, the RELEASE walks `red` toward the target at
    // Range / release_ms dB per ms, never past it, and a target under `red`
    // takes it at once. LOOKAHEAD is off: nothing here reads ahead.
    out.column_frames = col;
    out.expander_multiplier.resize(static_cast<size_t>(n));
    {
        const int64_t hold_cols =
            static_cast<int64_t>(std::nearbyint(expander.hold_ms * cps / 1000));
        // dB per column; release_ms 0 is the target at once.
        const double release_step =
            expander.release_ms > 0.0
                ? expander.range_db / (expander.release_ms * cps / 1000)
                : std::numeric_limits<double>::infinity();
        double  red  = 0.0;
        int64_t hold = 0;
        for (int64_t k = 0; k < n; ++k) {
            const int64_t f0 = k * col;
            const int64_t f1 = std::min(total_frames, f0 + col);
            const double leveled =
                peaks[static_cast<size_t>(k)] * waveform_gain_at(out, (f0 + f1) / 2);
            const double x = leveled > 0.0 ? 20 * std::log10(leveled)
                                           : -std::numeric_limits<double>::infinity();
            const double target = expander_target_reduction_db(x, expander);
            if (x > expander.threshold_db) {
                red  = 0.0;
                hold = hold_cols;
            } else if (hold > 0) {
                red = 0.0;
                --hold;
            } else if (red >= target) {
                red = target;
            } else {
                red = std::min(target, red + release_step);
            }
            // 10^0 is exactly 1, so an open column's multiplier is exact.
            out.expander_multiplier[static_cast<size_t>(k)] =
                static_cast<float>(std::pow(10.0, -red / 20));
        }
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
