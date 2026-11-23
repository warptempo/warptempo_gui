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
#include <expected>
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

// THE TWO BOUND GRAMMARS' SENTENCES, each one composer for two readers: the
// bound commit's refusal ("Range bound rejected: …") and the bound editor's
// UP / DOWN on a text the grammar refuses ("The bound is not well formed:
// …", step_editor_text; architect 2026-11-20 / 21).
constexpr const char* kCentBoundGrammar =
    "a bound is a sign, one digit and two decimals, as +0.00";
constexpr const char* kHopBoundGrammar =
    "a hop bound is a sign and one digit, as +0";

// THE BOUND COMMIT'S REFUSAL SENTENCE, one composer for its three readers:
// both columns' commits and the bound editor's UP / DOWN from an empty text
// (step_editor_text), which is refused as the commit it stands for.
std::string range_bound_rejected(const std::string& why) {
    return "Range bound rejected: " + why;
}

// A BOUND AGAINST ITS PARTNER, ONE JUDGE, both columns (warp cents and phase
// hops, each exact in int64_t): the lower never rises above the upper, the
// upper never falls below the lower, the partner the STORED other bound (0
// for a blank bracket). Null where the bound stands, else the refusal's
// reason. THREE READERS (architect 2026-11-21: "erased bound should not
// affect the other one. card should disallow the erase if it would make it
// invalid"): the two commits, a typed bound and an EMPTY TEXT alike — an
// erased bound is judged as the number zero, the value it commits — and the
// bound editor's UP / DOWN from an empty text, where a zero this refuses is a
// refused commit with no start to step from (step_editor_text).
const char* iter_bound_partner_refusal(MarkerCell side, int64_t value,
                                       int64_t partner) {
    if (side == MarkerCell::Lower && value > partner)
        return "the lower bound cannot rise above the upper";
    if (side == MarkerCell::Upper && value < partner)
        return "the upper bound cannot fall below the lower";
    return nullptr;
}

} // namespace

// Flag-editor cluster: the marker lane's two editors (the flag's
// canonical-line editor and the iteration bound editor — text_editor::Kind's
// FlagPayload and IterBound) —
// their enter / commit / exit paths — and the BPM form's field commit and
// RENDER gate, reaching undo and viewport through the struct's reference members. The
// eligibility and flag-text helpers (iter_bracket_carrier,
// iter_popup_eligible_marker, bpm_popup_eligible_marker, ...) live in
// warpmarkers.h alongside effective_disabled, so this TU sees them via
// #include "warpmarkers.h".

void GuiFlagEditor::exit_top_flag_edit_no_commit() {
    if (!text_editor::is_active(app.top_flag_editor)) return;
    // The strip repaint below is the whole damage of a close: the field and
    // the stem crossing its frame are the strip's (render_flag_editor_box),
    // and the stem's run below takes the flag cache's own damage.
    text_editor::deactivate(app.top_flag_editor);
    viewport.invalidate_top_strip();
}

// Shared core for the enter-editor flows. The wrapper below
// (enter_top_flag_edit) owns the kind-specific eligibility gates and
// seed-text builder, then delegates here. The live open routes are: Enter
// (EDIT on glass) on the focused marker with the payload addressed and the
// flag box's double tap (both -> enter_top_flag_edit). Every route opens the editor with its
// SEEDED content fully selected (open-selected) — typing replaces
// wholesale — so there is no
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

    // THE MODAL-OPEN STOP (architect 2026-11-20: "play should stop
    // on editor open. i don't use that workflow and never have";
    // GuiPlaybackLifecycle::stop_playback_for_modal_open owns the decision
    // table). Past every guard — the wrapper's and its callers' refusals, the
    // range test and the re-open on the live session's own target above, so a
    // refused open and a re-open stop nothing — and ahead of the select and
    // the land, so the land seats a resting playhead.
    playback_lifecycle.stop_playback_for_modal_open();

    // Target-switching path. Single-select the new editor target so the
    // marker-column outline follows it, and LAND the playhead on it: the marker
    // lane owns the playhead (the rule is stated in full at
    // land_playhead_on_marker, input_pointer.cpp), and an editor open is exactly
    // a route that hands the lane a new focus. This brings the one
    // set_single_selection caller that had opted out back onto the pointer
    // clicks' adjacency convention (selection then land, on the next line).
    // Reached in the WARP column alone — all three open routes gate the marker
    // view to it — so `idx` resolves against the warp store this helper reads.
    // BOTH AUDIO VIEWS (2026-08-24; the payload editor is the home-view
    // binding's fifth ruled exception; the inventory is at
    // active_column_authoring_allowed, app_state.h), so the domain is not
    // always the identity one: land_playhead_on_marker translates through the
    // ACTIVE domain itself (source_frame_to_active_domain at its body), so the
    // land is right in target view with nothing to add here.
    // THE LAND IS A MOVEMENT OWNER: an open moves the playhead onto one
    // marker, which ends an A/B audition as every movement does (the rule at
    // Viewport::move_playhead_to, viewport.cpp). This one
    // chokepoint covers both open roads — EDIT (bare Return with the payload
    // addressed, input_handler.cpp) and a marker's double tap
    // (input_pointer.cpp's marker lane arm). NEITHER CAN REACH A STANDING
    // EDITOR: while one stands the editors' veil swallows every press
    // outside its field, another flag and every column-switching key
    // included, and closes nothing (on_button_press's THE VEIL'S LAST WORD,
    // input_pointer.cpp), and Return is its own commit — so the editor's
    // marker and column stay fixed until its exit. The same_target return
    // above and the discard below are belts for that.
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

    // Open-selected: fully select the seeded content (anchor=0 /
    // cursor=end) so the first keystroke replaces it
    // wholesale; a blank seed
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

// The two flag-editor open routes (bare Return — EDIT on glass — with the
// payload addressed, and the flag box's double tap, architect 2026-11-21)
// end here. THE OPEN STOPS PLAY, as every editor's open does
// (architect 2026-11-20: "play should stop on editor open. i don't
// use that workflow and never have"): the stop is enter_text_edit's, past
// the guards, through GuiPlaybackLifecycle::stop_playback_for_modal_open, the
// one owner of the modal-open stop and its decision table, so nothing of the
// project's plays under the editor and its keyboard.
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

// The contract is at the declaration; this is the payload editor's open
// mechanics (enter_top_flag_edit), over TWO STORES, with the cell's own
// eligibility in front.
void GuiFlagEditor::enter_iter_bound_edit(char column, int idx,
                                          MarkerCell side) {
    if (idx < 0) return;
    // THE TWO-STORE FORK below is W's or P's; any other letter is no column.
    if (column != 'W' && column != 'P') return;
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
    // those, and the lock keeps an addressed cell's marker eligible; a mode-off
    // call cannot arrive, the axis falling back to the payload with the mode,
    // and an UNLIT-COLUMN call cannot either, a column switch clearing the
    // selection and so seating the axis back on the payload.
    if (!iteration_column_lit(app, column)) return;
    if (phase ? !phase_reset_iter_eligible_marker(pmv, idx)
              : !iter_popup_eligible_marker(mv, idx)) return;
    // A TIE FOLLOWER HAS NO FIELD (architect 2026-09-19): its cells show the
    // LEADER's numbers and its own pair is unread, so a field here would edit
    // one bracket while displaying another. Bare Return refuses on this same
    // predicate (flag_editor_open_actionable) and cards kBoundCellTiedCard at
    // its own arm; the DOUBLE-TAP road is silent, a
    // pointer gesture's non-event being its own answer, so this arm is the
    // refusal for that road and the belt for the other.
    if (bound_cell_is_tie_follower(app, column, idx)) return;

    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::IterBound &&
        app.top_flag_editor.target == idx &&
        iter_bound_editor_side(app.top_flag_editor) == side) {
        // Re-open on the live session's own cell: preserve the pending text
        // and any in-progress state, just repaint (the payload editor's rule).
        viewport.invalidate_top_strip();
        return;
    }

    // THE MODAL-OPEN STOP, past every guard above (the cell's eligibility,
    // the tie follower, the re-open on the live session's own cell) and
    // ahead of the land — the payload editor's open's (enter_text_edit;
    // architect 2026-11-20, every editor's open stops play).
    playback_lifecycle.stop_playback_for_modal_open();

    // The focus repaired, then single-selected and landed — the payload
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
    // THE SEED IS THE MARKER'S OWN TOKEN, in the one spelling the cell paints
    // on this column (format_iter_bound_cell's `+0.00` on the warp side,
    // format_phase_iter_bound_cell's `+0` on the phase side, each blank).
    // READING THE MARKER RATHER THAN THE TIE'S GOVERNOR IS EXACT HERE, and
    // the refusal above is why: past it the marker IS its tie's governor —
    // untied, or the leader — so the seed and the cell's painted numbers are
    // one number on every road that opens a field.
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

    // THE COLUMN IS READ LIVE AND IT IS THE OPEN'S COLUMN: the view CANNOT
    // MOVE under an open session — every column-switching key is dropped at
    // the keyboard-modal gate while any editor stands, and every
    // column-switching BUTTON's press is swallowed by the editors' veil,
    // which closes nothing (on_button_press's THE VEIL'S LAST WORD,
    // input_pointer.cpp) — so the session needs no stored column of its
    // own. The session's `iter_hops` bit is not that column: it exists for
    // text_editor.cpp alone, which selects the byte cap and cannot see the
    // app. (The column is W or P, the column axis's two letters.)
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
        text_editor::refuse(app.top_flag_editor);
        viewport.invalidate_top_strip();
        // ONE COMPOSER, TWO READERS: the stderr line keeps the offending
        // token after the sentence; the card does not, that text standing
        // selected in the field the refusal leaves.
        const std::string refusal = range_bound_rejected(why);
        std::fprintf(stderr, "warptempo_gui: %s: %s\n",
                     refusal.c_str(), next.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
    };

    std::vector<GuiWarpMarker> proposed = mv_const;
    GuiWarpMarker& m = proposed[static_cast<size_t>(idx)];
    // AN ERASED BOUND IS THE NUMBER ZERO (architect 2026-11-21: "erased
    // bound should not affect the other one"): an empty text
    // commits that bound as 0 and leaves the other as it stands, judged on
    // the very walls a typed +0.00 meets — the window always admits it
    // (iter_bound_tie_window's intersection holds 0), so where zero falls
    // on the wrong side of the other bound the erase is refused on the
    // partner's card, the editor standing on its empty text. Where the
    // other bound is zero too the write's two-zeroes rule makes the pair
    // the blank.
    int64_t value = 0;
    if (!next.empty() && !parse_signed_2dp_cents(next, value)) {
        refuse(kCentBoundGrammar);
        return;
    }
    // THE WALLS, the landing owner's two, refused rather than clamped:
    // THE TIE'S TEMPO WINDOW — every sweep cell renders base + delta for
    // EVERY tied member, so each member's base plus either bound must stay
    // inside the tempo bracket, and the interval is the tightest member's
    // (iter_bound_tie_window, app_state.h, the step's own owner, so a
    // typed bound and a stepped one are refused and clamped on ONE test
    // and no sweep cell can ever refuse) — and the partner bound, the
    // lower never above the upper and the upper never below the lower
    // (0 for a blank bracket, the step's own start;
    // iter_bound_partner_refusal, the one judge). Neither window nor
    // tie can move under the bracket afterwards: while grid iterations
    // stands the piece is locked and no base tempo can be authored
    // (authoring_locked, app_state.h), so no retroactive clamp is owed.
    //
    // ONLY THE NOUN FORKS on whether a tie stands, the tie's own verdict
    // owner's rule: the wall a typed bound broke may be another marker's.
    const IterBoundWindow w = iter_bound_tie_window(mv_const, idx);
    if (value < w.lo || value > w.hi) {
        const bool tied = live.iter_tie_group != 0;
        refuse(std::string(tied ? "a tied marker's base tempo"
                                : "the base tempo") +
               " plus the bound leaves the tempo bracket [" +
               format_tempo_cents(kTempoMinCents) + ", " +
               format_tempo_cents(kTempoMaxCents) + "]");
        return;
    }
    const int64_t partner = side == MarkerCell::Upper
                                ? live.iter_start_cents.value_or(0)
                                : live.iter_end_cents.value_or(0);
    if (const char* why = iter_bound_partner_refusal(side, value, partner)) {
        refuse(why);
        return;
    }
    // THE ONE WRITE SITE: the addressed side takes the value, the partner
    // keeps what it had, and a pair landing on two zeroes clears — the
    // same rule the arrows' step writes under, so a typed +0.00 pair and
    // a stepped one mean one thing.
    iter_bound_step_write(m, side, value);

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

    // The warp arm's refuse composer verbatim: text_editor::refuse (the whole
    // text selected), a top-strip repaint, one stderr line keeping the
    // offending token, one normal card carrying the sentence alone (that text
    // standing selected in the field the refusal leaves), and the session
    // left open for correction.
    auto refuse = [&](const std::string& why) {
        text_editor::refuse(app.top_flag_editor);
        viewport.invalidate_top_strip();
        const std::string refusal = range_bound_rejected(why);
        std::fprintf(stderr, "warptempo_gui: %s: %s\n",
                     refusal.c_str(), next.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
    };

    std::vector<GuiPhaseResetMarker> proposed = pv_const;
    GuiPhaseResetMarker& m = proposed[static_cast<size_t>(idx)];
    // AN ERASED BOUND IS THE NUMBER ZERO, the warp arm's rule (architect
    // 2026-11-21): an empty text commits that bound as 0 and leaves
    // the other as it stands, judged on the very walls a typed `+0` meets —
    // the hop window always admits it (phase_iter_bound_tie_window's
    // intersection holds 0), so where zero falls on the wrong side of the
    // other bound the erase is refused on the partner's card, the editor
    // standing on its empty text; the other bound zero too, the write's
    // two-zeroes rule makes the pair the blank.
    int value = 0;
    if (!next.empty() && !parse_signed_hops(next, value)) {
        refuse(kHopBoundGrammar);
        return;
    }
    // THE PARTNER, first and cheapest: the lower never rises above the
    // upper and the upper never falls below the lower (0 for a blank
    // bracket, the step's own start; iter_bound_partner_refusal, the one
    // judge).
    const int partner = side == MarkerCell::Upper
                            ? live.iter_start_hops.value_or(0)
                            : live.iter_end_hops.value_or(0);
    if (const char* why = iter_bound_partner_refusal(side, value, partner)) {
        refuse(why);
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
    //
    // AND IT IS THE TIE'S WINDOW (architect 2026-09-19): one cell
    // displaces EVERY tied reset by the same hop count, so the interval is
    // the tightest member's (phase_iter_bound_tie_window, app_state.h,
    // the step's own owner, so a typed bound and a stepped one meet ONE
    // test and no sweep cell can ever refuse). Only the noun forks on
    // whether a tie stands — the edge a typed bound would run into may be
    // another reset's.
    const PhaseHopWindow w = phase_iter_bound_tie_window(app, audio, idx);
    if (value < w.k_min || value > w.k_max) {
        refuse(live.iter_tie_group != 0
                   ? "the cell would take a tied reset out of the piece"
                   : "the cell would leave the piece");
        return;
    }
    // THE ONE WRITE SITE: the addressed side takes the value, the partner
    // keeps what it had, and a pair landing on two zeroes clears — the
    // same rule the arrows' step writes under, so a typed `+0` pair and a
    // stepped one mean one thing.
    phase_iter_bound_step_write(m, side, value);

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

// THE PAYLOAD COMMIT'S JUDGMENT AND ITS WRITE, ON A STORE — ONE OWNER FOR ITS
// TWO ROADS (2026-11-10): the flag editor's commit (commit_top_flag_edit) and
// the label paste (paste_payload_onto_selection, architect 2026-11-10), which
// runs it once per selected marker over one evolving copy of the store, so a
// pasted line is judged and written exactly as the same line typed into that
// marker's editor is — never by a second derivation.
//
// Validate `payload` as a single canonical line for `mv_const[idx]` and, on
// success, return the store with the parsed marker's fields written back
// onto that row. Cascade-renames label_def changes onto every other marker
// that referenced the old name when the edited row is the definition those
// references bind (the rule at the rename below). `canonical_changed` says
// whether any serialized field moved, `n_refs_renamed` and the two names
// what the cascade did. PRECONDITION: `idx` names a row of `mv_const`.
//
// On failure: the reason, one sentence, and nothing else — the caller owns
// the refusal's surface. The refusal CALLS the loader's own line predicate, no
// second grammar, so a payload loads iff it commits; the load's face of the
// same function is load-fatal.
namespace {

struct PayloadCommit {
    std::vector<GuiWarpMarker> proposed;
    bool                       canonical_changed = false;
    int                        n_refs_renamed    = 0;
    std::string                old_def;
    std::string                new_def;
};

// THE COMMIT'S PARSE, ONE BODY FOR ITS THREE READERS: the payload commit's
// judgment below, and the flag editor's extra keys (architect 2026-11-20 /
// 21), whose "well-formed string" is exactly the line this parse
// admits — step_editor_text and toggle_inherit_editor_text, which card its
// reason when it refuses. `row` is the marker the editor stands on (its
// position and its disabled bit, the two fields the payload does not carry);
// the answer is the parsed marker, the reason on failure.
std::expected<GuiWarpMarker, std::string> parse_payload_line(
        const GuiWarpMarker& row, const std::string& payload) {
    // The buffer is the plain payload: no bracket rides in it (the bounds
    // are the cells' own, each with its own editor), so
    // parse_single_canonical_line reads it as it reads a sidecar line.

    // Assemble the parse candidate in SERIALIZER form. The editor holds the
    // PAYLOAD alone, so the two fields ahead of the pipe come from the marker's
    // own state rather than from any typed bytes: `disabled` from the marker's
    // flag, and the position as the integer frame text the canonical line
    // grammar expects (format_authored_frame, frame_format.h — never the
    // MM:SS.mmm form the GUI shows elsewhere).
    std::string candidate;
    if (row.disabled) candidate += '#';
    candidate += format_authored_frame(row.time_frame);
    candidate += '|';
    candidate += payload;

    auto result = warpmarkers_internal::parse_single_canonical_line(candidate);
    if (!result) return std::unexpected(std::move(result.error()));
    GuiWarpMarker parsed;
    static_cast<WarpMarker&>(parsed) = std::move(*result);
    return parsed;
}

// THE STORE AS IF THE TEXT WERE COMMITTED (architect 2026-11-20 / 21: "a
// true toggle inherit that works just like the one in the main window
// does"): the extra keys' one answer to "the tempo this marker sounds at" —
// the store sliced (slice_to_warp_markers), the row under edit REPLACED BY
// THE PARSED TEXT `parsed` (parse_payload_line's answer on that row), so a
// `pass` text sounds at what a pass at this place inherits whatever the row
// committed holds. A LOCAL COPY, NEVER A WRITE: nothing commits, no undo, no
// damage. Its cost is the slice's one copy of the store per key press (a
// press, not a frame). PRECONDITION: `idx` names a row of `mv`.
//
// WHY THE ROW ALONE AND NOT THE COMMIT'S WHOLE PROPOSAL (propose_payload_
// commit's rename cascade and duplicate refusal): its one reader is
// marker_effective on THIS row, and only when the row is a pass (an owner
// answers its own fields, a reference is refused or toggled without a
// tempo). A pass's answer is the projection's backward walk to the nearest
// prior surviving owner (resolve_inherited_tempo, warp_frame_map_build.h),
// which SKIPS every label reference and runs before the references are
// normalized (the resolver's stage 4 ahead of its stage 5), so no label
// binding — the text's definition, a rename's cascade, a duplicate — moves
// it; the row's frame and disabled bit are the committed ones (the parse
// carries them over), so the coincidence (a fact of frames) is the
// committed store's. The row alone is therefore the commit's answer for
// this marker, byte for byte.
std::vector<WarpMarker> resolved_as_if_committed(
        const std::vector<GuiWarpMarker>& mv, int idx,
        const GuiWarpMarker& parsed) {
    std::vector<WarpMarker> resolved = slice_to_warp_markers(mv);
    resolved[static_cast<size_t>(idx)] = static_cast<const WarpMarker&>(parsed);
    return resolved;
}

std::expected<PayloadCommit, std::string> propose_payload_commit(
        const std::vector<GuiWarpMarker>& mv_const, int idx,
        const std::string& payload) {
    std::expected<GuiWarpMarker, std::string> line =
        parse_payload_line(mv_const[idx], payload);
    bool ok = line.has_value();
    std::string err;
    GuiWarpMarker parsed;
    if (ok) {
        parsed = std::move(*line);
    } else {
        err = std::move(line.error());
    }

    // Cross-marker check (edit target excluded), through the rule's ONE owner
    // label_def_taken (warpmarkers.h): a label an ENABLED definition holds is
    // refused on this card, and a label only disabled definitions hold is
    // accepted (architect 2026-10-29). A dangling label_ref is deliberately
    // not gated here — the parser resolver normalizes a dangling ref to a
    // plain 1.00 owner at render/preview time (one stderr line per timestamp),
    // and an ambiguous one (two enabled definitions, reached by re-enabling)
    // the same way.
    if (ok && label_def_taken(mv_const, parsed.label_def, idx)) {
        ok = false;
        err = "duplicate label definition: " + parsed.label_def;
    }
    // Removing a label_def whose refs remain is legal: the refs go
    // dangling, keep their `a.NN` payload (so the file still parses),
    // and the parser resolver normalizes each to a plain 1.00 owner at
    // render/preview time (one stderr line per timestamp). No editor-side
    // gate; the rename cascade below stays, scoped to non-empty new names.
    if (!ok) return std::unexpected(std::move(err));

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
    //
    // THE DEVIATION CHAIN RIDES WITH THE TEMPO (architect approval
    // 2026-09-18) because it IS the tempo's spelling: the parse resolved the
    // total into tempo_cents and left the terms beside it, so copying both
    // is what makes a typed `1.23+0.01` survive the commit. THE TWO ARMS
    // THAT WRITE A NON-OWNER CLEAR IT, and both are here: the pass arm
    // explicitly, and the second arm by assignment — a label ref parses with
    // an empty chain by grammar (`a.aa+0.01` is load-fatal), so the copy IS
    // the clear on that form and no third road exists.
    if (parsed.tempo_inherits) {
        m.tempo_inherits = true;
        m.tempo_cents    = 100;
        m.tempo_scale.reset();
        m.tempo_deviation_cents.clear();
        m.label_def      = parsed.label_def;
        m.label_ref.clear();
    } else {
        m.tempo_inherits = false;
        m.tempo_cents    = parsed.tempo_cents;
        m.tempo_scale    = parsed.tempo_scale;
        m.tempo_deviation_cents = parsed.tempo_deviation_cents;
        m.label_def      = parsed.label_def;
        m.label_ref      = parsed.label_ref;
    }
    m.time_frame = preserved_time;
    // disabled is not the editor's field — the candidate carried the marker's
    // own bit, parse_single_canonical_line populated it; reapply.
    m.disabled      = parsed.disabled;

    // Cascade rename: if label_def changed to another NON-EMPTY name,
    // every other marker that referenced old_def gets its ref updated to
    // the new name. Removal (new_def empty) deliberately does NOT
    // cascade: clearing a ref would leave that marker serializing as an
    // owning 0.00 line (a pure ref parses with tempo_cents 0), which the
    // parser rejects on reload. Dangling refs, by contrast, load fine and
    // normalize to a plain 1.00 owner at the render boundary — so the
    // refs are left pointing at the removed name.
    // THE RENAME CARRIES THE REFERENCES ONLY FROM THE DEFINITION THEY BIND
    // (2026-10-29, the label rule at label_def_taken, where two definitions
    // of one label became legal): the label's ONE ENABLED definition, or,
    // with none enabled, its one definition at all — the raw-store tier
    // marker_effective's ref branch reads (warp_frame_map_build.h). Renaming
    // the live successor of a disabled definition carries the references with
    // it; renaming the disabled one leaves them with the live one rather than
    // dragging them into a struck label; with two live definitions (the
    // ambiguous label, every reference struck) neither rename carries them,
    // and the definition left behind takes them over, honored again.
    const auto edited_row_binds_old = [&]() {
        int enabled = 0;
        int total   = 0;
        for (const GuiWarpMarker& r : mv_const) {
            if (r.label_def != old_def) continue;
            ++total;
            if (!r.disabled) ++enabled;
        }
        return mv_const[idx].disabled ? (enabled == 0 && total == 1)
                                      : enabled == 1;
    };
    int n_refs_renamed = 0;
    if (!old_def.empty() && !new_def.empty() && old_def != new_def &&
        edited_row_binds_old()) {
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
    // double tap are both refused on a payload axis while the lamp stands
    // (authoring_locked, app_state.h) — and the label paste, this body's
    // other road, is refused there at the lock's gate (Shift+Return is on
    // no admission of iteration_lock_key_blocked). So the invariant "a committed
    // NON-CARRIER never keeps a bracket" holds vacuously here, and the clear
    // below is a BELT over two already-empty optionals — kept because it is
    // the store's own rule stated at the surface that could otherwise break
    // it (Ctrl+N's owner->pass / ref->pass carrier-loss clears are its
    // siblings), and because it costs two resets. No clamp follows it: this
    // commit can move the base tempo, but never under a bracket.
    if (!iter_bracket_carrier(m)) {
        clear_iter_bracket(m);
        // AND THE TIE GOES WITH IT (2026-09-19), the belt's own second
        // clause: a non-carrier is no tie member, so a group id left behind
        // would let it govern somebody's cells.
        m.iter_tie_group = 0;
    }

    // Did any serialized field change? Cascade renames imply a label_def
    // change, already covered by the field compare below. THE DEVIATION
    // CHAIN IS A SERIALIZED FIELD AND IT IS ASKED HERE (architect approval
    // 2026-09-18): a commit that only RESPELLS — `1.24` typed as
    // `1.23+0.01` — moves no total and no other field, so without this term
    // the edit would leave the file changed with no undo entry behind it and
    // no Save lit (the dirty mark).
    const bool canonical_changed =
        m.tempo_inherits != before.tempo_inherits ||
        m.tempo_cents    != before.tempo_cents ||
        m.tempo_deviation_cents != before.tempo_deviation_cents ||
        m.tempo_scale    != before.tempo_scale ||
        m.label_def      != before.label_def ||
        m.label_ref      != before.label_ref ||
        m.disabled       != before.disabled ||
        n_refs_renamed > 0;

    // `canonical_changed` IS THE WHOLE STORE-CHANGE QUESTION: the bracket is
    // outside the undo domain (2026-09-10) and this editor cannot open under
    // a standing bracket, so the belt above clears nothing and no commit here
    // is bracket-only.

    PayloadCommit out;
    out.canonical_changed = canonical_changed;
    out.n_refs_renamed    = n_refs_renamed;
    out.old_def           = old_def;
    out.new_def           = new_def;
    out.proposed          = std::move(proposed);
    return out;
}

} // namespace

// THE PAYLOAD REFUSAL'S ONE SENTENCE, stderr and the card (the struct's
// THE REFUSAL'S REASON): the editor's commit and the label paste say a
// refused line in the same words.
void GuiFlagEditor::card_payload_refusal(const std::string& err) {
    const std::string refusal = "Edit rejected: " + err;
    std::fprintf(stderr, "warptempo_gui: %s\n", refusal.c_str());
    notifications.notify(AppState::NotificationClass::Normal, refusal);
}

// THE CHANGED PAYLOAD WRITE'S TAIL, the editor's commit's and the label
// paste's alike, run after a store write that moved a serialized field.
// `reland_idx` is the marker the target-view re-land follows: the editor's
// target, or the paste's focus.
void GuiFlagEditor::payload_commit_tail(int reland_idx) {
    // Unconditional by ruling — rationale at GuiTargetRender::trigger. Any
    // store change repaints and triggers.
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
    // image through reseat_playhead_to, a TRANSLATION and no movement (the
    // rule at Viewport::move_playhead_to, viewport.cpp). SOURCE VIEW NEEDS
    // NOTHING: identity domain, no image
    // moves.
    // AND canonical_changed IS THE SECOND TERM — the callers', who run this
    // tail on a changed write alone — which is exactly the map-input
    // set (tempo_inherits / tempo_cents / tempo_scale / label_def / label_ref /
    // disabled / renamed refs; position is not editable here) and which is
    // also the whole store-change question the commit asks: no BRACKET-ONLY
    // commit can happen under the iteration lock (2026-09-10), so one gate
    // serves both. THE LABEL PASTE
    // re-lands on ITS FOCUS (app.last_selected_marker, a member of the pasted
    // selection), the group verbs' own re-land (toggle_disabled_body and
    // toggle_inherits, warpmarkers_ops.cpp).
    if (app.active_audio_view == 'T') {
        viewport.kick_waveform_sync();
        const auto& mv_post = app.warpmarkers.markers();
        if (reland_idx >= 0 &&
            reland_idx < static_cast<int>(mv_post.size())) {
            viewport.reseat_playhead_to(source_frame_to_active_domain(
                app, audio, mv_post[reland_idx].time_frame));
        }
    }
    target_render.trigger();
}

// The flag editor's commit: the session's pending line through the payload
// commit's one owner (propose_payload_commit, above), pushing one undo entry
// covering all touched markers.
//
// On failure: selects the whole pending text (text_editor::refuse), cards
// the reason, leaves the editor active.
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

    std::expected<PayloadCommit, std::string> commit =
        propose_payload_commit(mv_const, idx, app.top_flag_editor.pending);
    if (!commit) {
        text_editor::refuse(app.top_flag_editor);
        viewport.invalidate_top_strip();
        card_payload_refusal(commit.error());
        return;
    }
    const bool canonical_changed = commit->canonical_changed;

    // A COMMIT THAT CHANGES THE MARKER IS A MARKER MUTATION AND STOPS (architect
    // 2026-10-14, the keyboard stop rule at stop_playback_if_playing's
    // declaration): past the refusal above and ahead of the write, and only on
    // a changed commit. The open already stopped play (architect 2026-11-20;
    // stop_playback_for_modal_open's decision table) and nothing
    // starts one under the editor, so this finds nothing to stop; it stays as
    // the keyboard stop rule's site on the changed write, the shape the label
    // paste's stop shares (paste_payload_onto_selection).
    if (canonical_changed) playback_lifecycle.stop_playback_if_playing();
    // Capture pre-state for undo BEFORE mutating.
    std::vector<GuiWarpMarker> pre_state = mv_const;
    app.warpmarkers.markers_mut() = std::move(commit->proposed);

    if (commit->n_refs_renamed > 0) {
        std::fprintf(stderr,
            "warptempo_gui: Renamed label_def '%s' -> '%s'; "
            "updated %d refs\n",
            commit->old_def.c_str(), commit->new_def.c_str(),
            commit->n_refs_renamed);
    }

    if (canonical_changed) {
        undo.push_undo_warp(std::move(pre_state));
    }

    text_editor::deactivate(app.top_flag_editor);

    if (!canonical_changed) return;

    payload_commit_tail(idx);
}

// THE LABEL PASTE (architect 2026-11-10: EDIT's hold "replaces the whole
// label on every selected marker with the clipboard's text, without opening
// the editor, through the editor's own validation"; the contract at the
// declaration). PER SELECTED MARKER, in the store's order, what opening its
// editor, selecting all, pasting and pressing Enter would do: a scratch
// session of the flag editor's kind seeded with the marker's line, the whole
// line selected (enter_text_edit's open-selected), the clipboard through
// THE ONE
// INCOMING FILTER (text_editor::replace_selection — its vocabulary and the
// kind's byte cap, no second derivation), then the line through the
// commit's one owner (propose_payload_commit) against the store AS THE
// EARLIER MARKERS OF THIS PASTE LEFT IT — so a pasted label definition
// meets the duplicate refusal at its second marker exactly as the second
// editor's Enter would. ALL OR NOTHING: every marker is judged before any is
// written; the first refusal cards its own sentence — the paste's
// over-capacity card, or the commit's "Edit rejected: …" — and the store
// stands untouched. A marker whose line already says the pasted one moves
// no serialized field, and A PASTE THAT CHANGES NO MARKER writes nothing: no
// undo entry, no stop, no damage. Otherwise ONE UNDO ENTRY covers the whole
// act, a marker mutation that stops a live play as the editor's changed
// commit does, with that commit's tail (payload_commit_tail: the dirty
// mark, the lanes' repaint, the target-view re-warp and the focus's
// re-land, the render trigger). THE SELECTION, THE FOCUS AND THE PLAYHEAD
// STAND: nothing here selects or moves (the target-view re-land is a
// translation onto the focus's own image, the family's contract at
// warpmarkers_ops.cpp's head).
void GuiFlagEditor::paste_payload_onto_selection(
        const std::string& clipboard_text) {
    // AN EMPTY CLIPBOARD PASTES NOTHING, silently — the editor's own paste
    // (GuiInputHandler::apply_editor_clipboard).
    if (clipboard_text.empty()) return;
    const std::vector<GuiWarpMarker>& mv_const = app.warpmarkers.markers();
    std::vector<GuiWarpMarker> proposed = mv_const;
    std::vector<std::string>   rename_lines;
    bool changed = false;
    for (const int idx : app.selected_markers) {
        // The stale-index belt's skip, the group verbs' own
        // (toggle_disabled_body, warpmarkers_ops.cpp).
        if (idx < 0 || idx >= static_cast<int>(proposed.size())) continue;
        text_editor::State scratch;
        text_editor::enter(scratch, idx, flag_text(proposed, idx),
                           text_editor::Kind::FlagPayload);
        if (!scratch.pending.empty()) {
            scratch.selection_anchor = 0;
            scratch.cursor_pos = static_cast<int>(scratch.pending.size());
        }
        if (!text_editor::replace_selection(scratch, clipboard_text)) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kPasteTooLongCard);
            return;
        }
        std::expected<PayloadCommit, std::string> commit =
            propose_payload_commit(proposed, idx, scratch.pending);
        if (!commit) {
            card_payload_refusal(commit.error());
            return;
        }
        changed = changed || commit->canonical_changed;
        if (commit->n_refs_renamed > 0) {
            rename_lines.push_back(
                "Renamed label_def '" + commit->old_def + "' -> '" +
                commit->new_def + "'; updated " +
                std::to_string(commit->n_refs_renamed) + " refs");
        }
        proposed = std::move(commit->proposed);
    }
    if (!changed) return;

    // A PASTE THAT CHANGES A MARKER IS A MARKER MUTATION AND STOPS, the
    // editor's changed commit's own rule (above), ahead of the write.
    playback_lifecycle.stop_playback_if_playing();
    std::vector<GuiWarpMarker> pre_state = mv_const;
    app.warpmarkers.markers_mut() = std::move(proposed);
    for (const std::string& line : rename_lines)
        std::fprintf(stderr, "warptempo_gui: %s\n", line.c_str());
    undo.push_undo_warp(std::move(pre_state));
    payload_commit_tail(app.last_selected_marker);
}

// -- THE EDITOR'S EXTRA KEYS (architect 2026-11-20 / 21) -------------------
//
// The three acts' contract, his words and the rules each follows are at the
// declarations (flag_editor.h); the keys' seat and grey at
// onscreen_keyboard.h's THE EDITOR'S EXTRA KEYS; the router
// handle_top_flag_editor_key (input_key_dispatch.cpp).

// THE NOT-WELL-FORMED CARD: one sentence on the card, in words that say the
// text was judged, not committed. NO STDERR LINE, as the keys' other refusal
// cards write none (the label reference's, the coincidence's): a held UP on
// a malformed text fires once per synthesized repeat, the card coalescing
// (held_repeat_dispatch) where a line per fire would not.
void GuiFlagEditor::card_unparsed_editor_text(const std::string& sentence) {
    notifications.notify(AppState::NotificationClass::Normal, sentence);
}

// THE WHOLE-TEXT REWRITE: the whole text selected and replaced through THE
// ONE INCOMING FILTER (text_editor::replace_selection), so the caret lands at
// the new text's end with no selection (an empty text has no selection to
// replace, so the caret seats at 0 first and the insertion carries it to the
// end). ITS CAP REFUSAL HAS NO PRODUCER: every text these acts write is the
// canonical spelling of a line the commit's parse admits — the flag's
// through flag_text, inside kMaxPendingChars, a tight bound on every
// well-formed line (text_editor.h), a bound's through its own formatter,
// the field's exact width — so the answer is not asked. The lane's damage is
// the box's (the strip, as every keystroke's).
void GuiFlagEditor::rewrite_editor_text(const std::string& text) {
    text_editor::State& ed = app.top_flag_editor;
    if (ed.pending.empty()) {
        ed.selection_anchor = -1;
        ed.cursor_pos       = 0;
    } else {
        ed.selection_anchor = 0;
        ed.cursor_pos       = static_cast<int>(ed.pending.size());
    }
    (void)text_editor::replace_selection(ed, text);
    viewport.invalidate_top_strip();
}

void GuiFlagEditor::step_editor_text(int delta) {
    const text_editor::State& ed = app.top_flag_editor;
    if (!text_editor::is_active(ed)) return;
    const int idx = ed.target;

    if (ed.kind == text_editor::Kind::IterBound) {
        // THE BOUND STEP ON THE TEXT'S BOUND (the declaration). The column is
        // the live one, which is the open's (commit_iter_bound_edit's own
        // reading). An empty text is the blank, whose cells read zero, the
        // main page's step's start from a blank bracket; any other text must
        // be the bound commit's grammar. The tie follower and the ineligible
        // marker, the main page's two other refusals, cannot stand under
        // this editor (enter_iter_bound_edit refuses to open on either).
        //
        // AN EMPTY TEXT STEPS ONLY WHERE IT WOULD COMMIT (architect
        // 2026-11-21): read as if committed, an erased bound is the
        // number zero and the other bound stays as stored
        // (commit_iter_bound_edit), so where zero falls on the wrong side of
        // the stored other bound the empty text is a refused commit with no
        // start to step from — the step cards the commit's own sentence
        // (iter_bound_partner_refusal, the one judge; the card alone, no
        // stderr line, card_unparsed_editor_text's reason) and the text stays
        // empty. Everywhere else the step starts from zero and walls at the
        // stored partner as a step from any text does.
        const MarkerCell side = iter_bound_editor_side(ed);
        auto empty_text_refused = [&](int64_t partner) {
            if (!ed.pending.empty()) return false;
            const char* why = iter_bound_partner_refusal(side, 0, partner);
            if (why == nullptr) return false;
            notifications.notify(AppState::NotificationClass::Normal,
                                 range_bound_rejected(why));
            return true;
        };
        if (app.active_markers_view == 'P') {
            const std::vector<GuiPhaseResetMarker>& pv =
                app.phaseresetmarkers.markers();
            if (idx < 0 || idx >= static_cast<int>(pv.size())) return;
            int start = 0;
            if (!ed.pending.empty() && !parse_signed_hops(ed.pending, start)) {
                card_unparsed_editor_text(
                    std::string("The bound is not well formed: ") +
                    kHopBoundGrammar);
                return;
            }
            const GuiPhaseResetMarker& p = pv[static_cast<size_t>(idx)];
            if (empty_text_refused(side == MarkerCell::Upper
                                       ? p.iter_start_hops.value_or(0)
                                       : p.iter_end_hops.value_or(0)))
                return;
            const int landing = phase_iter_bound_step_landing_from(
                app, audio, idx, side, start, delta);
            if (landing == start) return;   // the wall, silent
            rewrite_editor_text(format_signed_hops(landing));
            return;
        }
        const std::vector<GuiWarpMarker>& mv = app.warpmarkers.markers();
        if (idx < 0 || idx >= static_cast<int>(mv.size())) return;
        int64_t start = 0;
        if (!ed.pending.empty() && !parse_signed_2dp_cents(ed.pending, start)) {
            card_unparsed_editor_text(
                std::string("The bound is not well formed: ") +
                kCentBoundGrammar);
            return;
        }
        const GuiWarpMarker& w = mv[static_cast<size_t>(idx)];
        if (empty_text_refused(side == MarkerCell::Upper
                                   ? w.iter_start_cents.value_or(0)
                                   : w.iter_end_cents.value_or(0)))
            return;
        const int64_t landing =
            iter_bound_step_landing_from(mv, idx, side, start, delta);
        if (landing == start) return;   // the wall, silent
        rewrite_editor_text(format_signed_delta_cents(landing));
        return;
    }

    if (ed.kind != text_editor::Kind::FlagPayload) return;
    const std::vector<GuiWarpMarker>& mv = app.warpmarkers.markers();
    if (idx < 0 || idx >= static_cast<int>(mv.size())) return;
    std::expected<GuiWarpMarker, std::string> line =
        parse_payload_line(mv[static_cast<size_t>(idx)], ed.pending);
    if (!line) {
        card_unparsed_editor_text("The flag is not well formed: " +
                                  line.error());
        return;
    }
    GuiWarpMarker m = std::move(*line);
    // THE KIND REFUSAL, the singleton arm's, in its order: the reference
    // asked of the TEXT (it is the text that would be stepped), the
    // coincidence of the store's row (tempo_cent_step_kind_refusal_for's two
    // tests; the coincident one shared, tempo_cent_step_coincident_refusal_
    // for). Both card, as the main page's key does.
    if (!m.label_ref.empty()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kLabelRefHasNoTempoCard);
        return;
    }
    if (const char* refusal =
            tempo_cent_step_coincident_refusal_for(app, audio, idx)) {
        notifications.notify(AppState::NotificationClass::Normal, refusal);
        return;
    }
    // THE SINGLETON ARM'S LOOP BODY on the parsed flag (adjust_tempo_cents,
    // warpmarkers_ops.cpp): the start through the one seed body (a pass
    // resolved through the projection of THE STORE AS IF THE TEXT WERE
    // COMMITTED, resolved_as_if_committed — so a `pass` text freezes at what
    // a pass here inherits, whatever the committed row holds; an owner text
    // answers its own fields and reads no store), the move through the one
    // landing owner (the chain's last term riding the step), the write
    // through the one write site, the start's scale written beside.
    const std::vector<WarpMarker> resolved =
        resolved_as_if_committed(mv, idx, m);
    const WarpTempoStart start = warp_tempo_step_start(
        m, resolved, idx, audio.sample_rate(), audio.total_frames());
    const WarpTempoStepMove move = warp_tempo_step_move(
        start.cents, m.tempo_deviation_cents, static_cast<int64_t>(delta));
    // THE BRACKET'S END (or the last term's wall) IS THE SILENT WALL: an
    // owner whose landing is its own value. A pass always freezes, so it
    // always changes.
    if (!m.tempo_inherits && move.cents == m.tempo_cents) return;
    m.tempo_inherits = false;
    warp_tempo_step_write(m, move);
    m.tempo_scale = start.scale;
    // SPELLED BACK BY THE FLAG'S OWN FORMATTER (flag_text, the editor's
    // seed's composer), the label definition and the scale with it.
    rewrite_editor_text(flag_text(std::vector<GuiWarpMarker>{m}, 0));
}

void GuiFlagEditor::toggle_inherit_editor_text() {
    const text_editor::State& ed = app.top_flag_editor;
    if (!text_editor::is_active(ed) ||
        ed.kind != text_editor::Kind::FlagPayload)
        return;
    const int idx = ed.target;
    const std::vector<GuiWarpMarker>& mv = app.warpmarkers.markers();
    if (idx < 0 || idx >= static_cast<int>(mv.size())) return;
    std::expected<GuiWarpMarker, std::string> line =
        parse_payload_line(mv[static_cast<size_t>(idx)], ed.pending);
    if (!line) {
        card_unparsed_editor_text("The flag is not well formed: " +
                                  line.error());
        return;
    }
    GuiWarpMarker m = std::move(*line);
    // THE VERB'S THREE ARMS (GuiWarpMarkersOps::toggle_inherits,
    // warpmarkers_ops.cpp), on the parsed flag, in its order. The bracket
    // and tie clears of its two carrier-loss arms have nothing to clear
    // here: the flag editor cannot open while grid iterations stands
    // (authoring_locked), and the commit's own belt clears them anyway.
    if (!m.label_ref.empty()) {
        m.label_ref.clear();
        m.tempo_inherits = true;
        m.tempo_cents    = 100;
        m.tempo_scale.reset();
        m.tempo_deviation_cents.clear();
    } else if (m.tempo_inherits) {
        // THE FREEZE: the tempo this marker would sound at with the text
        // committed, marker_effective over THE STORE AS IF THE TEXT WERE
        // COMMITTED (resolved_as_if_committed: the row the parsed `pass`, so
        // the answer is what a pass here inherits, never the committed
        // row's own number; the verb's projection-aware walk; a coincident
        // member its 1.00), the plain base it lands as — no chain, the verb's
        // own rule. base_cents 0 ("could not resolve") is the verb's
        // unreachable fallback, mirrored.
        const MarkerEffective eff =
            marker_effective(resolved_as_if_committed(mv, idx, m), idx,
                             audio.sample_rate(), audio.total_frames());
        m.tempo_inherits = false;
        if (eff.base_cents != 0) {
            m.tempo_cents = eff.base_cents;
            m.tempo_scale = eff.scale;
        } else {
            m.tempo_cents = 100;
            m.tempo_scale.reset();
        }
        m.tempo_deviation_cents.clear();
    } else {
        m.tempo_inherits = true;
        m.tempo_cents    = 100;
        m.tempo_scale.reset();
        m.tempo_deviation_cents.clear();
    }
    rewrite_editor_text(flag_text(std::vector<GuiWarpMarker>{m}, 0));
}

bool GuiFlagEditor::complete_label_reference() {
    const text_editor::State& ed = app.top_flag_editor;
    if (!text_editor::is_active(ed) ||
        ed.kind != text_editor::Kind::FlagPayload)
        return false;
    const std::string& text = ed.pending;
    if (text.empty()) return false;
    const std::vector<GuiWarpMarker>& mv = app.warpmarkers.markers();
    // THE CANDIDATES: every distinct defined label the whole text is a
    // proper prefix of, the row under edit's own definition excluded (the
    // declaration). A label is the grammar's four bytes, so a text of four
    // or more has nothing to extend, and a text no label starts with —
    // garbage, an owner, a definition — has no candidate.
    std::string run;
    bool any = false;
    for (int i = 0; i < static_cast<int>(mv.size()); ++i) {
        if (i == ed.target) continue;
        const std::string& label = mv[static_cast<size_t>(i)].label_def;
        if (label.size() <= text.size() || !label.starts_with(text)) continue;
        if (!any) {
            run = label;
            any = true;
            continue;
        }
        // THE LONGEST RUN EVERY CANDIDATE SHARES, narrowed one label at a
        // time (never below the text, which every candidate starts with).
        std::size_t k = 0;
        while (k < run.size() && k < label.size() && run[k] == label[k]) ++k;
        run.resize(k);
    }
    if (!any || run.size() <= text.size()) return false;
    rewrite_editor_text(run);
    return true;
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
// iteration-mode exit route shares, TWO routes: the `i` toggle's turning-off
// branch and the iteration sweep's success tail — exiting the mode is the
// clear on every route, so a bracket exists only while the mode paints it on
// the flags. The BPM opener is no route: it is REFUSED under a lit lamp
// (architect 2026-09-10: NO SILENT SWAPS; enter_bpm_mode), so bare `i` is the
// only key that reaches this. THE S->T AUDIO-VIEW TOGGLE IS NO ROUTE
// (2026-08-07): iteration mode is target-legal, so entering target view
// neither exits the mode nor clears anything (the record is at
// switch_active_audio_view_to, input_handler.cpp). BOTH callers can run in
// TARGET view, where the write is the granted home-view-binding exception
// argued at the sweep's tail (run_iteration_sweep_render,
// input_key_dispatch.cpp).
// THE LOAD IN PLACE IS NOT A ROUTE EITHER (architect 2026-09-02): iteration
// mode is where you stand, so apply_recipe_in_place leaves the mode bit alone
// and resets no bracket — and the load CANNOT RUN while the mode is lit at
// all, the iteration lock refusing it on every road, so the ruling holds
// vacuously (the record is at that body, input_key_dispatch.cpp).
// IT PUSHES NOTHING AT ALL (architect 2026-09-10: "Iterations are the least
// durable thing in this GUI as far as the flags. It's okay to clear them and
// not remember them very often. They just don't go in the undo stack at all;
// they're considered transient by design"). The bracket is outside the undo
// domain whole, and a wipe that pushed would be the one entry able to put a
// bracket back. So the clear is FINAL —
// "a bracket exists only on a marker that is on screen or it is gone for
// good" — and it moves no dirty flag either, neither bracket ever having
// serialized.
// Callers own the mode-flag flip and the repaint invalidation.
// AND IT PUTS AN ADDRESSED BOUND CELL BACK ON THE PAYLOAD (architect
// 2026-09-04): the cells go with the mode, so a Lower or Upper axis
// (AppState::addressed_cell) falls back to Payload here, ahead of the
// bracket test, because this body is the one thing every exit from the mode
// runs — there is no single mode setter, the two writers of the off edge
// each flip the bit themselves after calling this — so a step outside the
// mode can only ever be the tempo step, on either column. History-less like
// everything else here: the axis is a session address, not content.
// AND IT DISSOLVES EVERY TIE WITH THE BRACKETS (2026-09-19): a tie is the
// bracket's own kind of session state (GuiWarpMarker::iter_tie_group,
// warpmarkers.h) and leaves by the same door, on both columns.
void GuiFlagEditor::wipe_iter_state() {
    if (app.addressed_cell == MarkerCell::Lower ||
        app.addressed_cell == MarkerCell::Upper) {
        app.addressed_cell = MarkerCell::Payload;
    }
    // The two scans read the stores CONST, so a bracketless exit bumps neither
    // generation and rebuilds no cache: markers_mut is what bumps, and it is
    // reached only past the test below.
    // EACH SCAN ASKS THE TIE TOO (2026-09-19): a store can carry ties and no
    // bracket at all — every follower is bracketless by rule, and a leader's
    // own pair may be blank — so a bracket-only test would leave a tie
    // standing past the mode that authored it.
    bool warp_any = false;
    for (const auto& m : app.warpmarkers.markers()) {
        if (m.iter_start_cents.has_value() || m.iter_end_cents.has_value() ||
            m.iter_tie_group != 0) {
            warp_any = true;
            break;
        }
    }
    bool phase_any = false;
    for (const auto& p : app.phaseresetmarkers.markers()) {
        if (p.iter_start_hops.has_value() || p.iter_end_hops.has_value() ||
            p.iter_tie_group != 0) {
            phase_any = true;
            break;
        }
    }
    if (!warp_any && !phase_any) return;
    // Each column is touched only where it has something to clear, for the
    // same reason: an untouched store keeps its generation.
    if (warp_any) {
        for (auto& m : app.warpmarkers.markers_mut()) {
            clear_iter_bracket(m);
            m.iter_tie_group = 0;
        }
    }
    if (phase_any) {
        for (auto& p : app.phaseresetmarkers.markers_mut()) {
            clear_iter_bracket(p);
            p.iter_tie_group = 0;
        }
    }
}

// Wipe every marker's session-only bpm state — owner flag, beats, bracket
// bounds, and span endpoint back to their defaults. Runs on every bpm-mode
// exit (the single chokepoint exit_bpm_mode, which the sweep's dispatch and
// every road out of the BPM form reach), so a value exists only while the
// mode is live; re-entering bpm mode always opens a BLANK form. Its one other caller is
// the load in place (apply_recipe_in_place), where the mode is off by
// construction and the call is a statement over a set that already carries
// defaults — never a mode exit.
//
// History-less on purpose: bpm values are session-only with no undo of their
// own (see commit_bpm_field), so no undo entry is pushed, and undo can never
// resurrect bpm state — the bpm session is modal, so no snapshot-taking op can
// run while a marker carries live bpm fields, and outside the session every
// marker is already wiped. THE ITER WIPE IS THE SAME SHAPE (2026-09-10): it
// pushes nothing either, and the bracket's own snapshots are stripped at
// every push. Callers own the repaint.
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

// THE BPM FORM'S FIELD COMMIT (the contract is at the declaration). The
// refusal is the editors' one shape and the settings editor's words' shape:
// the whole value selected, the editor standing, one composed sentence on
// stderr and the card.
void GuiFlagEditor::commit_bpm_field() {
    if (!text_editor::is_active(app.settings_editor) ||
        app.settings_editor.kind != text_editor::Kind::BpmField)
        return;
    const int owner = bpm_owner_index(app.warpmarkers.markers());
    const int field = app.settings_editor.target;
    // THE BUFFER IS THE FIELD'S VALUE, THE KEY THE ROW'S OWN (architect
    // 2026-11-19): nothing typed can name another field.
    const std::string  key   = kBpmFieldKeys[field];
    const std::string& value = app.settings_editor.pending;
    auto reject = [&](const std::string& reason) {
        text_editor::refuse(app.settings_editor);
        viewport.invalidate_dialog_editor_box();
        const std::string refusal = "BPM edit rejected: " + reason;
        std::fprintf(stderr, "warptempo_gui: %s: %s=%s\n",
            refusal.c_str(), key.c_str(), value.c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
    };
    GuiWarpMarker& m = app.warpmarkers.markers_mut()[owner];
    switch (static_cast<GuiBpmField>(field)) {
        case GuiBpmField::Beats: {
            int beats = 0;
            if (!value.empty() && !read_bpm_beats(value, beats)) {
                reject("beats must be a whole number from 1 to " +
                       std::to_string(kBpmBeatsMax));
                return;
            }
            m.bpm_beats = beats;
            break;
        }
        case GuiBpmField::Low:
        case GuiBpmField::High: {
            double bpm = 0.0;
            if (!value.empty() && !read_bpm_tempo(value, bpm)) {
                reject(key + " must be a tempo from " +
                       format_value_double(kBpmMin, 0) + " to " +
                       format_value_double(kBpmMax, 0) +
                       " bpm, with no trailing zeros");
                return;
            }
            (field == static_cast<int>(GuiBpmField::Low) ? m.bpm_lo
                                                          : m.bpm_hi) = bpm;
            break;
        }
    }
    // The editor's close is the box's damage; the form the close hook
    // reopens damages the whole window (open_bpm_form).
    viewport.invalidate_modal_dialog_area();
    text_editor::deactivate(app.settings_editor);
}

// THE BPM FORM'S RENDER GATE (the contract is at the declaration). Every
// sweep cell carries a derived base tempo into its cell markers, a derived
// scale into its cell .settings, and a rescaled tempo (2026-08-26) into
// every effectively ENABLED owning marker outside the span (a disabled one
// is invisible to the act, 2026-08-29, in the span and out of it, so it can
// neither be rescaled nor refuse a bracket); the derivation
// (compute_base_tempo_scale) is monotone in bpm and the rescale rides it, so
// the bracket ends bound every cell: if either endpoint bpm refuses — the
// derived base tempo lands outside [kTempoMinCents, kTempoMaxCents], the
// derived scale outside [kScaleMin, kScaleMax], or any rescaled marker
// outside the tempo bracket — the press refuses with its card. Never clamp:
// a clamped derivation would silently mistune the span. Gated on a
// well-formed span (owner before endpoint, positive duration); without one,
// render_bpm_sweep early-bails and derives nothing. The form and its field
// editor are modal to the store — no authoring act runs while either stands
// — so n here equals the store size the Ctrl+B gate recorded bpm_endpoint
// against.
bool GuiFlagEditor::bpm_sweep_bracket_admits() {
    const auto& mv = app.warpmarkers.markers();
    const int idx = bpm_owner_index(mv);
    if (idx < 0) return false;
    const GuiWarpMarker& o = mv[idx];
    const int endpoint_idx = o.bpm_endpoint;
    const int n = static_cast<int>(mv.size());
    // The span-end frame is the boundary marker's time when one exists — the
    // NEXT EFFECTIVELY-ENABLED marker after the last selected one, which is
    // what bpm_endpoint names (warpmarkers.h) — else the song end
    // (bpm_endpoint == n is the song-end sentinel).
    if (audio.sample_rate() <= 0 || endpoint_idx <= idx || endpoint_idx > n)
        return true;
    const int64_t span_end_frame =
        (endpoint_idx < n) ? mv[endpoint_idx].time_frame
                           : audio.total_frames();
    const double duration_seconds =
        (span_end_frame - o.time_frame) /
        static_cast<double>(audio.sample_rate());
    if (!(duration_seconds > 0.0)) return true;
    const auto refuse = [&](const std::string& refusal) {
        std::fprintf(stderr, "warptempo_gui: %s: %d at [%s, %s]\n",
            refusal.c_str(), o.bpm_beats,
            format_value_double(o.bpm_lo, 0).c_str(),
            format_value_double(o.bpm_hi, 0).c_str());
        notifications.notify(AppState::NotificationClass::Normal, refusal);
        return false;
    };
    const auto at_lo =
        compute_base_tempo_scale(duration_seconds, o.bpm_beats, o.bpm_lo);
    const auto at_hi =
        compute_base_tempo_scale(duration_seconds, o.bpm_beats, o.bpm_hi);
    if (!at_lo || !at_hi) {
        return refuse(
            "BPM edit rejected: derived tempo or scale outside "
            "its bracket (tempo [" +
            format_tempo_cents(kTempoMinCents) + ", " +
            format_tempo_cents(kTempoMaxCents) + "], scale [" +
            format_value_double(kScaleMin, 4) + ", " +
            format_value_double(kScaleMax, 4) + "])");
    }
    // The RESCALED MAP's arm of the same gate (2026-08-26): the sweep
    // rescales every effectively enabled owning marker outside the span by
    // the owner's change (bpm_cell_warp_markers, input_handler.h — the
    // sweep's own per-cell rewrite, run here at the two ends the
    // derivation's monotonicity makes sufficient), and a marker whose
    // rescaled tempo would leave the bracket refuses the press exactly like
    // an out-of-bracket derivation — never clamped, which would deform the
    // shape the rescale preserves.
    if (!bpm_cell_warp_markers(mv, idx, endpoint_idx,
                               at_lo->base_tempo_cents) ||
        !bpm_cell_warp_markers(mv, idx, endpoint_idx,
                               at_hi->base_tempo_cents)) {
        return refuse(
            "BPM edit rejected: a marker outside the span would "
            "leave the tempo bracket [" +
            format_tempo_cents(kTempoMinCents) + ", " +
            format_tempo_cents(kTempoMaxCents) + "] once rescaled");
    }
    return true;
}

// Full mode-on transition for BPM mode. Validates the activation gate,
// maintains the single-owner invariant, and marks the
// FIRST selected marker as the BPM owner (mode exit wipes the bpm state, so a
// fresh entry always opens a blank form), then flips the mode flag. The
// span endpoint is explicit — supplied by the `m` handler and recorded on the
// owner's bpm_endpoint (section-based, architect 2026-07-23) — so this does not
// auto-select an endpoint cue. The full section gate (non-empty, contiguous,
// ref-free) lives in the `m` handler; this route re-checks only that at least
// one marker is selected and the owner is eligible. The Ctrl+B handler calls
// this and then opens the BPM form (GuiInputHandler::open_bpm_form).
void GuiFlagEditor::enter_bpm_mode() {
    if (app.bpm_mode_enabled) return;
    if (app.active_markers_view != 'W') return;
    if (app.selected_markers.empty()) return;
    const int owner = *app.selected_markers.begin();   // first selected
    const auto& mv_const = app.warpmarkers.markers();
    if (owner < 0 || owner >= static_cast<int>(mv_const.size())) return;
    if (!bpm_popup_eligible_marker(mv_const[owner])) return;

    // NO FORCED ITER-OFF (architect 2026-09-10: "we should card the exit,
    // because it is still one button automatically affecting the other" —
    // NO SILENT SWAPS ANYWHERE). The BPM opener is REFUSED under a lit
    // iterations lamp, at the keyboard gate with the lock's own card and on
    // the BPM Iterations button's greyed face (iteration_lock_key_blocked and
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
    // values stay zero; bpm_field_text spells each as the EMPTY value, so the
    // BPM form opens on three blank lines. Every prior mode exit wiped the
    // bpm state, so a fresh entry always finds an untagged owner and opens a
    // blank form.
    if (!mv[owner].bpm_owner) {
        mv[owner].bpm_owner = true;
        mv[owner].bpm_beats          = 0;
        mv[owner].bpm_lo             = 0;
        mv[owner].bpm_hi             = 0;
    }

    // AND THE SELECTION IS SPENT (architect 2026-09-12, the lamps resolved by
    // use case): "use bpm" is one of the acts he named — the run has been
    // handed to the sweep, so Add to selection's building pass is over. It
    // stands past this route's five bails, on the flag flip itself, and it is
    // the LAMP's write alone: the selected run stays selected, being the span's
    // own cue (selection_consumed, app_state.h, where the class and its
    // inventory live).
    selection_consumed(app);
    app.bpm_mode_enabled = true;
    viewport.invalidate_top_strip();
}

// The single bpm-mode-off chokepoint: every route that turns bpm mode off
// funnels here. Wipes the session-only bpm state (mode exit IS the clear, so
// no marker carries bpm state once the mode is down) and repaints both the
// top strip and the MODAL's own lane — the controls row, where the BPM
// form's keys and its field editor stand.
void GuiFlagEditor::exit_bpm_mode() {
    if (!app.bpm_mode_enabled) return;
    app.bpm_mode_enabled = false;
    wipe_bpm_state();
    viewport.invalidate_top_strip();
    viewport.invalidate_modal_dialog_area();
}
