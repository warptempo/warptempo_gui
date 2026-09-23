#include "undo.h"

#include "input_handler.h"        // land_playhead_on_marker,
                                  // GuiInputHandler::switch_active_audio_view_to
                                  // — the S/T tag's restore chokepoint,
                                  // center_span_in_view — the restore visual
                                  // tail's group camera (frame_span_into_view
                                  // is its cannot-fit arm and is not called
                                  // from this TU)
#include "target_render.h"
#include "warp_frame_map_view.h"  // source_frame_to_active_domain, for the
                                  // singleton centre and the group camera,
                                  // and active_domain_to_source_frame for the
                                  // restore's map-change re-land

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace {

// THE ROW EQUALITY BASIS AND THE TOUCHED-SET RECONSTRUCTION BOTH LIVE IN
// app_state.h — warp_row_fields_differ, phase_reset_row_fields_differ,
// restore_touched_indices and the whole-list pair built on the first two
// (warp_rows_equal / phase_reset_rows_equal). They moved there 2026-09-04 so
// the Restrict undo to viewport lamp's face could ask the touched set a
// restore WOULD produce; that measurement left with the camera restriction on
// 2026-09-22 (the lamp is Restrict Undo to Current View now and reads the
// entry's view tags alone), and they stay where they are because the header
// is a neutral home and nothing gains from the move back. This file is still
// their applier, and entry_restores_live_marker_stores below is still where a
// whole store is asked the row question.

// True when restoring `entry` would write back the marker stores THAT ARE
// ALREADY LIVE — the question the coalesced burst's net-zero pop asks
// (Undo::record_gesture, where the rule is stated). ALL THREE columns, because
// every entry carries the full set and a restore assigns each unconditionally,
// so a 'W' entry that a merged press returned to its snapshot is only
// byte-equal when the other two columns match too.
//
// THE STORES ARE THE WHOLE CONTENT the question has to consider, and the
// entry's THIRD payload — its engine settings block — needs no term of its own:
// the five coalescing kinds (GestureKind, undo.h, re-grepped 2026-09-16: the
// THREE position nudges — warp, phase reset and magnification level — and the
// two value steps, the tempo cent step and the magnification level step)
// write no engine setting, and no engine-settings writer can run between a
// burst's opener and a merged press without killing the stamp the merge was
// verdicted on. There are three of them, re-grepped at this writing
// (`app.engine_settings =`): the typed engine commit pushes an 'S' entry
// (push_settings_undo), the render-entry load in place pushes a both-columns
// entry (push_undo_both) — and EVERY push helper clears the stamp — while a
// file load resets the history whole. So entry.settings is provably the live
// block wherever this is asked.
// THE VIEW TAGS AND THE HINTS ARE NOT CONTENT EITHER: both merge arms already
// require the tab and the audio view to have stood still, the column is the
// entry's own op_mode, and the touched hints are presentation the restore
// derives a selection from — they die with the entry when it goes.
bool entry_restores_live_marker_stores(const AppState& app,
                                       const UndoEntry& entry) {
    // The whole-list compare is warp_rows_equal / phase_reset_rows_equal
    // (app_state.h), beside the row comparators they walk: the lamp's proposed
    // target map asks the warp face the same question of a marker list, so
    // "these two stores hold the same state" has one spelling.
    return warp_rows_equal(entry.snapshot, app.warpmarkers.markers()) &&
           phase_reset_rows_equal(entry.phase_reset_snapshot,
                                  app.phaseresetmarkers.markers()) &&
           magnification_level_rows_equal(
               entry.magnification_level_snapshot,
               app.magnificationlevelmarkers.markers());
}

}  // namespace

void Undo::recompute_dirty() {
    const auto& h = app.history;
    // THE FOUR FLAGS ARE SET AND CLEARED AS ONE, so the three arms below and
    // the per-entry walk each name all four (the flags' contract is at
    // AppState::warp_dirty; the magnification level column took its own on
    // 2026-09-15, when op_mode 'M' gained real producers and the walk's old
    // `else` would have called a level edit a warp change).
    const auto clear_all = [&] {
        app.warp_dirty                = false;
        app.phase_reset_dirty         = false;
        app.magnification_level_dirty = false;
        app.settings_dirty            = false;
    };
    // ONE ARM PER op_mode, exhaustive over the four tags an entry can carry.
    const auto light = [&](char m) {
        switch (m) {
            case 'P': app.phase_reset_dirty         = true; break;
            case 'M': app.magnification_level_dirty = true; break;
            case 'S': app.settings_dirty            = true; break;
            default:  app.warp_dirty                = true; break;   // 'W'
        }
    };
    if (!h.saved_valid) {
        app.warp_dirty                = true;
        app.phase_reset_dirty         = true;
        app.magnification_level_dirty = true;
        app.settings_dirty            = true;
    } else if (h.saved_distance == 0) {
        clear_all();
    } else if (h.saved_distance < 0) {
        // Saved is `n` undos behind the current cursor. The last n
        // entries of undo_stack moved us from saved baseline to current.
        clear_all();
        const int n  = -h.saved_distance;
        const int us = static_cast<int>(h.undo_stack.size());
        for (int i = std::max(0, us - n); i < us; ++i)
            light(h.undo_stack[i].op_mode);
    } else {
        // Saved is `n` redos ahead. The top n entries of redo_stack
        // would, if redone, take us back to the saved state.
        clear_all();
        const int n  = h.saved_distance;
        const int rs = static_cast<int>(h.redo_stack.size());
        for (int i = std::max(0, rs - n); i < rs; ++i)
            light(h.redo_stack[i].op_mode);
    }
    const bool was_dirty = app.dirty;
    app.dirty = app.warp_dirty || app.phase_reset_dirty ||
                app.magnification_level_dirty || app.settings_dirty;
    // THE DIRTY MARK HAS ONE SURFACE AND ONE DERIVE-OWNER. This is where
    // app.dirty is derived, so every mutation, save and undo/redo transition
    // passes through here.
    // EVERY ENTRY COUNTS (architect 2026-09-10): the walks above used to skip
    // the three session-only iteration-bracket entries, because a bracket
    // never reaches a sidecar and crossing one must not light the dot. The
    // bracket left the undo domain whole — nothing pushes it and every push
    // strips it from its snapshot — so nothing about a bracket reaches the
    // history and nothing about it reaches the mark. There is no class left to
    // skip.
    // ROW 8'S `*` (architect 2026-09-09) is that surface on both backends,
    // and it is READ, never pushed: the painter takes app.dirty straight out of
    // the state as the clock's own suffix, so what this tail owes it is DAMAGE,
    // and only ON A TRANSITION. This body runs after every command, and an
    // unconditional invalidate would repaint the bottom row on every keypress
    // for a mark that did not move. (The window title carried a second
    // asterisk until 2026-09-09, pushed from here through a seam setter; the
    // architect ruled the duplicate signal off and both are deleted, so damage
    // is the whole of what this tail does for the mark.)
    // (The load's own four-flag reset is the only other transition, and it
    // owes no damage of its own: load_file invalidates the whole window on
    // both sides of that assignment.)
    if (app.dirty != was_dirty) viewport.invalidate_status_cell_area();
}

// THE PUSH HELPERS ALL STRIP THE SESSION-ONLY ITERATION BRACKET from
// both snapshots they build (architect 2026-09-10: "They just don't go in the
// undo stack at all; they're considered transient by design"). It is done HERE
// rather than at the callers so it is one statement over every producer that
// exists or will exist, and it is what makes "a restore never installs a
// bracket" a property of the code: no entry can hold one, so neither can a
// counter-entry, which is built from live stores that themselves hold none
// while any push is reachable (the LOCK — while grid iterations stands, every
// act that would push refuses; authoring_locked, app_state.h).
void Undo::push_undo_warp(std::vector<GuiWarpMarker> pre_state,
                          std::vector<int> touched_snapshot,
                          std::vector<int> touched_live) {
    UndoEntry e;
    e.snapshot           = std::move(pre_state);
    e.phase_reset_snapshot = app.phaseresetmarkers.markers();
    e.magnification_level_snapshot = app.magnificationlevelmarkers.markers();
    strip_iter_fields(e.snapshot);
    strip_iter_fields(e.phase_reset_snapshot);
    e.settings           = capture_current_settings(app);
    e.op_mode            = 'W';
    e.tab                = app.active_tab_view;
    e.audio_view         = app.active_audio_view;
    e.touched_snapshot   = std::move(touched_snapshot);
    e.touched_live       = std::move(touched_live);
    app.history.push(std::move(e));
    last_gesture_kind_ = GestureKind::None;   // see coalesce_gesture
}

void Undo::push_undo_phase_reset(std::vector<GuiPhaseResetMarker> pre_state,
                               std::vector<int> touched_snapshot,
                               std::vector<int> touched_live) {
    UndoEntry e;
    e.snapshot           = app.warpmarkers.markers();
    e.phase_reset_snapshot = std::move(pre_state);
    e.magnification_level_snapshot = app.magnificationlevelmarkers.markers();
    strip_iter_fields(e.snapshot);
    strip_iter_fields(e.phase_reset_snapshot);
    e.settings           = capture_current_settings(app);
    e.op_mode            = 'P';
    e.tab                = app.active_tab_view;
    e.audio_view         = app.active_audio_view;
    e.touched_snapshot   = std::move(touched_snapshot);
    e.touched_live       = std::move(touched_live);
    app.history.push(std::move(e));
    last_gesture_kind_ = GestureKind::None;   // see coalesce_gesture
}

void Undo::push_undo_magnification_level(
        std::vector<GuiMagnificationLevelMarker> pre_state,
        std::vector<int> touched_snapshot,
        std::vector<int> touched_live) {
    UndoEntry e;
    e.snapshot           = app.warpmarkers.markers();
    e.phase_reset_snapshot = app.phaseresetmarkers.markers();
    e.magnification_level_snapshot = std::move(pre_state);
    strip_iter_fields(e.snapshot);
    strip_iter_fields(e.phase_reset_snapshot);
    e.settings           = capture_current_settings(app);
    e.op_mode            = 'M';
    e.tab                = app.active_tab_view;
    e.audio_view         = app.active_audio_view;
    e.touched_snapshot   = std::move(touched_snapshot);
    e.touched_live       = std::move(touched_live);
    app.history.push(std::move(e));
    last_gesture_kind_ = GestureKind::None;   // see coalesce_gesture
}

void Undo::push_undo_both(
        std::vector<GuiWarpMarker> warp_pre,
        std::vector<GuiPhaseResetMarker> phase_reset_pre,
        std::vector<GuiMagnificationLevelMarker> magnification_level_pre,
        char op_mode) {
    UndoEntry e;
    e.snapshot           = std::move(warp_pre);
    e.phase_reset_snapshot = std::move(phase_reset_pre);
    e.magnification_level_snapshot = std::move(magnification_level_pre);
    strip_iter_fields(e.snapshot);
    strip_iter_fields(e.phase_reset_snapshot);
    e.settings           = capture_current_settings(app);
    e.op_mode            = op_mode;
    e.tab                = app.active_tab_view;
    e.audio_view         = app.active_audio_view;
    app.history.push(std::move(e));
    last_gesture_kind_ = GestureKind::None;   // see coalesce_gesture
}

void Undo::push_settings_undo(SettingsSnapshot pre_state) {
    UndoEntry e;
    e.snapshot           = app.warpmarkers.markers();
    e.phase_reset_snapshot = app.phaseresetmarkers.markers();
    e.magnification_level_snapshot = app.magnificationlevelmarkers.markers();
    strip_iter_fields(e.snapshot);
    strip_iter_fields(e.phase_reset_snapshot);
    e.settings           = std::move(pre_state);
    e.op_mode            = 'S';
    e.tab                = app.active_tab_view;
    e.audio_view         = app.active_audio_view;
    app.history.push(std::move(e));
    last_gesture_kind_ = GestureKind::None;   // see coalesce_gesture
    recompute_dirty();
}

void Undo::stamp_top_entry_with_landing_view() {
    // The caller's push is the precondition (the contract at the
    // declaration): a press that pushed leaves its entry on top, so an empty
    // stack is unreachable.
    assert(!app.history.undo_stack.empty());
    UndoEntry& top = app.history.undo_stack.back();
    top.tab        = app.active_tab_view;
    top.audio_view = app.active_audio_view;
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
    // THE KIND IS BLIND TO DIRECTION AND, SINCE 2026-08-31, TO MAGNITUDE (the
    // step ladder — R12: Ctrl+Up / Down steps three units and Shift+Up / Down
    // ten since 2026-09-21, shift the long stride).
    // The blindness is UNCHANGED rather than newly granted: a Left tap and a
    // Right tap inside the window have always merged into one entry, and a
    // bare tap followed by a shifted one now merges the same way. Both stay
    // right for the same reason — an undo entry restores a SNAPSHOT taken
    // before the run, so what the run did between them is not the entry's
    // business, and the one-entry-per-BURST rule is untouched either way (a
    // held key's repeats all carry the press's own chord, so a burst has one
    // magnitude by construction, and a held button's fires take the arm's
    // modifiers, which the arm cannot change mid-hold). Splitting on the
    // magnitude would need a fourth stamp field to buy nothing.
    //
    // EVERY CHANGE OF THE UNDO-STACK TOP CLEARS THE STAMP — the four push
    // helpers (push_undo_warp / push_undo_phase_reset /
    // push_undo_magnification_level / push_undo_both / push_settings_undo) and
    // restore_history_entry, the shared do_undo/do_redo
    // core, one line each — so a valid stamp can never coexist with a foreign stack
    // top, and that is what lets BOTH arms assume the top of the undo stack is the
    // burst's own entry (refresh_coalesced_touched_live's precondition). AND SO
    // DOES EVERY CHANGE OF THE SAVED REFERENCE (note_saved, 2026-09-02): the
    // save moves the reference onto the burst's live state without touching a
    // stack top, and a merge behind it would rewrite that state with no entry
    // between it and the file — so a valid stamp can never coexist with a
    // reference the burst has moved either, and both arms may also assume the
    // saved reference sits where the burst's own push left it (the pop belt at
    // UndoHistory::pop_undo_top_with_saved_ref rests on that).
    const bool stamp_matches =
        last_gesture_kind_ == kind && !app.history.undo_stack.empty();

    // (TWO KIND-SPECIFIC SUBJECT TERMS STOOD HERE — the ADDRESSED CELL from
    // 2026-09-04 and the W/P COLUMN from 2026-09-09, each read by
    // GestureKind::IterBoundStep alone, that kind's subject being a FIELD of
    // the selection on one of TWO stores rather than the selection itself.
    // Both went with the kind on 2026-09-10, when the iteration bracket left
    // the undo domain: the bound step pushes nothing, so it has no entry for a
    // later tap to merge into and no subject to keep apart. The five kinds
    // standing now (GestureKind, undo.h, re-grepped 2026-09-16: WarpNudge,
    // PhaseResetNudge, TempoStep, MagnificationLevelNudge,
    // MagnificationLevelStep) are one-body kinds whose subject the selection
    // carries whole — the two value steps' FIELDS keep apart by the kind
    // itself, each field its own kind.)

    bool merge = false;
    if (stamp_matches) {
        if (synthesized_repeat) {
            // ARM (1), REPEAT IDENTITY — NO CLOCK. A press the process
            // synthesized itself from a still-held input merges without a
            // clock, because the burst's structure already supplies the
            // adjacency a clock would enforce numerically. Three parts:
            // (a) and (b) buy the window, (c) tests the subject.
            // (a) Layer (1) of the platform's key-repeat contract (stated at
            // GuiInputCore::maybe_fire_repeat) disarms the
            // hold at every intervening pointer-button press, key press, and
            // completed wheel emission, so a synthesized repeat STRUCTURALLY
            // CANNOT arrive after another INPUT-BORNE command ran — and the
            // held-BUTTON producer beside it (tick_chrome_press_repeat, whose
            // repeat-eligible members are the four cardinal arrows and, since
            // 2026-09-13, Undo / Redo, whose
            // restores push nothing through this body and clear the stamp
            // (restore_history_entry) — the membership is kToolbarChords' own
            // `repeats` column, never a second list) buys the same property
            // from its own edge, the physical
            // key delivery (main.cpp's set_on_key hook), the only edge that
            // can let a command in while a finger holds a button (the
            // inventory is at AppState::ChromePress). (b) The burst's OPENER
            // is guaranteed to have taken the PHYSICAL arm first. For a held
            // KEY that opener is its own physical press — the press acts and
            // pushes on its own merits before any repeat arrives, the plain
            // adjacency of a press and its repeats. For a held BUTTON it is
            // the burst's FIRST FIRE, which its producer dispatches with
            // synthesized_repeat CLEARED because a button's press acts at the
            // lift and pushes nothing (the flip is the tick's own, stated at
            // its head). Either way the opener runs the arrival-invalidate
            // and the tap arm's own rules, and only the repeats BEHIND it
            // reach this arm — without that, a hold begun over a surviving
            // foreign stamp would merge its first fire into
            // another subject's entry. What (a) and (b) together buy is the
            // WINDOW: no clock is needed, and none is asked. THE SUBJECT IS
            // NOT bought by them — see (c) below. Keeping it clock-free is
            // deliberate: a hold must coalesce at ANY compositor repeat delay or
            // rate, and that independence is the whole reason repeat identity
            // replaced the retired kGestureCoalesceMs.
            //
            // (c) AND THE SUBJECT IS TESTED ANYWAY, since 2026-08-29 — the tap
            // arm's own subject terms, minus the clock. Halves (a) and (b)
            // argue
            // that no COMMAND runs between a burst's opener and its repeats,
            // and layer (1) does disarm at every input edge; what neither can
            // see is the RUN LOOP'S TICK, which is not an input edge and which
            // the A/B AUDITION uses to switch tabs mid-act (GuiAbAudition's
            // natural-end branch calls switch_active_tab_view_to, which clears
            // the selection and runs the coincidence auto-select). A held
            // Up/Down straddling that switch would otherwise step the OTHER
            // tab's marker with no push of its own, so one Ctrl+Z reverted two
            // markers on two tabs — the exact two-subject composition the
            // arrival-invalidate below was written to kill, arriving on the
            // repeat side through the one edge the premise did not cover. A
            // MISMATCH OPENS A NEW ENTRY; a legitimate burst still merges,
            // record_gesture re-taking them all on every accepted fire, so
            // the terms compare a repeat against the fire before it. The
            // clock stays out: a hold must coalesce at any repeat rate.
            merge = last_gesture_tab_ == app.active_tab_view
                 && last_gesture_audio_view_ == app.active_audio_view
                 && last_gesture_selection_ == app.selected_markers;
        } else {
            // ARM (2), THE TAP WINDOW — a physical press merging into the previous
            // one. Two extra conditions, because a tap has NONE of the repeat
            // arm's structure:
            //   * WITHIN kTapCoalesceMs of the LAST ACCEPTED coalesce event (the
            //     push, or the last merge — physical or synthesized), so a run of
            //     taps extends press by press rather than racing one deadline from
            //     the first;
            //   * THE SUBJECT STILL STANDS. Almost nothing disarms anything
            //     between two taps: a marker click, a Tab jump, a shift-range
            //     extension or a Ctrl+Tab can all run in the gap and push
            //     NOTHING, leaving the stamp and the stack top untouched (the
            //     one gap act that DOES disarm is the SAVE, which clears the
            //     stamp at note_saved because it moves the saved reference —
            //     no subject term could see that). Without this test the second
            //     tap would merge a DIFFERENT marker's nudge into the first
            //     marker's entry and then overwrite that entry's touched_live
            //     hints — one Ctrl+Z reverting two unrelated edits, which is the
            //     exact composition the arrival-invalidate was introduced to kill
            //     on the repeat side. The selection is the honest subject for all
            //     three eligible kinds (the nudges act on its focus, the tempo step
            //     on its members) and THE TAB AND THE AUDIO VIEW ARE TWO OF THE
            //     THREE TAGS THE ENTRY IS FILED UNDER — the restore writes the
            //     A/B tab, the W/P column and the S/T audio view back, so a `t`
            //     between two taps must open a new entry exactly as a Ctrl+Tab
            //     does, or Ctrl+Z would land the view the burst OPENED in while
            //     the last press was authored in the other. THE COLUMN NEEDS NO
            //     TERM OF ITS OWN: switching it clears the selection, and each of
            //     the three has ONE BODY over ONE store, so there is no second body
            //     on the other column for a tap to merge into. (The iteration bound
            //     step was the one kind that had one, and it left the undo domain
            //     on 2026-09-10 with the bracket.)
            // The comparison runs on the clock's OWN duration, never on a
            // whole-millisecond count: duration_cast truncates toward zero, so
            // counting first would have admitted every real interval inside
            // the millisecond ABOVE the window as if it were the window's own
            // last one. Compared directly, the boundary is exactly
            // kTapCoalesceMs.
            const std::chrono::steady_clock::duration elapsed =
                std::chrono::steady_clock::now() - last_gesture_time_;
            merge = elapsed <= std::chrono::milliseconds{kTapCoalesceMs}
                 && last_gesture_tab_ == app.active_tab_view
                 && last_gesture_audio_view_ == app.active_audio_view
                 && last_gesture_selection_ == app.selected_markers;
        }
    }

    // AN ELIGIBLE PHYSICAL PRESS INVALIDATES THE STAMP ON ARRIVAL (converted
    // 2026-07-29, and it survives the hybrid by moving BELOW the verdict) — this
    // query's one side effect, and the reason it is not const. Every eligible route
    // asks this question past the refusals ITS BUTTON GREYS ON and ahead of every
    // other, so a press that goes on to REFUSE for a reason the face cannot see
    // leaves the stamp INVALID; a press that COMMITS re-stamps it
    // in record_gesture, which is why clearing here costs the tap arm nothing.
    // THE DEFECT THIS CLOSES, as derived: a physical press can REFUSE without
    // committing (the eligible refusals left here are the ineligible tempo
    // steps — a label ref, or in target view a coincident-collapse member;
    // the WALL refusals moved out from under this
    // call, see below, and the async displayed-map flip that made the phase
    // twin's wall test the concrete producer went with them, every survivor
    // needing a COMMAND to flip),
    // which pushes nothing and so cleared nothing; if the refusal then FLIPS
    // mid-hold the first synthesized repeat commits
    // and finds the stale same-kind stamp from a DIFFERENT subject's burst, skipping
    // its own push and refreshing that older entry's touched_live. One Ctrl+Z would
    // then revert two separate holds, with snapshot and live identity hints naming
    // different markers. Clearing on stack-top changes alone could not see this: the
    // intervening acts (a pointer selection command, or the refusing press's OWN
    // focus collapse under the focus-act prologue) change no stack top.
    //
    // A WALL NO-OP TOUCHES NOTHING, AND THAT SUPERSEDES THE "EVERY REFUSED
    // PRESS SPLITS THE RUN" CLAUSE FOR THE WALL CLASS (planner-ruled
    // 2026-08-31 on the refinement arc's own logic, converting codex round A's
    // MED finding; it was "at its ENTRY, before its own refusals run" from
    // 2026-07-29). THE PROBLEM WAS TWO SURFACES, ONE WALL: the truthful-buttons
    // ruling greys a button wherever its press would change nothing, and a
    // greyed press never dispatches — so with the wall test BEHIND this call
    // the key poisoned the stamp and the button did not, and one Ctrl+Z after
    // "step to the wall, press again, step back" undid a different amount
    // depending on which surface was used. THE THREE WALL ROUTES now run their
    // wall test AHEAD of this call and refuse without reaching it: the position
    // nudges' shared prologue (through marker_nudge_actionable, which is the
    // Left / Right face's own term), the singleton cent step (through
    // tempo_cent_step_direction_actionable) and the group cent step (through
    // tempo_cent_step_group_actionable). THE DISCRIMINATOR IS THE FACE, NOT THE
    // CARD: a refusal the button greys on runs ahead of the stamp — the group
    // step's card moves with it, and since 2026-09-13 so does the singleton
    // cent step's carded KIND refusal, asked just ahead of its wall — while a
    // refusal that keeps a LIVE face stays behind it, so both surfaces poison
    // alike there.
    // WHAT IT COSTS, accepted: a refused wall press no longer ends the previous
    // burst, so a later press of the same kind can merge into it (the tap arm
    // within kTapCoalesceMs, or a repeat behind a mid-hold flip). That is
    // exactly what the greyed button already gave, and the merged run is
    // HARMLESS BY CONSTRUCTION: the refusal wrote nothing, so one entry over
    // both runs restores the same state two entries would. The composition this
    // invalidate exists to kill — merging into ANOTHER subject's burst — is
    // killed regardless by the subject terms both arms carry (the tab, the
    // audio view and the selection; clause (c) above on the repeat arm since
    // 2026-08-29).
    // THE ORDER IS THE WHOLE TRICK under the hybrid: verdict first, invalidate
    // second. Invalidating first (the pre-2026-08-01 shape) would have killed the
    // very stamp a tap needs to read, so the tap arm would never have fired.
    // ONE SITE, DELIBERATELY: the invalidate lives HERE rather than being spelled at
    // each of the eligible routes, so a route cannot forget it and no enumeration
    // has to be kept in sync — the standing "one authoritative site per concept"
    // preference. The routes are the nudges' shared prologue, both arms of
    // the Up/Down cent step, and the magnification level column's level step
    // (grep this function's callers; the Up/Down BOUND
    // step was among them until 2026-09-10, when the iteration bracket left
    // the undo domain and that step stopped asking any verdict at all).
    if (!synthesized_repeat) last_gesture_kind_ = GestureKind::None;

    // NO ACCEPTED DELTA REMAINS on either arm. record_gesture runs AFTER the push
    // at every eligible route — SIX routes over FIVE call sites, re-grepped
    // 2026-09-16 (the warp and phase-reset position nudges through their
    // shared commit tail, the magnification level nudge and level step each
    // at its own, and the singleton and group arms of the Up/Down cent
    // step) — and ONLY
    // on the
    // accepted path, so a REFUSED press
    // never enables a later merge into an older entry, tap or repeat. Presses
    // beyond the window, or after a subject change, open their own entries.
    return merge;
}

void Undo::record_gesture(GestureKind kind, bool merged) {
    // THE BYTE-EQUAL POP — the merge path's half of the commit-on-NET-CHANGE
    // principle (architect 2026-09-01). Every PUSH site in the product already
    // gates on net change, and the marker drag's commit states the reason for
    // all of them, verbatim: "pushing an undo entry there would record a
    // snapshot byte-equal to the live store, a no-op history entry that both
    // undo and redo restore invisibly" (marker_drag.cpp). A MERGED press skips
    // the push and skipped that gate with it — and a nudge pair is exactly
    // reversible FOR A MARKER ALREADY ON THE CURRENT COLUMN GRID (the painted
    // grid is anchored at frame 0 and reorder_markers_by_time is stable, so
    // the store comes back row for row; every nudge, drag and drop puts a
    // marker there, while one loaded from file, pasted, or authored at another
    // zoom sits OFF the grid and Right+Left lands it ON the grid, up to half a
    // column from where it started — a real change, whose entry then correctly
    // stays and whose dot correctly stays lit),
    // which is how a tap Right then a tap Left inside kTapCoalesceMs left the
    // burst's surviving entry byte-equal to the live store: one Ctrl+Z that
    // changed nothing at all, and a dirty dot lit over a store equal to the
    // file. The same hole stood on all four coalesce-eligible kinds. On the
    // iteration bound step it is what retires a tap up then a tap down on a
    // blank bracket: the step's own [0, 0] clearing rule puts the store back
    // byte for byte (iter_bound_step_write, app_state.h), so the entry goes.
    //
    // SO THE MERGE TAIL ASKS THE PRODUCERS' OWN QUESTION, and it asks it
    // POST-MUTATION because that is the only place the answer exists — the
    // coalesce verdict is computed ahead of the act and cannot know where the
    // press lands. When the burst's entry would restore the stores that are
    // already live, the entry comes back OFF the stack and the stamp is
    // cleared, so the burst dissolves as if it had never happened and the NEXT
    // press opens its own entry. THIS IS NOT A NEW RULE and it narrows nothing:
    // direction-blind merging stays exactly as it was (Left×5 then Right×5
    // still merge — they now merge into nothing), and the kind, the window and
    // the subject terms are untouched.
    //
    // ONE SEAM FOR EVERY KIND: every eligible route reaches this call
    // post-mutation on its accepted path — SIX routes over FIVE call sites,
    // re-grepped 2026-09-16: the warp and phase-reset position nudges through
    // their shared commit tail, the magnification level nudge and level step
    // each at its own, and the singleton and group arms of the Up/Down cent
    // step — so the equality question has ONE owner here
    // rather than six copies; the
    // per-column readers it uses are the row enumerations at the head of this
    // file. (The Up/Down BOUND step was a fifth route over a fourth call site
    // until 2026-09-10, when the iteration bracket left the undo domain: it
    // pushes nothing now, so it has no entry to pop and no kind to stamp, and
    // the row comparators dropped the iter fields with it.)
    // IT CANNOT FIRE ON A NO-OP PRESS: a route reaches this call only past its
    // own refusals and past its own mutation, and a press refused AT A WALL
    // never even asks the coalesce verdict (the wall-before-stamp order,
    // 2026-08-31, at coalesce_gesture) — so a wall no-op still touches nothing,
    // and what pops is only ever a merge that MUTATED to equality.
    // THE SELECTION AND THE FOCUS ARE NOT ITS BUSINESS: an entry restores STORE
    // content, and its touched hints are presentation a restore derives a
    // selection from. A burst that nets to zero in the store while having
    // collapsed a group selection to its focus (the position nudges' prologue)
    // pops regardless — that collapse was never in the entry to begin with, the
    // selection is never parked, and the hints die with the entry.
    // THE REDO BRANCH STAYS CLEARED, by the ordinary rule and not by a
    // judgement made here: the redo clear at the burst's FIRST press is the
    // standing any-edit rule running (an action taken mid-history disrupts the
    // redo branch unless it is viewport-related), so the pop does not and need
    // not resurrect it. The SAVED REFERENCE is given back — the arithmetic is
    // at UndoHistory::pop_undo_top_with_saved_ref.
    if (merged && !app.history.undo_stack.empty() &&
        entry_restores_live_marker_stores(app, app.history.undo_stack.back())) {
        app.history.pop_undo_top_with_saved_ref();
        last_gesture_kind_ = GestureKind::None;
        // THE DOT IS RE-DERIVED HERE, by the owner that moved the reference,
        // rather than being left to the callers' own recompute_dirty below this
        // call: the pop is what puts a saved baseline back at distance 0, and a
        // net-zero wobble over a saved file must read CLEAN again. The callers'
        // own call right after is then a same-state re-ask, and costs nothing:
        // recompute_dirty damages row 8 only on a TRANSITION, and this one has
        // already happened here.
        recompute_dirty();
        return;
    }
    // THE STAMP IS WRITTEN AS ONE UNIT, on the accepted path only (the callers put
    // this after their push / skip, past every refusal). The timestamp is what
    // makes the tap window measure from the last ACCEPTED event rather than from
    // the burst's first press; the subject is captured POST-act, so a position
    // nudge's focus collapse and its reorder remap are already folded in and the
    // next tap compares against what this press actually left standing.
    last_gesture_kind_       = kind;
    last_gesture_time_       = std::chrono::steady_clock::now();
    last_gesture_tab_        = app.active_tab_view;
    last_gesture_audio_view_ = app.active_audio_view;
    last_gesture_selection_  = app.selected_markers;
}

void Undo::note_saved() {
    // THE SAVE ENDS THE TAP WINDOW (architect 2026-09-02, the four-tier
    // review's R-2). The tap arm's merge test reads the clock and the subject
    // terms, and a Ctrl+S changes none of them and no stack top — so
    // before this a save inside a burst left the stamp standing, and the next
    // press inside kTapCoalesceMs MERGED: the store mutated with no push, the
    // reference (just rebound to 0, the burst's live state) stayed at 0, and
    // recompute_dirty read CLEAN over a store that no longer matched the file.
    // The byte-equal pop then gave the wobble a second face — Right, save,
    // Left popped the entry and stepped the reference 0 → +1 over an EMPTY
    // redo stack, clean again with the file holding the post-Right state —
    // and Ctrl+Q read the flag and exited with no prompt.
    //
    // THE FIX IS ONE CLEAR AT THE OWNER, beside the reference move it belongs
    // to, so no caller can move the reference and forget the stamp: the next
    // eligible press finds no burst to merge into and PUSHES the pre-press
    // snapshot — the state the file holds — and the reference steps to −1
    // under push()'s own arithmetic, the dot coming back on. A synthesized
    // repeat cannot follow a save inside its own burst at all (the save is a
    // key press, and a key press disarms both hold producers — layer (1) at
    // maybe_fire_repeat and the set_on_key hook the button hold dies on), and
    // one that somehow did would find the stamp None and push likewise. A save
    // is a deliberate act between taps, so the entry it opens is the honest
    // history anyway. Neither stack is touched: Ctrl+Z still reverts the last
    // op, and the ONE undo entry a merged run had stays one.
    app.history.mark_saved();
    last_gesture_kind_ = GestureKind::None;
    // THE MARK IS RE-DERIVED HERE, by the owner that moved the reference — the
    // same shape as the byte-equal pop above. recompute_dirty also owns the
    // damage for it (row 8's cell, on the transition alone), so the save itself
    // asks for none.
    recompute_dirty();
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

// Shared post-restore SELECTION rule for both marker lists: take the touched
// set a restore of `entry` produces and make it the selection, with the
// EARLIEST touched marker as focus (all members are equal; the tempo step's
// re-land and Tab's start both tolerate it, and a singleton's earliest IS the
// touched marker).
//
// THE RECONSTRUCTION IS NOT THIS FUNCTION'S — restore_touched_indices
// (app_state.h) owns it, and this is its applier. The classification, the
// identity hints, the three count arms and the row-equality basis they consume
// are all stated there.
//
// THE TOUCHED SET WINS UNCONDITIONALLY, THE EMPTY CASE INCLUDED — an empty set
// EMPTIES the selection rather than leaving the prior one standing, and the
// world changed under the old fall-through: it used to preserve "whatever the
// user had", which was a defensible thing to keep. Since the never-parked
// selection ruling (architect 2026-07-29) the entry's TAB SWITCH
// (restore_history_entry runs it before the stores are restored) ends in
// COINCIDENCE AUTO-SELECT, so what a fall-through preserves is a MACHINE GUESS
// — the destination tab's stored cursor happening to stand on a marker — and
// the visual tail then treats that guess as though the undo had touched it: a
// spurious land, recenter, and an ARMED MARKER LANE after an undo that changed
// no marker in this column. Emptying instead makes the standing rule ("the
// restore's touched set wins over the tab-entry auto-select in every reachable
// case") true with no exception, and it is not a SELECT — the same shape the
// 'S' arm uses. REACHABILITY, the reachable sequence: `push_undo_both` (notably
// the render-entry LOAD-IN-PLACE, which records the current marker mode and the
// dispatch tab while the entry may change only engine settings and/or the OTHER
// column) leaves the active column's vector byte-identical, so undoing it from
// the other tab auto-selects on arrival and the active-column diff then finds
// nothing. A REMOVAL reaches the same empty answer for its own reason — it
// leaves no touched row to select at all — and both take this one clear, which
// runs through the Selection mutator so the region, the shift anchor and the
// subject-change damage are all handled by their owner.
//
// The VISUAL tail — the playhead land (on the FOCUS in both arms, which is the
// touched marker for a singleton and the earliest touched member for a group;
// the universal land-on-the-focus rule at land_playhead_on_marker) and the
// camera that centres what was restored — lives in restore_history_entry AFTER
// sanitize.
template <class M, class FieldsDiffer>
void apply_post_restore_rules_impl(Selection& selection,
                                   const UndoEntry& entry,
                                   const std::vector<M>& before,
                                   const std::vector<M>& after,
                                   FieldsDiffer  fields_differ) {
    const std::set<int> target_set =
        restore_touched_indices(entry, before, after, fields_differ);
    if (target_set.empty()) {
        selection.clear_selection();
        return;
    }

    // Through the Selection mutator (the whole-set replace, focus on the
    // earliest touched member): the focus write is the mutator's own, which
    // is what resets the addressed cell (Selection::seat_focus) — a restore
    // is not a marker press, so the restored focus rests on its payload
    // unless the entry itself names a cell, which restore_history_entry's
    // tail writes back behind this call. The mutator's two clears are the
    // ones sanitize already made, and its top-strip damage is inside the
    // restore's whole-window repaint.
    const int focus = *target_set.begin();
    selection.replace_selection(std::move(target_set), focus);
}

}  // namespace

void Undo::apply_post_restore_rules_warp(const UndoEntry& entry,
                                         const std::vector<GuiWarpMarker>& before) {
    // The row equality basis is the shared enumeration at the head of this
    // file — this matcher's own field list until 2026-09-01, when the net-zero
    // pop became its second reader.
    apply_post_restore_rules_impl(
        selection, entry, before, app.warpmarkers.markers(),
        warp_row_fields_differ);
}

void Undo::apply_post_restore_rules_phase_reset(
        const UndoEntry& entry,
        const std::vector<GuiPhaseResetMarker>& before) {
    // The row equality basis is the shared enumeration at the head of this
    // file (the warp twin's rule verbatim).
    apply_post_restore_rules_impl(
        selection, entry, before,
        app.phaseresetmarkers.markers(),
        phase_reset_row_fields_differ);
}

void Undo::apply_post_restore_rules_magnification_level(
        const UndoEntry& entry,
        const std::vector<GuiMagnificationLevelMarker>& before) {
    // The third column's applier, the two siblings' body over its own row
    // comparator (magnification_level_row_fields_differ, app_state.h).
    apply_post_restore_rules_impl(
        selection, entry, before,
        app.magnificationlevelmarkers.markers(),
        magnification_level_row_fields_differ);
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
// delegates to it, because the Undo / Redo buttons (the icon row's, since the
// 2026-08-12 relayout dissolved the toolbar row) must GREY on
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
    counter.magnification_level_snapshot =
        app.magnificationlevelmarkers.markers();
    counter.settings            = capture_current_settings(app);
    counter.op_mode             = entry.op_mode;
    counter.tab                 = entry.tab;
    // The three context tags travel VERBATIM onto the counter rather than being
    // re-captured from live state: they describe the OP, and the counter is the
    // same op in the opposite direction, so redoing it must land the same
    // view undoing it did (selection-model.md).
    counter.audio_view          = entry.audio_view;
    // THE COUNTER NEEDS NO ITER STRIP OF ITS OWN, though it is built from the
    // LIVE stores and not from a push helper: a bracket exists only while grid
    // iterations is lit, and while it is lit history_step_actionable is false,
    // so neither do_undo nor do_redo can reach this body with one standing
    // (architect 2026-09-10 — the lock, app_state.h's authoring_locked). Said
    // here rather than defended with a fifth call, so the reason travels with
    // the one entry builder that is not a push.
    // The touched-set identity hints SWAP coordinate spaces on the counter: the
    // counter's snapshot is the op's after-state, so the rows touched by a
    // restore of the counter (= redoing this op) are entry.touched_live, and the
    // rows live when the counter was pushed (this entry's snapshot state) are
    // entry.touched_snapshot. Empty stays empty (hint-less producers).
    counter.touched_snapshot    = entry.touched_live;
    counter.touched_live        = entry.touched_snapshot;
    std::vector<GuiWarpMarker>       before_w = counter.snapshot;
    std::vector<GuiPhaseResetMarker> before_t = counter.phase_reset_snapshot;
    std::vector<GuiMagnificationLevelMarker> before_m =
        counter.magnification_level_snapshot;

    to.push_back(std::move(counter));
    // No kCap trim here: each restore moves one entry between the stacks (`from`
    // popped above, `to` pushed here), and push — the only operation that grows
    // the total — clears the redo stack and caps the undo stack. So
    // undo_stack.size() + redo_stack.size() never exceeds kCap and the
    // destination cannot overflow.
    if (app.history.saved_valid) app.history.saved_distance += saved_distance_delta;

    // -- THE VIEW, ALL THREE AXES, EACH THROUGH ITS OWN OWNER ---------------
    //
    // A restore puts the reader back in the view the op LANDED in (the rule and
    // its one exception, the phase-reset pastes' restamp, are in
    // selection-model.md), and the view has
    // THREE axes, not two (architect bug report 2026-08-28; the field list and
    // the defect the third one closes are at UndoEntry, app_state.h). Each is
    // written by the chokepoint that owns it — the same body its own key runs —
    // so every invariant those three carry arrives by construction rather than
    // being re-spelled here: the tab's band swap and coincidence auto-select,
    // the S/T domain translation of playhead and viewport with its target-view
    // entry gate and its flag-editor teardown, the column's selection clear.
    //
    // THE ORDER IS TAB, DATA, COLUMN, SELECTION, AUDIO VIEW, and every step of
    // it is decided rather than chosen:
    //   * THE TAB FIRST, because the S/T switch treats the two tabs
    //     DIFFERENTLY: the ACTIVE tab's playhead is translated and re-anchored
    //     on its own painted column (and re-expressed onto a focused marker),
    //     while the parked tab's is translated and its viewport merely shifted
    //     by the same delta. Running the switch first would hand the careful
    //     half to the tab the restore is about to leave.
    //   * THE DATA BEFORE THE AUDIO VIEW: THE TRANSLATION MUST READ THE MAP THE
    //     ENTRY RESTORES, NOT THE ONE IT REPLACES. The S/T switch builds its
    //     warp frame map out of the LIVE markers and the LIVE engine block and
    //     translates the playhead and both tabs' viewports through it, so a
    //     switch run ahead of the swap mints the destination domain's numbers
    //     under a map the restore is about to throw away: undoing a `scale`
    //     edit typed in target view translated under the POST-edit scale and
    //     the result was then read under the PRE-edit one, carrying the
    //     playhead and the camera off the instant they stood on. While the
    //     swap lands, the view standing over it sees a MAP CHANGE, which is a
    //     shape the product already owns (the re-land below); the domain flip
    //     is a separate act and runs after it, on the finished map.
    //   * THE COLUMN AFTER THE DATA, its old reason having gone with the move:
    //     it stood ahead of the swap so clear_selection's stem and flag damage
    //     would resolve against the LEAVING column's painted pixels, and this
    //     body's tail invalidates the whole waveform area and re-renders the
    //     plate synchronously either way, so that damage was already subsumed.
    //   * THE SELECTION SETTLED BEFORE THE AUDIO VIEW, because the switch
    //     RE-EXPRESSES A SURVIVING FOCUS through the store — an INDEX into the
    //     marker vector — so it has to read the selection the entry restores
    //     against the vector the entry restores. The post-restore rules and
    //     their sanitize are what settle it, so they move up with the column
    //     they are mode-bound to; a 'W' or 'P' entry that shortens its column
    //     would otherwise hand the switch an index naming another marker.
    //   * THEN the audio view, and then the visual tail, which lands and frames
    //     in the domain the restore ends in.
    // ACCEPTED COST: the tab switch renders a plate of the pre-restore state
    // that the data swap then re-renders, and the column and audio-view
    // switches kick once more each — one keystroke's worth of synchronous plate
    // work, the bill `t` and Ctrl+Tab already pay. What is NOT acceptable is a
    // PREVIEW dispatched against a state this same restore replaces, and that
    // is what the data-first order closes: the audio view's target readiness
    // ask (GuiTargetRender::ensure_ready, at the tail of
    // switch_active_audio_view_to) now reads the restored map, so the tail's
    // own unconditional trigger() is a same-state re-derive rather than a
    // correction of one.
    if (entry.tab != app.active_tab_view) {
        active_views.switch_active_tab_view_to(entry.tab);
    }

    // THE PLAYHEAD'S OWN MUSICAL INSTANT, in SOURCE frames and read while the
    // OLD map still stands — the subject of the map-change re-land below. Read
    // after the tab switch, which restores the entering tab's own cursor, and
    // before the swap that rebuilds the map under it.
    // active_domain_to_source_frame (warp_frame_map_view.h) is the product's
    // one inverse for a bare frame — the identity in source view, the memoized
    // target map's inverse in target view — so off target view it costs two
    // compares and is read unconditionally.
    const int64_t playhead_source_frame = active_domain_to_source_frame(
        app, viewport.audio, app.playhead_cursor_sample);

    // Restore engine settings before the marker swap. Marker entries get their
    // settings field populated from app at push time (carry-everywhere), so the
    // restore is a no-op for marker-only ops. Settings-only entries get the
    // actual pre-edit settings restored here.
    app.engine_settings    = std::move(entry.settings.engine_settings);

    // ALL THREE columns are assigned on EVERY entry — an undo entry carries
    // the full set, so a 'W' entry restores byte-identical phase-reset and
    // magnification level vectors and an 'S' entry restores all three
    // unchanged — and the assigns are unconditional: a
    // field-only restore (a disabled toggle, a tempo, a label) moves no row but
    // must still land its values, and markers_mut's generation bump reports it
    // either way. No row-identity comparison rides these replaces any more: the
    // per-column structural bumps that stood here existed for the parked
    // selections' liveness rule, and both died 2026-07-29.
    app.warpmarkers.markers_mut()    = std::move(entry.snapshot);
    app.phaseresetmarkers.markers_mut() = std::move(entry.phase_reset_snapshot);
    // The magnification level column, display-only: the gain profile it feeds
    // re-keys by the store's generation, which this assign bumps, and the
    // tail's synchronous plate render (kick_waveform_sync, below) reads the
    // new profile — so the picture's gain lands in the restore's own frame.
    app.magnificationlevelmarkers.markers_mut() =
        std::move(entry.magnification_level_snapshot);

    // THE MAP-CHANGE RE-LAND, the shape the product already owns for a map
    // rebuilt under a STANDING view (the family contract is at the head of
    // warpmarkers_ops.cpp) — A TRANSLATION, NOT A MOVEMENT, and on this road
    // NOT A SCROLL EITHER (architect 2026-09-22): the writer is
    // Viewport::translate_playhead_to, which carries the cursor with no
    // keep-visible edge-align. The family's other members take the reseat's
    // edge-align; this one does not, because the restore's camera answers to
    // the markers it restores and never to the playhead — through the reseat,
    // an OFFSCREEN playhead dragged the viewport to where its instant now
    // painted, the "random spot" a target-view undo used to land on. TARGET
    // VIEW ONLY: in source view the swap changes no domain and the cursor's
    // number already names its own instant. It carries the cursor across the
    // re-warp, so the audio-view switch below — and the visual tail after it —
    // start from the instant the user stood on rather than from a number the
    // replaced map minted. The subject is the PLAYHEAD's own instant rather
    // than a focus's image, for the delete's reason: a restore may leave no
    // focus at all (a removal empties the selection), and then this is the
    // whole of what happens to the cursor and the camera stays put.
    // NO KICK OF ITS OWN, unlike the family's other members: the target map
    // cache rebuilds on demand for the conversion, and this body's tail already
    // renders the plate synchronously once for the finished state.
    if (app.active_audio_view == 'T') {
        viewport.translate_playhead_to(source_frame_to_active_domain(
            app, viewport.audio, playhead_source_frame));
    }

    // THE W/P RESTORE, through the `p` chokepoint's own writer
    // (GuiActiveViews::switch_active_markers_view_to, which Undo reaches like
    // the tab's — the column switch's selection clear and the seated pinch's
    // anchor clear are that helper's, not a hand-kept copy of it; the hand-kept
    // copy that used to stand at this spot went with the move onto the owner).
    // Gated off 'S' because op_mode is that entry kind's MARKER rather than a
    // column: a settings-only entry carries no authoring column to return to.
    //
    // AN 'M' ENTRY TAKES ITS AUDIO VIEW FIRST (architect 2026-09-15; the
    // column's home flipped 2026-09-16: the magnification level markers
    // column is source view only, and the column writer refuses 'M' outside
    // it). Its audio tag is 'S' — the view the act landed in, which the push
    // helper reads off the live view (the landing-view rule and its one
    // restamp are at Undo::stamp_top_entry_with_landing_view, undo.h) — on ONE
    // guarantee that covers every producer: an 'M' entry is filed only by an
    // act gated on the M COLUMN, and the column writer refuses 'M' outside source view
    // (GuiActiveViews::switch_active_markers_view_to) while the audio writer
    // lands the column on W before it leaves for target — so a press that
    // files one stood in S+M. (The magnification level PASTES needed a
    // guarantee of their own while their gates read the W column and admitted
    // them from T+W; both gate on M since 2026-09-19, so the exception is
    // gone with the second guarantee.) So the audio restore below runs ahead of the column write rather
    // than after it, landing source first so the writer admits 'M', the
    // selection cleared first so the flip has no focus to re-express (the
    // column switch would clear it one line later anyway). Leaving target
    // never refuses, so on this road the column write always lands; the
    // shape is kept as the audio restore's own best-effort rule, which goes
    // on in the view it has whenever a switch refuses.
    //
    // A 'P' ENTRY TAKES ITS AUDIO VIEW FIRST TOO, THE TWIN (architect
    // 2026-09-21: the phase-reset column is target view only, the column
    // writer refusing 'P' outside it and the audio writer landing T+P on W
    // before it leaves for source). Its audio tag is 'T', the view the act
    // landed in — read off the live view at the push for every act that
    // stays where it is, and RESTAMPED after the landing for the one pair
    // that pushes before crossing, the phase-reset pastes (the rule is at
    // Undo::stamp_top_entry_with_landing_view, undo.h). The order is what
    // lets the column write land:
    // restored from S+W, audio first enters target and then the writer
    // admits 'P', where column first would have been refused and left T+W.
    // Unlike M's road, entering target CAN refuse (the tripwire class); the
    // column write then refuses in turn, the restore goes on in the view it
    // has — the audio restore's best-effort rule — and the post-restore rules
    // below stand down, the column the entry names not being the one shown.
    if (entry.op_mode == 'M' || entry.op_mode == 'P') {
        selection.clear_selection();
        if (input) input->switch_active_audio_view_to(entry.audio_view);
    }
    if (entry.op_mode != 'S') {
        active_views.switch_active_markers_view_to(entry.op_mode);
    }

    // Settings-only entries carry no marker or focus post-restore work. THE
    // 'M' ARM IS THE OTHER TWO COLUMNS' (architect 2026-09-15, when the column
    // gained its own authoring and so its own entries): a drop, a drag, a
    // nudge, a delete, a disable toggle, a level step and the level editor's
    // commit all file under 'M' now, each with the identity hints its twin on
    // the other columns carries, so the restore re-selects the touched set
    // exactly as a 'W' or 'P' restore does. (Until that day the column's one
    // producer was the recipe load in place, which names no touched row at
    // all and takes the empty set's own clear — which this arm still gives
    // it, restore_touched_indices answering empty for a hint-less entry whose
    // column did not move.)
    //
    // THE RULES RUN ONLY WHERE THE ENTRY'S COLUMN STANDS. They install STORE
    // INDICES of the entry's column into the one selection, and every reader
    // after this point — the sanitize, the visual tail's land, the flag
    // pass — resolves them through the ACTIVE column's store. The column
    // write above can refuse (a 'P' entry whose target entry failed its
    // tripwire-class gate leaves the session off P), and indices installed
    // then would name the same-numbered rows of another column, where a
    // Delete, a disable or a nudge would act on an unrelated marker. So a
    // column that did not land takes the empty set's own clear instead — the
    // phase-reset paste's landing guards its created set on the same
    // question (land_paste_in_target_view). The store itself is restored
    // either way; only the selection's claim on it stands down. THE SAME ARM
    // CONTAINS THE PASTE'S REFUSED LANDING: a phase paste from S+W whose
    // target entry refused honestly records the view it ended in, S+W, over a
    // 'P' entry, and its restore lands S, the column write refuses, and the
    // selection clears here — so no tripwire guards the 'P'-says-'T' claim,
    // which would only turn this contained case into a crash.
    if (entry.op_mode != 'S' && app.active_markers_view != entry.op_mode) {
        selection.clear_selection();
    } else if (entry.op_mode == 'P') {
        apply_post_restore_rules_phase_reset(entry, before_t);
        selection.sanitize_selection_after_restore(
            static_cast<int>(app.phaseresetmarkers.markers().size()));
    } else if (entry.op_mode == 'W') {
        apply_post_restore_rules_warp(entry, before_w);
        selection.sanitize_selection_after_restore(
            static_cast<int>(app.warpmarkers.markers().size()));
    } else if (entry.op_mode == 'M') {
        apply_post_restore_rules_magnification_level(entry, before_m);
        selection.sanitize_selection_after_restore(
            static_cast<int>(app.magnificationlevelmarkers.markers().size()));
    }

    // THE 'S' ARM CLEARS THE SELECTION (architect 2026-07-29): a
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
    //
    // IT STANDS BESIDE THE POST-RESTORE RULES, NOT AFTER THE AUDIO-VIEW SWITCH,
    // so that EVERY entry kind reaches that switch with a SETTLED selection.
    // The switch re-expresses the playhead through a surviving focus
    // (switch_active_audio_view_to, input_handler.h), so an 'S' entry that also
    // flips the audio view used to move the cursor through a focus this very
    // line then cleared — the one path on which "an 'S' restore selects nothing
    // and lands nothing" was not literally true. The five-axis restore order is
    // untouched (tab, data with its re-land, column, audio view, addressed
    // cell): the selection is not one of those axes, and both the marker arms
    // and this one now write it in the same place.
    if (entry.op_mode == 'S') selection.clear_selection();

    // THE S/T RESTORE, through the set-to spelling of the `t` chokepoint (the
    // contract is at its declaration, input_handler.h). Unconditional like the
    // tab restore above — a settings-only entry carries the view it was typed in
    // just as it carries the tab — and the chokepoint's own same-view early
    // return is what makes that free. It is BEST-EFFORT in exactly one direction:
    // entering target view can refuse its validity gate (the tripwire class,
    // unreachable from program-written input, silent on screen since
    // 2026-08-30), and a refusal leaves the audio view where it stands while
    // the rest of the restore proceeds — there is no aborting a restore whose
    // entry is already popped.
    //
    // NO RESTORE SYNTHESIZES A VIEW THE USER WAS NEVER IN. With all three axes
    // recorded, a restore lands the combination the op LANDED in. Before
    // this tag existed the restore MANUFACTURED S+P out of a T+P entry undone
    // from S+W, which is the defect it closed; since 2026-09-21 S+P is no
    // state at all (the phase-reset column is target view only), the two
    // writers holding it unreachable on this road as on every other — a 'P'
    // or 'M' entry has taken its audio view above, and this call is then the
    // same-view no-op, while any other entry leaving T+P for source lands the
    // column on W inside the switch.
    if (input) input->switch_active_audio_view_to(entry.audio_view);

    // VISUAL TAIL (architect 2026-07-25 — undo/redo adopts the group visual
    // language; THE CAMERA RULED 2026-09-22): the restore re-selects the
    // touched set (done above) and LANDS the playhead on its FOCUS — the
    // touched marker for a singleton, the EARLIEST touched member for a group
    // (the focus rule at apply_post_restore_rules_impl) — the members' own
    // brightened flags and the always-visible cursor on the focus being the
    // whole cue. THE CAMERA ANSWERS TO THE RESTORED MARKERS AND NEVER TO THE
    // PLAYHEAD (architect 2026-09-22):
    //   * ONE MARKER is CENTRED, ALWAYS, at the current zoom — onscreen or
    //     not, so every restore of one marker lands it in the same place;
    //   * SEVERAL MARKERS have their range's MIDDLE centred, ALWAYS, at the
    //     current zoom, and when the range plus the edge margin on each side
    //     cannot fit the window the camera ZOOMS OUT until it does; it never
    //     zooms in (center_span_in_view, input_handler.cpp);
    //   * NO MARKER (a removal, or an entry that touched nothing in this
    //     column) moves NO CAMERA: the only cursor write such a restore makes
    //     is the map-change re-land's translation above, which scrolls
    //     nothing.
    // Runs AFTER sanitize_selection_after_restore so the land sees the final
    // membership, after the tab / data / column / audio-view restores so it
    // centres in the view the restore ends in, and BEFORE the
    // recompute/invalidate/kick block below so restore's one sync render
    // covers the final geometry. It branches on the POST-sanitize live size,
    // so a defensive edge takes the matching arm (a group entry sanitized
    // down to one member lands as a singleton; a removal cleared to empty is
    // the size == 0 no-op).
    //
    // A SETTINGS-ONLY ('S') restore lands nothing and moves no camera: it
    // selects nothing (the clear above), touches no marker, and the whole
    // land/camera block is gated off it. (The restore hid the trim region
    // overlay until 2026-08-19 at its own site and through the land until the
    // resting overlay was deleted on 2026-09-22; trim is outside the undo
    // stacks by ruling.)
    if (entry.op_mode != 'S') {
        const size_t sel_size = app.selected_markers.size();
        if (sel_size == 1) {
            const int t = *app.selected_markers.begin();
            // Resolve the touched marker's source frame with ONE bounds check up
            // front — an out-of-range t skips the WHOLE singleton visual (land +
            // centre) rather than half-applying it (a bad t would else land
            // nothing but centre on the src_f=0 default). Defensive only:
            // post-sanitize the selection indices are always in range, so this
            // guards an impossible state, never a reachable one.
            // The active column's store through its selector pair
            // (active_marker_count / active_marker_time_frame, app_state.h),
            // all three columns.
            const bool in_range = (t >= 0 && t < active_marker_count(app));
            const int64_t src_f =
                in_range ? active_marker_time_frame(app, t) : 0;
            if (in_range) {
                // LAND: two-step placement basis, direct cursor write, NO viewport
                // move, through the movement owner. Playback is already
                // stopped above, so land's scanner-inactive premise holds.
                land_playhead_on_marker(app, viewport.audio, viewport, t);
                // CENTRE, ALWAYS, at the CURRENT zoom (architect 2026-09-22 —
                // until then only an OFFSCREEN marker was recentred, so a
                // restore's camera depended on where the marker happened to
                // stand): the touched marker's active-domain image at the
                // window's middle, re-snapped and wall-clamped through the one
                // chokepoint, no zoom change. The frame is the LAND'S, exactly
                // — the crossing into the active domain then the live-domain
                // clamp (the crossing can round a right-wall marker onto
                // domain_total_frames itself, one past the last frame) — so the
                // centre is the frame the land seated.
                const int64_t domain_frame = clamp_playhead_to_live_domain(
                    source_frame_to_active_domain(app, viewport.audio, src_f),
                    app, viewport.audio);
                const int64_t visible = samples_visible(app, viewport.audio);
                app.viewport_start_sample = domain_frame - visible / 2;
                clamp_viewport_start(app, viewport.audio);
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
            // sanitize that pruned the focus still lands somewhere live.
            // land_playhead_on_marker is internally bounds-guarded (an
            // impossible out-of-range index no-ops the land) and writes NO
            // viewport, so the camera below is its own; playback is already
            // stopped above (land's scanner-inactive premise).
            land_playhead_on_marker(app, viewport.audio, viewport,
                                    *app.selected_markers.begin());
            // THE CAMERA CENTRES THE RANGE'S MIDDLE, zooming out only when the
            // range plus the edge margin cannot fit (the rule and its fit
            // arithmetic at center_span_in_view's definition,
            // input_handler.cpp; the restore is its one caller). WHAT IS THIS
            // SITE'S OWN: the ACTIVE-DOMAIN extent derived just below, and the
            // unconditional invalidate + kick_waveform_sync at the tail of this
            // body, which is the damage the owner deliberately does not do.
            // THE RANGE IS THE TOUCHED SET'S OWN [earliest, latest]
            // ACTIVE-DOMAIN EXTENT, each member through the LAND'S formula —
            // clamp_playhead_to_live_domain(source_frame_to_active_domain(...))
            // — so the endpoints are the playable frames the land would seat.
            // `have` false (every restored index stale — degenerate, and
            // impossible post-sanitize) moves no camera.
            int64_t lo = 0, hi = 0;
            bool    have = false;
            {
                // The index bound and the frame from the active column's one
                // selector pair (active_marker_count / active_marker_time_frame,
                // app_state.h), all three columns.
                const int n = active_marker_count(app);
                for (int idx : app.selected_markers) {
                    if (idx < 0 || idx >= n) continue;   // defensive
                    const int64_t src_f = active_marker_time_frame(app, idx);
                    const int64_t pos = clamp_playhead_to_live_domain(
                        source_frame_to_active_domain(app, viewport.audio, src_f),
                        app, viewport.audio);
                    if (!have) { lo = hi = pos; have = true; }
                    else { if (pos < lo) lo = pos; if (pos > hi) hi = pos; }
                }
            }
            if (have) {
                center_span_in_view(app, viewport.audio, viewport, lo, hi);
            }
        }
        // sel_size == 0: nothing — the removal branch cleared, and the camera
        // stays where it stands.
    }

    // (AN ADDRESSED-CELL WRITE-BACK STOOD HERE from 2026-09-05 to 2026-09-10:
    // a bracket-only entry carried the cell it changed, and this tail put the
    // restored focus back on the bound the undo had moved. The bracket left
    // the undo domain, so no entry carries a cell, and every restore comes to
    // rest on the payload — which is exactly what the selection writes above
    // have already seated, each of them going through Selection::seat_focus.
    // The axis survives as session state; only its ride on an entry is gone.)

    recompute_dirty();
    viewport.invalidate_waveform_area();
    // One-shot discrete jump: undo/redo restored markers / phase resets /
    // settings, changing the displayed plate (the target-view warp_frame_map).
    // The visual tail above may have LANDED the playhead (on the restored focus
    // in either arm) and centred the restored markers, zooming out for a
    // group that cannot fit; these invalidations and the
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
    viewport.invalidate_clock_area();
    // Unconditional by ruling — rationale at GuiTargetRender::trigger; an
    // undo/redo restoring only normalization-inert state (e.g. a disabled-
    // marker-only restore) stops playback and re-previews through
    // dispatch_render_now's reuse rungs — cache hit, accepted. No-op in source
    // view (trigger's own gate).
    target_render.trigger();
}

// SILENT, BOTH OF THEM, AND DELIBERATELY (2026-08-30): the Ctrl+Z / Ctrl+
// Shift+Z dispatch arm asks history_step_actionable ahead of these calls so
// it can NAME which of its two terms refused — an empty stack, or a top entry
// belonging to the locked other tab — and one press owes one card, so the
// authoritative guard below stays the belt it always was.
//
// SILENT IS NOT ANSWERLESS: each returns whether the restore RAN (the
// contract and the car's reason for reading it are at the declaration). The
// bail still leaves the entry on the stack, the view unchanged and the screen
// unmarked; the bool is the caller's, not a message.
bool Undo::do_undo() {
    if (!history_entry_actionable(app.history.undo_stack)) return false;
    restore_history_entry(app.history.undo_stack, app.history.redo_stack, +1);
    return true;
}

bool Undo::do_redo() {
    if (!history_entry_actionable(app.history.redo_stack)) return false;
    restore_history_entry(app.history.redo_stack, app.history.undo_stack, -1);
    return true;
}
