#include "warpmarkers_parse.h"          // WarpMarker, parse_warpmarkers_file
#include "phaseresetmarkers_parse.h"  // PhaseResetMarker, parse_phaseresetmarkers_file
#include "engine_settings.h"            // EngineSettings
#include "settings_file.h"              // SettingsFile, read_settings_file
#include "warp_frame_map_build.h"               // build_warp_frame_map,
                                        // resolve_warp_markers_for_render
#include "phase_reset_frame_map_build.h"  // build_phase_reset_source_frames
#include "marker_store_validate.h"      // first_past_eof_wall_defect,
                                        // first_view_range_defect
#include "time_format.h"                // format_timestamp
#include "engine/engine.h"              // EngineParams, run_warptempo_engine
#include "engine/engine_geometry.h"     // kN, kRs
#include "locale_check.h"
#include "trimmer.h"                    // plan_trim, finish_render,
                                        // validate_render_projection
#include "render_output_naming.h"       // render_output_directory,
                                        // render_output_stem,
                                        // compose_render_output_paths,
                                        // render_staging_path
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
    std::fprintf(stderr, "usage: %s <source-audio>\n", argv0);
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

    // --- settings: required, like the two marker sidecars below. The strict
    // whole-file reader refuses an unopenable file with its own could-not-open
    // diagnostic, which surfaces verbatim through the print below.
    // output_format, title, limiter, and the applied trim all come from it. ---
    EngineSettings es;
    SettingsFile   sf;
    SettingsTrim   trim;
    {
        // The whole-file strict schema (settings_file.h), the same reader
        // the GUI runs at source load: a sidecar set is loadable in both
        // products or neither, GUI-kind keys included (an off-preset
        // playback_speed refuses here exactly as it refuses the GUI load).
        auto parsed = read_settings_file(set_path);
        if (!parsed) {
            std::fprintf(stderr,
                "warptempo_cli: settings rejected in '%s': %s\n",
                set_path.c_str(), parsed.error().c_str());
            return 1;
        }
        sf = std::move(*parsed);
        es = sf.engine;
        // Both tabs are kept for the past-EOF guard below; the render applies
        // the active tab's trim, matching the GUI, which renders the trim of
        // the tab persisted in active_tab_view.
        const char active_tab =
            sf.has_active_tab_view ? sf.active_tab_view : 'A';
        trim = (active_tab == 'B') ? sf.tab_b.trim : sf.tab_a.trim;
    }

    // --- source-clobber guard, adversarial and load-fatal: a hand-edited
    // sidecar whose title/output_format composes a render output path onto the
    // source audio itself would overwrite the source. The GUI editor refuses
    // this at commit and the GUI loader refuses the hand-edited file at load;
    // refuse it here too, first-error and stderr-only, so a file set is
    // loadable in both products or neither. The shared predicate matches the
    // GUI's composition exactly. ---
    if (auto collision = render_output_source_collision(es, source_path)) {
        std::fprintf(stderr,
            "warptempo_cli: settings in '%s' would make the render output '%s' "
            "overwrite the source audio file\n",
            set_path.c_str(), collision->string().c_str());
        return 1;
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

    // --- markers: required. parse_warpmarkers_file refuses an unopenable file
    // with its own cannot-open-file diagnostic, which surfaces verbatim through
    // the print below. ---
    std::vector<WarpMarker> markers;
    {
        auto wmp = parse_warpmarkers_file(wm_path);
        if (!wmp) {
            std::fprintf(stderr, "warptempo_cli: %s: %s\n",
                         wm_path.c_str(), wmp.error().c_str());
            return 1;
        }
        markers = std::move(*wmp);
    }

    // --- phase reset markers: required. parse_phaseresetmarkers_file refuses
    // an unopenable file with its own cannot-open-file diagnostic, which
    // surfaces verbatim through the print below; the empty file is the
    // no-resets form and parses to an empty list. ---
    std::vector<PhaseResetMarker> resets;
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
    // Print the probe owner's diagnostic verbatim in the unified shape. The
    // CLI is insurance-only; per the non-adversarial rubric it gets the owner
    // detail with NO convert-once friendliness hint (that hint is GUI-side).
    auto info = audio_probe(source_path);
    if (!info) {
        std::fprintf(stderr,
            "warptempo_cli: source open failed for '%s': %s\n",
            source_path.c_str(), info.error().c_str());
        return 1;
    }
    const long sample_rate  = info->sample_rate;
    const long total_frames = static_cast<long>(info->frames);

    // Rates below 44.1k are out of scope by ruling, and the whole-frame gesture
    // pixel guarantees assume the 44100 floor (higher rates only widen the margins).
    if (sample_rate < 44100) {
        std::fprintf(stderr,
            "warptempo_cli: source load failed for '%s': sample rate %ld is "
            "below the 44100 floor\n",
            source_path.c_str(), sample_rate);
        return 1;
    }

    // The corpus is stereo, and mono-for-sale is delivered as locked stereo, so
    // off-corpus channel counts are refused rather than supported (convert once
    // outside, e.g. with ffmpeg). The stereo invariant also makes every
    // product-written wav payload even (see WavWriter::close), so the RIFF
    // odd-payload pad byte stays unreachable. Same compare in both binaries: a
    // file set is loadable in both products or neither.
    if (info->channels != 2) {
        std::fprintf(stderr,
            "warptempo_cli: source load failed for '%s': %d channels (stereo "
            "sources only)\n",
            source_path.c_str(), info->channels);
        return 1;
    }

    // --- adversarial past-EOF guard: refuse the load like a corrupt audio
    // file when any marker or any tab's trim sits past its wall. Such a
    // position is uncommittable through the GUI (marker EOF walls, per-bound
    // trim walls) and a sidecar applies only to the audio it was authored
    // against, so a past-EOF position means the audio was swapped outside the
    // GUI. BOTH tabs' trim is checked even though this CLI renders only the
    // active tab's trim: a file set must be loadable or not, identically in
    // both binaries — first_past_eof_wall_defect (marker_store_validate.h)
    // is the one implementation the GUI load runs too. The downstream
    // render-boundary EOF refusals stay as breach backstops for hand-edited
    // maps. ---
    {
        const int64_t total = static_cast<int64_t>(total_frames);
        if (auto wall = first_past_eof_wall_defect(
                markers, resets, sf.tab_a.trim, sf.tab_b.trim,
                total, sample_rate)) {
            std::fprintf(stderr, "warptempo_cli: %s\n", wall->c_str());
            return 1;
        }
    }

    // --- persisted view-scratch range guard, the same shared validator the
    // GUI load runs (first_view_range_defect, marker_store_validate.h) so a
    // sidecar set stays loadable in both products or neither. The CLI never
    // applies a viewport or playhead, but persisted view positions live in
    // the persisted active_audio_view's domain — while 'T' the GUI's live
    // view fields carry target-frame values, so 'S' or absent walls at the
    // source total and 'T' walls at the built warp frame map's deformed
    // target total (a slowing map legitimately persists positions past the
    // source total). The 'T' total mirrors the GUI runtime's
    // target_view_warp_frame_map_cached tgt_total_frames math
    // (warp_frame_map_view.cpp) exactly, over the same resolve-then-build
    // the render performs below — built early here for the check only, the
    // render's own build and its refusal messages stay untouched. When the
    // map cannot build (the tripwire class — the resolver normalizes marker
    // arrangements, so it never refuses) the check is SKIPPED entirely,
    // matching the GUI's skip: there is no target total to wall against, and
    // the render-boundary owners refuse then. ---
    {
        const int64_t total = static_cast<int64_t>(total_frames);
        bool    run_view_check = true;
        int64_t domain_total   = total;
        const char persisted_audio_view =
            sf.has_active_audio_view ? sf.active_audio_view : 'S';
        if (persisted_audio_view == 'T') {
            auto view_resolved =
                resolve_warp_markers_for_render(markers, sample_rate,
                                                total_frames);
            if (!view_resolved) {
                run_view_check = false;
            } else if (auto view_map = build_warp_frame_map(
                           *view_resolved, es.scale, sample_rate,
                           total_frames);
                       !view_map) {
                run_view_check = false;
            } else {
                domain_total = target_total_frames_for_map(total, *view_map);
            }
        }
        if (run_view_check) {
            if (auto detail = first_view_range_defect(
                    sf.tab_a, sf.tab_b, domain_total)) {
                std::fprintf(stderr, "warptempo_cli: %s\n", detail->c_str());
                return 1;
            }
        }
    }

    // --- locked engine geometry (shared with render_pipeline.cpp via
    // engine_geometry.h) ---
    const int     N_fft = kN;
    const int     R_s   = kRs;

    // --- full (untrimmed) frame map, do_render's full_warp_frame_map: the
    // parser knows nothing of trim; a trimmed render hands the engine the
    // prepost trimmer's translated maps derived from this one below. ---
    auto resolved = resolve_warp_markers_for_render(markers, sample_rate,
                                                    total_frames);
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
        build_phase_reset_source_frames(resets, sample_rate, total_frames);
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
    // Trimmed renders adopt the schedule plan_trim derives from the full map
    // (identity by construction, not the translated map's re-interpolation);
    // full renders pass none and the engine generates its own. trim_plan
    // outlives the engine call below (finish_render reads it too), and the
    // pointer targets a member never moved out. Identical to do_render.
    ep.source_frame_schedule = trim_plan
        ? &trim_plan->pre.source_frame_schedule
        : nullptr;
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
    const std::string staging_output_path = render_staging_path(out_path);
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
