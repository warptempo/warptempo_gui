#include "file_loader.h"

#include "input_handler.h"   // validate_target_view_entry (load gate below)
#include "prompt.h"
#include "render_output_naming.h"
#include "settings_io.h"
#include "target_render.h"
#include "warp_frame_map_view.h"

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

void apply_settings_engine_and_prefs(AppState& app, const SettingsFile& sf) {
    app.engine_settings = sf.engine;
    app.follow_mode         = sf.follow;
    app.active_audio_view   = sf.active_audio_view;
    app.active_markers_view = sf.active_markers_view;
    app.active_tab_view     = sf.active_tab_view;
    app.playback_speed      = sf.playback_speed;
    // GUI font size, applied once at launch when the source loads — the same
    // behavior class as playback_speed (see the font_size descriptor in
    // settings_io.cpp).
    app.font_size           = sf.font_size;
    // GUI launch preference for the `l` render-listen command, applied
    // verbatim: a blank value is the deliberate no-player opt-out. Adopt shares
    // this routine, so an adopted render entry's player is 1:1 with its file.
    app.audio_player        = sf.audio_player;
}

bool GuiFileLoader::load_file(const std::string& path) {
    // Opening the private peaks cache is a careless wrong-file slip, so it
    // refuses loudly at this earliest surface — a dismiss-only notice, no
    // load. It is never silently redirected to a guessed owner path:
    // silently doing a different operation than the one requested is
    // correction, not refusal, and the peaks cache is derived state the user
    // never legitimately opens. A stray `.samples` file (no live producer
    // since the FLAC-reload source cache was retired) has no RIFF magic, so it
    // takes the generic unknown-magic refusal below — no special-cased handling.
    if (is_peaks_cache_path(path)) {
        std::string msg = "'" + path +
            "' is a waveform peaks cache; open the original source audio file "
            "instead.";
        std::fprintf(stderr, "warptempo_gui: %s\n", msg.c_str());
        return false;
    }

    // Preflight. Print the probe owner's diagnostic verbatim in the unified
    // shape: a malformed but recognized WAV (duplicate chunk, truncated
    // header) must not be misread as an unsupported format. The convert-once
    // acquisition hint applies only when the magic
    // matched no container at all (kUnknownAudioMagicError), so it is
    // appended in that one case.
    auto source_info = audio_probe(path);
    if (!source_info) {
        if (source_info.error() == kUnknownAudioMagicError) {
            std::fprintf(stderr,
                "warptempo_gui: source open failed for '%s': %s; inputs are "
                "WAV only, so convert once at acquisition (e.g. with ffmpeg) "
                "and load the converted file\n",
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
    // Loading is a blocking, uninterruptible phase: the run loop is suspended
    // for the whole synchronous decode/pyramid/install, so the callback below
    // only pumps the compositor (drain_events reads no socket fd) and never
    // reports cancel. A Ctrl+Q / WM close pressed during loading is not
    // observed until the load completes; the queued event is then read when
    // run() resumes and the deferred quit is honored on completion. An urgent
    // abort is pkill / the compositor's force-close.
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

    audio = std::move(next);
    app.loading       = false;

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
    app.playback_speed = 0.7f;
    // Mirror for font_size: construction-state default before the .settings
    // parse below. Every key is required by the schema, so the parse always
    // assigns font_size; this initializer only covers the no-.settings /
    // first-open path. Applied to the renderer after the parse, beside
    // set_speed.
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
    app.active_markers_view    = 'W';
    app.drag = DragState{};
    app.playhead_drag = PlayheadDragState{};
    app.trim_drag = TrimDragState{};
    app.scroll_drag = ScrollDragState{};
    // Project trim is not cleared implicitly by the fresh-ViewState assignment
    // (it lives on AppState now). Reset it explicitly before the initial-playhead
    // read: this is construction-state for the no-.settings / first-open path.
    // A .settings always carries the four trim keys (unset is the `-1` value, not
    // an absent key), so its parse always assigns; the reset only keeps a
    // launch with no .settings from reading a leftover begin out of the
    // base-state struct (playhead at sample 0).
    app.trim.has_begin      = false;
    app.trim.has_end        = false;
    app.trim.begin_frame  = 0;
    app.trim.end_frame    = 0;
    app.trim_begin_selected = false;
    app.trim_end_selected   = false;
    app.editor_text_drag = EditorTextDragState{};
    app.last_sel_group = LastSelGroup::Markers;
    // Fresh file = fresh history. Both stacks cleared; the loaded state
    // is the saved baseline (signed_distance = 0, valid).
    app.history.reset();
    app.dirty              = false;
    app.warp_dirty         = false;
    app.phase_reset_dirty    = false;
    app.settings_dirty     = false;
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
    // process exits, so the user never sees a half-loaded state. Persisted
    // viewport/playhead positions are display scratch, not authored data:
    // they carry no audio-relative range check and apply verbatim, the
    // runtime clamps owning any out-of-range value — clamp_viewport_start for
    // the viewport, and the live-domain playhead clamp
    // (clamp_playhead_to_live_domain, both tab snapshots) at the end of this
    // block, the earliest point the persisted S/T domain is computable. Trim
    // bound ordering is normalized, not checked: a per-tab pair with end <=
    // begin clears both of that tab's bounds after the adversarial walls run
    // (the auto-clear block below the past-EOF guard), and the render's
    // ambiguous-trim fallback renders untrimmed — never a refusal.
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
        // sidecar whose title composes a render output path onto
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
        // vocabulary; the per-tab view scratch applies verbatim here. Viewport
        // and playhead positions are display scratch, not authored data, so
        // there is no audio-relative range check on them — the runtime clamps
        // own any out-of-range value.
        auto apply = [&](const SettingsFileTab& src, ViewState& dst) {
            dst.viewport_start_sample = src.viewport_start;
            dst.zoom_level            = src.zoom;
            // Applied verbatim here; the live-domain clamp runs on both tab
            // snapshots after this settings block, once the persisted S/T
            // domain is computable (the clamp site below).
            dst.playhead_cursor_sample = src.playhead;
        };
        apply(sf.tab_a, app.tab_a);
        apply(sf.tab_b, app.tab_b);
        // Engine block plus the scalar session prefs (follow,
        // active_audio_view, active_markers_view, active_tab_view,
        // playback_speed, font_size, audio_player), VALUES ONLY. The
        // side effects that consume these (set_speed, set_gui_font_size_pt,
        // on_resize) stay below where they always ran. The render-entry adopt
        // shares this exact routine so its in-memory result is 1:1 with a load.
        apply_settings_engine_and_prefs(app, sf);
        // Per-tab trim: apply each bound the file set (SettingsTrim's has_begin
        // /has_end reflect the `-1` unset decode); an unset bound leaves the
        // load-time reset (above) in place. Values apply verbatim here; a
        // crossed/equal pair normalizes in the auto-clear block after the
        // past-EOF guard below (order rationale there).
        if (sf.tab_a.trim.has_begin) { app.tab_a.trim.has_begin = true; app.tab_a.trim.begin_frame = sf.tab_a.trim.begin_frame; }
        if (sf.tab_a.trim.has_end)   { app.tab_a.trim.has_end   = true; app.tab_a.trim.end_frame   = sf.tab_a.trim.end_frame; }
        if (sf.tab_b.trim.has_begin) { app.tab_b.trim.has_begin = true; app.tab_b.trim.begin_frame = sf.tab_b.trim.begin_frame; }
        if (sf.tab_b.trim.has_end)   { app.tab_b.trim.has_end   = true; app.tab_b.trim.end_frame   = sf.tab_b.trim.end_frame; }
        app.tab_a.read_only = sf.tab_a.read_only;
        app.tab_b.read_only = sf.tab_b.read_only;

        // Persisted viewport/playhead positions are display scratch, not
        // authored data: they apply verbatim (above) with no load-range
        // check. The runtime clamps own any out-of-range value harmlessly —
        // clamp_viewport_start for the viewport, and the live-domain
        // playhead clamp (clamp_playhead_to_live_domain, both tab snapshots)
        // immediately after this block.
    }
    // If the parsed .settings landed us in target view, the deformed
    // total the viewport clamp below needs is derived on demand from the
    // warp_frame_map cache by live_total_frames (the markers and engine_settings
    // it derives from are already loaded at this point). No cached total
    // to populate here — a file that *opens* in target view gets the
    // correct deformed length on first read, same as the S→T toggle path.

    // Persisted playheads clamp into the live domain HERE — the earliest
    // point the persisted S/T domain is computable (active_audio_view and
    // the markers/engine settings the target total derives from are all
    // applied above; both tabs live in that one global domain, so one
    // total clamps both). Load-lenient stays: clamping is the ruling, not
    // a refusal — in-domain values pass through unchanged, and the
    // historically load-legal playhead == total silently rests at
    // total - 1, the ruled rest domain ([0, total - 1],
    // move_playhead_to). This bounds the value BEFORE any consumer's
    // translation arithmetic (the S/T toggle's double->int64 conversion,
    // Shift+Space's launch offset) rather than at first gesture use.
    app.tab_a.playhead_cursor_sample = clamp_playhead_to_live_domain(
        app.tab_a.playhead_cursor_sample, app, audio);
    app.tab_b.playhead_cursor_sample = clamp_playhead_to_live_domain(
        app.tab_b.playhead_cursor_sample, app, audio);

    // Activate the parsed-tab: copy its snapshot into the live AppState
    // fields. active_tab_view was set from the parsed-settings block above.
    {
        const ViewState& parsed_tab = (app.active_tab_view == 'B')
                                      ? app.tab_b : app.tab_a;
        app.viewport_start_sample = parsed_tab.viewport_start_sample;
        app.zoom_level            = parsed_tab.zoom_level;
        // Already clamped into the live domain by the tab-snapshot clamp
        // above, so the live copy is in [0, total - 1] by construction.
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

    // Load auto-clear: a persisted per-tab trim pair with end <= begin —
    // exact integer compare — clears BOTH of that tab's bounds, one stderr
    // line per cleared tab (crossed/equal cannot REST anywhere; the trim
    // sibling of the marker normalizations, gesture half at
    // auto_clear_crossed_trim). Deliberately AFTER the adversarial past-EOF
    // hard-fail above: a crossed pair containing a past-EOF bound must
    // still abort the load, identically in warptempo_cli (which loads the
    // same values, runs the same wall check, and has no clear) — clearing
    // first would swallow the adversarial defect in one product only. The
    // live app.trim mirror was copied from the active tab at activation
    // above, so it re-syncs after the clears. Render-entry sidecars reach
    // memory through adopt_render_entry's own application path and
    // deliberately skip this: they are written once at dispatch from a live
    // store that can no longer rest crossed — trusted, no re-check.
    {
        const auto clear_crossed_tab = [](TrimState& t, char tab_name) {
            if (t.has_begin && t.has_end && t.end_frame <= t.begin_frame) {
                t = TrimState{};
                std::fprintf(stderr,
                    "warptempo_gui: tab %c trim bounds crossed or equal; "
                    "both cleared\n",
                    tab_name);
            }
        };
        clear_crossed_tab(app.tab_a.trim, 'A');
        clear_crossed_tab(app.tab_b.trim, 'B');
        app.trim = (app.active_tab_view == 'B') ? app.tab_b.trim
                                                : app.tab_a.trim;
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
    // store (the resolver normalizes ambiguous arrangements to tempo 1.00,
    // one stderr line per timestamp — marker arrangements always enter) and
    // build the whole-song warp_frame_map with the loaded scale. Trim plays
    // no part (crossed/equal cleared above; an ambiguous trim at render
    // time falls back to untrimmed), so a persisted T view restores
    // whenever the map builds. Every input the walk consumes is in place by
    // this point: markers (parsed above, default zero-marker seeded) and
    // engine settings (strict block above). On failure — the tripwire-class
    // build failures only — force source view SILENTLY (a load is not a
    // user command; the next `t` entry surfaces the refusal through the
    // popup). Forcing 'S' intentionally means a later save persists
    // active_audio_view=S — the saved line reflects the view actually
    // shown.
    if (app.active_audio_view == 'T') {
        if (!validate_target_view_entry(
                app.warpmarkers.markers(),
                app.engine_settings.scale,
                audio.sample_rate(),
                static_cast<long>(audio.total_frames()))) {
            app.active_audio_view = 'S';
            // The tab-activation and playhead clamps above ran against the
            // target-domain total (live_total_frames consults the target map
            // cache while active_audio_view=='T'); with the view forced back
            // to source the source total governs, so re-clamp the viewport
            // and both tabs' playheads plus the live copy — cheap insurance
            // for the forced-S path (with the map unbuildable the earlier
            // clamps already fell back to the source total, so this is
            // usually a no-op).
            clamp_viewport_start(app, audio);
            app.tab_a.playhead_cursor_sample = clamp_playhead_to_live_domain(
                app.tab_a.playhead_cursor_sample, app, audio);
            app.tab_b.playhead_cursor_sample = clamp_playhead_to_live_domain(
                app.tab_b.playhead_cursor_sample, app, audio);
            app.playhead_cursor_sample = clamp_playhead_to_live_domain(
                app.playhead_cursor_sample, app, audio);
        }
    }

    // If the load landed us in target view — the parsed settings said 'T'
    // AND the loaded state passed the same validity walk that gates a
    // keyboard S → T entry (the gate above) — dispatch a fresh target
    // render now so the first Space press is ready without a first-edit
    // wait. The target buffer is empty on this sole load; ensure_ready's
    // is_dirty_=true (its construction default) falls through to
    // trigger(). No-op if active_audio_view=='S', including an invalid
    // target-view load just forced to source view: the eager preview waits
    // for a clean `t` entry. A load just loads — whatever the marker
    // stores hold, the parser resolver normalizes ambiguous arrangements
    // to tempo 1.00 when they first resolve (one stderr line per
    // timestamp); nothing gates or modals in the GUI.
    target_render.ensure_ready();

    gui.invalidate_region(0, 0, app.width, app.height);
    return true;
}
