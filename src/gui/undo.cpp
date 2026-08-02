#include "undo.h"

#include "input_handler.h"        // land_playhead_on_marker,
                                  // clear_region_highlight,
                                  // frame_span_into_view — the restore visual tail
#include "platform_wayland.h"     // viewport.gui.set_title_dirty — the window
                                  // title's dirty half, pushed from
                                  // recompute_dirty's tail
#include "target_render.h"
#include "warp_frame_map_view.h"  // source_frame_to_active_domain, for the
                                  // singleton recenter and the group framing

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
    // THE DIRTY DOT LIVES IN THE WINDOW TITLE (architect 2026-08-01): this is
    // the derive-owner of app.dirty, so every mutation, save and undo/redo
    // transition passes through here, and pushing the flag from this one tail
    // is what keeps the titlebar honest without a scattered set of inline
    // title strings. The setter is a no-op when the flag has not moved.
    // (The load's own four-flag reset is the only other transition — it pushes
    // the same way from file_loader.cpp.)
    viewport.gui.set_title_dirty(app.dirty);
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
    last_gesture_kind_ = GestureKind::None;   // see coalesce_gesture
}

void Undo::push_undo_phase_reset(std::vector<GuiPhaseResetMarker> pre_state,
                               std::vector<int> touched_snapshot,
                               std::vector<int> touched_live) {
    // No affects_persistence parameter, unlike push_undo_warp above: a
    // recorded asymmetry, not an omission — the field it would set
    // (UndoEntry::affects_persistence, app_state.h) marks an entry inert for
    // the ITERATION BRACKET, which is warp-only session state, so a
    // phase-reset entry has nothing to mark.
    UndoEntry e;
    e.snapshot           = app.warpmarkers.markers();
    e.phase_reset_snapshot = std::move(pre_state);
    e.settings           = capture_current_settings(app);
    e.op_mode            = 'P';
    e.tab                = app.active_tab_view;
    e.touched_snapshot   = std::move(touched_snapshot);
    e.touched_live       = std::move(touched_live);
    app.history.push(std::move(e));
    last_gesture_kind_ = GestureKind::None;   // see coalesce_gesture
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
    last_gesture_kind_ = GestureKind::None;   // see coalesce_gesture
}

void Undo::push_settings_undo(SettingsSnapshot pre_state) {
    UndoEntry e;
    e.snapshot           = app.warpmarkers.markers();
    e.phase_reset_snapshot = app.phaseresetmarkers.markers();
    e.settings           = std::move(pre_state);
    e.op_mode            = 'S';
    e.tab                = app.active_tab_view;
    app.history.push(std::move(e));
    last_gesture_kind_ = GestureKind::None;   // see coalesce_gesture
    recompute_dirty();
}

bool Undo::coalesce_gesture(GestureKind kind, bool synthesized_repeat) {
    // THE HYBRID VERDICT (architect 2026-08-01). Two arms over one stamp; the
    // shared precondition is that the stamp is VALID for this kind and there is an
    // entry to merge into.
    //
    // `kind` separates a nudge burst from a tempo-step burst. A repeat re-enters
    // the full dispatcher, so a hold's kind cannot change mid-burst and the test is
    // expected always-true on the repeat arm; on the tap arm it is load-bearing (a
    // nudge tap after a tempo tap must not merge). The non-empty-stack guard covers
    // a stack cleared by a load/reset.
    //
    // EVERY CHANGE OF THE UNDO-STACK TOP CLEARS THE STAMP — the four push
    // helpers (push_undo_warp / push_undo_phase_reset / push_undo_both /
    // push_settings_undo) and restore_history_entry, the shared do_undo/do_redo
    // core, one line each — so a valid stamp can never coexist with a foreign stack
    // top, and that is what lets BOTH arms assume the top of the undo stack is the
    // burst's own entry (refresh_coalesced_touched_live's precondition).
    const bool stamp_matches =
        last_gesture_kind_ == kind && !app.history.undo_stack.empty();

    bool merge = false;
    if (stamp_matches) {
        if (synthesized_repeat) {
            // ARM (1), REPEAT IDENTITY — NO CLOCK. A press the process
            // synthesized itself from a still-held key merges unconditionally,
            // because the platform's key-repeat contract already supplies the
            // adjacency property a clock would enforce numerically: layer (1) of
            // that contract (stated at GuiPlatform::maybe_fire_repeat) disarms the
            // hold at every intervening pointer-button press, key press, and
            // completed wheel emission, so a synthesized repeat STRUCTURALLY
            // CANNOT arrive after another command ran. "Same selection / same tab
            // / same history" all follow, which is why this arm needs neither the
            // window nor the subject test below. Keeping it clock-free is
            // deliberate: a hold must coalesce at ANY compositor repeat delay or
            // rate, and that independence is the whole reason repeat identity
            // replaced the retired kGestureCoalesceMs.
            merge = true;
        } else {
            // ARM (2), THE TAP WINDOW — a physical press merging into the previous
            // one. Two extra conditions, because a tap has NONE of the repeat
            // arm's structure:
            //   * WITHIN kTapCoalesceMs of the LAST ACCEPTED coalesce event (the
            //     push, or the last merge — physical or synthesized), so a run of
            //     taps extends press by press rather than racing one deadline from
            //     the first;
            //   * THE SUBJECT STILL STANDS. Nothing disarms anything between two
            //     taps: a marker click, a Tab jump, a shift-range extension or a
            //     Ctrl+Tab can all run in the gap and push NOTHING, leaving the
            //     stamp and the stack top untouched. Without this test the second
            //     tap would merge a DIFFERENT marker's nudge into the first
            //     marker's entry and then overwrite that entry's touched_live
            //     hints — one Ctrl+Z reverting two unrelated edits, which is the
            //     exact composition the arrival-invalidate was introduced to kill
            //     on the repeat side. The selection is the honest subject for all
            //     three eligible kinds (the nudges act on its focus, the tempo step
            //     on its members) and the tab is what the entry is filed under.
            // The comparison runs on the clock's OWN duration, never on a
            // whole-millisecond count: duration_cast truncates toward zero, so
            // counting first would have admitted every real interval in
            // [500ms, 501ms) as "500". Compared directly, the boundary is
            // exactly kTapCoalesceMs.
            const std::chrono::steady_clock::duration elapsed =
                std::chrono::steady_clock::now() - last_gesture_time_;
            merge = elapsed <= std::chrono::milliseconds{kTapCoalesceMs}
                 && last_gesture_tab_ == app.active_tab_view
                 && last_gesture_selection_ == app.selected_markers;
        }
    }

    // AN ELIGIBLE PHYSICAL PRESS INVALIDATES THE STAMP ON ARRIVAL (converted
    // 2026-07-29, and it survives the hybrid by moving BELOW the verdict) — this
    // query's one side effect, and the reason it is not const. Every eligible route
    // asks this question at its ENTRY, before its own refusals run, so a press that
    // goes on to REFUSE leaves the stamp INVALID; a press that COMMITS re-stamps it
    // in record_gesture, which is why clearing here costs the tap arm nothing.
    // THE DEFECT THIS CLOSES: a physical press can REFUSE without committing (a
    // phase-reset nudge at its wall, a zero-step press, an ineligible tempo step),
    // which pushes nothing and so cleared nothing; if the refusal then FLIPS
    // mid-hold — the async waveform worker publishes a new displayed map, changing
    // the painted columns the wall test reads — the first synthesized repeat commits
    // and finds the stale same-kind stamp from a DIFFERENT subject's burst, skipping
    // its own push and refreshing that older entry's touched_live. One Ctrl+Z would
    // then revert two separate holds, with snapshot and live identity hints naming
    // different markers. Clearing on stack-top changes alone could not see this: the
    // intervening acts (a pointer selection command, or the refusing press's OWN
    // focus collapse under the focus-act prologue) change no stack top.
    // THE ORDER IS THE WHOLE TRICK under the hybrid: verdict first, invalidate
    // second. Invalidating first (the pre-2026-08-01 shape) would have killed the
    // very stamp a tap needs to read, so the tap arm would never have fired.
    // ONE SITE, DELIBERATELY: the invalidate lives HERE rather than being spelled at
    // each of the eligible routes, so a route cannot forget it and no enumeration
    // has to be kept in sync — the standing "one authoritative site per concept"
    // preference. The routes are the nudges' shared prologue plus both arms of the
    // Up/Down cent step (grep this function's callers).
    if (!synthesized_repeat) last_gesture_kind_ = GestureKind::None;

    // NO ACCEPTED DELTA REMAINS on either arm. record_gesture runs AFTER the push
    // at every eligible route — FOUR routes over THREE call sites (the two position
    // nudges through their shared commit tail, plus the singleton and group arms of
    // the Up/Down cent step) — and ONLY on the accepted path, so a REFUSED press
    // never enables a later merge into an older entry, tap or repeat. Presses
    // beyond the window, or after a subject change, open their own entries.
    return merge;
}

void Undo::record_gesture(GestureKind kind) {
    // THE STAMP IS WRITTEN AS ONE UNIT, on the accepted path only (the callers put
    // this after their push / skip, past every refusal). The timestamp is what
    // makes the tap window measure from the last ACCEPTED event rather than from
    // the burst's first press; the subject is captured POST-act, so a position
    // nudge's focus collapse and its reorder remap are already folded in and the
    // next tap compares against what this press actually left standing.
    last_gesture_kind_      = kind;
    last_gesture_time_      = std::chrono::steady_clock::now();
    last_gesture_tab_       = app.active_tab_view;
    last_gesture_selection_ = app.selected_markers;
}

void Undo::refresh_coalesced_touched_live(std::vector<int> touched_live) {
    // Single-writer: the burst's entry is the top of the undo stack on BOTH
    // coalesce arms (a repeat admits no intervening push — a command would have
    // ended the hold; a tap admits none either — every stack-top change clears
    // the stamp the merge was verdicted on).
    // Overwrite only touched_live; the first-press touched_snapshot stays the
    // restore-produces coordinates.
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
// picks the EARLIEST touched marker as focus (equal members; the tempo step's
// re-land, Tab's start, and the lane/readout
// fallbacks all tolerate it, and a singleton's earliest IS the touched marker).
// THE TOUCHED SET WINS UNCONDITIONALLY, the empty case included — an empty set
// EMPTIES the selection rather than leaving the prior one standing (the derivation
// is at that branch below; it is what closes the tab-entry auto-select hole).
// The VISUAL tail — the playhead land (on the FOCUS in both arms, which is the
// touched marker for a singleton and the earliest touched member for a group;
// the universal land-on-the-focus rule at land_playhead_on_marker) and the
// offscreen framing/recenter — lives in restore_history_entry AFTER sanitize.
template <class M, class FieldsDiffer>
void apply_post_restore_rules_impl(AppState& app,
                                   Selection& selection,
                                   const UndoEntry& entry,
                                   const std::vector<M>& before,
                                   const std::vector<M>& after,
                                   FieldsDiffer  fields_differ) {
    std::set<int> target_set;

    // Explicit identity hints first (the position movers — the reposition drag and
    // the two nudges): entry.touched_snapshot
    // names the touched marker directly in THIS entry's snapshot coordinates,
    // which are exactly `after` (the state a restore of this entry produced). Use
    // them verbatim, bounds-filtered against `after` defensively; only when they
    // are absent (every hint-less producer) or filter empty (defensive) does the
    // diff reconstruction below run. The hints exist because that diff matcher
    // cannot tell a moved row from an untouched one when a column-snapped move
    // lands field-identical at another row's position.
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
        // Same body as the NOTHING-TOUCHED empty-target_set arm below (this
        // branch is reachable only when target_set is provably still empty
        // here) — kept as its own arm rather than falling through so the
        // removal case reads locally; a future edit to "touched set wins
        // unconditionally" must update both arms.
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
        // to an untouched row (a column-snapped move onto a row-identical marker)
        // from that untouched
        // row — it would flag the wrong subset. The position movers therefore
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

    // NOTHING TOUCHED => NOTHING SELECTED (converted 2026-07-29). This case must
    // NOT fall through any more, and the reason is that
    // the world changed under the old fall-through: it used to preserve "whatever
    // the user had", which was a defensible thing to keep. Since the never-parked
    // selection ruling (architect 2026-07-29)
    // the entry's TAB SWITCH (restore_history_entry runs it before the stores are
    // restored) ends in COINCIDENCE AUTO-SELECT, so what a fall-through preserves is
    // a MACHINE GUESS — the destination tab's stored cursor happening to stand on a
    // marker — and the visual tail then treats that guess as though the undo had
    // touched it: a spurious land, recenter, and an ARMED MARKER LANE after an undo
    // that changed no marker in this column. Emptying instead makes the standing
    // rule ("the restore's touched set wins over the tab-entry auto-select in every
    // reachable case") true with no exception, and it is not a SELECT — the same
    // shape the 'S' arm uses. REACHABILITY, the reachable sequence: `push_undo_both`
    // (notably the render-entry ADOPT, which records the current marker mode and the
    // dispatch tab while the entry may change only engine settings and/or the OTHER
    // column) leaves the active column's vector byte-identical, so undoing it from
    // the other tab auto-selects on arrival and the active-column diff then finds
    // nothing. The clear runs through the Selection mutator so the region, the shift
    // anchor and the subject-change damage are all handled by their owner.
    // Same body as the removal arm above (it clears for the identical reason,
    // reachably provable there rather than derived here) — cross-referenced,
    // not merged, so each site reads without following the other.
    if (target_set.empty()) {
        selection.clear_selection();
        return;
    }

    app.selected_markers = target_set;
    // EARLIEST touched marker as focus — one rule for singleton (trivially the
    // touched marker) and group (all members are equal; there is no stored focus
    // hint). sanitize keeps it (it is in the set and in range); the visual tail
    // in restore_history_entry then lands the playhead on that focus in either
    // arm, and for a group additionally frames the restored set into view.
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
//
// THE TEST ITSELF LIVES OUT AT history_step_actionable (app_state.h) and this
// delegates to it, because the toolbar's Undo / Redo buttons must GREY on
// exactly the fact these guards refuse on — one predicate, two readers, no way
// for the face and the action to disagree. The rationale above stays here,
// where the guard is run.
bool Undo::history_entry_actionable(const std::vector<UndoEntry>& stack) const {
    return history_step_actionable(app, stack);
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
    UndoEntry entry = std::move(from.back());
    from.pop_back();
    // A restore rewrites BOTH stack tops, so it invalidates the coalesce stamp for
    // the same reason a push does (see coalesce_gesture): the entry the stamp named
    // is no longer the one a later repeat would merge into. Neither do_undo nor
    // do_redo is a coalescing route, so this only ever removes a stale merge.
    last_gesture_kind_ = GestureKind::None;

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

    // BOTH columns are assigned on EVERY entry — an undo entry carries a full
    // pair, so a 'W' entry restores a byte-identical phase-reset vector and an
    // 'S' entry restores both unchanged — and the assigns are unconditional: a
    // field-only restore (a disabled toggle, a tempo, a label) moves no row but
    // must still land its values, and markers_mut's generation bump reports it
    // either way. No row-identity comparison rides these replaces any more: the
    // per-column structural bumps that stood here existed for the parked
    // selections' liveness rule, and both died 2026-07-29.
    app.warpmarkers.markers_mut()    = std::move(entry.snapshot);
    app.phaseresetmarkers.markers_mut() = std::move(entry.phase_reset_snapshot);

    // Switch active mode to match the op being restored before applying
    // post-restore rules — selection state is mode-bound, so the rules
    // and the sanitize step must run against the correct list. Skip
    // entirely for settings-only entries: they don't carry an authoring
    // mode, and active_markers_view is a view-state key that's not undoable.
    //
    // A COLUMN SWITCH CLEARS THE SELECTION (the scope rule), so the swap is the
    // clear plus the mode assignment and nothing else — no slot is stashed and
    // none is restored (the parked selections died 2026-07-29, and their restore
    // half here was already dead: the post-restore rules below write the touched
    // set wholesale). clear_selection also takes the shift-range anchor through
    // the ordinary mutator contract; the visual tail clears any resting region
    // outright above the 'S' gate. Damage rides this
    // function's own unconditional full-waveform invalidate in the tail.
    //
    // Kept inline rather than delegated to
    // GuiActiveViews::switch_active_markers_view_to only because Undo does not
    // hold that cluster; the two now agree on the whole selection story (clear,
    // then flip); the helper's one extra act was a hover-popup clear, and the
    // hover popup no longer exists (row 5).
    if (entry.op_mode != 'S' && entry.op_mode != app.active_markers_view) {
        selection.clear_selection();
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
    // having already taken any resting span) and its flag BRIGHTENS from the
    // restored selection (no stamp); a GROUP
    // restore re-selects the touched set (done above) and LANDS the playhead on
    // its FOCUS — the EARLIEST touched member, by the focus rule above — the
    // visible cursor on that member plus the members' own brightened flags
    // being the group's whole cue since the SPAN FORM retired (architect
    // 2026-07-30); then,
    // when any member is
    // offscreen, it PREFERS a plain scroll and only ZOOMS OUT if the group cannot fit
    // at the current level (the group arm below, which derives the framed span
    // from the restored members' positions). Runs AFTER
    // sanitize_selection_after_restore so the land sees the final membership,
    // and BEFORE the recompute/invalidate/kick block below so restore's one sync
    // render covers the final geometry. The LAND/FRAMING block is gated off 'S', and
    // the 'S' gate is now SIMPLE: a settings-only restore selects nothing, writes no
    // region, and lands nothing — it CLEARS both the region and the selection
    // (below), which is why the narrowing it briefly carried (a
    // target-view re-land onto a surviving focus) is gone with the surviving focus
    // itself. It is
    // branches on the POST-sanitize live size, so a defensive edge takes the
    // matching arm (a group entry sanitized down to one member lands as a
    // singleton; a removal cleared to empty is the size == 0 no-op).
    //
    // THE TAIL OPENS WITH A WHOLESALE REGION CLEAR (architect 2026-07-29,
    // REVERSING the recorded boundary that made undo/redo a route where "a
    // resting region is display scratch"): a restore rewrites the world the span
    // was measured against, so any span still standing describes a world that no
    // longer exists — a scratch span resting stale across a map-changing restore
    // would aim `x` at a window the user never drew. EVERY arm rests with NO region now, the group arm's extent write
    // having retired with the SPAN FORM (architect 2026-07-30) — a restore
    // creates no trim scratch.
    // THE CLEAR IS NOT GATED OFF 'S' (architect 2026-07-29, closing the settings
    // side of the same hole): a SETTINGS-ONLY restore rewrites engine_settings
    // and rebuilds the target map underneath a resting highlight — drag a span
    // under scale A, commit scale B, and Ctrl+Z would otherwise rest an
    // A-domain span under B — so it clears too. The REST of the 'S' gate stands
    // exactly: a settings restore still must not select and must not WRITE a
    // region, which is why only this one call sits above the gate and the whole
    // land/framing block stays inside it. The no-LAND half is EXCEPTIONLESS again:
    // the target-view re-land it briefly allowed — onto a selection
    // surviving the restore — died with the selection clear directly below, which
    // leaves no focus to land on.
    clear_region_highlight(app, viewport);
    // THE 'S' ARM CLEARS THE SELECTION TOO (architect 2026-07-29): a
    // settings-only restore rewrites engine_settings and rebuilds the map under
    // every marker INDEX and IMAGE, so no marker keeps the identity a focus
    // named. It is the SYMMETRIC twin of the engine-key
    // settings COMMIT, which clears both at its own chokepoint
    // (settings_editor.cpp); GUI-kind keys are history-less, so 'S' is the only
    // settings entry kind there is and the pair covers the whole surface. Together
    // they are what let the never-span-less ENFORCEMENT be deleted — these were its
    // last two producers, and closing them symmetrically means no collapse protocol
    // is owed. It does not
    // violate the 'S' gate's no-SELECT half: emptying a selection is not selecting.
    // The non-'S' entries need nothing here — they re-select the touched set.
    if (entry.op_mode == 'S') selection.clear_selection();
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
                // The restored singleton needs no cue work here: its flag
                // BRIGHTENS from the restored membership and the top-strip /
                // full-waveform invalidates below repaint it. Stems do not
                // enter it at all — they are class-colored and always on,
                // selection playing no part — so there is nothing to stamp
                // or pin.
            }
        } else if (sel_size >= 2) {
            // GROUP: LAND the playhead on the restore's FOCUS (architect
            // 2026-07-25, its region half retired 2026-07-30 with the SPAN
            // FORM). This obeys the
            // universal land-on-the-focus rule with no special case, because a
            // restore's focus IS the earliest touched member by construction
            // (apply_post_restore_rules_impl) — spelled as
            // *selected_markers.begin() rather than last_selected_marker so a
            // sanitize that pruned the focus still lands somewhere live. The
            // group's visual is the restored members' brightened flags plus the
            // always-visible cursor sitting on the earliest of them — the
            // extent-region write that used to follow this land is gone, the
            // region being trim scratch that a restore has no business creating.
            // land_playhead_on_marker is internally bounds-guarded (an
            // impossible out-of-range index no-ops the land) and writes NO
            // viewport, so the three-way offscreen arm below is unaffected;
            // playback is already stopped above (land's scanner-inactive
            // premise).
            land_playhead_on_marker(app, viewport.audio, viewport,
                                    *app.selected_markers.begin());
            // OFFSCREEN handling (architect 2026-07-25 post-labwc, decided on
            // PAINTED COLUMNS): PREFER a plain scroll at the current zoom, ZOOM only
            // when the group cannot fit. The fit contract is PAINTED COLUMNS, not a
            // sample span — an endpoint's flag paints at its CENTER column and the
            // painter does NOT edge-clamp that center, so the capacity is the
            // pixel range [0, W), NOT q*W samples (which overcounts by up to a
            // column) and NOT the grid-snapped start (clamp_viewport_start moves it
            // ~half a pixel). Both tests decide on the endpoints' columns under the
            // painter's own basis (painter_samples_per_pixel + the shared
            // displayed_column_at rounding — the region endpoints already live in
            // the active display domain, so no warp map is walked). Three arms:
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
            //     gutter, where the flag (at its painted width) can show WHOLE just
            //     outside the effective span — flag centers use the effective
            //     W (floored to a multiple of 16) while the flag surface
            //     spans the full strip. At
            //     the ruled deployment widths (1920 / 2560 / 3840, all multiples of
            //     16) the gutter is empty and it half-culls. Either way NO route
            //     places the endpoint INSIDE the effective span at whole-song-
            //     visible — the standing flags-may-hang-half-offscreen geometry (cull
            //     only when FULLY out), the SAME cull the level-preserving
            //     navigation routes show there (Tab, which keeps the level; the
            //     marker-click land, which writes no viewport; and the trim-bar
            //     double-click framer itself, no-op under this conjunction) — not a
            //     framing defect, and identical
            //     under every option reachable within the whole-song-ceiling and
            //     centered-flag rulings. The futile framer call is left as-is (a
            //     harmless no-op there); a ceiling special-case would be a branch for
            //     ZERO behavioral difference. This whole arm diverges from the
            //     trim-bar DOUBLE-CLICK's unconditional zoom-to-span; the framer
            //     itself is untouched, an undo-tail rule only.
            // ACCEPTED COST on the framer arm: apply_zoom_to_start runs one sync
            // render and the unconditional kick_waveform_sync below runs a second
            // over identical final state — a bounded duplicate on a discrete
            // keystroke (the keyboard zoom's per-press cost).
            // THE SPAN THE FRAMING DECIDES ON IS THE TOUCHED SET'S OWN
            // [earliest, latest] ACTIVE-DOMAIN EXTENT, derived right here from
            // the restored members' positions (architect 2026-07-30): it used to
            // be read back out of the region this arm had just written, and with
            // that write retired the framing owns its span source directly. The
            // per-member formula is the LAND'S, exactly —
            // clamp_playhead_to_live_domain(source_frame_to_active_domain(...)) —
            // so the endpoints are the same playable frames the old extent
            // carried and every framing decision below is unchanged. `have`
            // false (every restored index stale — degenerate, and impossible
            // post-sanitize) frames nothing, matching the old extent owner's own
            // no-op return.
            int64_t lo = 0, hi = 0;
            bool    have = false;
            {
                const bool phase_reset = (app.active_markers_view == 'P');
                const auto& warp_vec = app.warpmarkers.markers();
                const auto& phase_reset_vec = app.phaseresetmarkers.markers();
                // The index bound from its one owner (active_marker_count,
                // app_state.h — it reads the same live stores the refs above
                // bind); the refs stay for the per-element time_frame reads.
                const int n = active_marker_count(app);
                for (int idx : app.selected_markers) {
                    if (idx < 0 || idx >= n) continue;   // defensive
                    const int64_t src_f = phase_reset
                        ? phase_reset_vec[idx].time_frame
                        : warp_vec[idx].time_frame;
                    const int64_t pos = clamp_playhead_to_live_domain(
                        source_frame_to_active_domain(app, viewport.audio, src_f),
                        app, viewport.audio);
                    if (!have) { lo = hi = pos; have = true; }
                    else { if (pos < lo) lo = pos; if (pos > hi) hi = pos; }
                }
            }
            if (have) {
                const int     W       = waveform_area(app).w;
                const double  q       = painter_samples_per_pixel(
                    app, viewport.audio, waveform_area(app));
                // Endpoint column under a given viewport start, on the flag
                // painters' basis (the shared displayed_column_at rounding,
                // warp_frame_map_view.h); empty q (no geometry) leaves the
                // viewport put.
                auto both_columns_visible = [&](int64_t vp_start) {
                    const int lo_col = displayed_column_at(
                        static_cast<double>(lo), static_cast<double>(vp_start), q);
                    const int hi_col = displayed_column_at(
                        static_cast<double>(hi), static_cast<double>(vp_start), q);
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
    // in either arm) and recentered
    // or framed the viewport; these invalidations and the
    // sync kick cover all of that as well as the marker change. Render it
    // synchronously so the restored markers and the waveform land together. A
    // single keystroke, so bounded — the drag-time async-warp_frame_map policy is about
    // the marker-drag torrent, not discrete events. kick_waveform_sync's damage
    // duplicates invalidate_waveform_area above (harmless); the (now plain)
    // trigger below owns target-buffer freshness when target view is available.
    viewport.kick_waveform_sync();
    // NO 'S' RE-LAND, and none is possible: the map-change re-land that sat here
    // (target view only, onto a surviving selection's focus, because the restored
    // map moved that focus's image out from under the cursor) died with the 'S'
    // selection clear above — architect 2026-07-29. An 'S' restore leaves
    // no lane and no focus, so the resting cursor is the whole playhead and keeps
    // its own value. The engine-key settings COMMIT's twin re-land died the same way
    // (settings_editor.cpp). Every other op_mode still lands through the visual tail
    // above, on its restored focus in both arms.
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
