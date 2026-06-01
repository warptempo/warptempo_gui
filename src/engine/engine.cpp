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
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <fftw3.h>

#include "stft_container.h"
#include "limiter.h"
#include "synthesis.h"

namespace {

// libsndfile virtual-IO over a caller-owned interleaved-float buffer. Lets
// the engine read its source from memory without a wav-on-disk shim; the
// per-frame sf_seek + sf_readf_float calls in synthesize_full hit this
// VIO transparently.
struct MemSrcCtx {
    const uint8_t* data_bytes  = nullptr;
    sf_count_t     total_bytes = 0;
    sf_count_t     pos_bytes   = 0;
};

sf_count_t mem_get_filelen(void* user) {
    return static_cast<MemSrcCtx*>(user)->total_bytes;
}

sf_count_t mem_seek(sf_count_t offset, int whence, void* user) {
    auto* c = static_cast<MemSrcCtx*>(user);
    sf_count_t np;
    switch (whence) {
        case SEEK_SET: np = offset; break;
        case SEEK_CUR: np = c->pos_bytes + offset; break;
        case SEEK_END: np = c->total_bytes + offset; break;
        default:       return c->pos_bytes;
    }
    if (np < 0) np = 0;
    if (np > c->total_bytes) np = c->total_bytes;
    c->pos_bytes = np;
    return c->pos_bytes;
}

sf_count_t mem_read(void* ptr, sf_count_t count, void* user) {
    auto* c = static_cast<MemSrcCtx*>(user);
    sf_count_t avail = c->total_bytes - c->pos_bytes;
    sf_count_t n = count < avail ? count : avail;
    if (n <= 0) return 0;
    std::memcpy(ptr, c->data_bytes + c->pos_bytes, static_cast<size_t>(n));
    c->pos_bytes += n;
    return n;
}

sf_count_t mem_tell(void* user) {
    return static_cast<MemSrcCtx*>(user)->pos_bytes;
}

sf_count_t mem_write(const void*, sf_count_t, void*) {
    return 0;
}

// Shared FFTW thread init for full-render and detection-only paths. Sets
// audio_stft.fftw_threads_inited if init succeeded.
void init_fftw_threads(AudioSTFT& audio_stft) {
    // Per-transform threading intentionally off: channel-level threads
    // (synthesis.cpp) are the parallelism; FFTW internal threads on a single
    // 8192 transform are net-negative and would nest.
    if (fftw_init_threads()) {
        fftw_plan_with_nthreads(1);
        audio_stft.fftw_threads_inited = true;
    } else {
        std::cerr << "  ! fftw_init_threads failed; FFTW will run single-threaded.\n";
    }
}

// Validate strict monotonicity of a (src,tgt) timemap. Returns true if OK.
bool validate_timemap_monotonic(const std::vector<TimeMapSegment>& tm) {
    for (size_t i = 1; i < tm.size(); ++i) {
        if (tm[i].src_frame <= tm[i - 1].src_frame) {
            std::cerr << "Error: timemap entry " << i << " has non-monotonic src_frame ("
                      << tm[i - 1].src_frame << " -> "
                      << tm[i].src_frame << ").\n";
            return false;
        }
        if (tm[i].tgt_frame <= tm[i - 1].tgt_frame) {
            std::cerr << "Error: timemap entry " << i << " has non-monotonic tgt_frame ("
                      << tm[i - 1].tgt_frame << " -> "
                      << tm[i].tgt_frame << ").\n";
            return false;
        }
    }
    return true;
}

} // namespace

EngineResult run_warptempo_engine(const EngineParams& p,
                                  std::vector<int64_t>* out_frame_map,
                                  int* out_R_s,
                                  const std::atomic<bool>* cancel_flag) {
    AudioSTFT audio_stft;

    audio_stft.N = p.N;
    audio_stft.cancel_flag = cancel_flag;

    init_fftw_threads(audio_stft);

    auto& lp = audio_stft.limiter_params;
    lp.ceiling_dbfs         = p.limiter_ceiling_dbfs;
    lp.tolerance_db         = p.limiter_tolerance_db;
    lp.diag                 = p.limiter_diag;

    // "<buffer>" is a log-only sentinel — never pass it to filesystem APIs.
    // All such callers (synthesis.cpp's sf_open, limiter.cpp's diag path) sit
    // inside passes that are gated off on the buffer-output path.
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

    // Populate timemap from caller and validate monotonicity.
    audio_stft.timemap.clear();
    audio_stft.timemap.reserve(p.timemap.size());
    for (const auto& e : p.timemap) {
        audio_stft.timemap.push_back({e.first, e.second});
    }
    if (!validate_timemap_monotonic(audio_stft.timemap)) return EngineResult::Failed;

    if (p.source_audio_samples == nullptr || p.source_audio_frames == 0 ||
        p.source_channels <= 0 || p.source_sample_rate <= 0) {
        std::cerr << "Error: source buffer parameters are invalid "
                     "(samples=" << static_cast<const void*>(p.source_audio_samples)
                  << ", frames=" << p.source_audio_frames
                  << ", channels=" << p.source_channels
                  << ", sr=" << p.source_sample_rate << ")\n";
        return EngineResult::Failed;
    }

    // SF_INFO fields libsndfile needs for RAW float read. The total byte
    // length the virtual-IO get_filelen callback returns is what libsndfile
    // actually uses to size reads; the `frames` field here is informational.
    audio_stft.src_info.samplerate = p.source_sample_rate;
    audio_stft.src_info.channels   = p.source_channels;
    audio_stft.src_info.frames     = static_cast<sf_count_t>(p.source_audio_frames);
    audio_stft.src_info.format     = SF_FORMAT_RAW | SF_FORMAT_FLOAT | SF_ENDIAN_CPU;
    audio_stft.src_info.sections   = 1;
    audio_stft.src_info.seekable   = 1;

    MemSrcCtx mem_ctx;
    mem_ctx.data_bytes =
        reinterpret_cast<const uint8_t*>(p.source_audio_samples);
    mem_ctx.total_bytes = static_cast<sf_count_t>(p.source_audio_frames) *
                          static_cast<sf_count_t>(p.source_channels) *
                          static_cast<sf_count_t>(sizeof(float));

    SF_VIRTUAL_IO vio;
    vio.get_filelen = mem_get_filelen;
    vio.seek        = mem_seek;
    vio.read        = mem_read;
    vio.write       = mem_write;
    vio.tell        = mem_tell;

    audio_stft.src_snd = sf_open_virtual(&vio, SFM_READ, &audio_stft.src_info,
                                         &mem_ctx);
    if (!audio_stft.src_snd) {
        std::cerr << "Error: sf_open_virtual failed for in-memory source: "
                  << sf_strerror(nullptr) << "\n";
        return EngineResult::Failed;
    }
    audio_stft.channels = audio_stft.src_info.channels;
    audio_stft.nyquist = audio_stft.src_info.samplerate / 2.0;
    audio_stft.bin_hz_width = static_cast<double>(audio_stft.src_info.samplerate) / audio_stft.N;
    audio_stft.target_total_frames = audio_stft.timemap.back().tgt_frame + audio_stft.N;

    audio_stft.init_fftw();
    audio_stft.frame_map = audio_stft.generate_frame_map();

    audio_stft.attenuation_map.assign(audio_stft.frame_map.size(),
        std::vector<double>(audio_stft.num_bands, 1.0));

    double duration_sec = static_cast<double>(audio_stft.target_total_frames) /
                          audio_stft.src_info.samplerate;
    std::cout << "[warptempo] Source: <in-memory buffer, "
              << p.source_audio_frames << " frames>"
              << ", Target: " << std::fixed << duration_sec << "s at "
              << audio_stft.src_info.samplerate << "Hz\n";

    Limiter        limiter;
    Synthesis      synthesis;

    auto pass_ms = [](std::chrono::steady_clock::time_point t0,
                      std::chrono::steady_clock::time_point t1) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    };

    // Env-gated pass-level profiling. Timers accumulate unconditionally (the
    // now() overhead is negligible against a full render); the [profile] line
    // is emitted to std::cerr only when WARPTEMPO_PROFILE is set. Runtime env
    // gating only — no compile-time macro — so it toggles without a rebuild.
    const bool prof = (std::getenv("WARPTEMPO_PROFILE") != nullptr);
    int64_t p1_ns = 0, p2_ns = 0, p3_ns = 0;
    auto ns_between = [](std::chrono::steady_clock::time_point a,
                         std::chrono::steady_clock::time_point b) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
    };

    // Pass 1: phase reset placement.
    auto t_p1_0 = std::chrono::steady_clock::now();
    audio_stft.phase_reset_markers.clear();
    audio_stft.phase_reset_markers.reserve(p.phase_reset_frames.size());
    const auto& fm = audio_stft.frame_map;
    // The src-frame -> synth-frame upper_bound filter drops entries before
    // the first frame.
    for (size_t i = 0; i < p.phase_reset_frames.size(); ++i) {
        int64_t F = p.phase_reset_frames[i];
        auto it = std::upper_bound(fm.begin(), fm.end(), F);
        if (it == fm.begin()) continue;
        --it;
        size_t s = static_cast<size_t>(it - fm.begin());
        if (s >= fm.size()) continue;
        audio_stft.phase_reset_markers.push_back({static_cast<int>(s), F});
    }
    std::cout << "[Pass 1/3] Phase reset placement............. "
              << audio_stft.phase_reset_markers.size()
              << " phase resets\n";
    auto t_p1_1 = std::chrono::steady_clock::now();
    p1_ns = ns_between(t_p1_0, t_p1_1);
    std::cout << "  (" << pass_ms(t_p1_0, t_p1_1) << " ms)\n";

    // Pass 2: synthesis (clean render, identity attenuation_map). The
    // limiter-on disk path renders into an in-memory buffer that Pass 3 limits
    // in place; every other path streams straight to its destination.
    // attenuation_map stays all-1.0 (a no-op row), so synthesize_full — and
    // therefore the limiter-off disk render — is byte-identical to the
    // pre-relocation build.
    // The limited chain runs on a buffer; the clean (None) disk path streams
    // float straight to file.
    const bool limited = audio_stft.limiter;
    std::vector<float> render_buf;
    auto t_p2_0 = std::chrono::steady_clock::now();
    if (p.output_buffer) {
        synthesis.process_to_buffer(audio_stft, p.output_buffer);
    } else if (limited) {
        synthesis.process_to_buffer(audio_stft, &render_buf);
    } else {
        synthesis.process(audio_stft);            // None -> 32-bit float, clean
    }
    auto t_p2_1 = std::chrono::steady_clock::now();
    p2_ns = ns_between(t_p2_0, t_p2_1);
    std::cout << "  (" << pass_ms(t_p2_0, t_p2_1) << " ms)\n";
    if (audio_stft.cancellation_observed) {
        std::cerr << "[Cancelled] " << audio_stft.output_audio_file << "\n";
        audio_stft.cleanup();
        return EngineResult::Cancelled;
    }

    // Pass 3: the limited chain — spectral(-0.3) then peak(0) backstop — applied
    // in place on whichever buffer Pass 2 filled (disk render or target view).
    // The None disk path has no Pass 3. The disk render is then written out.
    if (limited) {
        auto t_p3_0 = std::chrono::steady_clock::now();
        std::vector<float>& buf = p.output_buffer ? *p.output_buffer : render_buf;
        limiter.process(audio_stft, buf);          // spectral -0.3
        if (audio_stft.cancellation_observed) {
            std::cerr << "[Cancelled] " << audio_stft.output_audio_file << "\n";
            audio_stft.cleanup();
            return EngineResult::Cancelled;
        }
        apply_peak_backstop(audio_stft, buf);      // peak 0 net
        if (!p.output_buffer)
            synthesis.write_render_to_file(audio_stft, render_buf);  // plain write
        auto t_p3_1 = std::chrono::steady_clock::now();
        p3_ns = ns_between(t_p3_0, t_p3_1);
        std::cout << "  (" << pass_ms(t_p3_0, t_p3_1) << " ms)\n";
    }

    if (prof) {
        std::cerr << "[profile] passes:"
                  << " P1=" << (p1_ns / 1e6)
                  << " P2=" << (p2_ns / 1e6)
                  << " P3=" << (p3_ns / 1e6) << "\n";
    }

    std::cout << "[Success] " << audio_stft.output_audio_file << "\n";

    if (out_frame_map) *out_frame_map = audio_stft.frame_map;
    if (out_R_s)       *out_R_s       = audio_stft.R_s;

    audio_stft.cleanup();
    return EngineResult::Success;
}
