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

// THE HOP BOUND'S GRAMMAR, READ BACK — parse_signed_2dp_cents' twin for the
// phase-reset column (2026-09-09). It is FIXED-WIDTH too and narrower: a sign
// and ONE DIGIT (format_signed_hops, warpmarkers.h, the one form a phase cell
// shows), because a bound is capped at kIterHopMax — the architect's single
// digit, "past nine it is no longer the phase reset it was". So this reader
// takes exactly those two bytes and nothing else: no surrounding whitespace,
// no bare digit, no double sign, ONE CANONICAL SPELLING PER VALUE. THE READER
// IS THE GRAMMAR'S OWNER, which is why it is exact rather than merely wide
// enough to catch ordinary typing: the field's two-byte cap
// (kMaxPendingCharsIterHop, text_editor.h) bounds what can be TYPED and this
// bounds what can be COMMITTED. The conversion is digit-to-int direct, and it
// needs no overflow arm at all — one digit never leaves [-9, +9]; the hop
// WINDOW's own walls, refused at the commit below, are what hold a bound
// inside the piece. ZERO HAS ONE SIGN AND IT IS '+': the writer spells zero
// `+0`, so `-0` is a SECOND spelling of a value that already has one and is
// refused here like any other non-canonical token.
bool parse_signed_hops(const std::string& v, int& out) {
    if (v.size() != 2) return false;
    if (v[0] != '+' && v[0] != '-') return false;
    if (!std::isdigit(static_cast<unsigned char>(v[1]))) return false;
    const int mag = v[1] - '0';
    if (mag == 0 && v[0] == '-') return false;   // -0: zero's second spelling
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
// over TWO STORES with the cell's own eligibility in front.
void GuiFlagEditor::enter_iter_bound_edit(char column, int idx,
                                          MarkerCell side) {
    if (idx < 0) return;
    const bool phase = (column == 'P');
    const auto& mv  = app.warpmarkers.markers();
    const auto& pmv = app.phaseresetmarkers.markers();
    const int n = phase ? static_cast<int>(pmv.size())
                        : static_cast<int>(mv.size());
    if (idx >= n) return;
    if (side != MarkerCell::Lower && side != MarkerCell::Upper) return;
    // NO CELL, NO EDITOR: the cell paints on exactly the markers the sweep
    // reads while the mode is lit ON THAT COLUMN (warp_iter_cells /
    // phase_iter_cells, render.cpp, off the same two predicates under the same
    // column verdict), so an editor opens on exactly
    // those. The callers card the kind refusal ahead of this belt; a mode-off
    // call cannot arrive, the axis falling back to the payload with the mode,
    // and an UNLIT-COLUMN call cannot either, a column switch clearing the
    // selection and so seating the axis back on the payload.
    if (!iteration_column_lit(app, column)) return;
    if (phase ? !phase_reset_iter_eligible_marker(pmv, idx)
              : !iter_popup_eligible_marker(mv, idx)) return;

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
    // editor's open verbatim (its comments carry the why, the select and the
    // land both resolving against the ACTIVE column's store). The select
    // resets the addressed cell to the payload through the Selection
    // chokepoint, so the cell is written AFTER it: an editor open seats the
    // cell it edits.
    selection.repair_last_selected();
    selection.set_single_selection(idx);
    land_playhead_on_marker(app, audio, viewport, idx);
    app.addressed_cell = side;

    if (text_editor::is_active(app.top_flag_editor)) {
        text_editor::deactivate(app.top_flag_editor);
    }
    // THE SEED IS THE CELL'S OWN TOKEN — the one spelling the cell paints on
    // this column (format_iter_bound_cell's `+0.00` on the warp side,
    // format_phase_iter_bound_cell's `+0` on the phase side, each blank), so
    // what the cell shows and what its editor opens with cannot differ.
    const std::string seed =
        phase ? format_phase_iter_bound_cell(pmv[static_cast<size_t>(idx)],
                                             side)
              : format_iter_bound_cell(mv[static_cast<size_t>(idx)], side);
    text_editor::enter(app.top_flag_editor, idx, seed,
                       text_editor::Kind::IterBound,
                       /*iter_upper=*/side == MarkerCell::Upper,
                       /*iter_hops=*/phase);

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

    // THE COLUMN IS READ LIVE AND IT IS THE OPEN'S COLUMN, for
    // commit_measure_edit's own reason (stated in full there): the view CANNOT
    // MOVE under an open session — every column-switching key is dropped at
    // the keyboard-modal gate while any editor stands, and every
    // column-switching BUTTON acts at the LIFT whose own PRESS already closed
    // this editor — so the session needs no stored column of its own. The
    // session's `iter_hops` bit is not that column: it exists for
    // text_editor.cpp alone, which selects the byte cap and cannot see the
    // app.
    if (app.active_markers_view == 'P') {
        this->commit_phase_iter_bound_edit(idx, side, next);
        return;
    }

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
        // THE TEMPO WINDOW — every sweep cell renders base + delta, so the
        // base plus either bound must stay inside the tempo bracket — and the
        // partner bound, the lower never above the upper and the upper never
        // below the lower (0 for a blank bracket, the step's own start). The
        // window cannot move under the bracket afterwards: while grid
        // iterations stands the piece is locked and no base tempo can be
        // authored (authoring_locked, app_state.h), which is what retired the
        // retroactive clamp this refusal used to name.
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

    // NO UNDO ENTRY AND NO DIRTY RE-DERIVE (architect 2026-09-10: "They just
    // don't go in the undo stack at all; they're considered transient by
    // design" — "The iterations don't actually do anything to the map itself;
    // they push that onto the tmp folder as sidecars. So it's more truthful to
    // exclude them from the dirty dot and from the history"). The commit
    // writes the store and nothing else: no snapshot, no push, no gesture
    // stamp, no dirty flag to move. The way back from a typed bound is to type
    // the old one, or to leave the mode, which clears the bracket whole.
    app.warpmarkers.markers_mut() = std::move(proposed);

    // NO RENDER AND NO MAP REBUILD: a bracket is not a map input (excluded
    // from build_warp_frame_map and the render recipe alike), so the cell is
    // the only thing that moved and the strip is the only damage.
    text_editor::deactivate(app.top_flag_editor);
    viewport.invalidate_top_strip();
}

// THE PHASE ARM OF THE BOUND COMMIT (2026-09-09). The contract is at the
// declaration; this is the warp arm's every clause in the HOP domain, and
// TYPED INPUT GATES LOUD here for the same reason: a bound typed into its own
// cell is authored input arriving at its own surface, so the walls the arrows'
// step clamps at silently (phase_iter_bound_step_landing) are refusals here,
// each named.
void GuiFlagEditor::commit_phase_iter_bound_edit(int idx, MarkerCell side,
                                                 const std::string& next) {
    const auto& pv_const = app.phaseresetmarkers.markers();
    // The target may have gone out from under the editor (the store is frozen
    // under the modal editor, so it cannot happen today; the belt is the
    // payload commit's own): drop the edit.
    if (idx < 0 || idx >= static_cast<int>(pv_const.size())) {
        this->exit_top_flag_edit_no_commit();
        return;
    }
    const GuiPhaseResetMarker& live = pv_const[static_cast<size_t>(idx)];

    // The warp arm's refuse composer verbatim: `red = true`, a top-strip
    // repaint, one stderr line keeping the offending token, one normal card
    // carrying the sentence alone (that text standing in the red field the
    // refusal leaves), and the session left open for correction.
    auto refuse = [&](const std::string& why) {
        app.top_flag_editor.red = true;
        viewport.invalidate_top_strip();
        const std::string refusal = "Range bound rejected: " + why;
        std::fprintf(stderr, "warptempo_gui: %s: %s\n",
                     refusal.c_str(), next.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
    };

    std::vector<GuiPhaseResetMarker> proposed = pv_const;
    GuiPhaseResetMarker& m = proposed[static_cast<size_t>(idx)];
    if (next.empty()) {
        // AN EMPTY COMMIT CLEARS THE WHOLE BRACKET: a bracket is a pair and
        // one bound alone is not representable, so emptying either cell is the
        // removal — the measure's own empty-removes rule.
        m.iter_start_hops.reset();
        m.iter_end_hops.reset();
    } else {
        int value = 0;
        if (!parse_signed_hops(next, value)) {
            refuse("a hop bound is a sign and one digit, as +0");
            return;
        }
        // THE PARTNER, first and cheapest: the lower never rises above the
        // upper and the upper never falls below the lower (0 for a blank
        // bracket, the step's own start).
        const int partner = side == MarkerCell::Upper
                                ? live.iter_start_hops.value_or(0)
                                : live.iter_end_hops.value_or(0);
        if (side == MarkerCell::Lower && value > partner) {
            refuse("the lower bound cannot rise above the upper");
            return;
        }
        if (side == MarkerCell::Upper && value < partner) {
            refuse("the upper bound cannot fall below the lower");
            return;
        }
        // THEN THE HOP WINDOW, refused rather than clamped: a cell that would
        // push the reset before frame 0 or past the last frame is refused at
        // authoring, like the tempo window. A NEIGHBOURING RESET IS NOT A
        // WALL — two ranges may cross or meet and the sweep sorts each cell
        // before the request (phase_reset_hop_window, warp_frame_map_view.h).
        //
        // ONLY ONE WALL CAN ARRIVE HERE, AND IT IS THE PIECE'S EDGE, so the
        // refusal names it outright and the window hands back numbers alone:
        // THE GRAMMAR BOUNDS THE DIGIT BEFORE THE WINDOW IS ASKED —
        // parse_signed_hops takes a sign and ONE digit and the field cannot
        // hold a third byte (kMaxPendingCharsIterHop, text_editor.h), so a
        // committed value already lies inside [-kIterHopMax, kIterHopMax],
        // the very interval the digit wall closes. A typed `+10` never reaches
        // this test; it is refused as a full field or by the grammar sentence
        // above. THE DIGIT CEILING ITSELF IS NOT DEAD — it survives as the
        // NUMERIC bound the STEP clamps at, stopping at +/-9 there
        // (phase_iter_bound_step_landing) — it simply has no typed road, which
        // is why this site reads the interval and never a wall kind.
        const PhaseHopWindow w = phase_reset_hop_window(app, audio, idx);
        if (value < w.k_min || value > w.k_max) {
            refuse("the cell would leave the piece");
            return;
        }
        // THE ONE WRITE SITE: the addressed side takes the value, the partner
        // keeps what it had, and a pair landing on two zeroes clears — the
        // same rule the arrows' step writes under, so a typed `+0` pair and a
        // stepped one mean one thing.
        phase_iter_bound_step_write(m, side, value);
    }

    // A COMMIT THAT CHANGES NOTHING IS NOT A CHANGE: no undo entry, no store
    // bump — the shape every no-op commit in the product takes.
    if (m.iter_start_hops == live.iter_start_hops &&
        m.iter_end_hops   == live.iter_end_hops) {
        this->exit_top_flag_edit_no_commit();
        return;
    }

    // NO UNDO ENTRY AND NO DIRTY RE-DERIVE, the warp arm's own ruling
    // (2026-09-10): the bracket is outside the undo domain on both columns, so
    // the commit writes the store and nothing else.
    app.phaseresetmarkers.markers_mut() = std::move(proposed);

    // NO RENDER AND NO MAP REBUILD: a bracket is not a position and not a map
    // input, so the cell is the only thing that moved and the strip is the
    // only damage.
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

    // ONE UNDO ENTRY: a measure is serialized content and its edit dirties the
    // tab like any other authored change. The snapshot is taken before the
    // write, the store's own convention.
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

    // Cross-marker check (edit target excluded), through the rule's ONE owner
    // label_def_taken (warpmarkers.h), which the history revert asks of its own
    // proposed store. A dangling label_ref is deliberately not gated here — the
    // parser resolver normalizes a dangling ref to a plain 1.00 owner at
    // render/preview time (one stderr line per timestamp); def uniqueness is a
    // load rule, so the editor still gates it.
    if (ok && label_def_taken(mv_const, parsed.label_def, idx)) {
        ok = false;
        err = "duplicate label definition: " + parsed.label_def;
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
    // tell whether the engine/dirty state actually moved.
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

    // THE BRACKET IS THE STORE'S, NOT THIS EDITOR'S, AND THIS EDITOR CANNOT
    // MEET ONE (architect 2026-09-10, the iteration lock). No line above
    // writes a bound — the bounds are the cells' own, each with its own
    // editor — and a bracket exists only while grid iterations is lit, where
    // this editor cannot open at all: bare Return and the payload
    // double-click are both refused on a payload axis while the lamp stands
    // (authoring_locked, app_state.h). So the invariant "a committed
    // NON-CARRIER never keeps a bracket" holds vacuously here, and the clear
    // below is a BELT over two already-empty optionals — kept because it is
    // the store's own rule stated at the surface that could otherwise break
    // it (Ctrl+N's owner->pass / ref->pass carrier-loss clears are its
    // siblings), and because it costs two resets. THE RETROACTIVE CLAMP THAT
    // FOLLOWED IT IS DELETED with its owner: this commit can move the base
    // tempo, but never under a bracket.
    if (!iter_bracket_carrier(m)) {
        m.iter_start_cents.reset();
        m.iter_end_cents.reset();
    }

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

    // (A SECOND TERM ASKED WHETHER THE SESSION-ONLY BRACKET HAD MOVED, and
    // it went with the bracket's exit from the undo domain on 2026-09-10:
    // this editor cannot open under a standing bracket, so the belt above
    // clears nothing and no commit here is bracket-only. `canonical_changed`
    // is the whole store-change question now.)

    // Capture pre-state for undo BEFORE mutating.
    std::vector<GuiWarpMarker> pre_state = mv_const;
    app.warpmarkers.markers_mut() = std::move(proposed);

    if (n_refs_renamed > 0) {
        std::fprintf(stderr,
            "warptempo_gui: Renamed label_def '%s' -> '%s'; "
            "updated %d refs\n",
            old_def.c_str(), new_def.c_str(), n_refs_renamed);
    }

    if (canonical_changed) {
        undo.push_undo_warp(std::move(pre_state));
    }

    text_editor::deactivate(app.top_flag_editor);

    if (!canonical_changed) return;

    // Unconditional by ruling — rationale at GuiTargetRender::trigger. Any
    // store change repaints and triggers.
    undo.recompute_dirty();
    // THE CENTERED POSTURE COLLAPSES (the rule is at AppState::centered_mode):
    // canonical_changed IS the map-input set on this surface — a tempo, a
    // scale, a label definition or reference, the disabled bit — so a commit
    // that reaches this line moved the map, the rule's MAP clause. Past the
    // canonical_changed return above, so this is the changed path, and the
    // re-land below is a TRANSLATION that collapses nothing of its own.
    collapse_centered_posture(app);
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
    // AND canonical_changed IS THE SECOND TERM, which is exactly the map-input
    // set (tempo_inherits / tempo_cents / tempo_scale / label_def / label_ref /
    // disabled / renamed refs; position is not editable here) and which, since
    // 2026-09-10, is also the whole store-change question this body asks: the
    // BRACKET-ONLY commit the second term was carved out for — the carrier-loss
    // clear and the retired retroactive clamp — cannot happen under the
    // iteration lock, so the two gates collapsed into one.
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

// Wipe BOTH STORES' session-only iter brackets (2026-09-09, with the mode's
// second column: a warp marker's cent bracket and a phase reset's hop bracket
// go together, the mode being one lamp over both). IT STAYS A TWO-STORE WIPE
// UNDER THE COLUMN STAMP (2026-09-10): brackets can only be authored on the
// column the lamp was lit in, so the other store is empty by construction and
// the second clear is a BELT — kept because a belt against an invariant costs
// one loop and an exit that left a stale bracket behind would be invisible
// (the mode's own cells are what show a bracket, and they are gone by then).
// No arm reads the stamp here and none should: the wipe runs at the OFF EDGE,
// where the stamp is already meaningless. The single clear every
// iteration-mode exit route shares, TWO routes since 2026-09-10: the `i`
// toggle's turning-off branch and the iteration sweep's success tail —
// exiting the mode is the clear on every route, so a bracket exists only while
// the mode paints it on the flags. ENTER_BPM_MODE'S FORCED ITER-OFF WAS THE
// THIRD and it is gone with the swap it performed (architect 2026-09-10: NO
// SILENT SWAPS — bare `m` is REFUSED under a lit lamp now rather than being
// its second exit), so bare `i` is the only key that reaches this. THE S->T AUDIO-VIEW TOGGLE IS NO LONGER ONE
// OF THEM (2026-08-07): iteration mode is target-legal, so entering target view
// neither exits the mode nor clears anything (the record is at
// switch_active_audio_view_to, input_handler.cpp). BOTH
// surviving callers can run in TARGET view, where the write is the granted
// home-view-binding exception argued at the sweep's tail
// (run_iteration_sweep_render, input_key_dispatch.cpp).
// THE LOAD IN PLACE IS NOT A ROUTE EITHER (architect 2026-09-02): iteration
// mode is where you stand, so apply_recipe_in_place leaves the mode bit alone
// and resets no bracket — and since 2026-09-10 the load CANNOT RUN while the
// mode is lit at all, the iteration lock refusing it on every road, so the
// ruling holds vacuously (the record is at that body,
// input_key_dispatch.cpp).
// IT PUSHES NOTHING AT ALL (architect 2026-09-10: "Iterations are the least
// durable thing in this GUI as far as the flags. It's okay to clear them and
// not remember them very often. They just don't go in the undo stack at all;
// they're considered transient by design"). It pushed one both-columns entry
// until then, so that leaving the mode by accident could be undone; the
// bracket left the undo domain whole with that ruling, and a wipe that pushed
// would be the one entry able to put a bracket back. So the clear is FINAL —
// "a bracket exists only on a marker that is on screen or it is gone for
// good" — and it moves no dirty flag either, neither bracket ever having
// serialized.
// Callers own the mode-flag flip and the repaint invalidation.
// AND IT PUTS AN ADDRESSED BOUND CELL BACK ON THE PAYLOAD (architect
// 2026-09-04): the cells go with the mode, so a Lower or Upper axis
// (AppState::addressed_cell) falls back to Payload here, ahead of the
// bracket test, because this body is the one thing every exit from the mode
// runs — there is no single mode setter, the three writers of the off edge
// each flip the bit themselves after calling this — so a step outside the
// mode can only ever be the tempo step, on either column. An addressed
// MEASURE is left alone: that cell is not the mode's. History-less like
// everything else here: the axis is a session address, not content.
void GuiFlagEditor::wipe_iter_state() {
    if (app.addressed_cell == MarkerCell::Lower ||
        app.addressed_cell == MarkerCell::Upper) {
        app.addressed_cell = MarkerCell::Payload;
    }
    // The two scans read the stores CONST, so a bracketless exit bumps neither
    // generation and rebuilds no cache: markers_mut is what bumps, and it is
    // reached only past the test below.
    bool warp_any = false;
    for (const auto& m : app.warpmarkers.markers()) {
        if (m.iter_start_cents.has_value() || m.iter_end_cents.has_value()) {
            warp_any = true;
            break;
        }
    }
    bool phase_any = false;
    for (const auto& p : app.phaseresetmarkers.markers()) {
        if (p.iter_start_hops.has_value() || p.iter_end_hops.has_value()) {
            phase_any = true;
            break;
        }
    }
    if (!warp_any && !phase_any) return;
    // Each column is touched only where it has something to clear, for the
    // same reason: an untouched store keeps its generation.
    if (warp_any) {
        for (auto& m : app.warpmarkers.markers_mut()) {
            m.iter_start_cents.reset();
            m.iter_end_cents.reset();
        }
    }
    if (phase_any) {
        for (auto& p : app.phaseresetmarkers.markers_mut()) {
            p.iter_start_hops.reset();
            p.iter_end_hops.reset();
        }
    }
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
// own (see commit_bpm_edit), so no undo entry is pushed, and undo can never
// resurrect bpm state — the bpm session is modal, so no snapshot-taking op can
// run while a marker carries live bpm fields, and outside the session every
// marker is already wiped. THE ITER WIPE IS THE SAME SHAPE SINCE 2026-09-10
// and no longer the contrast it was: it pushes nothing either, and the
// bracket's own snapshots are stripped at every push. Callers own the repaint.
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

// Full mode-on transition for BPM mode. Validates the activation gate,
// maintains the single-owner invariant, and marks the
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

    // NO FORCED ITER-OFF (architect 2026-09-10, that evening: "we should card
    // the exit, because it is still one button automatically affecting the
    // other" — NO SILENT SWAPS ANYWHERE). Bare `m` used to be grid iterations'
    // second exit, wiping every bracket on its way in; it is REFUSED under a
    // lit lamp now, at the keyboard gate with the lock's own card and on the
    // BPM Iterations button's greyed face (iteration_lock_key_blocked and
    // iteration_lock_greys), so this body can only ever run with the lamp
    // dark and there is no bracket here to smuggle out. Bare `i` is the
    // mode's one exit.

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
