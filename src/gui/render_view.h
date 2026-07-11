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
// the per-entry selection stash with stat-tuple gating, the load-into-active-audio
// path that parks the source GuiAudio onto an owned member, and the inverse
// restore. clear_hover_popup is reached through viewport;
// refresh_active_tab_view_from_app is reached through active_views.
//
// `source_audio_held` is owned (not a reference): it was a local in main()
// before extraction and is used only by load_render_view_at /
// restore_source_audio. Promoting it to a member keeps lifetime tied to
// the struct rather than to main's stack frame.
struct GuiRenderView {
    AppState&         app;
    GuiAudio&         audio;
    GuiPlayback&      playback;
    GuiPlatform&      gui;
    Selection&        selection;
    Viewport&         viewport;
    GuiActiveViews&   active_views;
    GuiTargetRender&  target_render;

    // Parked source audio. Populated only while app.render_view.enabled is
    // true — std::move'd off `audio` on toggle-in and std::move'd back on
    // toggle-out so the source doesn't have to be re-read from disk.
    // Default-constructed (empty / total_frames() == 0) when render-view is
    // off.
    GuiAudio source_audio_held;

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
    bool load_render_view_at(int index);
    void restore_source_audio();

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

    // Tear down render-view state and restore source audio. Mirrors
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
