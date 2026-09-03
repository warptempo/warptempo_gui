#include "file_loader.h"

#include "input_handler.h"   // validate_target_view_entry (load gate below)
#include "prompt.h"
#include "render_output_naming.h"
#include "selection.h"
#include "settings_io.h"
#include "target_render.h"
#include "warp_frame_map_view.h"

#include "marker_store_validate.h"   // first_past_eof_wall_defect

#include "audio_probe.h"
#include "wav_io.h"     // checked_audio_sample_count (the dry-run's allocation arm)

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

void apply_settings_engine_and_prefs(AppState& app, Viewport& viewport,
                                     const SettingsFile& sf) {
    app.engine_settings = sf.engine;
    app.follow_mode         = sf.follow;
    // The centered lamp loads with follow. The derivation memory resets with
    // it so the first pre-paint under a lit lamp derives the camera from the
    // loaded playhead — the invariant holds from the session's first frame
    // (the derivation point is main.cpp's pre-paint hook).
    app.centered_mode               = sf.centered;
    app.centered_derived_cursor     = -1;
    app.centered_derived_tab        = 0;
    app.centered_derived_audio_view = 0;
    app.centered_derived_scanner    = false;
    // Event-synchronized hit geometry: this routine (re)establishes the live
    // view from settings, so the displayed hit map and its viewport mirror go
    // COLD through their one owner — the map on screen reflects the OTHER
    // file's last target item pixels. Membership and the recorded cold-state
    // seam are at reset_displayed_target_basis (app_state.h); the ruling is at
    // the selector.
    reset_displayed_target_basis(app);
    // THE LOAD PATH HIDES THE TRIM REGION OVERLAY. It discards nothing — the
    // overlay is DERIVED from the trim (RegionState, app_state.h) and the load
    // brings its own — but a piece arriving with a stranger's window already lit
    // on the waveform is the wrong greeting, and hiding is what every other
    // turn-to-other-work route does. THE SOURCE LOAD IS ITS WHOLE POPULATION
    // since 2026-08-24: the `'` load-in-place stopped sharing this routine when
    // it stopped writing anything a source load writes beyond the engine block,
    // and it hides nothing — it brings no trim of its own to greet the user
    // with, the resting window being the one that was already on screen.
    app.region = RegionState{};
    // AND THE SEATED PINCH'S ANCHOR, for the same structural reason and on the
    // same line of argument the region hide above makes (codex round 21): the
    // three assignments below REPLACE the active view state wholesale, and this
    // routine is where that write lives — so the clear lives here rather than at
    // the caller. THE VALUES-ONLY CONTRACT STILL HOLDS otherwise: this is a
    // lifecycle END, not a side effect a caller could time differently, which is
    // why the Viewport parameter buys nothing else here. Membership and
    // derivation at clear_touch_zoom_seat's declaration (input_handler.h). A
    // no-op in practice at the one caller left: the source load runs once from
    // the startup tick before any input exists — and it is also what covers
    // load_file's own two direct writes to these fields (the pre-parse 'W' reset
    // and the forced 'S' of a failed target-view restore).
    clear_touch_zoom_seat(app, viewport);
    app.active_audio_view   = sf.active_audio_view;
    app.active_markers_view = sf.active_markers_view;
    app.active_tab_view     = sf.active_tab_view;
    // (THREE ASSIGNMENTS LEFT THIS ROUTINE 2026-08-27 with their keys. The
    // sidecar no longer carries playback_speed, gui_scale or audio_player: the
    // first retired whole, and the two DEVICE preferences moved to the per-device
    // config (where audio_player retired whole 2026-08-28 with the in-app
    // render player), which gui_main reads ONCE at startup before the window exists
    // (device_config.h) — a per-source routine is the wrong place to apply a
    // fact about the panel, and this load must not overwrite what the config
    // said.)
    // The waveform PICTURE's magnification LEVEL, applied verbatim. It is
    // consumed by the painter alone — the plate fingerprint
    // and the overview bar cache both key on it — so this routine's VALUES-ONLY
    // contract holds with no side effect at the caller's tail: the load's own
    // full rebuild repaints both surfaces.
    app.waveform_magnification_level = sf.waveform_magnification_level;
    // (`projects_repo` LEFT THIS ROUTINE 2026-08-27 with its key: the projects
    // home is the DEVICE config's now, read once by gui_main and never by a
    // source load — device_config.h.)
}

std::optional<GuiFailure> source_load_dry_run(
        const std::filesystem::path& source) {
    // EVERY REASON BELOW IS TWO CLAUSES (GuiFailure, failure.h — the
    // universal shape since 2026-09-02): the display is a NOTIFICATION
    // CARD'S ONE LINE — the Open project picker's third refusal — so every
    // path in it is named by the basename rule's composer
    // (shown_project_path, device_config.h: the project folder and the file,
    // never the projects path), while the diagnostic names the full path for
    // the stderr line the picker prints. A reason with no path in it is the
    // same words on both. The full spelling stays in `path`, which is what
    // the probe and the collision check are ASKED of.
    const std::string path = source.string();
    auto info = audio_probe(path);
    if (!info) {
        return path_failure("Source open failed for ", source,
                            shown_project_path(source), ": " + info.error());
    }
    if (info->sample_rate < 44100) {
        return plain_failure("Sample rate " +
                             std::to_string(info->sample_rate) +
                             " is below the 44100 floor");
    }
    // A PROPER SENTENCE (architect 2026-09-01, the capitalization sweep): this
    // read "<N> channels (stereo sources only)", which opened a card with a
    // digit and misagreed in number at one channel. Its sample-rate sibling
    // above already had the shape.
    if (info->channels != 2) {
        return plain_failure("The source is not stereo (" +
                             std::to_string(info->channels) + " channels)");
    }
    // THE DECODER'S ALLOCATION ARM, ASKED OF THE PROBED SHAPE. The decode
    // itself is deliberately not run here, but the predicate it would refuse
    // on is a PURE SHAPE CHECK — frames times channels against the decoded
    // float32 ceiling — and both numbers are already in hand, so the refusal
    // that would otherwise arrive after the old session was torn down arrives
    // at Enter instead. The same owner the read path calls (wav_io.h), so the
    // words are the load's own.
    if (auto samples = checked_audio_sample_count(info->frames,
                                                  info->channels);
        !samples) {
        return path_failure("Source open failed for ", source,
                            shown_project_path(source),
                            ": " + samples.error());
    }

    std::filesystem::path parent = source.parent_path();
    if (parent.empty()) parent = std::filesystem::path(".");
    const std::string stem = source.stem().string();
    const std::filesystem::path wm_path  = parent / (stem + ".warpmarkers");
    const std::filesystem::path tm_path  = parent / (stem + ".phaseresetmarkers");
    const std::filesystem::path set_path = parent / (stem + ".settings");

    // The three strict readers, each on a companion that is PRESENT by the one
    // presence predicate the real load asks (sidecar_present, settings_io.h) —
    // so a non-regular object wearing a sidecar's name is parsed and refused
    // here exactly as the load would refuse it, and only a genuine absence is
    // read as "the load will write the template" (a new project passes
    // trivially). A stat that fails is its own refusal, in the system's words.
    // Fresh stores and a fresh SettingsFile, discarded at the return.
    GuiWarpMarkers       warp;
    GuiPhaseResetMarkers phase_resets;
    SettingsTrim tab_a_trim;
    SettingsTrim tab_b_trim;
    auto wm_here = sidecar_present(wm_path);
    if (!wm_here) return wm_here.error();
    if (*wm_here) {
        if (auto r = warp.load(wm_path.string()); !r) {
            return path_failure("Invalid warp markers in ", wm_path,
                                shown_project_path(wm_path),
                                ": " + r.error());
        }
    }
    auto tm_here = sidecar_present(tm_path);
    if (!tm_here) return tm_here.error();
    if (*tm_here) {
        if (auto r = phase_resets.load(tm_path.string()); !r) {
            return path_failure("Invalid phase reset markers in ", tm_path,
                                shown_project_path(tm_path),
                                ": " + r.error());
        }
    }
    auto set_here = sidecar_present(set_path);
    if (!set_here) return set_here.error();
    if (*set_here) {
        auto sf = read_settings_file(set_path.string());
        if (!sf) {
            return path_failure("Invalid settings in ", set_path,
                                shown_project_path(set_path),
                                ": " + sf.error());
        }
        if (auto collision = render_output_source_collision(sf->engine, path)) {
            return two_path_failure(
                "Settings in ", set_path, shown_project_path(set_path),
                " would make the render output ", *collision,
                shown_project_path(*collision),
                " overwrite the source audio file");
        }
        tab_a_trim = sf->tab_a.trim;
        tab_b_trim = sf->tab_b.trim;
    } else {
        // No settings yet: the load stamps the full window on both tabs, which
        // is inside the wall by construction.
        const TrimState full = full_trim_window(info->frames);
        tab_a_trim.begin_frame = tab_b_trim.begin_frame = full.begin_frame;
        tab_a_trim.end_frame   = tab_b_trim.end_frame   = full.end_frame;
    }
    if (auto defect = first_past_eof_wall_defect(
            slice_to_warp_markers(warp.markers()),
            slice_to_phase_reset_markers(phase_resets.markers()),
            tab_a_trim, tab_b_trim, info->frames, info->sample_rate)) {
        return plain_failure(std::move(*defect));
    }
    return std::nullopt;
}

bool GuiFileLoader::load_file(const GuiProjectSource& project) {
    const std::string path = project.source.string();
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
                "warptempo_gui: Source open failed for '%s': %s; inputs are "
                "WAV only, so convert once at acquisition (e.g. with ffmpeg) "
                "and load the converted file\n",
                path.c_str(), source_info.error().c_str());
        } else {
            std::fprintf(stderr,
                "warptempo_gui: Source open failed for '%s': %s\n",
                path.c_str(), source_info.error().c_str());
        }
        return false;
    }

    // Rates below 44.1k are out of scope by ruling, and the whole-frame gesture
    // pixel guarantees assume the 44100 floor (higher rates only widen the margins).
    if (source_info->sample_rate < 44100) {
        std::fprintf(stderr,
            "warptempo_gui: Source load failed for '%s': sample rate %d is "
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
            "warptempo_gui: Source load failed for '%s': %d channels (stereo "
            "sources only)\n",
            path.c_str(), source_info->channels);
        return false;
    }

    app.loading       = true;
    app.queue_progress_text = "Loading...";
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
    // Open at the working zoom (2.4 s) for normal files; a file too short for
    // it opens at its effective ceiling (whole-song-visible) instead.
    app.zoom_level = std::min(kWorkingZoomLevel, effective_max_zoom_level(
        waveform_area(app).w, audio.total_frames(), audio.sample_rate()));
    clamp_viewport_start(app, audio);

    // (NO PLAYBACK-SPEED OR gui_scale RESET HERE ANY MORE — 2026-08-27. The
    // speed retired with its key, and the scale is the DEVICE's: gui_main has
    // already read it out of the device config and applied it before this load
    // begins, so re-seeding it to 100 here would throw away the live value the
    // whole file exists to carry.)
    // Mirror for the waveform magnification level: construction-state
    // default before the .settings parse below, which the required key always
    // overwrites. It needs no push at all — the painter reads
    // app.waveform_magnification_level directly.
    app.waveform_magnification_level = 0;

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
    // THE WINDOW TITLE IS THE PROJECT NAME (architect 2026-08-01, replacing the
    // full source path + " - warptempo_gui"): the name of the project's folder
    // under the projects path — `<projects_path>/K551/take3.wav` shows "K551".
    // The folder is what the architect calls the project, and it reads and
    // versions better than either the audio filename or the output `title=`
    // settings key (a different thing entirely: that one names the render). The
    // " - warptempo_gui" tail and the dirty asterisk are composed by the
    // title's owner, GuiPlatform::apply_window_title.
    //
    // TAKEN FROM THE RESOLVED PROJECT, never derived from the source's parent:
    // the model already answered which folder this is, and a derivation off the
    // canonical source would answer the LINK TARGET's folder wherever a symlink
    // is in the way — a name the user never chose, and one the Open project
    // picker's already-open compare would then miss. This is the field's ONE producer
    // (the statement is at AppState::project_name).
    gui.set_project_title(project.name);
    app.project_name = project.name;

    create_if_missing(wm_path, "0|1.00\n");
    // The empty file is the canonical blank phase reset sidecar: resets have
    // no mandatory first marker, so the seed is empty content, unlike warp's
    // seeded first-marker line.
    create_if_missing(tm_path, "");
    // The first-open template stamps the FULL trim window for this source on
    // both tabs, so it needs the loaded total (the `-1` unset spelling it used
    // to write no longer parses).
    create_if_missing(set_path,
                      format_default_settings_template(stem,
                                                       audio.total_frames()));

    // Load the markers file. A present-but-malformed sidecar aborts the
    // load: GuiWarpMarkers::load clears the store before parsing, so a parse
    // failure would leave an empty in-memory store while the authored file
    // sits on disk. GuiSaveOps::save writes the stores unconditionally on
    // Ctrl+S, so continuing would let one later save overwrite the authored
    // sidecar. Aborting preserves the on-disk file, the same contract as a
    // corrupt audio file or invalid engine settings below.
    app.warpmarkers.clear();
    app.phaseresetmarkers.clear();
    selection.clear_selection();
    app.active_markers_view    = 'W';
    app.drag = DragState{};
    app.region_drag = RegionDragState{};
    app.pending_marker_press = PendingMarkerPress{};
    app.pending_trim_drag = PendingTrimDrag{};
    app.pending_click = PendingClickAct{};
    app.trim_drag = TrimDragState{};
    app.scroll_drag = ScrollDragState{};
    app.overview_drag = OverviewDragState{};
    app.double_click = DoubleClickCandidate{};
    app.trim_bar_press = TrimBarPressSeed{};
    // Belt-and-braces: dissolve the shift-range-select anchor on load (the
    // selection.clear_selection() above already clears it — the Selection
    // mutators are the anchor's only owners).
    app.shift_range_anchor = -1;
    // The STICKY CTRL rides the same belt for the same reason: a new piece
    // starts with no mode standing, and the clear_selection above has already
    // ended it (the two bits share the Selection chokepoint; the contract is
    // at AppState::add_to_selection).
    app.add_to_selection = false;
    // (The displayed hit map AND the trim region overlay's visibility are reset in
    // apply_settings_engine_and_prefs, this load's own view-establishment
    // routine, not here.)
    // Project trim is not cleared implicitly by the fresh-ViewState assignment
    // (it lives on AppState now). SEED IT TO THE FULL WINDOW explicitly before
    // the initial-playhead read: the window is always set (2026-07-30), so
    // "reset" means [0, total-1] for THIS source, through the one seeding owner
    // full_trim_window (app_state.h). The audio is already loaded above, so the
    // total is known here. A .settings always carries the four trim keys, so its
    // parse always overwrites this; the seed covers the no-.settings /
    // first-open path and keeps a launch from reading a leftover pair out of the
    // base-state struct.
    app.trim = full_trim_window(audio.total_frames());
    app.editor_text_drag = EditorTextDragState{};
    // Fresh file = fresh history. Both stacks cleared; the loaded state
    // is the saved baseline (signed_distance = 0, valid).
    app.history.reset();
    app.dirty              = false;
    app.warp_dirty         = false;
    app.phase_reset_dirty    = false;
    app.settings_dirty     = false;
    // The load is the ONE dirty transition that does not go through
    // Undo::recompute_dirty (it assigns the four flags outright), so it carries
    // the title's dirty half itself. The other transition site is
    // recompute_dirty's tail — those two are the whole inventory, since the four
    // flags above have no other writer in the tree.
    gui.set_title_dirty(false);
    if (auto r = app.warpmarkers.load(wm_path.string()); !r) {
        std::fprintf(stderr,
            "warptempo_gui: Source load aborted: invalid warp markers in "
            "'%s': %s\n",
            wm_path.string().c_str(), r.error().c_str());
        gui.request_exit();
        return false;
    } else {
        std::fprintf(stderr, "warptempo_gui: Parsed %zu markers from %s\n",
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
            "warptempo_gui: Source load aborted: invalid phase reset "
            "markers in '%s': %s\n",
            tm_path.string().c_str(), r.error().c_str());
        gui.request_exit();
        return false;
    } else {
        std::fprintf(stderr, "warptempo_gui: Parsed %zu phase_resets from %s\n",
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
    app.viewport_start_sample = app.playhead_cursor_sample;
    clamp_viewport_start(app, audio);

    // Seed both tabs with the freshly-computed default post-load state.
    // Parsed .settings values overwrite per-key below.
    ViewState default_tab;
    default_tab.viewport_start_sample = app.viewport_start_sample;
    default_tab.zoom_level            = app.zoom_level;
    default_tab.playhead_cursor_sample       = app.playhead_cursor_sample;
    // Per-tab trim seeds the FULL window for this source, exactly like the live
    // app.trim seed above: a ViewState's default-constructed pair is [0, 0],
    // canonical only at total == 1, so the total has to be applied here rather
    // than left to the struct default.
    default_tab.trim                  = full_trim_window(audio.total_frames());
    app.tab_a          = default_tab;
    app.tab_b          = default_tab;
    app.engine_settings = EngineSettings{};

    // The whole-file strict settings schema (read_settings_file,
    // settings_file.h), shared verbatim with warptempo_cli so a sidecar set
    // is loadable in both products or neither. Any schema violation —
    // unknown key, duplicate, malformed value, missing required
    // key — aborts the load with the first error,
    // the same shape as a corrupt audio file: the load fails and the
    // process exits, so the user never sees a half-loaded state. Persisted
    // viewport/playhead positions are display scratch, not authored data:
    // they carry no audio-relative range check and apply verbatim, the
    // runtime clamps owning any out-of-range value — clamp_viewport_start for
    // the viewport, and the live-domain playhead clamp
    // (clamp_playhead_to_live_domain, both tab snapshots) at the end of this
    // block, the earliest point the persisted S/T domain is computable. Trim
    // bound ordering is normalized, not checked: a per-tab pair with end <=
    // begin RESETS both of that tab's bounds to the song edges after the
    // adversarial walls run (the crossed-reset block below the past-EOF guard),
    // and the render's ambiguous-trim fallback renders untrimmed — never a
    // refusal.
    {
        auto sf_r = read_settings_file(app.settings_path);
        if (!sf_r) {
            std::fprintf(stderr,
                "warptempo_gui: Source load aborted: invalid settings in "
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
        // adversarial settings refusals. The shared predicate is the settings
        // editor's own, and its rationale is stated at that commit refusal.
        if (auto collision =
                render_output_source_collision(sf.engine,
                                               app.source_audio_path)) {
            std::fprintf(stderr,
                "warptempo_gui: Source load aborted: settings in '%s' would "
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
        // waveform_magnification_level), VALUES ONLY. The
        // one side effect that consumes these (on_resize) stays below where it
        // always ran.
        apply_settings_engine_and_prefs(app, viewport, sf);
        // Per-tab trim: both bounds are always meaningful in the schema (the
        // `-1` unset spelling died 2026-07-30 and is now a load-fatal malformed
        // value), so the pair applies verbatim, overwriting the full-window
        // seed above. A crossed/equal pair normalizes in the reset block after
        // the past-EOF guard below (order rationale there).
        app.tab_a.trim.begin_frame = sf.tab_a.trim.begin_frame;
        app.tab_a.trim.end_frame   = sf.tab_a.trim.end_frame;
        app.tab_b.trim.begin_frame = sf.tab_b.trim.begin_frame;
        app.tab_b.trim.end_frame   = sf.tab_b.trim.end_frame;
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
    // Space's lead-in launch offset) rather than at first gesture use.
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
            s.begin_frame = t.begin_frame;
            s.end_frame   = t.end_frame;
            return s;
        };
        const auto detail = first_past_eof_wall_defect(
            slice_to_warp_markers(app.warpmarkers.markers()),
            slice_to_phase_reset_markers(app.phaseresetmarkers.markers()),
            trim_of(app.tab_a.trim), trim_of(app.tab_b.trim),
            audio.total_frames(), audio.sample_rate());
        if (detail) {
            // An APPENDED reason is lowercase (the rule and its one owner
            // lowercase_initial are at notifications.h): this seam appends
            // the frozen producer's sentence, while the dry-run card above
            // uses that same string WHOLE and so leaves it capitalized.
            std::fprintf(stderr,
                "warptempo_gui: Source load aborted: %s\n",
                lowercase_initial(*detail).c_str());
            gui.request_exit();
            return false;
        }
    }

    // Load crossed-reset: a persisted per-tab trim pair with end <= begin —
    // exact integer compare — RESETS BOTH of that tab's bounds to the song
    // edges, one stderr line per reset tab (crossed/equal cannot REST anywhere;
    // the trim sibling of the marker normalizations, gesture half at
    // auto_clear_crossed_trim, which spells the same full-window-first
    // precedence so a one-frame source's canonical [0, 0] is not read as
    // crossed).
    //
    // THIS ARM HAS NO GUI PRODUCER LEFT, AND IS KEPT ANYWAY (architect
    // 2026-08-02). No gesture can write a crossed pair into a .settings any
    // more: every trim commit route runs auto_clear_crossed_trim before it
    // rests, the bound-set clicks refuse a click on or past the partner
    // outright, the SWEEP orders its pair and floors its width, and since
    // 2026-08-02 the
    // single-bound drag CLAMPS INCLUSIVELY AT ITS PARTNER, so a drag hands the
    // commit tail begin == end and never a crossing. What reaches here is
    // therefore a HAND-EDITED pair only — the shape the load boundary otherwise
    // hard-fails (the -1 spelling, font_size, a past-EOF bound). The architect
    // ruled the exception deliberately: a crossed pair is a plausible
    // slip of the hand in an editable file, and the answer to it is the Shift+[
    // full-window reset applied at load rather than a refused source. This is
    // the ONE crossed-pair recovery, and it is semantically Shift+[ — the same
    // full window, through the same full_trim_window formula.
    //
    // Deliberately AFTER the adversarial past-EOF
    // hard-fail above: a crossed pair containing a past-EOF bound must
    // still abort the load, identically in warptempo_cli (which loads the
    // same values, runs the same wall check, and has no reset) — resetting
    // first would swallow the adversarial defect in one product only. The
    // live app.trim mirror was copied from the active tab at activation
    // above, so it re-syncs after the resets. Render-entry sidecars reach
    // memory through load_render_entry_in_place's own application path and
    // deliberately skip this: they are written once at dispatch from a live
    // store that can no longer rest crossed — trusted, no re-check.
    {
        const int64_t total = audio.total_frames();
        const auto clear_crossed_tab = [total](TrimState& t, char tab_name) {
            if (trim_is_full_window(t, total)) return;
            if (t.end_frame <= t.begin_frame) {
                t = full_trim_window(total);
                std::fprintf(stderr,
                    "warptempo_gui: Tab %c trim bounds crossed or equal; "
                    "both reset to the song edges\n",
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
            "warptempo_gui: Playback disabled; space bar will no-op.\n");
    }
    // Route the load's geometry consequences through the same rebuild path a
    // window resize performs: on_resize re-clamps zoom/viewport against
    // the strip geometry, the next redraw re-measures
    // the grid metrics, and the cache fingerprints (area dims keyed off the
    // strip heights, which sum the per-lane metrics — main.cpp's
    // top_lane_height table is the one place that enumerates which metric
    // sizes which lane) rebuild the waveform/flag surfaces. The full-window
    // invalidation at the end of this load supplies the damage, mirroring the
    // resize path's full-surface damage.
    //
    // (THE gui_scale PUSH AND THE TOUCH-SLOP PUSH THAT RODE IT LEFT THIS SITE
    // 2026-08-27. The scale is a DEVICE fact now, read out of the device config
    // by gui_main before the window is even created, so it is already installed
    // — and already right on the first painted frame, which it never was while
    // it arrived with the source — long before this load runs. The INIT road
    // for both pushes is that one place; the inventory is at
    // GuiInputCore::set_touch_slop_px.)
    paint_handler.on_resize(app.width, app.height);

    const double load_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::fprintf(stderr,
                 "warptempo_gui: Loaded %s: sr=%d, channels=%d, frames=%lld, "
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
    // no part (crossed/equal reset above; an ambiguous trim at render
    // time falls back to untrimmed), so a persisted T view restores
    // whenever the map builds. Every input the walk consumes is in place by
    // this point: markers (parsed above, default zero-marker seeded) and
    // engine settings (strict block above). On failure — the tripwire-class
    // build failures only — force source view SILENTLY, naming the cause on
    // stderr and nowhere else. THE `t` ENTRY REFUSES IDENTICALLY SINCE
    // 2026-08-30 (architect): one predicate, one answer, the same stderr
    // sentence at both surfaces (the ruling is at the predicate's
    // declaration, input_handler.h). Forcing 'S' intentionally means a later
    // save persists active_audio_view=S — the saved line reflects the view
    // actually shown.
    if (app.active_audio_view == 'T') {
        auto entry = validate_target_view_entry(
            app.warpmarkers.markers(),
            app.engine_settings.scale,
            audio.sample_rate(),
            static_cast<long>(audio.total_frames()));
        if (!entry) {
            std::fprintf(stderr,
                         "warptempo_gui: Target view entry refused: %s\n",
                         entry.error().c_str());
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

    // COINCIDENCE AUTO-SELECT, the load chokepoint (the rule, the formula and the
    // authoritative call-site inventory live at auto_select_marker_at_playhead,
    // input_pointer.cpp / input_handler.h). A
    // parsed `.settings` band carries a playhead but never a selection, so the
    // session opens with a marker selected exactly when the restored cursor lands
    // on one — the entry reads like a marker click, with the cursor standing on
    // the selected flag from the first frame. PLACED HERE, past the target-view
    // validity gate and its forced-S re-clamps: the scan's conversion is
    // domain-dependent (source view identity, target view through the warp map),
    // so it must read the view the session actually opens in and the playhead's
    // final value. The full-window invalidate at the tail paints the result.
    auto_select_marker_at_playhead(app, audio, selection, viewport);

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
