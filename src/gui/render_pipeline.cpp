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

// write_frame_map_pair (over write_warp_frame_map / write_phase_reset_frame_map)
// lives in the parser (map_output.cpp) so the GUI render pipeline's cache-dir
// framemap pair and the headless parser CLI emit byte-identical artifacts, and
// share the one all-or-nothing write-or-clean home, from one implementation.

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

    // --- Read settings (typed; the live app.engine_settings is mutated
    // through strict-validated authoring paths, so every field is in
    // range by construction here). Wav is the only render product. ---
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

    // Assembled here, at the probe, so the reset frames are always validated
    // against the probed source length (the authored positions are whole
    // int64 source frames that widen exactly into the parser's double
    // intermediate — a sidecar is authored against one audio file's frame
    // grid).
    auto phase_reset_source_frames_r =
        build_phase_reset_source_frames(
            slice_to_phase_reset_markers(req.phase_resets), sample_rate,
            total_frames);
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
                                        sample_rate, total_frames);
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
    // render beside the full map: the untrimmed engine input, the cache-dir
    // framemap pair's phase reset column, and plan_trim's translate/filter
    // source are all this one list.
    const std::vector<double> full_phase_reset_frame_map =
        derive_phase_reset_frame_map(phase_reset_source_frames,
                                     full_warp_frame_map);

    // --- Compose the wav output path. ---
    // Batch renders name into the batch folder with the batch basename;
    // source-sibling renders name into the source's parent with
    // render_output_stem (the title). Wav is the only product, so a render
    // composes exactly one path.
    const bool batch_render = !req.batch_folder.empty();
    auto compose_source_sibling_path = [&]() {
        return compose_render_output_path(
            render_output_directory(req.source_audio_path),
            render_output_stem(req.engine_settings));
    };
    const std::filesystem::path output_path =
        batch_render
            ? compose_render_output_path(req.batch_folder, req.batch_basename)
            : compose_source_sibling_path();
    const std::string final_output_path = output_path.string();
    // Hard refusal: never overwrite the source audio itself. Overwriting a
    // previous render with the same title is intended behavior; the source
    // is the one path that must survive every dispatch. equivalent() is an
    // inode-level match and only succeeds when both paths exist — if the
    // output path doesn't exist yet it cannot be the source. The output
    // path's render_staging_path sibling is checked too: the staging name is
    // opened with a truncating write before the render completes, so an
    // existing staging file resolving to the source (a symlink or hard link,
    // or the source literally named `<final>.tmp`) would destroy it just as
    // surely. This is the render-time inode backstop, so it also covers
    // batch-folder stagings, whose finals are composed from the batch folder
    // rather than the source sibling.
    for (const std::filesystem::path& candidate :
             {output_path,
              std::filesystem::path(
                  render_staging_path(output_path.string()))}) {
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

    // --- Cache-dir framemap pair (future-proofing; GUI archival disk route
    // only). Drop the FULL warp frame map and its phase-reset column into the
    // RenderCache per-process dir, named by the final wav's basename stem —
    // together exactly the engine's input for the FULL render (deliverable-
    // relative target column, absolute source frames, always untrimmed). The
    // dir is removed at shutdown and orphans
    // are swept at the next launch, so the pair rests only between a render
    // and program close and nothing accumulates. Buffer-route previews
    // deliberately skip; the CLI writes the same pair through the same shared
    // write_frame_map_pair into the same cache convention, its pid dir swept as
    // a dead-PID orphan by the GUI's next launch. This write is non-fatal — a
    // failure prints one stderr line and the render proceeds — and needs no
    // staging/rename dance
    // (these are cache files, not deliverables) or cancel gating (a cancelled
    // render leaving a pair in the swept pid dir is harmless).
    //   Collision namespace: batch cells in DIFFERENT numbered batch folders
    // legitimately reuse basenames (e.g. 1_+0.00.wav), so a flat <pid>/<stem>.*
    // would let a re-run sweep clobber an earlier batch's pair while both wavs
    // stay addressable under renders/. A batch cell therefore mirrors its batch
    // folder name as a subdir — retrieval walks <pid>/<batch-folder-name>/
    // <stem>.* — created here (creation failure just makes the write fail into
    // the existing non-fatal skip). A source-dir single (empty batch_folder)
    // stays flat <pid>/<stem>.* — one source per process, no collision. ---
    if (!req.output_buffer) {
        const std::string pid_dir = req.render_cache->process_dir();
        if (!pid_dir.empty()) {
            std::filesystem::path target_dir = pid_dir;
            if (!req.batch_folder.empty()) {
                target_dir /=
                    std::filesystem::path(req.batch_folder).filename();
                std::error_code mkec;
                std::filesystem::create_directories(target_dir, mkec);
            }
            const std::string stem =
                std::filesystem::path(final_output_path).stem().string();
            if (auto w = write_frame_map_pair(
                    target_dir.string(), stem,
                    full_warp_frame_map, full_phase_reset_frame_map); !w) {
                std::fprintf(stderr,
                    "warptempo_gui: render warning: cache framemap write "
                    "skipped: %s\n", w.error().c_str());
            }
        }
    }

    // Staging path (render_staging_path: final path plus ".tmp") for the
    // atomic rename used by the wav engine path and the reuse rungs: every
    // publication writes staging first, gates on cancel, then renames to the
    // final name, so a cancel never lands a partial file under a final name.
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
            // adopts it with plain load semantics.
            {
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
                // domain before writing — the adopted view then lands sensibly
                // on the entry's own axis rather than off its end. Viewport
                // start sits in [0, total-1] (a start on the total's frame
                // shows nothing) and the playhead in [0, total] (it may rest
                // on the end exactly). target_total is this entry's map domain
                // total.
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
                                         req.authoring.audio_player,
                                         req.engine_settings)) {
                    note_failure(st_path);
                    return result;
                }
                note_created(st_path, existed);
            }

            return result;
        };

    auto finish_success = [&](const char* outcome) -> RenderOutcome {
        (void)outcome;
        std::fprintf(stderr, "warptempo_gui: render complete: %s\n",
                     req.output_buffer ? "<buffer>" : final_output_path.c_str());
        return RenderOutcome::Success;
    };

    // Trim plan. plan_trim validates the authored bounds
    // first — validate_trim_frames stays the sole author of the
    // trim-validity vocabulary — then derives the source cut, the
    // translated maps, and the output crop in one computation. A refusal is
    // NOT a render failure: ambiguous trim falls back to the full,
    // untrimmed deliverable with one stderr line (the reachable case is the
    // target span rounding below one output sample — legal whole-frame
    // bounds under a fast tempo; crossed/equal cannot rest after the commit
    // and load auto-clears, and past-EOF is adversarial load-fatal).
    // trim_plan stays unset then, and every downstream RENDER stage (source
    // view, engine maps/schedule, crop, projection) keys on
    // trim_plan — never on "a bound was set" — so the fallback render is
    // byte-identical to a no-trim render of the same recipe. The plan is
    // computed here, AHEAD of the fingerprint up-to-date and reuse returns
    // below, precisely so that fallback line prints once per dispatch even
    // when the deliverable is already current — a repeat dispatch of a
    // fallback recipe must still surface the signal that its visible trim
    // bounds produced an untrimmed deliverable. The fingerprint below and
    // the entry .settings recipe record keep the request's trim bounds
    // unchanged on purpose: under the fallback that recipe genuinely
    // renders the full bytes, so the attestation stays honest.
    std::optional<TrimPlan> trim_plan;
    if (req.has_trim_begin || req.has_trim_end) {
        auto plan = plan_trim(full_warp_frame_map, full_phase_reset_frame_map,
                              req.has_trim_begin, req.trim_begin_frame,
                              req.has_trim_end, req.trim_end_frame,
                              static_cast<int64_t>(total_frames),
                              N_fft, R_s);
        if (!plan) {
            // plan.error() strings all begin with "trim ..." (the
            // vocabulary in trimmer.cpp), so no extra category word here.
            std::fprintf(stderr,
                "warptempo_gui: %s; rendering untrimmed\n",
                plan.error().c_str());
        } else {
            trim_plan = std::move(*plan);
        }
    }

    // The fingerprint's source identity is built directly from the request's
    // load-time-captured identity (req.source_load_size/mtime): the loaded
    // source is immutable for the process lifetime, so no on-disk re-stat is
    // needed. Every render is wav, so the fingerprint is always computed.
    RenderFileIdentity source_identity;
    source_identity.size = req.source_load_size;
    source_identity.mtime = req.source_load_mtime;
    const std::vector<uint8_t> fingerprint = render_fingerprint(
        req.source_audio_path, source_identity,
        static_cast<int>(sample_rate), req.warp_markers, req.phase_resets,
        req.engine_settings,
        req.has_trim_begin, req.trim_begin_frame,
        req.has_trim_end, req.trim_end_frame);
    if (cancel_requested()) return cancelled_outcome();
    if (!req.output_buffer &&
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

    // On-disk wav publishes finish here. Ctrl+Alt+R one-off wavs are primary
    // artifacts: .fingerprint is warning-only. Sweep batch wavs are
    // committable artifact sets: wav plus source-domain .warpmarkers,
    // source-domain .phaseresetmarkers (including the empty-file form), and
    // .settings. Those commit-critical sidecars must publish before the wav
    // is reported as successful. .fingerprint is written last of all: it is
    // the attestation that the artifact set is complete, so a fingerprint
    // match on a later render implies those files exist. Process death
    // after the wav rename lands on disk but before those sidecars finish
    // can leave an orphan wav that enumerate_render_entries surfaces;
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
        // The fingerprint is the attestation that the artifact set is
        // complete; every render is wav, so it is always written here.
        if (!write_fingerprint_sidecar(final_output_path, fingerprint)) {
            std::fprintf(stderr,
                "warptempo_gui: fingerprint sidecar write skipped for %s\n",
                final_output_path.c_str());
        }
        cleanup_all();
        return finish_success(outcome);
    };

    // Reuse rungs, in trust order, above the engine: a project artifact
    // byte-copy, then a render-cache wav-byte publish. Both run before any
    // source-load or engine work; the buffer route (target-view preview)
    // skips them.
    if (cancel_requested()) return cancelled_outcome();
    if (!req.output_buffer) {
        // Rung: project artifact candidate. A batch entry whose fixed
        // archival sibling already holds a validated artifact for this
        // exact fingerprint is published by byte copy — the highest-
        // integrity reuse there is. When final_output_path already equals
        // the candidate, this rung is the up-to-date check above and has
        // already run.
        const std::string artifact_candidate =
            compose_source_sibling_path().string();
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

    std::fprintf(stderr, "warptempo_gui: rendering -> %s\n",
                 final_output_path.c_str());

    {
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
        }

        // The limiter is always on: every path (disk trimmed/untrimmed and the
        // target-view buffer) gets the spectral(-0.3) + lifted peak(0) chain,
        // and disk output is always the limited PCM 24 deliverable.

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

        // Projection refusal, orchestrator-side before the engine allocates
        // (refuse-before-cost): the engine's buffered emission is llrint of
        // its map's last anchor target; the encoded length is the crop.
        const int64_t engine_output_frames = static_cast<int64_t>(
            std::llrint(ep.warp_frame_map.back().tgt_frame));
        const int64_t encoded_frames =
            trim_plan ? trim_plan->post.samples : engine_output_frames;
        if (auto v = validate_render_projection(
                engine_output_frames, encoded_frames, src_ch,
                /*encode_to_disk=*/req.output_buffer == nullptr); !v) {
            std::fprintf(stderr, "warptempo_gui: render error: %s\n",
                         v.error().c_str());
            cleanup_all();
            return RenderOutcome::Failed;
        }

        auto handle_eng = [&](EngineResult r) -> RenderOutcome {
            if (r == EngineResult::Success)   return RenderOutcome::Success;
            cleanup_all();
            return (r == EngineResult::Cancelled)
                ? RenderOutcome::Cancelled
                : RenderOutcome::Failed;
        };

        const EngineResult er = run_warptempo_engine(ep, cancel_flag);
        if (er != EngineResult::Success) {
            if (er == EngineResult::Failed) {
                std::fprintf(stderr, "warptempo_gui: render error: engine failed\n");
            }
            return handle_eng(er);
        }
        if (cancel_requested()) return cancelled_outcome();

        // The shared post-engine chain (finish_render, src/prepost/): crop
        // to the exact authored window when trimmed, the peak limiter, then
        // the pcm24 sink — encode to the staging path on the disk route, or
        // the in-place PCM_24 snap (no encode) on the target-view buffer
        // route. One implementation shared with warptempo_cli, so the CLI
        // stays byte-identical to the GUI by construction (the CLI passes no
        // cancel flag, so its chain always completes).
        auto fin = finish_render(
            *out_buf, src_ch, src_sr,
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

        if (req.output_buffer) {
            // Target playback auditions the deliverable lattice —
            // finish_render just snapped the limited buffer to PCM_24 in
            // place — so fresh renders, cache hits, and archival-artifact
            // loads carry sample-identical target-view audio: the writer
            // thread encodes the already-quantized buffer exactly by the
            // codec's roundtrip identity, so the cache blob decodes back to
            // these floats.
            const int64_t inserted_frames = src_ch > 0
                ? static_cast<int64_t>(req.output_buffer->size() /
                                       static_cast<size_t>(src_ch))
                : 0;
            // The fingerprint is always computed; the insert additionally
            // requires a non-empty buffer.
            if (inserted_frames > 0) {
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
            // hasn't landed yet simply re-renders.
            {
                std::vector<char> wav_blob;
                if (!read_file_bytes(final_output_path, wav_blob)) {
                    std::fprintf(stderr,
                        "warptempo_gui: render warning: read failed for "
                        "'%s'\n",
                        final_output_path.c_str());
                } else if (encoded_frames > 0) {
                    req.render_cache->insert(fingerprint, wav_blob, src_ch,
                                             src_sr, encoded_frames);
                }
            }
            return finalize_published_wav("success");
        }
    }

    // Only the buffer route (target-view preview) reaches here: the disk
    // route always returns inside the block above (finalize_published_wav or
    // an error return). The buffer already holds the synthesised audio; there
    // is no staging file to publish and no batch sidecar to write.
    cleanup_all();
    return finish_success("success");
}
