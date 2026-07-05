// warptempo_gui - phase vocoder for time-warping classical orchestral
// recordings toward target tempos.
//
// Copyright (C) 2024-2026  warptempo
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
// for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>.

#include "engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <fftw3.h>

#include "stft_container.h"
#include "limiter.h"
#include "synthesis.h"
#include "profile_util.h"
#include "wav_io.h"

namespace {

// Initialize FFTW's thread support for deterministic single-thread plans. Sets
// audio_stft.fftw_threads_inited if init succeeded.
void init_fftw_threads(AudioSTFT& audio_stft) {
    // Per-transform threading intentionally off: channel-level threads
    // (synthesis.cpp) are the parallelism; FFTW internal threads on a single
    // 8192 transform are net-negative and would nest.
    if (fftw_init_threads()) {
        // Single-thread FFTW planning is a determinism invariant: a multi-threaded
        // transform splits the work nondeterministically, breaking bit-identical
        // output. Must stay 1.
        fftw_plan_with_nthreads(1);
        audio_stft.fftw_threads_inited = true;
    } else {
        std::cerr << "  ! fftw_init_threads failed; FFTW will run single-threaded.\n";
    }
}

// Engine-boundary ordering guards. These two init-time hardfails are
// deliberate and stay even though the warp_frame_map.h /
// phase_reset_frame_map.h artifact readers validate
// line shape only: the writers' contract (build_warp_maps, the trimmed-artifact
// derivation, the phaseresetframemap writer) makes both checks unreachable
// from
// program-written inputs, but a breach — a hand-edited artifact fed to the
// engine CLI, or a future writer bug — would otherwise render silently wrong
// deliverable bytes (a misinterpolated map, or resets silently skipped by the
// forward synthesis cursor). A loud init refusal is the designed response.
//
// The two predicates differ on purpose. The frame map must be strictly
// ascending on both axes: the map helpers binary-search and interpolate over
// it, and an equal breakpoint is a zero-length segment (division by zero in
// the interpolation). The reset list only needs to be non-decreasing,
// duplicates allowed: each reset frame maps through upper_bound over the
// monotone source_frame_positions, so a non-decreasing input yields
// non-decreasing synth-frame placements, and the synthesis cursor's while
// loop consumes equal placements on the same frame correctly. Equal reset
// frames are unreachable under the usage model: the dispatch chain's
// forward-map, subtract-anticipation, inverse-map slopes cancel within a
// tempo segment, so two authored resets a millisecond apart land about 44
// engine frames apart at 44100 Hz regardless of compression, and even a
// boundary-straddling anticipation gap needs a slope ratio near ninety to
// collide them — far past the usage model's actual ceiling. They remain
// legal, though, under the no-ceiling rule for tempo products and
// ref-implied multipliers, and the consumer handles them correctly. Only a
// decrease is a breach: the writer's output is provably non-decreasing, and
// a decrease of even one sample can land in a synth frame the forward-only
// cursor has already passed, which it would then skip silently. So the
// predicate rejects every decrease and nothing else, with no epsilon band.

// Validate strict monotonicity of a (src,tgt) warp_frame_map. Returns true if OK.
bool validate_warp_frame_map_monotonic(const std::vector<WarpFrameMapSegment>& tm) {
    for (size_t i = 1; i < tm.size(); ++i) {
        if (tm[i].src_frame <= tm[i - 1].src_frame) {
            std::cerr << "Error: warp_frame_map entry " << i << " has non-monotonic src_frame ("
                      << tm[i - 1].src_frame << " -> "
                      << tm[i].src_frame << ").\n";
            return false;
        }
        if (tm[i].tgt_frame <= tm[i - 1].tgt_frame) {
            std::cerr << "Error: warp_frame_map entry " << i << " has non-monotonic tgt_frame ("
                      << tm[i - 1].tgt_frame << " -> "
                      << tm[i].tgt_frame << ").\n";
            return false;
        }
    }
    return true;
}

// Validate the phase reset list is non-decreasing (duplicates allowed; see
// the ruling comment above). Returns true if OK.
bool validate_phase_reset_frame_map_ordered(const std::vector<double>& resets) {
    for (size_t i = 1; i < resets.size(); ++i) {
        if (resets[i] < resets[i - 1]) {
            std::cerr << "Error: phase reset entry " << i << " is out of order ("
                      << resets[i - 1] << " -> "
                      << resets[i] << ").\n";
            return false;
        }
    }
    return true;
}

} // namespace

EngineResult run_warptempo_engine(const EngineParams& p,
                                  const std::atomic<bool>* cancel_flag) {
    AudioSTFT audio_stft;

    audio_stft.N = p.N;
    audio_stft.cancel_flag = cancel_flag;

    // Env-gated pass-level profiling. Timers accumulate unconditionally (the
    // now() overhead is negligible against a full render); the [profile] line
    // is emitted to std::cerr only when WARPTEMPO_PROFILE is set. Runtime env
    // gating only — no compile-time macro — so it toggles without a rebuild.
    const bool prof = profile::enabled();
    init_fftw_threads(audio_stft);

    auto& lp = audio_stft.limiter_params;
    lp.ceiling_dbfs         = p.limiter_ceiling_dbfs;
    lp.tolerance_db         = p.limiter_tolerance_db;

    // "<buffer>" is a log-only sentinel — never pass it to filesystem APIs.
    // The output writer is gated off on the buffer-output path; the limiter
    // reads this field only as a string comparison for its verbose gate.
    audio_stft.output_audio_file       =
        p.output_buffer ? std::string("<buffer>") : p.output_audio_path;
    audio_stft.limiter                 = p.limiter;
    audio_stft.peak_limiter_ceiling_dbfs = p.peak_limiter_ceiling_dbfs;
    audio_stft.peak_limiter_attack_ms    = p.peak_limiter_attack_ms;
    audio_stft.peak_limiter_release_ms   = p.peak_limiter_release_ms;

    if (audio_stft.N % 4 != 0) {
        std::cerr << "Error: N must be divisible by 4.\n";
        return EngineResult::Failed;
    }

    // Populate warp_frame_map from caller and validate monotonicity. Whole-segment
    // copy carries the precise breakpoints; the dense schedule still reads the
    // rounded fields until the interpolation flip.
    audio_stft.warp_frame_map = p.warp_frame_map;
    if (!validate_warp_frame_map_monotonic(audio_stft.warp_frame_map)) return EngineResult::Failed;
    if (!validate_phase_reset_frame_map_ordered(p.phase_reset_frame_map)) return EngineResult::Failed;

    if (p.source_audio_samples == nullptr || p.source_audio_frames == 0 ||
        p.source_channels <= 0 || p.source_sample_rate <= 0) {
        std::cerr << "Error: source buffer parameters are invalid "
                     "(samples=" << static_cast<const void*>(p.source_audio_samples)
                  << ", frames=" << p.source_audio_frames
                  << ", channels=" << p.source_channels
                  << ", sr=" << p.source_sample_rate << ")\n";
        return EngineResult::Failed;
    }

    audio_stft.src_info.samplerate = p.source_sample_rate;
    audio_stft.src_info.channels   = p.source_channels;
    audio_stft.src_info.frames     = static_cast<int64_t>(p.source_audio_frames);

    audio_stft.src_samples = p.source_audio_samples;
    audio_stft.channels = audio_stft.src_info.channels;
    audio_stft.target_total_frames =
        static_cast<size_t>(std::llrint(audio_stft.warp_frame_map.back().tgt_frame)) +
        audio_stft.N;

    audio_stft.init_fftw();
    // Emit cap. The engine renders the supplied map but emits only
    // emit_sample_cap output samples. On the explicit-cap path (a trimmed
    // render handed a pre-sliced sub-map) the cap comes straight from the
    // caller; on the default path (full render) it is the map's last-anchor
    // target. The cap is resolved here, before the dense schedule is
    // generated, so the output-size refusals below can reject an implausible
    // render before the schedule's target-length-proportional reserve is
    // paid.
    if (p.emit_sample_cap > 0) {
        audio_stft.emit_sample_cap = p.emit_sample_cap;
    } else {
        // Full-render cap is the last anchor's target; map_source_to_target
        // at the final node is exactly that node's target, so read it direct.
        const double tgt_end = audio_stft.warp_frame_map.back().tgt_frame;
        audio_stft.emit_sample_cap = static_cast<int64_t>(std::llrint(tgt_end));
        // A sub-half-sample final target rounds to a zero cap, and the
        // synthesis loop reads cap 0 as uncapped — the render would emit
        // the full STFT tail instead of a near-zero-length deliverable.
        // A deliverable of zero samples is not renderable output, so
        // refuse it here, symmetric with the trimmed path's degenerate
        // window refusal.
        if (audio_stft.emit_sample_cap <= 0) {
            std::cerr << "Error: render refused: final map target of "
                      << tgt_end
                      << " frames rounds to zero output samples\n";
            audio_stft.cleanup();
            return EngineResult::Failed;
        }
    }

    // synthesize_full buffers the full output before any write, so refuse
    // implausible allocations and un-finalizable disk shapes here, as soon as
    // the projected size is known and before any output-proportional cost —
    // the dense schedule's reserve or the synthesis itself — is paid.
    auto projected = checked_audio_sample_count(audio_stft.emit_sample_cap,
                                                audio_stft.channels);
    if (!projected) {
        std::cerr << "Error: render refused: " << projected.error() << "\n";
        audio_stft.cleanup();
        return EngineResult::Failed;
    }
    if (!p.output_buffer) {
        const WavSampleFormat fmt = audio_stft.limiter
            ? WavSampleFormat::Pcm24 : WavSampleFormat::Float32;
        if (wav_projected_exceeds_riff_limits(
                fmt, audio_stft.channels,
                static_cast<uint64_t>(audio_stft.emit_sample_cap))) {
            std::cerr << "Error: render refused: projected output of "
                      << audio_stft.emit_sample_cap
                      << " frames exceeds RIFF 32-bit limits\n";
            audio_stft.cleanup();
            return EngineResult::Failed;
        }
    }

    audio_stft.source_frame_positions = audio_stft.generate_source_frame_positions();
    // Synthesis window. On the explicit-cap path synthesis is bounded to
    // exactly the frames whose OLA span touches [0, emit_sample_cap) and no
    // further, so trimmed renders do not synthesize out to the sub-map's far
    // closing anchor. On the default path the whole map is synthesized.
    {
        const int num_frames =
            static_cast<int>(audio_stft.source_frame_positions.size());
        if (p.emit_sample_cap > 0) {
            // After the N/2 head trim, frame m covers output samples
            // [m*R_s - N/2, m*R_s + N/2), and every emitted sample needs all four
            // overlapping frames for unity COLA gain. The last frame overlapping
            // output sample cap-1 is (cap-1)/R_s + 2, so synthesis runs through it
            // inclusive. This also keeps the mono push total above the cap, so the
            // cap, not the mono length, binds the emitted count.
            int64_t need = ((p.emit_sample_cap - 1) / audio_stft.R_s) + 3;
            if (need > num_frames) need = num_frames;
            audio_stft.synth_frame_end = static_cast<int>(need);
        } else {
            audio_stft.synth_frame_end = num_frames;
        }
    }

    double duration_sec = static_cast<double>(audio_stft.emit_sample_cap) /
                          audio_stft.src_info.samplerate;
    std::cout << "[warptempo] Source: <in-memory buffer, "
              << p.source_audio_frames << " frames>"
              << ", Target: " << std::fixed << duration_sec << "s at "
              << audio_stft.src_info.samplerate << "Hz\n";

    Limiter        limiter;
    Synthesis      synthesis;

    double p1_ms = 0.0, p2_ms = 0.0, p3_ms = 0.0;

    // Pass 1: phase reset placement.
    auto t_p1_0 = profile::now();
    audio_stft.phase_reset_placements.clear();
    audio_stft.phase_reset_placements.reserve(p.phase_reset_frame_map.size());
    const auto& fm = audio_stft.source_frame_positions;
    // The src-frame -> synth-frame upper_bound filter drops entries before
    // the first frame.
    for (size_t i = 0; i < p.phase_reset_frame_map.size(); ++i) {
        // Quantization into the integer query schedule happens here,
        // engine-owned, symmetric with generate_source_frame_positions.
        // Rounding before the less-than-or-equal search makes a position
        // within half a sample below a schedule entry count as at that entry.
        const int64_t F =
            static_cast<int64_t>(std::llrint(p.phase_reset_frame_map[i]));
        auto it = std::upper_bound(fm.begin(), fm.end(), F);
        if (it == fm.begin()) continue;
        --it;
        size_t s = static_cast<size_t>(it - fm.begin());
        audio_stft.phase_reset_placements.push_back({static_cast<int>(s), F});
    }
    std::cout << "[Pass 1/" << (audio_stft.limiter ? 3 : 2)
              << "] Phase reset placement............. "
              << audio_stft.phase_reset_placements.size()
              << " phase resets\n";
    auto t_p1_1 = profile::now();
    p1_ms = profile::ms(t_p1_0, t_p1_1);
    std::cout << "  (" << static_cast<long long>(profile::ms(t_p1_0, t_p1_1)) << " ms)\n";
    if (prof) {
        std::cerr << "[profile] engine_pass name=phase_reset_placement ms="
                  << p1_ms
                  << " phase_reset_count=" << p.phase_reset_frame_map.size()
                  << " placed_count=" << audio_stft.phase_reset_placements.size()
                  << "\n";
    }

    // Pass 2: synthesis (clean render). The limiter-on disk path renders into
    // an in-memory buffer that Pass 3 limits in place; every other path streams
    // straight to its destination. synthesize_full applies no attenuation, so
    // the limiter-off disk render is byte-identical to the pre-relocation build.
    // The limited chain runs on a buffer; the clean (None) disk path streams
    // float straight to file.
    const bool limited = audio_stft.limiter;
    std::vector<float> render_buf;
    auto t_p2_0 = profile::now();
    if (p.output_buffer) {
        synthesis.process_to_buffer(audio_stft, p.output_buffer);
    } else if (limited) {
        synthesis.process_to_buffer(audio_stft, &render_buf);
    } else {
        if (!synthesis.process(audio_stft)) {     // None -> 32-bit float, clean
            std::cerr << "  ! render failed: output write error '"
                      << audio_stft.output_audio_file << "'\n";
            audio_stft.cleanup();
            return EngineResult::Failed;
        }
    }
    auto t_p2_1 = profile::now();
    p2_ms = profile::ms(t_p2_0, t_p2_1);
    std::cout << "  (" << static_cast<long long>(profile::ms(t_p2_0, t_p2_1)) << " ms)\n";
    if (prof) {
        const std::vector<float>* out_buf = p.output_buffer ? p.output_buffer
            : (limited ? &render_buf : nullptr);
        const size_t out_samples = out_buf ? out_buf->size() : 0;
        const size_t out_frames = (audio_stft.channels > 0)
            ? out_samples / static_cast<size_t>(audio_stft.channels) : 0;
        std::cerr << "[profile] engine_pass name=synthesis ms="
                  << p2_ms
                  << " source_frames=" << p.source_audio_frames
                  << " target_frames=" << audio_stft.emit_sample_cap
                  << " channels=" << audio_stft.channels
                  << " output_buffer=" << (p.output_buffer ? "yes" : "no")
                  << " output_frames=" << out_frames
                  << "\n";
    }
    if (audio_stft.cancellation_observed) {
        std::cerr << "[Cancelled] " << audio_stft.output_audio_file << "\n";
        audio_stft.cleanup();
        return EngineResult::Cancelled;
    }

    // Pass 3: the limited chain — spectral(-0.3) then peak(0) backstop — applied
    // in place on whichever buffer Pass 2 filled (disk render or target view).
    // The None disk path has no Pass 3. The disk render is then written out.
    if (limited) {
        auto t_p3_0 = profile::now();
        std::vector<float>& buf = p.output_buffer ? *p.output_buffer : render_buf;
        const size_t limiter_input_frames = (audio_stft.channels > 0)
            ? buf.size() / static_cast<size_t>(audio_stft.channels) : 0;
        limiter.process(audio_stft, buf);          // spectral -0.3
        if (audio_stft.cancellation_observed) {
            std::cerr << "[Cancelled] " << audio_stft.output_audio_file << "\n";
            audio_stft.cleanup();
            return EngineResult::Cancelled;
        }
        apply_peak_backstop(audio_stft, buf);      // peak 0 net
        if (!p.output_buffer) {
            if (!synthesis.write_render_to_file(audio_stft, render_buf)) {  // plain write
                std::cerr << "  ! render failed: output write error '"
                          << audio_stft.output_audio_file << "'\n";
                audio_stft.cleanup();
                return EngineResult::Failed;
            }
        }
        auto t_p3_1 = profile::now();
        p3_ms = profile::ms(t_p3_0, t_p3_1);
        std::cout << "  (" << static_cast<long long>(profile::ms(t_p3_0, t_p3_1)) << " ms)\n";
        if (prof) {
            std::cerr << "[profile] engine_pass name=limiter ms="
                      << p3_ms
                      << " sample_frames=" << limiter_input_frames
                      << " channels=" << audio_stft.channels
                      << " output_buffer=" << (p.output_buffer ? "yes" : "no")
                      << "\n";
        }
    } else if (prof) {
        std::cerr << "[profile] engine_pass name=limiter ms=0.000 sample_frames=0 channels="
                  << audio_stft.channels << " bypass=yes\n";
    }

    if (prof) {
        std::cerr << "[profile] passes:"
                  << " P1=" << p1_ms
                  << " P2=" << p2_ms
                  << " P3=" << p3_ms << "\n";
    }

    std::cout << "[Success] " << audio_stft.output_audio_file << "\n";

    audio_stft.cleanup();
    return EngineResult::Success;
}
