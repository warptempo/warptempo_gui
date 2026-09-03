#include "trimmer.h"

#include "peak_limiter.h"
#include "pcm24.h"
#include "wav_io.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

// Terminal message strings in this file carry sentence-initial capitals
// (architect approval 2026-08-02, the terminal capitalization pass —
// text-only, otherwise byte-identical output). These refusals are printed
// as standalone messages after the program-name prefix (the CLI's bare
// "warptempo_cli: %s" sites, and the orchestrators' "%s; rendering
// untrimmed" line), so the capital belongs here at the definition; the
// GUI render pipeline also shows some of them after its "Render error: "
// category words, the same accepted cost recorded for the six GUI-painted
// refusals (warp_frame_map_build.cpp).

std::expected<void, std::string> validate_trim_frames(
        int64_t begin_frame, int64_t end_frame,
        int64_t total_frames,
        const std::vector<WarpFrameMapSegment>& full_warp_frame_map) {
    // Both bounds are set by contract (the trimmer requires the pair; the
    // orchestrators complete a lone bound to its extreme before calling). The
    // authored bounds
    // widen exactly to doubles — every int64 in range is exactly
    // representable at these magnitudes, audio frame counts far below 2^53.
    const double total = static_cast<double>(total_frames);
    const double b_src = static_cast<double>(begin_frame);
    const double e_src = static_cast<double>(end_frame);
    if (e_src <= b_src) {
        return std::unexpected("Trim end at or before trim begin");
    }
    if (b_src >= total) {
        return std::unexpected("Trim begin at or past source end");
    }
    if (e_src > total) {
        return std::unexpected("Trim end past source end");
    }
    // Target-span refusal: the authored window must round to at least one
    // output sample. T_b and T_e are the bounds' exact double target images
    // through the full map — the same values the crop is cut from — so this
    // check and the crop cannot disagree.
    const double T_b = map_source_to_target(b_src, full_warp_frame_map);
    const double T_e = map_source_to_target(e_src, full_warp_frame_map);
    if (std::llrint(T_e) - std::llrint(T_b) < 1) {
        return std::unexpected(
            "Trim target span rounds below one output sample");
    }
    return {};
}

// Trim geometry (architect-confirmed derivation; the null condition binds
// every choice). Notation: N and R_s from engine_geometry.h; total_frames =
// source length; the full map is the untrimmed warp frame map.
//
//  1. Authored source positions are whole int64 frames widened exactly to
//     doubles: b_src = begin_frame, e_src = end_frame — the authored bounds
//     themselves, bit-identical (both are set by contract).
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
//  5. Source cut end: the tail-influence chain from the last kept output
//     sample, computed in ABSOLUTE full-map coordinates (legitimate because
//     the trimmed hop grids coincide with the full render's — step 8):
//       K     = llrint(T_e) - 1, the last kept output sample (K >= 0:
//               validation guarantees llrint(T_e) >= llrint(T_b) + 1 with
//               T_b >= 0).
//       f_max = K / R_s (floor), the last spectral-limiter analysis frame
//               covering K. The limiter frames the emission on the same R_s
//               grid with N-wide windows, so that frame's analysis reads
//               emitted samples through L = f_max*R_s + N - 1: the trimmed
//               emission must carry full-render-identical audio through L.
//               The spectral limiter always runs, so this bound applies
//               unconditionally; it strictly contains the bare kept-audio
//               requirement (identical audio through K < L).
//       m_max = (L + N/2) / R_s (floor), the last synthesis frame whose OLA
//               window can touch sample L (the frame at exactly L + N/2
//               lands on window index 0, weight zero; including it costs at
//               most one hop of context and removes any dependence on
//               zero-weight edge behavior).
//     The chain ends one frame LATER than m_max: frame m_max's synthesized
//     phase depends on frame m_max+1's analysis CONTENT, because the PGHI
//     prep time-gradient is centered — pghi_prep for frame m reads ph_nxt,
//     the analysis phases of frame m+1 (stft_container.h; synthesis.cpp
//     delivers prep(frame) with the frame+1 slot). That one-deep lookahead
//     is the non-obvious link; do not shorten the chain to m_max. So with
//     c2 = map_target_to_source((m_max+1)*R_s), frame m_max+1's exact
//     source center, whose analysis read ends at
//     llrint(c2 - N/2) + N - 1 <= c2 + N/2 - 0.5,
//       cut_end = min(total_frames, llrint(c2) + N/2 + 1)
//     covers that read strictly (llrint(c2) >= c2 - 0.5) and every earlier
//     frame's read a fortiori (the map is monotone). The remaining coverage
//     requirements follow automatically:
//       - Schedule identity holds by CONSTRUCTION, not by coincidence: the
//         trimmed analysis schedule is not re-interpolated from the
//         translated map. plan_trim derives it below from the FULL map's own
//         evaluations at absolute hops A0+m and hands it to the engine
//         verbatim (pre.source_frame_schedule -> EngineParams), so every
//         trimmed entry IS the full render's llrint value at hop A0+m
//         translated by the integer -cut_begin — identically, over the WHOLE
//         schedule, not merely through entry m_max+1, and through real map
//         segments rather than the translated map's identity extrapolation.
//         cut_end >= c2 is therefore no longer needed for schedule identity;
//         it remains the AUDIO-coverage bound (real cut source present for
//         every read frame m_max+1 and earlier). That gives phase reset
//         PLACEMENT identity: a reset with llrint(query) < t_a(m_max+1)
//         places at the same frame <= m_max in both renders, and one at or
//         past t_a(m_max+1) places past m_max in BOTH renders (the schedules
//         are nondecreasing and, by construction, the trimmed one IS the full
//         one translated over its whole length), where it can only influence
//         samples past L.
//       - The emit cap — llrint of the closing target — is at or past the
//         translated (m_max+1)*R_s minus rounding, which exceeds the
//         translated L by more than N/2 minus rounding, so the trimmed
//         emission carries real full-render-identical audio through L —
//         everything a kept-sample-covering limiter frame reads.
//     When the min() clamps, the closing anchor IS the full map's EOF pair:
//     the trimmed schedule, extrapolation, emission extent, and zero-padded
//     reads past the buffer end all coincide with the full render's own
//     tail behavior, so the null survives the clamp. Synthesis frames past
//     m_max+1 may read past the cut buffer's end zero-padded exactly as a
//     full render reads past its own source end; they influence only
//     samples past L.
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
//     the source cut position: with it, the trimmed engine's synthesis
//     frame m coincides with the full render's frame A0+m and the trimmed
//     limiter's frame grid coincides with the full render's limiter grid —
//     the coincidence step 5's influence chain is computed on. That frame
//     coincidence is now read-for-read by CONSTRUCTION: the engine does not
//     re-interpolate the translated map for its analysis schedule — plan_trim
//     derives the schedule from the FULL map at absolute hops A0+m and the
//     engine adopts it verbatim (source_frame_schedule), so trimmed frame m's
//     read position equals the full render's frame A0+m read position minus
//     the integer cut_begin exactly, with no half-integer-tie divergence
//     between two floating interpolators. Frames through m_max read identical
//     source audio, see the identical lookahead frame m_max+1, and fire
//     identical phase resets (step 5), so the emission is full-render-
//     identical through L and every limiter frame covering a kept sample
//     analyzes identical AUDIO. The synthesis/PGHI null therefore holds
//     unconditionally through the last kept sample. The LIMITERS sit outside that guarantee by ruling: the
//     peak limiter whenever it engages (unchanged), and the spectral
//     limiter's per-peak DECISION layer, which is nonlocal — resolving any
//     peak rescans plus-minus 100 limiter hops, removed peaks re-detect
//     with their retry lineage incremented, and a lineage reaching the
//     retry cap retires its peak WITHOUT a gain update — so whether a peak
//     near either window edge is acted on can depend on peaks far outside
//     any finite cut, and the coupling chains transitively, so no finite
//     margin can close it. The limiter is unconditional, so the null is guaranteed
//     only when no over-spectral-ceiling peak lies within lineage-coupling
//     range of either window edge; in practice trim bounds sit in quiet
//     seams (the audition's meat is at the window's center), and the null
//     contract is understood as a phase/synthesis contract.
//  9. Crop: begin_sample = llrint(T_b) - A0*R_s, samples = llrint(T_e) -
//     llrint(T_b).
std::expected<TrimPlan, std::string> plan_trim(
        const std::vector<WarpFrameMapSegment>& full_warp_frame_map,
        const std::vector<double>& full_phase_reset_frame_map,
        int64_t begin_frame, int64_t end_frame,
        int64_t total_frames,
        int N, int R_s) {
    if (auto v = validate_trim_frames(begin_frame, end_frame,
                                      total_frames,
                                      full_warp_frame_map); !v) {
        return std::unexpected(std::move(v.error()));
    }

    const double b_src = static_cast<double>(begin_frame);
    const double e_src = static_cast<double>(end_frame);
    const double T_b = map_source_to_target(b_src, full_warp_frame_map);
    const double T_e = map_source_to_target(e_src, full_warp_frame_map);

    // Output origin hop with 3 hops of pre-roll (full OLA weight at T_b).
    int64_t A0 = static_cast<int64_t>(
                     std::floor(T_b / static_cast<double>(R_s))) - 3;
    if (A0 < 0) A0 = 0;
    const int64_t origin_target = A0 * static_cast<int64_t>(R_s);

    // Source cut: the origin hop's integer analysis read position at the
    // head; at the tail, the influence chain of derivation step 5 — last
    // kept sample -> last limiter frame covering it -> its read extent ->
    // last synthesis frame touching that -> its centered-gradient lookahead
    // frame -> that frame's analysis read, rounded outward. Named
    // cut_begin/cut_end to keep them distinct from the authored-bound
    // parameters (begin_frame / end_frame).
    int64_t cut_begin = std::llrint(
        map_target_to_source(static_cast<double>(origin_target),
                             full_warp_frame_map)
        - static_cast<double>(N) / 2.0);
    if (cut_begin < 0) cut_begin = 0;
    const int64_t R64 = static_cast<int64_t>(R_s);
    const int64_t N64 = static_cast<int64_t>(N);
    // K >= 0 (see step 5), so the floor divisions below are plain
    // nonnegative integer division.
    const int64_t K     = std::llrint(T_e) - 1;
    const int64_t f_max = K / R64;
    const int64_t L     = f_max * R64 + N64 - 1;
    const int64_t m_max = (L + N64 / 2) / R64;
    const double  c2    = map_target_to_source(
        static_cast<double>((m_max + 1) * R64), full_warp_frame_map);
    int64_t cut_end = std::llrint(c2) + N64 / 2 + 1;
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

    // Trimmed analysis schedule, handed to the engine verbatim
    // (EngineParams::source_frame_schedule). Entry m is the FULL render's own
    // schedule value at absolute synthesis hop A0 + m, translated into the
    // cut's source domain by the integer -cut_begin:
    //   positions[m] = llrint(map_target_to_source(origin_target + m*R_s,
    //                                               full map) - N/2) - cut_begin
    // — the EXACT expression the engine's generator evaluates for the full
    // render at hop A0 + m (origin_target + m*R_s = (A0 + m)*R_s), against the
    // same shared inline interpolator from warp_frame_map.h, the same llrint,
    // IEEE-defined arithmetic with no fast-math, so identical in any
    // translation unit; then an exact integer subtraction. The trimmed
    // schedule is therefore the full render's schedule rigidly translated by
    // construction: schedule identity with the full render no longer rests on
    // the translated map above re-interpolating llrint-identically to the full
    // map at half-integer ties (architect approval 2026-07-30; the site record
    // added 2026-08-29 under that day's approval). The loop
    // bound is the engine's own target-extent test on the TRANSLATED map
    // (m*R_s < llrint(close_tgt) + N, the same t_s < target_total_frames the
    // engine steps), derived from the same close_tgt the closing anchor
    // carries, so the entry count equals the engine's generation count by
    // construction.
    //
    // Tail nuance: entries past the closing anchor's target (m*R_s >=
    // close_tgt) now evaluate through the FULL map's real segments (translated)
    // rather than the translated map's identity extrapolation. Those entries
    // influence only samples the crop discards — the influence chain of
    // derivation step 5 ends at m_max+1, which the closing anchor covers — and
    // phase reset placement (an upper_bound over these entries) now enjoys
    // exact translation over the WHOLE schedule, not only through entry
    // m_max+1.
    const int64_t trimmed_target_total = std::llrint(close_tgt) + N64;
    if (R64 > 0) {
        plan.pre.source_frame_schedule.reserve(
            static_cast<size_t>(trimmed_target_total / R64) + 1);
    }
    for (int64_t m = 0; m * R64 < trimmed_target_total; ++m) {
        plan.pre.source_frame_schedule.push_back(
            std::llrint(
                map_target_to_source(
                    static_cast<double>(origin_target + m * R64),
                    full_warp_frame_map)
                - static_cast<double>(N) / 2.0)
            - cut_begin);
    }

    // Trimmed phase reset frame map: the full deliverable-form derivation
    // translated by -cut_begin (the engine query domain is source-anchored,
    // so the source translation is the whole re-anchor) and range-filtered.
    //
    // Filter predicate, derived from the binding invariant — every reset
    // that fires in the full render at a frame the kept window depends on
    // (frames through m_max, derivation step 5) must fire at the identical
    // synthesis frame in the trimmed render (the engine's frames over the cut
    // pair coincide read-for-read with the full render's frames A0+m BY
    // CONSTRUCTION now — the engine adopts source_frame_schedule, which is the
    // full render's schedule translated by the integer -cut_begin, above — so
    // a reset firing at full frame A0+m must land on trimmed frame m):
    //   HEAD: drop when llrint(translated) < the trimmed schedule's frame-0
    //   read, llrint(map_target_to_source(0, trimmed map) - N/2). That value
    //   equals source_frame_schedule.front() exactly (cut_begin is that same
    //   frame-0 read rounded, and subtracting an integer from a double far
    //   below 2^53 is exact — the result lies on the minuend's own ulp grid
    //   or finer — so A - cut_begin is exact and the two
    //   integer-shifted spellings round alike), so it is the engine's own
    //   placement-drop verdict quantized exactly as Pass 1 quantizes (llrint,
    //   then upper_bound over the read schedule); by the by-construction
    //   schedule translation it equals the full render's "fires before frame
    //   A0" verdict — the reset precedes the trimmed schedule, exactly as it
    //   precedes the kept window in the full render.
    //   TAIL: drop when the translated position is at or past the cut's
    //   extent (>= pre.frames). Every reset the invariant needs — one that
    //   places at a frame <= m_max in the full render — has llrint(query) <
    //   t_a(m_max+1) <= c2 - N/2 + 0.5, so its query sits about N before
    //   the cut end (cut_end >= c2 + N/2 + 0.5, derivation step 5): nothing
    //   needed is dropped, and its placement frame reads real cut audio.
    //   Under the total_frames clamp the margin is still over N/2: queries
    //   are authored frames minus N/2 with the authored wall at
    //   total_frames - 1. What drops (query at or past the cut end) has
    //   llrint(query) >= cut_end > t_a(m_max+1), so it places past frame
    //   m_max in the FULL render too; frames past m_max write only samples
    //   past L, which the crop discards and only limiter frames gating
    //   discarded samples read — so keeping them could only perturb
    //   discarded samples.
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
    // target — always covers begin_sample + samples: the derived cut end
    // puts the closing target past L + N/2 > T_e (unclamped, step 5), and
    // under the clamp it is the full map's own EOF target, at or beyond
    // T_e; llrint is monotone.
    // THE CROP IS END-EXCLUSIVE ON AN INCLUSIVE AUTHORED DOMAIN, ACCEPTED AND
    // RECORDED (architect 2026-09-02, the truthfulness deep dive's item K;
    // architect approval 2026-09-02, comment-only). The kept output is
    // [llrint(tgt(b)), llrint(tgt(e))), so the authored end frame e — the last
    // frame INSIDE the window by data-model.md's inclusive [0, total-1]
    // ruling, the frame End lands on and the endcap sits on — is the first
    // frame NOT rendered, one source frame's target span (1/tempo output
    // samples, 23 µs at 44.1 kHz). It is not fixed to map(e + 1): the trim is
    // already a rounded quantity here — both edges are llrint of mapped
    // positions and the pre-roll anchors on hop multiples to keep the phase
    // null past the first in-window reset — so the exclusive end is one more
    // sample of that same rounding class. It is also the whole reason the
    // full-window translation (trim_window_is_full) is load-bearing rather
    // than an optimization: a (0, total-1) pair through this crop would not be
    // byte-identical to untrimmed. The one place it shows is target view under
    // a sub-window, where End parks the playhead one sample past the preview
    // buffer's last frame and Space plays nothing.
    plan.post.begin_sample = std::llrint(T_b) - origin_target;
    plan.post.samples      = std::llrint(T_e) - std::llrint(T_b);
    return plan;
}

std::expected<void, std::string> apply_post_trim(
        std::vector<float>& buffer, int channels, const PostTrim& post) {
    if (channels <= 0 || post.begin_sample < 0 || post.samples < 0) {
        return std::unexpected(
            "Post-trim crop has an invalid shape (channels=" +
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
            "Post-trim crop exceeds the engine emission (buffer " +
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
        int channels, bool encode_to_disk) {
    auto projected = checked_audio_sample_count(engine_output_frames,
                                                channels);
    if (!projected) {
        return std::unexpected(projected.error());
    }
    if (encode_to_disk) {
        if (wav_projected_exceeds_riff_limits(
                channels, static_cast<uint64_t>(encoded_frames))) {
            return std::unexpected(
                "Projected output of " + std::to_string(encoded_frames) +
                " frames exceeds RIFF 32-bit limits");
        }
    }
    return {};
}

std::expected<FinishRenderStatus, std::string> finish_render(
        std::vector<float>& buffer, int channels, int sample_rate,
        const PostTrim* post_trim,
        const std::string& output_wav_path,
        const std::atomic<bool>* cancel_flag,
        std::optional<FinishRenderSinkFailure>* sink_failure) {
    const auto cancelled = [&]() {
        return cancel_flag && cancel_flag->load();
    };
    // The three sink refusals compose their sentence HERE, from the parts,
    // and hand the parts back where the caller asked for them (architect
    // approval 2026-09-02, the granted frozen touch): the GUI card names the
    // file the project's way with the writer's words after it, the terminal
    // keeps the full path, and a caller that had only the composed sentence
    // could reach the words only by parsing English around a path that may
    // itself hold a quote or a colon. The returned string is unchanged by
    // construction — one composition, both readers.
    const auto sink_refusal = [&](const char* before,
                                  const std::string& words) {
        const std::string after = ": " + words;
        if (sink_failure) {
            *sink_failure = FinishRenderSinkFailure{before, output_wav_path,
                                                    after};
        }
        return std::unexpected(before + ("'" + output_wav_path + "'") + after);
    };
    // Output-buffer contract, validated once before the crop, the limiter,
    // and sink selection so every route below may assume a well-shaped,
    // finite buffer. A legal engine emission always passes; a violation is an
    // internal breach, refused loudly rather than left to route-dependent
    // handling (a division by zero, a silently floored ragged tail, or a
    // non-finite sample the buffer route maps to code zero while a writer
    // refuses it). The finiteness scan is a single linear pass — negligible
    // next to synthesis, the same argument as the buffer route's PCM 24 snap.
    if (channels <= 0) {
        return std::unexpected(
            "Render buffer has an invalid channel count (channels=" +
            std::to_string(channels) + ")");
    }
    if (sample_rate <= 0) {
        return std::unexpected(
            "Render buffer has an invalid sample rate (sample_rate=" +
            std::to_string(sample_rate) + ")");
    }
    if (buffer.size() % static_cast<size_t>(channels) != 0) {
        return std::unexpected(
            "Render buffer is not whole interleaved frames (size=" +
            std::to_string(buffer.size()) + ", channels=" +
            std::to_string(channels) + ")");
    }
    for (size_t i = 0; i < buffer.size(); ++i) {
        if (!std::isfinite(buffer[i])) {
            return std::unexpected(
                "Render buffer has a non-finite sample at index " +
                std::to_string(i));
        }
    }
    if (post_trim) {
        if (auto cropped = apply_post_trim(buffer, channels, *post_trim);
            !cropped) {
            return std::unexpected(cropped.error());
        }
    }
    if (cancelled()) return FinishRenderStatus::Cancelled;
    apply_peak_limiter(buffer, channels, sample_rate);
    if (cancelled()) return FinishRenderStatus::Cancelled;
    if (output_wav_path.empty()) {
        // Buffer route (target view). Target playback auditions the
        // deliverable lattice, so the buffer is snapped to PCM 24 in place —
        // one linear scan, negligible next to synthesis — making fresh
        // renders, cache hits, and archival-artifact loads carry
        // sample-identical target-view audio.
        for (float& sample : buffer) {
            sample = pcm24_quantize(sample);
        }
        // Buffer-route completion gate: a cancel that lands during the snap
        // (or any earlier stage) must never surface as Completed — the
        // orchestrator treats Completed as licence to publish the buffer and
        // its cache entry.
        if (cancelled()) return FinishRenderStatus::Cancelled;
        return FinishRenderStatus::Completed;
    }
    auto writer = WavWriter::open_file(output_wav_path, channels,
                                       sample_rate);
    if (!writer) {
        return sink_refusal("Could not open output ", writer.error());
    }
    const int64_t frames = static_cast<int64_t>(
        buffer.size() / static_cast<size_t>(channels));
    if (auto ok = writer->write_frames(buffer.data(), frames); !ok) {
        return sink_refusal("Could not write output ", ok.error());
    }
    if (auto closed = writer->close(); !closed) {
        return sink_refusal("Could not close output ", closed.error());
    }
    return FinishRenderStatus::Completed;
}
