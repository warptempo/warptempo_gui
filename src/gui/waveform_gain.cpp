#include "waveform_gain.h"

// app_state.h is included here and not in the header for ONE reason: it owns
// the zoom map's two constants, and the column width and the reference screen
// below are derived from them rather than restated. Nothing else is read from
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

// --- the reference screen: derived, not chosen -----------------------------
// The authoring laptop's width, the reference every crop-authored pixel
// ruling reads against.
constexpr int kReferenceScreenColumns = 1920;
// The screen at working zoom, where all fine horizontal placement happens:
// 1.25 ms * 1920 = 2.4 s. A time constant of this rule is stated as a share
// of it — a derived multiple of the working screen, not a free second.
constexpr double kReferenceScreenSeconds =
    working_zoom_ms_per_px() * kReferenceScreenColumns / 1000.0;

// --- the fixed constants -----------------------------------------------------
constexpr double kLevelDb = 6.02;  // one doubling of the picture, in dB
// THE ANALYSIS HOP is resolution only. Measured against a 10 ms hop at working
// zoom (1.25 ms/px, where 0.1 s is an 80 px linear segment) over the 40th's
// first movement, the two curves differ by 0.028 dB mean, 0.21 dB at p99,
// 1.47 dB at the maximum, 0.10 % of columns over 0.5 dB — indistinguishable,
// so the coarser hop stands (architect 2026-09-23).
constexpr double kStepSeconds = 0.1;

// --- the free constants, each with its reason -------------------------------
// The typical top: the loudest 10% may clip (convention; the architect's own
// hand-set levels clipped 7 to 12 % on every section that could reach the
// edge).
constexpr double kTopPercentile = 0.9;
// Columns under this are silence or tape hiss (the architect's old gates ran
// -40..-50).
constexpr double kGateDb = -50.0;
// A window with less audible material than this fraction is silence.
constexpr double kGatedMinFraction = 0.25;
// Section scale: the finest window whose exposition and repeat still agree
// (1 s splits them; 3 s, R128's, misses lead-ins) — five eighths of the
// working-zoom screen. The consequence the architect accepted with it: a
// centred window's p90 reports loud once a tenth of it is loud, so the quiet
// before a loud entry fades over roughly the last quarter of a screen
// (~0.6 s) and the first quarter after it — symmetric in time, no forward
// look-ahead — and a lone accent halos its neighbours for about half a window
// on each side. Both are the rule's shape, not defects.
constexpr double kWindowSeconds = kReferenceScreenSeconds * 5.0 / 8.0;
static_assert(kWindowSeconds == 1.5, "the exposition/repeat criterion's 1.5 s");
// THE CLAMP'S TWO CONSTANTS. The floor never attenuates: a tutti whose
// typical top already rests under the edge is left alone.
constexpr double kGainMin = 1.0;
// The cap: four doublings, the old level ladder's top.
constexpr double kGainMax = 16.0;

double db(double x) { return 20 * std::log10(std::max(x, 1e-6)); }

class Analysis {
public:
    Analysis(std::vector<double> columns, double gate)
        : c_(std::move(columns)), n_(static_cast<int64_t>(c_.size())), gate_(gate) {}

    // The number of doublings the typical top of columns [lo, hi) has under
    // 0 dBFS, or nothing when too little of the window is audible. The slice
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
            1, static_cast<int64_t>(static_cast<double>(hi - lo) * kGatedMinFraction));
        if (static_cast<int64_t>(scratch_.size()) < need) return std::nullopt;
        const size_t idx = static_cast<size_t>(
            kTopPercentile * static_cast<double>(scratch_.size() - 1));
        std::nth_element(scratch_.begin(), scratch_.begin() + static_cast<std::ptrdiff_t>(idx),
                         scratch_.end());
        return -db(scratch_[idx]) / kLevelDb;
    }

private:
    std::vector<double> c_;
    int64_t             n_;
    double              gate_;
    std::vector<double> scratch_;
};

}  // namespace

WaveformGainCurve derive_waveform_gain(const float* interleaved, int64_t total_frames,
                                       int sample_rate) {
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
    const double gate = std::pow(10.0, kGateDb / 20);

    Analysis an(std::move(columns), gate);

    // THE MEASURE at the analysis hop.
    std::vector<double> coarse;
    {
        const int64_t hw = static_cast<int64_t>(kWindowSeconds * cps / 2);
        std::vector<std::optional<double>> raw;
        std::vector<int64_t> known;
        for (int64_t i = 0; i < n; i += st) {
            raw.push_back(an.top_level(i - hw, i + hw));
            if (raw.back()) known.push_back(static_cast<int64_t>(raw.size()) - 1);
        }
        coarse.resize(raw.size());
        if (known.empty()) {
            std::fill(coarse.begin(), coarse.end(), 3.0);
        } else {
            // A silent point takes the NEARER known point, the earlier on a tie.
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

    // THE GAIN: 2^d, clamped to [kGainMin, kGainMax]. Nothing else.
    WaveformGainCurve out;
    out.hop_frames = st * col;
    out.gain.resize(coarse.size());
    for (size_t k = 0; k < coarse.size(); ++k)
        out.gain[k] = std::clamp(std::exp2(coarse[k]), kGainMin, kGainMax);
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
