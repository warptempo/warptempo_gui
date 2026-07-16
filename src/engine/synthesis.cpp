#include "synthesis.h"
#include "render_profile.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

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
    // Synthesis frame window [0, wend): synthesis always runs the whole map,
    // so wend is num_frames on every path. The emit cap truncates the emitted
    // stream at the map's last anchor target (a trimmed render's translated
    // map carries its own closing anchor); ta_for stays absolute over the
    // full fm.
    const int wend   = num_frames;
    const int wcount = wend;

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
    // map's last anchor target (render length == target length). See the
    // timing-convention block in stft_container.h. mono_len is the uncapped
    // per-channel push total (what each run_channel actually appends); with
    // the whole map synthesized it always exceeds the cap, so the cap binds.
    // The reserve uses mono_len so the cap never under-reserves the mono
    // buffer.
    const int64_t mono_len =
        (wcount > 0) ? static_cast<int64_t>(wcount - 1) * R_s + N / 2 : 0;
    int64_t out_frames = mono_len;
    if (stft.emit_sample_cap < out_frames)
        out_frames = stft.emit_sample_cap;

    // One mono output stream per channel. Each channel computes its whole
    // output independently into its own buffer; the streams are interleaved
    // only at the end. Each channel's pipeline state is fully local to
    // run_channel so each channel can be handed to its own thread.
    std::vector<std::vector<float>> mono(channels);
    std::vector<char> ch_cancelled(channels, 0);

    // Temporary render profiling: per-channel stage timings, printed at stage
    // end. Slots are sized to channels so each channel thread writes only its
    // own; reads/clocks happen only when profiling is enabled.
    using prof_clock = std::chrono::steady_clock;
    auto prof_secs = [](prof_clock::duration d) {
        return std::chrono::duration<double>(d).count();
    };
    std::vector<SynthChannelProfile> chan_prof(static_cast<size_t>(channels));

    const int kAnalysisRingDepth = 4;
    class AnalysisRing {
    public:
        struct Slot {
            std::vector<double> mag;
            std::vector<double> phi;
            std::vector<double> dt;
            std::vector<double> df;
            std::vector<char> quiet;
        };

        AnalysisRing(int depth, int k)
            : slots_(static_cast<size_t>(depth)) {
            for (Slot& slot : slots_) {
                slot.mag.resize(static_cast<size_t>(k));
                slot.phi.resize(static_cast<size_t>(k));
                slot.dt.resize(static_cast<size_t>(k));
                slot.df.resize(static_cast<size_t>(k));
                slot.quiet.resize(static_cast<size_t>(k));
            }
        }

        Slot* begin_push() {
            std::unique_lock<std::mutex> lock(mutex_);
            not_full_.wait(lock, [&] {
                return abort_ || count_ + reserved_ < slots_.size();
            });
            if (abort_) return nullptr;
            Slot& slot = slots_[tail_];
            tail_ = (tail_ + 1) % slots_.size();
            ++reserved_;
            return &slot;
        }

        bool finish_push() {
            std::unique_lock<std::mutex> lock(mutex_);
            --reserved_;
            if (abort_) {
                lock.unlock();
                not_full_.notify_one();
                not_empty_.notify_all();
                return false;
            }
            ++count_;
            lock.unlock();
            not_empty_.notify_one();
            return true;
        }

        bool pop(std::vector<double>& mag,
                 std::vector<double>& phi,
                 std::vector<double>& dt,
                 std::vector<double>& df,
                 std::vector<char>& quiet) {
            std::unique_lock<std::mutex> lock(mutex_);
            not_empty_.wait(lock, [&] { return abort_ || count_ > 0; });
            if (abort_) return false;
            Slot& slot = slots_[head_];
            slot.mag.swap(mag);
            slot.phi.swap(phi);
            slot.dt.swap(dt);
            slot.df.swap(df);
            slot.quiet.swap(quiet);
            head_ = (head_ + 1) % slots_.size();
            --count_;
            lock.unlock();
            not_full_.notify_one();
            return true;
        }

        void request_abort() {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                abort_ = true;
            }
            not_full_.notify_all();
            not_empty_.notify_all();
        }

        bool abort_requested() {
            std::lock_guard<std::mutex> lock(mutex_);
            return abort_;
        }

    private:
        std::vector<Slot> slots_;
        size_t head_ = 0;
        size_t tail_ = 0;
        size_t count_ = 0;
        size_t reserved_ = 0;
        bool abort_ = false;
        std::mutex mutex_;
        std::condition_variable not_full_;
        std::condition_variable not_empty_;
    };

    // Per-channel synthesis pass. Touches only read-only shared inputs (planar,
    // fm, stft.phase_reset_placements, stft.fft_ws[ch],
    // stft.window/synth_window) and writes only mono[ch] and
    // ch_cancelled[ch]. Analysis runs on a per-channel producer thread, while
    // synthesis-side inter-frame state stays consumer-local. The RNG is
    // consumer-only, so quiet-bin draws stay in frame/bin order. Forward and
    // inverse FFTW resources are disjoint between the two threads.
    auto run_channel = [&](int ch) {
        const int K2 = K;
        const bool prof = render_profile_enabled();
        SynthChannelProfile& cp = chan_prof[ch];
        // --- One-deep consumer lookahead state ------------------------------
        // The producer analyzes frames in order, while the consumer keeps the
        // current frame plus one analyzed lookahead. Producer-side PGHI prep
        // rides with the lookahead slot: slot 0 has no usable prep, and slot
        // f+1 carries prep for synthesis frame f. This is INTERNAL pipeline
        // latency only: synthesis frame m still OLA-adds into output position
        // m*R_s. All inter-frame phase state is held in these per-channel
        // buffers; the PGHI helpers read them directly.
        std::vector<double> ph_prev(K2,0.0), ph_cur(K2,0.0), ph_nxt(K2,0.0);
        std::vector<double> mag_prev(K2,0.0), mag_cur(K2,0.0), mag_nxt(K2,0.0);
        std::vector<double> th_prev(K2,0.0), dt_prev(K2,0.0);
        std::vector<double> dt_in(K2), df_in(K2);
        std::vector<char>   quiet_in(K2);
        // Per-channel synthesis scratch.
        std::vector<double> theta(K2);
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
        // the warp_frame_map range is exhausted (alpha == 1), so each extra
        // analysis-only frame advances by R_s. We never read more than one
        // frame past EOF: that single analysis-only frame supplies the last
        // emitted frame's phi_next and is never itself emitted, so the total
        // emitted length is unchanged.
        auto ta_for = [&](int aidx) -> int64_t {
            if (aidx < num_frames) return fm[aidx];
            return fm[num_frames - 1] +
                   static_cast<int64_t>(R_s) * (aidx - (num_frames - 1));
        };

        AnalysisRing analysis_ring(kAnalysisRingDepth, K2);
        std::thread analysis_thread;
        struct AnalysisThreadJoiner {
            AnalysisRing& ring;
            std::thread& thread;
            bool abort_on_destroy = true;

            ~AnalysisThreadJoiner() {
                if (!thread.joinable()) return;
                if (abort_on_destroy) ring.request_abort();
                thread.join();
            }

            void join_normally() {
                if (!thread.joinable()) return;
                abort_on_destroy = false;
                thread.join();
            }
        } analysis_joiner{analysis_ring, analysis_thread};

        auto cancel_requested = [&]() -> bool {
            return stft.cancel_flag && stft.cancel_flag->load();
        };

        if (wcount > 0) {
            analysis_thread = std::thread([&, ch, psrc] {
                std::vector<double> ph_prev_prod(K2,0.0), ph_cur_prod(K2,0.0), ph_nxt_prod(K2,0.0);
                std::vector<double> mag_prev_prod(K2,0.0), mag_cur_prod(K2,0.0), mag_nxt_prod(K2,0.0);
                for (int aidx = 0; aidx <= wend; ++aidx) {
                    if (analysis_ring.abort_requested() || cancel_requested()) {
                        analysis_ring.request_abort();
                        return;
                    }
                    stft.analyze_frame(ch, psrc, ta_for(aidx), src_frames,
                                       mag_nxt_prod, ph_nxt_prod);
                    AnalysisRing::Slot* slot = analysis_ring.begin_push();
                    if (!slot) return;
                    slot->mag = mag_nxt_prod;
                    slot->phi = ph_nxt_prod;
                    // Slot 0 carries analysis(0) only; prep(0) is delivered
                    // with slot 1, so the consumer never reads slot 0's prep.
                    if (aidx >= 1) {
                        const int f = aidx - 1;
                        const int64_t ta_back = ta_for(f);
                        const int64_t R_a_back = (f > 0) ? (ta_back - ta_for(f - 1)) : 0;
                        const int64_t R_a_fwd = ta_for(f + 1) - ta_back;
                        stft.pghi_prep(f == 0, R_a_back, R_a_fwd,
                                       mag_prev_prod, mag_cur_prod,
                                       ph_prev_prod, ph_cur_prod, ph_nxt_prod,
                                       slot->dt, slot->df, slot->quiet);
                    }
                    if (!analysis_ring.finish_push()) return;
                    ph_prev_prod.swap(ph_cur_prod);
                    ph_cur_prod.swap(ph_nxt_prod);
                    mag_prev_prod.swap(mag_cur_prod);
                    mag_cur_prod.swap(mag_nxt_prod);
                }
            });
        }

        auto pop_analysis = [&](std::vector<double>& md,
                                std::vector<double>& pd,
                                std::vector<double>& dt,
                                std::vector<double>& df,
                                std::vector<char>& quiet) -> bool {
            return analysis_ring.pop(md, pd, dt, df, quiet);
        };

        // Prime: analysis frames 0 and 1 (1 is the analysis-only frame when
        // the window is a single synthesis frame).
        // ph_prev stays zero — the window's first frame is a seed, so it needs
        // no phi_prev (same as frame 0 on the full path).
        int64_t ta_prev = 0, ta_cur = 0, ta_nxt = 0;
        if (wcount >= 1) {
            prof_clock::time_point pt{};
            if (prof) pt = prof_clock::now();
            if (!pop_analysis(mag_cur, ph_cur, dt_in, df_in, quiet_in)) {
                ch_cancelled[ch] = 1;
                return;
            }
            if (prof) { cp.analysis_wait_s += prof_secs(prof_clock::now() - pt); pt = prof_clock::now(); }
            if (!pop_analysis(mag_nxt, ph_nxt, dt_in, df_in, quiet_in)) {
                ch_cancelled[ch] = 1;
                return;
            }
            if (prof) cp.analysis_wait_s += prof_secs(prof_clock::now() - pt);
            ta_cur = ta_for(0);
            ta_nxt = ta_for(1);
        }

        // Phase reset cursor: every placement's synth_frame indexes the
        // schedule (upper_bound over fm in run_warptempo_engine), so every
        // placement lies inside [0, wend). A placement's synth_frame selects
        // the frame that SEEDS: pghi_integrate seats theta = phi on that
        // frame, whose analysis window centers at the authored reset
        // position. The forward-only walk is safe because placements are
        // non-decreasing in synth_frame: the strictly ascending input list
        // (engine init refuses anything else) maps through a monotone
        // upper_bound in run_warptempo_engine, so a placement never lands
        // behind the cursor. Equal placements after the engine's llrint
        // quantization remain legal — quantization is downstream of input
        // validation — and are consumed together by the walk at the top of
        // the frame loop.
        int  phase_reset_cursor = 0;
        // Start-trim: N/2 samples -- the origin-centered analysis alignment
        // latency only, so source frame 0 maps to output frame 0. The OLA
        // ramp-up is intentionally NOT trimmed; it is kept as a brief head
        // fade-in (see the timing-convention block in stft_container.h).
        // That head is the absolute time-sync anchor: output sample 0 is
        // musical time 0 for uses such as video sync. The tail past emit cap is
        // expendable by contrast. Real sources' mastering fades cover both
        // ends; future synthesis replacements must preserve the head timing.
        int  frames_to_skip = N / 2;
        int  progress_stride = std::max(100, wcount / 100);
        int  last_pct = -1;

        for (int frame_idx = 0; frame_idx < wend; ++frame_idx) {
            // Cooperative cancellation: stft.cancel_flag is set by the GUI when
            // the user presses Esc during a render. Worst-case cancel-to-stop
            // latency is one frame — well below human perception.
            if (cancel_requested()) {
                analysis_ring.request_abort();
                ch_cancelled[ch] = 1;
                return;
            }
            // Phase reset: consume this frame's placements. A hit makes this
            // frame the seed frame — pghi_integrate seats theta = phi here,
            // and this frame's analysis window centers at the authored reset
            // position. A frame-0 placement is inert: frame 0 seeds as
            // frame0 anyway. Channel-independent: each channel walks its own
            // cursor.
            bool phase_reset_fired = false;
            while (phase_reset_cursor < static_cast<int>(stft.phase_reset_placements.size()) &&
                   stft.phase_reset_placements[phase_reset_cursor].synth_frame == frame_idx) {
                ++phase_reset_cursor;
                phase_reset_fired = true;
            }
            // Pipeline invariant at loop top: ph_cur/mag_cur = analysis(frame),
            // ph_nxt/mag_nxt = analysis(frame+1), dt_in/df_in/quiet_in =
            // prep(frame) delivered with the frame+1 slot, and ph_prev/mag_prev
            // = analysis(frame-1) (zero at frame 0).
            // R_a_actual is the integer source hop this frame actually
            // traversed: fm[frame] - fm[frame-1]. fm is the once-rounded
            // schedule from generate_source_frame_positions (each entry
            // map_target_to_source(m*R_s) - N/2, llrint half-to-even), so the
            // positions are integer by the project rounding convention and
            // the engine analyzes at exactly those source frames. The hop is
            // therefore the faithful integer realization of R_a = R_s/alpha:
            // phase is propagated against the distance actually read. A
            // fractional R_a over integer-positioned reads would describe a
            // hop the analysis never took and regress the output, so the
            // integerness here is intentional, not a precision leak.
            const int64_t R_a_actual = (frame_idx > 0) ? (ta_cur - ta_prev) : 0;
            const int64_t R_a_fwd    = ta_nxt - ta_cur;
            const bool    frame0     = (frame_idx == 0);
            const bool    seed_heap  = frame0 || phase_reset_fired;

            prof_clock::time_point t{};
            if (prof) { cp.frames += 1; t = prof_clock::now(); }
            stft.pghi_integrate(seed_heap, R_a_actual, R_a_fwd,
                                mag_prev, mag_cur, ph_cur,
                                th_prev, dt_prev, dt_in, df_in, quiet_in,
                                theta, done_scratch, heap_scratch, rng,
                                prof ? &cp.pghi : nullptr);
            if (prof) cp.pghi_s += prof_secs(prof_clock::now() - t);
            // dt_in (this frame's dt) becomes the next frame's dt_prev.
            dt_prev.swap(dt_in);

            if (prof) t = prof_clock::now();
            stft.populate_synth_spectrum(ch, mag_cur, theta);
            if (prof) cp.populate_s += prof_secs(prof_clock::now() - t);

            // IFFT length M; un-shift the centered frame back into the [0, N)
            // OLA window (the inverse of analyze_frame's placement). With n in
            // [0, N) and Mfft = 2N the index resolves to two contiguous ranges,
            // so the split below replaces the per-sample modulo with no change to
            // the loads, multiplies, or accumulation order.
            if (prof) t = prof_clock::now();
            fftw_execute(stft.fft_ws[ch].plan_inv);
            if (prof) cp.ifft_s += prof_secs(prof_clock::now() - t);

            if (prof) t = prof_clock::now();
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

            // End-of-frame per-channel state shift. theta -> th_prev (a swap:
            // PGHI fully overwrites theta before reading it next frame, so the
            // stale th_prev the swap leaves in theta is dead on arrival);
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
            if (prof) cp.ola_emit_s += prof_secs(prof_clock::now() - t);

            // Progress is reported by channel 0 only; with channels running
            // concurrently it is approximate, which is fine (cosmetic).
            if (show_progress && ch == 0 && wcount > 0 &&
                (frame_idx % progress_stride) == 0) {
                int pct = static_cast<int>((frame_idx * 100LL) / wcount);
                if (pct != last_pct) {
                    std::cout << "\r" << pass_label << pct << "%" << std::flush;
                    last_pct = pct;
                }
            }

            // Advance the analysis pipeline by one frame. Only needed while
            // another synthesis frame follows; the analysis-only frame
            // (index == wend == num_frames) is reached when frame_idx ==
            // wend-2 and sits one hop past the schedule, so the last emitted
            // frame's phi_next comes through ta_for's clamped tail. Each
            // spectrum is analyzed exactly once.
            if (frame_idx + 1 < wend) {
                ta_prev = ta_cur;
                ta_cur  = ta_nxt;
                ta_nxt  = ta_for(frame_idx + 2);
                if (prof) t = prof_clock::now();
                if (!pop_analysis(mag_nxt, ph_nxt, dt_in, df_in, quiet_in)) {
                    ch_cancelled[ch] = 1;
                    return;
                }
                if (prof) cp.analysis_wait_s += prof_secs(prof_clock::now() - t);
            }
        }

        analysis_joiner.join_normally();

        // Final tail: the last N-R_s samples of the OLA window.
        const int remaining = N - R_s;
        for (int n = 0; n < remaining; ++n)
            out.push_back(static_cast<float>(ola[n]));
    };

    // Run each channel's pipeline concurrently. ch 0 runs on this (main) thread
    // so its progress output isn't interleaved; ch 1..n-1 get worker threads.
    // All per-channel state is private to run_channel and fft_ws[ch] is
    // per-channel, so the passes are independent. Join before the interleave
    // below.
    double prof_synth_wall_s = 0.0;
    prof_clock::time_point prof_wall_t0{};
    if (render_profile_enabled()) prof_wall_t0 = prof_clock::now();
    {
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(std::max(0, channels - 1)));
        for (int ch = 1; ch < channels; ++ch)
            workers.emplace_back(run_channel, ch);
        run_channel(0);
        for (auto& w : workers) w.join();
    }
    if (render_profile_enabled())
        prof_synth_wall_s = prof_secs(prof_clock::now() - prof_wall_t0);

    for (int ch = 0; ch < channels; ++ch) {
        if (ch_cancelled[ch]) {
            stft.cancellation_observed = true;
            return;                      // cancellation on any channel aborts before emission
        }
    }

    // Temporary render profiling report: one cluster of lines per channel,
    // then the whole channel-run wall. Skipped on cancellation (returns above).
    if (render_profile_enabled()) {
        for (int ch = 0; ch < channels; ++ch) {
            const SynthChannelProfile& cp = chan_prof[ch];
            const PghiProfile& hp = cp.pghi;
            char line[512];
            std::snprintf(line, sizeof(line),
                "[profile] synth ch%d: frames %lld pghi %.3fs (quiet %.3fs heap %.3fs) "
                "populate %.3fs ifft %.3fs ola %.3fs analysis-wait %.3fs",
                ch, cp.frames, cp.pghi_s, hp.quiet_s, hp.heap_s,
                cp.populate_s, cp.ifft_s, cp.ola_emit_s, cp.analysis_wait_s);
            std::cerr << line << "\n";
            const long long total_bins = hp.quiet_bins + hp.significant_bins;
            const double inert_pct = hp.pops_total
                ? 100.0 * static_cast<double>(hp.pops_inert) /
                  static_cast<double>(hp.pops_total)
                : 0.0;
            const double quiet_pct = total_bins
                ? 100.0 * static_cast<double>(hp.quiet_bins) /
                  static_cast<double>(total_bins)
                : 0.0;
            char line2[512];
            std::snprintf(line2, sizeof(line2),
                "[profile] synth ch%d heap: pops %lld inert %lld (%.1f%%) "
                "quiet-bins %.1f%% seed-frames %lld",
                ch, hp.pops_total, hp.pops_inert, inert_pct, quiet_pct,
                hp.seed_frames);
            std::cerr << line2 << "\n";
        }
        char wline[128];
        std::snprintf(wline, sizeof(wline),
            "[profile] synth wall: %.3fs", prof_synth_wall_s);
        std::cerr << wline << "\n";
    }

    if (show_progress) std::cout << "\r" << pass_label << "100%\n";

    // Interleave the per-channel mono streams and emit in one write_cb call.
    // All channels emit out_frames samples; the downstream write_cb is
    // order-preserving and chunk-agnostic, so a single call is equivalent to
    // any chunking.
    if (out_frames > 0) {
        std::vector<float> inter(static_cast<size_t>(out_frames) * channels);
        for (int ch = 0; ch < channels; ++ch) {
            const std::vector<float>& m = mono[ch];
            assert(static_cast<int64_t>(m.size()) >= out_frames);
            for (int64_t f = 0; f < out_frames; ++f)
                inter[static_cast<size_t>(f) * channels + ch] = m[static_cast<size_t>(f)];
        }
        write_cb(inter.data(), static_cast<size_t>(out_frames));
    }
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
    // The spectral limiter (Pass 3) runs in the engine after synthesis, in
    // place on this buffer — process_to_buffer always does the plain append.
    synthesize_full(stft, append_to_buffer,
                    /*show_progress=*/true,
                    /*pass_label=*/"[Pass 2/3] Synthesis........................ ");
}
