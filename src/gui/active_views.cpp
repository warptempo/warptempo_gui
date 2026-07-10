#include "active_views.h"

#include <cstdio>
#include <string>

// Active-views management cluster: the W/P marker-view, A/B tab-view, and
// S/T audio-view switches plus the live/slot view-state sync, reaching
// selection, viewport, and playback_lifecycle through the struct's
// reference members (clamp_viewport_start is a free function declared in
// app_state.h).

// Boundary sync points for the live/slot view-state cache. The live
// AppState view fields (viewport / zoom / playhead / selection) are the
// working copy; the per-view slots are synced only here. The live fields
// are pushed into the active slot on Ctrl+Tab (pre-flip) and Ctrl+S
// (pre-write) via refresh_active_tab_view_from_app, and the destination
// slot is restored into the live fields on switch. During normal editing
// only the live fields move and the backing slot is intentionally stale
// until the next boundary. active_view_state() resolves the active slot:
// the active tab's slot in source view, the active render entry's `state`
// in render view, or nullptr when render-view is on with no valid entry
// (callers no-op on nullptr rather than writing a fallback slot).

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
    // Lockstep with switch_active_tab_view_to's pull block: adding a per-tab
    // live-mirror field means updating this push, that pull, and ViewState.
    t.trim                = app.trim;
    t.trim_begin_selected = app.trim_begin_selected;
    t.trim_end_selected   = app.trim_end_selected;
    t.last_selected_trim  = app.last_selected_trim;
    t.last_sel_group      = app.last_sel_group;
}

// Indirection that returns the currently
// active ViewState — the slot that holds the inactive-mode
// selection. Source-view: the active tab. Render-view: the
// active render entry's `state`. Returns nullptr when no valid
// active view-state is available; callers must handle nullptr
// by no-op-ing rather than silently corrupting a fallback slot.
ViewState* GuiActiveViews::active_view_state() {
    if (app.render_view.enabled) {
        if (app.render_view.index >= 0 &&
            app.render_view.index <
                static_cast<int>(app.render_view.list.size())) {
            return &app.render_view.list[app.render_view.index].state;
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
    // Lockstep with refresh_active_tab_view_from_app's push block: adding a
    // per-tab live-mirror field means updating that push, this pull, and
    // ViewState.
    app.trim                = target.trim;
    app.trim_begin_selected = target.trim_begin_selected;
    app.trim_end_selected   = target.trim_end_selected;
    app.last_selected_trim  = target.last_selected_trim;
    app.last_sel_group      = target.last_sel_group;
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
}

// `p` key: toggle into/out of phase reset view. Phase reset markers are
// authored independent of output_format — they're consumed when
// output_format=wav drives the engine and when warptempo_maps derives the
// pair's .phaseresetframemap column; generic_map and midi_map ignore them.
void GuiActiveViews::toggle_active_markers_view() {
    if (app.active_markers_view == 'P') {
        this->switch_active_markers_view_to('W');
    } else {
        this->switch_active_markers_view_to('P');
    }
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
}
