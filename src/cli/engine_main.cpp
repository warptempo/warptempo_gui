#include "engine/engine.h"   // EngineParams, run_warptempo_engine, EngineResult
#include "engine/engine_geometry.h"   // kN, kRs, phase_reset_offset_samples
#include "frame_map.h"       // FrameMapSegment, read_frame_map, read_reset_map

#include <sndfile.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s <source-audio> -o <output.wav> [--framemap <f>] "
        "[--resetmap <f>] [--no-limiter]\n"
        "  Runs the PGHI engine on a prebuilt framemap and writes the warped\n"
        "  wav. The framemap defaults to the sibling <source-stem>.warpframemap;\n"
        "  the resetmap to the sibling <source-stem>.resetmap when present\n"
        "  (undisplaced source frames). Limiter is on unless --no-limiter is\n"
        "  given. N is fixed at 4096. Trim is not an engine concern: the supplied\n"
        "  map is rendered wholesale.\n",
        argv0);
}

}  // namespace

int main(int argc, char** argv) {
    std::string source_path, out_path, framemap_path, resetmap_path;
    bool no_limiter = false;
    bool resetmap_explicit = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc)              out_path      = argv[++i];
        else if (a == "--framemap" && i + 1 < argc) framemap_path = argv[++i];
        else if (a == "--resetmap" && i + 1 < argc) { resetmap_path = argv[++i];
                                                      resetmap_explicit = true; }
        else if (a == "--no-limiter")               no_limiter = true;
        else if (!a.empty() && a[0] != '-' && source_path.empty()) source_path = a;
        else { usage(argv[0]); return 2; }
    }
    if (source_path.empty() || out_path.empty()) { usage(argv[0]); return 2; }

    std::filesystem::path src(source_path);
    std::filesystem::path parent = src.parent_path();
    if (parent.empty()) parent = std::filesystem::path(".");
    const std::string stem = src.stem().string();

    // Sibling defaults: framemap is required (default or override must exist);
    // resetmap is optional — the sibling is used only when present, and an
    // explicit --resetmap must exist (read_reset_map fails on an unopenable
    // file, caught below).
    if (framemap_path.empty())
        framemap_path = (parent / (stem + ".warpframemap")).string();
    if (resetmap_path.empty()) {
        const std::string sib = (parent / (stem + ".resetmap")).string();
        if (std::filesystem::exists(sib)) resetmap_path = sib;
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

    // --- framemap (required) ---
    std::optional<std::vector<FrameMapSegment>> fm =
        read_frame_map(framemap_path);
    if (!fm) {
        std::fprintf(stderr,
            "warptempo_engine: could not read framemap '%s' "
            "(missing, unreadable, or malformed)\n", framemap_path.c_str());
        return 1;
    }
    if (fm->empty()) {
        std::fprintf(stderr,
            "warptempo_engine: framemap '%s' is empty; nothing to render\n",
            framemap_path.c_str());
        return 1;
    }

    // --- resetmap (optional) ---
    std::vector<int64_t> reset_src;
    if (!resetmap_path.empty()) {
        std::optional<std::vector<int64_t>> rm = read_reset_map(resetmap_path);
        if (!rm) {
            std::fprintf(stderr,
                "warptempo_engine: could not read resetmap '%s' "
                "(unreadable or malformed)\n", resetmap_path.c_str());
            return 1;
        }
        reset_src = std::move(*rm);
    }
    (void)resetmap_explicit;  // explicit-missing already fails via read_reset_map

    // --- source wav, full file, interleaved float (mirrors
    // load_source_range_to_buffer for the whole-file case; bare sndfile so the
    // parser archive is not linked). ---
    std::vector<float> src_samples;
    int src_sr = 0, src_ch = 0;
    {
        SF_INFO info{};
        info.format = 0;
        SNDFILE* sf = sf_open(source_path.c_str(), SFM_READ, &info);
        if (!sf) {
            std::fprintf(stderr,
                "warptempo_engine: could not open source '%s'\n",
                source_path.c_str());
            return 1;
        }
        src_sr = info.samplerate;
        src_ch = info.channels;
        const sf_count_t frames = info.frames;
        src_samples.assign(
            static_cast<size_t>(frames) * static_cast<size_t>(src_ch), 0.0f);
        const sf_count_t got = sf_readf_float(sf, src_samples.data(), frames);
        sf_close(sf);
        if (got != frames) {
            std::fprintf(stderr,
                "warptempo_engine: short read on source '%s' (%lld/%lld)\n",
                source_path.c_str(),
                static_cast<long long>(got), static_cast<long long>(frames));
            return 1;
        }
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
    ep.output_audio_path    = out_path;

    // The supplied map is rendered wholesale — synthesis does no trim, so
    // emit_sample_cap stays 0 (full render to the map's last anchor).
    ep.frame_map = *fm;
    ep.emit_sample_cap = 0;

    ep.N            = N_fft;
    ep.limiter      = !no_limiter;   // on by default; --no-limiter clears it
    ep.limiter_diag = false;
    // limiter_ceiling_dbfs / peak_* stay at EngineParams defaults, matching
    // render_pipeline / cli_main.

    // Phase resets: the resetmap holds undisplaced source frames. Displace each
    // by the canonical offset, clamped at 0 -- the inlined equivalent of the
    // parser's displace_phase_reset_frames (not linked here). The engine then
    // resolves each to its synthesis frame by binary search over the map.
    ep.phase_reset_frames.reserve(reset_src.size());
    for (const int64_t F : reset_src) {
        if (F - phase_reset_offset_samples < 0) {
            std::fprintf(stderr,
                "warptempo_engine: phase reset at source frame %lld clamped "
                "to engine frame 0 (offset shift would place it before audio "
                "start)\n", static_cast<long long>(F));
        }
        int64_t engine_frame = F - phase_reset_offset_samples;
        if (engine_frame < 0) engine_frame = 0;
        ep.phase_reset_frames.push_back(engine_frame);
    }

    // --- render (engine writes out_path directly) ---
    std::fprintf(stderr,
        "warptempo_engine: rendering N=%d R_s=%d -> %s\n",
        N_fft, R_s, out_path.c_str());
    const EngineResult er = run_warptempo_engine(ep);
    if (er != EngineResult::Success) {
        std::fprintf(stderr, "warptempo_engine: engine %s\n",
                     er == EngineResult::Cancelled ? "cancelled" : "failed");
        return 1;
    }

    std::fprintf(stderr, "warptempo_engine: wrote %s\n", out_path.c_str());
    return 0;
}
