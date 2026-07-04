#include "render_pipeline.h"

#include "engine/engine.h"
#include "engine/engine_geometry.h"
#include "app_state.h"
#include "audio.h"
#include "render.h"
#include "phaseresetmarkers.h"
#include "map_output.h"
#include "settings_io.h"
#include "source_audio_io.h"
#include "frame_map_view.h"
#include "render_assembly.h"
#include "profile_util.h"
#include "render_cache.h"
#include "source_sample_cache.h"

#include "audio_probe.h"
#include "pcm24.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

// Silent-on-missing unlink wrapper.
void unlink_silent(const std::string& path) {
    if (path.empty()) return;
    ::unlink(path.c_str());
}

struct CommitCriticalSidecars {
    bool ok = true;
    std::vector<std::string> created_paths;
};

// write_frame_map and write_tempo_map moved to the parser
// (map_output.cpp) so the GUI render pipeline and the headless parser CLI
// emit byte-identical artifacts from one implementation.

// resolve_markers_for_render moved to frame_map_build.cpp (public function) so the
// target-view paint can reach it without crossing the render_pipeline
// boundary. Both callers — do_render below and the GUI paint in
// paint_handler — receive the same resolved list.

}  // namespace

std::filesystem::path compose_sibling_output_path(
    const std::string& source_audio_path,
    const EngineSettings& es) {
    const std::string ext =
        (es.output_format == "framemap")  ? ".warpframemap" :
        (es.output_format == "tempomap") ? ".tempomap" : ".wav";
    std::filesystem::path src(source_audio_path);
    std::filesystem::path dir = src.parent_path();
    if (dir.empty()) dir = std::filesystem::path(".");
    const bool clean_float_render =
        es.output_format == "wav" && !es.limiter;
    const std::string out_filename = clean_float_render
        ? ("limiter=false;" + es.title + ext)
        : (es.title + ext);
    return dir / out_filename;
}

RenderRequest build_render_request(std::string source_audio_path,
                                   std::vector<GuiWarpMarker> markers,
                                   std::vector<GuiPhaseResetMarker> phase_resets,
                                   EngineSettings engine_settings,
                                   bool has_trim_begin, double trim_begin_sec,
                                   bool has_trim_end,   double trim_end_sec,
                                   long sample_rate,
    std::string batch_folder,
    std::string batch_basename) {
    RenderRequest req;
    req.source_audio_path  = std::move(source_audio_path);
    req.markers            = std::move(markers);
    req.engine_settings    = std::move(engine_settings);
    req.phase_reset_frames = phase_reset_source_frames(
        slice_to_phaseresetmarkers(phase_resets), sample_rate);
    req.phase_resets       = std::move(phase_resets);
    req.has_trim_begin     = has_trim_begin;
    req.trim_begin_sec     = trim_begin_sec;
    req.has_trim_end       = has_trim_end;
    req.trim_end_sec       = trim_end_sec;
    req.batch_folder       = std::move(batch_folder);
    req.batch_basename     = std::move(batch_basename);
    return req;
}

RenderOutcome do_render(const RenderRequest& req,
                        const std::atomic<bool>* cancel_flag) {
    if (req.source_audio_path.empty()) return RenderOutcome::Failed;
    const bool prof = profile::enabled();
    const auto t_render_0 = profile::now();
    double source_read_ms = 0.0;
    double engine_ms = 0.0;
    int64_t profile_trim_begin_frame = 0;
    int64_t profile_trim_end_frame = 0;
    int64_t profile_trim_span_frames = 0;
    int64_t profile_target_frames = 0;
    double profile_target_seconds = 0.0;
    size_t profile_source_frames_passed = 0;
    int profile_source_channels = 0;
    int profile_source_sample_rate = 0;

    // --- Read settings (typed; the live app.engine_settings is mutated
    // through strict-validated authoring paths, so every field is in
    // range by construction here). ---
    const std::string& output_format = req.engine_settings.output_format;
    const double scale               = req.engine_settings.scale;
    const int    N_fft               = kN;
    const int    R_s                 = kRs;

    // --- Probe source audio for sample rate / total frames. ---
    auto src_info = audio_probe(req.source_audio_path);
    if (!src_info) {
        std::fprintf(stderr,
            "warptempo_gui: render error: open failed for '%s'\n",
            req.source_audio_path.c_str());
        return RenderOutcome::Failed;
    }
    const long sample_rate  = src_info->sample_rate;
    const long total_frames = static_cast<long>(src_info->frames);
    const int source_channels_probe = src_info->channels;
    profile_source_channels = source_channels_probe;
    profile_source_sample_rate = static_cast<int>(sample_rate);

    // --- Compute output path. ---
    auto ext_for_format = [&]() -> std::string {
        if (output_format == "framemap")  return ".warpframemap";
        if (output_format == "tempomap") return ".tempomap";
        return ".wav";
    };
    const bool batch_render = !req.batch_folder.empty();
    std::string final_output_path;
    if (batch_render) {
        final_output_path =
            (std::filesystem::path(req.batch_folder) /
             (req.batch_basename + ext_for_format())).string();
    } else {
        final_output_path =
            compose_sibling_output_path(req.source_audio_path,
                                        req.engine_settings).string();
    }
    // Hard refusal: never overwrite the source audio itself. Overwriting a
    // previous render with the same title is intended behavior; the source
    // is the one path that must survive every dispatch. equivalent() is an
    // inode-level match and only succeeds when both paths exist — if the
    // output path doesn't exist yet it cannot be the source.
    {
        std::error_code ec;
        if (std::filesystem::exists(final_output_path, ec) &&
            std::filesystem::equivalent(final_output_path,
                                        req.source_audio_path, ec)) {
            std::fprintf(stderr,
                "warptempo_gui: render error: output '%s' resolves to the "
                "source audio file; refusing to overwrite the source. "
                "Change the title setting.\n",
                final_output_path.c_str());
            return RenderOutcome::Failed;
        }
    }

    // Staging path used by the wav engine path's atomic rename. Text-file
    // formats write final_output_path directly.
    const std::string staging_output_path = final_output_path + ".tmp";

    auto cleanup_all = [&]() {
        unlink_silent(staging_output_path);
    };

    auto remove_created_commit_sidecars =
        [](const std::vector<std::string>& paths) {
            for (const std::string& path : paths) {
                std::error_code ec;
                std::filesystem::remove(path, ec);
                if (ec) {
                    std::fprintf(stderr,
                        "warptempo_gui: render warning: remove failed for "
                        "'%s': %s\n",
                        path.c_str(), ec.message().c_str());
                }
            }
        };

    auto remove_newly_published_wav = [&]() {
        std::error_code ec;
        std::filesystem::remove(final_output_path, ec);
        if (ec) {
            std::fprintf(stderr,
                "warptempo_gui: render warning: remove failed for '%s': "
                "%s\n",
                final_output_path.c_str(), ec.message().c_str());
        }
    };

    auto publish_commit_critical_batch_sidecars =
        [&](bool hard_fail) -> CommitCriticalSidecars {
            CommitCriticalSidecars result;
            if (!batch_render || req.output_buffer) return result;

            const std::filesystem::path bf(req.batch_folder);
            auto existed_before = [](const std::filesystem::path& path) {
                std::error_code ec;
                const bool exists = std::filesystem::exists(path, ec);
                return ec ? true : exists;
            };
            auto note_failure = [&](const std::filesystem::path& path) {
                std::fprintf(stderr,
                    hard_fail
                        ? "warptempo_gui: render error: write failed for '%s'\n"
                        : "warptempo_gui: render warning: write failed for '%s'\n",
                    path.string().c_str());
                result.ok = false;
            };
            auto note_created = [&](const std::filesystem::path& path,
                                    bool existed) {
                if (!existed) result.created_paths.push_back(path.string());
            };

            const std::filesystem::path wm_path =
                bf / (req.batch_basename + ".warpmarkers");
            bool existed = existed_before(wm_path);
            if (!GuiWarpMarkers::save(wm_path.string(), req.markers)) {
                note_failure(wm_path);
                return result;
            }
            note_created(wm_path, existed);

            const std::filesystem::path tm_path =
                bf / (req.batch_basename + ".phaseresetmarkers");
            existed = existed_before(tm_path);
            if (!GuiPhaseResetMarkers::save(tm_path.string(),
                                            req.phase_resets)) {
                note_failure(tm_path);
                return result;
            }
            note_created(tm_path, existed);

            // `.rendersettings` sidecar: the canonical engine block, the
            // render-view scratch defaults, and the optional dispatch-time
            // authoring snapshot used by Ctrl+Alt+C.
            const std::filesystem::path rs_path =
                bf / (req.batch_basename + ".rendersettings");
            existed = existed_before(rs_path);
            if (!write_rendersettings(rs_path, req.engine_settings,
                                      /*viewport_start=*/0,
                                      /*zoom_level=*/kFitFileLevel,
                                      /*playhead=*/0,
                                      req.authoring)) {
                note_failure(rs_path);
                return result;
            }
            note_created(rs_path, existed);

            return result;
        };

    auto finish_success = [&](const char* outcome) -> RenderOutcome {
        if (prof) {
            const auto t_render_1 = profile::now();
            const double render_ms = profile::ms(t_render_0, t_render_1);
            std::fprintf(stderr,
                "[profile] render_summary route=%s output_format=%s sr=%d ch=%d source_frames_passed=%zu trim_begin_frame=%lld trim_end_frame=%lld trim_span_frames=%lld target_frames=%lld target_seconds=%.3f source_read_ms=%.3f engine_ms=%.3f render_ms=%.3f marker_count=%zu phase_reset_count=%zu offset_samples=%lld output_buffer=%s limiter=%s outcome=%s\n",
                req.output_buffer ? "target" : "file", output_format.c_str(),
                profile_source_sample_rate, profile_source_channels,
                profile_source_frames_passed,
                static_cast<long long>(profile_trim_begin_frame),
                static_cast<long long>(profile_trim_end_frame),
                static_cast<long long>(profile_trim_span_frames),
                static_cast<long long>(profile_target_frames),
                profile_target_seconds, source_read_ms, engine_ms, render_ms,
                req.markers.size(), req.phase_reset_frames.size(),
                static_cast<long long>(phase_reset_offset_samples),
                req.output_buffer ? "yes" : "no",
                req.engine_settings.limiter ? "yes" : "no",
                outcome);
        }
        std::fprintf(stderr, "warptempo_gui: render complete: %s\n",
                     req.output_buffer ? "<buffer>" : final_output_path.c_str());
        return RenderOutcome::Success;
    };

    RenderFileIdentity source_identity;
    std::vector<uint8_t> fingerprint;
    if (output_format == "wav") {
        if (stat_file_identity(req.source_audio_path, source_identity)) {
            fingerprint = render_fingerprint(
                req.source_audio_path, source_identity,
                static_cast<int>(sample_rate), req.markers, req.phase_resets,
                req.engine_settings,
                req.has_trim_begin, req.trim_begin_sec,
                req.has_trim_end, req.trim_end_sec);
        }
    }
    if (!req.output_buffer && !fingerprint.empty() &&
        fingerprint_sidecar_matches(final_output_path, fingerprint)) {
        std::fprintf(stderr,
            "[warptempo_gui] render up to date (fingerprint match): %s\n",
            final_output_path.c_str());
        CommitCriticalSidecars sidecars =
            publish_commit_critical_batch_sidecars(/*hard_fail=*/true);
        if (!sidecars.ok) {
            remove_created_commit_sidecars(sidecars.created_paths);
            cleanup_all();
            return RenderOutcome::Failed;
        }
        // A fingerprint match only proves the wav and commit-critical
        // sidecars are current; it says nothing about the render-domain
        // display sidecars render-view reads for marker positions. Batch
        // renders publish those, so a missing one here means an earlier
        // publish limped through a warning-only display-sidecar failure (or
        // the file was deleted by hand) — refuse the reuse rather than
        // report up to date with stale or absent display state. One-off
        // renders never produce display sidecars, so they are exempt.
        if (batch_render) {
            const std::filesystem::path bf(req.batch_folder);
            const std::filesystem::path wmd_path =
                bf / (req.batch_basename + ".renderwarpmarkers");
            const std::filesystem::path tmd_path =
                bf / (req.batch_basename + ".renderphaseresetmarkers");
            std::error_code wmd_ec;
            std::error_code tmd_ec;
            const bool wmd_exists = std::filesystem::exists(wmd_path, wmd_ec);
            const bool tmd_exists = std::filesystem::exists(tmd_path, tmd_ec);
            if (!wmd_exists || !tmd_exists) {
                const std::filesystem::path& missing =
                    !wmd_exists ? wmd_path : tmd_path;
                std::fprintf(stderr,
                    "warptempo_gui: render reuse refused, missing display "
                    "sidecar: %s\n",
                    missing.string().c_str());
                remove_created_commit_sidecars(sidecars.created_paths);
                cleanup_all();
                return RenderOutcome::Failed;
            }
        }
        return finish_success("reused_up_to_date");
    }

    // --- Build the maps from in-memory markers. ---
    MapBuildInput tmin;
    tmin.markers        = resolve_markers_for_render(slice_to_warp_markers(req.markers));
    tmin.scale          = scale;
    tmin.sample_rate    = sample_rate;
    tmin.total_frames   = total_frames;
    tmin.has_trim_begin = req.has_trim_begin;
    tmin.trim_begin_sec = req.trim_begin_sec;
    tmin.has_trim_end   = req.has_trim_end;
    tmin.trim_end_sec   = req.trim_end_sec;

    auto r = build_maps(tmin);
    if (!r) {
        std::fprintf(stderr,
            "warptempo_gui: render error: map build failed: %s\n",
            r.error().c_str());
        return RenderOutcome::Failed;
    }
    MapBuildResult tmres = std::move(*r);

    // Full (untrimmed) frame map for the wav engine path. A trimmed wav render
    // slices this canonical untrimmed map into a re-anchored sub-map plus an
    // emit_sample_cap via assign_engine_frame_map before dispatch, rather than
    // rebuilding a local trimmed map; that inherited t_a history from frame 0 is
    // what makes the windowed render null against the full render.
    // build_maps already produces the untrimmed segment list when
    // trim is forced off, so reuse it instead of duplicating the map math.
    // tmres stays the source for the frame-map/tempo-map else-branch (which keeps
    // its trimmed frame map + -trimmed.wav sibling); tmfull feeds the engine and
    // the render-domain sidecar block. Declared here (not inside the wav
    // branch) so the sidecar block outside the branch can read it.
    MapBuildInput tmin_full = tmin;
    tmin_full.has_trim_begin = false;
    tmin_full.trim_begin_sec = 0.0;
    tmin_full.has_trim_end   = false;
    tmin_full.trim_end_sec   = 0.0;
    auto rfull = build_maps(tmin_full);
    if (!rfull) {
        std::fprintf(stderr,
            "warptempo_gui: render error: full map build failed: %s\n",
            rfull.error().c_str());
        return RenderOutcome::Failed;
    }
    MapBuildResult tmfull = std::move(*rfull);

    const TrimSourceWindow trim_window = resolve_trim_source_window(
        req.has_trim_begin, req.trim_begin_sec,
        req.has_trim_end, req.trim_end_sec,
        sample_rate, total_frames, N_fft);

    // The engine no longer windows; render-domain sidecars subtract the slice
    // origin captured in window_offset_samples. 0 when untrimmed. Reuse rungs
    // need the same value before the engine assembly path runs.
    int64_t window_offset_samples = 0;
    if ((req.has_trim_begin || req.has_trim_end) && output_format == "wav") {
        const WindowedFrameMap w = slice_frame_map_to_trim_window(
            tmfull.frame_map, trim_window.trim_begin_src,
            trim_window.trim_end_src, N_fft, R_s);
        window_offset_samples = w.window_offset_samples;
    }

    // Returns false if any attempted display-sidecar write failed (vacuously
    // true when the batch/wav conditions skip the writes). The wav publish
    // path uses that to withhold the fingerprint attestation.
    auto publish_render_domain_sidecars = [&]() -> bool {
        if (!batch_render || req.output_buffer) return true;

        bool all_ok = true;
        const std::filesystem::path bf(req.batch_folder);

        // Render-domain sidecars (.renderwarpmarkers / .renderphaseresetmarkers).
        // Render-view loads these instead of the source-domain pair so
        // visible marker positions match the rendered audio's time axis.
        // The source-domain pair above stays authoritative for
        // Ctrl+Alt+C commit and Ctrl+S authoring saves; the render-domain
        // pair is display-only and never read back into authoring memory.
        // Only wav renders produce these — frame-map/tempo-map formats skip
        // the engine. The phase-reset sidecar is always written on wav batch
        // renders, including as an empty file.
        if (output_format == "wav" && !tmres.frame_map.empty() &&
            sample_rate > 0) {
            // Walk the FULL frame map (no synthetic trim anchors); each emitted
            // render-domain time is the full-render output sample minus the
            // slice origin. When untrimmed, window_offset_samples is 0 and this
            // reduces to old behavior.
            const FrameMapRealRange real = real_segments(tmfull);
            const int64_t trim_begin =
                static_cast<int64_t>(tmres.trim_begin_frame);
            const int64_t trim_end = tmres.trimmed
                ? static_cast<int64_t>(tmres.trim_end_frame)
                : static_cast<int64_t>(total_frames);
            const double sr_d = static_cast<double>(sample_rate);
            // window_offset_samples was computed at slice time. The engine no
            // longer windows, so there is no engine-side offset to recompute.

            // After head alignment, markers sit on the aligned
            // feature: a marker at source F displays at tgt(F) - window_offset
            // with no start-trim term.

            // Markers: lockstep walk between req.markers and the real-segment
            // range of tmfull.frame_map (built trim-off, so no synthetic trim
            // anchors). Each non-disabled marker consumes the next-in-order
            // segment; the trim range gates only emission, not consumption, so
            // the lockstep stays in step with tmfull's all-segments vector.
            // s.tgt_frame is the full-render target sample; subtracting
            // window_offset_samples places it on the trimmed wav axis.
            std::set<std::string> disabled_label_defs;
            for (const auto& m : req.markers) {
                if (!m.label_def.empty() && m.disabled) {
                    disabled_label_defs.insert(m.label_def);
                }
            }
            auto is_cascade_disabled_ref = [&](const GuiWarpMarker& m) {
                return !m.disabled && !m.label_ref.empty() &&
                       disabled_label_defs.count(m.label_ref) > 0;
            };

            auto seg_it = real.begin;
            std::vector<GuiWarpMarker> warped_markers;
            warped_markers.reserve(req.markers.size());
            for (const auto& g : req.markers) {
                const bool eff_disabled =
                    g.disabled || is_cascade_disabled_ref(g);

                // resolve_markers_for_render filter.
                if (eff_disabled) continue;

                // Consume this marker's segment first (tmfull holds every
                // segment, so the lockstep advances for every non-disabled
                // marker regardless of trim).
                if (seg_it == real.end) break;
                const auto& s = *seg_it;
                ++seg_it;

                // Trim-range filter (inclusive both ends — matches the
                // post-pass at frame_map_build.cpp). Gates emission only; the segment
                // above is already consumed.
                const int64_t source_frame_abs = static_cast<int64_t>(
                    std::nearbyint(g.time_seconds * sr_d));
                if (source_frame_abs < trim_begin || source_frame_abs > trim_end) continue;

                GuiWarpMarker w = g;
                w.time_seconds  =
                    (s.tgt_frame - static_cast<double>(window_offset_samples))
                    / sr_d;
                if (w.time_seconds < 0.0) w.time_seconds = 0.0;
                warped_markers.push_back(std::move(w));
            }
            const std::string wmd_path =
                (bf / (req.batch_basename + ".renderwarpmarkers")).string();
            if (!GuiWarpMarkers::save(wmd_path, warped_markers)) {
                std::fprintf(stderr,
                    "warptempo_gui: render warning: write failed for '%s'\n",
                    wmd_path.c_str());
                all_ok = false;
            }

            std::vector<FrameMapSegment> real_map(real.begin, real.end);

            // The render-domain phase-reset sidecar is always written for wav
            // batch renders, including the empty-file form. Surviving resets
            // are forward-mapped from their clicked source frame through the
            // same map and placed at tgt(F) - window_offset, so a reset sits
            // on the same musical position in render-view as in source and
            // target views. Drop out-of-trim and disabled.
            std::vector<GuiPhaseResetMarker> warped_phase_resets;
            warped_phase_resets.reserve(req.phase_resets.size());
            for (const auto& t : req.phase_resets) {
                if (t.disabled) continue;
                const int64_t source_frame_abs = static_cast<int64_t>(
                    std::nearbyint(t.time_seconds * sr_d));
                if (source_frame_abs < trim_begin || source_frame_abs > trim_end) continue;
                const int64_t render_frame =
                    static_cast<int64_t>(std::llrint(map_source_to_target(
                        static_cast<double>(source_frame_abs), real_map))) -
                    window_offset_samples;
                GuiPhaseResetMarker w = t;
                w.time_seconds = static_cast<double>(render_frame) / sr_d;
                if (w.time_seconds < 0.0) w.time_seconds = 0.0;
                warped_phase_resets.push_back(std::move(w));
            }
            const std::string tmd_path =
                (bf / (req.batch_basename + ".renderphaseresetmarkers"))
                .string();
            if (!GuiPhaseResetMarkers::save(tmd_path, warped_phase_resets)) {
                std::fprintf(stderr,
                    "warptempo_gui: render warning: write failed for '%s'\n",
                    tmd_path.c_str());
                all_ok = false;
            }
        }
        return all_ok;
    };

    // On-disk wav publishes finish here. Ctrl+Alt+R one-off wavs are primary
    // artifacts: .fingerprint, .peaks, and display sidecars are warning-only.
    // Ctrl+Alt+E batch wavs are committable artifact sets: wav plus
    // source-domain .warpmarkers, source-domain .phaseresetmarkers
    // (including the empty-file form), and .rendersettings. Those
    // commit-critical sidecars must publish before the wav is reported as
    // successful. The .peaks cache and the render-domain display sidecars
    // (.renderwarpmarkers / .renderphaseresetmarkers) publish next, and
    // .fingerprint is written last of all: it is the attestation that the
    // full artifact set, peaks and display sidecars included, is complete,
    // so a fingerprint match on a later render implies those files exist.
    // If the peaks or display-sidecar writes fail, the render still succeeds
    // (the wav and commit-critical sidecars are intact) but the fingerprint
    // is withheld so the attestation stays truthful: a later matching render
    // finds no fingerprint and regenerates, rather than matching an
    // incomplete set. The fingerprint-match reuse path above still refuses a
    // match whose display sidecars are missing rather than silently
    // reporting up to date — that covers fingerprints published before a
    // sidecar was deleted out from under them. Process
    // death after the wav rename lands on disk but before those sidecars
    // finish can leave an orphan wav that render-view enumerates and offers;
    // Ctrl+Alt+C's validate-before-mutate path refuses that entry cleanly.
    // That residual crash window is the accepted design.
    auto finalize_published_wav = [&](const char* outcome) -> RenderOutcome {
        CommitCriticalSidecars sidecars =
            publish_commit_critical_batch_sidecars(/*hard_fail=*/true);
        if (!sidecars.ok) {
            remove_created_commit_sidecars(sidecars.created_paths);
            remove_newly_published_wav();
            cleanup_all();
            return RenderOutcome::Failed;
        }
        const bool peaks_ok = write_peaks_cache_for_wav(final_output_path);
        const bool display_ok = publish_render_domain_sidecars();
        if (!peaks_ok || !display_ok) {
            std::fprintf(stderr,
                "[warptempo_gui] fingerprint withheld for %s: %s write "
                "failed\n",
                final_output_path.c_str(),
                !peaks_ok ? "peaks cache" : "display sidecar");
        } else if (!fingerprint.empty() &&
            !write_fingerprint_sidecar(final_output_path, fingerprint)) {
            std::fprintf(stderr,
                "[warptempo_gui] fingerprint sidecar write skipped for %s\n",
                final_output_path.c_str());
        }
        cleanup_all();
        return finish_success(outcome);
    };

    // Reuse rungs, in trust order, above the engine: a project artifact
    // byte-copy, then a render-cache wav-byte publish. Both run before any
    // source-load or engine work; an empty fingerprint
    // (source stat failure) skips both exactly as it already skips the
    // up-to-date check above.
    if (!req.output_buffer && !fingerprint.empty()) {
        // Rung: project artifact candidate. A batch entry whose fixed
        // archival sibling already holds a validated artifact for this
        // exact fingerprint is published by byte copy — the highest-
        // integrity reuse there is. When final_output_path already equals
        // the candidate, this rung is the up-to-date check above and has
        // already run.
        const std::string artifact_candidate =
            compose_sibling_output_path(req.source_audio_path,
                                        req.engine_settings).string();
        if (artifact_candidate != final_output_path &&
            fingerprint_sidecar_matches(artifact_candidate, fingerprint)) {
            std::error_code ec;
            std::filesystem::copy_file(
                artifact_candidate, staging_output_path,
                std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) {
                std::filesystem::rename(staging_output_path, final_output_path, ec);
            }
            if (!ec) {
                return finalize_published_wav("reused_artifact");
            }
            std::fprintf(stderr,
                "warptempo_gui: render warning: project artifact reuse "
                "failed for '%s': %s; falling back to a full render\n",
                artifact_candidate.c_str(), ec.message().c_str());
            cleanup_all();
        }

        // Rung: render cache. A confirmed hit publishes the canonical wav
        // bytes by direct byte I/O: RAM dumps the blob, disk copies the entry
        // file after sidecar confirmation. No sample conversion occurs.
        // req.render_cache is null only defensively (the GUI always populates
        // it); a null cache simply skips this rung.
        if (req.render_cache &&
            req.render_cache->publish_wav(fingerprint, source_channels_probe,
                                          static_cast<int>(sample_rate),
                                          staging_output_path)) {
            std::error_code ec;
            std::filesystem::rename(staging_output_path, final_output_path, ec);
            if (!ec) {
                return finalize_published_wav("reused_cache");
            }
            std::fprintf(stderr,
                "warptempo_gui: render warning: render cache publish "
                "failed for '%s'; falling back to a full render\n",
                final_output_path.c_str());
            cleanup_all();
        }
    }

    std::fprintf(stderr, "warptempo_gui: rendering %s -> %s\n",
                 output_format.c_str(), final_output_path.c_str());

    if (output_format == "wav") {
        const int64_t trim_begin_src = trim_window.trim_begin_src;
        const int64_t trim_end_src = trim_window.trim_end_src;
        profile_trim_begin_frame = trim_begin_src;
        profile_trim_end_frame = trim_end_src;
        profile_trim_span_frames = trim_end_src - trim_begin_src;
        std::vector<float> src_samples;
        const float* src_sample_data = nullptr;
        size_t src_sample_frames = 0;
        int src_sr = 0;
        int src_ch = 0;
        {
            // Borrow the GUI's shared source buffer when it covers the required
            // prefix; shared ownership keeps mid-render file swaps safe.
            // Null or mismatched requests fall back to the self-contained cache read.
            const auto t_source_load_0 = profile::now();
            bool used_gui_buffer = false;
            SourceSampleCacheStatus cache_status = SourceSampleCacheStatus::Bypassed;
            bool used_cache = false;
            if (req.source_samples && source_channels_probe > 0 &&
                trim_window.load_begin_frame == 0 &&
                trim_window.load_end_frame <=
                    static_cast<size_t>(std::numeric_limits<int64_t>::max()) &&
                req.source_total_frames >=
                    static_cast<int64_t>(trim_window.load_end_frame)) {
                const uint64_t expected_samples =
                    static_cast<uint64_t>(req.source_total_frames) *
                    static_cast<uint64_t>(source_channels_probe);
                if (expected_samples == req.source_samples->size()) {
                    src_sample_data = req.source_samples->data();
                    src_sample_frames = trim_window.load_end_frame;
                    src_sr = static_cast<int>(sample_rate);
                    src_ch = source_channels_probe;
                    used_gui_buffer = true;
                    // Borrowed samples are the audio the user authored
                    // against, so they render as-is; only the claim that this
                    // render is reproducible from the source file as it now
                    // stands is dropped when the identities diverge.
                    if (!fingerprint.empty() &&
                        (!req.has_source_load_identity ||
                         req.source_load_size != source_identity.size ||
                         req.source_load_mtime != source_identity.mtime)) {
                        std::fprintf(stderr,
                            "warptempo_gui: render warning: source changed on disk since it "
                            "was loaded; rendering the loaded audio and skipping fingerprint "
                            "and cache publication for '%s'\n",
                            final_output_path.c_str());
                        fingerprint.clear();
                    }
                }
            }
            if (!used_gui_buffer) {
                auto source_read_result = load_source_range_with_source_sample_cache(
                    req.source_audio_path, *src_info,
                    trim_window.load_begin_frame, trim_window.load_end_frame,
                    src_samples, src_sr, src_ch);
                if (!source_read_result) {
                    std::fprintf(stderr, "warptempo_gui: render error: %s\n",
                                 source_read_result.error().c_str());
                    cleanup_all();
                    return RenderOutcome::Failed;
                }
                cache_status = source_read_result->cache_status;
                used_cache = source_read_result->used_cache;
                src_sample_data = src_samples.data();
                src_sample_frames = (src_ch > 0)
                    ? src_samples.size() / static_cast<size_t>(src_ch) : 0;
            }
            if (prof) {
                const auto t_source_load_1 = profile::now();
                const unsigned long long bytes =
                    static_cast<unsigned long long>(src_sample_frames) *
                    static_cast<unsigned long long>(src_ch > 0 ? src_ch : 0) *
                    static_cast<unsigned long long>(sizeof(float));
                source_read_ms = profile::ms(t_source_load_0, t_source_load_1);
                profile_source_frames_passed = src_sample_frames;
                profile_source_channels = src_ch;
                profile_source_sample_rate = src_sr;
                std::fprintf(stderr,
                    "[profile] source_read ms=%.3f source_kind=%s cache_status=%s source_frames_passed=%zu trim_span_frames=%lld approx_mb=%.1f channels=%d sample_rate=%d\n",
                    source_read_ms,
                    used_gui_buffer ? "gui_buffer" :
                        (used_cache ? "source_sample_cache" : "path"),
                    source_sample_cache_status_name(cache_status),
                    profile_source_frames_passed,
                    static_cast<long long>(profile_trim_span_frames),
                    profile::bytes_to_mb(bytes), src_ch, src_sr);
            }
        }

        // The fingerprint names the source identity statted at dispatch. The
        // samples just loaded were validated against the file as it stood at
        // read time, either directly, through the .samples cache identity, or
        // through the borrow path's load-time identity check above. If
        // the identity moved between those points, the association can no
        // longer be proven, so publish the render without one. A rename-replace
        // that lands mid-read can skip the fingerprint of a still-consistent
        // render; the cost is one re-render, in the safe direction. Reuse rungs
        // above are deliberately unguarded because they publish audio and
        // fingerprint that were created together, so their association holds
        // regardless of what the file does afterward.
        if (!fingerprint.empty()) {
            RenderFileIdentity now_identity;
            if (!stat_file_identity(req.source_audio_path, now_identity) ||
                now_identity.size != source_identity.size ||
                now_identity.mtime != source_identity.mtime) {
                std::fprintf(stderr,
                    "warptempo_gui: render warning: source identity changed "
                    "during render; skipping fingerprint and cache publication "
                    "for '%s'\n",
                    final_output_path.c_str());
                fingerprint.clear();
            }
        }

        // Global limiter toggle. When on, every path (disk trimmed/untrimmed and
        // the target-view buffer) gets the spectral(-0.3) + peak(0) chain; when
        // off, no limiter anywhere and disk output is clean 32-bit float.

        EngineParams ep;
        ep.source_audio_samples = src_sample_data;
        ep.source_audio_frames  = src_sample_frames;
        ep.source_sample_rate   = src_sr;
        ep.source_channels      = src_ch;
        // Output sink: when a caller-owned buffer was supplied, route
        // synthesis to it (no on-disk staging, no rename, no sidecars).
        // Otherwise the existing wav-on-disk path with atomic rename runs.
        if (req.output_buffer) {
            ep.output_buffer = req.output_buffer;
        } else {
            ep.output_audio_path = staging_output_path;
        }
        // Trim is a parser-side slice of the full untrimmed map, not an engine
        // window. When a bound is set, hand the engine the re-anchored
        // sub-map covering the synthesis-frame window; the engine renders it
        // wholesale and stays trim-ignorant. Untrimmed: the full map verbatim,
        // offset 0 (provably identical to the pre-slice behavior).
        window_offset_samples = assign_engine_frame_map(
            ep, tmfull.frame_map, req.has_trim_begin || req.has_trim_end,
            trim_begin_src, trim_end_src, N_fft, R_s);
        if (window_offset_samples < 0) {
            std::fprintf(stderr,
                "warptempo_gui: render error: trim window too short to "
                "emit any output samples\n");
            cleanup_all();
            return RenderOutcome::Failed;
        }
        ep.N                    = N_fft;
        ep.limiter              = req.engine_settings.limiter;
        const int64_t render_target_frames = assign_engine_phase_resets(
            ep, req.phase_reset_frames, tmfull.frame_map, window_offset_samples,
            N_fft);
        profile_target_frames = render_target_frames;
        profile_target_seconds = ep.source_sample_rate > 0
            ? static_cast<double>(profile_target_frames) /
              static_cast<double>(ep.source_sample_rate)
            : 0.0;

        auto handle_eng = [&](EngineResult r) -> RenderOutcome {
            if (r == EngineResult::Success)   return RenderOutcome::Success;
            cleanup_all();
            return (r == EngineResult::Cancelled)
                ? RenderOutcome::Cancelled
                : RenderOutcome::Failed;
        };

        const auto t_engine_0 = profile::now();
        const EngineResult er = run_warptempo_engine(ep, cancel_flag);
        if (prof) {
            const auto t_engine_1 = profile::now();
            engine_ms = profile::ms(t_engine_0, t_engine_1);
            std::fprintf(stderr,
                "[profile] stage name=engine_total ms=%.3f result=%d source_buffer_frames=%zu target_frames=%lld output_buffer=%s limiter=%s\n",
                engine_ms, static_cast<int>(er),
                ep.source_audio_frames,
                static_cast<long long>(ep.emit_sample_cap),
                req.output_buffer ? "yes" : "no",
                ep.limiter ? "yes" : "no");
        }
        if (er != EngineResult::Success) {
            if (er == EngineResult::Failed) {
                std::fprintf(stderr, "warptempo_gui: render error: engine failed\n");
            }
            return handle_eng(er);
        }
        if (req.output_buffer && ep.limiter) {
            // Target playback auditions the deliverable lattice. A fresh
            // limited master is snapped to PCM_24 in place before publication,
            // so fresh renders, cache hits, and archival-artifact loads carry
            // sample-identical target-view audio. This is one linear scan on
            // the render worker thread, negligible next to synthesis; the
            // writer thread then encodes the already-quantized buffer exactly
            // by the codec's roundtrip identity, so the cache blob decodes
            // back to these floats. Limiter-off target renders skip this
            // branch because their float wav deliverable needs no PCM_24 snap.
            for (float& sample : *req.output_buffer) {
                sample = pcm24_quantize(sample);
            }
            const int64_t inserted_frames = src_ch > 0
                ? static_cast<int64_t>(req.output_buffer->size() /
                                       static_cast<size_t>(src_ch))
                : 0;
            if (req.render_cache && !fingerprint.empty() &&
                inserted_frames > 0) {
                req.render_cache->insert_master_floats(
                    fingerprint, *req.output_buffer, src_ch, src_sr,
                    inserted_frames);
            }
        }

        // Atomic publish: staging → final. Buffer path skips this — the
        // synthesised audio already landed in *req.output_buffer.
        if (!req.output_buffer) {
            std::error_code ec;
            std::filesystem::rename(staging_output_path, final_output_path, ec);
            if (ec) {
                std::fprintf(stderr,
                    "warptempo_gui: render error: rename failed for '%s' -> "
                    "'%s': %s\n",
                    staging_output_path.c_str(), final_output_path.c_str(),
                    ec.message().c_str());
                cleanup_all();
                return RenderOutcome::Failed;
            }
            // Populate the render cache with the canonical bytes that were
            // just published. The insert races nothing: the rename above
            // already landed, the cache's writer thread copies its own job
            // data, and a concurrent lookup that misses because the write
            // hasn't landed yet simply re-renders. req.render_cache is null
            // only defensively (the GUI always populates it); skip the insert
            // then.
            if (req.render_cache && !fingerprint.empty()) {
                std::vector<char> wav_blob;
                if (!read_file_bytes(final_output_path, wav_blob)) {
                    std::fprintf(stderr,
                        "warptempo_gui: render warning: read failed for "
                        "'%s'\n",
                        final_output_path.c_str());
                } else if (profile_target_frames > 0) {
                    req.render_cache->insert(fingerprint, wav_blob, src_ch,
                                             src_sr, profile_target_frames);
                }
            }
            return finalize_published_wav("success");
        }
    } else {
        // output_format == "framemap" or "tempomap". No engine, no limiter.
        // When trim is active, emit a sibling trimmed wav so the consumer
        // adapter operates on the trimmed source range; when there's no
        // trim, the consumer can use the original source directly.
        std::filesystem::path out_dir =
            std::filesystem::path(final_output_path).parent_path();
        if (out_dir.empty()) out_dir = std::filesystem::path(".");
        if (tmres.trimmed) {
            const std::string src_stem =
                std::filesystem::path(req.source_audio_path).stem().string();
            const std::string trimmed_path =
                (out_dir / (src_stem + "-trimmed.wav")).string();
            if (auto r = write_trimmed_wav(req.source_audio_path, trimmed_path,
                                   tmres.trim_begin_frame,
                                   tmres.trim_end_frame); !r) {
                std::fprintf(stderr, "warptempo_gui: render error: %s\n",
                             r.error().c_str());
                cleanup_all();
                return RenderOutcome::Failed;
            }
        }
        auto map_write = (output_format == "framemap")
            ? write_frame_map(final_output_path, tmres.frame_map,
                                     /*drop_zero_zero=*/false)
            : write_tempo_map(final_output_path, tmres.tempo_map);
        if (!map_write) {
            std::fprintf(stderr, "warptempo_gui: render error: %s\n",
                         map_write.error().c_str());
            cleanup_all();
            return RenderOutcome::Failed;
        }
        if (prof) {
            profile_trim_begin_frame = static_cast<int64_t>(tmres.trim_begin_frame);
            profile_trim_end_frame = static_cast<int64_t>(tmres.trim_end_frame);
            profile_trim_span_frames =
                profile_trim_end_frame - profile_trim_begin_frame;
            profile_source_channels = source_channels_probe;
            profile_source_sample_rate = static_cast<int>(sample_rate);
        }
    }

    // Non-wav batch artifacts are not render-view commit candidates. Preserve
    // their existing warning-only sidecar behavior, while still writing the
    // source-domain phase-reset companion as an empty file when the list is
    // empty.
    publish_commit_critical_batch_sidecars(/*hard_fail=*/false);
    publish_render_domain_sidecars();

    cleanup_all();
    return finish_success("success");
}
