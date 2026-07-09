#include "input_handler.h"

#include "marker_store_validate.h"
#include "trimmer.h"
#include "warp_frame_map_view.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

// Forced-choice defect-resolution modal series. A commit that leaves the
// stores render-invalid — or a source load that carries file-borne,
// GUI-committable defects — opens a modal demanding resolution; after each
// choice the state is re-validated from scratch (one undo can fix several
// defects) and the next modal opens until the stores are clean. Modal
// resolutions push NO undo history: the offending commit's history entry
// remains, so Ctrl+Z after resolving returns to the pre-commit valid state,
// redo re-applies the offending commit and re-fires the modal, and plain
// undo is deliberately not a gated commit site — it can only land on states
// that were previously accepted (the flag-setting comments live in
// undo.cpp).
//
// The defect predicate is the parser-side enumerate_marker_store_defects
// over the sliced live stores, plus a trim column owned here (trim defects
// have no MarkerDefect shape). The trim column walks after the last marker
// defect: the map-free exact frame check first (crossed-or-equal), then the
// map-format-with-trim conflict (trim is wav-only), then — only when the
// marker walk is clean AND the live map builds — the full
// validate_trim_frames against the memoized target-view map. Guarding
// validate_trim_frames on a clean marker store is not a gap: when markers
// are broken their modals run first and trim re-validates on the next pass
// of the same series. The crossed-or-equal and validate_trim_frames defects
// share the single [Delete]-both-bounds resolution; the map-format conflict
// carries its own [U]ndo / [R]eset / [Delete] set (see TrimDefectKind).

namespace {

// The defect column's live focus index. The active selection pair is
// mode-bound (it indexes the DISPLAYED column's list), so a defect in the
// other column reads the active tab's per-mode slot instead.
int column_last_selected(const AppState& app, char column) {
    const char active_col = (app.active_markers_view == 'P') ? 'P' : 'W';
    if (active_col == column) return app.last_selected_marker;
    const ViewState& t = active_view_state(app);
    return (column == 'P') ? t.phase_reset_last_selected
                           : t.warp_last_selected;
}

// After an erasure (or a front insert, which shifts every index the same
// way) the column's index-shaped state points at the wrong markers. Mirror
// of what delete_selected_marker / force_delete_selected_marker
// (warpmarkers_ops.cpp) and delete_selected_phase_reset
// (phaseresetmarkers_ops.cpp) do after erasing — clear the selection set
// and the focus — routed to wherever the column's selection currently
// lives: the active pair when the column is displayed, the active tab's
// per-mode slot otherwise. The inactive tab's slot stays stale exactly as
// it does under those delete ops; it is pruned at the swap boundary
// (Selection::prune_live_selection).
void clear_column_selection_state(AppState& app, char column) {
    const char active_col = (app.active_markers_view == 'P') ? 'P' : 'W';
    if (active_col == column) {
        app.selected_markers.clear();
        app.last_selected_marker = -1;
        return;
    }
    ViewState& t = active_view_state(app);
    if (column == 'P') {
        t.phase_reset_selected.clear();
        t.phase_reset_last_selected = -1;
    } else {
        t.warp_selected.clear();
        t.warp_last_selected = -1;
    }
}

// Erase `indices` (ascending store indices) from the defect's column,
// walking descending so earlier indices stay valid — the same order the
// delete ops use.
void erase_column_indices(AppState& app, char column,
                          const std::vector<size_t>& indices) {
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        if (column == 'P') {
            app.phaseresetmarkers.remove_marker(static_cast<int>(*it));
        } else {
            app.warpmarkers.remove_marker(static_cast<int>(*it));
        }
    }
    clear_column_selection_state(app, column);
}

}  // namespace

void GuiInputHandler::run_commit_validation() {
    if (app.defect_series.pending_validation == PendingValidation::None)
        return;
    // A prompt already owning the bottom strip means no edit can be in
    // flight (prompts are modal); keep the flag for the next tick rather
    // than clobber the prompt — the same deferral shape
    // enforce_target_view_validity uses.
    if (app.prompt.active) return;
    if (app.loading) return;
    // Never mid-gesture: a pointer gesture begun in the same event batch as
    // the flagging commit defers the series one tick at a time until it
    // ends. Without this, the prompt's generic mouse swallow would strand
    // the in-flight drag (motion and release are dropped while a prompt is
    // up), leaving it to resume against post-resolution state.
    if (app.drag.active || app.trim_drag.active ||
        app.scroll_drag.active || app.playhead_drag.active ||
        app.editor_text_drag.active) return;
    // Blank state: nothing to validate against; the load path clears the
    // flag, this is just the same-tick backstop.
    if (audio.total_frames() <= 0) return;
    const PendingValidation origin = app.defect_series.pending_validation;
    app.defect_series.pending_validation = PendingValidation::None;
    // Commit context only for Commit-origin series: a Load-origin walk has
    // no touched marker for the coincident-group narrowing, and it offers
    // no [U]ndo naturally — the loader clears history, so the
    // undo_stack-non-empty condition already yields none (no special case).
    open_defect_series(origin == PendingValidation::Commit);
}

bool GuiInputHandler::open_defect_series(bool commit_context) {
    const long sr    = static_cast<long>(audio.sample_rate());
    const long total = static_cast<long>(audio.total_frames());

    app.defect_series.commit_context = commit_context;

    if (sr > 0 && total > 0) {
        // Marker defects walk first, in the enumerator's chronological
        // order; only the FIRST defect is stashed — every resolution
        // re-enumerates from scratch, so a stale defect queue cannot exist
        // (see DefectSeriesState).
        std::vector<MarkerDefect> defects = enumerate_marker_store_defects(
            slice_to_warp_markers(app.warpmarkers.markers()),
            slice_to_phase_reset_markers(app.phaseresetmarkers.markers()), sr);
        if (!defects.empty()) {
            MarkerDefect d = std::move(defects.front());
            // [U]ndo is offered exactly when the undo stack is non-empty
            // and the defect can be rewound through history: marker
            // defects (here) and the map-format conflict (a settings-class
            // entry, below); the delete-both-bounds trim defects cannot
            // (trim is not in history).
            std::vector<char>        keys;
            std::vector<std::string> labels;
            if (!app.history.undo_stack.empty()) {
                keys.push_back('u');
                labels.push_back("[U]ndo");
            }
            switch (d.kind) {
            case MarkerDefectKind::CoincidentGroup:
                keys.push_back('\x7f');
                labels.push_back("[Delete]");
                break;
            case MarkerDefectKind::DanglingLabelRef:
                keys.push_back('r');
                labels.push_back("[R]eset");
                keys.push_back('\x7f');
                labels.push_back("[Delete]");
                break;
            case MarkerDefectKind::FirstMarkerGrammar:
                keys.push_back('r');
                labels.push_back("[R]eset");
                break;
            }
            app.prompt.active          = true;
            app.prompt.text            = d.message;  // enumerator's, verbatim
            app.prompt.response_keys   = std::move(keys);
            app.prompt.response_labels = std::move(labels);
            app.prompt.trigger         = DialogTrigger::DEFECT_RESOLUTION;
            app.defect_series.defect           = std::move(d);
            app.defect_series.trim_defect_kind = TrimDefectKind::None;
            viewport.clear_hover_popup();
            viewport.invalidate_all();
            return true;
        }

        // The trim column walks after the last marker defect — trim's full
        // validity is downstream of the marker map. The map-free check runs
        // first, as an exact frame-double compare on the authored bounds —
        // no rounding anywhere, the same e_src <= b_src comparison
        // validate_trim_frames applies: crossed-or-equal when both bounds
        // are set (past-EOF bounds no longer reach here — the gesture walls
        // and the load boundary make them unreachable in memory), then the
        // map-format-with-trim conflict — a cross-domain conflict, not a
        // settings-validity failure: trim is wav-only (map artifacts are
        // always the FULL maps), so any set bound under a map format is a
        // walked defect, same wording as the render refusal. When both
        // pass, the full validate_trim_frames runs against the memoized
        // target-view map — the FULL trim-off map the render preflight and
        // target-view gate validate against — but only when that map built
        // (markers are already clean here; a build failure is the
        // non-modeled class the render preflight's popup backstops). The
        // crossed-or-equal and validate_trim_frames defects share the one
        // [Delete]-both-bounds resolution; the map-format conflict carries
        // [U]ndo / [R]eset / [Delete] (see TrimDefectKind).
        if (app.trim.has_begin || app.trim.has_end) {
            std::string trim_msg;
            TrimDefectKind kind = TrimDefectKind::ClearBounds;
            if (app.trim.has_begin && app.trim.has_end &&
                app.trim.end_frame <= app.trim.begin_frame) {
                trim_msg = "trim bounds crossed or equal";
            } else if (app.engine_settings.output_format != "wav") {
                trim_msg =
                    "map formats take no trim; clear the trim or render wav";
                kind = TrimDefectKind::MapFormatConflict;
            } else {
                const TargetWarpFrameMapCache& c =
                    target_view_warp_frame_map_cached(
                        app, audio.sample_rate(), total);
                if (c.build_error.empty()) {
                    if (auto v = validate_trim_frames(
                            app.trim.has_begin, app.trim.begin_frame,
                            app.trim.has_end,   app.trim.end_frame,
                            static_cast<int64_t>(total),
                            c.warp_frame_map); !v) {
                        trim_msg = std::move(v.error());  // trimmer's, verbatim
                    }
                }
            }
            if (!trim_msg.empty()) {
                std::vector<char>        keys;
                std::vector<std::string> labels;
                if (kind == TrimDefectKind::MapFormatConflict) {
                    // [U]ndo on the standard undo_stack-non-empty
                    // condition: a settings commit that created the
                    // conflict is an ordinary settings-class entry, so
                    // do_undo reverts it. [R]eset flips output_format back
                    // to wav (the trim survives); [Delete] clears both
                    // bounds (the format survives).
                    if (!app.history.undo_stack.empty()) {
                        keys.push_back('u');
                        labels.push_back("[U]ndo");
                    }
                    keys.push_back('r');
                    labels.push_back("[R]eset");
                }
                keys.push_back('\x7f');
                labels.push_back("[Delete]");
                app.prompt.active          = true;
                app.prompt.text            = std::move(trim_msg);
                app.prompt.response_keys   = std::move(keys);
                app.prompt.response_labels = std::move(labels);
                app.prompt.trigger         = DialogTrigger::DEFECT_RESOLUTION;
                app.defect_series.defect           = MarkerDefect{};
                app.defect_series.trim_defect_kind = kind;
                viewport.clear_hover_popup();
                viewport.invalidate_all();
                return true;
            }
        }
    }

    // Clean: close the series. Dismiss only our own prompt — another
    // trigger's prompt is never touched from here.
    app.defect_series.trim_defect_kind = TrimDefectKind::None;
    if (app.prompt.active &&
        app.prompt.trigger == DialogTrigger::DEFECT_RESOLUTION) {
        app.prompt.active = false;
        viewport.invalidate_all();
    }
    return false;
}

void GuiInputHandler::handle_defect_response(char k) {
    if (!app.prompt.active ||
        app.prompt.trigger != DialogTrigger::DEFECT_RESOLUTION) return;
    // Only the offered keys act; everything else stays swallowed by the
    // modal (Esc included — see the DialogTrigger comment).
    if (std::find(app.prompt.response_keys.begin(),
                  app.prompt.response_keys.end(), k) ==
        app.prompt.response_keys.end()) return;

    const TrimDefectKind trim_kind = app.defect_series.trim_defect_kind;
    if (trim_kind == TrimDefectKind::MapFormatConflict && k == 'u') {
        // Rewind the settings commit that created the conflict — the same
        // history-symmetric shape as the marker-defect [U]ndo below.
        undo.do_undo();
    } else if (trim_kind == TrimDefectKind::MapFormatConflict && k == 'r') {
        // [R]eset: flip output_format back to wav directly, NO history
        // push; the trim survives. Same history-less bookkeeping as the
        // store-mutating resolutions below (saved_valid = false pins the
        // dirty flags until the next save rebinds the reference). Repaint
        // rides the re-validation at the bottom: both its outcomes (next
        // modal or clean close) invalidate_all.
        app.engine_settings.output_format = "wav";
        app.history.saved_valid = false;
        undo.recompute_dirty();
    } else if (trim_kind != TrimDefectKind::None) {
        // [Delete]: clear BOTH bounds — the mirror of handle_trim_clear_both
        // / handle_trim_unset (has flags, selection flags, focus char, the
        // same invalidations and target render the unset path emits). Trim
        // is gesture-owned and excluded from undo/dirty, so no history or
        // dirty bookkeeping here. For the map-format conflict this is the
        // format-survives arm.
        app.trim.has_begin      = false;
        app.trim.has_end        = false;
        app.trim.begin_frame  = 0.0;
        app.trim.end_frame    = 0.0;
        app.trim_begin_selected = false;
        app.trim_end_selected   = false;
        app.last_selected_trim  = 0;
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    } else if (k == 'u') {
        // Rewind the offending commit. do_undo is history-symmetric (the
        // entry moves to the redo stack), so redo re-applies the commit and
        // re-fires the modal via do_redo's pending flag.
        undo.do_undo();
    } else {
        // All non-undo actions mutate the stores directly with NO history
        // push; deletions preserve time order and the reset insert lands at
        // the front of a sorted store, so no reorder_markers_by_time.
        const MarkerDefect& d = app.defect_series.defect;
        bool store_changed = false;

        switch (d.kind) {
        case MarkerDefectKind::CoincidentGroup: {
            // Delete target: in commit context, when the defect column's
            // last-selected index is a member of the group, delete that
            // member alone — it is the marker that moved onto the other.
            // The drag (set_single_selection at first motion, remapped
            // through the commit reorder), nudge (collapse to the focused
            // marker, remapped), create (drop_* selects the new index),
            // and enable-toggle (selection untouched, so the toggled
            // marker stays focused) paths all leave the touched marker
            // last-selected. The state paste does not — paste_apply sets
            // no selection and the switch to 'P' restores that column's
            // stashed slot, unrelated to the pasted markers — so paste
            // simply falls through to the later-markers rule below.
            std::vector<size_t> doomed;
            const int last_sel = column_last_selected(app, d.column);
            const bool focused_member =
                app.defect_series.commit_context && last_sel >= 0 &&
                std::find(d.indices.begin(), d.indices.end(),
                          static_cast<size_t>(last_sel)) != d.indices.end();
            if (focused_member) {
                doomed.push_back(static_cast<size_t>(last_sel));
            } else if (d.indices.size() > 1) {
                // Every member except the first: the later markers.
                doomed.assign(d.indices.begin() + 1, d.indices.end());
            }
            if (!doomed.empty()) {
                erase_column_indices(app, d.column, doomed);
                store_changed = true;
            }
            break;
        }
        case MarkerDefectKind::DanglingLabelRef: {
            if (d.indices.empty()) break;
            const int idx = static_cast<int>(d.indices.front());
            if (k == 'r') {
                // [R]eset: make the ref a plain 1.00 marker — clear
                // label_ref, own the tempo at 1.0, keep its time and
                // disabled flag. Refs cannot carry defs (WarpMarker: at
                // most one of label_def / label_ref is non-empty), so
                // there is no def to preserve.
                if (GuiWarpMarker* m = app.warpmarkers.marker_mut(idx)) {
                    m->label_ref.clear();
                    m->tempo_inherits = false;
                    m->tempo_base     = 1.0;
                    m->tempo_scale.clear();
                    store_changed = true;
                }
            } else {  // '\x7f': remove the ref marker
                erase_column_indices(app, d.column, d.indices);
                store_changed = true;
            }
            break;
        }
        case MarkerDefectKind::FirstMarkerGrammar: {
            // [R]eset is the hard reset, the single non-undo remedy for
            // the whole family (empty list, first off zero, disabled,
            // pass, label ref).
            const auto& mv = app.warpmarkers.markers();
            if (!mv.empty() && mv[0].time_frame == 0.0) {
                // Rewrite the zero marker in place to the plain default.
                // Rewriting covers enabling, so there is no separate
                // [E]nable option. Dropping a def with live refs is fine:
                // re-validation chains a DanglingLabelRef modal, and a def
                // on the zero marker is useless in practice.
                if (GuiWarpMarker* m = app.warpmarkers.marker_mut(0)) {
                    m->disabled       = false;
                    m->tempo_inherits = false;
                    m->tempo_base     = 1.0;
                    m->tempo_scale.clear();
                    m->label_ref.clear();
                    m->label_def.clear();
                    store_changed = true;
                }
            } else {
                // Insert the plain default 1.00 marker at 00:00.000 at the
                // front — the same shape as the missing-file seed at source
                // load ("0|1.00", file_loader.cpp). Every other
                // marker has time > 0 here, so insert_marker's lower_bound
                // lands it at index 0; the front insert shifts every warp
                // index by one, the same index-shaped staleness an erasure
                // causes, so the same clear applies.
                GuiWarpMarker nm;
                nm.time_frame   = 0.0;
                nm.tempo_inherits = false;
                nm.tempo_base     = 1.0;
                app.warpmarkers.insert_marker(std::move(nm));
                clear_column_selection_state(app, 'W');
                store_changed = true;
            }
            break;
        }
        }

        if (store_changed) {
            // History-less store mutation: the dirty flags are derived
            // purely from history distance from the last save, and a
            // mutation outside history makes that distance meaningless.
            // saved_valid = false pins all three dirty flags true until
            // the next save rebinds the reference — the truthful answer.
            app.history.saved_valid = false;
            undo.recompute_dirty();
            viewport.invalidate_waveform_area();
            viewport.invalidate_top_strip();
            viewport.invalidate_timestamp_area();
            target_render.trigger();
        }
    }

    // Re-validate from scratch with the same commit-context flag: the next
    // defect opens, or the series closes. NO auto-proceed after the series
    // closes — the user re-triggers whatever they were doing. Deliberate:
    // resolution returns control to the user; it never replays the action
    // the defect interrupted.
    open_defect_series(app.defect_series.commit_context);
}
