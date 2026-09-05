#pragma once

#include "active_views.h"
#include "app_state.h"
#include "gui_input.h"          // kHoldBeatMs, the product's one beat — the
                                // tap-coalesce window below IS it
#include "playback_lifecycle.h"
#include "selection.h"
#include "viewport.h"

#include <chrono>
#include <set>
#include <vector>

struct GuiTargetRender;
struct GuiInputHandler;

// UNDO COALESCING, A HYBRID OF TWO RULES (architect 2026-08-01, superseding the
// pure repeat-identity model's "separate presses are separate entries" clause on
// the live complaint that rapid manual taps each pushed their own entry). A burst
// of eligible keyboard
// gestures — FOUR of them, re-grepped 2026-09-04 (it was three from 2026-07-29,
// when the W+target tempo-IMAGE step was deleted with the whole tempo-image
// family, marker_drag.h): the warp and
// phase-reset position nudges (Left/Right in the
// marker lane), the tempo cent step (Up/Down; no wheel route) and the same
// arrows' ITERATION BOUND STEP (Up/Down on an addressed bound cell) — each in
// the
// step ladder's three magnitudes since 2026-08-31, which the coalescing is
// blind to exactly as it is blind to direction (the record is at
// coalesce_gesture) — collapses
// into ONE undo entry under EITHER rule:
//   (1) REPEAT IDENTITY, for a HELD key OR a HELD BUTTON: the burst's OPENER
//       pushes the pre-burst snapshot, and every SYNTHESIZED REPEAT behind it
//       (GuiInputState::synthesized_repeat — TWO producers, one per surface:
//       GuiInputCore::maybe_fire_repeat for held keys, and
//       GuiInputHandler::tick_chrome_press_repeat for the repeating BUTTONS,
//       whose hold-repeat returned 2026-08-16 after three days deleted; the
//       membership is the chord table's `repeats` column) SKIPS its
//       own push. THE OPENER DIFFERS PER SURFACE: a held KEY's burst opens
//       with the PHYSICAL press itself — the press acts and pushes, then its
//       repeats merge behind it — while a held BUTTON's opens with the
//       burst's FIRST FIRE, dispatched with the bit cleared by the tick's own
//       flip, because a button's press acts at the lift and pushes nothing
//       (the flip and its argument are at tick_chrome_press_repeat,
//       input_pointer.cpp; the platform producer needs no flip).
//       NO CLOCK IS CONSULTED on this arm, and that independence is the
//       point of keeping it: a hold coalesces for any compositor at any key-repeat
//       delay, so nothing here can drift out of sync with the desktop's repeat
//       configuration.
//   (2) THE TAP WINDOW, for consecutive PHYSICAL presses: a press of the same
//       kind arriving within kTapCoalesceMs of the last ACCEPTED coalesce event
//       merges too. FIXED compiled constant, no settings key, no compositor
//       coupling — the full derivation (and why this is NOT the retired
//       kGestureCoalesceMs reborn) is at the constant below.
// Either way the visible move, reorder/remap, dirty tracking, and the target-view
// preview stay per-press and unchanged — only the redundant history push is
// suppressed, and a single Ctrl+Z reverts the whole burst.
// Presses BEYOND the window are separate entries, as they always were.
// AND A SAVE ENDS THE BURST (architect 2026-09-02): Ctrl+S moves the saved
// reference onto the burst's LIVE state without touching a stack top, and a
// merge after it would rewrite that very state with no entry between it and
// the file — the dot reading clean over a store that differs from disk. So the
// save clears the stamp at the owner (Undo::note_saved, the save's one tail)
// and the next press opens its own entry; the derivation is at note_saved.
// AND A BURST THAT NETS TO ZERO POPS ITS OWN ENTRY (architect 2026-09-01, the
// byte-equal pop): a merge skips the push and skipped the push sites' NET
// CHANGE gate with it, so a tap Right then a tap Left inside the window left an
// entry byte-equal to the live store — one Ctrl+Z that changed nothing, over a
// dirty dot that stayed lit. The merge tail now asks the producers' own
// question post-mutation and takes the entry back off the stack when the answer
// is equal. The rule, its reach and what it deliberately does not give back are
// at Undo::record_gesture.
//
// "Same target / same tab / same history" follow for FREE on arm (1): a
// synthesized repeat can
// only arrive while the hold is still armed, and each surface's hold dies on
// the edges that let another command in — the platform key hold on three
// (every intervening pointer press, key press, and completed wheel emission,
// layer (1), stated at maybe_fire_repeat), and the BUTTON hold on the one
// physical key delivery (main.cpp's set_on_key hook; the inventory, and why
// the pointer and wheel edges need no mirror there, are at
// AppState::ChromePress) — so
// no command can run
// between a burst's opener and the repeats behind it. ARM (2) HAS NO SUCH
// STRUCTURE — a pointer click, a Tab, a view switch can all run between two taps
// without pushing anything — so the tap arm carries an explicit SUBJECT TEST in
// their place (the stamped selection and A/B tab must still stand); the
// derivation is at coalesce_gesture's definition.
// THE ELIGIBLE KINDS, one per coalescing gesture plus None: the two position
// nudges, the Up/Down cent step (TempoStep, singleton and group — its own kind
// keeps a nudge burst and a tempo burst separate) and, since 2026-09-04, the
// same arrows' ITERATION BOUND STEP (IterBoundStep, singleton and group — the
// arrows' second body, stepping a bound cell of the bracket; its own kind
// because the two bodies write different fields and a burst has one subject,
// and the one kind whose ENTRY carries the addressed cell too, so a restore
// lands the focus back on the bound the burst moved —
// UndoEntry::addressed_cell, pushed by push_undo_iter_bracket).
// TempoImageStep was a kind until 2026-07-29 and went caller-less with the
// tempo-image family's deletion (marker_drag.h).
enum class GestureKind {
    None, WarpNudge, PhaseResetNudge, TempoStep, IterBoundStep
};

// THE TAP-COALESCE WINDOW (architect 2026-08-01): two consecutive PHYSICAL
// presses of the same eligible kind, no more than this many milliseconds apart,
// land in ONE undo entry. Measured on std::chrono::steady_clock from the last
// ACCEPTED coalesce event (the push, or the last merge — physical or synthesized
// alike), so a whole run of taps extends the window press by press rather than
// racing one fixed deadline from the first.
//
// IT IS NOT kGestureCoalesceMs REBORN, and the distinction is what keeps the
// undo history legible: that retired constant gated ALL coalescing, held keys
// included, so it had to EXCEED the compositor's key-repeat delay — a number
// this process cannot see, which is precisely why repeat identity replaced it.
// This one gates ONLY the tap arm; held keys coalesce by identity with no clock
// at all (arm (1) at the head of this file), so the value has one job — cover
// the interval a human leaves between deliberate taps of the same key — and is
// compositor-independent by construction. Both halves of that still hold, and
// neither is what the value is: nothing here reads a compositor setting, and
// the held-key arm still consults no clock.
//
// IT IS kHoldBeatMs (gui_input.h), THE PRODUCT'S ONE BEAT — 575 ms, the
// architect's own labwc <repeatDelay> matched by convention (architect
// 2026-08-28: "increase the coalesce wait to 575 ms, the global wait time for
// long press, key repeat, etc."). It was its own 500 from 2026-08-01 until
// that ruling. THE JOB IS UNCHANGED and so is the argument for the SIZE of it
// — the interval a human leaves between deliberate taps of one key is the
// same interval a deliberate hold has to cross and a deliberate second tap has
// to arrive inside — so the product asks the hand for ONE cadence rather than
// for a number per surface. The beat's own declaration carries the readers'
// one inventory and lists this window apart from the holds, as it lists the
// double-click window: this is not a hold.
inline constexpr long long kTapCoalesceMs = kHoldBeatMs;

// Undo-cluster operations, extracted from main.cpp's inline lambdas.
// The struct holds references to the long-lived state the methods read and
// write; bodies are byte-identical to the originals modulo `this->` access
// on the captured references. The viewport reference is reached through
// viewport; stop_playback_if_playing is reached through playback_lifecycle;
// switch_active_tab_view_to and switch_active_markers_view_to are reached
// through active_views (so do_undo / do_redo can restore the originating A/B tab
// and W/P column before applying the marker change).
struct Undo {
    AppState&             app;
    Viewport&             viewport;
    Selection&            selection;
    GuiPlaybackLifecycle& playback_lifecycle;
    GuiActiveViews&       active_views;
    GuiTargetRender&   target_render;
    // Back-pointer to the input handler, wired in main.cpp after both are
    // constructed (the input handler holds Undo by reference, so the dependency
    // is a cycle resolved with a pointer set post-construction — the same shape
    // as the settings editor's and the propagate's `input` back-wires). ONE
    // READER: restore_history_entry, which restores the entry's S/T tag through
    // switch_active_audio_view_to — the third view axis's owner lives on
    // GuiInputHandler, the other two on GuiActiveViews above.
    GuiInputHandler*      input = nullptr;

    Undo(AppState&             app_,
         Viewport&             viewport_,
         Selection&            selection_,
         GuiPlaybackLifecycle& playback_lifecycle_,
         GuiActiveViews&       active_views_,
         GuiTargetRender&   target_render_)
        : app(app_),
          viewport(viewport_),
          selection(selection_),
          playback_lifecycle(playback_lifecycle_),
          active_views(active_views_),
          target_render(target_render_) {}

    void recompute_dirty();
    // touched_snapshot / touched_live are the position movers' identity hints
    // (UndoEntry — the reposition drag and the two nudges, one marker each):
    // defaulted empty for every other caller, which then uses the
    // diff-based touched-set reconstruction in the post-restore rules.
    // `addressed_cell` stamps UndoEntry::addressed_cell and has exactly one
    // caller, push_undo_iter_bracket below; every other push leaves the
    // entry on the payload.
    void push_undo_warp(std::vector<GuiWarpMarker> pre_state,
                        bool affects_persistence = true,
                        std::vector<int> touched_snapshot = {},
                        std::vector<int> touched_live = {},
                        MarkerCell addressed_cell = MarkerCell::Payload);
    // THE BRACKET-ONLY WARP ENTRY — the iteration bracket's own push, and the
    // one entry kind that carries an addressed cell. Three callers: the
    // singleton and group arms of the Up/Down bound step
    // (GuiWarpMarkersOps::adjust_iter_bound_cents and its group twin) and the
    // bound editor's commit (GuiFlagEditor::commit_iter_bound_edit). It fixes
    // affects_persistence FALSE — iteration bounds are session-only fields
    // that never serialize, so crossing such an entry must not move the dirty
    // dot — and stamps the LIVE addressed cell onto the entry, so an undo or
    // redo of the step lands the focus back on the bound it moved (the field's
    // contract is at UndoEntry::addressed_cell, app_state.h). `touched` is the
    // group arm's identity hint and fills BOTH coordinate spaces: a bound step
    // moves no marker, so the entry's snapshot rows and its live rows are the
    // same indices.
    void push_undo_iter_bracket(std::vector<GuiWarpMarker> pre_state,
                                std::vector<int> touched = {});
    void push_undo_phase_reset(std::vector<GuiPhaseResetMarker> pre_state,
                             std::vector<int> touched_snapshot = {},
                             std::vector<int> touched_live = {});
    // Files under the LIVE tab like the three helpers around it. (A
    // `tab_override` parameter stood here for the load-in-place, which used to
    // switch to the tab its file named; it lost its last producer on 2026-08-24
    // when the act stopped writing view state, and went with it.)
    void push_undo_both(std::vector<GuiWarpMarker> warp_pre,
                        std::vector<GuiPhaseResetMarker> phase_reset_pre,
                        char op_mode);
    // Settings-only undo entry. op_mode='S' marks it as settings-class so
    // do_undo / do_redo skip the mode-switch and post-restore-rules
    // dispatch. Markers are captured wholesale at push time (carry-
    // everywhere shape) so the restore is symmetric with marker entries.
    void push_settings_undo(SettingsSnapshot pre_state);
    void apply_post_restore_rules_warp(const UndoEntry& entry,
                                       const std::vector<GuiWarpMarker>& before);
    void apply_post_restore_rules_phase_reset(const UndoEntry& entry,
                                            const std::vector<GuiPhaseResetMarker>& before);
    void do_undo();
    void do_redo();

    // Whether the current eligible gesture press of `kind` coalesces into the
    // burst's existing undo entry — TRUE on either arm of the hybrid (a
    // synthesized repeat of a matching burst, or a physical press inside
    // kTapCoalesceMs of the last accepted event with the subject unchanged; the
    // full shape is at the definition). `synthesized_repeat` is the bit AS
    // DELIVERED (GuiInputState::synthesized_repeat), threaded from the key
    // event that reached the handler — a held BUTTON burst's first fire
    // arrives here PHYSICAL by its producer's own flip (the opener; arm (1)
    // at the head of this file). When it returns true the caller SKIPS its
    // undo push — and a coalesced press has NO other side effect since row 5
    // (note_coalesced_commit mirrored the push helpers' hover-popup clear, and
    // died with the popup). Either way the caller then calls record_gesture.
    // NOT A PURE QUERY since 2026-07-29: a PHYSICAL
    // press (synthesized_repeat false) INVALIDATES the coalescing stamp here, on
    // arrival, AFTER computing its own verdict — so a press that goes on to REFUSE
    // leaves no stamp for anything later to merge through, while a press that
    // COMMITS re-stamps in record_gesture. It stays callable
    // anywhere before record_gesture and in any order with the handler's own
    // refusals, and WHERE EACH CALLER PUTS IT IS RULED BY THE FACE since
    // 2026-08-31: behind every refusal the act's BUTTON GREYS ON — the walls,
    // which therefore leave the stamp exactly as a greyed press does — and
    // ahead of every refusal a per-tick face cannot see, which is what keeps a
    // key and its button coalescing alike. The full rule, its cost and its
    // supersession of the older "at the ENTRY, before its own refusals" shape
    // are at the definition.
    bool coalesce_gesture(GestureKind kind, bool synthesized_repeat);
    // SETTLE THE BURST at the tail of an eligible press. Two outcomes, and
    // `merged` (the verdict coalesce_gesture returned for this same press)
    // picks which is even possible:
    //   * THE BYTE-EQUAL POP, on a MERGED press only (architect 2026-09-01):
    //     when the burst's surviving entry would restore the marker stores that
    //     are ALREADY LIVE — a tap Right then a tap Left inside kTapCoalesceMs
    //     — the entry comes off the undo stack, the stamp is cleared and the
    //     dirty dot is re-derived, so the burst dissolves as if it had never
    //     happened and the next press opens its own entry. This is the
    //     commit-on-NET-CHANGE principle every PUSH site already gates on
    //     (stated at marker_drag.cpp's commit), extended to the one path that
    //     skips the push; direction-blind merging is untouched. THE MERGE TAIL
    //     IS THE ONE SEAM all four eligible kinds share, which is why the
    //     question is asked here and not at the five call sites.
    //   * OTHERWISE THE STAMP, written as one unit: the KIND, the
    //     ACCEPTED-EVENT TIMESTAMP the tap window measures from, and the
    //     SUBJECT — the selection, the A/B tab and
    //     the S/T AUDIO VIEW since 2026-08-29, which both the tap arm and the
    //     held-key arm
    //     re-test (the audio view joined when the A/B audition's tick-driven
    //     switch was found able to land between a burst's opener and its
    //     repeats), plus the ADDRESSED CELL since 2026-09-04, which the
    //     iteration bound step alone reads (Lower and Upper are two different
    //     fields of one selection; the argument is at coalesce_gesture).
    // Call after the push / skip and after the mutation — and ONLY on the
    // accepted path, which is what makes a refusing press leave the stamp
    // invalid and is what keeps the pop off every no-op press.
    void record_gesture(GestureKind kind, bool merged);
    // Refresh the coalesced burst entry's touched_live to a continuation press's
    // LATEST post-reorder indices (the position nudges, which reorder — the
    // tempo step never does). The surviving first-press undo entry keeps its
    // touched_snapshot (the pre-burst snapshot coordinates a restore produces),
    // but its touched_live — the coordinates a redo of the whole coalesced op must
    // re-select — must track the final after-state as later presses reorder
    // further. THE STACK TOP IS THIS BURST'S ENTRY on both coalesce arms: on the
    // REPEAT arm because no command can run between the opener that pushed
    // it and the repeats that merge into it, and on the TAP arm because every
    // route that changes the undo-stack top CLEARS the stamp, so no merge can be
    // verdicted against an entry that has since moved. A no-op on
    // an empty stack (defensive).
    void refresh_coalesced_touched_live(std::vector<int> touched_live);
    // THE SAVE'S ONE TAIL (architect 2026-09-02): rebind the saved reference to
    // the current timeline position (UndoHistory::mark_saved — neither stack
    // is touched, so Ctrl+Z still reverts the last op), CLEAR the coalescing
    // stamp, and re-derive the dirty flags. The stamp clear is the point: a
    // save is the one act that moves the saved reference without changing a
    // stack top, and a merge after it would mutate the store with no push —
    // the reference staying at the burst's live state while the store walks
    // away from the file, and the byte-equal pop then stepping it onto a redo
    // entry that does not exist. With the stamp cleared the next eligible press
    // opens its own entry and the dot comes back on. CALLERS, one: GuiSaveOps::
    // save, on its success path — the Save-and-Commit prelude reaches it
    // through that same owner. THE FILE LOAD NEEDS NO CALL: it resets the
    // history whole (UndoHistory::reset, file_loader.cpp), and the surviving
    // stamp is inert against an empty stack until a push helper clears it
    // (coalesce_gesture's non-empty-stack guard); a reopen constructs a fresh
    // Undo per project (main.cpp). The definition carries the derivation.
    void note_saved();

  private:
    // THE COALESCING STAMP — the whole coalescing state, session-only and never
    // serialized, written as ONE unit by record_gesture and invalidated as one.
    //
    // `last_gesture_kind_` is the previous eligible commit's gesture kind, and it
    // doubles as the stamp's VALIDITY bit (None = no burst to merge into). A press
    // merges only into a burst of its
    // OWN kind, which is what keeps a nudge burst and a tempo-step burst separate.
    // FOUR writers: record_gesture stamps it at each eligible commit; every route
    // that changes the undo-stack top OR THE SAVED REFERENCE CLEARS it — the
    // four push helpers plus the do_undo/do_redo restore core (the stack top),
    // so a stale stamp cannot outlive the entry it named, and note_saved (the
    // reference, since 2026-09-02), so a merge cannot rewrite the state the
    // file was just written from — the file load resets the history whole and
    // needs no clear (the stamp is inert against an empty stack, see
    // note_saved); and coalesce_gesture itself clears it when the arriving
    // press is PHYSICAL, so a press that then refuses for a reason its button
    // cannot show leaves nothing behind — a press refused AT A WALL never
    // reaches the call at all since 2026-08-31 (both reasons, and the face rule
    // that divides them, are stated at coalesce_gesture's definition).
    GestureKind last_gesture_kind_ = GestureKind::None;
    // The last ACCEPTED coalesce event's instant, read ONLY by the tap arm.
    // steady_clock: monotonic, immune to a wall-clock step. The default-
    // constructed epoch value is never consulted — the kind stamp gates every
    // read and starts None.
    std::chrono::steady_clock::time_point last_gesture_time_{};
    // THE SUBJECT the stamped burst acted on. It was the tap arm's alone until
    // 2026-08-29, standing in for the structural adjacency the platform's
    // repeat contract gives arm (1) for free and arm (2) not at all; the
    // repeat arm now tests it too (clause (c) at coalesce_gesture — the run
    // loop's tick is not an input edge, so the A/B audition can switch tabs
    // between a burst's opener and its repeats). All four eligible gesture
    // families derive their target from the selection (the nudges from its
    // focus, the tempo step and the bound step from its members) and AN UNDO
    // ENTRY IS FILED UNDER THREE VIEW TAGS — the A/B
    // tab, the W/P column and the S/T audio view, all three of which the restore
    // writes back — so a tap that follows a marker click, a Tab, a range
    // extension, a Ctrl+Tab or a `t` finds a CHANGED subject and opens its own
    // entry instead of merging into an entry filed elsewhere. THE COLUMN NEEDS
    // NO TERM OF ITS OWN: switch_active_markers_view_to CLEARS the selection
    // (the scope rule), and every eligible family refuses without one, so a
    // column switch between two taps is already a subject change the selection
    // term sees. The bound step needs one more term than the selection can
    // carry, the addressed cell below. Captured POST-act (record_gesture), so
    // the position nudges' focus collapse and their reorder remap are already
    // reflected and a steady run of taps compares like against like.
    std::set<int> last_gesture_selection_;
    char          last_gesture_tab_ = 0;
    char          last_gesture_audio_view_ = 0;
    // The addressed cell, the fourth subject term and the only kind-specific
    // one: IterBoundStep alone reads it, because that kind's subject is a
    // field of the selected markers (Lower or Upper) rather than the markers
    // themselves, so a press on the other cell moves nothing the three terms
    // above can see. The other three kinds each move one field by
    // construction and ignore it. Stamped with the rest on every accepted
    // fire; the compare and its derivation are at coalesce_gesture.
    MarkerCell    last_gesture_cell_ = MarkerCell::Payload;

    // Shared authoritative guard for do_undo / do_redo: true when the step
    // would actually act (non-empty source stack, top entry's target tab
    // writable). See the rationale block at the definition.
    bool history_entry_actionable(const std::vector<UndoEntry>& stack) const;
    // Direction-parameterized restore core shared by do_undo / do_redo. Pops
    // `from`, pushes the live-state counter-entry onto `to`, and applies the
    // whole common restore body; saved_distance_delta is +1 for undo, -1 for
    // redo. Callers guard with history_entry_actionable first.
    void restore_history_entry(std::vector<UndoEntry>& from,
                               std::vector<UndoEntry>& to,
                               int saved_distance_delta);
};
