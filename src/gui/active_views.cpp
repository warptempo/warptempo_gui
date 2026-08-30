#include "active_views.h"

#include "input_handler.h"   // auto_select_marker_at_playhead

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
    // NOT EVERY ViewState MEMBER IS MIRRORED and the absent ones are not
    // omissions: read_only and bare `0`'s zoom_recall_level are read and written
    // IN PLACE through active_view_state(app), so they need no boundary sync and
    // must not be given one.
    t.trim                = app.trim;
}

// Toggle active editing mode between 'W' (warp) and 'P' (phase reset), and
// CLEAR THE SELECTION: a COLUMN switch clears (the scope rule, architect
// 2026-07-29 — the two columns hold different markers, so an index set means
// nothing after the flip, and clearing is what returns the bare arrows to the
// waveform lane instead of leaving an invisible authoring mode armed). Nothing
// is parked and nothing is restored.
// Visible state (viewport / zoom / playhead) is genuinely unaffected here — with
// the selection emptied this helper owes the marker lane no land at all. ITS
// FOUR CALLERS OWN WHAT HAPPENS NEXT, and this is their inventory:
// toggle_active_markers_view (`p`, below) runs the coincidence auto-select;
// the propagate paste's target-view tail (phase_reset_propagate.cpp) writes
// its OWN selection and lands on that; the undo restore (undo.cpp) writes the
// entry's column tag with its data already installed; and Shift+S's drop from
// any view (input_handler.cpp) lands its own act after the clear.
// The clear runs BEFORE the mode flip so clear_selection's stem/overlay damage
// resolves against the LEAVING column's painted pixels — damage follows the
// basis of the pixels it erases. Caller decides what further invalidations to
// run; the only damage this helper owns is the one a SEATED PINCH's clear owes
// (below), which is why it holds the viewport reference at all.
void GuiActiveViews::switch_active_markers_view_to(char target_mode) {
    if (target_mode == app.active_markers_view) return;
    selection.clear_selection();
    // THE SEATED PINCH'S ANCHOR DIES ON THE W/P WRITE, and it is written HERE —
    // at the writer — rather than in the `p` toggle below, which is where codex
    // round 20 put it and where round 21 found the hole: the toggle is not this
    // helper's only caller, and the others reach it DIRECT and inherit nothing
    // it spells (the undo restore's column tag, the propagate paste's
    // target-view tail and Shift+S's drop from any view all call this, each
    // writing its own selection or landing its own act after the clear — grep
    // the name for the live list). Below the same-mode early return, so a
    // switch that never happened clears nothing. This is the FRESH-GRIP half
    // of the rule — the flip moves neither domain nor viewport, so the held
    // frame stays arithmetically valid and the cost is a re-seat at the
    // centroid on the next two-finger frame. The membership, the derivation
    // and the correctness / fresh-grip split are at clear_touch_zoom_seat's
    // declaration (input_handler.h).
    clear_touch_zoom_seat(app, viewport);
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
    // (THE TAB SWITCH'S OVERLAY HIDE IS DELETED, architect 2026-08-19. It was
    // an IN-PLACE reset here and never a call of clear_region_highlight's. THE OVERLAY'S VISIBILITY IS NOT A PLAYHEAD, SELECTION OR
    // MUTATION CONCERN — it is a view preference about whether the user is
    // looking at the trim, and the ENTERING tab has a trim of its own for the
    // overlay to derive from, so a switch has nothing to put away. Hiding
    // discarded nothing either way, which is exactly why it bought nothing.)
    // The SEATED PINCH's anchor IS cleared here, this function being the A/B
    // WRITER and so a member of that rule in its own right — and it outlived
    // the overlay hide above because it answers a different question, a stale
    // song frame rather than a view preference (codex round 20, moved
    // onto the writers at round 21; the argument, the whole membership and the
    // do-not-do-this note are at clear_touch_zoom_seat's declaration,
    // input_handler.h): the entering tab restores another band entirely, so a
    // pinch held across the switch would resume about a point the fingers never
    // grabbed — the FRESH-GRIP half of the rule, the held frame staying
    // arithmetically valid. Its next two-finger frame seats afresh.
    clear_touch_zoom_seat(app, viewport);
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
    // the field, app_state.h). It touches no region: the overlay's visibility is
    // not a selection concern and no selection mutator writes it.
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
    // tick. TWO OWNERS then cover what the sync rebuild
    // does not, each named for the pixels it erases: the STATUS BAR for the
    // readout, whose eligibility reads the marker view, and the CLOCK CELL for
    // the restored playhead. (The A/B letter the status line also used to cover
    // left the row with the row-7 collapse; the tabs show the active tab now.
    // The clock left it for row 8 on 2026-08-11, which is why this is two calls
    // and not one, and the chain left the bottom row for the tab row on
    // 2026-08-13, which is why the two now name two rows.)
    viewport.kick_waveform_sync();
    viewport.invalidate_clock_area();
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
    // (THE SWAP'S OVERLAY HIDE IS DELETED, 2026-08-19, with the A/B tab
    // switch's and the S/T flip's. THE OVERLAY HIDES WHEN THE PLAYHEAD'S
    // POSITION IN THE MUSIC CHANGES, WHEN A MARKER IS TOUCHED AND WHEN THE
    // SWEEP ENDS — the rule at
    // clear_region_highlight, input_handler.h — and a COLUMN SWITCH does none
    // of the three: the swap empties the selection, so there is no focus to
    // re-express and the playhead is genuinely untouched across the flip, and no
    // marker is touched by a change of which column is drawn. Its 2026-07-29
    // argument was "the user has turned to the other column"; the trim belongs
    // to the TAB rather than the column, so the overlay re-derives unchanged
    // across the flip and there was nothing to turn away from. It discarded
    // nothing either way, which is why it bought nothing.)
    //
    // COINCIDENCE AUTO-SELECT, the column-entry chokepoint (the rule, the formula
    // and the authoritative call-site inventory live at
    // auto_select_marker_at_playhead, input_pointer.cpp / input_handler.h):
    // the newly-active column is scanned against THIS tab's playhead, so flipping
    // onto a column that has a marker exactly under the cursor arrives with that
    // marker selected — the lane re-entered by coincidence rather than by memory.
    // It lives HERE rather than in
    // switch_active_markers_view_to because that helper has other callers (its
    // own comment owns the inventory) — the propagate paste's target-view tail
    // writes its own selection one line later, and an auto-select there would
    // be overwritten for nothing.
    // (NO clear_touch_zoom_seat call here: it moved down onto the W/P WRITER,
    // switch_active_markers_view_to above, at codex round 21 — this toggle's own
    // call was one of the three command-wrapper spellings that let the propagate
    // paste reach a column switch with a seated pinch intact. The flip this
    // function performs still clears the seat; it inherits it from the helper,
    // which is the point.)
    auto_select_marker_at_playhead(app, audio, selection, viewport);
    // THE HISTORY MODE'S OWN FOCUS CLEARS ON THIS SWITCH — the W/P half of the
    // rule the S/T toggle carries at its own chokepoint
    // (switch_active_audio_view_to; the contract and the full clearer list
    // are at AppState::HistoryMode::focus). The focus is an ordinal into the
    // PAINTED diff-flag list, and this flip changes that list wholesale: the
    // mode paints the ACTIVE column's half of a commit's delta, so W and P show
    // different flags at different ordinals. Placed before the kick below, which
    // rebuilds the flag cache — `focus` is one of that cache's fingerprint
    // fields. Unconditional, and a no-op with the mode down (the pair rests
    // empty there). It goes through the ONE clearer, which takes the mode's
    // multi-selection with the focus for the identical reason
    // (clear_history_mode_focus, app_state.h). The propagate paste's tail
    // reaches switch_active_markers_view_to directly rather than this toggle,
    // and needs nothing: no route into it is admitted while the mode stands.
    clear_history_mode_focus(app.history_mode);
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
    // routing through this same function. THE STATUS BAR ALONE below, unlike
    // the A/B switch above: this route damages the selection READOUT (the
    // coincidence auto-select may have changed it) and MOVES NO PLAYHEAD — the
    // auto-select only selects the marker the cursor already stands on — so the
    // clock cell has nothing to repaint.
    viewport.kick_waveform_sync();
}
