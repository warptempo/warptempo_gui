#pragma once

#include "engine_settings.h"
#include "marker_store_validate.h"
#include "render_pipeline.h"
#include "render.h"
#include "settings_file.h"
#include "text_editor.h"
#include "phase_reset_clipboard.h"
#include "phaseresetmarkers.h"
#include "warp_frame_map_view.h"
#include "warpmarkers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
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
// for zoom: only `0` toggles between the working zoom and full zoom-out, and
// `C` jumps to the working zoom centered on the playhead (or on the focused
// marker). Smaller level = less file per window = more zoomed in. kMinZoom is
// the deepest zoom-in the manual walk can reach (1.2 s); kWorkingZoomLevel is
// the fine-tuning rest point every snap/toggle gesture lands on (2.4 s, one
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

// Hoisted from main.cpp's anonymous namespace so the hit_test_*
// free functions (in app_state.cpp) and the GuiInputHandler mouse handler
// (in input_handler.cpp) can reach them. Hit-test half-width only:
// clicking/hovering tolerance for stems, flags, and trim bounds. It is
// NOT a spacing gap — markers may sit arbitrarily close, overlap
// exactly, and cross during gestures; ordering degeneracy collapses at
// the render boundary, not at authoring time.
constexpr int kMarkerHitHalfPx    = 4;

// Vertical drag distance (px) that moves the zoom-strip drag by one continuous
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
// THE FORMERS — THE AUTHORITATIVE INVENTORY (re-derive by grepping every writer of
// `region.active = true`). TWO CODE SITES ACTIVATE A REGION, and BOTH DESELECT AT
// PRESS:
//   * the plain upper-half waveform DRAG (paints it live, leaving the selection
//     EMPTY throughout — the press's deselect-all is the committed act);
//   * the waveform SHIFT+click region former (playhead-to-click with nothing
//     selected, else furthest-selected-marker-to-click, DROPPING the selection),
//     which also arms a drag on the far endpoint.
// SO A REGION RESTS ONLY BESIDE AN EMPTY SELECTION, structurally — there is no
// route that rests one beside a live selection, and every consumer may rely on
// it. Everything that used to write a region from somewhere else is DELETED with
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
// from under the span), a plain UPPER-HALF waveform PRESS (the placement press
// dissolves any resting highlight at mouse-down, before it knows whether the
// gesture is a click or a fresh region drag — via arm_region_drag_at; a
// lower-half scrub press leaves the region alone, that press being the region's
// PREVIEW gesture), and the kick validator's live-domain reclamp when a bound
// falls outside a shrunken domain. The full clear-site enumeration lives at
// clear_region_highlight's declaration (input_handler.h).
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
// selection stays EMPTY from the press's deselect-all through release. A
// sub-threshold press-release is a
// plain waveform click and simply disarms — the highlight already dissolved at
// press, so there is no release-time collapse. TWO presses arm this drag: the
// plain upper-half waveform press (arm_region_drag_at — dissolves the resting
// region at mouse-down, anchors at the CLICK column) and the SHIFT-exact former
// (labwc 2026-07-24, arm_region_drag_preserving — PRESERVES the just-formed
// region, anchors at the FAR endpoint = playhead / the drop's furthest marker),
// which share every motion and release path unchanged (the anchor semantic is
// identical: a_frame = anchor_frame fixed, b_frame tracks the pointer). Alt/Ctrl
// no-op earlier. A completed drag rests the
// region on release UNLESS its final on-screen span is
// under the same kDragMovedThresholdPx gate — the gate latches once past the
// arm and never re-engages, so a jitter drag could otherwise rest a sliver,
// which dissolves like a click instead (end_region_drag_min_size_check, at both
// end points). The drag never touches the selection anywhere — the press's
// deselect/drop was the committed act, and downward-only is structural (there
// is no selection write in the drag or at its ends). ESC DOES NOTHING AT ALL now
// (architect 2026-07-29, superseding the same-day Esc-clears-the-region arm):
// pointer gestures have no cancel, so Esc mid-drag is a consumed no-op and the
// drag keeps extending under the pointer; the release rests the region where it
// stands (Free, the drag's normal product, under the sliver gate). This state was
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

// Pending trim chip/bridge drag, armed by a PLAIN (unmodified) left press in the
// top-strip CHIP ROW (a b/e chip rect, or the inter-chip bridge span). The
// trim sibling of PendingMarkerDrag: the press CLAIMS the chip/bridge geometry
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
// crossing. Requires the FULL bound pair (a lone bound is gesture-inert — the
// router never arms one); a read-only tab claims the press but never arms.
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
// from a plain chip-drag pending at all.
struct PendingTrimDrag {
    bool active   = false;
    bool is_begin = false;  // which bound the single drag targets (Begin if both)
    bool both     = false;  // the inter-chip bridge (pair) drag
    int  press_x  = 0;      // press position (window px): the gate + begin anchor
    int  press_y  = 0;
};

// Trim boundary drag (the live trim pointer gesture). Armed from a PendingTrim-
// Drag once the plain chip-row press crosses the threshold — a chip-rect hit
// drags one bound, the inter-chip bridge drags the pair. Parallel to DragState
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

    // Inter-chip bridge (top-strip chip-row span) move-both-bounds drag: both
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

// Dual-axis zoom/pan drag (Ableton-style navigation), armed by TWO surfaces: a
// plain left-drag on the live zoom-strip row, and a CTRL-exact left-drag inside
// the waveform (the same gesture, triggered on the waveform for reach). The
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
    // True for the zoom-row arm, FALSE for the ctrl-exact waveform arm. A
    // motionless release seeds a ZoomRow double-click candidate only when this
    // is set, so the zoom-bar double-click stays a zoom-row-only affordance — a
    // ctrl+waveform press-release commits and seeds nothing. Every other
    // release / motion-lost / force-end path is origin-agnostic (keys on
    // `active`); there is no cancel path.
    bool   double_click_seed = true;
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

// (The SCRUB has no drag state: the plain lower-half waveform press — the ONE
// scrub surface, the marker-text lane's having been deleted (architect
// 2026-07-27) — is a ONE-SHOT scrub act (scrub_act_at: stop a live session,
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
// the three double-click surfaces from cross-firing: a candidate seeded on one
// surface can only be consumed by a press on the SAME surface (a zoom-row click
// then a marker click within the window can never consume). None = no candidate.
enum class DoubleClickSurface { None, ZoomRow, Marker, EditorText, EmptyLane };

// Double-click detection (Wayland delivers no double-click event, so it is
// hand-rolled from two plain clicks). A click on a double-click-bearing surface
// records this candidate (at a motionless release for ZoomRow / EditorText; at
// the PRESS for Marker — see below); the NEXT plain press on the SAME surface,
// if it lands within kDoubleClickMs and kDoubleClickSlackPx of the recorded
// position AND (for Marker) targets the same marker, is consumed as that
// surface's double-click action instead of the single-click action. A drag that
// MOVED records nothing and clears any candidate. Surfaces:
//   ZoomRow    -> the zoom-bar double-click zoom command (target unused; the
//                 zoom row is thin, so only press_x slack is compared).
//   Marker     -> opens the marker's flag editor (target = marker index; both
//                 axes' slack compared). The marker is ONE pointer item: the hit
//                 is its flag SHAPE or its rendered marker-text LANE RUN, and a
//                 candidate seeded on one part consumes on the other. One seed
//                 timing for the whole surface — the PRESS; a press that then
//                 becomes a real marker drag (the reposition drag, the only one
//                 left since the tempo drag's deletion) drops the candidate at the
//                 threshold crossing, so a moved drag never carries one.
//   EditorText -> selects the clicked character class's RUN (word / punctuation
//                 / whitespace) in the active text editor (target unused; both
//                 axes' slack compared).
//   EmptyLane  -> creates a marker at the clicked position on an EMPTY flag /
//                 triangle lane spot (architect 2026-07-23): the AUGMENTED
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
    int     press_x   = 0;      // seed x (Marker seeds at the press; ZoomRow /
    int     press_y   = 0;      //   EditorText at a motionless release)
    int     target    = -1;     // marker index for Marker; unused otherwise
};

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

// Hover state, two surfaces driven by one hovered marker. Any marker under the
// cursor — either column — shows its OWN value in the marker-text lane (top
// strip; paint_marker_text_lane), and a pass/label_ref warp marker ALSO shows
// its resolved tempo in the bottom strip's transient row (paint_bottom_strip).
// SELECTION now drives BOTH surfaces too (paint-side, not through this struct):
// every selected marker shows its own value persistently in the lane, and the
// last-selected pass/ref shows its readout in the bottom strip when no hover
// readout wins. This struct stays the HOVER cache; the persistent selection runs
// read the live store directly in the paint path.
// The motion and viewport-recompute handlers set these fields the instant the
// cursor lands on a flag rect (no dwell); dismiss conditions clear the whole
// struct. A store mutation under a stationary cursor does NOT clear — the cached
// generations go stale, and the recompute (driven by the on_tick refresh when no
// motion follows) re-reads the hovered marker's current fields in place.
//
// `lane_text` is the marker's own payload — the canonical flag line
// (flag_text_iter) for a warp marker, kPhaseResetLaneToken for a phase reset
// marker (render.h: a display-only token, since a phase reset authors no
// payload) — sized and centered by `source_frame` in the lane. `readout_text`
// is the pass/ref resolved readout for the bottom strip (compute_hover_popup_text),
// empty on owners and phase resets. Both are computed once per rect-entry (or
// per in-place mutation of the hovered marker) and read unchanged by the paint
// path, so paint never repeats the math. Discarded on rect-exit; there is no
// asynchronous work to cancel — a transition recomputes the text and the prior
// result is dropped.
struct HoverPopupState {
    int         marker_index = -1;
    // WHICH part of the unified marker item the hover hit came from (MarkerHit::
    // on_flag): true = the FLAG shape, false = the rendered lane RUN. The lane's
    // TEXT-HOVER EXPANSION keys on this — a run whose full composed text exceeds
    // the ambient budget expands to full text ONLY while its own TEXT run is
    // hovered (on_flag == false), not its flag. The recompute short-circuit
    // compares this alongside marker_index (a flag->run move is the same index but
    // a different part, and must recompute so the expansion appears). Irrelevant
    // when marker_index < 0.
    bool        on_flag = false;
    // Marker-store generations captured at set time, one per column (the same
    // counters the flag cache fingerprints). marker_index alone identifies
    // WHICH marker is hovered, but every derived field (lane_text, readout_text,
    // copy_payload, source_frame) is read from that marker's CURRENT fields and
    // position, so an in-place mutation under a stationary cursor — a tempo step,
    // a Ctrl+N eligibility change, a nudge — would otherwise leave the cached
    // text stale. The recompute short-circuit requires index AND both generations
    // equal, so any store mutation forces a full re-read on the next recompute;
    // the on_tick refresh drives that recompute even when no pointer motion
    // follows the keyboard mutation.
    long long   warp_gen  = -1;
    long long   phase_gen = -1;
    // The displayed-map generation (app.displayed_map_gen) captured at set time.
    // The hovered marker's IDENTITY is resolved by marker_hit_at (flag shape OR
    // rendered lane run) against the displayed flag / run positions, so when the
    // displayed map advances (a promotion) the flag under a stationary cursor can
    // change even though neither marker store mutated. Bundling the map generation into the short-circuit forces a
    // re-read on the next recompute, and the on_tick repair fires on a map-gen
    // mismatch too — so a silent promotion cannot leave the hover naming a marker
    // whose flag moved away (and cannot leave a stale copy_payload).
    long long   displayed_gen = -1;
    // The hovered marker's authored source frame, the lane run's centering
    // origin (lane_text_left_x_at_frame) — column-agnostic, so the lane paint
    // needs no knowledge of which store the marker came from.
    int64_t     source_frame = 0;
    std::string lane_text;
    std::string readout_text;
    // The pasteable effective tempo value for the hovered marker, in the exact
    // form the flag editor accepts (base, plus "*scale" when a scale is
    // present). Computed alongside readout_text at each rect-entry and copied to
    // the clipboard by the Ctrl+C hover-copy binding while a readout shows.
    // Non-empty exactly when readout_text is (both are pass/ref only), so the
    // binding never fires with an unset payload.
    std::string copy_payload;

    // Whether either hover surface currently paints. Drives the damage decision
    // at hover transitions and the clear path.
    bool any_visible() const {
        return !lane_text.empty() || !readout_text.empty();
    }
};

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

// In-window modal prompt state. When `active` is true, the bottom strip
// overlays the prompt's text and response options in place of the
// timestamp / tab letter / dirty indicator.
// Input is owned by the prompt: only the response keys (and Esc, which
// activates the rightmost response) do anything; everything else is
// swallowed. `response_keys` holds lowercase letters; the activator
// lowercases incoming keypresses before comparing.
struct PromptState {
    bool                     active = false;
    std::string              text;
    std::vector<char>        response_keys;     // lowercase
    std::vector<std::string> response_labels;   // e.g. "[s]ave"
    DialogTrigger            trigger = DialogTrigger::CLOSE_WINDOW;
};

// Trim store (architect-ruled hardfail model). begin and end are authored
// NAMED ROLES — no gesture ever reassigns which bound is which — holding
// whole source frames in int64_t, exactly like marker times (a fractional
// bound is unrepresentable; the .settings writer persists the exact value as
// integer text via frame_format.h, so a saved bound reloads bit-identically).
// Every trim GESTURE clamps each bound to its own
// absolute walls: begin spans frame 0 to EOF-1 (a begin at or past the
// source end can never render), end spans frame 0 to EOF exactly
// (end-at-EOF is valid, so the GUI must be able to represent it) — plain
// integer compares, the load guard's own comparison — so past-EOF
// cannot be gestured. There are NO partner walls — a bound crosses its
// partner freely during any gesture — but crossed or equal bounds can no
// longer REST anywhere: every trim commit auto-clears a pair left with
// end_frame <= begin_frame (both bounds destroyed, silently —
// GuiInputHandler::auto_clear_crossed_trim, the trim sibling of the marker
// normalizations), and a persisted crossed/equal pair clears per tab at
// load with one stderr line (file_loader). The zero floor
// is now subsumed by the per-bound walls, but it remains the reason the
// floor exists at all: a negative position is unrepresentable in the
// authored frame form the .settings file persists (parse_authored_frame
// rejects negatives as malformed) — a format-representability floor, not a
// validity rule. A past-EOF
// bound is adversarial (the gesture walls make it
// uncommittable and a .settings applies only to its own audio, so a
// past-EOF bound means the audio was swapped outside the GUI), hard-failed
// at the load boundary (file_loader / CLI) like a corrupt audio file.
// validate_trim_frames (trimmer.h) stays the sole author of the
// trim-validity vocabulary, but a refusal at render time means "render
// untrimmed" (plan_trim's callers fall back to the full deliverable, one
// stderr line), never a refused render; it never guards a gesture.
// Readers of MID-GESTURE state must not assume begin <= end — crossing is
// free until the commit — but at REST the order begin < end now holds
// whenever both bounds are set.
struct TrimState {
    int64_t begin_frame = 0;    // whole source frame (int64_t)
    int64_t end_frame   = 0;    // whole source frame (int64_t)
    bool    has_begin   = false;
    bool    has_end     = false;
};

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

    // Per-tab read-only lock. Toggled by bare `o`. While true, the active
    // tab admits a subset of keys (navigation, playback, view-switch) and
    // its mouse handlers block authoring gestures (drop, drag, label
    // edit). Persisted as tab_a_read_only / tab_b_read_only in .settings.
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
    //   * the upper-half PLACEMENT PRESS (input_pointer.cpp), which moves the
    //     cursor and reseeks.
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
    // The integer sample stays the domain / change-detection anchor (loop-wrap,
    // the cur == sample short-circuit, the viewport-centering targets, the
    // timestamp readout). Meaningful only while active, like the integer sample.
    double  playhead_scanner_precise = 0.0;
    bool    playhead_scanner_active = false;
    float   playback_speed          = 0.7f;

    // Looping audition (trim set, launch-captured at launch_playback_from —
    // the shared body under the Space toggle and the scrub launch).
    // `playback_loop_start_sample` is the domain-coordinate loop start decided
    // ONCE at the play launch (-1 = this session does not loop); the
    // click-keep-alive reseek threads it back through play() so a mid-session
    // trim edit cannot alter the running session's loop verdict.
    // `playback_loop_wrap_seen` is the last loop_wrap_seq() the per-redraw
    // drive observed; a change means the audio callback wrapped and the drive
    // resyncs the predictor. It is never reset (loop_wrap_seq is monotonic and
    // never reset either), so it stays valid across sessions.
    int64_t  playback_loop_start_sample = -1;
    uint64_t playback_loop_wrap_seen    = 0;

    // GUI-wide monospace text size in points (the font_size setting; 6..72,
    // default 11). A display preference, not engine input and not authoring
    // state: persisted on Ctrl+S like playback_speed, applied at file load
    // and set through the settings editor (`:font_size=`, no hotkey), and
    // pushed to the renderer's file-scope state via set_gui_font_size_pt at
    // each of those application points.
    double  font_size               = 11.0;

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
    // the render hotkeys (Ctrl+Alt+R / Ctrl+Alt+I) and
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

    // Selected-marker stem visibility model (architect 2026-07-25, superseding
    // the conditional-stem apparatus). The focus stem
    // (paint_selected_stem) is the SINGLETON selection's focus visual and ALWAYS
    // paints for the one selected marker — no hover, pin, or gesture condition.
    // The former LATERAL-GESTURE PIN (stem_pin_marker / stem_pin_command_seq, the
    // five stamp sites, the on_tick reaper, StemPinPreserveGuard, and the damage_seq
    // counter it consumed) existed only to keep the stem visible after a lateral
    // gesture without a hover; always-on subsumes that purpose, so the whole
    // apparatus was harvested. Stem-transition DAMAGE now rides ONE subject-change
    // owner on Selection (stem_subject / damage_stem_on_subject_change, the
    // phase-overlay pattern's sibling), wired at the selection mutators: the stem
    // appears/moves/disappears iff the singleton subject changes, and the gestures
    // that move a subject marker's FRAME or IMAGE (nudges, drags, re-warps) already
    // full-damage the waveform. A focused GROUP (2+ selected) shows no stem — its
    // cue is the members' ink triangles plus the always-visible cursor landed on
    // the focus.

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
    // to the 1.00 normalization fallback, painted kAccent in their flags unless
    // selected (see warp_frame_map_view.h). Mutable: refreshed from the const
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
    // (with the staged value) at source load and `'` adopt (through
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

    // Monotonic counter bumped once each time on_redraw PROMOTES a staged
    // displayed map into displayed_target_warp_frame_map (the top-of-frame
    // promotion). Every promotion — target-view map swap or source-view clear —
    // advances it, so hover identity (HoverPopupState::displayed_gen) can detect
    // a silent geometry change under a stationary cursor and refresh on the next
    // tick. Never reset (shutdown is terminal); wrap is unreachable at any real
    // frame rate.
    long long displayed_map_gen = 0;

    // Displayed-VIEWPORT mirror — the SIBLING of displayed_target_warp_frame_map
    // for the viewport half of the same event-synchronized hit geometry. The
    // flag item pixels are painted from the flag cache's rebuild-time
    // fingerprint (wf_cache.fp_vp_start / fp_vp_end / fp_area_w), NOT the live
    // viewport; painted_column_of_source_frame reads the LIVE viewport
    // (app.viewport_start_sample). During an async plate-publish window (a
    // worker-dispatched viewport change — follow-scroll, center-on-playhead) the
    // live viewport already holds the NEW span while the flags still paint at the
    // OLD one, so a lane run centered by the LIVE viewport would jump off its
    // flag until the worker caught up. These fields hold the vp_start/vp_end/
    // area_w the LAST COMMITTED frame's flag cache was built against, promoted
    // in LOCKSTEP with displayed_target_warp_frame_map at the frame that blits
    // that cache, so the marker-text lane geometry (run centering, the visible-
    // set cull, the run hit rects — see item_viewport_basis in this header),
    // the LIVE TRIM pass (GuiPaintHandler::paint_trim — its chips/stems paint on
    // this basis so hit_test_trim_chip / route_trim_chip_press land on the drawn
    // pixels) ride the
    // same basis the flags do. (The selected-stem DAMAGE was listed here until
    // 2026-07-30 and never belonged: the stem paints on the PLATE basis, so its
    // item-basis narrow damage was the wrong epoch. It is a full waveform-area
    // invalidate now — see Selection::damage_stem_on_subject_change.)
    // area_w == 0 means cold (nothing promoted
    // yet); the accessor then falls back to the live viewport, matching
    // displayed_or_live_target_map's cold live-map fallback. Written by the
    // paint-pass promotion; cleared alongside displayed_target_warp_frame_map at
    // every clear site (source load / `'` adopt / view toggle). Deliberately
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

    // Pending trim chip/bridge drag, armed by a plain chip-row press (the
    // trim-drag machinery begins only past the threshold). Cleared on the
    // threshold crossing, on button release / lost button, by the force-end
    // finalizer, and on file load.
    PendingTrimDrag pending_trim_drag;

    // The resting region-select span (session-only). Cleared on file load, the
    // A/B tab switch, and the S/T audio-view switch (Esc no longer clears it —
    // the ladder is deleted).
    RegionState region;

    // Live trim boundary drag (chip / inter-chip bridge). Cleared on button
    // release / lost button, by the force-end finalizer (both COMMIT its live
    // bounds), and on file load.
    TrimDragState trim_drag;

    // Plain left-drag on a live strip row (zoom/pan navigation). Cleared on
    // button release and file load.
    StripDragState strip_drag;

    // Double-click candidate, shared by the zoom-row, flag, and editor-text
    // surfaces (the surface tag prevents cross-firing). Seeded by a motionless
    // press-release on a double-click-bearing surface; cleared on file load and
    // when the double-click action fires.
    DoubleClickCandidate double_click;

    // Alt+drag on the waveform (continuous 1:1 grab-pan). Cleared on button
    // release / lost button, by the force-end finalizer, and file load.
    ScrollDragState scroll_drag;

    // Mouse drag-to-select inside the active text editor. Cleared on
    // button release, on a lost button mid-drag, and on file load.
    EditorTextDragState editor_text_drag;

    // Hover-popup state. See HoverPopupState above.
    HoverPopupState   hover_popup;

    // Cursor screen position from the last on_motion event. Used by
    // recompute_hover_at_cursor() to re-evaluate hover after a viewport
    // mutation (when the cursor is stationary but rects have shifted). -1
    // means "no motion seen yet".
    int               last_mouse_x = -1;
    int               last_mouse_y = -1;

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
    // font_size, audio_player, and the four *_hash env-attestation keys) — do
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
    // `'` render-commit (adopt_render_entry, a full engine-settings
    // adopt) all mutate fields of this struct directly. Carried by
    // RenderRequest at dispatch; serialized to .settings on Ctrl+S.
    // Default-constructed before any source load.
    EngineSettings engine_settings;

    // Live working copy of the active tab's trim state. The per-tab
    // backing store lives in ViewState::trim. Excluded from undo/redo.
    // Mirrored to/from the active tab's ViewState slot at the tab-swap
    // boundary in active_views.cpp (same pattern as viewport/zoom/playhead).
    // Trim is a region authored purely by the plain chip-row pointer drags
    // (single-bound chip, chip-row inter-chip bridge/pair), the bare-x set arm
    // (a live region sets the trim to it and KEEPS/re-syncs the highlight; no
    // region is a silent no-op), and the Shift+X unset arm (clears both bounds)
    // — it is NOT part of the selection system (no bound selection, no Tab stop,
    // no Delete arm).
    TrimState trim;

    // Bottom-strip command prompt. Active only when a close / re-detect
    // gesture fires while a confirmation is required. Originally
    // a centered modal dialog; the same modal semantics now live in the
    // bottom strip.
    PromptState prompt;

    // Shared text-editor state for two editors distinguished by Kind: the
    // top-strip flag editor (Kind::FlagPayload — active when editing a warp
    // marker's payload, its text run and caret painted live in the marker-text
    // lane centered over the marker, not on the flags, which are textless
    // shapes) and the bottom-strip BPM editor (Kind::BpmBracket). The editor
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

    // Render-commit prompt editor. Opens on bare `'` from an authoring view,
    // takes a render entry's identifier relative to renders/
    // (`<batch_dir>/<basename>` or a globally-unique bare basename), and on
    // Enter adopts that render's frozen sidecar recipe as the new authoring
    // baseline (GuiInputHandler::adopt_render_entry). A bottom-strip modal like
    // the settings editor; separate State so the two paint regions stay
    // independent.
    text_editor::State commit_editor;
    bool commit_editor_blink_last = false;

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
    // render the timestamp normally." Mutually exclusive with prompt.active in
    // practice (these updates can't fire while a prompt is up).
    std::string queue_progress_text;

    // Transient one-line status message shown in the bottom strip after
    // the dirty dot, alongside the S/T·W/P·A/B indicators. Set by a
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

    // Internal text clipboard (session-only, lost on app close). The GUI has
    // no outside-world clipboard: hover-copy (Ctrl+C over a hover popup) and
    // the bottom-strip editors' copy/cut/paste all round-trip through this
    // one string, so a hover value pastes into a flag/settings text box.
    std::string        text_clipboard;

    // Iteration mode. Toggled by plain `i` in warp's home (W marker view +
    // source audio view; no-op elsewhere). Session-only (off at load, lost on
    // app close); survives the W/P marker-view switch, but entering target
    // audio view (S->T) exits the mode through wipe_iter_state, so the mode
    // can never rest in target view. When true, hover popups are suppressed
    // and a persistent iteration popup is rendered above every owning
    // marker's flag rect.
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
    // Consumed by the `l` listen-to-renders launcher and the `'` commit
    // editor (adopt_render_entry).
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
GuiRect top_zoom_row_area(const AppState& a);
GuiRect top_upper_row_area(const AppState& a);
GuiRect top_marker_text_row_area(const AppState& a);
GuiRect top_flag_row_area(const AppState& a);
GuiRect top_triangle_row_area(const AppState& a);
GuiRect bottom_upper_row_area(const AppState& a);
GuiRect bottom_lower_row_area(const AppState& a);
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
// Consumed by the wheel_context
// predicate (on_wheel's completed-detent gate and the platform's per-frame
// sub-detent accumulator probe both route through it), the gate that must
// never fire mid-gesture: the "nothing pops mid-gesture" boundary. The pending
// drags are included so a wheel cannot shift the viewport out from under the
// press before the drag begins.
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
// non-home view a column is display/navigation-only (selection, hover, Tab,
// readouts all live; every placement/store mutation refuses silently,
// navigation-class, exactly the read-only-tab convention). The TWO ruled
// exceptions live at their sites: (1) the bare UP/DOWN TEMPO CENT STEP in
// W+target (owner-only there, adjust_tempo_cents — singleton and group), which is
// the WHOLE tempo surface now and is dispatched without consulting this
// predicate; (2) the
// phase-reset propagate (a warp-view gesture that authors phase resets; its
// paste lands in target view). The list SHRANK to these two on 2026-07-29: the
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
// are PLATE-REGISTERED: paint_playheads draws both the cursor and the scanner
// through the explicit-basis form at GuiPaintHandler::plate_viewport_basis.
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
// Ctrl+Tab restore, and the render-entry adopt's tab bands — so an
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
// hit_test_flag: scan the flag rectangles in the top strip and return the
// marker index under (mouse_x, mouse_y), or -1. The hit area is each fixed
// flag rectangle PLUS its fused tip-down triangle, derived from the rect via
// flag_triangle_half_width_at (the same taper owner paint_flag_shape fills
// with) — see the body for the exact test. Rects may overlap, and the walk
// resolves an overlap to the topmost-painted flag. Mirror of the painters'
// z-order (render_flags / render_phase_reset_flags): the SELECTED shapes paint
// above the unselected, and within each class the leftmost paints on top. So
// the walk runs twice — first the first-containing rect whose marker is
// selected, else the first-containing rect unconditionally (rects are emitted
// ascending-x, so each pass resolves to that class's leftmost = topmost).
// Priority overall: topmost = selected leftmost > leftmost. Deliberately
// visible to every consumer (selection clicks, plain flag-drag reposition grabs,
// the hover popup's target): the topmost-painted flag is what the user sees, so
// it is what a click grabs (WYSIWYG). Works in both 'W' and 'P' authoring views
// (each column's own flag list).
int hit_test_flag(const AppState& app, const GuiAudio& audio,
                  int mouse_x, int mouse_y);

// Which trim boundary, if any, a waveform-area click lands on.
enum class TrimHit { None, Begin, End };

// hit_test_trim_chip: return which trim bound's painted CHIP RECT (in the upper
// top row) contains the press, or None. Early-outs to None unless the FULL pair
// is set — the sole consumer (route_trim_chip_press) routes here only then (a
// lone bound is gesture-inert), so both bounds are guaranteed present.
// AUTHORING views — the active tab's live pair, project-level in both 'W' and
// 'P' views. Each chip is a textless SQUARE (flag_lane_w_px() wide, and as tall
// — its lane's height is that same accessor) EDGE-ANCHORED on the bound's
// painted column — the begin chip's LEFT edge on it, the end chip's RIGHT edge
// on it — exactly what render_trim_flags fills, this rect built the same way,
// so paint and hit cannot drift. Tests both mouse_x and mouse_y. Walks the
// display warp_frame_map in target view so the hit lands on the drawn chip.
// The chip and the inter-chip bridge are the ONLY trim grab handles (the
// waveform stem grab retired).
TrimHit hit_test_trim_chip(const AppState& app, const GuiAudio& audio,
                           int mouse_x, int mouse_y);

// displayed_or_live_target_map: the warp_frame_map the item hit tests decide
// against — the map the aimed-at item pixels (flags from the committed cache;
// the live trim chips/stems and selected stem, which read it directly per frame)
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
// the viewport span the item hit tests (flag shape, trim chip, marker-text lane)
// and the lane geometry decide against, so a run centers on and a hit lands on
// the column the flag/chip pixels were painted at. In target OR source view with
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
// hit_test_trim_chip live basis, so cold behavior is unchanged).
//
// This is the free-function owner homed beside displayed_or_live_target_map so
// the render.cpp free functions (lane_text_left_x_at_frame, the lane run
// resolver, marker_hit_at), the app_state.cpp hit tests (hit_test_flag,
// hit_test_trim_chip), and the LIVE TRIM paint pass (GuiPaintHandler::paint_trim —
// paint and hit share the one basis by construction) share ONE basis. (The
// selected-stem DAMAGE was listed here as a consumer until 2026-07-30 and was
// never one: paint_selected_stem paints on the PLATE basis, so the narrow
// item-basis stem invalidator erased columns the stem was never drawn at inside
// the resize window named below. That damage is a full waveform-area invalidate
// now, owned by Selection::damage_stem_on_subject_change, and the narrow route
// is deleted.) It
// is DELIBERATELY DISTINCT from
// GuiPaintHandler::plate_viewport_basis, which reads the LIVE wf_cache.fp_*
// (the plate's current fingerprint): the paint-handler method registers the
// PLATE-REGISTERED paint overlays — enumerated at its own declaration
// (paint_handler.h), the one authoritative site for that membership — with the
// just-blitted plate, whereas this owner registers the flag/chip/lane/hit
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
// The double vp_start/spp serve the lane column math (painted_column_of_source_
// frame_on_basis); the int64 vp_start_frame/vp_end_frame/area_w serve the hit
// tests, which pass the integer span + width to compute_flag_hit_rects /
// trim_bound_column verbatim, and the lane cull (exact vp_end, no reconstruction).
struct ItemViewportBasis {
    double  vp_start       = 0.0;
    double  spp            = 0.0;
    int64_t vp_start_frame = 0;
    int64_t vp_end_frame   = 0;
    int     area_w         = 0;
};
ItemViewportBasis item_viewport_basis(const AppState& app,
                                                const GuiAudio& audio);

// Promoted from a lambda in main(). True iff the warp marker
// at `idx` is hover-popup-eligible — i.e. its rect doesn't already
// display a numeric tempo (pass markers and label_ref markers qualify;
// owning markers don't). Requires warp view with iteration mode off.
// Always false in phase reset view (no pass concept).
bool popup_eligible_marker(const AppState& app, int idx);
