#include "selection.h"

#include "audio.h"
#include "warp_frame_map_view.h"

#include <cstdint>
#include <optional>
#include <set>
#include <vector>

void Selection::damage_playhead_if_focus_flipped(bool was_empty) {
    if (was_empty == app.selected_markers.empty()) return;   // no focus flip
    if (audio.total_frames() <= 0) return;
    // Damage the playhead column (line span + triangle lane). The two-argument
    // form with equal endpoints damages exactly that one column — the playhead
    // itself has not moved, only its PRESENCE at the cursor changed with the
    // selection emptiness: empty paints the cursor line+triangle there;
    // non-empty conceptually moves the cursor COINCIDENT with the marker (hidden
    // behind it — the always-on focus stem stands in for the cursor line, the
    // flag occludes the triangle), so the cursor form stops painting there.
    const double px = playhead_pixel_x(app, audio);
    viewport.invalidate_playhead_columns(px, px);
}

std::optional<int64_t> Selection::phase_overlay_subject() const {
    // Mirror phase_reset_overlay_band's SELECTION-STATE visibility guards
    // (paint_handler.cpp) exactly: P view + target view, selection under the
    // 2-member suppression, no active region, and the focused marker a valid
    // ENABLED phase reset. The geometry guards there (area size, samples-per-
    // pixel, sub-pixel forward width) are NOT selection state — they cannot
    // change across a Selection mutation — so they are excluded here. The
    // subject is the reset's FRAME, not its store index: a reorder remap
    // preserves frames (subject-stable), and two resets sharing one frame paint
    // the overlay at the same column, so a focus swap between them is not a
    // subject change.
    if (app.active_markers_view != 'P') return std::nullopt;
    if (app.active_audio_view != 'T') return std::nullopt;
    if (app.selected_markers.size() >= 2) return std::nullopt;
    if (app.region.active) return std::nullopt;
    const auto& markers = app.phaseresetmarkers.markers();
    const int idx = app.last_selected_marker;
    if (idx < 0 || idx >= static_cast<int>(markers.size())) return std::nullopt;
    if (markers[idx].disabled) return std::nullopt;
    return markers[idx].time_frame;
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
    // (now-deleted) selection-hash rebuild damage. The <2 -> 2+ direction is
    // ALSO covered by the multi-select builders' own invalidate_waveform_area
    // (the downward selection->extent coupling) — redundant-but-harmless.
    viewport.invalidate_waveform_area();
}

std::optional<int64_t> Selection::stem_subject() const {
    // The always-on selected-marker stem paints for a SINGLETON selection, BOTH
    // columns and BOTH audio views, under no further condition. The subject is
    // that one marker's ACTIVE-COLUMN SOURCE frame (invalidate_stem_column maps
    // it to the displayed column, source view identity). Empty or 2+ selected ->
    // no stem.
    // Frame, not index: a reorder remap preserves frames (subject-stable). Reads
    // *selected_markers.begin() to mirror paint_selected_stem's own idx pick.
    if (app.selected_markers.size() != 1) return std::nullopt;
    const int idx = *app.selected_markers.begin();
    if (idx < 0) return std::nullopt;
    if (app.active_markers_view == 'P') {
        const auto& pv = app.phaseresetmarkers.markers();
        if (idx >= static_cast<int>(pv.size())) return std::nullopt;
        return pv[idx].time_frame;
    }
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return std::nullopt;
    return mv[idx].time_frame;
}

void Selection::damage_stem_on_subject_change(
    std::optional<int64_t> old_subject) {
    const std::optional<int64_t> new_subject = stem_subject();
    if (new_subject == old_subject) return;   // subject unchanged
    if (audio.total_frames() <= 0) return;
    // Narrow displayed-basis column damage: the OLD subject's column (loses the
    // stem) and the NEW one's (gains it) — each a no-op when absent or offscreen.
    // invalidate_stem_column erases the COMMITTED DISPLAYED stem pixels
    // (damage-follows-the-pixels), so a same-frame old==new is filtered above and
    // a genuine A->B damages both columns.
    if (old_subject) viewport.invalidate_stem_column(*old_subject);
    if (new_subject) viewport.invalidate_stem_column(*new_subject);
}

void Selection::repair_last_selected() {
    if (app.last_selected_marker < 0) return;
    if (app.selected_markers.count(app.last_selected_marker)) return;
    // The focus is no longer a member; it moves to the largest remaining
    // selected index, or to none. In P + target view with no active region that
    // can change the overlay subject (a stale reset -> the largest remaining
    // selected reset, or -> none), and this runs without a reliable enclosing
    // waveform repaint — bare Return/KpEnter routes here and, in P view, returns
    // before the warp-only editor with no waveform damage, and `c`'s recenter
    // can be a no-op at an already-centered rest — so own the overlay repaint
    // here like every other focus-writing mutator. When reached from
    // toggle_selection_membership this double-fires with that mutator's own pair,
    // harmlessly (both damage the same subject change, a benign damage-union).
    const std::optional<int64_t> old_subject = phase_overlay_subject();
    const std::optional<int64_t> old_stem    = stem_subject();
    if (app.selected_markers.empty()) {
        app.last_selected_marker = -1;
    } else {
        // Pick the largest remaining index in selected_markers (or -1
        // if empty).
        app.last_selected_marker = *app.selected_markers.rbegin();
    }
    damage_overlay_on_subject_change(old_subject);
    damage_stem_on_subject_change(old_stem);
}

void Selection::set_single_selection(int idx) {
    // Membership replace -> CLEAR a SelectionExtent region (it is no longer this
    // selection's extent, and a span whose owner died does not linger).
    // Harmless when a downward selection->extent click re-owns right after
    // (clear-then-re-own). Clearing ACTIVE pixels needs waveform damage of its
    // own — the recolored ground and split halves go, and the singleton stem the
    // region was suppressing comes back — and it cannot be left to a nearby
    // clear_region_highlight, which early-returns on the now-inactive region.
    if (clear_region_on_membership_replace(app.region))
        viewport.invalidate_waveform_area();
    const bool was_empty = app.selected_markers.empty();
    const std::optional<int64_t> old_subject = phase_overlay_subject();
    const std::optional<int64_t> old_stem    = stem_subject();
    // Any non-range selection change dissolves the shift-range anchor (its
    // lifecycle: owned by these mutators alone — it survives a shift release and
    // dies here, at the next membership replace). This is also cycle_selection's
    // clear route (it delegates here).
    app.shift_range_anchor = -1;
    app.selected_markers.clear();
    if (idx >= 0) app.selected_markers.insert(idx);
    app.last_selected_marker = (idx >= 0) ? idx : -1;
    viewport.invalidate_top_strip();
    // The bottom-strip pass/ref readout now shows for the last-selected marker
    // too (not only on hover), so a selection change damages the timestamp area
    // like a hover change does — the marker-text lane rides the top-strip damage.
    viewport.invalidate_timestamp_area();
    damage_playhead_if_focus_flipped(was_empty);
    damage_overlay_on_subject_change(old_subject);
    damage_stem_on_subject_change(old_stem);
}

void Selection::clear_selection() {
    // Membership replace (to empty) -> CLEAR a SelectionExtent region: the span's
    // owner just died, so this takes the span, pixels and all. That is the whole
    // contract from here, WITH NO EXCEPTIONS AT ALL since 2026-07-29 (the Esc
    // ladder's first rung was the one caller that patched the pixels back
    // afterward, and the whole ladder is deleted): no caller gets a surviving
    // extent span out of a deselect, and no route re-forms one.
    if (clear_region_on_membership_replace(app.region))
        viewport.invalidate_waveform_area();
    app.shift_range_anchor = -1;   // dissolve the shift-range anchor
    if (app.selected_markers.empty() && app.last_selected_marker == -1)
        return;   // nothing selected (already empty -> no focus flip)
    const std::optional<int64_t> old_subject = phase_overlay_subject();
    const std::optional<int64_t> old_stem    = stem_subject();
    app.selected_markers.clear();
    app.last_selected_marker = -1;
    viewport.invalidate_top_strip();
    viewport.invalidate_timestamp_area();
    // Non-empty -> empty is always a focus flip here (the already-empty case
    // returned above), so the playhead column repaints — nothing at the cursor
    // (non-empty) back to the cursor line+triangle (empty) — even with no
    // playhead move.
    damage_playhead_if_focus_flipped(/*was_empty=*/false);
    // Clearing the focus erases any overlay it annotated (subject frame -> none)
    // and any singleton stem (its subject -> none).
    damage_overlay_on_subject_change(old_subject);
    damage_stem_on_subject_change(old_stem);
}

void Selection::collapse_to_focused() {
    // TWO CALLER CLASSES, both DELIBERATE ACTS OF THE GESTURE — re-derived
    // 2026-07-29 when ruling 6 deleted the third:
    //   * the FINE-TUNING VALUE gestures (the inherit toggle, the singleton
    //     tempo step), which narrow the selection so the operation and the
    //     resulting selection both target last_selected only;
    //   * HORIZONTAL MOVEMENT, which is a FOCUS ACT (architect 2026-07-29 —
    //     groups are never moved): both position nudges collapse here through
    //     their shared prologue and then step the focus alone. The doctrine, and
    //     the group-verb rule it instances, live at the head of
    //     position_nudge.h.
    // THE DELETED THIRD CLASS was the never-span-less ENFORCEMENT — six sites that
    // collapsed a group because a clear had taken its span. That invariant is
    // RETIRED (ruling 6; the retirement paragraph is at clear_region_highlight's
    // declaration, input_handler.h), so no caller collapses as REPAIR any more:
    // every call here is a gesture doing what it means to do.
    // The GROUP TEMPO gestures do NOT collapse — they went group (architect
    // 2026-07-23) and move the whole selection's images rigidly.
    // last_selected_marker
    // is untouched — it stays the focus. Callers that full-invalidate afterward
    // make the top-strip / timestamp damage here redundant (a benign damage-union,
    // accepted).
    // Membership replace (collapse to the focused singleton) -> CLEAR a
    // SelectionExtent region (a 1-marker extent is degenerate, and the collapse
    // is what killed the span's owner). A TrimWindow region SURVIVES this
    // membership clear, which is right for both classes: a value gesture and a
    // nudge have no business tearing down a chip-row highlight — moot in practice,
    // since a TrimWindow region rests only beside an EMPTY selection and these
    // callers need a focus.
    if (clear_region_on_membership_replace(app.region))
        viewport.invalidate_waveform_area();
    app.shift_range_anchor = -1;   // dissolve the shift-range anchor
    // No focus -> nothing to collapse TO. Both surviving classes depend on the
    // focus being a live member of the very selection they are collapsing, and it
    // is: a 2+ membership carrying focus -1 has exactly one producer in the
    // tree — sanitize_selection_after_restore's empty-touched-set shape — and no
    // real mutation yields an empty touched set, so the state is unreachable
    // today. A derivation, not a guard: this early return exists for the
    // ordinary no-selection call, and the gesture callers never meet it.
    if (app.last_selected_marker < 0) return;   // nothing focused
    const std::optional<int64_t> old_subject = phase_overlay_subject();
    const std::optional<int64_t> old_stem    = stem_subject();
    if (app.selected_markers.size() == 1 &&
        app.selected_markers.count(app.last_selected_marker))
        return;   // already exactly the focused singleton
    app.selected_markers.clear();
    app.selected_markers.insert(app.last_selected_marker);
    viewport.invalidate_top_strip();
    viewport.invalidate_timestamp_area();
    // A REAL multi -> singleton collapse turns the always-on selected-marker stem
    // ON for the focused singleton (its subject goes none -> focused frame): the
    // subject-change owner below damages that column narrowly. The fine-tuning
    // callers can collapse then REFUSE (a wall/no-change nudge, a bracket-edge
    // tempo step) and full-invalidate NOTHING, so this owner — not the caller —
    // covers the stem's appear (both columns, both views). The phase-reset overlay's
    // own P+target repaint rides its subject owner, which the 2+ -> 1 case here also
    // triggers.
    damage_overlay_on_subject_change(old_subject);
    damage_stem_on_subject_change(old_stem);
}

bool Selection::toggle_selection_membership(int idx) {
    // Membership replace -> CLEAR a SelectionExtent region. Harmless when a
    // downward selection->extent ctrl-toggle re-owns right after
    // (clear-then-re-own).
    if (clear_region_on_membership_replace(app.region))
        viewport.invalidate_waveform_area();
    const bool was_empty = app.selected_markers.empty();
    const std::optional<int64_t> old_subject = phase_overlay_subject();
    const std::optional<int64_t> old_stem    = stem_subject();
    app.shift_range_anchor = -1;   // dissolve the shift-range anchor
    if (idx < 0) return false;
    bool added;
    auto it = app.selected_markers.find(idx);
    if (it == app.selected_markers.end()) {
        app.selected_markers.insert(idx);
        app.last_selected_marker = idx;
        added = true;
    } else {
        app.selected_markers.erase(it);
        if (app.last_selected_marker == idx) repair_last_selected();
        added = false;
    }
    viewport.invalidate_top_strip();
    viewport.invalidate_timestamp_area();
    damage_playhead_if_focus_flipped(was_empty);
    damage_overlay_on_subject_change(old_subject);
    // The stem's singleton subject can change here: a toggle to/from a 1-marker
    // selection. (When repair_last_selected fired above it double-fires its own
    // pair, a benign damage-union.)
    damage_stem_on_subject_change(old_stem);
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
    // Membership replace -> CLEAR a SelectionExtent region (the downward
    // selection->extent shift-range re-owns right after via
    // set_region_to_selection_extent, so this is clear-then-re-own).
    if (clear_region_on_membership_replace(app.region))
        viewport.invalidate_waveform_area();
    const bool was_empty = app.selected_markers.empty();
    const std::optional<int64_t> old_subject = phase_overlay_subject();
    const std::optional<int64_t> old_stem    = stem_subject();

    // The active column's store size — the same phase-reset/warp selector
    // cycle_selection uses.
    const int n = (app.active_markers_view == 'P')
        ? static_cast<int>(app.phaseresetmarkers.markers().size())
        : static_cast<int>(app.warpmarkers.markers().size());

    int anchor = app.shift_range_anchor;
    if (anchor < 0 || anchor >= n) {
        // No live anchor: ADOPT THE FOCUS (architect labwc round 2,
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
        // it. Mirror its body (clear + insert + last + the top-strip/timestamp
        // damage pair) and additionally anchor on idx.
        app.selected_markers.clear();
        app.selected_markers.insert(idx);
        app.last_selected_marker = idx;
        app.shift_range_anchor   = idx;
        viewport.invalidate_top_strip();
        viewport.invalidate_timestamp_area();
        damage_playhead_if_focus_flipped(was_empty);
        damage_overlay_on_subject_change(old_subject);
        damage_stem_on_subject_change(old_stem);
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
    app.last_selected_marker = idx;
    viewport.invalidate_top_strip();
    viewport.invalidate_timestamp_area();
    damage_playhead_if_focus_flipped(was_empty);
    damage_overlay_on_subject_change(old_subject);
    damage_stem_on_subject_change(old_stem);
}

void Selection::sanitize_selection_after_restore(int n) {
    // A restore replaces the selection membership from the entry -> CLEAR a
    // SelectionExtent region. This is the CLEAR half of the group restore's
    // clear-then-derive (architect 2026-07-25): restore_history_entry's
    // visual tail runs AFTER this, so a GROUP restore then RE-DERIVES the
    // SelectionExtent region from the fresh touched set (the multi-select clicks'
    // order). For a singleton / settings restore the clear is terminal (no
    // re-derive) — a stale SelectionExtent region must not retarget silently, and
    // now does not rest at all. The sole caller (restore_history_entry) repaints
    // the whole waveform anyway, so this damage is normally redundant; it stays
    // as the structural owner, like the subject-change owners below.
    if (clear_region_on_membership_replace(app.region))
        viewport.invalidate_waveform_area();
    // A restore (undo/redo) dissolves the shift-range anchor, like every other
    // non-range selection mutator (the mutators are its only owners — see the
    // field's lifecycle comment). This is also the route that closes
    // Ctrl+Shift+Z, which arrives with shift still held: the restore's clear is
    // what dissolves the anchor under it.
    app.shift_range_anchor = -1;
    const std::optional<int64_t> old_subject = phase_overlay_subject();
    const std::optional<int64_t> old_stem    = stem_subject();
    std::set<int> cleaned;
    for (int idx : app.selected_markers) {
        if (idx >= 0 && idx < n) cleaned.insert(idx);
    }
    app.selected_markers = std::move(cleaned);
    if (!app.selected_markers.count(app.last_selected_marker)) {
        app.last_selected_marker = -1;
    }
    // Pruning the focused reset out of range erases its overlay (subject ->
    // none), and a sanitize that lands on / leaves a singleton changes the stem
    // subject. The sole caller (undo/redo restore) full-repaints the waveform, so
    // both are normally redundant; they stay as the structural owners so a
    // subject-dropping restore cannot leave a stale overlay/stem if a future path
    // sanitizes without a full redraw.
    damage_overlay_on_subject_change(old_subject);
    damage_stem_on_subject_change(old_stem);
}

void Selection::cycle_selection(bool forward) {
    const bool phase_reset = (app.active_markers_view == 'P');

    // Bind const refs once so the count, frame_of, and is_disabled reads below
    // all index the same live authoring stores.
    const std::vector<GuiWarpMarker>& warp_vec = app.warpmarkers.markers();
    const std::vector<GuiPhaseResetMarker>& phase_reset_vec =
        app.phaseresetmarkers.markers();

    const int n = phase_reset
        ? static_cast<int>(phase_reset_vec.size())
        : static_cast<int>(warp_vec.size());
    // The Tab walk is markers-only (trim bounds are not cycle stops — trim is
    // outside the selection system). frame_of / is_disabled below are only
    // invoked for indices in [0, n), so n == 0 simply yields no candidate.

    // Helper to read frame-of-index in the active domain. Source view:
    // marker source-frame == active-domain frame (identity). Target view:
    // forward-translate through the display context (the live map) so
    // frame_of values are comparable to playhead_cursor_sample /
    // viewport_start_sample below.
    auto frame_of = [&](int i) -> int64_t {
        int64_t src_f;
        if (phase_reset) {
            src_f = phase_reset_vec[i].time_frame;
        } else {
            src_f = warp_vec[i].time_frame;
        }
        return source_frame_to_active_domain(app, audio, src_f);
    };

    // Disabled-skip predicate. Warp side respects label_ref cascade via
    // effective_disabled; phase reset has no cascade and reads the bool.
    auto is_disabled = [&](int i) -> bool {
        if (phase_reset) {
            return phase_reset_vec[i].disabled;
        }
        return effective_disabled(warp_vec, i);
    };

    // The playhead frame is the sole cycle anchor. Strict frame inequalities
    // in the scan below prevent re-landing on the stop we are standing on;
    // markers sharing one active-domain frame are traversed by the in-group
    // step so every member is Tab-reachable — stacks are legal at rest
    // (same-column coincidences, which the parser resolver normalizes at
    // render/preview time). Disabled markers are skipped as if absent from the
    // active mode's list. Trim is not part of the selection system, so trim
    // bounds are not cycle stops; the walk is markers-only.
    const int64_t ph_f = app.playhead_cursor_sample;

    // Current stop: the last-selected marker when it sits on the playhead frame
    // (a playhead moved elsewhere by a click breaks the equality and disables
    // the in-group step below naturally).
    int cur_marker = -1;
    {
        const int last = app.last_selected_marker;
        if (last >= 0 && last < n && frame_of(last) == ph_f) cur_marker = last;
    }

    // In-group step, tried before the frame scan. When the previous Tab landed
    // on a marker, the caller synced the playhead onto it, so that marker's
    // frame equals ph_f. Advance one place within the shared frame in the cycle
    // direction (ascending index forward, descending backward).
    int land_marker = -1;
    if (cur_marker >= 0) {
        if (forward) {
            for (int i = cur_marker + 1; i < n; ++i) {
                if (frame_of(i) != ph_f) break;   // frame-sorted: group ends
                if (is_disabled(i)) continue;
                land_marker = i; break;
            }
        } else {
            for (int i = cur_marker - 1; i >= 0; --i) {
                if (frame_of(i) != ph_f) break;
                if (is_disabled(i)) continue;
                land_marker = i; break;
            }
        }
    }

    // Frame scan: nearest marker strictly past the playhead in the walk
    // direction. Markers are frame-sorted, so the first in-direction hit is the
    // nearest.
    if (land_marker < 0) {
        if (forward) {
            for (int i = 0; i < n; ++i) {
                if (frame_of(i) > ph_f && !is_disabled(i)) {
                    land_marker = i; break;
                }
            }
        } else {
            for (int i = n - 1; i >= 0; --i) {
                if (frame_of(i) < ph_f && !is_disabled(i)) {
                    land_marker = i; break;
                }
            }
        }
    }

    if (land_marker < 0) return;   // nothing ahead

    // Selection only. Viewport positioning is owned entirely by the sole
    // caller (cycle_marker_focus), which always centers the focused stop in
    // one write. A scroll-into-view here would be a redundant
    // intermediate viewport write — overridden by that centering in the same
    // keypress — and the resulting damage, accumulated against a non-final
    // viewport, is what produced the outline-blink / cursor-hop artifact.
    set_single_selection(land_marker);
}

void Selection::select_next_marker() { cycle_selection(true);  }
void Selection::select_prev_marker() { cycle_selection(false); }
