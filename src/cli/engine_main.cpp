#include "engine/engine.h"   // EngineParams, run_warptempo_engine, EngineResult
#include "engine/engine_geometry.h"   // kN, kRs, phase_reset_offset_samples
#include "warp_frame_map.h"           // WarpFrameMapSegment, read_warp_frame_map
#include "phase_reset_frame_map.h"    // read_phase_reset_frame_map
#include "locale_check.h"
#include "phase_reset_dispatch.h"

#include "audio_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

void unlink_silent(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s <source-audio> -o <output.wav> [--warpframemap <f>] "
        "[--phaseresetframemap <f>] [--no-limiter]\n"
        "  Runs the PGHI engine on a prebuilt warpframemap and writes the\n"
        "  warped wav. The warpframemap defaults to the sibling\n"
        "  <source-stem>.warpframemap; the phaseresetframemap to the sibling\n"
        "  <source-stem>.phaseresetframemap when present (undisplaced source\n"
        "  frames). Limiter is on unless --no-limiter is given. N is fixed at\n"
        "  4096. Trim is not an engine concern: the supplied map is rendered\n"
        "  wholesale.\n",
        argv0);
}

}  // namespace

int main(int argc, char** argv) {
    if (!verify_c_numeric_locale("warptempo_engine")) return 1;

    std::string source_path, out_path, warpframemap_path, phaseresetframemap_path;
    bool no_limiter = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc)              out_path      = argv[++i];
        else if (a == "--warpframemap" && i + 1 < argc) warpframemap_path = argv[++i];
        else if (a == "--phaseresetframemap" && i + 1 < argc) phaseresetframemap_path = argv[++i];
        else if (a == "--no-limiter")               no_limiter = true;
        else if (!a.empty() && a[0] != '-' && source_path.empty()) source_path = a;
        else { usage(argv[0]); return 2; }
    }
    if (source_path.empty() || out_path.empty()) { usage(argv[0]); return 2; }

    std::filesystem::path src(source_path);
    std::filesystem::path parent = src.parent_path();
    if (parent.empty()) parent = std::filesystem::path(".");
    const std::string stem = src.stem().string();

    // Sibling defaults: the warpframemap is required (default or override must
    // exist); the phaseresetframemap is optional — the sibling is used only
    // when present, and an explicit --phaseresetframemap must exist
    // (read_phase_reset_frame_map fails on an unopenable file, caught below).
    if (warpframemap_path.empty())
        warpframemap_path = (parent / (stem + ".warpframemap")).string();
    if (phaseresetframemap_path.empty()) {
        const std::string sib = (parent / (stem + ".phaseresetframemap")).string();
        if (std::filesystem::exists(sib)) phaseresetframemap_path = sib;
    }

    // Hard refusal: never write over the source audio itself. equivalent() is
    // an inode match and only succeeds when both paths exist.
    {
        std::error_code ec;
        if (std::filesystem::exists(out_path, ec) &&
            std::filesystem::equivalent(out_path, source_path, ec)) {
            std::fprintf(stderr,
                "warptempo_engine: output '%s' resolves to the source audio "
                "file; refusing to overwrite the source\n",
                out_path.c_str());
            return 1;
        }
    }

    // --- warpframemap (required) ---
    std::optional<std::vector<WarpFrameMapSegment>> fm =
        read_warp_frame_map(warpframemap_path);
    if (!fm) {
        std::fprintf(stderr,
            "warptempo_engine: could not read warpframemap '%s' "
            "(missing, unreadable, or malformed)\n", warpframemap_path.c_str());
        return 1;
    }
    if (fm->empty()) {
        std::fprintf(stderr,
            "warptempo_engine: warpframemap '%s' is empty; nothing to render\n",
            warpframemap_path.c_str());
        return 1;
    }

    // --- phaseresetframemap (optional) ---
    std::vector<double> reset_src;
    if (!phaseresetframemap_path.empty()) {
        std::optional<std::vector<double>> rm = read_phase_reset_frame_map(phaseresetframemap_path);
        if (!rm) {
            std::fprintf(stderr,
                "warptempo_engine: could not read phaseresetframemap '%s' "
                "(unreadable or malformed)\n", phaseresetframemap_path.c_str());
            return 1;
        }
        reset_src = std::move(*rm);
    }

    // --- source audio, full file, interleaved float. ---
    std::vector<float> src_samples;
    int src_sr = 0, src_ch = 0;
    {
        AudioFileInfo info;
        auto full = audio_read_full(source_path, &info);
        if (!full) {
            std::fprintf(stderr,
                "warptempo_engine: could not read source '%s': %s\n",
                source_path.c_str(), full.error().c_str());
            return 1;
        }
        src_samples = std::move(*full);
        src_sr = info.sample_rate;
        src_ch = info.channels;
    }

    // --- locked engine geometry ---
    const int     N_fft = kN;
    const int     R_s   = kRs;

    // --- engine params ---
    EngineParams ep;
    ep.source_audio_samples = src_samples.data();
    ep.source_audio_frames  =
        src_samples.size() / static_cast<size_t>(src_ch);
    ep.source_sample_rate   = src_sr;
    ep.source_channels      = src_ch;
    const std::string staging_output_path = out_path + ".tmp";
    ep.output_audio_path    = staging_output_path;

    // The supplied map is rendered wholesale — synthesis does no trim, so
    // emit_sample_cap stays 0 (full render to the map's last anchor).
    ep.warp_frame_map = *fm;
    ep.emit_sample_cap = 0;

    ep.N            = N_fft;
    ep.limiter      = !no_limiter;   // on by default; --no-limiter clears it
    // limiter_ceiling_dbfs / peak_* stay at EngineParams defaults, matching
    // render_pipeline / cli_main.

    const int64_t render_target_frames = fm->empty()
        ? 0
        : std::max<int64_t>(
            0, static_cast<int64_t>(std::llrint(fm->back().tgt_frame)));
    ep.phase_reset_frame_map = phase_reset_dispatch_frames_target_domain(
        reset_src,
        *fm,
        *fm,
        0,
        render_target_frames,
        phase_reset_offset_samples,
        N_fft / 2);

    // --- render: engine writes a sibling staging file and success publishes it
    // atomically via rename. ---
    std::fprintf(stderr,
        "warptempo_engine: rendering N=%d R_s=%d -> %s\n",
        N_fft, R_s, out_path.c_str());
    const EngineResult er = run_warptempo_engine(ep);
    if (er != EngineResult::Success) {
        unlink_silent(staging_output_path);
        std::fprintf(stderr, "warptempo_engine: engine %s\n",
                     er == EngineResult::Cancelled ? "cancelled" : "failed");
        return 1;
    }

    {
        std::error_code ec;
        std::filesystem::rename(staging_output_path, out_path, ec);
        if (ec) {
            unlink_silent(staging_output_path);
            std::fprintf(stderr,
                "warptempo_engine: could not publish '%s' to '%s': %s\n",
                staging_output_path.c_str(), out_path.c_str(),
                ec.message().c_str());
            return 1;
        }
    }

    std::fprintf(stderr, "warptempo_engine: wrote %s\n", out_path.c_str());
    return 0;
}
