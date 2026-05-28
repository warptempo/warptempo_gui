#include "synthesis.h"
#include "peak_limiter.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <iostream>
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

    // --- One-deep lookahead analysis pipeline -------------------------------
    // Analysis runs one frame ahead of synthesis so PGHI's centered
    // time-derivative can read the NEXT frame's analysis phase. This is
    // INTERNAL pipeline latency only: synthesis frame m still OLA-adds into
    // output position m*R_s. All inter-frame phase state is held in these
    // loop-local per-channel buffers; the heap helper reads them directly.
    std::vector<std::vector<double>> ph_prev (channels, std::vector<double>(K, 0.0));
    std::vector<std::vector<double>> ph_cur  (channels, std::vector<double>(K, 0.0));
    std::vector<std::vector<double>> ph_nxt  (channels, std::vector<double>(K, 0.0));
    std::vector<std::vector<double>> mag_prev(channels, std::vector<double>(K, 0.0));
    std::vector<std::vector<double>> mag_cur (channels, std::vector<double>(K, 0.0));
    std::vector<std::vector<double>> mag_nxt (channels, std::vector<double>(K, 0.0));
    std::vector<std::vector<double>> th_prev (channels, std::vector<double>(K, 0.0));
    std::vector<std::vector<double>> dt_prev (channels, std::vector<double>(K, 0.0));

    std::vector<double> theta(K), dt_scratch(K), df_scratch(K);
    std::vector<char>   done_scratch(K);
    std::vector<PghiHeapNode> heap_scratch;
    heap_scratch.reserve(K);

    std::vector<float> a_read(N * channels, 0.0f);

    // t_a for analysis-frame index `aidx`. Beyond the last synthesis frame the
    // timemap range is exhausted (alpha == 1), so each extra analysis-only
    // frame advances by R_s. We never read more than one frame past EOF: that
    // single analysis-only frame supplies the last emitted frame's phi_next and
    // is never itself emitted, so the total emitted length is unchanged.
    auto ta_for = [&](int aidx) -> int64_t {
        if (aidx < num_frames) return fm[aidx];
        return fm[num_frames - 1] +
               static_cast<int64_t>(R_s) * (aidx - (num_frames - 1));
    };
    auto analyze_into = [&](int aidx,
                            std::vector<std::vector<double>>& magd,
                            std::vector<std::vector<double>>& phid) {
        const int64_t ta = ta_for(aidx);
        std::fill(a_read.begin(), a_read.end(), 0.0f);
        if (ta >= 0 && ta < stft.src_info.frames) {
            sf_seek(stft.src_snd, ta, SEEK_SET);
            sf_readf_float(stft.src_snd, a_read.data(), N);
        }
        for (int ch = 0; ch < channels; ++ch)
            stft.analyze_frame(ch, channels, a_read.data(), magd[ch], phid[ch]);
    };

    // Prime: analysis frames 0 and 1 (frame 1 is the EOF analysis-only frame
    // when there is only a single synthesis frame). ph_prev stays zero — frame
    // 0 is a seed and consumes no time recursion, so it needs no phi_prev.
    int64_t ta_prev = 0, ta_cur = 0, ta_nxt = 0;
    if (num_frames >= 1) {
        analyze_into(0, mag_cur, ph_cur);
        analyze_into(1, mag_nxt, ph_nxt);
        ta_cur = ta_for(0);
        ta_nxt = ta_for(1);
    }

    int phase_reset_cursor = 0;
    // `prev_reset` carries "the previous frame fired a reset" into the next
    // iteration so heap_phase reseats theta = phi on the post-reset frame.
    bool prev_reset = false;

    std::vector<std::vector<double>> ola_out(channels, std::vector<double>(N, 0.0));

    std::vector<float> write_buf(N * channels, 0.0f);

    // Start-trim: N samples = N/2 of OLA ramp-up plus N/2 of latency added by
    // the origin-centered analysis convention (see the timing-convention block
    // in stft_container.h).
    int frames_to_skip = N;

    // Progress reporting every ~1% of frames (or every 100 frames, whichever is rarer).
    int progress_stride = std::max(100, num_frames / 100);
    int last_pct = -1;

    for (int frame_idx = 0; frame_idx < num_frames; ++frame_idx) {
        // Cooperative cancellation: stft.cancel_flag is set by the GUI when
        // the user presses Esc during a render. Worst-case cancel-to-stop
        // latency is one frame (100-300 us on the target hardware) — well
        // below human perception.
        if (stft.cancel_flag && stft.cancel_flag->load()) {
            stft.cancellation_observed = true;
            return;
        }
        // Pipeline invariant at loop top: ph_cur/mag_cur = analysis(frame_idx),
        // ph_nxt/mag_nxt = analysis(frame_idx+1), ph_prev/mag_prev =
        // analysis(frame_idx-1) (zero at frame 0). R_a values follow from the
        // tracked t_a's, identical to the old fm[m]-fm[m-1].
        const int64_t R_a_actual = (frame_idx > 0) ? (ta_cur - ta_prev) : 0;
        const int64_t R_a_fwd    = ta_nxt - ta_cur;
        const bool    frame0     = (frame_idx == 0);
        const bool    seed_heap  = frame0 || prev_reset;

        const double* atten_row = stft.attenuation_map[frame_idx].data();

        for (int ch = 0; ch < channels; ++ch) {
            stft.heap_phase(seed_heap, frame0, R_a_actual, R_a_fwd,
                            mag_prev[ch], mag_cur[ch],
                            ph_prev[ch], ph_cur[ch], ph_nxt[ch],
                            th_prev[ch], dt_prev[ch],
                            theta, dt_scratch, df_scratch, done_scratch,
                            heap_scratch);
            // dt_scratch (this frame's dt) becomes the next frame's dt_prev.
            dt_prev[ch].swap(dt_scratch);
            stft.populate_synth_spectrum(mag_cur[ch], theta, atten_row);

            if (spectra_cache) {
                std::complex<float>* dst = spectra_cache +
                    (static_cast<size_t>(frame_idx) * channels + ch) * K;
                for (int k = 0; k < K; ++k) {
                    dst[k] = std::complex<float>(
                        static_cast<float>(stft.ifft_in[k][0]),
                        static_cast<float>(stft.ifft_in[k][1]));
                }
            }

            // IFFT length M; un-shift the centered frame back into the
            // [0, N) OLA window (the inverse of analyze_frame's placement).
            // synth_window stays length N — only the N samples nearest the
            // window center participate in OLA; the M-N zero-padded tail of
            // the IFFT is discarded along with its (1/M)-scaled energy.
            fftw_execute(stft.plan_inv);
            const double inv_M = 1.0 / Mfft;
            const int half = N / 2;
            for (int n = 0; n < N; ++n) {
                const double v = stft.ifft_out[(n - half + Mfft) % Mfft];
                ola_out[ch][n] += (v * inv_M) * stft.synth_window[n];
            }

            // End-of-frame per-channel state shift. theta -> th_prev (this
            // frame's synth phase, for the next frame); ph_cur -> ph_prev and
            // ph_nxt -> ph_cur (mag likewise). After this, ph_prev holds
            // frame_idx's analysis phase, which the reset re-seat below reads.
            th_prev[ch] = theta;
            ph_prev[ch].swap(ph_cur[ch]);
            ph_cur[ch].swap(ph_nxt[ch]);
            mag_prev[ch].swap(mag_cur[ch]);
            mag_cur[ch].swap(mag_nxt[ch]);
        }

        // Phase-reset re-seat. theta_prev seats from ph_prev (which holds
        // frame_idx's analysis phase after the end-of-frame swap above), so
        // heap_phase on the NEXT frame sees seed_heap=true and reseats
        // theta = phi. Marker mode is ignored — the engine is heap-only.
        bool reset_fired = false;
        while (phase_reset_cursor < static_cast<int>(stft.phase_reset_markers.size()) &&
               stft.phase_reset_markers[phase_reset_cursor].synth_frame == frame_idx) {
            for (int c = 0; c < channels; ++c)
                th_prev[c] = ph_prev[c];
            ++phase_reset_cursor;
            reset_fired = true;
        }
        prev_reset = reset_fired;

        // Advance the analysis pipeline by one frame for the next iteration.
        // Only needed while another synthesis frame follows; the EOF
        // analysis-only frame (index == num_frames) is reached here when
        // frame_idx == num_frames-2, supplying the last frame's phi_next. Each
        // analyzed spectrum is computed exactly once.
        if (frame_idx + 1 < num_frames) {
            ta_prev = ta_cur;
            ta_cur  = ta_nxt;
            ta_nxt  = ta_for(frame_idx + 2);
            analyze_into(frame_idx + 2, mag_nxt, ph_nxt);
        }

        int write_offset = 0, write_len = R_s;
        if (frames_to_skip > 0) {
            if (frames_to_skip >= write_len) {
                frames_to_skip -= write_len;
                write_len = 0;
            } else {
                write_offset   = frames_to_skip;
                write_len     -= frames_to_skip;
                frames_to_skip = 0;
            }
        }
        for (int n = write_offset; n < write_offset + write_len; ++n) {
            for (int ch = 0; ch < channels; ++ch) {
                double v = ola_out[ch][n];
                write_buf[(n - write_offset) * channels + ch] = static_cast<float>(v);
            }
        }
        if (write_len > 0)
            write_cb(write_buf.data(), static_cast<size_t>(write_len));

        for (int ch = 0; ch < channels; ++ch) {
            std::memmove(ola_out[ch].data(), ola_out[ch].data() + R_s,
                         static_cast<size_t>(N - R_s) * sizeof(double));
            std::fill(ola_out[ch].data() + (N - R_s), ola_out[ch].data() + N, 0.0);
        }

        // Live progress via carriage return, gated on show_progress.
        if (show_progress && num_frames > 0 && (frame_idx % progress_stride) == 0) {
            int pct = static_cast<int>((frame_idx * 100LL) / num_frames);
            if (pct != last_pct) {
                std::cout << "\r" << pass_label << pct << "%" << std::flush;
                last_pct = pct;
            }
        }
    }

    const int remaining = N - R_s;
    if (remaining > 0) {
        for (int ch = 0; ch < channels; ++ch) {
            for (int n = 0; n < remaining; ++n) {
                double v = ola_out[ch][n];
                write_buf[n * channels + ch] = static_cast<float>(v);
            }
        }
        write_cb(write_buf.data(), static_cast<size_t>(remaining));
    }

    if (show_progress) {
        std::cout << "\r" << pass_label << "100%\n";
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
                        /*pass_label=*/"[Pass 3/3] Synthesis........................ ");
        pl.flush(write_to_file);
    } else {
        synthesize_full(stft, nullptr, write_to_file,
                        /*show_progress=*/true,
                        /*pass_label=*/"[Pass 3/3] Synthesis........................ ");
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
                        /*pass_label=*/"[Pass 3/3] Synthesis........................ ");
        pl.flush(append_to_buffer);
    } else {
        synthesize_full(stft, nullptr, append_to_buffer,
                        /*show_progress=*/true,
                        /*pass_label=*/"[Pass 3/3] Synthesis........................ ");
    }
}
