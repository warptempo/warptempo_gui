#include "selection.h"

#include "audio.h"
#include "warp_frame_map_view.h"

#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace {
// THE ONE SUBJECT DERIVATION, file-static so the member below and the lead-in
// launch offset owner read one spelling (the contract is at the member's
// declaration, selection.h).
std::optional<int64_t> overlay_subject(const AppState& app) {
    // Mirror phase_reset_overlay_band's SELECTION-STATE visibility guards
    // (paint_handler.cpp) exactly: P view + target view, selection under the
    // 2-member suppression, and the focused marker a valid ENABLED phase reset.
    // The geometry guards there (area size, samples-per-
    // pixel, sub-pixel forward width) are NOT selection state — they cannot
    // change across a Selection mutation — so they are excluded here. THE `h`
    // HISTORY VIEW'S SUPPRESSION (2026-08-05) is excluded on the same test: the
    // view hides the ring because it paints no live marker surface, and that is
    // a display fact, not selection state. The divergence is deliberate and
    // costs nothing observable, playback having left the view the same day —
    // Space is a consumed no-op in there, so the mirror's other reader, the
    // lead-in launch, cannot run. The
    // subject is the reset's FRAME, not its store index: a reorder remap
    // preserves frames (subject-stable), and two resets sharing one frame paint
    // the overlay at the same column, so a focus swap between them is not a
    // subject change.
    // NO REGION GATE, on either side of the mirror, and none is wanted: the
    // overlay is the trim window and this subject is one phase reset's lead-in
    // ring, so the two annotate different things and neither hides the other.
    // (The belt that stood here — "a span rests only beside an empty selection"
    // — is retired, 2026-08-18: bare `[` shows the overlay and writes no
    // selection, so a shown overlay may rest beside any selection. The
    // conclusion never needed it.) The derivation is at the band
    // (paint_handler.cpp) and the shown/hidden model at RegionState.
    if (app.active_markers_view != 'P') return std::nullopt;
    if (app.active_audio_view != 'T') return std::nullopt;
    if (app.selected_markers.size() >= 2) return std::nullopt;
    const auto& markers = app.phaseresetmarkers.markers();
    const int idx = app.last_selected_marker;
    if (idx < 0 || idx >= static_cast<int>(markers.size())) return std::nullopt;
    if (markers[idx].disabled) return std::nullopt;
    return markers[idx].time_frame;
}
}  // namespace

std::optional<int64_t> Selection::phase_overlay_subject() const {
    return overlay_subject(app);
}

// THE LEAD-IN LAUNCH OFFSET (architect 2026-08-30) — the contract and the
// reader inventory are at the declaration (app_state.h). It lives here
// beside the one subject derivation it reads; kPhaseResetLeadInSamples
// (kN/2) output samples is the drop's own subtraction inverted, so the launch
// lands on the instant the drop was derived to protect — past the seed grain
// TO THE SCHEDULE'S ROUNDING, the accepted residue recorded with the
// derivation at drop_phase_reset_lead_in_at_playhead,
// phaseresetmarkers_ops.cpp (three terms, each half a source frame at the
// LOCAL SEGMENT SLOPE or half an output sample — no fixed sample count, and
// the enumeration is that one prose home's, not this site's). The offset's
// own arithmetic is exact integer
// work, and it is never the painted band, whose right edge snaps to the
// engine's hop lattice and is at most this to that same rounding.
//
// WHAT "DROP THEN SPACE AUDITIONS FROM EXACTLY WHERE THE CURSOR WAS" IS
// EXACT TO (the composition's own residue, ONE term and not the grain's
// three): the two N/2 offsets cancel exactly, so all that separates the
// launch from the pressed-at cursor is THE DROP'S OWN WHOLE-FRAME SNAP —
// the authored reset is snapped to a source frame, and its target image
// therefore sits up to half a source frame at the LOCAL SEGMENT SLOPE from
// the point the drop was measured against. Under a speed-up (slope < 1, the
// common direction) that is under half an OUTPUT sample and the launch is
// exact in integer terms; under a slow-down it is ~2 output samples at tempo
// 0.30 and ~8 at the numeric slope ceiling of 16, unbounded only across a
// label-reference segment. The schedule's rounding is NOT part of this
// quantity — it bears on where the engine's seed grain ends, which is what
// the offset was derived against, not on where the launch lands.
int64_t phase_reset_lead_in_launch_offset(const AppState& app,
                                          const GuiPlayback& playback) {
    return (!playback.is_playing() && overlay_subject(app).has_value())
               ? kPhaseResetLeadInSamples
               : 0;
}

void Selection::damage_overlay_on_subject_change(
    std::optional<int64_t> old_subject) {
    if (phase_overlay_subject() == old_subject) return;   // subject unchanged
    if (audio.total_frames() <= 0) return;
    // Full plate damage: the overlay's forward span is wider than the mutators'
    // top-strip/playhead damage, and a whole-plate blit on a selection change is
    // bounded (selection changes are rare). A subject change is the overlay
    // appearing/disappearing (0<->1 focus, the 1<->2 suppression crossing) or
    // the focus moving to a reset at a different frame — every case the old
    // size-2-only helper missed and that fell back to the stem cache's
    // (now-deleted) selection-hash rebuild damage.
    viewport.invalidate_waveform_area();
}

// THE SELECTED-MARKER STEM'S DAMAGE PAIR IS DELETED (row 5, 2026-08-01).
// stem_subject() named the SINGLETON selection's frame and
// damage_stem_on_subject_change() paid one full waveform-area invalidate
// whenever that frame changed — the whole apparatus existed because the stem
// was a SELECTION cue that could appear, move or vanish with no other repaint
// (a collapse that then refuses, a membership toggle across the 1<->2 line).
// Stems are selection-INDEPENDENT now: every enabled marker stems, always, in
// its class's unselected colour, so a selection change moves no stem and the
// flag's own colour swap is a top-strip fact the mutators already damage. The
// phase-overlay pair above is untouched — its subject really is the focus.

void Selection::repair_last_selected() {
    if (app.last_selected_marker < 0) return;
    if (app.selected_markers.count(app.last_selected_marker)) return;
    // The focus is no longer a member; it moves to the largest remaining
    // selected index, or to none. In P + target view that
    // can change the overlay subject (a stale reset -> the largest remaining
    // selected reset, or -> none), and this runs without a reliable enclosing
    // waveform repaint — bare Return/KpEnter routes here and, in P view, returns
    // before the warp-only editor with no waveform damage, and `c`'s recenter
    // can be a no-op at an already-centered rest — so own the overlay repaint
    // here like every other focus-writing mutator. When reached from
    // toggle_selection_membership this double-fires with that mutator's own pair,
    // harmlessly (both damage the same subject change, a benign damage-union).
    const std::optional<int64_t> old_subject = phase_overlay_subject();
    if (app.selected_markers.empty()) {
        seat_focus(-1);
    } else {
        // Pick the largest remaining index in selected_markers (or -1
        // if empty).
        seat_focus(*app.selected_markers.rbegin());
    }
    damage_overlay_on_subject_change(old_subject);
}

// The contract is at the declaration. The addressed cell is reset here and
// nowhere else in this file, which is what lets a mutator added later
// inherit it by construction.
void Selection::seat_focus(int idx) {
    app.last_selected_marker = idx;
    app.addressed_cell       = MarkerCell::Payload;
}

void Selection::set_single_selection(int idx) {
    const std::optional<int64_t> old_subject = phase_overlay_subject();
    // Any non-range selection change dissolves the shift-range anchor (its
    // lifecycle: owned by these mutators alone — it survives a shift release and
    // dies here, at the next membership replace). This is also cycle_selection's
    // clear route (it delegates here).
    app.shift_range_anchor = -1;
    // AND THE STICKY CTRL DIES WITH IT (2026-08-18): a membership REPLACE is
    // exactly the boundary that ends a plain ctrl+click's accumulated effect,
    // so it ends the mode that produces those clicks. The two bits share this
    // chokepoint and differ only in their one keeper —
    // select_range_from_anchor keeps the anchor, toggle_selection_membership
    // keeps the mode. The whole contract is at AppState::add_to_selection.
    app.add_to_selection = false;
    app.selected_markers.clear();
    if (idx >= 0) app.selected_markers.insert(idx);
    seat_focus((idx >= 0) ? idx : -1);
    viewport.invalidate_top_strip();
    // ONE DAMAGE OWNER, THE TOP STRIP, since 2026-08-29: a selection change
    // moves nothing on the bottom row any more. It used to damage that row's
    // state lane too, for the RESOLVED READOUT that showed the last-selected
    // marker's value there, and that readout retired whole with the one-day
    // status bar — the state cell carries only the two progress-class strings,
    // which no selection writes. The flags' own selected/unselected colour swap
    // rides the top-strip damage above (through the flag cache's selection
    // fingerprint).
    damage_overlay_on_subject_change(old_subject);
}

// The whole-set replace (contract at the declaration): set_single_selection's
// body one arity up, with the same two clears and the same damage — one
// chokepoint, so the sticky ctrl and the shift anchor cannot survive a replace
// made programmatically any more than one made by a click.
void Selection::replace_selection(std::set<int> members, int focus) {
    const std::optional<int64_t> old_subject = phase_overlay_subject();
    app.shift_range_anchor = -1;
    app.add_to_selection   = false;
    app.selected_markers   = std::move(members);
    seat_focus(app.selected_markers.count(focus) ? focus : -1);
    viewport.invalidate_top_strip();
    damage_overlay_on_subject_change(old_subject);
}

void Selection::clear_selection() {
    app.shift_range_anchor = -1;   // dissolve the shift-range anchor
    app.add_to_selection   = false;   // and the sticky ctrl that fed it
    // (Both clears sit ABOVE the already-empty early return below, so a
    // redundant clear still ends the mode — `p`, Ctrl+Tab and the load path
    // all reach this body with nothing selected. NO SELECTION MUTATOR DAMAGES
    // THE BOTTOM ROW, so the Add to selection button's lamp is repainted by
    // the roster's per-tick face comparator (main.cpp), which is that face's
    // standing owner for every bit that moves without a damage call.)
    if (app.selected_markers.empty() && app.last_selected_marker == -1)
        return;   // nothing selected (already empty)
    const std::optional<int64_t> old_subject = phase_overlay_subject();
    app.selected_markers.clear();
    seat_focus(-1);
    viewport.invalidate_top_strip();
    // Clearing the focus erases any overlay it annotated (subject frame -> none).
    damage_overlay_on_subject_change(old_subject);
}

void Selection::collapse_to_focused() {
    // TWO CALLER CLASSES, both DELIBERATE ACTS OF THE GESTURE — re-derived by grep
    // 2026-07-30:
    //   * the FINE-TUNING VALUE gestures (the Ctrl+N inherit toggle, the singleton
    //     tempo step), which narrow the selection so the operation and the
    //     resulting selection both target last_selected only;
    //   * HORIZONTAL MOVEMENT, which is a FOCUS ACT (architect 2026-07-29 —
    //     groups are never moved): both position nudges collapse here through
    //     their shared prologue and then step the focus alone.
    // The doctrine, the group-verb rule both instance, and the architect's
    // general statement of it live at the head of position_nudge.h.
    // THE SPAN-DROPPING CLASS IS GONE (architect 2026-07-30): `t` and `c` were
    // its two members, each collapsing only to keep a group from resting SPANLESS
    // — and with the SPAN FORM retired there is no such state, so both now CARRY
    // their group. (Bare `0` had left the same class one day earlier, re-ruled a
    // pure viewport command.) The never-span-less ENFORCEMENT class died before
    // them, 2026-07-29: no caller collapses as REPAIR any more,
    // every call here is a gesture doing what it means to do.
    // The GROUP TEMPO gestures do NOT collapse — they went group (architect
    // 2026-07-23) and move the whole selection's images rigidly.
    // last_selected_marker
    // is untouched — it stays the focus. Callers that full-invalidate afterward
    // make the top-strip / status-chain damage here redundant (a benign damage-union,
    // accepted).
    app.shift_range_anchor = -1;   // dissolve the shift-range anchor
    app.add_to_selection   = false;   // and the sticky ctrl (a narrow IS a replace)
    // No focus -> nothing to collapse TO. Both surviving classes depend on the
    // focus being a live member of the very selection they are collapsing, and it
    // is: a 2+ membership carrying focus -1 has exactly one producer in the
    // tree — sanitize_selection_after_restore's empty-touched-set shape — and no
    // real mutation yields an empty touched set, so the state is unreachable
    // today. A derivation, not a guard: this early return exists for the
    // ordinary no-selection call, and the gesture callers never meet it.
    if (app.last_selected_marker < 0) return;   // nothing focused
    const std::optional<int64_t> old_subject = phase_overlay_subject();
    if (app.selected_markers.size() == 1 &&
        app.selected_markers.count(app.last_selected_marker))
        return;   // already exactly the focused singleton
    app.selected_markers.clear();
    app.selected_markers.insert(app.last_selected_marker);
    viewport.invalidate_top_strip();
    // The phase-reset overlay's own P+target repaint rides its subject owner,
    // which the 2+ -> 1 case here triggers. (A collapse used to owe the
    // selected-marker stem's APPEAR a damage as well — the fine-tuning callers
    // can collapse then REFUSE and full-invalidate nothing — but stems no longer
    // key on selection at all, so that debt is gone with the stem pair.)
    damage_overlay_on_subject_change(old_subject);
}

bool Selection::toggle_selection_membership(int idx) {
    const std::optional<int64_t> old_subject = phase_overlay_subject();
    app.shift_range_anchor = -1;   // dissolve the shift-range anchor
    // THIS IS THE ONE MUTATOR THAT KEEPS app.add_to_selection (2026-08-18) —
    // the exact mirror of select_range_from_anchor's relationship to the
    // anchor. The mode exists to make plain flag clicks land HERE, so its own
    // act cannot be what ends it; every other Selection mutator clears it.
    // repair_last_selected, called from the remove arm below, deliberately
    // clears neither bit: it is a FOCUS repair inside this act, not an act of
    // its own.
    if (idx < 0) return false;
    bool added;
    auto it = app.selected_markers.find(idx);
    if (it == app.selected_markers.end()) {
        app.selected_markers.insert(idx);
        seat_focus(idx);
        added = true;
    } else {
        app.selected_markers.erase(it);
        if (app.last_selected_marker == idx) repair_last_selected();
        added = false;
    }
    viewport.invalidate_top_strip();
    // (When repair_last_selected fired above it double-fires its own overlay
    // damage, a benign damage-union.)
    damage_overlay_on_subject_change(old_subject);
    return added;
}

void Selection::select_range_from_anchor(int idx) {
    // File-manager inclusive range select (architect 2026-07-23). This is the
    // ONE mutator that keeps/sets app.shift_range_anchor; every OTHER Selection
    // method clears it (see the field's lifecycle comment). The caller lands the
    // playhead on the FOCUS this sets — idx, the clicked range end (architect
    // 2026-07-28, replacing the earliest-selected land) — after this returns, so
    // idx < 0 (never reached from the
    // shift-click path, which resolves a real hit) is a plain no-op guard.
    if (idx < 0) return;
    // SHIFT ENDS THE STICKY CTRL (2026-08-18). Shift+click has its own gesture
    // and beats the mode at the click (the `toggle` term at
    // run_marker_click_act), and the range it selects is a membership REPLACE
    // — the boundary the mode auto-clears on. So this is the one Selection
    // body that KEEPS the shift-range anchor and CLEARS the mode, the exact
    // mirror of toggle_selection_membership above.
    app.add_to_selection = false;
    const std::optional<int64_t> old_subject = phase_overlay_subject();

    // The active column's store size, from its one owner (active_marker_count,
    // app_state.h).
    const int n = active_marker_count(app);

    int anchor = app.shift_range_anchor;
    if (anchor < 0 || anchor >= n) {
        // No live anchor: ADOPT THE FOCUS (architect
        // 2026-07-23). The file-manager convention ranges a shift-click from
        // the CURRENT focus — plain-click A then shift+click B selects A..B —
        // so the anchor seed is the focused marker whenever one exists, not
        // only a prior shift-click. This is the ORDINARY path, not a recovery
        // one: every non-range mutator clears the anchor, so the first
        // shift-click after any plain click, focus move, restore or load lands
        // here. A shift RELEASE is NOT one of those routes — the anchor
        // survives releases (see the field's lifecycle and its accepted delta),
        // so a shift interaction re-started after a release ranges from the
        // surviving anchor and never reaches this arm. (The bounds check stays
        // belt-and-braces for a store shrink.)
        anchor = app.last_selected_marker;
    }
    if (anchor < 0 || anchor >= n) {
        // Nothing focused either: the click anchors the interaction on its own
        // marker (selection = {idx}). Cannot delegate to
        // set_single_selection: that method CLEARS the anchor, and we must set
        // it. Mirror its body (clear + insert + last + the top-strip/status-chain
        // damage pair) and additionally anchor on idx.
        app.selected_markers.clear();
        app.selected_markers.insert(idx);
        seat_focus(idx);
        app.shift_range_anchor   = idx;
        viewport.invalidate_top_strip();
        damage_overlay_on_subject_change(old_subject);
            return;
    }

    // Live (or just-adopted) anchor: selection becomes exactly the inclusive
    // index range between
    // the anchor and idx (stores are time-sorted, so index range == time
    // range; clicks in any order, lo/hi normalized). last_selected == idx (the
    // range end = focus); the anchor is (re-)stored so it stays put across
    // successive shift-clicks of the interaction.
    // Disabled markers in the range are included (selection of disabled markers
    // is legal — Delete and Ctrl+D already operate on them).
    app.shift_range_anchor = anchor;
    const int lo = anchor < idx ? anchor : idx;
    const int hi = anchor < idx ? idx : anchor;
    app.selected_markers.clear();
    for (int i = lo; i <= hi; ++i) app.selected_markers.insert(i);
    seat_focus(idx);
    viewport.invalidate_top_strip();
    damage_overlay_on_subject_change(old_subject);
}

void Selection::sanitize_selection_after_restore(int n) {
    // A restore (undo/redo) dissolves the shift-range anchor, like every other
    // non-range selection mutator (the mutators are its only owners — see the
    // field's lifecycle comment). This is also the route that closes
    // Ctrl+Shift+Z, which arrives with shift still held: the restore's clear is
    // what dissolves the anchor under it.
    app.shift_range_anchor = -1;
    // The sticky ctrl goes with it: a restore REPLACES the membership from a
    // snapshot, which is the boundary the mode ends on.
    app.add_to_selection = false;
    const std::optional<int64_t> old_subject = phase_overlay_subject();
    std::set<int> cleaned;
    for (int idx : app.selected_markers) {
        if (idx >= 0 && idx < n) cleaned.insert(idx);
    }
    app.selected_markers = std::move(cleaned);
    if (!app.selected_markers.count(app.last_selected_marker)) {
        seat_focus(-1);
    }
    // Pruning the focused reset out of range erases its overlay (subject ->
    // none). The sole caller (undo/redo restore) full-repaints the waveform, so
    // this is normally redundant; it stays as the structural owner so a
    // subject-dropping restore cannot leave a stale overlay if a future path
    // sanitizes without a full redraw.
    damage_overlay_on_subject_change(old_subject);
}

void Selection::cycle_selection(bool forward) {
    // THE LANDING IS THE ONE OWNER'S (marker_walk_landing, app_state.cpp —
    // planner decision 59, 2026-08-30): this body's own scan, hoisted whole so
    // the Walk previous / Walk next buttons' face reads the same landing —
    // the seat at the playhead, the in-group step, the nearest enabled marker
    // in the walk direction, an empty or all-disabled store yielding none.
    // Its contract, the domain rule and the disabled-skip are at the owner.
    const int land_marker = marker_walk_landing(app, audio, forward);
    // Nothing ahead — the face is grey here, and since 2026-08-30 the sole
    // caller asks this same owner BEFORE calling at all (cycle_marker_focus's
    // gate, which cards the refusal), so this return is a belt rather than the
    // walk's wall.
    if (land_marker < 0) return;

    // Selection only. Viewport positioning is owned entirely by the sole
    // caller (cycle_marker_focus), which frames the focused stop in one write
    // as ITS caller asked — a centre or follow's page, decided there and never
    // here. A scroll-into-view here would be a redundant intermediate viewport
    // write — overridden by that one write in the same keypress — and the
    // resulting damage, accumulated against a non-final viewport, is what
    // produced the outline-blink / cursor-hop artifact.
    set_single_selection(land_marker);
}

void Selection::select_next_marker() { cycle_selection(true);  }
void Selection::select_prev_marker() { cycle_selection(false); }
