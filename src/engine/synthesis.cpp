#include "synthesis.h"
#include "peak_limiter.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

void Synthesis::synthesize_full(
    AudioSTFT& stft,
    std::complex<float>* spectra_cache,
    std::function<void(const float*, size_t)> write_cb,
    bool show_progress,
    const char* pass_label) {
    const int N          = stft.N;
    const int Mfft       = stft.M;
    const int R_s        = stft.R_s;
    const int channels   = stft.channels;
    const int K          = Mfft / 2 + 1;
    const auto& fm       = stft.frame_map;
    const int num_frames = static_cast<int>(fm.size());

    // --- Env-gated sub-stage profiling --------------------------------------
    // Five ns counters per channel (held in chprof, below), reduced across
    // channels after the per-channel passes complete, plus a single
    // write/limiter timer for the one interleaved write_cb. The totals are
    // aggregate over the whole render (both channels folded in). The [profile]
    // line is emitted to std::cerr only when WARPTEMPO_PROFILE is set (runtime
    // env gating only — no compile-time macro).
    //
    // CAVEAT: the two now() reads per sub-stage per frame slightly inflate the
    // cheapest stages (synthspec, ola) relative to the expensive ones. The
    // FFT (ifft, analysis) and heap figures are the accurate ones and the ones
    // we care about; this is acceptable for a first-cut breakdown.
    const bool prof = (std::getenv("WARPTEMPO_PROFILE") != nullptr);
    using prof_clock = std::chrono::steady_clock;
    auto prof_ns = [](prof_clock::time_point a, prof_clock::time_point b) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
    };

    const int64_t src_frames = stft.src_info.frames;
    // Planar source: channel-contiguous float copy of the whole source, read
    // once. Replaces the per-frame sf_seek/sf_readf in analyze_into (each
    // sample was previously read ~4x through libsndfile). Bit-identical: same
    // floats, just resident in RAM.
    std::vector<float> planar(static_cast<size_t>(channels) *
                              static_cast<size_t>(src_frames));
    {
        constexpr int64_t kChunk = 1 << 16;                  // frames per read
        std::vector<float> stage(static_cast<size_t>(kChunk) * channels);
        sf_seek(stft.src_snd, 0, SEEK_SET);
        int64_t got = 0, pos = 0;
        while (pos < src_frames &&
               (got = sf_readf_float(stft.src_snd, stage.data(),
                       std::min<int64_t>(kChunk, src_frames - pos))) > 0) {
            for (int64_t f = 0; f < got; ++f)
                for (int ch = 0; ch < channels; ++ch)
                    planar[static_cast<size_t>(ch) * src_frames + (pos + f)] =
                        stage[static_cast<size_t>(f) * channels + ch];
            pos += got;
        }
    }

    // Emitted output length (timing-convention: (num_frames-1)*R_s).
    const int64_t out_frames =
        (num_frames > 0) ? static_cast<int64_t>(num_frames - 1) * R_s : 0;

    // One mono output stream per channel. Each channel computes its whole
    // output independently into its own buffer; the streams are interleaved
    // only at the end. This is the channel-major restructure (brief C.1): a
    // pure reorganization of the previously frame-major loop, with each
    // channel's pipeline state now fully local to run_channel so brief C.2
    // can hand each channel to its own thread.
    std::vector<std::vector<float>> mono(channels);
    struct ChProf { int64_t analysis=0, heap=0, synthspec=0, ifft=0, ola=0; };
    std::vector<ChProf> chprof(channels);
    std::vector<char> ch_cancelled(channels, 0);

    // Per-channel synthesis pass. Touches only read-only shared inputs (planar,
    // fm, stft.phase_reset_markers, stft.attenuation_map, stft.fft_ws[ch],
    // stft.window/synth_window) and writes only mono[ch], chprof[ch],
    // ch_cancelled[ch]. Every buffer that was shared across channels in the old
    // frame-major loop (theta/dt/df/done scratch, the heap scratch, the OLA
    // row, the analysis-state rows, the RNG) is now a local here — that
    // locality is exactly what makes C.2 safe.
    auto run_channel = [&](int ch) {
        const int K2 = K;
        // --- One-deep lookahead analysis pipeline ---------------------------
        // Analysis runs one frame ahead of synthesis so PGHI's centered
        // time-derivative can read the NEXT frame's analysis phase. This is
        // INTERNAL pipeline latency only: synthesis frame m still OLA-adds into
        // output position m*R_s. All inter-frame phase state is held in these
        // per-channel buffers; the heap helper reads them directly.
        std::vector<double> ph_prev(K2,0.0), ph_cur(K2,0.0), ph_nxt(K2,0.0);
        std::vector<double> mag_prev(K2,0.0), mag_cur(K2,0.0), mag_nxt(K2,0.0);
        std::vector<double> th_prev(K2,0.0), dt_prev(K2,0.0);
        // Per-channel scratch (was shared and serially reused across channels).
        std::vector<double> theta(K2), dt_scratch(K2), df_scratch(K2);
        std::vector<char>   done_scratch(K2);
        std::vector<PghiHeapNode> heap_scratch; heap_scratch.reserve(K2);
        // Per-channel quiet-bin RNG. Each stream is consumed only by its own
        // channel, in bin order, so the draw sequence is identical whether
        // channels run serially (now) or on separate threads (C.2) — which is
        // what keeps C bit-identical to B. Seed scheme matches brief B exactly;
        // the golden-ratio stride just separates the seeds.
        std::mt19937 rng(static_cast<std::uint32_t>(
            0x5715E11u ^ (static_cast<std::uint32_t>(ch) * 0x9E3779B9u)));
        const float* psrc = &planar[static_cast<size_t>(ch) * src_frames];

        std::vector<double> ola(N, 0.0);
        std::vector<float>& out = mono[ch];
        out.reserve(static_cast<size_t>(out_frames));

        // t_a for analysis-frame index `aidx`. Beyond the last synthesis frame
        // the timemap range is exhausted (alpha == 1), so each extra
        // analysis-only frame advances by R_s. We never read more than one
        // frame past EOF: that single analysis-only frame supplies the last
        // emitted frame's phi_next and is never itself emitted, so the total
        // emitted length is unchanged.
        auto ta_for = [&](int aidx) -> int64_t {
            if (aidx < num_frames) return fm[aidx];
            return fm[num_frames - 1] +
                   static_cast<int64_t>(R_s) * (aidx - (num_frames - 1));
        };
        // analysis folds in the two one-time priming calls below as well as the
        // per-frame in-loop call — all analysis goes through this lambda.
        auto analyze1 = [&](int aidx, std::vector<double>& md,
                                      std::vector<double>& pd) {
            const auto _a0 = prof_clock::now();
            stft.analyze_frame(ch, psrc, ta_for(aidx), src_frames, md, pd);
            chprof[ch].analysis += prof_ns(_a0, prof_clock::now());
        };

        // Prime: analysis frames 0 and 1 (frame 1 is the EOF analysis-only
        // frame when there is only a single synthesis frame). ph_prev stays
        // zero — frame 0 is a seed, so it needs no phi_prev.
        int64_t ta_prev = 0, ta_cur = 0, ta_nxt = 0;
        if (num_frames >= 1) {
            analyze1(0, mag_cur, ph_cur);
            analyze1(1, mag_nxt, ph_nxt);
            ta_cur = ta_for(0);
            ta_nxt = ta_for(1);
        }

        int  phase_reset_cursor = 0;
        // `prev_reset` carries "the previous frame fired a reset" into the next
        // iteration so heap_phase reseeds theta = phi on the post-reset frame.
        bool prev_reset = false;
        // Start-trim: N samples = N/2 of OLA ramp-up plus N/2 of latency added
        // by the origin-centered analysis convention (see the timing-convention
        // block in stft_container.h).
        int  frames_to_skip = N;
        int  progress_stride = std::max(100, num_frames / 100);
        int  last_pct = -1;

        for (int frame_idx = 0; frame_idx < num_frames; ++frame_idx) {
            // Cooperative cancellation: stft.cancel_flag is set by the GUI when
            // the user presses Esc during a render. Worst-case cancel-to-stop
            // latency is one frame — well below human perception.
            if (stft.cancel_flag && stft.cancel_flag->load()) {
                ch_cancelled[ch] = 1;
                return;
            }
            // Pipeline invariant at loop top: ph_cur/mag_cur = analysis(frame),
            // ph_nxt/mag_nxt = analysis(frame+1), ph_prev/mag_prev =
            // analysis(frame-1) (zero at frame 0).
            const int64_t R_a_actual = (frame_idx > 0) ? (ta_cur - ta_prev) : 0;
            const int64_t R_a_fwd    = ta_nxt - ta_cur;
            const bool    frame0     = (frame_idx == 0);
            const bool    seed_heap  = frame0 || prev_reset;
            const double* atten_row  = stft.attenuation_map[frame_idx].data();

            const auto _h0 = prof_clock::now();
            stft.heap_phase(seed_heap, frame0, R_a_actual, R_a_fwd,
                            mag_prev, mag_cur, ph_prev, ph_cur, ph_nxt,
                            th_prev, dt_prev, theta, dt_scratch,
                            df_scratch, done_scratch, heap_scratch, rng);
            chprof[ch].heap += prof_ns(_h0, prof_clock::now());
            // dt_scratch (this frame's dt) becomes the next frame's dt_prev.
            dt_prev.swap(dt_scratch);

            const auto _s0 = prof_clock::now();
            stft.populate_synth_spectrum(ch, mag_cur, theta, atten_row);
            chprof[ch].synthspec += prof_ns(_s0, prof_clock::now());

            if (spectra_cache) {
                std::complex<float>* dst = spectra_cache +
                    (static_cast<size_t>(frame_idx) * channels + ch) * K;
                const fftw_complex* src = stft.fft_ws[ch].ifft_in;
                for (int k = 0; k < K; ++k)
                    dst[k] = std::complex<float>(static_cast<float>(src[k][0]),
                                                 static_cast<float>(src[k][1]));
            }

            // IFFT length M; un-shift the centered frame back into the [0, N)
            // OLA window (the inverse of analyze_frame's placement).
            const auto _i0 = prof_clock::now();
            fftw_execute(stft.fft_ws[ch].plan_inv);
            chprof[ch].ifft += prof_ns(_i0, prof_clock::now());

            const auto _o0 = prof_clock::now();
            const double inv_M = 1.0 / Mfft;
            const int half = N / 2;
            const double* io = stft.fft_ws[ch].ifft_out;
            for (int n = 0; n < N; ++n) {
                const double v = io[(n - half + Mfft) % Mfft];
                ola[n] += (v * inv_M) * stft.synth_window[n];
            }
            chprof[ch].ola += prof_ns(_o0, prof_clock::now());

            // End-of-frame per-channel state shift. theta -> th_prev; ph_cur ->
            // ph_prev and ph_nxt -> ph_cur (mag likewise). After this, ph_prev
            // holds frame_idx's analysis phase, ready for the next heap call.
            th_prev = theta;
            ph_prev.swap(ph_cur);
            ph_cur.swap(ph_nxt);
            mag_prev.swap(mag_cur);
            mag_cur.swap(mag_nxt);

            // Emit this frame's R_s samples (less the start-trim) into mono out.
            int write_offset = 0, write_len = R_s;
            if (frames_to_skip > 0) {
                if (frames_to_skip >= write_len) { frames_to_skip -= write_len; write_len = 0; }
                else { write_offset = frames_to_skip; write_len -= frames_to_skip; frames_to_skip = 0; }
            }
            for (int n = write_offset; n < write_offset + write_len; ++n)
                out.push_back(static_cast<float>(ola[n]));

            std::memmove(ola.data(), ola.data() + R_s,
                         static_cast<size_t>(N - R_s) * sizeof(double));
            std::fill(ola.data() + (N - R_s), ola.data() + N, 0.0);

            // Progress is reported by channel 0 only; under C.2 it'll be
            // approximate, which is fine (cosmetic).
            if (show_progress && ch == 0 && num_frames > 0 &&
                (frame_idx % progress_stride) == 0) {
                int pct = static_cast<int>((frame_idx * 100LL) / num_frames);
                if (pct != last_pct) {
                    std::cout << "\r" << pass_label << pct << "%" << std::flush;
                    last_pct = pct;
                }
            }

            // Phase reset. The post-reset frame re-grounds via seed_heap
            // (theta = phi). The loop only advances the marker cursor and
            // records that a reset fired so `prev_reset` seeds the next frame.
            // Channel-independent: each channel walks its own cursor.
            bool reset_fired = false;
            while (phase_reset_cursor < static_cast<int>(stft.phase_reset_markers.size()) &&
                   stft.phase_reset_markers[phase_reset_cursor].synth_frame == frame_idx) {
                ++phase_reset_cursor;
                reset_fired = true;
            }
            prev_reset = reset_fired;

            // Advance the analysis pipeline by one frame. Only needed while
            // another synthesis frame follows; the EOF analysis-only frame
            // (index == num_frames) is reached when frame_idx == num_frames-2,
            // supplying the last frame's phi_next. Each spectrum is analyzed
            // exactly once.
            if (frame_idx + 1 < num_frames) {
                ta_prev = ta_cur;
                ta_cur  = ta_nxt;
                ta_nxt  = ta_for(frame_idx + 2);
                analyze1(frame_idx + 2, mag_nxt, ph_nxt);
            }
        }

        // Final tail: the last N-R_s samples of the OLA window.
        const int remaining = N - R_s;
        for (int n = 0; n < remaining; ++n)
            out.push_back(static_cast<float>(ola[n]));
    };

    // Run the channels (serial, this brief; C.2 threads them).
    for (int ch = 0; ch < channels; ++ch) run_channel(ch);

    for (int ch = 0; ch < channels; ++ch) {
        if (ch_cancelled[ch]) {
            stft.cancellation_observed = true;
            return;                      // matches the old early-return on cancel
        }
    }
    if (show_progress) std::cout << "\r" << pass_label << "100%\n";

    // Interleave the per-channel mono streams and emit in one write_cb call.
    // All channels emit out_frames samples; the downstream write_cb
    // (sf_writef / buffer-append / peak-limiter) is order-preserving and
    // chunk-agnostic, so one big call is identical to the old per-frame calls.
    int64_t t_write = 0;
    if (out_frames > 0) {
        std::vector<float> inter(static_cast<size_t>(out_frames) * channels);
        for (int ch = 0; ch < channels; ++ch) {
            const std::vector<float>& m = mono[ch];
            assert(static_cast<int64_t>(m.size()) == out_frames);
            for (int64_t f = 0; f < out_frames; ++f)
                inter[static_cast<size_t>(f) * channels + ch] = m[static_cast<size_t>(f)];
        }
        const auto _w0 = prof_clock::now();
        write_cb(inter.data(), static_cast<size_t>(out_frames));
        t_write += prof_ns(_w0, prof_clock::now());
    }

    if (prof) {
        int64_t t_analysis=0, t_heap=0, t_synthspec=0, t_ifft=0, t_ola=0;
        for (int ch = 0; ch < channels; ++ch) {
            t_analysis += chprof[ch].analysis; t_heap += chprof[ch].heap;
            t_synthspec += chprof[ch].synthspec; t_ifft += chprof[ch].ifft;
            t_ola += chprof[ch].ola;
        }
        const int64_t total = t_analysis + t_heap + t_synthspec +
                              t_ifft + t_ola + t_write;
        const double denom = total > 0 ? static_cast<double>(total) : 1.0;
        auto pct = [&](int64_t v) { return 100.0 * v / denom; };
        std::cerr << "[profile] synth:"
                  << " analysis=" << (t_analysis / 1e6) << "(" << pct(t_analysis) << "%)"
                  << " heap="     << (t_heap     / 1e6) << "(" << pct(t_heap)     << "%)"
                  << " synthspec="<< (t_synthspec/ 1e6) << "(" << pct(t_synthspec)<< "%)"
                  << " ifft="     << (t_ifft     / 1e6) << "(" << pct(t_ifft)     << "%)"
                  << " ola="      << (t_ola      / 1e6) << "(" << pct(t_ola)      << "%)"
                  << " write/limiter=" << (t_write / 1e6) << "(" << pct(t_write) << "%)"
                  << " total="    << (total      / 1e6) << "\n";
    }
}

void Synthesis::process(AudioSTFT& stft) {
    SF_INFO tgt_info = stft.src_info;
    tgt_info.format = SF_FORMAT_WAV |
        (stft.limiter_mode != LimiterMode::None
            ? SF_FORMAT_PCM_24 : SF_FORMAT_FLOAT);

    SNDFILE* output_snd = sf_open(stft.output_audio_file.c_str(), SFM_WRITE, &tgt_info);
    if (!output_snd) {
        std::cerr << "  ! could not open output '" << stft.output_audio_file << "'\n";
        return;
    }

    auto write_to_file = [output_snd](const float* buf, size_t n_frames) {
        sf_writef_float(output_snd, buf, static_cast<sf_count_t>(n_frames));
    };

    if (stft.limiter_mode == LimiterMode::Peak) {
        PeakLimiter pl(stft.peak_limiter_ceiling_dbfs,
                       stft.peak_limiter_attack_ms,
                       stft.peak_limiter_release_ms,
                       stft.src_info.samplerate,
                       stft.channels);
        auto write_through_limiter = [&](const float* buf, size_t n_frames) {
            pl.process(buf, n_frames, write_to_file);
        };
        synthesize_full(stft, nullptr, write_through_limiter,
                        /*show_progress=*/true,
                        /*pass_label=*/"[Pass 2/3] Synthesis........................ ");
        pl.flush(write_to_file);
    } else {
        synthesize_full(stft, nullptr, write_to_file,
                        /*show_progress=*/true,
                        /*pass_label=*/"[Pass 2/3] Synthesis........................ ");
    }
    sf_close(output_snd);
}

void Synthesis::process_to_buffer(AudioSTFT& stft,
                                  std::vector<float>* output_buffer) {
    const int channels = stft.channels;
    auto append_to_buffer = [output_buffer, channels](const float* buf,
                                                      size_t n_frames) {
        output_buffer->insert(
            output_buffer->end(), buf,
            buf + n_frames * static_cast<size_t>(channels));
    };
    // Mirror Synthesis::process: when limiter_mode == Peak, wrap the
    // append in a PeakLimiter so the target render's audio is
    // brick-walled at the configured ceiling. The spectral limiter is
    // skipped on the buffer path (Pass 2 is gated off in engine.cpp);
    // the peak limiter is the only limiter that runs here.
    if (stft.limiter_mode == LimiterMode::Peak) {
        PeakLimiter pl(stft.peak_limiter_ceiling_dbfs,
                       stft.peak_limiter_attack_ms,
                       stft.peak_limiter_release_ms,
                       stft.src_info.samplerate,
                       stft.channels);
        auto write_through_limiter = [&](const float* buf, size_t n_frames) {
            pl.process(buf, n_frames, append_to_buffer);
        };
        synthesize_full(stft, nullptr, write_through_limiter,
                        /*show_progress=*/true,
                        /*pass_label=*/"[Pass 2/3] Synthesis........................ ");
        pl.flush(append_to_buffer);
    } else {
        synthesize_full(stft, nullptr, append_to_buffer,
                        /*show_progress=*/true,
                        /*pass_label=*/"[Pass 2/3] Synthesis........................ ");
    }
}

void Synthesis::write_render_to_file(AudioSTFT& stft,
                                     const std::vector<float>& render) {
    SF_INFO tgt_info = stft.src_info;
    // Spectral disk path -> 24-bit PCM (same non-None decision as process()).
    tgt_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;

    SNDFILE* output_snd = sf_open(stft.output_audio_file.c_str(), SFM_WRITE, &tgt_info);
    if (!output_snd) {
        std::cerr << "  ! could not open output '" << stft.output_audio_file << "'\n";
        return;
    }

    auto write_to_file = [output_snd](const float* buf, size_t n_frames) {
        sf_writef_float(output_snd, buf, static_cast<sf_count_t>(n_frames));
    };

    // Always-after backstop: catches the sub-dB residual the spectral limiter's
    // per-peak cap leaves plus any rare blip. Free on compliant material.
    PeakLimiter pl(stft.peak_limiter_ceiling_dbfs,
                   stft.peak_limiter_attack_ms,
                   stft.peak_limiter_release_ms,
                   stft.src_info.samplerate,
                   stft.channels);
    const size_t total_frames = stft.channels > 0
        ? render.size() / static_cast<size_t>(stft.channels) : 0;
    pl.process(render.data(), total_frames, write_to_file);
    pl.flush(write_to_file);
    sf_close(output_snd);
}
