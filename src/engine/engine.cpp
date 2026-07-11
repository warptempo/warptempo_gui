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
        std::cerr << "! fftw_init_threads failed; FFTW will run single-threaded.\n";
    }
}

// Engine-boundary input guards. The two validators are fully symmetric
// per column: same per-entry finiteness predicate, same strict-ascent
// predicate, no epsilon band, same loud init refusal. The map simply has
// two columns (src and tgt) to the reset list's one because the objects'
// shapes differ, not their treatment. These init-time hardfails are
// deliberate and stay even though the producers' contract
// (build_warp_frame_map, the parser's phase reset frame map derivation,
// and the prepost trimmer's translate/filter, which preserves strict
// ascent on both columns) makes them unreachable from program-written
// inputs: a breach — a producer bug, or a future driver fed hand-edited
// artifacts — would otherwise render silently wrong deliverable bytes (a
// misinterpolated map, or resets silently skipped by the forward
// synthesis cursor). A loud init refusal is the designed response.
//
// Strictness is the right predicate on both lists because the engine
// validates pre-quantization doubles, and every program path produces
// strictly increasing ones. Authored marker files load with equal times
// permitted, but neither column lets a tie reach the engine: strict
// ascent of the phase reset input is guaranteed by the raw-store
// coincidence refusal (marker_store_validate.h, at commit and load) plus
// build_phase_reset_source_frames' sub-frame refusal and the parser's
// derivation (engine query positions are the authored source frames
// shifted by the constant N/2, so the strictly increasing authored list
// carries strict ascent through unchanged; the render-end drop only
// shortens the list), with .phaseresetframemap artifacts written from that same
// derivation; warp ordering degeneracy is refused at build_warp_frame_map
// (its sub-frame segment error) before any engine input exists. Equal
// values here therefore always mean a breach. No epsilon band on either.
//
// Finiteness is checked explicitly on EVERY entry — including entry zero,
// which the ascent loops never inspect — because the ascent comparisons
// are not finite checks: NaN compares false against everything and would
// slip through, and a trailing +inf passes ascent (inf is greater than
// any finite prior). The producers validate finiteness at build time
// (build_warp_frame_map refuses non-finite tempo-scale divisors and
// non-finite or non-advancing target anchors at its emission chokepoint;
// the phase reset derivation emits finite authored positions shifted by
// a finite constant), so a non-finite entry here likewise always means a
// breach.

// Validate the (src,tgt) warp_frame_map: non-empty, finite entries, strict
// ascent on both axes. Returns true if OK. The empty refusal guards the
// .back() reads at init (target_total_frames, emit_sample_cap) and the
// map interpolation in the dense schedule: program paths always emit the
// {0,0} seed segment, so an empty map means a hand-edited artifact —
// exactly the breach class these guards exist for. An EMPTY phase reset
// list, by contrast, stays legal in the sibling validator below (no phase
// resets is a normal render): the map is the engine's geometry and must
// have an extent, the reset list is optional point events — arity and
// role, not treatment asymmetry.
bool validate_warp_frame_map_strictly_ascending(const std::vector<WarpFrameMapSegment>& map) {
    if (map.empty()) {
        std::cerr << "Error: warp_frame_map is empty (program-written maps "
                     "always carry the {0,0} seed segment).\n";
        return false;
    }
    for (size_t i = 0; i < map.size(); ++i) {
        if (!std::isfinite(map[i].src_frame) || !std::isfinite(map[i].tgt_frame)) {
            std::cerr << "Error: warp_frame_map entry " << i << " is not finite ("
                      << map[i].src_frame << ", "
                      << map[i].tgt_frame << ").\n";
            return false;
        }
        if (i == 0) continue;
        if (map[i].src_frame <= map[i - 1].src_frame) {
            std::cerr << "Error: warp_frame_map entry " << i << " has non-monotonic src_frame ("
                      << map[i - 1].src_frame << " -> "
                      << map[i].src_frame << ").\n";
            return false;
        }
        if (map[i].tgt_frame <= map[i - 1].tgt_frame) {
            std::cerr << "Error: warp_frame_map entry " << i << " has non-monotonic tgt_frame ("
                      << map[i - 1].tgt_frame << " -> "
                      << map[i].tgt_frame << ").\n";
            return false;
        }
    }
    return true;
}

// Validate the phase reset list: finite entries, strictly ascending (see
// the ruling comment above). An empty list is legal — no phase resets is
// a normal render. Returns true if OK.
bool validate_phase_reset_frame_map_strictly_ascending(const std::vector<double>& resets) {
    for (size_t i = 0; i < resets.size(); ++i) {
        if (!std::isfinite(resets[i])) {
            std::cerr << "Error: phase reset entry " << i << " is not finite ("
                      << resets[i] << ").\n";
            return false;
        }
        if (i == 0) continue;
        if (resets[i] <= resets[i - 1]) {
            std::cerr << "Error: phase reset entry " << i << " is not strictly ascending ("
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

    audio_stft.limiter         = p.limiter;
    audio_stft.limiter_verbose = p.limiter_verbose;

    if (audio_stft.N % 4 != 0) {
        std::cerr << "Error: N must be divisible by 4.\n";
        return EngineResult::Failed;
    }

    // Buffer-out only: the output buffer is the engine's sole sink; encode
    // lives orchestrator-side in the prepost chain.
    if (p.output_buffer == nullptr) {
        std::cerr << "Error: output_buffer is required "
                     "(the engine is buffer-out only).\n";
        return EngineResult::Failed;
    }

    // Populate warp_frame_map from caller and validate strict ascent. Whole-segment
    // copy carries the precise breakpoints; the dense schedule still reads the
    // rounded fields until the interpolation flip.
    audio_stft.warp_frame_map = p.warp_frame_map;
    if (!validate_warp_frame_map_strictly_ascending(audio_stft.warp_frame_map)) return EngineResult::Failed;
    if (!validate_phase_reset_frame_map_strictly_ascending(p.phase_reset_frame_map)) return EngineResult::Failed;

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
    // Emit cap: the map's last anchor is the engine's single termination
    // owner. Output length is llrint of the last anchor's target on every
    // path — map-extent emission is pure full-render behavior, and a trimmed
    // render's translated map carries its own closing anchor, so trimmed
    // renders behave exactly like full renders here; map_source_to_target at
    // the final node is exactly that node's target, so read it direct.
    // Resolved here, before the dense schedule is generated.
    const double tgt_end = audio_stft.warp_frame_map.back().tgt_frame;
    audio_stft.emit_sample_cap = static_cast<int64_t>(std::llrint(tgt_end));
    // A sub-half-sample final target rounds to a zero cap, and the synthesis
    // loop reads cap 0 as uncapped — the render would emit the full STFT
    // tail instead of a near-zero-length deliverable. A deliverable of zero
    // samples is not renderable output, so refuse it here. Like the
    // strict-ascent validators above, this is a breach tripwire for
    // hand-edited artifacts, unreachable from program-written input (the
    // trimmer's closing anchor rounds to at least one sample by its
    // validated geometry, and full maps carry the source's whole target
    // extent); a breach would otherwise render silently wrong bytes.
    if (audio_stft.emit_sample_cap <= 0) {
        std::cerr << "Error: render refused: final map target of "
                  << tgt_end
                  << " frames rounds to zero output samples\n";
        audio_stft.cleanup();
        return EngineResult::Failed;
    }

    // The projection refusals (implausible allocation, RIFF limits) run
    // orchestrator-side before the engine is invoked
    // (validate_render_projection in src/prepost/trimmer.h), preserving the
    // refuse-before-cost property without the engine knowing encode shapes.

    audio_stft.source_frame_positions = audio_stft.generate_source_frame_positions();

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
        audio_stft.phase_reset_placements.push_back({static_cast<int>(s)});
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

    // Pass 2: synthesis (clean render) into the caller-owned output buffer.
    // synthesize_full applies no attenuation, so a limiter-off buffer holds
    // the clean render exactly.
    const bool limited = audio_stft.limiter;
    auto t_p2_0 = profile::now();
    synthesis.process_to_buffer(audio_stft, p.output_buffer);
    auto t_p2_1 = profile::now();
    p2_ms = profile::ms(t_p2_0, t_p2_1);
    std::cout << "  (" << static_cast<long long>(profile::ms(t_p2_0, t_p2_1)) << " ms)\n";
    if (prof) {
        const size_t out_samples = p.output_buffer->size();
        const size_t out_frames = (audio_stft.channels > 0)
            ? out_samples / static_cast<size_t>(audio_stft.channels) : 0;
        std::cerr << "[profile] engine_pass name=synthesis ms="
                  << p2_ms
                  << " source_frames=" << p.source_audio_frames
                  << " target_frames=" << audio_stft.emit_sample_cap
                  << " channels=" << audio_stft.channels
                  << " output_frames=" << out_frames
                  << "\n";
    }
    // Pass boundaries check the raw flag alongside cancellation_observed: a
    // kill that lands after a pass's last internal check must still stop the
    // render here rather than letting the remaining passes run to a Success
    // return the dispatcher already abandoned.
    auto cancel_pending = [&]() {
        return audio_stft.cancellation_observed ||
               (audio_stft.cancel_flag && audio_stft.cancel_flag->load());
    };
    if (cancel_pending()) {
        std::cerr << "[Cancelled]\n";
        audio_stft.cleanup();
        return EngineResult::Cancelled;
    }

    // Pass 3: the spectral limiter, applied in place on the emitted buffer.
    // Limiter-off renders have no Pass 3. The peak stage and the encode both
    // live orchestrator-side, downstream of the engine.
    if (limited) {
        auto t_p3_0 = profile::now();
        std::vector<float>& buf = *p.output_buffer;
        const size_t limiter_input_frames = (audio_stft.channels > 0)
            ? buf.size() / static_cast<size_t>(audio_stft.channels) : 0;
        limiter.process(audio_stft, buf);          // spectral -0.3
        if (cancel_pending()) {
            std::cerr << "[Cancelled]\n";
            audio_stft.cleanup();
            return EngineResult::Cancelled;
        }
        auto t_p3_1 = profile::now();
        p3_ms = profile::ms(t_p3_0, t_p3_1);
        std::cout << "  (" << static_cast<long long>(profile::ms(t_p3_0, t_p3_1)) << " ms)\n";
        if (prof) {
            std::cerr << "[profile] engine_pass name=limiter ms="
                      << p3_ms
                      << " sample_frames=" << limiter_input_frames
                      << " channels=" << audio_stft.channels
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

    if (cancel_pending()) {
        std::cerr << "[Cancelled]\n";
        audio_stft.cleanup();
        return EngineResult::Cancelled;
    }
    std::cout << "[Success]\n";

    audio_stft.cleanup();
    return EngineResult::Success;
}
