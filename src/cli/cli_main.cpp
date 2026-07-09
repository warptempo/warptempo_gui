#include "warpmarkers_parse.h"          // WarpMarker, parse_warpmarkers_file
#include "phaseresetmarkers_parse.h"  // PhaseResetMarker, parse_phaseresetmarkers_file
#include "engine_settings.h"            // EngineSettings, read_engine_settings_from_file
#include "settings_trim.h"              // SettingsTrim, read_settings_trim
#include "warp_frame_map_build.h"               // build_warp_frame_map,
                                        // resolve_warp_markers_for_render
#include "phase_reset_frame_map_build.h"  // build_phase_reset_source_frames
#include "marker_store_validate.h"      // enumerate_marker_store_defects
#include "time_format.h"                // format_timestamp
#include "engine/engine.h"              // EngineParams, run_warptempo_engine
#include "engine/engine_geometry.h"     // kN, kRs
#include "locale_check.h"
#include "trimmer.h"                    // plan_trim, finish_render,
                                        // validate_render_projection
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
#include <utility>
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
    EngineSettings   es;
    SettingsTrim     trim;
    SettingsTrimTabs trim_tabs;
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
        // Both tabs are kept for the past-EOF guard below; the render applies
        // the active tab's trim, matching the GUI, which renders the trim of
        // the tab persisted in active_tab_view.
        trim_tabs = *tabs_result;
        trim = (trim_tabs.active_tab == 'B') ? trim_tabs.tab_b
                                             : trim_tabs.tab_a;
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

    // --- adversarial past-EOF guard: refuse the load like a corrupt audio
    // file when any marker or any tab's trim sits past its wall. Such a
    // position is uncommittable through the GUI (marker EOF walls, per-bound
    // trim walls) and a sidecar applies only to the audio it was authored
    // against, so a past-EOF position means the audio was swapped outside the
    // GUI. BOTH tabs' trim is checked even though this CLI renders only the
    // active tab's trim: a file set must be loadable or not, identically in
    // both binaries. Checked in order — warp markers (wall total-1), phase
    // reset markers (wall total exactly), then tab A begin (wall total-1),
    // tab A end (wall total), tab B begin, tab B end — first offender only,
    // disabled markers included. Every comparison is an exact frame-double
    // compare against the authored value — literally the same comparison the
    // GUI gesture walls apply, with no rounding anywhere — so a legal
    // at-the-wall position always reloads clean. Embedded times in the
    // messages are display renderings (format_timestamp(frame / sr)). The
    // downstream render-boundary EOF refusals stay as breach backstops for
    // hand-edited maps. ---
    {
        const double sr_d   = static_cast<double>(sample_rate);
        const double totalf = static_cast<double>(total_frames);
        std::string detail;
        for (const auto& m : markers) {
            if (m.time_frame > totalf - 1.0) {
                detail = "warp marker past end of audio at "
                         + format_timestamp(m.time_frame / sr_d);
                break;
            }
        }
        if (detail.empty()) {
            for (const auto& m : resets) {
                if (m.time_frame > totalf) {
                    detail = "phase reset marker past end of audio at "
                             + format_timestamp(m.time_frame / sr_d);
                    break;
                }
            }
        }
        if (detail.empty()) {
            const struct { const char* name; const SettingsTrim& t; } tabs[] = {
                {"tab A", trim_tabs.tab_a}, {"tab B", trim_tabs.tab_b},
            };
            for (const auto& [name, t] : tabs) {
                if (t.has_begin && t.begin_frame > totalf - 1.0) {
                    detail = std::string(name)
                             + " trim begin past end of audio at "
                             + format_timestamp(t.begin_frame / sr_d);
                    break;
                }
                if (t.has_end && t.end_frame > totalf) {
                    detail = std::string(name)
                             + " trim end past end of audio at "
                             + format_timestamp(t.end_frame / sr_d);
                    break;
                }
            }
        }
        if (!detail.empty()) {
            std::fprintf(stderr, "warptempo_cli: %s\n", detail.c_str());
            return 1;
        }
    }

    // --- raw-store defect listing: enumerate every render-invalidating
    // authoring defect across both columns and print all of them, one stderr
    // line each, before the single-error render-boundary owners downstream
    // refuse on the first. The CLI is an insurance policy, not an editing
    // route, so the complete listing is preferred here; the downstream
    // refusals stay untouched as the backstop. ---
    {
        const std::vector<MarkerDefect> defects =
            enumerate_marker_store_defects(markers, resets, sample_rate);
        if (!defects.empty()) {
            for (const MarkerDefect& d : defects) {
                std::fprintf(stderr, "warptempo_cli: %s\n", d.message.c_str());
            }
            return 1;
        }
    }

    // --- locked engine geometry (shared with render_pipeline.cpp via
    // engine_geometry.h) ---
    const int     N_fft = kN;
    const int     R_s   = kRs;

    // --- full (untrimmed) frame map, do_render's full_warp_frame_map: the
    // parser knows nothing of trim; a trimmed render hands the engine the
    // prepost trimmer's translated maps derived from this one below. ---
    auto resolved = resolve_warp_markers_for_render(markers, sample_rate);
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

    // --- full deliverable-form phase reset derivation, built once beside
    // the full map exactly as do_render builds it. ---
    auto phase_reset_source_frames_r =
        build_phase_reset_source_frames(resets, total_frames);
    if (!phase_reset_source_frames_r) {
        std::fprintf(stderr, "warptempo_cli: %s\n",
                     phase_reset_source_frames_r.error().c_str());
        return 1;
    }
    const std::vector<double> full_phase_reset_frame_map =
        derive_phase_reset_frame_map(*phase_reset_source_frames_r,
                                     full_warp_frame_map);

    // --- trim plan. plan_trim validates the authored bounds first (the sole
    // owner of every trim refusal) and its error string surfaces verbatim. ---
    std::optional<TrimPlan> trim_plan;
    if (trim.has_begin || trim.has_end) {
        auto plan = plan_trim(full_warp_frame_map, full_phase_reset_frame_map,
                              trim.has_begin, trim.begin_frame,
                              trim.has_end, trim.end_frame,
                              static_cast<int64_t>(total_frames),
                              N_fft, R_s);
        if (!plan) {
            std::fprintf(stderr, "warptempo_cli: %s\n", plan.error().c_str());
            return 1;
        }
        trim_plan = std::move(*plan);
    }

    // --- load the full source; trimmed renders read an offset+length view
    // into it, no copy (the GUI's shared buffer supports the same view). ---
    std::vector<float> src_samples;
    int src_sr = 0, src_ch = 0;
    {
        if (auto lr = load_source_range_to_buffer(
                source_path, 0, static_cast<size_t>(total_frames),
                src_samples, src_sr, src_ch); !lr) {
            std::fprintf(stderr, "warptempo_cli: %s\n", lr.error().c_str());
            return 1;
        }
    }

    // --- engine params. The engine is buffer-out only; the shared
    // post-engine chain encodes to the staging path below. ---
    EngineParams ep;
    ep.source_audio_samples = src_samples.data();
    ep.source_audio_frames  =
        src_samples.size() / static_cast<size_t>(src_ch);
    ep.source_sample_rate   = src_sr;
    ep.source_channels      = src_ch;
    if (trim_plan) {
        ep.source_audio_samples +=
            static_cast<size_t>(trim_plan->pre.begin_frame) *
            static_cast<size_t>(src_ch);
        ep.source_audio_frames =
            static_cast<size_t>(trim_plan->pre.frames);
    }
    std::vector<float> render_buf;
    ep.output_buffer = &render_buf;

    // Trimmed renders hand the engine the trimmer's translated maps — the
    // engine renders them wholesale, ends at its map's last anchor, and
    // stays trim-ignorant. Untrimmed: the full pair verbatim. Identical to
    // do_render's wav branch.
    ep.warp_frame_map = trim_plan
        ? std::move(trim_plan->pre.warp_frame_map)
        : full_warp_frame_map;
    ep.phase_reset_frame_map = trim_plan
        ? std::move(trim_plan->pre.phase_reset_frame_map)
        : full_phase_reset_frame_map;
    ep.N            = N_fft;
    ep.limiter      = es.limiter;
    // limiter_ceiling_dbfs / tolerance stay at EngineParams defaults —
    // do_render sets only limiter and inherits the rest.

    // Projection refusal, orchestrator-side before the engine allocates
    // (refuse-before-cost), same shape as do_render's wav branch.
    const int64_t engine_output_frames = static_cast<int64_t>(
        std::llrint(ep.warp_frame_map.back().tgt_frame));
    const int64_t encoded_frames =
        trim_plan ? trim_plan->post.samples : engine_output_frames;
    if (auto v = validate_render_projection(
            engine_output_frames, encoded_frames, src_ch, ep.limiter,
            /*encode_to_disk=*/true); !v) {
        std::fprintf(stderr, "warptempo_cli: %s\n", v.error().c_str());
        return 1;
    }

    // --- render into the buffer, then run the shared post-engine chain
    // (crop when trimmed -> peak limiter when limiter=true -> encode to the
    // sibling staging file); success publishes atomically via rename.
    const std::string staging_output_path = out_path + ".tmp";
    const EngineResult er = run_warptempo_engine(ep);
    if (er != EngineResult::Success) {
        std::fprintf(stderr, "warptempo_cli: engine %s\n",
                     er == EngineResult::Cancelled ? "cancelled" : "failed");
        return 1;
    }
    if (auto fin = finish_render(render_buf, src_ch, src_sr, ep.limiter,
                                 trim_plan ? &trim_plan->post : nullptr,
                                 staging_output_path); !fin) {
        unlink_silent(staging_output_path);
        std::fprintf(stderr, "warptempo_cli: %s\n", fin.error().c_str());
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
