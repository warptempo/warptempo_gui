#include "active_views.h"

#include "input_handler.h"   // auto_select_marker_at_playhead, clear_region_highlight

// Active-views management cluster: the W/P marker-view, A/B tab-view, and
// S/T audio-view switches plus the live/slot view-state sync, reaching
// selection, viewport, and playback_lifecycle through the struct's
// reference members (clamp_viewport_start is a free function declared in
// app_state.h).

// Boundary sync points for the live/slot view-state cache. The live
// AppState view fields (viewport / zoom / playhead) are the
// working copy; the per-view slots are synced only here. The live fields
// are pushed into the active slot on Ctrl+Tab (pre-flip) and Ctrl+S
// (pre-write) via refresh_active_tab_view_from_app, and the destination
// slot is restored into the live fields on switch. During normal editing
// only the live fields move and the backing slot is intentionally stale
// until the next boundary. THE SELECTION IS NOT PART OF THIS: it is never
// parked (the rule is at ViewState, app_state.h) — a column or tab switch
// CLEARS it and re-acquires by coincidence at the entry.

// Overwrite the active tab's snapshot with the live AppState viewport /
// zoom / playhead. Shared by Ctrl+Tab (pre-flip) and Ctrl+S (pre-write)
// so "remembered spot" semantics stay consistent between the two paths.
void GuiActiveViews::refresh_active_tab_view_from_app() {
    ViewState& t = (app.active_tab_view == 'B') ? app.tab_b : app.tab_a;
    t.viewport_start_sample = app.viewport_start_sample;
    t.zoom_level            = app.zoom_level;
    t.playhead_cursor_sample       = app.playhead_cursor_sample;
    // Lockstep with switch_active_tab_view_to's pull block: adding a per-tab
    // live-mirror field means updating this push, that pull, and ViewState.
    t.trim                = app.trim;
}

// Toggle active editing mode between 'W' (warp) and 'P' (phase reset), and
// CLEAR THE SELECTION: a COLUMN switch clears (the scope rule, architect
// 2026-07-29 — the two columns hold different markers, so an index set means
// nothing after the flip, and clearing is what returns the bare arrows to the
// waveform lane instead of leaving an invisible authoring mode armed). Nothing
// is parked and nothing is restored.
// Visible state (viewport / zoom / playhead) is genuinely unaffected here — with
// the selection emptied this helper owes the marker lane no land at all. Its two
// callers own what happens next: toggle_active_markers_view (`p`) clears any
// resting region and runs the coincidence auto-select, while the propagate
// paste's target-view tail writes its OWN selection and lands on that.
// The clear runs BEFORE the mode flip so clear_selection's stem/overlay damage
// resolves against the LEAVING column's painted pixels — damage follows the
// basis of the pixels it erases. Caller decides what further invalidations to
// run.
void GuiActiveViews::switch_active_markers_view_to(char target_mode) {
    if (target_mode == app.active_markers_view) return;
    selection.clear_selection();
    app.active_markers_view = target_mode;
}

// Ctrl+Tab toggles A/B navigational tabs. Stops playback (deactivating the
// scanner; the cursor is untouched either way), saves current
// viewport/zoom/playhead to the leaving tab, restores the target tab. Does
// not mark the document dirty.
//
// This is the AUTHORING tab switch: it swaps the live view fields WITH the
// per-tab slots (app.tab_a / app.tab_b).
void GuiActiveViews::switch_active_tab_view_to(char target_tab) {
    // THE GESTURE STOP, through its one owner. This is a
    // cursor-committing handler, exactly the caller class named at
    // stop_playback_if_playing, and its guard is the RIGHT one: a
    // narrower `playback.is_playing()` test misses the sub-tick window in which
    // audio has reached its natural end but the scanner is still ACTIVE (the
    // tick deactivates it, and a Ctrl+Tab dispatched before that tick would
    // carry the stopped scanner into the entering tab — the synchronous kick at
    // the tail can paint it there for one frame). The helper handles
    // !is_playing() && scanner_active and early-returns when there is nothing to
    // do, so the call stays refusal-shaped.
    // It does not touch the cursor — that is the Space-launch position and was
    // never moved during playback — so the leaving tab's snapshot below captures
    // it exactly, as it did under the hand-spelled pair this replaced (and which
    // the two stop edges themselves collapsed onto this same call in 2026-07-30's
    // one-stop-body ruling). Its damage is fully
    // redundant here either way: the kick_waveform_sync at the tail invalidates
    // the window from y=0 through the waveform's bottom, top strip included,
    // which is a superset of the stop's own full waveform-area invalidate.
    playback_lifecycle.stop_playback_if_playing();
    // The region-select span is view-domain scratch; the entering tab restores
    // a different viewport (and, under a differing map, a different active
    // domain), so a resting region cannot carry across. The kick_waveform_sync
    // below repaints the whole waveform area, restoring the plain canvas ground.
    app.region = RegionState{};
    // A TAB SWITCH CLEARS THE SELECTION (the scope rule, architect 2026-07-29 —
    // a column or tab switch clears; only the `t` audio-view switch carries).
    // Nothing is stashed and nothing is restored: the tab's remembered spot is
    // its VALUE-shaped band alone (viewport / zoom / playhead / trim /
    // read_only), and the entry re-acquires a selection by coincidence at the
    // tail. Placed HERE, before the band flips, so clear_selection's
    // stem/overlay/playhead-column damage resolves against the LEAVING tab's
    // basis — the basis of the pixels it erases. It also subsumes the
    // shift-range anchor clear this site used to spell out by hand (every
    // Selection mutator dissolves the anchor; the authoritative clear list is at
    // the field, app_state.h). The region reset above stands on its own — no
    // selection mutator touches the region any more.
    selection.clear_selection();
    this->refresh_active_tab_view_from_app();
    app.active_tab_view = target_tab;
    const ViewState& target = (app.active_tab_view == 'A') ? app.tab_a : app.tab_b;
    app.viewport_start_sample = target.viewport_start_sample;
    app.zoom_level            = target.zoom_level;
    // Clamp the restored playhead through the shared live-domain chokepoint
    // (clamp_playhead_to_live_domain, app_state.h) before it goes live: the
    // parked tab's stored cursor can be stale against a CHANGED live domain —
    // marker or scale edits made in target view move the target total between tab
    // switches, and the S/T re-express only translates the values it sees at
    // toggle time. In the common case — both tabs share the one global
    // domain and every stash boundary captures an in-domain live value —
    // the stored value is already in [0, total - 1] and the clamp is a no-op.
    // THE STORED CURSOR IS HONORED VERBATIM (nothing overrides it any more): the
    // clause that used to override it — a restored non-empty selection landing on
    // its focus instead — died with the parked selections it was repairing, so
    // the tabs' "remembered spot" contract is now simply true, with no exception
    // to state. The land Ctrl+Tab can still perform is the coincidence
    // auto-select's, and that one writes back the value already here.
    app.playhead_cursor_sample       = clamp_playhead_to_live_domain(
        target.playhead_cursor_sample, app, audio);
    // Lockstep with refresh_active_tab_view_from_app's push block: adding a
    // per-tab live-mirror field means updating that push, this pull, and
    // ViewState.
    app.trim                = target.trim;
    clamp_viewport_start(app, audio);
    // COINCIDENCE AUTO-SELECT, the tab-entry chokepoint (the rule, the formula and
    // the authoritative call-site inventory live at auto_select_marker_at_playhead,
    // input_pointer.cpp / input_handler.h).
    // PLACED HERE by domain validity: after the playhead, trim, viewport and zoom
    // of the entering tab are all live, so the scan's land-formula conversion
    // reads the ENTERING tab's state. Its single-select damage is mid-flight
    // narrow damage superseded by the kick below, exactly as the deleted collapse
    // at this site was.
    auto_select_marker_at_playhead(app, audio, selection, viewport);
    // One-shot discrete jump (Ctrl+Tab A/B switch): the entering tab restores a
    // different viewport / zoom / playhead, so render the plate synchronously
    // and publish the displayed fingerprint now instead of leaving it to the
    // tick. invalidate_timestamp_area still covers the bottom row the sync
    // rebuild does not — the timestamp itself, and the readout, whose eligibility
    // reads the marker view. (The A/B letter it also used to cover left the row
    // with the row-7 collapse; the tabs show the active tab now.)
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
    // THE SWAP LEFT THE SELECTION EMPTY (a column switch clears — the rule is at
    // switch_active_markers_view_to), so `p` owes the marker lane no land: with no
    // lane the cursor IS the playhead and keeps its own value, and the playhead is
    // genuinely untouched across the flip.
    // THE SWAP CLEARS ANY RESTING REGION (architect
    // 2026-07-29, REVERSING "the STORED highlight survives the column flip"): the
    // span is trim SCRATCH drawn against the column the user just left, and the
    // trim it would aim belongs to the tab, not the column — a span carried
    // across the flip describes work that is no longer on screen. So the clear is
    // wholesale, and it is unconditional because
    // the swap always commits: this function flips W<->P outright, so the helper's
    // same-mode early return cannot fire from here, and its two callers (bare `p`
    // and the settings editor's active_markers_view key, which refuses an
    // unchanged value before dispatching) reach it only for a real flip.
    //
    // COINCIDENCE AUTO-SELECT, the column-entry chokepoint (the rule, the formula
    // and the authoritative call-site inventory live at
    // auto_select_marker_at_playhead, input_pointer.cpp / input_handler.h):
    // the newly-active column is scanned against THIS tab's playhead, so flipping
    // onto a column that has a marker exactly under the cursor arrives with that
    // marker selected — the lane re-entered by coincidence rather than by memory.
    // It runs AFTER the region clear so the single-select it may make is the only
    // thing resting here, and it lives HERE rather than in
    // switch_active_markers_view_to because that helper's second caller — the
    // propagate paste's target-view tail — writes its own selection one line later
    // and an auto-select there would be overwritten for nothing.
    clear_region_highlight(app, viewport);
    auto_select_marker_at_playhead(app, audio, selection, viewport);
    // THE HISTORY MODE'S OWN FOCUS CLEARS ON THIS SWITCH — the W/P half of the
    // rule the S/T toggle carries at its own chokepoint
    // (handle_active_audio_view_toggle; the contract and the full clearer list
    // are at AppState::HistoryMode::focus). The focus is an ordinal into the
    // PAINTED diff-flag list, and this flip changes that list wholesale: the
    // mode paints the ACTIVE column's half of a commit's delta, so W and P show
    // different flags at different ordinals. Placed before the kick below, which
    // rebuilds the flag cache — `focus` is one of that cache's fingerprint
    // fields. Unconditional, and a no-op with the mode down (the field rests at
    // -1 there). The propagate paste's tail reaches
    // switch_active_markers_view_to directly rather than this toggle, and needs
    // nothing: no route into it is admitted while the mode stands.
    app.history_mode.focus = -1;
    // SYNCHRONOUS REBUILD, the third member of the view-switch class (architect
    // 2026-07-30). `p` moves NO viewport and NO domain, so unlike its two
    // siblings the plate CONTENT is unchanged — but app.active_markers_view is a
    // flag-cache FINGERPRINT field, and on_redraw is blit-only: the run loop
    // services the frame callback BEFORE the timerfd tick that runs the
    // fingerprint-guarded rebuild, so a bare invalidate blitted the LEAVING
    // column's flag pixels under the entering column's live passes (lane text,
    // stem, overlay ring all read active_markers_view live) for one frame. This
    // route is the siblings' shape rather than a flag-only reach because
    // GuiActiveViews holds no GuiPaintHandler and the flag rebuild is reachable
    // only through it; the redundant plate render is one discrete keypress's
    // cost, exactly what `t` and Ctrl+Tab already pay. It subsumes the
    // invalidate_waveform_area this replaces — the rebuild damages the identical
    // rect (y=0 through the waveform's bottom, top strip included) — and it
    // covers the settings active_markers_view= twin by construction, that key
    // routing through this same function.
    viewport.kick_waveform_sync();
    viewport.invalidate_timestamp_area();
}
