#include "engine.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
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
void init_fftw_threads(AudioSTFT& audio_stft, int requested_threads) {
    int fftw_threads = requested_threads;
    if (fftw_threads <= 0) {
        unsigned hc = std::thread::hardware_concurrency();
        fftw_threads = static_cast<int>(std::max(1u, hc / 2));
    }
    if (fftw_init_threads()) {
        fftw_plan_with_nthreads(fftw_threads);
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

    init_fftw_threads(audio_stft, p.fftw_threads);

    auto& lp = audio_stft.limiter_params;
    lp.enabled              = (p.limiter_mode == LimiterMode::Spectral);
    lp.ceiling_dbfs         = p.limiter_ceiling_dbfs;
    lp.tolerance_db         = p.limiter_tolerance_db;
    lp.num_bands_override   = p.limiter_num_bands;
    lp.diag                 = p.limiter_diag;

    // "<buffer>" is a log-only sentinel — never pass it to filesystem APIs.
    // All such callers (synthesis.cpp's sf_open, limiter.cpp's diag path) sit
    // inside passes that are gated off on the buffer-output path.
    audio_stft.output_audio_file       =
        p.output_buffer ? std::string("<buffer>") : p.output_audio_path;
    audio_stft.limiter_mode            = p.limiter_mode;
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

    auto t_p2_0 = std::chrono::steady_clock::now();
    audio_stft.phase_reset_markers.clear();
    audio_stft.phase_reset_markers.reserve(p.phase_reset_frames.size());
    const auto& fm = audio_stft.frame_map;
    for (int64_t F : p.phase_reset_frames) {
        auto it = std::upper_bound(fm.begin(), fm.end(), F);
        if (it == fm.begin()) continue;
        --it;
        size_t s = static_cast<size_t>(it - fm.begin());
        if (s >= fm.size()) continue;
        audio_stft.phase_reset_markers.push_back(
            {static_cast<int>(s), F});
    }
    std::cout << "[Pass 1/3] Phase reset placement............. "
              << audio_stft.phase_reset_markers.size()
              << " phase resets\n";
    auto t_p2_1 = std::chrono::steady_clock::now();
    std::cout << "  (" << pass_ms(t_p2_0, t_p2_1) << " ms)\n";

    // Pass 2 (spectral limiter) is skipped on the buffer-output path. The
    // attenuation_map was already assign()'d to all-1.0 above, which is
    // the same identity row the spectral limiter would have produced for
    // a no-overshoot signal — so synthesis sees a no-op attenuation row
    // regardless of which branch we take. Pass 3 still applies the peak
    // limiter when limiter_mode == Peak (the target render sets
    // force_peak_limiter at the GUI boundary to opt in).
    if (!p.output_buffer) {
        auto t_p3_0 = std::chrono::steady_clock::now();
        limiter.process(audio_stft);
        auto t_p3_1 = std::chrono::steady_clock::now();
        std::cout << "  (" << pass_ms(t_p3_0, t_p3_1) << " ms)\n";
        if (audio_stft.cancellation_observed) {
            std::cerr << "[Cancelled] " << audio_stft.output_audio_file << "\n";
            audio_stft.cleanup();
            return EngineResult::Cancelled;
        }
    }

    auto t_p4_0 = std::chrono::steady_clock::now();
    if (p.output_buffer) {
        synthesis.process_to_buffer(audio_stft, p.output_buffer);
    } else {
        synthesis.process(audio_stft);
    }
    auto t_p4_1 = std::chrono::steady_clock::now();
    std::cout << "  (" << pass_ms(t_p4_0, t_p4_1) << " ms)\n";
    if (audio_stft.cancellation_observed) {
        std::cerr << "[Cancelled] " << audio_stft.output_audio_file << "\n";
        audio_stft.cleanup();
        return EngineResult::Cancelled;
    }

    std::cout << "[Success] " << audio_stft.output_audio_file << "\n";

    if (out_frame_map) *out_frame_map = audio_stft.frame_map;
    if (out_R_s)       *out_R_s       = audio_stft.R_s;

    audio_stft.cleanup();
    return EngineResult::Success;
}
