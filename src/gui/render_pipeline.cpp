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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

#include <sndfile.h>

namespace {

// Silent-on-missing unlink wrapper.
void unlink_silent(const std::string& path) {
    if (path.empty()) return;
    ::unlink(path.c_str());
}

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
    SF_INFO src_info{};
    src_info.format = 0;
    SNDFILE* sf = sf_open(req.source_audio_path.c_str(), SFM_READ, &src_info);
    if (!sf) {
        std::fprintf(stderr,
            "warptempo_gui: render error: could not open source '%s'\n",
            req.source_audio_path.c_str());
        return RenderOutcome::Failed;
    }
    const long sample_rate  = src_info.samplerate;
    const long total_frames = static_cast<long>(src_info.frames);
    const int source_channels_probe = src_info.channels;
    sf_close(sf);
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

    std::vector<uint8_t> fingerprint;
    if (output_format == "wav" && !req.output_buffer) {
        RenderFileIdentity source_identity;
        if (stat_file_identity(req.source_audio_path, source_identity)) {
            fingerprint = render_fingerprint(
                req.source_audio_path, source_identity,
                static_cast<int>(sample_rate), req.markers, req.phase_resets,
                req.engine_settings,
                req.has_trim_begin, req.trim_begin_sec,
                req.has_trim_end, req.trim_end_sec);
        }
    }
    if (!fingerprint.empty() &&
        fingerprint_sidecar_matches(final_output_path, fingerprint)) {
        std::fprintf(stderr,
            "[warptempo_gui] render up to date (fingerprint match): %s\n",
            final_output_path.c_str());
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
    // hands the engine the canonical untrimmed frame map and windows it,
    // rather than rebuilding a local trimmed map; that inherited t_a history
    // from frame 0 is what makes the windowed render null against the full
    // render. build_maps already produces the untrimmed segment list when
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

    std::fprintf(stderr, "warptempo_gui: rendering %s -> %s\n",
                 output_format.c_str(), final_output_path.c_str());

    // The engine no longer windows; render-domain sidecars subtract the slice
    // origin captured in window_offset_samples. 0 when untrimmed.
    int64_t window_offset_samples = 0;

    if (output_format == "wav") {
        const TrimSourceWindow trim_window = resolve_trim_source_window(
            req.has_trim_begin, req.trim_begin_sec,
            req.has_trim_end, req.trim_end_sec,
            sample_rate, total_frames, N_fft);
        const int64_t trim_begin_src = trim_window.trim_begin_src;
        const int64_t trim_end_src = trim_window.trim_end_src;
        profile_trim_begin_frame = trim_begin_src;
        profile_trim_end_frame = trim_end_src;
        profile_trim_span_frames = trim_end_src - trim_begin_src;
        // See resolve_trim_source_window for the frame-0 load invariant.
        std::vector<float> src_samples;
        int src_sr = 0;
        int src_ch = 0;
        {
            // Reusing GuiAudio's in-memory samples was evaluated and rejected:
            // it saves only cache-read milliseconds per dispatch while creating
            // cross-thread lifetime coupling between the GUI audio object and
            // render worker. This self-contained read stays independent of file
            // load and revert timing.
            const auto t_source_load_0 = profile::now();
            auto source_read_result = load_source_range_with_source_sample_cache(
                req.source_audio_path, src_info,
                trim_window.load_begin_frame, trim_window.load_end_frame,
                src_samples, src_sr, src_ch);
            if (!source_read_result) {
                std::fprintf(stderr, "warptempo_gui: render error: %s\n",
                             source_read_result.error().c_str());
                cleanup_all();
                return RenderOutcome::Failed;
            }
            if (prof) {
                const auto t_source_load_1 = profile::now();
                const unsigned long long bytes =
                    static_cast<unsigned long long>(src_samples.size()) *
                    static_cast<unsigned long long>(sizeof(float));
                source_read_ms = profile::ms(t_source_load_0, t_source_load_1);
                profile_source_frames_passed = (src_ch > 0)
                    ? src_samples.size() / static_cast<size_t>(src_ch) : 0;
                profile_source_channels = src_ch;
                profile_source_sample_rate = src_sr;
                const bool used_cache = source_read_result->used_cache;
                std::fprintf(stderr,
                    "[profile] source_read ms=%.3f source_kind=%s cache_status=%s source_frames_passed=%zu trim_span_frames=%lld approx_mb=%.1f channels=%d sample_rate=%d\n",
                    source_read_ms,
                    used_cache ? "source_sample_cache" : "path",
                    source_sample_cache_status_name(source_read_result->cache_status),
                    profile_source_frames_passed,
                    static_cast<long long>(profile_trim_span_frames),
                    profile::bytes_to_mb(bytes), src_ch, src_sr);
            }
        }

        // Global limiter toggle. When on, every path (disk trimmed/untrimmed and
        // the target-view buffer) gets the spectral(-0.3) + peak(0) chain; when
        // off, no limiter anywhere and disk output is clean 32-bit float.

        EngineParams ep;
        ep.source_audio_samples = src_samples.data();
        ep.source_audio_frames  =
            src_samples.size() / static_cast<size_t>(src_ch);
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
        ep.N                    = N_fft;
        ep.limiter              = req.engine_settings.limiter;
        const int64_t render_target_frames = assign_engine_phase_resets(
            ep, req.phase_reset_frames, tmfull.frame_map, window_offset_samples,
            N_fft, sample_rate, "warptempo_gui");
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

        // Atomic publish: staging → final. Buffer path skips this — the
        // synthesised audio already landed in *req.output_buffer.
        if (!req.output_buffer) {
            std::error_code ec;
            std::filesystem::rename(staging_output_path, final_output_path, ec);
            if (ec) {
                std::fprintf(stderr,
                    "warptempo_gui: render error: rename '%s' -> '%s' failed: %s\n",
                    staging_output_path.c_str(), final_output_path.c_str(),
                    ec.message().c_str());
                cleanup_all();
                return RenderOutcome::Failed;
            }
            if (!fingerprint.empty() &&
                !write_fingerprint_sidecar(final_output_path, fingerprint)) {
                std::fprintf(stderr,
                    "[warptempo_gui] fingerprint sidecar write skipped for %s\n",
                    final_output_path.c_str());
            }
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

    // Deposit a peak-pyramid sidecar next to the rendered wav. Fire-and-forget;
    // the function logs its own errors and never affects render success.
    // Only meaningful when there's a rendered wav on disk — buffer-output
    // renders have no on-disk artifact to pyramid against.
    if (output_format == "wav" && !req.output_buffer) {
        write_peaks_cache_for_wav(final_output_path);
    }

    // Batch render: capture the per-render marker + phase reset sidecars now
    // that the wav rename has succeeded. These are the markers and
    // phase resets THIS render was produced from, not snapshots of the
    // current source authoring state — render-view loads them later to
    // display alongside the rendered audio. Sidecar write failures are
    // logged but never abort: the wav itself is the primary artifact.
    // Batch sidecars (.warpmarkers / .phaseresetmarkers / .rendersettings /
    // .renderwarpmarkers / .renderphaseresetmarkers) are tied to an on-disk
    // rendered wav. The buffer-output path has no such artifact, so skip
    // sidecar emission entirely on that path.
    if (batch_render && !req.output_buffer) {
        const std::filesystem::path bf(req.batch_folder);
        const std::string wm_path =
            (bf / (req.batch_basename + ".warpmarkers")).string();
        if (!GuiWarpMarkers::save(wm_path, req.markers)) {
            std::fprintf(stderr,
                "warptempo_gui: render warning: failed to write '%s'\n",
                wm_path.c_str());
        }
        if (!req.phase_resets.empty()) {
            const std::string tm_path =
                (bf / (req.batch_basename + ".phaseresetmarkers")).string();
            if (!GuiPhaseResetMarkers::save(tm_path, req.phase_resets)) {
                std::fprintf(stderr,
                    "warptempo_gui: render warning: failed to write '%s'\n",
                    tm_path.c_str());
            }
        }
        // `.rendersettings` sidecar: eight canonical engine keys (engine
        // block, byte-identical to the engine block of a Ctrl+S
        // `.settings` write) followed by the three view-state keys
        // (viewport_start, zoom, playhead) at their natural "user has
        // not yet viewed this render" defaults — render-view rewrites
        // the view-state block on first nav. Only Ctrl+Alt+C inside
        // a BPM batch folder reads the engine block back into
        // app.engine_settings (and even then, only scale); the other
        // batch modes write it as archival documentation.
        const std::filesystem::path rs_path =
            bf / (req.batch_basename + ".rendersettings");
        if (!write_rendersettings(rs_path, req.engine_settings,
                                  /*viewport_start=*/0,
                                  /*zoom_level=*/kFitFileLevel,
                                  /*playhead=*/0)) {
            std::fprintf(stderr,
                "warptempo_gui: render warning: failed to write '%s'\n",
                rs_path.string().c_str());
        }

        // Render-domain sidecars (.renderwarpmarkers / .renderphaseresetmarkers).
        // Render-view loads these instead of the source-domain pair so
        // visible marker positions match the rendered audio's time axis.
        // The source-domain pair above stays authoritative for
        // Ctrl+Alt+C commit and Ctrl+S authoring saves; the render-domain
        // pair is display-only and never read back into authoring memory.
        // Only wav renders produce these — frame-map/tempo-map formats skip
        // the engine.
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
                const int64_t sf_abs = static_cast<int64_t>(
                    std::nearbyint(g.time_seconds * sr_d));
                if (sf_abs < trim_begin || sf_abs > trim_end) continue;

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
                    "warptempo_gui: render warning: failed to write '%s'\n",
                    wmd_path.c_str());
            }

            std::vector<FrameMapSegment> real_map(real.begin, real.end);

            // Phase resets: forward-map each reset's clicked source frame
            // through the same map and place it at tgt(F) - window_offset, so
            // a reset sits on the same musical position in render-view as in
            // source and target views. Drop out-of-trim and disabled.
            if (!req.phase_resets.empty()) {
                std::vector<GuiPhaseResetMarker> warped_phase_resets;
                warped_phase_resets.reserve(req.phase_resets.size());
                for (const auto& t : req.phase_resets) {
                    if (t.disabled) continue;
                    const int64_t sf_abs = static_cast<int64_t>(
                        std::nearbyint(t.time_seconds * sr_d));
                    if (sf_abs < trim_begin || sf_abs > trim_end) continue;
                    const int64_t render_frame =
                        static_cast<int64_t>(std::llrint(map_source_to_target(
                            static_cast<double>(sf_abs), real_map))) -
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
                        "warptempo_gui: render warning: failed to write '%s'\n",
                        tmd_path.c_str());
                }
            }
        }
    }

    cleanup_all();
    return finish_success("success");
}
