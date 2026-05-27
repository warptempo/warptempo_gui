#pragma once

#include <algorithm>
#include <atomic>
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <fftw3.h>
#include <sndfile.h>

#include "engine.h"

// --- Data Structures ---
struct TimeMapSegment {
    size_t src_frame;
    size_t tgt_frame;
};

struct PhaseResetMarker {
    int synth_frame;
    int64_t src_frame;
    // Concrete phase-propagation model owned from this marker until the next
    // mode-owning marker. Defaults to Peak so a marker constructed without an
    // explicit mode (older callers, empty EngineParams::phase_reset_modes)
    // reproduces the historical all-peak behavior.
    Mode mode = Mode::Peak;
};

struct LimiterParams {
    bool   enabled               = true;
    double ceiling_dbfs          = -0.3;
    double tolerance_db          = 0.01;
    int    num_bands_override    = 0;    // 0 = auto-derive from 1/3-octave grid
    bool   diag                  = false;
};

// --- DSP Helpers ---
inline double princarg(double phase) {
    return phase - 2.0 * M_PI * std::floor((phase + M_PI) / (2.0 * M_PI));
}

// PGHI ("heap") tunables (Part 2). kPghiTol is the significance threshold,
// relative to the per-frame-pair peak magnitude (the LTFAT default, ~-120 dB);
// it decides only which bins may ANCHOR a frequency-spread, never who gets a
// phase. kPghiFreqStep is the per-bin frequency step b_a (constant for the r2c
// layout); the synthesis-side step b_s equals b_a — see the b_s comment in
// heap_phase for the empirical validation of the (stretch-independent) scale.
// Both are named (not inlined) for later ear-tuning and so the scale can be
// re-checked against an offline LTFAT/PVDR reference render.
inline constexpr double kPghiTol      = 1e-6;
inline constexpr double kPghiFreqStep = 1.0;

// One entry in the PGHI integration max-heap (Algorithm 1). `mag` is the heap
// key. `bin` is the spectral bin. `current` distinguishes the two live frames:
// false => a PREVIOUS-frame significant bin, eligible to be time-stepped into
// the current frame; true => a CURRENT-frame bin already assigned, eligible to
// frequency-spread to its neighbors. Ordered by `mag` so the loudest available
// propagation path wins.
struct PghiHeapNode {
    double mag;
    int    bin;
    bool   current;
};

// get_alpha() returns tgt_dur / src_dur, the engine-internal
// alpha used by the phase vocoder.
// alpha < 1.0: output shorter than source (user's tempo value > 1, speedup).
// alpha > 1.0: output longer than source (user's tempo value < 1, slowdown).
// Note: the engine's alpha is the reciprocal of the tempo value the
// user authors in the warp marker file, which is consumed by the
// parser as delta_tgt = delta_src / (tempo * scale).
inline double get_alpha(size_t t_s, const std::vector<TimeMapSegment>& map) {
    if (map.empty()) return 1.0;
    if (t_s <= map.front().tgt_frame) return 1.0;
    for (size_t i = 0; i < map.size() - 1; ++i) {
        if (t_s >= map[i].tgt_frame && t_s < map[i+1].tgt_frame) {
            double tgt_dur = static_cast<double>(map[i+1].tgt_frame - map[i].tgt_frame);
            double src_dur = static_cast<double>(map[i+1].src_frame - map[i].src_frame);
            return tgt_dur / src_dur;
        }
    }
    return 1.0;
}

inline double map_source_to_target(size_t src_frame, const std::vector<TimeMapSegment>& map) {
    if (map.empty()) return static_cast<double>(src_frame);
    if (src_frame <= map.front().src_frame) return map.front().tgt_frame;
    for (size_t i = 0; i < map.size() - 1; ++i) {
        if (src_frame >= map[i].src_frame && src_frame < map[i+1].src_frame) {
            double src_dur = static_cast<double>(map[i+1].src_frame - map[i].src_frame);
            double tgt_dur = static_cast<double>(map[i+1].tgt_frame - map[i].tgt_frame);
            double offset = static_cast<double>(src_frame - map[i].src_frame);
            return map[i].tgt_frame + (offset * (tgt_dur / src_dur));
        }
    }
    const auto& last = map.back();
    if (src_frame >= last.src_frame) {
        return last.tgt_frame + (src_frame - last.src_frame);
    }
    return 0.0;
}

// Inverse of map_source_to_target: piecewise-linear interpolation over
// the same segment list, mapping a target-frame query back to its
// source-frame position. Used by the GUI's target view to translate
// per-column target-frame ranges into source-frame ranges for the
// shared waveform paint. Symmetric edge cases: clamp to the first
// segment for queries before the timemap's tgt start; identity past
// the last segment; empty map degenerates to identity.
inline double map_target_to_source(size_t tgt_frame, const std::vector<TimeMapSegment>& map) {
    if (map.empty()) return static_cast<double>(tgt_frame);
    if (tgt_frame <= map.front().tgt_frame) return map.front().src_frame;
    for (size_t i = 0; i < map.size() - 1; ++i) {
        if (tgt_frame >= map[i].tgt_frame && tgt_frame < map[i+1].tgt_frame) {
            double src_dur = static_cast<double>(map[i+1].src_frame - map[i].src_frame);
            double tgt_dur = static_cast<double>(map[i+1].tgt_frame - map[i].tgt_frame);
            double offset = static_cast<double>(tgt_frame - map[i].tgt_frame);
            return map[i].src_frame + (offset * (src_dur / tgt_dur));
        }
    }
    const auto& last = map.back();
    if (tgt_frame >= last.tgt_frame) {
        return last.src_frame + (tgt_frame - last.tgt_frame);
    }
    return 0.0;
}

// --- Output sample timing convention ---
// Both the phase vocoder pass (Pass 1) and synthesis (Pass 4) emit samples with the
// same OLA ramp-up trim: the first N/2 samples are dropped via `frames_to_skip = N/2`.
// Consequences any downstream module must respect:
//   - Output sample 0 in the final WAV corresponds to pre-trim OLA position N/2.
//   - A phase reset marker with synth_frame m lands at output sample m * R_s; the +N/2
//     window-center offset is absorbed by the trim, so diag spikes must NOT add it.
//   - Total output length = num_frames * R_s + N/2 - R_s.
//     Any auxiliary buffer sized to match the output (limiter meas_ola, diag WAVs)
//     must use this formula; target_total_frames describes the *input* plan, not
//     the emitted sample count.
//
// --- Central Pipeline Container ---
// Peak memory dominated by overlap-add buffers, FFTW planning, and phase vocoder state arrays.
struct AudioSTFT {
    // Source metadata
    SF_INFO src_info{};
    SNDFILE* src_snd = nullptr;
    // Default N=2560 (2^9*5): ~40 Hz practical bass floor for this
    // material gives 2.32 cycles/window, clearing the ~2-cycle phase-vocoder
    // coherence floor; phase resets cover quiet-section onsets. 2^9*5 is the
    // most FFTW-efficient factorization in the viable range. Must stay
    // divisible by 4 (R_s = N/4).
    int N = 2560;
    int R_s = 0;
    int channels = 0;
    double nyquist = 0.0;
    double bin_hz_width = 0.0;
    size_t target_total_frames = 0;

    // Timemap
    std::vector<TimeMapSegment> timemap;

    // Windows
    std::vector<double> window;
    std::vector<double> synth_window;

    // FFTW resources (shared across modules)
    double* fft_in = nullptr;
    fftw_complex* fft_out = nullptr;
    fftw_plan plan_fwd{};
    fftw_complex* ifft_in = nullptr;
    double* ifft_out = nullptr;
    fftw_plan plan_inv{};
    bool fftw_threads_inited = false;

    // Phase vocoder accumulators
    std::vector<std::vector<double>> phi_prev;
    std::vector<std::vector<double>> theta_prev;
    std::vector<std::vector<double>> overlap_add;

    // Virtual target buffer (Pass 1 output)
    std::vector<float> virtual_tgt_buf;

    // Phase reset markers
    std::vector<PhaseResetMarker> phase_reset_markers;

    // Spectral limiter
    LimiterParams limiter_params;
    int num_bands = 0;
    std::vector<int> bin_to_band;                        // size K = N/2+1
    std::vector<std::vector<double>> attenuation_map;    // [num_frames][num_bands]

    // Output path (derived from MD5 of source audio)
    std::string output_audio_file;
    // Mirrors EngineParams::limiter_mode. Synthesis derives the wav format
    // from this (Spectral/Peak → 24-bit PCM, None → 32-bit float) and decides
    // whether to wrap its write_cb through a PeakLimiter.
    LimiterMode limiter_mode = LimiterMode::None;
    double peak_limiter_ceiling_dbfs = -0.3;
    double peak_limiter_attack_ms    = 0.25;
    double peak_limiter_release_ms   = 0.5;

    // Cached frame map (populated once in main, reused by all passes)
    std::vector<int64_t> frame_map;

    // Optional cancellation hook. When non-null, synthesize_full checks
    // cancel_flag->load() at the top of every frame iteration; if true, it
    // sets cancellation_observed and returns early. Limiter::process and
    // engine.cpp then observe cancellation_observed at pass boundaries and
    // short-circuit further work. Set by run_warptempo_engine from its
    // optional parameter — left null for non-GUI callers (e.g. the parser
    // binary) so existing CLI tools are unaffected.
    const std::atomic<bool>* cancel_flag = nullptr;
    bool cancellation_observed = false;

    // --- Generate the canonical frame map ---
    // Centralizes the t_a accumulation logic to prevent floating-point drift
    // between modules. Returns int64_t sequence of t_a_rounded values.
    // t_s for frame m is implicitly m * R_s.
    // R_a_actual for frame m is frame_map[m] - frame_map[m-1] (caller derives).
    std::vector<int64_t> generate_frame_map() const {
        std::vector<int64_t> fmap;
        double t_a = -(double)N / 2.0;
        size_t t_s = 0;
        int idx = 0;
        // Forward-only cursor: O(segments + frames) instead of O(segments * frames)
        size_t seg = 0;

        while (t_s < target_total_frames) {
            // Advance cursor while the next segment starts at or before t_s
            while (timemap.size() >= 2 && seg + 2 < timemap.size() &&
                   t_s >= timemap[seg + 1].tgt_frame)
                ++seg;

            double alpha = 1.0;
            if (timemap.size() >= 2 && t_s > timemap.front().tgt_frame &&
                t_s >= timemap[seg].tgt_frame && t_s < timemap[seg + 1].tgt_frame) {
                double tgt_dur = static_cast<double>(timemap[seg + 1].tgt_frame - timemap[seg].tgt_frame);
                double src_dur = static_cast<double>(timemap[seg + 1].src_frame - timemap[seg].src_frame);
                alpha = tgt_dur / src_dur;
            }

            double R_a = R_s / alpha;
            if (idx > 0) t_a += R_a;
            int64_t t_a_rounded = static_cast<int64_t>(std::llround(t_a));
            fmap.push_back(t_a_rounded);

            t_s += R_s;
            idx++;
        }
        return fmap;
    }

    void init_fftw() {
        R_s = N / 4;
        window.resize(N);
        synth_window.resize(N);
        for (int n = 0; n < N; ++n) {
            window[n] = 0.5 * (1.0 - std::cos(2.0 * M_PI * n / (N - 1)));
            synth_window[n] = window[n] / 1.5;
        }

        fft_in = fftw_alloc_real(N);
        fft_out = fftw_alloc_complex(N / 2 + 1);
        plan_fwd = fftw_plan_dft_r2c_1d(N, fft_in, fft_out, FFTW_ESTIMATE);
        ifft_in = fftw_alloc_complex(N / 2 + 1);
        ifft_out = fftw_alloc_real(N);
        plan_inv = fftw_plan_dft_c2r_1d(N, ifft_in, ifft_out, FFTW_ESTIMATE);

        phi_prev.assign(channels, std::vector<double>(N / 2 + 1, 0.0));
        theta_prev.assign(channels, std::vector<double>(N / 2 + 1, 0.0));
        overlap_add.assign(channels, std::vector<double>(N, 0.0));

        // 1/3-octave bin-to-band lookup (centers at 1000 * 2^(n/3), 20 Hz .. Nyquist)
        const int K = N / 2 + 1;
        bin_to_band.assign(K, 0);
        std::vector<double> centers;
        int n_min = static_cast<int>(std::ceil(3.0 * std::log2(20.0 / 1000.0)));
        int n_max = static_cast<int>(std::floor(3.0 * std::log2(nyquist / 1000.0)));
        for (int n = n_min; n <= n_max; ++n)
            centers.push_back(1000.0 * std::pow(2.0, n / 3.0));
        if (centers.empty()) centers.push_back(1000.0);
        num_bands = static_cast<int>(centers.size());
        for (int k = 0; k < K; ++k) {
            double hz = k * bin_hz_width;
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

    void reset_phase_state() {
        for (int ch = 0; ch < channels; ++ch) {
            std::fill(phi_prev[ch].begin(), phi_prev[ch].end(), 0.0);
            std::fill(theta_prev[ch].begin(), theta_prev[ch].end(), 0.0);
            std::fill(overlap_add[ch].begin(), overlap_add[ch].end(), 0.0);
        }
    }

    // --- Phase computation, split into byte-preserving helpers --------------
    //
    // The historical fused phase_vocoder_frame is split into three pure helpers
    // so the lookahead pipeline (synthesis.cpp) can run analysis one frame
    // ahead of synthesis and dispatch the phase step per marker mode. The peak
    // helpers carry the EXACT float-operation sequence of the original fused
    // routine, so an all-peak / untagged render stays byte-identical (Gate 1).
    // None of these helpers touch the member phi_prev/theta_prev accumulators;
    // the pipeline owns all inter-frame state explicitly.

    // Analysis: window + forward FFT of one channel of frame_buf; extract
    // magnitude and analysis phase into M_out, phi_out (size K). Verbatim from
    // the original analysis block.
    void analyze_frame(int ch, int ch_stride, const float* frame_buf,
                       std::vector<double>& M_out,
                       std::vector<double>& phi_out) {
        const int K = N / 2 + 1;
        for (int n = 0; n < N; ++n)
            fft_in[n] = frame_buf[n * ch_stride + ch] * window[n];
        fftw_execute(plan_fwd);
        for (int k = 0; k < K; ++k) {
            M_out[k]   = std::hypot(fft_out[k][0], fft_out[k][1]);
            phi_out[k] = std::atan2(fft_out[k][1], fft_out[k][0]);
        }
    }

    // Peak (Laroche-Dolson) phase propagation for one frame. Reads M (current
    // magnitude, for peak detection), ph_cur/ph_prev (analysis phase n / n-1)
    // and th_prev (synthesis phase n-1); writes theta (size K). `seed` selects
    // the frame-0 seat (theta = phi). peaks is reused scratch. The arithmetic
    // is verbatim from the historical phase_vocoder_frame — DO NOT alter it;
    // Gate 1 (byte-identical untagged render) depends on it.
    void peak_phase(bool seed, int64_t R_a_actual,
                    const std::vector<double>& M,
                    const std::vector<double>& ph_prev,
                    const std::vector<double>& ph_cur,
                    const std::vector<double>& th_prev,
                    std::vector<double>& theta,
                    std::vector<int>& peaks) {
        const int K = N / 2 + 1;
        if (seed) {
            for (int k = 0; k < K; ++k) theta[k] = ph_cur[k];
            return;
        }
        peaks.clear();
        for (int k = 1; k < N / 2; ++k)
            if (M[k] > M[k - 1] && M[k] > M[k + 1]) peaks.push_back(k);
        if (peaks.empty()) peaks.push_back(N / 4);

        for (int p : peaks) {
            double omega_p = 2.0 * M_PI * p / N;
            theta[p] = th_prev[p] +
                       (omega_p + princarg(ph_cur[p] - ph_prev[p] - omega_p * R_a_actual) / R_a_actual) * R_s;
        }
        size_t peak_idx = 0;
        for (int k = 0; k < K; ++k) {
            if (peak_idx < peaks.size() - 1 &&
                std::abs(k - peaks[peak_idx + 1]) < std::abs(k - peaks[peak_idx]))
                ++peak_idx;
            int p = peaks[peak_idx];
            if (k != p) theta[k] = theta[p] + ph_cur[k] - ph_cur[p];
        }
    }

    // PGHI ("heap") phase propagation for one frame (Part 2). Same outputs as
    // peak_phase (writes theta for all k) plus dt_out, this frame's CENTERED
    // time-derivative (rad/sample), to be stored as the next frame's dt_prev.
    //
    //   seed   : frame-0 or immediately-after-a-reset frame. There is no prior
    //            synthesis phase to integrate from, so theta is seated to phi
    //            (identical to the peak frame-0/post-reset seat); dt_out is
    //            still computed for the following frame.
    //   frame0 : the file's first frame — has no real backward neighbor, so the
    //            time-derivative uses the forward difference only. (The only
    //            time-axis boundary; interior and mode-switch seams have real
    //            neighbors on both sides via the lookahead pipeline.)
    //   R_a_back/R_a_fwd : the actual backward/forward analysis hops (samples).
    //                      Their ratio to R_s carries the time-stretch into the
    //                      time integration; the frequency integration is
    //                      stretch-independent (b_s = b_a), so alpha is not a
    //                      parameter.
    //
    // Scratch (size K, reused across channels/frames): df_scratch, done_scratch,
    // heap_scratch. heap_scratch is cleared on entry.
    void heap_phase(bool seed, bool frame0,
                    int64_t R_a_back, int64_t R_a_fwd,
                    const std::vector<double>& mag_prev,
                    const std::vector<double>& mag_cur,
                    const std::vector<double>& ph_prev,
                    const std::vector<double>& ph_cur,
                    const std::vector<double>& ph_nxt,
                    const std::vector<double>& th_prev,
                    const std::vector<double>& dt_prev,
                    std::vector<double>& theta,
                    std::vector<double>& dt_out,
                    std::vector<double>& df_scratch,
                    std::vector<char>&   done_scratch,
                    std::vector<PghiHeapNode>& heap_scratch) {
        const int K = N / 2 + 1;

        // 2a. Centered time-derivative (instantaneous frequency) for every bin,
        // each half normalized by its own hop so an alpha change across a warp
        // marker needs no special handling. Stored for the next frame's dt_prev.
        const double inv_back = (R_a_back != 0) ? 1.0 / static_cast<double>(R_a_back) : 0.0;
        const double inv_fwd  = (R_a_fwd  != 0) ? 1.0 / static_cast<double>(R_a_fwd)  : 0.0;
        for (int p = 0; p < K; ++p) {
            const double omega_p = 2.0 * M_PI * p / N;
            const double freq_fwd =
                omega_p + princarg(ph_nxt[p] - ph_cur[p] - omega_p * R_a_fwd) * inv_fwd;
            if (frame0) {
                dt_out[p] = freq_fwd;            // forward-only at the file start
            } else {
                const double freq_back =
                    omega_p + princarg(ph_cur[p] - ph_prev[p] - omega_p * R_a_back) * inv_back;
                dt_out[p] = 0.5 * (freq_back + freq_fwd);
            }
        }

        // 2f. Reset / frame-0 seat: seed theta = phi, skip integration.
        if (seed) {
            for (int k = 0; k < K; ++k) theta[k] = ph_cur[k];
            return;
        }

        // 2b. Frequency-direction derivative (centered; one-sided at the DC and
        // Nyquist edges, which are inaudible). b_a = kPghiFreqStep.
        //
        // The expected per-one-bin phase progression must be demodulated out
        // before princarg, exactly as the time derivative subtracts omega_p*R_a
        // above. analyze_frame applies the window UN-shifted over [0,N) (no
        // fftshift), so the window is centered near sample n_c, not index 0;
        // that time-origin offset puts a linear-in-bin group-delay term on the
        // analysis phase (2*pi*n_c/N per one-bin step). Taking princarg of the
        // bare bin-to-bin difference would leave that constant on every step and
        // the heap would accumulate it into a phase ramp across frequency (the
        // rotating-Leslie sweep). The Hann is built symmetric over [0, N-1]
        // (init_fftw), so its center is n_c = (N-1)/2 — close to but not exactly
        // N/2; using pi would reintroduce a smaller rotation. expected_f is a
        // phase OFFSET subtracted before princarg and is orthogonal to inv_ba
        // (which scales the result into a per-unit-frequency gradient).
        const double inv_ba     = 1.0 / kPghiFreqStep;
        const double n_c        = 0.5 * (N - 1);
        const double expected_f = 2.0 * M_PI * n_c / N;
        for (int m = 0; m < K; ++m) {
            if (m == 0) {
                df_scratch[m] = princarg(ph_cur[1] - ph_cur[0] - expected_f) * inv_ba;
            } else if (m == K - 1) {
                df_scratch[m] = princarg(ph_cur[K - 1] - ph_cur[K - 2] - expected_f) * inv_ba;
            } else {
                const double dfb = princarg(ph_cur[m]     - ph_cur[m - 1] - expected_f) * inv_ba;
                const double dff = princarg(ph_cur[m + 1] - ph_cur[m]     - expected_f) * inv_ba;
                df_scratch[m] = 0.5 * (dfb + dff);
            }
        }

        // 2c/2d. Significance set I and the quiet-bin policy. abstol is relative
        // to the loudest bin across frames n and n-1.
        double peak_mag = 0.0;
        for (int k = 0; k < K; ++k) {
            if (mag_cur[k]  > peak_mag) peak_mag = mag_cur[k];
            if (mag_prev[k] > peak_mag) peak_mag = mag_prev[k];
        }
        const double abstol  = kPghiTol * peak_mag;
        const double half_Rs = 0.5 * static_cast<double>(R_s);
        // Synthesis-side frequency step. Time-stretching rescales the TIME grid
        // (the horizontal phase advances by R_s while the analysis hop is
        // R_s/alpha — that ratio carries the stretch via dt above); the
        // FREQUENCY grid is unchanged, so the frequency-direction integration
        // uses the analysis gradient as-is: b_s = b_a. This was validated
        // empirically against the trusted peak path — with b_s = b_a the
        // per-sample output energy of a stationary sinusoid and of a linear
        // chirp is stretch-invariant and tracks peak (ratio ~1.0 at stretch
        // 1.5), whereas b_s = alpha*b_a (the brief's literal text, which it
        // flagged for validation) and b_s = b_a/alpha both lose or inflate
        // energy under stretch. alpha therefore does not enter the vertical
        // integration.
        const double b_s = kPghiFreqStep;

        // 2e. POLICY (b): quiet bins (not in I) are TIME-PROPAGATED on their own
        // instantaneous frequency — never randomized, never copied. They keep
        // advancing exactly as under the peak model, so reverb/decay tails do
        // not develop spectral holes. The tolerance gate decides only who may
        // anchor a frequency-spread; everyone gets a phase. done==1 marks a bin
        // assigned (quiet bins immediately; significant bins via the heap).
        for (int k = 0; k < K; ++k) {
            if (mag_cur[k] > abstol) {
                done_scratch[k] = 0;                                  // in I
            } else {
                theta[k] = th_prev[k] + half_Rs * (dt_prev[k] + dt_out[k]);
                done_scratch[k] = 1;
            }
        }

        // Algorithm 1. Seed the heap with the previous frame's significant bins
        // (time-step candidates), keyed on their previous magnitude.
        auto cmp = [](const PghiHeapNode& a, const PghiHeapNode& b) {
            return a.mag < b.mag;                                     // max-heap on mag
        };
        heap_scratch.clear();
        for (int k = 0; k < K; ++k) {
            if (done_scratch[k] == 0) {
                heap_scratch.push_back({mag_prev[k], k, false});
                std::push_heap(heap_scratch.begin(), heap_scratch.end(), cmp);
            }
        }
        while (!heap_scratch.empty()) {
            std::pop_heap(heap_scratch.begin(), heap_scratch.end(), cmp);
            const PghiHeapNode node = heap_scratch.back();
            heap_scratch.pop_back();
            const int m = node.bin;
            if (!node.current) {
                // Previous-frame bin -> trapezoidal time-step into frame n.
                if (done_scratch[m] == 0) {
                    theta[m] = th_prev[m] + half_Rs * (dt_prev[m] + dt_out[m]);
                    done_scratch[m] = 1;
                    heap_scratch.push_back({mag_cur[m], m, true});
                    std::push_heap(heap_scratch.begin(), heap_scratch.end(), cmp);
                }
            } else {
                // Current-frame bin -> integrate the frequency gradient to its
                // still-unassigned significant neighbors (trapezoidal in df).
                for (int dir = 0; dir < 2; ++dir) {
                    const int nb = (dir == 0) ? m + 1 : m - 1;
                    if (nb < 0 || nb >= K) continue;
                    if (done_scratch[nb] != 0) continue;     // not in I, or done
                    // df_scratch holds the demodulated DEVIATION; the full
                    // synthesis-phase step per one-bin move is expected_f +
                    // deviation, mirroring how the time axis re-adds omega_p
                    // after its princarg (2a above). The expected progression is
                    // real vertical phase structure, not noise — integrating the
                    // deviation alone leaves a fixed per-step offset (a static
                    // comb / tap-delay and the attendant inter-bin cancellation).
                    const double dev  = 0.5 * b_s * (df_scratch[m] + df_scratch[nb]);
                    const double step = expected_f + dev;
                    // The (+step up / -step down) sign convention applies to the
                    // WHOLE step: stepping up a bin adds the expected progression,
                    // stepping down subtracts it — same direction as the deviation.
                    theta[nb] = theta[m] + ((nb == m + 1) ? step : -step);
                    done_scratch[nb] = 1;
                    heap_scratch.push_back({mag_cur[nb], nb, true});
                    std::push_heap(heap_scratch.begin(), heap_scratch.end(), cmp);
                }
            }
        }
    }

    // Synthesis: populate ifft_in from magnitude M and phase theta, applying
    // optional per-band attenuation. Verbatim from the historical
    // phase_vocoder_frame tail; both models share this one path so the
    // magnitude/spectrum step is identical regardless of how theta was derived.
    void populate_synth_spectrum(const std::vector<double>& M,
                                 const std::vector<double>& theta,
                                 const double* atten_row) {
        const int K = N / 2 + 1;
        if (atten_row) {
            for (int k = 0; k < K; ++k) {
                double scaled = M[k] * atten_row[bin_to_band[k]];
                ifft_in[k][0] = scaled * std::cos(theta[k]);
                ifft_in[k][1] = scaled * std::sin(theta[k]);
            }
        } else {
            for (int k = 0; k < K; ++k) {
                ifft_in[k][0] = M[k] * std::cos(theta[k]);
                ifft_in[k][1] = M[k] * std::sin(theta[k]);
            }
        }
    }

    void cleanup() {
        fftw_destroy_plan(plan_fwd);
        fftw_destroy_plan(plan_inv);
        fftw_free(fft_in);
        fftw_free(fft_out);
        fftw_free(ifft_in);
        fftw_free(ifft_out);
        if (fftw_threads_inited) fftw_cleanup_threads();
        if (src_snd) sf_close(src_snd);
    }
};
