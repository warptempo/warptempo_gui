#include "undo.h"

#include "audio.h"
#include "target_render.h"
#include "warp_frame_map_view.h"

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

void Undo::push_undo_warp(std::vector<GuiWarpMarker> pre_state, int hint_last,
                          bool affects_persistence) {
    UndoEntry e;
    e.snapshot           = std::move(pre_state);
    e.phase_reset_snapshot = app.phaseresetmarkers.markers();
    e.settings           = capture_current_settings(app);
    e.op_mode            = 'W';
    e.tab                = app.active_tab_view;
    e.hint_last_selected = hint_last;
    e.affects_persistence = affects_persistence;
    app.history.push(std::move(e));
    viewport.clear_hover_popup();
}

void Undo::push_undo_phase_reset(std::vector<GuiPhaseResetMarker> pre_state,
                               int hint_last) {
    UndoEntry e;
    e.snapshot           = app.warpmarkers.markers();
    e.phase_reset_snapshot = std::move(pre_state);
    e.settings           = capture_current_settings(app);
    e.op_mode            = 'P';
    e.tab                = app.active_tab_view;
    e.hint_last_selected = hint_last;
    app.history.push(std::move(e));
    viewport.clear_hover_popup();
}

void Undo::push_undo_both(std::vector<GuiWarpMarker> warp_pre,
                          std::vector<GuiPhaseResetMarker> phase_reset_pre,
                          char op_mode, int hint_last, char tab_override) {
    UndoEntry e;
    e.snapshot           = std::move(warp_pre);
    e.phase_reset_snapshot = std::move(phase_reset_pre);
    e.settings           = capture_current_settings(app);
    e.op_mode            = op_mode;
    e.tab                = tab_override ? tab_override : app.active_tab_view;
    e.hint_last_selected = hint_last;
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
    e.hint_last_selected = app.last_selected_marker;
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
    // does not advance command_seq).
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

namespace {

// Shared post-restore selection + playhead rule for both marker lists. After a
// marker swap, classify before -> after as add / remove / same-count and set
// the selection (and, where appropriate, the playhead) to the touched markers.
// `fields_differ` is the same-count in-place-edit predicate;
// `remove_target_frame` chooses the playhead target among the removed markers
// (warp refines toward the op's subject, phase-reset takes the rightmost).
// Authored times are whole int64 source frames, so matching is EXACT integer
// multiset consumption — no epsilon, no double widening, no re-rounding — and
// multiplicity-aware: when one of two exactly coincident markers is
// added/removed, each before-row match consumes exactly one after-row, so the
// touched member of the tie is still identified.
template <class M, class FieldsDiffer, class RemoveTargetFrame>
void apply_post_restore_rules_impl(AppState& app, Selection& selection,
                                   const UndoEntry& entry,
                                   const std::vector<M>& before,
                                   const std::vector<M>& after,
                                   FieldsDiffer  fields_differ,
                                   RemoveTargetFrame remove_target_frame) {
    std::set<int> target_set;
    bool want_playhead_jump = false;

    if (after.size() > before.size()) {
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
        want_playhead_jump = !target_set.empty();
    } else if (after.size() < before.size()) {
        const int sr = selection.audio.sample_rate();
        if (sr > 0) {
            std::multiset<int64_t> after_frames;
            for (const auto& m : after) after_frames.insert(m.time_frame);
            std::vector<size_t> removed;  // indices into `before`
            for (size_t i = 0; i < before.size(); ++i) {
                auto it = after_frames.find(before[i].time_frame);
                if (it != after_frames.end()) {
                    after_frames.erase(it);  // consume: one match per row
                } else {
                    removed.push_back(i);
                }
            }
            if (!removed.empty()) {
                int64_t rightmost = before[removed.front()].time_frame;
                for (const size_t i : removed) {
                    rightmost = std::max(rightmost, before[i].time_frame);
                }
                const int64_t src_frame =
                    remove_target_frame(before, removed, rightmost);
                // time_frame is source-domain; the playhead is active-domain.
                // Forward-translate so the playhead lands at the restored
                // marker's displayed position, mirroring
                // Selection::sync_playhead_to_last_selected.
                const int64_t target_sample = source_frame_to_active_domain(
                    app, selection.audio, src_frame);
                selection.jump_playhead_to(target_sample);
            }
        }
        app.selected_markers.clear();
        app.last_selected_marker = -1;
        return;
    } else {  // same count: flag any in-place edit
        for (size_t i = 0; i < after.size(); ++i) {
            if (fields_differ(after[i], before[i]))
                target_set.insert(static_cast<int>(i));
        }
        want_playhead_jump = !target_set.empty();
    }

    if (target_set.empty()) return;

    app.selected_markers = target_set;
    if (target_set.count(entry.hint_last_selected)) {
        app.last_selected_marker = entry.hint_last_selected;
    } else {
        app.last_selected_marker = *target_set.rbegin();
    }

    if (!want_playhead_jump) return;
    selection.sync_playhead_to_last_selected();
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
                || a.label_ref      != b.label_ref;
        },
        [&entry](const std::vector<GuiWarpMarker>& bef,
                 const std::vector<size_t>& removed,
                 int64_t rightmost) {
            // Prefer the op's subject when it is itself one of the removed
            // markers, so a multi-selection delete lands the playhead on the
            // subject the hint names rather than the rightmost-in-time marker
            // the batch removed.
            const int hi = entry.hint_last_selected;
            for (const size_t i : removed) {
                if (static_cast<int>(i) == hi) return bef[i].time_frame;
            }
            return rightmost;
        });
}

void Undo::apply_post_restore_rules_phase_reset(
        const UndoEntry& entry,
        const std::vector<GuiPhaseResetMarker>& before) {
    apply_post_restore_rules_impl(
        app, selection, entry, before, app.phaseresetmarkers.markers(),
        [](const GuiPhaseResetMarker& a, const GuiPhaseResetMarker& b) {
            return a.time_frame != b.time_frame
                || a.disabled   != b.disabled;
        },
        [](const std::vector<GuiPhaseResetMarker>&,
           const std::vector<size_t>&, int64_t rightmost) {
            return rightmost;
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
    counter.hint_last_selected  = entry.hint_last_selected;
    counter.affects_persistence = entry.affects_persistence;
    std::vector<GuiWarpMarker>       before_w = counter.snapshot;
    std::vector<GuiPhaseResetMarker> before_t = counter.phase_reset_snapshot;

    to.push_back(std::move(counter));
    // No kCap trim here: each restore moves one entry between the stacks (`from`
    // popped above, `to` pushed here), and push — the only operation that grows
    // the total — clears the redo stack and caps the undo stack. So
    // undo_stack.size() + redo_stack.size() never exceeds kCap and the
    // destination cannot overflow.
    if (app.history.saved_valid) app.history.saved_distance += saved_distance_delta;

    // Restore the originating A/B tab before the marker swap. The
    // post-restore rules below call selection.jump_playhead_to(...),
    // which writes app.playhead_cursor_sample / app.viewport_start_sample —
    // those writes must land on the tab the action was authored on.
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
        // then restore the destination mode's slot.
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
    recompute_dirty();
    viewport.invalidate_waveform_area();
    // One-shot discrete jump: undo/redo restored markers / phase resets /
    // settings, changing the displayed plate (the target-view warp_frame_map, and the
    // viewport when an offscreen-marker restore recenters). Render it
    // synchronously so the restored markers and the waveform land together. A
    // single keystroke, so bounded — the drag-time async-warp_frame_map policy is about
    // the marker-drag torrent, not discrete events. kick_waveform_sync's damage
    // duplicates invalidate_waveform_area above (harmless); the (now plain)
    // trigger below owns target-buffer freshness when target view is available.
    viewport.kick_waveform_sync();
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
