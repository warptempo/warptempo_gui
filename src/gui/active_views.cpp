#include "active_views.h"

#include "input_handler.h"   // land_playhead_on_marker

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
// until the next boundary. active_view_state() resolves the active
// AUTHORING tab's slot.

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
}

// Returns the active AUTHORING tab's ViewState slot — the slot that holds
// the inactive-mode selection.
ViewState* GuiActiveViews::active_view_state() {
    return (app.active_tab_view == 'B') ? &app.tab_b : &app.tab_a;
}

// Toggle active editing mode between 'W' (warp) and 'P' (phase reset).
// Saves the active selection into the leaving mode's per-tab slot,
// then restores the destination mode's slot. Visible state (viewport /
// zoom / playhead) is unaffected — the PLAYHEAD LAND belongs to the callers,
// which disagree about it: toggle_active_markers_view (`p`) lands on the
// restored focus, while the propagate paste's tail overwrites this selection
// with its own and lands on that. Caller decides what invalidations to
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

// Ctrl+Tab toggles A/B navigational tabs. Stops playback (deactivating the
// scanner; the cursor is untouched either way), saves current
// viewport/zoom/playhead to the leaving tab, restores the target tab. Does
// not mark the document dirty.
//
// This is the AUTHORING tab switch: it swaps the live view fields WITH the
// per-tab slots (app.tab_a / app.tab_b).
void GuiActiveViews::switch_active_tab_view_to(char target_tab) {
    // Mirror toggle_playback's stop branch (playback.stop() then
    // restore_playhead_to_lsp()). Neither this nor stop_playback_if_playing
    // touches the cursor — it is the Space-launch position and was never
    // moved during playback — so the leaving tab's snapshot below captures
    // it exactly regardless of which stop path runs.
    if (playback_lifecycle.playback.is_playing()) {
        playback_lifecycle.playback.stop();
        playback_lifecycle.restore_playhead_to_lsp();
    }
    viewport.clear_hover_popup();
    // The region-select span is view-domain scratch; the entering tab restores
    // a different viewport (and, under a differing map, a different active
    // domain), so a resting region cannot carry across. The kick_waveform_sync
    // below repaints the whole waveform area, restoring the plain canvas ground.
    app.region = RegionState{};
    this->refresh_active_tab_view_from_app();
    app.active_tab_view = target_tab;
    const ViewState& target = (app.active_tab_view == 'A') ? app.tab_a : app.tab_b;
    app.viewport_start_sample = target.viewport_start_sample;
    app.zoom_level            = target.zoom_level;
    // Clamp the restored playhead through the shared live-domain chokepoint
    // (clamp_playhead_to_live_domain, app_state.h) before it goes live: the
    // parked tab's stash can be stale against a CHANGED live domain — marker
    // or scale edits made in target view move the target total between tab
    // switches, and the S/T re-express only translates the values it sees at
    // toggle time. In the common case — both tabs share the one global
    // domain and every stash boundary captures an in-domain live value —
    // the stash is already in [0, total - 1] and the clamp is a no-op.
    app.playhead_cursor_sample       = clamp_playhead_to_live_domain(
        target.playhead_cursor_sample, app, audio);
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
    clamp_viewport_start(app, audio);
    // One-shot discrete jump (Ctrl+Tab A/B switch): the entering tab restores a
    // different viewport / zoom / playhead, so render the plate synchronously
    // and publish the displayed fingerprint now instead of leaving it to the
    // tick. invalidate_timestamp_area still covers the bottom-strip letter +
    // ts text the sync rebuild does not.
    viewport.kick_waveform_sync();
    viewport.invalidate_timestamp_area();
}

// `p` key: toggle into/out of phase reset view. Phase reset markers are
// consumed by the engine on every wav render (the only product) and drive
// the .phaseresetframemap column of the cache-dir framemap pair.
void GuiActiveViews::toggle_active_markers_view() {
    if (app.active_markers_view == 'P') {
        this->switch_active_markers_view_to('W');
    } else {
        this->switch_active_markers_view_to('P');
    }
    // The marker lane owns the playhead (the rule is stated in full at
    // land_playhead_on_marker, input_pointer.cpp). The swap above restores the
    // destination column's stored selection and focus and deliberately leaves the
    // visible state alone, so `p` can re-enter the lane with the focus at one
    // position and the playhead at another — the restored flag would claim to be
    // the playhead while Space played from wherever the W-side click left it.
    // Land on the restored focus — a PURE playhead write, `p` adding no region
    // clear of its own. An EMPTY destination slot LEAVES the lane, so the
    // playhead stays put and the cursor simply paints again (the rule's second
    // clause) — no move. THE REGION STORY IS THE SWAP'S OWN membership replace:
    // prune_live_selection inside the helper above clears a SelectionExtent span
    // whose owning selection just left with the column, while a TrimWindow or
    // Free region deliberately SURVIVES the column flip (the stored highlight is
    // column-independent).
    //
    // The land lives HERE and not in switch_active_markers_view_to because that
    // helper has a second caller: the propagate paste's target-view tail
    // (PhaseResetPropagate::land_paste_in_target_view), which sets its OWN
    // selection and its own land immediately afterward. A land inside the helper
    // would fire against the restored P-slot selection and be overwritten one
    // line later.
    // The emptiness test IS the lane test; no separate focus-index guard is
    // needed (prune_live_selection above leaves the focus a live member whenever
    // the selection is non-empty, and the helper is bounds-guarded regardless).
    if (!app.selected_markers.empty()) {
        land_playhead_on_marker(app, audio, viewport, app.last_selected_marker);
    }
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
}
