#pragma once

#include "active_views.h"
#include "app_state.h"
#include "audio.h"
#include "playback.h"
#include "selection.h"
#include "viewport.h"
#include "platform_wayland.h"

#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

struct GuiTargetRender;

// Render-view cluster, extracted from main.cpp's inline lambdas.
// Covers the directory enumeration of <source_parent>/renders/<batch>/<basename>.wav,
// the per-entry .settings snapshot (render view is a one-way bubble: disk owns
// each entry's browse state — the entry loader APPLIES the persisted
// tab/zoom/viewport/playhead/W-P on every display, and the view-state autosave
// persists the browsed state back onto the same .settings on every nav-away —
// plus the strict entry load: markers, engine scale, recipe trim),
// the per-entry selection stash with stat-tuple gating, the entry-audio
// decode-and-bind path, and the authoring-session restore on exit.
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
    // Persist the browsed view state onto the entry's `.settings` (strict
    // read-modify-write). Returns the underlying helper's status: false on a
    // refused read-modify-write, true on success. Most callers ignore it (a
    // refused autosave costs the persisted view state, never the session);
    // the Ctrl+Alt+C commit checks it, because it is about to READ the file
    // it just wrote.
    bool write_settings_for(const AppState::RenderViewEntry& e);
    std::pair<uintmax_t, int64_t> wav_stat_tuple(
        const std::filesystem::path& p);
    void stash_render_view_selection_to_active_entry();

    // Persist the active entry's browse state at a leave-this-entry
    // boundary: the .settings view-state autosave (write_settings_for)
    // followed by the in-memory selection stash. The R-toggle exit, both
    // navigation chords, and the close/revert prompts all run this same
    // pair. No-op (returns true) when render view is off or no entry is
    // active. Returns false only when the .settings autosave was refused
    // (strict read-modify-write parse failure) — the Ctrl+Alt+C commit
    // treats that as adversarial and aborts before its own file read.
    bool autosave_active_entry();

    // Load the entry at `index`. Render view is a one-way bubble: disk owns
    // each entry's browse state, so on EVERY load — render-view entry and
    // entry-to-entry navigation alike — this applies the entry's persisted
    // tab / zoom / viewport / playhead / W-P from its strict-parsed .settings
    // (nothing inherits from the live authoring session). The authoring tab
    // and mode are stashed at render-view entry for the exit restore.
    // Rationale at the definition's apply block.
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

    // Clear exactly the snapshot-context fields of the RenderViewContext:
    // the display marker/reset vectors, the snapshot warp frame map, the
    // entry domain begin, the snapshot display total, the snapshot trim
    // bounds, the snapshot commit tab (reset to 'A'), and the authoring-session
    // stash (tab reset to 'A', W/P mode reset to 'W'). Deliberately does
    // NOT touch enabled, list, index, or last_path — those are selection and
    // lifecycle state whose handling legitimately differs per exit, so each
    // clear site keeps its own lines for them. Exists so a new snapshot field
    // gets ONE clear site instead of four hand-maintained lists (which had
    // already drifted on snapshot_commit_tab); the app_state.h declaration
    // comment promising every snapshot field is cleared at every clear site
    // is made true by routing all four exits through this method.
    void clear_snapshot_context();

    // Show the first .wav of the just-rendered batch. Called from
    // GuiInputHandler::dispatch_next_batch_entry's terminal success
    // branch after a batch finishes uncancelled with at least one
    // render on disk, behind the render-view entry gate (render view
    // opens only against an idle worker with nothing parked). Mirrors
    // the R-key toggle-on entry sequence, targeting the first list
    // entry whose batch_folder matches instead of render_view.last_path.
    // A completion under an OPEN render view is unreachable under the
    // entry-gated contract — rationale at the definition — so this
    // method only ever enters fresh.
    void auto_open_batch_at_first_file(
        const std::filesystem::path& batch_folder);
};
