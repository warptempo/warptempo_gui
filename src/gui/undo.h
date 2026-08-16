#pragma once

#include "active_views.h"
#include "app_state.h"
#include "playback_lifecycle.h"
#include "selection.h"
#include "viewport.h"

#include <chrono>
#include <set>
#include <vector>

struct GuiTargetRender;

// UNDO COALESCING, A HYBRID OF TWO RULES (architect 2026-08-01, superseding the
// pure repeat-identity model's "separate presses are separate entries" clause on
// the live complaint that rapid manual taps each pushed their own entry). A burst
// of eligible keyboard
// gestures — THREE of them, re-derived 2026-07-29 when the W+target tempo-IMAGE
// step was deleted with the whole tempo-image family (marker_drag.h): the warp and
// phase-reset position nudges (bare Left/Right in the
// marker lane) and the tempo cent step (bare Up/Down; no wheel route) — collapses
// into ONE undo entry under EITHER rule:
//   (1) REPEAT IDENTITY, for a HELD key OR a HELD BUTTON: the burst's OPENER
//       pushes the pre-burst snapshot, and every SYNTHESIZED REPEAT behind it
//       (GuiInputState::synthesized_repeat — TWO producers, one per surface:
//       GuiPlatform::maybe_fire_repeat for held keys, and
//       GuiInputHandler::tick_chrome_press_repeat for the four cardinal arrow
//       BUTTONS, whose hold-repeat returned 2026-08-16 after three days
//       deleted) SKIPS its
//       own push. THE OPENER IS THE HOLD'S FIRST REPEAT on both surfaces, and
//       for one reason: neither hold's press dispatches anything (a command
//       key's act is at its release under the 2026-08-16 keyup model, a
//       button's at its lift since 2026-08-13), so each producer's side clears
//       the delivered bit on that first fire and it takes the PHYSICAL arm
//       below, exactly as the press itself used to (the flip and its argument
//       are at GuiInputHandler::on_key; the platform producer is untouched,
//       that one being a consumer-side clear).
//       NO CLOCK IS CONSULTED on this arm, and that independence is the
//       point of keeping it: a hold coalesces for any compositor at any key-repeat
//       delay, so nothing here can drift out of sync with the desktop's repeat
//       configuration.
//   (2) THE TAP WINDOW, for consecutive PHYSICAL TAPS (each acting at its own
//       key's release): a tap of the same
//       kind arriving within kTapCoalesceMs of the last ACCEPTED coalesce event
//       merges too. FIXED compiled constant, no settings key, no compositor
//       coupling — the full derivation (and why this is NOT the retired
//       kGestureCoalesceMs reborn) is at the constant below.
// Either way the visible move, reorder/remap, dirty tracking, and the target-view
// preview stay per-fire and unchanged — only the redundant history push is
// suppressed, and a single Ctrl+Z reverts the whole burst.
// Taps BEYOND the window are separate entries, as they always were.
//
// "Same target / same tab / same history" follow for FREE on arm (1): a
// synthesized repeat can
// only arrive while the hold is still armed, and each surface's hold dies on
// the edges that let another command in — the platform key hold on three
// (every intervening pointer press, key press, and completed wheel emission,
// layer (1), stated at maybe_fire_repeat), and the BUTTON hold on both physical
// key edges, the press router's top and the keyup dispatch's (the inventory,
// and why the pointer and wheel edges need no mirror there, are at
// AppState::ChromePress) — so
// no command can run
// between a burst's opener and the repeats behind it. ARM (2) HAS NO SUCH
// STRUCTURE — a pointer click, a Tab, a view switch can all run between two taps
// without pushing anything — so the tap arm carries an explicit SUBJECT TEST in
// their place (the stamped selection and A/B tab must still stand); the
// derivation is at coalesce_gesture's definition.
// THE ELIGIBLE KINDS, one per coalescing gesture plus None: the two position
// nudges and the Up/Down cent step (TempoStep, singleton and group — its own kind
// keeps a nudge burst and a tempo burst separate). TempoImageStep was a fourth
// until 2026-07-29 and went caller-less with the tempo-image family's deletion
// (marker_drag.h).
enum class GestureKind {
    None, WarpNudge, PhaseResetNudge, TempoStep
};

// THE TAP-COALESCE WINDOW (architect 2026-08-01): two consecutive PHYSICAL
// TAPS of the same eligible kind, no more than this many milliseconds apart,
// land in ONE undo entry. Measured on std::chrono::steady_clock from the last
// ACCEPTED coalesce event (the push, or the last merge — physical or synthesized
// alike), so a whole run of taps extends the window tap by tap rather than
// racing one fixed deadline from the first.
//
// IT IS NOT kGestureCoalesceMs REBORN, and the distinction is what keeps the
// undo history legible: that retired constant gated ALL coalescing, held keys
// included, so it had to EXCEED the compositor's key-repeat delay — a number
// this process cannot see, which is precisely why repeat identity replaced it.
// This one gates ONLY the tap arm; held keys coalesce by identity with no clock
// at all (arm (1) at the head of this file), so the value has one job — cover
// the interval a human leaves between deliberate taps of the same key — and is
// compositor-independent by construction. 500 ms is the architect's number.
inline constexpr long long kTapCoalesceMs = 500;

// Undo-cluster operations, extracted from main.cpp's inline lambdas.
// The struct holds references to the long-lived state the methods read and
// write; bodies are byte-identical to the originals modulo `this->` access
// on the captured references. The viewport reference is reached through
// viewport; stop_playback_if_playing is reached through playback_lifecycle;
// switch_active_tab_view_to is reached through active_views (so do_undo / do_redo
// can restore the originating A/B tab before applying the marker change).
struct Undo {
    AppState&             app;
    Viewport&             viewport;
    Selection&            selection;
    GuiPlaybackLifecycle& playback_lifecycle;
    GuiActiveViews&       active_views;
    GuiTargetRender&   target_render;

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
    void push_undo_warp(std::vector<GuiWarpMarker> pre_state,
                        bool affects_persistence = true,
                        std::vector<int> touched_snapshot = {},
                        std::vector<int> touched_live = {});
    void push_undo_phase_reset(std::vector<GuiPhaseResetMarker> pre_state,
                             std::vector<int> touched_snapshot = {},
                             std::vector<int> touched_live = {});
    // tab_override attributes entries pushed on behalf of a tab the caller is
    // about to switch to. The entry belongs to the edit's semantic tab, not
    // the incidental tab the cursor is in when history is pushed.
    void push_undo_both(std::vector<GuiWarpMarker> warp_pre,
                        std::vector<GuiPhaseResetMarker> phase_reset_pre,
                        char op_mode, char tab_override = 0);
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

    // Whether the current eligible gesture fire of `kind` coalesces into the
    // burst's existing undo entry — TRUE on either arm of the hybrid (a
    // synthesized repeat of a matching burst, or a physical fire inside
    // kTapCoalesceMs of the last accepted event with the subject unchanged; the
    // full shape is at the definition). `synthesized_repeat` is the bit AS
    // DELIVERED (GuiInputState::synthesized_repeat), threaded from the key
    // event that reached the handler — so a hold's first repeat, whose bit its
    // own producer's side cleared, arrives here PHYSICAL (the opener, on either
    // surface; arm (1) at the head of this file). When it returns true the caller SKIPS its
    // undo push — and a coalesced fire has NO other side effect since row 5
    // (note_coalesced_commit mirrored the push helpers' hover-popup clear, and
    // died with the popup). Either way the caller then calls record_gesture.
    // NOT A PURE QUERY since 2026-07-29: a PHYSICAL
    // fire (synthesized_repeat false) INVALIDATES the coalescing stamp here, on
    // arrival, AFTER computing its own verdict — so a fire that goes on to REFUSE
    // leaves no stamp for anything later to merge through, while a fire that
    // COMMITS re-stamps in record_gesture. It stays callable
    // anywhere before record_gesture and in any order with the handler's own
    // refusals; the point of putting it at the ENTRY question is precisely that it
    // runs BEFORE those refusals can return.
    bool coalesce_gesture(GestureKind kind, bool synthesized_repeat);
    // Record this eligible fire as the burst's latest, stamping the whole
    // coalescing state: the KIND, the ACCEPTED-EVENT TIMESTAMP the tap window
    // measures from, and the SUBJECT (selection + A/B tab) the tap arm re-tests.
    // Call after the push / skip — and ONLY on the accepted path, which is what
    // makes a refusing fire leave the stamp invalid.
    void record_gesture(GestureKind kind);
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

  private:
    // THE COALESCING STAMP — the whole coalescing state, session-only and never
    // serialized, written as ONE unit by record_gesture and invalidated as one.
    //
    // `last_gesture_kind_` is the previous eligible commit's gesture kind, and it
    // doubles as the stamp's VALIDITY bit (None = no burst to merge into). A press
    // merges only into a burst of its
    // OWN kind, which is what keeps a nudge burst and a tempo-step burst separate.
    // THREE writers: record_gesture stamps it at each eligible commit; every route
    // that changes the undo-stack top CLEARS it — the four push helpers plus the
    // do_undo/do_redo restore core, so a stale stamp cannot outlive the entry it
    // named; and coalesce_gesture itself clears it when the arriving press is
    // PHYSICAL, so a press that then REFUSES leaves nothing behind (both reasons
    // are stated at coalesce_gesture's definition).
    GestureKind last_gesture_kind_ = GestureKind::None;
    // The last ACCEPTED coalesce event's instant, read ONLY by the tap arm.
    // steady_clock: monotonic, immune to a wall-clock step. The default-
    // constructed epoch value is never consulted — the kind stamp gates every
    // read and starts None.
    std::chrono::steady_clock::time_point last_gesture_time_{};
    // THE SUBJECT the stamped burst acted on, read ONLY by the tap arm — standing
    // in for the structural adjacency the platform's repeat contract gives arm (1)
    // for free and arm (2) not at all. Both eligible gesture families derive their
    // target from the selection (the nudges from its focus, the tempo step from
    // its members) and an undo entry is filed under an A/B tab, so a tap that
    // follows a marker click, a Tab, a range extension or a Ctrl+Tab finds a
    // CHANGED subject and opens its own entry instead of merging into an entry
    // about someone else. Captured POST-act (record_gesture), so the position
    // nudges' focus collapse and their reorder remap are already reflected and a
    // steady run of taps compares like against like.
    std::set<int> last_gesture_selection_;
    char          last_gesture_tab_ = 0;

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
