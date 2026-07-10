#include "file_loader.h"

#include "input_handler.h"   // validate_target_view_entry (load gate below)
#include "prompt.h"
#include "settings_io.h"
#include "target_render.h"
#include "warp_frame_map_view.h"
#include "waveform_worker.h"
#include "source_sample_cache.h"

#include "warp_frame_map.h"
#include "time_format.h"

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
    // Opening a private cache file is a careless wrong-file slip, so it
    // refuses loudly at this earliest surface — a dismiss-only notice, no
    // load. It is never silently redirected to a guessed owner path:
    // silently doing a different operation than the one requested is
    // correction, not refusal, and the caches are derived state the user
    // never legitimately opens.
    if (is_source_sample_cache_path(path) || is_peaks_cache_path(path)) {
        const bool samples = is_source_sample_cache_path(path);
        std::string msg = "'" + path + "' is a " +
            (samples ? "private source sample cache"
                     : "waveform peaks cache") +
            "; open the original source audio file instead.";
        std::fprintf(stderr, "warptempo_gui: %s\n", msg.c_str());
        if (prompt) prompt->open_error_notice(std::move(msg));
        return false;
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

    // Rates below 44.1k are out of scope by ruling, and the whole-frame gesture
    // pixel guarantees assume the 44100 floor (higher rates only widen the margins).
    if (source_info->sample_rate < 44100) {
        std::fprintf(stderr,
            "warptempo_gui: source load failed for '%s': sample rate %d is "
            "below the 44100 floor\n",
            path.c_str(), source_info->sample_rate);
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
    // Mirror for font_size: reset to the default before the .settings parse
    // below, which overwrites it when the key is present (absent key means
    // 11.0). Applied to the renderer after the parse, beside set_speed.
    app.font_size      = 11.0;

    // Companion files: discover paths, create <basename>.warpmarkers,
    // <basename>.phaseresetmarkers, and <basename>.settings if missing.
    // Companion file convention is <source_dir>/<source_basename>.<ext>
    // (sibling, basename-prefixed), not the legacy hidden `./.warpmarkers`
    // form.
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

    create_if_missing(wm_path, "0|1.00\n");
    // The empty file is the canonical blank phase reset sidecar: resets have
    // no mandatory first marker, so the seed is empty content, unlike warp's
    // seeded first-marker line.
    create_if_missing(tm_path, "");
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
    // A fresh load replaces the stores wholesale: drop any pending
    // validation and dismiss an open defect-resolution prompt. This
    // wholesale reset must run BEFORE the Load-origin pending flag set at
    // the end of a successful load — order matters, or the reset would
    // clear the very flag that opens the load-time walk.
    app.defect_series = DefectSeriesState{};
    if (app.prompt.active &&
        app.prompt.trigger == DialogTrigger::DEFECT_RESOLUTION) {
        app.prompt.active = false;
    }
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
    app.trim.begin_frame  = 0;
    app.trim.end_frame    = 0;
    app.trim_begin_selected = false;
    app.trim_end_selected   = false;
    app.editor_text_drag = EditorTextDragState{};
    app.last_sel_group = LastSelGroup::Markers;
    // Queued renders snapshot the loaded source's authoring state against
    // the loaded source's frame grid, so entries must not outlive the
    // source they were queued on.
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

    // Load the phase reset markers file. The empty file is the canonical
    // no-resets form and parses to an empty list; the load-time creation
    // above guarantees the file is present, so the load is unconditional. A
    // present-but-malformed sidecar aborts the load: GuiPhaseResetMarkers::load
    // clears the store before parsing, so a parse failure would leave an empty
    // in-memory store while the authored file sits on disk, and an unconditional
    // Ctrl+S save would later overwrite it. Aborting preserves the on-disk file,
    // the same contract as the warp load above.
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
    // file that fails to open or fails the trim reader's syntax checks
    // (malformed timestamp, duplicate key, bad active_tab_view — bound
    // ordering is deliberately unchecked; equal/inverted trim loads intact
    // and the render boundary refuses instead) aborts the load, same shape
    // as the strict engine-settings block below: the reader already printed
    // the specific reason, so only the abort line is added here before
    // reverting and returning false.
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
        // GUI font size, same application shape as playback_speed: absent
        // key means the default. Loading a source can therefore change the
        // GUI text size mid-session — the same recorded behavior class as
        // playback_speed (see the font_size descriptor in settings_io.cpp).
        app.font_size      = ps.has_font_size ? ps.font_size : 11.0;
        // Per-tab trim: apply each bound when its key is present;
        // absence leaves the load-time reset (above) in place.
        if (ps.has_tab_a_trim_begin) { app.tab_a.trim.has_begin = true; app.tab_a.trim.begin_frame = ps.tab_a_trim_begin; }
        if (ps.has_tab_a_trim_end)   { app.tab_a.trim.has_end   = true; app.tab_a.trim.end_frame   = ps.tab_a_trim_end; }
        if (ps.has_tab_b_trim_begin) { app.tab_b.trim.has_begin = true; app.tab_b.trim.begin_frame = ps.tab_b_trim_begin; }
        if (ps.has_tab_b_trim_end)   { app.tab_b.trim.has_end   = true; app.tab_b.trim.end_frame   = ps.tab_b_trim_end; }
        if (ps.has_tab_a_read_only) app.tab_a.read_only = ps.tab_a_read_only;
        if (ps.has_tab_b_read_only) app.tab_b.read_only = ps.tab_b_read_only;
    }

    // Strict engine-settings deserialization. Non-canonical keys are
    // ignored (they are GUI-kind, owned by parse_settings_file above); a
    // duplicate canonical key, an invalid value, or a missing required key
    // fails the load with the first violation reported. Treat like a
    // corrupt audio file: revert the partial load and return false so the
    // user sees no half-loaded state.
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

    // Adversarial past-EOF guard: hard-fail the load like a corrupt audio
    // file when any marker or any tab's trim sits past its wall. Such a
    // position is uncommittable through the GUI (the marker EOF walls and the
    // per-bound trim walls) and a sidecar applies only to the audio it was
    // authored against, so a past-EOF position means the audio was swapped
    // outside the GUI. BOTH tabs' trim is checked — trim is per-tab, and an
    // inactive-tab bound would otherwise load and go live on the next tab
    // switch. Checked in order — warp markers (wall total-1), phase reset
    // markers (wall total exactly), then tab A begin (wall total-1), tab A
    // end (wall total), tab B begin, tab B end — first offender only,
    // disabled markers included (a disabled past-EOF marker is equally
    // unauthorable). The guard compares the authored domain directly: every
    // check is a plain integer compare against the stored value —
    // literally the same comparison the gesture walls apply, with no
    // rounding anywhere — so a legal at-the-wall position always reloads
    // clean. Embedded times in the messages are display renderings
    // (format_timestamp(frame / sr)). The render-boundary EOF refusals
    // downstream stay as breach backstops for hand-edited maps.
    {
        const double  sr_d  = static_cast<double>(audio.sample_rate());
        const int64_t total = audio.total_frames();
        std::string detail;
        for (const auto& m : app.warpmarkers.markers()) {
            if (m.time_frame > total - 1) {
                detail = "warp marker past end of audio at "
                         + format_timestamp(m.time_frame / sr_d);
                break;
            }
        }
        if (detail.empty()) {
            for (const auto& m : app.phaseresetmarkers.markers()) {
                if (m.time_frame > total) {
                    detail = "phase reset marker past end of audio at "
                             + format_timestamp(m.time_frame / sr_d);
                    break;
                }
            }
        }
        if (detail.empty()) {
            const struct { const char* name; const TrimState& t; } tabs[] = {
                {"tab A", app.tab_a.trim}, {"tab B", app.tab_b.trim},
            };
            for (const auto& [name, t] : tabs) {
                if (t.has_begin && t.begin_frame > total - 1) {
                    detail = std::string(name)
                             + " trim begin past end of audio at "
                             + format_timestamp(t.begin_frame / sr_d);
                    break;
                }
                if (t.has_end && t.end_frame > total) {
                    detail = std::string(name)
                             + " trim end past end of audio at "
                             + format_timestamp(t.end_frame / sr_d);
                    break;
                }
            }
        }
        if (!detail.empty()) {
            std::fprintf(stderr,
                "warptempo_gui: source load aborted: %s\n", detail.c_str());
            revert_to_blank();
            return false;
        }
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

    // Push the loaded font size to the renderer's file-scope state and
    // route the geometry consequences through the same rebuild path a
    // window resize performs: on_resize re-clamps zoom/viewport against
    // the (possibly changed) strip geometry, the next redraw re-measures
    // the grid metrics, and the cache fingerprints (area dims keyed off
    // monospace_row_h()) rebuild the waveform/stem/flag surfaces. The
    // full-window invalidation at the end of this load supplies the
    // damage, mirroring the resize path's full-surface damage.
    set_gui_font_size_pt(app.font_size);
    paint_handler.on_resize(app.width, app.height);

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

    // Target-view validity gate, load half: restoring active_audio_view=T
    // from .settings is an entry into target view and is gated by EXACTLY
    // the predicate that gates a keyboard S → T entry —
    // validate_target_view_entry (input_handler.h): resolve the loaded warp
    // store, build the whole-song warp_frame_map with the loaded scale,
    // and, when the active tab loaded a trim bound, validate_trim_frames
    // against that map. Every input the walk consumes is in place by this
    // point: markers (parsed above, default zero-marker seeded), engine
    // settings (strict block above), and the active tab's trim (tab
    // activation above). On failure, force source view SILENTLY — no popup
    // and no series open here: pending_validation = Load (set below) is the
    // ruled surface and opens the defect-resolution series on the first
    // tick, now from source view, so no target render of an invalid store
    // is ever dispatched. The gate mirrors the entry gate ONLY — defects
    // the entry gate does not block on (coincident markers wider than the
    // owners' sub-frame refusals, a trim bound under a map format,
    // dangling-ref states the resolver walks) load intact and the series
    // handles them. Forcing 'S' intentionally means a later save persists
    // active_audio_view=S — the same outcome the kick-back gate
    // (enforce_target_view_validity) produces for an invalidating edit made
    // in target view; the saved line reflects the view actually shown.
    if (app.active_audio_view == 'T') {
        if (!validate_target_view_entry(
                app.warpmarkers.markers(), app.engine_settings.scale,
                audio.sample_rate(), static_cast<long>(audio.total_frames()),
                app.trim.has_begin, app.trim.begin_frame,
                app.trim.has_end,   app.trim.end_frame)) {
            app.active_audio_view = 'S';
            // The tab-activation clamp above ran against the target-domain
            // total (live_total_frames consults the target map cache while
            // active_audio_view=='T'); with the view forced back to source
            // the source total governs, so re-clamp. This matters when the
            // map builds but the trim fails: the deformed total can exceed
            // the source total, leaving viewport_start past the
            // source-domain maximum.
            clamp_viewport_start(app, audio);
        }
    }

    // If the load landed us in target view — the parsed settings said 'T'
    // AND the loaded state passed the same validity walk that gates a
    // keyboard S → T entry (the gate above) — dispatch a fresh target
    // render now so the first Space press is ready without a first-edit
    // wait. The target buffer was cleared by cancel_for_load above;
    // ensure_ready's is_dirty_=true falls through to trigger(). No-op if
    // active_audio_view=='S', including an invalid target-view load just
    // forced to source view: that path's surface is the load-origin defect
    // series, and the eager preview waits for a clean `t` entry.
    target_render.ensure_ready();

    // Load-origin defect walk: a loaded file set with GUI-committable
    // defects (coincident markers, bad first marker, dangling refs,
    // crossed trim, a trim bound under a map format) gets its modal walk
    // on the first tick after the load, not first at render dispatch or
    // target-view entry — if the GUI would allow the state, the GUI forces
    // its resolution at the earliest surface, and load is that surface for
    // file-borne defects. The tick gate (run_commit_validation: prompt not
    // active, not loading, no gesture in flight) opens the walk; the
    // wholesale defect_series reset above already ran, so this flag
    // survives to that tick. A Load-origin series offers no [U]ndo —
    // history was cleared above. A blank/missing marker file seeds the
    // default marker and produces no defects, so its walk closes silently.
    app.defect_series.pending_validation = PendingValidation::Load;

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
    // Mirror for font_size: back to the default on revert, pushed to the
    // renderer immediately (the invalidate_all below supplies the damage;
    // the blank state has no caches to rebuild).
    app.font_size               = 11.0;
    set_gui_font_size_pt(app.font_size);

    app.warpmarkers.clear();
    app.phaseresetmarkers.clear();
    app.selected_markers.clear();
    app.last_selected_marker = -1;
    // Same reset as load_file: the stores are gone, so the defect series
    // and its pending flag go with them.
    app.defect_series = DefectSeriesState{};
    if (app.prompt.active &&
        app.prompt.trigger == DialogTrigger::DEFECT_RESOLUTION) {
        app.prompt.active = false;
    }
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
    app.trim.begin_frame  = 0;
    app.trim.end_frame    = 0;
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
