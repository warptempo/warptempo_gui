#include "trimmer.h"

#include "peak_limiter.h"
#include "pcm24.h"
#include "wav_io.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace {

// Authored bound -> exact double source frame: the single widening point.
// The authored bound is a whole int64 source frame; on the has-bound branch
// it widens exactly to a double (every int64 in range is exactly
// representable at these magnitudes — audio frame counts are far below 2^53).
// An unset begin means source frame 0; an unset end means the source end
// exactly (the caller passes the matching unset_value).
double bound_source_frame(bool has, int64_t frame, int64_t unset_value) {
    return static_cast<double>(has ? frame : unset_value);
}

}  // namespace

std::expected<void, std::string> validate_trim_frames(
        bool has_begin, int64_t begin_frame,
        bool has_end,   int64_t end_frame,
        int64_t total_frames,
        const std::vector<WarpFrameMapSegment>& full_warp_frame_map) {
    const double total = static_cast<double>(total_frames);
    const double b_src = bound_source_frame(has_begin, begin_frame, 0);
    const double e_src = bound_source_frame(has_end, end_frame, total_frames);
    if (e_src <= b_src) {
        return std::unexpected("trim end at or before trim begin");
    }
    if (b_src >= total) {
        return std::unexpected("trim begin at or past source end");
    }
    if (e_src > total) {
        return std::unexpected("trim end past source end");
    }
    // Target-span refusal: the authored window must round to at least one
    // output sample. T_b and T_e are the bounds' exact double target images
    // through the full map — the same values the crop is cut from — so this
    // check and the crop cannot disagree.
    const double T_b = map_source_to_target(b_src, full_warp_frame_map);
    const double T_e = map_source_to_target(e_src, full_warp_frame_map);
    if (std::llrint(T_e) - std::llrint(T_b) < 1) {
        return std::unexpected(
            "trim target span rounds below one output sample");
    }
    return {};
}

// Trim geometry (architect-confirmed derivation; the null condition binds
// every choice). Notation: N and R_s from engine_geometry.h; total_frames =
// source length; the full map is the untrimmed warp frame map.
//
//  1. Authored source positions are whole int64 frames widened exactly to
//     doubles: b_src = begin_frame, e_src = end_frame — the authored bounds
//     themselves, bit-identical. Unset begin -> 0; unset end -> total_frames.
//  2. T_b / T_e are their exact double target images through the FULL map.
//  3. Output origin hop A0 = max(0, floor(T_b / R_s) - 3). The 3 hops
//     (N/R_s - 1) of pre-roll give full OLA weight at T_b; the pre-roll is
//     discarded by the crop (the render is deliberately slightly too long at
//     the head).
//  4. Source cut begin = max(0, llrint(map_target_to_source(A0*R_s) - N/2))
//     — the origin hop's analysis read position, an INTEGER frame so the
//     engine's llrint read schedule translates exactly. The max(0, ...) is
//     the near-piece-start clamp: cut nothing; the engine zero-pads its head
//     exactly like a full render's own head.
//  5. Source cut end = min(total_frames, llrint(e_src) + N) — a tail margin
//     of REAL audio so the frames near T_e read what the full render reads
//     and the null extends through the last kept sample (slightly too long
//     at the tail).
//  6. Trimmed warp frame map = the full map translated by (-cut_begin in
//     source, -A0*R_s in target); out-of-window breakpoints dropped; a seed
//     anchor interpolated at target 0 (it lies on the containing segment's
//     line, so it changes the function nowhere); a closing anchor at the cut
//     audio's own EOF pair — source = cut length, target = the translated
//     image of the cut end — exactly the way build_warp_frame_map closes
//     every full map at source EOF. Strictly ascending on both axes (the
//     engine's init validators are unchanged tripwires).
//  7. Trimmed phase reset frame map = the full deliverable-form derivation
//     translated by the same -cut_begin and range-filtered to the cut's
//     query range (the filter predicate is derived at the loop below).
//  8. The null condition is the HOP-MULTIPLE TARGET RE-ANCHOR (A0*R_s), not
//     the source cut position: with it, the trimmed engine's read schedule
//     coincides read-for-read with the full render's frames A0+m, so the
//     null holds through the spectral limiter (its framing sits on the same
//     R_s grid) and through the peak limiter whenever it does not engage.
//  9. Crop: begin_sample = llrint(T_b) - A0*R_s, samples = llrint(T_e) -
//     llrint(T_b).
std::expected<TrimPlan, std::string> plan_trim(
        const std::vector<WarpFrameMapSegment>& full_warp_frame_map,
        const std::vector<double>& full_phase_reset_frame_map,
        bool has_begin, int64_t begin_frame,
        bool has_end,   int64_t end_frame,
        int64_t total_frames,
        int N, int R_s) {
    if (auto v = validate_trim_frames(has_begin, begin_frame,
                                      has_end, end_frame,
                                      total_frames,
                                      full_warp_frame_map); !v) {
        return std::unexpected(std::move(v.error()));
    }

    const double b_src = bound_source_frame(has_begin, begin_frame, 0);
    const double e_src = bound_source_frame(has_end, end_frame, total_frames);
    const double T_b = map_source_to_target(b_src, full_warp_frame_map);
    const double T_e = map_source_to_target(e_src, full_warp_frame_map);

    // Output origin hop with 3 hops of pre-roll (full OLA weight at T_b).
    int64_t A0 = static_cast<int64_t>(
                     std::floor(T_b / static_cast<double>(R_s))) - 3;
    if (A0 < 0) A0 = 0;
    const int64_t origin_target = A0 * static_cast<int64_t>(R_s);

    // Source cut: the origin hop's integer analysis read position at the
    // head, llrint(e_src) + N of real tail margin at the end. Named
    // cut_begin/cut_end to keep them distinct from the authored-bound
    // parameters (begin_frame / end_frame).
    int64_t cut_begin = std::llrint(
        map_target_to_source(static_cast<double>(origin_target),
                             full_warp_frame_map)
        - static_cast<double>(N) / 2.0);
    if (cut_begin < 0) cut_begin = 0;
    int64_t cut_end = std::llrint(e_src) + N;
    if (cut_end > total_frames) cut_end = total_frames;

    TrimPlan plan;
    plan.pre.begin_frame = cut_begin;
    plan.pre.frames      = cut_end - cut_begin;

    // Trimmed warp frame map: rigid translation of the full map by
    // (-cut_begin, -origin_target), seed anchor at target 0, closing
    // anchor at the cut's own EOF pair. The seed anchor is the full map's
    // exact inverse image of the origin target, translated — collinear with
    // its containing segment, so the piecewise function is unchanged
    // everywhere it is queried. Interior breakpoints are kept only strictly
    // between the two anchors on BOTH axes (the strict guards against the
    // last kept pair are the floating-point backstop that keeps the emitted
    // map strictly ascending, as in build_warp_frame_map's contract).
    std::vector<WarpFrameMapSegment>& tm = plan.pre.warp_frame_map;
    const double seed_src =
        map_target_to_source(static_cast<double>(origin_target),
                             full_warp_frame_map)
        - static_cast<double>(cut_begin);
    const double close_src = static_cast<double>(plan.pre.frames);
    const double close_tgt =
        map_source_to_target(static_cast<double>(cut_end),
                             full_warp_frame_map)
        - static_cast<double>(origin_target);
    tm.push_back(WarpFrameMapSegment{seed_src, 0.0});
    for (const auto& s : full_warp_frame_map) {
        const double st = s.src_frame - static_cast<double>(cut_begin);
        const double tt = s.tgt_frame - static_cast<double>(origin_target);
        if (st <= tm.back().src_frame || tt <= tm.back().tgt_frame) continue;
        if (st >= close_src || tt >= close_tgt) continue;
        tm.push_back(WarpFrameMapSegment{st, tt});
    }
    tm.push_back(WarpFrameMapSegment{close_src, close_tgt});

    // Trimmed phase reset frame map: the full deliverable-form derivation
    // translated by -cut_begin (the engine query domain is source-anchored,
    // so the source translation is the whole re-anchor) and range-filtered.
    //
    // Filter predicate, derived from the binding invariant — every reset
    // that fires in the full render inside the kept window must fire at the
    // identical synthesis frame in the trimmed render (the engine's frames
    // over the cut pair coincide read-for-read with the full render's frames
    // A0+m, so a reset firing at full frame A0+m must land on trimmed frame
    // m):
    //   HEAD: drop when llrint(translated) < the trimmed schedule's frame-0
    //   read, llrint(map_target_to_source(0, trimmed map) - N/2). This is
    //   the engine's own placement-drop verdict quantized exactly as Pass 1
    //   quantizes (llrint, then upper_bound over the read schedule), and by
    //   the schedule translation it equals the full render's "fires before
    //   frame A0" verdict — the reset precedes the trimmed schedule, exactly
    //   as it precedes the kept window in the full render.
    //   TAIL: drop when the translated position is at or past the cut's
    //   extent (>= pre.frames). Every reset the invariant needs reads real
    //   cut audio (its query sits at least N/2 before the cut end by the
    //   tail-margin construction), so nothing needed is dropped; what drops
    //   would otherwise clamp onto the trimmed schedule's final frames past
    //   the crop — frames the full render schedules differently and the crop
    //   discards, so keeping them could only perturb discarded samples.
    // Translation and subsetting both preserve strict ascent, so the engine's
    // strict-ascent init validator holds by construction.
    const int64_t schedule_origin_read = std::llrint(
        map_target_to_source(0.0, tm) - static_cast<double>(N) / 2.0);
    plan.pre.phase_reset_frame_map.reserve(full_phase_reset_frame_map.size());
    for (const double query : full_phase_reset_frame_map) {
        const double translated =
            query - static_cast<double>(cut_begin);
        if (std::llrint(translated) < schedule_origin_read) continue;
        if (translated >= static_cast<double>(plan.pre.frames)) continue;
        plan.pre.phase_reset_frame_map.push_back(translated);
    }

    // Crop to the exact authored window. begin_sample discards the pre-roll
    // (and the sub-hop remainder of T_b); samples is the authored target
    // span, exact. The engine's emit cap — llrint of the closing anchor's
    // target — always covers begin_sample + samples: the tail margin puts
    // the closing target at or beyond T_e, and llrint is monotone.
    plan.post.begin_sample = std::llrint(T_b) - origin_target;
    plan.post.samples      = std::llrint(T_e) - std::llrint(T_b);
    return plan;
}

std::expected<void, std::string> apply_post_trim(
        std::vector<float>& buffer, int channels, const PostTrim& post) {
    if (channels <= 0 || post.begin_sample < 0 || post.samples < 0) {
        return std::unexpected(
            "post-trim crop has an invalid shape (channels=" +
            std::to_string(channels) + ", begin_sample=" +
            std::to_string(post.begin_sample) + ", samples=" +
            std::to_string(post.samples) + ")");
    }
    const size_t begin = static_cast<size_t>(post.begin_sample) *
                         static_cast<size_t>(channels);
    const size_t count = static_cast<size_t>(post.samples) *
                         static_cast<size_t>(channels);
    // plan_trim's geometry guarantees the engine's emission covers
    // begin_sample + samples (see the crop comment there). If the buffer
    // cannot supply that window the extent contract broke; refuse rather
    // than zero-fill a padded deliverable.
    if (begin > buffer.size() || count > buffer.size() - begin) {
        return std::unexpected(
            "post-trim crop exceeds the engine emission (buffer " +
            std::to_string(buffer.size()) + " samples, crop needs " +
            std::to_string(begin) + " + " + std::to_string(count) + ")");
    }
    buffer.erase(buffer.begin(),
                 buffer.begin() + static_cast<std::ptrdiff_t>(begin));
    buffer.resize(count);
    return {};
}

std::expected<void, std::string> validate_render_projection(
        int64_t engine_output_frames, int64_t encoded_frames,
        int channels, bool limiter, bool encode_to_disk) {
    auto projected = checked_audio_sample_count(engine_output_frames,
                                                channels);
    if (!projected) {
        return std::unexpected(projected.error());
    }
    if (encode_to_disk) {
        const WavSampleFormat fmt =
            limiter ? WavSampleFormat::Pcm24 : WavSampleFormat::Float32;
        if (wav_projected_exceeds_riff_limits(
                fmt, channels, static_cast<uint64_t>(encoded_frames))) {
            return std::unexpected(
                "projected output of " + std::to_string(encoded_frames) +
                " frames exceeds RIFF 32-bit limits");
        }
    }
    return {};
}

std::expected<FinishRenderStatus, std::string> finish_render(
        std::vector<float>& buffer, int channels, int sample_rate,
        bool limiter, const PostTrim* post_trim,
        const std::string& output_wav_path,
        const std::atomic<bool>* cancel_flag) {
    const auto cancelled = [&]() {
        return cancel_flag && cancel_flag->load();
    };
    if (post_trim) {
        if (auto cropped = apply_post_trim(buffer, channels, *post_trim);
            !cropped) {
            return std::unexpected(cropped.error());
        }
    }
    if (cancelled()) return FinishRenderStatus::Cancelled;
    if (limiter) {
        apply_peak_limiter(buffer, channels, sample_rate,
                           kPeakLimiterCeilingDbfs, kPeakLimiterAttackMs,
                           kPeakLimiterReleaseMs);
        if (cancelled()) return FinishRenderStatus::Cancelled;
    }
    if (output_wav_path.empty()) {
        // Buffer route (target view). Target playback auditions the
        // deliverable lattice, so a limited buffer is snapped to PCM 24 in
        // place — one linear scan, negligible next to synthesis — making
        // fresh renders, cache hits, and archival-artifact loads carry
        // sample-identical target-view audio. Limiter-off buffers need no
        // snap: their float wav deliverable is the clean floats themselves.
        if (limiter) {
            for (float& sample : buffer) {
                sample = pcm24_quantize(sample);
            }
        }
        // Buffer-route completion gate: a cancel that lands during the snap
        // (or any earlier stage) must never surface as Completed — the
        // orchestrator treats Completed as licence to publish the buffer and
        // its cache entry.
        if (cancelled()) return FinishRenderStatus::Cancelled;
        return FinishRenderStatus::Completed;
    }
    const WavSampleFormat fmt =
        limiter ? WavSampleFormat::Pcm24 : WavSampleFormat::Float32;
    auto writer = WavWriter::open_file(output_wav_path, fmt, channels,
                                       sample_rate);
    if (!writer) {
        return std::unexpected("could not open output '" + output_wav_path +
                               "': " + writer.error());
    }
    const int64_t frames = static_cast<int64_t>(
        buffer.size() / static_cast<size_t>(channels));
    if (auto ok = writer->write_frames(buffer.data(), frames); !ok) {
        return std::unexpected("could not write output '" + output_wav_path +
                               "': " + ok.error());
    }
    if (auto closed = writer->close(); !closed) {
        return std::unexpected("could not close output '" + output_wav_path +
                               "': " + closed.error());
    }
    return FinishRenderStatus::Completed;
}
