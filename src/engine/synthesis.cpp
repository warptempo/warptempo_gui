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
#include <thread>
#include <vector>

// Apply the peak-limiter backstop to `buf` in place. One-shot process+flush, so
// it is sample-for-sample identical to the previous streaming application in
// write_render_to_file. Ceiling = stft.peak_limiter_ceiling_dbfs (hardcoded 0
// dBFS) - a pure clip net above the spectral limiter's -0.3.
void apply_peak_backstop(AudioSTFT& stft, std::vector<float>& buf) {
    const int channels = stft.channels;
    const size_t total_frames =
        channels > 0 ? buf.size() / static_cast<size_t>(channels) : 0;
    if (total_frames == 0) return;
    PeakLimiter pl(stft.peak_limiter_ceiling_dbfs,
                   stft.peak_limiter_attack_ms,
                   stft.peak_limiter_release_ms,
                   stft.src_info.samplerate, channels);
    std::vector<float> out;
    out.reserve(buf.size());
    auto sink = [&](const float* p, size_t n) {
        out.insert(out.end(), p, p + n * static_cast<size_t>(channels));
    };
    pl.process(buf.data(), total_frames, sink);
    pl.flush(sink);
    buf.swap(out);
}

void Synthesis::synthesize_full(
    AudioSTFT& stft,
    std::function<void(const float*, size_t)> write_cb,
    bool show_progress,
    const char* pass_label) {
    const int N          = stft.N;
    const int Mfft       = stft.M;
    const int R_s        = stft.R_s;
    const int channels   = stft.channels;
    const int K          = Mfft / 2 + 1;
    const auto& fm       = stft.source_frame_positions;
    const int num_frames = static_cast<int>(fm.size());
    // Synthesis frame window [wbegin, wend): the half-open range of frames this
    // pass actually emits. num_frames stays the full map size and ta_for stays
    // absolute over the full fm; only the emit range narrows. Defaults describe
    // the whole map (full-render behavior); engine.cpp narrows synth_frame_end
    // when EngineParams::emit_sample_cap is set (a trimmed render). wend == 0
    // means "unset" (a caller that bypassed engine.cpp's resolution) -> treat
    // as the full map.
    const int wbegin = stft.synth_frame_begin;
    const int wend   = (stft.synth_frame_end > 0) ? stft.synth_frame_end
                                                  : num_frames;
    const int wcount = wend - wbegin;

    // --- Env-gated sub-stage profiling --------------------------------------
    // Five ns counters per channel (held in chprof, below), summed across the
    // channel threads after the per-channel passes complete, plus a single
    // write/limiter timer for the one interleaved write_cb. Because the channels
    // run concurrently, the summed work_sum reads ~channel-count times the real
    // elapsed time; wall (measured below) is the true elapsed ms of the threaded
    // compute+write region, so the two can't be confused. The [profile] line is
    // emitted to std::cerr only when WARPTEMPO_PROFILE is set (runtime env gating
    // only — no compile-time macro).
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
    // Planar source: channel-contiguous float copy of the whole source,
    // deinterleaved once directly from the caller-owned interleaved buffer.
    // Bit-identical: same floats, just resident in RAM.
    std::vector<float> planar(static_cast<size_t>(channels) *
                              static_cast<size_t>(src_frames));
    {
        const float* src = stft.src_samples;
        for (int64_t f = 0; f < src_frames; ++f)
            for (int ch = 0; ch < channels; ++ch)
                planar[static_cast<size_t>(ch) * src_frames + f] =
                    src[static_cast<size_t>(f) * channels + ch];
    }

    // Emitted output length: (wcount-1)*R_s plus the N/2 the reduced head trim
    // leaves in, then capped at stft.emit_sample_cap so the file ends at the
    // window's target position (render length == target length). See the
    // timing-convention block in stft_container.h. mono_len is the uncapped
    // per-channel push total (what each run_channel actually appends); the
    // reserve uses it so the cap never under-reserves the mono buffer.
    const int64_t mono_len =
        (wcount > 0) ? static_cast<int64_t>(wcount - 1) * R_s + N / 2 : 0;
    int64_t out_frames = mono_len;
    if (stft.emit_sample_cap > 0 && stft.emit_sample_cap < out_frames)
        out_frames = stft.emit_sample_cap;

    // One mono output stream per channel. Each channel computes its whole
    // output independently into its own buffer; the streams are interleaved
    // only at the end. This is the channel-major restructure: a
    // pure reorganization of the previously frame-major loop, with each
    // channel's pipeline state fully local to run_channel so each
    // channel can be handed to its own thread.
    std::vector<std::vector<float>> mono(channels);
    struct ChProf { int64_t analysis=0, heap=0, synthspec=0, ifft=0, ola=0; };
    std::vector<ChProf> chprof(channels);
    std::vector<char> ch_cancelled(channels, 0);

    // Per-channel synthesis pass. Touches only read-only shared inputs (planar,
    // fm, stft.phase_reset_markers, stft.fft_ws[ch],
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
        // channels run serially or on separate threads — which is
        // what keeps the threaded output bit-identical to the serial output.
        // The golden-ratio stride just separates the seeds.
        std::mt19937 rng(static_cast<std::uint32_t>(
            0x5715E11u ^ (static_cast<std::uint32_t>(ch) * 0x9E3779B9u)));
        const float* psrc = &planar[static_cast<size_t>(ch) * src_frames];

        std::vector<double> ola(N, 0.0);
        std::vector<float>& out = mono[ch];
        out.reserve(static_cast<size_t>(mono_len));

        // t_a for analysis-frame index `aidx`. Beyond the last synthesis frame
        // the frame_map range is exhausted (alpha == 1), so each extra
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

        // Prime: analysis frames wbegin and wbegin+1 (wbegin+1 is the
        // analysis-only frame when the window is a single synthesis frame).
        // ph_prev stays zero — the window's first frame is a seed, so it needs
        // no phi_prev (same as frame 0 on the full path).
        int64_t ta_prev = 0, ta_cur = 0, ta_nxt = 0;
        if (wcount >= 1) {
            analyze1(wbegin,     mag_cur, ph_cur);
            analyze1(wbegin + 1, mag_nxt, ph_nxt);
            ta_cur = ta_for(wbegin);
            ta_nxt = ta_for(wbegin + 1);
        }

        // Phase reset cursor: skip markers placed before the window so the
        // in-loop equality check (synth_frame == frame_idx) lines up. Markers
        // before wbegin are dropped; markers at or after wend never match a
        // frame_idx in range and so never fire.
        int  phase_reset_cursor = 0;
        while (phase_reset_cursor <
                   static_cast<int>(stft.phase_reset_markers.size()) &&
               stft.phase_reset_markers[phase_reset_cursor].synth_frame < wbegin)
            ++phase_reset_cursor;
        // `prev_reset` carries "the previous frame fired a reset" into the next
        // iteration so heap_phase reseeds theta = phi on the post-reset frame.
        bool prev_reset = false;
        // Start-trim: N/2 samples -- the origin-centered analysis alignment
        // latency only, so source frame 0 maps to output frame 0. The OLA
        // ramp-up is intentionally NOT trimmed; it is kept as a brief head
        // fade-in (see the timing-convention block in stft_container.h).
        int  frames_to_skip = N / 2;
        int  progress_stride = std::max(100, wcount / 100);
        int  last_pct = -1;

        for (int frame_idx = wbegin; frame_idx < wend; ++frame_idx) {
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
            const int64_t R_a_actual = (frame_idx > wbegin) ? (ta_cur - ta_prev) : 0;
            const int64_t R_a_fwd    = ta_nxt - ta_cur;
            const bool    frame0     = (frame_idx == wbegin);
            const bool    seed_heap  = frame0 || prev_reset;

            const auto _h0 = prof_clock::now();
            stft.heap_phase(seed_heap, frame0, R_a_actual, R_a_fwd,
                            mag_prev, mag_cur, ph_prev, ph_cur, ph_nxt,
                            th_prev, dt_prev, theta, dt_scratch,
                            df_scratch, done_scratch, heap_scratch, rng);
            chprof[ch].heap += prof_ns(_h0, prof_clock::now());
            // dt_scratch (this frame's dt) becomes the next frame's dt_prev.
            dt_prev.swap(dt_scratch);

            const auto _s0 = prof_clock::now();
            stft.populate_synth_spectrum(ch, mag_cur, theta);
            chprof[ch].synthspec += prof_ns(_s0, prof_clock::now());

            // IFFT length M; un-shift the centered frame back into the [0, N)
            // OLA window (the inverse of analyze_frame's placement). With n in
            // [0, N) and Mfft = 2N the index resolves to two contiguous ranges,
            // so the split below replaces the per-sample modulo with no change to
            // the loads, multiplies, or accumulation order.
            const auto _i0 = prof_clock::now();
            fftw_execute(stft.fft_ws[ch].plan_inv);
            chprof[ch].ifft += prof_ns(_i0, prof_clock::now());

            const auto _o0 = prof_clock::now();
            const double inv_M = 1.0 / Mfft;
            const int half = N / 2;
            const double* io = stft.fft_ws[ch].ifft_out;
            for (int n = 0; n < half; ++n) {
                const double v = io[n + Mfft - half];
                ola[n] += (v * inv_M) * stft.synth_window[n];
            }
            for (int n = half; n < N; ++n) {
                const double v = io[n - half];
                ola[n] += (v * inv_M) * stft.synth_window[n];
            }
            chprof[ch].ola += prof_ns(_o0, prof_clock::now());

            // End-of-frame per-channel state shift. theta -> th_prev (a swap:
            // heap_phase fully overwrites theta before reading it next frame, so
            // the stale th_prev the swap leaves in theta is dead on arrival);
            // ph_cur -> ph_prev and ph_nxt -> ph_cur (mag likewise). After this,
            // ph_prev holds frame_idx's analysis phase, ready for the next heap call.
            th_prev.swap(theta);
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
            if (show_progress && ch == 0 && wcount > 0 &&
                ((frame_idx - wbegin) % progress_stride) == 0) {
                int pct = static_cast<int>(((frame_idx - wbegin) * 100LL) / wcount);
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
            // another synthesis frame follows within the window; the window's
            // analysis-only frame (index == wend) is reached when frame_idx ==
            // wend-2, supplying the last emitted frame's phi_next. ta_for
            // supplies fm[wend] when wend < num_frames and the clamped tail
            // otherwise. Each spectrum is analyzed exactly once.
            if (frame_idx + 1 < wend) {
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

    // Run each channel's pipeline concurrently. ch 0 runs on this (main) thread
    // so its progress output isn't interleaved; ch 1..n-1 get worker threads.
    // All per-channel state is private to run_channel and fft_ws[ch] is
    // per-channel, so the passes are independent — see the race audit in the
    // brief. Join before the interleave below.
    const auto _wall0 = prof_clock::now();
    {
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(std::max(0, channels - 1)));
        for (int ch = 1; ch < channels; ++ch)
            workers.emplace_back(run_channel, ch);
        run_channel(0);
        for (auto& w : workers) w.join();
    }

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
            assert(static_cast<int64_t>(m.size()) >= out_frames);
            for (int64_t f = 0; f < out_frames; ++f)
                inter[static_cast<size_t>(f) * channels + ch] = m[static_cast<size_t>(f)];
        }
        const auto _w0 = prof_clock::now();
        write_cb(inter.data(), static_cast<size_t>(out_frames));
        t_write += prof_ns(_w0, prof_clock::now());
    }

    const int64_t t_wall = prof_ns(_wall0, prof_clock::now());
    if (prof) {
        int64_t t_analysis=0, t_heap=0, t_synthspec=0, t_ifft=0, t_ola=0;
        for (int ch = 0; ch < channels; ++ch) {
            t_analysis += chprof[ch].analysis; t_heap += chprof[ch].heap;
            t_synthspec += chprof[ch].synthspec; t_ifft += chprof[ch].ifft;
            t_ola += chprof[ch].ola;
        }
        const int64_t work_sum = t_analysis + t_heap + t_synthspec +
                                 t_ifft + t_ola + t_write;
        const double denom = work_sum > 0 ? static_cast<double>(work_sum) : 1.0;
        auto pct = [&](int64_t v) { return 100.0 * v / denom; };
        std::cerr << "[profile] synth:"
                  << " analysis=" << (t_analysis / 1e6) << "(" << pct(t_analysis) << "%)"
                  << " heap="     << (t_heap     / 1e6) << "(" << pct(t_heap)     << "%)"
                  << " synthspec="<< (t_synthspec/ 1e6) << "(" << pct(t_synthspec)<< "%)"
                  << " ifft="     << (t_ifft     / 1e6) << "(" << pct(t_ifft)     << "%)"
                  << " ola="      << (t_ola      / 1e6) << "(" << pct(t_ola)      << "%)"
                  << " write/limiter=" << (t_write / 1e6) << "(" << pct(t_write) << "%)"
                  << " work_sum=" << (work_sum / 1e6)
                  << " wall="     << (t_wall    / 1e6)
                  << "  (work_sum = per-stage ms summed over " << channels
                  << " channel threads; wall = elapsed ms of the compute+write"
                  << " region. work_sum exceeds wall by about the channel count"
                  << " when threading is healthy.)\n";
    }
}

void Synthesis::process(AudioSTFT& stft) {
    SF_INFO tgt_info = stft.src_info;
    tgt_info.format = SF_FORMAT_WAV |
        (stft.limiter ? SF_FORMAT_PCM_24 : SF_FORMAT_FLOAT);

    SNDFILE* output_snd = sf_open(stft.output_audio_file.c_str(), SFM_WRITE, &tgt_info);
    if (!output_snd) {
        std::cerr << "  ! could not open output '" << stft.output_audio_file << "'\n";
        return;
    }

    // Byte-reproducible output: libsndfile stamps the float WAV PEAK chunk
    // with wall-clock time(NULL), so two otherwise-identical float renders
    // differ by that timestamp under literal cmp while the data chunk is
    // bit-identical. Suppress the chunk. This is a no-op on the PCM_24 paths
    // (integer WAV carries no PEAK chunk) and must precede any frame write.
    sf_command(output_snd, SFC_SET_ADD_PEAK_CHUNK, NULL, SF_FALSE);

    auto write_to_file = [output_snd](const float* buf, size_t n_frames) {
        sf_writef_float(output_snd, buf, static_cast<sf_count_t>(n_frames));
    };

    const std::string pass_label = stft.limiter
        ? "[Pass 2/3] Synthesis........................ "
        : "[Pass 2/2] Synthesis........................ ";
    synthesize_full(stft, write_to_file,
                    /*show_progress=*/true,
                    /*pass_label=*/pass_label.c_str());
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
    // The limited chain (spectral + peak backstop) runs in the engine after
    // synthesis, in place on this buffer — process_to_buffer always does the
    // plain append.
    const std::string pass_label = stft.limiter
        ? "[Pass 2/3] Synthesis........................ "
        : "[Pass 2/2] Synthesis........................ ";
    synthesize_full(stft, append_to_buffer,
                    /*show_progress=*/true,
                    /*pass_label=*/pass_label.c_str());
}

void Synthesis::write_render_to_file(AudioSTFT& stft,
                                     const std::vector<float>& render) {
    SF_INFO tgt_info = stft.src_info;
    tgt_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
    SNDFILE* output_snd = sf_open(stft.output_audio_file.c_str(), SFM_WRITE, &tgt_info);
    if (!output_snd) {
        std::cerr << "  ! could not open output '" << stft.output_audio_file << "'\n";
        return;
    }
    const size_t total_frames = stft.channels > 0
        ? render.size() / static_cast<size_t>(stft.channels) : 0;
    sf_writef_float(output_snd, render.data(),
                    static_cast<sf_count_t>(total_frames));
    sf_close(output_snd);
}
