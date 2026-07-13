#include "file_loader.h"

#include "input_handler.h"   // validate_target_view_entry (load gate below)
#include "prompt.h"
#include "render_output_naming.h"
#include "settings_io.h"
#include "target_render.h"
#include "warp_frame_map_view.h"
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
        return false;
    }

    // Preflight. Print the probe owner's diagnostic verbatim in the unified
    // shape: a malformed but recognized WAV/FLAC (duplicate chunk, truncated
    // header, non-finite Float32) must not be misread as an unsupported
    // format. The convert-once acquisition hint applies only when the magic
    // matched no container at all (kUnknownAudioMagicError), so it is
    // appended in that one case.
    auto source_info = audio_probe(path);
    if (!source_info) {
        if (source_info.error() == kUnknownAudioMagicError) {
            std::fprintf(stderr,
                "warptempo_gui: source open failed for '%s': %s; inputs are "
                "native FLAC or WAV, so convert once at acquisition (e.g. with "
                "ffmpeg or opusdec) and load the converted file\n",
                path.c_str(), source_info.error().c_str());
        } else {
            std::fprintf(stderr,
                "warptempo_gui: source open failed for '%s': %s\n",
                path.c_str(), source_info.error().c_str());
        }
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

    // The corpus is stereo, and mono-for-sale is delivered as locked stereo, so
    // off-corpus channel counts are refused rather than supported (convert once
    // outside, e.g. with ffmpeg). The stereo invariant also makes every
    // product-written wav payload even (see WavWriter::close), so the RIFF
    // odd-payload pad byte stays unreachable.
    if (source_info->channels != 2) {
        std::fprintf(stderr,
            "warptempo_gui: source load failed for '%s': %d channels (stereo "
            "sources only)\n",
            path.c_str(), source_info->channels);
        return false;
    }

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
        // The sole source load (at launch) failed to decode. GuiAudio::load
        // already printed the reason; there is no prior project to fall back
        // to and no in-session way to load another, so exit — the user reads
        // the terminal and relaunches.
        app.loading       = false;
        app.queue_progress_text.clear();
        gui.request_exit();
        return false;
    }

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
                    "warptempo_gui: source sample cache write skipped for %s\n",
                    path.c_str());
            }
        });

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
        gui.request_exit();
        return false;
    } else {
        std::fprintf(stderr, "warptempo_gui: parsed %zu markers from %s\n",
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
        gui.request_exit();
        return false;
    } else {
        std::fprintf(stderr, "warptempo_gui: parsed %zu phase_resets from %s\n",
                     app.phaseresetmarkers.markers().size(),
                     tm_path.string().c_str());
    }

    // Initial playhead: land at trim-begin if a b= marker was parsed,
    // otherwise sample 0. Must happen after marker parse so the trim
    // range reflects the on-disk state. Scroll the viewport so the
    // playhead is visible rather than lurking off the left edge.
    // Deliberately unclamped: trim begin walls at total - 1 (load-fatal
    // past it), so the value is inside the playhead's [0, total - 1]
    // domain by construction (move_playhead_to holds the ruling).
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

    // The whole-file strict settings schema (read_settings_file,
    // settings_file.h), shared verbatim with warptempo_cli so a sidecar set
    // is loadable in both products or neither. Any schema violation —
    // unknown key, duplicate, malformed value, off-preset playback_speed,
    // missing required engine key — aborts the load with the first error,
    // the same shape as a corrupt audio file: the load fails and the
    // process exits, so the user never sees a half-loaded state. On top of the
    // context-dependent viewport/playhead range rules run at the end of
    // this block, equally load-fatal. Persisted view positions live in the
    // persisted active_audio_view's domain — while 'T' the live view fields
    // carry target-frame values and the S/T toggle translates BOTH tabs
    // together, so the domain is global to the file — and the wall is that
    // domain's total: 'S' or absent walls at the source total; 'T' walls at
    // the built warp frame map's deformed target total, which a slowing map
    // legitimately puts past the source total (Ctrl+S from target view
    // writes such values). A position past its own domain's total is
    // adversarial exactly like a past-EOF marker (the sidecar was authored
    // against this audio's frame grid). Trim bound ordering stays
    // deliberately unchecked — equal/inverted bounds load intact and walk
    // the defect series; the render boundary owns trim refusals.
    {
        auto sf_r = read_settings_file(app.settings_path);
        if (!sf_r) {
            std::fprintf(stderr,
                "warptempo_gui: source load aborted: invalid settings in "
                "'%s': %s\n",
                app.settings_path.c_str(), sf_r.error().c_str());
            gui.request_exit();
            return false;
        }
        const SettingsFile& sf = *sf_r;
        // Source-clobber guard, adversarial and load-fatal: a hand-edited
        // sidecar whose title/output_format composes a render output path onto
        // the source audio itself would overwrite the source at render time.
        // The GUI editor refuses this at commit, so the state is
        // GUI-uncommittable; refuse the hand-edited file here at load, the
        // earliest boundary, in the same abort-and-exit shape as the other
        // adversarial settings refusals. The shared predicate matches the
        // editor's composition exactly (single-render source-sibling paths).
        if (auto collision =
                render_output_source_collision(sf.engine,
                                               app.source_audio_path)) {
            std::fprintf(stderr,
                "warptempo_gui: source load aborted: settings in '%s' would "
                "make the render output '%s' overwrite the source audio file\n",
                app.settings_path.c_str(), collision->string().c_str());
            gui.request_exit();
            return false;
        }
        // The schema already enforced syntax, non-negativity, and the zoom
        // vocabulary; the per-tab view scratch applies verbatim here. The
        // audio-relative viewport/playhead bounds run at the end of this
        // block instead, because the 'T' domain total needs the warp frame
        // map built from the loaded markers and the adopted scale.
        auto apply = [&](const SettingsFileTab& src, ViewState& dst) {
            if (src.has_viewport_start) {
                dst.viewport_start_sample = src.viewport_start;
            }
            if (src.has_zoom) {
                dst.zoom_level = src.zoom;
            }
            if (src.has_playhead) {
                // Deliberately unclamped: persisted view scratch, validated
                // by first_view_range_defect below; playhead == total stays
                // load-legal (pre-wall files) and the runtime clamp
                // (move_playhead_to) owns the value at first use.
                dst.playhead_cursor_sample = src.playhead;
            }
        };
        apply(sf.tab_a, app.tab_a);
        apply(sf.tab_b, app.tab_b);
        app.engine_settings = sf.engine;
        app.follow_mode    = sf.has_follow ? sf.follow : true;
        app.active_audio_view   = sf.has_active_audio_view   ? sf.active_audio_view   : 'S';
        app.active_markers_view = sf.has_active_markers_view ? sf.active_markers_view : 'W';
        app.active_tab_view     = sf.has_active_tab_view     ? sf.active_tab_view     : 'A';
        app.playback_speed = sf.has_playback_speed ? sf.playback_speed : 1.0f;
        // GUI font size, same application shape as playback_speed: absent
        // key means the default. Loading a source can therefore change the
        // GUI text size mid-session — the same recorded behavior class as
        // playback_speed (see the font_size descriptor in settings_io.cpp).
        app.font_size      = sf.has_font_size ? sf.font_size : 11.0;
        // Per-tab trim: apply each bound when its key is present;
        // absence leaves the load-time reset (above) in place.
        if (sf.tab_a.trim.has_begin) { app.tab_a.trim.has_begin = true; app.tab_a.trim.begin_frame = sf.tab_a.trim.begin_frame; }
        if (sf.tab_a.trim.has_end)   { app.tab_a.trim.has_end   = true; app.tab_a.trim.end_frame   = sf.tab_a.trim.end_frame; }
        if (sf.tab_b.trim.has_begin) { app.tab_b.trim.has_begin = true; app.tab_b.trim.begin_frame = sf.tab_b.trim.begin_frame; }
        if (sf.tab_b.trim.has_end)   { app.tab_b.trim.has_end   = true; app.tab_b.trim.end_frame   = sf.tab_b.trim.end_frame; }
        if (sf.tab_a.has_read_only) app.tab_a.read_only = sf.tab_a.read_only;
        if (sf.tab_b.has_read_only) app.tab_b.read_only = sf.tab_b.read_only;

        // Deferred audio-relative viewport/playhead validation — the domain
        // rule from the block comment above, one shared implementation with
        // warptempo_cli (first_view_range_defect, marker_store_validate.h)
        // so a sidecar set is loadable in both products or neither. The
        // domain total: the source total for 'S'; for 'T' the memoized
        // target-view cache's tgt_total_frames — EXACTLY the value
        // live_total_frames feeds the runtime viewport clamps and zoom
        // bounds, so the load-time wall and the runtime domain always agree
        // (the markers and engine scale the cache derives from are both in
        // place by this point). When 'T' is persisted but the map cannot
        // build (the marker store carries walkable defects, which load
        // intact by design), SKIP this validation entirely: there is no
        // target total to wall against, refusing would lock out a
        // legitimately saved session whose sidecar was later hand-edited
        // into a walkable state, and the runtime viewport clamps plus the
        // target-view validity gates (validate_target_view_entry below,
        // enforce_target_view_validity per tick) own the values then.
        {
            bool    run_view_check = true;
            int64_t domain_total   = audio.total_frames();
            if (app.active_audio_view == 'T') {
                const TargetWarpFrameMapCache& c =
                    target_view_warp_frame_map_cached(
                        app, audio.sample_rate(),
                        static_cast<long>(audio.total_frames()));
                if (!c.build_error.empty()) {
                    run_view_check = false;
                } else {
                    domain_total = c.tgt_total_frames;
                }
            }
            if (run_view_check) {
                if (auto detail = first_view_range_defect(
                        sf.tab_a, sf.tab_b, domain_total)) {
                    std::fprintf(stderr,
                        "warptempo_gui: source load aborted: invalid "
                        "settings in '%s': %s\n",
                        app.settings_path.c_str(), detail->c_str());
                    gui.request_exit();
                    return false;
                }
            }
        }
    }
    if (app.active_audio_view == 'T' &&
        !target_render.target_view_available()) {
        std::fprintf(stderr,
            "warptempo_gui: source load aborted: invalid settings in '%s': "
            "active_audio_view=T requires output_format=wav\n",
            app.settings_path.c_str());
        gui.request_exit();
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
        // Deliberately unclamped: persisted view scratch, validated by
        // first_view_range_defect below; playhead == total stays load-legal
        // (pre-wall files) and the runtime clamp (move_playhead_to) owns
        // the value at first use.
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
    // switch. The six wall comparisons live in first_past_eof_wall_defect
    // (marker_store_validate.h), the one implementation the CLI runs too —
    // a file set is loadable or not, the same in both binaries. The
    // render-boundary EOF refusals downstream stay as breach backstops for
    // hand-edited maps.
    {
        auto trim_of = [](const TrimState& t) {
            SettingsTrim s;
            s.has_begin   = t.has_begin;
            s.begin_frame = t.begin_frame;
            s.has_end     = t.has_end;
            s.end_frame   = t.end_frame;
            return s;
        };
        const auto detail = first_past_eof_wall_defect(
            slice_to_warp_markers(app.warpmarkers.markers()),
            slice_to_phase_reset_markers(app.phaseresetmarkers.markers()),
            trim_of(app.tab_a.trim), trim_of(app.tab_b.trim),
            audio.total_frames(), audio.sample_rate());
        if (detail) {
            std::fprintf(stderr,
                "warptempo_gui: source load aborted: %s\n", detail->c_str());
            gui.request_exit();
            return false;
        }
    }

    // Bring up the audio device bound to the new sample buffer. Init
    // failure disables playback but leaves the rest of the GUI usable.
    // Domain offset 0: the source is its own domain origin.
    if (!playback.init(audio.sample_rate(), audio.channels(),
                       audio.samples_ptr(), audio.total_frames(), 0)) {
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
                 "warptempo_gui: loaded %s: sr=%d, channels=%d, frames=%lld, "
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
    // wait. The target buffer is empty on this sole load; ensure_ready's
    // is_dirty_=true (its construction default) falls through to
    // trigger(). No-op if
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
