#include "active_views.h"

#include "target_render.h"

#include <cstdio>
#include <string>

// X.7.7: active-views management cluster. Method bodies are byte-identical
// to the lambdas they replaced in main.cpp, with these mechanical rewrites:
//
//   active_view_state,
//   refresh_active_tab_view_from_app,
//   switch_active_markers_view_to,
//   switch_active_tab_view_to            → this->method_name (intra-cluster calls)
//   prune_live_selection            → selection.prune_live_selection
//   invalidate_waveform_area        → viewport.invalidate_waveform_area
//   invalidate_timestamp_area       → viewport.invalidate_timestamp_area
//   clear_hover_popup               → viewport.clear_hover_popup
//   stop_playback_if_playing        → playback_lifecycle.stop_playback_if_playing
//                                     (X.7.13 retired the std::function forwarders)
//   settings_get                    → free function, takes app explicitly
//
// Free function calls (clamp_viewport_start) keep their original spelling —
// declared at file scope in app_state.h.

// Overwrite the active tab's snapshot with the live AppState viewport /
// zoom / playhead. Shared by Ctrl+Tab (pre-flip) and Ctrl+S (pre-write)
// so "remembered spot" semantics stay consistent between the two paths.
// Also stashes the active selection into the per-mode slot so a tab
// flip + mode flip can restore the right pair on return.
void GuiActiveViews::refresh_active_tab_view_from_app() {
    ViewState& t = (app.active_tab_view == 'B') ? app.tab_b : app.tab_a;
    t.viewport_start_sample = app.viewport_start_sample;
    t.zoom_level            = app.zoom_level;
    t.playhead_cursor_sample       = app.playhead_cursor_sample;
    if (app.active_markers_view == 'P') {
        t.phase_reset_selected      = app.selected_markers;
        t.phase_reset_last_selected = app.last_selected_marker;
    } else {
        t.warp_selected           = app.selected_markers;
        t.warp_last_selected      = app.last_selected_marker;
    }
}

// Brief J.2 Section 1: indirection that returns the currently
// active ViewState — the slot that holds the inactive-mode
// selection. Source-view: the active tab. Render-view: the
// active render entry's `state`. Returns nullptr when no valid
// active view-state is available; callers must handle nullptr
// by no-op-ing rather than silently corrupting a fallback slot.
ViewState* GuiActiveViews::active_view_state() {
    if (app.render_view_enabled) {
        if (app.render_view_index >= 0 &&
            app.render_view_index <
                static_cast<int>(app.render_view_list.size())) {
            return &app.render_view_list[app.render_view_index].state;
        }
        // Render-view enabled but no valid entry. Return null
        // rather than silently writing render-view indices into
        // a source tab slot.
        return nullptr;
    }
    return (app.active_tab_view == 'B') ? &app.tab_b : &app.tab_a;
}

// Toggle active editing mode between 'W' (warp) and 'P' (phase reset).
// Saves the active selection into the leaving mode's per-tab slot,
// then restores the destination mode's slot. Visible state (viewport /
// zoom / playhead) is unaffected. Caller decides what invalidations to
// run; this helper just shuffles the AppState fields.
void GuiActiveViews::switch_active_markers_view_to(char target_mode) {
    if (target_mode == app.active_markers_view) return;
    ViewState* vs = this->active_view_state();
    if (!vs) return;
    if (app.active_markers_view == 'P') {
        vs->phase_reset_selected      = app.selected_markers;
        vs->phase_reset_last_selected = app.last_selected_marker;
        app.selected_markers        = vs->warp_selected;
        app.last_selected_marker    = vs->warp_last_selected;
    } else {
        vs->warp_selected           = app.selected_markers;
        vs->warp_last_selected      = app.last_selected_marker;
        app.selected_markers        = vs->phase_reset_selected;
        app.last_selected_marker    = vs->phase_reset_last_selected;
    }
    app.active_markers_view = target_mode;
    selection.prune_live_selection();
    viewport.clear_hover_popup();
}

// Ctrl+Tab toggles A/B navigational tabs. Stops playback with
// return-to-launch, saves current viewport/zoom/playhead to the
// leaving tab, restores the target tab. Does not mark the document
// dirty.
void GuiActiveViews::switch_active_tab_view_to(char target_tab) {
    // Mirror toggle_playback's stop branch: tab switch is not a
    // navigational commit, so the leaving tab's snapshot should
    // capture the Space-launch position rather than the run-time
    // audio cursor. stop_playback_if_playing's LSP overwrite is
    // wrong here.
    if (playback_lifecycle.playback.is_playing()) {
        playback_lifecycle.playback.stop();
        playback_lifecycle.restore_playhead_to_lsp();
    }
    viewport.clear_hover_popup();
    this->refresh_active_tab_view_from_app();
    app.active_tab_view = target_tab;
    const ViewState& target = (app.active_tab_view == 'A') ? app.tab_a : app.tab_b;
    app.viewport_start_sample = target.viewport_start_sample;
    app.zoom_level            = target.zoom_level;
    app.playhead_cursor_sample       = target.playhead_cursor_sample;
    // Restore the active selection from the destination tab's
    // current-mode slot. Mode itself is per-AppState (not per-tab),
    // so the destination tab's other-mode slot stays warm for any
    // future `p` flip back inside that tab.
    if (app.active_markers_view == 'P') {
        app.selected_markers     = target.phase_reset_selected;
        app.last_selected_marker = target.phase_reset_last_selected;
    } else {
        app.selected_markers     = target.warp_selected;
        app.last_selected_marker = target.warp_last_selected;
    }
    clamp_viewport_start(app, audio);
    // One-shot discrete jump (Ctrl+Tab A/B switch): the entering tab restores a
    // different viewport / zoom / playhead, so render the plate synchronously
    // and publish the displayed fingerprint now instead of leaving it to the
    // tick. kick_waveform_sync emits the same waveform-region damage
    // invalidate_waveform_area does, so the explicit call below is a redundant
    // coalesced duplicate left in place for a minimal diff. invalidate_
    // timestamp_area still covers the bottom-strip letter + ts text the sync
    // rebuild does not.
    viewport.kick_waveform_sync();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    // Per-tab trim is engine input. Switching tabs in target view
    // invalidates the target buffer (rendered against the leaving
    // tab's trim) — fire a fresh render against the entering tab.
    // No-op in source view.
    target_render.trigger();
}

// `p` key: toggle into/out of phase reset view. Phase reset markers are
// authored independent of output_format — they're consumed only when
// output_format=wav drives the engine; non-wav formats ignore them.
void GuiActiveViews::toggle_active_markers_view() {
    if (app.active_markers_view == 'P') {
        this->switch_active_markers_view_to('W');
    } else {
        this->switch_active_markers_view_to('P');
    }
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
}
