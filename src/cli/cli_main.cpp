#include "warpmarkers_parse.h"          // WarpMarker, parse_warpmarkers_file
#include "phase_reset_markers_parse.h"  // PhaseResetMarker, parse_phaseresetmarkers_file
#include "engine_settings.h"            // EngineSettings, read_engine_settings_from_file
#include "settings_trim.h"              // SettingsTrim, read_settings_trim
#include "frame_map_build.h"               // MapBuildInput/Result, build_maps,
                                        // resolve_markers_for_render,
                                        // phase_reset_source_frames,
                                        // displace_phase_reset_frames,
                                        // slice_frame_map_to_trim_window,
                                        // load_source_range_to_buffer
#include "engine/engine.h"              // EngineParams, run_warptempo_engine
#include "engine/engine_geometry.h"     // kCanonicalN, kPhaseResetOffsetHops
#include "render_assembly.h"            // assign_engine_frame_map

#include <sndfile.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s <source-audio> -o <output.wav>\n"
        "  Reads <source-stem>.warpmarkers, <source-stem>.phaseresetmarkers,\n"
        "  and <source-stem>.settings beside the source audio and writes the\n"
        "  warped wav the GUI would render for the same project. Runs the full\n"
        "  PGHI engine; output_format must be wav (use warptempo_parser for\n"
        "  framemap/tempomap). -o is required; there is no default sibling.\n",
        argv0);
}

}  // namespace

int main(int argc, char** argv) {
    std::string source_path, out_path;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc)            out_path = argv[++i];
        else if (!a.empty() && a[0] != '-' && source_path.empty()) source_path = a;
        else { usage(argv[0]); return 2; }
    }
    if (source_path.empty() || out_path.empty()) { usage(argv[0]); return 2; }

    std::filesystem::path src(source_path);
    std::filesystem::path parent = src.parent_path();
    if (parent.empty()) parent = std::filesystem::path(".");
    const std::string stem     = src.stem().string();
    const std::string wm_path  = (parent / (stem + ".warpmarkers")).string();
    const std::string pr_path  = (parent / (stem + ".phaseresetmarkers")).string();
    const std::string set_path = (parent / (stem + ".settings")).string();

    // --- settings (engine block); defaults if the file is absent ---
    EngineSettings es;    // scale 1.0, output_format "wav", limiter true, title ""
    SettingsTrim   trim;  // all-false => untrimmed when .settings is absent
    if (std::filesystem::exists(set_path)) {
        auto parsed = read_engine_settings_from_file(set_path);
        if (!parsed) {
            std::fprintf(stderr,
                "warptempo_cli: engine settings rejected: %s\n",
                parsed.error().c_str());
            return 1;
        }
        es = *parsed;
        trim = read_settings_trim(set_path);
    }

    // --- engine-only: this CLI renders wav. framemap/tempomap are
    // warptempo_parser's job (the engine never runs for those). ---
    if (es.output_format != "wav") {
        std::fprintf(stderr,
            "warptempo_cli: output_format '%s' is not a wav render "
            "(use warptempo_parser for framemap/tempomap)\n",
            es.output_format.c_str());
        return 1;
    }

    // --- hard refusal: never write over the source audio itself. equivalent()
    // is an inode match and only succeeds when both paths exist, so an output
    // that does not exist yet can never resolve to the source. ---
    {
        std::error_code ec;
        if (std::filesystem::exists(out_path, ec) &&
            std::filesystem::equivalent(out_path, source_path, ec)) {
            std::fprintf(stderr,
                "warptempo_cli: output '%s' resolves to the source audio "
                "file; refusing to overwrite the source\n",
                out_path.c_str());
            return 1;
        }
    }

    // --- markers; empty if the file is absent ---
    std::vector<WarpMarker> markers;
    if (std::filesystem::exists(wm_path)) {
        auto wmp = parse_warpmarkers_file(wm_path);
        if (!wmp) {
            std::fprintf(stderr, "warptempo_cli: %s: %s\n",
                         wm_path.c_str(), wmp.error().c_str());
            return 1;
        }
        markers = std::move(*wmp);
    }

    // --- phase reset markers; empty if the file is absent ---
    std::vector<PhaseResetMarker> resets;
    if (std::filesystem::exists(pr_path)) {
        auto prp = parse_phaseresetmarkers_file(pr_path);
        if (!prp) {
            std::fprintf(stderr, "warptempo_cli: %s: %s\n",
                         pr_path.c_str(), prp.error().c_str());
            return 1;
        }
        resets = std::move(*prp);
    }

    // --- source sample rate / total frames ---
    SF_INFO info{};
    info.format = 0;
    SNDFILE* sf = sf_open(source_path.c_str(), SFM_READ, &info);
    if (!sf) {
        std::fprintf(stderr,
            "warptempo_cli: could not open source '%s'\n", source_path.c_str());
        return 1;
    }
    const long sample_rate  = info.samplerate;
    const long total_frames = static_cast<long>(info.frames);
    sf_close(sf);

    // --- locked engine geometry (mirrors render_pipeline.cpp) ---
    const int     N_fft = kCanonicalN;
    const int     R_s   = N_fft / 4;
    const int64_t phase_reset_offset_samples = static_cast<int64_t>(
        std::nearbyint(kPhaseResetOffsetHops * static_cast<double>(R_s)));

    // --- full (untrimmed) frame map. The engine always renders the full map;
    // trim is applied by slicing it, never by an engine window. This is
    // do_render's tmfull: its t_a history from frame 0 is what keeps a windowed
    // render sample-aligned with the full render. ---
    MapBuildInput tmin;
    tmin.markers        = resolve_markers_for_render(markers);
    tmin.scale          = es.scale;
    tmin.sample_rate    = sample_rate;
    tmin.total_frames   = total_frames;
    tmin.has_trim_begin = false;
    tmin.trim_begin_sec = 0.0;
    tmin.has_trim_end   = false;
    tmin.trim_end_sec   = 0.0;

    auto r = build_maps(tmin);
    if (!r) {
        std::fprintf(stderr,
            "warptempo_cli: map build failed: %s\n", r.error().c_str());
        return 1;
    }
    MapBuildResult tmfull = std::move(*r);

    // --- absolute source-frame trim bounds (nearbyint per the rounding rule;
    // defaults span the full extent). ---
    const int64_t trim_begin_src = trim.has_begin
        ? static_cast<int64_t>(std::nearbyint(
              trim.begin_sec * static_cast<double>(sample_rate)))
        : 0;
    const int64_t trim_end_src = trim.has_end
        ? static_cast<int64_t>(std::nearbyint(
              trim.end_sec * static_cast<double>(sample_rate)))
        : static_cast<int64_t>(total_frames);

    // --- load source from frame 0 to the end-trim point (+margin). The begin
    // MUST stay 0 (the frame map's t_a accumulation runs from frame 0). The end
    // margin (2*N) covers the final analysis window's reach past trim_end; an
    // undersized margin only zero-pads the trailing edge, never crashes. ---
    std::vector<float> src_samples;
    int src_sr = 0, src_ch = 0;
    {
        const int64_t end_margin = 2LL * static_cast<int64_t>(N_fft);
        const size_t b = 0;
        const size_t e = trim.has_end
            ? static_cast<size_t>(std::min<int64_t>(
                  total_frames, trim_end_src + end_margin))
            : static_cast<size_t>(total_frames);
        if (auto r = load_source_range_to_buffer(source_path, b, e,
                                         src_samples, src_sr, src_ch); !r) {
            std::fprintf(stderr, "warptempo_cli: %s\n", r.error().c_str());
            return 1;
        }
    }

    // --- engine params ---
    EngineParams ep;
    ep.source_audio_samples = src_samples.data();
    ep.source_audio_frames  =
        src_samples.size() / static_cast<size_t>(src_ch);
    ep.source_sample_rate   = src_sr;
    ep.source_channels      = src_ch;
    ep.output_audio_path    = out_path;

    // Trim is a parser-side slice of the full untrimmed map, not an engine
    // window. With a bound set, hand the engine the re-anchored sub-map and its
    // emit cap; untrimmed, the full map verbatim (offset 0). Identical to
    // do_render's wav branch. ---
    assign_engine_frame_map(
        ep, tmfull.frame_map, trim.has_begin || trim.has_end,
        trim_begin_src, trim_end_src, N_fft, R_s);

    ep.N            = N_fft;
    ep.limiter      = es.limiter;
    ep.limiter_diag = false;
    // limiter_ceiling_dbfs / peak_* stay at EngineParams defaults — do_render
    // sets only limiter + limiter_diag and inherits the rest.

    // Phase resets: drop disabled + time->source frame via
    // phase_reset_source_frames, then displace by one hop, clamped at 0 via
    // displace_phase_reset_frames. Absolute source-frame domain throughout —
    // the engine resolves resets by binary search over the full frame map, so
    // there is no trim re-basing. The per-reset clamp notice mirrors do_render
    // (informational; displace clamps identically with or without it). ---
    const std::vector<int64_t> reset_src_frames =
        phase_reset_source_frames(resets, sample_rate);
    for (const int64_t F : reset_src_frames) {
        if (F - phase_reset_offset_samples < 0) {
            std::fprintf(stderr,
                "warptempo_cli: phase reset at %.3f s clamped to engine "
                "frame 0 (offset shift would place it before audio start)\n",
                static_cast<double>(F) / static_cast<double>(sample_rate));
        }
    }
    ep.phase_reset_frames =
        displace_phase_reset_frames(reset_src_frames, phase_reset_offset_samples);

    // --- render. The engine writes out_path directly (no staging/rename). ---
    const EngineResult er = run_warptempo_engine(ep);
    if (er != EngineResult::Success) {
        std::fprintf(stderr, "warptempo_cli: engine %s\n",
                     er == EngineResult::Cancelled ? "cancelled" : "failed");
        return 1;
    }

    std::fprintf(stderr, "warptempo_cli: wrote %s\n", out_path.c_str());
    return 0;
}
