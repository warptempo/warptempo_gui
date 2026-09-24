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
constexpr double kLevelDb = 6.02;  // one doubling of the picture, in dB
// THE ANALYSIS HOP is resolution only. Measured against a 10 ms hop at working
// zoom (1.25 ms/px, where 0.1 s is an 80 px linear segment) over the 40th's
// first movement, the two curves differ by 0.028 dB mean, 0.21 dB at p99,
// 1.47 dB at the maximum, 0.10 % of columns over 0.5 dB — indistinguishable,
// so the coarser hop stands (architect 2026-09-23).
constexpr double kStepSeconds = 0.1;

// --- the defaults' reasons ---------------------------------------------------
// The seven tunables are the device config's (WaveformGainParams,
// waveform_gain.h; the grammar and brackets at device_config.h). These are the
// reasons for the DEFAULTS both first-run templates stamp.
//
// `waveform_gain_percentile` 0.90, the typical top: the loudest 10% may clip
// (convention; the architect's own hand-set levels clipped 7 to 12 % on every
// section that could reach the edge).
//
// `waveform_gain_gate_db` -50: columns under it are silence or tape hiss (the
// architect's old gates ran -40..-50).
//
// `waveform_gain_min_fraction` 0.25: a window with less audible material than
// this share is silence. Ruled LEAVE AS IS (architect 2026-09-23), together
// with the gate above and the x8 all-silent fallback below: the architect
// never works on a short excerpt or on a file under -50 dBFS, and his oldest
// usable recordings (1970s tape remastered in the 2020s) pass the hiss gate.
// No further criterion.
//
// `waveform_gain_window_s` 1.5, section scale: the finest window whose
// exposition and repeat still agree (1 s splits them; 3 s, R128's, misses
// lead-ins) — five eighths of the working-zoom screen: the reference screen,
// the authoring laptop's 1920 columns that every crop-authored pixel ruling
// reads against, at working zoom is 1.25 ms * 1920 = 2.4 s, and a time
// constant of this rule is stated as a derived multiple of it, not a free
// second. The consequence the architect accepted with it: a centred
// window's p90 reports loud once a tenth of it is loud, so the quiet before a
// loud entry fades over roughly the last quarter of a screen (~0.6 s) and the
// first quarter after it — symmetric in time, no forward look-ahead (literally
// so only up to the hop lattice, whose hops sit at multiples of the hop from
// frame 0, and the silent hop's earlier-on-a-tie choice) — and a lone accent
// halos its neighbours for about half a window on each side. Both are the
// rule's shape, not defects.
//
// `waveform_gain_threshold_db` -3 and `waveform_gain_ratio` 1.18, THE
// EXPANDER (architect 2026-09-23). Measured over the four-piece corpus
// 2026-09-23: the loud windows' tops sit at -7..-8 dBFS and the quiet ones'
// at -19..-21 on every piece. At -3 / 1.18 the tutti drops from x2.2 to x1.7,
// the quiet passages keep their gain, and the quiet-over-loud contrast rises
// from 4.1..4.7 to 5.3..6.2 — the loud body sits a little under the edge and
// the quiet passages stand further from it. Ratio 1 would be the same leveler
// aimed at -3 with the contrast unchanged; threshold 0 and ratio 1 are the
// plain leveler exactly (d = -L / 6.02).
//
// `waveform_gain_max` 16, the cap: four doublings, the old level ladder's top.
//
// THE FLOOR IS FIXED, not a key: it forbids attenuation. d >= 0 for every
// window by the expander's max(0, .), so the floor is reached only at d = 0
// and the picture is NEVER pushed below its own level — a window whose
// typical top already sits at or over the threshold takes x1, and one below
// it is raised (by ratio x 6 dB of gain per 6 dB under), not left alone.
constexpr double kGainMin = 1.0;

double db(double x) { return 20 * std::log10(std::max(x, 1e-6)); }

class Analysis {
public:
    Analysis(std::vector<double> columns, double gate, double percentile,
             double min_fraction)
        : c_(std::move(columns)), n_(static_cast<int64_t>(c_.size())), gate_(gate),
          percentile_(percentile), min_fraction_(min_fraction) {}

    // The typical top of columns [lo, hi) in dBFS (L), or nothing when too
    // little of the window is audible. The slice
    // clamps to the song; the audibility threshold reads the UNCLAMPED width,
    // so a window hanging off either end needs as much audible material as a
    // whole one.
    std::optional<double> top_level(int64_t lo, int64_t hi) {
        scratch_.clear();
        const int64_t b = std::max<int64_t>(0, lo);
        const int64_t e = std::min<int64_t>(n_, hi);
        for (int64_t i = b; i < e; ++i) {
            const double x = c_[static_cast<size_t>(i)];
            if (x > gate_) scratch_.push_back(x);
        }
        const int64_t need = std::max<int64_t>(
            1, static_cast<int64_t>(static_cast<double>(hi - lo) * min_fraction_));
        if (static_cast<int64_t>(scratch_.size()) < need) return std::nullopt;
        // p = 1.00 (the bracket's upper wall) is exact: 1.0 * (n - 1) is
        // n - 1 with no rounding, so idx names the last element and
        // nth_element seats the window's maximum there; n >= 1 here (need is
        // at least 1), so n - 1 never underflows.
        const size_t idx = static_cast<size_t>(
            percentile_ * static_cast<double>(scratch_.size() - 1));
        std::nth_element(scratch_.begin(), scratch_.begin() + static_cast<std::ptrdiff_t>(idx),
                         scratch_.end());
        return db(scratch_[idx]);
    }

private:
    std::vector<double> c_;
    int64_t             n_;
    double              gate_;
    double              percentile_;
    double              min_fraction_;
    std::vector<double> scratch_;
};

}  // namespace

WaveformGainCurve derive_waveform_gain(const float* interleaved, int64_t total_frames,
                                       int sample_rate, const WaveformGainParams& params) {
    if (total_frames <= 0) return {};

    const int64_t col = working_zoom_column_frames(sample_rate);
    assert(col >= 1);

    // The column heights: the largest |x| over both channels of each column
    // of `col` frames, the last column partial.
    const int64_t n = (total_frames + col - 1) / col;
    std::vector<double> columns(static_cast<size_t>(n));
    for (int64_t k = 0; k < n; ++k) {
        const int64_t f0 = k * col;
        const int64_t f1 = std::min(total_frames, f0 + col);
        float peak = 0.0f;
        for (const float* p = interleaved + 2 * f0, *pe = interleaved + 2 * f1; p < pe; ++p) {
            const float a = std::fabs(*p);
            if (a > peak) peak = a;
        }
        columns[static_cast<size_t>(k)] = static_cast<double>(peak);
    }

    const double cps = static_cast<double>(sample_rate) / static_cast<double>(col);
    const int64_t st = static_cast<int64_t>(std::nearbyint(cps * kStepSeconds));
    assert(st >= 1);
    const double gate = std::pow(10.0, params.gate_db / 20);

    Analysis an(std::move(columns), gate, params.percentile, params.min_fraction);

    // THE MEASURE at the analysis hop, then THE EXPANDER per hop: the
    // doublings d = max(0, ratio * (threshold - L)) / kLevelDb. With
    // threshold 0 and ratio 1 this is -L / kLevelDb exactly (0 - L is -L and
    // 1 * x is x in IEEE arithmetic, and L <= 0 for a decoded PCM peak), the
    // leveler the rule was until 2026-09-23.
    auto doublings = [&params](double level_db) {
        return std::max(0.0, params.ratio * (params.threshold_db - level_db)) / kLevelDb;
    };
    std::vector<double> coarse;
    {
        const int64_t hw = static_cast<int64_t>(params.window_s * cps / 2);
        std::vector<std::optional<double>> raw;
        std::vector<int64_t> known;
        for (int64_t i = 0; i < n; i += st) {
            const std::optional<double> level = an.top_level(i - hw, i + hw);
            raw.push_back(level ? std::optional<double>(doublings(*level)) : std::nullopt);
            if (raw.back()) known.push_back(static_cast<int64_t>(raw.size()) - 1);
        }
        coarse.resize(raw.size());
        if (known.empty()) {
            // No window is audible anywhere: x8 flat (the min_fraction
            // default's LEAVE AS IS ruling), clamped by the cap below like any
            // other d.
            std::fill(coarse.begin(), coarse.end(), 3.0);
        } else {
            // A silent point takes the NEARER known point, the earlier on a tie
            // (the retained detector's rule). The experiment's reference script
            // interpolates d between the known hops instead; the difference is
            // intentional and visible only on gated-but-nonzero material, true
            // silence painting nothing at any gain.
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

    // THE GAIN: 2^d, clamped to [kGainMin, gain_max] (gain_max >= 1 by the
    // config reader's bracket). Nothing else.
    WaveformGainCurve out;
    out.hop_frames = st * col;
    out.gain.resize(coarse.size());
    for (size_t k = 0; k < coarse.size(); ++k)
        out.gain[k] = std::clamp(std::exp2(coarse[k]), kGainMin, params.gain_max);
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
