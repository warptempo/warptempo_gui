#include "flag_editor.h"

#include "frame_format.h"

#include "target_render.h"

#include "audio.h"
#include "input_handler.h"
#include "render.h"
#include "text_editor.h"
#include "warp_frame_map_view.h"
#include "warpmarkers_ops.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

// THE ITERATION BOUND'S GRAMMAR, READ BACK. It is FIXED-WIDTH — a sign, one
// integer digit, the point, two decimals (format_signed_delta_cents,
// warpmarkers.h, the one form a bound cell shows) — so this reader takes
// exactly those five bytes and nothing else: no surrounding whitespace, no
// leading zero, no second integer digit, ONE CANONICAL SPELLING PER VALUE.
// THE READER IS THE GRAMMAR'S OWNER, which is why it is exact rather than
// merely wide enough to catch ordinary typing: the field's five-byte cap
// (kMaxPendingCharsIterBound, text_editor.h) bounds what can be TYPED and
// this bounds what can be COMMITTED, so a pending that reached the commit by
// any other road meets the same judge. The conversion is digit-to-cents
// direct — no strtod, no doubles — and needs no overflow arm at all, one
// integer digit and two decimals never leaving [-9.99, +9.99]; the tempo
// window's own wall, refused below, is what holds a bound inside +/-3.75.
// ZERO HAS ONE SIGN AND IT IS '+': the writer spells zero `+0.00` (its sign is
// `cents < 0 ? '-' : '+'`), so `-0.00` is a SECOND spelling of a value that
// already has one and is refused here like any other non-canonical token.
bool parse_signed_2dp_cents(const std::string& v, int64_t& out) {
    if (v.size() != 5) return false;
    if (v[0] != '+' && v[0] != '-') return false;
    if (v[2] != '.') return false;
    const char digits[3] = {v[1], v[3], v[4]};
    for (const char c : digits) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    const int64_t mag = (digits[0] - '0') * 100 + (digits[1] - '0') * 10 +
                        (digits[2] - '0');
    if (mag == 0 && v[0] == '-') return false;   // -0.00: zero's second spelling
    out = (v[0] == '-') ? -mag : mag;
    return true;
}

} // namespace

// Flag-editor cluster: the marker lane's three editors (the flag's
// canonical-line editor, the measure editor, the iteration bound editor) —
// their enter / commit / exit paths — and the bpm-bracket editor session,
// reaching undo and viewport through the struct's reference members. The
// eligibility and flag-text helpers (iter_bracket_carrier,
// iter_popup_eligible_marker, bpm_popup_eligible_marker, ...) live in
// warpmarkers.h alongside effective_disabled, so this TU sees them via
// #include "warpmarkers.h".

void GuiFlagEditor::exit_top_flag_edit_no_commit() {
    if (!text_editor::is_active(app.top_flag_editor)) return;
    // A close while the editor is RED un-flashes the marker's STEM, and a stem
    // is a waveform pixel (the flash reaches it since 2026-08-01 —
    // GuiPaintHandler::paint_marker_stems), so the strip repaint below is not
    // the whole damage. Gated on the flash rather than paid on every close: the
    // ordinary close changes no waveform pixel at all. This is the chokepoint
    // the POINTER close comes through (any left press with the editor open
    // closes it, input_pointer.cpp) — the keyboard flips are edge-damaged at
    // handle_top_flag_editor_key, and the two overlap harmlessly on Esc/Ctrl+Q.
    // (Kind-exact: this teardown serves the BPM bracket session too, and that
    // editor's flash recolors the modal dialog's FIELD, with no stem behind it.)
    if (app.top_flag_editor.red &&
        app.top_flag_editor.kind == text_editor::Kind::FlagPayload) {
        viewport.invalidate_waveform_area();
    }
    text_editor::deactivate(app.top_flag_editor);
    viewport.invalidate_top_strip();
}

// Shared core for the enter-editor flows. Wrappers below
// (enter_top_flag_edit, enter_bpm_edit) own the kind-specific eligibility
// gates and seed-text builders, then delegate here. The live open routes
// are: Enter on the focused marker with the payload addressed, the flag
// box's double-click (both -> enter_top_flag_edit), and the BPM editor
// open (enter_bpm_edit). Every route opens the editor with its
// SEEDED content fully selected (open-selected) — typing replaces
// wholesale, bare Left/Right collapse to the extremes — so there is no
// clicked-glyph caret to seat; a specific caret spot is a click inside the
// already-open editor (input_pointer's F2.1 path). A blank seed selects
// nothing and rests at caret 0, the same rule degenerating for the
// blank-seeded bottom editors.
void GuiFlagEditor::enter_text_edit(int idx,
                                    text_editor::Kind kind,
                                    std::string initial_pending) {
    if (idx < 0) return;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return;

    const bool same_target =
        text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == kind &&
        app.top_flag_editor.target == idx;

    if (same_target) {
        // Re-open on the active editor's own target: preserve pending
        // text and any in-progress state, just repaint.
        viewport.invalidate_top_strip();
        return;
    }

    // Target-switching path. Single-select the new editor target so the
    // marker-column outline follows it, and LAND the playhead on it: the marker
    // lane owns the playhead (the rule is stated in full at
    // land_playhead_on_marker, input_pointer.cpp), and an editor open is exactly
    // a route that hands the lane a new focus. This brings the one
    // set_single_selection caller that had opted out back onto the pointer
    // clicks' adjacency convention (selection then land, on the next line).
    // Reached in the WARP column alone — all three open routes gate the marker
    // view to it — so `idx` resolves against the warp store this helper reads.
    // BOTH AUDIO VIEWS SINCE 2026-08-24 (the payload editor is the home-view
    // binding's fifth ruled exception; the inventory is at
    // active_column_authoring_allowed, app_state.h), so the domain is no
    // longer the identity one: land_playhead_on_marker translates through the
    // ACTIVE domain itself (source_frame_to_active_domain at its body), so the
    // land is right in target view with nothing to add here.
    // THE LAND HIDES THE TRIM REGION OVERLAY: an open moves the playhead onto
    // one marker, so the overlay goes with it — unconditionally, never gated on
    // the land having moved anything, and discarding nothing (the trim stands
    // and a later `[` re-shows the same overlay). Since 2026-08-19 that is the
    // LAND'S own act rather than a second call here (the rule and its two
    // movement owners are at clear_region_highlight, input_handler.h). This one
    // chokepoint covers every open and retarget (bare Return, the pointer
    // double-click, `m`, a pointer retarget of the live editor); `m` re-derives
    // NOTHING after it since
    // 2026-07-30 (the extent owner died with the SPAN FORM). The same_target
    // early return above skips both: the
    // playhead is already there from the first open and nothing changed.
    selection.set_single_selection(idx);
    land_playhead_on_marker(app, audio, viewport, idx);

    // Discard any prior edit silently before switching targets.
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.target != idx) {
        text_editor::deactivate(app.top_flag_editor);
    }
    text_editor::enter(
        app.top_flag_editor, idx,
        std::move(initial_pending),
        kind);

    // Open-selected: fully select the seeded content (mirrors Ctrl+A's
    // anchor=0 / cursor=end assignments) so the first keystroke replaces it
    // wholesale and bare Left/Right collapse to the extremes; a blank seed
    // selects nothing and rests at caret 0. text_editor::enter already
    // refreshed blink_epoch, so no touch_blink is needed here.
    if (!app.top_flag_editor.pending.empty()) {
        app.top_flag_editor.selection_anchor = 0;
        app.top_flag_editor.cursor_pos =
            static_cast<int>(app.top_flag_editor.pending.size());
    } else {
        app.top_flag_editor.selection_anchor = -1;
        app.top_flag_editor.cursor_pos = 0;
    }

    viewport.invalidate_top_strip();
}

// The two flag-editor open routes (bare Return with the payload addressed,
// and the flag box's double-click) end here. NO PLAYBACK STOP, and that is
// an explicit exemption rather than an omission: the top-strip flag editor
// is the one modal surface that keeps playing, so a live audition survives
// the open. The decision and its rationale are recorded at
// GuiPlaybackLifecycle::stop_playback_for_modal_open, the one owner of the
// modal-open stop the dialog surfaces call.
//
// THE SEED IS THE PLAIN COMPOSER'S LINE in every mode (architect 2026-09-05):
// the same tempo, scale and label the flag paints, and never a bracket. The
// iteration bounds are the two cells' own, each with its own editor
// (enter_iter_bound_edit), so this editor neither shows nor writes them; its
// commit's one contact with the bracket is the carrier-loss clear.
void GuiFlagEditor::enter_top_flag_edit(int idx) {
    if (idx < 0) return;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return;
    this->enter_text_edit(idx, text_editor::Kind::FlagPayload,
                          flag_text(mv, idx));
}

// The contract is at the declaration; this is enter_measure_edit's mechanics
// on the warp store with the cell's own eligibility in front.
void GuiFlagEditor::enter_iter_bound_edit(int idx, MarkerCell side) {
    if (idx < 0) return;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return;
    if (side != MarkerCell::Lower && side != MarkerCell::Upper) return;
    // NO CELL, NO EDITOR: the cell paints on exactly the markers the sweep
    // reads while the mode is on (warp_iter_cells, render.cpp, off the same
    // predicate), so an editor opens on exactly those. The callers card the
    // kind refusal ahead of this belt; a mode-off call cannot arrive, the
    // axis falling back to the payload with the mode.
    if (!app.iteration_mode_enabled) return;
    if (!iter_popup_eligible_marker(mv, idx)) return;

    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::IterBound &&
        app.top_flag_editor.target == idx &&
        iter_bound_editor_side(app.top_flag_editor) == side) {
        // Re-open on the live session's own cell: preserve the pending text
        // and any in-progress state, just repaint (the payload editor's rule).
        viewport.invalidate_top_strip();
        return;
    }

    // The focus repaired, then single-selected and landed — the measure
    // editor's open verbatim (its comments carry the why). The select resets
    // the addressed cell to the payload through the Selection chokepoint, so
    // the cell is written AFTER it: an editor open seats the cell it edits.
    selection.repair_last_selected();
    selection.set_single_selection(idx);
    land_playhead_on_marker(app, audio, viewport, idx);
    app.addressed_cell = side;

    if (text_editor::is_active(app.top_flag_editor)) {
        text_editor::deactivate(app.top_flag_editor);
    }
    // THE SEED IS THE CELL'S OWN TOKEN — the one spelling the cell paints
    // (format_iter_bound_cell, `+0.00` on a blank bracket), so what the cell
    // shows and what its editor opens with cannot differ.
    text_editor::enter(app.top_flag_editor, idx,
                       format_iter_bound_cell(mv[static_cast<size_t>(idx)],
                                              side),
                       text_editor::Kind::IterBound,
                       /*iter_upper=*/side == MarkerCell::Upper);

    // Open-selected, the family's rule: the token is fully selected so the
    // first keystroke replaces it wholesale. The seed is never empty here.
    app.top_flag_editor.selection_anchor = 0;
    app.top_flag_editor.cursor_pos =
        static_cast<int>(app.top_flag_editor.pending.size());

    viewport.invalidate_top_strip();
}

// The contract is at the declaration. THE REFUSAL IS THE TOP-STRIP FAMILY'S
// OWN SHAPE: `red = true`, a top-strip repaint, one stderr line, a normal
// card carrying the same composed sentence, and the session left standing
// with the offending text in place. TYPED INPUT GATES LOUD: a bound typed
// here is authored input arriving at its own surface, so the walls the
// arrows' step clamps at silently (iter_bound_step_landing) are refusals
// here, each named — the grammar, the partner bound, the tempo window.
void GuiFlagEditor::commit_iter_bound_edit() {
    if (!text_editor::is_active(app.top_flag_editor)) return;
    if (app.top_flag_editor.kind != text_editor::Kind::IterBound) return;
    const int idx = app.top_flag_editor.target;
    const MarkerCell side = iter_bound_editor_side(app.top_flag_editor);
    const std::string next = app.top_flag_editor.pending;

    const auto& mv_const = app.warpmarkers.markers();
    // The target may have gone out from under the editor, or stopped being a
    // carrier (the store is frozen under the modal editor, so neither can
    // happen today; the belts are the payload commit's own): drop the edit.
    if (idx < 0 || idx >= static_cast<int>(mv_const.size()) ||
        !iter_bracket_carrier(mv_const[static_cast<size_t>(idx)])) {
        this->exit_top_flag_edit_no_commit();
        return;
    }
    const GuiWarpMarker& live = mv_const[static_cast<size_t>(idx)];

    auto refuse = [&](const std::string& why) {
        app.top_flag_editor.red = true;
        viewport.invalidate_top_strip();
        // ONE COMPOSER, TWO READERS: the stderr line keeps the offending
        // token after the sentence; the card does not, that text standing in
        // the red field the refusal leaves.
        const std::string refusal = "Range bound rejected: " + why;
        std::fprintf(stderr, "warptempo_gui: %s: %s\n",
                     refusal.c_str(), next.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
    };

    std::vector<GuiWarpMarker> proposed = mv_const;
    GuiWarpMarker& m = proposed[static_cast<size_t>(idx)];
    if (next.empty()) {
        // AN EMPTY COMMIT CLEARS THE WHOLE BRACKET: a bracket is a pair and
        // one bound alone is not representable, so emptying either cell is
        // the removal — the measure's own empty-removes rule.
        m.iter_start_cents.reset();
        m.iter_end_cents.reset();
    } else {
        int64_t value = 0;
        if (!parse_signed_2dp_cents(next, value)) {
            refuse("a bound is a sign, one digit and two decimals, as +0.00");
            return;
        }
        // THE WALLS, the landing owner's two, refused rather than clamped:
        // the tempo window clamp_iter_bracket_to_tempo_bracket states —
        // every sweep cell renders base + delta, so the base plus either
        // bound must stay inside the tempo bracket — and the partner bound,
        // the lower never above the upper and the upper never below the
        // lower (0 for a blank bracket, the step's own start).
        const int64_t lo_wall = kTempoMinCents - live.tempo_cents;
        const int64_t hi_wall = kTempoMaxCents - live.tempo_cents;
        if (value < lo_wall || value > hi_wall) {
            refuse("the base tempo plus the bound leaves the tempo bracket [" +
                   format_tempo_cents(kTempoMinCents) + ", " +
                   format_tempo_cents(kTempoMaxCents) + "]");
            return;
        }
        const int64_t partner = side == MarkerCell::Upper
                                    ? live.iter_start_cents.value_or(0)
                                    : live.iter_end_cents.value_or(0);
        if (side == MarkerCell::Lower && value > partner) {
            refuse("the lower bound cannot rise above the upper");
            return;
        }
        if (side == MarkerCell::Upper && value < partner) {
            refuse("the upper bound cannot fall below the lower");
            return;
        }
        // THE ONE WRITE SITE: the addressed side takes the value, the partner
        // keeps what it had, and a pair landing on two zeroes clears — the
        // same rule the arrows' step writes under, so a typed +0.00 pair and
        // a stepped one mean one thing.
        iter_bound_step_write(m, side, value);
    }

    // A COMMIT THAT CHANGES NOTHING IS NOT A CHANGE: no undo entry, no store
    // bump — the shape every no-op commit in the product takes.
    if (m.iter_start_cents == live.iter_start_cents &&
        m.iter_end_cents   == live.iter_end_cents) {
        this->exit_top_flag_edit_no_commit();
        return;
    }

    // ONE BRACKET-ONLY ENTRY (affects_persistence false — session-only
    // fields, never serialized, so the dirty dot stays where it is), carrying
    // the ADDRESSED CELL this editor seated at its open, so undoing the commit
    // leaves the focus bright on the cell it was typed into. The snapshot is
    // taken before the write, the store's own convention.
    std::vector<GuiWarpMarker> pre_state = mv_const;
    app.warpmarkers.markers_mut() = std::move(proposed);
    undo.push_undo_iter_bracket(std::move(pre_state));
    undo.recompute_dirty();

    // NO RENDER AND NO MAP REBUILD: a bracket is not a map input (excluded
    // from build_warp_frame_map and the render recipe alike), so the cell is
    // the only thing that moved and the strip is the only damage.
    text_editor::deactivate(app.top_flag_editor);
    viewport.invalidate_top_strip();
}

// THE MEASURE EDITOR'S OPEN. The contract is at the declaration; this is the
// mechanics, and they are enter_text_edit's shape written for TWO stores
// instead of one.
//
// NO PLAYBACK STOP, the top-strip family's recorded exemption: this editor is
// keyboard-modal and pointer/wheel-transparent exactly as the payload editor
// is, so a live audition survives the open. The decision table is at
// GuiPlaybackLifecycle::stop_playback_for_modal_open, which this surface — like
// its sibling — deliberately does not call.
void GuiFlagEditor::enter_measure_edit(char column, int idx) {
    if (idx < 0) return;
    const bool phase = (column == 'P');
    const int  n = phase
        ? static_cast<int>(app.phaseresetmarkers.markers().size())
        : static_cast<int>(app.warpmarkers.markers().size());
    if (idx >= n) return;

    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::MeasureText &&
        app.top_flag_editor.target == idx) {
        // Re-open on the live session's own target: preserve the pending text
        // and any in-progress state, just repaint (the payload editor's rule).
        viewport.invalidate_top_strip();
        return;
    }

    // THE FOCUS IS REPAIRED FIRST, the bare-Return arm's twin (input_handler.cpp
    // — a stale focus outside the selection moves to the largest remaining
    // member before anything reads it). The key ROUTE repairs before resolving
    // `idx` from the focus; this second call is the pointer route's, where the
    // index came from a hit test, and it is idempotent on an already-consistent
    // focus.
    selection.repair_last_selected();

    // Target-switching: single-select the new target so the marker column's
    // outline follows it, and LAND the playhead on it — the marker lane owns
    // the playhead, and an editor open hands the lane a new focus (the rule is
    // at land_playhead_on_marker, input_pointer.cpp). Both are column-agnostic:
    // selection indices and the land alike resolve against the ACTIVE column's
    // store, which is the column this open was called with.
    selection.set_single_selection(idx);
    land_playhead_on_marker(app, audio, viewport, idx);
    // THE OPEN SEATS THE CELL IT EDITS (architect 2026-09-05): the select
    // above reset the addressed cell to the payload through the Selection
    // chokepoint, and the measure box is the cell this editor is, so it is
    // written here, behind the select — the bright cell follows the field.
    app.addressed_cell = MarkerCell::Measure;

    // Discard any prior edit silently before switching surfaces.
    if (text_editor::is_active(app.top_flag_editor)) {
        text_editor::deactivate(app.top_flag_editor);
    }
    // THE SEED IS THE MARKER'S OWN MEASURE — the only measure there is. A
    // display-time inheritance down the label cascade existed for one day and
    // the architect reversed it on 2026-08-20 (the field is a POSITION in the
    // score, wrong at a reference sitting bars later), so what a flag paints
    // and what this editor opens with are the same one field on both columns.
    // That the seed is the marker's OWN is now trivially true and is still
    // spelled here, because it is what makes an empty commit a REMOVAL of this
    // marker's measure and nothing else's.
    const std::string seed = phase
        ? app.phaseresetmarkers.markers()[static_cast<size_t>(idx)].measure
        : app.warpmarkers.markers()[static_cast<size_t>(idx)].measure;
    text_editor::enter(app.top_flag_editor, idx, seed,
                       text_editor::Kind::MeasureText);

    // Open-selected, the family's rule: the seeded text is fully selected so
    // the first keystroke replaces it wholesale; a blank seed selects nothing
    // and rests at caret 0.
    if (!app.top_flag_editor.pending.empty()) {
        app.top_flag_editor.selection_anchor = 0;
        app.top_flag_editor.cursor_pos =
            static_cast<int>(app.top_flag_editor.pending.size());
    } else {
        app.top_flag_editor.selection_anchor = -1;
        app.top_flag_editor.cursor_pos = 0;
    }

    viewport.invalidate_top_strip();
}

// THE MEASURE COMMIT, and it HAS a validator arm (2026-08-20, with the field's
// rebrand from the free-text comment): the measure is a GRAMMAR, so the buffer
// is judged HERE against validate_marker_measure — the same one judge the two
// file parsers and both history delta extractors use, which is what keeps
// "loadable iff it commits" exact rather than merely likely. The type-time
// filters cannot carry the grammar the way they carried the old byte class: a
// half-typed `12 4` is a legal prefix of a legal token, so refusal belongs at
// the commit and nowhere earlier. There is deliberately NO Kind-dependent
// keystroke filter; typing stays free and the commit decides.
//
// THE REFUSAL IS THIS EDITOR'S OWN SHAPE, not a dialog's: `red = true`, a
// top-strip repaint, one stderr line, A NORMAL CARD carrying that same
// composed sentence (2026-08-30 — a red field says only THAT it refused), and
// RETURN WITHOUT DEACTIVATING, so the
// session stands with the offending text in place for correction. The damage
// is the top strip alone — the stem flash that the FlagPayload refusal drives
// is gated on that Kind at the painter, so a MeasureText red never reaches a
// waveform pixel and there is no waveform-area edge to invalidate.
//
// AN EMPTY BUFFER REMOVES THE MEASURE and is exempt from the grammar (the
// validator has no "empty is fine" reading — an empty tail on disk is
// load-fatal), which is what makes the bare ` //` suffix a state the GUI can
// never write.
void GuiFlagEditor::commit_measure_edit() {
    if (!text_editor::is_active(app.top_flag_editor)) return;
    if (app.top_flag_editor.kind != text_editor::Kind::MeasureText) return;
    const int idx = app.top_flag_editor.target;
    const std::string next = app.top_flag_editor.pending;

    if (!next.empty()) {
        std::string measure_err;
        if (!validate_marker_measure(next, measure_err)) {
            app.top_flag_editor.red = true;
            viewport.invalidate_top_strip();
            // ONE COMPOSER, TWO READERS (architect 2026-08-30): the sentence is
            // built once and read by the stderr line and by the card. The
            // stderr line keeps the offending token after it; the card does
            // not, that text being on screen in the red field the refusal
            // leaves standing.
            const std::string refusal = "Measure rejected: " + measure_err;
            std::fprintf(stderr, "warptempo_gui: %s: %s\n",
                refusal.c_str(), next.c_str());
            notifications.notify(AppState::NotificationClass::Normal, refusal);
            return;
        }
    }
    // THE COLUMN IS READ LIVE AND IT IS THE OPEN'S COLUMN, because the view
    // CANNOT MOVE under an open session: every column-switching key (`p`, `t`,
    // the 1/2/3 selectors, Ctrl+Tab) is dropped at the keyboard-modal gate
    // while any editor stands, and every column-switching BUTTON acts at the
    // LIFT whose own PRESS already closed this editor
    // (close_top_flag_editor_for_outside_press). So the session needs no stored
    // column of its own.
    const bool phase = (app.active_markers_view == 'P');

    // The target may have gone out from under the editor (an undo or a delete
    // while it stood): drop the edit, exactly as the payload commit does.
    const int n = phase
        ? static_cast<int>(app.phaseresetmarkers.markers().size())
        : static_cast<int>(app.warpmarkers.markers().size());
    if (idx < 0 || idx >= n) {
        this->exit_top_flag_edit_no_commit();
        return;
    }

    // A COMMIT THAT CHANGES NOTHING IS NOT A CHANGE: no undo entry, no dirty
    // bit, no store bump — the shape every no-op commit in the product takes.
    const std::string& before = phase
        ? app.phaseresetmarkers.markers()[static_cast<size_t>(idx)].measure
        : app.warpmarkers.markers()[static_cast<size_t>(idx)].measure;
    if (before == next) {
        this->exit_top_flag_edit_no_commit();
        return;
    }

    // ONE UNDO ENTRY, affects_persistence TRUE: a measure is serialized content
    // and its edit dirties the tab like any other authored change. The snapshot
    // is taken before the write, the store's own convention.
    if (phase) {
        std::vector<GuiPhaseResetMarker> pre = app.phaseresetmarkers.markers();
        GuiPhaseResetMarker* m = app.phaseresetmarkers.marker_mut(idx);
        if (m) m->measure = next;
        undo.push_undo_phase_reset(std::move(pre));
    } else {
        std::vector<GuiWarpMarker> pre = app.warpmarkers.markers();
        GuiWarpMarker* m = app.warpmarkers.marker_mut(idx);
        if (m) m->measure = next;
        undo.push_undo_warp(std::move(pre));
    }
    undo.recompute_dirty();

    // NO RE-RENDER AND NO MAP REBUILD: a measure reaches neither the engine nor
    // the render fingerprint (the field's own contract at WarpMarker::measure),
    // so the box is the only thing that moved and the strip is the only damage.
    text_editor::deactivate(app.top_flag_editor);
    viewport.invalidate_top_strip();
}

// Validate `pending` as a single canonical line and, on success, write
// the parsed marker's fields back onto markers_[idx]. Cascade-renames
// label_def changes onto every other marker that referenced the old
// name. Pushes one undo entry covering all touched markers.
//
// On failure: sets `red`, leaves pending/cursor intact, leaves the
// editor active.
void GuiFlagEditor::commit_top_flag_edit() {
    if (!text_editor::is_active(app.top_flag_editor)) return;
    const int idx = app.top_flag_editor.target;
    const auto& mv_const = app.warpmarkers.markers();
    if (idx < 0 || idx >= static_cast<int>(mv_const.size())) {
        // Editor target became invalid (e.g. the marker was removed
        // underneath the editor by an undo or delete). Drop edit.
        this->exit_top_flag_edit_no_commit();
        return;
    }

    // The buffer is the plain payload: no bracket rides in it (the bounds
    // are the cells' own, each with its own editor), so
    // parse_single_canonical_line reads it as it reads a sidecar line.
    const std::string& payload = app.top_flag_editor.pending;

    // Assemble the parse candidate in SERIALIZER form. The editor holds the
    // PAYLOAD alone, so the two fields ahead of the pipe come from the marker's
    // own state rather than from any typed bytes: `disabled` from the marker's
    // flag, and the position as the integer frame text the canonical line
    // grammar expects (format_authored_frame, frame_format.h — never the
    // MM:SS.mmm form the GUI shows elsewhere).
    std::string candidate;
    if (mv_const[idx].disabled) candidate += '#';
    candidate += format_authored_frame(mv_const[idx].time_frame);
    candidate += '|';
    candidate += payload;

    GuiWarpMarker parsed;
    std::string err;
    auto result = warpmarkers_internal::parse_single_canonical_line(candidate);
    bool ok = result.has_value();
    if (ok) {
        static_cast<WarpMarker&>(parsed) = std::move(*result);
    } else {
        err = std::move(result.error());
    }

    // Cross-marker check (edit target excluded). A dangling label_ref is
    // deliberately not gated here — the parser resolver normalizes a
    // dangling ref to a plain 1.00 owner at render/preview time (one
    // stderr line per timestamp); def uniqueness is a load rule, so the
    // editor still gates it.
    if (ok && !parsed.label_def.empty()) {
        for (int i = 0; i < static_cast<int>(mv_const.size()); ++i) {
            if (i == idx) continue;
            if (mv_const[i].label_def == parsed.label_def) {
                ok = false;
                err = "duplicate label definition: " + parsed.label_def;
                break;
            }
        }
    }
    // Removing a label_def whose refs remain is legal: the refs go
    // dangling, keep their `a.NN` payload (so the file still parses),
    // and the parser resolver normalizes each to a plain 1.00 owner at
    // render/preview time (one stderr line per timestamp). No editor-side
    // gate; the rename cascade below stays, scoped to non-empty new names.
    if (!ok) {
        app.top_flag_editor.red = true;
        viewport.invalidate_top_strip();
        const std::string refusal = "Edit rejected: " + err;
        std::fprintf(stderr, "warptempo_gui: %s\n", refusal.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
        return;
    }

    // No first-marker special case: a marker at time 0 accepts any payload
    // the grammar allows — pass, label ref, label def. The parser resolver
    // normalizes the frame-0 arrangement at render/preview time (a missing
    // or ambiguous frame-0 owner becomes a plain 1.00 owner, one stderr
    // line per timestamp), never this editor.

    std::vector<GuiWarpMarker> proposed = mv_const;
    GuiWarpMarker& m = proposed[idx];
    const std::string old_def = m.label_def;
    const std::string new_def = parsed.label_def;

    // Snapshot the canonical (serialized) fields before writing so we can
    // tell whether the engine/dirty state actually moved. A bracket-only
    // commit (the carrier-loss clear or the clamp moving a bound under an
    // unchanged line) does not mark dirty — its undo entry pushes with
    // affects_persistence=false, which recompute_dirty honors — while the
    // commit tail below still repaints and triggers unconditionally like any
    // store mutation.
    const GuiWarpMarker before = m;

    // Time stays locked; preserve it (the candidate assembled above carried
    // the marker's own frame, so the parse produced the same value, but be
    // explicit).
    const int64_t preserved_time = m.time_frame;

    // Cache-free: typing `pass` writes inert defaults into
    // tempo_cents/tempo_scale; typing an explicit tempo writes the
    // owned value. label_def is independent of tempo source —
    // `pass:LABEL` carries a def at this position while inheriting
    // the tempo from a prior owning marker.
    if (parsed.tempo_inherits) {
        m.tempo_inherits = true;
        m.tempo_cents    = 100;
        m.tempo_scale.reset();
        m.label_def      = parsed.label_def;
        m.label_ref.clear();
    } else {
        m.tempo_inherits = false;
        m.tempo_cents    = parsed.tempo_cents;
        m.tempo_scale    = parsed.tempo_scale;
        m.label_def      = parsed.label_def;
        m.label_ref      = parsed.label_ref;
    }
    m.time_frame = preserved_time;
    // disabled is not the editor's field — the candidate carried the marker's
    // own bit, parse_single_canonical_line populated it; reapply.
    m.disabled      = parsed.disabled;
    // THE MEASURE IS NOT THIS EDITOR'S and is preserved by construction: `m`
    // is the live marker copied whole, and no line above writes the field. The
    // candidate parsed at accept_measure = false, so `parsed.measure` is
    // always empty and must never be assigned from — a ` //` typed into the
    // payload buffer is a grammar error the parse already red-flashed. The
    // measure has its own editor (Kind::MeasureText).

    // Cascade rename: if label_def changed to another NON-EMPTY name,
    // every other marker that referenced old_def gets its ref updated to
    // the new name. Removal (new_def empty) deliberately does NOT
    // cascade: clearing a ref would leave that marker serializing as an
    // owning 0.00 line (a pure ref parses with tempo_cents 0), which the
    // parser rejects on reload. Dangling refs, by contrast, load fine and
    // normalize to a plain 1.00 owner at the render boundary — so the
    // refs are left pointing at the removed name.
    int n_refs_renamed = 0;
    if (!old_def.empty() && !new_def.empty() && old_def != new_def) {
        for (int i = 0; i < static_cast<int>(proposed.size()); ++i) {
            if (i == idx) continue;
            if (proposed[i].label_ref == old_def) {
                proposed[i].label_ref = new_def;
                ++n_refs_renamed;
            }
        }
    }

    // THE BRACKET IS THE STORE'S, NOT THIS EDITOR'S: no line above writes a
    // bound (the bounds are the cells' own, each with its own editor), and
    // the two writes below are the store's rules about a bracket the marker
    // already carries.
    //
    // Invariant: a committed NON-CARRIER never keeps a bracket. A bracket
    // exists only on a carrier (iter_bracket_carrier, warpmarkers.h); a
    // commit that makes the marker a non-carrier (a pass, or a &ref) clears
    // both bounds unconditionally — in the mode and out of it, so a mode-off
    // pass conversion of an undo-restored bracketed owner also drops the
    // fields. This mirrors Ctrl+N's owner->pass / ref->pass carrier-loss
    // clears; undo is the sole sanctioned route that resurrects a cleared
    // bracket. A commit that DISABLES the owner (the `#`) is no loss: the
    // bracket stays on it dormant — off the flag, out of the sweep and
    // reachable by no editor until re-enabled — which is why the test below
    // is the carrier and not the sweep's eligibility (R-12, 2026-09-02).
    if (!iter_bracket_carrier(m)) {
        m.iter_start_cents.reset();
        m.iter_end_cents.reset();
    }
    // THE BRACKET RIDES ITS BASE: this commit may have moved the tempo under a
    // bracket it did not type. Fold the surviving bracket onto the committed
    // base (the one owner, clamp_iter_bracket_to_tempo_bracket in
    // warpmarkers.h). Placed before the change compares below, so a clamp
    // that moves a bound counts as a bracket change and earns its repaint
    // and its undo entry.
    clamp_iter_bracket_to_tempo_bracket(m);

    // Did any serialized field change? Cascade renames imply a label_def
    // change, already covered by the field compare below.
    const bool canonical_changed =
        m.tempo_inherits != before.tempo_inherits ||
        m.tempo_cents    != before.tempo_cents ||
        m.tempo_scale    != before.tempo_scale ||
        m.label_def      != before.label_def ||
        m.label_ref      != before.label_ref ||
        m.disabled       != before.disabled ||
        n_refs_renamed > 0;

    // Did the session-only iteration bracket move? Compare the ACTUAL
    // final fields: this is correct on every path, including an
    // ineligibility clear that resets the bounds on a commit whose canonical
    // fields did not move (the mode-off pass conversion).
    // optional<int64_t> equality: two bounds are equal when both are
    // nullopt or when the held cents compare equal. A bracket-only edit
    // does not mark dirty (iter values are session-only), but it is still a
    // real undoable change — the snapshot restores the iter values — so its
    // push must not be skipped.
    const bool bracket_changed =
        m.iter_start_cents != before.iter_start_cents ||
        m.iter_end_cents   != before.iter_end_cents;

    // An undo entry represents a state change, not a gesture. A commit
    // that moves neither a canonical field nor the bracket is a no-op:
    // it pushes nothing and touches no dirty/render state. The live-
    // vector assignment (which bumps the warp generation so the flag
    // cache repaints) and the editor deactivation below still run.
    const bool store_changed = canonical_changed || bracket_changed;

    // Capture pre-state for undo BEFORE mutating.
    std::vector<GuiWarpMarker> pre_state = mv_const;
    app.warpmarkers.markers_mut() = std::move(proposed);

    if (n_refs_renamed > 0) {
        std::fprintf(stderr,
            "warptempo_gui: Renamed label_def '%s' -> '%s'; "
            "updated %d refs\n",
            old_def.c_str(), new_def.c_str(), n_refs_renamed);
    }

    if (store_changed) {
        undo.push_undo_warp(std::move(pre_state),
                            /*affects_persistence=*/canonical_changed);
    }

    text_editor::deactivate(app.top_flag_editor);

    if (!store_changed) return;

    // Unconditional by ruling — rationale at GuiTargetRender::trigger. Any
    // store change repaints and triggers, a bracket-only commit included (its
    // undo entry already did): recompute_dirty derives dirty purely from
    // affects_persistence entries, so a bracket-only push (affects_persistence
    // false) leaves dirty untouched, and the trigger's re-derive is
    // identity-unchanged for session-only iteration scratch (excluded from the
    // render recipe) and lands on dispatch_render_now's reuse rungs.
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    // THE TARGET-VIEW TAIL (architect 2026-08-24). The payload editor is a
    // VALUE surface — tempo, label_def / label_ref, per-marker scale, the
    // disabled bit — and never a placement one, so it is a
    // member of the warp status/value family admitted in W+target, and it owes
    // that family's contract: the contract is stated once at the head of
    // warpmarkers_ops.cpp, and the target-view re-warp inventory it joins is
    // owned by Viewport::kick_waveform_sync's declaration (viewport.h). This
    // commit is the worked case for the re-land: a renamed label_def reprices
    // every reference to it, including references EARLIER in the timeline,
    // whose spans then change duration and shift everything downstream — the
    // edited marker's own image included — so the FOCUS (which is this
    // editor's target, single-selected at the open) re-lands on its post-commit
    // image through reseat_playhead_to, a TRANSLATION that must leave the trim
    // region overlay standing (the rule at clear_region_highlight,
    // input_handler.h). SOURCE VIEW NEEDS NOTHING: identity domain, no image
    // moves.
    // AND canonical_changed IS THE SECOND TERM, which is where this site
    // differs from its three siblings: they write nothing BUT map inputs,
    // while this commit can land a BRACKET-ONLY change (the carrier-loss
    // clear, the clamp) — session-only fields, excluded from
    // build_warp_frame_map and from the render recipe alike — which moves
    // no image and would make the kick a wasted
    // synchronous plate render and the re-land a write of the value the
    // playhead already holds. canonical_changed is exactly the map-input set
    // (tempo_inherits / tempo_cents / tempo_scale / label_def / label_ref /
    // disabled / renamed refs; position is not editable here), so it is the
    // honest gate and it is the same predicate the pre-2026-08-24 branch used
    // before that branch went unreachable and was deleted. It keeps its other
    // job as the undo push's affects_persistence gate above. The trigger keeps
    // its own store_changed gating (it re-derives identity-unchanged for an
    // iter-bracket-only commit and lands on the reuse rungs).
    if (app.active_audio_view == 'T' && canonical_changed) {
        viewport.kick_waveform_sync();
        const auto& mv_post = app.warpmarkers.markers();
        if (idx >= 0 && idx < static_cast<int>(mv_post.size())) {
            viewport.reseat_playhead_to(source_frame_to_active_domain(
                app, audio, mv_post[idx].time_frame));
        }
    }
    target_render.trigger();
}

// Wipe every marker's session-only iter bracket. The single clear every
// iteration-mode exit route shares: the `i` toggle's turning-off branch,
// enter_bpm_mode's forced iter-off, and the iteration sweep's success tail —
// exiting the mode is the clear on every route, so a bracket exists only while
// the mode paints it on the flags. THE S->T AUDIO-VIEW TOGGLE IS NO LONGER ONE
// OF THEM (2026-08-07): iteration mode is target-legal, so entering target view
// neither exits the mode nor clears anything (the record is at
// switch_active_audio_view_to, input_handler.cpp). Two of the three
// surviving callers can now run in TARGET view, where the write is the granted
// home-view-binding exception argued at the sweep's tail
// (run_iteration_sweep_render, input_key_dispatch.cpp).
// THE LOAD IN PLACE IS NOT A ROUTE EITHER (architect 2026-09-02): iteration
// mode is where you stand, so apply_recipe_in_place leaves the mode bit alone
// and resets no bracket — the incoming set carries none by construction, and
// undo restores the outgoing set with its brackets under a mode that never
// changed (the record is at that body, input_key_dispatch.cpp).
// Pushes one undo entry when something was cleared and no-ops otherwise, so
// a bracketless exit leaves the undo stack untouched; plain undo is
// deliberately ungated and may restore a previously accepted bracket set.
// Callers own the mode-flag flip and the repaint invalidation.
// AND IT PUTS AN ADDRESSED BOUND CELL BACK ON THE PAYLOAD (architect
// 2026-09-04): the cells go with the mode, so a Lower or Upper axis
// (AppState::addressed_cell) falls back to Payload here, ahead of the
// bracket test, because this body is the one thing every exit from the mode
// runs — there is no single mode setter, the three writers of the off edge
// each flip the bit themselves after calling this — so a step outside the
// mode can only ever be the tempo step. An addressed MEASURE is left alone:
// that cell is not the mode's. History-less: the axis is a session address,
// not content, and the snapshot below carries no such field.
void GuiFlagEditor::wipe_iter_state() {
    if (app.addressed_cell == MarkerCell::Lower ||
        app.addressed_cell == MarkerCell::Upper) {
        app.addressed_cell = MarkerCell::Payload;
    }
    auto& mv = app.warpmarkers.markers_mut();
    bool any = false;
    for (const auto& m : mv) {
        if (m.iter_start_cents.has_value() || m.iter_end_cents.has_value()) {
            any = true;
            break;
        }
    }
    if (!any) return;
    std::vector<GuiWarpMarker> pre_state = app.warpmarkers.markers();
    for (auto& m : mv) {
        m.iter_start_cents.reset();
        m.iter_end_cents.reset();
    }
    undo.push_undo_warp(std::move(pre_state),
                        /*affects_persistence=*/false);
}

// Wipe every marker's session-only bpm state — owner flag, beats, bracket
// bounds, and span endpoint back to their defaults. Runs on every bpm-mode
// exit (the single chokepoint exit_bpm_mode, which the sweep's dispatch and
// every editor close reach), so a bracket exists only while the mode is live;
// re-entering bpm mode always seeds an EMPTY field. Its one other caller is
// the load in place (apply_recipe_in_place), where the mode is off by
// construction and the call is a statement over a set that already carries
// defaults — never a mode exit.
//
// History-less on purpose: bpm values are session-only with no undo of their
// own (see commit_bpm_edit), so no undo entry is pushed. Unlike iter state,
// undo can never resurrect bpm state: the bpm session is modal, so no
// snapshot-taking op can run while a marker carries live bpm fields, and
// outside the session every marker is already wiped — every snapshot in
// history carries default bpm state. Callers own the repaint.
void GuiFlagEditor::wipe_bpm_state() {
    auto& mv = app.warpmarkers.markers_mut();
    for (auto& m : mv) {
        m.bpm_owner    = false;
        m.bpm_beats    = 0;
        m.bpm_lo       = 0.0;
        m.bpm_hi       = 0.0;
        m.bpm_endpoint = -1;
    }
}

// Open the BPM editor on `idx` — a modal on the bottom row since 2026-08-13.
// Seed pending is the
// current bracket text (EMPTY when blank, else `"<beats>@[<lo>,<hi>]"`) — a
// fresh open is a blank field the whole value is typed into
// (format_bpm_bracket_text, warpmarkers.h, carries that ruling).
// Reuses top_flag_editor with Kind::BpmBracket so the keyboard vocabulary
// swaps to digits + `@`/`,`/`[`/`]`; the dialog painter supplies the visible
// "BPM: " LABEL beside the field (kBpmEditorPrefix, paint_handler.h), so the
// buffer holds the bracket text alone.
void GuiFlagEditor::enter_bpm_edit(int idx) {
    if (idx < 0) return;
    if (!app.bpm_mode_enabled) return;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return;
    if (!bpm_popup_eligible_marker(mv[idx])) return;
    this->enter_text_edit(
        idx,
        text_editor::Kind::BpmBracket,
        format_bpm_bracket_text(mv[idx]));
    // enter_text_edit's tail invalidates the top strip, but the BPM editor
    // draws in the bottom row's modal, whose rect does not exist before its
    // first paint — so a MODAL OPEN damages the whole window (the settings
    // opener carries the rule).
    viewport.invalidate_all();
}

// Commit the BPM editor's pending buffer. Strict syntax via
// parse_bpm_bracket, then the derived-tempo bracket gate below. On
// refusal the editor stays open with a red
// outline and false is returned; on success the parsed values are stored
// on the marker (the marker is already the BPM owner), the editor closes,
// and true is returned. No undo entry — BPM values are session-only,
// treated like view state. The Enter dispatch fires render_bpm_sweep()
// when this returns true.
bool GuiFlagEditor::commit_bpm_edit() {
    if (!text_editor::is_active(app.top_flag_editor)) return false;
    if (app.top_flag_editor.kind !=
            text_editor::Kind::BpmBracket) return false;
    const int idx = app.top_flag_editor.target;
    const auto& mv_const = app.warpmarkers.markers();
    if (idx < 0 || idx >= static_cast<int>(mv_const.size())) {
        text_editor::deactivate(app.top_flag_editor);
        viewport.invalidate_modal_dialog_area();
        return false;
    }
    const std::string& s = app.top_flag_editor.pending;
    int    beats = 0;
    double lo = 0.0, hi = 0.0;
    if (!parse_bpm_bracket(s, beats, lo, hi)) {
        app.top_flag_editor.red = true;
        viewport.invalidate_modal_dialog_area();
        const std::string refusal = "BPM edit rejected: invalid syntax";
        std::fprintf(stderr, "warptempo_gui: %s: %s\n",
            refusal.c_str(), s.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
        return false;
    }
    // Derived-value bracket gate. Every sweep cell carries a derived base
    // tempo into its cell markers, a derived scale into its cell .settings,
    // and — since 2026-08-26 — a rescaled tempo into every effectively
    // ENABLED owning marker outside the span (a disabled one is invisible to
    // the act since 2026-08-29, in the span and out of it, so it can neither
    // be rescaled nor refuse a bracket); the derivation
    // (compute_base_tempo_scale) is
    // monotone in bpm and the rescale rides it, so the bracket ends bound
    // every cell: if either endpoint bpm refuses — the derived base tempo
    // lands outside [kTempoMinCents, kTempoMaxCents], the derived scale
    // outside [kScaleMin, kScaleMax], or any rescaled marker outside the
    // tempo bracket — the commit red-flashes like any invalid editor value. Never clamp: a clamped
    // derivation would silently mistune the span. Gated on a well-formed
    // span (owner before endpoint, positive duration); without one,
    // render_bpm_sweep early-bails and derives nothing. The bpm editor is
    // modal, so the store cannot change between `m` and this commit — n here
    // equals the store size the `m` gate recorded bpm_endpoint against.
    {
        const int endpoint_idx = mv_const[idx].bpm_endpoint;
        const int n = static_cast<int>(mv_const.size());
        // The span-end frame is the boundary marker's time when one exists
        // — the NEXT EFFECTIVELY-ENABLED marker after the last selected
        // one, which is what bpm_endpoint names (warpmarkers.h) — else the
        // song end (bpm_endpoint == n is the song-end sentinel). Guarded on
        // endpoint_idx > idx and endpoint_idx <= n.
        if (audio.sample_rate() > 0 &&
            endpoint_idx > idx &&
            endpoint_idx <= n) {
            const int64_t span_end_frame =
                (endpoint_idx < n) ? mv_const[endpoint_idx].time_frame
                                   : audio.total_frames();
            const double duration_seconds =
                (span_end_frame - mv_const[idx].time_frame) /
                static_cast<double>(audio.sample_rate());
            if (duration_seconds > 0.0) {
                const auto at_lo =
                    compute_base_tempo_scale(duration_seconds, beats, lo);
                const auto at_hi =
                    compute_base_tempo_scale(duration_seconds, beats, hi);
                if (!at_lo || !at_hi) {
                    app.top_flag_editor.red = true;
                    viewport.invalidate_modal_dialog_area();
                    const std::string refusal =
                        "BPM edit rejected: derived tempo or scale outside "
                        "its bracket (tempo [" +
                        format_tempo_cents(kTempoMinCents) + ", " +
                        format_tempo_cents(kTempoMaxCents) + "], scale [" +
                        format_value_double(kScaleMin, 4) + ", " +
                        format_value_double(kScaleMax, 4) + "])";
                    std::fprintf(stderr, "warptempo_gui: %s: %s\n",
                        refusal.c_str(), s.c_str());
                    notifications.notify(
                        AppState::NotificationClass::Normal, refusal);
                    return false;
                }
                // The RESCALED MAP's arm of the same gate (2026-08-26): the
                // sweep rescales every effectively enabled owning marker
                // outside the span by
                // the owner's change (bpm_cell_warp_markers, input_handler.h
                // — the sweep's own per-cell rewrite, run here at the two
                // ends the derivation's monotonicity makes sufficient), and
                // a marker whose rescaled tempo would leave the bracket
                // refuses the commit exactly like an out-of-bracket
                // derivation — never clamped, which would deform the shape
                // the rescale preserves.
                if (!bpm_cell_warp_markers(mv_const, idx, endpoint_idx,
                                           at_lo->base_tempo_cents) ||
                    !bpm_cell_warp_markers(mv_const, idx, endpoint_idx,
                                           at_hi->base_tempo_cents)) {
                    app.top_flag_editor.red = true;
                    viewport.invalidate_modal_dialog_area();
                    const std::string refusal =
                        "BPM edit rejected: a marker outside the span would "
                        "leave the tempo bracket [" +
                        format_tempo_cents(kTempoMinCents) + ", " +
                        format_tempo_cents(kTempoMaxCents) +
                        "] once rescaled";
                    std::fprintf(stderr, "warptempo_gui: %s: %s\n",
                        refusal.c_str(), s.c_str());
                    notifications.notify(
                        AppState::NotificationClass::Normal, refusal);
                    return false;
                }
            }
        }
    }
    // Single-owner invariant: clear bpm_owner on every other marker before
    // stamping this one. The toggle handler maintains the invariant on mode
    // entry, but the editor can target a different marker than the one
    // originally stamped, so reassert it here.
    std::vector<GuiWarpMarker> proposed = mv_const;
    for (int i = 0; i < static_cast<int>(proposed.size()); ++i) {
        if (i == idx) continue;
        if (proposed[i].bpm_owner) {
            proposed[i].bpm_owner = false;
            proposed[i].bpm_beats = 0;
            proposed[i].bpm_lo    = 0;
            proposed[i].bpm_hi    = 0;
            proposed[i].bpm_endpoint = -1;
        }
    }
    proposed[idx].bpm_owner = true;
    proposed[idx].bpm_beats = beats;
    proposed[idx].bpm_lo    = lo;
    proposed[idx].bpm_hi    = hi;
    app.warpmarkers.markers_mut() = std::move(proposed);
    text_editor::deactivate(app.top_flag_editor);
    // ONE SURFACE, ONE OWNER since 2026-08-29: the dialog editor just came
    // down, which is the BOTTOM row's damage, and that is all this commit
    // moves. (It was TWO — the stamped marker becomes a bpm OWNER, a class the
    // RESOLVED READOUT had nothing to resolve for, so the state lane changed
    // with it; that readout retired whole with the one-day status bar and the
    // second call went with it.)
    viewport.invalidate_modal_dialog_area();
    return true;
}

// Full mode-on transition for BPM mode. Validates the activation gate, toggles
// iter mode off if active, maintains the single-owner invariant, and marks the
// FIRST selected marker as the BPM owner (mode exit wipes the bpm state, so a
// fresh entry always opens on a blank field), then flips the mode flag. The
// span endpoint is explicit — supplied by the `m` handler and recorded on the
// owner's bpm_endpoint (section-based, architect 2026-07-23) — so this does not
// auto-select an endpoint cue. The full section gate (non-empty, contiguous,
// ref-free) lives in the `m` handler; this route re-checks only that at least
// one marker is selected and the owner is eligible. The `m` handler calls this
// and then opens the BPM editor on the owner.
void GuiFlagEditor::enter_bpm_mode() {
    if (app.bpm_mode_enabled) return;
    if (app.active_markers_view != 'W') return;
    if (app.selected_markers.empty()) return;
    const int owner = *app.selected_markers.begin();   // first selected
    const auto& mv_const = app.warpmarkers.markers();
    if (owner < 0 || owner >= static_cast<int>(mv_const.size())) return;
    if (!bpm_popup_eligible_marker(mv_const[owner])) return;

    if (app.iteration_mode_enabled) {
        // Forced iter-off is a mode exit like any other and takes the same
        // wipe the `i` toggle's turning-off branch runs — otherwise `m`
        // would smuggle live brackets out of iteration mode invisibly, and
        // the BPM commit's tempo rewrite would land on markers still
        // carrying them.
        wipe_iter_state();
        app.iteration_mode_enabled = false;
    }

    auto& mv = app.warpmarkers.markers_mut();
    for (int i = 0; i < static_cast<int>(mv.size()); ++i) {
        if (i == owner) continue;
        if (mv[i].bpm_owner) {
            mv[i].bpm_owner = false;
            mv[i].bpm_beats          = 0;
            mv[i].bpm_lo             = 0;
            mv[i].bpm_hi             = 0;
            mv[i].bpm_endpoint       = -1;
        }
    }
    // Tag owner with bpm_owner=true if not already set. Sentinel-zero
    // values stay zero; format_bpm_bracket_text renders the EMPTY string for
    // that state, which seeds the BPM dialog editor. Every prior mode exit
    // wiped the bpm state, so a fresh entry always finds an untagged owner
    // and opens on a blank field.
    if (!mv[owner].bpm_owner) {
        mv[owner].bpm_owner = true;
        mv[owner].bpm_beats          = 0;
        mv[owner].bpm_lo             = 0;
        mv[owner].bpm_hi             = 0;
    }

    app.bpm_mode_enabled = true;
    viewport.invalidate_top_strip();
}

// The single bpm-mode-off chokepoint: every route that turns bpm mode off
// funnels here. Wipes the session-only bpm state (mode exit IS the clear, so
// no marker carries bpm state once the mode is down) and repaints both the
// top strip and the MODAL's own lane — the bottom row, which is where the bpm
// editor's pixels are while it stands.
void GuiFlagEditor::exit_bpm_mode() {
    if (!app.bpm_mode_enabled) return;
    app.bpm_mode_enabled = false;
    wipe_bpm_state();
    viewport.invalidate_top_strip();
    viewport.invalidate_modal_dialog_area();
}
