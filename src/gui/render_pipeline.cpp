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

std::filesystem::path compose_sibling_output_path(
    const std::string& source_audio_path,
    const EngineSettings& es) {
    const std::string ext =
        (es.output_format == "timemap")  ? ".timemap" :
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

RenderOutcome do_render(const RenderRequest& req,
                        const std::atomic<bool>* cancel_flag) {
    if (req.source_audio_path.empty()) return RenderOutcome::Failed;

    // --- Read settings (typed; the live app.engine_settings is mutated
    // through strict-validated authoring paths, so every field is in
    // range by construction here). ---
    const std::string& output_format = req.engine_settings.output_format;
    const double scale               = req.engine_settings.scale;
    const int    N_fft               = req.engine_settings.N;
    const double phase_reset_offset_hops_mult = req.engine_settings.phase_reset_offset_hops;
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

    // Full (untrimmed) timemap for the wav engine path. A trimmed wav render
    // hands the engine the canonical untrimmed frame map and windows it,
    // rather than rebuilding a local trimmed map; that inherited t_a history
    // from frame 0 is what makes the windowed render null against the full
    // render. build_timemaps already produces the untrimmed segment list when
    // trim is forced off, so reuse it instead of duplicating the timemap math.
    // tmres stays the source for the timemap/tempomap else-branch (which keeps
    // its trimmed timemap + -trimmed.wav sibling); tmfull feeds the engine and
    // the render-domain sidecar block. Declared here (not inside the wav
    // branch) so the sidecar block outside the branch can read it.
    TimemapBuildInput tmin_full = tmin;
    tmin_full.has_trim_begin = false;
    tmin_full.trim_begin_sec = 0.0;
    tmin_full.has_trim_end   = false;
    tmin_full.trim_end_sec   = 0.0;
    TimemapBuildResult tmfull;
    if (!build_timemaps(tmin_full, tmfull)) {
        std::fprintf(stderr,
            "warptempo_gui: render error: full timemap build failed\n");
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

    std::fprintf(stderr, "warptempo_gui: rendering %s -> %s\n",
                 output_format.c_str(), final_output_path.c_str());

    // Populated by the wav (warptempo engine) path for render-domain phase
    // reset sidecar generation. timemap/tempomap paths leave these empty.
    std::vector<int64_t> engine_frame_map;
    int engine_R_s = 0;
    // Window origin resolved by the engine from the trim bounds (frame index
    // of the windowed render's first emitted frame). Sub-brief 3 uses it with
    // the now-full engine_frame_map to place render-domain sidecars on the
    // windowed time axis; this brief just captures it.
    int engine_synth_frame_begin = 0;

    if (output_format == "wav") {
        // Absolute source-frame trim bounds. Full-timemap path: the engine
        // resolves the synthesis window by binary search over the full frame
        // map, so these are absolute source frames (nearbyint per the
        // architecture rounding rule — matches timemap.cpp's own trim cut).
        // When a bound is unset it defaults to the full extent (0 / total),
        // which leaves the engine rendering the whole map.
        const int64_t trim_begin_src = req.has_trim_begin
            ? static_cast<int64_t>(std::nearbyint(
                  req.trim_begin_sec * static_cast<double>(sample_rate)))
            : 0;
        const int64_t trim_end_src = req.has_trim_end
            ? static_cast<int64_t>(std::nearbyint(
                  req.trim_end_sec * static_cast<double>(sample_rate)))
            : static_cast<int64_t>(total_frames);

        // Load the source from frame 0 to the end-trim point (plus margin),
        // NOT a begin-trimmed slice. The begin MUST stay at 0: the frame map's
        // t_a accumulation runs from frame 0, and that inherited history is the
        // entire reason the windowed render nulls against the full render. The
        // end is end-capped because no frame in the window reads source past
        // trim_end except the last analysis window's small reach — covered by
        // end_margin (one analysis hop, which grows with the stretch, plus the
        // N-sample window; 2*N covers realistic stretches). An undersized
        // margin only zero-pads the trailing edge — never a crash.
        std::vector<float> src_samples;
        int src_sr = 0;
        int src_ch = 0;
        {
            const int64_t end_margin = 2LL * static_cast<int64_t>(N_fft);
            const size_t b = 0;
            const size_t e = req.has_trim_end
                ? static_cast<size_t>(std::min<int64_t>(
                      total_frames, trim_end_src + end_margin))
                : static_cast<size_t>(total_frames);
            if (!load_source_range_to_buffer(req.source_audio_path, b, e,
                                             src_samples, src_sr, src_ch)) {
                cleanup_all();
                return RenderOutcome::Failed;
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
        // Full untrimmed timemap: the engine builds the whole frame map and
        // synthesizes only the windowed frames (set via ep.has_trim below),
        // so the windowed source reads match the full render frame-for-frame.
        ep.timemap.reserve(tmfull.standard.size());
        for (const auto& s : tmfull.standard) {
            ep.timemap.emplace_back(s.src_frame, s.tgt_frame);
        }
        ep.N                    = N_fft;
        ep.limiter              = req.engine_settings.limiter;
        ep.limiter_diag         = false;
        // Absolute source-frame domain. On the full-timemap path the engine
        // resolves resets by binary search over the full frame map, so the
        // reset list is in absolute source frames regardless of trim — exactly
        // like the untrimmed branch always did. No trim re-basing.
        ep.phase_reset_frames.reserve(req.phase_reset_frames.size());
        for (size_t i = 0; i < req.phase_reset_frames.size(); ++i) {
            const int64_t F = req.phase_reset_frames[i];
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

        // Synthesis frame window from the trim bounds. When neither bound is
        // set, has_trim is false and the engine renders the whole map
        // (unchanged untrimmed behavior). When set, the engine narrows
        // synthesis to the frames covering [trim_begin_src, trim_end_src] out
        // of the full map.
        ep.has_trim       = req.has_trim_begin || req.has_trim_end;
        ep.trim_begin_src = trim_begin_src;
        ep.trim_end_src   = trim_end_src;

        auto handle_eng = [&](EngineResult r) -> RenderOutcome {
            if (r == EngineResult::Success)   return RenderOutcome::Success;
            cleanup_all();
            return (r == EngineResult::Cancelled)
                ? RenderOutcome::Cancelled
                : RenderOutcome::Failed;
        };

        const EngineResult er = run_warptempo_engine(
            ep, &engine_frame_map, &engine_R_s, &engine_synth_frame_begin,
            cancel_flag);
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
            // Walk the FULL timemap (no synthetic trim anchors); each emitted
            // render-domain time is the full-render output sample minus the
            // window origin. When untrimmed, engine_synth_frame_begin is 0 so
            // window_offset_samples is 0 and this reduces to old behavior.
            const TimemapRealRange real = real_segments(tmfull);
            const int64_t trim_begin =
                static_cast<int64_t>(tmres.trim_begin_frame);
            const int64_t trim_end = tmres.trimmed
                ? static_cast<int64_t>(tmres.trim_end_frame)
                : static_cast<int64_t>(total_frames);
            const double sr_d = static_cast<double>(sample_rate);
            const int64_t window_offset_samples =
                static_cast<int64_t>(engine_synth_frame_begin) *
                static_cast<int64_t>(engine_R_s);

            // After the head-alignment brief, markers sit on the aligned
            // feature: a marker at source F displays at tgt(F) - window_offset
            // with no start-trim term.

            // Markers: lockstep walk between req.markers and the real-segment
            // range of tmfull.standard (built trim-off, so no synthetic trim
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
                // post-pass at timemap.cpp). Gates emission only; the segment
                // above is already consumed.
                const int64_t sf_abs = static_cast<int64_t>(
                    std::nearbyint(g.time_seconds * sr_d));
                if (sf_abs < trim_begin || sf_abs > trim_end) continue;

                GuiWarpMarker w = g;
                w.time_seconds  =
                    (static_cast<double>(s.tgt_frame) - window_offset_samples)
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

            // Forward-map a source frame to its target frame via the same
            // piecewise-linear timemap the warp markers use, so a phase-reset
            // marker displays at the clicked musical position -- identical
            // convention to source and target views. The engine still fires the
            // reset at F - phase_reset_offset_samples (the dispatch offset is an
            // engine mechanic and is deliberately NOT applied to the displayed
            // marker).
            auto src_to_tgt = [&](int64_t sf) -> double {
                if (real.begin == real.end) return static_cast<double>(sf);
                if (sf <= static_cast<int64_t>(real.begin->src_frame))
                    return static_cast<double>(real.begin->tgt_frame);
                for (auto it = real.begin; it + 1 != real.end; ++it) {
                    const auto& a = *it;
                    const auto& b = *(it + 1);
                    if (sf >= static_cast<int64_t>(a.src_frame) &&
                        sf <  static_cast<int64_t>(b.src_frame)) {
                        const double sd =
                            static_cast<double>(b.src_frame - a.src_frame);
                        const double td =
                            static_cast<double>(b.tgt_frame - a.tgt_frame);
                        const double off =
                            static_cast<double>(sf - static_cast<int64_t>(a.src_frame));
                        return static_cast<double>(a.tgt_frame) + off * (td / sd);
                    }
                }
                const auto& last = *(real.end - 1);
                return static_cast<double>(last.tgt_frame) +
                       static_cast<double>(sf - static_cast<int64_t>(last.src_frame));
            };

            // Phase resets: forward-map each reset's clicked source frame
            // through src_to_tgt and place it at tgt(F) - window_offset, so a
            // reset sits on the same musical position in render-view as in
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
                        static_cast<int64_t>(std::llround(src_to_tgt(sf_abs))) -
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
    std::fprintf(stderr, "warptempo_gui: render complete: %s\n",
                 req.output_buffer ? "<buffer>" : final_output_path.c_str());
    return RenderOutcome::Success;
}

