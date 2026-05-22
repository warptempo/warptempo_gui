#include "file_loader.h"

#include "settings_io.h"
#include "target_render.h"
#include "timemap.h"
#include "waveform_worker.h"

#include "engine/stft_container.h"

#include <sndfile.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

bool GuiFileLoader::load_file(const std::string& path) {
    // Preflight.
    {
        SF_INFO probe_info;
        std::memset(&probe_info, 0, sizeof(probe_info));
        SNDFILE* probe = sf_open(path.c_str(), SFM_READ, &probe_info);
        if (!probe) {
            std::fprintf(stderr,
                         "warptempo_gui: '%s': %s\n",
                         path.c_str(), sf_strerror(nullptr));
            return false;
        }
        sf_close(probe);
    }

    // Stop and tear down the audio device before the sample buffer it
    // borrows is replaced. Playing into a freed buffer would crash the
    // audio thread. Order (stop → shutdown → load → init) is fixed.
    playback.stop();
    playback.shutdown();
    app.playhead_scanner_active     = false;
    app.playhead_scanner_sample = 0;
    app.hover_popup    = HoverPopupState{};

    app.loading       = true;
    app.load_progress = 0.0f;
    gui.invalidate_region(0, 0, app.width, app.height);
    gui.drain_events();

    GuiAudio next;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = next.load(path, [&](float p) {
        app.load_progress = p;
        const int bar_y = app.height - kProgressBarHeight;
        gui.invalidate_region(0, bar_y, app.width, kProgressBarHeight);
        gui.drain_events();
    });
    const auto t1 = std::chrono::steady_clock::now();

    if (!ok) {
        app.loading       = false;
        app.load_progress = 0.0f;
        gui.invalidate_region(0, 0, app.width, app.height);
        return false;
    }

    // Stage A: drain any in-flight waveform render before swapping the
    // audio's internal buffers out from under it. The worker dereferences
    // the GuiAudio via the pointer captured in its WaveformJob, and the
    // move-assignment below replaces audio's pyramid vectors in place —
    // reading them mid-move is UB.
    waveform_worker.wait_until_idle();

    audio = std::move(next);
    app.audio_generation++;
    app.loading       = false;
    app.load_progress = 0.0f;

    // The just-loaded audio sets a fresh source-frame baseline; the
    // target-frame cache from any prior session is stale. The target
    // buffer is tied to the live source audio's content, so cancel any
    // in-flight target render and clear it — the eager ensure_ready()
    // at end-of-load will dispatch a fresh target render if the parsed
    // .settings landed us in target view.
    target_render.cancel_for_load();
    app.target_view_total_frames = 0;

    app.playhead_cursor_sample       = 0;
    app.viewport_start_sample = 0;
    const int max_num = max_valid_numeric_level(
        waveform_area(app).w, audio.total_frames(), audio.sample_rate());
    app.zoom_level = (max_num >= 0) ? kMinNumericLevel : kFitFileLevel;
    clamp_viewport_start(app, audio);

    // Reset playback bookkeeping; the device is brought up after markers
    // are parsed so the initial playhead has the final trim-begin.
    app.playback_speed = 1.0f;

    // Companion files: discover paths, create <basename>.warpmarkers
    // and <basename>.settings if missing. Companion file convention is
    // <source_dir>/<source_basename>.<ext> (sibling, basename-prefixed),
    // not the legacy hidden `./.warpmarkers` form.
    std::filesystem::path apath(path);
    std::filesystem::path parent = apath.parent_path();
    if (parent.empty()) parent = std::filesystem::path(".");
    const std::string stem = apath.stem().string();
    const std::filesystem::path wm_path  = parent / (stem + ".warpmarkers");
    const std::filesystem::path tm_path  = parent / (stem + ".phaseresetmarkers");
    const std::filesystem::path set_path = parent / (stem + ".settings");
    app.warpmarkers_path      = wm_path.string();
    app.phase_reset_markers_path = tm_path.string();
    app.settings_path         = set_path.string();
    app.source_audio_path     = path;

    create_if_missing(wm_path, "00:00.000|1.00\n");
    create_if_missing(set_path, format_default_settings_template(stem));

    // Load the markers file. Parse failures are non-fatal: we log each
    // error to stderr and leave app.warpmarkers empty. The GUI still works
    // as a waveform viewer.
    app.warpmarkers.clear();
    app.phase_reset_markers.clear();
    app.selected_markers.clear();
    app.last_selected_marker = -1;
    app.active_markers_view    = 'W';
    app.drag = DragState{};
    app.playhead_drag = PlayheadDragState{};
    // Fresh file = fresh history. Both stacks cleared; the loaded state
    // is the saved baseline (signed_distance = 0, valid).
    app.history.reset();
    app.dirty              = false;
    app.warp_dirty         = false;
    app.phase_reset_dirty    = false;
    app.settings_dirty     = false;
    app.first_save_pending = true;
    const bool markers_ok = app.warpmarkers.load(wm_path.string());
    if (!markers_ok) {
        for (const auto& err : app.warpmarkers.errors()) {
            if (err.line_number > 0) {
                std::fprintf(stderr,
                             "warptempo_gui: %s:%d: %s\n",
                             wm_path.string().c_str(),
                             err.line_number, err.message.c_str());
            } else {
                std::fprintf(stderr,
                             "warptempo_gui: %s: %s\n",
                             wm_path.string().c_str(),
                             err.message.c_str());
            }
        }
    } else {
        std::fprintf(stderr,
                     "[warptempo_gui] parsed %zu markers from %s\n",
                     app.warpmarkers.markers().size(),
                     wm_path.string().c_str());
    }

    // Load .phaseresetmarkers if present. Missing file is fine — the
    // phase reset list is just empty. Parse errors are logged to stderr;
    // the warp side stays usable regardless.
    if (std::filesystem::exists(tm_path)) {
        const bool tr_ok = app.phase_reset_markers.load(tm_path.string());
        if (!tr_ok) {
            for (const auto& err : app.phase_reset_markers.errors()) {
                if (err.line_number > 0) {
                    std::fprintf(stderr,
                                 "warptempo_gui: %s:%d: %s\n",
                                 tm_path.string().c_str(),
                                 err.line_number, err.message.c_str());
                } else {
                    std::fprintf(stderr,
                                 "warptempo_gui: %s: %s\n",
                                 tm_path.string().c_str(),
                                 err.message.c_str());
                }
            }
        } else {
            std::fprintf(stderr,
                         "[warptempo_gui] parsed %zu phase_resets from %s\n",
                         app.phase_reset_markers.markers().size(),
                         tm_path.string().c_str());
        }
    }

    // Initial playhead: land at trim-begin if a b= marker was parsed,
    // otherwise sample 0. Must happen after marker parse so the trim
    // range reflects the on-disk state. Scroll the viewport so the
    // playhead is visible rather than lurking off the left edge.
    app.playhead_cursor_sample = viewport.trim_begin_sample();
    if (app.zoom_level != kFitFileLevel) {
        app.viewport_start_sample = app.playhead_cursor_sample;
        clamp_viewport_start(app, audio);
    }

    // Seed both tabs with the freshly-computed default post-load state.
    // Parsed .settings values overwrite per-key below.
    ViewState default_tab;
    default_tab.viewport_start_sample = app.viewport_start_sample;
    default_tab.zoom_level            = app.zoom_level;
    default_tab.playhead_cursor_sample       = app.playhead_cursor_sample;
    app.tab_a          = default_tab;
    app.tab_b          = default_tab;
    app.engine_settings = EngineSettings{};

    // Parse .settings (if present) and apply tab values with silent
    // coerce on out-of-range. Missing file → all keys default.
    {
        ParsedSettings ps;
        if (!parse_settings_file(app.settings_path, ps)) {
            std::fprintf(stderr,
                "warptempo_gui: could not read '%s'\n",
                app.settings_path.c_str());
        }
        const int64_t total = audio.total_frames();
        auto valid_zoom = [](int z) -> bool {
            if (z == kFitFileLevel) return true;
            return z >= kMinNumericLevel && z <= kMaxNumericLevel;
        };
        auto apply = [&](bool has_vp, int64_t vp,
                         bool has_zoom, int zoom,
                         bool has_ph, int64_t ph,
                         ViewState& dst) {
            if (has_vp   && vp   >= 0 && vp   <  total)  dst.viewport_start_sample = vp;
            if (has_zoom && valid_zoom(zoom))            dst.zoom_level            = zoom;
            if (has_ph   && ph   >= 0 && ph   <= total)  dst.playhead_cursor_sample       = ph;
        };
        apply(ps.has_tab_a_vp, ps.tab_a_vp,
              ps.has_tab_a_zoom, ps.tab_a_zoom,
              ps.has_tab_a_ph, ps.tab_a_ph, app.tab_a);
        apply(ps.has_tab_b_vp, ps.tab_b_vp,
              ps.has_tab_b_zoom, ps.tab_b_zoom,
              ps.has_tab_b_ph, ps.tab_b_ph, app.tab_b);
        app.follow_mode    = ps.has_follow         ? ps.follow         : true;
        app.active_audio_view   = ps.has_active_audio_view   ? ps.active_audio_view   : 'S';
        app.active_markers_view = ps.has_active_markers_view ? ps.active_markers_view : 'W';
        app.active_tab_view     = ps.has_active_tab_view     ? ps.active_tab_view     : 'A';
        app.playback_speed = ps.has_playback_speed ? ps.playback_speed : 1.0f;
        // Trim: per-tab keys take precedence; the legacy singleton form,
        // when present without any per-tab keys, applies to tab_a only
        // (tab_b stays at default). When both are present, per-tab wins
        // — settings_io.cpp evaluates the typed-key branches in the order
        // listed so the per-tab branch shadows the legacy one for the same
        // ParsedSettings field.
        if (ps.has_tab_a_trim_begin) {
            app.tab_a.has_trim_begin     = true;
            app.tab_a.trim_begin_seconds = ps.tab_a_trim_begin;
        } else if (ps.has_trim_begin) {
            app.tab_a.has_trim_begin     = true;
            app.tab_a.trim_begin_seconds = ps.trim_begin;
        }
        if (ps.has_tab_a_trim_end) {
            app.tab_a.has_trim_end       = true;
            app.tab_a.trim_end_seconds   = ps.tab_a_trim_end;
        } else if (ps.has_trim_end) {
            app.tab_a.has_trim_end       = true;
            app.tab_a.trim_end_seconds   = ps.trim_end;
        }
        if (ps.has_tab_b_trim_begin) {
            app.tab_b.has_trim_begin     = true;
            app.tab_b.trim_begin_seconds = ps.tab_b_trim_begin;
        }
        if (ps.has_tab_b_trim_end) {
            app.tab_b.has_trim_end       = true;
            app.tab_b.trim_end_seconds   = ps.tab_b_trim_end;
        }
        if (ps.has_tab_a_read_only) app.tab_a.read_only = ps.tab_a_read_only;
        if (ps.has_tab_b_read_only) app.tab_b.read_only = ps.tab_b_read_only;
    }

    // Strict engine-settings deserialization. Any violation (unknown key,
    // duplicate, parse failure, missing required key) fails the load with
    // every reason logged. Treat like a corrupt audio file: revert the
    // partial load and return false so the user sees no half-loaded state.
    {
        auto es = read_engine_settings_from_file(app.settings_path);
        if (!es) {
            std::fprintf(stderr,
                "warptempo_gui: source load aborted: invalid engine "
                "settings in '%s'\n",
                app.settings_path.c_str());
            revert_to_blank();
            return false;
        }
        app.engine_settings = std::move(*es);
    }

    // If the parsed .settings landed us in target view, populate the
    // target-domain total-frame cache from the timemap before the
    // viewport clamp below reads live_total_frames. The cache is
    // otherwise only written by the S→T toggle handler; on a file that
    // *opens* in target view, that toggle never runs, so without this
    // step the clamp + first paint use the source length and the
    // waveform renders short of the target length. The target *length*
    // is a pure timemap computation — it does NOT require the audio
    // render to finish. Mirrors the S→T toggle's computation exactly
    // (trim-agnostic: full source frames in, fall back to source length
    // if the timemap is degenerate).
    if (app.active_audio_view == 'T') {
        const auto tmap = build_target_view_timemap(
            app, audio.sample_rate(),
            static_cast<long>(audio.total_frames()));
        const int64_t src_total = audio.total_frames();
        const double tgt_total_d = map_source_to_target(
            static_cast<size_t>(src_total < 0 ? 0 : src_total), tmap);
        const int64_t tgt_total =
            static_cast<int64_t>(std::nearbyint(tgt_total_d));
        app.target_view_total_frames = tgt_total > 0 ? tgt_total : src_total;
    }

    // Activate the parsed-tab: copy its snapshot into the live AppState
    // fields. active_tab_view was set from the parsed-settings block above.
    {
        const ViewState& parsed_tab = (app.active_tab_view == 'B')
                                      ? app.tab_b : app.tab_a;
        app.viewport_start_sample = parsed_tab.viewport_start_sample;
        app.zoom_level            = parsed_tab.zoom_level;
        app.playhead_cursor_sample       = parsed_tab.playhead_cursor_sample;
        clamp_viewport_start(app, audio);
    }

    // Bring up the audio device bound to the new sample buffer. Init
    // failure disables playback but leaves the rest of the GUI usable.
    if (!playback.init(audio.sample_rate(), audio.channels(),
                       audio.samples_ptr(), audio.total_frames())) {
        std::fprintf(stderr,
            "warptempo_gui: playback disabled; space bar will no-op.\n");
    }
    // Push the loaded speed to the engine so playback starts at the
    // persisted rate rather than the engine's default 1.0.
    playback.set_speed(app.playback_speed);

    const double load_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::fprintf(stderr,
                 "[warptempo_gui] loaded %s: sr=%d, channels=%d, frames=%lld, "
                 "pyramid_levels=%d, load_time=%.1f ms\n",
                 path.c_str(), audio.sample_rate(), audio.channels(),
                 static_cast<long long>(audio.total_frames()),
                 audio.num_levels(), load_ms);

    // If the parsed settings landed us in target view, dispatch a fresh
    // target render now so the first Space press is ready without a
    // first-edit wait. The target buffer was cleared by cancel_for_load
    // above; ensure_ready's is_dirty_=true falls through to trigger().
    // No-op if active_audio_view=='S'.
    target_render.ensure_ready();

    gui.invalidate_region(0, 0, app.width, app.height);
    return true;
}

void GuiFileLoader::revert_to_blank() {
    // Stop the audio thread before the sample buffer it borrows goes
    // away. Same invariant as load_file.
    playback.stop();
    playback.shutdown();
    app.playhead_scanner_active      = false;
    app.playhead_scanner_sample = 0;

    // Stage A: drain the waveform worker before discarding the audio.
    // Same invariant as load_file — the worker holds a pointer into
    // `audio`, and `audio = GuiAudio{}` will replace its internals.
    waveform_worker.wait_until_idle();

    audio = GuiAudio{};
    app.audio_generation++;
    wf_cache.destroy_surface();
    // Stage B: stem cache mirrors the waveform cache's lifecycle. Safe
    // to destroy on the main thread; the rebuild path is fully
    // synchronous so no in-flight work to drain.
    stem_cache.destroy_surface();
    // Stage C: flag cache mirrors the same lifecycle as the stem cache.
    flag_cache.destroy_surface();

    app.playhead_cursor_sample  = 0;
    app.viewport_start_sample   = 0;
    app.zoom_level              = 0;
    app.follow_mode             = true;
    app.playhead_scanner_sample = 0;
    app.playback_speed          = 1.0f;

    app.warpmarkers.clear();
    app.phase_reset_markers.clear();
    app.selected_markers.clear();
    app.last_selected_marker = -1;
    app.drag          = DragState{};
    app.playhead_drag = PlayheadDragState{};
    app.hover_popup   = HoverPopupState{};
    app.history.reset();
    app.dirty              = false;
    app.warp_dirty         = false;
    app.phase_reset_dirty    = false;
    app.settings_dirty     = false;
    app.first_save_pending = true;

    app.warpmarkers_path.clear();
    app.phase_reset_markers_path.clear();
    app.settings_path.clear();
    app.source_audio_path.clear();
    app.pending_drop_path.clear();
    app.engine_settings = EngineSettings{};

    app.tab_a = ViewState{};
    app.tab_b = ViewState{};

    // View-selector triplet defaults, in bottom-bar order [S/T] [W/P] [A/B].
    app.active_audio_view   = 'S';
    app.active_markers_view = 'W';
    app.active_tab_view     = 'A';

    // Target render state is tied to the live source audio; cancel any
    // in-flight target render and clear it on revert so a subsequent
    // file load doesn't inherit stale frames.
    target_render.cancel_for_load();
    app.target_view_total_frames = 0;

    viewport.invalidate_all();
}

// Process `path` and any drops that arrived while the load was running.
// Pending slot is last-wins, not a queue; rapid drags collapse.
void GuiFileLoader::load_then_drain(std::string path) {
    while (true) {
        load_file(path);
        if (app.pending_drop_path.empty()) break;
        path = std::move(app.pending_drop_path);
        app.pending_drop_path.clear();
    }
}
