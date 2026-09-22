#include "magnification_auto_detect.h"

// app_state.h is included here and not in the header for ONE reason: it owns
// the zoom map's two constants, and the column width below is derived from
// them rather than restated. Nothing else is read from it; the detector
// touches no application state.
#include "app_state.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

// --- the column: derived, not chosen ---------------------------------------
// The detector measures the picture at WORKING ZOOM, the placement-instrument
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
constexpr double kLevelDb = 6.02;  // one level = x2 (waveform_magnification_gain is 2^level)
// The lane-edge rule would put the loudest tuttis at 0 (15 sections in the
// 40th); 0 is not for this corpus, so it stays hand-only.
constexpr int kMinLevel = 1;
constexpr int kMaxLevel = 4;
constexpr double kStepSeconds = 0.1;  // analysis hop (resolution only)

// --- the free constants, each with its reason -------------------------------
// Where the typical column top lands: THE calibration (fitted across the 40th).
constexpr double kTargetDb = -1.5;
// The typical top: the loudest 10% may clip (convention).
constexpr double kTopPercentile = 0.9;
// Columns under this are silence or tape hiss (the architect's old gates ran
// -40..-50).
constexpr double kGateDb = -50.0;
// A window with less audible material than this fraction is silence.
constexpr double kGatedMinFraction = 0.25;
// Section scale: the finest window whose exposition and repeat still agree
// (1 s splits them; 3 s, R128's, misses lead-ins).
constexpr double kWindowSeconds = 1.5;
// Boundary placement looks this far ahead (a loud entry is clamped up to
// ~0.45 s early).
constexpr double kLookaheadSeconds = 0.5;

// --- derived: nothing below is a free choice --------------------------------
// A level is too much once it would push the typical top past the lane edge
// (0 dB): at level L a stretch measuring d puts its top at
// kTargetDb + (L - d) * kLevelDb, so the highest level that fits is
// floor(d - kTargetDb / kLevelDb) and level L owns [L + kEdge, L + 1 + kEdge).
constexpr double kEdge = kTargetDb / kLevelDb;  // -0.249: thresholds 0.751, 1.751, 2.751, 3.751
// A section shorter than the window cannot be measured.
constexpr double kMinSegmentSeconds = kWindowSeconds;
// A centred window's crossing is off by at most half its width.
constexpr double kPlaceSearchSeconds = kWindowSeconds / 2;

double db(double x) { return 20 * std::log10(std::max(x, 1e-6)); }

int level_of(double d) {
    const double f = std::floor(d - kEdge);
    return static_cast<int>(std::max<double>(kMinLevel, std::min<double>(kMaxLevel, f)));
}

double upper_bound_of(int level) {
    return level == kMaxLevel ? std::numeric_limits<double>::infinity()
                              : (level + 1) + kEdge;
}

double lower_bound_of(int level) {
    return level == kMinLevel ? -std::numeric_limits<double>::infinity()
                              : level + kEdge;
}

struct Section {
    int64_t begin;
    int64_t end;
    int     level;
};

void runs_of(const std::vector<int>& v, std::vector<Section>& out) {
    out.clear();
    const int64_t n = static_cast<int64_t>(v.size());
    int64_t s = 0;
    for (int64_t i = 1; i <= n; ++i) {
        if (i == n || v[static_cast<size_t>(i)] != v[static_cast<size_t>(s)]) {
            out.push_back({s, i, v[static_cast<size_t>(s)]});
            s = i;
        }
    }
}

// A short section joins the neighbour nearest in level; a tie goes to the
// longer neighbour, and a full tie to the earlier (no preference for either
// direction in time beyond that).
size_t absorb_into(const std::vector<Section>& sections, size_t k) {
    bool   have = false;
    size_t best = 0;
    int     best_diff = 0;
    int64_t best_neg_len = 0;
    const size_t count = sections.size();
    const size_t cands[2] = {k - 1, k + 1};
    for (int q = 0; q < 2; ++q) {
        if (q == 0 && k == 0) continue;
        const size_t j = cands[q];
        if (j >= count) continue;
        const int     diff    = std::abs(sections[j].level - sections[k].level);
        const int64_t neg_len = -(sections[j].end - sections[j].begin);
        if (!have || diff < best_diff || (diff == best_diff && neg_len < best_neg_len)) {
            have = true;
            best = j;
            best_diff = diff;
            best_neg_len = neg_len;
        }
    }
    assert(have);
    return best;
}

class Analysis {
public:
    Analysis(std::vector<double> columns, double gate)
        : c_(std::move(columns)), n_(static_cast<int64_t>(c_.size())), gate_(gate) {}

    // The level that puts the typical top of columns [lo, hi) at kTargetDb,
    // or `prev` when too little of the window is audible. The slice clamps to
    // the song; the audibility threshold reads the UNCLAMPED width, so a
    // window hanging off either end needs as much audible material as a
    // whole one.
    std::optional<double> top_level(int64_t lo, int64_t hi, std::optional<double> prev) {
        scratch_.clear();
        const int64_t b = std::max<int64_t>(0, lo);
        const int64_t e = std::min<int64_t>(n_, hi);
        for (int64_t i = b; i < e; ++i) {
            const double x = c_[static_cast<size_t>(i)];
            if (x > gate_) scratch_.push_back(x);
        }
        const int64_t need = std::max<int64_t>(
            1, static_cast<int64_t>(static_cast<double>(hi - lo) * kGatedMinFraction));
        if (static_cast<int64_t>(scratch_.size()) < need) return prev;
        const size_t idx = static_cast<size_t>(
            kTopPercentile * static_cast<double>(scratch_.size() - 1));
        std::nth_element(scratch_.begin(), scratch_.begin() + static_cast<std::ptrdiff_t>(idx),
                         scratch_.end());
        return (kTargetDb - db(scratch_[idx])) / kLevelDb;
    }

private:
    std::vector<double> c_;
    int64_t             n_;
    double              gate_;
    std::vector<double> scratch_;
};

}  // namespace

std::vector<GuiDetectedMagnificationLevel>
detect_magnification_levels(const float* interleaved, int64_t total_frames,
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

    // 1. THE CURVE at the analysis hop.
    std::vector<double> coarse;
    {
        const int64_t hw = static_cast<int64_t>(kWindowSeconds * cps / 2);
        std::vector<std::optional<double>> raw;
        std::vector<int64_t> known;
        for (int64_t i = 0; i < n; i += st) {
            raw.push_back(an.top_level(i - hw, i + hw, std::nullopt));
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

    // 2. THE LEVELS by the lane-edge rule, direction-free; the shortest short
    // section joins its nearest-level neighbour until none is short.
    std::vector<int> cls(coarse.size());
    for (size_t k = 0; k < coarse.size(); ++k) cls[k] = level_of(coarse[k]);

    std::vector<Section> r;
    const int64_t m = static_cast<int64_t>(kMinSegmentSeconds / kStepSeconds);
    for (;;) {
        runs_of(cls, r);
        if (r.size() < 2) break;
        bool   have = false;
        size_t k = 0;
        for (size_t q = 0; q < r.size(); ++q) {
            const int64_t len = r[q].end - r[q].begin;
            if (len >= m) continue;
            if (!have || len < r[k].end - r[k].begin) { have = true; k = q; }
        }
        if (!have) break;
        const int into = r[absorb_into(r, k)].level;
        for (int64_t i = r[k].begin; i < r[k].end; ++i) cls[static_cast<size_t>(i)] = into;
    }

    // 3. THE BOUNDARIES at column resolution. Getting louder, the new level
    // begins at the first column whose next kLookaheadSeconds measures
    // inside it; getting quieter, likewise from its other side.
    const int64_t fw = static_cast<int64_t>(kLookaheadSeconds * cps);
    const int64_t search = static_cast<int64_t>(kPlaceSearchSeconds / kStepSeconds);
    runs_of(cls, r);
    struct Breakpoint { int64_t column; int level; };
    std::vector<Breakpoint> bps;
    bps.push_back({0, cls[0]});
    for (size_t k = 1; k < r.size(); ++k) {
        const int64_t s      = r[k].begin;
        const int     l      = r[k].level;
        const int     prev_l = r[k - 1].level;
        const int64_t lo = std::max(bps.back().column + 1, (s - search) * st);
        const int64_t hi = std::min(n, (s + search) * st);
        const bool louder = l < prev_l;
        int64_t b = s * st;
        for (int64_t c = lo; c < hi; ++c) {
            const double d = *an.top_level(c, c + fw, static_cast<double>(prev_l));
            if (louder ? (d < upper_bound_of(l)) : (d >= lower_bound_of(l))) {
                b = c;
                break;
            }
        }
        bps.push_back({b, l});
    }

    // 3b. THE MINIMUM AGAIN at column resolution (placement can shrink a
    // section): the shortest short section takes its nearest-level
    // neighbour's level, equal neighbours merge keeping the earlier start,
    // and the first breakpoint stays at column 0.
    const int64_t mc = static_cast<int64_t>(kMinSegmentSeconds * cps);
    std::vector<Section> secs;
    while (bps.size() > 1) {
        secs.clear();
        for (size_t k = 0; k < bps.size(); ++k) {
            const int64_t end = k + 1 < bps.size() ? bps[k + 1].column : n;
            secs.push_back({bps[k].column, end, bps[k].level});
        }
        size_t k = 0;
        for (size_t q = 1; q < secs.size(); ++q)
            if (secs[q].end - secs[q].begin < secs[k].end - secs[k].begin) k = q;
        if (secs[k].end - secs[k].begin >= mc) break;
        secs[k].level = secs[absorb_into(secs, k)].level;
        bps.clear();
        for (const Section& sec : secs) {
            if (!bps.empty() && bps.back().level == sec.level) continue;
            bps.push_back({sec.begin, sec.level});
        }
        bps[0].column = 0;
    }

    std::vector<GuiDetectedMagnificationLevel> out;
    for (const Breakpoint& bp : bps) {
        const int64_t frame = bp.column * col;
        if (frame < total_frames) out.push_back({frame, bp.level});
    }
    return out;
}
