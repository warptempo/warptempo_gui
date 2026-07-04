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

// Validate strict monotonicity of a (src,tgt) frame_map. Returns true if OK.
bool validate_frame_map_monotonic(const std::vector<FrameMapSegment>& tm) {
    for (size_t i = 1; i < tm.size(); ++i) {
        if (tm[i].src_frame <= tm[i - 1].src_frame) {
            std::cerr << "Error: frame_map entry " << i << " has non-monotonic src_frame ("
                      << tm[i - 1].src_frame << " -> "
                      << tm[i].src_frame << ").\n";
            return false;
        }
        if (tm[i].tgt_frame <= tm[i - 1].tgt_frame) {
            std::cerr << "Error: frame_map entry " << i << " has non-monotonic tgt_frame ("
                      << tm[i - 1].tgt_frame << " -> "
                      << tm[i].tgt_frame << ").\n";
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

    // Populate frame_map from caller and validate monotonicity. Whole-segment
    // copy carries the precise breakpoints; the dense schedule still reads the
    // rounded fields until the interpolation flip.
    audio_stft.frame_map = p.frame_map;
    if (!validate_frame_map_monotonic(audio_stft.frame_map)) return EngineResult::Failed;

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
        static_cast<size_t>(std::llrint(audio_stft.frame_map.back().tgt_frame)) +
        audio_stft.N;

    audio_stft.init_fftw();
    audio_stft.source_frame_positions = audio_stft.generate_source_frame_positions();
    // Synthesis window and emit cap. The engine renders the supplied map but
    // emits only emit_sample_cap output samples. On the explicit-cap path (a
    // trimmed render handed a pre-sliced sub-map), synthesis is bounded to the
    // frames that touch [0, emit_sample_cap); frames past that write solely into
    // the truncated tail, so omitting them is byte-identical and keeps trimmed
    // renders from synthesizing to the sub-map's far closing anchor. On the
    // default path (full render) the whole map is synthesized and the cap is the
    // map's last-anchor target.
    {
        const int num_frames =
            static_cast<int>(audio_stft.source_frame_positions.size());
        if (p.emit_sample_cap > 0) {
            audio_stft.emit_sample_cap = p.emit_sample_cap;
            // Frames whose output start (m*R_s) is >= emit_sample_cap contribute
            // nothing to the emitted region. Synthesize enough that the mono
            // length (wcount-1)*R_s + N/2 exceeds the cap (so the cap, not the
            // mono length, binds the output); the +2 frame margin guarantees it.
            int64_t need = (p.emit_sample_cap / audio_stft.R_s) + 2;
            if (need > num_frames) need = num_frames;
            audio_stft.synth_frame_end = static_cast<int>(need);
        } else {
            audio_stft.synth_frame_end = num_frames;
            // Full-render cap is the last anchor's target; map_source_to_target
            // at the final node is exactly that node's target, so read it direct.
            const double tgt_end = audio_stft.frame_map.back().tgt_frame;
            audio_stft.emit_sample_cap = static_cast<int64_t>(std::llrint(tgt_end));
            if (audio_stft.emit_sample_cap < 0) audio_stft.emit_sample_cap = 0;
        }
    }

    // synthesize_full buffers the full output before any write, so refuse
    // implausible allocations and un-finalizable disk shapes here, at the last
    // point the projected size is known and before synthesis cost is paid.
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
    audio_stft.phase_reset_placements.reserve(p.phase_reset_frames.size());
    const auto& fm = audio_stft.source_frame_positions;
    // The src-frame -> synth-frame upper_bound filter drops entries before
    // the first frame.
    for (size_t i = 0; i < p.phase_reset_frames.size(); ++i) {
        int64_t F = p.phase_reset_frames[i];
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
                  << " phase_reset_count=" << p.phase_reset_frames.size()
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
