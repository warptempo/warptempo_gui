#include "undo.h"

#include "input_handler.h"        // land_playhead_on_marker,
                                  // set_region_to_selection_extent,
                                  // clear_region_highlight,
                                  // frame_span_into_view — the restore visual tail
#include "target_render.h"
#include "warp_frame_map_view.h"  // source_frame_to_active_domain, for the
                                  // singleton offscreen recenter

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

void Undo::recompute_dirty() {
    const auto& h = app.history;
    if (!h.saved_valid) {
        app.warp_dirty        = true;
        app.phase_reset_dirty = true;
        app.settings_dirty    = true;
    } else if (h.saved_distance == 0) {
        app.warp_dirty        = false;
        app.phase_reset_dirty = false;
        app.settings_dirty    = false;
    } else if (h.saved_distance < 0) {
        // Saved is `n` undos behind the current cursor. The last n
        // entries of undo_stack moved us from saved baseline to current.
        app.warp_dirty        = false;
        app.phase_reset_dirty = false;
        app.settings_dirty    = false;
        const int n  = -h.saved_distance;
        const int us = static_cast<int>(h.undo_stack.size());
        for (int i = std::max(0, us - n); i < us; ++i) {
            if (!h.undo_stack[i].affects_persistence) continue;
            const char m = h.undo_stack[i].op_mode;
            if      (m == 'P') app.phase_reset_dirty = true;
            else if (m == 'S') app.settings_dirty    = true;
            else               app.warp_dirty        = true;
        }
    } else {
        // Saved is `n` redos ahead. The top n entries of redo_stack
        // would, if redone, take us back to the saved state.
        app.warp_dirty        = false;
        app.phase_reset_dirty = false;
        app.settings_dirty    = false;
        const int n  = h.saved_distance;
        const int rs = static_cast<int>(h.redo_stack.size());
        for (int i = std::max(0, rs - n); i < rs; ++i) {
            if (!h.redo_stack[i].affects_persistence) continue;
            const char m = h.redo_stack[i].op_mode;
            if      (m == 'P') app.phase_reset_dirty = true;
            else if (m == 'S') app.settings_dirty    = true;
            else               app.warp_dirty        = true;
        }
    }
    app.dirty = app.warp_dirty || app.phase_reset_dirty || app.settings_dirty;
}

void Undo::push_undo_warp(std::vector<GuiWarpMarker> pre_state,
                          bool affects_persistence,
                          std::vector<int> touched_snapshot,
                          std::vector<int> touched_live) {
    UndoEntry e;
    e.snapshot           = std::move(pre_state);
    e.phase_reset_snapshot = app.phaseresetmarkers.markers();
    e.settings           = capture_current_settings(app);
    e.op_mode            = 'W';
    e.tab                = app.active_tab_view;
    e.affects_persistence = affects_persistence;
    e.touched_snapshot   = std::move(touched_snapshot);
    e.touched_live       = std::move(touched_live);
    app.history.push(std::move(e));
    viewport.clear_hover_popup();
}

void Undo::push_undo_phase_reset(std::vector<GuiPhaseResetMarker> pre_state,
                               std::vector<int> touched_snapshot,
                               std::vector<int> touched_live) {
    UndoEntry e;
    e.snapshot           = app.warpmarkers.markers();
    e.phase_reset_snapshot = std::move(pre_state);
    e.settings           = capture_current_settings(app);
    e.op_mode            = 'P';
    e.tab                = app.active_tab_view;
    e.touched_snapshot   = std::move(touched_snapshot);
    e.touched_live       = std::move(touched_live);
    app.history.push(std::move(e));
    viewport.clear_hover_popup();
}

void Undo::push_undo_both(std::vector<GuiWarpMarker> warp_pre,
                          std::vector<GuiPhaseResetMarker> phase_reset_pre,
                          char op_mode, char tab_override) {
    UndoEntry e;
    e.snapshot           = std::move(warp_pre);
    e.phase_reset_snapshot = std::move(phase_reset_pre);
    e.settings           = capture_current_settings(app);
    e.op_mode            = op_mode;
    e.tab                = tab_override ? tab_override : app.active_tab_view;
    app.history.push(std::move(e));
    viewport.clear_hover_popup();
}

void Undo::push_settings_undo(SettingsSnapshot pre_state) {
    UndoEntry e;
    e.snapshot           = app.warpmarkers.markers();
    e.phase_reset_snapshot = app.phaseresetmarkers.markers();
    e.settings           = std::move(pre_state);
    e.op_mode            = 'S';
    e.tab                = app.active_tab_view;
    app.history.push(std::move(e));
    viewport.clear_hover_popup();
    recompute_dirty();
}

namespace {
uint64_t gesture_steady_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

bool Undo::coalesce_gesture(GestureKind kind) const {
    const GestureCoalesce& c = gesture_coalesce;
    // Command adjacency is the whole correctness story. app.command_seq is
    // bumped once per discrete user command at the three dispatch entry points,
    // so this eligible press's own command is exactly c.command_seq + 1 iff NO
    // other command ran since the previous eligible commit. Any intervening
    // command — a click, Tab, paste, save, undo/redo, tab/column switch, or an
    // unhandled key — advances the counter an
    // extra step and breaks the burst, which subsumes "same selection / same
    // tab / same history": none of those can change without a command in
    // between. `kind` still separates nudge from tempo-step even when adjacent;
    // the window still splits a rapid burst from two adjacent-but-slow commands;
    // the non-empty-stack guard covers a stack cleared by a load/reset (which
    // does not advance command_seq). Adjacency being the CORRECTNESS story does
    // not make the window a free parameter: a HELD key is command-adjacent
    // throughout, so the window's length alone decides whether its first repeat
    // joins the burst or splits an entry off it — which is why kGestureCoalesceMs
    // is pinned to the compositor's key-repeat delay at its declaration.
    return c.kind == kind
        && (gesture_steady_ms() - c.last_ms) <= kGestureCoalesceMs
        && app.command_seq == c.command_seq + 1
        && !app.history.undo_stack.empty();
}

void Undo::record_gesture(GestureKind kind) {
    gesture_coalesce = GestureCoalesce{
        kind,
        gesture_steady_ms(),
        app.command_seq,          // this eligible press's command
    };
}

void Undo::note_coalesced_commit() {
    // Mirror the side effects of the push_undo_* helpers, minus the history
    // push the merge deliberately suppresses: the hover popup clears
    // per-press.
    viewport.clear_hover_popup();
}

void Undo::refresh_coalesced_touched_live(std::vector<int> touched_live) {
    // Single-writer: the burst's entry is the top of the undo stack (command
    // adjacency admits no intervening push). Overwrite only touched_live; the
    // first-press touched_snapshot stays the restore-produces coordinates.
    if (app.history.undo_stack.empty()) return;
    app.history.undo_stack.back().touched_live = std::move(touched_live);
}

namespace {

// Shared post-restore SELECTION rule for both marker lists. After a marker
// swap, classify before -> after as add / remove / same-count and set the
// selection to the touched markers. `fields_differ` is the ROW EQUALITY basis:
// !fields_differ(a, b) means the two rows are identical, which the same-count
// branch uses for identity matching. All three branches consume by exact
// multiset matching — no epsilon, no double widening, no re-rounding — and
// multiplicity-aware, each before-row matching at most one after-row: add /
// remove match on the whole-int64-source-frame time (`time_frame`), same-count
// matches on the FULL row (every field, via !fields_differ). So a crossing drag
// that reorders the store still flags only the changed row, and when one of two
// exactly coincident markers is touched, the tie's moved member is still
// identified. This resolves the touched set, writes it as the selection, and
// picks the EARLIEST touched marker as focus (equal members; the group nudge's
// playhead follow, the tempo step's re-land, Tab's start, and the lane/readout
// fallbacks all tolerate it, and a singleton's earliest IS the touched marker).
// The VISUAL tail — the playhead land (on the FOCUS in both arms, which is the
// touched marker for a singleton and the earliest touched member for a group;
// the universal land-on-the-focus rule at land_playhead_on_marker), the group
// SelectionExtent
// region, and the offscreen framing/recenter — lives in restore_history_entry
// AFTER sanitize (the region write must follow sanitize's membership clear).
template <class M, class FieldsDiffer>
void apply_post_restore_rules_impl(AppState& app,
                                   Selection& selection,
                                   const UndoEntry& entry,
                                   const std::vector<M>& before,
                                   const std::vector<M>& after,
                                   FieldsDiffer  fields_differ) {
    std::set<int> target_set;

    // Explicit identity hints first (reposition drags): entry.touched_snapshot
    // names the touched markers directly in THIS entry's snapshot coordinates,
    // which are exactly `after` (the state a restore of this entry produced). Use
    // them verbatim, bounds-filtered against `after` defensively; only when they
    // are absent (every hint-less producer) or filter empty (defensive) does the
    // diff reconstruction below run. The hints exist because that diff matcher
    // cannot tell a moved row from an untouched one when a translated group or a
    // column-snapped drag lands field-identical rows at another's position.
    if (!entry.touched_snapshot.empty()) {
        for (int idx : entry.touched_snapshot) {
            if (idx >= 0 && idx < static_cast<int>(after.size()))
                target_set.insert(idx);
        }
    }

    if (!target_set.empty()) {
        // Hints resolved the touched set — skip the diff reconstruction entirely.
    } else if (after.size() > before.size()) {
        std::multiset<int64_t> before_frames;
        for (const auto& m : before) before_frames.insert(m.time_frame);
        for (size_t i = 0; i < after.size(); ++i) {
            auto it = before_frames.find(after[i].time_frame);
            if (it != before_frames.end()) {
                before_frames.erase(it);  // consume: one match per row
            } else {
                target_set.insert(static_cast<int>(i));
            }
        }
    } else if (after.size() < before.size()) {
        // A removal leaves no touched row to select — clear the selection.
        // The empty post-sanitize selection then takes the visual tail's
        // size == 0 arm in restore_history_entry: no land, no region, playhead
        // and viewport still (clear_selection's damage is paint-only).
        selection.clear_selection();
        return;
    } else {  // same count: identity-based row matching
        // A crossing drag reorders the store (reorder_markers_by_time), so
        // before and after are a permutation plus one changed row: comparing
        // before[i] vs after[i] POSITIONALLY would flag every passed-over
        // marker (each sits at a shifted index and differs from its
        // counterpart). Match by identity instead, mirroring the add/remove
        // branches: an after-row is untouched iff it exactly equals some
        // not-yet-consumed before-row, each before-row consumed at most once.
        // The unmatched after-rows are the touched set.
        //
        // Plain O(n^2) consume (a used[] flag over `before`, inner scan with
        // !fields_differ) rather than a std::multiset: marker lists are small,
        // and this avoids inventing a strict ordering over the mixed field
        // tuple (label strings, doubles) a multiset key would need.
        //
        // Consequences: a pure permutation with no field change (a stable-sort
        // tie reorder) matches every row and yields an empty touched set — no
        // selection change, correct; coincident equal rows are handled by the
        // one-match-per-row consumption exactly like the add/remove branches.
        // This matcher CANNOT distinguish a moved row that lands field-identical
        // to an untouched row (a group translated by the inter-marker spacing, or
        // a column-snapped drag onto a row-identical marker) from that untouched
        // row — it would flag the wrong subset. The reposition drags therefore
        // supply explicit touched_snapshot hints (consumed above), and this
        // diff matcher is only the fallback for hint-less producers, where such
        // collisions do not arise.
        std::vector<char> used(before.size(), 0);
        for (size_t i = 0; i < after.size(); ++i) {
            bool matched = false;
            for (size_t j = 0; j < before.size(); ++j) {
                if (!used[j] && !fields_differ(after[i], before[j])) {
                    used[j] = 1;  // consume: one match per row
                    matched = true;
                    break;
                }
            }
            if (!matched) target_set.insert(static_cast<int>(i));
        }
    }

    if (target_set.empty()) return;

    // Undo/redo replaces the membership with the touched set -> CLEAR a
    // SelectionExtent region (a restored span must not silently retarget to the
    // touched set's extent, and an ownerless one must not rest either). sanitize
    // below also clears, so this is belt-and-braces for the explicit-site rule;
    // the visible outcome is decided further on by restore_history_entry's visual
    // tail, which clears ANY resting region regardless of provenance.
    // No damage call: restore_history_entry — the only route here — invalidates
    // the whole waveform area (and kicks a sync render) in its tail.
    (void)clear_region_on_membership_replace(app.region);
    app.selected_markers = target_set;
    // EARLIEST touched marker as focus — one rule for singleton (trivially the
    // touched marker) and group (all members are equal; there is no stored focus
    // hint). sanitize keeps it (it is in the set and in range); the visual tail
    // in restore_history_entry then lands the playhead on that focus in either
    // arm, and for a group additionally sets/frames the extent region.
    app.last_selected_marker = *target_set.begin();
}

}  // namespace

void Undo::apply_post_restore_rules_warp(const UndoEntry& entry,
                                         const std::vector<GuiWarpMarker>& before) {
    apply_post_restore_rules_impl(
        app, selection, entry, before, app.warpmarkers.markers(),
        [](const GuiWarpMarker& a, const GuiWarpMarker& b) {
            return a.time_frame     != b.time_frame
                || a.disabled       != b.disabled
                || a.tempo_inherits != b.tempo_inherits
                || a.tempo_cents    != b.tempo_cents
                || a.tempo_scale    != b.tempo_scale
                || a.label_def      != b.label_def
                || a.label_ref      != b.label_ref
                // Session-only iter/bpm fields ride undo snapshots too, and row
                // identity means the WHOLE struct: an iteration-bracket-only or
                // bpm-only undo mutates only these, so omitting them would leave
                // the same-count matcher finding no touched row and stranding
                // the selection. Every GuiWarpMarker field beyond the serialized
                // seven above.
                || a.iter_start_cents != b.iter_start_cents
                || a.iter_end_cents   != b.iter_end_cents
                || a.bpm_owner        != b.bpm_owner
                || a.bpm_beats        != b.bpm_beats
                || a.bpm_lo           != b.bpm_lo
                || a.bpm_hi           != b.bpm_hi
                || a.bpm_endpoint     != b.bpm_endpoint;
        });
}

void Undo::apply_post_restore_rules_phase_reset(
        const UndoEntry& entry,
        const std::vector<GuiPhaseResetMarker>& before) {
    apply_post_restore_rules_impl(
        app, selection, entry, before,
        app.phaseresetmarkers.markers(),
        [](const GuiPhaseResetMarker& a, const GuiPhaseResetMarker& b) {
            return a.time_frame != b.time_frame
                || a.disabled   != b.disabled;
        });
}

// True when do_undo / do_redo would actually act — the authoritative guard for
// both, run on the source stack. Two ways a step is a silent no-op:
//   - empty source stack;
//   - the top entry's TARGET tab is currently read-only. When the ACTIVE tab
//     is read-only Ctrl+Z / Ctrl+Shift+Z is already dropped at the keyboard gate
//     (read_only_key_blocked); this catches the remaining cross-tab path — the
//     active tab writable but the top entry targeting the OTHER, locked tab.
//     Read-only is a reversible per-tab toggle, so honored-ness is decided by
//     the target tab's state now, not when the action was recorded.
// Each bail leaves the entry on the stack and the view unchanged, so unlocking
// the tab makes the history reachable again with nothing lost.
bool Undo::history_entry_actionable(const std::vector<UndoEntry>& stack) const {
    if (stack.empty()) return false;
    const char tt = stack.back().tab;
    const bool target_ro = (tt == 'B') ? app.tab_b.read_only
                                        : app.tab_a.read_only;
    return !target_ro;
}

// Direction-parameterized restore core shared by do_undo / do_redo, making the
// two symmetric by construction. Pops the top entry of `from`, records the
// live-state counter-entry onto `to`, and applies the common restore body;
// saved_distance moves by `saved_distance_delta` (+1 undo, −1 redo). The caller
// has already run history_entry_actionable on `from`.
void Undo::restore_history_entry(std::vector<UndoEntry>& from,
                                 std::vector<UndoEntry>& to,
                                 int saved_distance_delta) {
    playback_lifecycle.stop_playback_if_playing();
    viewport.clear_hover_popup();
    UndoEntry entry = std::move(from.back());
    from.pop_back();

    // Counter-entry captured from live state so the opposite direction can
    // reverse this restore. Same carry-everywhere field list the push_undo_*
    // helpers use, so marker and settings entries round-trip identically.
    UndoEntry counter;
    counter.snapshot            = app.warpmarkers.markers();
    counter.phase_reset_snapshot = app.phaseresetmarkers.markers();
    counter.settings            = capture_current_settings(app);
    counter.op_mode             = entry.op_mode;
    counter.tab                 = entry.tab;
    counter.affects_persistence = entry.affects_persistence;
    // The touched-set identity hints SWAP coordinate spaces on the counter: the
    // counter's snapshot is the op's after-state, so the rows touched by a
    // restore of the counter (= redoing this op) are entry.touched_live, and the
    // rows live when the counter was pushed (this entry's snapshot state) are
    // entry.touched_snapshot. Empty stays empty (hint-less producers).
    counter.touched_snapshot    = entry.touched_live;
    counter.touched_live        = entry.touched_snapshot;
    std::vector<GuiWarpMarker>       before_w = counter.snapshot;
    std::vector<GuiPhaseResetMarker> before_t = counter.phase_reset_snapshot;

    to.push_back(std::move(counter));
    // No kCap trim here: each restore moves one entry between the stacks (`from`
    // popped above, `to` pushed here), and push — the only operation that grows
    // the total — clears the redo stack and caps the undo stack. So
    // undo_stack.size() + redo_stack.size() never exceeds kCap and the
    // destination cannot overflow.
    if (app.history.saved_valid) app.history.saved_distance += saved_distance_delta;

    // Restore the originating A/B tab before the marker swap. The swap writes
    // the live marker store and the post-restore rules write the tab-bound
    // selection, so both must land on the tab the action was authored on.
    if (entry.tab != app.active_tab_view) {
        active_views.switch_active_tab_view_to(entry.tab);
    }

    // Restore engine settings before the marker swap. Marker entries get their
    // settings field populated from app at push time (carry-everywhere), so the
    // restore is a no-op for marker-only ops. Settings-only entries get the
    // actual pre-edit settings restored here.
    app.engine_settings    = std::move(entry.settings.engine_settings);

    app.warpmarkers.markers_mut()    = std::move(entry.snapshot);
    app.phaseresetmarkers.markers_mut() = std::move(entry.phase_reset_snapshot);

    // Switch active mode to match the op being restored before applying
    // post-restore rules — selection state is mode-bound, so the rules
    // and the sanitize step must run against the correct list. Skip
    // entirely for settings-only entries: they don't carry an authoring
    // mode, and active_markers_view is a view-state key that's not undoable.
    //
    // Kept inline rather than delegated to
    // GuiActiveViews::switch_active_markers_view_to: that helper additionally
    // runs selection.prune_live_selection(), whose last_selected repair
    // (re-anchor to the max surviving index) differs from
    // sanitize_selection_after_restore's (set to -1) when the restored slot's
    // last_selected falls outside the clamped set while the set stays
    // non-empty. In the same-count / no-field-change branch the post-restore
    // rules leave the selection untouched, so that repair difference would be
    // observable — the swap must stay pre-sanitize-only here.
    if (entry.op_mode != 'S' && entry.op_mode != app.active_markers_view) {
        // Stash the current selection into the leaving mode's slot,
        // then restore the destination mode's slot. Replacing the live membership
        // CLEARS a SelectionExtent region (the W/P inline swap; sanitize below
        // also clears, belt-and-braces, and the visual tail then clears any
        // provenance outright — this entry runs only for non-'S' entries, exactly
        // the tail's own gate). Damage rides this function's own
        // unconditional full-waveform invalidate in the tail.
        (void)clear_region_on_membership_replace(app.region);
        ViewState& curtab = (app.active_tab_view == 'B') ? app.tab_b : app.tab_a;
        if (app.active_markers_view == 'P') {
            curtab.phase_reset_selected      = app.selected_markers;
            curtab.phase_reset_last_selected = app.last_selected_marker;
            app.selected_markers           = curtab.warp_selected;
            app.last_selected_marker       = curtab.warp_last_selected;
        } else {
            curtab.warp_selected           = app.selected_markers;
            curtab.warp_last_selected      = app.last_selected_marker;
            app.selected_markers           = curtab.phase_reset_selected;
            app.last_selected_marker       = curtab.phase_reset_last_selected;
        }
        app.active_markers_view = entry.op_mode;
    }

    // Settings-only entries carry no marker or focus post-restore work.
    if (entry.op_mode == 'P') {
        apply_post_restore_rules_phase_reset(entry, before_t);
        selection.sanitize_selection_after_restore(
            static_cast<int>(app.phaseresetmarkers.markers().size()));
    } else if (entry.op_mode != 'S') {
        apply_post_restore_rules_warp(entry, before_w);
        selection.sanitize_selection_after_restore(
            static_cast<int>(app.warpmarkers.markers().size()));
    }

    // VISUAL TAIL (architect 2026-07-25 — undo/redo adopts the group visual
    // language, superseding "undo/redo shows its target WITHOUT the playhead"):
    // a SINGLETON restore LANDS the playhead on its touched marker (which is its
    // focus; the land is a PURE playhead write, the tail's own clear below
    // having already taken any resting span) and its
    // always-on focus stem follows from the selection (no stamp); a GROUP
    // restore re-selects the touched set (done above), LANDS the playhead on its
    // FOCUS — the EARLIEST touched member, by the focus rule above — AND sets the
    // SelectionExtent REGION
    // — undo/redo joins the extent-region writers, then, when any member is
    // offscreen, PREFERS a plain scroll and only ZOOMS OUT if the group cannot fit
    // at the current level (the group arm below). The extent's split half-triangles
    // ARE the dissolved cursor playhead (its left half on the earliest member, the
    // spot the double-Esc collapse parks at), so the readout follows the land; the
    // multi-select clicks' land-then-extent order is the precedent
    // (the extent set below re-suppresses the cursor line before the frame
    // paints, so the land itself moves only the timestamp readout). Runs AFTER
    // sanitize_selection_after_restore so the region write follows sanitize's
    // membership clear (the clear-then-derive order the multi-select clicks use),
    // and BEFORE the recompute/invalidate/kick block below so restore's one sync
    // render covers the final geometry. The LAND/EXTENT block is gated off 'S' (a
    // settings-only restore never LANDS, never EXPANDS a selection, and never
    // WRITES a region; what it does do for every entry is CLEAR the region, and
    // it MAY collapse a 2+ selection to its focus as that clear's consequence —
    // see below), and
    // branches on the POST-sanitize live size, so a defensive edge takes the
    // matching arm (a group entry sanitized down to one member lands as a
    // singleton; a removal cleared to empty is the size == 0 no-op).
    //
    // THE TAIL OPENS WITH A WHOLESALE REGION CLEAR (architect 2026-07-29,
    // REVERSING the recorded boundary that made undo/redo a route where "a
    // resting region is display scratch"): a restore rewrites the world the span
    // was measured against, so any span still standing describes a world that no
    // longer exists — a TrimWindow highlight resting stale across a map-changing
    // restore is precisely the pattern break the two-forms model collapses on
    // sight. Every arm follows from that one clear: a singleton or emptied
    // restore rests with NO region, and the group arm's
    // set_region_to_selection_extent runs AFTER it (clear-then-derive), so the
    // group outcome is unchanged — that write overwrote whatever rested anyway.
    // THE CLEAR IS NOT GATED OFF 'S' (architect 2026-07-29, closing the settings
    // side of the same hole): a SETTINGS-ONLY restore rewrites engine_settings
    // and rebuilds the target map underneath a resting highlight — scale A ->
    // commit scale B -> chip-row re-sync under B -> Ctrl+Z would otherwise rest a
    // B-domain span under A — so it clears too. The REST of the 'S' gate stands
    // exactly: a settings restore still must not select and must not WRITE a
    // region, which is why only this one call sits above the gate and the whole
    // extent/framing block stays inside it. The no-LAND half was narrowed at the
    // tail (2026-07-29): a settings restore that changes the map under a
    // SURVIVING marker-lane selection re-lands on its focus in TARGET view,
    // because the image moved — see the re-land beside kick_waveform_sync. With
    // no selection the 'S' restore still lands nothing at all.
    clear_region_highlight(app, viewport);
    // The 'S' arm's ONE selection consequence (the never-rest-2+-without-a-span
    // invariant, stated at clear_region_highlight's declaration): the clear above
    // can strand a group in point form with no point, and a settings restore
    // derives no span, so a 2+ selection collapses to its FOCUS. The non-'S'
    // entries need nothing here — a group restore re-selects and re-derives its
    // own extent below, and the other arms rest at <=1 selected.
    if (entry.op_mode == 'S' && app.selected_markers.size() >= 2)
        selection.collapse_to_focused();
    if (entry.op_mode != 'S') {
        const size_t sel_size = app.selected_markers.size();
        if (sel_size == 1) {
            const int t = *app.selected_markers.begin();
            // Resolve the touched marker's source frame with ONE bounds check up
            // front — an out-of-range t skips the WHOLE singleton visual (land +
            // recenter) rather than half-applying it (a bad t would else
            // land nothing but recenter on the src_f=0 default). Defensive only:
            // post-sanitize the selection indices are always in range, so this
            // guards an impossible state, never a reachable one.
            int64_t src_f   = 0;
            bool    in_range = false;
            if (app.active_markers_view == 'P') {
                const auto& pv = app.phaseresetmarkers.markers();
                in_range = (t >= 0 && t < static_cast<int>(pv.size()));
                if (in_range) src_f = pv[t].time_frame;
            } else {
                const auto& wv = app.warpmarkers.markers();
                in_range = (t >= 0 && t < static_cast<int>(wv.size()));
                if (in_range) src_f = wv[t].time_frame;
            }
            if (in_range) {
                // LAND: two-step placement basis, direct cursor write, NO viewport
                // move — and NO region side effect (the land is a pure playhead
                // write; the point commands that want a collapse call
                // clear_region_highlight themselves, and this restore called it
                // once at the top of the tail for every arm). Playback is already
                // stopped above, so land's scanner-inactive premise holds.
                land_playhead_on_marker(app, viewport.audio, viewport, t);
                // OFFSCREEN -> plain recenter at the CURRENT zoom (no framer, no
                // zoom change): center on the touched marker's active-domain image
                // and re-snap/clamp through the one chokepoint only when it is
                // outside the visible span.
                const int64_t domain_frame =
                    source_frame_to_active_domain(app, viewport.audio, src_f);
                const int64_t visible = samples_visible(app, viewport.audio);
                const int64_t start   = app.viewport_start_sample;
                if (domain_frame < start || domain_frame >= start + visible) {
                    app.viewport_start_sample = domain_frame - visible / 2;
                    clamp_viewport_start(app, viewport.audio);
                }
                // The restored singleton's always-on focus stem follows
                // automatically from the selection — sanitize's subject-change
                // owner plus the full-waveform invalidate below repaint it on
                // the touched marker. No pin stamp is needed (the whole
                // conditional-stem apparatus was harvested).
            }
        } else if (sel_size >= 2) {
            // GROUP: LAND the playhead on the restore's FOCUS, THEN set the
            // SelectionExtent region (architect 2026-07-25). This obeys the
            // universal land-on-the-focus rule with no special case, because a
            // restore's focus IS the earliest touched member by construction
            // (apply_post_restore_rules_impl) — spelled as
            // *selected_markers.begin() rather than last_selected_marker so a
            // sanitize that pruned the focus still lands somewhere live. The
            // extent's split
            // half-triangles ARE the dissolved cursor playhead — its left half sits
            // ON the earliest member, the same spot the double-Esc collapse parks at
            // (min(a,b)) — so the playhead is "technically" there already and the
            // bottom-strip readout should say so; landing here makes the timestamp,
            // the Esc-collapse park, and Space's left-bound launch agree by
            // construction. LAND-THEN-EXTENT mirrors the multi-select clicks' order:
            // the tail's own clear above took whatever rested (any provenance),
            // and set_region_to_selection_extent then writes the fresh one — the
            // clear-then-derive order, the land itself touching no region.
            // Earliest =
            // *selected_markers.begin() (sorted set, smallest index = earliest in
            // time). land_playhead_on_marker is internally bounds-guarded (an
            // impossible out-of-range index no-ops the land and leaves the region
            // write to run) — the singleton branch's defensive in-range intent. The
            // land writes NO viewport, so the three-way offscreen arm below is
            // byte-identical; playback is already stopped above (land's
            // scanner-inactive premise). The extent write below re-suppresses the
            // cursor playhead before the frame paints, so the land's own visible
            // delta is the timestamp readout.
            land_playhead_on_marker(app, viewport.audio, viewport,
                                    *app.selected_markers.begin());
            // Set the SelectionExtent region (the one owner clamps endpoints
            // playable, sets provenance, and damages the waveform). A degenerate
            // all-coincident touched set yields an ACTIVE zero-width region (a == b,
            // the split-playhead's single-mask case), not a clear — matching the
            // multi-select clicks' land-then-extent bit-for-bit.
            set_region_to_selection_extent(app, viewport.audio, viewport);
            // OFFSCREEN handling (architect 2026-07-25 post-labwc; codex round:
            // column-decided): PREFER a plain scroll at the current zoom, ZOOM only
            // when the group cannot fit. The fit contract is PAINTED COLUMNS, not a
            // sample span — an endpoint's flag paints at its CENTER column and the
            // painter does NOT edge-clamp that center, so the capacity is the
            // pixel range [0, W), NOT q*W samples (which overcounts by up to a
            // column) and NOT the grid-snapped start (clamp_viewport_start moves it
            // ~half a pixel). Both tests decide on the endpoints' columns under the
            // painter's own basis (painter_samples_per_pixel + the flag painters'
            // nearbyint((frame - vp_start)/q) — the region endpoints already live in
            // the active display domain, so no warp map is walked, matching
            // region_columns). Three arms:
            //   - fully visible (both endpoint columns in [0, W) under the CURRENT
            //     start) -> no viewport write;
            //   - otherwise TENTATIVELY center at the current zoom (the singleton
            //     recenter's group sibling: viewport_start = extent midpoint -
            //     visible/2, then clamp_viewport_start) and re-test the columns
            //     under the clamped start: both in [0, W) -> the SCROLL stands (no
            //     zoom change, no margin);
            //   - else -> frame_span_into_view with margin (the cannot-fit
            //     fallback; the framer only ever zooms OUT to fit — fit level +
            //     2.5%-per-side, centered, clamped [kMinZoom, effective ceiling], NO
            //     playhead recenter). It OVERWRITES the tentative viewport wholesale
            //     (level + start via apply_zoom_to_start), so the tentative write
            //     needs no revert. Its apply_zoom_to_start no-op guard normally
            //     cannot leave the failing tentative state standing, because a fit
            //     that failed at the current level forces the framer to a DIFFERENT
            //     (more zoomed-out) level to seat the MARGIN-widened span — the
            //     level differs, so the guard does not short-circuit. THE ONE
            //     EXCEPTION (accepted, architect 2026-07-25 (ratified after
            //     talk-through)) is the
            //     CONJUNCTION the two code paths already embody: (a) an endpoint's
            //     painted column still fails the [0, W) test after the ceiling /
            //     start-0 clamp — which happens for ANY hi landing in the final
            //     half-pixel interval at the ceiling q, NOT only total-1 (e.g.
            //     W=1920, total=4,410,000, q=2296.875: hi = total-1000 rounds to
            //     column W without ending at EOF) — AND (b) the margined fit request
            //     clamps back to that SAME ceiling, so apply_zoom_to_start no-ops and
            //     the ceiling rest at start 0 stands. Both are required: a NARROW
            //     EOF-ending group fails (a) but not (b) — e.g. extent
            //     [4,000,000, total-1] ends at EOF yet its 5%-widened span frames to
            //     a DEEPER level, exercising no no-op — while the (a)-failing wide
            //     case no-ops because its margined span is already at least
            //     song-wide. When the conjunction holds the endpoint rests AT or
            //     PAST the effective waveform's right edge: half-culled, or (at a
            //     non-multiple-of-16 window width) sitting in the 0-15px inert right
            //     gutter, where the flag (its scaled width, flag_lane_w_px();
            //     15px at the default font size) can even show WHOLE just
            //     outside the effective span — flag centers use the effective
            //     W (floored to a multiple of 16) while the flag surface
            //     spans the full strip. At
            //     the ruled deployment widths (1920 / 2560 / 3840, all multiples of
            //     16) the gutter is empty and it half-culls. Either way NO route
            //     places the endpoint INSIDE the effective span at whole-song-
            //     visible — the standing flags-may-hang-half-offscreen geometry (cull
            //     only when FULLY out), the SAME cull the level-preserving
            //     navigation routes show there (Tab, which keeps the level; the
            //     marker-click land, which writes no viewport; and the zoom-row
            //     double-click framer itself, no-op under this conjunction) — not a
            //     framing defect, and identical
            //     under every option reachable within the whole-song-ceiling and
            //     centered-flag rulings. The futile framer call is left as-is (a
            //     harmless no-op there); a ceiling special-case would be a branch for
            //     ZERO behavioral difference. This whole arm diverges from the
            //     zoom-row DOUBLE-CLICK's unconditional zoom-to-span; the framer
            //     itself is untouched, an undo-tail rule only.
            // ACCEPTED COST on the framer arm: apply_zoom_to_start runs one sync
            // render and the unconditional kick_waveform_sync below runs a second
            // over identical final state — a bounded duplicate on a discrete
            // keystroke (the keyboard zoom's per-press cost).
            if (app.region.active) {
                const int64_t lo      = app.region.a_frame;
                const int64_t hi      = app.region.b_frame;
                const int     W       = waveform_area(app).w;
                const double  q       = painter_samples_per_pixel(
                    app, viewport.audio, waveform_area(app));
                // Endpoint column under a given viewport start, on the flag
                // painters' basis; empty q (no geometry) leaves the region put.
                auto both_columns_visible = [&](int64_t vp_start) {
                    const int lo_col = static_cast<int>(std::nearbyint(
                        (static_cast<double>(lo) - static_cast<double>(vp_start)) / q));
                    const int hi_col = static_cast<int>(std::nearbyint(
                        (static_cast<double>(hi) - static_cast<double>(vp_start)) / q));
                    return lo_col >= 0 && lo_col < W && hi_col >= 0 && hi_col < W;
                };
                if (q > 0.0 && W > 0 &&
                    !both_columns_visible(app.viewport_start_sample)) {
                    // Tentatively center at the current zoom and clamp.
                    const int64_t visible = samples_visible(app, viewport.audio);
                    app.viewport_start_sample = (lo + hi) / 2 - visible / 2;
                    clamp_viewport_start(app, viewport.audio);
                    if (!both_columns_visible(app.viewport_start_sample)) {
                        // Cannot fit at this level even centered -> zoom out to fit
                        // (overwrites the tentative viewport wholesale).
                        frame_span_into_view(app, viewport.audio, viewport,
                                             lo, hi, /*margin=*/true);
                    }
                }
            }
        }
        // sel_size == 0: nothing — the removal branch cleared, viewport/playhead
        // stay put.
    }

    recompute_dirty();
    viewport.invalidate_waveform_area();
    // One-shot discrete jump: undo/redo restored markers / phase resets /
    // settings, changing the displayed plate (the target-view warp_frame_map).
    // The visual tail above may have LANDED the playhead (on the restored focus
    // in either arm), recentered
    // or framed the viewport, and written a region; these invalidations and the
    // sync kick cover all of that as well as the marker change. Render it
    // synchronously so the restored markers and the waveform land together. A
    // single keystroke, so bounded — the drag-time async-warp_frame_map policy is about
    // the marker-drag torrent, not discrete events. kick_waveform_sync's damage
    // duplicates invalidate_waveform_area above (harmless); the (now plain)
    // trigger below owns target-buffer freshness when target view is available.
    viewport.kick_waveform_sync();
    // THE SETTINGS-ONLY ARM'S MAP-CHANGE RE-LAND, TARGET VIEW ONLY. An 'S'
    // restore rewrites engine_settings, which re-warps every marker's target
    // IMAGE, and the arm above deliberately does not land — correct for the
    // CURSOR-only case, wrong the moment a selection survives it: the lane owns
    // the playhead (land_playhead_on_marker's doctrine, input_pointer.cpp), so
    // the focused flag would claim a playhead the cursor no longer sits under.
    // The label-coupling re-land the singleton tempo step pays after its own
    // kick is the precedent, and the ordering is its ordering — AFTER
    // kick_waveform_sync, so the conversion reads the RESTORED map. Scoped to
    // 'S': every other op_mode ran the visual tail above, which already lands on
    // its restored focus in both arms. SOURCE VIEW NEEDS NOTHING, derived rather
    // than skipped — there the active domain IS the authored domain
    // (source_frame_to_active_domain is the identity), so no image moved. This
    // stays inside the 'S' gate's no-select / no-region-write half of the rule:
    // it is a pure cursor write, selecting nothing and writing no region.
    if (entry.op_mode == 'S' && app.active_audio_view == 'T' &&
        !app.selected_markers.empty() && app.last_selected_marker >= 0)
        land_playhead_on_marker(app, viewport.audio, viewport,
                                app.last_selected_marker);
    viewport.invalidate_timestamp_area();
    // Unconditional by ruling — rationale at GuiTargetRender::trigger; an
    // undo/redo restoring only normalization-inert state (e.g. a disabled-
    // marker-only restore) stops playback and re-previews through
    // dispatch_render_now's reuse rungs — cache hit, accepted. No-op in source
    // view (trigger's own gate).
    target_render.trigger();
}

void Undo::do_undo() {
    if (!history_entry_actionable(app.history.undo_stack)) return;
    restore_history_entry(app.history.undo_stack, app.history.redo_stack, +1);
}

void Undo::do_redo() {
    if (!history_entry_actionable(app.history.redo_stack)) return;
    restore_history_entry(app.history.redo_stack, app.history.undo_stack, -1);
}
