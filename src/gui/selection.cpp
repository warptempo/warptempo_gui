#include "selection.h"

#include "audio.h"
#include "warp_frame_map_view.h"

#include <cstdint>
#include <set>
#include <vector>

void Selection::damage_playhead_if_focus_flipped(bool was_empty) {
    if (was_empty == app.selected_markers.empty()) return;   // no focus flip
    if (audio.total_frames() <= 0) return;
    // Damage the playhead column (line span + triangle lane). The two-argument
    // form with equal endpoints damages exactly that one column — the playhead
    // itself has not moved, only its PRESENCE at the cursor changed with the
    // selection emptiness: empty paints the green line+triangle there, non-empty
    // paints nothing (the grey focus triangle moved onto the selected markers —
    // paint_selected_marker_triangles).
    const double px = playhead_pixel_x(app, audio);
    viewport.invalidate_playhead_columns(px, px);
}

void Selection::damage_overlay_on_size2_crossing(size_t old_size) {
    const bool old_multi = old_size >= 2;
    const bool new_multi = app.selected_markers.size() >= 2;
    if (old_multi == new_multi) return;   // 2 threshold not crossed
    // The overlay lives only in P + target view; elsewhere there is nothing to
    // paint/erase, so the crossing needs no waveform damage there.
    if (app.active_markers_view != 'P' || app.active_audio_view != 'T') return;
    if (audio.total_frames() <= 0) return;
    // Full plate damage: the overlay's forward span is wider than the mutators'
    // top-strip/playhead damage, and a whole-plate blit on a rare size-2 crossing
    // is bounded. The <2 -> 2+ direction is also covered by the multi-select
    // builders' own invalidate_waveform_area (the downward selection->extent
    // coupling); this makes the
    // 2+ -> <2 direction — e.g. a propagate-paste multi-selection reduced to one
    // by a plain marker click — equally covered, redundant-but-harmless with the
    // builders.
    viewport.invalidate_waveform_area();
}

void Selection::repair_last_selected() {
    if (app.last_selected_marker < 0) return;
    if (app.selected_markers.count(app.last_selected_marker)) return;
    if (app.selected_markers.empty()) {
        app.last_selected_marker = -1;
    } else {
        // Pick the largest remaining index in selected_markers (or -1
        // if empty).
        app.last_selected_marker = *app.selected_markers.rbegin();
    }
}

void Selection::set_single_selection(int idx) {
    // Membership replace -> demote a SelectionExtent region to Free (it is no
    // longer this selection's extent). Harmless when a Direction-B click re-owns
    // right after (demote-then-re-own).
    demote_region_provenance(app.region);
    const bool was_empty = app.selected_markers.empty();
    const size_t old_size = app.selected_markers.size();
    // Any non-range selection change dissolves the shift-range anchor (its
    // lifecycle: alive only across a continuous shift-held interaction). This
    // is also cycle_selection's clear route (it delegates here).
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
    damage_overlay_on_size2_crossing(old_size);
}

void Selection::focus_without_collapse(int idx) {
    // Membership untouched — only the FOCUS moves. Dissolve the shift-range
    // anchor (every non-range selection mutator does; a subsequent shift-click
    // re-adopts the focus). No damage_playhead_if_focus_flipped /
    // damage_overlay_on_size2_crossing: the selection SIZE does not change, so
    // neither the focus-emptiness boundary nor the size-2 threshold can cross.
    if (idx < 0) return;
    app.shift_range_anchor   = -1;
    app.last_selected_marker = idx;
    viewport.invalidate_top_strip();
    viewport.invalidate_timestamp_area();
}

void Selection::clear_selection() {
    // Membership replace (to empty) -> demote a SelectionExtent region to Free.
    // A region with no live owner is Free (the Esc lower rung relies on this: it
    // sets the extent then clears the selection, leaving a Free region).
    demote_region_provenance(app.region);
    app.shift_range_anchor = -1;   // dissolve the shift-range anchor
    if (app.selected_markers.empty() && app.last_selected_marker == -1)
        return;   // nothing selected (already empty -> no focus flip)
    const size_t old_size = app.selected_markers.size();
    app.selected_markers.clear();
    app.last_selected_marker = -1;
    viewport.invalidate_top_strip();
    viewport.invalidate_timestamp_area();
    // Non-empty -> empty is always a focus flip here (the already-empty case
    // returned above), so the playhead column repaints — nothing at the cursor
    // (non-empty) back to the green line+triangle (empty) — even with no playhead
    // move.
    damage_playhead_if_focus_flipped(/*was_empty=*/false);
    // A 2+ -> 0 clear crosses the overlay's 2 threshold (un-suppress).
    damage_overlay_on_size2_crossing(old_size);
}

void Selection::collapse_to_focused() {
    // The SINGLETON fine-tuning ops (inherit toggle, the singleton tempo step)
    // collapse the selection to the focused marker so the operation and the
    // resulting selection target last_selected only. The position NUDGES and the
    // GROUP tempo gestures do NOT collapse — they went group (architect
    // 2026-07-23), operating over the whole selection rigidly. last_selected_marker
    // is untouched — it stays the focus. Callers that full-invalidate afterward
    // make the top-strip / timestamp damage here redundant (a benign damage-union,
    // accepted).
    // Membership replace (collapse to the focused singleton) -> demote a
    // SelectionExtent region to Free (a 1-marker extent is degenerate).
    demote_region_provenance(app.region);
    app.shift_range_anchor = -1;   // dissolve the shift-range anchor
    if (app.last_selected_marker < 0) return;   // nothing focused
    const size_t old_size = app.selected_markers.size();
    if (app.selected_markers.size() == 1 &&
        app.selected_markers.count(app.last_selected_marker))
        return;   // already exactly the focused singleton
    app.selected_markers.clear();
    app.selected_markers.insert(app.last_selected_marker);
    viewport.invalidate_top_strip();
    viewport.invalidate_timestamp_area();
    // A REAL multi -> singleton collapse (old size >= 2; the early-returns above
    // don't reach here) crosses BOTH the phase-reset overlay's 2-suppression
    // threshold (2+ -> visible) and the selected-stem's singleton gate (now a
    // singleton is selected), so both WAVEFORM overlays flip. The fine-tuning
    // callers can collapse then REFUSE (a wall/no-change nudge, a bracket-edge
    // tempo step) and full-invalidate NOTHING, so this owns the damage rather
    // than relying on the caller — full waveform, once. Broader than
    // damage_overlay_on_size2_crossing's P+target gate ON PURPOSE: the phase-reset
    // overlay is P+target-only but the selected stem is view-independent (both
    // columns, W and P), so the flip must repaint in every view.
    if (old_size >= 2) viewport.invalidate_waveform_area();
}

bool Selection::toggle_selection_membership(int idx) {
    // Membership replace -> demote a SelectionExtent region to Free. Harmless
    // when a Direction-B ctrl-toggle re-owns right after (demote-then-re-own).
    demote_region_provenance(app.region);
    const bool was_empty = app.selected_markers.empty();
    const size_t old_size = app.selected_markers.size();
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
    damage_overlay_on_size2_crossing(old_size);
    return added;
}

void Selection::select_range_from_anchor(int idx) {
    // File-manager inclusive range select (architect 2026-07-23). This is the
    // ONE mutator that keeps/sets app.shift_range_anchor; every OTHER Selection
    // method clears it (see the field's lifecycle comment). The caller lands the
    // playhead on *selected_markers.begin() (the EARLIEST selected marker, the
    // R1 reversal) after this returns, so idx < 0 (never reached from the
    // shift-click path, which resolves a real hit) is a plain no-op guard.
    if (idx < 0) return;
    // Membership replace -> demote a SelectionExtent region to Free (the
    // Direction-B shift-range re-owns right after via set_region_to_selection_
    // extent, so this is demote-then-re-own).
    demote_region_provenance(app.region);
    const bool was_empty = app.selected_markers.empty();
    const size_t old_size = app.selected_markers.size();

    // The active column's store size — the same phase-reset/warp selector
    // cycle_selection uses.
    const int n = (app.active_markers_view == 'P')
        ? static_cast<int>(app.phaseresetmarkers.markers().size())
        : static_cast<int>(app.warpmarkers.markers().size());

    int anchor = app.shift_range_anchor;
    if (anchor < 0 || anchor >= n) {
        // No live shift-held anchor: ADOPT THE FOCUS (architect labwc round 2,
        // 2026-07-23). The file-manager convention ranges a shift-click from
        // the CURRENT focus — plain-click A then shift+click B selects A..B,
        // and a shift interaction re-started after a shift release ranges from
        // the previous click's focus — so the anchor seed is the focused
        // marker whenever one exists, not only a prior shift-click. This also
        // self-heals any platform-side anchor loss: the focus was set by the
        // first click regardless, so the range re-derives from it. (The bounds
        // check stays belt-and-braces for a store shrink.)
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
        damage_overlay_on_size2_crossing(old_size);
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
    damage_overlay_on_size2_crossing(old_size);
}

void Selection::sanitize_selection_after_restore(int n) {
    // A restore replaces the selection membership from the entry -> demote a
    // SelectionExtent region to Free (undo/redo regions are display scratch that
    // must not silently retarget to the restored selection's extent).
    demote_region_provenance(app.region);
    // A restore (undo/redo) dissolves the shift-range anchor — this is the
    // route that closes the shift-held hole for Ctrl+Shift+Z: redo holds
    // shift, so no shift falling edge fires (the platform falling-edge hook
    // owns the release half of the anchor's lifetime), and this restore-path
    // clear is what dissolves the anchor instead.
    app.shift_range_anchor = -1;
    std::set<int> cleaned;
    for (int idx : app.selected_markers) {
        if (idx >= 0 && idx < n) cleaned.insert(idx);
    }
    app.selected_markers = std::move(cleaned);
    if (!app.selected_markers.count(app.last_selected_marker)) {
        app.last_selected_marker = -1;
    }
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

void Selection::prune_live_selection() {
    // Pruning out-of-range members replaces the membership -> demote a
    // SelectionExtent region to Free.
    demote_region_provenance(app.region);
    app.shift_range_anchor = -1;   // dissolve the shift-range anchor
    const int n = (app.active_markers_view == 'P')
        ? static_cast<int>(app.phaseresetmarkers.markers().size())
        : static_cast<int>(app.warpmarkers.markers().size());
    for (auto it = app.selected_markers.begin();
         it != app.selected_markers.end();) {
        if (*it < 0 || *it >= n) {
            it = app.selected_markers.erase(it);
        } else {
            ++it;
        }
    }
    if (app.last_selected_marker < 0 ||
        app.last_selected_marker >= n ||
        !app.selected_markers.count(app.last_selected_marker)) {
        app.last_selected_marker =
            app.selected_markers.empty()
                ? -1
                : *app.selected_markers.rbegin();
    }
}
