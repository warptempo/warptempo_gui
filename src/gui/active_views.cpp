#include "active_views.h"

#include "input_handler.h"   // land_playhead_on_marker, clear_region_highlight

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
    // Stash the live selection into the ACTIVE column's slot, stamped with that
    // column's structural generation: from here on these are raw indices held
    // across commands, and the stamp is what lets the restore tell a still-valid
    // parked selection from one the other tab has shifted out from under (the
    // liveness rule at drop_parked_selection_if_stale, app_state.h).
    if (app.active_markers_view == 'P') {
        t.phase_reset_selected      = app.selected_markers;
        t.phase_reset_last_selected = app.last_selected_marker;
        park_selection_stamp(app, t, 'P');
    } else {
        t.warp_selected           = app.selected_markers;
        t.warp_last_selected      = app.last_selected_marker;
        park_selection_stamp(app, t, 'W');
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
// Saves the active selection into the leaving mode's per-tab slot VERBATIM — a
// leaving group is stashed as a group; the point-form collapse belongs to the
// RESTORE side, at each caller (see toggle_active_markers_view) — then restores
// the destination mode's slot. Visible state (viewport /
// zoom / playhead) is unaffected — the PLAYHEAD LAND belongs to the callers,
// which disagree about it: toggle_active_markers_view (`p`) lands on the
// restored focus, while the propagate paste's tail overwrites this selection
// with its own and lands on that. The wholesale REGION CLEAR is likewise the
// callers' (both of them do it, each at its own site); all this helper does to
// the region is prune_live_selection's membership clear of a SelectionExtent
// span. Caller decides what invalidations to
// run; this helper just shuffles the AppState fields.
void GuiActiveViews::switch_active_markers_view_to(char target_mode) {
    if (target_mode == app.active_markers_view) return;
    ViewState* vs = this->active_view_state();
    if (!vs) return;
    // Each arm STASHES the leaving column (stamped) and RESTORES the entering
    // one (stale-checked first): the entering slot has sat parked across
    // arbitrary commands, so a marker inserted or deleted in that column since
    // the stash makes its indices meaningless and the check empties it — this
    // swap then hands the lane nothing and the cursor stays the playhead. The
    // rule lives at drop_parked_selection_if_stale (app_state.h).
    if (app.active_markers_view == 'P') {
        vs->phase_reset_selected      = app.selected_markers;
        vs->phase_reset_last_selected = app.last_selected_marker;
        park_selection_stamp(app, *vs, 'P');
        drop_parked_selection_if_stale(app, *vs, 'W');
        app.selected_markers        = vs->warp_selected;
        app.last_selected_marker    = vs->warp_last_selected;
    } else {
        vs->warp_selected           = app.selected_markers;
        vs->warp_last_selected      = app.last_selected_marker;
        park_selection_stamp(app, *vs, 'W');
        drop_parked_selection_if_stale(app, *vs, 'P');
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
    // it exactly, as it did under the hand-spelled stop this replaces
    // (playback.stop() + restore_playhead_to_lsp()). The one damage that form
    // added and this one does not, restore_playhead_to_lsp's top-strip
    // invalidation, is redundant here: the kick_waveform_sync at the tail
    // invalidates the window from y=0 through the waveform's bottom, top strip
    // included.
    playback_lifecycle.stop_playback_if_playing();
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
    // STALE-CHECK BEFORE READING: this slot was stashed when the tab was last
    // left, and the marker stores are GLOBAL — an insert, a delete, or a
    // wholesale replace performed in the tab we are leaving has shifted or
    // destroyed the rows these indices name. On a mismatch the slot is emptied
    // and this restores nothing, which is exactly what the stored-cursor restore
    // above wants: no lane, so the cursor is the playhead. On a match this is
    // byte-identical to before. The rule and the why-drop-rather-than-repair
    // argument live at drop_parked_selection_if_stale (app_state.h).
    ViewState& target_mut = (app.active_tab_view == 'A') ? app.tab_a : app.tab_b;
    drop_parked_selection_if_stale(app, target_mut,
                                   app.active_markers_view == 'P' ? 'P' : 'W');
    if (app.active_markers_view == 'P') {
        app.selected_markers     = target.phase_reset_selected;
        app.last_selected_marker = target.phase_reset_last_selected;
    } else {
        app.selected_markers     = target.warp_selected;
        app.last_selected_marker = target.warp_last_selected;
    }
    // THE WHOLESALE REPLACE JOINS THE ANCHOR LIFECYCLE. This is a membership
    // replace like any Selection mutator's, so it dissolves the shift-range
    // anchor — the field's contract (app_state.h, which holds the authoritative
    // clear list this site is on as the wholesale-replace class) is that the
    // anchor dies at the next membership replace, and it no longer dies at the
    // shift release. Without the clear a tab-A anchor index survives into tab B
    // and a shift-click there ranges from an unrelated row whenever that index
    // happens to be in bounds; the collapse below covers only the 2+ case, and
    // only incidentally, through collapse_to_focused's own clear.
    app.shift_range_anchor = -1;
    // A 2+ selection restored from the entering tab's slot COLLAPSES to its
    // focus (the never-rest-2+-without-a-span invariant, stated at
    // clear_region_highlight's declaration): this switch cleared the region
    // above and derives none for the restored group, and the A/B flip is a view
    // switch like `t` and `p`, which land in point form by the same 2026-07-29
    // ruling. The stored slot keeps whatever it held — only the live selection
    // collapses.
    // ITS NARROW DAMAGE IS SUPERSEDED HERE, DELIBERATELY: collapse_to_focused
    // raises stem/overlay damage against the basis live at the call, and this
    // restore is MID-FLIGHT — the selection has been swapped in but the entering
    // tab's trim, viewport and zoom land only below, so that basis is not the
    // one the collapse will be painted on. The kick_waveform_sync at the tail
    // (plus its invalidate_timestamp_area) is what actually repaints it, and the
    // dependency is load-bearing: narrowing that tail would strand these pixels.
    // No reordering — the collapse belongs with the membership write it judges.
    if (app.selected_markers.size() >= 2) selection.collapse_to_focused();
    // Lockstep with refresh_active_tab_view_from_app's push block: adding a
    // per-tab live-mirror field means updating that push, this pull, and
    // ViewState.
    app.trim                = target.trim;
    clamp_viewport_start(app, audio);
    // A NON-EMPTY RESTORED SELECTION LANDS ON ITS FOCUS, OVERRIDING THE STORED
    // CURSOR. The stash is a PAIR, but the two halves can drift apart while the
    // tab is parked: marker stores and the warp map are GLOBAL, so an edit made
    // in the other tab (a tempo or scale change upstream of the parked focus)
    // moves that focus's target image while the saved cursor keeps the old one.
    // Restoring both verbatim then rests a flag claiming to be the playhead
    // beside a cursor somewhere else — THE MARKER LANE OWNS THE PLAYHEAD
    // (land_playhead_on_marker's doctrine, input_pointer.cpp), and this switch
    // hands the lane a focus, so it owes the land; the map-change re-land form is
    // the singleton tempo step's label-coupling precedent.
    // THIS OVERRIDES ONE CLAUSE OF THE STORED-PAIR CONTRACT ("Ctrl+Tab restores
    // whatever focus and playhead were stashed") for the NON-EMPTY case only. In
    // the coherent case — no cross-tab edit moved anything — the stored cursor
    // and the focus's image are the same frame and this changes nothing; it
    // matters exactly where they diverged, and there the stored cursor is the lie.
    // An EMPTY restored selection keeps the stored cursor verbatim, contract
    // intact: with no lane the cursor is the playhead in its own right.
    // Planner-converted from a codex finding, pending architect review.
    // PLACED HERE by domain validity: after the collapse (so it lands on the
    // focus that survives) and after the trim/viewport restore, so the land's
    // conversion reads the ENTERING tab's state; it is a pure cursor write with
    // no viewport move, and the kick below repaints it.
    if (!app.selected_markers.empty() && app.last_selected_marker >= 0)
        land_playhead_on_marker(app, audio, viewport, app.last_selected_marker);
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
    // Land on the restored focus — a PURE playhead write, the LAND itself
    // touching no region (the swap's own wholesale clear is stated right below
    // and runs before it). An EMPTY destination slot LEAVES the lane, so the
    // playhead stays put and the cursor simply paints again (the rule's second
    // clause) — no move. THE SWAP CLEARS ANY RESTING REGION, whatever its
    // provenance (architect 2026-07-29, REVERSING "the STORED highlight survives
    // the column flip"): a span rests beside a selection only as that selection's
    // own extent or as the trim's highlight, and the swap hands the lane a
    // DIFFERENT column's selection and focus — an extent of the column just left
    // has no owner here (prune_live_selection inside the helper above already
    // took that one through the membership clear), and a trim highlight left
    // standing across the flip is the same ownerless span one storage level up.
    // So the clear is wholesale, and it is unconditional because the swap always
    // commits: this function flips W<->P outright, so the helper's
    // same-mode early return cannot fire from here, and its two callers (bare `p`
    // and the settings editor's active_markers_view key, which refuses an
    // unchanged value before dispatching) reach it only for a real flip.
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
    //
    // A VIEW SWITCH ALWAYS LANDS YOU IN POINT FORM (architect 2026-07-29, shared
    // with the `t` S/T switch): a restored selection of 2+ COLLAPSES to its
    // focused marker, so `p` leaves a singleton (or an empty selection), never a
    // group. Most work here is done one marker at a time, and one consistent rule
    // beats an imagined convenience.
    // THE SHAPE IS COLLAPSE-AT-RESTORE, NOT COLLAPSE-AT-STASH: the helper above
    // STASHED the leaving column's live selection FIRST, so a group that was
    // selected in the column being left rests in its inactive slot AS A GROUP.
    // The collapse then fires on the RESTORED selection, here, synchronously
    // before any paint or reader sees it. "Group selections do not cross view
    // switches" therefore holds at the OBSERVABLE level: the stored group is
    // unreadable until some restore path brings it back, and every such path
    // either collapses it or overwrites it outright — `p` here, Ctrl+Tab at its
    // own collapse in switch_active_tab_view_to, the propagate paste's
    // no-created-set arm at its else-collapse, and undo's inline W/P swap, which
    // owns its own visual language (sanitize, then the restore tail's wholesale
    // region clear and, for a group entry, the touched-set re-select plus extent
    // re-derive).
    // ONE HONEST CAVEAT on "collapses or overwrites": undo's inline swap has an
    // EMPTY-TOUCHED-SET branch that does neither — it restores the stashed group
    // and the tail's >= 2 arm then LANDS on it and derives an extent, so the
    // group is visible again, with a span. The observable never-span-less ruling
    // still holds there (a group resting WITH its span is exactly what the rule
    // requires); what does not hold is the stricter "no group survives a view
    // switch" reading. The branch is producible only by a pure no-change
    // permutation of the store, which no real mutation pushes, so nothing
    // reaches it today. It runs BEFORE the clear and the land so
    // both act on the collapsed singleton — the collapse keeps
    // last_selected_marker, so the land below targets the same focus either way,
    // and its own membership clear is a no-op ahead of the wholesale clear. The
    // collapse lives HERE and not in switch_active_markers_view_to for the reason
    // the land does: the propagate paste rides that helper and overwrites the
    // selection with its created set one line later.
    // The size >= 2 guard is LOAD-BEARING, not an optimization:
    // collapse_to_focused early-returns only on an already-focused SINGLETON, so
    // against an EMPTY selection carrying a live focus index it would INSERT that
    // focus and resurrect a selection the user dropped. AppState declares the
    // pairing (focus is -1 or a member), and prune_live_selection repairs it
    // inside the helper above — but the guard keeps this site's precondition
    // local instead of leaning on either.
    if (app.selected_markers.size() >= 2) selection.collapse_to_focused();
    clear_region_highlight(app, viewport);
    if (!app.selected_markers.empty()) {
        land_playhead_on_marker(app, audio, viewport, app.last_selected_marker);
    }
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
}
