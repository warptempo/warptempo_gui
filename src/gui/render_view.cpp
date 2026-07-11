#include "render_view.h"

#include "marker_store_validate.h"
#include "phaseresetmarkers.h"
#include "render_cache.h"
#include "render_pipeline.h"
#include "settings_io.h"
#include "target_render.h"
#include "trimmer.h"
#include "warp_frame_map_build.h"
#include "warpmarkers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <utility>
#include <vector>

// Render-view cluster: batch-folder enumeration, entry load/refresh, the
// per-entry .settings view-state read/write, and the source-audio
// park/restore around render-view sessions. Reaches viewport,
// active_views, and selection through the struct's reference members
// (clamp_viewport_start and compute_trim_samples are free functions
// declared in app_state.h); source_audio_held is the parked source buffer
// while a render's audio is swapped in.

// Enumerate the flat render-view list under <source_parent>/renders/.
// Returns an empty vector if no source path is set or if the renders root
// contains no valid entries.
std::vector<AppState::RenderViewEntry>
GuiRenderView::enumerate_render_view_list() {
    std::vector<AppState::RenderViewEntry> out;
    if (app.source_audio_path.empty()) return out;
    std::filesystem::path src(app.source_audio_path);
    std::filesystem::path src_parent = src.parent_path();
    if (src_parent.empty()) src_parent = std::filesystem::path(".");
    const std::filesystem::path renders_root = src_parent / "renders";
    std::error_code ec;
    if (!std::filesystem::is_directory(renders_root, ec)) return out;

    auto leading_int = [](const std::string& s, size_t& end_out) -> int {
        int v = 0;
        size_t i = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + (s[i] - '0');
            ++i;
        }
        end_out = i;
        return v;
    };

    struct BatchSlot { int idx; std::filesystem::path path; };
    std::vector<BatchSlot> batches;
    for (const auto& de :
         std::filesystem::directory_iterator(renders_root, ec)) {
        if (!de.is_directory()) continue;
        const std::string name = de.path().filename().string();
        size_t end = 0;
        const int v = leading_int(name, end);
        if (end == 0 || end >= name.size() || name[end] != '_') continue;
        batches.push_back({v, de.path()});
    }
    std::sort(batches.begin(), batches.end(),
              [](const BatchSlot& a, const BatchSlot& b) {
                  return a.idx < b.idx;
              });

    for (const auto& b : batches) {
        struct WavSlot {
            int idx;
            std::filesystem::path path;
            std::string basename;
        };
        std::vector<WavSlot> wavs;
        for (const auto& fe :
             std::filesystem::directory_iterator(b.path, ec)) {
            if (!fe.is_regular_file()) continue;
            if (fe.path().extension() != ".wav") continue;
            const std::string stem = fe.path().stem().string();
            size_t end = 0;
            const int v = leading_int(stem, end);
            if (end == 0) continue;
            if (end != stem.size() && stem[end] != '_') continue;
            wavs.push_back({v, fe.path(), stem});
        }
        std::sort(wavs.begin(), wavs.end(),
                  [](const WavSlot& a, const WavSlot& b) {
                      return a.idx < b.idx;
                  });
        for (auto& w : wavs) {
            AppState::RenderViewEntry e;
            e.batch_folder = b.path;
            e.basename     = std::move(w.basename);
            e.wav_path     = std::move(w.path);
            out.push_back(std::move(e));
        }
    }
    return out;
}

// -- <basename>.settings snapshot --------------------------------------
//
// Per-render zoom/viewport/playhead/W-P persistence lives on the entry's
// standard `.settings` snapshot (written at render time by the pipeline
// beside the wav, browse defaults on the commit tab). The live render-view
// state is captured at navigation/exit boundaries through the strict
// read-modify-write helper below; the entry loader applies the file's view
// state on arrival.

std::filesystem::path GuiRenderView::settings_path(
        const AppState::RenderViewEntry& e) {
    return e.batch_folder / (e.basename + ".settings");
}

// Atomic view-state update of the entry's .settings (every other key is
// preserved from the strict parse). The browsed active_markers_view rides
// along so the entry restores the W/P mode it was last viewed in.
// Failures are non-fatal here — logged once by the underlying helper and
// otherwise discarded (a refused autosave costs the persisted view state,
// never the session).
void GuiRenderView::write_settings_for(
        const AppState::RenderViewEntry& e) {
    update_settings_view_state(
        this->settings_path(e),
        app.viewport_start_sample,
        app.zoom_level,
        app.playhead_cursor_sample,
        app.active_markers_view);
}

// Capture (size, mtime_seconds) for a wav path.
// Errors → (0, 0), interpreted as "no valid stat tuple" by callers
// (forces a mismatch on compare). Uses stat() directly because
// C++17's std::filesystem::file_time_type isn't portably
// convertible to system_clock; stat's st_mtime is seconds-since-
// epoch, which is what the persisted field stores.
std::pair<uintmax_t, int64_t> GuiRenderView::wav_stat_tuple(
        const std::filesystem::path& p) {
    struct stat st{};
    if (::stat(p.c_str(), &st) != 0) return {0, 0};
    return {static_cast<uintmax_t>(st.st_size),
            static_cast<int64_t>(st.st_mtime)};
}

// Stash the live selection into the active
// RenderViewEntry's matching-mode slot, along with the wav's
// current stat tuple. No-op when no entry is active. Called from
// the render-view exit path and from the batch-nav path
// (Shift+Left/Right) before the destination is loaded. The
// OTHER-mode slot was last written when active_markers_view flipped
// away from it via switch_active_markers_view_to (or never written if
// the user has not flipped mode in this render-view session);
// either way it is current at stash time.
void GuiRenderView::stash_render_view_selection_to_active_entry() {
    if (app.render_view.index < 0 ||
        app.render_view.index >=
            static_cast<int>(app.render_view.list.size())) {
        return;
    }
    auto& e = app.render_view.list[app.render_view.index];
    if (app.active_markers_view == 'P') {
        e.state.phase_reset_selected      = app.selected_markers;
        e.state.phase_reset_last_selected = app.last_selected_marker;
    } else {
        e.state.warp_selected           = app.selected_markers;
        e.state.warp_last_selected      = app.last_selected_marker;
    }
    const auto stat = this->wav_stat_tuple(e.wav_path);
    e.persisted_size  = stat.first;
    e.persisted_mtime = stat.second;
}

// Build the full archival re-render request for a stale entry from its
// already-loaded snapshot set (markers, engine block, recipe trim). The
// request names the entry's own batch folder and basename, so the rebuild
// republishes the wav, its .fingerprint, and the full sidecar set through
// do_render's normal staged/gated publication path.
//
// Shared resources come from the PARKED source, not the displayed render:
// do_render's wav arm reads req.source_samples directly (it never re-reads
// the source file — the probe only supplies metadata), and its
// load-identity hardfail compares the identity pair against the source
// path's current stat, so both must name the source audio. The caller
// passes source_audio_held, or `audio` on the first-entry-not-yet-parked
// case, mirroring the guard-domain fallback above the D1 guards.
//
// req.authoring reproduces the entry's own snapshot from its parsed
// .settings, so the republished .rendersettings authoring block matches
// what the entry carried. The republished .settings view keys reset to the
// dispatch writer's browse defaults — acceptable, the entry was stale.
static RenderRequest build_entry_rebuild_request(
        const AppState& app,
        const AppState::RenderViewEntry& e,
        const std::vector<GuiWarpMarker>& snapshot_warp,
        const std::vector<GuiPhaseResetMarker>& snapshot_phase_resets,
        const SettingsFile& settings,
        const SettingsFileTab& commit_tab,
        const GuiAudio& parked_source,
        RenderCache& render_cache) {
    const SettingsTrim& recipe_trim = commit_tab.trim;
    RenderRequest req = build_render_request(
        app.source_audio_path, snapshot_warp, snapshot_phase_resets,
        settings.engine,
        recipe_trim.has_begin, recipe_trim.begin_frame,
        recipe_trim.has_end,   recipe_trim.end_frame,
        e.batch_folder.string(), e.basename);
    req.render_cache        = &render_cache;
    req.source_samples      = parked_source.samples_shared();
    req.source_total_frames = parked_source.total_frames();
    req.source_load_size    = parked_source.source_load_size();
    req.source_load_mtime   = parked_source.source_load_mtime();
    req.authoring.valid             = true;
    req.authoring.active_tab        = settings.active_tab_view;
    req.authoring.active_audio_view = settings.active_audio_view;
    req.authoring.has_trim_begin    = recipe_trim.has_begin;
    req.authoring.trim_begin_frame  = recipe_trim.begin_frame;
    req.authoring.has_trim_end      = recipe_trim.has_end;
    req.authoring.trim_end_frame    = recipe_trim.end_frame;
    req.authoring.zoom_level        = commit_tab.zoom;
    req.authoring.viewport_start    = commit_tab.viewport_start;
    req.authoring.playhead          = commit_tab.playhead;
    req.authoring.active_markers_view = settings.active_markers_view;
    req.authoring.playback_speed      = settings.playback_speed;
    req.authoring.follow              = settings.follow;
    req.authoring.font_size           = settings.font_size;
    return req;
}

// Loads the render at app.render_view.list[index] into the active `audio`,
// parking the source audio on first entry. Entries are program-written
// snapshots, read STRICTLY: <basename>.warpmarkers /
// <basename>.phaseresetmarkers through the standard store loaders and
// <basename>.settings through read_settings_file. Any refusal — a read
// failure, a past-EOF wall defect, or a snapshot whose map cannot rebuild —
// is adversarial: one stderr line, first error only, the entry refuses to
// display (return false, prior state preserved). Display positions derive
// live from the snapshot through derive_render_display_positions — the same
// helper the pipeline's display-sidecar writer serializes — so the
// displayed positions are exactly what the old display sidecars carried.
// Computes F_begin/F_end against the cached source sr/total. Stops playback
// before the swap and re-binds the playback device.
//
// When the destination entry's persisted stat tuple matches the wav's current
// stat, restores the persisted selection. Mismatch leaves the live selection
// empty.
bool GuiRenderView::load_render_view_at(int index) {
    if (index < 0 ||
        index >= static_cast<int>(app.render_view.list.size())) {
        return false;
    }
    auto& e = app.render_view.list[index];
    // Leaving target view for render view, same as the T→S toggle: cancel any
    // in-flight target render so it does not keep pegging the cores under
    // render-view playback and cannot rebind playback to target_buffer behind
    // the render-view binding when it completes. No-op when nothing is updating
    // (render-to-render navigation).
    target_render.cancel_in_flight_update();

    GuiAudio next;
    if (!next.load(e.wav_path.string(), {})) {
        std::fprintf(stderr,
            "warptempo_gui: render-view: failed to load %s\n",
            e.wav_path.string().c_str());
        return false;
    }

    // Strict snapshot reads. The source-domain marker pair is the same set
    // Ctrl+Alt+C commit reloads when promoting a render into authoring
    // memory; the .settings snapshot carries the entry's engine recipe,
    // recipe trim (on the commit tab), and per-entry browse view state.
    const std::filesystem::path wm_path =
        e.batch_folder / (e.basename + ".warpmarkers");
    const std::filesystem::path pm_path =
        e.batch_folder / (e.basename + ".phaseresetmarkers");
    const std::filesystem::path st_path = this->settings_path(e);

    std::vector<GuiWarpMarker> snapshot_warp;
    {
        GuiWarpMarkers m;
        auto r = m.load(wm_path.string());
        if (!r) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: load failed for '%s': %s\n",
                wm_path.string().c_str(), r.error().c_str());
            return false;
        }
        snapshot_warp = m.markers();
    }
    std::vector<GuiPhaseResetMarker> snapshot_phase_resets;
    {
        GuiPhaseResetMarkers t;
        auto r = t.load(pm_path.string());
        if (!r) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: load failed for '%s': %s\n",
                pm_path.string().c_str(), r.error().c_str());
            return false;
        }
        snapshot_phase_resets = t.markers();
    }
    const auto settings = read_settings_file(st_path.string());
    if (!settings) {
        std::fprintf(stderr,
            "warptempo_gui: render-view: load failed for '%s': %s\n",
            st_path.string().c_str(), settings.error().c_str());
        return false;
    }
    // The commit tab (named by active_tab_view) carries the recipe trim
    // that shaped this render and the browse view state autosave owns.
    const SettingsFileTab& commit_tab =
        (settings->active_tab_view == 'B') ? settings->tab_b
                                           : settings->tab_a;
    const SettingsTrim& recipe_trim = commit_tab.trim;

    // Guard domain: the PARKED source's totals — the snapshot markers and
    // trim are source-domain, and while render view is up the live `audio`
    // holds a displayed render, never the guard domain (the same rule the
    // Ctrl+Alt+C commit guards apply). On first entry the source is not
    // parked yet and still sits in `audio` (the park below moves it), so
    // read whichever holds it.
    const bool source_parked = source_audio_held.total_frames() > 0;
    const int64_t source_total_frames = source_parked
        ? source_audio_held.total_frames() : audio.total_frames();
    const long source_sample_rate = static_cast<long>(
        source_parked ? source_audio_held.sample_rate()
                      : audio.sample_rate());

    // Adversarial guards, the render-invalid-cannot-display ruling: a
    // snapshot that cannot rebuild its map cannot be displayed. Past-EOF
    // walls first, one call per candidate file so the message names the
    // offending sidecar (the call order — warp markers, phase resets, trim
    // — is the validator's own internal order, so the first offender
    // reported matches the single-call composite).
    {
        const std::vector<WarpMarker> cand_warp =
            slice_to_warp_markers(snapshot_warp);
        const std::vector<PhaseResetMarker> cand_resets =
            slice_to_phase_reset_markers(snapshot_phase_resets);
        if (auto detail = first_past_eof_wall_defect(
                cand_warp, {}, SettingsTrim{}, SettingsTrim{},
                source_total_frames, source_sample_rate)) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: past-EOF wall defect for "
                "'%s': %s\n",
                wm_path.string().c_str(), detail->c_str());
            return false;
        }
        if (auto detail = first_past_eof_wall_defect(
                {}, cand_resets, SettingsTrim{}, SettingsTrim{},
                source_total_frames, source_sample_rate)) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: past-EOF wall defect for "
                "'%s': %s\n",
                pm_path.string().c_str(), detail->c_str());
            return false;
        }
        if (auto detail = first_past_eof_wall_defect(
                {}, {}, settings->tab_a.trim, settings->tab_b.trim,
                source_total_frames, source_sample_rate)) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: past-EOF wall defect for "
                "'%s': %s\n",
                st_path.string().c_str(), detail->c_str());
            return false;
        }
    }

    // Rebuild the FULL map the render was derived from: the same
    // resolve-then-build every render path runs, against the parked
    // source's totals and the entry's engine scale.
    auto resolved_warp_markers = resolve_warp_markers_for_render(
        slice_to_warp_markers(snapshot_warp), source_sample_rate);
    if (!resolved_warp_markers) {
        std::fprintf(stderr,
            "warptempo_gui: render-view: snapshot rejected for '%s': %s\n",
            wm_path.string().c_str(), resolved_warp_markers.error().c_str());
        return false;
    }
    auto full_map_r = build_warp_frame_map(
        *resolved_warp_markers, settings->engine.scale,
        source_sample_rate, static_cast<long>(source_total_frames));
    if (!full_map_r) {
        std::fprintf(stderr,
            "warptempo_gui: render-view: map build failed for '%s': %s\n",
            wm_path.string().c_str(), full_map_r.error().c_str());
        return false;
    }
    std::vector<WarpFrameMapSegment> full_warp_frame_map =
        std::move(*full_map_r);

    if (recipe_trim.has_begin || recipe_trim.has_end) {
        auto tv = validate_trim_frames(
            recipe_trim.has_begin, recipe_trim.begin_frame,
            recipe_trim.has_end, recipe_trim.end_frame,
            source_total_frames, full_warp_frame_map);
        if (!tv) {
            std::fprintf(stderr,
                "warptempo_gui: render-view: trim validation failed for "
                "'%s': %s\n",
                st_path.string().c_str(), tv.error().c_str());
            return false;
        }
    }

    // Display follows the snapshot (ruling 4): if the entry's on-disk
    // artifact does not match what its snapshot would render — an
    // accidentally deleted or regenerated wav or .fingerprint; dispatch
    // always writes matching pairs — the entry is NOT displayed as-is. The
    // GUI re-renders it in full from the snapshot, republishing the artifact
    // and its .fingerprint through the normal staged/gated publication path,
    // so disk and display never diverge. The snapshot fingerprint is the
    // same render_fingerprint a dispatch of this exact recipe computes:
    // source path + the PARKED source's load identity (the guard-domain
    // fallback above covers the first-entry case), the source sample rate,
    // the snapshot stores, the entry's engine block, and the recipe trim;
    // the compare is the same fingerprint_sidecar_matches rung do_render's
    // reuse path runs (a missing .fingerprint is a mismatch). Placed after
    // the adversarial guards and before any state mutation (playback stop /
    // park / store writes), so a refused entry preserves prior state.
    //
    // The rebuild is a DERIVED dispatch, not a user command: a passive
    // navigation must never kill an explicit render or a running batch, so
    // on a busy worker it refuses the entry with one stderr line and the
    // user retries after the worker drains. When idle it dispatches through
    // the single-archival chokepoint with the session fingerprint armed (an
    // identical re-dispatch no-ops; a fingerprint-matching preview trigger
    // waits and adopts). Either way the entry does not display; there is no
    // retry loop or auto-reload — the user's next navigation onto the entry
    // matches and displays.
    {
        const GuiAudio& parked_source =
            source_parked ? source_audio_held : audio;
        RenderFileIdentity source_identity;
        source_identity.size  = parked_source.source_load_size();
        source_identity.mtime = parked_source.source_load_mtime();
        std::vector<uint8_t> snapshot_fingerprint = render_fingerprint(
            app.source_audio_path, source_identity,
            static_cast<int>(source_sample_rate),
            snapshot_warp, snapshot_phase_resets, settings->engine,
            recipe_trim.has_begin, recipe_trim.begin_frame,
            recipe_trim.has_end,   recipe_trim.end_frame);
        if (!fingerprint_sidecar_matches(e.wav_path.string(),
                                         snapshot_fingerprint)) {
            RenderRequest req = build_entry_rebuild_request(
                app, e, snapshot_warp, snapshot_phase_resets, *settings,
                commit_tab, parked_source, target_render.render_cache);
            const bool dispatched =
                dispatch_archival_render_if_idle &&
                dispatch_archival_render_if_idle(
                    std::move(req), std::move(snapshot_fingerprint));
            if (dispatched) {
                std::fprintf(stderr,
                    "warptempo_gui: render-view: fingerprint mismatch for "
                    "'%s'; re-rendering from snapshot\n",
                    e.wav_path.string().c_str());
            } else {
                std::fprintf(stderr,
                    "warptempo_gui: render-view: fingerprint mismatch for "
                    "'%s'; renderer busy, retry after the current render\n",
                    e.wav_path.string().c_str());
            }
            return false;
        }
    }

    // Keep-filter the display stores through the SAME helper the pipeline's
    // display-sidecar writer serializes. The KEPT marker structs carry
    // their AUTHORED time_frame unchanged — render view no longer bakes
    // quantized render-domain positions into the stores; it translates
    // live through the snapshot map below, exactly as target view
    // translates through the live map. (The helper's parallel fractional
    // render-domain vectors still serve the .render* display-sidecar
    // writer; here only the keep-filtering and the crop origin are
    // consumed.) Displayed pixels are unchanged: the old baked position
    // was snap_authored_frame(tgt(F) - crop_begin) computed at load; the
    // new one is nearbyint(tgt(F)) - crop_begin computed at use, and
    // crop_begin is an integer-valued double (an llrint result), so
    // banker's rounding commutes with the integer translation —
    // nearbyint(x - k) == nearbyint(x) - k for integer k — and the two
    // are identical.
    RenderDisplayPositions display = derive_render_display_positions(
        snapshot_warp, snapshot_phase_resets, full_warp_frame_map,
        recipe_trim.has_begin, recipe_trim.begin_frame,
        recipe_trim.has_end, recipe_trim.end_frame,
        source_total_frames);
    std::vector<GuiWarpMarker> loaded_warp =
        std::move(display.warp_markers);
    std::vector<GuiPhaseResetMarker> loaded_phase_resets =
        std::move(display.phase_resets);

    playback.stop();
    playback.shutdown();
    app.playhead_scanner_active = false;
    app.playhead_scanner_restore_pending = false;
    app.playhead_scanner_endpoint_painted = false;
    app.playhead_scanner_sample = 0;
    viewport.clear_hover_popup();

    // Snapshot the live authoring playhead/viewport/zoom into the
    // active tab's slot before we overwrite `audio`. restore_source_audio
    // reads it back on render-view exit so the user lands where they
    // left the source view rather than at sample 0.
    if (source_audio_held.total_frames() == 0) {
        active_views.refresh_active_tab_view_from_app();
        source_audio_held = std::move(audio);
    }
    audio = std::move(next);
    app.audio_generation++;

    app.render_view.warp_markers           = std::move(loaded_warp);
    app.render_view.phase_resets        = std::move(loaded_phase_resets);
    // Snapshot display geometry for the Render display context: the FULL
    // map this entry's authored positions translate through, plus the
    // crop origin llrint(T_b) (an exact int64 recovery — the helper's
    // crop_begin is an llrint result carried as a double; 0 untrimmed).
    // Built once here, immutable while displayed; cleared beside the
    // display stores at every clear site.
    app.render_view.snapshot_warp_frame_map = std::move(full_warp_frame_map);
    app.render_view.snapshot_crop_begin =
        std::llrint(display.crop_begin);
    app.render_view.index             = index;
    app.render_view.last_path         = e.wav_path.string();

    // Apply the entry's persisted W/P mode BEFORE the stat-gated selection
    // restore below, so the matching-mode slot the restore reads is the
    // mode the selection was stashed under (autosave persists the browsed
    // mode at the same trigger the stash runs). Absent key applies the
    // struct default 'W'. The other .settings keys — the engine block,
    // playback_speed, follow, font_size, active_audio_view — are NOT
    // applied at browse time: they are the commit payload, adopted only by
    // Ctrl+Alt+C.
    app.active_markers_view = settings->active_markers_view;

    // Stat-tuple-gated selection restore. A matching persisted tuple
    // (non-zero, equal to current) means the wav hasn't changed since stash;
    // replay the persisted selection. Mismatch (or never-stashed defaults)
    // drops to empty selection — the destination entry has no remembered
    // session for this file.
    const auto cur_stat = this->wav_stat_tuple(e.wav_path);
    const bool stat_match =
        cur_stat.first  != 0 &&
        cur_stat.second != 0 &&
        cur_stat.first  == e.persisted_size &&
        cur_stat.second == e.persisted_mtime;
    if (stat_match) {
        // Load only the matching-mode slot
        // into the live pair. The OTHER-mode slot stays on state
        // and gets swapped in if mode flips during this render-
        // view session via switch_active_markers_view_to.
        if (app.active_markers_view == 'P') {
            app.selected_markers     = e.state.phase_reset_selected;
            app.last_selected_marker = e.state.phase_reset_last_selected;
        } else {
            app.selected_markers     = e.state.warp_selected;
            app.last_selected_marker = e.state.warp_last_selected;
        }
        selection.prune_live_selection();
    } else {
        // Stat mismatch invalidates BOTH slots — the wav has
        // changed, so any stashed indices for either mode could
        // be stale. Symmetric with stat-match's "live pair gets
        // matching slot, OTHER stays on state": when we don't
        // trust state, clear both the live pair AND the OTHER
        // slot on state so a later mode-flip doesn't pull in
        // stale data.
        app.selected_markers.clear();
        app.last_selected_marker = -1;
        e.state.warp_selected.clear();
        e.state.warp_last_selected      = -1;
        e.state.phase_reset_selected.clear();
        e.state.phase_reset_last_selected = -1;
    }

    // Apply this render's persisted zoom/viewport/playhead from the commit
    // tab of the already-parsed .settings (absent keys carry the schema
    // defaults: fit-file zoom, zeroed viewport/playhead). Apply order:
    // zoom → viewport → playhead → clamp_viewport_start (zoom drives the
    // spp used by clamp).
    app.zoom_level            = commit_tab.zoom;
    app.viewport_start_sample = commit_tab.viewport_start;
    // Deliberately unclamped: persisted playhead == total stays load-legal
    // (an exclusive-bound rest); the runtime clamp (move_playhead_to) owns
    // the value at first use.
    app.playhead_cursor_sample = commit_tab.playhead;
    clamp_viewport_start(app, audio);

    // Domain offset 0: the displayed wav is its own domain origin.
    if (!playback.init(audio.sample_rate(), audio.channels(),
                       audio.samples_ptr(), audio.total_frames(), 0)) {
        std::fprintf(stderr,
            "warptempo_gui: playback disabled in render-view\n");
    }
    // One-shot discrete jump: the loaded render swapped the audio buffer and
    // the viewport / zoom, so render the plate synchronously and publish the
    // displayed fingerprint now. Otherwise the markers / playhead repaint
    // immediately from the full-window invalidate below while the plate is left
    // to the next tick's worker, jumping the overlays a frame ahead of the
    // waveform on render-view entry and on render-to-render navigation. Covers
    // the R-key toggle-on, render navigation, and auto-open paths, which all
    // route through here.
    viewport.kick_waveform_sync();
    gui.invalidate_region(0, 0, app.width, app.height);
    return true;
}

// Restores source audio from the parked source_audio_held. Inverse
// of the load_render_view_at entry path. No-op when
// source_audio_held is empty (nothing to restore).
void GuiRenderView::restore_source_audio() {
    // Leaving render view: clear the flag BEFORE the synchronous waveform
    // kick at the end of this function. kick_waveform_sync resolves to
    // force_synchronous_waveform_rebuild, whose compute_waveform_render_inputs
    // derives is_target as (active_audio_view == 'T') && !render_view.enabled.
    // If the flag is still set when the kick runs, is_target is false, the
    // plate is rendered with no warp_frame_map (an unwarped source plate) and that
    // non-target fingerprint is published; the next on_redraw then sees the
    // inputs differ and rebuilds the real target plate on the async worker,
    // which is the visible flash on the render -> target transition. Clearing
    // here, ahead of the source_audio_held guard, makes every leave path
    // correct without each call site having to order the flag itself.
    app.render_view.enabled = false;
    if (source_audio_held.total_frames() == 0) return;
    playback.stop();
    playback.shutdown();
    app.playhead_scanner_active = false;
    app.playhead_scanner_restore_pending = false;
    app.playhead_scanner_endpoint_painted = false;
    app.playhead_scanner_sample = 0;
    viewport.clear_hover_popup();

    audio = std::move(source_audio_held);
    source_audio_held = GuiAudio{};
    app.audio_generation++;

    // Read back the active tab's snapshot saved when render-view was
    // first entered. The Tab key is gated out of render-view's input
    // allowlist, so app.active_tab_view is the same letter the snapshot
    // was written under.
    const ViewState& t = (app.active_tab_view == 'B') ? app.tab_b : app.tab_a;
    app.viewport_start_sample = t.viewport_start_sample;
    app.zoom_level            = t.zoom_level;
    // Deliberately unclamped: restores the authoring snapshot stashed at
    // render-view entry — a previously-resting value in the authoring
    // domain (move_playhead_to owns the value at first use).
    app.playhead_cursor_sample       = t.playhead_cursor_sample;
    // Load the matching-mode slot into the
    // live pair. Live pair held render-view selection while
    // render-view was active; restoring source-view requires
    // pulling the source tab's matching-mode slot back in.
    if (app.active_markers_view == 'P') {
        app.selected_markers     = t.phase_reset_selected;
        app.last_selected_marker = t.phase_reset_last_selected;
    } else {
        app.selected_markers     = t.warp_selected;
        app.last_selected_marker = t.warp_last_selected;
    }
    clamp_viewport_start(app, audio);
    selection.prune_live_selection();

    // Domain offset 0: the displayed wav is its own domain origin.
    if (!playback.init(audio.sample_rate(), audio.channels(),
                       audio.samples_ptr(), audio.total_frames(), 0)) {
        std::fprintf(stderr, "warptempo_gui: playback disabled\n");
    }
    // H1 fix: render-view exit lands playback bound to source.wav
    // unconditionally above. If the user is still in target view (R
    // does not touch active_audio_view), rebind to the target buffer —
    // ensure_ready dispatches a fresh render if the buffer is stale,
    // otherwise it short-circuits to a clean playback.rebind_buffer.
    if (app.active_audio_view == 'T') {
        target_render.ensure_ready();
    }
    // One-shot discrete jump: source audio is restored and the entering tab's
    // viewport / zoom / playhead are applied, so render the plate synchronously
    // and publish the displayed fingerprint now. Covers the R-key toggle-off and
    // exit_render_view_and_clear paths, which route through here. The plate is
    // built from the restored source audio (or, when active_audio_view is
    // Target, source audio plus the live warp_frame_map), independent of the target
    // render buffer ensure_ready rebinds above.
    viewport.kick_waveform_sync();
    gui.invalidate_region(0, 0, app.width, app.height);
}

// Re-enumerate the renders/ folder and migrate persisted per-entry
// state from the existing app.render_view.list into the refreshed
// list, keyed by wav_path. Mirrors the migration block in the R-key
// toggle-on path, but operates on the live render_view.list (in-
// session) rather than on a freshly-arrived list. Caller is
// responsible for stashing selection / autosaving view state for the
// outgoing entry *before* calling this, since the merge does not
// preserve any state that lives only on app.* fields (selected_markers
// etc.) — only the per-entry persisted slots survive.
bool GuiRenderView::refresh_render_view_list() {
    std::vector<AppState::RenderViewEntry> fresh =
        this->enumerate_render_view_list();
    if (fresh.empty()) {
        app.render_view.list.clear();
        app.render_view.index = -1;
        return false;
    }

    const int prior_index = app.render_view.index;
    std::string current_wav_path;
    if (prior_index >= 0 &&
        prior_index < static_cast<int>(app.render_view.list.size())) {
        current_wav_path =
            app.render_view.list[prior_index].wav_path.string();
    }

    if (!app.render_view.list.empty()) {
        std::map<std::string,
            AppState::RenderViewEntry*> prior;
        for (auto& pe : app.render_view.list) {
            prior[pe.wav_path.string()] = &pe;
        }
        for (auto& ne : fresh) {
            auto it = prior.find(ne.wav_path.string());
            if (it == prior.end()) continue;
            const auto& src = *it->second;
            ne.state           = src.state;
            ne.persisted_size  = src.persisted_size;
            ne.persisted_mtime = src.persisted_mtime;
        }
    }

    app.render_view.list = std::move(fresh);

    int new_index = -1;
    if (!current_wav_path.empty()) {
        for (size_t i = 0; i < app.render_view.list.size(); ++i) {
            if (app.render_view.list[i].wav_path.string() ==
                current_wav_path) {
                new_index = static_cast<int>(i);
                break;
            }
        }
    }
    if (new_index < 0) {
        const int n = static_cast<int>(app.render_view.list.size());
        int clamped = prior_index;
        if (clamped < 0)  clamped = 0;
        if (clamped >= n) clamped = n - 1;
        new_index = clamped;
    }
    app.render_view.index = new_index;
    return true;
}

// Show render-view at the first .wav of the just-finished batch. Two
// outcomes share one body — enumerate the renders/ folder, migrate the
// prior list's per-entry persisted state by wav_path, and select the first
// entry whose batch_folder matches the passed-in argument — differing only
// in the tail:
//
//   - Render-view closed: enter it at that entry, mirroring the R-key
//     toggle-on entry sequence, substituting the batch_folder match for the
//     last_path match.
//   - Render-view already open: a parked batch (dispatched during a running
//     render, then backgrounded by an R-toggle so the user browses old
//     renders) can pump to completion while the view is up. Refresh the view
//     in place so it shows what source-view completion would show — the fresh
//     list, landed on the new batch's first file — instead of the stale
//     pre-batch list. The live selection is stashed onto the outgoing entry
//     first (the migration below then carries it into the fresh list), and
//     the new entry loads through the same load path navigation uses, which
//     rebinds playback to its wav; the enter/park sequence is skipped because
//     the view is already up and the source audio already parked.
void GuiRenderView::auto_open_batch_at_first_file(
        const std::filesystem::path& batch_folder) {
    if (app.source_audio_path.empty()) return;

    const bool refreshing_in_place = app.render_view.enabled;

    // In-place refresh: the live selection still sits on the app.* fields, so
    // capture the outgoing entry's viewport sidecar and stash its selection
    // onto the active list entry BEFORE the migration below, so the
    // wav_path-keyed merge carries that just-stashed state into the fresh
    // list. Mirrors the entry-switch sequence the render-view nav handlers run.
    if (refreshing_in_place) {
        if (app.render_view.index >= 0 &&
            app.render_view.index <
                static_cast<int>(app.render_view.list.size())) {
            this->write_settings_for(
                app.render_view.list[app.render_view.index]);
        }
        this->stash_render_view_selection_to_active_entry();
    }

    std::vector<AppState::RenderViewEntry> list =
        this->enumerate_render_view_list();
    if (list.empty()) {
        std::fprintf(stderr,
            "warptempo_gui: auto-open: enumerator returned empty list\n");
        return;
    }

    // Migrate persisted per-entry state from the prior
    // app.render_view.list into the freshly enumerated list, keyed by
    // wav_path. Same shape as the R-toggle entry-path migration.
    if (!app.render_view.list.empty()) {
        std::map<std::string,
            AppState::RenderViewEntry*> prior;
        for (auto& pe : app.render_view.list) {
            prior[pe.wav_path.string()] = &pe;
        }
        for (auto& ne : list) {
            auto it = prior.find(ne.wav_path.string());
            if (it == prior.end()) continue;
            const auto& src = *it->second;
            ne.state           = src.state;
            ne.persisted_size  = src.persisted_size;
            ne.persisted_mtime = src.persisted_mtime;
        }
    }

    int target = -1;
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].batch_folder == batch_folder) {
            target = static_cast<int>(i);
            break;
        }
    }
    if (target < 0) {
        std::fprintf(stderr,
            "warptempo_gui: auto-open: batch folder '%s' not found "
            "in render-view list\n",
            batch_folder.string().c_str());
        return;
    }

    if (refreshing_in_place) {
        // View already up, source audio already parked in source_audio_held:
        // swap in the fresh list and load the new batch's first entry through
        // the same path navigation uses (it rebinds playback to the new wav
        // and does not re-park the render as source, since source_audio_held
        // is non-empty). On the defensive load-failure path fall back to a
        // clean source-view exit.
        app.render_view.list = std::move(list);
        if (!this->load_render_view_at(target)) {
            this->exit_render_view_and_clear();
        }
        return;
    }

    app.render_view.src_sr    = audio.sample_rate();
    app.render_view.src_total = audio.total_frames();
    app.render_view.list      = std::move(list);
    // Iter/BPM modes persist across render-view enter/leave. The flags are
    // inert inside render view (input gate drops i/M; paint
    // gates on !render_view.enabled), so they're simply restored on exit.
    app.render_view.enabled    = true;
    if (!this->load_render_view_at(target)) {
        app.render_view.enabled = false;
        app.render_view.list.clear();
    }
}

// Shared teardown for "render-view ends here" exits driven by the
// navigation handlers when the renders/ folder turns out to be empty
// after a refresh. Equivalent to the toggle-off branch of the R key,
// minus the live-state capture that the navigation handler already
// performed before calling refresh_render_view_list.
void GuiRenderView::exit_render_view_and_clear() {
    this->restore_source_audio();
    app.render_view.warp_markers.clear();
    app.render_view.phase_resets.clear();
    app.render_view.snapshot_warp_frame_map.clear();
    app.render_view.snapshot_crop_begin = 0;
    app.render_view.index             = -1;
}
