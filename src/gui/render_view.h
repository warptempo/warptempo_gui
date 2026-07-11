#pragma once

#include "active_views.h"
#include "app_state.h"
#include "audio.h"
#include "playback.h"
#include "render_pipeline.h"
#include "selection.h"
#include "viewport.h"
#include "platform_wayland.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

struct GuiTargetRender;

// Render-view cluster, extracted from main.cpp's inline lambdas.
// Covers the directory enumeration of <source_parent>/renders/<batch>/<basename>.wav,
// the per-entry .settings snapshot (per-render zoom/viewport/playhead/W-P
// persistence, plus the strict entry load: markers, engine scale, recipe trim),
// the per-entry selection stash with stat-tuple gating, the entry-audio
// decode-and-bind path, and the source-view restore on exit.
// clear_hover_popup is reached through viewport;
// refresh_active_tab_view_from_app is reached through active_views.
//
// The audio-domain invariant: the GuiAudio object (`audio`) is ALWAYS the
// source — it never swaps, in any view. Render view's audio is the
// view-owned decoded entry buffer below, bound to playback at the
// rendered window's target-axis origin (entry_domain_begin — the wav
// covers only the window of the full deformed timeline the view
// displays); the waveform plate is the SOURCE samples deformed through
// the entry's snapshot map, which by the render pipeline's own
// construction matches the rendered wav across the window.
struct GuiRenderView {
    AppState&         app;
    GuiAudio&         audio;
    GuiPlayback&      playback;
    GuiPlatform&      gui;
    Selection&        selection;
    Viewport&         viewport;
    GuiActiveViews&   active_views;
    GuiTargetRender&  target_render;

    // The displayed entry's decoded audio: interleaved float frames with
    // channels == the source's channel count (equal to the render's by
    // construction, verified at decode). Disk-on-demand: decoded whole in
    // load_render_view_at (entries are typically short trims), one entry
    // resident at a time, freed on nav-away / exit / commit. Playback binds
    // this buffer through rebind_buffer at the rendered window's origin
    // (app.render_view.entry_domain_begin). Empty (entry_frames == 0)
    // whenever no entry is displayed.
    std::vector<float> entry_samples;
    int64_t            entry_frames = 0;

    // Derived-dispatch surface for the stale-entry rebuild, bound in main.cpp
    // post-construction to GuiInputHandler::dispatch_archival_render_if_idle
    // (the dispatch chokepoints are GuiInputHandler methods and this struct
    // holds no handler reference — same post-construction back-wire shape as
    // file_loader.prompt). Returns true iff the request was dispatched on an
    // idle worker; false (no side effects) when the worker is busy — a
    // nav-triggered rebuild is a derived dispatch, never a kill, and the
    // caller prints the busy refusal. Null only before main.cpp wires it,
    // when no navigation can have happened yet.
    std::function<bool(RenderRequest, std::vector<uint8_t>)>
        dispatch_archival_render_if_idle;

    GuiRenderView(AppState&         app_,
                  GuiAudio&         audio_,
                  GuiPlayback&      playback_,
                  GuiPlatform&      gui_,
                  Selection&        selection_,
                  Viewport&         viewport_,
                  GuiActiveViews&   active_views_,
                  GuiTargetRender&  target_render_)
        : app(app_),
          audio(audio_),
          playback(playback_),
          gui(gui_),
          selection(selection_),
          viewport(viewport_),
          active_views(active_views_),
          target_render(target_render_) {}

    std::vector<AppState::RenderViewEntry> enumerate_render_view_list();
    std::filesystem::path settings_path(
        const AppState::RenderViewEntry& e);
    void write_settings_for(const AppState::RenderViewEntry& e);
    std::pair<uintmax_t, int64_t> wav_stat_tuple(
        const std::filesystem::path& p);
    void stash_render_view_selection_to_active_entry();

    // Persist the active entry's browse state at a leave-this-entry
    // boundary: the .settings view-state autosave (write_settings_for)
    // followed by the in-memory selection stash. The R-toggle exit, both
    // navigation chords, the in-place auto-open refresh, and the
    // close/revert prompts all run this same pair. No-op when render
    // view is off or no entry is active.
    void autosave_active_entry();

    bool load_render_view_at(int index);
    void restore_source_view();

    // The abandon arm of the render-view exit pair: restore_source_view
    // restores the stashed source view for an ordinary exit;
    // abandon_render_view tears down when the SOURCE itself is being
    // discarded (revert-to-blank), where restoring the stashed view would
    // be too late (the source is going away) and restore_source_view's
    // target-view ensure_ready tail would spuriously dispatch against the
    // dying source. Rebinds playback to the still-alive source buffer
    // before freeing the entry buffer, then clears every render-view
    // field. Deliberately does NOT touch the tab slots, the live view
    // fields, or active_audio_view — revert_to_blank resets those itself.
    void abandon_render_view();

    // Re-enumerate the renders/ folder and merge per-entry persisted
    // state (state, persisted_size, persisted_mtime) from the existing
    // app.render_view.list into the refreshed list, keyed by wav_path.
    // Updates app.render_view.index to follow the currently-viewed
    // entry by wav_path; if the current entry was deleted, the index
    // clamps to the closest surviving original position. Returns true
    // if the list is non-empty after refresh; false if the refresh
    // produces an empty list (caller should exit render-view in that
    // case). Live state stashing and load_render_view_at are NOT
    // performed by this method — callers do those at the appropriate
    // boundary.
    bool refresh_render_view_list();

    // Tear down render-view state and restore the source view. Mirrors
    // the toggle-off branch of the R key handler. Used by the
    // navigation handlers when refresh_render_view_list returns false
    // (renders/ folder emptied externally).
    void exit_render_view_and_clear();

    // Show the first .wav of the just-rendered batch. Called from
    // GuiInputHandler::dispatch_next_batch_entry's terminal success
    // branch after a batch finishes uncancelled with at least one
    // render on disk. Render-view off: mirrors the R-key toggle-on
    // entry sequence, targeting the first list entry whose
    // batch_folder matches instead of render_view.last_path.
    // Render-view already up (a parked batch can complete under an
    // open view): refreshes the list in place — stash the current
    // entry, re-enumerate, migrate persisted per-entry state, land on
    // the new batch's first file through the navigation load path —
    // so the view shows exactly what it would had the batch completed
    // with render-view closed.
    void auto_open_batch_at_first_file(
        const std::filesystem::path& batch_folder);
};
