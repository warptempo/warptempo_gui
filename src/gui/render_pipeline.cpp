#include "render_pipeline.h"

#include "engine/engine.h"
#include "engine/engine_geometry.h"
#include "app_state.h"
#include "render.h"
#include "phaseresetmarkers.h"
#include "map_output.h"
#include "phase_reset_frame_map_build.h"
#include "render_output_naming.h"
#include "settings_io.h"
#include "warp_frame_map_view.h"
#include "trimmer.h"
#include "profile_util.h"
#include "render_cache.h"

#include "audio_probe.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
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

// write_warp_frame_map and write_midi_tempo_map live in the parser
// (map_output.cpp) so the GUI render pipeline and the headless parser CLI
// emit byte-identical artifacts from one implementation.

// resolve_warp_markers_for_render lives in warp_frame_map_build.cpp (public
// function) so the target-view paint can reach it without crossing the
// render_pipeline boundary. Both callers — do_render below and the GUI paint
// in paint_handler — receive the same resolved list.

}  // namespace

RenderRequest build_render_request(std::string source_audio_path,
                                   std::vector<GuiWarpMarker> warp_markers,
                                   std::vector<GuiPhaseResetMarker> phase_resets,
                                   EngineSettings engine_settings,
                                   bool has_trim_begin, int64_t trim_begin_frame,
                                   bool has_trim_end,   int64_t trim_end_frame,
    std::string batch_folder,
    std::string batch_basename) {
    RenderRequest req;
    req.source_audio_path  = std::move(source_audio_path);
    req.warp_markers            = std::move(warp_markers);
    req.engine_settings    = std::move(engine_settings);
    req.phase_resets       = std::move(phase_resets);
    req.has_trim_begin     = has_trim_begin;
    req.trim_begin_frame     = trim_begin_frame;
    req.has_trim_end       = has_trim_end;
    req.trim_end_frame       = trim_end_frame;
    req.batch_folder       = std::move(batch_folder);
    req.batch_basename     = std::move(batch_basename);
    return req;
}

RenderOutcome do_render(const RenderRequest& req,
                        std::shared_ptr<const std::atomic<bool>> cancel_token) {
    // Raw view for the pipeline body; the owning token itself travels only
    // into insert_master_floats, whose writer thread outlives this call.
    const std::atomic<bool>* cancel_flag = cancel_token.get();
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
            "warptempo_gui: render error: open failed for '%s': %s\n",
            req.source_audio_path.c_str(), src_info.error().c_str());
        return RenderOutcome::Failed;
    }

    const long sample_rate  = src_info->sample_rate;
    const long total_frames = static_cast<long>(src_info->frames);
    const int source_channels_probe = src_info->channels;
    profile_source_channels = source_channels_probe;
    profile_source_sample_rate = static_cast<int>(sample_rate);

    // Assembled here, at the probe, so the reset frames are always validated
    // against the probed source length (the authored positions are whole
    // int64 source frames that widen exactly into the parser's double
    // intermediate — a sidecar is authored against one audio file's frame
    // grid).
    auto phase_reset_source_frames_r =
        build_phase_reset_source_frames(
            slice_to_phase_reset_markers(req.phase_resets), total_frames);
    if (!phase_reset_source_frames_r) {
        std::fprintf(stderr, "warptempo_gui: render error: %s\n",
                     phase_reset_source_frames_r.error().c_str());
        return RenderOutcome::Failed;
    }
    const std::vector<double>& phase_reset_source_frames =
        *phase_reset_source_frames_r;

    // --- Build the full (untrimmed) frame map from in-memory markers. The
    // engine always renders its map wholesale: a trimmed wav render hands it
    // the prepost trimmer's translated maps (plan_trim below), untrimmed
    // renders the full pair verbatim; the parser knows nothing of trim.
    // Built here at the probe, beside the phase-reset assembly above: the
    // two authored-position validations are this pipeline's parallel
    // pair. Building ahead of the fingerprint reuse return below is safe:
    // a fingerprint match implies this exact marker set builds clean, so
    // validating before the reuse rungs can never refuse a reusable
    // render, and the reuse hit's extra map build is negligible against
    // the pipeline.
    auto resolved_warp_markers =
        resolve_warp_markers_for_render(slice_to_warp_markers(req.warp_markers),
                                        sample_rate);
    if (!resolved_warp_markers) {
        std::fprintf(stderr,
            "warptempo_gui: render error: %s\n",
            resolved_warp_markers.error().c_str());
        return RenderOutcome::Failed;
    }
    auto rfull = build_warp_frame_map(
        *resolved_warp_markers, scale, sample_rate, total_frames);
    if (!rfull) {
        std::fprintf(stderr,
            "warptempo_gui: render error: map build failed: %s\n",
            rfull.error().c_str());
        return RenderOutcome::Failed;
    }
    const std::vector<WarpFrameMapSegment> full_warp_frame_map =
        std::move(*rfull);

    // The full deliverable-form phase reset derivation, built once per
    // render beside the full map: the untrimmed engine input, the map-format
    // pair's phase reset column, and plan_trim's translate/filter source are
    // all this one list.
    const std::vector<double> full_phase_reset_frame_map =
        derive_phase_reset_frame_map(phase_reset_source_frames,
                                     full_warp_frame_map);

    // --- Compose the full output-path list. ---
    // One entry per extension of the format, composed co-equally from a
    // directory and a stem (render_output_naming.h). Batch renders name into
    // the batch folder with the batch basename and no clean-float prefix;
    // source-sibling renders name into the source's parent with
    // render_output_stem. The warptempo_maps pair is the two entries of one
    // list, warp column first by the extension list's order.
    const bool batch_render = !req.batch_folder.empty();
    auto compose_source_sibling_paths = [&]() {
        return compose_render_output_paths(
            render_output_directory(req.source_audio_path),
            render_output_stem(
                req.engine_settings,
                std::filesystem::path(req.source_audio_path).stem().string()),
            output_format);
    };
    const std::vector<std::filesystem::path> output_paths =
        batch_render
            ? compose_render_output_paths(req.batch_folder, req.batch_basename,
                                          output_format)
            : compose_source_sibling_paths();
    const std::string final_output_path = output_paths.front().string();
    // Hard refusal: never overwrite the source audio itself. Overwriting a
    // previous render with the same title is intended behavior; the source
    // is the one path that must survive every dispatch. equivalent() is an
    // inode-level match and only succeeds when both paths exist — if the
    // output path doesn't exist yet it cannot be the source. Every output
    // path of the format is checked, so the warptempo_maps pair's second
    // file is covered by the same refusal — and each path's
    // render_staging_path sibling is checked too: the staging name is opened
    // with a truncating write before the render completes, so an existing
    // staging file resolving to the source (a symlink or hard link, or the
    // source literally named `<final>.tmp`) would destroy it just as surely.
    // This is the render-time inode backstop, so it also covers batch-folder
    // stagings, whose finals are composed from the batch folder rather than
    // the source siblings.
    for (const std::filesystem::path& out_path : output_paths) {
        for (const std::filesystem::path& candidate :
                 {out_path,
                  std::filesystem::path(
                      render_staging_path(out_path.string()))}) {
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) &&
                std::filesystem::equivalent(candidate,
                                            req.source_audio_path, ec)) {
                std::fprintf(stderr,
                    "warptempo_gui: render error: output '%s' resolves to "
                    "the source audio file; refusing to overwrite the "
                    "source. Change the title setting.\n",
                    candidate.string().c_str());
                return RenderOutcome::Failed;
            }
        }
    }

    // Staging path (render_staging_path: final path plus ".tmp") for the
    // atomic rename used by the wav engine path, the reuse rungs, and the
    // single-file map formats (generic_map, midi_map): every publication
    // writes staging first, gates on cancel, then renames to the final name,
    // so a cancel never lands a partial file under a final name. The
    // warptempo_maps pair stages both of its files under their own
    // render_staging_path names for the same reason.
    const std::string staging_output_path =
        render_staging_path(final_output_path);

    auto cleanup_all = [&]() {
        unlink_silent(staging_output_path);
    };

    // Cancellation gate for the orchestrator-side phases. The engine
    // observes the same token internally; these checks cover everything
    // around it — the reuse rungs, the post-engine chain, the map writes,
    // and every final-name publication — so an Esc (or a superseding
    // dispatch) that lands after the engine's last internal check can never
    // publish a deliverable, fingerprint, or cache entry as Success. A
    // cancelled return unlinks the staging file; nothing has landed under a
    // final name at any gated point. The buffer route gates twice: the
    // cache prep threads the OWNING cancel_token into insert_master_floats
    // — the token is per-dispatch and never reset, so the writer thread's
    // own loads of it (the pre-launch drop and the post-encode re-check)
    // name exactly this session even after later dispatches — and a
    // final buffer-route check ahead of finish_success keeps a cancelled
    // target render from ever reporting Success with an abandoned buffer.
    auto cancel_requested = [&]() {
        return cancel_flag && cancel_flag->load();
    };
    auto cancelled_outcome = [&]() -> RenderOutcome {
        cleanup_all();
        return RenderOutcome::Cancelled;
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

            // Authored-domain saves: these are the source-domain marker
            // stores copied beside the batch render, whole-frame positions
            // as integer text — the same serialization Ctrl+S writes.
            const std::filesystem::path wm_path =
                bf / (req.batch_basename + ".warpmarkers");
            bool existed = existed_before(wm_path);
            if (!GuiWarpMarkers::save(wm_path.string(), req.warp_markers)) {
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

            // `.settings` sidecar: the SAME standard whole-file schema a
            // source carries, so the Shift+. render-commit (adopt_render_entry)
            // adopts it with plain load semantics. Written only for wav
            // renders: an entry adopts as target view (active_audio_view =
            // 'T'), and 'T' requires output_format = wav, so map-format
            // artifacts get no .settings.
            if (output_format == "wav") {
                // The commit tab (named by active_tab_view) seeds the
                // queue/dispatch-moment position that built this render
                // (req.authoring's captured view keys), on the TARGET axis.
                // This file is written ONCE here and never touched again: the
                // Shift+. commit (adopt_render_entry) reads the whole frozen
                // file back as the session, so these keys land in the committed
                // target view THROUGH the file — the commit adopts the file,
                // never a separate live latch. Those keys are captured on
                // the LIVE map's axis; a sweep cell rewrites its markers per
                // cell, giving the cell a different (possibly shorter) target
                // axis, so the values are CLAMPED into this entry's own map
                // domain before writing — the GUI never authors a
                // load-refusable value. The clamp mirrors
                // first_view_range_defect's load rule exactly: viewport start
                // must sit in [0, total-1] (a start on the total's frame shows
                // nothing) and the playhead in [0, total] (it may rest on the
                // end exactly). target_total is this entry's map domain total.
                // Zoom passes through: the live zoom is always in the persisted
                // vocabulary. Its trim comes from the recipe trim that shaped
                // this render; read_only and the rest take their ViewState
                // defaults. The other tab is all defaults, no trim.
                const int64_t target_total = target_total_frames_for_map(
                    static_cast<int64_t>(total_frames), full_warp_frame_map);
                const int64_t vp_hi =
                    target_total > 0 ? target_total - 1 : 0;
                ViewState commit_tab;
                commit_tab.viewport_start_sample = std::clamp<int64_t>(
                    req.authoring.view_viewport_start_frame, 0, vp_hi);
                commit_tab.zoom_level             =
                    req.authoring.view_zoom_level;
                commit_tab.playhead_cursor_sample = std::clamp<int64_t>(
                    req.authoring.view_playhead_frame, 0, target_total);
                commit_tab.trim.has_begin   = req.authoring.has_trim_begin;
                commit_tab.trim.begin_frame = req.authoring.trim_begin_frame;
                commit_tab.trim.has_end     = req.authoring.has_trim_end;
                commit_tab.trim.end_frame   = req.authoring.trim_end_frame;

                ViewState other_tab;

                const bool commit_is_a = req.authoring.active_tab != 'B';
                const ViewState& tab_a = commit_is_a ? commit_tab : other_tab;
                const ViewState& tab_b = commit_is_a ? other_tab : commit_tab;

                // Values 0 / kFitFileLevel / the typed live prefs all sit
                // inside the strict schema's vocabularies by construction
                // (kFitFileLevel is in the persisted zoom vocabulary,
                // playback_speed is a live preset, font_size is the live
                // clamped value), so the file strict-parses under
                // read_settings_file — same writer, same canonical key order
                // as a source save — with no validation added here.
                const std::filesystem::path st_path =
                    bf / (req.batch_basename + ".settings");
                existed = existed_before(st_path);
                if (!write_settings_file(st_path.string(),
                                         tab_a, tab_b,
                                         req.authoring.follow,
                                         /*active_audio_view=*/'T',
                                         req.authoring.active_markers_view,
                                         req.authoring.active_tab,
                                         req.authoring.playback_speed,
                                         req.authoring.font_size,
                                         // audio_player is a global/session
                                         // launch preference, not part of the
                                         // per-render recipe the Shift+. commit
                                         // (adopt_render_entry) restores. Passing
                                         // empty writes a blank `audio_player=`
                                         // line (the key is always emitted);
                                         // adopt ignores it, so the live pref
                                         // survives the commit untouched.
                                         /*audio_player=*/std::string(),
                                         req.engine_settings)) {
                    note_failure(st_path);
                    return result;
                }
                note_created(st_path, existed);
            }

            return result;
        };

    auto finish_success = [&](const char* outcome) -> RenderOutcome {
        if (prof) {
            const auto t_render_1 = profile::now();
            const double render_ms = profile::ms(t_render_0, t_render_1);
            std::fprintf(stderr,
                "[profile] render_summary route=%s output_format=%s sr=%d ch=%d source_frames_passed=%zu trim_begin_frame=%lld trim_end_frame=%lld trim_span_frames=%lld target_frames=%lld target_seconds=%.3f source_read_ms=%.3f engine_ms=%.3f render_ms=%.3f marker_count=%zu phase_reset_count=%zu output_buffer=%s limiter=%s outcome=%s\n",
                req.output_buffer ? "target" : "file", output_format.c_str(),
                profile_source_sample_rate, profile_source_channels,
                profile_source_frames_passed,
                static_cast<long long>(profile_trim_begin_frame),
                static_cast<long long>(profile_trim_end_frame),
                static_cast<long long>(profile_trim_span_frames),
                static_cast<long long>(profile_target_frames),
                profile_target_seconds, source_read_ms, engine_ms, render_ms,
                req.warp_markers.size(), phase_reset_source_frames.size(),
                req.output_buffer ? "yes" : "no",
                req.engine_settings.limiter ? "yes" : "no",
                outcome);
        }
        std::fprintf(stderr, "warptempo_gui: render complete: %s\n",
                     req.output_buffer ? "<buffer>" : final_output_path.c_str());
        return RenderOutcome::Success;
    };

    // The fingerprint's source identity is built directly from the request's
    // load-time-captured identity (req.source_load_size/mtime): the loaded
    // source is immutable for the process lifetime, so no on-disk re-stat is
    // needed. fingerprint is non-empty exactly when output_format is wav; the
    // map formats compute none.
    RenderFileIdentity source_identity;
    std::vector<uint8_t> fingerprint;
    if (output_format == "wav") {
        source_identity.size = req.source_load_size;
        source_identity.mtime = req.source_load_mtime;
        fingerprint = render_fingerprint(
            req.source_audio_path, source_identity,
            static_cast<int>(sample_rate), req.warp_markers, req.phase_resets,
            req.engine_settings,
            req.has_trim_begin, req.trim_begin_frame,
            req.has_trim_end, req.trim_end_frame);
    }
    if (cancel_requested()) return cancelled_outcome();
    // Fingerprint emptiness doubles as a map-format gate on this shared
    // pre-branch path: non-wav formats compute no fingerprint.
    if (!req.output_buffer && !fingerprint.empty() &&
        fingerprint_sidecar_matches(final_output_path, fingerprint)) {
        std::fprintf(stderr,
            "warptempo_gui: render up to date (fingerprint match): %s\n",
            final_output_path.c_str());
        CommitCriticalSidecars sidecars =
            publish_commit_critical_batch_sidecars(/*hard_fail=*/true);
        if (!sidecars.ok) {
            remove_created_commit_sidecars(sidecars.created_paths);
            cleanup_all();
            return RenderOutcome::Failed;
        }
        // The fingerprint plus the commit-critical sidecars just
        // (re)published above are the whole reuse condition: the Shift+.
        // commit derives everything it adopts from the snapshot set, so there
        // is nothing else the entry needs on disk.
        return finish_success("reused_up_to_date");
    }

    // Trim plan (wav only; the map formats refuse a trim in their arm
    // below). plan_trim validates the authored bounds first — the sole owner
    // of every trim refusal — then derives the source cut, the translated
    // maps, and the output crop in one computation. The GUI dispatch
    // preflight already ran the same validation with its modal surface (the
    // defect series for the live store, the popup for snapshots and the
    // non-modeled class), so this stderr refusal is the async backstop.
    const bool trimmed = req.has_trim_begin || req.has_trim_end;
    std::optional<TrimPlan> trim_plan;
    if (trimmed && output_format == "wav") {
        auto plan = plan_trim(full_warp_frame_map, full_phase_reset_frame_map,
                              req.has_trim_begin, req.trim_begin_frame,
                              req.has_trim_end, req.trim_end_frame,
                              static_cast<int64_t>(total_frames),
                              N_fft, R_s);
        if (!plan) {
            std::fprintf(stderr,
                "warptempo_gui: render error: %s\n", plan.error().c_str());
            return RenderOutcome::Failed;
        }
        trim_plan = std::move(*plan);
    }

    // On-disk wav publishes finish here. Ctrl+Alt+R one-off wavs are primary
    // artifacts: .fingerprint is warning-only. Sweep batch wavs are
    // committable artifact sets: wav plus source-domain .warpmarkers,
    // source-domain .phaseresetmarkers (including the empty-file form), and
    // .settings. Those commit-critical sidecars must publish before the wav
    // is reported as successful. .fingerprint is written last of all: it is
    // the attestation that the artifact set is complete, so a fingerprint
    // match on a later render implies those files exist. Process death
    // after the wav rename lands on disk but before those sidecars finish
    // can leave an orphan wav that enumerate_render_view_list surfaces;
    // adopt_render_entry's validate-before-mutate path refuses that entry
    // cleanly. That residual crash window is the accepted design.
    auto finalize_published_wav = [&](const char* outcome) -> RenderOutcome {
        CommitCriticalSidecars sidecars =
            publish_commit_critical_batch_sidecars(/*hard_fail=*/true);
        if (!sidecars.ok) {
            remove_created_commit_sidecars(sidecars.created_paths);
            remove_newly_published_wav();
            cleanup_all();
            return RenderOutcome::Failed;
        }
        // An empty fingerprint here means the format computed none — the map
        // formats never build one; write no attestation then.
        if (!fingerprint.empty() &&
            !write_fingerprint_sidecar(final_output_path, fingerprint)) {
            std::fprintf(stderr,
                "warptempo_gui: fingerprint sidecar write skipped for %s\n",
                final_output_path.c_str());
        }
        cleanup_all();
        return finish_success(outcome);
    };

    // Reuse rungs, in trust order, above the engine: a project artifact
    // byte-copy, then a render-cache wav-byte publish. Both run before any
    // source-load or engine work; an empty fingerprint (non-wav formats
    // compute no fingerprint) skips both exactly as it already skips the
    // up-to-date check above.
    if (cancel_requested()) return cancelled_outcome();
    if (!req.output_buffer && !fingerprint.empty()) {
        // Rung: project artifact candidate. A batch entry whose fixed
        // archival sibling already holds a validated artifact for this
        // exact fingerprint is published by byte copy — the highest-
        // integrity reuse there is. When final_output_path already equals
        // the candidate, this rung is the up-to-date check above and has
        // already run.
        const std::string artifact_candidate =
            compose_source_sibling_paths().front().string();
        if (artifact_candidate != final_output_path &&
            fingerprint_sidecar_matches(artifact_candidate, fingerprint)) {
            std::error_code ec;
            std::filesystem::copy_file(
                artifact_candidate, staging_output_path,
                std::filesystem::copy_options::overwrite_existing, ec);
            // The copy above is a potentially long byte copy; a cancel that
            // lands during it must not publish. Re-check between the copy and
            // the rename to the final name — the staging file is not yet under
            // a final name, and cancelled_outcome unlinks it.
            if (!ec && cancel_requested()) return cancelled_outcome();
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
        if (req.render_cache->publish_wav(fingerprint, source_channels_probe,
                                          static_cast<int>(sample_rate),
                                          staging_output_path)) {
            // publish_wav did a potentially large byte write or disk copy to
            // the staging path; a cancel that lands during it must not
            // publish. Re-check between publish_wav and the rename to the
            // final name — the staging file is not yet under a final name, and
            // cancelled_outcome unlinks it.
            if (cancel_requested()) return cancelled_outcome();
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
        // Profiling: the cut the engine will see (the whole source when
        // untrimmed). stderr-only, no contract.
        profile_trim_begin_frame =
            trim_plan ? trim_plan->pre.begin_frame : 0;
        profile_trim_span_frames =
            trim_plan ? trim_plan->pre.frames
                      : static_cast<int64_t>(total_frames);
        profile_trim_end_frame =
            profile_trim_begin_frame + profile_trim_span_frames;
        const float* src_sample_data = nullptr;
        size_t src_sample_frames = 0;
        int src_sr = 0;
        int src_ch = 0;
        {
            // Borrow the GUI's shared source buffer — the one launch-time
            // immutable source; shared ownership keeps the buffer alive for
            // the render's lifetime (no mid-render swap occurs). Trimmed
            // renders read an offset+length view into the same buffer
            // (pre_trim's cut), no copy.
            const auto t_source_load_0 = profile::now();
            // Borrowed samples and the probed file are the same audio: the
            // loaded source is immutable for the process lifetime, so the
            // borrowed buffer decodes the probed bytes and covers the cut view
            // by construction for a well-formed container.
            src_ch = source_channels_probe;
            src_sample_data = req.source_samples->data();
            if (trim_plan) {
                src_sample_data +=
                    static_cast<size_t>(trim_plan->pre.begin_frame) *
                    static_cast<size_t>(src_ch);
                src_sample_frames =
                    static_cast<size_t>(trim_plan->pre.frames);
            } else {
                src_sample_frames = static_cast<size_t>(total_frames);
            }
            src_sr = static_cast<int>(sample_rate);
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
                    "[profile] source_read ms=%.3f source_frames_passed=%zu trim_span_frames=%lld approx_mb=%.1f channels=%d sample_rate=%d\n",
                    source_read_ms,
                    profile_source_frames_passed,
                    static_cast<long long>(profile_trim_span_frames),
                    profile::bytes_to_mb(bytes), src_ch, src_sr);
            }
        }

        // Global limiter toggle. When on, every path (disk trimmed/untrimmed
        // and the target-view buffer) gets the spectral(-0.3) + lifted
        // peak(0) chain; when off, both limiters sit out entirely and disk
        // output is clean 32-bit float.

        EngineParams ep;
        ep.source_audio_samples = src_sample_data;
        ep.source_audio_frames  = src_sample_frames;
        ep.source_sample_rate   = src_sr;
        ep.source_channels      = src_ch;
        // The engine is buffer-out only. When a caller-owned buffer was
        // supplied (target view), the engine renders straight into it;
        // otherwise a local buffer takes the emission and the shared
        // post-engine chain encodes it to the staging path below.
        std::vector<float> render_buf;
        std::vector<float>* out_buf =
            req.output_buffer ? req.output_buffer : &render_buf;
        ep.output_buffer = out_buf;
        // The spectral limiter's stdout diagnostics stay on for archival
        // renders and off for target-view scrubs.
        ep.limiter_verbose = (req.output_buffer == nullptr);
        // Trimmed renders hand the engine the trimmer's translated maps —
        // the engine renders them wholesale, ends at its map's last anchor,
        // and stays trim-ignorant. Untrimmed: the full pair verbatim.
        ep.warp_frame_map = trim_plan
            ? std::move(trim_plan->pre.warp_frame_map)
            : full_warp_frame_map;
        ep.phase_reset_frame_map = trim_plan
            ? std::move(trim_plan->pre.phase_reset_frame_map)
            : full_phase_reset_frame_map;
        // Trimmed renders adopt the schedule plan_trim derives from the full
        // map (identity by construction, not the translated map's
        // re-interpolation); full renders pass none and the engine generates
        // its own. trim_plan outlives the engine call below (it is also read
        // by finish_render), and the pointer targets a member never moved out.
        ep.source_frame_schedule = trim_plan
            ? &trim_plan->pre.source_frame_schedule
            : nullptr;
        ep.N                    = N_fft;
        ep.limiter              = req.engine_settings.limiter;

        // Projection refusal, orchestrator-side before the engine allocates
        // (refuse-before-cost): the engine's buffered emission is llrint of
        // its map's last anchor target; the encoded length is the crop.
        const int64_t engine_output_frames = static_cast<int64_t>(
            std::llrint(ep.warp_frame_map.back().tgt_frame));
        const int64_t encoded_frames =
            trim_plan ? trim_plan->post.samples : engine_output_frames;
        if (auto v = validate_render_projection(
                engine_output_frames, encoded_frames, src_ch, ep.limiter,
                /*encode_to_disk=*/req.output_buffer == nullptr); !v) {
            std::fprintf(stderr, "warptempo_gui: render error: %s\n",
                         v.error().c_str());
            cleanup_all();
            return RenderOutcome::Failed;
        }
        profile_target_frames = encoded_frames;
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
                static_cast<long long>(profile_target_frames),
                req.output_buffer ? "yes" : "no",
                ep.limiter ? "yes" : "no");
        }
        if (er != EngineResult::Success) {
            if (er == EngineResult::Failed) {
                std::fprintf(stderr, "warptempo_gui: render error: engine failed\n");
            }
            return handle_eng(er);
        }
        if (cancel_requested()) return cancelled_outcome();

        // The shared post-engine chain (finish_render, src/prepost/): crop
        // to the exact authored window when trimmed, peak limiter when
        // limiter=true, then the pcm24 decision — encode to the staging path
        // on the disk route, or the in-place PCM_24 snap (limiter-on only,
        // no encode) on the target-view buffer route. One implementation
        // shared with warptempo_cli, so the CLI stays byte-identical to the
        // GUI by construction (the CLI passes no cancel flag, so its chain
        // always completes).
        auto fin = finish_render(
            *out_buf, src_ch, src_sr, ep.limiter,
            trim_plan ? &trim_plan->post : nullptr,
            req.output_buffer ? std::string() : staging_output_path,
            cancel_flag);
        if (!fin) {
            std::fprintf(stderr, "warptempo_gui: render error: %s\n",
                         fin.error().c_str());
            cleanup_all();
            return RenderOutcome::Failed;
        }
        if (*fin == FinishRenderStatus::Cancelled) return cancelled_outcome();

        if (req.output_buffer && ep.limiter) {
            // Target playback auditions the deliverable lattice —
            // finish_render just snapped the limited buffer to PCM_24 in
            // place — so fresh renders, cache hits, and archival-artifact
            // loads carry sample-identical target-view audio: the writer
            // thread encodes the already-quantized buffer exactly by the
            // codec's roundtrip identity, so the cache blob decodes back to
            // these floats. Limiter-off target renders skip the insert as
            // before.
            const int64_t inserted_frames = src_ch > 0
                ? static_cast<int64_t>(req.output_buffer->size() /
                                       static_cast<size_t>(src_ch))
                : 0;
            // fingerprint is non-empty on every wav path (computed
            // unconditionally, never cleared); the insert additionally
            // requires a non-empty buffer.
            if (!fingerprint.empty() && inserted_frames > 0) {
                req.render_cache->insert_master_floats(
                    fingerprint, *req.output_buffer, src_ch, src_sr,
                    inserted_frames, cancel_token);
            }
        }

        // Buffer-route publication gate: the buffer path has no rename to
        // gate, so this is its last cancel check before finish_success. A
        // cancel that lands after finish_render's own gate — during the
        // cache prep above included — returns Cancelled here, and
        // on_render_done discards the abandoned buffer instead of binding
        // it to target view.
        if (req.output_buffer) {
            if (cancel_requested()) return cancelled_outcome();
        }

        // Atomic publish: staging → final. Buffer path skips this — the
        // synthesised audio already landed in *req.output_buffer.
        if (!req.output_buffer) {
            if (cancel_requested()) return cancelled_outcome();
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
            // hasn't landed yet simply re-renders. fingerprint is non-empty on
            // every wav path (computed unconditionally, never cleared); the
            // guard stays as a defensive check.
            if (!fingerprint.empty()) {
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
        // Map formats: output_format == "warptempo_maps", "generic_map", or
        // "midi_map". No engine, no limiter, and no trim (ruled): trim is a
        // wav-only render window, never an artifact shape — the map formats
        // only ever write the FULL maps. The GUI dispatch preflight refuses
        // a trimmed map-format render with the popup; this stderr refusal is
        // the async backstop.
        // These exports carry deliverable-relative targets and absolute
        // source frames, so a consumer reads the original source audio
        // directly and no companion audio is written.
        if (trimmed) {
            std::fprintf(stderr,
                "warptempo_gui: render error: map formats take no trim; "
                "clear the trim or render wav\n");
            cleanup_all();
            return RenderOutcome::Failed;
        }
        if (cancel_requested()) return cancelled_outcome();
        // The full midi tempo map is derived here, on the only path that
        // consumes it; the wav branch never needs it. The phase reset column
        // is the pipeline's full deliverable-form derivation, computed
        // against the very map shipped beside it, so the warptempo_maps pair
        // is self-consistent: an engine fed the pair renders that map's
        // geometry exactly.
        const std::vector<MidiTempoMapEntry> full_midi_tempo_map =
            derive_midi_tempo_map(full_warp_frame_map, sample_rate);
        if (output_format == "warptempo_maps") {
            // The pair: the warp frame map plus the phase reset frame map,
            // TWO files, together exactly the engine's input — the full map
            // and the full deliverable-form derivation built once for this
            // render above. Both
            // columns always ship — an empty reset list
            // still writes the empty .phaseresetframemap file, mirroring
            // the marker sidecars' empty-file convention.
            // output_paths' order comes from the extension list: entry 0 is
            // the warp column, entry 1 the phase reset column.
            const std::string warp_final = output_paths.front().string();
            const std::string phase_reset_final = output_paths.back().string();
            const std::string warp_staging = render_staging_path(warp_final);
            const std::string phase_reset_staging =
                render_staging_path(phase_reset_final);
            // Stage-first, gate, forfeit, publish: both staging files are
            // written to their ".tmp" names FIRST, before any old final is
            // touched. A staging-write failure or a cancel landing during the
            // stage now leaves the old pair fully intact — both stale finals
            // are still present, so a consumer reading them mid-render sees a
            // consistent previous generation, not a half pair. Only after both
            // stagings exist and the cancel gate has passed are the two old
            // finals unlinked (the old pair forfeited) and the stagings
            // rename-published, warp first, phase reset second. Between the two
            // renames there is deliberately no cancel check: once the first
            // rename lands, completing the pair beats leaving a half pair — the
            // same accepted-tail spirit as the post-WAV-rename attestations.
            // The forfeit window is thus just the two renames; a cancel or a
            // staging failure before it leaves the old pair untouched. The
            // mixed-generation invariant still holds: staging writes are not
            // final names, so the stale finals are gone before the first new
            // byte becomes visible UNDER A FINAL NAME. If a rename or process
            // death strikes inside the two-rename window, at most one column is
            // present, so an engine consumer refuses loudly at init on the
            // missing column. In-process the arms still tidy up — a staging-
            // write failure unlinks both stagings, and a second-rename failure
            // pulls back the just-published warp final.
            bool staged_ok = true;
            if (auto w = write_warp_frame_map(warp_staging,
                                              full_warp_frame_map); !w) {
                std::fprintf(stderr, "warptempo_gui: render error: %s\n",
                             w.error().c_str());
                staged_ok = false;
            } else if (auto w2 = write_phase_reset_frame_map(
                           phase_reset_staging,
                           full_phase_reset_frame_map); !w2) {
                std::fprintf(stderr, "warptempo_gui: render error: %s\n",
                             w2.error().c_str());
                staged_ok = false;
            }
            if (!staged_ok) {
                unlink_silent(warp_staging);
                unlink_silent(phase_reset_staging);
                cleanup_all();
                return RenderOutcome::Failed;
            }
            // Both stagings are on disk; a cancel here still publishes nothing
            // and leaves the old pair intact. Past this gate the forfeit
            // begins.
            if (cancel_requested()) {
                unlink_silent(warp_staging);
                unlink_silent(phase_reset_staging);
                return cancelled_outcome();
            }
            // Forfeit the old pair, then publish:
            unlink_silent(warp_final);
            unlink_silent(phase_reset_final);
            std::error_code ec;
            std::filesystem::rename(warp_staging, warp_final, ec);
            if (ec) {
                std::fprintf(stderr,
                    "warptempo_gui: render error: rename failed for '%s' -> "
                    "'%s': %s\n",
                    warp_staging.c_str(), warp_final.c_str(),
                    ec.message().c_str());
                unlink_silent(warp_staging);
                unlink_silent(phase_reset_staging);
                cleanup_all();
                return RenderOutcome::Failed;
            }
            std::filesystem::rename(phase_reset_staging, phase_reset_final, ec);
            if (ec) {
                std::fprintf(stderr,
                    "warptempo_gui: render error: rename failed for '%s' -> "
                    "'%s': %s\n",
                    phase_reset_staging.c_str(), phase_reset_final.c_str(),
                    ec.message().c_str());
                // A half pair must never land: pull back the just-published
                // warp file along with the remaining staging.
                unlink_silent(warp_final);
                unlink_silent(phase_reset_staging);
                cleanup_all();
                return RenderOutcome::Failed;
            }
        } else {
            // Single-file map formats: stage to staging_output_path (the
            // final path plus ".tmp", covered by cleanup_all) so a cancel
            // landing during the write publishes nothing. Write staging,
            // re-check the cancel flag, then rename to the final name.
            auto map_write = (output_format == "generic_map")
                ? write_warp_frame_map(staging_output_path,
                                       full_warp_frame_map)
                : write_midi_tempo_map(staging_output_path,
                                       full_midi_tempo_map);
            if (!map_write) {
                std::fprintf(stderr, "warptempo_gui: render error: %s\n",
                             map_write.error().c_str());
                cleanup_all();
                return RenderOutcome::Failed;
            }
            if (cancel_requested()) return cancelled_outcome();
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
        }
        if (prof) {
            // Map formats are untrimmed by the refusal above; the profiling
            // fields carry the whole source.
            profile_trim_begin_frame = 0;
            profile_trim_end_frame = static_cast<int64_t>(total_frames);
            profile_trim_span_frames = profile_trim_end_frame;
            profile_source_channels = source_channels_probe;
            profile_source_sample_rate = static_cast<int>(sample_rate);
        }
    }

    // Non-wav batch artifacts are not Shift+. commit candidates. Preserve
    // their existing warning-only sidecar behavior, while still writing the
    // source-domain phase-reset companion as an empty file when the list is
    // empty.
    publish_commit_critical_batch_sidecars(/*hard_fail=*/false);

    cleanup_all();
    return finish_success("success");
}
