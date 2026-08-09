#pragma once

#include "engine_settings.h"
#include "gui_input.h"
#include "history_diff.h"
#include "render_pipeline.h"
#include "render.h"
#include "settings_file.h"
#include "text_editor.h"
#include "phase_reset_clipboard.h"
#include "phaseresetmarkers.h"
#include "warp_frame_map_view.h"
#include "warpmarkers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

class GuiAudio;

// Zoom level numbering: the range constants (kMinZoom, kMaxZoom) live in
// settings_file.h — the persisted zoom vocabulary the whole-settings schema
// enforces in both products — and app_state.h re-exports them through the
// include above.
//
// The zoom level is a real-valued exponent resting anywhere in the ONE
// continuous domain [kMinZoom, kMaxZoom] (Ableton-style free rest): manual zoom
// walks it by whole steps from its current, possibly fractional, rung, and
// zoom-out saturates at the per-file effective ceiling
// (effective_max_zoom_level), where full zoom-out rests at whole-song-visible.
// There is no fit-file mode and no sentinel level. Bare-digit keys are unbound
// for zoom: `0` goes to full zoom-out and, pressed once already there, runs the
// `c` command (it stopped being a two-way toggle 2026-08-05), and `c` jumps to
// the working zoom centered on the playhead (or on the focused marker) — the
// Tab family, which recenters on its stop, changes no zoom at all. Smaller
// level = less file per window = more zoomed in. kMinZoom is
// the deepest zoom-in the manual walk can reach (1.2 s); kWorkingZoomLevel is
// the fine-tuning rest point the snap gestures land on (2.4 s, one
// step shallower), where the working-zoom authoring-grid bit-exactness claims
// hold.
constexpr double kWorkingZoomLevel = 2.0;  // 2.4 s — working zoom; manual
                                           // zoom-in can go one step deeper to
                                           // kMinZoom (1.2 s)

// Viewport lead/overlap fraction, expressed as a divisor of the visible
// span. Follow mode keeps this much of the window as lead context when it
// re-anchors; paged scroll (PageUp/PageDown) retains the same fraction as
// overlap so the two behaviors stay visually consistent. One source of
// truth — do not inline the divisor at either site.
constexpr int64_t kViewportLeadDivisor = 10;

// (THE ONE GLOBAL HIT HALF-WIDTH IS GONE — kMarkerHitHalfPx, deleted
// 2026-08-02 with its last reader long behind it. It was the single
// clicking/hovering tolerance shared by stems, flags and trim bounds; the
// redesign gave each surface its own authored, gui_scale-aware grab constant
// instead — kMarkerStemGrabPx / marker_stem_grab_px() below for the marker
// stem, kTrimEndcapGrabPx / trim_endcap_grab_px() in render.h for the trim
// endcaps — and the flags are hit on their painted boxes with no halo at all.
// The rule it carried outlives it and belongs to nothing in particular: a grab
// tolerance is NOT a spacing gap. Markers may sit arbitrarily close, overlap
// exactly, and cross during gestures; ordering degeneracy collapses at the
// render boundary, not at authoring time.)

// Vertical drag distance (px) that moves the strip drag by one continuous
// level. The strip zoom drags DOWN to zoom in (deeper, lower level) and UP to
// zoom out. Both this scale and that direction are architect-tunable on the
// labwc pass.
constexpr double kZoomStripPxPerLevel = 60.0;

// Wholesale snapshot of the undo-tracked settings. Holds the typed
// EngineSettings captured at undo-push time and restored on undo/redo.
struct SettingsSnapshot {
    EngineSettings engine_settings;
};

// One entry on either stack. Carries the pre-mutation marker snapshot; the
// touched rows a restore re-selects are recovered from the identity hints below
// (or the diff matcher), and group focus defaults to the earliest touched marker
// — there is no stored focus hint (a restore's visual is the selection + the
// singleton land / group region, not a remembered anchor).
//
// Every entry also carries the pre-mutation phase reset snapshot and the mode
// the operation was performed in. Both lists are always restored on undo/redo
// so the inverse is symmetric regardless of which list the op actually
// touched. `op_mode` lets undo flip the active mode; `tab` lets undo switch
// the active tab — both are context tags that restore the original authoring
// view as visual feedback for what's being undone.
//
// Carry-everywhere shape: every entry — marker, phase reset, or settings
// — populates `settings` from app at push time, so do_undo/do_redo can
// restore wholesale without caring which subset actually changed.
// op_mode 'S' marks settings-only entries: those skip the mode-switch
// and post-restore-rules dispatch since they don't carry an authoring
// selection-anchor.
struct UndoEntry {
    std::vector<GuiWarpMarker>      snapshot;
    std::vector<GuiPhaseResetMarker> phase_reset_snapshot;
    SettingsSnapshot          settings;
    char                      op_mode              = 'W';
    char                      tab                  = 'A';
    // False for an iteration-bracket-only snapshot. Iteration brackets are
    // session state and never serialize, so crossing such an entry must not
    // make recompute_dirty report a warp-file difference.
    bool                      affects_persistence  = true;
    // Explicit touched-set IDENTITY HINTS for the post-restore selection, filled
    // ONLY by producers whose touched row the diff reconstruction cannot
    // recover by marker identity — the position movers (the drags and the two
    // nudges), where a moved row can
    // land field-identical to an untouched row (the moved marker column-snapped
    // exactly onto a row-identical marker). ALWAYS SINGLETON-SCOPED since
    // 2026-07-29: every one of those gestures moves exactly one marker (groups are
    // never moved — the doctrine at the head of position_nudge.h), so a
    // multi-member hint has no producer left. The vectors stay vectors because the
    // RESTORE side is group-capable — an undo/redo restore may re-select a whole
    // set it took in wholesale. Empty means "no hint — use the diff reconstruction"
    // (every other producer). COORDINATE SPACES (kept distinct because the
    // counter-entry SWAPS them, restore_history_entry): touched_snapshot indexes
    // THIS entry's `snapshot` — the state a restore of this entry PRODUCES, so
    // apply_post_restore_rules reads it directly as the selection; touched_live
    // indexes the state that was LIVE when the entry was pushed (the op's
    // after-state), which becomes the snapshot coordinate of the counter-entry.
    std::vector<int>          touched_snapshot;
    std::vector<int>          touched_live;
};

// Session-only region selection — an Ableton-style arrangement span, and TRIM
// SCRATCH: forming one is how the user aims `x`, and that is its whole purpose
// (architect 2026-07-30, retiring the SPAN FORM). It is NOT a playhead form, NOT
// a selection visual, and NOT a trim-window display: the cursor playhead always
// paints straight across it, the singleton stem is never suppressed, and nothing
// publishes the trim window back into it. Endpoints are ACTIVE-DOMAIN frames
// (source frames in source view, target frames in target view), stored in drag
// order and normalized lo/hi at READ time, so the span survives pan/zoom
// mid-drag and at rest. NEVER serialized, and stored independently of the
// selection and undo systems (a transient visual — no undo entry, its own field,
// not derived from the selection set).
//
// SELECTION FLOWS DOWNWARD ONLY (architect 2026-07-23): highlighting a region does
// NOT select the markers it contains — do not re-propose the reverse coupling. The
// one coupling left runs region -> trim, through `x` alone. Trim never SELECTS a
// marker and no longer publishes a highlight either; the trim setters still EMPTY
// the selection as they commit (the setter-deselect rule).
//
// THE FORMER — THE AUTHORITATIVE INVENTORY (re-derive by grepping every writer of
// `region.active = true`: the drag's motion path is the ONE writer since
// 2026-08-05, every entry reaching it through the one arm arm_region_drag_at, so
// the inventory IS arm_region_drag_at's callers plus the callers of the body that
// wraps it, place_playhead_and_arm_region).
// THERE IS ONE FORMING GESTURE, THE PLACEMENT PRESS AND THE DRAG IT ARMS, in
// FOUR entries (re-derived 2026-08-06):
//   * the PLAIN press in the waveform's UPPER half (the lower half is the scrub);
//   * the SHIFT-exact press at EITHER height (architect 2026-08-05, THE FORMER'S
//     RESHAPE: the region ANCHORS AT THE CLICKED COLUMN now, so shift IS the
//     placement press — the playhead-anchor read and the furthest-selected-marker
//     argmax it used to choose between are deleted, and so is the one-act span
//     they formed. A motionless shift click-release therefore lands the playhead
//     and rests NO region, where it used to rest one);
//   * the EMPTY flag/triangle-lane PARITY press (architect 2026-07-23), which
//     runs the same placement body — the lane works like the waveform upper half,
//     drag included, the drag's motion path being y-agnostic once armed. PLAIN
//     ONLY there: a shift press on the lane claims nothing;
//   * the `h` HISTORY VIEW'S own press, full height (that view has no scrub since
//     playback left it), which is the same recipe minus the store deselect.
// EVERY REGION FORMER DROPS THE SELECTION ITS SURFACE OWNS — the family rule,
// stated here and pointed at from the sites (architect-RATIFIED 2026-08-05,
// promoting what had been the coder's reading of the mode's arm into the
// ruling). The THREE live entries DESELECT at press — all three through the one
// placement body — leaving the STORE selection
// EMPTY throughout the drag; the `h` view's entry clears THE MODE'S focus and
// diff-flag selection instead, through the one clearer that takes the pair, and
// touches no store selection at all by the view's own standing rule. So no
// former anywhere leaves a selection standing beside the span it is drawing, and
// the surface simply decides WHICH selection that is. Both of the view's
// waveform arms take it: the plain press clears through the placement body, and
// the shift press IS that press.
// SO A REGION RESTS BESIDE AN EMPTY SELECTION, WITHOUT AN EXCEPTION THE EDITOR
// CAN SEE, and what restores that is the VIEW-LOCAL RULE (architect 2026-08-05):
// the view's spans are cleared at its EXIT and at every `,` / `.` step and
// every walk-or-reading switch (close_history_mode and the two step owners,
// input_key_dispatch.cpp), so nothing formed in there can rest in the editor.
// The exception that survives is scoped to INSIDE the view, where the mode's own
// flag selection and a region may coexist — which no consumer out here reads.
// Everything that used to write a region from somewhere else is DELETED with
// the span form: the selection-extent owner, the trim-window sync, the two
// multi-delete demotions, and the whole three-value origin enum RegionState
// carried. A region has ONE origin now — the user drew it.
//
// Bare `x` is SET-ONLY and consumes THIS highlight: a live region trims to it,
// DESELECTS (the setter rule) and then CLEARS the region — its job is done and
// nothing re-publishes one. No region means `x` is a silent no-op, and so does a
// DEGENERATE result — an inverse-mapped span coming out end <= begin refuses
// rather than writing a pair the crossed-commit auto-clear would destroy (at
// handle_trim_x).
//
// CLEARED wholesale on: file load, the A/B tab switch, the S/T audio-view switch
// and the W/P marker-column switch (each flips the domain or the owning column out
// from under the span), EVERY WAVEFORM PLACEMENT PRESS (it dissolves any resting
// highlight at mouse-down, before it knows whether the gesture is a click or a
// fresh region drag — via arm_region_drag_at, all FOUR entries; a SCRUB press
// leaves the region alone in either entry, the lower-half left one and the bare
// right one, that gesture being the region's PREVIEW gesture), the `h` view's
// three edges (the view-local rule above), and the kick validator's live-domain
// reclamp when a bound falls outside a shrunken domain. The full clear-site
// enumeration lives at clear_region_highlight's declaration (input_handler.h).
struct RegionState {
    bool    active  = false;
    int64_t a_frame = 0;   // the press-anchor endpoint
    int64_t b_frame = 0;   // the far (pointer) endpoint
};

// Marker reposition drag state (begun by a plain flag drag past the shared
// threshold). ONE MARKER, ALWAYS — GROUPS ARE NEVER MOVED (architect 2026-07-29,
// HORIZONTAL MOVEMENT IS A FOCUS ACT; the doctrine and the dead rigid-group
// machinery are recorded at the head of position_nudge.h). `active` gates
// motion handling; the rest holds the drag's own working set — the pre-drag
// position the proposal is derived from and the pre-drag store the COMMIT pushes
// as its undo entry. Nothing here is a cancel
// origin: pointer gestures have no cancel (2026-07-29, the rule at the drag-modal
// gate in input_handler.cpp), so Esc mid-drag changes nothing and the release
// commits.
//
// `delta_min` / `delta_max` is a scalar ACTIVE-domain offset range
// (architect 2026-07-23): the dragged marker's wall
// headroom mapped through the displayed map — [fwd(0) − fwd(orig),
// fwd(eof_wall) − fwd(orig)] — plus its viewport clamp, all
// in the pointer-delta (active) domain. apply_drag_motion clamps the POINTER
// delta into it, so the marker's image stops at its wall in either view
// (source view is the fwd-identity special case). An absolute [0, eof_wall]
// source backstop follows for fp-safety. Neighbors do not bound a drag —
// the marker may cross them freely, and commit reorders the store. Trim is purely
// cosmetic and does not constrain edits.
struct DragState {
    bool                active = false;
    // The dragged marker's store index, held as a one-element vector because
    // DragOverlay pairs it positionally with moveable_times (slot 0 everywhere).
    std::vector<int>    dragging_markers;
    // Pre-drag position, parallel to dragging_markers. An at-rest copy of
    // the store's authored int64 frame.
    std::vector<int64_t> original_times;
    // Proposed new position during motion (a source-frame double — mid-
    // gesture positions are free and fractional), parallel
    // to dragging_markers. Written by apply_drag_motion as the DISPLAYED-map
    // proposal inv(fwd(orig) + active-domain delta) — the two-hop formula at
    // apply_drag_motion's header (identity orig + delta in source view);
    // consumed by paint via DragOverlay so the live marker store stays untouched
    // until commit. Seeded from original_times at begin_drag. This is a
    // MOTION/PAINT value: at commit it converts back to an authored frame through
    // the pixel-anchoring column snap (commit_drag).
    std::vector<double> moveable_times;
    // Press position in ACTIVE-domain frame doubles; the motion delta
    // (mouse_frame - anchor) therefore lives in active-domain frames, and
    // apply_drag_motion carries it into the source domain through the
    // displayed map's two hops (the uniform-rate model at its header).
    double              anchor_mouse_time_frame = 0.0;
    double              delta_min = -std::numeric_limits<double>::infinity();
    double              delta_max =  std::numeric_limits<double>::infinity();
    // No per-drag map copy: mid-drag target-view translation (paint, the
    // motion proposals, commit snapping) reads the DISPLAYED map —
    // displayed_or_live_target_map, falling back to the memoized live
    // display map (active_display_context /
    // target_view_warp_frame_map_cached) when cold. The displayed map is
    // frozen for the drag's lifetime by TWO halves working together: the
    // drag-freeze gate in maybe_enqueue_waveform_render suppresses any NEW
    // dispatch, and on_waveform_render_done DROPS a job that was already in
    // flight (or parked in the supersede slot) at the grab instead of publishing
    // its map — so neither an in-flight nor a fresh render can swap the basis
    // out from under a stationary pointer. The live cache is keyed on the
    // marker-store generation + scale (+ sample rate + total frames), and
    // nothing that changes either is reachable while a drag is in flight: the
    // frozen-coordinate regime keeps motion in the overlay (no generation
    // bump), the drag-modal gate swallows every key but the Ctrl+Q hatch, pointer
    // gestures are mutually exclusive, the wheel is blocked mid-gesture, editors
    // cannot open, and resize / WM-close END the drag first (a commit, through its
    // release body — pointer gestures have no cancel); a preview
    // completing mid-drag touches only the audio buffer, playback rebind, dirty
    // bit, and status text, never the store or scale. So the cache is stable
    // for the drag's lifetime and equals what a begin_drag copy would hold.
    // Full pre-drag marker state. Captured at button-press so commit_drag
    // can push it onto the undo stack when motion landed; discarded on
    // commit when no motion occurred (DragState is reset wholesale there).
    std::vector<GuiWarpMarker>      pre_drag_snapshot;
    std::vector<GuiPhaseResetMarker> pre_drag_phase_reset_snapshot;
    // NO CANCEL CAPTURES (the selection snapshot, the grab playhead and the
    // pre-drag region all deleted 2026-07-29): POINTER GESTURES HAVE NO CANCEL —
    // Esc mid-drag is a consumed no-op, release commits, and undo is the mitigation
    // (the rule is stated at the drag-modal gate in input_handler.cpp's on_key).
    // What survives above is the pre-drag STORE, which is the undo payload, not a
    // restore origin.
    // Playhead-follows-marker ruling (architect 2026-07-23, reversing the
    // 2026-07-20 decoupling): the arming click LANDS the playhead on
    // the pressed marker (source_frame_to_active_domain then
    // clamp_playhead_to_live_domain), so it is coincident by construction, and the
    // mid-motion follow plus commit_drag's unconditional land keep it there —
    // the drag tows it UNCONDITIONALLY, so a later Space auditions FROM it. The
    // lead-in workflow (parking
    // the playhead upstream to audition the approach) that the decoupling
    // served is supplied by the scrub surface instead. Only the RESTING
    // cursor playhead moves — move_playhead_to writes the cursor field only, so
    // a live scanner is left untouched; it stays the audio thread's to own.
    // (No `moved` latch and no `hit_marker`: both were group-era state with no
    // reader left and were deleted 2026-07-29. The drag's
    // net-change test compares the COMMITTED frame against original_times[0], which
    // is the true "did anything move" question; the grabbed marker's identity is
    // dragging_markers[0], the only slot there is.)
    // Which list this drag operates on. The motion / commit
    // handlers dispatch on this so a drag started in phase reset view
    // mutates the phase reset list.
    char                   drag_mode = 'W';
};

// Drag-time position overlay. Paint sites consult this when a marker
// index appears in `indices` to read the proposed new time from
// `times` rather than the live store's time_frame. The two spans
// alias DragState's `dragging_markers` and `moveable_times` — parallel
// vectors paired positionally by k. The indices are not necessarily in
// ascending order (a mid-drag store reorder remaps them in place); the
// linear scan below does not care. Empty overlay (default-constructed)
// is equivalent to "no drag active" and falls back to the live store.
struct DragOverlay {
    const std::vector<int>*    indices = nullptr;
    const std::vector<double>* times   = nullptr;

    // Returns the overlay time for marker `marker_idx`, or
    // `fallback_time_frame` when the index is not in the overlay.
    // Caller passes the live store's time_frame as the fallback.
    double effective_time(int marker_idx,
                          double fallback_time_frame) const {
        if (!indices || !times) return fallback_time_frame;
        for (size_t k = 0; k < indices->size(); ++k) {
            if ((*indices)[k] == marker_idx) return (*times)[k];
        }
        return fallback_time_frame;
    }
};

// Two-stack undo/redo history for marker mutations. Entries are full
// snapshots of the marker vector plus a pre-op selection
// hint — small enough to store directly rather than diff. Both stacks are
// capped at kCap; the oldest undo entry is evicted when the cap is exceeded.
//
// The saved reference is a signed distance from the current position to the
// snapshot corresponding to what's on disk. Positive = ahead on the redo
// stack; negative = behind on the undo stack; 0 = at current. `saved_valid`
// tracks whether the saved reference is still reachable: a new mutation
// that clears the redo stack while saved was ahead orphans it (saved_valid
// becomes false), and dirty stays true until the next save rebinds it.
struct UndoHistory {
    static constexpr size_t kCap = 500;
    std::vector<UndoEntry> undo_stack;
    std::vector<UndoEntry> redo_stack;
    int  saved_distance = 0;
    bool saved_valid    = true;

    // Evict the oldest (bottom) entry of the undo stack for the kCap trim while
    // keeping the saved reference honest. Saved distances into the undo stack
    // are negative. Hopping the saved baseline over an entry is
    // persistence-equivalent iff that entry's affects_persistence is false —
    // the same equivalence the redo-orphan collapse in push() applies. So when
    // the saved reference points at or below the evicted bottom: a session-only
    // evicted entry pins the reference to the stack-reachable bound (provably
    // equivalent for dirty purposes, since recompute_dirty's walk skips
    // session-only entries); a persistence-affecting evicted entry leaves the
    // baseline genuinely unreachable and inequivalent, so invalidate it —
    // recompute_dirty's invalid branch then marks everything dirty, the
    // conservative direction a save re-establishes. The undo stack is the ONLY
    // stack that ever evicts (the restore-side non-trim invariant is recorded
    // at its site in restore_history_entry).
    void evict_undo_bottom_with_saved_ref() {
        const bool evicted_affects_persistence =
            undo_stack.front().affects_persistence;
        undo_stack.erase(undo_stack.begin());
        if (!saved_valid) return;
        const int bound = -static_cast<int>(undo_stack.size());
        if (saved_distance >= bound) return;
        if (evicted_affects_persistence) saved_valid    = false;
        else                             saved_distance = bound;
    }

    // Push the pre-mutation entry. Clears the redo stack. If the saved
    // reference was on the redo stack, it is orphaned only when the path back
    // to it crosses a persistence-affecting entry. A path made solely of
    // session-only iteration-bracket entries is persistence-equivalent to the
    // current cursor, so collapse the saved reference here before clearing it.
    // If pushing evicts the bottom of the undo stack and the saved reference
    // pointed at or below the evicted entry, evict_undo_bottom_with_saved_ref
    // resolves it by the same equivalence rule (pin when session-only,
    // invalidate when persistence-affecting).
    void push(UndoEntry entry) {
        if (saved_valid && saved_distance > 0) {
            const int rs = static_cast<int>(redo_stack.size());
            bool path_affects_persistence = false;
            for (int i = std::max(0, rs - saved_distance); i < rs; ++i) {
                if (redo_stack[i].affects_persistence) {
                    path_affects_persistence = true;
                    break;
                }
            }
            if (path_affects_persistence) saved_valid = false;
            else                          saved_distance = 0;
        }
        redo_stack.clear();
        if (saved_valid) saved_distance -= 1;
        undo_stack.push_back(std::move(entry));
        if (undo_stack.size() > kCap) {
            evict_undo_bottom_with_saved_ref();
        }
    }

    void mark_saved() {
        saved_distance = 0;
        saved_valid    = true;
    }

    void reset() {
        undo_stack.clear();
        redo_stack.clear();
        saved_distance = 0;
        saved_valid    = true;
    }
};

// State for the plain (unmodified) left-drag region-select gesture on the
// waveform's UPPER half (the lower half is the scrub surface, whose press is a
// one-shot scrub act arming nothing — only the plain press splits by half). The PRESS
// does its press-time work (deselect-all, playhead
// placement, live-playback reseek — it never SELECTS a marker), DISSOLVES any
// resting highlight at mouse-down, and arms this drag; motion past the shared
// press-becomes-drag threshold (kDragMovedThresholdPx) extends app.region from
// the press frame to the pointer column. Under SELECTION FLOWS DOWNWARD ONLY
// (architect 2026-07-23) the drag does NOT select the span's markers — the
// selection stays EMPTY from the press's deselect-all through release. THE DRAG
// CARRIES THE PLAYHEAD (architect 2026-07-30): each changed column writes the
// cursor to the MOVING endpoint, both arms through the one motion path, with no
// viewport scroll and no playback reseek per motion. A
// sub-threshold press-release is a
// plain waveform click and simply disarms — the highlight already dissolved at
// press, so there is no release-time collapse. FOUR PRESSES ARM THIS DRAG AND
// ALL FOUR ARM IT THE SAME WAY, through arm_region_drag_at — dissolve the
// resting region at mouse-down, anchor at the CLICK column (membership
// re-derived 2026-08-06; the authoritative inventory is at RegionState): the
// plain upper-half waveform press, the SHIFT-exact waveform press at either
// height (architect 2026-08-05, when the former's anchor moved to the click and
// its non-dissolving twin died with the span it preserved), the empty
// flag/triangle-lane parity press (whose armed drag then extends normally, the
// motion path being y-agnostic) and the `h` history
// view's own full-height press. Alt/Ctrl no-op earlier. A completed drag rests the
// region on release UNLESS its final on-screen span is
// under the same kDragMovedThresholdPx gate — the gate latches once past the
// arm and never re-engages, so a jitter drag could otherwise rest a sliver,
// which dissolves like a click instead (end_region_drag_min_size_check, at both
// end points). The drag never touches the selection anywhere — the press's
// deselect/drop was the committed act, and downward-only is structural (there
// is no selection write in the drag or at its ends). ESC DOES NOTHING TO A DRAG
// IN FLIGHT: pointer gestures have no cancel, so a mid-drag Esc is swallowed by
// the drag-modal gate and the drag keeps extending under the pointer; the release
// rests the region where it stands (under the sliver gate). Esc clears a RESTED
// span (architect 2026-07-30, the arm in on_key) — clear but never cancel, and
// the gate is what makes those two cases distinct. This state was
// the first to lose its pre-press snapshot — the whole family followed. The rule
// is at the drag-modal gate (input_handler.cpp). Session-only, never undoable.
struct RegionDragState {
    bool    active       = false;
    bool    moved        = false;  // crossed the threshold into a real drag
    int     press_x      = 0;      // press position (window px), for the gate
    int     press_y      = 0;
    int64_t anchor_frame = 0;      // active-domain frame the press placed
};

// F2.1: mouse drag-to-select inside the active text editor. Only one
// editor is active at a time, so the active editor (and thus its text
// geometry) is discoverable from the per-editor is_active checks; a single
// armed flag is enough. Set on a press that lands on the active editor's
// text region; cleared on release, on a lost button mid-drag, and on file
// load (the motion / release handlers also self-heal if the backing editor
// closes out from under an in-flight drag).
struct EditorTextDragState {
    bool active = false;
};

// Pending marker-reposition drag, armed by a PLAIN (unmodified) flag press.
// The press single-selects its marker immediately (the click — with no exception
// for a member of a 2+ selection since 2026-07-29, groups being never moved; the
// doctrine is at the head of position_nudge.h), then
// arms this pending state instead of the drag itself: only once the pointer
// travels past kDragMovedThresholdPx (Chebyshev from the press; the one generic
// 8px gate shared by every press-becomes-drag surface) does begin_drag run and
// the marker-drag machinery take over. Deferring
// begin_drag to the crossing keeps its pre-drag snapshot (the undo payload) and
// its wall math exact — nothing mutates the store between press and crossing; the
// selection capture that used to be on that list was deleted with the cancels
// (2026-07-29) — and lets a sub-threshold press-release stay a pure click. Session-only,
// never serialized. Cleared on the crossing (begin_drag takes over), on
// release / lost button before the crossing, by the force-end finalizer, and on
// file load.
// Shift never arms it, and a read-only tab never arms it (marker mutation is
// refused there — the select still lands).
//
// NOTHING IS HELD BACK HERE. The GROUP-drag deferral (deferred_click, architect
// 2026-07-23 — a press on a member of a 2+ selection withheld its single-select +
// land so begin_drag could seed the whole group, the file-manager convention) died
// 2026-07-29 with the group drag itself: every marker press commits its whole
// click at press time, so a release / lost button / force-end has nothing to
// complete and simply DISARMS. Esc does nothing either (pointer gestures have no
// cancel — the rule at the drag-modal gate in input_handler.cpp): the arm survives
// the press and resolves by the threshold crossing or a real release / button loss.
struct PendingMarkerDrag {
    bool active         = false;
    int  marker         = -1; // marker index to reposition (active view's list)
    int  press_x        = 0;  // press position (window px): the gate + drag anchor
    int  press_y        = 0;
};

// (No pending TEMPO drag, and no TempoDragState: the whole target-view tempo drag
// is DELETED, architect 2026-07-29 — the tempo surface is the bare Up/Down cent
// step alone. The delete list and the do-not-re-propose note live at the head of
// marker_drag.h. So the reposition drag above is the ONLY pointer marker gesture,
// and W+target has no pointer authoring gesture at all.)

// Pending trim cap/bridge drag, armed by a PLAIN (unmodified) left press in the
// top-strip TRIM BAR lane (an endcap rect, or the bar's inter-cap bridge span).
// The trim sibling of PendingMarkerDrag: the press CLAIMS the cap/bridge geometry
// but arms only this pending state; begin_trim_drag runs (and the trim-drag
// machinery takes over) only once the pointer crosses kDragMovedThresholdPx
// (Chebyshev from the press). A SUB-THRESHOLD PRESS-RELEASE IS A CONSUMED
// NOTHING again (architect 2026-07-30): the lane-click model gave it one act —
// publishing the trim window as a region highlight — and that publish is retired
// with the SPAN FORM, so the click stops nothing, deselects nothing and commits
// nothing on BOTH end paths (clean release and lost button). The DRAG carries the
// setter's deselect and the trim-mutation stop at its first accepted bound
// change. Deferring begin_trim_drag to the crossing keeps
// its anchor capture exact — nothing mutates the trim store between press and
// crossing. A full ordered pair always rests (2026-07-30), so the router arms on
// GEOMETRY alone — in a read-only tab as in a writable one since 2026-08-07,
// when trim was reclassified as band rather than authored content and the
// band's read-only return was deleted.
// Session-only, never serialized. Cleared on the crossing (begin_trim_drag
// takes over), on release / lost button before the crossing, by the force-end
// finalizer, and on file load. `is_begin` names the single bound; `both` marks the
// pair (bridge) drag, for which is_begin is Begin by construction.
// FIVE FIELDS AND NO CAPTURES (2026-07-29): the set_click flag, the pre-press pair
// (preset_*_frame) and the pre-gesture selection + region were all an Esc-restore
// origin for the ctrl / ctrl+shift bound-set press, and POINTER GESTURES HAVE NO
// CANCEL (the rule at the drag-modal gate, input_handler.cpp). A bound-set press's
// click-set is committed the moment it is made — trim is history-less, so nothing
// takes it back — which is why the pending no longer needs to distinguish itself
// from a plain endcap-drag pending at all.
struct PendingTrimDrag {
    bool active   = false;
    bool is_begin = false;  // which bound the single drag targets (Begin if both)
    bool both     = false;  // the inter-endcap bridge (pair) drag
    int  press_x  = 0;      // press position (window px): the gate + begin anchor
    int  press_y  = 0;
};

// Trim boundary drag (the live trim pointer gesture). Armed from a PendingTrim-
// Drag once the plain trim-bar press crosses the threshold — an endcap-rect hit
// drags one bound, the inter-endcap bridge drags the pair. Parallel to DragState
// but motion mutates the active tab's live trim mirror directly (no overlay);
// release triggers a target render when the bound moved. Trim is excluded from
// undo/redo. Session-only.
struct TrimDragState {
    bool active   = false;
    bool is_begin = false;   // which bound the cursor is dragging
    bool moved    = false;   // whether motion actually changed the bound
    // Dragged bound's pre-drag value (an at-rest copy of the store's
    // authored int64 frame); base for the drag delta.
    int64_t orig_frame      = 0;
    // Press position in source-frame doubles, captured at drag-begin.
    // Motion applies the cursor's displacement from here (anchor-relative),
    // matching warp-marker drag — so the bound tracks the grab point with no
    // initial snap. See DragState::anchor_mouse_time_frame.
    double anchor_frame     = 0.0;

    // Inter-endcap bridge (top-strip trim-bar span) move-both-bounds drag: both
    // bounds translate together by
    // the same delta in the active (on-screen) domain, preserving the gap
    // as it appears under warp. The pair has no grabbed-bound notion — both
    // bounds are the subject, it has no viewport clamp, and it never moves the
    // playhead; `is_begin` is Begin by construction (the router arms it so) and
    // is read only by the single-bound path, which uses it to name the one
    // bound that moves.
    // orig_begin/orig_end are LIVE MECHANICS, the one thing to know about them now
    // that no cancel reads anything here (2026-07-29 — pointer gestures have no
    // cancel; the rule is at the drag-modal gate in input_handler.cpp): the rigid
    // PAIR path rides its delta off them, and commit_trim_drag's release snap uses
    // them as its untouched-bound test (a bound equal to its origin keeps its
    // stored value bit-exact instead of column-snapping). They hold the values at
    // BEGIN — for a bound-set-armed drag, the click-set values, since that click
    // already committed. The set_click flag and the pre-gesture
    // selection + region that used to sit here were the Esc-restore origin and are
    // deleted with it; a force-ended trim drag keeps its bounds, and trim's
    // exclusion from undo/redo means nothing takes them back — the standing trim
    // ruling, not a gap this opened.
    bool    both                 = false;
    int64_t orig_begin_frame   = 0;
    int64_t orig_end_frame     = 0;
    int64_t anchor_active_frame  = 0;
};

// Dual-axis zoom/pan drag (Ableton-style navigation), armed by ONE surface: a
// CTRL-exact left-drag inside the waveform. It had a second entry — a plain
// left-drag on the dedicated zoom lane — until that lane was deleted
// (architect 2026-07-31); the GESTURE is untouched and keeps its full reach on
// the surviving entry, which is why the deletion cost nothing. The
// gesture is DUAL-AXIS, freely composed with no axis lock: vertical motion
// drives the zoom level and horizontal motion pans the viewport, both applied
// per motion event. It is INCREMENTAL — each event reads the LIVE zoom level and
// viewport (never a stored press baseline) and applies its own dx/dy on top, so
// nothing goes stale across composed pan/zoom phases. One song anchor
// (anchor_sample) is the focus the zoom pivots around; the pan re-derives its
// drifted column each event, and the Ableton edge trick REBINDS the anchor to
// the nearest visible pixel when a pan pushes its column offscreen (the focus
// pins to the edge it hits and becomes that edge's content). Navigation-class:
// never touches the playhead or selection, allowed in read-only, does not toggle
// or override follow. Cleared on button release / button-lost, by the force-end
// finalizer, and on file load; nothing to revert anywhere (it applies its zoom and
// pan continuously, and pointer gestures have no cancel).
struct StripDragState {
    bool   active    = false;
    // True once any motion event has applied a change. A motionless
    // press-release must commit nothing, so the terminating event finalizes
    // (one final apply + synchronous rebuild) only when this is set.
    bool   moved     = false;
    // Pointer position at the press (window px) — the drag-threshold reference
    // ONLY (the Chebyshev gate deciding press-becomes-drag). Not a zoom or pan
    // baseline: the incremental model reads no press level and no fixed column.
    int    press_x   = 0;
    int    press_y   = 0;
    // Pointer position at the previous motion event (window px), seeded at the
    // press. Each event's dx/dy is the delta from here — dx pans, dy zooms.
    int    last_x    = 0;
    int    last_y    = 0;
    // Song position (frames, double) the zoom pivots around — the frame under
    // the press at the press, but REBINDABLE: when a pan drives its column off
    // the effective waveform width, the edge trick pins it to the nearest
    // onscreen pixel and rewrites this to that pixel's frame.
    double anchor_sample = 0.0;
};

// Alt+drag on the waveform: continuous 1:1 grab-pan of the viewport, driven by
// pointer motion, panning by the exact per-event pixel delta. It CAPTURES the
// pointer (begin_strip_pointer_capture, the same cursor-hide + lock the zoom
// strip uses): while captured the platform delivers unbounded virtual
// coordinates, so the pan travels infinitely while the viewport clamps at the
// song walls; the cursor reappears at the raw traveled virtual_pointer_x_ (the
// compositor clamps an off-window hint on-screen), y at the press row — the
// pan sets no anchor-stem restore override, unlike the strip drags. PAN-ONLY,
// though — no zoom axis and no anchor stem (the stem is the zoom pivot
// affordance, gated on strip_drag.active). Navigation-class: allowed in
// read-only, deliberately does NOT override follow, never touches the playhead
// or selection. Every exit path (release, motion button-lost, the force-end
// finalizer) calls end_strip_pointer_capture (idempotent); no cancel path exists. Cleared on button release / lost
// button, by the force-end finalizer (Ctrl+Q / resize / WM close), and on file
// load. Nothing to restore anywhere: it applies its pan continuously, and pointer
// gestures have no cancel.
struct ScrollDragState {
    bool   active   = false;
    // Pointer x (px) at the previous motion event, seeded at the Alt press.
    int    last_x   = 0;
};

// (The SCRUB has no drag state: its TWO press entries — the plain LOWER-HALF
// left press and, since 2026-08-01, the BARE RIGHT press over the waveform's
// FULL HEIGHT (architect: right-click anywhere on the waveform is a scrub) —
// each run ONE act through the one body, scrub_press_at (input_pointer.cpp,
// where the two-caller enumeration lives). The marker-text lane's own scrub was
// deleted (architect 2026-07-27, and the lane itself in row 5). The act
// is a ONE-SHOT (scrub_act_at: stop a live session,
// else start one at the clicked frame), issued once per click — the press arms
// nothing, a held press does nothing further, and motion over the scrub
// surface is inert (architect 2026-07-23, the Ableton model; the former
// per-column re-scrub drag and its drag-state struct are removed — each
// click pays AT MOST one stop-quiescence fence (a stopped session's launch
// pays none), so the per-column fence cadence is structurally gone). The
// gesture drives the SCANNER only, never
// the cursor: selection, region, cursor, follow, and double-click seeding are
// all untouched, the pure audition gesture. NOT the retired plain-drag scrub
// (61126db) — that one MOVED the cursor playhead per column.)

// The surface a double-click candidate belongs to. The surface tag is what keeps
// the four double-click surfaces from cross-firing: a candidate seeded on one
// surface can only be consumed by a press on the SAME surface (a trim-bar click
// then a marker click within the window can never consume). None = no candidate.
enum class DoubleClickSurface { None, TrimBar, Marker, EditorText, EmptyLane };

// Double-click detection (Wayland delivers no double-click event, so it is
// hand-rolled from two plain clicks). A click on a double-click-bearing surface
// records this candidate (at a motionless release for TrimBar / EditorText; at
// the PRESS for Marker — see below); the NEXT plain press on the SAME surface,
// if it lands within kDoubleClickMs and kDoubleClickSlackPx of the recorded
// position AND (for Marker) targets the same marker, is consumed as that
// surface's double-click action instead of the single-click action. A drag that
// MOVED records nothing and clears any candidate. Surfaces:
//   TrimBar    -> the SPAN-FRAMING command on the trim bar lane, its whole band
//                 (run_span_framing_command: a live region, else a proper trim
//                 sub-window, else the whole song) — and, while the `h` HISTORY
//                 VIEW stands, the VIEWED CHECKPOINT'S DIFF SPAN instead
//                 (frame_viewed_commit_diff_span, 2026-08-05: one gesture on one
//                 band, two commands, the mode picking between them at the
//                 press). Target unused; both axes'
//                 slack compared. It REHOMED here from the deleted zoom lane
//                 (architect 2026-07-31) and consumes AHEAD of the cap/bridge
//                 drag arm, so the second click frames rather than grabs. The
//                 seed is that lane's own press record (TrimBarPressSeed),
//                 not a strip-drag field: the lane arms a pending trim drag
//                 rather than a live one, so there is no drag state to hang it
//                 on and the motionless test is the release's own slack compare.
//   Marker     -> opens the marker's flag editor (target = marker index; both
//                 axes' slack compared). The marker is ONE pointer item: the hit
//                 is its FLAG BOX (the painter's published rect) or its STEM in
//                 the waveform's upper half — the marker-text lane's run died in
//                 row 5 — and a candidate seeded on one part consumes on the
//                 other. One seed
//                 timing for the whole surface — the PRESS; a press that then
//                 becomes a real marker drag (the reposition drag, the only one
//                 left since the tempo drag's deletion) drops the candidate at the
//                 threshold crossing, so a moved drag never carries one.
//   EditorText -> selects the clicked character class's RUN (word / punctuation
//                 / whitespace) in the active text editor (target unused; both
//                 axes' slack compared).
//   EmptyLane  -> creates a marker at the clicked position on an EMPTY flag /
//                 marker lane spot (architect 2026-07-23): the AUGMENTED
//                 drop, exactly what bare `s` performs (warp copy-previous /
//                 phase reset N/2 lead-in), home-view and read-only gated
//                 silently. PLAIN presses only — a modified press on the lane
//                 claims nothing and seeds nothing. Seeded at the PRESS
//                 (position-keyed, target unused, both axes' slack compared),
//                 which also runs the waveform-parity placement. Cleared like
//                 every candidate when the armed region drag moves.
// Cleared on file load, the moment an action fires, and — the KEYBOARD and
// WHEEL halves of the lifetime — at the TOP of every on_key AND on_wheel
// command: any keyboard command OR wheel frame between two
// clicks breaks EVERY candidate at those chokepoints, so a seed formed in one
// context can never consume in another after an intervening keypress (Esc
// included) or a wheel zoom/pan that moved content under the pointer. The
// pointer half is the on_button_press top-of-frame clear, the moved-drag clears,
// and the force-end finalizer's clear (a force-end is not a clean click
// sequence). Session-only.
struct DoubleClickCandidate {
    DoubleClickSurface surface = DoubleClickSurface::None;
    int64_t time_ms   = 0;      // CLOCK_MONOTONIC ms at the seeding press/release
    int     press_x   = 0;      // seed x (Marker seeds at the press; TrimBar /
    int     press_y   = 0;      //   EditorText at a motionless release)
    int     target    = -1;     // marker index for Marker; unused otherwise
};

// THE TRIM-BAR FRAMING DOUBLE-CLICK'S FIRST HALF, recorded at the press because
// only the RELEASE can tell a click from a drag. A plain trim-bar-lane press
// (any spot in the band — endcap, bridge, or bare ground; read-only included and
// the `h` history view too, the framing
// being pure navigation in both) records this; the left release seeds the TrimBar
// candidate when the pointer is still within kDoubleClickSlackPx of the recorded
// point and no trim drag went live. That slack IS the motionless test: it equals
// kDragMovedThresholdPx, so "never became a drag" and "never left the slack" are
// the same condition by construction. Cleared at every left release (the release
// consumes it) and by the force-end finalizer, beside the candidate's own clear.
// (A THIRD CLEARER lived here for one day of 2026-08-09 — the checkpoint's
// acknowledge modal, which could be raised from a worker's clock between this
// press and its release and would have had that release swallowed at its own
// prompt gate. It went with the modal, which became a paint-only slot: no
// asynchronous route raises a prompt any more, so a release is never stolen and
// this record is never stranded.) Session-only.
struct TrimBarPressSeed {
    bool active  = false;
    int  press_x = 0;
    int  press_y = 0;
};

// THE ROSTER OF REDESIGNED BUTTONS — the single enumeration of every flat
// button the kdenlive rows carry, in painted order: row 1's Quit, Navigation and
// Settings plus the view bar's three, row 2's toolbar four, row 3's two TABS,
// then row 4's fifteen view / mode / action buttons. It exists ONCE, here, because it indexes
// the painter's hit stash (AppState::redesign_buttons) and both readers key off
// it; each domain then attaches its own attribute to these ids and to nothing
// else — the painter's label/icon/layout table (paint_handler.cpp) and the
// press claim's chord table (input_pointer.cpp). Adding a button is one row
// here plus one row in each of those two tables.
//
// THE TABS ARE BUTTONS IN THE ROSTER SENSE (2026-07-31) and joined it rather
// than growing a parallel pair: their rect is painter-published from a shaped
// label exactly as every other entry's is, their hover is the same one-transition
// recompute, and their press is the same band claim dispatching a chord through
// on_key. What they do NOT take is the two row-2-only faces — no click face, and
// no disabled face of their own — which is stated at each face's site rather
// than modelled here (row 4 takes the click face but not the disabled one; the
// `h` history view's mode-scoped dead face, 2026-08-04, reaches all three rows
// and is the one exception, at redesign_button_enabled below). Row 1's SETTINGS
// and NAVIGATION are the roster's TWO non-chord entries: each press TOGGLES ITS
// OWN DROPDOWN, which no keyboard chord does, and both are spelled at the menu
// claim rather than in the chord table. Quit is not one of them — Ctrl+Q is its
// chord and it sits in the table like the rest.
//
// The enum ORDER is painted order, and redesign_button_index depends on the
// values staying 0..kRedesignButtonCount-1 contiguous (the tick comparator in
// main.cpp walks the range by index). The indices are DERIVED and never
// serialized, so inserting a button mid-roster (as Settings was) or REORDERING
// two of them (as row 1's Settings and Navigation were, 2026-08-03) renumbers
// the stash harmlessly.
enum class RedesignButton {
    // Row 1, the menu row: the three LEFT-FLOATING buttons, then the three of
    // the RIGHT-FLOATING view bar (2026-08-02) in their painted order — the
    // absolute view selectors S+W / T+P / T+W, which are bare 1/2/3.
    //
    // SETTINGS PAINTS LAST IN THE LEFT FLOAT (architect 2026-08-03). The float
    // is adjacent with no gap and its layout is a shaped-run walk, so the move
    // is an ORDER change and nothing else — no width, no padding, no anchor
    // expression follows it. This enum and the painter's kMenuButtons table are
    // the two places that carry the order, and they move together.
    Quit, Navigation, Settings, ViewSW, ViewTP, ViewTW,
    // Row 2, the toolbar.
    Save, Undo, Redo, Render,
    // Row 3, the tabs — TWO SLOTS, ALWAYS. They are the A/B tabs ordinarily and
    // the `h` view's WALK SELECTOR while it stands, "Remote" and "Local"
    // (architect 2026-08-08): the row grew to four for the (walk source,
    // reading) product on 2026-08-07 and went back to two the following day,
    // when the READING left the row for its own toggle button in row 4
    // (IconCumulative below) and the tabs were left naming the walk alone.
    // TabC and TabD are deleted whole with that arc — enum, chord rows, faces,
    // tooltips and the painter's defs — so nothing publishes an empty rect in
    // this row any more.
    TabA, TabB,
    // Row 4, the icon row, in painted order: the two view radio pairs, the
    // phase-reset clipboard pair, the three mode/editor buttons, the two
    // render-entry buttons, then the history mode's own. (THE ZOOM PAIR LEFT
    // 2026-08-02 — the architect's no-duplicate-commands ruling, its two
    // commands now living in the Navigation dropdown; the `-` / `=` KEYS are
    // untouched.)
    IconS, IconT, IconW, IconP,
    IconCopy, IconPaste, IconBpm, IconIter, IconFollow,
    IconListen, IconLoadInPlace,
    // THE HISTORY MODE'S BUTTON (2026-08-04), ruled with the mode itself and
    // landed with the commit act: bare `h`, in its own group past a separator,
    // lit while the mode stands. Its chord toggles, so the same click that
    // opened the view closes it.
    IconHistory,
    // THE CUMULATIVE READING'S TOGGLE (2026-08-08), immediately right of the
    // button that opens the view: bare `u` selects the CUMULATIVE delta
    // (the viewed member against the frozen live now side) and off is
    // ITERATIVE (the member against the next-newer one). It is the axis row 3
    // carried for one day as two more tabs; the architect moved it here and
    // made it a MODE toggle, so the tabs name the walk alone.
    //
    // IT IS A SESSION PREFERENCE, not view state: the bit lives on AppState
    // (history_cumulative) and survives every mode edge, which is why this
    // button's selected face is published from that bit in EVERY view rather
    // than only inside the mode. It rests DISABLED like the three below it —
    // `u` is bound only inside the view — so outside the mode it shows the
    // true reading on a dead face.
    IconCumulative,
    // THE REVERT ACT (2026-08-05), immediately right of the button that opens
    // the view and left of the walk's two: Ctrl+H applies the SELECTED diff
    // flags backwards into the live state and closes the view. It rests
    // DISABLED with the walk pair below and for the same reason — its chord is
    // bound only inside the view — and greys INSIDE it too whenever no diff
    // flag is selected, which is the allowlist's own conditional admission
    // arriving through the derived partition (redesign_button_enabled below).
    IconRevert,
    // THE WALK'S TWO STEPS (2026-08-05), sharing that group: OLDER (bare `,`)
    // and NEWER (bare `.`), so the checkpoint walk is drivable with the mouse
    // alone. They were the roster's FIRST buttons whose RESTING state is
    // DISABLED — outside the history view their keys are bound to nothing at
    // all, so a live face would advertise an act that does not exist — and the
    // exception is spelled at redesign_button_enabled below, beside the
    // history mode's own. (Revert above joined them the same day, the
    // Cumulative toggle on 2026-08-08, making that family FOUR.)
    IconHistoryOlder, IconHistoryNewer
};
// THE ROSTER, re-derived by counting the enumerators above: six in row 1, four
// in row 2, two in row 3 and sixteen in row 4 — 28. Of those, TWENTY-SIX carry
// a chord in kToolbarChords and TWO are the dropdown anchors (Settings and
// Navigation), which is the split the chord table's own static_assert checks.
// It was 29 until 2026-08-08, when row 3's compare-only pair was deleted and
// row 4 gained the Cumulative toggle.
inline constexpr int kRedesignButtonCount = 28;
inline constexpr int redesign_button_index(RedesignButton b) {
    const int i = static_cast<int>(b);
    // STATE THE INVARIANT THE ENUM ALREADY CARRIES, don't add an arm. A scoped
    // enum's VALUE RANGE is the smallest BIT-FIELD holding its enumerators, not
    // its enumerator set — so whenever the count is not a power of two the
    // optimizer must assume values above the last enumerator are possible, and
    // that assumption alone makes every roster subscript look one past the end
    // (-Warray-bounds fires at -O3). This is not an error arm (the validation
    // topology's "an error arm exists iff a producer exists" applies: there is
    // no producer — every value comes from a named enumerator or from main.cpp's
    // comparator loop, which is bounded by kRedesignButtonCount) but a statement
    // that the out-of-set value cannot occur, which is exactly what
    // std::unreachable is for. It stays even at a power-of-two count, where the
    // warning happens not to fire, because the invariant is what is true — not
    // the current arithmetic coincidence.
    if (i < 0 || i >= kRedesignButtonCount) std::unreachable();
    return i;
}

// WHICH BUTTONS ARE ROW 1'S — the menu row's three plus the view bar's three,
// named beside the roster because that is where a reader meets the membership.
// The enum's order IS the painted order, so the six happen to be contiguous at
// its head; this says ROW rather than "index < 6" anyway, because the row is
// the fact and the contiguity is an accident of how the roster is written.
//
// ITS ONE CONSUMER IS THE DROPDOWN CLOSE RULE (on_motion's open-dropdown branch,
// input_pointer.cpp): while a menu is up, a pointer inside a row-1 button that is
// not a dropdown anchor CLOSES it, because only one button in that row is lit at
// a time. It was briefly the hover predicate's too — an exemption letting row 1
// hover under an open popup — and that exemption is retired: with the close rule
// in front of it, a non-anchor row-1 button can no longer be hovered while a menu
// is up (the motion that reaches it closes the menu first), and an ANCHOR's pill
// comes from the painter's own open condition rather than the hover bit.
//
// EXHAUSTIVE, NO `default` ARM — redesign_button_enabled's rule for the same
// reason: a new button fails to compile here until its row is stated, instead
// of silently inheriting another row's answer.
inline constexpr bool redesign_button_in_menu_row(RedesignButton b) {
    switch (b) {
        case RedesignButton::Quit:
        case RedesignButton::Navigation:
        case RedesignButton::Settings:
        case RedesignButton::ViewSW:
        case RedesignButton::ViewTP:
        case RedesignButton::ViewTW:
            return true;
        case RedesignButton::Save:
        case RedesignButton::Undo:
        case RedesignButton::Redo:
        case RedesignButton::Render:
        case RedesignButton::TabA:
        case RedesignButton::TabB:
        case RedesignButton::IconS:
        case RedesignButton::IconT:
        case RedesignButton::IconW:
        case RedesignButton::IconP:
        case RedesignButton::IconCopy:
        case RedesignButton::IconPaste:
        case RedesignButton::IconBpm:
        case RedesignButton::IconIter:
        case RedesignButton::IconFollow:
        case RedesignButton::IconListen:
        case RedesignButton::IconLoadInPlace:
        case RedesignButton::IconHistory:
        case RedesignButton::IconCumulative:
        case RedesignButton::IconRevert:
        case RedesignButton::IconHistoryOlder:
        case RedesignButton::IconHistoryNewer:
            break;
    }
    return false;
}

// WHICH BUTTONS ARE ROW 3'S — the two tabs, which are the A/B pair ordinarily
// and the `h` view's Remote / Local walk selector while it stands. Named once
// because FOUR places ask it and all four are about the ROW rather than about
// either slot: the hover carve-out (the selected tab has no hover face), the
// tooltip override (the walk selector carries none), the label override, and
// the press claim's walk over the row's hit rects.
inline constexpr bool redesign_button_is_tab(RedesignButton b) {
    return b == RedesignButton::TabA || b == RedesignButton::TabB;
}

// THE MENU ROW'S DROPDOWNS — WHICH ONE IS UP. There is ONE popup state in the
// product (AppState::dropdown below), and this names its content; `None` IS the
// closed state, which is what makes "two dropdowns are never open together"
// structural rather than an invariant to maintain: opening one is writing this
// field, and a field holds one value.
enum class DropdownMenu { None, Settings, Navigation };

// WHICH BUTTON A MENU HANGS FROM. The dropdown is flush under the button that
// emits it (architect 2026-08-02), so the painter and the open edge's damage
// both need the anchor, and they must read ONE expression or the damaged band
// and the painted box could start on different rows of pixels.
inline constexpr RedesignButton dropdown_anchor_button(DropdownMenu m) {
    return m == DropdownMenu::Navigation ? RedesignButton::Navigation
                                         : RedesignButton::Settings;
}

// THE SETTINGS DROPDOWN'S ITEMS — the single enumeration, in painted order, of
// what the menu row's Settings button drops down. Each row pairs the HUMAN
// LABEL (the redesign's capitalization ruling; "GUI scale" and "URL" keep their
// acronym case) with the .settings KEY the click prefills into the editor, and
// `separator_before` marks the one place the two categories part: the two GUI
// keys, then the four metadata keys.
//
// It lives here rather than in the painter because three domains read it — the
// painter (labels, layout), the press claim (which key a click prefills) and
// the popup's own geometry (item count). The keys are the canonical .settings
// spellings, so the prefill goes through the ordinary recall serializer with
// nothing translated on the way.
struct SettingsPopupItem {
    const char* label;
    const char* key;
    bool        separator_before;
};
// (THE "Font size" ITEM LEFT WITH ITS KEY — row 7, 2026-08-01. The widest label
// is still "Playback speed", so the popup's authored width is unchanged; only
// the item count and the height derived from it moved.)
inline constexpr SettingsPopupItem kSettingsPopupItems[] = {
    {"GUI scale",      "gui_scale",      false},
    {"Playback speed", "playback_speed", false},
    {"Title",          "title",          true},
    {"Notes",          "notes",          false},
    {"URL",            "url",            false},
    {"Cover",          "cover",          false},
};
inline constexpr int kSettingsPopupItemCount =
    static_cast<int>(std::size(kSettingsPopupItems));

// THE NAVIGATION DROPDOWN'S ITEMS (architect 2026-08-02) — a COMMAND MENU,
// where the settings one is a list of keys to edit: every row IS an existing
// keyboard command, dispatched through on_key exactly as a redesigned button
// dispatches its chord, so every gate and refusal arrives by construction and
// no second route exists. Seven rows in two categories over one separator: the
// four zoom/framing commands, then the three marker/tab steppers.
//
// IT DISPLAYS ITS HOTKEYS, by explicit architect design and against nothing:
// the no-gesture-hints-in-UI preference is about hint PROSE inside labels, and
// the architect ordered kdenlive's accelerator column here (its own crop,
// dropdown_full_hotkeys.png, is the anatomy). The spellings follow that crop's
// convention — a bare letter uppercase, modifiers spelled out with `+`.
//
// AN ITEM NEVER GREYS OUT and never refuses here, WITH ONE RULED EXCEPTION: a
// command that cannot act right now still dispatches and its own arm answers,
// which is the roster's standing buttons-never-grey rule ("one that cannot act
// right now simply does nothing, exactly like its key") applied one surface
// further out. THE EXCEPTION IS "Walk both tabs" INSIDE THE `h` HISTORY VIEW
// (architect 2026-08-08): in there Ctrl+Shift+Tab is not the A/B walk at all —
// the mode claims it as the REVERSE cycle of its own walk-selector row — so an
// item left live would dispatch a chord that does something else entirely under
// a label promising the walk. It greys rather than lying. That is a difference
// in KIND from every other refusal on this menu, which are all "the same command,
// with nothing to act on"; the predicate and the whole argument are at
// dropdown_item_enabled, below AppState.
//
// EVERY OTHER ROW IS LIVE IN THAT VIEW, which is the other half of the same
// ruling and why the menu opens there at all: zoom in / out / overview reach the
// mode's keyboard allowlist (history_mode_key_blocked), and center-on-focus and
// the two marker steps are claimed by the mode's own vocabulary
// (history_mode_owns_key) as re-expressions over its diff flags. The dispatch
// below needs no arm for any of it — the items are chords, and the mode answers
// per item at the same two gates a key does.
struct NavigationPopupItem {
    const char* label;
    const char* hotkey;   // the accelerator column's text, right-aligned
    GuiKey      key;
    bool        ctrl;
    bool        shift;
    bool        alt;
    bool        separator_before;
};
inline constexpr NavigationPopupItem kNavigationPopupItems[] = {
    {"Zoom in",         "=",              GuiKeys::Equal, false, false, false, false},
    {"Zoom out",        "-",              GuiKeys::Minus, false, false, false, false},
    {"Overview",        "0",              GuiKeys::Digit0, false, false, false, false},
    {"Center on focus", "C",              GuiKeys::C,     false, false, false, false},
    {"Next marker",     "Tab",            GuiKeys::Tab,   false, false, false, true},
    {"Previous marker", "Shift+Tab",      GuiKeys::Tab,   false, true,  false, false},
    {"Walk both tabs",  "Ctrl+Shift+Tab", GuiKeys::Tab,   true,  true,  false, false},
};
inline constexpr int kNavigationPopupItemCount =
    static_cast<int>(std::size(kNavigationPopupItems));

// The published-rect array's size: the widest menu decides it, so a menu that
// grows a row grows the array with no second edit.
inline constexpr int kDropdownMaxItemCount =
    kSettingsPopupItemCount > kNavigationPopupItemCount
        ? kSettingsPopupItemCount : kNavigationPopupItemCount;

// THE PAINTER'S AND THE GEOMETRY'S VIEW OF AN ITEM — what the two menus share,
// which is exactly the row's TEXT and where the categories part. The ACTION is
// deliberately not in here: the two kinds differ in kind (a settings key to
// prefill, a chord to dispatch), each stays typed in its own table, and the
// release body switches on the menu once. One shared view, two typed actions.
struct DropdownRow {
    const char* label;
    const char* hotkey;   // nullptr -> this menu has no accelerator column
    bool        separator_before;
};
inline constexpr int dropdown_item_count(DropdownMenu m) {
    switch (m) {
        case DropdownMenu::Settings:   return kSettingsPopupItemCount;
        case DropdownMenu::Navigation: return kNavigationPopupItemCount;
        case DropdownMenu::None:       break;
    }
    return 0;
}
inline constexpr DropdownRow dropdown_row(DropdownMenu m, int i) {
    if (m == DropdownMenu::Navigation) {
        const NavigationPopupItem& it =
            kNavigationPopupItems[static_cast<size_t>(i)];
        return {it.label, it.hotkey, it.separator_before};
    }
    const SettingsPopupItem& it = kSettingsPopupItems[static_cast<size_t>(i)];
    return {it.label, nullptr, it.separator_before};
}

// The open dropdown's painted HEIGHT, derived from its table and the scale
// alone — no shaping, no paint. Its one non-painter reader is the OPEN EDGE
// (toggle_dropdown), which has to damage the box on the frame BEFORE the box
// exists: at 100% the settings popup happens to fit inside the top strip, but at
// 200% it hangs ~40px past it, and a redraw is clipped to the damage it was
// given. The painter calls this too, so the damaged height and the painted
// height are one expression and cannot drift.
inline int dropdown_h_px(DropdownMenu m) {
    const int count = dropdown_item_count(m);
    int separators = 0;
    for (int i = 0; i < count; ++i)
        if (dropdown_row(m, i).separator_before) ++separators;
    const int border = popup_border_px();
    return count * popup_item_h_px() +
           separators * (2 * popup_sep_margin_y_px() + border) +
           2 * popup_item_margin_y_px() +
           2 * border;
}

// Double-click window and positional slack (architect-tunable). Two motionless
// plain clicks in the same strip row inside this time and pixel distance are a
// double-click.
constexpr int64_t kDoubleClickMs      = 500;
constexpr int     kDoubleClickSlackPx = 8;

// ONE generic Chebyshev pixel distance a press must travel before it becomes a
// DRAG (architect-tunable), shared by EVERY press-becomes-drag surface — strip,
// region, trim, and the marker flag (the tempo flag was a fifth until the tempo
// drag's deletion, 2026-07-29). UNIFIED to 8px (architect
// 2026-07-24: region felt too hair-trigger at the old 3, and the separate
// kMarkerDragMovedThresholdPx = 8 was folded into this one constant). Two
// rationales, now one story:
//  - CAPTURE-JITTER / DOUBLE-CLICK STARVATION (strip, waveform region): under
//    pointer capture the relative-pointer stream delivers every sub-pixel sensor
//    tick as a motion event, so a physical click almost always rocks the sensor a
//    count or two; without this gate that jitter would mark every click as moved
//    and starve double-click detection, and on the waveform a click would become
//    a micro-region instead of plain playhead placement.
//  - MARKER GRAB SLOP (the marker flag): a flag must be easy to click
//    (select, or double-click to edit) without nudging it, and pixel-exact
//    fine-tuning lives on the bare Left/Right nudge rather than the drag — the
//    Ableton convention. 8px gives that slop; the strip/trim/region surfaces
//    inherit it.
// One latch shape everywhere — a motion event below the threshold is ignored
// outright (moved stays false, no apply, the drag stays armed); once a drag,
// always a drag, so dragging back near the press has no dead zone. The strip
// drag leaves last_x/last_y at the press until the crossing, so the crossing
// event folds the whole accumulated delta and no travel is lost.
// RECORDED FALLBACK: if the strip/trim feel degrades at 8, re-split into a
// per-surface pair (the pre-2026-07-24 form: strip/region/trim at 3, markers
// at 8).
constexpr int     kDragMovedThresholdPx = 8;

// THE HOVER POPUP STATE IS DELETED (row 5, 2026-08-01). HoverPopupState cached
// one hovered marker's identity, its composed lane text, its pass/ref resolved
// readout, its pasteable copy payload and three staleness generations (both
// marker stores plus the displayed map), and it drove three surfaces: the
// marker-text lane's one-run fallback tier and its spell-out expansion, the
// bottom strip's resolved readout, and the Ctrl+C copy. All three are settled
// without it — the lane and its resolver are gone (a marker's value is written
// on its flag), and the readout and the copy both took the SELECTION
// translation (their sites: paint_bottom_strip and the Ctrl+C binding in
// input_handler.cpp). The staleness machinery went with it: the three
// generations, the convergence loop, the on_tick repair and the pointer-leave
// clear all existed to keep a CACHE honest, and there is no cache left.

// What action triggered the modal prompt; the activate-response dispatch
// switches on this together with the response key. Save/Discard/Cancel
// applies to the unsaved-work prompt (CLOSE_WINDOW, the quit gesture).
// ERROR_NOTICE is the dismiss-only error popup for the environmental,
// settings-choice, and tripwire-class refusals (see
// GuiPrompt::open_error_notice's caller list). Its text is the owner's own
// error string, unmodified, and its sole response is acknowledge/dismiss.
enum class DialogTrigger {
    CLOSE_WINDOW,
    PASTE_CONFIRM,
    ERROR_NOTICE,
    // Load-time render-environment mismatch (GuiPrompt::open_env_hash_mismatch):
    // advisory only — 'o', the sole response key, stamps the stored hashes to
    // the current environment (history-less, no-dirty GUI-kind state that
    // persists on the next ordinary save). No dismiss-without-ack path:
    // acknowledging is the only way past the prompt.
    ENV_HASH_MISMATCH,
};

// In-window modal prompt state. When `active` is true, the bottom row's
// left-hand modal/status span carries the prompt's text and response options
// (the timestamp keeps its own reserved cell at the row's right edge, and the
// span clips where that reservation begins).
// Input is owned by the prompt: only the response keys (and Esc, which
// activates the rightmost response) do anything; everything else is
// swallowed. `response_keys` holds lowercase letters and the match is
// CASE-SENSITIVE on the codepoint (the rule is at the prompt dispatch,
// input_handler.cpp). THE LABEL CAPITALIZES ITS ACCELERATOR LETTER ANYWAY
// (architect 2026-08-02): "[S]ave" reads as the word does, on pacman's Y/n
// convention — the capital marks WHICH letter answers, not that shift is
// required, and the user reads "[S]" as `s`. The match is deliberately
// unchanged: a typed capital still does not answer, which is accepted.
// The two non-letter responses match on the GuiKey instead, carry no case, and
// therefore wear their proper key names: "[Delete]", "[Esc]".
struct PromptState {
    bool                     active = false;
    std::string              text;
    std::vector<char>        response_keys;     // lowercase
    std::vector<std::string> response_labels;   // e.g. "[S]ave"
    DialogTrigger            trigger = DialogTrigger::CLOSE_WINDOW;
};

// Trim store (architect-ruled hardfail model). begin and end are authored
// NAMED ROLES — no gesture ever reassigns which bound is which — holding
// whole source frames in int64_t, exactly like marker times (a fractional
// bound is unrepresentable; the .settings writer persists the exact value as
// integer text via frame_format.h, so a saved bound reloads bit-identically).
//
// THE WINDOW IS ALWAYS SET (architect 2026-07-30). There is no unset state and
// no lone bound anywhere: not in the store, not in the .settings grammar, not
// at the render boundary. THE REST INVARIANT: for total >= 2,
// 0 <= begin < end <= total-1; for a ONE-FRAME source (load-legal) the
// canonical full pair is [0, 0], where begin < end is impossible. Mid-gesture
// crossing stays free and DOCUMENTED — readers of mid-gesture state must not
// assume begin <= end, because nothing pops mid-gesture.
//
// THE FULL WINDOW [0, total-1] IS SEMANTICALLY THE OLD UNSET STATE: it renders
// UNTRIMMED (no trim plan built at all), plays to the natural end, hashes like
// the old unset encoding, and Home/End reach the song edges. The recognition
// has ONE owner, trim_window_is_full (settings_file.h — placed there so the
// GUI and the CLI cannot disagree); every consumer asks it and no site spells
// the compare a second time. A PROPER sub-window behaves exactly as a set trim
// always did.
//
// Every trim GESTURE clamps each bound to its own absolute walls: BOTH bounds
// span frame 0 to EOF-1, the shared inclusive [0, total-1] authored domain —
// plain integer compares, the load guard's own comparison — so past-EOF cannot
// be gestured. There are NO partner walls — a bound crosses its partner freely
// during any gesture — but crossed or equal bounds can no longer REST
// anywhere: every trim commit RESETS a pair left with end_frame <= begin_frame
// back to the full window (GuiInputHandler::auto_clear_crossed_trim, the trim
// sibling of the marker normalizations — the endcaps jumping to the song edges
// are the visible signal), and a persisted crossed/equal pair resets per tab at
// load with one stderr line (file_loader). The zero floor is subsumed by the
// per-bound walls, but it remains the reason the floor exists at all: a
// negative position is unrepresentable in the authored frame form the .settings
// file persists (parse_authored_frame rejects negatives as malformed) — a
// format-representability floor, not a validity rule. A past-EOF bound is
// adversarial (the gesture walls make it uncommittable and a .settings applies
// only to its own audio, so a past-EOF bound means the audio was swapped
// outside the GUI), hard-failed at the load boundary (file_loader / CLI) like a
// corrupt audio file. validate_trim_frames (trimmer.h) stays the sole author of
// the trim-validity vocabulary, but it now sees SUB-WINDOWS ONLY (the full
// window never reaches plan_trim), and a refusal at render time still means
// "render untrimmed" (plan_trim's callers fall back to the full deliverable,
// one stderr line), never a refused render; it never guards a gesture.
//
// SEEDING: the default-constructed pair [0, 0] is construction state only —
// canonical exactly at total == 1. Every entry route seeds the real full pair
// once the source total is known (the load reset in file_loader, the per-tab
// bands beside it).
struct TrimState {
    int64_t begin_frame = 0;    // whole source frame (int64_t)
    int64_t end_frame   = 0;    // whole source frame (int64_t)
};

// TrimState's own spelling of the shared full-window predicate (the owner and
// the rationale live at trim_window_is_full, settings_file.h — this is a
// forwarder, not a second compare).
inline bool trim_is_full_window(const TrimState& t, int64_t total_frames) {
    return trim_window_is_full(t.begin_frame, t.end_frame, total_frames);
}

// Seed a trim pair to the canonical FULL window for a source of `total_frames`
// frames: [0, total-1], which is [0, 0] at total == 1 and [0, 0] for a
// degenerate/unloaded total (nothing to trim). The single seeding route — the
// load reset, the per-tab bands, and the render-entry sidecar's inactive tab
// all call it, so "what does a fresh window look like" has one answer.
inline TrimState full_trim_window(int64_t total_frames) {
    TrimState t;
    t.begin_frame = 0;
    t.end_frame   = total_frames > 0 ? total_frames - 1 : 0;
    return t;
}

// Navigational bookmark. Holds a snapshot of the fields that define
// what the user sees and where playback would start. Not in the undo domain.
// ONE PLAYHEAD PER TAB: playhead_cursor_sample is the whole of it — no
// per-column sibling exists, and the columns share this one cursor.
//
// VALUE-SHAPED SESSION STATE PARKS SAFELY; INDEX-SHAPED SESSION STATE DOES NOT
// — the rule behind the deletion of the per-tab parked SELECTIONS (architect
// 2026-07-29). Every field below is frame-, value- or flag-shaped, so nothing a
// tab parks can be invalidated by work done in the other tab: the marker stores
// are GLOBAL, but a FRAME still names the same position after an insert, a
// delete or a wholesale replace, while a raw store INDEX does not. The parked
// selections were the product's one held-across-commands index, silently
// re-pointed at other rows by the other tab's ordinary editing, and they are
// gone: the selection lives in AppState alone, follows the `t` audio-view
// switch, and is CLEARED by the `p` column switch and by Ctrl+Tab — each of
// which then auto-selects a marker whose land value is exactly the RESTING
// playhead, as the source load does (the three chokepoints of
// auto_select_marker_at_playhead, input_pointer.cpp). The one domain
// hazard this value-shaped band does carry — a target total changed between
// switches — is handled by the single clamp_playhead_to_live_domain call at the
// restore.
struct ViewState {
    int64_t viewport_start_sample      = 0;
    double  zoom_level                 = kWorkingZoomLevel;
    int64_t playhead_cursor_sample     = 0;

    // Per-tab read-only lock. Toggled by bare `o`. IT PROTECTS THE AUTHORED
    // MUSICAL CONTENT — the two marker stores and the engine settings — AND
    // NOTHING ELSE (architect 2026-08-07): while true, the active tab admits a
    // subset of keys (navigation, playback, view-switch, the save, the renders
    // and the trim gestures) and its mouse handlers block the authoring gestures
    // (drop, drag, label edit) — but NOT the trim drags or bound-set clicks,
    // trim being BAND, which is to say the rest of this very struct: the
    // viewport, zoom, playhead and trim fields around this one are all freely
    // movable in a locked tab, as is the flag itself. The ruling's home is
    // read_only_key_blocked (input_key_dispatch.cpp).
    // Persisted as tab_a_read_only / tab_b_read_only in .settings.
    bool   read_only          = false;

    // Per-tab backing store for app.trim. Synced only at the tab-swap boundary
    // in active_views.cpp (same pattern as viewport/zoom/playhead).
    TrimState     trim;
};

struct AppState {
    int     width                 = 1400;
    int     height                = 800;
    bool    loading               = false;

    // Live working copy of the active view's state — exactly the three view
    // fields immediately below, playhead / zoom / viewport (follow_mode after
    // them is session-global, not a per-tab mirror). The SELECTION is NOT one of
    // them: it lives here alone and is parked nowhere, see selected_markers.
    // This is an INTENTIONAL cache of the active view's per-view slot, not
    // accidental duplication: the paint path and the input handlers touch
    // these constantly, and the active backing store varies (source tab A/B),
    // so reading through active_view_state()
    // on every access would be both hot and conditional. The slot is synced
    // to/from these fields only at view-switch boundaries (see active_views).
    // Do not collapse this into a projection — the duplication is the design.
    int64_t playhead_cursor_sample = 0;
    double  zoom_level             = kWorkingZoomLevel;
    int64_t viewport_start_sample  = 0;
    bool    follow_mode            = true;

    // True when the user has taken the viewport away from the chase for the
    // current playback session, which stops follow_scroll_if_needed from
    // snatching it back on the next tick. THE PRODUCER INVENTORY (grep-derived;
    // this is the ONE authoritative copy — the sites carry a class statement plus
    // a pointer here) is TWO CLASSES, both gated on playback being live:
    //   * ANY VIEWPORT PAN (joined 2026-07-30, architect — "every pan
    //     suppresses"): Viewport::scroll_viewport's changed branch, which is the
    //     funnel for PageUp/PageDown, alt+wheel, touchpad scroll and the alt+drag
    //     grab-pan; plus Viewport::apply_strip_drag_zoom, which bypasses that
    //     funnel and suppresses on EITHER of the strip drag's two axes — its own
    //     viewport write AND its level write, the drag's zoom being SONG-ANCHORED
    //     and so carrying the view off the scanner the same way a pan does (a
    //     level change can leave the viewport start bit-identical, which is why
    //     the site tests both). Before this the flag's
    //     "manual-pan suppression" named a producer class that did not exist and
    //     panning away during playback was impossible with follow on (the
    //     default). A pure keyboard ZOOM is deliberately NOT a producer: it
    //     centers on the scanner during playback, so it never leaves the chase.
    //   * the PLACEMENT PRESS (place_playhead_at_click_column, input_pointer.cpp
    //     — the one body that writes this flag), which moves the cursor and
    //     reseeks. FOUR PRESSES REACH IT (re-derived 2026-08-06, the membership
    //     matching reseek_keeping_alive's own at playback_lifecycle.cpp): the
    //     plain upper-half waveform press, the shift-exact press at either
    //     height, the empty flag/triangle-lane parity press and the `h` view's
    //     own full-height press — the last of which cannot actually produce,
    //     playback being unreachable inside that view since 2026-08-05.
    // CLEARED at FOUR sites (re-derived 2026-07-30 by grepping every write, all in
    // playback_lifecycle.cpp): the ONE stop body, stop_playback_if_playing (both
    // stop edges — Space's and the tick's natural end — collapsed onto it, retiring
    // the second clearer that used to sit in restore_playhead_to_lsp); the two
    // LAUNCH edges' defensive clears, toggle_playback's play arm and
    // scrub_launch_at, which run before their own validation so even a refused
    // launch leaves it clear; and an explicit `f` re-enable while playing
    // (set_follow_mode's off->on arm). So the chase resumes at the next launch,
    // or the moment the user re-engages it.
    bool    follow_overridden_for_session = false;

    // Split-playhead state. The cursor (above, mirrored from the active
    // ViewState) is the user's stationary reference frame. The scanner is the
    // engine's playback position and is MEANINGFUL ONLY while
    // playhead_scanner_active is true; every consumer gates on that flag, so at
    // REST the scanner sample / precise are stale by contract and no path reads
    // them. A STOPPED SCANNER IS DEACTIVATED IMMEDIATELY: there is NO
    // non-playing window in which these value fields are valid, and the rule
    // has no exceptions. Natural end-of-playback takes the same deactivation
    // every manual stop takes — literally the same call since 2026-07-30: the tick
    // sees the atomic playing flag drop and calls stop_playback_if_playing, which
    // takes the quiescence fence, clears the flag and damages the waveform area, so
    // the line simply vanishes from wherever the predictor last drew it (a few
    // pixels short of the exclusive end bound — the accepted delta of not holding
    // the endpoint).
    // The launch seed (launch_playback_from, the shared body under Space's
    // toggle and the scrub launch) and the per-paint predictor advance are the
    // only writers of the value fields. There is no resting coincidence with the
    // cursor — a coincide-at-rest relationship would be wrong, not just unused
    // (a plain Space launches the scanner from the cursor, while the lower-half
    // scrub gesture AND Space's region launch both launch it independently of
    // the cursor, from a clicked frame and from the span's left bound). The cursor is
    // per-tab; the scanner is session-only and not persisted.
    // `playback_speed` is authoritative on the main thread and pushed
    // to the playback engine on every change.
    int64_t playhead_scanner_sample = 0;
    // Continuous (sub-frame) sibling of playhead_scanner_sample: the scanner's
    // DRAWN pixel is computed from this double (scanner_pixel_x) so a per-frame
    // viewport rescale during a strip-drag zoom slides the scanner smoothly
    // instead of stepping on integer frames (smoothness over accuracy —
    // precision is judged at standstill). Written only on the active path: the
    // playback pre-paint hook writes the predictor's continuous position here,
    // and the launch seed writes the integer value as a double.
    // The integer sample stays the domain / change-detection anchor (the
    // cur == sample short-circuit, the viewport-centering targets, the
    // timestamp readout). Meaningful only while active, like the integer sample.
    double  playhead_scanner_precise = 0.0;
    bool    playhead_scanner_active = false;
    float   playback_speed          = 0.7f;

    // (THE font_size FIELD IS GONE — architect approval 2026-08-01. It was the
    // GUI-wide monospace text size in points, and row 7 deleted the monospace
    // face it sized: gui_scale below is the product's ONE scale axis now. The
    // key left the .settings schema whole in the same arc, so nothing carries
    // the value any more — a sidecar still holding it is load-fatal as an
    // unknown key, by the architect's explicit no-legacy instruction.)

    // GUI rendering scale in PERCENT (the gui_scale setting; 100..200, default
    // 100). 100 is the design baseline — 1920x1080, the one supported
    // resolution — and 200 is the 4K case. A display preference: not engine
    // input, not authoring state, persisted on Ctrl+S, applied at file load, and
    // set through the settings editor (`:gui_scale=`, no hotkey). LIVE since
    // 2026-07-31: pushed to the renderer's file-scope state via
    // set_gui_scale_percent at all three application points (file load, the
    // settings-editor commit, the `'` load-in-place), and the editor commit
    // APPLIES it
    // live through GuiInputHandler::apply_gui_scale (the resize-path geometry
    // rebuild). SINCE ROW 7 IT IS THE ONE SCALE AXIS — every painted dimension
    // in the product rides it, the former font_size axis having died with the
    // monospace face.
    int     gui_scale               = 100;

    // GUI-kind launch preference: the external audio player the `l`
    // ("Listen to renders") command spawns with the rendered wavs. The
    // "audacious" default lives in exactly two places: this member initializer
    // (the pre-load state) and the first-open template's audio_player line
    // (settings_io.cpp) — the key is required in every `.settings`, so a load
    // always assigns this field from the file. A BLANK value
    // (`audio_player=`) is the deliberate opt-out, and `l` then reports
    // "no audio_player set" and does nothing. Persisted on Ctrl+S. The one
    // GUI-kind key with NO gesture: the settings editor
    // (`:audio_player=<path>`) is its sole authoring surface; consumed only
    // by the `l` launcher.
    std::string audio_player = "audacious";

    // GUI-kind preference: the repository that is the PROJECTS HOME — where
    // the architect's committed working checkpoints live, and the corpus the
    // GitHub recheck reads history out of. Free text, host/path form by
    // convention; the recheck normalizes it against the local clone's own
    // `origin` remote and refuses a mismatch, since the clone is only the
    // transport and a rebound setting must never silently read the wrong
    // history. The key is REQUIRED in every `.settings` (architect approval
    // 2026-08-04, retiring the one-day optional-key exception it landed under),
    // so a load always assigns this field from the file and the initializer here
    // is pre-load state exactly like audio_player's. That value lives in ONE
    // place, kDefaultProjectsRepo (settings_file.h), read by this initializer,
    // the SettingsFile member and the first-open template — unlike audio_player,
    // whose default is spelled twice, because the stamp a fresh project gets and
    // the value a session starts with are worth pinning to one constant.
    // Persisted on Ctrl+S. No gesture: the settings editor
    // (`:projects_repo=<host/path>`) is its sole authoring surface.
    std::string projects_repo = kDefaultProjectsRepo;

    // Render-environment attestation: the STORED per-library stat-identity
    // digests (16 lowercase hex digits each, env_fingerprint.h) the loaded
    // `.settings` recorded at its last save. Pre-load default is empty — never
    // written as empty: the four keys are required, so a load always assigns
    // them, and the first-open template stamps the four CURRENT hashes (a fresh
    // project starts matched, no prompt). Compared against compute_render_env_hashes()
    // once at source load; any mismatch opens the env-hash prompt, whose [o]k
    // — the sole response — stamps all four LIVE hashes to the current
    // environment's (no dismiss-without-ack path exists). The settings editor
    // (`:libm_hash=<16hex>` etc.) is the manual authoring surface. These are
    // history-less, no-dirty GUI-kind state: a restamp (prompt 'o' / editor
    // commit) mutates only these live fields and never marks the file dirty;
    // the new quartet persists on the next ordinary save (save_ops writes the
    // live values verbatim), and a save-less session simply re-modals the
    // mismatch on the next load by design (self-healing). Stored identity, not
    // recipe: the render fingerprint never reads these.
    std::string libm_hash;
    std::string libmvec_hash;
    std::string fftw3_hash;
    std::string fftw3_threads_hash;

    // Companion files discovered alongside the loaded audio.
    std::string warpmarkers_path;
    std::string settings_path;
    // Sibling `.phaseresetmarkers` path. Computed at file load. Empty when
    // no audio is loaded.
    std::string phaseresetmarkers_path;

    // Absolute or relative path of the currently loaded audio file. Used by
    // the render hotkeys (Ctrl+Alt+R and its shifted twin) and
    // the render pipeline to compute output paths. Empty when no file is
    // loaded (blank state).
    std::string source_audio_path;

    // Sidecar artifact identifiers use the sidecar's exact spelling
    // (warpmarkers, phaseresetmarkers, no underscores): files, path fields,
    // stores, and parse machinery. Musical concepts keep word separators
    // (phase_reset_dirty, phase_reset_frame_map).
    // Parsed warp markers for the currently loaded audio. Empty on load
    // failure or before the first audio load.
    GuiWarpMarkers  warpmarkers;

    // Parsed phase reset markers. Authored by the GUI and compiled by the
    // parser (derive_phase_reset_frame_map) into engine input on every render.
    GuiPhaseResetMarkers phaseresetmarkers;

    // Multi-selection set + focus. `last_selected_marker` is either -1 or
    // a member of `selected_markers`; keyed operations (Tab cycling, `j`)
    // anchor on it.
    //
    // THE SELECTION IS NEVER PARKED (architect 2026-07-29): this pair is the
    // ONE selection in the product — for the current tab and the current
    // `active_markers_view` — and no snapshot of it lives anywhere. It follows
    // the `t` audio-view switch (which translates domains, not columns) and is
    // CLEARED OUTRIGHT by the `p` column switch and by Ctrl+Tab, both of which
    // then re-acquire by coincidence (auto_select_marker_at_playhead,
    // input_pointer.cpp). The rule and its rationale are stated at ViewState.
    std::set<int> selected_markers;
    int           last_selected_marker = -1;

    // Shift-range-select anchor (file-manager style): the index the current
    // shift-range interaction ranges from, over the active column's store;
    // -1 = none. OWNED BY THE SELECTION LAYER ALONE — the mutators plus the
    // wholesale replaces listed below (architect 2026-07-29, which deleted the
    // platform shift falling-edge hook that used to own a "release half" of the
    // lifecycle): the anchor SURVIVES shift releases and
    // dies at the next membership replace or focus move — exactly the file
    // manager's anchor, which survives until the next plain click. It is
    // therefore no longer tied to the physical shift hold, and the platform
    // knows nothing about it.
    // THE AUTHORITATIVE CLEAR LIST (re-derived 2026-07-29 by grepping every
    // write, not inherited). Set ONLY inside
    // Selection::select_range_from_anchor, which is also the one mutator that
    // KEEPS it; every OTHER Selection mutator clears it —
    // set_single_selection, clear_selection,
    // collapse_to_focused, toggle_selection_membership and
    // sanitize_selection_after_restore (cycle_selection clears through
    // set_single_selection; load_source_file's explicit clear is belt over the
    // clear_selection it already runs).
    // THE WHOLESALE REPLACES — re-derived 2026-07-29 by grepping every
    // assignment of app.selected_markers outside those mutators, after the
    // parked-selection deletion took three of the five the list used to carry
    // (the `p` swap's slot restore, undo's inline W/P swap restore, and
    // Ctrl+Tab's slot restore) — are TWO, and both are covered: the propagate
    // paste's tail (phase_reset_propagate.cpp) assigns its created set one line
    // after switch_active_markers_view_to's clear_selection, whose same-mode
    // early return is unreachable from there (the paste is W-mode-gated), and
    // undo's touched-set restore (undo.cpp) is followed by
    // sanitize_selection_after_restore on exactly the same non-'S' gate. That
    // sanitize is also the orthogonal index-invalidation concern — a
    // store/selection mutation under a still-held shift — and it is what closes
    // Ctrl+Shift+Z, which arrives WITH shift held.
    // A marker REORDER is the one index event
    // that does NOT clear: remap_marker_indices_after_reorder carries the anchor
    // through the permutation with the selection and the focus (its declaration
    // owns that inventory), because a reorder moves rows without ending the
    // range interaction.
    // A cleared anchor does NOT silence the next shift-click:
    // select_range_from_anchor SEEDS the anchor by ADOPTING THE FOCUS when none
    // is live (architect 2026-07-23 — plain-click A then
    // shift+click B ranges A..B; only with nothing focused does the click anchor
    // on itself), and since every non-range mutator clears, that seed arm is the
    // ORDINARY first-shift-click path rather than a recovery one.
    // ACCEPTED DELTA of the hook's deletion: a shift-click after a release
    // ranges from the SURVIVING anchor instead of from the focus — shift-click A,
    // shift-click B (anchor A, focus B), release shift, re-press, shift-click C
    // now selects A..C where the hook made it B..C. That rare shape (release
    // shift mid-range-adjust, then extend again) moves TOWARD the file-manager
    // convention the rest of the model cites. The hook's own motivating sequence
    // is unchanged either way: shift-click 2, release, re-press, shift-click 7
    // gave 2..7 WITH the hook too (the hook cleared the anchor and the
    // adopt-the-focus seed re-derived 2 from the focus), and gives 2..7 now from
    // the surviving anchor 2.
    int           shift_range_anchor = -1;

    // STEMS ARE NO LONGER A SELECTION VISUAL AT ALL (row 5, architect). Every
    // ENABLED marker of the active column stems, always, in its class's
    // UNSELECTED colour (GuiPaintHandler::paint_marker_stems, off the marker
    // painter's stash); a disabled marker stems never. Selection's cue is its
    // flags' bright colour pair and nothing else. The successive apparatus this
    // replaces is worth naming once, because each layer was deleted for the same
    // reason the next one was: the conditional stem's hover/pin arms
    // (harvested 2026-07-25 for always-on-for-a-singleton), then the singleton
    // model's subject-change damage pair on Selection (deleted with row 5 —
    // stems no longer key on selection, so no selection mutation can move one).

    // Active markers view: 'W' = warp markers, 'P' = phase reset markers.
    // Toggled by `p`. Determines which marker collection is visible / edited /
    // hit-tested and which color set is used for the playhead and selected
    // indicators.
    char active_markers_view = 'W';

    // Active audio view: 'S' = source (the authored timeline), 'T' =
    // target (the engine's deformed-output timeline). Orthogonal to
    // active_markers_view ('W'/'P'): `t` toggles S/T, `p` toggles
    // W/P. While 'T', app.viewport_start_sample / playhead_cursor_sample /
    // zoom_level carry target-frame values; the live fields'
    // interpretation flips on toggle. Target view was
    // formerly read-only; the target-render audio subsystem makes target
    // view playable with live engine output.
    char active_audio_view = 'S';

    // Memoized target-view warp_frame_map (see warp_frame_map_view.h). Mutable: consulted and
    // refreshed from const hit-test paths.
    mutable TargetWarpFrameMapCache target_warp_frame_map_cache;

    // Memoized red-flag sets — the marker-store indices whose render resolves
    // to the 1.00 normalization fallback, painted in the marker lane's red class
    // whether or not they are selected (see warp_frame_map_view.h). Mutable:
    // refreshed from the const
    // flag-cache build. Keyed on the respective store generation, so the
    // classification runs once per change, not per tick, and freezes through a
    // marker drag (the store mutates only at commit).
    mutable WarpRedFlagCache warp_red_flag_cache;
    mutable PhaseResetRedFlagCache phase_reset_red_flag_cache;

    // The warp_frame_map the LAST COMMITTED frame's target-view item pixels
    // (flags; the live trim/selected-stem passes read it directly per frame)
    // were painted with — the geometry the
    // user is currently looking at, to commit granularity. This is the PROMOTED
    // half of a two-phase commit: the item-cache rebuild stages a value
    // (staged_displayed_*, below), and the paint pass promotes it here at the
    // frame that blits that cache surface (GuiPaintHandler::on_redraw), so the
    // hit map advances exactly when the on-screen items commit — not at the
    // offscreen rebuild. Lifecycle: WRITTEN by the paint-pass promotion (target
    // view) / cleared-value promotion (source view, mapless items); CLEARED
    // (with the staged value) at source load and `'` load-in-place (through
    // apply_settings_engine_and_prefs) and at a view toggle
    // (handle_active_audio_view_toggle). Shutdown is terminal — no teardown
    // clear. The item hit tests read it through displayed_or_live_target_map so
    // what you grab is what you see (event-synchronized hit geometry — the
    // ruling at that selector). Empty = cold (no target frame has committed its
    // items yet); the selector then falls back to the live display map. The
    // irreducible remaining seam is commit-to-scanout plus human reaction —
    // input is always a response to the previously presented frame — recorded
    // as accepted at the selector.
    std::vector<WarpFrameMapSegment> displayed_target_warp_frame_map;

    // Staging half of the two-phase commit above. The item-cache rebuild
    // (maybe_rebuild_flag_cache, the sole stage site since the trim-stem cache
    // retired and trim went live) writes the map it
    // baked here — or an empty clear in source view — and sets the valid flag;
    // GuiPaintHandler::on_redraw promotes it into displayed_target_warp_frame_map
    // once, at the next committed frame (which blits that cache), then clears
    // the flag (idle frames with no staged value do nothing). A COPY, not a
    // reference to wf_cache.fp_warp_frame_map, so a worker completion between
    // the rebuild and the paint cannot mutate what the committed items were
    // built against. Cleared alongside displayed_target_warp_frame_map at every
    // clear site (a stale staged map surviving a view flip would promote wrong
    // geometry at the next paint). staged_displayed_valid distinguishes a
    // staged CLEAR (empty map, source view) from no-stage.
    std::vector<WarpFrameMapSegment> staged_displayed_target_warp_frame_map;
    bool staged_displayed_valid = false;

    // (THE PROMOTION COUNTER IS GONE — displayed_map_gen, a monotonic long long
    // bumped once per top-of-frame promotion, deleted 2026-08-02. Its
    // subscribers were the hover cache's staleness key and the tick-side
    // stationary-cursor repair, and row 5 deleted both surfaces; it had been
    // WRITE-ONLY ever since — one bump, no reader — and a counter kept for a
    // reader that may never come is state the next reader has to re-verify
    // anyway. The promotion itself is unchanged and lives in one block in
    // on_redraw, which is where a future subscriber would hang its key.)

    // Displayed-VIEWPORT mirror — the SIBLING of displayed_target_warp_frame_map
    // for the viewport half of the same event-synchronized hit geometry. The
    // flag item pixels are painted from the flag cache's rebuild-time
    // fingerprint (wf_cache.fp_vp_start / fp_vp_end / fp_area_w), NOT the live
    // viewport; painted_column_of_source_frame reads the LIVE viewport
    // (app.viewport_start_sample). During an async plate-publish window (a
    // worker-dispatched viewport change — follow-scroll, center-on-playhead) the
    // live viewport already holds the NEW span while the flags still paint at the
    // OLD one, so an overlay centered by the LIVE viewport would jump off its
    // flag until the worker caught up. These fields hold the vp_start/vp_end/
    // area_w the LAST COMMITTED frame's flag cache was built against, promoted
    // in LOCKSTEP with displayed_target_warp_frame_map at the frame that blits
    // that cache, so the flag editor's box placement (see item_viewport_basis
    // in this header) and the LIVE TRIM pass (GuiPaintHandler::paint_trim — its bar/endcaps paint on
    // this basis so hit_test_trim_endcap / route_trim_bar_press land on the drawn
    // pixels) ride the
    // same basis the flags do. (The selected-stem DAMAGE was listed here until
    // 2026-07-30 and never belonged: that stem painted on the PLATE basis, so
    // its item-basis narrow damage was the wrong epoch. Both the damage and the
    // stem it served are gone — row 5's stems key on no selection at all.)
    // area_w == 0 means cold (nothing promoted
    // yet); the accessor then falls back to the live viewport, matching
    // displayed_or_live_target_map's cold live-map fallback. Written by the
    // paint-pass promotion; cleared alongside displayed_target_warp_frame_map at
    // every clear site (source load / `'` load-in-place / view toggle).
    // Deliberately
    // SEPARATE from GuiPaintHandler::plate_viewport_basis, which reads the
    // LIVE wf_cache.fp_* so the PLATE-REGISTERED paint overlays stay locked to
    // the just-blitted plate; WHICH overlays those are is enumerated at that
    // accessor's own declaration (paint_handler.h), the one authoritative site —
    // this comment states only its own side of the split.
    // The split PERSISTS as a
    // mechanism/lifecycle distinction (direct fp read for plate-registered
    // overlays vs this staged/promoted mirror for item-registered geometry), and
    // the two are NUMERICALLY EQUAL at every frame committed by the TWO PLATE
    // WRITERS (worker publish, synchronous rebuild): each
    // rebuilds the flag cache inline before the damaged frame can paint or
    // commit (the sync writer queues its invalidation first, but
    // invalidate_region only QUEUES and nothing paints re-entrantly inside the
    // synchronous call, so the rebuild/stage still completes ahead of the
    // frame; closure dates to the
    // worker publish joining the synchronous writer's inline shape), so those
    // frames commit new plate + new items together and the mirror promote at the
    // top of the committing paint agrees with the plate fp by construction. The
    // equality is NOT unconditional, though — the accepted RESIZE
    // ITEM-ONLY-PROMOTION window is the live exception: a resize changes the
    // top-strip dims, so maybe_rebuild_flag_cache fires from on_tick and stages
    // the OLD fp_vp span over the NEW effective width while the still-displayed
    // plate pairs that same span with its OLD fp_area_w until the in-flight
    // worker render publishes — so this promoted mirror's spp and the plate-fp
    // accessor's spp diverge for that window. Item-registered geometry
    // (paint_trim, the hit tests, the lane) rides THIS mirror through it —
    // paint == hit holds because both read the same owner — see the consumer-
    // side statement at GuiPaintHandler::paint_trim's basis comment. So the two
    // owners must NOT be collapsed on the strength of the plate-writer
    // equality; any future unification has to resolve the resize window first.
    int64_t displayed_vp_start = 0;
    int64_t displayed_vp_end   = 0;
    int     displayed_area_w   = 0;

    // Staging half of the displayed-viewport mirror above — the viewport twin of
    // staged_displayed_target_warp_frame_map, sharing its staged_displayed_valid
    // flag (one stage, one promote, one gen bump). The flag-cache rebuild writes
    // the wf_cache.fp_* viewport it baked here; on_redraw promotes it into
    // displayed_vp_* at the next committed frame. Cleared alongside the staged map.
    int64_t staged_displayed_vp_start = 0;
    int64_t staged_displayed_vp_end   = 0;
    int     staged_displayed_area_w   = 0;

    // Marker reposition drag state. Not reset across file loads — explicitly
    // cleared there and at every gesture end (release / lost button / the
    // force-end finalizer, all of which COMMIT).
    DragState     drag;

    // Region-select drag state (plain left-drag). Cleared on button release / lost
    // button, by the force-end finalizer, and on file load.
    RegionDragState region_drag;

    // Pending marker-reposition drag, armed by a plain flag press. The press
    // single-selects the marker immediately, whatever was selected before — groups
    // are never moved, so there is no deferral (the field's declaration carries the
    // story). The drag begins
    // only past the threshold. Cleared on the threshold crossing, on button
    // release / lost button, by the force-end finalizer, and on file load.
    PendingMarkerDrag pending_marker_drag;

    // Pending trim endcap/bridge drag, armed by a plain trim-bar press (the
    // trim-drag machinery begins only past the threshold). Cleared on the
    // threshold crossing, on button release / lost button, by the force-end
    // finalizer, and on file load.
    PendingTrimDrag pending_trim_drag;

    // The resting region-select span (session-only). BARE ESC CLEARS IT — the
    // one route that clears a span and nothing else, and one of the six bare-Esc
    // bindings — along with every playhead-moving and selection-changing route,
    // all of them through clear_region_highlight (input_handler.h), whose
    // declaration owns the authoritative clear-site inventory and the equally
    // authoritative list of what deliberately does NOT clear. The three clears
    // that stay IN PLACE rather than going through it are file load, the A/B tab
    // switch and the S/T audio-view switch, each pairing the reset with a domain
    // flip or a full repaint rather than that exact damage shape.
    RegionState region;

    // Live trim boundary drag (endcap / inter-endcap bridge). Cleared on button
    // release / lost button, by the force-end finalizer (both COMMIT its live
    // bounds), and on file load.
    TrimDragState trim_drag;

    // Ctrl-exact left-drag on the waveform (dual-axis zoom/pan navigation).
    // Cleared on button release and file load.
    StripDragState strip_drag;

    // Double-click candidate, shared by the trim-bar, flag, empty-lane and
    // editor-text surfaces (the surface tag prevents cross-firing). Seeded by a
    // motionless press-release (or, for Marker / EmptyLane, at the press);
    // cleared on file load and when the double-click action fires.
    DoubleClickCandidate double_click;

    // The trim-bar framing double-click's press record (see TrimBarPressSeed).
    // Written by every plain trim-bar press, consumed by the next left release.
    TrimBarPressSeed trim_bar_press;

    // Alt+drag on the waveform (continuous 1:1 grab-pan). Cleared on button
    // release / lost button, by the force-end finalizer, and file load.
    ScrollDragState scroll_drag;

    // Mouse drag-to-select inside the active text editor. Cleared on
    // button release, on a lost button mid-drag, and on file load.
    EditorTextDragState editor_text_drag;


    // THE REDESIGNED ROWS' BUTTONS — hit geometry PUBLISHED BY THE PAINTER, the
    // displayed-basis doctrine applied to proportional surfaces. Each button's
    // width is a HarfBuzz-shaped run's width (text_shape) plus its paddings,
    // which only the paint pass computes; the pointer code reads these stashes
    // and never re-shapes, so a clickable rect is exactly the painted one by
    // construction and the two cannot drift the way a re-derived measurement
    // would.
    //
    // ONE MECHANISM FOR EVERY REDESIGNED BUTTON (2026-07-31): row 1's single
    // pair of fields folded into this array when row 2 brought four more, so
    // there is one stash, one hover recompute and one hover clear rather than a
    // per-row copy of each. Every entry is zero-rect / not-hovered until its
    // row's first paint, which is the correct pre-display state: an empty rect
    // contains no point, so neither hover nor press can fire before the button
    // has been shown.
    //
    // `hovered` is written only on a TRANSITION (the motion tail's recompute and
    // the pointer-leave hook), a transition paying one invalidate_top_strip. A
    // press does not change it — the hover face survives a click; what a press
    // writes instead is `redesign_pressed` below, row 2's third face.
    //
    // `enabled` is the ENABLED VECTOR THE PAINTER LAST PAINTED, stashed beside
    // the rect for one reason: the facts it derives from (the undo/redo stacks,
    // the active tab's read-only flag, loading/blank) all change through routes
    // that damage nothing in the top strip, so the strip would keep showing a
    // stale face. main.cpp's per-tick comparator (beside the waveform
    // fingerprint dirty-detect) compares the LIVE vector against this stash and
    // pays one invalidate_top_strip on drift — one comparator site, no
    // per-mutation invalidate anywhere. It starts TRUE, so a cold roster settles
    // in one compare/paint pass.
    // `selected` is the second stashed bit and rides the SAME comparator for the
    // same reason: `f` and `i` flip their flags with no top-strip damage at all,
    // so a toggled face would otherwise stay wrong until something else
    // repainted. (`t` and `p` do damage — they take the full sync rebuild — but
    // they go through the one comparator anyway rather than being trusted.)
    struct RedesignButtonFace {
        GuiRect rect{0, 0, 0, 0};
        bool    hovered  = false;
        bool    enabled  = true;
        bool    selected = false;
    };
    std::array<RedesignButtonFace, kRedesignButtonCount> redesign_buttons{};

    // THE ACTIVE TAB'S LOCK, in its own reserved close-icon slot — published by
    // paint_tab_row and read by the press claim. THE SLOT IS PERMANENT since
    // 2026-08-01 (architect): every tab carries it in every state, closed and
    // bright when read-only, open and dimmed when writable, so this rect is
    // non-zero whenever the tab row has painted at all. "Visible" and
    // "clickable" are still one fact — they are now both simply always true,
    // and the click TOGGLES rather than only releasing.
    //
    // WITH ONE STATE'S EXCEPTION since 2026-08-05: while the `h` history view
    // stands the tabs are not tabs but the WALK SELECTOR, so no padlock is
    // drawn, no slot is reserved and this stays ZERO — the press path's lock
    // branch is then unreachable through the rect's own emptiness rather than
    // through a mode test, and "visible" and "clickable" stay one fact.
    //
    // It is still zeroed at the top of every paint run: a cold frame, a
    // degenerate lane or a row that never painted must not strand a target.
    //
    // ONLY THE ACTIVE TAB'S IS PUBLISHED, deliberately. Both tabs SHOW their
    // lock — a read-only B is worth seeing from A — but only the active one's
    // is a target, because the act it performs (bare `o`) is defined on the
    // ACTIVE tab: `o` toggles active_view_state(app).read_only and nothing
    // else. Clicking the INACTIVE tab stays Ctrl+Tab whole, lock included;
    // switch first, then unlock, which is also the order the keyboard makes you
    // take.
    GuiRect tab_lock_rect{0, 0, 0, 0};

    // THE MARKER PAINTER'S STASH — the second surface on the painter-publishes
    // contract the roster above established, and for a harder reason than the
    // roster had. A row-5 marker flag's WIDTH is derived from its shaped label,
    // so no consumer can re-derive the box without repeating a HarfBuzz pass;
    // the pixels' own painter is the only honest owner of the geometry. Both
    // vectors are written by ONE producer PER FRAME — the flag-cache rebuild,
    // whose THREE mutually exclusive lane painters (re-derived by grep
    // 2026-08-05: render_flags, render_phase_reset_flags and, while the `h`
    // history mode stands, render_history_diff_flags, one call site each) each
    // clear both stashes first — against the DISPLAYED basis those pixels were
    // painted with, so a click during an async publish window tests the flag it
    // can see rather than the one the live viewport would put there.
    //
    // `flag_hit_rects` is in PAINT order (store order), so hit_test_flag walks
    // it BACKWARDS: last painted = topmost = what a click grabs. `marker_stems`
    // carries one entry per DRAWN stem, which on the two LIVE columns means one
    // per ENABLED marker — a disabled marker has no stem ever, expressed as an
    // absent entry (MarkerStem, render.h) — and in the history mode means one
    // per diff flag, that lane's classes all stemming. Consumers read the stash
    // rather than re-deciding, so "drawn" and "grabbable" stay one fact in both.
    //
    // THE INDEX DOMAIN FOLLOWS THE PAINTER: `marker_index` is a store index on
    // the live columns and an index into history_mode.flags in the mode. The
    // mode EDGES are what that costs — drop_lane_stash_across_history_edge
    // (input_key_dispatch.cpp) carries the argument and empties both.
    //
    // Cold (before the first rebuild) both are empty, so nothing is clickable
    // and no stem paints — the same "visible iff hit-testable" property the
    // redesigned rows' stash has, and the honest one: a flag that has never
    // painted has no box to click.
    std::vector<FlagHitRect> flag_hit_rects;
    std::vector<MarkerStem>  marker_stems;

    // THE OPEN FLAG EDITOR'S BOX, published by the same painter-owns-derived-
    // geometry rule the two stashes above follow, and for the same reason: the
    // unrolled box's width is its SHAPED text's width and its per-byte caret
    // stops are that run's own accumulated pen, neither of which any consumer
    // can re-derive without repeating a HarfBuzz pass. Written every frame by
    // render_flag_editor_box (which zeroes it when no FlagPayload editor is
    // open), read by the pointer path for the in-box test, the click-to-caret
    // mapping and the drag-select. See FlagEditorBox (render.h) for the field
    // contract, including why `byte_x` is what click-to-byte searches.
    FlagEditorBox flag_editor_box;

    // THE OPEN BOTTOM-STRIP EDITOR'S TEXT GEOMETRY — the same painter-publishes-
    // shaped-geometry contract one row down, for the settings / load /
    // BPM editors after row 7 took them off the monospace grid (2026-08-01).
    // There is no BOX to publish: those editors have no chip around them any
    // more (the press region is the whole bottom strip, as it has always been),
    // so this carries only what click-to-byte needs.
    //
    // `text_origin_x` is the window x of PENDING's byte 0 — the prefix's own
    // shaped width is already spent in it — and `byte_x` holds pending.size()+1
    // pen offsets RELATIVE to that origin, so the pair reads exactly like
    // FlagEditorBox's and editor_byte_index_at searches either the same way.
    // Written by paint_bottom_strip: zeroed at the top of every run, filled by
    // whichever editor branch actually paints. That makes it a statement about
    // what is ON SCREEN — an editor the row's precedence hides (a prompt is up)
    // publishes nothing and takes no clicks, which is the correct answer.
    struct BottomEditorText {
        bool                valid         = false;
        double              text_origin_x = 0.0;
        std::vector<double> byte_x;
    };
    BottomEditorText bottom_editor_text;

    // THE PRESSED BUTTON — the CLICK FACE, and the only piece of press-state
    // machinery the redesigned rows have. A roster index while a left button is
    // physically held down on an ENABLED button that HAS the face AND the
    // pointer's claim on that hold still stands, -1 otherwise.
    // Written by exactly two routes, each damaging the strip on the transition:
    // the press claim sets it (input_pointer.cpp) and clear_redesign_button_press
    // clears it (the left release and the pointer-leave / capability-loss hook).
    // The face rides the PHYSICAL hold, not the action — the chord already fired
    // at the press — so it is visual only and survives the pointer wandering off
    // the button mid-hold. THE ONE PLACE THE INDEX AND THE HOLD PART COMPANY is
    // the leave: the pointer going out of the window drops the face while the
    // button may still be physically down, because a face is a statement about
    // where the pointer IS. After an ordinary leave the release still arrives
    // normally and simply finds nothing to clear. WHICH buttons have it is the
    // chord table's
    // `click_face` column (rows 2 and 4 do; rows 1 and 3 keep two faces), not a
    // fact restated here.
    int redesign_pressed = -1;

    // THE HOVER TOOLTIP'S TIMING STATE — the whole of it. `hover_ms` is the
    // CLOCK_MONOTONIC stamp of the moment a tooltip-bearing button became
    // hovered (0 = none is), written by the hover recompute; `visible` is what
    // the painter draws, flipped by the run loop's existing tick when the delay
    // comes due. No timer, no callback, no per-frame damage: the tick already
    // runs, it compares two numbers, and it damages ONCE on each edge.
    // `rect` is the painter's published tooltip box, needed only for damage
    // (nothing hit-tests a tooltip).
    // `owner` is the roster index the dwell belongs to (-1 = none). It is what
    // makes "a fresh dwell on each arrival" TRUE rather than merely intended: a
    // single motion can leave one tooltip-bearing button and enter the other in
    // the same recompute, and without the id the stamp would survive that change
    // — the second button would inherit however much of the first's dwell had
    // already elapsed, showing instantly if the first tooltip was already up.
    struct RedesignTooltip {
        int64_t hover_ms = 0;
        int     owner    = -1;
        bool    visible  = false;
        GuiRect rect{0, 0, 0, 0};
    };
    RedesignTooltip redesign_tooltip;

    // THE MENU ROW'S DROPDOWN — ONE popup state for BOTH menus (Settings since
    // 2026-07-31, Navigation since 2026-08-02), hanging under whichever button
    // emits it. `menu` is the whole modality AND the whole "never two at once"
    // rule: while it is not None the popup owns the keyboard (on_key's popup
    // gate), the pointer (the press claim's popup-first block) and the wheel,
    // the roster stops hovering, and opening the other menu is simply writing
    // this field — one value, one menu, no invariant to keep.
    // `hovered_item` is -1 or an index into the open menu's item table, written
    // by the hover recompute (from motion AND from the run loop's settled tail,
    // the two callers named at recompute_dropdown_hover — the rects it hit-tests
    // are published by the painter, so they move with no pointer event under
    // them), by every close (the struct reset) and by the
    // POINTER-LEAVE drop — that last one because the painter lights the item it
    // names with no pointer_in_window term of its own, so the item would stay
    // lit for the whole time the pointer is outside (until a re-entry's
    // synthesized motion recomputes it — or forever, on capability loss, which
    // has no return to wait for).
    // `rect` and `item_rects` are PAINTER-PUBLISHED, so the hit tests read
    // exactly the painted boxes and never re-shape a label (the displayed-basis
    // doctrine).
    // Every rect is zero while closed, which is the correct cold answer: an
    // empty rect contains no point.
    // `pressed_item` is the ARMED item: set by a press on one, cleared by the
    // release, by every close and by the same pointer-leave drop. It exists
    // because the dropdown is the ONE redesign surface that acts on RELEASE —
    // every row button fires on press, a menu triggers on release by universal
    // convention — which is also the only reason a pressed face is visible long
    // enough to be worth painting.
    // THE ARM FOLLOWS THE POINTER while the press is live (architect
    // 2026-08-03): sliding from one item to the next moves it, and sliding onto
    // the separator, the chrome or off the box sets it to -1 with the press
    // still live — which is also how a press that began on the ANCHOR arms its
    // first item without ever having pressed one (`press_began_on_item` below).
    // That is what keeps EXACTLY ONE item distinguished at a time —
    // an arm that stayed where it went down lit the pressed face there while
    // `hovered_item` lit the hover face under the pointer, two lit items in a
    // menu that shows one. The rule lives at recompute_dropdown_hover
    // (input_pointer.cpp). THE RELEASE READS THAT SAME DEFINITION FOR ITSELF, at
    // the coordinates the release carries (finish_dropdown_release), rather than
    // this field: the rects the walk hit-tests are published by the PAINTER, and
    // a paint can land between the last walk and the release inside one dispatch
    // batch, so the act is derived where the face is remembered.
    // `press_began_on_item` is WHETHER THE HELD BUTTON BELONGS TO THIS POPUP'S
    // GESTURE — where it went down, NOT whether it is still down: the platform's
    // own tracking answers that (GuiInputState::primary_button_held), and the
    // arm cannot answer either question now that it moves — it reads -1 both
    // before any press and while a live press stands over a separator.
    // TWO PRODUCERS, ONE MEANING: the popup's own ITEM press, and (architect
    // 2026-08-03) the ANCHOR press that OPENS a menu — press Settings or
    // Navigation, hold, drag down into the menu that came up, and releasing on
    // an item fires it, the desktop menu bar's one continuous gesture. The name
    // predates the second producer and is kept: both are the same fact, "the
    // button now down was pressed on this popup — either on one of its items or
    // on the anchor that brought it up". An anchor press that CLOSES its menu
    // claims nothing (nothing is left to belong to), and the claim's value is
    // read back from the toggle's outcome rather than predicted, at the press
    // site rather than in toggle_dropdown — whose other callers, the menu-row
    // hover open and the hover switch, carry no held button at all.
    // Cleared by the release, by the pointer-leave drop and by every close (the
    // struct reset — which is why a mid-hold hover switch onto the other anchor
    // drops claim and arm together).
    // `menu_row_armed` is the MENU ROW'S MODE, and it is the one field here that
    // means something while the popup is CLOSED: once a menu has been opened
    // from the row, the row answers the pointer alone — entering either anchor's
    // rect opens that anchor's menu with no click (on_motion's no-gesture tail,
    // open_menu_row_anchor_on_hover), which is what every desktop's menu bar
    // does; the "pointer left the row" half is a separate entry with the
    // opposite guard list (update_menu_row_exit, at the top of on_motion). COLD,
    // an anchor answers a CLICK and nothing else, and that is the whole reason
    // the bit exists: a row that sprang a menu open at a pointer merely crossing
    // it, with no click ever given, would be a misfeature rather than this one.
    // ARMED BY toggle_dropdown's OPEN path, the single route that opens either
    // menu (the click and the armed hover both go through it, and no keyboard
    // chord opens a dropdown at all), so "a menu is open" implies "the row is
    // armed" by construction.
    // WHAT ENDS THE MODE — the authoritative list, and it is deliberately short
    // because each entry is a blanket rule rather than a route:
    //   (1) close_dropdown, which clears the bit ABOVE its own "nothing is open"
    //       return: bare Esc and Ctrl+Q through the popup's keyboard gate, an
    //       item activating, the anchor click that closes its own menu, a press
    //       anywhere else, the wheel, a resize, and the WM close. A dismissal
    //       must end the mode WHETHER OR NOT a menu happens to be open — menu
    //       closed and row armed is the state this feature exists for — or Esc
    //       would put away a menu that the next pointer twitch reopens;
    //   (2) ANY pointer press (on_button_press's top) and ANY key press
    //       (on_key's top), both through disarm_menu_row. Neither needs an
    //       exception list: the one press that must keep the mode is the anchor
    //       press, which re-arms through the open path a few lines later, and no
    //       chord opens a dropdown at all;
    //   (3) the pointer LEAVING ROW 1's band with no menu up (on_motion's exit
    //       half, resolved above every branch so a modal cannot hide it), which
    //       keeps the mode from outliving the visit — wander down to the
    //       waveform and Settings needs a click again;
    //   (4) the pointer leaving the WINDOW OUTSIDE ROW 1'S BAND (the platform's
    //       pointer-leave hook, which asks point_in_menu_row_band of the
    //       REMEMBERED position and calls the disarm only on a no). Row 1 abuts
    //       the titlebar, so sliding one pixel UP off the row is the commonest
    //       way to leave the window from it, and that is a step onto the
    //       titlebar rather than out of the visit: the mode survives it and the
    //       hovered row-1 button keeps its face, exactly as an OPEN menu already
    //       survives the same edge (architect 2026-08-08). A leave anywhere
    //       else — below the row, or with the mode not armed — disarms, AND SO
    //       DOES EVERY POINTER-CAPABILITY LOSS whatever the position said: that
    //       edge is the hard end of the stream, with no return motion to make
    //       sense of a kept mode, so the hook reads the platform's leave REASON
    //       ahead of the band (GuiPointerLeaveReason, platform_wayland.h).
    // Entries (2)-(4) share one gated writer, disarm_menu_row, which is inert
    // while a menu is open — there the popup's own routes decide, and (1) is
    // what they call. The ONE close that KEEPS the mode is the row-1 hover
    // close, a step ACROSS the bar rather than a dismissal, and it re-arms
    // explicitly at its own site.
    // IT LIVES IN THIS STRUCT because the popup and its mode are one surface's
    // state, and because the reset then carries the bit for free on the ordinary
    // path; the unconditional clear above the early return is what makes the
    // rule hold in the closed-and-armed state the reset never reaches.
    struct Dropdown {
        DropdownMenu menu                = DropdownMenu::None;
        int          hovered_item        = -1;
        int          pressed_item        = -1;
        bool         press_began_on_item = false;
        bool         menu_row_armed      = false;
        GuiRect      rect{0, 0, 0, 0};
        std::array<GuiRect, kDropdownMaxItemCount> item_rects{};

        bool open() const { return menu != DropdownMenu::None; }
    };
    Dropdown dropdown;

    // THE GITHUB RECHECK'S HISTORY VIEW — a READ-ONLY MODE over the committed
    // sidecar history of the loaded source (the model, and what "committed
    // history" means here, are at GuiHistoryDiff, history_diff.h).
    //
    // WHAT IT SHOWS. While it stands the marker lane paints NO live marker.
    // In their place it paints ONE COMMIT'S DELTA against the session: a GREEN
    // flag per line the session has and that commit did not, a RED flag per line
    // that commit had and the session dropped, and ONE DOUBLE-WIDTH flag per
    // same-frame pair (red half then, green half now). The flags sit at their
    // authored frames through the live lane's own column mapping, so a removed
    // marker stands exactly where it stood. The bottom strip's modal span
    // carries the commit's position, its short SHA and its `scale=` value.
    //
    // THE WALK IS LOAD-GATED (architect 2026-08-04): membership is the
    // load-in-place gate itself — each candidate commit's three sidecars must
    // pass the strict whole-set load (load_commit_sidecars_strict,
    // history_diff.h, the `'` act's own validation, one predicate) —
    // so every checkpoint the mode can step to is one `'` can load.
    // Ineligible commits leave the walk, one stderr line counting
    // them, and the corner's n/N counts the ELIGIBLE list. The diff model's
    // old leniencies (unparseable lines dropped, a missing file read as an
    // empty side, the per-commit Ambiguous display) died with the gate.
    //
    // AND THE WALK IS UNCAPPED AND PREFETCHED (architect 2026-08-07): that gate
    // is what made `h` stall, so the whole git half runs at STARTUP on a
    // background worker (GuiHistoryPrefetch, history_prefetch.h) with no depth
    // limit, STREAMING each eligible commit to the main thread as it passes.
    // The visit BINDS to that store instead of building a list, which has three
    // visible consequences: entry is instant; a view opened while the scan is
    // still running shows an empty or partial walk and FILLS IN under the user,
    // `n/N` growing and the `,` wall moving outward; and the store is re-warmed
    // after a checkpoint publishes and whenever an entry finds it stale. A kick
    // that would land while this view stands is DEFERRED to the exit — the
    // visit's list must not be swapped underneath it.
    //
    // WHAT OPENS IT: bare `h`, and nothing else — AND NOT WHILE A CHECKPOINT IS
    // PUBLISHING (2026-08-07): the act runs on a worker now, and a walk measured
    // against a repository that worker is mid-mutation on would be a lie, so the
    // open is a consumed no-op with one stderr line while
    // history_checkpoint_in_flight stands. The key reaches the toggle only
    // from on_key's main body, so every gate above that point is an entry
    // refusal for free — a prompt, any of the five editors, an open dropdown,
    // loading or absent audio, and any live pointer gesture (the authoritative
    // ordering is at the gate itself, handle_history_mode_key in
    // input_key_dispatch.cpp). An UNAVAILABLE session refuses too: init() states
    // its own reason on stderr and the mode simply does not open.
    //
    // WHAT CLOSES IT, the whole list: bare `h` again; ANY LOAD-IN-PLACE — the
    // `'` editor's three acts, its render-entry load, its
    // load-in-place-from-a-commit (2026-08-04) and its LOCAL-tab load of a
    // timeline state (2026-08-08), each of which rewrites the very state the
    // frozen now side was measured against (the renders-side one cannot actually
    // run from in here, and the closer inventory at close_history_mode,
    // input_key_dispatch.cpp, says why it still calls the closer);
    // THE COMMIT ACT, WHEN ITS SAVE LANDS (architect 2026-08-07, superseding the
    // checkpoint-in-the-repository partition of 2026-08-05, which in turn
    // superseded the act's in-place re-entry: the checkpoint's own verdict now
    // arrives seconds later on a worker, so the last thing the closing thread
    // knows is whether the save succeeded — the partition and its principle are
    // at run_history_commit, input_key_dispatch.cpp); Ctrl+Q and the WM close, trivially, the process
    // going with it. ESC IS NOT
    // ON THAT LIST AND CANNOT BE: the toggle is handle_history_mode_key's, and
    // Escape is not in that function's vocabulary at all (the membership is
    // re-derived at history_mode_owns_key), so no Esc reaches it. A RESIZE
    // does NOT close it: the delta is re-laid-out against the new geometry by
    // the flag cache's own rebuild, exactly as live flags are.
    //
    // BARE ESC IS ADMITTED (architect 2026-08-04) AND ADDS NO SEVENTH ESC PLACE.
    // The bare-Esc inventory is still the six enumerated at its dispatch point
    // (input_handler.cpp); the mode's allowlist merely stops dropping the key,
    // so the two bindings that can be live in here run — the REGION CLEAR (a
    // span formed before `h`, or one formed INSIDE the view by its own placement
    // press and drag) and the RENDER / BATCH CANCEL (a render launched
    // before `h`).
    // Neither touches authored state, which is why admitting it costs the frozen
    // now side nothing. With neither standing, Esc is a consumed nothing.
    //
    // WHAT IT REFUSES, and where: every state-mutating route is a consumed no-op
    // while it stands, through TWO gates and no scattered ifs — history_mode_-
    // key_blocked (the keyboard allowlist, which the redesigned buttons and the
    // Navigation menu's items pass through too, since both dispatch as chords
    // via on_key) and handle_history_mode_press (the pointer allowlist). Each
    // states its own admitted set at its definition. THE SETTINGS DROPDOWN is
    // shut out structurally instead, at toggle_dropdown: its six items all open
    // the settings editor, a modal this view has no place for, and refusing the
    // menu is one line where covering that one pointer bypass per item would be
    // several.
    //
    // THE NAVIGATION DROPDOWN OPENS IN HERE (architect 2026-08-08), so a popup
    // and this mode DO stand together and the old "never together" invariant is
    // retired to the Settings half. It costs the gates nothing: every one of its
    // seven rows is a CHORD dispatched through on_key, so the two gates above
    // answer per item exactly as they do for a redesigned button — zoom in / out
    // / overview are admitted by the allowlist, and center-on-focus and the two
    // marker steps are claimed by history_mode_owns_key as re-expressions over
    // the diff flags. ONE row greys instead of dispatching, the menus' first and
    // only per-item disabled state: "Walk both tabs", whose Ctrl+Shift+Tab is
    // this mode's reverse walk-tab cycle rather than the A/B walk the label
    // promises (dropdown_item_enabled, below).
    //
    // AND THE ROSTER WEARS THOSE REFUSALS (architect 2026-08-04): every
    // redesigned button whose act this mode consumes takes its row's DISABLED
    // face and ignores the pointer, and the ones that still work stay lit. The
    // membership below is RE-DERIVED from the code (the retell rule: walk the
    // chord table against the two predicates rather than edit an inherited
    // list), 2026-08-08.
    //   DEAD — Undo (Ctrl+Z), Redo (Ctrl+Shift+Z), RENDER (Ctrl+Alt+R, which
    //   left the allowlist with its shifted twin on 2026-08-08 when the
    //   checkpoint act moved onto Ctrl+S), copy (Ctrl+P), paste (Ctrl+Alt+P),
    //   the bpm opener (`m`), iteration (`i`), follow (`f`), listen (`l`), and
    //   the SETTINGS anchor — the one anchor left in this column since
    //   2026-08-08, and the one entry here that is not a chord's refusal but the
    //   toggle_dropdown lockout's.
    //   LIT — Quit (Ctrl+Q), the view bar's three (bare 1/2/3), the
    //   COMMIT-FACED SAVE (Ctrl+S, the act itself), the S/T + W/P radios (bare
    //   `t` / `p`), BOTH row-3 tabs and the history button (Ctrl+Tab and
    //   bare `h`, the mode's OWN vocabulary, which the derivation asks about
    //   first), the walk's two arrows (bare `,` / `.`, the same), and the
    //   NAVIGATION anchor since 2026-08-08 — the menu it opens works in here,
    //   and its one dead row greys at the ITEM (dropdown_item_enabled) rather
    //   than through this partition, which knows only about buttons.
    //   THREE OF THE LIT ARE SESSION-CONDITIONAL, each one decision serving the
    //   key and the face: Save greys with an empty head delta (or a checkpoint
    //   in flight), Revert greys with no diff flag selected, and the
    //   load-in-place opener greys only on an UNBOUND local walk (2026-08-08,
    //   when the Local walk got the act — the term is the blank-lane state a
    //   live tab cannot reach, so in practice that button is lit on both tabs
    //   in either reading).
    // The partition is
    // DERIVED
    // from the two gates above (plus the Settings anchor's toggle_dropdown
    // lockout, the one hand entry left) and
    // inventoried in one place — history_mode_disables_button, input_pointer.cpp
    // — and it is read live from `active` below, so leaving the mode restores
    // every face on the next frame with nothing latched.
    //
    // ROW 3'S TABS ARE THE EXCEPTION, and a repurposing rather than a refusal
    // (architect 2026-08-05): their chord stays consumed like every other A/B
    // tab switch, and the SURFACE becomes the WALK SELECTOR — two slots,
    // "Remote" and "Local", one per GuiHistoryWalkSource. See `source` below.
    // (The row carried the walk-and-reading PRODUCT as four slots for one day,
    // 2026-08-07..08; the READING is row 4's own Cumulative toggle now, over a
    // session bit that is not in this struct at all — AppState::history_-
    // cumulative, which is why nothing here names it.)
    //
    // AND THERE ARE TWO WALKS TO SELECT BETWEEN SINCE 2026-08-07 (architect,
    // "the local history feature will be helpful for understanding undo/redo
    // history"): the COMMITTED history this mode was built on, and THE SESSION'S
    // OWN UNDO/REDO TIMELINE read through the identical delta machinery — every
    // state Ctrl+Z and Ctrl+Shift+Z can reach plus the live one, newest first
    // since 2026-08-08 (GuiHistoryLocalWalk, history_diff.h, owns the model and
    // the pairing derivation). The lane, the flags, the colours, the corner's `n/N`, the
    // walk's `,` / `.`, the diff-flag cycle, the trim bar's span and its framing
    // double-click are all SOURCE-AGNOSTIC — they read the displayed delta and
    // the active walk's position, never a named walk. THREE surfaces are not,
    // and each says why at its own site: the corner's SHA token (an undo entry
    // has no name), the `'` LOAD-IN-PLACE, which is LIVE ON BOTH WALKS since
    // 2026-08-08 but FORKS ON THE SOURCE — the editor asks for a commit spelling
    // on the Remote tab and a member NUMBER on the Local one, and the act behind
    // it is a different function per walk (the mode's two, at the opener and at
    // load_editor_commit) — and SAVE-AND-COMMIT, whose reach
    // and grey stay the commit walk's because the act publishes into the
    // repository. THE REVERT ACT IS LIVE ON LOCAL FLAGS and deliberately so: it
    // reads the painted flags' frames and then-side lines and knows nothing
    // about where they came from, so selecting part of one undo event and
    // putting just that part back is the feature working.
    //
    // ENTRY IS STILL GATED ON THE COMMIT WALK ALONE — the local walk RIDES the
    // mode, it does not carry it — so a piece with no committed history cannot
    // be opened to read its undo stack.
    //
    // THE FIRST ADMITTED MUTATOR IS BARE `'` (architect 2026-08-04) — the mode's
    // own act, not an exception carved out of the allowlist's reasoning (the
    // second is Ctrl+S, further down, on the same reasoning). In the
    // mode that editor's subject CHANGES, AND IT CHANGES WITH THE WALK
    // (2026-08-08, when the architect gave the Local walk the act his 2026-08-07
    // ruling had it consume). ON THE REMOTE TAB it opens prefilled with the
    // viewed commit's full SHA, takes any spelling git can resolve in its place,
    // and on Enter loads THAT COMMIT's three sidecars into the live session in
    // place, 1:1
    // (GuiInputHandler::load_history_commit_in_place — parse-gated by the strict
    // whole-file loaders, so an unresolvable commit, a missing sidecar or a
    // legacy format is a red flash and one stderr line with nothing touched).
    // ON THE LOCAL TABS it opens prefilled with the viewed member's displayed
    // NUMBER, takes any member number in place of it, and loads THAT STATE of
    // this session's timeline — the two marker columns and the engine block, all
    // an undo entry carries (GuiInputHandler::load_history_local_entry_in_place;
    // a non-number or an out-of-range one is the same red flash and one stderr
    // line). EITHER WAY it is ONE cross-file undo entry ON TOP of the current
    // state rather than a rollback — so Ctrl+Z afterwards returns to the state
    // from just before the load — and no disk write anywhere. The mode closes as
    // part of a successful load-in-place, so the frozen now side never outlives
    // the state it
    // was measured against. THE EDITOR-OPEN SUB-STATE is the mode standing with
    // that editor up: the mode's two gates stop being reached — the
    // keyboard-modal editor gate sits above them in on_key, and any pointer
    // press outside the editor's own text-drag reach is the editor's to swallow
    // — so `h`, `,` and `.` TYPE into the buffer rather than stepping the walk,
    // and the mode's bottom-strip line yields its cell to the editor for the
    // life of the edit.
    //
    // THE OTHER ADMITTED MUTATOR IS Ctrl+S, AND IT WRITES OUTSIDE THIS
    // SESSION (architect 2026-08-04, REHOMED FROM Ctrl+Alt+R 2026-08-08): while
    // the mode stands that chord is not the plain disk save but THE
    // SAVE-AND-COMMIT ACT — the mode bit selecting the command exactly as the
    // iteration bit selects the sweep, one route with the selection inside it
    // (on_key's `s` arm). It belongs on THIS chord because the act runs the
    // ordinary save as its first step, so the Save button is the surface that
    // tells the truth about it; Render kept its own chord and greys in the view
    // with the rest of the consumed roster, and THE PLAIN DISK SAVE HAS NO
    // HOTKEY IN HERE at all (a settings-only drift — the one thing the head
    // delta calls "nothing to checkpoint" — is saved by leaving the view first,
    // architect-accepted 2026-08-08). IT ASKS FOR THE COMMIT
    // MESSAGE FIRST, through the COMMIT-TITLE EDITOR (architect 2026-08-07,
    // replacing the confirmation prompt that used to guard it and superseding
    // "the message is derived, not chosen"): a fourth bottom-strip modal,
    // prefilled with `Update <id>`, where a bare Enter is the old `y` and typing
    // over the prefill names the checkpoint. On Enter the act runs THE ORDINARY
    // SAVE beside the
    // source through its one owner (GuiSaveOps::save — the same act Ctrl+S is,
    // dirty cleared with it) and only then writes the live authoring state as
    // the three sidecars into the piece's directory in the projects repository,
    // commits them pathspec-scoped under the entered title and pushes
    // (commit_history_checkpoint, history_diff.h — the product's ONE mutating
    // git route, and its only writer outside the user's own save). A FAILED SAVE
    // REFUSES THE WHOLE ACT before any of that, one stderr line and nothing
    // committed; run_history_commit (input_key_dispatch.cpp) owns the order, the
    // refusal and the coincident-path reasoning. NEITHER RENDER CHORD is
    // admitted, so both stay consumed nothings here and the Render button wears
    // its ordinary face over the mode's disabled one; the SAVE button wears the
    // commit icon and the label "Save and Commit" while the mode stands, and
    // reaches the act through its own chord. THE
    // ADMISSION IS CONDITIONAL since 2026-08-05: with nothing to checkpoint the
    // chord is a consumed no-op and that button greys (head_delta_empty, below,
    // owns the bit and the one decision both readers take it from), and since
    // 2026-08-07 the same is true while a checkpoint is already publishing
    // (history_checkpoint_in_flight, which lives on AppState rather than here
    // because the act outlives the view).
    //
    // EVERYTHING PAST THE SAVE RUNS ON A WORKER (architect 2026-08-07): the git
    // steps are a network act, and freezing the window for them was the one
    // place this product made the user wait on a remote. The act captures what
    // it needs by value, closes the view and hands the job to
    // GuiHistoryCommitWorker; its three failing verdicts come back to THE
    // BOTTOM ROW'S CRITICAL SLOT (architect 2026-08-09, replacing the
    // acknowledge modal they raised until then: a critical failure must be
    // impossible to miss and impossible to hijack the keyboard with, so the
    // report is permanent and paint-only — critical_error_message, below), and
    // its two clean ones say what they have to say on stderr and CLEAR the slot.
    //
    // THE ACT CLOSES THE VIEW WHEN ITS SAVE LANDS (architect 2026-08-07,
    // superseding the checkpoint-in-the-repository partition of 2026-08-05,
    // which superseded his 2026-08-04 "the mode stays open"): the view exists to
    // ask what differs from a checkpoint, and an act that has just published one
    // has finished the question — while holding the view open for a verdict that
    // arrives seconds later on a worker would be a modal wait dressed as a
    // review. It is still a PARTITION, not a blanket close: a FAILED SAVE leaves
    // the view exactly as it was, like every other refusal in the product, and
    // run_history_commit (input_key_dispatch.cpp) owns it.
    //
    // WHY THE FROZEN NOW SIDE CANNOT GO STALE — as a statement about the
    // AUTHORED state, which is what the flags describe. GuiHistoryDiff captures
    // the three sidecar texts once at init() and measures every commit against
    // them. That would be a real hazard in an authoring session — the answer
    // would drift from what is on screen — and it is not one here BY
    // CONSTRUCTION: the two gates above refuse every route that could change the
    // markers or the engine settings for the whole life of the session EXCEPT
    // the mode's own three MUTATORS — `'` (the load-in-place), Ctrl+S (the
    // commit act, on Ctrl+Alt+R until 2026-08-08) and Ctrl+H (the revert act;
    // membership re-derived 2026-08-06)
    // — and every one of those closes the view as it ends, so no session
    // outlives a write to its own now side. The
    // mode's entry re-inits, so each visit measures against the state at that
    // visit.
    //
    // THE SAME DERIVATION FREEZES BOTH UNDO STACKS, which is what the LOCAL walk
    // rests on (2026-08-07; both stacks since 2026-08-08, when the walk grew to
    // the whole undo/redo timeline): every route that could push, pop or evict an
    // entry on either stack is an authoring route, so the two gates consume it or
    // one of the three mutators closes the view as part of itself. The walk
    // therefore captures both sizes once and indexes them for the visit —
    // re-reading them on every ask as a BOUNDS PRECONDITION on the subscript it
    // is about to perform, which is all that check is.
    //
    // IT IS EXCEPTIONLESS BY CONSTRUCTION: the premise shipped with one admitted
    // producer — the S->T view switch's iteration-bracket push — and that push is
    // DELETED with the ruling that iteration mode is target-legal, so entering
    // target view changes no store (the record is at
    // handle_active_audio_view_toggle, input_handler.cpp). The bit itself cannot
    // move in here either: `i` is not on the keyboard allowlist and the icon
    // row's iteration button greys with it. So the walk has NO producer to
    // tolerate, and the derivation is the whole argument — no runtime check
    // stands behind it. (A per-entry PUSH SERIAL, the walk verifying each
    // captured position's identity on every read, lived for one day of that same
    // date, written for the kCap-eviction corner the admitted push could reach.
    // The architect DELETED it when that producer went: a producer-less
    // mechanism, not a granularity change, in a feature-complete project. Do not
    // re-propose it.)
    //
    // WHAT THE FROZEN SIDE DOES DRIFT IN is the SETTINGS file's view state, and
    // the commit act is the one route that has to care. Both allowlists admit
    // routes that move it (membership re-derived 2026-08-06): zoom, the paged
    // scroll and the overview command move viewport_start_sample or zoom_level,
    // the
    // pointer's pan / strip / ruler drags move both, the mode's OWN cursor-moving
    // acts land the playhead (the diff-flag click, the placement press, and the
    // keyboard's Tab cycle / Home / End / `c` — which `0` reaches too, from full
    // zoom out), and since 2026-08-04 the admitted VIEW SWITCHES
    // move the two whole-file keys `active_audio_view=` and
    // `active_markers_view=` (`t` moving the per-tab band with them, the
    // playhead and viewport translating across the domain flip) — every one of
    // them a key the settings writer persists. So after any of them the frozen text is a stale
    // snapshot of bytes a Ctrl+S would write differently. That is invisible in
    // the diff (the mode displays only `scale=`) and would be a LIE in a
    // checkpoint, so the act rebuilds the now side fresh at commit time rather
    // than committing the frozen one.
    //
    // PER-SESSION SCRATCH: never persisted, never stashed, cleared WHOLE on
    // exit (session and all, so the next entry pays a fresh commit walk).
    struct HistoryMode {
        bool        active = false;
        // Index into the COMMIT walk, 0 = newest. `,` steps older (+1), `.`
        // newer (-1), each clamping at its wall as a consumed no-op. It is the
        // commit walk's alone since 2026-08-07 — the local walk keeps its own
        // position in `local_index` below, and the two survive each other's
        // visits within one session of the view.
        std::size_t index  = 0;
        // WHICH WALK THE LANE IS READING (architect 2026-08-07) — the row-3 tab
        // grid's other axis. GuiHistoryWalkSource (history_diff.h) owns the
        // pair's definitions and the local walk's whole model; what lives here
        // is the session's own state.
        //
        // COMMIT IS THE DEFAULT AT EVERY ENTRY, this plain member initializer
        // applied by the whole-struct machinery at both edges exactly as the
        // compare bit's is: a visit never inherits the last visit's tab.
        //
        // EACH SOURCE KEEPS ITS OWN POSITION across a switch, which is what makes
        // the pair of walks two places rather than one place with a changing
        // subject: read three checkpoints back, look at what your last two undo
        // steps did, come back and you are still three checkpoints back.
        GuiHistoryWalkSource source = GuiHistoryWalkSource::Commit;
        // Index into the LOCAL walk, 0 = the NEWEST MEMBER — which since
        // 2026-08-08 is the furthest FUTURE state, the far end of the redo
        // stack, the walk being the whole undo/redo timeline read as states
        // (GuiHistoryLocalWalk owns the model). Same two steps, same clamps,
        // same clearing edges — everything the walk does reads the ACTIVE
        // source's position through walk_index() below rather than naming either
        // field.
        //
        // THE ENTRY POSITION IS THE ENTRY OWNER'S, NOT THIS INITIALIZER'S: the
        // whole-struct reset lands 0 here, and open_history_mode_fresh then sets
        // the LIVE member's index — where the session is standing — because only
        // a bound walk knows what that index is. With no redo entries the two
        // are the same number.
        std::size_t local_index = 0;
        // THE READING IS NOT A FIELD OF THIS STRUCT — it is AppState's own
        // `history_cumulative` (architect 2026-08-08), which is exactly why it
        // is not here: this struct is RESET WHOLE at both mode edges, and the
        // reading has to survive that. The contract, the default and the
        // session scope are stated at that field; the mapping onto
        // GuiHistoryCompare is AppState::history_compare(), the one site that
        // turns the bit into the delta machinery's vocabulary.
        //
        // WHAT THIS STRUCT STILL OWNS IS THE WALK SOURCE ABOVE, and the
        // asymmetry is deliberate: the SOURCE resets to Commit at every entry
        // (a per-visit fact — where you are looking) while the READING persists
        // (a preference — how you want deltas read).
        //
        // A READING SWITCH IS STILL A MODE EDGE, exactly like a `,` / `.` step,
        // and one owner does all of it (GuiInputHandler::set_history_reading,
        // which takes the (source, reading) pair and writes both halves): clear
        // the mode focus, drop the lane's published content, clear a resting
        // region, REPUBLISH THE LANE SYNCHRONOUSLY (2026-08-07 — the arriving
        // reading's flags stand before the press returns, so the swap shows no
        // blank frame), damage the window. IT MOVES NO VIEWPORT (architect
        // 2026-08-08, superseding the 2026-08-05 reset to full zoom out at this
        // edge and at the step): the window is the user's while the view stands,
        // and only the ENTRY frames the whole song.
        //
        // EVERY READER OF THE DISPLAYED DELTA PASSES THE PAIR, and since
        // 2026-08-07 they do so THROUGH ONE ACCESSOR (displayed_delta() below)
        // rather than by naming both halves: the reading is a (source, compare)
        // PAIR, and four call sites spelling that fork is four places for a
        // Local tab to keep showing commit flags. Its readers, re-derived by
        // grep on displayed_delta: the flag cache's rebuild (waveform_cache.cpp),
        // frame_viewed_commit_diff_span (input_key_dispatch.cpp),
        // GuiPaintHandler::paint_trim's diff-span substitution and the bottom
        // strip's corner line. The ONE reader that deliberately does NOT is
        // head_delta_empty below, which names the COMMIT walk's index 0 and the
        // Cumulative reading explicitly, and says why.
        // The mode's OWN focus: an index into `flags` below, -1 for none. It is
        // NOT a marker index and touches no selection.
        //
        // EVERY SETTER (re-derived by grep 2026-08-06): the mode's own PLAIN
        // focus CLICK on either of a diff flag's two pointer surfaces — its box
        // in the lane and its STEM in the waveform's upper half, one shared body
        // (focus_history_diff_flag); the LANE's two MODIFIED clicks, which focus
        // the flag they select (select_history_diff_flags_modified — the lane
        // alone since the symmetry ruling of 2026-08-06); and the mode's own
        // bare Tab / Shift+Tab / IsoLeftTab cycle. Each sets it and lands the
        // playhead on that flag's frame, and nothing else writes it true.
        //
        // EVERY CLEARER, the whole list, and all but the last clear for ONE
        // reason: the value is an ordinal into the PAINTED list, so anything
        // that rebuilds that list would otherwise leave the highlight on an
        // unrelated flag.
        //   - a click on empty lane (the deliberate clear)
        //   - each `,` / `.` step (handle_history_mode_key)
        //   - each TAB SWITCH (2026-08-05, set_history_reading): the two
        //     readings are two different lists, and since 2026-08-07 so are the
        //     two walks, so it is the step's own reason at a different edge
        //   - each VIEW SWITCH, both axes (2026-08-04, when `t` / `p` / 1 / 2 /
        //     3 joined the keyboard allowlist): the lane paints only the ACTIVE
        //     COLUMN's half of a commit's delta, so W and P are different lists,
        //     and the S/T flip re-lays the same list on another domain. The
        //     clear sits at each axis's own toggle — handle_active_audio_view_-
        //     toggle and GuiActiveViews::toggle_active_markers_view — which is
        //     what makes the 1/2/3 selectors, the view bar and the icon row's
        //     radios inherit it by composition.
        //   - entry and exit (the whole-struct reset at both owners)
        //   - bare HOME / END (2026-08-05) and the WAVEFORM PLACEMENT PRESS
        //     (same day; re-derived 2026-08-06 — it is the WHOLE waveform at
        //     EITHER height, plain or shift-exact, the plain arm falling to it
        //     when no stem is hit and the shift arm ignoring stems outright under
        //     the symmetry ruling), and these are
        //     the exception to the reason above: the list is untouched, but the
        //     playhead moves to a spot nothing marks — an end of the song, or the
        //     pressed column — LEAVING the focused flag, so the focus goes with
        //     it, the mode's analog of the live arms' selection clear. The
        //     press's clear runs ahead of its own gutter return, exactly where
        //     the live body's deselect runs.
        // The playhead a click or a cycle step landed is NOT taken back by any
        // of the list-rebuilding clearers: that landing was navigation, and it
        // stays where the user put it.
        //
        // THE FOCUS AND THE SELECTION SET BELOW CLEAR TOGETHER, ALWAYS, through
        // the one owner clear_history_mode_focus (below this struct): the set is
        // ordinals into the same painted list, so every reason above is its
        // reason too, and a second clearer inventory to keep in step is exactly
        // what that helper exists to prevent. RE-DERIVED 2026-08-06 by grepping
        // every writer of `history_mode.focus` and `history_mode.selection`:
        // every CLEAR of the pair now runs through that owner (the empty-lane
        // click's did so inline until that date, and was routed through it), and
        // the two remaining direct writers are SETTERS rather than clearers —
        // the plain focus click, which calls the owner and then re-seats the
        // focus over it, and the modified lane pair, which writes a non-empty
        // set and the focus it lands on.
        int         focus  = -1;
        // THE MODE'S OWN MULTI-SELECTION (architect 2026-08-05), ordinals into
        // `flags` beside the focus and the REVERT ACT's subject. It is the live
        // selection model re-expressed mode-locally — shift-click takes the
        // contiguous range from the focus, ctrl-click toggles one flag's
        // membership, and a PLAIN click clears it, the focus alone then being a
        // selection of one — and it touches the store selection no more than the
        // focus does.
        //
        // WHY A SET AND NOT A SPAN: the ctrl toggle can leave any subset
        // standing, and the act applies each member independently, so there is
        // no contiguity to lean on. Ordered, so the act walks the subject in
        // frame order (the list is frame-sorted) and the paint reads membership
        // in one lookup.
        //
        // EVERY SETTER: the two MODIFIED diff-flag clicks, IN THE MARKER LANE
        // and nowhere else (select_history_diff_flags_modified,
        // input_pointer.cpp — the flag's stem answers plain clicks only since
        // the symmetry ruling of 2026-08-06, waveform modifiers being gesture
        // vocabulary in every view). Nothing else writes a member — the Tab cycle and
        // the plain click both leave it EMPTY, the live cycle's own
        // replace-with-a-singleton shape.
        //
        // EVERY CLEARER is the focus's, above, and they are the same line of
        // code: clear_history_mode_focus clears the pair.
        std::set<int> selection;
        // The commit's delta resolved into painted order, published by the flag
        // cache's rebuild (the sole producer, beside the hit rects and stems it
        // publishes for these same items). `focus` and every hit rect's
        // marker_index index INTO THIS VECTOR, so all three move together or not
        // at all.
        //
        // IT IS PAINT-CACHE OUTPUT, so it carries the once-per-tick cadence of
        // its producer, and IT IS DROPPED AT EVERY MODE EDGE alongside the two
        // stashes for exactly that reason — a `,` / `.` step would otherwise
        // leave the LEAVING commit's flags standing for the keyboard to cycle
        // (drop_lane_stash_across_history_edge, input_key_dispatch.cpp, owns the
        // argument). SINCE 2026-08-07 every edge runs the producer again in the
        // same press (republish_history_lane_now — the exit included, where what
        // it publishes is the LIVE lane), so the drop and the refill are one
        // atomic swap and the lane never paints blank between two contents; the
        // emptied state is still what the producer's own refusals leave, which is
        // why every reader below still
        // reads an empty list as "nothing there". Its READERS, re-derived by grep 2026-08-05: the producer
        // itself and the lane painter it feeds (waveform_cache.cpp), the mode's
        // Tab cycle and its bare `c` (handle_history_mode_key), and the focus
        // click's shared body (focus_history_diff_flag). Every one of them reads
        // an EMPTY list as "nothing there" — the cold answer the drop relies on.
        std::vector<HistoryDiffFlag> flags;

        // WHICH SESSION THIS IS — a counter the ONE entry owner
        // (open_history_mode_fresh) bumps on every entry, and the reason the
        // flag cache can tell one visit from the next.
        //
        // (active, index, focus, compare) is not enough, and the gap is not a corner:
        // A CLOSE AND A REOPEN THE PAINT NEVER SEES BETWEEN. Two sessions of the
        // same piece land on index 0 with focus -1, the iterative reading and
        // `active` true — the ordinary shape both times — and the paint consults this fingerprint
        // once per frame, so a close and an open delivered in one dispatch batch
        // (an `h` off and an `h` on; or, since 2026-08-05, THE COMMIT ACT's own
        // close followed by the user reopening the view) reach it as a single
        // edge with `active` never observed false. Every other fingerprint input
        // (the marker generations, the viewport) can be unchanged across it too,
        // since neither closing nor committing touches memory. Without the
        // counter the cached surface MATCHES and the previous session's diff
        // flags keep being blitted over a session that no longer has them —
        // visible, unclickable (the hit stashes are cleared at every mode edge),
        // and contradicting the very delta the new session was opened to show.
        // Damage cannot fix that: invalidate_all schedules a repaint, and the
        // repaint is what consults this fingerprint. Which is also why the
        // counter SURVIVES the close reset (close_history_mode states that half).
        //
        // It never wraps in any real session and is never persisted; it is only
        // ever compared for equality.
        unsigned long long generation = 0;

        // IS THERE ANYTHING TO CHECKPOINT? — the HEAD DELTA's emptiness, and
        // the one term that greys the Save-and-Commit act (architect
        // 2026-08-05). The head delta is index 0's, the NEWEST eligible
        // checkpoint, measured against the frozen now side; empty means the
        // session and that checkpoint carry the same authoring content
        // (GuiHistoryCommitDelta::is_empty, history_diff.h, owns the terms).
        //
        // COMPUTED ONCE, AT THE ONE ENTRY OWNER (open_history_mode_fresh), and
        // STATIC FOR THE SESSION'S LIFE BY CONSTRUCTION: both sides are fixed
        // for the visit — the now side is frozen at init(), and EVERY ROUTE THAT
        // COULD CHANGE THE OTHER SIDE CLOSES THE VIEW. That is the honest
        // derivation, re-stated 2026-08-06: the allowlist DOES admit an authoring
        // chord — Ctrl+H, the revert act — so "no authoring route is admitted" is
        // not what makes the bit safe. What makes it safe is that all THREE of
        // the mode's mutators (`'`, Ctrl+S and Ctrl+H) end by closing the
        // view, so no session outlives a write to its own now side. A FUTURE
        // MUTATOR THAT DOES NOT CLOSE THE VIEW WOULD HAVE TO RECOMPUTE THIS BIT.
        // It is cleared by the whole-struct reset at close,
        // like every other field but the generation.
        //
        // IT READS INDEX 0 ALWAYS, never the walk position: `,` and `.` step
        // what is DISPLAYED, while the act always commits on top of the newest
        // checkpoint, so stepping back to an older one must not offer to
        // "re-commit" it.
        //
        // AND IT READS THE COMMIT WALK ALWAYS, which the LOCAL walk of
        // 2026-08-07 changes nothing about: the act publishes a checkpoint into
        // the repository, so "is there anything to checkpoint" is the live state
        // against the newest COMMIT whatever walk the lane happens to be
        // showing. Reading the undo stack here would grey the act on a session
        // that had undone its way back to its own start while the repository
        // still lacked every one of those changes. Its measurement site
        // (measure_history_head_delta) therefore names `session` outright, and
        // Save-and-Commit's reach and face are untouched by the source axis.
        //
        // AND IT NAMES THE CUMULATIVE DELTA EXPLICITLY, never the session's
        // reading bit (AppState::history_cumulative — this is that bit's one
        // deliberate non-reader): the act commits THE LIVE STATE, so "is there
        // anything to checkpoint" is live-vs-newest whatever the lane happens to
        // be displaying, and a `u` press must not move this face.
        //
        // SINCE THE ITERATIVE READING TURNED FORWARD (architect 2026-08-05) the
        // two readings COINCIDE at index 0 — both are the newest checkpoint
        // against the live now side — so the explicit naming currently picks the
        // same delta the bit would. It is KEPT explicit anyway, because the
        // coincidence is a property of the pairing rather than of this question:
        // what this field asks is live-vs-newest by definition, and spelling it
        // is what keeps that true if the pairing ever moves again.
        //
        // WHAT IT DELIBERATELY DOES NOT SEE (a recorded asymmetry, architect
        // 2026-08-05): the delta's vocabulary is the two marker columns plus
        // `scale`, so a session whose only drift is the invisible settings
        // bookkeeping — the per-tab view band the mode's own navigation moves —
        // reads EMPTY and greys the act, even though a byte-level commit would
        // land. Deliberate: a checkpoint is about authoring content, the same
        // reasoning that makes `scale` the only settings key this mode
        // displays. THE PLAIN DISK SAVE IS NOT THE ESCAPE HATCH IT WAS: since
        // 2026-08-08 Ctrl+S in the view IS this act, so a settings-only drift is
        // saved by leaving the view first (architect-accepted with the move).
        // The act's own NothingToCommit arm remains the byte-level backstop for
        // the state where the bit says there IS something and the repository
        // disagrees.
        //
        // ONE READER, TWO CONSUMERS: history_mode_key_blocked (input_key_-
        // dispatch.cpp) makes its Ctrl+S admission conditional on this, so
        // the chord is a consumed no-op and the SAVE button takes its row's
        // disabled face from the SAME decision — never two spellings of it.
        //
        // "MEASURED ONCE AT ENTRY" BECAME "MEASURED ONCE" (2026-08-07, with the
        // streaming walk): a visit may open before member 0 has arrived, and
        // there is then nothing to measure. It RESTS TRUE in that window — the
        // conservative face, the act greyed while the answer is unknown — and
        // the measurement happens at the first drain that delivers member 0,
        // after which it is static for the visit exactly as before. Still ONE
        // measurement site (GuiInputHandler::measure_history_head_delta), which
        // both the entry and the arrival hook call; `head_delta_measured` below
        // is what makes it once.
        bool head_delta_empty = true;

        // Whether the bit above is an ANSWER rather than the resting default.
        // False until member 0 has been compared against the frozen now side;
        // the one measurement site sets it and nothing clears it inside a visit
        // (the whole-struct reset at both edges does).
        bool head_delta_measured = false;

        // THE EDITOR'S NAVIGATION BAND, PARKED FOR THE VISIT (architect
        // 2026-08-05 — "the view is a VIEWER"). The mode navigates freely, and
        // before this it navigated the LIVE band: whatever pan, zoom and
        // playhead the review left behind was what the editor came back to.
        // These four fields are the way back — snapshotted at the ONE entry
        // owner (open_history_mode_fresh) and restored at the ONE exit owner
        // (close_history_mode), so a visit costs the editor nothing.
        //
        // WHAT IS AND IS NOT IN THE SNAPSHOT. In: the trio the mode's own
        // vocabulary moves — the viewport start, the zoom level and the resting
        // playhead. Out: the ACTIVE VIEWS, deliberately (the user keeps the S/T
        // and W/P he switched to — those switches are how both halves of a
        // delta get read, and undoing them would undo the reading); the A/B TAB,
        // which cannot move at all while the view stands (both tab switches are
        // consumed, and since 2026-08-05 the two tab BUTTONS do not even show the
        // A/B pair — they are the walk selector in here); and trim,
        // read_only and the selection, none of which the mode touches.
        //
        // THE TRIO IS ACTIVE-DOMAIN STATE, NOT AUTHORED STATE — in 'T' the three
        // fields carry TARGET frames (AppState::active_audio_view states it) —
        // so the snapshot carries the VIEW IT WAS TAKEN IN beside them. The
        // restore is bit-exact whenever the audio view has not moved, which is
        // every visit that switches no view and every visit whose switches
        // cancel out; when it HAS moved, the two frame-shaped values translate
        // through the live warp map in the flip's own direction, exactly as the
        // `t` toggle translates the band it carries across (the zoom LEVEL rides
        // untranslated there too). The restore's site owns that reasoning.
        int64_t entry_viewport_start_sample = 0;
        double  entry_zoom_level            = kWorkingZoomLevel;
        int64_t entry_playhead_cursor_sample = 0;
        char    entry_audio_view            = 'S';

        // THE TWO WALKS. `session` is the committed history (git, the strict
        // load gate, the prefetch store); `local` is this session's own STATE
        // TIMELINE — every state undo and redo can reach plus the live one —
        // read through the same delta machinery, its membership and indexing
        // owned by GuiHistoryLocalWalk (history_diff.h) and not restated here.
        // Both are bound at the ONE
        // entry owner and both measure against the SAME frozen now side — the
        // local walk takes it from the session rather than capturing a second
        // one (GuiHistoryDiff::now_side).
        GuiHistoryDiff      session;
        GuiHistoryLocalWalk local;

        // -- THE ACTIVE WALK, in three lines -------------------------------
        //
        // The three questions every walk-facing reader asks — how many members,
        // where am I, and what does the lane show — answered once for the live
        // source instead of forked at each site. What is NOT here is anything
        // PER-VOCABULARY by intent: the corner's short-SHA token names `session`
        // and `index` outright because an undo entry has no commit to name, and
        // the `'` editor's PREFILL and its act fork on the source at their own
        // two sites (the opener and load_editor_commit) because the two walks
        // name their members differently — a SHA on one, the displayed member
        // NUMBER on the other (`local_index + 1`, the corner's own arithmetic,
        // named directly inside a branch that has already established the
        // source).

        // How many members the ACTIVE walk carries — the `n/N` denominator.
        std::size_t walk_count() const {
            return source == GuiHistoryWalkSource::Local
                       ? local.entry_count()
                       : session.commit_count();
        }

        // Where the ACTIVE walk stands. The setter is the only writer either
        // position field has outside the entry reset, so a step cannot move the
        // wrong walk.
        std::size_t walk_index() const {
            return source == GuiHistoryWalkSource::Local ? local_index : index;
        }
        void set_walk_index(std::size_t to) {
            if (source == GuiHistoryWalkSource::Local) local_index = to;
            else                                       index       = to;
        }

        // THE DELTA THE LANE SHOWS — the ONE accessor for it (architect
        // 2026-08-07's second walk made the reading a PAIR, and a pair spelled at
        // four sites is four places to forget one of its halves). Non-const
        // because both walks compute lazily and cache; the returned pointer is
        // stable for the visit on either (each class's delta_at states its own
        // contract). nullptr means "nothing to show" and every reader already
        // draws that as the blank lane: an out-of-range index, an unavailable
        // session, or an empty walk.
        // `compare` is handed IN rather than read off this struct: the reading
        // is a program-session preference on AppState (history_cumulative), and
        // a nested struct cannot reach its enclosing object. Every caller spells
        // it `app.history_mode.displayed_delta(app.history_compare())`, which is
        // the one mapping owner doing the translation at each use.
        const GuiHistoryCommitDelta* displayed_delta(GuiHistoryCompare compare) {
            return source == GuiHistoryWalkSource::Local
                       ? local.delta_at(local_index, compare)
                       : session.delta_at(index, compare);
        }
    };
    HistoryMode history_mode;

    // THE CUMULATIVE READING'S BIT — how the history view's delta is read, and
    // the ONE piece of that view's state that is NOT in HistoryMode above
    // (architect 2026-08-08). It lives out here for the reason
    // history_checkpoint_in_flight does: it OUTLIVES THE VIEW. HistoryMode is
    // reset whole at both mode edges, so a field in there could not remember
    // anything across a visit, and remembering is the point.
    //
    // OFF IS ITERATIVE — the viewed member against the NEXT-NEWER one, "what
    // happened after this" — and ON IS CUMULATIVE, the member against the frozen
    // live now side, "how does my session differ". Off at program start, which
    // is this initializer.
    //
    // PROGRAM-SESSION SCOPED, and that is its whole contract: a mode exit and
    // re-entry KEEPS it (superseding the 2026-08-05..08 rule that Iterative was
    // the default at every entry — the reading is a preference now, and a
    // preference the user re-sets on every visit is not one), and closing the
    // program FORGETS it. It is NEVER SERIALIZED: not a settings key, not in the
    // schema, not in a sidecar, not in the render fingerprint's settings terms —
    // it changes no rendered bytes, only how a read-only lane groups them.
    //
    // ITS ONE WRITER is GuiInputHandler::set_history_reading, the switch owner
    // the tab press and the `u` toggle share; its readers are
    // AppState::history_compare() just below (the delta machinery's mapping) and
    // redesign_button_selected (the Cumulative button's lamp, published in every
    // view because the bit is true in every view).
    bool history_cumulative = false;

    // THE BIT IN THE DELTA MACHINERY'S OWN VOCABULARY — the ONE site that maps
    // the session preference onto GuiHistoryCompare, so no caller spells the
    // ternary and the two can never come to disagree about which way round the
    // bit runs.
    GuiHistoryCompare history_compare() const {
        return history_cumulative ? GuiHistoryCompare::Cumulative
                                  : GuiHistoryCompare::Iterative;
    }

    // WINDOW ACTIVATION (keyboard focus), mirrored from the platform's
    // xdg_toplevel state on each activation EDGE (main.cpp's hook, beside the
    // pointer-leave one). The redesigned rows 1 and 2 paint their ground from
    // it — focused #292c30, unfocused #202326 — so the app's header tracks the
    // labwc titlebar above it, which darkens the same way. Nothing else reads
    // it and nothing else in the rows changes: separators, borders, the accent,
    // labels and icons all keep their colors, and there is NO fade — a hard
    // swap on the edge. Row 3's ground is already the unfocused value and does
    // not move. False until the first configure, which is the honest cold answer
    // (the platform's accessor states why that is never visible).
    bool window_activated = false;

    // THE REMEMBERED POINTER POSITION, from the last on_motion event, and the
    // source every pointer-derived answer re-reads when it has to re-resolve with
    // no motion under it. -1 means "no motion seen yet". Its readers, each with
    // its own cadence and its own reason:
    //   * the ROSTER's button faces (recompute_redesign_button_hover), repaired
    //     from the TICK, gated on "no pointer gesture" so the freeze-hover rule
    //     holds — geometry and enabled-state changes move those faces under a
    //     stationary cursor;
    //   * the POINTER CURSOR (refresh_pointer_cursor) and the open dropdown's
    //     ITEM FACES (recompute_dropdown_hover), both repaired from the run
    //     loop's per-iteration SETTLED HOOK, because their inputs — about ten
    //     fact families for the one, the painter-published item rects for the
    //     other — settle with no pointer event of any kind under them;
    //   * the POINTER-LEAVE HOOK (main.cpp), the one reader that is not a repair
    //     and the one that WANTS the position the pointer left behind: it asks
    //     point_in_menu_row_band whether the leave went out through row 1, which
    //     decides whether the menu row's armed mode and the hovered button's
    //     face survive it (the rule is at AppState::Dropdown::menu_row_armed).
    // The dropdown's RELEASE is deliberately NOT a reader: it derives its item
    // from the coordinates the release itself carries (finish_dropdown_release).
    int               last_mouse_x = -1;
    int               last_mouse_y = -1;

    // Is the pointer INSIDE the window? last_mouse_{x,y} keep the last position
    // the pointer was seen at, which is a point INSIDE the window even after it
    // has left — so anything that re-resolves from those coordinates without this
    // flag would answer for a pointer that is gone (a re-lit hover pill, a
    // re-lit menu item, a cursor kind for a zone nobody is over). That is the
    // shared guard of all three repairs above, each carrying it inside its OWN
    // body rather than at its wiring: the tick and the settled hook keep running
    // after a leave, and neither may resurrect what the pointer-leave hook just
    // dropped. The ROSTER's repair refuses the whole walk on it (the other two
    // do the same), which is also what lets that hook KEEP a face — the row-1
    // button still lit while the pointer rests on the titlebar — instead of
    // having it wiped a wakeup later.
    // Written at exactly two edges: true in on_motion, false in the
    // pointer-leave / capability-loss hook beside the hover and press clears.
    // Re-entry delivers a synthesized motion, which sets it back.
    bool              pointer_in_window = false;

    // Undo/redo history for marker mutations. The dirty flags below are
    // derived from it (Undo::recompute_dirty walks saved_distance against
    // each persistence-affecting entry's op_mode). Save/load reshape the
    // saved reference rather than touching the flags directly.
    UndoHistory history;

    // True if any authoring-class file has changes since the last save.
    // app.dirty = warp_dirty || phase_reset_dirty || settings_dirty,
    // recomputed after every persistent push/undo/redo by walking
    // saved_distance against each persistence-affecting entry's op_mode.
    // Drives both the unsaved-work dialog and the dirty-dot.
    //
    // Authoring-class settings — the six engine-block keys (title, scale,
    // bpm, notes, url, cover; editor commits carry undo history) — participate
    // in dirty via settings_dirty. View-state keys — the GUI-kind keys
    // (viewport/zoom/playhead per tab, follow, active_audio_view,
    // active_markers_view, active_tab_view, playback_speed, trim, read_only,
    // gui_scale, audio_player, projects_repo, and the four *_hash
    // env-attestation keys) — do
    // NOT participate: they are silently persisted on Ctrl+S and not tracked as
    // dirty, so quitting without saving simply drops them. Trim is
    // gesture-owned, excluded from undo/redo history, and render-affecting but
    // deliberately treated as transient view state.
    bool        warp_dirty           = false;
    bool        phase_reset_dirty    = false;
    bool        settings_dirty       = false;
    bool        dirty                = false;

    // Target-view live audio buffer. The render pipeline appends
    // synthesised samples here via RenderRequest::output_buffer when the
    // target_render.trigger() helper dispatches a render in target view.
    // On completion the playback device rebinds to this buffer via
    // GuiPlayback::rebind_buffer so playback plays the warped result. The
    // buffer is replaced wholesale per target render — no historical
    // cache. Source-view playback continues to read source.wav from the
    // GuiAudio store, not this buffer.
    std::vector<float> target_buffer;
    // Cached frame count for the populated target_buffer (i.e.
    // target_buffer.size() / channels). Set in the target render's on_done
    // callback; read by the playback rebind. Zero before the first
    // successful target render in this session.
    int64_t target_buffer_frames = 0;
    // The buffer's domain anchor — the full-target-frame coordinate that
    // target_buffer[0] represents — is NOT app state: it travels with the
    // playback bind as GuiPlayback's domain offset, stamped at production time
    // by GuiTargetRender from the trim values the produced samples embody and
    // carried to the completion rebind.

    // Active tab view: 'A' or 'B'. Selects which ViewState snapshot
    // (tab_a or tab_b) is mirrored into the live AppState fields.
    // Toggled by Ctrl+Tab; persisted to .settings. tab_a and tab_b
    // each hold an independent viewport/zoom/playhead/trim/read_only
    // tuple — VALUE-SHAPED ONLY, no selection and nothing else
    // index-shaped (the selection is never parked, architect 2026-07-29;
    // the rule is stated at ViewState) — but share the same
    // warpmarkers, phaseresetmarkers, and engine_settings.
    ViewState tab_a;
    ViewState tab_b;
    char active_tab_view = 'A';

    // Typed engine settings. The live authoring store: settings editor
    // commits, .settings file load, the BPM-sweep scale commit, and the
    // `'` load-in-place (load_render_entry_in_place, a full engine-settings
    // application) all mutate fields of this struct directly. Carried by
    // RenderRequest at dispatch; serialized to .settings on Ctrl+S.
    // Default-constructed before any source load.
    EngineSettings engine_settings;

    // Live working copy of the active tab's trim state. The per-tab
    // backing store lives in ViewState::trim. Excluded from undo/redo.
    // Mirrored to/from the active tab's ViewState slot at the tab-swap
    // boundary in active_views.cpp (same pattern as viewport/zoom/playhead).
    // Trim is a region authored purely by the plain trim-bar pointer drags
    // (single-bound endcap, inter-endcap bridge/pair), the ctrl /
    // ctrl+shift bound-set clicks, the bare-x set arm (a live region sets the
    // trim to it and consumes the span; no region is a silent no-op), the
    // Shift+X MAXIMIZER (writes the full window), and the settings editor's
    // `:tab_X_trim_*=` commits — it is NOT part of the selection system (no
    // bound selection, no Tab stop, no Delete arm). It is ALWAYS SET: the full
    // ruling is at the TrimState store.
    TrimState trim;

    // Bottom-strip command prompt. Active only when a close / re-detect
    // gesture fires while a confirmation is required. Originally
    // a centered modal dialog; the same modal semantics now live in the
    // bottom strip.
    PromptState prompt;

    // Shared text-editor state for two editors distinguished by Kind: the
    // top-strip flag editor (Kind::FlagPayload — active when editing a warp
    // marker's payload, its text run and caret painted live ON THE FLAG ITSELF
    // since row 5's text-on-flag model: render_flag_editor_box unrolls the
    // marker's own box, which the flag pass therefore skips) and the
    // bottom-strip BPM editor (Kind::BpmBracket). The editor
    // owns the keyboard while active.
    text_editor::State top_flag_editor;
    // Last-painted cursor visibility, so the tick can detect a flip and
    // invalidate the top strip without redundant repaints.
    bool top_flag_editor_blink_last = false;

    // Settings-prompt editor. Opens on `:`, accepts a single `key=value`
    // line, writes to engine_settings on commit. Lives in the bottom
    // strip; separate from top_flag_editor so the two paint regions stay
    // independent (the in-practice mutual exclusion comes from the flag
    // editor swallowing all keys while active).
    text_editor::State settings_editor;
    bool settings_editor_blink_last = false;

    // Load prompt editor. Opens on bare `'` from an authoring view,
    // takes a render entry's identifier relative to renders/
    // (`<batch_dir>/<basename>` or a globally-unique bare basename), and on
    // Enter loads that render's frozen sidecar recipe in place as the new
    // authoring baseline (GuiInputHandler::load_render_entry_in_place). A
    // bottom-strip modal like
    // the settings editor; separate State so the two paint regions stay
    // independent.
    text_editor::State load_editor;
    bool load_editor_blink_last = false;

    // THE COMMIT-TITLE EDITOR (architect 2026-08-07), the fourth bottom-strip
    // modal and the `h` history view's own: Ctrl+S while the view stands
    // opens it prefilled with the checkpoint's default message (`Update <id>`,
    // history_checkpoint_title's own spelling) and Enter runs the Save-and-
    // Commit act with whatever the buffer holds as the commit title. It
    // REPLACED the confirmation prompt that used to guard the act: the question
    // "shall I?" and the question "under what message?" are the same pause, and
    // only the second one carries information — a bare Enter is the old `y`.
    // Esc abandons with nothing written, and an empty or whitespace-only buffer
    // red-flashes rather than committing an unnamed checkpoint.
    // A bottom-strip modal like the two above, with its own State so the paint
    // regions stay independent; it can only be open while the history mode
    // stands, which is what keeps it out of every other surface's way.
    text_editor::State commit_title_editor;
    bool commit_title_editor_blink_last = false;

    // IS A CHECKPOINT ACT IN FLIGHT? (architect 2026-08-07, with the act's move
    // onto a background worker.) Written on the MAIN THREAD at exactly two
    // edges — true when run_history_commit dispatches the job, false when the
    // completion event is consumed — so it is plain state, never read from the
    // worker thread and needing no atomic. It is the GUI-side truth the worker's
    // own is_busy() mirrors: the refusals below are pure reads of AppState (one
    // of them an inline face predicate), and giving them a reference to the
    // worker would put the button's face and the key's admission on two
    // different objects.
    //
    // THREE REFUSALS READ IT, single-in-flight and the write race being the two
    // rules: the history view's Ctrl+S admission (history_mode_key_blocked, so
    // the chord is a consumed no-op AND the Save-and-Commit button greys from
    // that same one decision, exactly as the head-delta bit does); bare `h`
    // itself, which will not open a view whose walk would be measured against a
    // repository the worker is mid-mutation on; and — since 2026-08-08 — EVERY
    // SAVE, globally, at the one save owner (GuiSaveOps::save). That third one
    // is the bit's only VISIBLE face: the act closes the view and `h` will not
    // reopen one, so the first refusal's grey is structural, while the save
    // lockout shows as the Save button's "Committing..." wherever the user is.
    // Its reason is a real race rather than a policy — the worker writes the
    // three sidecars into projects/<id>/ off the main thread, and in the
    // coincident workflow a concurrent Ctrl+S writes those same paths through
    // the same fixed temp name.
    bool history_checkpoint_in_flight = false;

    // THE CRITICAL SLOT — the bottom row's leftmost cell, and the product's one
    // permanent failure surface (architect 2026-08-09, REPLACING the acknowledge
    // modal the checkpoint's failures used to raise). A critical failure must be
    // IMPOSSIBLE TO MISS and IMPOSSIBLE TO HIJACK WITH: a modal is missable
    // (it can be dismissed with a keystroke aimed at something else) and it is a
    // hijack (it takes the keyboard from whatever the user was doing, on a
    // clock he did not choose). This is neither. It is PAINT-ONLY — it owns no
    // key, no chord, no click and no rect the pointer can reach — and it is
    // PERMANENT: nothing in the input layer can clear it, there is no timer
    // behind it, and there is nothing to dismiss.
    //
    // ITS ONE CLEARING ROUTE IS A LATER SUCCESS: a checkpoint act that ends
    // Committed or NothingToCommit clears it, because a success supersedes the
    // stale failure it replaces — the message says what the repository's last
    // answer was, so a newer answer is what retires it. Otherwise it stands
    // until the program closes.
    //
    // IT IS SESSION-SCOPED AND SURVIVES EVERYTHING BELOW IT: the history view's
    // entry and exit, every view and tab switch, and BOTH load-in-places (a
    // commit's or a timeline state's) leave it exactly as they found it. That is
    // deliberate rather than incidental — loading another state in place does
    // not un-fail the checkpoint that failed, and the row is global.
    //
    // GENERAL-PURPOSE IN SHAPE, ONE PRODUCER TODAY. The name and the contract say
    // "critical", not "checkpoint", so a future critical producer can write it
    // with no new surface; today the only writer is the checkpoint worker's
    // completion (GuiInputHandler::on_history_checkpoint_complete), which sets it
    // on the three failing verdicts and clears it on the two clean ones. Empty
    // means nothing critical has happened, which is the resting state of every
    // session that never publishes a checkpoint.
    std::string critical_error_message;

    // Tick backstop bookkeeping: last live-domain total observed by the
    // on_tick clamp (see main.cpp). 0 = not yet observed.
    int64_t last_tick_live_total = 0;

    // Archival render dispatch state. `queue_running` is true while any
    // archival render dispatch is in flight (the Ctrl+Alt+R one-shot and the
    // iteration/BPM sweep batch runs). The Esc handler checks it to scope the
    // cancel binding away from normal interaction. `queue_cancel_requested`
    // is set by Esc during a run and read between batch entries.
    bool queue_running           = false;
    bool queue_cancel_requested  = false;

    // Non-interactive bottom-strip status text, giving the user visual
    // feedback while no other UI is updating. Driven by the shared batch
    // runner (the iteration/BPM sweeps), startup loading, Ctrl+Alt+R, and
    // target-preview updates — not a manual queue. Empty means "no status —
    // render the timestamp normally."
    // IT COEXISTS WITH prompt.active, and PAINT PRECEDENCE is what keeps that
    // invisible rather than any exclusion: an archival render runs on, so
    // dirtying the project and pressing Ctrl+Q raises the close prompt over a
    // live run (the prompt cancels nothing), and the run's own completion can
    // then rewrite or clear this string while the prompt stands. The two share
    // ONE bottom-strip slot and the prompt is its FIRST tier (paint_handler's
    // chain tests prompt.active before this), so a prompt is what the user sees
    // for as long as it is up and this string is simply whatever the run left
    // behind when it goes.
    std::string queue_progress_text;

    // Transient one-line status message shown in the bottom row's modal/status
    // span, one tier above the resolved readout (the row-7
    // chain; it was an appendix on the status line when the strip had two
    // rows and view letters). Set by a
    // command that wants to report a non-fatal outcome (e.g. phase-reset
    // state-paste divergence); cleared on the next keyboard press in
    // on_key. Empty = nothing to show. General-purpose: not specific to
    // any one command, so future commands can reuse it.
    std::string transient_status_message;

    // One-slot pending archival render command. An archival dispatch
    // (Ctrl+Alt+R / the iteration or bpm sweep) that finds the
    // worker busy kills the running render (the Esc pair: request_cancel +
    // queue_cancel_requested) and parks its fully built request set here;
    // the worker-idle pump (GuiTargetRender::maybe_dispatch_pending, via
    // GuiInputHandler::dispatch_pending_archival_command) dispatches it once
    // the cooperative cancellation drains — never a synchronous wait on the
    // GUI thread. A newer command replaces an older parked one wholesale.
    // Esc during the drain disarms the slot (Esc means stop rendering, and
    // the parked command would otherwise resurrect a render right after the
    // cancel lands).
    struct PendingArchivalCommand {
        bool armed = false;
        // Ctrl+Alt+R shape (single): reqs holds exactly one request,
        // dispatched through dispatch_single_archival_render. Batches
        // (armed && !single) go through start_render_batch with
        // `batch_label`.
        bool                       single = false;
        std::vector<RenderRequest> reqs;
        std::string                batch_label;
        // A parked Ctrl+Alt+Shift+R command late-binds its output folder/cell
        // at the worker-idle pump, not at command time: a command-time scan can
        // be invalidated by the very render this command kills (that render
        // may still publish into renders/ during its cancellation drain,
        // after the scan but before the cancel flag lands, stealing the
        // scanned cell name). Set only by the Ctrl+Alt+Shift+R park site; the
        // pump allocates the folder/cell here. Because Esc disarming the slot
        // (the wholesale `pending_archival = {}` reset) clears this flag with
        // everything else, a parked-then-abandoned misc command creates no
        // folder at all — the allocation never runs.
        bool                       miscellaneous = false;
    };
    PendingArchivalCommand pending_archival;

    // Phase reset propagate (W-mode Ctrl+P / Ctrl+Alt+P). Single-slot
    // session-only clipboard cleared on app exit. `pending_paste_anchor`
    // is the destination warp-marker index captured when the paste
    // confirmation prompt opens; consumed by the prompt response.
    PhaseResetClipboard phase_reset_clipboard;
    int                pending_paste_anchor = -1;

    // (THERE IS NO TEXT CLIPBOARD FIELD HERE — 2026-08-02. The session-only
    // `text_clipboard` string is DELETED with the system clipboard's arrival:
    // the payload has ONE representation now, GuiPlatform's, which has to exist
    // anyway to serve the compositor's `send` event and which owns the
    // self-paste short circuit. A second copy in AppState was pure duplication
    // with drift risk — every reader it ever had was a pass-through handing it
    // straight back to clipboard_set_text. The three copy sites compose their
    // string and hand it over directly. The PHASE-RESET clipboard above is a
    // different concept and stays.)

    // Iteration mode. Toggled by plain `i` in the WARP COLUMN, in EITHER AUDIO
    // VIEW (no-op in phase-reset view). Session-only (off at load, lost on app
    // close); it survives the W/P marker-view switch AND the S/T audio-view
    // switch in both directions — ITERATION MODE IS TARGET-LEGAL (architect
    // 2026-08-07, superseding his 2026-07-23 ruling that entering target view
    // wipes the brackets and exits the mode; the deleted wipe's record is at
    // handle_active_audio_view_toggle, input_handler.cpp, and the sweep
    // dispatches from either view too). Bracket AUTHORING stays source-only at
    // the flag editor's own home-view gate. When true, flag_text_iter splices the
    // inline `+[lo, hi]` bracket into every eligible owning marker's composed
    // label, so the mode is visible directly on the flags (it is a flag-cache
    // fingerprint field for exactly that reason).
    bool iteration_mode_enabled = false;

    // BPM mode. Toggled by plain `m` in warp view. Mutually
    // exclusive with iteration_mode_enabled (toggling one ON forces the
    // other OFF). Session-only. The BPM owner is identified at runtime
    // by walking markers for bpm_owner=true; at most one marker holds
    // the flag at a time, maintained as an invariant by the toggle.
    bool bpm_mode_enabled = false;

    // One entry in the flat list of valid renders under
    // <source_parent>/renders/, produced by
    // GuiRendersDir::enumerate_render_entries. Just the three path fields;
    // a render entry's sidecar set (.warpmarkers / .phaseresetmarkers /
    // .settings) is written ONCE at queue/dispatch and never touched again.
    // Consumed by the `l` listen-to-renders launcher and the `'` load
    // editor (load_render_entry_in_place).
    struct RenderEntry {
        std::filesystem::path batch_folder;     // <source_parent>/renders/<i>_<tag>
        std::string           basename;         // e.g. "01" (no extension)
        std::filesystem::path wav_path;         // batch_folder / (basename + ".wav")
    };
};

// Geometry helpers — definitions live at file scope in main.cpp. Declared
// here so viewport.cpp can call them.
int     top_strip_h(const AppState& a);
int     bottom_strip_h(const AppState& a);
GuiRect waveform_area(const AppState& a);
GuiRect top_strip_area(const AppState& a);
GuiRect bottom_strip_area(const AppState& a);
// One shared lane-rect helper for every strip lane (see the layout contract at
// its definition in main.cpp). lane_from_window_edge indexes from the strip's
// window edge, 0 = edge-most. The named lane accessors below delegate to it.
GuiRect strip_row_rect(const AppState& a, bool top_strip,
                       int lane_from_window_edge);
GuiRect top_menu_row_area(const AppState& a);
// ROW 1'S BAND AS A PREDICATE — the ONE spelling of "this point is on the menu
// row", so the geometry is written once for the two consumers that decide the
// MENU ROW'S MODE by it (AppState::Dropdown::menu_row_armed):
//   * update_menu_row_exit (input_pointer.cpp), which ends the mode when a
//     delivered MOTION leaves the band — wander down to the waveform and
//     Settings needs a click again;
//   * the platform's POINTER-LEAVE hook (main.cpp), which asks the same question
//     of the REMEMBERED position to tell a leave THROUGH row 1 (upward onto the
//     titlebar, which row 1 abuts — mode and hovered face both survive) from a
//     leave anywhere else (both go).
// It is the press claim's own rect too, so "on the row" means one thing to the
// claim, the exit and the leave alike.
inline bool point_in_menu_row_band(const AppState& a, int x, int y) {
    return rect_contains(top_menu_row_area(a), x, y);
}
GuiRect top_toolbar_row_area(const AppState& a);
GuiRect top_tab_row_area(const AppState& a);
GuiRect top_icon_row_area(const AppState& a);
// ROW 5's three lanes (2026-08-01), replacing the legacy
// chip / marker-text / flag / triangle four.
GuiRect top_trim_row_area(const AppState& a);
GuiRect top_ruler_row_area(const AppState& a);
GuiRect top_marker_row_area(const AppState& a);
// ROW 7's single bottom lane (2026-08-01), replacing the legacy
// upper/lower pair: the lane with its two borders, and the content band inside
// them.
GuiRect bottom_row_area(const AppState& a);
GuiRect bottom_row_content_area(const AppState& a);
int64_t samples_visible(const AppState& a, const GuiAudio& audio);
double  current_samples_per_pixel(const AppState& a, const GuiAudio& audio);
// The pure level→spp exponent: ms_per_px = 0.625 * 2^(level - 1), fully
// level-determined and domain-independent. Non-static/public because it is
// called from input_render_dispatch.cpp's dispatch-time view-anchor math (a
// domain OTHER than the active display context's — a cell's own map domain),
// so it cannot be main-private.
double  samples_per_pixel_at(double zoom_level, int sample_rate);
// Active-domain sample range a marker may occupy to stay within the visible
// strip: pixel 0 (viewport_start) through the last fully-visible pixel
// (area.w - 1). Mouse-driven marker moves clamp the grabbed marker to this so
// it can never hide offscreen, where its precise location would be lost. The
// playhead is exempt — it alone may reach 1px past the strip, and only at EOF.
// Returns {lo, hi} as active-domain samples.
std::pair<int64_t, int64_t> viewport_marker_bounds(const AppState& a,
                                                   const GuiAudio& audio);

// The ONLY route by which a double becomes an authored position anywhere in
// the tree: every gesture commit — nudge, drag release, the
// trim gestures, propagate paste — funnels its final
// position through this before writing a marker time or trim bound, so
// every store mutation commits a whole int64 frame (the field type makes a
// fractional authored position unrepresentable). Banker's rounding
// (std::nearbyint under the default rounding mode — the project-wide
// convention), no epsilon; the cast after nearbyint is exact. The ties are
// real, not theoretical: at 44.1 kHz
// the zoom table's frames-per-pixel values are 27.5625, 55.125, 110.25,
// 220.5, 441, ... (0.625 ms/px deepest, doubling), so at the 5 ms level
// every odd pixel offset is an exact half-frame tie — banker's rounding
// debiases them. No other call site may round or cast an authored
// position on its own.
inline int64_t snap_authored_frame(double frame) {
    return static_cast<int64_t>(std::nearbyint(frame));
}

// The single query for "some pointer gesture is in flight" — a marker
// reposition drag, a trim drag, a strip-row
// zoom/pan drag, a region-select drag, an editor
// text drag, or a pending marker / trim drag
// armed by a press (button held, watching for the threshold). (The scrub is a
// one-shot press action, not a gesture — it arms nothing and so never appears
// here. The target-view TEMPO drag and its pending were on this list until
// 2026-07-29, when the whole tempo drag was deleted — see marker_drag.h.)
// FIVE CONSUMERS, re-derived by grep 2026-08-09, each stating the same
// "nothing pops mid-gesture" boundary from its own side — and EVERY ONE OF THEM
// IS AN INPUT ROUTE, which is the shape this predicate is for:
//   * wheel_context (input_handler.cpp) — on_wheel's completed-detent gate and
//     the platform's per-frame sub-detent accumulator probe both route through
//     it, so a wheel cannot shift the viewport out from under a gesture (the
//     PENDING drags are included for exactly that: not out from under a press
//     before its drag begins either);
//   * repeat_eligible (input_key_dispatch.cpp) — a key held through a gesture
//     must not arm a repeat that fires once the gesture ends;
//   * the run loop's per-tick redesign-button hover refresh (main.cpp) — an
//     active gesture FREEZES hover;
//   * the BARE RIGHT-PRESS SCRUB gate (on_button_press, input_pointer.cpp,
//     2026-08-01) — the right button is deliverable while the left one is held,
//     so the one scrub act it runs must not fire into a live drag;
//   * pointer_cursor_kind's live-gesture refusal (input_pointer.cpp) — a cue
//     must not promise a press mid-drag — RANKED BELOW the trim-gesture arm,
//     the one gesture that keeps its own cursor (architect 2026-08-03; the
//     contract is at pointer_cursor_kind's declaration, input_handler.h).
// (A SIXTH CONSUMER lived here for one day of 2026-08-09 — the checkpoint's
// acknowledge modal, which asked this before raising itself from a worker's
// clock, a prompt over a live gesture having its release swallowed at the
// prompt's own gate. It went with the modal, which became a paint-only slot.
// Nothing outside the input layer asks this question now.)
inline bool any_pointer_gesture_active(const AppState& app) {
    return app.drag.active ||
           app.trim_drag.active ||
           app.strip_drag.active || app.scroll_drag.active ||
           app.region_drag.active || app.editor_text_drag.active ||
           app.pending_marker_drag.active ||
           app.pending_trim_drag.active;
}

// architect ruling 2026-07-22: each marker column authors in its HOME view
// only — warp markers in source view, phase resets in target view. In the
// non-home view a column is display/navigation-only (selection, Tab and the
// selection-only readout all live — the retired hover popup and lane readouts
// are recorded at the HoverPopupState deletion note above;
// every placement/store mutation refuses silently,
// navigation-class, exactly the read-only-tab convention). The THREE ruled
// exceptions live at their sites: (1) the bare UP/DOWN TEMPO CENT STEP in
// W+target (owner-only there, adjust_tempo_cents — singleton and group), which is
// the WHOLE tempo surface now and is dispatched without consulting this
// predicate; (2) the
// phase-reset propagate (a warp-view gesture that authors phase resets; its
// paste lands in target view); (3) THE ITERATION-BRACKET WIPE IN TARGET VIEW
// (granted 2026-08-07 with the ruling that iteration mode is TARGET-LEGAL — two
// sites, the iteration sweep's success tail and the `i` toggle's OFF branch,
// both reachable in W+target now that the mode rests there; the class is argued
// once at run_iteration_sweep_render's tail, input_key_dispatch.cpp). It is the
// narrowest of the three: iter brackets are SESSION-ONLY warp-store fields,
// never serialized and excluded from the render recipe, pushed with
// affects_persistence=false, so the write reaches neither disk nor a render —
// the cent step's own class of argument. The `i` TOGGLE ITSELF IS NOT ON THIS
// LIST: it moves MODE STATE, not authored content, and simply gates on the warp
// column in either view. The list SHRANK to two on 2026-07-29 and grew back to
// three on 2026-08-07: the
// tempo family's other two flavors — the pointer tempo DRAG and the bare
// Left/Right TEMPO-IMAGE STEP — were DELETED wholesale (the delete list is at the
// head of marker_drag.h), so bare Left/Right in W+target with a selection is now
// a consumed refusal and W+target has no pointer authoring gesture at all.
// (The 2026-07-24 "third exception" — a both-views
// warp POSITION nudge — was re-ruled away the same day: there is no warp
// position authoring in target view at all.) The flag DRAG and every other warp
// mutation stay home-view-only through this predicate.
inline bool active_column_authoring_allowed(const AppState& app) {
    return (app.active_markers_view == 'P') ? (app.active_audio_view == 'T')
                                            : (app.active_audio_view == 'S');
}

// Restore ascending time_frame order after a mutation that may have
// moved markers past their neighbors (shift, nudge, drag commit). The
// marker stores are always sorted by time_frame at rest; mutations
// that change times in place call this immediately after writing.
// Stable: equal-time markers keep their pre-sort relative order, so
// ties resolve deterministically. Returns the old-index -> new-index
// permutation when a reorder happened, or an empty vector when the list
// was already in order (the common case — the up-front scan keeps that
// path allocation-free). Callers pass the result to
// remap_marker_indices_after_reorder so every index-shaped piece of
// state that referenced a moved marker follows it. The marker container
// is read only through `time_frame`, so both marker types work.
template <typename Marker>
std::vector<int> reorder_markers_by_time(std::vector<Marker>& markers) {
    const int n = static_cast<int>(markers.size());
    bool sorted = true;
    for (int i = 1; i < n; ++i) {
        if (markers[i - 1].time_frame > markers[i].time_frame) {
            sorted = false;
            break;
        }
    }
    if (sorted) return {};
    std::vector<int> order(n);
    for (int i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
        [&markers](int a, int b) {
            return markers[a].time_frame < markers[b].time_frame;
        });
    std::vector<Marker> reordered;
    reordered.reserve(n);
    for (int old_idx : order) reordered.push_back(std::move(markers[old_idx]));
    markers = std::move(reordered);
    std::vector<int> old_to_new(n);
    for (int new_idx = 0; new_idx < n; ++new_idx) {
        old_to_new[order[new_idx]] = new_idx;
    }
    return old_to_new;
}

// Apply a reorder_markers_by_time permutation to the index-shaped state that
// must follow moved markers. NO `column` PARAMETER: every piece of state below
// belongs to the ACTIVE column, and every caller reorders the ACTIVE column's
// store, so there is nothing for a column test to select between (the parameter
// existed only for the per-tab parked selections, deleted 2026-07-29).
//
// THE COMPLETENESS CLAIM, re-derived 2026-07-29 by reading every field of
// AppState and of the live drag state rather than by inheriting a list. Exactly
// three kinds of state hold a marker INDEX, and all three are covered:
//   * the LIVE selection — app.selected_markers + app.last_selected_marker, the
//     product's ONLY selection (nothing parks a copy). Correct without a column
//     test because every caller reorders the ACTIVE column's store (the
//     home-view binding puts warp gestures in source view and phase-reset
//     gestures in target view, and the four call sites are the two position
//     nudges and the two marker-drag commits);
//   * the SHIFT-RANGE ANCHOR — app.shift_range_anchor, over that same active
//     column's store. It is REMAPPED, not cleared: a reorder does not end a
//     range interaction, and since the anchor survives shift releases (see its
//     field) a stale pre-reorder index would silently name the wrong row at the
//     next shift-click;
//   * the live DRAG state's ONE held index, when a drag is live on the reordered
//     store: dragging_markers[0], the dragged marker (re-derived 2026-07-29 —
//     hit_marker was a second entry here until it went, having no reader left).
// NOTHING ELSE HOLDS AN INDEX ACROSS COMMANDS AT ALL, which is why the list is
// this short: every field ViewState parks is frame- or value-shaped
// (viewport_start_sample, zoom_level, playhead_cursor_sample and the TrimState
// bounds are FRAMES; read_only is a flag) — the rule stated at ViewState. Undo
// snapshots copy whole marker lists rather than indices, and their touched-index
// hints are rewritten by their own callers post-reorder.
// No-op on an empty permutation (the store was already in order). Body in
// app_state.cpp.
void remap_marker_indices_after_reorder(AppState& app,
                                        const std::vector<int>& old_to_new);

// CLOCK_MONOTONIC milliseconds (steady_clock is CLOCK_MONOTONIC on this
// platform). The ONE shared wall-clock reader for the press-driven double-click
// time base (strip-row / marker double-click detection, input_pointer.cpp). Body
// in app_state.cpp so no TU copies its own clock reader.
int64_t monotonic_ms();

void    clamp_viewport_start(AppState& a, const GuiAudio& audio);
// Returns the pixel column (offset from waveform_area.x) for the cursor.
// ONE FORM ONLY, and it takes the viewport AND its samples-per-pixel
// EXPLICITLY: there is no live-viewport convenience overload any more (both
// playhead_pixel_x's and scanner_pixel_x's were deleted 2026-07-30 caller-less,
// the last users having moved to the plate basis under the damage rule below —
// do not reintroduce one, since the only basis these pixels have is the plate's).
// Use it from on_redraw to align the cursor/scanner paint with the cached layers
// (waveform, marker stems, flags) — those layers render against
// wf_cache.fp_vp_start at the displayed spp (derivable as
// (fp_vp_end - fp_vp_start) / fp_area_w), including the 1-2 paint frames while
// the worker rebuilds against a viewport change. Threading BOTH parameters
// through keeps cursor/scanner and surrounding markers in lockstep during that
// window; passing fp_vp_start alone but reading the live spp would mix frames of
// reference and visibly displace the cursor for one frame after each zoom
// gesture.
//
// THE PLAYHEADS' DAMAGE RULE (architect 2026-07-30 — the one authoritative
// statement; every damage site carries only its own class plus a pointer here).
// DAMAGE FOLLOWS THE BASIS OF THE PIXELS IT ERASES, and the playheads' pixels
// are PLATE-REGISTERED: paint_playheads (the cursor) and paint_scanner — one
// pass each since 2026-08-01, when the scanner moved above the marker stems —
// both draw through the explicit-basis form at
// GuiPaintHandler::plate_viewport_basis.
// So NARROW playhead/scanner damage must resolve its columns on the PLATE basis
// too. The superseded rule here said the opposite ("the narrow-damage path needs
// the live position because live == displayed in steady state") — true at a
// settled rest, false in exactly the windows that matter: an ASYNC publish is in
// flight (a follow-scroll page turn, a resize, the launch load, a preview-driven
// total drift) whenever live holds the NEW span and the plate still shows the
// OLD, and a live-basis column then damages pixels the playhead was never drawn
// at — a frozen scanner line, or a stop that leaves its last column un-erased,
// until the next publish heals it.
//
// TWO SHAPES SATISFY THE RULE, chosen per site by CADENCE (re-derived by grep
// 2026-07-30 over Viewport::invalidate_playhead_columns' callers and the
// playhead-writing full-area sites):
//  - NARROW ON PLATE, reserved for the PER-FRAME sites, which cannot afford a
//    full repaint and can see a GuiPaintHandler to compute plate columns: the
//    two 60 Hz SCANNER sites in main.cpp — the tick heartbeat and the pre-paint
//    advance — and nothing else. Both pass plate_viewport_basis() through the
//    explicit-basis overloads.
//  - FULL WAVEFORM-AREA DAMAGE for every DISCRETE, human-paced playhead write:
//    the stop teardown (stop_playback_if_playing) and the launch
//    (launch_playback_from), the MARKER LAND (land_playhead_on_marker — through
//    which the Tab/`c` jump now inherits this shape too, having stopped
//    hand-copying the recipe 2026-07-30), move_playhead_to's no-scroll branch,
//    and the live-domain cursor repair (clamp_display_state_to_live_domain). A
//    full-area invalidate is ownership-window-proof by construction — it cannot
//    ride the wrong epoch — and the repaint is affordable at the rate these
//    fire (the project already pays a full synchronous plate RENDER per pan
//    event at the same key-repeat and pointer-frame cadence). Several of these
//    sites additionally CANNOT reach a paint handler (Viewport,
//    GuiPlaybackLifecycle and the free land helper all see none), so for them
//    the widening is the only honest shape as well as the affordable one.
double  playhead_pixel_x(const AppState& a, int64_t vp_start, double spp);
// Returns the pixel column (offset from waveform_area.x) for the scanner,
// computed from the CONTINUOUS playhead_scanner_precise (not the integer
// sample) so a viewport rescale slides it smoothly. Meaningful only while
// playhead_scanner_active — at rest playhead_scanner_precise is stale by
// contract, so every caller reads this behind that gate. Explicit-basis only,
// for the reason given on playhead_pixel_x above.
double  scanner_pixel_x(const AppState& a, int64_t vp_start, double spp);
// Active-domain total frame count. Source view returns audio.total_frames();
// target view returns the deformed total derived from the warp_frame_map cache
// (the forward-translated source length). Used by every viewport helper that needs the
// "length of the timeline currently being viewed" — the viewport clamp and
// the effective zoom-out ceiling. Declared here so any TU touching the
// viewport math can reach it.
int64_t live_total_frames(const AppState& a, const GuiAudio& audio);

// Live-domain playhead clamp — the single spelling of the playhead domain
// ruling: the playhead rests in [0, total - 1] of its LIVE view's domain,
// everywhere, after any gesture. All authored positions — both marker columns
// and both trim bounds — wall at total - 1, so every sync onto one is in-domain
// by construction; the clamp is the load-lenient runtime guard for persisted
// scratch values, not a source-view authored-position concern. Every playhead write
// funnels through here: Viewport::move_playhead_to (the gesture route),
// and the non-gesture live-ization routes a persisted or stashed value
// takes into the live fields — the source load's tab snapshots, the
// Ctrl+Tab restore, and the render-entry load-in-place's tab bands — so an
// arbitrary non-negative persisted int64 (the settings schema is
// load-lenient on view scratch) rests in-domain BEFORE any translation
// arithmetic (the S/T toggle's double->int64 conversion, Space's lead-in
// launch offset) can consume it. The clamp reads live_total_frames — the
// active display context's domain total, source-frame total in source view
// and target-frame total (cached at `t`-toggle) in target view — so it
// always matches the domain the value will live in. Clamping IS the
// load-lenient ruling, never a refusal: a value already in [0, total - 1]
// passes through unchanged. An empty live domain (total <= 0 — unreachable
// once audio is loaded, zero-frame sources refuse) has no in-domain frame
// and clamps to 0.
inline int64_t clamp_playhead_to_live_domain(int64_t frame,
                                             const AppState& a,
                                             const GuiAudio& audio) {
    const int64_t total = live_total_frames(a, audio);
    if (total <= 0) return 0;
    if (frame < 0) return 0;
    if (frame >= total) return total - 1;
    return frame;
}

double  effective_max_zoom_level(int waveform_width_px,
                                 int64_t total_frames,
                                 int sample_rate);
// Clamp a requested zoom level into the per-file window [kMinZoom, effective
// per-file ceiling]. The single owner of the level-bounds pair, shared by the
// clamp_viewport_start chokepoint and apply_zoom_change's pre-clamp. A no-op
// while loading (no live frames), so it cannot stomp a level the load path is
// mid-assignment.
double  clamp_zoom_level(const AppState& a, const GuiAudio& audio, double level);
// The rightmost on-grid viewport start (the flush-right rest). The single
// right-wall owner, shared by the clamp_viewport_start chokepoint and the strip
// drag's per-event pan clamp (apply_strip_drag_at) so both derive the same wall.
int64_t max_viewport_start_grid(const AppState& a, const GuiAudio& audio);
std::pair<long long, long long> compute_trim_samples(
    const AppState& a, long long total_frames);
GuiRect timestamp_invalidate_rect(const AppState& a);
GuiRect playhead_invalidate_rect(const GuiRect& area, double px_x);
bool    rects_intersect(GuiRect a, GuiRect b);
GuiRect union_rect(GuiRect a, GuiRect b);

// Free-function form of GuiActiveViews::active_view_state(): the active A/B
// tab's view-state slot. The renderer / the b/e/u handlers don't have access
// to GuiActiveViews but need to reach the active tab's view-state from an
// AppState reference alone.
inline ViewState& active_view_state(AppState& a) {
    return (a.active_tab_view == 'B') ? a.tab_b : a.tab_a;
}
inline const ViewState& active_view_state(const AppState& a) {
    return (a.active_tab_view == 'B') ? a.tab_b : a.tab_a;
}

// THE ACTIVE MARKER COLUMN'S STORE SIZE, one owner for the phase-reset/warp
// selector the selection walk, the shift-range anchor check, the Tab/`c` jump's
// focus resolution, the marker drag's index bound and the undo restore's extent
// walk all gate their indices on. A COUNT ONLY, deliberately: callers that also
// need elements (cycle_selection, the undo extent walk) bind their own store
// refs beside it — a type-erased "active store" accessor is exactly the
// speculative generality the two co-equal marker columns refuse. (The many
// `active_markers_view == 'P'` dispatch sites elsewhere are NOT this concept;
// they pick behavior, not a size.)
inline int active_marker_count(const AppState& a) {
    return (a.active_markers_view == 'P')
        ? static_cast<int>(a.phaseresetmarkers.markers().size())
        : static_cast<int>(a.warpmarkers.markers().size());
}

// THE ONE HISTORY-STEP ACTIONABILITY PREDICATE: true when a restore FROM
// `stack` would actually act. Two ways a step is a silent no-op — an empty
// source stack, or a top entry whose TARGET tab is currently read-only (a
// reversible per-tab toggle, so it is decided now rather than at record time).
//
// It lives out here, rather than inside Undo, because it has TWO readers that
// must never drift: Undo::history_entry_actionable (the authoritative guard
// do_undo / do_redo run before touching a stack — it delegates straight to
// this) and the Undo/Redo BUTTONS' enabled predicate below. A button that greys
// on a fact the key does not consult, or stays lit on one it does, is exactly
// the drift the redesign's chord-dispatch rule exists to prevent, and the same
// answer here is what makes "the button is its chord" true for the face as well
// as for the action.
inline bool history_step_actionable(const AppState& a,
                                    const std::vector<UndoEntry>& stack) {
    if (stack.empty()) return false;
    const char tt = stack.back().tab;
    return !((tt == 'B') ? a.tab_b.read_only : a.tab_a.read_only);
}

// DROP THE HISTORY VIEW'S OWN FOCUS AND SELECTION — the ONE clearer for the
// pair (2026-08-05, with the multi-selection), and the reason there is no second
// inventory to keep in step: the two are ordinals into the same painted list, so
// every reason to drop one is a reason to drop the other. The clearer sites, and
// why each of them clears, are enumerated ONCE at AppState::HistoryMode::focus;
// each site here calls this and states only its own class.
//
// "ONE CLEARER" IS EXACT AS OF 2026-08-06, when the plain focus click's
// empty-lane arm — which had cleared the pair inline — was routed through here;
// re-derived by grepping every writer of the two fields. What remains outside is
// SETTERS, not clearers: that same click re-seats the focus over this call, and
// the modified lane pair writes a non-empty set with the focus it lands on.
//
// It RETURNS WHETHER ANYTHING WAS STANDING, because two of those sites damage
// the window only when something actually changed (bare Home / End and the
// waveform placement press — a face swap costs a repaint, and nothing swapping
// costs none). Idempotent, and a no-op with the mode down: both fields already
// rest empty there, the whole-struct reset at close_history_mode having put them
// so.
inline bool clear_history_mode_focus(AppState::HistoryMode& mode) {
    const bool stood = (mode.focus != -1) || !mode.selection.empty();
    mode.focus = -1;
    mode.selection.clear();
    return stood;
}

// IS THERE ANYTHING FOR THE REVERT ACT TO ACT ON? — the act's SUBJECT in one
// word (architect 2026-08-05): the selected diff flags, else the focused one
// alone. Empty means Ctrl+H is a consumed no-op and the Revert button greys, and
// that is ONE decision serving both readers, on the head-delta precedent
// exactly: history_mode_key_blocked's admission is conditional on this, and the
// button's face is derived from that admission rather than from a second
// spelling of it.
//
// IT IS A PURE READ OF THE MODE, so the face answers per frame with nothing
// latched: a click that selects lights the button on the next frame and a step
// that clears greys it again, both through the clearer above.
inline bool history_mode_revert_subject_standing(
        const AppState::HistoryMode& mode) {
    return !mode.selection.empty() || mode.focus >= 0;
}

// IS THIS DROPDOWN ITEM LIVE? — the menus' ONE per-item disabled state, and the
// one predicate every reader of it goes through: the painter (which draws the
// greyed inks and no hover or press face), the popup's press claim, its hover
// recompute and its release body. Geometry is deliberately NOT in here — an item
// keeps its row, its rect and its place in the layout whether it greys or not
// (kdenlive's disabled rows do), so dropdown_item_at stays the one geometric
// answer and this is the one enablement answer, asked beside it.
//
// IT HAS EXACTLY ONE PRODUCER (architect 2026-08-08): the Navigation menu's
// "Walk both tabs" row while the `h` history view stands, where Ctrl+Shift+Tab is
// the mode's own reverse walk-tab cycle rather than the walk the label
// promises. The argument for greying it — and for every other row on both menus
// staying live — is at kNavigationPopupItems above. The SETTINGS menu has no
// producer at all and answers true throughout: it does not open in that view (its
// anchor is refused at toggle_dropdown), and outside it its six items keep the
// never-grey rule, their own refusals answering.
//
// THE ROW IS IDENTIFIED BY ITS CHORD, not by its table position, so reordering
// kNavigationPopupItems cannot silently grey a different command; the alt term is
// spelled with the others because the chord's shape is what is being named, and
// because the mode refuses alt outright (history_mode_owns_key).
inline bool dropdown_item_enabled(const AppState& a, DropdownMenu menu, int i) {
    if (menu != DropdownMenu::Navigation) return true;
    if (!a.history_mode.active) return true;
    if (i < 0 || i >= kNavigationPopupItemCount) return true;
    const NavigationPopupItem& it =
        kNavigationPopupItems[static_cast<std::size_t>(i)];
    return !(it.ctrl && it.shift && !it.alt && it.key == GuiKeys::Tab);
}

// WOULD THIS BUTTON'S ACT BE CONSUMED BY THE `h` HISTORY VIEW? True for exactly
// the buttons the view refuses, false for the ones that still work in it.
// DERIVED FROM THE GATES, never hand-listed — the definition (input_pointer.cpp,
// beside the chord table it walks) asks history_mode_key_blocked about each
// button's own chord and hand-answers the TWO ANCHORS, which have none — Settings
// dead on the toggle_dropdown lockout, Navigation live since 2026-08-08, its menu
// opening in the view — and IT CARRIES THE AUTHORITATIVE PARTITION INVENTORY. Read
// only while the mode stands (the caller below tests that), so it says nothing
// about any other state.
//
// IT TAKES THE WHOLE AppState because the gate it asks does: THREE of that
// gate's admissions are conditional on state (re-derived 2026-08-07 — the commit
// act's, on head_delta_empty and on history_checkpoint_in_flight, and the revert
// act's, on history_mode_revert_subject_standing above), so both readers must
// hand it the SAME state or the face
// and the key would answer differently. The caller passes `a` and
// restates none of its terms.
bool history_mode_disables_button(const AppState& app, RedesignButton b);

// THE REDESIGNED BUTTONS' ENABLED PREDICATE — one owner for the DISABLED FACE
// (row 2's third face, and every row's while the history view stands) and for
// hoverability, mirroring each chord's OWN refusals rather than inventing a
// policy. Three readers: the painter (which stashes what it painted), the press
// claim (a disabled press is a consumed nothing — the chord is not dispatched),
// and main.cpp's staleness comparator.
//
// WHAT EACH ENTRY MIRRORS, read off the routes themselves:
//   * ALL FOUR row-2 chords drop at on_key's `app.loading || total <= 0` guard
//     (input_handler.cpp). THE PER-TAB READ-ONLY GATE NOW SPLITS THEM (architect
//     2026-08-07): read-only protects the AUTHORED MUSICAL CONTENT — the marker
//     stores and the engine settings — so its allowlist
//     (read_only_key_blocked, input_key_dispatch.cpp) admits Ctrl+S and
//     Ctrl+Alt+R (a save writes the state the tab already holds; a render reads
//     it) while still dropping Ctrl+Z and Ctrl+Shift+Z. So a locked tab greys
//     UNDO AND REDO ALONE and leaves Save and Render live, which is the truth
//     the keys have — the term lives in the per-button switch below rather than
//     as a blanket line, because it is no longer a blanket fact. (It was one
//     until this ruling, when "a locked tab greys the whole toolbar" was
//     recorded here as code truth.)
//   * Undo / Redo additionally take history_step_actionable on their own stack
//     — the exact guard do_undo / do_redo run.
//   * Save takes BOTH of its route's stable-state refusals (GuiSaveOps::save):
//     an empty warpmarkers_path, and — since 2026-08-08 — a CHECKPOINT IN
//     FLIGHT, the global save lockout that keeps the checkpoint worker's writes
//     from racing a concurrent save of the same three paths. Its OTHER refusal
//     — a numeric locale that is no longer "C" — is deliberately NOT here: that
//     is a mid-session dynamic fault, not stable state, and greying a button on
//     it would hide the one stderr line that reports it.
//   * Render takes Ctrl+Alt+R's own first line, an empty source_audio_path. Its
//     history-view grey needs no arm here and never did: in the view both render
//     chords are off the allowlist (2026-08-08), so the mode line at the top of
//     this body answers for it through the derived partition, exactly as it does
//     for Undo, Redo and the rest of the consumed roster.
//   * Row 1's Quit and row 3's tabs answer true HERE: Quit keeps its two faces
//     by ruling, and a tab has no disabled face of its own. Their entries exist
//     so the vector is total over the roster and the comparator needs no
//     membership test. (The tabs answer true in EVERY state since 2026-08-05:
//     the history view, which greyed them for one day, repurposes the row as
//     its WALK selector instead, and the chord it gave that selector —
//     Ctrl+Tab, the mode's own cycle over the walk sources — is what makes the
//     derived partition call them LIVE, so the mode line at the top of this
//     body never fires for them either, and row 3 has no disabled face at all.)
// MODAL gates are deliberately absent: a prompt or a bottom-strip editor
// swallows the PRESS at the pointer path's own modal gate, and a modal that
// greyed the chrome under it would be a fourth face nobody asked for.
// THE FIRST SWITCH IS EXHAUSTIVE over the roster with NO `default` arm,
// deliberately: a new button then fails to compile here (-Wswitch) until it is
// classified, instead of silently inheriting some other button's answer. The
// second switch can take a `default` because the first has already returned for
// every id that is not one of row 2's four.
inline bool redesign_button_enabled(const AppState& a, int64_t total_frames,
                                    RedesignButton b) {
    // THE `h` HISTORY VIEW IS THE ONE MODE-SCOPED EXCEPTION TO THE ROWS' FACE
    // SCOPES (architect 2026-08-04): while it stands, EVERY button whose act the
    // view consumes wears its row's disabled face and ignores the pointer, and
    // the ones that stay lit are exactly the ones that still work. It is ranked
    // FIRST because it outranks every row's own answer below — rows 1, 3 and 4
    // return true unconditionally there — and because the partition is about the
    // MODE rather than about the button's own chord refusals, which still decide
    // everything else. The reasoning is the roster's standing one: a face that
    // says nothing while the press does nothing is the drift the enabled
    // predicate exists to prevent. Whose act is consumed is DERIVED from the
    // mode's own gates (history_mode_disables_button, above), so this line
    // cannot fall out of step with the allowlist.
    if (a.history_mode.active &&
        history_mode_disables_button(a, b)) {
        return false;
    }
    switch (b) {
        // Rows 1, 3 and 4 have NO DISABLED FACE OF THEIR OWN — row 4 by the
        // architect's design (he provided five states and no disabled one), rows
        // 1 and 3 by their face scope. (THREE of row 4's fifteen are the ruled
        // exception, and they are the arm below this one: the walk's older /
        // newer steps and the revert act rest disabled because their keys are
        // bound only inside the history view. The rule stated here is still the
        // row's — the exception is named where it lives, not counted into this
        // arm.) Their presses
        // always dispatch and the CHORDS' OWN refusals answer: the read-only
        // gate blocks the authoring
        // ones, the loading gate blocks everything, each arm keeps its own
        // guards. Inherited through on_key, never mirrored here — which is why
        // these are a plain `return true` and not a second copy of those gates.
        // (The history view above is the one thing that greys them, and it is
        // scoped to that mode: leave the view and these rows answer true again
        // on the very next frame, no latched state anywhere.)
        //
        // THE VIEW BAR'S "DISABLED" CROPS ARE THE UNFOCUSED WINDOW (architect
        // 2026-08-02), not a disabled button: they are the row-1/2 ground swap's
        // sibling on app.window_activated, a PAINT-ONLY variant of the whole
        // bar, and never this bit. So the row-1 claim above stays true in its own
        // terms — no button on this row has a disabled face of its own — and the
        // three join the same arm. THE VIEW BAR STAYS LIVE IN THE HISTORY VIEW
        // (its 1/2/3 are on the mode's allowlist), so the two faces never meet
        // there either.
        case RedesignButton::Quit:
        case RedesignButton::Settings:
        case RedesignButton::Navigation:
        case RedesignButton::ViewSW:
        case RedesignButton::ViewTP:
        case RedesignButton::ViewTW:
        case RedesignButton::TabA:
        case RedesignButton::TabB:
        case RedesignButton::IconS:
        case RedesignButton::IconT:
        case RedesignButton::IconW:
        case RedesignButton::IconP:
        case RedesignButton::IconCopy:
        case RedesignButton::IconPaste:
        case RedesignButton::IconBpm:
        case RedesignButton::IconIter:
        case RedesignButton::IconFollow:
        case RedesignButton::IconListen:
        case RedesignButton::IconLoadInPlace:
        // THE HISTORY BUTTON MIRRORS NOTHING EITHER, and its gates are worth
        // naming because the temptation to mirror them is real: `h` refuses
        // while audio is loading or absent (on_key's own blank-state return,
        // above every dispatch) and the mode refuses to open when the git walk
        // finds no history — and that second answer is NOT KNOWABLE PER FRAME.
        // It costs subprocesses to ask, the row repaints on every hover, and the
        // refusal is already a consumed no-op with its own stderr line. So the
        // button joins the row's arm on the row's own terms.
        case RedesignButton::IconHistory:
            return true;
        // THE WALK'S TWO STEPS ARE THE ROW'S SECOND FACE EXCEPTION, and the
        // only one that is not mode-SCOPED but mode-INVERTED (architect
        // 2026-08-05): they REST DISABLED and come alive inside the history
        // view. Every other button on this row mirrors nothing because its
        // chord always means something and its own refusals answer; bare `,`
        // and bare `.` are bound in exactly one place in the product
        // (handle_history_mode_key, the only reader of either key),
        // so outside the view there is no act to refuse, and a live face would
        // promise one. The bit is the mode itself, which is why this arm is a
        // plain read of it rather than a mirror of any gate.
        //
        // THE MODE LINE AT THE TOP OF THIS BODY NEVER FIRES FOR THEM: their
        // chords are the view's OWN vocabulary (history_mode_owns_key admits
        // bare `,` and `.`), which the derived partition asks first, so
        // history_mode_disables_button answers LIVE and this arm decides.
        //
        // THEY DO NOT GREY AT THE WALK'S WALLS, deliberately: stepping past
        // the oldest or newest checkpoint is a consumed no-op on the keyboard
        // (handle_history_mode_key returns having done nothing), and a click at
        // a wall is the same consumed nothing. Two states are what this button
        // has to say — the view is open or it is not — and a third that tracked
        // the walk index would repaint the row on every step to report
        // something the walk itself already shows (the `n/N` corner readout).
        case RedesignButton::IconHistoryOlder:
        case RedesignButton::IconHistoryNewer:
        // THE REVERT BUTTON TAKES THE SAME INVERTED REST (2026-08-05) and for
        // the same reason: Ctrl+H is bound inside the view and nowhere else, so
        // outside it there is no act for a live face to promise. ITS IN-VIEW
        // GREY IS NOT DECIDED HERE, though, and that is the point of leaving
        // this arm a plain read of the mode: with no diff flag selected the
        // view's allowlist stops admitting the chord, so the MODE LINE at the
        // top of this body has already returned false through the derived
        // partition — the same one decision that refuses the key. This arm is
        // reached only when the act would act.
        case RedesignButton::IconRevert:
        // THE CUMULATIVE TOGGLE IS THE FAMILY'S FOURTH (2026-08-08), on the
        // walk steps' own terms: bare `u` is bound in exactly one place
        // (handle_history_mode_key, the only reader of that key), so outside
        // the view there is no reading to switch and a live face would promise
        // one. Its chord is the mode's OWN vocabulary too, so the mode line at
        // the top of this body never fires for it either and this arm decides.
        //
        // ITS SELECTED FACE IS DELIBERATELY NOT SCOPED THIS WAY: the reading is
        // a session preference (AppState::history_cumulative) and outlives
        // every visit, so the button reports it in EVERY view — a DISABLED
        // button wearing the SELECTED fill, which this row already composes
        // with no special case. The icon row's one dead face is a MIX toward
        // the row ground applied to whatever the button was going to wear
        // (kRedesignDisabledMix over fill, outline and glyph alike), so a dead
        // selected toggle comes out muted rather than blank — the painter
        // states that rule for iteration and follow, and this button is the
        // first to wear it at REST rather than only inside the view.
        case RedesignButton::IconCumulative:
            return a.history_mode.active;
        case RedesignButton::Save:
        case RedesignButton::Undo:
        case RedesignButton::Redo:
        case RedesignButton::Render:
            break;
    }
    if (a.loading || total_frames <= 0) return false;
    // THE READ-ONLY TERM IS UNDO'S AND REDO'S ALONE since 2026-08-07 (it stood
    // here as a blanket line over all four until then): the gate admits Ctrl+S
    // and Ctrl+Alt+R in a locked tab, so greying their buttons would be the face
    // promising less than the key delivers — the exact drift this predicate
    // exists to prevent. It stays a mirror of the gate, one arm per chord.
    switch (b) {
        // SAVE'S SECOND TERM IS THE PUBLISHING CHECKPOINT (2026-08-08), and it
        // is GLOBAL rather than mode-scoped because the act outlives the view it
        // was launched from: while the worker writes the three sidecars into
        // projects/<id>/, every save is refused at the one save owner
        // (GuiSaveOps::save, which states why), so this arm is that refusal's
        // mirror exactly as the read-only terms below mirror the key gate. The
        // face it produces is the "Committing..." label's own state, and the
        // per-tick drift comparator (main.cpp) is what repaints the row on both
        // edges of the bit with no damage call at either.
        case RedesignButton::Save:
            return !a.warpmarkers_path.empty() &&
                   !a.history_checkpoint_in_flight;
        case RedesignButton::Undo:
            return !active_view_state(a).read_only &&
                   history_step_actionable(a, a.history.undo_stack);
        case RedesignButton::Redo:
            return !active_view_state(a).read_only &&
                   history_step_actionable(a, a.history.redo_stack);
        case RedesignButton::Render:
            return !a.source_audio_path.empty();
        default:
            break;   // unreachable: the switch above returned for every other id
    }
    return true;
}

// THE TOGGLED-ON ("selected") FACE'S PREDICATE — row 1's three view-bar
// buttons, row 3's tabs and row 4's four radio/two toggle buttons, each reading
// THE SAME live fact its chord flips, so a lit button and the state it reports
// can never drift. Three readers: the painter (which stashes what it painted),
// the press claim's RADIO refusal (a radio button already selected is a consumed
// nothing — the two reasons that can make it so are at the flag's declaration,
// input_pointer.cpp), and main.cpp's staleness comparator.
//
// MOMENTARY BY DESIGN, and therefore false here: Copy, Paste, Listen,
// Load-in-place —
// each is an action that completes, with no state to stay lit for — and BPM,
// whose editor is a transient modal SESSION rather than a resting mode (it
// cannot rest open, and `m` never reaches dispatch while it is up), so lighting
// it would advertise a mode this product does not have.
inline bool redesign_button_selected(const AppState& a, RedesignButton b) {
    // ROW 3'S TABS ARE THE WALK SELECTOR WHILE THE `h` VIEW STANDS (architect
    // 2026-08-05 for the repurposing, 2026-08-08 for what it selects) — the
    // Render-button hijack applied to a whole row: the surface is repurposed,
    // not duplicated, so the selected face marks the live WALK SOURCE rather
    // than the live tab. Ranked first for the same reason the Save label's
    // history arm is: the view is the outer mode, and the A/B tab it hides
    // cannot move in here anyway — the tab chord is the walk cycle there.
    //
    // TWO SLOTS, ONE AXIS: "Remote" is the committed checkpoint walk and
    // "Local" the session timeline, so exactly one is ever lit and the radio
    // rule falls out of the pair being a pair. THE READING IS NOT HERE — it
    // left the row on 2026-08-08 for row 4's Cumulative toggle, which reads the
    // session bit below and needs no mode term at all.
    if (a.history_mode.active) {
        const bool local =
            a.history_mode.source == GuiHistoryWalkSource::Local;
        if (b == RedesignButton::TabA) return !local;
        if (b == RedesignButton::TabB) return  local;
    }
    switch (b) {
        // THE VIEW BAR READS THE LIVE COMBINATION — both axes at once, which is
        // what an ABSOLUTE selector reports — so a button lights however the
        // state was reached: `t`, `p`, a digit, or one of these three. AT MOST
        // ONE IS EVER LIT, and S+P lights NONE: that fourth combination is
        // deliberately keyless, so the bar has no button to give it and all
        // three read false there. That is the honest face, not a gap — an unlit
        // bar says "you are in the combination none of these selects".
        case RedesignButton::ViewSW:     return a.active_audio_view   == 'S' &&
                                                a.active_markers_view == 'W';
        case RedesignButton::ViewTP:     return a.active_audio_view   == 'T' &&
                                                a.active_markers_view == 'P';
        case RedesignButton::ViewTW:     return a.active_audio_view   == 'T' &&
                                                a.active_markers_view == 'W';
        case RedesignButton::TabA:       return a.active_tab_view     == 'A';
        case RedesignButton::TabB:       return a.active_tab_view     == 'B';
        case RedesignButton::IconS:      return a.active_audio_view   == 'S';
        case RedesignButton::IconT:      return a.active_audio_view   == 'T';
        case RedesignButton::IconW:      return a.active_markers_view == 'W';
        case RedesignButton::IconP:      return a.active_markers_view == 'P';
        case RedesignButton::IconFollow: return a.follow_mode;
        case RedesignButton::IconIter:   return a.iteration_mode_enabled;
        // The iteration button's pattern exactly: a TOGGLE reading the live bit
        // its own chord flips, so the lamp and the mode cannot drift.
        case RedesignButton::IconHistory: return a.history_mode.active;
        // THE CUMULATIVE READING'S LAMP (2026-08-08), the same pattern over a
        // bit that is NOT the mode's: history_cumulative is a program-session
        // preference, so this reads true wherever the session left it and the
        // button's own resting-disabled face is what says the key is elsewhere.
        // Publishing it unconditionally is the point — a mode term here would
        // make the row lie about the reading the moment the view closed.
        case RedesignButton::IconCumulative: return a.history_cumulative;
        case RedesignButton::Quit:
        case RedesignButton::Settings:
        case RedesignButton::Navigation:
        case RedesignButton::Save:
        case RedesignButton::Undo:
        case RedesignButton::Redo:
        case RedesignButton::Render:
        case RedesignButton::IconCopy:
        case RedesignButton::IconPaste:
        case RedesignButton::IconBpm:
        case RedesignButton::IconListen:
        case RedesignButton::IconLoadInPlace:
        // THE REVERT BUTTON IS MOMENTARY TOO, and more plainly than the arrows:
        // it is an ACT, not a mode — it runs once and closes the view — so
        // there is no bit for a lamp to read. What it has to say about state it
        // says with its enabled face, which greys when nothing is selected.
        case RedesignButton::IconRevert:
        // THE WALK'S TWO STEPS ARE MOMENTARY like copy and paste, not toggles
        // like follow and iteration: each is a step that completes, with no
        // state to stay lit for. WHERE the walk stands is the corner readout's
        // `n/N`, which says it in numbers; a lit arrow could only mean "you
        // pressed this", which the click face already says for as long as it is
        // true.
        case RedesignButton::IconHistoryOlder:
        case RedesignButton::IconHistoryNewer:
            break;
    }
    return false;
}

// THE SHIFT-AUGMENTED BUTTONS — the ONE owner of "this button's chord comes in
// a pair the keyboard already spells, so a SHIFT-exact press reaches the twin".
// FOUR carry it, each for that one reason: Render (Ctrl+Alt+R renders beside the
// source, Ctrl+Alt+Shift+R into a numbered _miscellaneous cell), Paste
// (Ctrl+Alt+P pastes phase resets, Ctrl+Alt+Shift+P pastes with state) and — since
// 2026-08-07 — THE WALK'S TWO ARROWS, whose shifted twins are the walk's WALL
// JUMPS: bare `,` steps one checkpoint older and Shift+`,` goes to the oldest,
// bare `.` steps one newer and Shift+`.` goes to the newest
// (handle_history_mode_key, input_key_dispatch.cpp, owns both shapes; the arrows
// dispatch them through the one press body like every other button).
//
// THIS STAYS THE STRUCTURAL FACT — "the keyboard spells a twin for this chord"
// — and is therefore stateless. Render's twin does NOTHING in iteration mode
// (Ctrl+Alt+Shift+R is a consumed no-op there, refused inside the render route),
// but the shift press still routes to it and is still swallowed by the one
// refusal rather than by a second gate here. What follows the mode is the
// advertised FACE: the tooltip's shift line, at the override below.
//
// It lives here rather than as a column in the press claim's chord table
// because it has TWO readers that must not drift: that table's shift rule, and
// the TOOLTIP — the shift hint exists exactly where a shift press does
// something, so "which buttons admit shift" and "which buttons advertise it"
// are one fact by construction rather than two lists to keep in step.
inline constexpr bool redesign_button_shift_admits(RedesignButton b) {
    return b == RedesignButton::Render || b == RedesignButton::IconPaste ||
           b == RedesignButton::IconHistoryOlder ||
           b == RedesignButton::IconHistoryNewer;
}

// THE HOVER TOOLTIP'S TEXT — name and chord, kdenlive's pattern, one row per
// button that has one. It sits with the roster (rather than with the chord
// table in input_pointer.cpp) because BOTH the painter and the pointer read it,
// and because membership is the interesting part: a null `line1` means "this
// button has no tooltip", and the buttons that carry none are the WHOLE MENU
// ROW — stated as the row rather than as a count, so a button added to row 1
// inherits the exclusion instead of falsifying a number. The switch's null arms
// and redesign_button_in_menu_row's true arms are the same six names.
//
// THE MENU ROW CARRIES NO TOOLTIPS, and that is the RULE rather than a list of
// names (architect 2026-07-31): row 1's buttons are word labels that already
// say what they do — "Quit" quits, "Settings" and "Navigation" open menus that
// name themselves — so a hint repeating the label would be noise. Stating it as
// the ROW's property is what let Navigation inherit the exclusion in 2026-08-02
// without being remembered. Every button on rows 2, 3 and 4 has one; its icon or
// single letter is not self-describing.
//
// The names follow HELP's vocabulary so the hint and the manual agree.
//
// `line2` is the SHIFT LINE and is non-null on exactly the shift-admitting
// buttons, which is not a coincidence to be maintained: it is asserted against
// redesign_button_shift_admits below, so the hint cannot advertise a shift press
// that does nothing (or stay silent about one that does).
struct RedesignTooltipText {
    const char* line1;   // nullptr -> no tooltip at all
    const char* line2;   // nullptr -> the one-line form
};
inline constexpr RedesignTooltipText redesign_button_tooltip(RedesignButton b) {
    switch (b) {
        // Row 1 — the menu row: no tooltips, per the rule above. The view bar's
        // three joined the exclusion with the row (2026-08-02): their labels are
        // the combinations themselves, so a hint could only restate them.
        case RedesignButton::Quit:
        case RedesignButton::Settings:
        case RedesignButton::Navigation:
        case RedesignButton::ViewSW:
        case RedesignButton::ViewTP:
        case RedesignButton::ViewTW:     return {nullptr, nullptr};
        case RedesignButton::Save:       return {"Save (Ctrl+S)", nullptr};
        case RedesignButton::Undo:       return {"Undo (Ctrl+Z)", nullptr};
        case RedesignButton::Redo:       return {"Redo (Ctrl+Shift+Z)", nullptr};
        // THE SHIFT LINE NAMES THE OTHER FUNCTION (architect 2026-07-31), not
        // "for more": a hint that does not say what it gets you is not a hint.
        // It is also the standing no-gesture-hints preference's ONE ruled
        // exception, scoped to exactly the shift-admitting buttons — this one,
        // Paste, and the walk's two arrows since 2026-08-07.
        case RedesignButton::Render:     return {"Render (Ctrl+Alt+R)",
                                                 "Press Shift for miscellaneous render."};
        // THE PADLOCK GETS NO TOOLTIP OF ITS OWN, and the tab's does not
        // mention it (planner's call, 2026-08-01). Two reasons, both about
        // keeping this table's shape: a lock icon in a tab's close slot is
        // self-evident, and a tooltip whose TEXT moved with state would be the
        // only stateful string here — every other row is a constant naming a
        // constant chord. The lock's key is `o`, which HELP carries.
        case RedesignButton::TabA:       return {"Tab A (Ctrl+Tab)", nullptr};
        case RedesignButton::TabB:       return {"Tab B (Ctrl+Tab)", nullptr};
        case RedesignButton::IconS:      return {"Source view (T)", nullptr};
        case RedesignButton::IconT:      return {"Target view (T)", nullptr};
        case RedesignButton::IconW:      return {"Warp markers (P)", nullptr};
        case RedesignButton::IconP:      return {"Phase resets (P)", nullptr};
        case RedesignButton::IconCopy:   return {"Copy phase resets (Ctrl+P)", nullptr};
        case RedesignButton::IconPaste:  return {"Paste phase resets (Ctrl+Alt+P)",
                                                 "Press Shift for paste phase state."};
        case RedesignButton::IconBpm:    return {"BPM editor (M)", nullptr};
        case RedesignButton::IconIter:   return {"Iteration mode (I)", nullptr};
        case RedesignButton::IconFollow: return {"Follow (F)", nullptr};
        case RedesignButton::IconListen: return {"Listen to renders (L)", nullptr};
        case RedesignButton::IconLoadInPlace:
            // "Load in place" not "Load render in place": the act loads A
            // STATE — a renders/ entry's sidecar set (the render name is
            // only the match key) or, in the history view, a commit's
            // sidecars or a member of the session's own timeline — so naming
            // "render" overclaims the surface.
            return {"Load in place (')", nullptr};
        // HELP's own vocabulary for the mode ("Checking history"), one line: the
        // key toggles and there is no shifted twin.
        case RedesignButton::IconHistory: return {"History (h)", nullptr};
        // THE CUMULATIVE TOGGLE, one line: the key toggles and has no shifted
        // twin. Like the three below it, the hint shows on the dead face it
        // wears outside the view — which is where a user most needs to be told
        // what the button is and that the history view is where it lives.
        case RedesignButton::IconCumulative:
            return {"Cumulative (U)", nullptr};
        // THE WALK'S TWO STEPS, in the TWO-LINE form since 2026-08-07: their
        // shifted twins jump to the walk's walls, so the hint says so — the same
        // rule the static_assert below states, met by two more buttons. The
        // shift line deliberately does not name the member kind since
        // 2026-08-08: the Local walk's members are states of the session's own
        // undo/redo timeline, not checkpoints, so "checkpoint" would lie on half
        // the surface these arrows serve.
        // THEY ARE HOVERABLE HINTS EVERYWHERE, including outside the history
        // view where the pair rests disabled (architect 2026-08-07, kdenlive's
        // own behavior: a disabled icon still explains itself). The hint is what
        // tells a user what the greyed arrows would do and where they work; the
        // dead FACE is untouched, and so is the inert press.
        case RedesignButton::IconHistoryOlder:
            return {"Older (,)", "Press Shift for oldest."};
        case RedesignButton::IconHistoryNewer:
            return {"Newer (.)", "Press Shift for newest."};
        // THE REVERT ACT, one line: the chord has no shifted twin. It rests
        // disabled outside the view like the two arrows, and is greyed inside it
        // whenever nothing is selected — in all three states it shows this hint,
        // per the same ruling.
        case RedesignButton::IconRevert: return {"Revert (Ctrl+H)", nullptr};
    }
    return {nullptr, nullptr};
}

// ROW 2'S TWO STATEFUL BUTTONS — the ONE row on this whole surface whose text
// follows STATE rather than being a constant (architect 2026-08-02 for Render,
// 2026-08-04 for the history face, MOVED ONTO SAVE 2026-08-08). Each is a chord
// whose MEANING is selected by a mode bit, and the button says whichever command
// it currently is:
//
//   SAVE, WITH THE HISTORY MODE STANDING → "Save and Commit", and the vcs-commit
//   icon with it: Ctrl+S there SAVES the piece beside its source through this
//   very button's ordinary act and then commits the live state into the projects
//   repository as a checkpoint (run_history_commit, input_key_dispatch.cpp, owns
//   the order and the refusal). The label carries both halves because the act
//   does. IT LIVES ON THIS BUTTON BECAUSE THE ACT IS SAVE-FIRST BY DEFINITION
//   (architect 2026-08-08, correcting the Render hijack it shipped under): a
//   surface that runs the save first belongs on the save's own slot, and Render
//   went back to being a render in every mode.
//
//   SAVE, WITH A CHECKPOINT PUBLISHING → "Committing...", the same commit icon,
//   DISABLED, in EVERY view (the act outlives the view it was launched from).
//   Ranked FIRST of the three because it is the outermost fact: while the worker
//   is writing the three sidecars no save may run at all (GuiSaveOps::save's own
//   term, whose mirror this face is). Three literal dots, not an ellipsis
//   character — the product's text is ASCII in every label.
//
//   RENDER, WITH ITERATION MODE ON → "Render Iterations", the sweep, and its own
//   one-line hint. The history mode gives Render NO face of its own any more: in
//   the view both render chords are consumed, so the button wears its ordinary
//   label and icon over the derived disabled face.
//
// THE TITLE CASE IS DELIBERATE AND SCOPED TO THESE TWO STRINGS (architect
// 2026-08-03 for the capital I, 2026-08-04 for the capital C beside it): every
// other multi-word GUI label in the product stays sentence case ("Playback
// speed", "Center on focus", "Next marker") — these two are the named
// exceptions, not a precedent to copy outward or "fix". The joining word stays
// LOWERCASE ("and"), which is what title case means and what the architect
// spelled. (The prompt that used to ask about the act was PROSE and took the
// ordinary sentence case; it is gone — the act asks for its commit MESSAGE now,
// through an editor whose prefix is the plain "Commit: " label — so the title
// case here is the button's alone.)
//
// "COMMITTING..." NEEDS NO EXCEPTION OF ITS OWN: it is one word, so sentence
// case and title case spell it identically.
//
// RENDER'S SHIFT LINE GOES WITH ITS ITERATION FACE, and that is the same fact
// rather than a second decision: Ctrl+Alt+Shift+R is a consumed no-op in
// iteration mode (the refusal is in the render route, input_key_dispatch.cpp),
// so advertising a shift press there would advertise nothing. The rule the
// static_assert below states — the hint exists exactly where a shift press does
// something — therefore holds on this form too, not only on the constant table
// it overrides. In the HISTORY VIEW the whole button is dead, hint included in
// the sense that it describes what the button does where it works — its two-line
// constant form, the resting-disabled family's own answer.
//
// ALL THREE STRINGS LIVE HERE, beside the constant table, so the label a button
// paints and the name its hint gives cannot drift into two different words.
inline constexpr const char* kRenderIterationsLabel = "Render Iterations";
inline constexpr const char* kSaveCommitLabel       = "Save and Commit";
inline constexpr const char* kSaveCommittingLabel   = "Committing...";
// ROW 3'S TWO WALK-SELECTOR WORDS, while the `h` view stands and the tabs
// select the walk instead of being the A/B pair (architect 2026-08-05 for the
// repurposing, 2026-08-08 for what the words say). Sentence case, the ordinary
// convention: these are ordinary labels and not row 2's two named title-case
// exceptions. They live beside the toolbar trio for the same reason that trio
// lives beside the constant table — one place where a stateful button's word is
// written.
//
// TWO WORDS, ONE AXIS: "Remote" is the committed checkpoint walk and "Local"
// the session's own undo/redo timeline. The words are the SURFACE's, not the
// model's — GuiHistoryWalkSource stays Commit | Local, the committed history
// being what a remote publishes.
//
// THE READING IS NOT ON THIS ROW. For one day (2026-08-07..08) it was: the row
// carried the (walk source, reading) product as four self-labelled tabs
// ("Iterative (Remote)" ... "Cumulative (Local)"), then briefly as two labelled
// groups with the reading said in a text block above each pair. The architect
// retired both on 2026-08-08 — the reading is row 4's Cumulative toggle now
// (RedesignButton::IconCumulative, bare `u`), a MODE bit rather than a
// selection — so the group-label constants and the text-block painter are
// deleted whole and the row is two cells again.
//
// THE WIDTH IS ABSORBED, checked rather than assumed (the row is one
// left-to-right accumulation of max(kTabMinWidthPx, shaped + 2*pad), no wrap
// and no clip): shaped at the product's one size the two cells measure 76 + 58
// = 134 px at 100% and 152 + 116 = 268 px at 200%, against the 1920 px window.
// "Local" sits at the minimum and "Remote" clears it by 18 px, so the two are
// deliberately unequal — a label-sized tab bar, which is what this walk has
// always been.
inline constexpr const char* kCompareRemoteLabel = "Remote";
inline constexpr const char* kCompareLocalLabel  = "Local";
inline RedesignTooltipText redesign_button_tooltip(const AppState& a,
                                                   RedesignButton b) {
    // THE WALK-SELECTOR TABS CARRY NO TOOLTIP, on the view bar's own reasoning
    // (row 1's three): their labels ARE the thing a hint would name, and the
    // live tabs' "Tab A (Ctrl+Tab)" would be a lie about the act. The CHORD is
    // no longer the reason it once was — Ctrl+Tab has selected in here since
    // 2026-08-05 — but one chord shared by both buttons is not a per-button
    // hint either, so the row stays silent.
    if (a.history_mode.active && redesign_button_is_tab(b)) {
        return {nullptr, nullptr};
    }
    // THE SAVE BUTTON'S TWO OVERRIDES, in the label's own rank order. A
    // publishing checkpoint outranks the view because it outlives it; inside the
    // view the button IS the act. Both are one-line forms: Save admits no shift
    // press in any state (redesign_button_shift_admits), so neither can grow a
    // second line. THE IN-FLIGHT HINT SHOWS ON A DEAD BUTTON, per the
    // 2026-08-07 tooltips-on-disabled ruling, and names what the button is doing
    // rather than what a press would do — there is no press here, and the face
    // already says as much.
    if (b == RedesignButton::Save && a.history_checkpoint_in_flight) {
        return {"Committing the checkpoint (Ctrl+S)", nullptr};
    }
    if (b == RedesignButton::Save && a.history_mode.active) {
        return {"Save and Commit (Ctrl+S)", nullptr};
    }
    // RENDER HAS NO HISTORY-VIEW HINT since 2026-08-08: the act left this button
    // with its chord, so in the view Render is an ordinary dead button and shows
    // its ordinary two-line hint — what it does and where it works, which is the
    // same thing the four resting-disabled row-4 buttons show.
    if (b == RedesignButton::Render && a.iteration_mode_enabled) {
        return {"Render Iterations (Ctrl+Alt+R)", nullptr};
    }
    return redesign_button_tooltip(b);
}

// A BUTTON'S LABEL, by the same bits and for the same reason. The constant
// per-button labels live with the painters' roster halves (kToolbarButtons and
// kTabs, paint_handler.cpp); this answers only "does this button override its
// own", which FOUR now do — the SAVE button on two bits (a publishing
// checkpoint, then the history view), the Render button on the iteration bit,
// and row 3's two tabs on the history view's. LABEL MEMBERSHIP IS UNCHANGED by
// any override: a button with a label keeps one in every mode. (The TOOLTIP
// override's membership does move, once: the walk-selector tabs drop theirs, on
// the view bar's own reasoning — the sibling function above states it.)
inline const char* redesign_button_label(const AppState& a, RedesignButton b,
                                         const char* table_label) {
    // THE TABS ARE THE WALK SELECTOR IN THE `h` VIEW, so they say which walk
    // they select rather than which tab they are; the READING is row 4's
    // Cumulative toggle since 2026-08-08 and has no label on this row at all.
    // The shaped-run layout each painter does absorbs the width change (both
    // words are wider than "A"/"B", and the tab's width has always been
    // max(minimum, shaped + 2*pad)). The order is the enum's, which is the
    // painted one, and the same one the press claim and the Ctrl+Tab cycle
    // read: Remote then Local.
    if (a.history_mode.active) {
        if (b == RedesignButton::TabA) return kCompareRemoteLabel;
        if (b == RedesignButton::TabB) return kCompareLocalLabel;
    }
    // THE SAVE BUTTON'S THREE STATES, ranked outermost first: a publishing
    // checkpoint (global — the act outlives the view), then the view itself,
    // then the plain save from the table.
    if (b == RedesignButton::Save && a.history_checkpoint_in_flight) {
        return kSaveCommittingLabel;
    }
    if (b == RedesignButton::Save && a.history_mode.active) {
        return kSaveCommitLabel;
    }
    if (b == RedesignButton::Render && a.iteration_mode_enabled) {
        return kRenderIterationsLabel;
    }
    return table_label;
}

// THE SHIFT LINE EXISTS EXACTLY WHERE A SHIFT PRESS DOES SOMETHING. Checked at
// compile time so the two tables cannot drift: a button that gains a shifted
// chord without gaining the line (or the reverse) fails to build here.
static_assert(
    (redesign_button_tooltip(RedesignButton::Render).line2 != nullptr) ==
        redesign_button_shift_admits(RedesignButton::Render) &&
    (redesign_button_tooltip(RedesignButton::IconPaste).line2 != nullptr) ==
        redesign_button_shift_admits(RedesignButton::IconPaste) &&
    (redesign_button_tooltip(RedesignButton::IconHistoryOlder).line2 !=
     nullptr) ==
        redesign_button_shift_admits(RedesignButton::IconHistoryOlder) &&
    (redesign_button_tooltip(RedesignButton::IconHistoryNewer).line2 !=
     nullptr) ==
        redesign_button_shift_admits(RedesignButton::IconHistoryNewer) &&
    (redesign_button_tooltip(RedesignButton::Save).line2 == nullptr) &&
    (redesign_button_tooltip(RedesignButton::IconCopy).line2 == nullptr),
    "the shift hint and the shift binding must name the same buttons");

// THE HOVER ZONE — "the pointer is over this button in a way the surface
// answers at all", which is hoverability WITHOUT the enabled term. It exists
// because the two things a hover produces stopped agreeing on that one term
// (architect 2026-08-07): the FACE still refuses on a disabled button, the
// TOOLTIP no longer does. Everything else the two share — the open dropdown, the
// selected tab — is stated once, here, so the hint and the pill can differ in
// exactly the one way that was ruled and in no other.
//
// AN OPEN DROPDOWN OWNS THE POINTER, AND NO ROSTER BUTTON HOVERS UNDER IT. A lit
// button beside an open menu would advertise a click the popup is about to
// swallow (rows 2, 3 and 4, which it floats over) or a second lit button in a row
// that shows one at a time (row 1) — and a HINT under an open menu is the
// two-floating-surfaces rule, which this same term is what makes structural.
//
// ROW 1 HELD A BRIEF EXEMPTION and it is retired (architect 2026-08-03): the
// row-1 close rule (on_motion, input_pointer.cpp) means a pointer can no
// longer BE over a non-anchor row-1 button while a menu is up — the motion
// that arrives there closes the menu first, and this predicate then answers
// for a closed popup on that same frame — while an ANCHOR's pill is the
// painter's own open condition (paint_menu_row), not this bit. So the
// exemption named no case the close rule does not already own, and one
// mechanism per behaviour is the shape to keep.
//
// THE TAB CARVE-OUT FOLLOWS THE SELECTED BIT, not the tab letter, which is what
// carries it into the history view for free: in there the row selects the WALK
// SOURCE and the lit one is still the one with no hover face. Both slots take
// it, in both meanings of the row, with no mode term anywhere in the
// expression. It sits in the ZONE rather than in hoverability alone because the
// tabs carry no tooltip either way while the view stands (their null rows are
// membership), so no behaviour rests on the distinction and one statement is
// better than two.
inline bool redesign_button_hover_zone(const AppState& a, RedesignButton b) {
    if (a.dropdown.open()) return false;
    if (redesign_button_is_tab(b)) return !redesign_button_selected(a, b);
    return true;
}

// Hoverability = the zone plus ENABLED: a disabled button never sets `hovered`
// and therefore never wears a hover face. Consulted only by the hover recompute,
// so the refusal is one line at one site rather than a condition smeared over
// the painter.
//
// ROW 4'S AND THE VIEW BAR'S SELECTED BUTTONS DO HOVER, and that asymmetry with
// the tabs is the crops': both ship a selected-hover state (the accent outline
// over the selected fill) and row 3 does not. So the zone's carve-out names the
// tabs alone; the icon row's radios and the view bar's three are hoverable in
// both states, and their already-selected press is refused in the ACTION (the
// chord table's `radio` flag), not in their hoverability.
inline bool redesign_button_hoverable(const AppState& a, int64_t total_frames,
                                      RedesignButton b) {
    return redesign_button_hover_zone(a, b) &&
           redesign_button_enabled(a, total_frames, b);
}

// Snapshot the undo-tracked settings from `app` (engine_settings; trim is
// excluded). Called by Undo's push helpers at push time so every entry
// carries-everywhere; also called by do_undo / do_redo when constructing the
// inverse entry. Body in app_state.cpp.
SettingsSnapshot capture_current_settings(const AppState& app);

// Promoted from lambdas in main(). Mode-aware hit-tests against
// the visible marker / flag / popup geometry. Bodies live in app_state.cpp
// and pull in cairo + paint_handler.h for the popup-rect math; the
// signatures stay free of cairo so the header keeps a clean include list.
//
// hit_test_flag: return the marker index whose PAINTED FLAG BOX contains
// (mouse_x, mouse_y), or -1. It reads the painter's own stash
// (AppState::flag_hit_rects — the contract, including why a derived width has
// no second owner, is at the field) and tests plain rects: the fused tip-down
// triangle and its taper test died with the triangle lane in row 5, and so did
// the live rect rebuild. THE BOX INCLUDES ITS 1px LEFT BORDER (2026-08-02): the
// stash carries the painted extent, so the flag's reach grew one column to the
// LEFT of its frame column and a press on the border resolves the marker.
// Boxes OVERLAP freely (later over earlier in store
// order, the whole occlusion model), and the walk runs BACKWARDS so the topmost
// = last-painted box wins — WYSIWYG for every consumer (selection clicks, the
// drag grab, the double-click editor). Selection does not lift a box: it is a
// colour swap now, so there is no second pass. Works in both 'W' and 'P'
// authoring views — the stash holds the ACTIVE column's boxes only, because
// that is the column the painter drew.
int hit_test_flag(const AppState& app, const GuiAudio& audio,
                  int mouse_x, int mouse_y);

// THE STEM AS A POINTER TARGET (architect 2026-08-01, at the row-5 live test): a
// press within kMarkerStemGrabPx of a PAINTED stem's column, IN THE WAVEFORM'S
// UPPER HALF, is that item's click. Returns the stash's own index — a marker
// index on the live columns, a history diff-flag ordinal while the `h` mode
// stands — or -1.
//
// TWO CALL SITES (2026-08-05), each the plain upper-half press of its own lane's
// vocabulary: on_button_press (input_pointer.cpp), where a stem is the marker's
// second surface and routes through its flag's click bodies, and
// handle_history_mode_press, where a diff flag's stem routes through that flag's
// focus click. Neither restates the geometry; this function is the one owner of
// the half test, the tolerance and the arbitration.
//
// BOTH CALLERS GATE IT PLAIN-EXACT, WITH NO EXCEPTION ANYWHERE (architect
// 2026-08-01, made UNIVERSAL by the symmetry ruling of 2026-08-06) — stated here
// because it bounds what this function is for, and spelled at each call site:
// SHIFT and CTRL bind to the FLAG BOX ALONE, in the marker lane, in every view.
// Both modifiers already own a waveform gesture at the very pixels a stem stands
// on (ctrl = the strip drag, shift = the placement press since 2026-08-05), so a
// modified press near a stem is not a hit at all and falls through to the
// waveform underneath. The `h` history view's own modified arm asked this
// function for one day — its stem-based multi-select — and that arm is DELETED:
// waveform modifiers are gesture vocabulary and selection is lane vocabulary, in
// both views alike. This function is unchanged and unconditional; only its
// callers decide when to ask.
//
// UPPER HALF ONLY, and that is a structural fit rather than a compromise: the
// plain waveform press already splits by half — upper is playhead placement +
// the region-drag arm, lower is the one-shot scrub — so this claim slots into a
// seam that already exists. A full-height band would need a carve-out inside the
// LEFT press's scrub branch and would make it impossible to LEFT-scrub at a
// marker's column, which is exactly where a user scrubs most. (The BARE RIGHT
// press added 2026-08-01 scrubs at full height and so reaches a stem's column
// unobstructed — it never resolves a marker, the stem claim being left-press
// vocabulary. That relieves the cost this argument weighed but does not change
// the ruling: the half split is what keeps the LEFT press's two arms legible.)
//
// IT READS THE PAINTER'S STASH (AppState::marker_stems), so the grabbable stem
// is the DRAWN stem by construction — same column, same displayed basis — and a
// DISABLED marker, which publishes no stem entry at all, is not grabbable for
// the same reason it is not visible. One fact, not two. THE STASH ANSWERS FOR
// THE HISTORY LANE TOO, by that same construction: whichever diff classes that
// painter stems are exactly the ones a press can claim, with no second predicate
// deciding.
//
// NEAREST WINS, TIES TO LATER-IN-STORE. The stash is in paint order, so a tie
// resolving to the later entry is the same "topmost = last painted" rule
// hit_test_flag uses on the boxes — two overlapping stems answer the way the two
// overlapping flags above them do.
int hit_test_marker_stem(const AppState& app, int mouse_x, int mouse_y);

// The stem's grab half-width in AUTHORED pixels, per side. A 1px line is under
// any pointing tolerance, so the drawn stem and the grabbable stem are
// deliberately NOT the same column — the same deliberate difference the trim
// endcaps record (kTrimEndcapGrabPx), and stated here for the same reason:
// everywhere else in the redesign paint and hit are identical by construction.
inline constexpr int kMarkerStemGrabPx = 4;
inline int marker_stem_grab_px() {
    return scaled_px(kMarkerStemGrabPx, 0);
}

// Which trim boundary, if any, a waveform-area click lands on.
enum class TrimHit { None, Begin, End };

// hit_test_trim_endcap: return which trim bound's painted ENDCAP contains the
// press, or None. Both bounds are always meaningful (the trim window is always
// set since 2026-07-30), so it reads the pair directly.
// AUTHORING views — the active tab's live pair, project-level in both 'W' and
// 'P' views. Each bound's mark is one of row 5's trim-bar ENDCAPS (the square
// b/e chips of the old chip row are gone, 2026-08-01): a trim_endcap_w_px()
// column run spanning the trim bar lane's full height, EDGE-ANCHORED on the
// bound's painted column — the begin cap's LEFT edge on it, the end cap's RIGHT
// edge on it — from trim_endcap_rect, the ONE rect owner render_trim_flags fills
// through, so paint and hit cannot drift. THE HIT RECT IS THAT CAP INFLATED by
// kTrimEndcapGrabPx per side (a 2px cap is under any pointing tolerance); it is
// the one place in this lane where the drawn and the grabbable rect differ, and
// it is why two caps at nearby columns can overlap as targets at all (the
// arbitration is at the sort). Tests both mouse_x and mouse_y. Walks the
// display warp_frame_map in target view so the hit lands on the drawn cap.
// The endcaps and the bar's inter-cap bridge span are the ONLY trim grab
// handles (the waveform stem grab retired).
TrimHit hit_test_trim_endcap(const AppState& app, const GuiAudio& audio,
                           int mouse_x, int mouse_y);

// point_in_trim_bridge_span: is (mouse_x, mouse_y) on the trim bar's INTER-CAP
// BRIDGE — the painted bar's stretch between the two endcaps, the pair drag's
// handle? The endcap test's twin, and it is a shared owner for the same reason:
// TWO consumers ask this question and they must not answer it differently — the
// plain trim-bar press router (route_trim_bar_press, which arms the pair drag on
// a true) and the pointer cursor's zone map (pointer_cursor_kind, which shows the
// bridge's TrimResize cue on a true). It was the router's own inline body until
// the cursor needed the same verdict; hoisting it whole was the alternative to a
// second copy of the column math.
//
// The y-gate is top_trim_row_area, the same lane band hit_test_trim_endcap
// gates on. The interval is trim_bridge_gap (render.h — the one owner the
// painter's midpoint mark also fits against) over the two bounds'
// TrimBoundColumns on the DISPLAYED basis (item_viewport_basis +
// displayed_trim_ms through displayed_or_live_target_map), which is the exact
// owner chain the live trim pass paints the bar with, so the grabbable bridge is
// the drawn one. The [0, area_w) click gate is the PAINTER's own effective-width
// clip: the inert non-multiple-of-16 right gutter neither paints the bar nor
// answers true here.
//
// THE ENDCAPS ARE NOT IN IT: trim_bridge_gap insets each end by a painted cap's
// width, so the cap rects sit outside the interval and this needs no reliance on
// a caller testing the caps first. Both bounds are always set (the trim window
// always rests), so there is no pair gate.
bool point_in_trim_bridge_span(const AppState& app, const GuiAudio& audio,
                               int mouse_x, int mouse_y);

// displayed_or_live_target_map: the warp_frame_map the item hit tests decide
// against — the map the aimed-at item pixels (flags from the committed cache;
// the live trim lane's bar and endcaps, which read it directly per frame)
// were painted with, so a grab lands on what is
// drawn (WYSIWYG grabs). In target view with a non-empty displayed map
// (app.displayed_target_warp_frame_map, promoted at the frame commit that blits
// the flag cache — see the two-phase stage/promote at that member) it returns
// that map; otherwise the live display context's map (source view = the live
// context's identity/empty map, unchanged semantics; target-view cold = the
// live map until the first committed target frame).
//
// EVENT-SYNC RULING: hit DECISIONS read the committed frame's map, so hit
// geometry flips at the exact instant the on-screen items flip — the FRAME
// COMMIT that blits the flag cache, not the offscreen item rebuild (which
// only stages) and not the earlier plate publish. Gesture MECHANICS — anchors,
// walls, motion translation, the release snap, the x-coincidence images — stay
// on the LIVE map: the display converges to live within the rebuild+commit
// interval, and once the pick is made everything downstream is uniformly live.
// Synchronizing by TIME (a delay) was rejected — the true lag varies through
// zero with worker load and refresh rate, so any constant would invert the skew
// in the common fast-commit case. The remaining seams are ALL accepted:
// (a) commit-to-scanout plus human reaction — irreducible for any GUI, since
// input is always a response to the previously PRESENTED frame; (b) the
// cold-state fallback (first paint / view toggle / just-after-load, live map
// until the first committed target frame); (c) the column-based
// playhead-placement clicks (out of scope by ruling — a far subtler seam).
const std::vector<WarpFrameMapSegment>&
displayed_or_live_target_map(const AppState& app, const GuiAudio& audio);

// item_viewport_basis: the VIEWPORT twin of displayed_or_live_target_map —
// the viewport span the item PAINTERS and the trim hit test decide against, so
// an endcap is grabbed and the flag editor's box is centered on the column those
// pixels were painted at. (The flag HIT no longer reads it — hit_test_flag
// takes the painter's published rects, which are that basis by construction.) In target OR source view with
// a warm promoted mirror (app.displayed_area_w > 0) it returns the vp_start/
// vp_end/area_w triple the LAST COMMITTED frame's flag cache was built
// against — vp_start/vp_end from wf_cache.fp_* and area_w the LIVE effective
// waveform width the item render used (staged at rebuild, not fp_area_w which is
// the possibly-stale PLATE width) — so `spp` == (vp_end - vp_start) / area_w is
// the flags' OWN samples-per-pixel, exact on the committing frame, not just at
// rest. Cold (nothing promoted yet — first paint / view flip / just-after-load)
// it falls back to the LIVE viewport {viewport_start_sample, viewport_end_sample
// at current_samples_per_pixel, effective width}, matching the live-map cold
// fallback of displayed_or_live_target_map (and the pre-mirror hit_test_flag /
// hit_test_trim_endcap live basis, so cold behavior is unchanged).
//
// This is the free-function owner homed beside displayed_or_live_target_map so
// render_flag_editor_box (the unrolled editor box's column), the app_state.cpp
// trim hit test (hit_test_trim_endcap), and the LIVE TRIM paint pass
// (GuiPaintHandler::paint_trim — paint and hit share the one basis by
// construction) share ONE basis. (Three former consumers left the list in row 5:
// the marker-text lane's run resolver, marker_hit_at, and lane_text_left_x —
// hit_test_flag and the editor's click-to-caret both read a PAINTER'S STASH now
// instead of re-deriving on this basis, which is the stronger form of the same
// guarantee. The selected-stem DAMAGE was listed here until 2026-07-30 and was
// never a consumer at all.) It
// is DELIBERATELY DISTINCT from
// GuiPaintHandler::plate_viewport_basis, which reads the LIVE wf_cache.fp_*
// (the plate's current fingerprint): the paint-handler method registers the
// PLATE-REGISTERED paint overlays — enumerated at its own declaration
// (paint_handler.h), the one authoritative site for that membership — with the
// just-blitted plate, whereas this owner registers the flag/endcap/lane/hit
// geometry with the committed FLAG item cache (the promoted mirror). The two
// are NUMERICALLY EQUAL at every frame committed by the TWO PLATE WRITERS
// (worker publish, synchronous rebuild) — the old one-frame
// plate-published-NEW-while-items-show-OLD divergence is retired, since each
// writer rebuilds the flag cache inline before the damaged frame paints or
// commits (damage-REQUEST order varies — the sync writer queues its
// invalidation first — but invalidate_region only queues, so the rebuild and
// stage always land ahead of the frame) and this
// mirror promotes at the top of that frame's paint (closure dates to the
// worker-publish inline rebuild) — but the equality is NOT unconditional: the
// accepted RESIZE ITEM-ONLY-PROMOTION window is the live exception. A resize
// changes the top-strip dims, so the flag rebuild fires from on_tick and
// stages the OLD fp_vp span over the NEW effective width while the
// still-displayed plate pairs that span with its OLD fp_area_w until the
// in-flight worker render publishes — this owner and the plate-fp method
// diverge for that window, and item-registered consumers (the hit tests, the
// lane, the live trim pass) must ride THIS owner so paint == hit holds through
// it (the consumer-side statement lives at GuiPaintHandler::paint_trim's basis
// comment). The two
// owners PERSIST as a mechanism/lifecycle split — direct fp read for
// plate-registered overlays vs the staged/promoted mirror for item-registered
// geometry; do not collapse them on the strength of the plate-writer equality —
// any future unification has to resolve the resize window first.
//
// The double vp_start/spp serve the editor-box column math
// (painted_column_of_source_frame_on_basis); the int64
// vp_start_frame/vp_end_frame/area_w serve the trim hit test, which passes the
// integer span + width to trim_bound_column verbatim. (compute_flag_hit_rects
// was the other verbatim consumer until row 5 replaced it with the painter's
// stash.)
struct ItemViewportBasis {
    double  vp_start       = 0.0;
    double  spp            = 0.0;
    int64_t vp_start_frame = 0;
    int64_t vp_end_frame   = 0;
    int     area_w         = 0;
};
ItemViewportBasis item_viewport_basis(const AppState& app,
                                                const GuiAudio& audio);

// Promoted from a lambda in main(). True iff the warp marker at `idx` has a
// RESOLVED value worth showing — i.e. its flag does not already display a
// numeric tempo (pass markers and label_ref markers qualify; owning markers
// don't). Requires warp view with iteration mode off; always false in phase
// reset view (no pass concept). Its two callers are the surfaces that took the
// SELECTION translation in row 5: the bottom strip's readout and the Ctrl+C
// copy. The name is the hover popup's — the gate is not.
bool popup_eligible_marker(const AppState& app, int idx);
