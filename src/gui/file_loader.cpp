#include "file_loader.h"

#include "settings_io.h"
#include "target_render.h"
#include "warp_frame_map_view.h"
#include "waveform_worker.h"
#include "source_sample_cache.h"

#include "warp_frame_map.h"

#include "audio_probe.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

GuiFileLoader::~GuiFileLoader() {
    join_source_sample_cache_writer();
}

void GuiFileLoader::join_source_sample_cache_writer() {
    if (source_sample_cache_writer_.joinable()) {
        source_sample_cache_writer_.join();
    }
}

bool GuiFileLoader::load_file(const std::string& path) {
    if (is_source_sample_cache_path(path)) {
        auto owner = source_path_for_source_sample_cache(path);
        if (!owner || is_source_sample_cache_path(*owner) ||
            is_peaks_cache_path(*owner)) {
            std::fprintf(stderr,
                "warptempo_gui: '%s' is a private source sample cache; "
                "open the original source audio file instead.\n",
                path.c_str());
            return false;
        }
        std::fprintf(stderr,
            "[warptempo_gui] redirecting private source sample cache %s to %s\n",
            path.c_str(), owner->c_str());
        return load_file(*owner);
    }
    if (is_peaks_cache_path(path)) {
        auto owner = source_path_for_peaks_cache(path);
        if (!owner || is_source_sample_cache_path(*owner) ||
            is_peaks_cache_path(*owner)) {
            std::fprintf(stderr,
                "warptempo_gui: '%s' is a waveform peaks cache; "
                "open the original source audio file instead.\n",
                path.c_str());
            return false;
        }
        std::fprintf(stderr,
            "[warptempo_gui] redirecting waveform peaks cache %s to %s\n",
            path.c_str(), owner->c_str());
        return load_file(*owner);
    }

    // Preflight.
    auto source_info = audio_probe(path);
    if (!source_info) {
        std::fprintf(stderr,
            "warptempo_gui: unsupported source format for '%s': inputs are native FLAC or WAV "
            "(convert once at acquisition, e.g. with ffmpeg or opusdec, and load the converted file)\n",
            path.c_str());
        return false;
    }

    // Stop and tear down the audio device before the sample buffer it
    // borrows is replaced. Playing into a freed buffer would crash the
    // audio thread. Order (stop → shutdown → load → init) is fixed.
    playback.stop();
    playback.shutdown();
    app.playhead_scanner_active = false;
    app.playhead_scanner_restore_pending = false;
    app.playhead_scanner_endpoint_painted = false;
    app.playhead_scanner_sample = 0;
    app.hover_popup    = HoverPopupState{};

    app.loading       = true;
    app.queue_progress_text = "loading...";
    gui.invalidate_region(0, 0, app.width, app.height);
    gui.paint_now();

    GuiAudio next;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = next.load(path, [&](float) {
        // Pump the event loop so the compositor stays responsive across a
        // multi-frame load.
        gui.drain_events();
    });
    const auto t1 = std::chrono::steady_clock::now();

    if (!ok) {
        app.loading       = false;
        app.queue_progress_text.clear();
        gui.invalidate_region(0, 0, app.width, app.height);
        return false;
    }

    // Drain any in-flight waveform render before swapping the
    // audio's internal buffers out from under it. The worker dereferences
    // the GuiAudio via the pointer captured in its WaveformJob, and the
    // move-assignment below replaces audio's pyramid vectors in place —
    // reading them mid-move is UB.
    waveform_worker.wait_until_idle();

    // Keep the source-sample-cache writer serialized with audio swaps.
    join_source_sample_cache_writer();

    audio = std::move(next);
    app.audio_generation++;
    app.loading       = false;

    source_sample_cache_writer_ = std::thread(
        [path, source_info, samples = audio.samples_shared(),
         frames = audio.total_frames(), channels = audio.channels()] {
            if (!ensure_source_sample_cache_from_buffer(
                    path, *source_info, samples ? samples->data() : nullptr,
                    frames, channels)) {
                std::fprintf(stderr,
                    "[warptempo_gui] source sample cache write skipped for %s\n",
                    path.c_str());
            }
        });

    // The just-loaded audio sets a fresh source-frame baseline; the
    // target-frame cache from any prior session is stale. The target
    // buffer is tied to the live source audio's content, so cancel any
    // in-flight target render and clear it — the eager ensure_ready()
    // at end-of-load will dispatch a fresh target render if the parsed
    // .settings landed us in target view.
    target_render.cancel_for_load();

    app.playhead_cursor_sample       = 0;
    app.viewport_start_sample = 0;
    const int max_num = max_valid_numeric_level(
        waveform_area(app).w, audio.total_frames(), audio.sample_rate());
    // Open at the 2.4 s snap level for normal files; fall back to the
    // deepest available level for files too short to support it.
    app.zoom_level = (max_num >= kSnapZoomLevel) ? kSnapZoomLevel
                   : ((max_num >= 0) ? kMinNumericLevel : kFitFileLevel);
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
    app.phaseresetmarkers_path = tm_path.string();
    app.settings_path         = set_path.string();
    app.source_audio_path     = path;
    // The title shows the canonical absolute source path: a relative or
    // symlinked command-line spelling would otherwise surface verbatim in
    // the window title. canonical() cannot fail here in practice (the file
    // was just opened); on error the spelled path is the fallback.
    std::error_code title_ec;
    const std::filesystem::path title_path =
        std::filesystem::canonical(apath, title_ec);
    gui.set_title((title_ec ? path : title_path.string()) +
                  " - warptempo_gui");

    create_if_missing(wm_path, "00:00.000|1.00\n");
    create_if_missing(set_path, format_default_settings_template(stem));

    // Load the markers file. A present-but-malformed sidecar aborts the
    // load: GuiWarpMarkers::load clears the store before parsing, so a parse
    // failure would leave an empty in-memory store while the authored file
    // sits on disk. GuiSaveOps::save writes the stores unconditionally on
    // Ctrl+S, so continuing would let one later save overwrite the authored
    // sidecar. Aborting preserves the on-disk file, the same contract as a
    // corrupt audio file or invalid engine settings below.
    app.warpmarkers.clear();
    app.phaseresetmarkers.clear();
    app.selected_markers.clear();
    app.last_selected_marker = -1;
    app.active_markers_view    = 'W';
    app.drag = DragState{};
    app.playhead_drag = PlayheadDragState{};
    app.trim_drag = TrimDragState{};
    app.scroll_drag = ScrollDragState{};
    // Project trim is no longer cleared implicitly by the fresh-ViewState
    // assignment (it lives on AppState now). Reset it explicitly before the
    // line-194 initial-playhead read so an untrimmed project loaded after a
    // trimmed one sees unset trim (playhead at sample 0, not stale begin).
    app.trim.has_begin      = false;
    app.trim.has_end        = false;
    app.trim.begin_seconds  = 0.0;
    app.trim.end_seconds    = 0.0;
    app.trim_begin_selected = false;
    app.trim_end_selected   = false;
    app.editor_text_drag = EditorTextDragState{};
    app.last_sel_group = LastSelGroup::Markers;
    // Queued renders snapshot the loaded source's authoring state and the
    // dispatch loop converts their phase reset times with the loaded
    // source's sample rate, so entries must not outlive the source they
    // were queued on.
    app.queued_renders.clear();
    // Fresh file = fresh history. Both stacks cleared; the loaded state
    // is the saved baseline (signed_distance = 0, valid).
    app.history.reset();
    app.dirty              = false;
    app.warp_dirty         = false;
    app.phase_reset_dirty    = false;
    app.settings_dirty     = false;
    app.first_save_pending = true;
    if (auto r = app.warpmarkers.load(wm_path.string()); !r) {
        std::fprintf(stderr,
            "warptempo_gui: source load aborted: invalid warp markers in "
            "'%s': %s\n",
            wm_path.string().c_str(), r.error().c_str());
        revert_to_blank();
        return false;
    } else {
        std::fprintf(stderr, "[warptempo_gui] parsed %zu markers from %s\n",
                     app.warpmarkers.markers().size(), wm_path.string().c_str());
    }

    // Load .phaseresetmarkers if present. Missing file is fine — the phase
    // reset list is just empty; absence is not malformation. A present-but-
    // malformed sidecar aborts the load: GuiPhaseResetMarkers::load clears
    // the store before parsing, so continuing would leave an empty store that
    // an unconditional Ctrl+S save would write over the authored file.
    // Aborting preserves the on-disk file, the same contract as a corrupt
    // audio file or invalid engine settings below.
    if (std::filesystem::exists(tm_path)) {
        if (auto r = app.phaseresetmarkers.load(tm_path.string()); !r) {
            std::fprintf(stderr,
                "warptempo_gui: source load aborted: invalid phase reset "
                "markers in '%s': %s\n",
                tm_path.string().c_str(), r.error().c_str());
            revert_to_blank();
            return false;
        } else {
            std::fprintf(stderr, "[warptempo_gui] parsed %zu phase_resets from %s\n",
                         app.phaseresetmarkers.markers().size(),
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
    // coerce on out-of-range. Missing file → all keys default. A present
    // file that fails to open or fails trim validation aborts the load,
    // same shape as the strict engine-settings block below: the reader
    // already printed the specific reason, so only the abort line is
    // added here before reverting and returning false.
    {
        ParsedSettings ps;
        if (!parse_settings_file(app.settings_path, ps)) {
            std::fprintf(stderr,
                "warptempo_gui: source load aborted: invalid settings in '%s'\n",
                app.settings_path.c_str());
            revert_to_blank();
            return false;
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
        // Per-tab trim: apply each bound when its key is present;
        // absence leaves the load-time reset (above) in place.
        if (ps.has_tab_a_trim_begin) { app.tab_a.trim.has_begin = true; app.tab_a.trim.begin_seconds = ps.tab_a_trim_begin; }
        if (ps.has_tab_a_trim_end)   { app.tab_a.trim.has_end   = true; app.tab_a.trim.end_seconds   = ps.tab_a_trim_end; }
        if (ps.has_tab_b_trim_begin) { app.tab_b.trim.has_begin = true; app.tab_b.trim.begin_seconds = ps.tab_b_trim_begin; }
        if (ps.has_tab_b_trim_end)   { app.tab_b.trim.has_end   = true; app.tab_b.trim.end_seconds   = ps.tab_b_trim_end; }
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
                "settings in '%s': %s\n",
                app.settings_path.c_str(), es.error().c_str());
            revert_to_blank();
            return false;
        }
        app.engine_settings = std::move(*es);
    }
    if (app.active_audio_view == 'T' &&
        !target_render.target_view_available()) {
        std::fprintf(stderr,
            "warptempo_gui: source load aborted: invalid settings in '%s': "
            "active_audio_view=T requires output_format=wav\n",
            app.settings_path.c_str());
        revert_to_blank();
        return false;
    }

    // If the parsed .settings landed us in target view, the deformed
    // total the viewport clamp below needs is derived on demand from the
    // warp_frame_map cache by live_total_frames (the markers and engine_settings
    // it derives from are already loaded at this point). No cached total
    // to populate here — a file that *opens* in target view gets the
    // correct deformed length on first read, same as the S→T toggle path.

    // Activate the parsed-tab: copy its snapshot into the live AppState
    // fields. active_tab_view was set from the parsed-settings block above.
    {
        const ViewState& parsed_tab = (app.active_tab_view == 'B')
                                      ? app.tab_b : app.tab_a;
        app.viewport_start_sample = parsed_tab.viewport_start_sample;
        app.zoom_level            = parsed_tab.zoom_level;
        app.playhead_cursor_sample       = parsed_tab.playhead_cursor_sample;
        app.trim                = parsed_tab.trim;
        app.trim_begin_selected = parsed_tab.trim_begin_selected;
        app.trim_end_selected   = parsed_tab.trim_end_selected;
        app.last_selected_trim  = parsed_tab.last_selected_trim;
        app.last_sel_group      = parsed_tab.last_sel_group;
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

    // Source-view load ends with an empty status; a target-view load lets
    // ensure_ready() -> trigger() replace it with "updating..." for the
    // eager preview below, so there is no gap in feedback.
    app.queue_progress_text.clear();

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
    app.playhead_scanner_active = false;
    app.playhead_scanner_restore_pending = false;
    app.playhead_scanner_endpoint_painted = false;
    app.playhead_scanner_sample = 0;

    // Drain the waveform worker before discarding the audio.
    // Same invariant as load_file — the worker holds a pointer into
    // `audio`, and `audio = GuiAudio{}` will replace its internals.
    waveform_worker.wait_until_idle();

    // Keep the source-sample-cache writer serialized with audio clears.
    join_source_sample_cache_writer();

    audio = GuiAudio{};
    app.audio_generation++;
    wf_cache.destroy_surface();
    // Stem cache mirrors the waveform cache's lifecycle. Safe
    // to destroy on the main thread; the rebuild path is fully
    // synchronous so no in-flight work to drain.
    stem_cache.destroy_surface();
    // Flag cache mirrors the same lifecycle as the stem cache.
    flag_cache.destroy_surface();

    app.playhead_cursor_sample  = 0;
    app.viewport_start_sample   = 0;
    app.zoom_level              = 0;
    app.follow_mode             = true;
    app.playhead_scanner_active = false;
    app.playhead_scanner_restore_pending = false;
    app.playhead_scanner_endpoint_painted = false;
    app.playhead_scanner_sample = 0;
    app.playback_speed          = 1.0f;

    app.warpmarkers.clear();
    app.phaseresetmarkers.clear();
    app.selected_markers.clear();
    app.last_selected_marker = -1;
    app.drag          = DragState{};
    app.playhead_drag = PlayheadDragState{};
    app.trim_drag     = TrimDragState{};
    app.scroll_drag   = ScrollDragState{};
    app.last_sel_group = LastSelGroup::Markers;
    app.hover_popup   = HoverPopupState{};
    app.history.reset();
    app.dirty              = false;
    app.warp_dirty         = false;
    app.phase_reset_dirty    = false;
    app.settings_dirty     = false;
    app.first_save_pending = true;

    app.warpmarkers_path.clear();
    app.phaseresetmarkers_path.clear();
    app.settings_path.clear();
    app.source_audio_path.clear();
    gui.set_title("warptempo_gui");
    app.pending_drop_path.clear();
    app.engine_settings = EngineSettings{};

    app.tab_a = ViewState{};
    app.tab_b = ViewState{};

    // Project trim lives on AppState; the fresh-ViewState assignment above
    // no longer clears it. Reset explicitly so no trim carries into the
    // next load.
    app.trim.has_begin      = false;
    app.trim.has_end        = false;
    app.trim.begin_seconds  = 0.0;
    app.trim.end_seconds    = 0.0;
    app.trim_begin_selected = false;
    app.trim_end_selected   = false;

    // View-selector triplet defaults, in bottom-bar order [S/T] [W/P] [A/B].
    app.active_audio_view   = 'S';
    app.active_markers_view = 'W';
    app.active_tab_view     = 'A';

    // Target render state is tied to the live source audio; cancel any
    // in-flight target render and clear it on revert so a subsequent
    // file load doesn't inherit stale frames.
    target_render.cancel_for_load();

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
