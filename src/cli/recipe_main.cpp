#include "warpmarkers_parse.h"   // WarpMarker, parse_warpmarkers_file
#include "engine_settings.h"     // EngineSettings, read_engine_settings_from_file
#include "timemap_core.h"        // TimemapBuildInput/Result, resolve, build_timemaps
#include "recipe_output.h"       // write_standard_frame_map / write_midi_tempomap

#include <sndfile.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s <source-audio> [--format framemap|tempomap] [-o <output>]\n"
        "  Reads <source-stem>.warpmarkers and <source-stem>.settings beside\n"
        "  the source audio and writes the framemap or tempomap recipe.\n",
        argv0);
}

// Trim is GUI view-state (settings_io.cpp parses trim_begin / trim_end into
// the active view), outside the parser library's EngineSettings. Until that
// parse is relocated to the parser the recipe CLI cannot honor trim, so it
// refuses a project whose .settings carries an active trim bound rather than
// emit an untrimmed map. Separator-agnostic: the key is the leading run up to
// the first space, tab, or '='.
bool settings_has_active_trim(const std::string& set_path) {
    std::ifstream f(set_path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        size_t k = i;
        while (k < line.size() &&
               line[k] != ' ' && line[k] != '\t' && line[k] != '=') ++k;
        const std::string key = line.substr(i, k - i);
        if (key == "trim_begin" || key == "trim_end") return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    std::string source_path, out_path, fmt_override;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--format" && i + 1 < argc)      fmt_override = argv[++i];
        else if (a == "-o" && i + 1 < argc)       out_path     = argv[++i];
        else if (!a.empty() && a[0] != '-' && source_path.empty()) source_path = a;
        else { usage(argv[0]); return 2; }
    }
    if (source_path.empty()) { usage(argv[0]); return 2; }

    std::filesystem::path src(source_path);
    std::filesystem::path parent = src.parent_path();
    if (parent.empty()) parent = std::filesystem::path(".");
    const std::string stem     = src.stem().string();
    const std::string wm_path  = (parent / (stem + ".warpmarkers")).string();
    const std::string set_path = (parent / (stem + ".settings")).string();

    // --- settings (engine block); defaults if the file is absent ---
    EngineSettings es;  // scale 1.0, output_format "wav", title "" by default
    if (std::filesystem::exists(set_path)) {
        if (settings_has_active_trim(set_path)) {
            std::fprintf(stderr,
                "warptempo_recipe: '%s' sets a trim bound; trimmed projects are "
                "not yet supported by the recipe CLI\n", set_path.c_str());
            return 1;
        }
        std::optional<EngineSettings> parsed =
            read_engine_settings_from_file(set_path);
        if (!parsed) return 1;  // reader already logged the violations
        es = *parsed;
    }

    // --- emit format: --format overrides the project setting ---
    const std::string fmt = !fmt_override.empty() ? fmt_override : es.output_format;
    if (fmt != "framemap" && fmt != "tempomap") {
        std::fprintf(stderr,
            "warptempo_recipe: nothing to emit for output_format '%s' "
            "(this tool writes framemap or tempomap; pass --format)\n",
            fmt.c_str());
        return 1;
    }

    // --- markers; empty if the file is absent ---
    std::vector<WarpMarker> markers;
    if (std::filesystem::exists(wm_path)) {
        WarpMarkersParse wmp = parse_warpmarkers_file(wm_path);
        if (!wmp.ok) {
            for (const auto& e : wmp.errors)
                std::fprintf(stderr, "warptempo_recipe: %s:%d: %s\n",
                             wm_path.c_str(), e.line_number, e.message.c_str());
            return 1;
        }
        markers = std::move(wmp.markers);
    }

    // --- source sample rate / total frames ---
    SF_INFO info{};
    info.format = 0;
    SNDFILE* sf = sf_open(source_path.c_str(), SFM_READ, &info);
    if (!sf) {
        std::fprintf(stderr,
            "warptempo_recipe: could not open source '%s'\n", source_path.c_str());
        return 1;
    }
    const long sample_rate  = info.samplerate;
    const long total_frames = static_cast<long>(info.frames);
    sf_close(sf);

    // --- resolve + build (trim forced off; refused above) ---
    TimemapBuildInput in;
    in.markers      = resolve_markers_for_render(markers);
    in.scale        = es.scale;
    in.sample_rate  = sample_rate;
    in.total_frames = total_frames;

    TimemapBuildResult out;
    if (!build_timemaps(in, out)) {
        std::fprintf(stderr, "warptempo_recipe: timemap build failed\n");
        return 1;
    }

    // --- output path: -o, else the sibling convention ---
    if (out_path.empty()) {
        const std::string ext = (fmt == "framemap") ? ".warpframemap" : ".tempomap";
        out_path = (parent / (stem + ext)).string();
    }

    const bool ok = (fmt == "framemap")
        ? write_standard_frame_map(out_path, out.standard, /*drop_zero_zero=*/false)
        : write_midi_tempomap(out_path, out.midi);
    if (!ok) return 1;

    std::fprintf(stderr, "warptempo_recipe: wrote %s\n", out_path.c_str());
    return 0;
}
