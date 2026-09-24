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
// The eight tunables are the device config's (WaveformGainParams,
// waveform_gain.h; the grammar and brackets at device_config.h). These are the
// reasons for the DEFAULTS both first-run templates stamp.
//
// `waveform_gain_window_s` 3.0: the LUFS short-term length, chosen by eye
// (architect 2026-09-23) for the contrast it gives and for the earlier dip
// before a forte. The 1.5 s that stood before it was the marker regime's
// "finest window whose exposition and repeat still agree", a criterion that
// died with the quantizer. Measured 2026-09-23 on the 40th's first movement:
// 3 s halves the curve's reversal rate against 1.5 s (8 against 16 per minute
// over a quarter doubling) and moves the first tutti's drop about half a
// second earlier, with the section medians unchanged. The earlier dip is
// musically right: the composer naturally writes a dip before a tutti,
// mastering engineers drop about half a dB before a loud section, and the
// material just before a tutti carries less meaning. The window's shape — the
// half-window fade on each side of a loud entry, the lone accent's halo — is
// at the header.
//
// THE PERCENTILE IS FIXED AT THE MAXIMUM, not a key (the 100th settled by eye,
// architect 2026-09-23; `waveform_gain_percentile` was a key for that one
// day): the window's maximum is brought to the edge with nothing clipping,
// pure peak normalization per window — in dynamics terms +24 dB of make-up
// into a brickwall limiter with 1.5 s look-ahead and 1.5 s hold — because the
// second theme's shape between its onsets survives and the tutti/quiet
// difference reads at a glance. The 0.90 that stood before it was the marker
// regime's "the loudest tenth may clip" convention. Measured on the corpus,
// the 100th lowers every gain about a quarter against the 99th and leaves the
// contrast unchanged.
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
// `waveform_gain_max` 16, the cap: four doublings, the old level ladder's top.
//
// THE UPWARD COMPRESSION'S FOUR (the curve at waveform_gain.h; architect
// 2026-09-24). Measured 2026-09-24 on the corpus's four movements — the
// 40th's three and the Jupiter's first — each working-zoom column's level
// against its own 3 s ceiling: the columns sit at the same places on every
// movement — the top quarter within about 6.4 dB of the ceiling, the median
// column at about -9 dB, the bottom quarter below about -12.5 dB, the bottom
// tenth below about -17 dB. A window's RMS sits about 8 dB under its peak with
// a 4 to 5 dB spread, so a threshold fixed against the ceiling does an RMS
// threshold's job without a second measure (a loudness-relative threshold is
// ruled out, waveform_gain.h). THE CRITERION any setting is measured against:
// the floor (the 5th..25th percentile columns) lifts 4 to 6 dB, keeps at
// least 6 of its 7.8 dB of detail, and the top quarter moves 0.0 dB.
//
// `waveform_gain_upward_ratio` 1.00, off: the stage is a knob the architect
// turns on by eye; the default leaves the leveler's picture as it stood, and
// the other three are inert until he does. With a range in the chain the
// ratio decides only how abrupt the step between the two regions is: the
// transition band is D * R / (R - 1) dB wide.
//
// `waveform_gain_upward_threshold_db` -9: the median column; the picture
// stays truthful above it.
//
// `waveform_gain_upward_knee_db` 6: the interquartile band, -12.5 to -6.4
// around the -9 threshold, so the top quarter is untouched by construction.
//
// `waveform_gain_upward_range_db` 6: the floor's maximum rise. The range is
// what keeps the troughs' own texture: with the ratio alone the spread
// between the 5th and 25th percentile columns collapses from 7.8 dB to 3.9 at
// ratio 2 and to 1.9 at ratio 4, while with range 4 at ratio 3 it keeps
// 6.0 dB and the floor still rises 4 dB.
//
// THE FLOOR IS FIXED, not a key: it forbids attenuation. d = -L / 6.02 >= 0
// for every window (L <= 0), so the floor is reached only at d = 0 and the
// picture is NEVER pushed below its own level — a window whose top
// already sits on the edge takes x1, and every other window is raised to it.
constexpr double kGainMin = 1.0;

double db(double x) { return 20 * std::log10(std::max(x, 1e-6)); }

class Analysis {
public:
    Analysis(std::vector<double> columns, double gate, double min_fraction)
        : c_(std::move(columns)), n_(static_cast<int64_t>(c_.size())), gate_(gate),
          min_fraction_(min_fraction) {}

    // The top of columns [lo, hi) in dBFS (L) — the gated slice's MAXIMUM, in
    // one pass (the percentile is fixed at the maximum, the reason at the
    // defaults above) — or nothing when too little of the window is audible.
    // The slice clamps to the song; the audibility threshold reads the
    // UNCLAMPED width, so a window hanging off either end needs as much
    // audible material as a whole one.
    std::optional<double> top_level(int64_t lo, int64_t hi) const {
        const int64_t b = std::max<int64_t>(0, lo);
        const int64_t e = std::min<int64_t>(n_, hi);
        int64_t audible = 0;
        double  peak    = 0.0;
        for (int64_t i = b; i < e; ++i) {
            const double x = c_[static_cast<size_t>(i)];
            if (x > gate_) {
                ++audible;
                if (x > peak) peak = x;
            }
        }
        const int64_t need = std::max<int64_t>(
            1, static_cast<int64_t>(static_cast<double>(hi - lo) * min_fraction_));
        if (audible < need) return std::nullopt;
        return db(peak);
    }

private:
    std::vector<double> c_;
    int64_t             n_;
    double              gate_;
    double              min_fraction_;
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

    const Analysis an(std::move(columns), gate, params.min_fraction);

    // THE MEASURE at the analysis hop, then the doublings per hop,
    // d = -L / kLevelDb: the leveler, every window's top on the edge.
    // No max(0, .): L <= 0 for a decoded PCM peak (|x| <= 1), so d >= 0.
    auto doublings = [](double level_db) { return -level_db / kLevelDb; };
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
    // config reader's bracket). Nothing else; the upward compression's four
    // values ride along for the painter, which alone applies them.
    WaveformGainCurve out;
    out.hop_frames          = st * col;
    out.upward_ratio        = params.upward_ratio;
    out.upward_threshold_db = params.upward_threshold_db;
    out.upward_knee_db      = params.upward_knee_db;
    out.upward_range_db     = params.upward_range_db;
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

double upward_compressed_tip(double t, const WaveformGainCurve& curve) {
    const double r = curve.upward_ratio;
    const double a = std::fabs(t);
    // Ratio 1 is the identity exactly; a tip under -120 dB lifts to nothing
    // visible and 0 stays 0 (the guard before the log).
    if (r == 1.0 || a < 1e-6) return t;
    const double x    = 20.0 * std::log10(a);
    const double k    = curve.upward_knee_db;
    const double hi   = curve.upward_threshold_db + k / 2;
    const double lo   = curve.upward_threshold_db - k / 2;
    const double over = 1.0 - 1.0 / r;
    double y;
    if (x >= hi) {
        return t;  // above the knee nothing moves
    } else if (x >= lo) {
        // The quadratic soft knee (reached only when k > 0: at k = 0,
        // lo = hi and every x under hi falls to the branch below).
        y = x + over * (hi - x) * (hi - x) / (2 * k);
    } else {
        y = (lo + over * k / 2) - (lo - x) / r;
    }
    y = std::min(y, x + curve.upward_range_db);  // the range
    y = std::min(y, 0.0);
    const double m = std::pow(10.0, y / 20.0);
    return t < 0.0 ? -m : m;
}
