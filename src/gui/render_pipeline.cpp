#include "render_pipeline.h"

#include "engine/engine.h"
#include "engine/engine_geometry.h"
#include "app_state.h"
#include "audio.h"
#include "render.h"
#include "phaseresetmarkers.h"
#include "map_output.h"
#include "phase_reset_frame_map_build.h"
#include "render_output_naming.h"
#include "settings_io.h"
#include "warp_frame_map_view.h"
#include "render_assembly.h"
#include "profile_util.h"
#include "render_cache.h"

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

// write_warp_frame_map and write_midi_tempo_map moved to the parser
// (map_output.cpp) so the GUI render pipeline and the headless parser CLI
// emit byte-identical artifacts from one implementation.

// resolve_warp_markers_for_render moved to warp_frame_map_build.cpp (public function) so the
// target-view paint can reach it without crossing the render_pipeline
// boundary. Both callers — do_render below and the GUI paint in
// paint_handler — receive the same resolved list.

}  // namespace

RenderRequest build_render_request(std::string source_audio_path,
                                   std::vector<GuiWarpMarker> warp_markers,
                                   std::vector<GuiPhaseResetMarker> phase_resets,
                                   EngineSettings engine_settings,
                                   bool has_trim_begin, double trim_begin_sec,
                                   bool has_trim_end,   double trim_end_sec,
    std::string batch_folder,
    std::string batch_basename) {
    RenderRequest req;
    req.source_audio_path  = std::move(source_audio_path);
    req.warp_markers            = std::move(warp_markers);
    req.engine_settings    = std::move(engine_settings);
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

    // Hard refusal: the on-screen authored state (markers, trim, settings)
    // describes the loaded buffer, so rendering a source that changed on disk
    // since load — an ordinary mid-session swap or regeneration at the same
    // path, or anything else — would silently bind those authoring decisions
    // to different audio; hence refusal, not warn-and-continue. Every dispatched
    // render on both the file and target-view buffer paths runs this check
    // unconditionally, and the target-view reuse rungs in
    // GuiTargetRender::dispatch_render_now independently prove the same
    // load-identity equality before reusing cached or archival audio,
    // falling through to this refusal on mismatch. The load identity is a
    // required request field — GuiAudio refuses a load it cannot stat, and
    // every dispatcher forwards the pair — so the check runs unconditionally.
    {
        RenderFileIdentity load_time_identity;
        if (!stat_file_identity(req.source_audio_path, load_time_identity) ||
            load_time_identity.size != req.source_load_size ||
            load_time_identity.mtime != req.source_load_mtime) {
            std::fprintf(stderr,
                "warptempo_gui: render error: source '%s' changed on disk "
                "since it was loaded; refusing to render (reload the "
                "source)\n",
                req.source_audio_path.c_str());
            return RenderOutcome::Failed;
        }
    }

    const long sample_rate  = src_info->sample_rate;
    const long total_frames = static_cast<long>(src_info->frames);
    const int source_channels_probe = src_info->channels;
    profile_source_channels = source_channels_probe;
    profile_source_sample_rate = static_cast<int>(sample_rate);

    // Derived here, at the probe, so the reset frames are always in the frame
    // domain of the source actually being rendered, symmetric with
    // build_warp_frame_map's conversion of warp marker seconds; the conversion
    // also validates the authored reset times against the probed source
    // length.
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
    // engine always renders its map wholesale: a trimmed wav render slices
    // this canonical map into the trimmed deliverable map via
    // assign_engine_warp_frame_map before dispatch, and the trimmed
    // .warpframemap / .phaseresetframemap / .miditempomap artifacts derive
    // from that same window —
    // one trim computation, shared. The inherited t_a history from frame 0
    // is what makes the windowed render null against the full render.
    // Built here at the probe, beside the phase-reset conversion above: the
    // two authored-time-to-frame conversions are this pipeline's parallel
    // pair. Running it ahead of the fingerprint reuse return below is
    // behavior-neutral — a fingerprint match implies this exact marker set
    // already built successfully when the matching render was produced, so
    // no reuse path can fail here that previously succeeded, and the reuse
    // hit's extra map build is negligible against the pipeline. ---
    auto resolved_warp_markers =
        resolve_warp_markers_for_render(slice_to_warp_markers(req.warp_markers));
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
    // file is covered by the same refusal.
    for (const std::filesystem::path& out_path : output_paths) {
        std::error_code ec;
        if (std::filesystem::exists(out_path, ec) &&
            std::filesystem::equivalent(out_path,
                                        req.source_audio_path, ec)) {
            std::fprintf(stderr,
                "warptempo_gui: render error: output '%s' resolves to the "
                "source audio file; refusing to overwrite the source. "
                "Change the title setting.\n",
                out_path.string().c_str());
            return RenderOutcome::Failed;
        }
    }

    // Staging path used by the wav engine path's atomic rename. Text-file
    // formats write final_output_path directly, except the warptempo_maps
    // pair, which stages both of its files (path + ".tmp") so a half pair
    // never lands.
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
                req.warp_markers.size(), phase_reset_source_frames.size(),
                static_cast<long long>(phase_reset_offset_samples),
                req.output_buffer ? "yes" : "no",
                req.engine_settings.limiter ? "yes" : "no",
                outcome);
        }
        std::fprintf(stderr, "warptempo_gui: render complete: %s\n",
                     req.output_buffer ? "<buffer>" : final_output_path.c_str());
        return RenderOutcome::Success;
    };

    // The identity check above just proved the on-disk identity equals the
    // request's load identity, so the fingerprint's source identity is built
    // from that proven pair directly — no second stat, no race window between
    // the check and the fingerprint. fingerprint is non-empty exactly when
    // output_format is wav, until the mid-render identity re-check in the wav
    // arm deliberately clears it.
    RenderFileIdentity source_identity;
    std::vector<uint8_t> fingerprint;
    if (output_format == "wav") {
        source_identity.size = req.source_load_size;
        source_identity.mtime = req.source_load_mtime;
        fingerprint = render_fingerprint(
            req.source_audio_path, source_identity,
            static_cast<int>(sample_rate), req.warp_markers, req.phase_resets,
            req.engine_settings,
            req.has_trim_begin, req.trim_begin_sec,
            req.has_trim_end, req.trim_end_sec);
    }
    // Fingerprint emptiness doubles as a map-format gate on this shared
    // pre-branch path: non-wav formats compute no fingerprint.
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
            const std::filesystem::path warp_sidecar_path =
                bf / (req.batch_basename + ".renderwarpmarkers");
            const std::filesystem::path phase_reset_sidecar_path =
                bf / (req.batch_basename + ".renderphaseresetmarkers");
            std::error_code warp_sidecar_ec;
            std::error_code phase_reset_sidecar_ec;
            const bool warp_sidecar_exists = std::filesystem::exists(warp_sidecar_path, warp_sidecar_ec);
            const bool phase_reset_sidecar_exists = std::filesystem::exists(phase_reset_sidecar_path, phase_reset_sidecar_ec);
            if (!warp_sidecar_exists || !phase_reset_sidecar_exists) {
                const std::filesystem::path& missing =
                    !warp_sidecar_exists ? warp_sidecar_path : phase_reset_sidecar_path;
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

    const TrimSourceWindow trim_window = resolve_trim_source_window(
        req.has_trim_begin, req.trim_begin_sec,
        req.has_trim_end, req.trim_end_sec,
        sample_rate, total_frames, N_fft);

    // Source-aware trim check. Runs before any window computation, engine
    // slice, or artifact derivation.
    if (auto v = validate_trim_frames(
            trim_window.trim_begin_src, trim_window.trim_end_src,
            req.has_trim_begin, req.has_trim_end,
            static_cast<int64_t>(total_frames)); !v) {
        std::fprintf(stderr,
            "warptempo_gui: render error: %s\n", v.error().c_str());
        return RenderOutcome::Failed;
    }

    // The engine no longer windows; render-domain sidecars subtract the slice
    // origin captured in window_offset_samples. 0 when untrimmed. Reuse rungs
    // need the same value before the engine assembly path runs. The sidecar
    // writer also needs the trim's cap (0 when untrimmed) for its
    // window-participation verdict below — the cap is value-identical to the
    // trimmed deliverable map's boundary-pair target, exactly, so the verdict
    // bound is the deliverable map's own final anchor target (not the integer
    // output length assign_engine_phase_reset_frame_map derives from the
    // map's last anchor).
    // Only the offset and cap fields are read here; a degenerate stored-zero
    // window carries no map, which this pre-slice never touches.
    int64_t window_offset_samples = 0;
    int64_t trim_emit_sample_cap = 0;
    if ((req.has_trim_begin || req.has_trim_end) && output_format == "wav") {
        const WindowedWarpFrameMap w = slice_warp_frame_map_to_trim_window(
            full_warp_frame_map, trim_window.trim_begin_src,
            trim_window.trim_end_src, N_fft, R_s);
        window_offset_samples = w.window_offset_samples;
        trim_emit_sample_cap = w.emit_sample_cap;
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
        // Only wav renders produce these — the map formats (warptempo_maps,
        // generic_map, midi_map) skip
        // the engine. The phase-reset sidecar is always written on wav batch
        // renders, including as an empty file.
        if (output_format == "wav") {
            // Walk the FULL frame map; each emitted render-domain time is the
            // full-render output sample minus the slice origin. When untrimmed,
            // window_offset_samples is 0 and this reduces to old behavior.
            const double sr_d = static_cast<double>(sample_rate);
            // window_offset_samples was computed at slice time. The engine no
            // longer windows, so there is no engine-side offset to recompute.

            // After head alignment, markers sit on the aligned
            // feature: a marker at source F displays at tgt(F) - window_offset
            // with no start-trim term.

            // Markers: lockstep walk between req.warp_markers and the real-segment
            // range of full_warp_frame_map (built trim-off, so no synthetic
            // trim anchors). Each kept marker consumes the
            // next-in-order segment; the trim range gates only emission, not
            // consumption, so the lockstep stays in step with
            // full_warp_frame_map's all-segments vector.
            // s.tgt_frame is the full-render target sample; subtracting
            // window_offset_samples places it on the trimmed wav axis. The
            // effective-disabled verdict comes from the shared
            // warp_markers_render_keep_mask — the same mask
            // resolve_warp_markers_for_render filters on — so the sidecar's
            // marker set and the resolver's render list agree by construction.
            // The mask gates consumption itself (a dropped marker has no
            // segment in full_warp_frame_map, so it skips before the iterator
            // advances), while out-of-trim and pre-origin gate emission only,
            // each running after the segment is consumed.
            const std::vector<bool> warp_keep = warp_markers_render_keep_mask(
                slice_to_warp_markers(req.warp_markers));

            auto seg_it = full_warp_frame_map.begin();
            std::vector<GuiWarpMarker> sidecar_warp_markers;
            sidecar_warp_markers.reserve(req.warp_markers.size());
            for (size_t mi = 0; mi < req.warp_markers.size(); ++mi) {
                const GuiWarpMarker& g = req.warp_markers[mi];
                if (!warp_keep[mi]) continue;

                // Consume this marker's segment first (full_warp_frame_map
                // holds every segment, so the lockstep advances for every
                // non-disabled marker regardless of trim).
                if (seg_it == full_warp_frame_map.end()) break;
                const auto& s = *seg_it;
                ++seg_it;

                // Trim-range filter (inclusive both ends), run in authored
                // seconds against the request's trim bounds. Gates emission
                // only; the segment above is already consumed.
                if (req.has_trim_begin && g.time_seconds < req.trim_begin_sec) continue;
                if (req.has_trim_end && g.time_seconds > req.trim_end_sec) continue;

                GuiWarpMarker w = g;
                w.time_seconds  =
                    (s.tgt_frame - static_cast<double>(window_offset_samples))
                    / sr_d;
                // Pre-origin filter: a marker whose render position falls
                // before the delivered WAV's first sample is not present in
                // the delivered audio (the trimmed deliverable starts about
                // N/2 after the trim instant), so it is dropped rather than
                // pinned at zero. After this drop, the surviving warp times
                // are strictly ascending doubles by map monotonicity, so the
                // display sidecar's timestamps are strictly ascending too.
                if (w.time_seconds < 0.0) continue;
                sidecar_warp_markers.push_back(std::move(w));
            }
            const std::string warp_sidecar_path =
                (bf / (req.batch_basename + ".renderwarpmarkers")).string();
            if (!GuiWarpMarkers::save(warp_sidecar_path, sidecar_warp_markers)) {
                std::fprintf(stderr,
                    "warptempo_gui: render warning: write failed for '%s'\n",
                    warp_sidecar_path.c_str());
                all_ok = false;
            }

            // The render-domain phase-reset sidecar is always written for wav
            // batch renders, including the empty-file form. Surviving resets
            // are forward-mapped from their clicked source time through the
            // same map and placed at tgt(F) - window_offset by the derivation
            // chain's own window verdict (phase_reset_window_target_frame in
            // phase_reset_frame_map_build.h), so a reset sits on the same
            // musical position in render-view as in source and target views,
            // expressed as the same exact double the warp loop above uses,
            // with no intermediate frame rounding. The trim-range filter runs
            // in authored seconds against the request's trim bounds, ahead of
            // the window verdict. Drop disabled, out-of-trim, and window
            // non-participants.
            //
            // Render target end for the window verdict: the trim's cap when
            // the trim set one (value-identical to the trimmed deliverable
            // map's boundary-pair target, exactly), else the full map's last
            // target anchor unrounded — the deliverable map's own final
            // anchor target in both cases, matching the exact bound the
            // deliverable-form derivation compares against.
            const double render_target_end_for_sidecars =
                trim_emit_sample_cap > 0
                    ? static_cast<double>(trim_emit_sample_cap)
                    : full_warp_frame_map.back().tgt_frame;
            std::vector<GuiPhaseResetMarker> sidecar_phase_resets;
            sidecar_phase_resets.reserve(req.phase_resets.size());
            for (const auto& t : req.phase_resets) {
                if (t.disabled) continue;
                if (req.has_trim_begin && t.time_seconds < req.trim_begin_sec) continue;
                if (req.has_trim_end && t.time_seconds > req.trim_end_sec) continue;
                // Window-participation verdict, shared with the engine-input
                // derivation. nullopt drops two classes, both deliberate. A
                // negative window target is the derivation's before-window
                // drop: the instant precedes the deliverable's first sample
                // and is unrepresentable on its time axis. A window target at
                // or past the deliverable map's final anchor target is the
                // derivation's final-anchor drop: the instant lies beyond the
                // deliverable's last sample (a reset exactly at the trim end,
                // or at source EOF on an untrimmed render), so display
                // participation converges on the window's own bounds. Resets
                // dropped only by the derivation's lead-in dropzone (window
                // target above zero but within phase_reset_offset_samples of
                // the window start) still display, deliberately: the display
                // sidecars show every authored marker that exists on the
                // deliverable's time axis regardless of its engine-side fate,
                // exactly as the warp sidecar displays markers whose
                // breakpoints the slicer coalesced into window anchors.
                const auto window_target = phase_reset_window_target_frame(
                    t.time_seconds * sr_d, full_warp_frame_map,
                    window_offset_samples, render_target_end_for_sidecars);
                if (!window_target) continue;
                GuiPhaseResetMarker w = t;
                w.time_seconds = *window_target / sr_d;
                sidecar_phase_resets.push_back(std::move(w));
            }
            const std::string phase_reset_sidecar_path =
                (bf / (req.batch_basename + ".renderphaseresetmarkers"))
                .string();
            if (!GuiPhaseResetMarkers::save(phase_reset_sidecar_path, sidecar_phase_resets)) {
                std::fprintf(stderr,
                    "warptempo_gui: render warning: write failed for '%s'\n",
                    phase_reset_sidecar_path.c_str());
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
        } else if (
            // An empty fingerprint here means the mid-render identity
            // re-check cleared it (mid-render source replacement); write no
            // attestation then.
            !fingerprint.empty() &&
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
    // source-load or engine work; an empty fingerprint (non-wav formats
    // compute no fingerprint) skips both exactly as it already skips the
    // up-to-date check above.
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
        const float* src_sample_data = nullptr;
        size_t src_sample_frames = 0;
        int src_sr = 0;
        int src_ch = 0;
        {
            // Borrow the GUI's shared source buffer; shared ownership keeps
            // mid-render file swaps safe.
            const auto t_source_load_0 = profile::now();
            // Borrowed samples and the probed file are the same audio: the
            // probe's load-identity hardfail proved the borrowed buffer
            // decodes the probed bytes, so it covers load_end_frame by
            // construction for a well-formed container.
            src_sample_data = req.source_samples->data();
            src_sample_frames = trim_window.load_end_frame;
            src_sr = static_cast<int>(sample_rate);
            src_ch = source_channels_probe;
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

        // The fingerprint names the load identity, which the probe's hardfail
        // proved still matched the on-disk file at dispatch. This re-check
        // compares the post-render stat against that same identity: if the
        // source was replaced mid-render, the association between the borrowed
        // samples and the fingerprint can no longer be proven, so publish the
        // render without one. A rename-replace that lands mid-render can skip
        // the fingerprint of a still-consistent render; the cost is one
        // re-render, in the safe direction. Reuse rungs above are deliberately
        // unguarded because they publish audio and fingerprint that were
        // created together, so their association holds regardless of what the
        // file does afterward. fingerprint is always non-empty here — the wav
        // arm computes it unconditionally and nothing clears it before this
        // point — so the clear below is the only producer of an empty
        // fingerprint on the wav path.
        {
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
        // window. When a bound is set, hand the engine the trimmed
        // deliverable map, which ends at its rounded boundary pair; the
        // engine renders it wholesale, ends at its last anchor, and stays
        // trim-ignorant. Untrimmed: the full map verbatim, offset 0.
        window_offset_samples = assign_engine_warp_frame_map(
            ep, full_warp_frame_map, req.has_trim_begin || req.has_trim_end,
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
        const int64_t render_target_frames = assign_engine_phase_reset_frame_map(
            ep, phase_reset_source_frames);
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
            // The fingerprint may have been cleared by the mid-render
            // identity re-check above; skip the insert then.
            if (!fingerprint.empty() && inserted_frames > 0) {
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
            // hasn't landed yet simply re-renders. The fingerprint may have
            // been cleared by the mid-render identity re-check above; skip
            // the insert then.
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
        // "midi_map". No engine, no limiter.
        // These exports carry deliverable-relative targets and absolute
        // source frames, so a consumer reads the original source audio
        // directly — trimmed or not — and no companion audio is written.
        const bool trimmed = req.has_trim_begin || req.has_trim_end;
        // Trimmed artifacts derive from the same window the engine renders, so
        // they describe the trimmed deliverable byte-for-byte; untrimmed renders
        // write the full maps verbatim. This path runs no engine. The full midi
        // tempo map is derived here, on the only path that consumes it; the wav
        // branch never needs it.
        const std::vector<MidiTempoMapEntry> full_midi_tempo_map =
            derive_midi_tempo_map(full_warp_frame_map, sample_rate);
        TrimmedArtifactMaps artifacts;
        if (trimmed) {
            auto a = derive_trimmed_artifact_maps(
                full_warp_frame_map, full_midi_tempo_map,
                phase_reset_source_frames,
                trim_window.trim_begin_src, trim_window.trim_end_src,
                N_fft, R_s, sample_rate);
            if (!a) {
                std::fprintf(stderr, "warptempo_gui: render error: %s\n",
                             a.error().c_str());
                cleanup_all();
                return RenderOutcome::Failed;
            }
            artifacts = std::move(*a);
        } else {
            // Untrimmed: the full maps verbatim, with the phase reset column
            // filled by the same deliverable-form derivation the trimmed path
            // runs inside derive_trimmed_artifact_maps — here against the
            // full map, so both cases flow through the identical formula and
            // the member is always populated.
            artifacts = TrimmedArtifactMaps{
                full_warp_frame_map,
                derive_phase_reset_frame_map(phase_reset_source_frames,
                                             full_warp_frame_map),
                full_midi_tempo_map};
        }
        if (output_format == "warptempo_maps") {
            // The pair: the warp frame map plus the phase reset frame map,
            // TWO files, together exactly warptempo_engine's input. The
            // phase reset column was derived beside its siblings when
            // `artifacts` was filled above — the deliverable-form derivation
            // of the pipeline's already-built source-frame list against
            // artifacts.warp_frame_map (untrimmed the full map, trimmed
            // the trimmed deliverable map) — so the window verdict and
            // anticipation are computed against the very map shipped in
            // the pair and the two files are self-consistent:
            // warptempo_engine fed the pair renders that map's geometry
            // exactly. Both columns always ship — an empty reset list
            // still writes the empty .phaseresetframemap file, mirroring
            // the marker sidecars' empty-file convention.
            // output_paths' order comes from the extension list: entry 0 is
            // the warp column, entry 1 the phase reset column.
            const std::string warp_final = output_paths.front().string();
            const std::string phase_reset_final = output_paths.back().string();
            const std::string warp_staging = warp_final + ".tmp";
            const std::string phase_reset_staging = phase_reset_final + ".tmp";
            // Delete-first, all-or-nothing publish: the old pair's two finals
            // are unlinked before any new byte becomes visible under a final
            // name; only then are both staging files written and rename-
            // published, warp first, phase reset second. Once this sequence
            // begins the old pair is forfeited, and from that point every
            // possible interruption — an in-process write or rename failure,
            // or process death at any instant — leaves at most one column
            // present on disk, so warptempo_engine refuses loudly on the
            // missing column. A mixed-generation pair (a fresh warp map beside
            // a phase reset list derived against a different warp map) can no
            // longer exist: the stale finals are gone before the first new byte
            // lands under a final name. In-process the arms still tidy up — a
            // staging-write failure unlinks both stagings, and a second-rename
            // failure pulls back the just-published warp final. The cost is
            // that a failed or interrupted publish destroys the previous pair;
            // that is accepted, because the pair regenerates from the authored
            // sources with a single re-run, and a loud refusal beats a silent
            // wrong render. The single-file formats below stay direct writes: a
            // lone artifact has no cross-generation sibling to mix with.
            // Forfeit the old pair up front:
            unlink_silent(warp_final);
            unlink_silent(phase_reset_final);
            bool staged_ok = true;
            if (auto w = write_warp_frame_map(warp_staging,
                                              artifacts.warp_frame_map); !w) {
                std::fprintf(stderr, "warptempo_gui: render error: %s\n",
                             w.error().c_str());
                staged_ok = false;
            } else if (auto w2 = write_phase_reset_frame_map(
                           phase_reset_staging,
                           artifacts.phase_reset_frame_map); !w2) {
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
            auto map_write = (output_format == "generic_map")
                ? write_warp_frame_map(final_output_path,
                                       artifacts.warp_frame_map)
                : write_midi_tempo_map(final_output_path,
                                       artifacts.midi_tempo_map);
            if (!map_write) {
                std::fprintf(stderr, "warptempo_gui: render error: %s\n",
                             map_write.error().c_str());
                cleanup_all();
                return RenderOutcome::Failed;
            }
        }
        if (prof) {
            profile_trim_begin_frame = trim_window.trim_begin_src;
            profile_trim_end_frame = trim_window.trim_end_src;
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
