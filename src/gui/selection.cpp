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
    // (paint_handler.cpp) exactly: the P column in target view (S+P paints
    // no overlay and so launches no lead-in, architect 2026-10-12), selection under the
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
// 0.30 and ~8 at the slope ceiling of 16, which a resolved label-reference
// segment shares (the drop's comment). The schedule's rounding is NOT part of this
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
    // Full plate damage, for the one mutator that runs no flag rebuild of
    // its own (sanitize_selection_after_restore; the rule at the
    // declaration): a subject change is the overlay appearing/disappearing
    // (0<->1 focus, the 1<->2 suppression crossing) or the focus moving to a
    // reset at a different frame, and with no rebuild behind it nothing
    // else would compare the ring.
    viewport.invalidate_waveform_area();
}

// PUBLISH A SELECTION CHANGE (architect 2026-11-05, his blink: "saw the
// blink twice in about a dozen or two selects"). A SELECTION CHANGE MOVES THE
// STEMS: the stem is the selection's (2026-11-05; render.h's THE STATES;
// MarkerStem published per selected cue, enabled or disabled, by the flag cache's
// rebuild), so a select takes the stem off the old members and stands it on
// the new. A SELECTION CHANGE'S WHOLE DAMAGE IS THE FLAG CACHE REBUILD'S
// (architect 2026-11-16, the beats; maybe_rebuild_flag_cache,
// waveform_cache.cpp: the selection and the focus are its fingerprint's
// fp_selection_hash, and its tail damages each stem that left or arrived,
// at the stem's own place, and the lead-in ring's four sides where they
// stood and where they stand) — THE MUTATORS DECLARE NO RECT OF THEIR OWN,
// and a change that moves no stem and no ring declares nothing. THE TOP
// STRIP IS NOT DAMAGED HERE: NOTHING PAINTED IN ITS ROWS
// READS THE SELECTION — a cue's chip and label are the same selected or not
// (resolve_flag_face: the selection is the stem's alone), the stem's head
// on the lane's last row and the frame's line is inside the stem's own
// rect, the ring is the canvas's, and every key's face, whatever it asks,
// is the tick's comparator's (main.cpp; the marker verbs' keys stand in
// the bottom icon row) — AND NO CALLER LEANS ON SUCH A DAMAGE for a change
// of its own (every call site of the six publishing mutators):
// clear_selection, collapse_to_focused and repair_last_selected
// publish nothing where the selection already stands as asked, so no site
// of theirs could; and each site of the three that always publish declares
// what it changes itself — the land its playhead (the viewport's lanes and
// the clock), the drops, the paste and the restore the whole waveform area,
// the editors' opens their own strip damage, the BPM form its opener's, the
// ADD SEL sweep its highlight.
// THE RING NEEDS NO DAMAGE OF ITS OWN HERE (architect 2026-11-16, the
// beats): a change of subject is a change of
// the membership or the focus — nothing else of the subject moves inside a
// Selection mutator — so the fingerprint misses and the rebuild below runs
// its tail, which compares the ring's geometry itself: where the geometry
// moved, the old sides and the new are damaged (or the whole area, wherever
// that tail keeps it: a ring some frame drew that no rebuild had compared,
// FlagCache's `ring_drawn_undeclared`, among them); where the subject
// changed and the painted ring did not (a band off screen, or under a px),
// nothing is owed. The states in which the rebuild cannot run paint no
// ring: a load in flight and no audio build a canvas list without one, and
// "no plate yet" exists only under the open's presentation hold, whose
// release presents the whole window. A
// rebuild left to the tick would let a select that lands the playhead in the
// same event (land_playhead_on_marker, the chip tap's road) damage a frame
// that could paint BEFORE the tick: the playhead's line on the newly
// selected marker with the old stems still standing, the new stem a frame
// late — the blink. So this runs the rebuild IN THE SAME EVENT
// (Viewport::refresh_flag_cache, fingerprint-guarded, no plate render), the
// ONE-STATE RULE at paint_marker_stems (paint_handler.cpp): the stash holds
// the new selection's stems before the event returns, whatever it paints
// next, and the stem and the landing paint together. Publishing before the
// caller's land is correct: the stem's column is the marker's, never the
// playhead's. A later write in the same event that moves a fingerprint
// field (a camera move's synchronous plate rebuild, whose tail rebuilds the
// flags) rebuilds again before the frame; the fingerprint makes a repeat
// with nothing moved free. THE COST, ACCEPTED (planner's call 2026-11-05):
// a walk that recenters the camera rebuilds twice; the first
// position-arrow press on a group rebuilds for the collapse and again at
// finish_position_nudge's tail, three times if the held column moves the
// camera. Discrete events, one real rebuild each extra: a rebuild deferred
// to the event's end would be a structural change for a per-event cost, not
// a per-frame one.
// EVERY MUTATOR THAT WRITES THE MEMBERSHIP OR THE FOCUS ENDS HERE —
// set_single_selection, replace_selection, clear_selection,
// collapse_to_focused, toggle_selection_membership and
// repair_last_selected (cycle_selection through set_single_selection) — but
// sanitize_selection_after_restore, whose seat says why.
void Selection::publish_selection_change() {
    viewport.refresh_flag_cache();
}

void Selection::repair_last_selected() {
    if (app.last_selected_marker < 0) return;
    if (app.selected_markers.count(app.last_selected_marker)) return;
    // The focus is no longer a member; it moves to the largest remaining
    // selected index, or to none. In P + target view that
    // can change the overlay subject (a stale reset -> the largest remaining
    // selected reset, or -> none), and this runs without a reliable enclosing
    // waveform repaint — bare Return routes here and, in P view, returns
    // before the warp-only editor with no waveform damage, and `c`'s recenter
    // can be a no-op at an already-centered rest — so own the overlay repaint
    // here like every other focus-writing mutator, through the one publish
    // (publish_selection_change, whose flag rebuild compares the ring and
    // which a standalone repair — bare Return, `c` — needs as much as any
    // select). When reached from
    // toggle_selection_membership this double-fires with that mutator's own
    // publish, harmlessly (the same damage, a benign damage-union, and the
    // second rebuild a fingerprint hit).
    if (app.selected_markers.empty()) {
        seat_focus(-1);
    } else {
        // Pick the largest remaining index in selected_markers (or -1
        // if empty).
        seat_focus(*app.selected_markers.rbegin());
    }
    publish_selection_change();
}

// The contract is at the declaration. The addressed cell is reset here and
// nowhere else in this file, which is what lets a mutator added later
// inherit it by construction.
void Selection::seat_focus(int idx) {
    app.last_selected_marker = idx;
    app.addressed_cell       = MarkerCell::Payload;
}

void Selection::set_single_selection(int idx) {
    // THE STICKY CTRL DOES NOT DIE HERE (architect 2026-09-10): the Add to
    // selection lamp is a TOOL POSTURE whose off edges are acts and lamps
    // (its toggle, the spending acts, its own sweep's commit, TRIM's lit
    // edge), so no Selection mutator touches it. The whole contract is at
    // AppState::add_to_selection.
    app.selected_markers.clear();
    if (idx >= 0) app.selected_markers.insert(idx);
    seat_focus((idx >= 0) ? idx : -1);
    // NO RECT OF ITS OWN (the publish's head): a selection change moves
    // nothing on the bottom row (its keys are the tick's comparator's), the
    // status bar's selection count is the tick's status bar comparator's
    // (refresh_status_bar, main.cpp), and nothing in the top strip moves but
    // the stems' heads, which ride the publish's same-event flag rebuild with the
    // stems.
    publish_selection_change();
}

// The whole-set replace (contract at the declaration): set_single_selection's
// body one arity up, with the same publish — one chokepoint for a replace made
// programmatically.
void Selection::replace_selection(std::set<int> members, int focus) {
    app.selected_markers   = std::move(members);
    seat_focus(app.selected_markers.count(focus) ? focus : -1);
    publish_selection_change();
}

void Selection::clear_selection() {
    if (app.selected_markers.empty() && app.last_selected_marker == -1)
        return;   // nothing selected (already empty)
    app.selected_markers.clear();
    seat_focus(-1);
    // Clearing the focus erases any overlay it annotated (subject frame ->
    // none), and the stems with the membership: both the publish's flag
    // rebuild's to damage.
    publish_selection_change();
}

void Selection::collapse_to_focused() {
    // TWO CALLER CLASSES, both DELIBERATE ACTS OF THE GESTURE:
    //   * the singleton TEMPO STEP (adjust_tempo_cents, warpmarkers_ops.cpp),
    //     which narrows the selection so the operation and the resulting
    //     selection both target last_selected only — its selection already a
    //     singleton there, the group step taking every 2+ press;
    //   * HORIZONTAL MOVEMENT, which is a FOCUS ACT (architect 2026-07-29 —
    //     groups are never moved): both position nudges collapse here through
    //     their shared prologue and then step the focus alone.
    // The doctrine, the group-verb rule both instance, and the architect's
    // general statement of it live at the head of position_nudge.h.
    // `t`, `c` and bare `0` CARRY their group (architect 2026-07-30): no
    // caller collapses as REPAIR, every call here is a gesture doing what it
    // means to do.
    // The GROUP TEMPO gestures do NOT collapse — they went group (architect
    // 2026-07-23) and move the whole selection's images rigidly.
    // last_selected_marker
    // is untouched — it stays the focus. Callers that full-invalidate afterward
    // make the stems' damage here redundant (a benign damage-union,
    // accepted).
    // No focus -> nothing to collapse TO. Both surviving classes depend on the
    // focus being a live member of the very selection they are collapsing, and it
    // is: a 2+ membership carrying focus -1 has exactly one producer in the
    // tree — sanitize_selection_after_restore's empty-touched-set shape — and no
    // real mutation yields an empty touched set, so the state is unreachable
    // today. A derivation, not a guard: this early return exists for the
    // ordinary no-selection call, and the gesture callers never meet it.
    if (app.last_selected_marker < 0) return;   // nothing focused
    if (app.selected_markers.size() == 1 &&
        app.selected_markers.count(app.last_selected_marker))
        return;   // already exactly the focused singleton
    app.selected_markers.clear();
    app.selected_markers.insert(app.last_selected_marker);
    // The phase-reset overlay's own P+target repaint — the 2+ -> 1 case here
    // brings the ring back — rides the publish's flag rebuild, which
    // compares the ring. (A 2+ position nudge can collapse then REFUSE at
    // its wall and full-invalidate nothing, so the stems the
    // collapse takes off the other members are that rebuild's
    // to repaint too, in this event.)
    publish_selection_change();
}

bool Selection::toggle_selection_membership(int idx) {
    // app.add_to_selection is untouched here, as it is in every Selection
    // body (2026-09-10): the mode exists to make plain flag clicks land
    // HERE, and it is a tool posture that only its own toggle puts out.
    // repair_last_selected, called from the remove arm below, likewise clears
    // nothing: it is a FOCUS repair inside this act, not an act of its own.
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
    // (When repair_last_selected fired above it published already, a benign
    // damage-union and a fingerprint hit.)
    publish_selection_change();
    return added;
}

void Selection::sanitize_selection_after_restore(int n) {
    // The sticky ctrl does NOT go with a restore (architect 2026-09-10): a restore
    // replaces the membership, but the lamp describes the POINTER's posture
    // rather than the selection under it, so it survives the restore as it
    // survives every other selection act.
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
    // NOT THROUGH publish_selection_change, the one mutator that is not
    // (2026-11-05): its sole caller, restore_history_entry (undo.cpp), runs it
    // MID-RESTORE — the stores replaced, the audio-view switch and the
    // landing still to come — and that body's tail invalidates the window's
    // rows and kicks the synchronous plate rebuild, whose tail rebuilds the
    // flag cache before any frame paints. A rebuild here would read a basis
    // that restore is about to move, and the frame waits for the tail's.
    // (The post-restore rules' replace_selection / clear_selection just
    // before it on that road do publish. The restore's final plate tail is a
    // fingerprint HIT when the displayed viewport and the target map are
    // unchanged — the common same-tab, same-column undo — so that publish is
    // then the one real rebuild; a restore that also moves a keyed viewport
    // or the target map rebuilds twice. The price of a mutator that never
    // asks who called it.)
    damage_overlay_on_subject_change(old_subject);
}

void Selection::cycle_selection(bool forward) {
    // THE LANDING IS THE ONE OWNER'S (marker_walk_landing, app_state.cpp —
    // 2026-08-30): this body's own scan, hoisted whole so
    // the walk button's face reads the same landing —
    // the seat at the playhead, the in-group step, the nearest enabled marker
    // in the walk direction, an empty or all-disabled store yielding none.
    // Its contract, the domain rule and the disabled-skip are at the owner.
    const int land_marker = marker_walk_landing(app, audio, forward);
    // Nothing ahead — the face is grey here, and the sole caller asks this
    // same owner BEFORE calling at all (cycle_marker_focus's gate, its
    // refusal silent), so this return is a belt rather than the walk's
    // wall.
    if (land_marker < 0) return;

    // Selection only. Viewport positioning is owned entirely by the sole
    // caller (cycle_marker_focus), which frames the focused stop in one write
    // as ITS caller asked — the landing owner (Viewport::land_subject), decided there and never
    // here. A scroll-into-view here would be a redundant intermediate viewport
    // write — overridden by that one write in the same keypress — and the
    // resulting damage, accumulated against a non-final viewport, would
    // produce an outline-blink / cursor-hop artifact.
    set_single_selection(land_marker);
}

void Selection::select_next_marker() { cycle_selection(true);  }
void Selection::select_prev_marker() { cycle_selection(false); }
