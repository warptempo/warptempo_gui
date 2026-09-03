#include "undo.h"

#include "input_handler.h"        // land_playhead_on_marker (which owns the
                                  // restore's overlay hide),
                                  // GuiInputHandler::switch_active_audio_view_to
                                  // — the S/T tag's restore chokepoint,
                                  // bring_span_into_view — the restore visual
                                  // tail's group framing (the shared owner
                                  // since 2026-08-16; frame_span_into_view is
                                  // its cannot-fit arm and is no longer called
                                  // from this TU)
#include "platform.h"           // viewport.gui.set_title_dirty — the window
                                  // title's dirty half, pushed from
                                  // recompute_dirty's tail
#include "target_render.h"
#include "warp_frame_map_view.h"  // source_frame_to_active_domain, for the
                                  // singleton recenter and the group framing,
                                  // and active_domain_to_source_frame for the
                                  // restore's map-change re-land

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace {

// THE ROW EQUALITY BASIS, ONE ENUMERATION PER COLUMN AND TWO READERS (hoisted
// out of the post-restore matcher's own two lambdas 2026-09-01, when the
// net-zero pop below needed the same question asked of a whole store and a
// second spelling of a field list would have been exactly the drift the "one
// authoritative enumeration per concept" preference exists to prevent).
// `differ(a, b)` is false exactly when the two rows are identical in EVERY
// field the store holds, serialized and session-only alike — row identity means
// the WHOLE struct. THE TWO READERS: apply_post_restore_rules_impl's same-count
// arm, which asks it of one row pair at a time to reconstruct a touched set,
// and entry_restores_live_marker_stores below, which asks it of a whole store.
// EXACT COMPARES, no epsilon and no re-rounding: an authored position is a
// whole frame and an authored tempo whole cents BY TYPE, and the two double
// fields (tempo_scale, the bpm bracket) are compared as stored — what a restore
// would write back, field for field.
bool warp_row_fields_differ(const GuiWarpMarker& a, const GuiWarpMarker& b) {
    return a.time_frame     != b.time_frame
        || a.disabled       != b.disabled
        || a.tempo_inherits != b.tempo_inherits
        || a.tempo_cents    != b.tempo_cents
        || a.tempo_scale    != b.tempo_scale
        || a.label_def      != b.label_def
        || a.label_ref      != b.label_ref
        // The measure is a serialized field like the rest: a measure-only undo
        // mutates nothing else, so omitting it would strand the selection
        // exactly as an omitted bracket would.
        || a.measure        != b.measure
        // Session-only iter/bpm fields ride undo snapshots too, and row
        // identity means the WHOLE struct: an iteration-bracket-only or
        // bpm-only undo mutates only these, so omitting them would leave the
        // same-count matcher finding no touched row and stranding the
        // selection. Every GuiWarpMarker field beyond the serialized eight
        // above.
        || a.iter_start_cents != b.iter_start_cents
        || a.iter_end_cents   != b.iter_end_cents
        || a.bpm_owner        != b.bpm_owner
        || a.bpm_beats        != b.bpm_beats
        || a.bpm_lo           != b.bpm_lo
        || a.bpm_hi           != b.bpm_hi
        || a.bpm_endpoint     != b.bpm_endpoint;
}

bool phase_reset_row_fields_differ(const GuiPhaseResetMarker& a,
                                   const GuiPhaseResetMarker& b) {
    return a.time_frame != b.time_frame
        || a.disabled   != b.disabled
        // The measure, for the warp column's reason: a measure-only undo
        // mutates nothing else, and row identity means the whole struct.
        || a.measure    != b.measure;
}

// True when restoring `entry` would write back the marker stores THAT ARE
// ALREADY LIVE — the question the coalesced burst's net-zero pop asks
// (Undo::record_gesture, where the rule is stated). BOTH columns, because every
// entry carries a full pair and a restore assigns both unconditionally, so a
// 'W' entry that a merged press returned to its snapshot is only byte-equal
// when the phase column matches too.
//
// THE STORES ARE THE WHOLE CONTENT the question has to consider, and the
// entry's THIRD payload — its engine settings block — needs no term of its own:
// the three coalescing kinds (both position nudges and the tempo cent step)
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
    const std::vector<GuiWarpMarker>&       live_w = app.warpmarkers.markers();
    const std::vector<GuiPhaseResetMarker>& live_p =
        app.phaseresetmarkers.markers();
    if (entry.snapshot.size() != live_w.size()) return false;
    if (entry.phase_reset_snapshot.size() != live_p.size()) return false;
    for (std::size_t i = 0; i < live_w.size(); ++i) {
        if (warp_row_fields_differ(entry.snapshot[i], live_w[i])) return false;
    }
    for (std::size_t i = 0; i < live_p.size(); ++i) {
        if (phase_reset_row_fields_differ(entry.phase_reset_snapshot[i],
                                          live_p[i]))
            return false;
    }
    return true;
}

}  // namespace

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
    e.audio_view         = app.active_audio_view;
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
    e.audio_view         = app.active_audio_view;
    e.touched_snapshot   = std::move(touched_snapshot);
    e.touched_live       = std::move(touched_live);
    app.history.push(std::move(e));
    last_gesture_kind_ = GestureKind::None;   // see coalesce_gesture
}

void Undo::push_undo_both(std::vector<GuiWarpMarker> warp_pre,
                          std::vector<GuiPhaseResetMarker> phase_reset_pre,
                          char op_mode) {
    UndoEntry e;
    e.snapshot           = std::move(warp_pre);
    e.phase_reset_snapshot = std::move(phase_reset_pre);
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
    e.settings           = std::move(pre_state);
    e.op_mode            = 'S';
    e.tab                = app.active_tab_view;
    e.audio_view         = app.active_audio_view;
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
    // THE KIND IS BLIND TO DIRECTION AND, SINCE 2026-08-31, TO MAGNITUDE (the
    // step ladder — R12: Shift+arrow steps three units and Ctrl+arrow ten).
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
    // helpers (push_undo_warp / push_undo_phase_reset / push_undo_both /
    // push_settings_undo) and restore_history_entry, the shared do_undo/do_redo
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
            // 2026-08-26, the waveform magnification pair, which fires here
            // and pushes nothing — the membership is kToolbarChords' own
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
            // arm's own three terms, minus the clock. Halves (a) and (b) argue
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
            // record_gesture re-taking all three on every accepted fire, so
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
            //     TERM: switch_active_markers_view_to clears the selection and
            //     both eligible families refuse without one, so a column switch
            //     is already a subject change the selection term sees.
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
    // steps — a label ref, or in target view a pass, a ref or a
    // coincident-collapse member; the WALL refusals moved out from under this
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
    // step's card moves with it — while a refusal that keeps a LIVE face stays
    // behind it, so both surfaces poison alike there.
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
    // file. The same hole stood on all three coalesce-eligible kinds.
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
    // ONE SEAM FOR ALL THREE KINDS: every eligible route reaches this call
    // post-mutation on its accepted path (the two position nudges through their
    // shared commit tail, both arms of the Up/Down cent step at their own
    // tails), so the equality question has ONE owner here rather than three
    // copies — the per-column readers it uses are the row enumerations at the
    // head of this file.
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
        // own call right after is then a same-state re-ask (recompute_dirty's
        // title setter does not move a flag that has not moved).
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
    // review's R-2). The tap arm's merge test reads the clock and the three
    // subject terms, and a Ctrl+S changes none of them and no stack top — so
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
    // THE DOT IS RE-DERIVED HERE, by the owner that moved the reference — the
    // same shape as the byte-equal pop above. recompute_dirty pushes the flag
    // to the window title itself, so the save requests no damage.
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
// re-land and Tab's start both tolerate it, and a singleton's earliest IS the
// touched marker).
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
    // (notably the render-entry LOAD-IN-PLACE, which records the current
    // marker mode and the
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
    // The row equality basis is the shared enumeration at the head of this
    // file — this matcher's own field list until 2026-09-01, when the net-zero
    // pop became its second reader.
    apply_post_restore_rules_impl(
        app, selection, entry, before, app.warpmarkers.markers(),
        warp_row_fields_differ);
}

void Undo::apply_post_restore_rules_phase_reset(
        const UndoEntry& entry,
        const std::vector<GuiPhaseResetMarker>& before) {
    // The row equality basis is the shared enumeration at the head of this
    // file (the warp twin's rule verbatim).
    apply_post_restore_rules_impl(
        app, selection, entry, before,
        app.phaseresetmarkers.markers(),
        phase_reset_row_fields_differ);
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
    counter.settings            = capture_current_settings(app);
    counter.op_mode             = entry.op_mode;
    counter.tab                 = entry.tab;
    // The three context tags travel VERBATIM onto the counter rather than being
    // re-captured from live state: they describe the OP, and the counter is the
    // same op in the opposite direction, so redoing it must land the same
    // authoring view undoing it did.
    counter.audio_view          = entry.audio_view;
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

    // -- THE AUTHORING VIEW, ALL THREE AXES, EACH THROUGH ITS OWN OWNER -----
    //
    // A restore puts the reader back where the op was authored, and the view has
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

    // THE MAP-CHANGE RE-LAND, the shape the product already owns for a map
    // rebuilt under a STANDING view (the family contract is at the head of
    // warpmarkers_ops.cpp; the writer is Viewport::reseat_playhead_to, and a
    // TRANSLATION IS NOT A MOVEMENT — it hides no trim region overlay and ends
    // no audition). TARGET VIEW ONLY: in source view the swap changes no domain
    // and the cursor's number already names its own instant. It carries the
    // cursor across the re-warp, so the audio-view switch below — and the
    // visual tail after it — start from the instant the user stood on rather
    // than from a number the replaced map minted. The subject is the PLAYHEAD's
    // own instant rather than a focus's image, for the delete's reason: a
    // restore may leave no focus at all (a removal empties the selection).
    // NO KICK OF ITS OWN, unlike the family's other members: the target map
    // cache rebuilds on demand for the conversion, and this body's tail already
    // renders the plate synchronously once for the finished state.
    if (app.active_audio_view == 'T') {
        viewport.reseat_playhead_to(source_frame_to_active_domain(
            app, viewport.audio, playhead_source_frame));
    }

    // THE W/P RESTORE, through the `p` chokepoint's own writer
    // (GuiActiveViews::switch_active_markers_view_to, which Undo reaches like
    // the tab's — the column switch's selection clear and the seated pinch's
    // anchor clear are that helper's, not a hand-kept copy of it; the hand-kept
    // copy that used to stand at this spot went with the move onto the owner).
    // Gated off 'S' because op_mode is that entry kind's MARKER rather than a
    // column: a settings-only entry carries no authoring column to return to.
    if (entry.op_mode != 'S') {
        active_views.switch_active_markers_view_to(entry.op_mode);
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
    // recorded, a restore lands the combination the op was AUTHORED in; the
    // keyless S+P now arrives when, and only when, the op was authored there
    // (reachable by toggling `t` off T+P, where the marker MEASURE authors —
    // the home-view binding's fourth ruled exception). Before this tag existed
    // the restore MANUFACTURED S+P out of a T+P entry undone from S+W, which is
    // the defect it closes.
    if (input) input->switch_active_audio_view_to(entry.audio_view);

    // VISUAL TAIL (architect 2026-07-25 — undo/redo adopts the group visual
    // language, superseding "undo/redo shows its target WITHOUT the playhead"):
    // a SINGLETON restore LANDS the playhead on its touched marker (which is its
    // focus; the land is the movement owner, so it takes the trim region overlay
    // with it) and its flag BRIGHTENS from the
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
    // the 'S' gate is now SIMPLE: a settings-only restore selects nothing, shows
    // no overlay, and lands nothing — it HIDES the overlay and clears the
    // selection (below), which is why the narrowing it briefly carried (a
    // target-view re-land onto a surviving focus) is gone with the surviving focus
    // itself. It is
    // branches on the POST-sanitize live size, so a defensive edge takes the
    // matching arm (a group entry sanitized down to one member lands as a
    // singleton; a removal cleared to empty is the size == 0 no-op).
    //
    // THE OVERLAY HIDE IS NO LONGER THE TAIL'S OWN. It discards NOTHING either
    // way — the overlay is DERIVED from the trim (RegionState, app_state.h),
    // which the restore does not touch at all, trim being outside the undo
    // stacks by ruling.
    //
    // (THE RESTORE'S OWN OVERLAY HIDE IS DELETED, 2026-08-19, with the call-site
    // inventory it belonged to.) A MARKER restore still hides, and does it where
    // the rule says: both marker arms LAND the playhead on the restored focus
    // below, and the land is one of the rule's two movement owners
    // (clear_region_highlight, input_handler.h). A SETTINGS-ONLY ('S') restore
    // lands nothing and hides nothing now — it moves no playhead and touches no
    // marker, and its old argument (the rebuilt map under a shown overlay) died
    // on 2026-08-18 when the region became the trim: the span is DERIVED from
    // source-domain trim bounds every frame, so a rebuilt map re-derives the
    // overlay rather than stranding it.
    // The 'S' gate stands
    // exactly as it did: a settings restore still must not select and must not
    // SHOW an overlay, and the whole land/framing block stays inside it. The
    // no-LAND half is EXCEPTIONLESS again:
    // the target-view re-land it briefly allowed — onto a selection
    // surviving the restore — died with the selection clear directly below, which
    // leaves no focus to land on.
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
                // move — and THE OVERLAY HIDE RIDES IT since 2026-08-19, the land
                // being one of the rule's two movement owners (the rule at
                // clear_region_highlight, input_handler.h), which is what
                // replaced this tail's own call. Playback is already
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
            // extent-region write that used to follow this land is gone: the
            // region IS THE TRIM, which a restore has no business writing.
            // land_playhead_on_marker is internally bounds-guarded (an
            // impossible out-of-range index no-ops the land) and writes NO
            // viewport, so the three-way offscreen arm below is unaffected;
            // playback is already stopped above (land's scanner-inactive
            // premise).
            land_playhead_on_marker(app, viewport.audio, viewport,
                                    *app.selected_markers.begin());
            // OFFSCREEN handling: PREFER a plain scroll at the current zoom,
            // ZOOM only when the group cannot fit — this restore's own rule
            // since 2026-07-25 and, since 2026-08-16, SHARED CODE. The whole
            // argument (the painted-column fit contract, the three arms, the
            // ceiling/half-pixel exception to the framer's no-op guard, and the
            // accepted duplicate render) lives at bring_span_into_view's
            // definition, input_handler.cpp; it was hoisted verbatim out of
            // this spot when the Show trim region button asked for the same
            // behaviour, so this arm is unchanged in effect and only its home
            // moved. WHAT IS THIS SITE'S OWN: it hands the owner an
            // ACTIVE-DOMAIN extent derived just below, and the unconditional
            // invalidate + kick_waveform_sync at the tail of this body is the
            // damage the owner deliberately does not do.
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
                bring_span_into_view(app, viewport.audio, viewport, lo, hi);
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
void Undo::do_undo() {
    if (!history_entry_actionable(app.history.undo_stack)) return;
    restore_history_entry(app.history.undo_stack, app.history.redo_stack, +1);
}

void Undo::do_redo() {
    if (!history_entry_actionable(app.history.redo_stack)) return;
    restore_history_entry(app.history.redo_stack, app.history.undo_stack, -1);
}
