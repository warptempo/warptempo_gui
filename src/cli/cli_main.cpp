#include "warpmarkers_parse.h"          // WarpMarker, parse_warpmarkers_file
#include "phaseresetmarkers_parse.h"  // PhaseResetMarker, parse_phaseresetmarkers_file
#include "engine_settings.h"            // EngineSettings, read_engine_settings_from_file
#include "settings_trim.h"              // SettingsTrim, read_settings_trim
#include "warp_frame_map_build.h"               // build_warp_frame_map,
                                        // resolve_warp_markers_for_render
#include "phase_reset_frame_map_build.h"  // build_phase_reset_source_frames
#include "engine/engine.h"              // EngineParams, run_warptempo_engine
#include "engine/engine_geometry.h"     // kN, kRs
#include "locale_check.h"
#include "render_assembly.h"            // render parameter assembly helpers
#include "render_output_naming.h"       // render_output_directory,
                                        // render_output_stem,
                                        // compose_render_output_paths
#include "source_audio_io.h"            // load_source_range_to_buffer

#include "audio_probe.h"

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
        "usage: %s <source-audio>\n"
        "  Reads <source-stem>.warpmarkers, <source-stem>.phaseresetmarkers,\n"
        "  and <source-stem>.settings beside the source audio (all three are\n"
        "  required; the GUI creates them on source load) and writes the\n"
        "  warped wav the GUI would render for the same project, at the same\n"
        "  path: title-named beside the source (<title>.wav, or\n"
        "  limiter=false;<title>.wav when the settings limiter is off). Runs\n"
        "  the full PGHI engine; output_format must be wav (this CLI renders\n"
        "  wav only; the GUI produces warptempo_maps/generic_map/midi_map). The\n"
        "  trim applied is the active tab's (the persisted active_tab_view\n"
        "  key), matching the GUI.\n",
        argv0);
}

}  // namespace

int main(int argc, char** argv) {
    if (!verify_c_numeric_locale("warptempo_cli")) return 1;

    std::string source_path;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (!a.empty() && a[0] != '-' && source_path.empty()) source_path = a;
        else { usage(argv[0]); return 2; }
    }
    if (source_path.empty()) { usage(argv[0]); return 2; }

    std::filesystem::path src(source_path);
    std::filesystem::path parent = src.parent_path();
    if (parent.empty()) parent = std::filesystem::path(".");
    const std::string stem     = src.stem().string();
    const std::string wm_path  = (parent / (stem + ".warpmarkers")).string();
    const std::string pr_path  = (parent / (stem + ".phaseresetmarkers")).string();
    const std::string set_path = (parent / (stem + ".settings")).string();

    // --- settings: required, like the two marker sidecars below — the GUI
    // creates this sidecar on source load, so an absent file is a startup
    // error, not a defaults case. output_format, title, limiter, and the
    // applied trim all come from it. ---
    EngineSettings es;
    SettingsTrim   trim;
    if (!std::filesystem::exists(set_path)) {
        std::fprintf(stderr,
            "warptempo_cli: missing settings file '%s' "
            "(the GUI creates this sidecar on source load)\n",
            set_path.c_str());
        return 1;
    }
    {
        auto parsed = read_engine_settings_from_file(set_path);
        if (!parsed) {
            std::fprintf(stderr,
                "warptempo_cli: engine settings rejected: %s\n",
                parsed.error().c_str());
            return 1;
        }
        es = *parsed;
        const auto tabs_result = read_settings_trim(set_path);
        if (!tabs_result) {
            std::fprintf(stderr,
                "warptempo_cli: trim settings rejected in '%s': %s\n",
                set_path.c_str(),
                tabs_result.error().c_str());
            return 1;
        }
        // The active tab's trim, matching the GUI, which renders the trim of
        // the tab persisted in active_tab_view.
        const SettingsTrimTabs& tabs = *tabs_result;
        trim = (tabs.active_tab == 'B') ? tabs.tab_b : tabs.tab_a;
    }

    // --- engine-only: this CLI renders wav. The map formats
    // (warptempo_maps/generic_map/midi_map) are produced by the GUI (the
    // engine never runs for those). ---
    if (es.output_format != "wav") {
        std::fprintf(stderr,
            "warptempo_cli: output_format '%s' is not a wav render "
            "(this CLI renders wav only; the GUI produces "
            "warptempo_maps/generic_map/midi_map)\n",
            es.output_format.c_str());
        return 1;
    }

    // --- output path: the shared composer's single wav entry, title-named
    // beside the source with the limiter=false; prefix when the settings
    // limiter is off — exactly where the GUI writes the same project's
    // deliverable. ---
    const std::string out_path =
        compose_render_output_paths(render_output_directory(source_path),
                                    render_output_stem(es, stem),
                                    es.output_format)
            .front()
            .string();

    // --- hard refusal: never write over the source audio itself. equivalent()
    // is an inode match and only succeeds when both paths exist, so an output
    // that does not exist yet can never resolve to the source. ---
    {
        std::error_code ec;
        if (std::filesystem::exists(out_path, ec) &&
            std::filesystem::equivalent(out_path, source_path, ec)) {
            std::fprintf(stderr,
                "warptempo_cli: output '%s' resolves to the source audio "
                "file; refusing to overwrite the source. Change the title "
                "setting.\n",
                out_path.c_str());
            return 1;
        }
    }

    // --- markers; a missing sidecar is a startup error. Without it an absent
    // file would flow an empty marker list through build_warp_frame_map to a
    // seed-anchor-only map whose zero emit cap the engine refuses at dispatch;
    // erroring here gives the pointed missing-file message instead of that
    // indirect refusal. ---
    std::vector<WarpMarker> markers;
    if (!std::filesystem::exists(wm_path)) {
        std::fprintf(stderr,
            "warptempo_cli: missing warp markers file '%s' "
            "(the GUI creates this sidecar on source load)\n",
            wm_path.c_str());
        return 1;
    }
    {
        auto wmp = parse_warpmarkers_file(wm_path);
        if (!wmp) {
            std::fprintf(stderr, "warptempo_cli: %s: %s\n",
                         wm_path.c_str(), wmp.error().c_str());
            return 1;
        }
        markers = std::move(*wmp);
    }

    // --- phase reset markers; a missing sidecar is a startup error. The empty
    // file is the no-resets form and parses to an empty list; the GUI creates
    // this sidecar on source load, so an absent file is a hard error. ---
    std::vector<PhaseResetMarker> resets;
    if (!std::filesystem::exists(pr_path)) {
        std::fprintf(stderr,
            "warptempo_cli: missing phase reset markers file '%s' "
            "(the GUI creates this sidecar on source load)\n",
            pr_path.c_str());
        return 1;
    }
    {
        auto prp = parse_phaseresetmarkers_file(pr_path);
        if (!prp) {
            std::fprintf(stderr, "warptempo_cli: %s: %s\n",
                         pr_path.c_str(), prp.error().c_str());
            return 1;
        }
        resets = std::move(*prp);
    }

    // --- source sample rate / total frames ---
    auto info = audio_probe(source_path);
    if (!info) {
        std::fprintf(stderr,
            "warptempo_cli: could not open source '%s'\n", source_path.c_str());
        return 1;
    }
    const long sample_rate  = info->sample_rate;
    const long total_frames = static_cast<long>(info->frames);

    // --- reject out-of-range trim before it can silently shorten the render,
    // the same source-aware check validate_trim_frames applies for the GUI and
    // the parser CLI. Convert with the nearbyint * sample_rate the window
    // resolver uses: an explicit begin must lie strictly inside the source, an
    // explicit end at most at the source end. ---
    {
        const int64_t begin_src = trim.has_begin
            ? static_cast<int64_t>(std::nearbyint(
                  trim.begin_sec * static_cast<double>(sample_rate)))
            : 0;
        const int64_t end_src = trim.has_end
            ? static_cast<int64_t>(std::nearbyint(
                  trim.end_sec * static_cast<double>(sample_rate)))
            : total_frames;
        if (auto v = validate_trim_frames(begin_src, end_src, trim.has_begin,
                                          trim.has_end, total_frames); !v) {
            std::fprintf(stderr, "warptempo_cli: %s\n", v.error().c_str());
            return 1;
        }
    }

    // --- locked engine geometry (shared with render_pipeline.cpp and
    // engine_main.cpp via engine_geometry.h) ---
    const int     N_fft = kN;
    const int     R_s   = kRs;

    // --- full (untrimmed) frame map. The engine always renders the full map;
    // trim is applied by slicing it, never by an engine window. This is
    // do_render's full_warp_frame_map: its t_a history from frame 0 is what
    // keeps a windowed render sample-aligned with the full render. ---
    auto resolved = resolve_warp_markers_for_render(markers);
    if (!resolved) {
        std::fprintf(stderr, "warptempo_cli: %s\n", resolved.error().c_str());
        return 1;
    }
    auto r = build_warp_frame_map(*resolved,
                                  es.scale, sample_rate, total_frames);
    if (!r) {
        std::fprintf(stderr,
            "warptempo_cli: map build failed: %s\n", r.error().c_str());
        return 1;
    }
    const std::vector<WarpFrameMapSegment> full_warp_frame_map =
        std::move(*r);

    const TrimSourceWindow trim_window = resolve_trim_source_window(
        trim.has_begin, trim.begin_sec, trim.has_end, trim.end_sec,
        sample_rate, total_frames, N_fft);
    const int64_t trim_begin_src = trim_window.trim_begin_src;
    const int64_t trim_end_src = trim_window.trim_end_src;

    // --- load source; see resolve_trim_source_window for the frame-0 invariant. ---
    std::vector<float> src_samples;
    int src_sr = 0, src_ch = 0;
    {
        if (auto r = load_source_range_to_buffer(
                source_path, trim_window.load_begin_frame,
                trim_window.load_end_frame, src_samples, src_sr, src_ch); !r) {
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
    const std::string staging_output_path = out_path + ".tmp";
    ep.output_audio_path    = staging_output_path;

    // Trim is a parser-side slice of the full untrimmed map, not an engine
    // window. With a bound set, hand the engine the trimmed deliverable map,
    // which ends at its rounded boundary pair; untrimmed, the full map
    // verbatim (offset 0). Identical to do_render's wav branch. ---
    const int64_t window_offset_samples = assign_engine_warp_frame_map(
        ep, full_warp_frame_map, trim.has_begin || trim.has_end,
        trim_begin_src, trim_end_src, N_fft, R_s);
    if (window_offset_samples < 0) {
        std::fprintf(stderr,
            "warptempo_cli: trim window too short to emit any "
            "output samples\n");
        return 1;
    }

    ep.N            = N_fft;
    ep.limiter      = es.limiter;
    // limiter_ceiling_dbfs / peak_* stay at EngineParams defaults — do_render
    // sets only limiter and inherits the rest.

    auto phase_reset_source_frames_r =
        build_phase_reset_source_frames(resets, sample_rate, total_frames);
    if (!phase_reset_source_frames_r) {
        std::fprintf(stderr, "warptempo_cli: %s\n",
                     phase_reset_source_frames_r.error().c_str());
        return 1;
    }
    assign_engine_phase_reset_frame_map(ep, *phase_reset_source_frames_r);

    // --- render. The engine writes a sibling staging file and success
    // publishes it atomically via rename.
    const EngineResult er = run_warptempo_engine(ep);
    if (er != EngineResult::Success) {
        unlink_silent(staging_output_path);
        std::fprintf(stderr, "warptempo_cli: engine %s\n",
                     er == EngineResult::Cancelled ? "cancelled" : "failed");
        return 1;
    }

    {
        std::error_code ec;
        std::filesystem::rename(staging_output_path, out_path, ec);
        if (ec) {
            unlink_silent(staging_output_path);
            std::fprintf(stderr,
                "warptempo_cli: could not publish '%s' to '%s': %s\n",
                staging_output_path.c_str(), out_path.c_str(),
                ec.message().c_str());
            return 1;
        }
    }

    std::fprintf(stderr, "warptempo_cli: wrote %s\n", out_path.c_str());
    return 0;
}
