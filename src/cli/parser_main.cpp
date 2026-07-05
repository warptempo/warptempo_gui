#include "warpmarkers_parse.h"          // WarpMarker, parse_warpmarkers_file
#include "phaseresetmarkers_parse.h"  // PhaseResetMarker, parse_phaseresetmarkers_file
#include "engine_settings.h"            // EngineSettings, read_engine_settings_from_file
#include "settings_trim.h"              // SettingsTrim, read_settings_trim
#include "frame_map_build.h"               // MapBuildInput/Result, resolve,
                                        // build_maps, phase_reset_source_frames
#include "locale_check.h"
#include "map_output.h"                 // write_frame_map /
                                        // write_tempo_map / write_reset_map
#include "engine/engine_geometry.h"     // kN, kRs (header-only constants)

#include "audio_probe.h"

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
        "usage: %s <source-audio> [--format framemap|tempomap|resetmap] [-o <output>] [--tab A|B]\n"
        "  Reads <source-stem>.warpmarkers, <source-stem>.phaseresetmarkers,\n"
        "  and <source-stem>.settings beside the source audio and writes the\n"
        "  framemap, tempomap, or resetmap. framemap/tempomap are built from the\n"
        "  warp markers; resetmap is the undisplaced source-frame phase-reset\n"
        "  list (the frame-domain companion the synthesis engine consumes).\n"
        "  resetmap must be requested via --format; it is never a settings\n"
        "  output_format. --tab selects which per-tab trim to apply (default A).\n",
        argv0);
}

}  // namespace

int main(int argc, char** argv) {
    if (!verify_c_numeric_locale("warptempo_parser")) return 1;

    std::string source_path, out_path, fmt_override;
    char tab = 'A';
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--format" && i + 1 < argc)      fmt_override = argv[++i];
        else if (a == "-o" && i + 1 < argc)       out_path     = argv[++i];
        else if (a == "--tab" && i + 1 < argc) {
            const std::string t = argv[++i];
            if (t == "A" || t == "B") tab = t[0];
            else { usage(argv[0]); return 2; }
        }
        else if (!a.empty() && a[0] != '-' && source_path.empty()) source_path = a;
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

    // --- settings (engine block); defaults if the file is absent ---
    EngineSettings es;    // scale 1.0, output_format "wav", title "" by default
    SettingsTrim   trim;  // all-false => untrimmed when .settings is absent
    if (std::filesystem::exists(set_path)) {
        auto parsed = read_engine_settings_from_file(set_path);
        if (!parsed) {
            std::fprintf(stderr,
                "warptempo_parser: engine settings rejected: %s\n",
                parsed.error().c_str());
            return 1;
        }
        es = *parsed;
        const auto tabs_result = read_settings_trim(set_path);
        if (!tabs_result) {
            std::fprintf(stderr,
                "warptempo_parser: trim settings rejected in '%s': %s\n",
                set_path.c_str(),
                tabs_result.error().c_str());
            return 1;
        }
        const SettingsTrimTabs& tabs = *tabs_result;
        trim = (tab == 'B') ? tabs.tab_b : tabs.tab_a;
    }

    // --- emit format: --format overrides the project setting. resetmap is a
    // CLI-only format (read_engine_settings_from_file only accepts wav /
    // framemap / tempomap), so it can arrive only through --format. ---
    const std::string fmt = !fmt_override.empty() ? fmt_override : es.output_format;
    if (fmt != "framemap" && fmt != "tempomap" && fmt != "resetmap") {
        std::fprintf(stderr,
            "warptempo_parser: nothing to emit for output_format '%s' "
            "(this tool writes framemap, tempomap, or resetmap; pass --format)\n",
            fmt.c_str());
        return 1;
    }

    // --- source sample rate / total frames (every format needs the rate) ---
    auto info = audio_probe(source_path);
    if (!info) {
        std::fprintf(stderr,
            "warptempo_parser: could not open source '%s'\n", source_path.c_str());
        return 1;
    }
    const long sample_rate  = info->sample_rate;
    const long total_frames = static_cast<long>(info->frames);

    // --- resetmap: undisplaced source-frame phase-reset list. Independent of
    // the warp markers and the map build — reads only the phase-reset
    // sidecar and the source sample rate. phase_reset_source_frames drops
    // disabled markers and converts time->source frame via nearbyint, the
    // same conversion the GUI and render CLIs use before target-domain
    // dispatch placement. The file is undisplaced by design:
    // phase_reset_offset_samples is an engine-input convention, applied
    // driver-side after temporal warping, not baked into the portable file.
    // An absent sidecar yields an empty (valid) resetmap. ---
    if (fmt == "resetmap") {
        std::vector<PhaseResetMarker> resets;
        if (std::filesystem::exists(pr_path)) {
            auto prp = parse_phaseresetmarkers_file(pr_path);
            if (!prp) {
                std::fprintf(stderr, "warptempo_parser: %s: %s\n",
                             pr_path.c_str(), prp.error().c_str());
                return 1;
            }
            resets = std::move(*prp);
        }
        const std::vector<int64_t> source_frames =
            phase_reset_source_frames(resets, sample_rate);

        if (out_path.empty())
            out_path = (parent / (stem + ".resetmap")).string();
        if (auto w = write_reset_map(out_path, source_frames); !w) {
            std::fprintf(stderr, "warptempo_parser: %s\n", w.error().c_str());
            return 1;
        }
        std::fprintf(stderr, "warptempo_parser: wrote %s\n", out_path.c_str());
        return 0;
    }

    // --- framemap / tempomap: built from the warp markers. A missing sidecar
    // is a startup error. Without it an absent file would flow an empty marker
    // list through build_maps to a seed-anchor-only map whose zero emit cap the
    // engine refuses at dispatch; erroring here gives the pointed missing-file
    // message instead of that indirect refusal. ---
    std::vector<WarpMarker> markers;
    if (!std::filesystem::exists(wm_path)) {
        std::fprintf(stderr,
            "warptempo_parser: missing warp markers file '%s' "
            "(the GUI creates this sidecar on source load)\n",
            wm_path.c_str());
        return 1;
    }
    {
        auto wmp = parse_warpmarkers_file(wm_path);
        if (!wmp) {
            std::fprintf(stderr, "warptempo_parser: %s: %s\n",
                         wm_path.c_str(), wmp.error().c_str());
            return 1;
        }
        markers = std::move(*wmp);
    }

    // --- trim frames from the project .settings, with the same source-aware
    // check the GUI and render CLI share. Convert with the nearbyint *
    // sample_rate the window resolver uses. ---
    const int64_t trim_begin_src = trim.has_begin
        ? static_cast<int64_t>(std::nearbyint(
              trim.begin_sec * static_cast<double>(sample_rate)))
        : 0;
    const int64_t trim_end_src = trim.has_end
        ? static_cast<int64_t>(std::nearbyint(
              trim.end_sec * static_cast<double>(sample_rate)))
        : total_frames;
    if (auto v = validate_trim_frames(trim_begin_src, trim_end_src,
                                      trim.has_begin, trim.has_end,
                                      total_frames); !v) {
        std::fprintf(stderr, "warptempo_parser: %s\n", v.error().c_str());
        return 1;
    }

    // --- resolve + build the full untrimmed map ---
    MapBuildInput in;
    in.markers        = resolve_markers_for_render(markers);
    in.scale          = es.scale;
    in.sample_rate    = sample_rate;
    in.total_frames   = total_frames;

    auto r = build_maps(in);
    if (!r) {
        std::fprintf(stderr,
            "warptempo_parser: map build failed: %s\n", r.error().c_str());
        return 1;
    }
    MapBuildResult out = std::move(*r);

    // --- trimmed artifacts derive from the same window the engine renders, so
    // they describe the trimmed deliverable byte-for-byte; untrimmed writes the
    // full maps verbatim. ---
    const bool trimmed = trim.has_begin || trim.has_end;
    TrimmedArtifactMaps artifacts;
    if (trimmed) {
        auto a = derive_trimmed_artifact_maps(out.frame_map, out.tempo_map,
                                              trim_begin_src, trim_end_src,
                                              kN, kRs, sample_rate);
        if (!a) {
            std::fprintf(stderr, "warptempo_parser: %s\n", a.error().c_str());
            return 1;
        }
        artifacts = std::move(*a);
    } else {
        artifacts = TrimmedArtifactMaps{out.frame_map, out.tempo_map};
    }

    // --- output path: -o, else the sibling convention ---
    if (out_path.empty()) {
        const std::string ext = (fmt == "framemap") ? ".warpframemap" : ".tempomap";
        out_path = (parent / (stem + ext)).string();
    }

    if (fmt == "framemap") {
        if (auto w = write_frame_map(out_path, artifacts.frame_map); !w) {
            std::fprintf(stderr, "warptempo_parser: %s\n", w.error().c_str());
            return 1;
        }
    } else {
        if (auto w = write_tempo_map(out_path, artifacts.tempo_map); !w) {
            std::fprintf(stderr, "warptempo_parser: %s\n", w.error().c_str());
            return 1;
        }
    }

    std::fprintf(stderr, "warptempo_parser: wrote %s\n", out_path.c_str());
    return 0;
}
