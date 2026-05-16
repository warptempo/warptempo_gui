#include "render_view.h"

#include "phase_reset_markers.h"
#include "settings_io.h"
#include "target_render.h"
#include "warpmarkers.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <utility>
#include <vector>

// X.7.6: render-view cluster. Method bodies are byte-identical to the
// lambdas they replaced in main.cpp, with these mechanical rewrites:
//
//   clear_hover_popup            → viewport.clear_hover_popup
//   refresh_active_tab_from_app  → tab_mode.refresh_active_tab_from_app
//                                  (X.7.13 retired the std::function forwarders)
//   prune_live_selection         → selection.prune_live_selection
//   rendersettings_path,
//   write_rendersettings_for,
//   apply_rendersettings_for,
//   wav_stat_tuple,
//   stash_render_view_selection_to_active_entry,
//   load_render_view_at,
//   restore_source_audio         → this->method_name (intra-cluster calls)
//
// Free function calls (clamp_viewport_start, compute_trim_samples) keep
// their original spelling — both are declared at file scope in app_state.h.
// The owned source_audio_held member replaces the same-named local in
// main(); accesses stay on the bare identifier (resolved as this->
// source_audio_held).

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

// -- <basename>.rendersettings sidecar ---------------------------------
//
// Per-render zoom/viewport/playhead persistence. Captures the live
// render-view state at navigation/exit boundaries; applied on entry
// / arrival. The on-disk file is shared with the engine-block written
// by render_pipeline (see write_rendersettings in settings_io); these
// helpers only touch the view-state block.

std::filesystem::path GuiRenderView::rendersettings_path(
        const AppState::RenderViewEntry& e) {
    return e.batch_folder / (e.basename + ".rendersettings");
}

// Atomic update of the view-state block (engine block, if present on
// disk, is left untouched). Failures are non-fatal — logged once by
// the underlying writer and otherwise discarded.
void GuiRenderView::write_rendersettings_for(
        const AppState::RenderViewEntry& e) {
    update_rendersettings_view_state(
        this->rendersettings_path(e),
        app.viewport_start_sample,
        app.zoom_level,
        app.playhead_sample);
}

// Tolerant parser. Missing / malformed file applies fit-file zoom
// and zeroed viewport/playhead. Apply order: zoom → viewport →
// playhead → clamp_viewport_start (zoom drives the spp used by
// clamp).
void GuiRenderView::apply_rendersettings_for(
        const AppState::RenderViewEntry& e) {
    const RenderViewState vs =
        read_rendersettings_view_state(this->rendersettings_path(e));
    int z = vs.zoom_level;
    // Sanitize zoom — accept kFitFileLevel (=0) or kMinNumericLevel..
    // kMaxNumericLevel. kFitFileLevel falls inside the [0, kMaxNumericLevel]
    // range so a single range check suffices.
    if (z < 0 || z > kMaxNumericLevel) {
        z = kFitFileLevel;
    }
    app.zoom_level            = z;
    app.viewport_start_sample = vs.viewport_start;
    app.playhead_sample       = vs.playhead;
    clamp_viewport_start(app, audio);
}

// Brief F Section 4: capture (size, mtime_seconds) for a wav path.
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

// Brief J.2 Section 4: stash the live selection into the active
// RenderViewEntry's matching-mode slot, along with the wav's
// current stat tuple. No-op when no entry is active. Called from
// the render-view exit path and from the batch-nav path
// (Shift+Left/Right) before the destination is loaded. The
// OTHER-mode slot was last written when active_markers_view flipped
// away from it via switch_active_markers_view_to (or never written if
// the user has not flipped mode in this render-view session);
// either way it is current at stash time.
void GuiRenderView::stash_render_view_selection_to_active_entry() {
    if (app.render_view_index < 0 ||
        app.render_view_index >=
            static_cast<int>(app.render_view_list.size())) {
        return;
    }
    auto& e = app.render_view_list[app.render_view_index];
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

// Loads the render at app.render_view_list[index] into the active
// `audio`, parking the source audio on first entry. Parses sibling
// <basename>.warpmarkers and <basename>.phaseresetmarkers into
// app.render_view_markers/phase resets and computes F_begin/F_end
// against the cached source sr/total. Stops playback before the
// swap and re-binds the playback device. Returns true on success;
// on failure logs to stderr and the prior state is preserved.
//
// Brief F Section 4: when the destination entry's persisted stat
// tuple matches the wav's current stat, restores the persisted
// selection. Mismatch leaves the live selection empty.
bool GuiRenderView::load_render_view_at(int index) {
    if (index < 0 ||
        index >= static_cast<int>(app.render_view_list.size())) {
        return false;
    }
    auto& e = app.render_view_list[index];

    GuiAudio next;
    if (!next.load(e.wav_path.string(), {})) {
        std::fprintf(stderr,
            "warptempo_gui: render-view: failed to load %s\n",
            e.wav_path.string().c_str());
        return false;
    }

    // Render-view consumes render-domain sidecars
    // (.renderwarpmarkers / .renderphaseresetmarkers) so visible marker
    // positions match the rendered audio's time axis. The source-domain
    // pair (.warpmarkers / .phaseresetmarkers) is what Ctrl+Alt+C commit
    // reloads when promoting a render's markers into authoring memory.
    std::vector<GuiWarpMarker>     loaded_warp;
    std::vector<GuiPhaseResetMarker>  loaded_trans;
    {
        const std::filesystem::path wmd =
            e.batch_folder / (e.basename + ".renderwarpmarkers");
        std::error_code ec;
        if (std::filesystem::exists(wmd, ec)) {
            GuiWarpMarkers m;
            m.load(wmd.string());
            loaded_warp = m.markers();
        } else {
            std::fprintf(stderr,
                "warptempo_gui: render-view: %s missing — markers will "
                "not be displayed for this render\n",
                wmd.string().c_str());
        }
    }
    {
        const std::filesystem::path tmd =
            e.batch_folder / (e.basename + ".renderphaseresetmarkers");
        std::error_code ec;
        if (std::filesystem::exists(tmd, ec)) {
            GuiPhaseResetMarkers t;
            t.load(tmd.string());
            loaded_trans = t.markers();
        }
    }
    playback.stop();
    playback.shutdown();
    app.is_playing      = false;
    app.playback_cursor = 0;
    viewport.clear_hover_popup();

    // Snapshot the live authoring playhead/viewport/zoom into the
    // active tab's slot before we overwrite `audio`. restore_source_audio
    // reads it back on render-view exit so the user lands where they
    // left the source view rather than at sample 0.
    if (source_audio_held.total_frames() == 0) {
        tab_mode.refresh_active_tab_from_app();
        source_audio_held = std::move(audio);
    }
    audio = std::move(next);
    app.audio_generation++;

    // Render-view has no trim — the rendered audio is already trimmed.
    app.render_view_src_F_begin = 0;
    app.render_view_src_F_end   = app.render_view_src_total;

    app.render_view_markers           = std::move(loaded_warp);
    app.render_view_phase_resets        = std::move(loaded_trans);
    app.render_view_index             = index;
    app.last_render_view_path         = e.wav_path.string();

    // Brief F Section 4: stat-tuple-gated selection restore. A
    // matching persisted tuple (non-zero, equal to current) means
    // the wav hasn't changed since stash; replay the persisted
    // selection. Mismatch (or never-stashed defaults) drops to
    // empty selection — the destination entry has no remembered
    // session for this file.
    const auto cur_stat = this->wav_stat_tuple(e.wav_path);
    const bool stat_match =
        cur_stat.first  != 0 &&
        cur_stat.second != 0 &&
        cur_stat.first  == e.persisted_size &&
        cur_stat.second == e.persisted_mtime;
    if (stat_match) {
        // Brief J.2 Section 4: load only the matching-mode slot
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

    // Apply this render's persisted zoom/viewport/playhead (or
    // fit-file defaults when no .rendersettings sidecar exists).
    // Order matters: apply_rendersettings_for sets zoom first
    // (clamp depends on it) and runs clamp at the end.
    this->apply_rendersettings_for(e);

    if (!playback.init(audio.sample_rate(), audio.channels(),
                       audio.samples_ptr(), audio.total_frames())) {
        std::fprintf(stderr,
            "warptempo_gui: playback disabled in render-view\n");
    }
    gui.invalidate_region(0, 0, app.width, app.height);
    return true;
}

// Restores source audio from the parked source_audio_held. Inverse
// of the load_render_view_at entry path. No-op when
// source_audio_held is empty (nothing to restore).
void GuiRenderView::restore_source_audio() {
    if (source_audio_held.total_frames() == 0) return;
    playback.stop();
    playback.shutdown();
    app.is_playing      = false;
    app.playback_cursor = 0;
    viewport.clear_hover_popup();

    audio = std::move(source_audio_held);
    source_audio_held = GuiAudio{};
    app.audio_generation++;

    // Read back the active tab's snapshot saved when render-view was
    // first entered. The Tab key is gated out of render-view's input
    // allowlist, so app.active_tab is the same letter the snapshot
    // was written under.
    const ViewState& t = (app.active_tab == 'B') ? app.tab_b : app.tab_a;
    app.viewport_start_sample = t.viewport_start_sample;
    app.zoom_level            = t.zoom_level;
    app.playhead_sample       = t.playhead_sample;
    // Brief J.2 Section 4: load the matching-mode slot into the
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

    if (!playback.init(audio.sample_rate(), audio.channels(),
                       audio.samples_ptr(), audio.total_frames())) {
        std::fprintf(stderr, "warptempo_gui: playback disabled\n");
    }
    // H1 fix: render-view exit lands playback bound to source.wav
    // unconditionally above. If the user is still in target view (R
    // does not touch view_domain), rebind to the target buffer —
    // ensure_ready dispatches a fresh render if the buffer is stale,
    // otherwise it short-circuits to a clean playback.rebind_buffer.
    if (app.view_domain == ViewDomain::Target) {
        target_render.ensure_ready();
    }
    gui.invalidate_region(0, 0, app.width, app.height);
}

// Re-enumerate the renders/ folder and migrate persisted per-entry
// state from the existing app.render_view_list into the refreshed
// list, keyed by wav_path. Mirrors the migration block in the R-key
// toggle-on path, but operates on the live render_view_list (in-
// session) rather than on a freshly-arrived list. Caller is
// responsible for stashing selection / writing rendersettings for the
// outgoing entry *before* calling this, since the merge does not
// preserve any state that lives only on app.* fields (selected_markers
// etc.) — only the per-entry persisted slots survive.
bool GuiRenderView::refresh_render_view_list() {
    std::vector<AppState::RenderViewEntry> fresh =
        this->enumerate_render_view_list();
    if (fresh.empty()) {
        app.render_view_list.clear();
        app.render_view_index = -1;
        return false;
    }

    const int prior_index = app.render_view_index;
    std::string current_wav_path;
    if (prior_index >= 0 &&
        prior_index < static_cast<int>(app.render_view_list.size())) {
        current_wav_path =
            app.render_view_list[prior_index].wav_path.string();
    }

    if (!app.render_view_list.empty()) {
        std::map<std::string,
            AppState::RenderViewEntry*> prior;
        for (auto& pe : app.render_view_list) {
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

    app.render_view_list = std::move(fresh);

    int new_index = -1;
    if (!current_wav_path.empty()) {
        for (size_t i = 0; i < app.render_view_list.size(); ++i) {
            if (app.render_view_list[i].wav_path.string() ==
                current_wav_path) {
                new_index = static_cast<int>(i);
                break;
            }
        }
    }
    if (new_index < 0) {
        const int n = static_cast<int>(app.render_view_list.size());
        int clamped = prior_index;
        if (clamped < 0)  clamped = 0;
        if (clamped >= n) clamped = n - 1;
        new_index = clamped;
    }
    app.render_view_index = new_index;
    return true;
}

// Auto-open render-view at the first .wav of the just-finished
// batch. Mirrors the R-key toggle-on entry sequence in input_
// handler.cpp, with one substitution: the target index is the
// first list entry whose batch_folder matches the passed-in
// batch_folder argument, rather than the entry whose wav_path
// matches app.last_render_view_path.
//
// Per the input-handler gatekeeper invariant (S / E / Ctrl+Alt+R /
// Ctrl+Alt+E / Ctrl+Alt+I / Ctrl+Alt+M are all dropped while
// render-view is active), this method is reachable only when
// render-view is off. The defensive early-return guards against
// a future change that breaks the invariant — entering twice
// would corrupt the existing session.
void GuiRenderView::auto_open_batch_at_first_file(
        const std::filesystem::path& batch_folder) {
    if (app.render_view_enabled) return;
    if (app.source_audio_path.empty()) return;

    std::vector<AppState::RenderViewEntry> list =
        this->enumerate_render_view_list();
    if (list.empty()) {
        std::fprintf(stderr,
            "warptempo_gui: auto-open: enumerator returned empty list\n");
        return;
    }

    // Migrate persisted per-entry state from the prior
    // app.render_view_list (which survived toggle-off) into the
    // freshly enumerated list, keyed by wav_path. Same shape as
    // the R-toggle entry-path migration.
    if (!app.render_view_list.empty()) {
        std::map<std::string,
            AppState::RenderViewEntry*> prior;
        for (auto& pe : app.render_view_list) {
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

    app.render_view_src_sr    = audio.sample_rate();
    app.render_view_src_total = audio.total_frames();
    app.render_view_list      = std::move(list);
    app.iteration_mode_enabled = false;
    app.bpm_mode_enabled       = false;
    app.render_view_enabled    = true;
    if (!this->load_render_view_at(target)) {
        app.render_view_enabled = false;
        app.render_view_list.clear();
    }
}

// Shared teardown for "render-view ends here" exits driven by the
// navigation handlers when the renders/ folder turns out to be empty
// after a refresh. Equivalent to the toggle-off branch of the R key,
// minus the live-state capture that the navigation handler already
// performed before calling refresh_render_view_list.
void GuiRenderView::exit_render_view_and_clear() {
    this->restore_source_audio();
    app.render_view_enabled = false;
    app.render_view_markers.clear();
    app.render_view_phase_resets.clear();
    app.render_view_index             = -1;
    app.render_view_src_F_begin       = 0;
    app.render_view_src_F_end         = 0;
}
