#include "undo.h"

#include "audio.h"
#include "target_render.h"
#include "warp_frame_map_view.h"

#include <algorithm>
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
            const char m = h.redo_stack[i].op_mode;
            if      (m == 'P') app.phase_reset_dirty = true;
            else if (m == 'S') app.settings_dirty    = true;
            else               app.warp_dirty        = true;
        }
    }
    app.dirty = app.warp_dirty || app.phase_reset_dirty || app.settings_dirty;
}

void Undo::push_undo_warp(std::vector<GuiWarpMarker> pre_state, int hint_last) {
    UndoEntry e;
    e.snapshot           = std::move(pre_state);
    e.phase_reset_snapshot = app.phaseresetmarkers.markers();
    e.settings           = capture_current_settings(app);
    e.op_mode            = 'W';
    e.tab                = app.active_tab_view;
    e.hint_last_selected = hint_last;
    app.history.push(std::move(e));
    viewport.clear_hover_popup();
    // Commit funnel: one commit = one history entry, so the push helpers
    // are where every marker commit site funnels. Flag the once-per-tick
    // defect validation (GuiInputHandler::run_commit_validation) instead of
    // editing every call site.
    app.defect_series.pending_validation = PendingValidation::Commit;
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
    // Commit funnel (see push_undo_warp).
    app.defect_series.pending_validation = PendingValidation::Commit;
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
    // Commit funnel (see push_undo_warp).
    app.defect_series.pending_validation = PendingValidation::Commit;
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
    // Commit funnel (see push_undo_warp). Settings entries cannot move
    // marker times, but scale participates in the trim target-span
    // validity (validate_trim_frames runs inside the series' trim column)
    // and output_format participates in the map-format-with-trim conflict,
    // so a settings commit can create a walked defect.
    app.defect_series.pending_validation = PendingValidation::Commit;
}

namespace {

// Shared post-restore selection + playhead rule for both marker lists. After a
// marker swap, classify before -> after as add / remove / same-count and set
// the selection (and, where appropriate, the playhead) to the touched markers.
// `fields_differ` is the same-count in-place-edit predicate; `remove_target_time`
// chooses the playhead target among the removed markers (warp refines toward the
// op's subject, phase-reset takes the rightmost).
template <class M, class FieldsDiffer, class RemoveTargetTime>
void apply_post_restore_rules_impl(AppState& app, Selection& selection,
                                   const UndoEntry& entry,
                                   const std::vector<M>& before,
                                   const std::vector<M>& after,
                                   FieldsDiffer  fields_differ,
                                   RemoveTargetTime remove_target_time) {
    constexpr double kEps = 1e-9;

    std::set<int> target_set;
    bool want_playhead_jump = false;

    if (after.size() > before.size()) {
        std::vector<double> before_times;
        before_times.reserve(before.size());
        for (const auto& m : before) before_times.push_back(m.time_seconds);
        std::sort(before_times.begin(), before_times.end());
        for (size_t i = 0; i < after.size(); ++i) {
            const double t = after[i].time_seconds;
            auto it = std::lower_bound(before_times.begin(),
                                       before_times.end(), t - kEps);
            const bool matched = (it != before_times.end() &&
                                  std::abs(*it - t) < kEps);
            if (!matched) target_set.insert(static_cast<int>(i));
        }
        want_playhead_jump = !target_set.empty();
    } else if (after.size() < before.size()) {
        const int sr = selection.audio.sample_rate();
        if (sr > 0) {
            std::vector<double> after_times;
            after_times.reserve(after.size());
            for (const auto& m : after) after_times.push_back(m.time_seconds);
            std::sort(after_times.begin(), after_times.end());
            double rightmost = 0.0;
            bool   any       = false;
            for (const auto& m : before) {
                const double t = m.time_seconds;
                auto it = std::lower_bound(after_times.begin(),
                                           after_times.end(), t - kEps);
                const bool matched = (it != after_times.end() &&
                                      std::abs(*it - t) < kEps);
                if (!matched && (!any || t > rightmost)) {
                    rightmost = t;
                    any       = true;
                }
            }
            if (any) {
                const double target_time =
                    remove_target_time(before, after_times, rightmost, kEps);
                const int64_t src_sample = static_cast<int64_t>(
                    std::nearbyint(target_time * static_cast<double>(sr)));
                // time_seconds is source-domain; the playhead is active-domain.
                // Forward-translate so the playhead lands at the restored
                // marker's displayed position, mirroring
                // Selection::sync_playhead_to_last_selected.
                const int64_t target_sample = source_frame_to_active_domain(
                    app, selection.audio, src_sample);
                selection.jump_playhead_to(target_sample);
            }
        }
        app.selected_markers.clear();
        app.last_selected_marker = -1;
        return;
    } else {  // same count: flag any in-place edit
        for (size_t i = 0; i < after.size(); ++i) {
            if (fields_differ(after[i], before[i], kEps))
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
        [](const GuiWarpMarker& a, const GuiWarpMarker& b, double kEps) {
            return std::abs(a.time_seconds - b.time_seconds) > kEps
                || a.disabled       != b.disabled
                || a.tempo_inherits != b.tempo_inherits
                || std::abs(a.tempo_base - b.tempo_base) > kEps
                || a.tempo_scale    != b.tempo_scale
                || a.label_def      != b.label_def
                || a.label_ref      != b.label_ref;
        },
        [&entry](const std::vector<GuiWarpMarker>& bef,
                 const std::vector<double>& after_times,
                 double rightmost, double kEps) {
            // Prefer the op's subject when it is itself one of the removed
            // markers, so a cascaded label_def force-delete lands the playhead
            // on the def rather than the rightmost-in-time ref the cascade
            // pulled in.
            double target_time = rightmost;
            const int hi = entry.hint_last_selected;
            if (hi >= 0 && hi < static_cast<int>(bef.size())) {
                const double ht = bef[hi].time_seconds;
                auto it = std::lower_bound(after_times.begin(),
                                           after_times.end(), ht - kEps);
                const bool matched = (it != after_times.end() &&
                                      std::abs(*it - ht) < kEps);
                if (!matched) target_time = ht;
            }
            return target_time;
        });
}

void Undo::apply_post_restore_rules_phase_reset(
        const UndoEntry& entry,
        const std::vector<GuiPhaseResetMarker>& before) {
    apply_post_restore_rules_impl(
        app, selection, entry, before, app.phaseresetmarkers.markers(),
        [](const GuiPhaseResetMarker& a, const GuiPhaseResetMarker& b, double kEps) {
            return std::abs(a.time_seconds - b.time_seconds) > kEps
                || a.disabled != b.disabled;
        },
        [](const std::vector<GuiPhaseResetMarker>&,
           const std::vector<double>&, double rightmost, double) {
            return rightmost;
        });
}

void Undo::do_undo() {
    if (app.history.undo_stack.empty()) return;
    // Render view is read-only: undo and redo are silent no-ops so the
    // underlying source-view marker lists are not mutated behind the
    // user's back while the UI says render view is read-only.
    if (app.render_view.enabled) return;
    // Peek the entry on top of the undo stack: if the tab it targets is
    // currently read-only, undo is a silent no-op. Read-only is a reversible
    // per-tab toggle, so honored-ness is decided by the target tab's state
    // now, not when the action was recorded. Peek-then-bail — the entry stays
    // on the stack and the view is unchanged, so unlocking the tab makes the
    // history reachable again with nothing lost.
    {
        const char tt = app.history.undo_stack.back().tab;
        const bool target_ro = (tt == 'B') ? app.tab_b.read_only
                                            : app.tab_a.read_only;
        if (target_ro) return;
    }
    playback_lifecycle.stop_playback_if_playing();
    viewport.clear_hover_popup();
    UndoEntry entry = std::move(app.history.undo_stack.back());
    app.history.undo_stack.pop_back();

    UndoEntry redo_entry;
    redo_entry.snapshot           = app.warpmarkers.markers();
    redo_entry.phase_reset_snapshot = app.phaseresetmarkers.markers();
    redo_entry.settings           = capture_current_settings(app);
    redo_entry.op_mode            = entry.op_mode;
    redo_entry.tab                = entry.tab;
    redo_entry.hint_last_selected = entry.hint_last_selected;
    std::vector<GuiWarpMarker>    before_w = redo_entry.snapshot;
    std::vector<GuiPhaseResetMarker> before_t = redo_entry.phase_reset_snapshot;

    app.history.redo_stack.push_back(std::move(redo_entry));
    if (app.history.redo_stack.size() > UndoHistory::kCap) {
        app.history.redo_stack.erase(app.history.redo_stack.begin());
    }
    if (app.history.saved_valid) app.history.saved_distance += 1;

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

    // Switch active mode to match the op being undone before applying
    // post-restore rules — selection state is mode-bound, so the rules
    // and the sanitize step must run against the correct list. Skip
    // entirely for settings-only entries: they don't carry an authoring
    // mode, and active_markers_view is a view-state key that's not undoable.
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
    // duplicates invalidate_waveform_area above (harmless); target_render.trigger
    // below still owns target-buffer freshness when target view is available.
    viewport.kick_waveform_sync();
    viewport.invalidate_timestamp_area();
    if (!target_render.target_view_available()) {
        target_render.leave_target_view();
    }
    // Every undo restores engine input (markers, phase resets, or
    // settings) — fire a target render in target view. No-op in
    // source view.
    target_render.trigger();
    // Undo is deliberately NOT a gated commit site (no pending_validation
    // flag): it can only land on states that were previously accepted —
    // either the modal series validated them or they predate the offending
    // commit — so undoing out of a defect must close the series, not
    // re-open it.
}

void Undo::do_redo() {
    if (app.history.redo_stack.empty()) return;
    if (app.render_view.enabled) return;
    // Symmetric to do_undo: peek the entry on top of the redo stack and bail
    // silently if its target tab is currently read-only. Entry stays on the
    // stack; unlocking the tab restores reachability.
    {
        const char tt = app.history.redo_stack.back().tab;
        const bool target_ro = (tt == 'B') ? app.tab_b.read_only
                                            : app.tab_a.read_only;
        if (target_ro) return;
    }
    playback_lifecycle.stop_playback_if_playing();
    viewport.clear_hover_popup();
    UndoEntry entry = std::move(app.history.redo_stack.back());
    app.history.redo_stack.pop_back();

    UndoEntry undo_entry;
    undo_entry.snapshot           = app.warpmarkers.markers();
    undo_entry.phase_reset_snapshot = app.phaseresetmarkers.markers();
    undo_entry.settings           = capture_current_settings(app);
    undo_entry.op_mode            = entry.op_mode;
    undo_entry.tab                = entry.tab;
    undo_entry.hint_last_selected = entry.hint_last_selected;
    std::vector<GuiWarpMarker>    before_w = undo_entry.snapshot;
    std::vector<GuiPhaseResetMarker> before_t = undo_entry.phase_reset_snapshot;

    app.history.undo_stack.push_back(std::move(undo_entry));
    if (app.history.undo_stack.size() > UndoHistory::kCap) {
        app.history.undo_stack.erase(app.history.undo_stack.begin());
    }
    if (app.history.saved_valid) app.history.saved_distance -= 1;

    if (entry.tab != app.active_tab_view) {
        active_views.switch_active_tab_view_to(entry.tab);
    }

    app.engine_settings    = std::move(entry.settings.engine_settings);

    app.warpmarkers.markers_mut()    = std::move(entry.snapshot);
    app.phaseresetmarkers.markers_mut() = std::move(entry.phase_reset_snapshot);

    if (entry.op_mode != 'S' && entry.op_mode != app.active_markers_view) {
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
    // duplicates invalidate_waveform_area above (harmless); target_render.trigger
    // below still owns target-buffer freshness when target view is available.
    viewport.kick_waveform_sync();
    viewport.invalidate_timestamp_area();
    if (!target_render.target_view_available()) {
        target_render.leave_target_view();
    }
    // Every redo restores engine input — fire a target render in
    // target view. No-op in source view.
    target_render.trigger();
    // Redo IS a gated commit site, unlike undo just above: it re-applies a
    // commit that may have been the offending one, so the defect modal must
    // re-fire on the re-applied state.
    app.defect_series.pending_validation = PendingValidation::Commit;
}
