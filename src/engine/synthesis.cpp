// Dead includes removed under grant (architect approval 2026-08-02).
#include "synthesis.h"
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace {

// Stable descending-magnitude, ascending-bin sort of a frame's K magnitudes
// into PghiHeapNode key order. The pack casts each magnitude to float (the key
// precision), then places the bitwise-complemented float bits in the high 32
// bits of a uint64 and the source bin in the low 32; four LSB-first stable
// counting passes inspect ONLY the four key bytes (bits 32..63, shifts
// 32/40/48/56) — the low 32-bit bin field is carried payload, never
// radix-processed, so ascending-bin ties among equal keys come from the
// ascending fill order plus the passes' stability — and the unpack recovers
// the float by complement + memcpy. The
// domain requirement — magnitudes nonnegative and finite (negative zero and
// NaN unreachable) — is guaranteed by the caller (see the order-contract
// comment at the call site); no defensive handling for values outside it. The
// two uint64 vectors are caller-owned scratch, reused across frames. The
// float-bits round trip is exact and the whole sort is integer work, so the
// output order is deterministic and byte-identical to the explicit comparator
// it replaces.
void radix_sort_magnitudes(const double* mag, int K,
                           std::vector<uint64_t>& a,
                           std::vector<uint64_t>& b,
                           std::vector<PghiHeapNode>& out) {
    a.clear();
    for (int k = 0; k < K; ++k) {
        const float m = static_cast<float>(mag[k]);
        uint32_t bits;
        std::memcpy(&bits, &m, 4);
        a.push_back((uint64_t(~bits) << 32) | uint32_t(k));
    }
    uint32_t hist[256];
    for (int pass = 0; pass < 4; ++pass) {
        const int shift = 32 + pass * 8;
        std::memset(hist, 0, sizeof(hist));
        for (uint64_t v : a) ++hist[(v >> shift) & 0xff];
        uint32_t sum = 0;
        for (int i = 0; i < 256; ++i) { uint32_t c = hist[i]; hist[i] = sum; sum += c; }
        b.resize(a.size());
        for (uint64_t v : a) b[hist[(v >> shift) & 0xff]++] = v;
        a.swap(b);
    }
    out.resize(static_cast<size_t>(K));
    for (int k = 0; k < K; ++k) {
        const uint64_t v = a[static_cast<size_t>(k)];
        const uint32_t bits = ~static_cast<uint32_t>(v >> 32);
        float m;
        std::memcpy(&m, &bits, 4);
        out[static_cast<size_t>(k)].mag = m;
        out[static_cast<size_t>(k)].bin = static_cast<int32_t>(v & 0xffffffffu);
    }
}

}  // namespace

void Synthesis::process_to_buffer(AudioSTFT& stft,
                                  std::vector<float>* output_buffer) {
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

    const int kAnalysisRingDepth = 4;
    class AnalysisRing {
    public:
        struct Slot {
            std::vector<double> mag;
            std::vector<double> phi;
            std::vector<double> dt;
            std::vector<double> df;
            std::vector<char> quiet;
            // Producer-built PGHI key orders for the synthesis frame this
            // slot's prep serves (see the build site in the producer loop):
            // prev_stream is the previous-frame magnitude stream (filtered to
            // this slot's significance set), cur_order the full sorted
            // current-frame key order (the consumer's frontier rank order).
            // Both reserved to K up front and recycled by swap in pop(), so
            // no steady-state reallocation.
            std::vector<PghiHeapNode> prev_stream;
            std::vector<PghiHeapNode> cur_order;
        };

        AnalysisRing(int depth, int k)
            : slots_(static_cast<size_t>(depth)) {
            for (Slot& slot : slots_) {
                slot.mag.resize(static_cast<size_t>(k));
                slot.phi.resize(static_cast<size_t>(k));
                slot.dt.resize(static_cast<size_t>(k));
                slot.df.resize(static_cast<size_t>(k));
                slot.quiet.resize(static_cast<size_t>(k));
                slot.prev_stream.reserve(static_cast<size_t>(k));
                slot.cur_order.reserve(static_cast<size_t>(k));
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
                 std::vector<char>& quiet,
                 std::vector<PghiHeapNode>& prev_stream,
                 std::vector<PghiHeapNode>& cur_order) {
            std::unique_lock<std::mutex> lock(mutex_);
            not_empty_.wait(lock, [&] { return abort_ || count_ > 0; });
            if (abort_) return false;
            Slot& slot = slots_[head_];
            slot.mag.swap(mag);
            slot.phi.swap(phi);
            slot.dt.swap(dt);
            slot.df.swap(df);
            slot.quiet.swap(quiet);
            slot.prev_stream.swap(prev_stream);
            slot.cur_order.swap(cur_order);
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
        // --- One-deep consumer lookahead state ------------------------------
        // The producer analyzes frames in order, while the consumer keeps the
        // current frame plus one analyzed lookahead. Producer-side PGHI prep
        // and the sorted key orders (prev_stream, cur_order) ride with the
        // lookahead slot: slot 0 has no usable prep, and slot f+1 carries
        // prep and orders for synthesis frame f. This is INTERNAL pipeline
        // latency only: synthesis frame m still OLA-adds into output position
        // m*R_s. All inter-frame phase state is held in these per-channel
        // buffers; the PGHI helpers read them directly.
        std::vector<double> ph_prev(K2,0.0), ph_cur(K2,0.0), ph_nxt(K2,0.0);
        std::vector<double> mag_prev(K2,0.0), mag_cur(K2,0.0), mag_nxt(K2,0.0);
        std::vector<double> th_prev(K2,0.0), dt_prev(K2,0.0);
        std::vector<double> dt_in(K2), df_in(K2);
        std::vector<char>   quiet_in(K2);
        // Per-channel synthesis scratch. prev_stream and cur_order are not
        // scratch: they receive the producer-built sorted key orders through
        // the ring pop's swap, alongside dt_in/df_in/quiet_in. rank_of_bin
        // and the two frontier bitset levels are pghi_integrate's ranked
        // active-frontier scratch, rebuilt each frame (capacity reused).
        std::vector<double> theta(K2);
        std::vector<char>   done_scratch(K2);
        std::vector<PghiHeapNode> prev_stream; prev_stream.reserve(K2);
        std::vector<PghiHeapNode> cur_order;   cur_order.reserve(K2);
        std::vector<int32_t>  rank_of_bin(K2);
        const size_t frontier_words = (static_cast<size_t>(K2) + 63) / 64;
        std::vector<uint64_t> frontier_leaf(frontier_words);
        std::vector<uint64_t> frontier_summary((frontier_words + 63) / 64);
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
                // Two-deep rotation of full sorted key orders: each iteration
                // aidx >= 1 builds order_cur for analysis(aidx-1) and the
                // end-of-iteration swap retains it one iteration as
                // order_prev. Seed order_cur with analysis(-1)'s order — the
                // zero magnitude vector (mag_cur_prod's zero initialization)
                // under the total key order: all keys tie at 0.0f, so bins
                // ascending. Iteration 0's rotation hands it to iteration 1
                // as order_prev; frame 0 always seeds, so the stream filtered
                // from it is never drained, but the shipped value stays
                // well-defined.
                std::vector<PghiHeapNode> order_cur, order_prev;
                order_cur.reserve(static_cast<size_t>(K2));
                order_prev.reserve(static_cast<size_t>(K2));
                // Scratch buffers for radix_sort_magnitudes, reserved to K2
                // once and reused every frame (no steady-state allocation).
                std::vector<uint64_t> radix_a, radix_b;
                radix_a.reserve(static_cast<size_t>(K2));
                radix_b.reserve(static_cast<size_t>(K2));
                for (int k = 0; k < K2; ++k)
                    order_cur.push_back({0.0f, k});
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
                    // with slot 1, so the consumer never reads slot 0's prep —
                    // its prev_stream and cur_order ship empty.
                    slot->prev_stream.clear();
                    slot->cur_order.clear();
                    if (aidx >= 1) {
                        const int f = aidx - 1;
                        const int64_t ta_back = ta_for(f);
                        const int64_t R_a_back = (f > 0) ? (ta_back - ta_for(f - 1)) : 0;
                        const int64_t R_a_fwd = ta_for(f + 1) - ta_back;
                        stft.pghi_prep(f == 0, R_a_back, R_a_fwd,
                                       mag_prev_prod, mag_cur_prod,
                                       ph_prev_prod, ph_cur_prod, ph_nxt_prod,
                                       slot->dt, slot->df, slot->quiet);
                        // PGHI key orders for synthesis frame f, built here so
                        // the sort overlaps the consumer's drain. ONE full sort
                        // per iteration of frame f's magnitudes (mag_cur_prod)
                        // into order_cur under the normative TOTAL key order —
                        // descending float(mag), ties ascending bin. This is
                        // the sole statement of that order: the magnitudes are
                        // sqrt of bounded PCM analysis, so each is nonnegative
                        // and finite (negative zero and NaN unreachable), and
                        // over that domain the IEEE-754 float bit pattern is
                        // monotone with numeric value — so ascending order on
                        // the complemented bits equals descending magnitude,
                        // and a stable radix fed bins in ascending order
                        // reproduces the ascending-bin tie-break. That radix is
                        // radix_sort_magnitudes, the single sort in the pipeline
                        // (pghi_integrate consumes both orders as-is and never
                        // sorts). The one order serves twice:
                        // shipped whole as this slot's cur_order (the
                        // consumer's frontier rank order for frame f), and
                        // retained one iteration as order_prev, so THIS slot's
                        // prev_stream is the previous iteration's retained
                        // order — frame f-1's magnitudes, exactly the
                        // consumer's mag_prev at frame f — filtered by a
                        // linear pass to the bins this slot's quiet mask
                        // leaves in I, equal keys in the explicit total order.
                        // Built for every frame, including frames the consumer
                        // will seed (frame 0, phase resets — consumer knowledge
                        // the producer doesn't have): a seed frame's orders are
                        // simply unused, an accepted harmless waste. The
                        // reserved vectors are filled in place (order_cur by the
                        // radix's resize/index writes, prev_stream by clear +
                        // push_back), reusing capacity; building before
                        // finish_push keeps the slot
                        // publication order — nothing half-built is ever
                        // consumer-visible.
                        radix_sort_magnitudes(mag_cur_prod.data(), K2,
                                              radix_a, radix_b, order_cur);
                        slot->cur_order = order_cur;
                        for (const PghiHeapNode& n : order_prev) {
                            if (slot->quiet[n.bin] != 1)
                                slot->prev_stream.push_back(n);
                        }
                    }
                    if (!analysis_ring.finish_push()) return;
                    ph_prev_prod.swap(ph_cur_prod);
                    ph_cur_prod.swap(ph_nxt_prod);
                    mag_prev_prod.swap(mag_cur_prod);
                    mag_cur_prod.swap(mag_nxt_prod);
                    // Retain this iteration's order for the next slot's
                    // prev_stream (at iteration 0 this hands over the seeded
                    // zero-key order; the empty ex-order_prev left in
                    // order_cur is cleared before the next build).
                    order_prev.swap(order_cur);
                }
            });
        }

        auto pop_analysis = [&](std::vector<double>& md,
                                std::vector<double>& pd,
                                std::vector<double>& dt,
                                std::vector<double>& df,
                                std::vector<char>& quiet,
                                std::vector<PghiHeapNode>& stream,
                                std::vector<PghiHeapNode>& order) -> bool {
            return analysis_ring.pop(md, pd, dt, df, quiet, stream, order);
        };

        // Prime: analysis frames 0 and 1 (1 is the analysis-only frame when
        // the window is a single synthesis frame).
        // ph_prev stays zero — the window's first frame is a seed, so it needs
        // no phi_prev (same as frame 0 on the full path).
        int64_t ta_prev = 0, ta_cur = 0, ta_nxt = 0;
        if (wcount >= 1) {
            if (!pop_analysis(mag_cur, ph_cur, dt_in, df_in, quiet_in,
                              prev_stream, cur_order)) {
                ch_cancelled[ch] = 1;
                return;
            }
            if (!pop_analysis(mag_nxt, ph_nxt, dt_in, df_in, quiet_in,
                              prev_stream, cur_order)) {
                ch_cancelled[ch] = 1;
                return;
            }
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
            // THE SEED FRAME IS SYNTHESIZED UNSTRETCHED (recorded 2026-09-02,
            // architect approval 2026-09-02, comment-only): theta = phi
            // verbatim (stft_container.h's seed seat), while every propagated
            // frame's content is placed on the map through the alpha-scaled
            // frequency spread (stft_container.h's frontier step, the gradient
            // scaled by R_s/R_a). So the seed grain's content at source offset
            // δ from its centre lands at output m·R_s + δ, where the map puts
            // it at m·R_s + δ·R_s/R_a — δ·(1 − R_s/R_a) off the map: late
            // under a speed-up (up to +4.6 ms at tempo 1.20, +7.7 ms at 1.33;
            // measured +130 samples on a real onset at 1.20), early under a
            // slow-down; the propagated frames re-converge on the map. THOSE
            // TWO FIGURES ARE FOR δ = R_a (recorded 2026-09-02; architect
            // approval 2026-09-02, comment-only), the farthest the AUTHORED
            // frame itself can sit from the seed centre — pass 1 seeds within
            // R_a of it — and not the grain's own reach: the grain runs to
            // δ = N/2 in both directions, so its outer edges are late by
            // 7.7 ms at tempo 1.20 already. Nothing rests on the larger
            // figure — the drop's N/2 rule puts the protected point outside
            // the grain entirely — but the smaller one must not be read as
            // the grain's bound. And
            // the seed is the LAST frame whose centre ≤ the authored frame
            // (engine.cpp's pass 1), 0 to R_a before it, so "a phase reset
            // fires exactly at the authored frame" is true of WHICH frame
            // seeds, not of where that frame's content lands. The GUI's
            // lead-in drop authors a reset N/2 output samples before the point
            // it protects, which puts that point outside the seed grain TO THE
            // SCHEDULE'S ROUNDING, so the grain's lateness does not reach it;
            // neither a nearest-centre placement nor a stretched seed is
            // opened. THE RESIDUE IN THAT "TO THE ROUNDING" (recorded
            // 2026-09-02, restated the same day after codex round 2;
            // architect approval 2026-09-02, comment-only): pass 1's placement
            // compare is on the schedule's ROUNDED window start against the
            // integer query, so a seed centre up to half a source frame past
            // the authored reset still qualifies and the grain can end HALF A
            // SOURCE FRAME'S TARGET IMAGE AT THE LOCAL SEGMENT SLOPE past the
            // protected point — the slope being 1/(tempo · marker_scale ·
            // settings_scale), NOT 1/tempo — with the GUI drop's own
            // whole-frame snap adding a second such term and its painted band's
            // rounding half an output sample more: a few output samples at
            // ordinary tempos, up to ~8 + ~8 + ½ at the numeric slope ceiling
            // of 16, unbounded only across a label-reference segment, and NOT
            // the universal two samples this clause claimed for one day. It
            // lands at the grain's own ZERO-WEIGHT edge, where the Hann² window
            // has tapered to nothing. It has no effect on the audio and none on
            // the drop, whose authored frame is unchanged — the marker is never
            // off and the render stays deterministic. IT IS NOT THE TRIM CROP'S
            // ROUNDING CLASS (that comparison is withdrawn: the trim crop's
            // rounding is transient, a tool's own window, while this seam sits
            // in the geometry of every deliverable), and it is not worth a
            // third rounding rule. The three terms, the worked case and the
            // decision not to make the offset quantization-aware are at
            // phaseresetmarkers_ops.cpp's derivation comment.
            bool phase_reset_fired = false;
            while (phase_reset_cursor < static_cast<int>(stft.phase_reset_placements.size()) &&
                   stft.phase_reset_placements[phase_reset_cursor].synth_frame == frame_idx) {
                ++phase_reset_cursor;
                phase_reset_fired = true;
            }
            // Pipeline invariant at loop top: ph_cur/mag_cur = analysis(frame),
            // ph_nxt/mag_nxt = analysis(frame+1), dt_in/df_in/quiet_in =
            // prep(frame) delivered with the frame+1 slot, prev_stream = the
            // producer-built sorted stream for `frame` (analysis(frame-1)
            // magnitudes filtered by prep(frame)'s quiet mask, from the same
            // slot), cur_order = the full sorted key order of mag_cur (same
            // slot), and ph_prev/mag_prev = analysis(frame-1) (zero at frame
            // 0; consumer-side those two are swap-recycling storage only).
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
            const bool    seed_frame = frame0 || phase_reset_fired;

            stft.pghi_integrate(seed_frame, R_a_actual, R_a_fwd,
                                ph_cur,
                                th_prev, dt_prev, dt_in, df_in, quiet_in,
                                theta, done_scratch, prev_stream, cur_order,
                                rank_of_bin, frontier_leaf, frontier_summary,
                                rng);
            // dt_in (this frame's dt) becomes the next frame's dt_prev.
            dt_prev.swap(dt_in);

            stft.populate_synth_spectrum(ch, mag_cur, theta);

            // IFFT length M; un-shift the centered frame back into the [0, N)
            // OLA window (the inverse of analyze_frame's placement). With n in
            // [0, N) and Mfft = 2N the index resolves to two contiguous ranges,
            // so the split below replaces the per-sample modulo with no change to
            // the loads, multiplies, or accumulation order.
            fftw_execute(stft.fft_ws[ch].plan_inv);

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
            // ph_prev holds frame_idx's analysis phase, ready for the next
            // pghi_integrate call.
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

            // Progress is reported by channel 0 only; with channels running
            // concurrently it is approximate, which is fine (cosmetic).
            if (ch == 0 && wcount > 0 &&
                (frame_idx % progress_stride) == 0) {
                int pct = static_cast<int>((frame_idx * 100LL) / wcount);
                if (pct != last_pct) {
                    std::cout << "\r"
                              << "[pass 2/3] synthesis........................ "
                              << pct << "%" << std::flush;
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
                if (!pop_analysis(mag_nxt, ph_nxt, dt_in, df_in, quiet_in,
                                  prev_stream, cur_order)) {
                    ch_cancelled[ch] = 1;
                    return;
                }
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
            return;                      // cancellation on any channel aborts before emission
        }
    }

    std::cout << "\r"
              << "[pass 2/3] synthesis........................ 100%\n";

    // Interleave the per-channel mono streams and append them to the caller's
    // buffer in one emission. All channels emit out_frames samples; the append
    // is a single contiguous insert.
    if (out_frames > 0) {
        std::vector<float> inter(static_cast<size_t>(out_frames) * channels);
        for (int ch = 0; ch < channels; ++ch) {
            const std::vector<float>& m = mono[ch];
            // Always-on (not an assert — Release is the shipped build);
            // breach-only — the emission accounting upstream sizes the buffer,
            // so a breach here would be a silent buffer overrun, the class the
            // engine owns loudly.
            if (static_cast<int64_t>(m.size()) < out_frames) {
                // Terminal message strings in this file carry
                // sentence-initial capitals (architect approval
                // 2026-08-02, the terminal capitalization pass —
                // text-only, otherwise byte-identical output); the
                // dot-leader progress rows above are structured readout,
                // not prose, and are unchanged. The "warptempo_gui:"
                // prefix is CORRECT in both binaries (architect ruling
                // 2026-08-02): warptempo_gui is the PROJECT name (the
                // GitHub repository name), not the GUI binary's name, so
                // the engine — compiled into both products — prefixes
                // with it in the CLI too.
                std::fprintf(stderr,
                             "warptempo_gui: Synthesis output buffer shorter "
                             "than the frame emission; internal breach\n");
                std::abort();
            }
            for (int64_t f = 0; f < out_frames; ++f)
                inter[static_cast<size_t>(f) * channels + ch] = m[static_cast<size_t>(f)];
        }
        // Append the full interleaved emit to the caller-owned buffer. The
        // spectral limiter (Pass 3) then runs in the engine after synthesis,
        // in place on this buffer.
        output_buffer->insert(output_buffer->end(), inter.data(),
                              inter.data() + inter.size());
    }
}
