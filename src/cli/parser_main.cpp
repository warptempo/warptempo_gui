#include "warpmarkers_parse.h"          // WarpMarker, parse_warpmarkers_file
#include "phaseresetmarkers_parse.h"  // PhaseResetMarker, parse_phaseresetmarkers_file
#include "engine_settings.h"            // EngineSettings, read_engine_settings_from_file
#include "settings_trim.h"              // SettingsTrim, read_settings_trim
#include "warp_frame_map_build.h"               // resolve_warp_markers_for_render,
                                        // build_warp_frame_map,
                                        // derive_midi_tempo_map
#include "phase_reset_frame_map_build.h"  // build_phase_reset_source_frames,
                                        // derive_phase_reset_frame_map
#include "locale_check.h"
#include "map_output.h"                 // write_warp_frame_map /
                                        // write_midi_tempo_map / write_phase_reset_frame_map
#include "render_output_naming.h"       // render_output_directory,
                                        // render_output_stem,
                                        // compose_render_output_paths
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
        "usage: %s <source-audio>\n"
        "  Reads <source-stem>.warpmarkers, <source-stem>.phaseresetmarkers,\n"
        "  and <source-stem>.settings beside the source audio (all three are\n"
        "  required; the GUI creates them on source load) and writes the map\n"
        "  artifacts for the project's output_format beside the source with the\n"
        "  source basename — project files named like the authoring sidecars,\n"
        "  exactly where the GUI writes them for the same project.\n"
        "  warptempo_maps writes TWO files, <source-stem>.warpframemap plus\n"
        "  <source-stem>.phaseresetframemap (the engine query-domain phase-reset\n"
        "  list, anticipation and drops applied, computed against the same\n"
        "  warp frame map) — together exactly warptempo_engine's input.\n"
        "  generic_map writes <source-stem>.warpframemap alone for generic\n"
        "  external stretch consumers; midi_map writes <source-stem>.miditempomap\n"
        "  for DAW hosts. output_format=wav is refused: the engine renders\n"
        "  wav (use warptempo_cli). The trim applied is the active tab's\n"
        "  (the persisted active_tab_view key), matching the GUI.\n",
        argv0);
}

}  // namespace

int main(int argc, char** argv) {
    if (!verify_c_numeric_locale("warptempo_parser")) return 1;

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
    // error, not a defaults case. output_format, title, and the applied trim
    // all come from it. ---
    EngineSettings es;
    SettingsTrim   trim;
    if (!std::filesystem::exists(set_path)) {
        std::fprintf(stderr,
            "warptempo_parser: missing settings file '%s' "
            "(the GUI creates this sidecar on source load)\n",
            set_path.c_str());
        return 1;
    }
    {
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
        // The active tab's trim, matching the GUI, which renders the trim of
        // the tab persisted in active_tab_view.
        const SettingsTrimTabs& tabs = *tabs_result;
        trim = (tabs.active_tab == 'B') ? tabs.tab_b : tabs.tab_a;
    }

    // --- emit format: the project's output_format. The settings reader
    // validates the four-value vocabulary, so the only non-map value here is
    // wav, which has nothing for this tool to emit (the engine renders wav). ---
    const std::string fmt = es.output_format;
    if (fmt == "wav") {
        std::fprintf(stderr,
            "warptempo_parser: nothing to emit for output_format 'wav' "
            "(this tool writes warptempo_maps, generic_map, or midi_map; "
            "the engine renders wav — use warptempo_cli, or change "
            "output_format in the project .settings)\n");
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

    // --- warp markers: required for every format, including the pair's
    // phase reset column — its derivation is computed against the built map,
    // and the GUI creates this sidecar on source load. A missing sidecar is a
    // startup error. Without the check an absent file would flow an empty
    // marker list through build_warp_frame_map to a seed-anchor-only map
    // whose zero emit cap the engine refuses at render dispatch; erroring
    // here gives the pointed missing-file message instead of that indirect
    // refusal. ---
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

    // --- phase reset markers: required for every format, like the warp
    // sidecar above — the artifacts carry the phase reset column beside its
    // siblings, so every format needs the authored source-frame list for its
    // derived column. A missing sidecar is a startup error (the GUI creates
    // it on source load); the empty FILE is the no-resets form and yields an
    // empty list and an empty derived column. build_phase_reset_source_frames
    // drops disabled markers and converts time->exact double source frame.
    // Running it here for every format aligns this CLI with the GUI render
    // pipeline, which builds the same intermediate at its source probe for
    // every output format. ---
    std::vector<double> phase_reset_source_frames;
    if (!std::filesystem::exists(pr_path)) {
        std::fprintf(stderr,
            "warptempo_parser: missing phase reset markers file '%s' "
            "(the GUI creates this sidecar on source load)\n",
            pr_path.c_str());
        return 1;
    }
    {
        auto prp = parse_phaseresetmarkers_file(pr_path);
        if (!prp) {
            std::fprintf(stderr, "warptempo_parser: %s: %s\n",
                         pr_path.c_str(), prp.error().c_str());
            return 1;
        }
        phase_reset_source_frames =
            build_phase_reset_source_frames(*prp, sample_rate);
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

    // --- resolve + build the full untrimmed map, then derive the full midi
    // tempo map from it ---
    auto r = build_warp_frame_map(resolve_warp_markers_for_render(markers),
                                  es.scale, sample_rate, total_frames);
    if (!r) {
        std::fprintf(stderr,
            "warptempo_parser: map build failed: %s\n", r.error().c_str());
        return 1;
    }
    const std::vector<WarpFrameMapSegment> full_warp_frame_map =
        std::move(*r);
    const std::vector<MidiTempoMapEntry> full_midi_tempo_map =
        derive_midi_tempo_map(full_warp_frame_map, sample_rate);

    // --- trimmed artifacts derive from the same window the engine renders, so
    // they describe the trimmed deliverable byte-for-byte; untrimmed writes the
    // full maps verbatim. ---
    const bool trimmed = trim.has_begin || trim.has_end;
    TrimmedArtifactMaps artifacts;
    if (trimmed) {
        auto a = derive_trimmed_artifact_maps(full_warp_frame_map,
                                              full_midi_tempo_map,
                                              phase_reset_source_frames,
                                              trim_begin_src, trim_end_src,
                                              kN, kRs, sample_rate);
        if (!a) {
            std::fprintf(stderr, "warptempo_parser: %s\n", a.error().c_str());
            return 1;
        }
        artifacts = std::move(*a);
    } else {
        // Untrimmed: the full maps verbatim, with the phase reset column
        // filled by the same deliverable-form derivation the trimmed path
        // runs inside derive_trimmed_artifact_maps — here against the full
        // map, so both cases flow through the identical formula and the
        // member is always populated.
        artifacts = TrimmedArtifactMaps{
            full_warp_frame_map,
            derive_phase_reset_frame_map(phase_reset_source_frames,
                                         full_warp_frame_map),
            full_midi_tempo_map};
    }

    // --- output paths: the shared composer, source-stem-named beside the
    // source — exactly where the GUI writes the same project's map artifacts
    // (one entry per extension of the format; for warptempo_maps the warp
    // column first, the phase reset column second, by render_output_extensions'
    // order). The map formats are project files named by the source stem like
    // the authoring sidecars, so render_output_stem returns the source stem for
    // every format this tool emits (only wav, which this tool never emits, is
    // title-named). ---
    const std::vector<std::filesystem::path> out_paths =
        compose_render_output_paths(render_output_directory(source_path),
                                    render_output_stem(es, stem), fmt);
    const std::string out_path = out_paths.front().string();

    // --- hard refusal: never write over the source audio itself.
    // equivalent() is an inode match and only succeeds when both paths
    // exist, so an output that does not exist yet can never resolve to the
    // source. Every output path of the format is checked, so the
    // warptempo_maps pair's second file is covered by the same refusal. ---
    for (const std::filesystem::path& out : out_paths) {
        std::error_code ec;
        if (std::filesystem::exists(out, ec) &&
            std::filesystem::equivalent(out, source_path, ec)) {
            std::fprintf(stderr,
                "warptempo_parser: output '%s' resolves to the source audio "
                "file; refusing to overwrite the source.\n",
                out.string().c_str());
            return 1;
        }
    }

    // --- warptempo_maps: the pair, the warp frame map plus the phase reset
    // frame map, TWO files, together exactly warptempo_engine's input. The
    // phase reset column was derived beside its siblings when `artifacts` was
    // filled above: the engine query-domain phase-reset list, anticipation
    // and drops applied, computed against the same map shipped beside it
    // (untrimmed: the full map; trimmed: the trimmed deliverable map), so
    // the engine consumes the pair as-is. The authored-domain record of
    // reset positions remains the .phaseresetmarkers file. An empty reset
    // list yields an empty (valid) .phaseresetframemap. Direct writes, like
    // the single-file formats: this tool has never staged, so a failed
    // second write exits nonzero and the caller reruns. ---
    if (fmt == "warptempo_maps") {
        const std::string pr_out = out_paths[1].string();
        if (auto w = write_warp_frame_map(out_path,
                                          artifacts.warp_frame_map); !w) {
            std::fprintf(stderr, "warptempo_parser: %s\n", w.error().c_str());
            return 1;
        }
        if (auto w = write_phase_reset_frame_map(
                pr_out, artifacts.phase_reset_frame_map); !w) {
            std::fprintf(stderr, "warptempo_parser: %s\n", w.error().c_str());
            return 1;
        }
        std::fprintf(stderr, "warptempo_parser: wrote %s\n", out_path.c_str());
        std::fprintf(stderr, "warptempo_parser: wrote %s\n", pr_out.c_str());
        return 0;
    }

    if (fmt == "generic_map") {
        if (auto w = write_warp_frame_map(out_path, artifacts.warp_frame_map); !w) {
            std::fprintf(stderr, "warptempo_parser: %s\n", w.error().c_str());
            return 1;
        }
    } else {
        if (auto w = write_midi_tempo_map(out_path, artifacts.midi_tempo_map); !w) {
            std::fprintf(stderr, "warptempo_parser: %s\n", w.error().c_str());
            return 1;
        }
    }

    std::fprintf(stderr, "warptempo_parser: wrote %s\n", out_path.c_str());
    return 0;
}
