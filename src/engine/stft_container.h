#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <fftw3.h>

#include "engine.h"
#include "render_profile.h"
#include "synth_spectrum_trig.h"
#include "warp_frame_map.h"

// --- Data Structures ---
struct PhaseResetPlacement {
    int synth_frame;
};

struct LimiterParams {
    double ceiling_dbfs          = -0.3;
    double tolerance_db          = 0.01;
};

struct SourceInfo {
    int     samplerate = 0;
    int     channels   = 0;
    int64_t frames     = 0;
};

// --- DSP Helpers ---
inline double princarg(double phase) {
    return phase - 2.0 * M_PI * std::floor((phase + M_PI) / (2.0 * M_PI));
}

// PGHI ("heap") tunables. kPghiTol is the significance threshold,
// relative to the per-frame-pair peak magnitude (the LTFAT default, ~-120 dB);
// it decides only which bins may ANCHOR a frequency-spread, never who gets a
// phase. kPghiFreqStep is the per-bin frequency step b_a (constant for the r2c
// layout) and is itself stretch-independent; the synthesis-side step is
// b_s = alpha * b_a, applied per-frame as the alpha_fp scaling in PGHI's
// frequency integration. See the b_s comment in pghi_integrate.
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

// --- Output sample timing convention ---
// Synthesis start-trims frames_to_skip = N/2 samples from the head. That N/2 is
// the alignment latency the origin-centered analysis convention contributes: a
// frame's content is centered at its window center, which the synthesis un-shift
// lands at OLA index N/2. Trimming exactly N/2 makes source frame 0 map to output
// frame 0. The remaining OLA ramp-up (the head region before overlap reaches
// unity) is intentionally NOT trimmed -- it is kept as a brief head fade-in over
// (near-)silent lead-in material; it is amplitude only and does not move
// alignment. Consequences any downstream module must respect:
//   - Output sample 0 in the final WAV corresponds to pre-trim OLA position N/2,
//     so a feature at source S lands at output sample tgt(S) (== map_source_to_
//     target(S)) -- frame-exact with an ideal time-stretch from frame 0 onward.
//   - The +N/2 head trim is uniform, so phase resets keep their relative
//     alignment to the audio.
//   - Total output length is AudioSTFT::emit_sample_cap: llrint of the map's
//     last anchor's target (warp_frame_map.back().tgt_frame), resolved in
//     engine.cpp on every path. (num_frames - 1) * R_s is not the output
//     length, and target_total_frames describes the *input* plan, not the
//     emitted sample count. Any auxiliary buffer sized to match the
//     output (limiter meas_ola) must use the actually emitted length (synthesis
//     out_frames), not a recomputed frame-count formula.
//
// --- Central Pipeline Container ---
// Peak memory dominated by the per-channel FFTW workspaces and, during
// synthesis, the channel-contiguous planar source copy and per-channel output
// streams.
struct AudioSTFT {
    AudioSTFT() = default;
    // Explicit cleanup calls remain on normal returns; the destructor covers
    // exception unwinds.
    ~AudioSTFT() { cleanup(); }
    AudioSTFT(const AudioSTFT&) = delete;
    AudioSTFT& operator=(const AudioSTFT&) = delete;

    // Source metadata
    SourceInfo src_info{};
    // Caller-owned interleaved float source (EngineParams::source_audio_samples).
    // Valid for the duration of run_warptempo_engine. src_info carries the
    // frame count, channel count, and sample rate that describe it.
    const float* src_samples = nullptr;
    // Default N=4096: the analysis window length and the OLA frame stride
    // (R_s = N/4) baseline. Must stay divisible by 4; 4096 = 2^12 is FFTW-clean.
    int N = 4096;
    // FFT length used by the forward/inverse plans. Set to 2*N in init_fftw to
    // give the analysis frame a centered zero-padded layout (Prusa-Holighaus):
    // the N-length window sits with its center at FFT index 0, and the
    // remaining M-N samples are zero. Doubles the bin density (bin_hz_width =
    // sr/M) and is what makes the heap-integration phase consistent on the
    // truncated-Gaussian-like Hann lobe in the bin grid.
    int M = 0;
    int R_s = 0;
    int channels = 0;
    double bin_hz_width = 0.0;
    size_t target_total_frames = 0;

    // Frame map
    std::vector<WarpFrameMapSegment> warp_frame_map;

    // Windows
    std::vector<double> window;
    std::vector<double> synth_window;

    // Per-channel FFTW scratch + plans. One workspace per channel so each
    // channel's analysis/synthesis transforms are independent — the
    // prerequisite for running channels on separate threads. All plans are
    // FFTW_ESTIMATE over the same M, so per-channel plans are numerically
    // identical to the single plan they replace (bit-identical render).
    struct FftWorkspace {
        double*       fft_in   = nullptr;   // length M
        fftw_complex* fft_out  = nullptr;   // length M/2+1
        fftw_plan     plan_fwd{};
        fftw_complex* ifft_in  = nullptr;   // length M/2+1
        double*       ifft_out = nullptr;   // length M
        fftw_plan     plan_inv{};
    };
    std::vector<FftWorkspace> fft_ws;       // size == channels
    bool fftw_threads_inited = false;

    // Phase reset placements: synth_frame is the synthesis frame the
    // placement seeds — theta re-grounds to the analysis phase there, and
    // that frame's analysis window centers at the authored reset.
    std::vector<PhaseResetPlacement> phase_reset_placements;

    // Spectral limiter
    LimiterParams limiter_params;

    // Per-synthesis-frame source read schedule, evaluated once in
    // engine.cpp and reused by every pass. source_frame_positions[m] is the
    // source read position for synthesis frame m -- map_target_to_source
    // sampled at t_s = m*R_s, minus the N/2 origin-centered analysis offset.
    // This is the dense, materialized form of the warp; the warp itself
    // (the piecewise-linear src->tgt segments) lives in the sparse segment
    // map, not here.
    //
    // "Frame" is used in two distinct senses, kept separate on purpose:
    //   - the INDEX m is a synthesis (STFT) frame -- one analysis/synthesis
    //     hop, at output sample position m*R_s;
    //   - the VALUE is a source sample-frame position (one multichannel
    //     sample in the source), the same unit as the phase-reset frames.
    // The sequence is monotonically non-decreasing, so engine.cpp can
    // std::upper_bound it to invert a source position back to a synthesis
    // frame for phase-reset placement; synthesis.cpp reads it forward as
    // source_frame_positions[m].
    std::vector<int64_t> source_frame_positions;

    // Emit cap: output length in samples, resolved at init from the map's
    // last anchor on every path (llrint of warp_frame_map.back().tgt_frame; a
    // cap that rounds to zero is refused at init). The synthesizer truncates
    // its emitted stream to this many samples so render length equals the
    // map's target length; synthesize_full reads it as resolved.
    int64_t emit_sample_cap = 0;

    // Optional cancellation hook. When non-null, synthesize_full checks
    // cancel_flag->load() at the top of every frame iteration; if true, it
    // sets cancellation_observed and returns early. Limiter::process checks
    // the flag through its pre-queue and per-peak phases the same way, and
    // engine.cpp re-checks the raw flag alongside cancellation_observed at
    // every pass boundary and before the Success return. Set by
    // run_warptempo_engine from its optional parameter — left null by
    // warptempo_cli, which has no kill semantics.
    const std::atomic<bool>* cancel_flag = nullptr;
    bool cancellation_observed = false;

    // --- Generate the canonical source frame positions ---
    // Centralizes the t_a accumulation logic to prevent floating-point drift
    // between modules. Returns the int64_t sequence of t_a_rounded values:
    // source_frame_positions[m], indexed by synthesis frame m (output sample
    // position m * R_s), valued in source sample-frames.
    // R_a_actual for frame m is source_frame_positions[m] -
    // source_frame_positions[m-1] (caller derives).
    // generate_source_frame_positions (like phase_reset_placements above)
    // keeps its unprefixed name: the analysis schedule and the placement list
    // are engine-internal, not artifacts, so they sit outside the
    // warp/phase-reset artifact naming columns.
    std::vector<int64_t> generate_source_frame_positions() const {
        // Synthesis frame m sits at output position t_s = m * R_s. Its source
        // read position is the exact inverse-warp_frame_map value at t_s, minus the
        // N/2 origin-centered analysis offset:
        //     t_a(m) = map_target_to_source(m * R_s) - N/2.
        // map_target_to_source is piecewise-linear and splits exactly at every
        // warp segment boundary, so a synthesis hop that straddles a tempo
        // change gets the true source advance on each side of the boundary --
        // no per-hop alpha sampling, no boundary quantization drift. This is
        // identical to the old right-Riemann accumulation inside any single
        // constant-alpha segment; it differs only on boundary-crossing hops,
        // which is exactly the error being removed. R_a_actual for frame m is
        // still source_frame_positions[m] - source_frame_positions[m-1]
        // (caller derives); near a boundary that difference is now the exact
        // straddled advance.
        std::vector<int64_t> positions;
        if (R_s > 0)
            positions.reserve(target_total_frames / static_cast<size_t>(R_s) + 1);
        for (size_t t_s = 0; t_s < target_total_frames; t_s += R_s) {
            double src = map_target_to_source(static_cast<double>(t_s), warp_frame_map)
                       - static_cast<double>(N) / 2.0;
            // Round half-to-even (llrint), the project-wide convention; do not
            // reintroduce llround (half-away-from-zero) here.
            positions.push_back(static_cast<int64_t>(std::llrint(src)));
        }
        return positions;
    }

    void init_fftw() {
        R_s = N / 4;
        M = 2 * N;
        bin_hz_width = static_cast<double>(src_info.samplerate) / M;
        window.resize(N);
        synth_window.resize(N);
        // Periodic Hann (denominator N, not N - 1); at R_s = N/4 the four
        // overlapping squared windows sum to exactly 3/2 at every sample, so the
        // /1.5 synthesis window is the exact Prusa-Holighaus dual (their eq. 11
        // with constant denominator) and the OLA round trip is unity with no
        // COLA ripple.
        for (int n = 0; n < N; ++n) {
            window[n] = 0.5 * (1.0 - std::cos(2.0 * M_PI * n / N));
            synth_window[n] = window[n] / 1.5;
        }

        fft_ws.assign(channels, FftWorkspace{});
        for (int ch = 0; ch < channels; ++ch) {
            FftWorkspace& w = fft_ws[ch];
            w.fft_in   = fftw_alloc_real(M);
            std::fill(w.fft_in, w.fft_in + M, 0.0);
            w.fft_out  = fftw_alloc_complex(M / 2 + 1);
            // FFTW_ESTIMATE is a determinism invariant: it builds a fixed plan,
            // whereas FFTW_MEASURE/FFTW_PATIENT time the host and may pick a
            // different algorithm run-to-run, breaking bit-identical output.
            w.plan_fwd = fftw_plan_dft_r2c_1d(M, w.fft_in, w.fft_out, FFTW_ESTIMATE);
            w.ifft_in  = fftw_alloc_complex(M / 2 + 1);
            w.ifft_out = fftw_alloc_real(M);
            w.plan_inv = fftw_plan_dft_c2r_1d(M, w.ifft_in, w.ifft_out, FFTW_ESTIMATE);
        }

    }

    // --- Phase computation helpers ------------------------------------------
    //
    // The synthesis pipeline runs a one-deep analysis lookahead. The analysis
    // producer computes per-bin PGHI prep from the analysis stream, and the
    // consumer integrates phases and populates the synthesis spectrum. The
    // pipeline owns all inter-frame state in run_channel locals (these helpers
    // hold no inter-frame accumulators of their own).

    // Analysis: window + forward FFT of one channel from its planar source
    // slice (planar_ch, the channel-contiguous source copy) starting at sample
    // ta; extract magnitude and analysis phase into M_out, phi_out (size K =
    // M/2+1).
    // Origin-centered, zero-padded placement (Prusa-Holighaus): the windowed
    // frame's center sits at FFT index 0 and the rest of the M-length buffer
    // is zero. Clearing the whole buffer each frame is cheap next to the
    // M-point FFT. Centered placement makes the per-bin phase carry no
    // group-delay ramp (expected_f == 0 in PGHI) — which is the condition
    // under which PGHI's heap integration actually converges.
    void analyze_frame(int ch, const float* planar_ch,
                       int64_t ta, int64_t src_frames,
                       std::vector<double>& M_out,
                       std::vector<double>& phi_out) {
        FftWorkspace& w = fft_ws[ch];
        const int K = M / 2 + 1;
        const int half = N / 2;
        // The centered placement (n - half + M) % M for n in [0, navail), navail <= N,
        // only ever writes [0, N/2) and [M - N/2, M). The middle gap was zeroed once
        // at allocation and is never written, so clearing the two end regions is a
        // full clear of every sample any frame can have written.
        std::fill(w.fft_in, w.fft_in + half, 0.0);
        std::fill(w.fft_in + M - half, w.fft_in + M, 0.0);
        // Whole-frame guard: a frame whose start is out of [0, src_frames) is
        // entirely zero; a valid frame reads min(N, src_frames - ta) samples
        // and zero-pads the tail.
        if (ta >= 0 && ta < src_frames) {
            const int navail =
                static_cast<int>(std::min<int64_t>(N, src_frames - ta));
            // The centered index (n - half + M) % M resolves to two contiguous
            // disjoint ranges (same split the synthesis-side un-shift uses in
            // synthesis.cpp): n in [0, half) writes [M - half, M), and n in
            // [half, navail) writes [0, navail - half). Bit-identical to the
            // modulo write, minus the per-sample hardware divide. navail can be
            // < N at the file tail, hence the n_head clamp: for navail <= half
            // the second loop is empty, matching the modulo loop's coverage.
            const int n_head = std::min(navail, half);
            for (int n = 0; n < n_head; ++n)
                w.fft_in[M - half + n] = planar_ch[ta + n] * window[n];
            for (int n = n_head; n < navail; ++n)
                w.fft_in[n - half] = planar_ch[ta + n] * window[n];
        }
        fftw_execute(w.plan_fwd);
        for (int k = 0; k < K; ++k) {
            // |re|, |im| are bounded by a few thousand at audio FFT scales, so
            // re*re + im*im is nowhere near double's overflow/underflow limits;
            // sqrt of the sum matches hypot to within one ulp without hypot's
            // scaling guard.
            const double re = w.fft_out[k][0];
            const double im = w.fft_out[k][1];
            M_out[k]   = std::sqrt(re * re + im * im);
            phi_out[k] = std::atan2(w.fft_out[k][1], w.fft_out[k][0]);
        }
    }

    // Producer-side PGHI prep for one frame on the centered, zero-padded STFT.
    // Writes dt_out (this frame's centered time-derivative), df_out (this
    // frame's frequency derivative), and quiet_out (1 for sub-tolerance quiet
    // bins, 0 for significant bins). These are pure per-bin functions of the
    // analysis stream and hop schedule, so the analysis producer ships them to
    // the consumer with the lookahead frame.
    //
    //   frame0 : the file's first frame — no real backward neighbor, so the
    //            time-derivative falls back to the forward difference.
    //   R_a_back/R_a_fwd : actual backward/forward analysis hops used by the
    //                      centered time derivative; the same hop pair is
    //                      supplied to integration for the frequency-spread
    //                      alpha.
    void pghi_prep(bool frame0,
                   int64_t R_a_back, int64_t R_a_fwd,
                   const std::vector<double>& mag_prev,
                   const std::vector<double>& mag_cur,
                   const std::vector<double>& ph_prev,
                   const std::vector<double>& ph_cur,
                   const std::vector<double>& ph_nxt,
                   std::vector<double>& dt_out,
                   std::vector<double>& df_out,
                   std::vector<char>&   quiet_out) {
        const int K = M / 2 + 1;

        // Centered time-derivative (instantaneous frequency) for every bin,
        // each half normalized by its own hop so an alpha change across a warp
        // marker needs no special handling. omega_p is the per-bin angular
        // frequency on the M-point grid. Stored for the next frame's dt_prev.
        const double inv_back = (R_a_back != 0) ? 1.0 / static_cast<double>(R_a_back) : 0.0;
        const double inv_fwd  = (R_a_fwd  != 0) ? 1.0 / static_cast<double>(R_a_fwd)  : 0.0;
        for (int p = 0; p < K; ++p) {
            const double omega_p = 2.0 * M_PI * p / M;
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

        // Frequency-direction derivative (centered; one-sided at DC and
        // Nyquist). With origin-centered analysis the window is centered at
        // FFT index 0, so the per-bin expected phase progression is zero —
        // the bare princarg difference IS the gradient (in units of b_a;
        // b_a = kPghiFreqStep = 1, so no scaling).
        for (int m = 0; m < K; ++m) {
            if (m == 0) {
                df_out[m] = princarg(ph_cur[1] - ph_cur[0]);
            } else if (m == K - 1) {
                df_out[m] = princarg(ph_cur[K - 1] - ph_cur[K - 2]);
            } else {
                const double dfb = princarg(ph_cur[m]     - ph_cur[m - 1]);
                const double dff = princarg(ph_cur[m + 1] - ph_cur[m]);
                df_out[m] = 0.5 * (dfb + dff);
            }
        }

        // Significance set I and the quiet-bin policy. abstol is relative to
        // the loudest bin across frames n and n-1.
        double peak_mag = 0.0;
        for (int k = 0; k < K; ++k) {
            if (mag_cur[k]  > peak_mag) peak_mag = mag_cur[k];
            if (mag_prev[k] > peak_mag) peak_mag = mag_prev[k];
        }
        const double abstol  = kPghiTol * peak_mag;

        // Prusa Alg. 1 line 3 classifies sub-tolerance ("quiet") bins before
        // heap integration. The consumer still performs quiet-bin RNG draws so
        // the draw stream stays in synthesis frame/bin order.
        for (int k = 0; k < K; ++k) {
            if (mag_cur[k] > abstol) {
                quiet_out[k] = 0;                                  // in I
            } else {
                quiet_out[k] = 1;
            }
        }
    }

    // Consumer-side PGHI phase integration for one frame (Prusa-Holighaus
    // Alg. 1). The producer supplies dt_cur, df, and quiet. The consumer keeps
    // the seed seating, quiet-bin RNG draws, and heap propagation so RNG state
    // and sequential phase dependencies stay local to synthesis.
    //
    //   seed   : frame-0 or immediately-after-a-reset frame. No prior synthesis
    //            phase to integrate from, so theta seats to phi and no quiet-bin
    //            draws are consumed.
    //   R_a_back/R_a_fwd : actual backward/forward analysis hops. Their ratio
    //                      to R_s is alpha (the stretch); used as b_s =
    //                      alpha * b_a in the frequency-spread step (Prusa).
    //
    // Scratch (size K, reused): done_scratch, heap_scratch.
    void pghi_integrate(bool seed,
                        int64_t R_a_back, int64_t R_a_fwd,
                        const std::vector<double>& mag_prev,
                        const std::vector<double>& mag_cur,
                        const std::vector<double>& ph_cur,
                        const std::vector<double>& th_prev,
                        const std::vector<double>& dt_prev,
                        const std::vector<double>& dt_cur,
                        const std::vector<double>& df,
                        const std::vector<char>& quiet,
                        std::vector<double>& theta,
                        std::vector<char>&   done_scratch,
                        std::vector<PghiHeapNode>& heap_scratch,
                        std::mt19937& rng,
                        PghiProfile* prof = nullptr) {
        const int K = M / 2 + 1;

        // Profiling (temporary): count/clock only when prof is non-null.
        std::chrono::steady_clock::time_point prof_t_entry, prof_t_quiet;
        if (prof) {
            prof->frames += 1;
            prof_t_entry = std::chrono::steady_clock::now();
        }

        // Per-frame alpha = R_s / R_a, the synthesis-to-analysis hop ratio.
        // Used as b_s = alpha * b_a in the vertical (frequency) integration
        // step below; the time-axis integration carries alpha implicitly via
        // the actual hop in the principal-arg demodulation.
        const double alpha_fp =
            (R_a_back > 0) ? static_cast<double>(R_s) / static_cast<double>(R_a_back)
          : (R_a_fwd  > 0) ? static_cast<double>(R_s) / static_cast<double>(R_a_fwd)
          : 1.0;

        // Reset / frame-0 seat: seed theta = phi, skip integration.
        if (seed) {
            for (int k = 0; k < K; ++k) theta[k] = ph_cur[k];
            if (prof) {
                prof->seed_frames += 1;
                prof->heap_s += std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - prof_t_entry).count();
            }
            return;
        }

        const double half_Rs = 0.5 * static_cast<double>(R_s);

        // Prusa Alg. 1 line 3: sub-tolerance ("quiet") bins are assigned a
        // uniform-random synthesis phase rather than time-propagated. Random
        // here is the published choice — quiet bins carry no coherent
        // structure, so freshly randomized phase is what keeps the residual
        // floor decorrelated from frame to frame. Significant bins (in I)
        // get their phase via the heap below.
        // Countdown of still-unassigned significant bins (the set I); the drain
        // loop below runs while it is nonzero (Algorithm 1). Also read by the
        // profiler to classify inert pops.
        long long remaining = 0;
        std::uniform_real_distribution<double> quiet_dist(-M_PI, M_PI);
        for (int k = 0; k < K; ++k) {
            if (quiet[k] == 1) {
                theta[k] = quiet_dist(rng);
                done_scratch[k] = 1;
                if (prof) prof->quiet_bins += 1;
            } else {
                done_scratch[k] = 0;                                  // in I
                ++remaining;
                if (prof) prof->significant_bins += 1;
            }
        }
        if (prof) {
            prof_t_quiet = std::chrono::steady_clock::now();
            prof->quiet_s += std::chrono::duration<double>(
                prof_t_quiet - prof_t_entry).count();
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
        // Drain while I is nonempty (Algorithm 1, tracked by `remaining`). The
        // !empty() conjunct is defensive only: every bin in I is seeded with its
        // previous-frame node, so the heap cannot empty while remaining > 0. Any
        // nodes still in the heap at exit are provably inert — a previous-frame
        // node finds its bin already done, and a current-frame node finds no
        // undone significant neighbor — so none can write theta; heap_scratch.clear()
        // at the top of the next call discards them.
        while (remaining != 0 && !heap_scratch.empty()) {
            std::pop_heap(heap_scratch.begin(), heap_scratch.end(), cmp);
            const PghiHeapNode node = heap_scratch.back();
            heap_scratch.pop_back();
            if (prof) {
                prof->pops_total += 1;
                if (remaining == 0) prof->pops_inert += 1;
            }
            const int m = node.bin;
            if (!node.current) {
                // Previous-frame bin -> trapezoidal time-step into frame n.
                if (done_scratch[m] == 0) {
                    theta[m] = th_prev[m] + half_Rs * (dt_prev[m] + dt_cur[m]);
                    done_scratch[m] = 1;
                    --remaining;
                    heap_scratch.push_back({mag_cur[m], m, true});
                    std::push_heap(heap_scratch.begin(), heap_scratch.end(), cmp);
                }
            } else {
                // Current-frame bin -> integrate the frequency gradient (scaled
                // by b_s = alpha * b_a) to its still-unassigned significant
                // neighbors (trapezoidal in df). Centered convention -> no
                // expected_f offset; the gradient is the whole step.
                for (int dir = 0; dir < 2; ++dir) {
                    const int nb = (dir == 0) ? m + 1 : m - 1;
                    if (nb < 0 || nb >= K) continue;
                    if (done_scratch[nb] != 0) continue;     // not in I, or done
                    const double step = alpha_fp * 0.5 * (df[m] + df[nb]);
                    theta[nb] = theta[m] + ((nb == m + 1) ? step : -step);
                    done_scratch[nb] = 1;
                    --remaining;
                    heap_scratch.push_back({mag_cur[nb], nb, true});
                    std::push_heap(heap_scratch.begin(), heap_scratch.end(), cmp);
                }
            }
        }
        if (prof) {
            prof->heap_s += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - prof_t_quiet).count();
        }
    }

    // Synthesis: populate ifft_in from magnitude mag and phase theta. Caller
    // IFFTs and un-shifts to recover the origin-centered time-domain frame.
    void populate_synth_spectrum(int ch,
                                 const std::vector<double>& mag,
                                 const std::vector<double>& theta) {
        FftWorkspace& w = fft_ws[ch];
        const int K = M / 2 + 1;
        // Separate cos+sin evaluated through the vectorized helper: the
        // two-output sincos does not auto-vectorize, whereas the helper's
        // dedicated -ffast-math TU lets GCC lower the pair to glibc libmvec
        // (the Arch/glibc-only charter is what licenses that dependency). The
        // helper writes all K bins including the two endpoints; the correction
        // loop below then overwrites those two.
        synth_spectrum_trig(theta.data(), mag.data(), w.ifft_in, K);
        // Hermitian-endpoint correction, applied AFTER the interior loop so it
        // overwrites the two endpoint coefficients the helper wrote. The
        // half-spectrum of an even-length real
        // transform is Hermitian; the DC bin (k == 0) and the Nyquist bin
        // (k == K-1) have no imaginary degree of freedom, and FFTW's c2r
        // ignores whatever imaginary part sits there. The old code wrote
        // mag[k]*(cos theta, sin theta) at those two bins, so c2r effectively
        // synthesized mag[k]*cos(theta[k]) — a non-real theta scaled the
        // endpoint magnitude by |cos(theta)| (down toward zero near a quarter
        // turn) instead of rotating it. Project theta onto the only legal
        // endpoint phase set {0, pi}: keep the analyzed magnitude and let the
        // sign of cos pick the nearer legal phase. copysign carries the tie:
        // cos(theta) == 0.0 (theta at pi/2 to double precision) yields
        // copysign(mag, +0.0) = +mag — a deterministic, documented choice.
        //
        // Determinism: this touches ONLY the two endpoint coefficients per
        // frame per channel. The quiet-bin RNG draws, theta, the propagation
        // heap, and every phase-domain input are untouched (draw alignment is
        // a standing determinism ruling), so the write is a pure function of
        // the same inputs and holds bit-for-bit run to run. Seed frames carry
        // real endpoint phases from the r2c analysis (cos is exactly +/-1
        // there), so their endpoints are byte-identical through this change.
        for (const int k : {0, K - 1}) {
            w.ifft_in[k][0] = std::copysign(mag[k], std::cos(theta[k]));
            w.ifft_in[k][1] = 0.0;
        }
    }

    void cleanup() {
        for (auto& w : fft_ws) {
            fftw_destroy_plan(w.plan_fwd);
            fftw_destroy_plan(w.plan_inv);
            fftw_free(w.fft_in);  fftw_free(w.fft_out);
            fftw_free(w.ifft_in); fftw_free(w.ifft_out);
        }
        fft_ws.clear();
        if (fftw_threads_inited) {
            fftw_cleanup_threads();
            fftw_threads_inited = false;
        }
    }
};
