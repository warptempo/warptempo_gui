#pragma once

#include "app_state.h"
#include "audio.h"
#include "playback.h"
#include "selection.h"
#include "tab_mode.h"
#include "viewport.h"
#include "platform_wayland.h"

#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

// X.7.6: render-view cluster, extracted from main.cpp's inline lambdas.
// Covers the directory enumeration of <source_parent>/renders/<batch>/<basename>.wav,
// the .rendersettings sidecar (per-render zoom/viewport/playhead persistence),
// the per-entry selection stash with stat-tuple gating, the load-into-active-audio
// path that parks the source GuiAudio onto an owned member, and the inverse
// restore. clear_hover_popup is reached through viewport;
// refresh_active_tab_from_app is reached through tab_mode.
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
    GuiTabMode&       tab_mode;

    // Chunk W: parked source audio. Populated only while
    // app.render_view_enabled is true — std::move'd off `audio` on
    // toggle-in and std::move'd back on toggle-out so the source
    // doesn't have to be re-read from disk. Default-constructed
    // (empty / total_frames() == 0) when render-view is off.
    GuiAudio source_audio_held;

    GuiRenderView(AppState&         app_,
                  GuiAudio&         audio_,
                  GuiPlayback&      playback_,
                  GuiPlatform&      gui_,
                  Selection&        selection_,
                  Viewport&         viewport_,
                  GuiTabMode&       tab_mode_)
        : app(app_),
          audio(audio_),
          playback(playback_),
          gui(gui_),
          selection(selection_),
          viewport(viewport_),
          tab_mode(tab_mode_) {}

    std::vector<AppState::RenderViewEntry> enumerate_render_view_list();
    std::filesystem::path rendersettings_path(
        const AppState::RenderViewEntry& e);
    void write_rendersettings_for(const AppState::RenderViewEntry& e);
    void apply_rendersettings_for(const AppState::RenderViewEntry& e);
    std::pair<uintmax_t, int64_t> wav_stat_tuple(
        const std::filesystem::path& p);
    void stash_render_view_selection_to_active_entry();
    bool load_render_view_at(int index);
    void restore_source_audio();

    // Re-enumerate the renders/ folder and merge per-entry persisted
    // state (state, persisted_size, persisted_mtime) from the existing
    // app.render_view_list into the refreshed list, keyed by wav_path.
    // Updates app.render_view_index to follow the currently-viewed
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

    // Enter render-view at the first .wav of the just-rendered batch.
    // Called from GuiInputHandler::dispatch_next_batch_entry's
    // terminal success branch after a multi-entry (or single-entry)
    // batch finishes uncancelled with at least one render on disk.
    // Mirrors the R-key toggle-on entry sequence, but targets the
    // first list entry whose batch_folder matches batch_folder
    // instead of last_render_view_path. By the input-handler
    // gatekeeper invariant, render-view must be off on entry —
    // this method early-returns defensively otherwise.
    void auto_open_batch_at_first_file(
        const std::filesystem::path& batch_folder);
};
