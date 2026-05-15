#include "render_pipeline.h"

#include "engine/engine.h"
#include "app_state.h"
#include "audio.h"
#include "render.h"
#include "phase_reset_markers.h"
#include "settings_io.h"
#include "timemap.h"

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

bool write_standard_timemap(const std::string& path,
                            const std::vector<TimemapSegment>& segs,
                            bool drop_zero_zero) {
    std::ofstream of(path);
    if (!of) {
        std::fprintf(stderr,
            "warptempo_gui: render error: could not write timemap '%s'\n",
            path.c_str());
        return false;
    }
    for (const auto& s : segs) {
        if (drop_zero_zero && s.src_frame == 0 && s.tgt_frame == 0) continue;
        of << s.src_frame << " " << s.tgt_frame << "\n";
    }
    return true;
}

bool write_midi_tempomap(const std::string& path,
                         const std::vector<TempomapEntry>& entries) {
    std::ofstream of(path);
    if (!of) {
        std::fprintf(stderr,
            "warptempo_gui: render error: could not write tempomap '%s'\n",
            path.c_str());
        return false;
    }
    of << std::fixed << std::setprecision(16);
    for (const auto& e : entries) {
        of << e.target_time_sec << " " << e.multiplier << "\n";
    }
    return true;
}

// resolve_markers_for_render moved to timemap.cpp (public function) so the
// target-view paint can reach it without crossing the render_pipeline
// boundary. Both callers — do_render below and the GUI paint in
// paint_handler — receive the same resolved list.

}  // namespace

RenderOutcome do_render(const RenderRequest& req,
                        const std::atomic<bool>* cancel_flag) {
    if (req.source_audio_path.empty()) return RenderOutcome::Failed;

    // --- Read settings (typed; the live app.engine_settings is mutated
    // through strict-validated authoring paths, so every field is in
    // range by construction here). ---
    const std::string& title         = req.engine_settings.title;
    const std::string& output_format = req.engine_settings.output_format;
    const double scale               = req.engine_settings.scale;
    const bool   user_limiter_en     = req.engine_settings.limiter_enabled_on_render;
    const int    N_fft               = req.engine_settings.N;
    const int    fftw_threads        = req.engine_settings.fftw_threads;
    const double phase_reset_offset_hops_mult = req.engine_settings.phase_reset_offset_hops;
    const double limiter_ceiling     = req.engine_settings.limiter_ceiling;
    const double limiter_attack_ms   = req.engine_settings.limiter_attack_ms;
    const double limiter_release_ms  = req.engine_settings.limiter_release_ms;
    const int    R_s                 = N_fft / 4;
    const int64_t phase_reset_offset_samples = static_cast<int64_t>(
        std::nearbyint(phase_reset_offset_hops_mult *
                       static_cast<double>(R_s)));

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
    sf_close(sf);

    // --- Build timemap from in-memory markers. ---
    TimemapBuildInput tmin;
    tmin.markers        = resolve_markers_for_render(req.markers);
    tmin.scale          = scale;
    tmin.sample_rate    = sample_rate;
    tmin.total_frames   = total_frames;
    tmin.has_trim_begin = req.has_trim_begin;
    tmin.trim_begin_sec = req.trim_begin_sec;
    tmin.has_trim_end   = req.has_trim_end;
    tmin.trim_end_sec   = req.trim_end_sec;

    TimemapBuildResult tmres;
    if (!build_timemaps(tmin, tmres)) {
        std::fprintf(stderr, "warptempo_gui: render error: timemap build failed\n");
        return RenderOutcome::Failed;
    }

    // --- Compute output path. ---
    auto ext_for_format = [&]() -> std::string {
        if (output_format == "timemap")  return ".timemap";
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
        std::filesystem::path src(req.source_audio_path);
        std::filesystem::path dir = src.parent_path();
        if (dir.empty()) dir = std::filesystem::path(".");
        // Prefix marks a wav that's genuinely unlimited on disk. Only
        // applies to the wav path; timemap/tempomap outputs are text
        // files and don't run any limiter.
        const bool output_unlimited =
            output_format == "wav" && !user_limiter_en;
        std::string out_filename = output_unlimited
            ? ("limiter_enabled_on_render=false;" + title + ext_for_format())
            : (title + ext_for_format());
        final_output_path = (dir / out_filename).string();
    }
    // Staging path used by the wav engine path's atomic rename. Text-file
    // formats write final_output_path directly.
    const std::string staging_output_path = final_output_path + ".tmp";

    auto cleanup_all = [&]() {
        unlink_silent(staging_output_path);
    };

    std::fprintf(stderr, "warptempo_gui: rendering %s -> %s\n",
                 output_format.c_str(), final_output_path.c_str());

    // Populated by the wav (warptempo engine) path for render-domain phase
    // reset sidecar generation. timemap/tempomap paths leave these empty.
    std::vector<int64_t> engine_frame_map;
    int engine_R_s = 0;

    if (output_format == "wav") {
        // Load the source range to an in-memory buffer (full source when
        // no trim; [trim_begin, trim_end) when trimmed). The engine reads
        // from this buffer via libsndfile virtual IO — no wav-on-disk
        // shim for trim.
        std::vector<float> src_samples;
        int src_sr = 0;
        int src_ch = 0;
        {
            const size_t b = tmres.trimmed ? tmres.trim_begin_frame : 0;
            const size_t e = tmres.trimmed
                ? tmres.trim_end_frame
                : static_cast<size_t>(total_frames);
            if (!load_source_range_to_buffer(req.source_audio_path, b, e,
                                             src_samples, src_sr, src_ch)) {
                cleanup_all();
                return RenderOutcome::Failed;
            }
        }

        // Limiter routing. `limiter_enabled_on_render=false` means no
        // limiter anywhere. When true, trim state decides: no trim →
        // engine spectral limiter (frequency-domain, final-archival);
        // trim → engine peak limiter (time-domain, fast iteration).
        LimiterMode limiter_mode = LimiterMode::None;
        if (user_limiter_en) {
            limiter_mode = tmres.trimmed
                ? LimiterMode::Peak
                : LimiterMode::Spectral;
        }

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
        ep.timemap.reserve(tmres.standard.size());
        for (const auto& s : tmres.standard) {
            ep.timemap.emplace_back(s.src_frame, s.tgt_frame);
        }
        ep.N                    = N_fft;
        ep.fftw_threads         = fftw_threads;
        ep.limiter_mode         = limiter_mode;
        ep.limiter_ceiling_dbfs       = limiter_ceiling;
        ep.peak_limiter_ceiling_dbfs  = limiter_ceiling;
        ep.peak_limiter_attack_ms     = limiter_attack_ms;
        ep.peak_limiter_release_ms    = limiter_release_ms;
        ep.limiter_diag         = false;
        // Trim-relative source-frame domain. The engine receives a sliced
        // source buffer and a trim-shifted timemap, so phase_reset_frames
        // must live in the same trimmed-source domain as the rest of the
        // engine input. Drop predicates and domain match the
        // .renderphaseresetmarkers writer below so the on-disk
        // visualization and the engine-applied placement agree on which
        // phase resets are in scope.
        if (tmres.trimmed) {
            const int64_t trim_begin =
                static_cast<int64_t>(tmres.trim_begin_frame);
            const int64_t trim_end =
                static_cast<int64_t>(tmres.trim_end_frame);
            ep.phase_reset_frames.reserve(req.phase_reset_frames.size());
            for (int64_t F : req.phase_reset_frames) {
                if (F < trim_begin || F > trim_end) continue;
                int64_t engine_frame =
                    (F - trim_begin) - phase_reset_offset_samples;
                if (engine_frame < 0) {
                    std::fprintf(stderr,
                        "warptempo_gui: phase reset at %.3f s clamped to "
                        "engine frame 0 (offset shift would place it "
                        "before trim begin)\n",
                        static_cast<double>(F) /
                            static_cast<double>(sample_rate));
                    engine_frame = 0;
                }
                ep.phase_reset_frames.push_back(engine_frame);
            }
        } else {
            ep.phase_reset_frames.reserve(req.phase_reset_frames.size());
            for (int64_t F : req.phase_reset_frames) {
                int64_t engine_frame = F - phase_reset_offset_samples;
                if (engine_frame < 0) {
                    std::fprintf(stderr,
                        "warptempo_gui: phase reset at %.3f s clamped to "
                        "engine frame 0 (offset shift would place it "
                        "before audio start)\n",
                        static_cast<double>(F) /
                            static_cast<double>(sample_rate));
                    engine_frame = 0;
                }
                ep.phase_reset_frames.push_back(engine_frame);
            }
        }

        auto handle_eng = [&](EngineResult r) -> RenderOutcome {
            if (r == EngineResult::Success)   return RenderOutcome::Success;
            cleanup_all();
            return (r == EngineResult::Cancelled)
                ? RenderOutcome::Cancelled
                : RenderOutcome::Failed;
        };

        const EngineResult er = run_warptempo_engine(
            ep, &engine_frame_map, &engine_R_s, cancel_flag);
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
        }
    } else {
        // output_format == "timemap" or "tempomap". No engine, no limiter.
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
            if (!write_trimmed_wav(req.source_audio_path, trimmed_path,
                                   tmres.trim_begin_frame,
                                   tmres.trim_end_frame)) {
                cleanup_all();
                return RenderOutcome::Failed;
            }
        }
        const bool ok = (output_format == "timemap")
            ? write_standard_timemap(final_output_path, tmres.standard,
                                     /*drop_zero_zero=*/false)
            : write_midi_tempomap(final_output_path, tmres.midi);
        if (!ok) {
            cleanup_all();
            return RenderOutcome::Failed;
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
        // `.rendersettings` sidecar: ten canonical engine keys (engine
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
        // Only wav renders produce these — timemap/tempomap formats skip
        // the engine, so engine_frame_map and engine_R_s are unset.
        if (output_format == "wav" && !tmres.standard.empty() &&
            sample_rate > 0) {
            const auto& seg = tmres.standard;
            const int64_t trim_begin =
                static_cast<int64_t>(tmres.trim_begin_frame);
            const int64_t trim_end = tmres.trimmed
                ? static_cast<int64_t>(tmres.trim_end_frame)
                : static_cast<int64_t>(total_frames);
            const double sr_d = static_cast<double>(sample_rate);

            // Markers: lockstep walk between req.markers and tmres.standard.
            // Each surviving marker (post resolve filter + post trim filter)
            // pairs with the next-in-order surviving segment. seg.tgt_frame
            // is already post-shift (render-domain) so the render-time is
            // tgt_frame / sr directly.
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

            size_t seg_idx = 0;
            std::vector<GuiWarpMarker> warped_markers;
            warped_markers.reserve(req.markers.size());
            for (const auto& g : req.markers) {
                const bool eff_disabled =
                    g.disabled || is_cascade_disabled_ref(g);

                // resolve_markers_for_render filter.
                if (eff_disabled) continue;

                // Trim-range filter (inclusive both ends — matches the
                // post-pass at timemap.cpp line 209).
                const int64_t sf_abs = static_cast<int64_t>(
                    std::nearbyint(g.time_seconds * sr_d));
                if (sf_abs < trim_begin || sf_abs > trim_end) continue;

                if (seg_idx >= seg.size()) break;
                const auto& s = seg[seg_idx];
                ++seg_idx;

                GuiWarpMarker w = g;
                w.time_seconds  = static_cast<double>(s.tgt_frame) / sr_d;
                warped_markers.push_back(std::move(w));
            }
            const std::string wmd_path =
                (bf / (req.batch_basename + ".renderwarpmarkers")).string();
            if (!GuiWarpMarkers::save(wmd_path, warped_markers)) {
                std::fprintf(stderr,
                    "warptempo_gui: render warning: failed to write '%s'\n",
                    wmd_path.c_str());
            }

            // Phase resets: locate each phase reset's source frame in the
            // engine's frame_map via binary search and emit at synth_frame *
            // R_s — same placement convention the engine uses internally.
            // Drop out-of-trim and disabled. time_seconds on the emitted
            // marker is the engine-domain render_frame divided by sr.
            if (!req.phase_resets.empty()) {
                std::vector<GuiPhaseResetMarker> warped_phase_resets;
                warped_phase_resets.reserve(req.phase_resets.size());
                for (const auto& t : req.phase_resets) {
                    if (t.disabled) continue;
                    const int64_t sf_abs = static_cast<int64_t>(
                        std::nearbyint(t.time_seconds * sr_d));
                    if (sf_abs < trim_begin || sf_abs > trim_end) continue;
                    if (engine_frame_map.empty()) continue;
                    const int64_t sf_rel = sf_abs - trim_begin;
                    auto it = std::upper_bound(engine_frame_map.begin(),
                                               engine_frame_map.end(),
                                               sf_rel);
                    size_t m;
                    if (it == engine_frame_map.begin()) {
                        m = 0;
                    } else if (it == engine_frame_map.end()) {
                        m = engine_frame_map.size() - 1;
                    } else {
                        --it;
                        m = static_cast<size_t>(it - engine_frame_map.begin());
                    }
                    const int64_t render_frame =
                        static_cast<int64_t>(m) *
                        static_cast<int64_t>(engine_R_s);
                    GuiPhaseResetMarker w;
                    w.time_seconds = static_cast<double>(render_frame) / sr_d;
                    w.disabled     = false;
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
    std::fprintf(stderr, "warptempo_gui: render complete: %s\n",
                 req.output_buffer ? "<buffer>" : final_output_path.c_str());
    return RenderOutcome::Success;
}

