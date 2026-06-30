#include "limiter.h"
#include "wt_profile.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <iostream>
#include <limits>
#include <vector>

// Post-render spectral limiter. The finished time-domain render is re-analyzed
// on the limiter's OWN N-point STFT (no zero-pad, conventional origin-aligned
// frames), the over-ceiling peaks are resolved against a limiter-local
// attenuation map, the buffer is reconstructed, and the result replaces the
// input render. The solver (c_id identity contributions, eval_map coherent-sum
// prediction, apply_update proportional/clamp-redistribution, the symmetric
// dist_to_ceiling selection, the outer re-queue/rescan loop) is the same one
// that ran in-PV; only its analysis grid and data source changed.
//
// Two sample-coordinate systems are in play:
//   - pre  : front-padded buffer coords. A front-pad of N_lim zeros is
//            prepended so the Hann COLA is fully formed at render sample 0.
//            Frame m contributes to pre samples [m*R_s_lim, m*R_s_lim + N_lim).
//            meas_ola, peaks, and all frame<->sample math live in pre coords.
//   - post : render coords = pre - N_lim. Output sample 0 (the first emitted
//            render sample) sits at pre position N_lim.
// Single source of truth for the shift: post = pre - N_lim.

namespace {
constexpr int    RESCAN_HALF_WIDTH_FRAMES = 100;  // base radius (frames) for rescan region
constexpr int    MIN_PEAK_EDGE_MARGIN     = 25;   // minimum frames between any peak and region edge
constexpr int    PEAK_DEDUP_RADIUS        = 4;    // per-channel minimum sample gap between peaks
constexpr int    MAX_REFINEMENT_TRIES     = 3;    // extra attempts past the first (inner predictive aim)
constexpr int    MAX_CLAMP_REDIST_TRIES   = 8;    // safety cap on apply_update inner loop
constexpr int    MAX_PEAK_RESOLVE_PASSES  = 4;    // per-lineage outer re-queue cap (architect sweeps)
constexpr double DIAG_FLOOR_DB            = 12.0; // reduction (dB) that fills diag floor
constexpr bool   kCidSelfCheck            = false; // cross-check direct c_id against band_ifft

struct Peak {
    int64_t sample_idx;     // position in the reconstruction (pre coords)
    int     ch;
    int     sign;           // +1 or -1
    double  magnitude;      // |measured value|
    double  original_mag;   // magnitude at first measurement (for diag amplitude)
    int     passes = 0;     // re-queue count for this lineage (see MAX_PEAK_RESOLVE_PASSES)
};

// The limiter's own analysis grid + inverse-FFT scratch, passed to the IFFT
// helpers so they read no AudioSTFT FFT member. No fftshift: frame samples go
// straight to/from index n (the PV's origin-centered unshift does not apply on
// this conventional grid).
struct LimGrid {
    int N, M, R_s, K, channels, num_frames, num_bands;
    fftw_plan     plan_inv;
    fftw_complex* inv_in;
    double*       inv_out;
    const double* synth_window;
    const int*    bin_to_band;
};

// IFFT of a single band (all other bins zeroed) with gain applied, scaled by
// 1/M and synth-windowed into the [0, N) OLA window. No unshift. Self-check
// only: the main path computes c_id by single-point inverse DFT (see the peak
// loop); this remains as the byte-for-byte reference the kCidSelfCheck flag
// cross-checks against.
static void band_ifft(const LimGrid& g, const std::complex<float>* spec,
                      int band, double gain, std::vector<double>& out) {
    const int K = g.K;
    for (int k = 0; k < K; ++k) {
        if (g.bin_to_band[k] == band) {
            g.inv_in[k][0] = spec[k].real() * gain;
            g.inv_in[k][1] = spec[k].imag() * gain;
        } else {
            g.inv_in[k][0] = 0.0;
            g.inv_in[k][1] = 0.0;
        }
    }
    fftw_execute(g.plan_inv);
    const double inv_M = 1.0 / g.M;
    for (int n = 0; n < g.N; ++n)
        out[n] = g.inv_out[n] * inv_M * g.synth_window[n];
}

// Full-spectrum IFFT with per-band gain from gain_row (one frame's map row).
static void full_ifft_with_map(const LimGrid& g, const std::complex<float>* spec,
                               const double* gain_row, std::vector<double>& out) {
    const int K = g.K;
    for (int k = 0; k < K; ++k) {
        const double gg = gain_row[g.bin_to_band[k]];
        g.inv_in[k][0] = spec[k].real() * gg;
        g.inv_in[k][1] = spec[k].imag() * gg;
    }
    fftw_execute(g.plan_inv);
    const double inv_M = 1.0 / g.M;
    for (int n = 0; n < g.N; ++n)
        out[n] = g.inv_out[n] * inv_M * g.synth_window[n];
}

// At 75% overlap (R_s = N/4), exactly 4 frames contribute to each output
// sample. `s` is in pre coords.
static void contributing_frames(int64_t s, int R_s, int N, int num_frames,
                                 std::vector<int>& out) {
    out.clear();
    int overlap = N / R_s;
    int m_hi = static_cast<int>(s / R_s);
    if (m_hi >= num_frames) m_hi = num_frames - 1;
    int m_lo = m_hi - (overlap - 1);
    if (m_lo < 0) m_lo = 0;
    for (int m = m_lo; m <= m_hi; ++m) {
        int64_t fs = static_cast<int64_t>(m) * R_s;
        if (fs <= s && s < fs + N) out.push_back(m);
    }
}

// Scan [n_start, n_end) for one channel (pre coords). Emits one Peak per
// above-ceiling cluster (contiguous run within PEAK_DEDUP_RADIUS samples).
static void find_peaks_in_range(const float* ola, int64_t n_start, int64_t n_end,
                                int channels, int ch, double ceiling,
                                std::vector<Peak>& out_peaks) {
    int64_t best_idx = -1;
    double best_mag = 0.0;
    int    best_sign = 0;
    int64_t last_above = std::numeric_limits<int64_t>::min() / 2;

    auto flush_cluster = [&]() {
        if (best_idx >= 0) {
            Peak p;
            p.sample_idx = best_idx;
            p.ch = ch;
            p.sign = best_sign;
            p.magnitude = best_mag;
            p.original_mag = best_mag;
            p.passes = 0;
            out_peaks.push_back(p);
            best_idx = -1;
            best_mag = 0.0;
            best_sign = 0;
        }
    };

    for (int64_t n = n_start; n < n_end; ++n) {
        float v = ola[n * channels + ch];
        double a = std::abs(v);
        if (a > ceiling) {
            if (best_idx >= 0 && n - last_above > PEAK_DEDUP_RADIUS) flush_cluster();
            if (best_idx < 0 || a > best_mag) {
                best_idx = n;
                best_mag = a;
                best_sign = (v >= 0.0f) ? 1 : -1;
            }
            last_above = n;
        }
    }
    flush_cluster();
}

// Re-synthesize OLA over [s_start, s_end) (pre coords) from cached spectra and
// the current gain map. The returned slice's index 0 corresponds to pre
// position s_start. Writes interleaved floats into dest.
static void rescan_region(const LimGrid& g,
                          const std::complex<float>* cached_spectra,
                          const std::vector<std::vector<double>>& gain_map,
                          int64_t s_start, int64_t s_end,
                          std::vector<float>& dest) {
    const int N = g.N;
    const int R_s = g.R_s;
    const int channels = g.channels;
    const int K = g.K;
    const int num_frames = g.num_frames;
    const size_t slice_len = static_cast<size_t>(s_end - s_start);

    std::vector<std::vector<double>> local_ola(channels, std::vector<double>(slice_len, 0.0));
    std::vector<double> frame_time(N);

    int overlap = N / R_s;
    int m_hi = static_cast<int>((s_end - 1) / R_s);
    if (m_hi >= num_frames) m_hi = num_frames - 1;
    int m_lo = static_cast<int>(s_start / R_s) - (overlap - 1);
    if (m_lo < 0) m_lo = 0;

    for (int m = m_lo; m <= m_hi; ++m) {
        int64_t frame_start = static_cast<int64_t>(m) * R_s;
        if (frame_start + N <= s_start) continue;
        if (frame_start >= s_end) break;
        for (int ch = 0; ch < channels; ++ch) {
            const std::complex<float>* spec = cached_spectra
                + (static_cast<size_t>(m) * channels + ch) * K;
            full_ifft_with_map(g, spec, gain_map[m].data(), frame_time);
            for (int n = 0; n < N; ++n) {
                int64_t out_idx = frame_start + n - s_start;
                if (out_idx < 0 || out_idx >= static_cast<int64_t>(slice_len)) continue;
                local_ola[ch][out_idx] += frame_time[n];
            }
        }
    }

    dest.assign(slice_len * channels, 0.0f);
    for (size_t n = 0; n < slice_len; ++n)
        for (int ch = 0; ch < channels; ++ch)
            dest[n * channels + ch] = static_cast<float>(local_ola[ch][n]);
}

}  // anonymous namespace

void Limiter::process(AudioSTFT& stft, std::vector<float>& render) {
    const bool prof = wtprof::enabled();
    const auto t_total_0 = wtprof::now();
    auto& lp = stft.limiter_params;

    const int channels    = stft.channels;
    const int sample_rate  = stft.src_info.samplerate;
    if (channels <= 0) return;
    const int64_t render_frames = static_cast<int64_t>(render.size()) / channels;
    if (render_frames <= 0) {
        std::cout << "[Pass 3/3] Limiter.......................... empty render, skipped\n";
        if (prof) {
            const auto t_total_1 = wtprof::now();
            std::cerr << "[profile] limiter_summary ms="
                      << wtprof::ms(t_total_0, t_total_1)
                      << " sample_frames=0 channels=" << channels
                      << " peak_count=0 iteration_count=0 active=no bypass=empty\n";
        }
        return;
    }

    // -- Limiter's own analysis grid (independent of the PV's 2N-padded grid) --
    const int    N_lim   = stft.N;            // 4096
    const int    M_lim   = N_lim;             // no zero-pad: magnitude/coherent sum
    const int    R_s_lim = N_lim / 4;         // 1024
    const int    K_lim   = M_lim / 2 + 1;     // 2049
    const double bin_hz_width_lim = static_cast<double>(sample_rate) / N_lim;

    // COLA analysis/synthesis Hann pair for 75% overlap (synth = analysis/1.5).
    // Periodic Hann (denominator N_lim, not N_lim - 1): at R_s_lim = N_lim/4 the
    // four overlapping squared windows sum to exactly 3/2 at every sample, so
    // the /1.5 synthesis window is the exact dual and the round-trip is unity
    // with no COLA ripple. Replicated locally for N_lim — the PV's windows are
    // length N too but planned on the 2N grid.
    std::vector<double> window(N_lim), synth_window(N_lim);
    for (int n = 0; n < N_lim; ++n) {
        window[n]       = 0.5 * (1.0 - std::cos(2.0 * M_PI * n / N_lim));
        synth_window[n] = window[n] / 1.5;
    }

    // -- 1/3-octave band table over the limiter's own bin grid --
    // The band table auto-derives from the one-third-octave grid (centers
    // 1000*2^(n/3) from 20 Hz to Nyquist, hard nearest-log-distance bin->band),
    // mirroring AudioSTFT::init_fftw.
    std::vector<int> bin_to_band(K_lim, 0);
    int num_bands = 0;
    {
        const double nyq = sample_rate / 2.0;
        std::vector<double> centers;
        int n_min = static_cast<int>(std::ceil (3.0 * std::log2(20.0 / 1000.0)));
        int n_max = static_cast<int>(std::floor(3.0 * std::log2(nyq  / 1000.0)));
        for (int n = n_min; n <= n_max; ++n)
            centers.push_back(1000.0 * std::pow(2.0, n / 3.0));
        if (centers.empty()) centers.push_back(1000.0);
        num_bands = static_cast<int>(centers.size());
        for (int k = 0; k < K_lim; ++k) {
            double hz = k * bin_hz_width_lim;
            if (hz <= centers.front()) { bin_to_band[k] = 0; continue; }
            if (hz >= centers.back())  { bin_to_band[k] = num_bands - 1; continue; }
            double log_hz = std::log2(hz);
            int best = 0;
            double best_dist = std::abs(log_hz - std::log2(centers[0]));
            for (int b = 1; b < num_bands; ++b) {
                double d = std::abs(log_hz - std::log2(centers[b]));
                if (d < best_dist) { best_dist = d; best = b; }
            }
            bin_to_band[k] = best;
        }
    }

    // -- Own FFTW plans/scratch (destroyed at every return) --
    double*       fwd_in   = fftw_alloc_real(M_lim);
    fftw_complex* fwd_out  = fftw_alloc_complex(K_lim);
    // FFTW_ESTIMATE here is a determinism invariant (fixed plan, no host timing);
    // FFTW_MEASURE/FFTW_PATIENT would break bit-identical output.
    fftw_plan     plan_fwd = fftw_plan_dft_r2c_1d(M_lim, fwd_in, fwd_out, FFTW_ESTIMATE);
    fftw_complex* inv_in   = fftw_alloc_complex(K_lim);
    double*       inv_out  = fftw_alloc_real(M_lim);
    fftw_plan     plan_inv = fftw_plan_dft_c2r_1d(M_lim, inv_in, inv_out, FFTW_ESTIMATE);
    auto destroy = [&]() {
        fftw_destroy_plan(plan_fwd);
        fftw_destroy_plan(plan_inv);
        fftw_free(fwd_in);  fftw_free(fwd_out);
        fftw_free(inv_in);  fftw_free(inv_out);
    };

    // pre/post framing. Front-pad N_lim zeros; the kept render occupies pre
    // [valid_lo, valid_hi). num_frames_lim covers up to the last kept sample so
    // all four COLA contributions exist across the whole kept region.
    const int64_t valid_lo = N_lim;                   // pre coord of render[0]
    const int64_t valid_hi = N_lim + render_frames;   // one past render end
    const int num_frames_lim =
        static_cast<int>((valid_hi - 1) / R_s_lim) + 1;
    const int64_t padded_total =
        static_cast<int64_t>(num_frames_lim - 1) * R_s_lim + N_lim;

    LimGrid g;
    g.N = N_lim; g.M = M_lim; g.R_s = R_s_lim; g.K = K_lim;
    g.channels = channels; g.num_frames = num_frames_lim; g.num_bands = num_bands;
    g.plan_inv = plan_inv; g.inv_in = inv_in; g.inv_out = inv_out;
    g.synth_window = synth_window.data(); g.bin_to_band = bin_to_band.data();

    // Single-sample inverse-DFT twiddle table: cos and sin of 2*pi*j/M_lim
    // for j in [0, M_lim). The c_id evaluation below indexes it with
    // (k * local) mod M_lim via an integer stride, so every angle factor is
    // an exact table entry — no recurrence drift across the bin loop.
    std::vector<double> tw_cos(M_lim), tw_sin(M_lim);
    for (int j = 0; j < M_lim; ++j) {
        const double a = 2.0 * M_PI * j / M_lim;
        tw_cos[j] = std::cos(a);
        tw_sin[j] = std::sin(a);
    }

    // Front-padded interleaved sample read (zero outside [0, render_frames)).
    auto padded_sample = [&](int64_t pre_idx, int ch) -> double {
        int64_t r = pre_idx - N_lim;
        if (r < 0 || r >= render_frames) return 0.0;
        return render[r * channels + ch];
    };

    // -- Forward STFT of the front-padded render --
    std::vector<std::complex<float>> cached_spectra(
        static_cast<size_t>(num_frames_lim) * channels * K_lim);
    for (int m = 0; m < num_frames_lim; ++m) {
        const int64_t fs = static_cast<int64_t>(m) * R_s_lim;
        for (int ch = 0; ch < channels; ++ch) {
            for (int n = 0; n < N_lim; ++n)
                fwd_in[n] = padded_sample(fs + n, ch) * window[n];
            fftw_execute(plan_fwd);
            std::complex<float>* dst =
                cached_spectra.data() + (static_cast<size_t>(m) * channels + ch) * K_lim;
            for (int k = 0; k < K_lim; ++k)
                dst[k] = std::complex<float>(static_cast<float>(fwd_out[k][0]),
                                             static_cast<float>(fwd_out[k][1]));
        }
    }

    // Limiter-local gain map [frame][band], identity. The limiter solves over
    // this map alone and reads nothing from the PV grid.
    std::vector<std::vector<double>> gain_map(
        num_frames_lim, std::vector<double>(num_bands, 1.0));

    const double ceiling    = std::pow(10.0, lp.ceiling_dbfs / 20.0);
    const double tol_amp_hi = std::pow(10.0, (lp.ceiling_dbfs + lp.tolerance_db) / 20.0);
    const double tol_amp_lo = std::pow(10.0, (lp.ceiling_dbfs - lp.tolerance_db) / 20.0);
    const double band3_hi   = std::pow(10.0, (lp.ceiling_dbfs + 3.0 * lp.tolerance_db) / 20.0);
    const double band3_lo   = std::pow(10.0, (lp.ceiling_dbfs - 3.0 * lp.tolerance_db) / 20.0);

    // -- Initial identity reconstruction -> meas_ola (pre coords) --
    std::vector<float> meas_ola;
    rescan_region(g, cached_spectra.data(), gain_map, 0, padded_total, meas_ola);

    // -- Peak detection over the kept region only --
    std::vector<Peak> queue;
    for (int ch = 0; ch < channels; ++ch)
        find_peaks_in_range(meas_ola.data(), valid_lo, valid_hi, channels, ch,
                            ceiling, queue);

    if (queue.empty()) {
        std::cout << "[Pass 3/3] Limiter.......................... 0 peaks, no attenuation required\n";
        destroy();
        if (prof) {
            const auto t_total_1 = wtprof::now();
            std::cerr << "[profile] limiter_summary ms="
                      << wtprof::ms(t_total_0, t_total_1)
                      << " sample_frames=" << render_frames
                      << " channels=" << channels
                      << " peak_count=0 iteration_count=0 active=no bypass=no_attenuation\n";
        }
        return;   // render left untouched (no round-trip applied)
    }
    const size_t initial_peak_count = queue.size();

    auto cmp_desc = [](const Peak& a, const Peak& b) {
        if (a.magnitude != b.magnitude) return a.magnitude > b.magnitude;
        return a.sample_idx < b.sample_idx;
    };
    std::sort(queue.begin(), queue.end(), cmp_desc);

    std::vector<Peak>   resolved;
    std::vector<double> reduction_db_list;
    std::vector<double> residual_db_list;
    resolved.reserve(queue.size());
    reduction_db_list.reserve(queue.size());
    residual_db_list.reserve(queue.size());

    std::vector<float>  rescan_slice;
    std::vector<int>    frames_cov;
    double cid_selfcheck_max_diff = 0.0;  // tracked only when kCidSelfCheck

    int iterations = 0;

    auto within_tol  = [&](double v) { double a = std::abs(v); return a >= tol_amp_lo && a <= tol_amp_hi; };
    auto within_3tol = [&](double v) { double a = std::abs(v); return a >= band3_lo && a <= band3_hi; };

    while (!queue.empty()) {
        if (stft.cancel_flag && stft.cancel_flag->load()) {
            stft.cancellation_observed = true;
            destroy();
            if (prof) {
                const auto t_total_1 = wtprof::now();
                std::cerr << "[profile] limiter_summary ms="
                          << wtprof::ms(t_total_0, t_total_1)
                          << " sample_frames=" << render_frames
                          << " channels=" << channels
                          << " peak_count=" << initial_peak_count
                          << " iteration_count=" << iterations
                          << " active=yes outcome=cancelled\n";
            }
            return;
        }
        Peak peak = queue.front();
        queue.erase(queue.begin());
        ++iterations;

        contributing_frames(peak.sample_idx, R_s_lim, N_lim, num_frames_lim, frames_cov);
        if (frames_cov.empty()) continue;

        // c_id[i][b] = identity-gain contribution of band b at the offending
        // sample: a single-point inverse DFT accumulated per band in one pass
        // over the bins. Equals band_ifft(...)[local] for every band at once
        // (halfcomplex c2r identity above), times the same 1/M and
        // synth_window[local] scaling band_ifft applied.
        std::vector<double> c_id(frames_cov.size() * num_bands, 0.0);
        for (size_t i = 0; i < frames_cov.size(); ++i) {
            int m = frames_cov[i];
            int64_t local = peak.sample_idx - static_cast<int64_t>(m) * R_s_lim;
            if (local < 0 || local >= N_lim) continue;
            const std::complex<float>* spec =
                cached_spectra.data() + (static_cast<size_t>(m) * channels + peak.ch) * K_lim;
            double* row = &c_id[i * num_bands];
            const int step = static_cast<int>(local);   // < M_lim by the guard above
            int j = 0;                                  // (k * step) mod M_lim
            for (int k = 0; k < K_lim; ++k) {
                const double re = spec[k].real();
                const double im = spec[k].imag();
                const double w  = (k == 0 || k == K_lim - 1) ? 1.0 : 2.0;
                row[bin_to_band[k]] += w * (re * tw_cos[j] - im * tw_sin[j]);
                j += step;
                if (j >= M_lim) j -= M_lim;
            }
            const double s = (1.0 / M_lim) * synth_window[static_cast<size_t>(local)];
            for (int b = 0; b < num_bands; ++b) row[b] *= s;

            if (kCidSelfCheck) {
                std::vector<double> frame_time_buf(N_lim);
                for (int b = 0; b < num_bands; ++b) {
                    band_ifft(g, spec, b, 1.0, frame_time_buf);
                    double d = std::abs(frame_time_buf[local] - row[b]);
                    if (d > cid_selfcheck_max_diff) cid_selfcheck_max_diff = d;
                }
            }
        }

        // Snapshot current gain for contributing frames/bands (pre-attempt base).
        std::vector<std::vector<double>> snapshot(frames_cov.size(),
            std::vector<double>(num_bands, 0.0));
        for (size_t i = 0; i < frames_cov.size(); ++i) {
            int m = frames_cov[i];
            for (int b = 0; b < num_bands; ++b)
                snapshot[i][b] = gain_map[m][b];
        }

        // Analytic current peak value (coherent sum prediction).
        auto eval_map = [&](const std::vector<std::vector<double>>& smap) {
            double y = 0.0;
            for (size_t i = 0; i < frames_cov.size(); ++i)
                for (int b = 0; b < num_bands; ++b)
                    y += smap[i][b] * c_id[i * num_bands + b];
            return y;
        };
        double ref_val = eval_map(snapshot);
        if (std::abs(ref_val) <= ceiling) {
            // Already conforming under the current map (neighbor attenuations
            // resolved this peak indirectly). Credit the full reduction from
            // original_mag to |ref_val|.
            double reduction_db = 0.0;
            if (std::abs(ref_val) > 1e-30 && peak.original_mag > 1e-30)
                reduction_db = 20.0 * std::log10(peak.original_mag / std::abs(ref_val));
            if (reduction_db < 0.0) reduction_db = 0.0;
            resolved.push_back(peak);
            reduction_db_list.push_back(reduction_db);
            residual_db_list.push_back(20.0 * std::log10(std::abs(ref_val) / ceiling));
            continue;
        }
        double ref_sign = (ref_val >= 0.0) ? 1.0 : -1.0;

        // Apply proportional update with signed target. Operates on `base`,
        // writes into `out`. r = target/current clamped to [0,1]. Inner loop
        // freezes bands that would clamp below zero and redistributes their
        // share to the unfrozen pool (legitimate dominant-band case; also keeps
        // the outer loop from stalling on a single band).
        auto apply_update = [&](const std::vector<std::vector<double>>& base,
                                double current_val, double target_val,
                                std::vector<std::vector<double>>& out) {
            out = base;
            if (std::abs(current_val) < 1e-30) return;
            double r = target_val / current_val;
            if (r < 0.0) r = 0.0;
            if (r > 1.0) r = 1.0;

            const size_t n_pairs = frames_cov.size() * static_cast<size_t>(num_bands);
            std::vector<double> contrib(n_pairs, 0.0);
            std::vector<unsigned char> frozen(n_pairs, 0);
            double total_mag = 0.0;
            for (size_t i = 0; i < frames_cov.size(); ++i)
                for (int b = 0; b < num_bands; ++b) {
                    double c = std::abs(base[i][b] * c_id[i * num_bands + b]);
                    contrib[i * num_bands + b] = c;
                    total_mag += c;
                }
            if (total_mag < 1e-30) return;

            const double one_minus_r = 1.0 - r;
            double active_total_mag    = total_mag;
            double clamped_contrib_sum = 0.0;
            double one_minus_r_adj     = one_minus_r;

            for (int inner = 0; inner < MAX_CLAMP_REDIST_TRIES; ++inner) {
                bool any_new_clamp = false;
                for (size_t p = 0; p < n_pairs; ++p) {
                    if (frozen[p]) continue;
                    double tentative = 1.0 - one_minus_r_adj * contrib[p] / active_total_mag;
                    if (tentative < 0.0) {
                        frozen[p] = 1;
                        clamped_contrib_sum += contrib[p];
                        active_total_mag    -= contrib[p];
                        any_new_clamp = true;
                    }
                }
                if (!any_new_clamp) break;
                if (active_total_mag < 1e-30) break;
                one_minus_r_adj =
                    (one_minus_r * total_mag - clamped_contrib_sum) / active_total_mag;
            }

            for (size_t i = 0; i < frames_cov.size(); ++i)
                for (int b = 0; b < num_bands; ++b) {
                    size_t p = i * num_bands + b;
                    double factor;
                    if (frozen[p] || active_total_mag < 1e-30) {
                        factor = 0.0;
                    } else {
                        factor = 1.0 - one_minus_r_adj * contrib[p] / active_total_mag;
                        if (factor < 0.0) factor = 0.0;
                    }
                    out[i][b] = base[i][b] * factor;
                }
        };

        // First attempt: aim at ceiling.
        std::vector<std::vector<double>> attempt_map;
        apply_update(snapshot, ref_val, ref_sign * ceiling, attempt_map);
        double attempt_val = eval_map(attempt_map);

        std::vector<std::vector<double>> best_map = attempt_map;
        double best_val = attempt_val;
        auto dist_to_ceiling = [&](double v) { return std::abs(std::abs(v) - ceiling); };

        // Refinement if outside 3*tol band.
        if (!(within_tol(attempt_val) || within_3tol(attempt_val))) {
            double last_val = attempt_val;
            double last_target_mag = ceiling;
            for (int t = 0; t < MAX_REFINEMENT_TRIES; ++t) {
                if (std::abs(last_val) < 1e-30) break;
                double new_target_mag = last_target_mag * (ceiling / std::abs(last_val));
                if (new_target_mag < ceiling * 0.01) new_target_mag = ceiling * 0.01;
                apply_update(snapshot, ref_val, ref_sign * new_target_mag, attempt_map);
                double v = eval_map(attempt_map);
                if (dist_to_ceiling(v) < dist_to_ceiling(best_val)) {
                    best_val = v;
                    best_map = attempt_map;
                }
                last_val = v;
                last_target_mag = new_target_mag;
                if (within_tol(v)) break;
            }
        }

        // Commit best map into the limiter-local gain map.
        for (size_t i = 0; i < frames_cov.size(); ++i) {
            int m = frames_cov[i];
            for (int b = 0; b < num_bands; ++b)
                gain_map[m][b] = best_map[i][b];
        }

        double reduction_db = 0.0;
        if (std::abs(best_val) > 1e-30 && peak.original_mag > 1e-30)
            reduction_db = 20.0 * std::log10(peak.original_mag / std::abs(best_val));
        if (reduction_db < 0.0) reduction_db = 0.0;
        double residual_db = 20.0 * std::log10(std::abs(best_val) / ceiling);

        // -- Rescan region (pre coords) --
        int64_t reg_start = peak.sample_idx - static_cast<int64_t>(RESCAN_HALF_WIDTH_FRAMES) * R_s_lim;
        int64_t reg_end   = peak.sample_idx + static_cast<int64_t>(RESCAN_HALF_WIDTH_FRAMES) * R_s_lim;
        if (reg_start < valid_lo) reg_start = valid_lo;
        if (reg_end   > valid_hi) reg_end   = valid_hi;

        bool extended = true;
        while (extended) {
            extended = false;
            for (const auto& q : queue) {
                if (q.sample_idx < reg_start || q.sample_idx >= reg_end) continue;
                int64_t need = static_cast<int64_t>(MIN_PEAK_EDGE_MARGIN) * R_s_lim;
                if (q.sample_idx - reg_start < need) {
                    int64_t new_start = q.sample_idx - need;
                    if (new_start < valid_lo) new_start = valid_lo;
                    if (new_start < reg_start) { reg_start = new_start; extended = true; }
                }
                if (reg_end - 1 - q.sample_idx < need) {
                    int64_t new_end = q.sample_idx + need + 1;
                    if (new_end > valid_hi) new_end = valid_hi;
                    if (new_end > reg_end) { reg_end = new_end; extended = true; }
                }
            }
        }

        rescan_region(g, cached_spectra.data(), gain_map, reg_start, reg_end, rescan_slice);

        for (int64_t n = reg_start; n < reg_end; ++n) {
            size_t src_off = static_cast<size_t>(n - reg_start) * channels;
            size_t dst_off = static_cast<size_t>(n) * channels;
            for (int ch = 0; ch < channels; ++ch)
                meas_ola[dst_off + ch] = rescan_slice[src_off + ch];
        }

        // Remove queued peaks in region (carry their identity for matching).
        std::vector<Peak> carried;
        carried.reserve(queue.size());
        queue.erase(std::remove_if(queue.begin(), queue.end(),
            [&](const Peak& q) {
                bool inside = (q.sample_idx >= reg_start && q.sample_idx < reg_end);
                if (inside) carried.push_back(q);
                return inside;
            }), queue.end());

        // Re-scan region for new peaks across all channels.
        std::vector<Peak> region_peaks;
        for (int ch = 0; ch < channels; ++ch)
            find_peaks_in_range(meas_ola.data(), reg_start, reg_end, channels, ch,
                                ceiling, region_peaks);

        for (auto& np : region_peaks) {
            // Lineage carry: a re-detected survivor inherits original_mag and a
            // bumped pass count from whichever peak it continues — a displaced
            // queued neighbor (carried) or the just-resolved popped peak itself.
            // Unmatched survivors are genuinely new and keep passes = 0.
            bool carried_lineage = false;
            for (const auto& op : carried) {
                if (op.ch == np.ch &&
                    std::llabs(op.sample_idx - np.sample_idx) <= PEAK_DEDUP_RADIUS) {
                    np.original_mag = op.original_mag;
                    np.passes       = op.passes + 1;
                    carried_lineage = true;
                    break;
                }
            }
            if (!carried_lineage && peak.ch == np.ch &&
                std::llabs(peak.sample_idx - np.sample_idx) <= PEAK_DEDUP_RADIUS) {
                np.original_mag = peak.original_mag;
                np.passes       = peak.passes + 1;
            }

            if (np.passes < MAX_PEAK_RESOLVE_PASSES) {
                queue.push_back(np);
            } else {
                // Cap-retired: permanently dropped, its post-spectral residual
                // (still over ceiling) is handed to the peak limiter backstop.
                double rd = 0.0;
                if (np.magnitude > 1e-30 && np.original_mag > 1e-30)
                    rd = 20.0 * std::log10(np.original_mag / np.magnitude);
                if (rd < 0.0) rd = 0.0;
                resolved.push_back(np);
                reduction_db_list.push_back(rd);
                residual_db_list.push_back(20.0 * std::log10(np.magnitude / ceiling));
            }
        }
        std::sort(queue.begin(), queue.end(), cmp_desc);

        resolved.push_back(peak);
        reduction_db_list.push_back(reduction_db);
        residual_db_list.push_back(residual_db);
    }

    if (kCidSelfCheck)
        std::cerr << "  [c_id self-check] max |direct - band_ifft| = "
                  << cid_selfcheck_max_diff << "\n";

    // Replace the render with the limited reconstruction (kept region). The
    // whole buffer is the STFT round-trip, so this is not bit-nullable against
    // the clean render even in regions that needed no attenuation.
    for (int64_t i = 0; i < render_frames; ++i) {
        size_t dst = static_cast<size_t>(i) * channels;
        size_t src = static_cast<size_t>(i + N_lim) * channels;
        for (int ch = 0; ch < channels; ++ch)
            render[dst + ch] = meas_ola[src + ch];
    }

    // The [Pass 3/3] header and per-peak residual loop go to stdout. On the
    // target-view buffer path (output_audio_file == "<buffer>") that would spam
    // every scrub, so gate both on the disk path.
    const bool verbose = (stft.output_audio_file != "<buffer>");
    if (verbose) {
        std::cout << "[Pass 3/3] Limiter.......................... "
                  << resolved.size() << " peaks, " << iterations
                  << " iterations, done\n";

        // -- Per-peak terminal diagnostic (drives the MAX_PEAK_RESOLVE_PASSES
        //    sweep). time = post coords / sample_rate; residual > 0 is the bit
        //    handed to the peak limiter. --
        for (size_t i = 0; i < resolved.size(); ++i) {
            int64_t post = resolved[i].sample_idx - N_lim;
            double  sec  = static_cast<double>(post) / sample_rate;
            char ln[160];
            std::snprintf(ln, sizeof ln,
                          "  peak @ %.3f s (sample %lld)  residual %+.2f dB\n",
                          sec, static_cast<long long>(post), residual_db_list[i]);
            std::cout << ln;
        }
    }

    // -- Optional diagnostic WAV (gated on lp.diag) --
    if (lp.diag) {
        std::string diag_path = stft.output_audio_file;
        auto dot = diag_path.find_last_of('.');
        if (dot != std::string::npos) diag_path.insert(dot, "-limiter-diag");
        else                          diag_path += "-limiter-diag";

        SF_INFO dinfo = stft.src_info;
        dinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
        dinfo.channels = 1;
        SNDFILE* dsnd = sf_open(diag_path.c_str(), SFM_WRITE, &dinfo);
        if (!dsnd) {
            std::cerr << "  ! could not create limiter diag file '" << diag_path << "'\n";
        } else {
            std::vector<float> dbuf(static_cast<size_t>(render_frames), 0.0f);
            auto poke = [&](int64_t post, float amp) {
                if (post < 0 || post >= render_frames) return;
                dbuf[static_cast<size_t>(post)] = amp;
            };
            for (size_t i = 0; i < resolved.size(); ++i) {
                double red = reduction_db_list[i];
                double scale = red / DIAG_FLOOR_DB;
                if (scale > 1.0) scale = 1.0;
                if (scale < 0.0) scale = 0.0;
                float s = static_cast<float>(scale);
                int64_t post = resolved[i].sample_idx - N_lim;
                poke(post - 1, -0.5f * s);
                poke(post,     -1.0f * s);
                poke(post + 1, -0.5f * s);
            }
            sf_writef_float(dsnd, dbuf.data(), static_cast<sf_count_t>(render_frames));
            sf_close(dsnd);
        }
    }

    destroy();
    if (prof) {
        const auto t_total_1 = wtprof::now();
        std::cerr << "[profile] limiter_summary ms="
                  << wtprof::ms(t_total_0, t_total_1)
                  << " sample_frames=" << render_frames
                  << " channels=" << channels
                  << " peak_count=" << initial_peak_count
                  << " resolved_peak_count=" << resolved.size()
                  << " iteration_count=" << iterations
                  << " active=yes bypass=no\n";
    }
}
