#pragma once

#include "engine_settings.h"
#include "gui_input.h"
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
// SCRUB press leaves the region alone in either entry, the lower-half left one
// and the bare right one, that gesture being the region's
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
// selection stays EMPTY from the press's deselect-all through release. THE DRAG
// CARRIES THE PLAYHEAD (architect 2026-07-30): each changed column writes the
// cursor to the MOVING endpoint, both arms through the one motion path, with no
// viewport scroll and no playback reseek per motion. A
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
// GEOMETRY alone; a read-only tab claims the press but never arms.
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
//                 sub-window, else the whole song). Target unused; both axes'
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
// (any spot in the band — endcap, bridge, or bare ground; read-only included,
// the framing
// being pure navigation) records this; the left release seeds the TrimBar
// candidate when the pointer is still within kDoubleClickSlackPx of the recorded
// point and no trim drag went live. That slack IS the motionless test: it equals
// kDragMovedThresholdPx, so "never became a drag" and "never left the slack" are
// the same condition by construction. Cleared at every left release (the release
// consumes it) and by the force-end finalizer, beside the candidate's own clear.
// Session-only.
struct TrimBarPressSeed {
    bool active  = false;
    int  press_x = 0;
    int  press_y = 0;
};

// THE ROSTER OF REDESIGNED BUTTONS — the single enumeration of every flat
// button the kdenlive rows carry, in painted order: row 1's Quit, Navigation and
// Settings plus the view bar's three, row 2's toolbar four, row 3's two TABS,
// then row 4's eleven view / mode / action buttons. It exists ONCE, here, because it indexes
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
// on_key. What they do NOT take is the two row-2-only faces — no click face and
// no disabled face — which is stated at each face's site rather than modelled
// here (row 4 takes the click face but not the disabled one). Row 1's SETTINGS
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
    // Row 3, the tabs.
    TabA, TabB,
    // Row 4, the icon row, in painted order: the two view radio pairs, the
    // phase-reset clipboard pair, the three mode/editor buttons, then the two
    // render-entry buttons. (THE ZOOM PAIR LEFT 2026-08-02 — the architect's
    // no-duplicate-commands ruling, its two commands now living in the
    // Navigation dropdown; the `-` / `=` KEYS are untouched.)
    IconS, IconT, IconW, IconP,
    IconCopy, IconPaste, IconBpm, IconIter, IconFollow,
    IconListen, IconCommit
};
inline constexpr int kRedesignButtonCount = 23;
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
// ITS ONE CONSUMER IS THE HOVER PREDICATE (redesign_button_hoverable below),
// which lets row 1 keep hovering under an open dropdown while the rows the
// popup can cover do not.
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
        case RedesignButton::IconCommit:
            break;
    }
    return false;
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
// AN ITEM NEVER GREYS OUT and never refuses here: a command that cannot act
// right now still dispatches and its own arm answers, which is the roster's
// standing buttons-never-grey rule ("one that cannot act right now simply does
// nothing, exactly like its key") applied one surface further out.
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
    // settings-editor commit, the `'` adopt), and the editor commit APPLIES it
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

    // Pending trim endcap/bridge drag, armed by a plain trim-bar press (the
    // trim-drag machinery begins only past the threshold). Cleared on the
    // threshold crossing, on button release / lost button, by the force-end
    // finalizer, and on file load.
    PendingTrimDrag pending_trim_drag;

    // The resting region-select span (session-only). Cleared on file load, the
    // A/B tab switch, and the S/T audio-view switch (Esc no longer clears it —
    // the ladder is deleted).
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
    // vectors are written by ONE producer (the flag-cache rebuild's
    // render_flags / render_phase_reset_flags call — grep them: there is
    // exactly one call site each) against the DISPLAYED basis those pixels were
    // painted with, so a click during an async publish window tests the flag it
    // can see rather than the one the live viewport would put there.
    //
    // `flag_hit_rects` is in PAINT order (store order), so hit_test_flag walks
    // it BACKWARDS: last painted = topmost = what a click grabs. `marker_stems`
    // carries one entry per ENABLED marker only — a disabled marker has no stem
    // ever, expressed as an absent entry (MarkerStem, render.h).
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
    // shaped-geometry contract one row down, for the settings / render-commit /
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
    // physically held down on an ENABLED button that HAS the face, -1 otherwise.
    // Written by exactly two routes, each damaging the strip on the transition:
    // the press claim sets it (input_pointer.cpp) and clear_redesign_button_press
    // clears it (the left release and the pointer-leave / button-lost hook). The
    // face rides the PHYSICAL hold, not the action — the chord already fired at
    // the press — so it is visual only and survives the pointer wandering off
    // the button mid-hold. WHICH buttons have it is the chord table's
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
    // `hovered_item` is -1 or an index into the open menu's item table; `rect`
    // and `item_rects` are PAINTER-PUBLISHED, so the hit tests read exactly the
    // painted boxes and never re-shape a label (the displayed-basis doctrine).
    // Every rect is zero while closed, which is the correct cold answer: an
    // empty rect contains no point.
    // `pressed_item` is the ARMED item: set by a press on one, cleared by the
    // release (wherever it lands) and by every close. It exists because the
    // dropdown is the ONE redesign surface that acts on RELEASE — every row
    // button fires on press, a menu triggers on release by universal
    // convention — which is also the only reason a pressed face is visible long
    // enough to be worth painting.
    struct Dropdown {
        DropdownMenu menu         = DropdownMenu::None;
        int          hovered_item = -1;
        int          pressed_item = -1;
        GuiRect      rect{0, 0, 0, 0};
        std::array<GuiRect, kDropdownMaxItemCount> item_rects{};

        bool open() const { return menu != DropdownMenu::None; }
    };
    Dropdown dropdown;

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

    // Cursor screen position from the last on_motion event. Used by
    // recompute_redesign_button_hover() — the one surviving hover — to
    // re-evaluate the button faces from the tick when the cursor is stationary.
    // -1 means "no motion seen yet".
    int               last_mouse_x = -1;
    int               last_mouse_y = -1;

    // Is the pointer INSIDE the window? last_mouse_{x,y} keep the last position
    // the pointer was seen at, which is a point INSIDE the window even after it
    // has left — so anything that re-resolves hover from those coordinates
    // without this flag would re-light a button under a pointer that is gone.
    // That matters because the hover recompute is no longer motion-only: the run
    // loop's tick runs it too (so state and geometry changes that arrive without
    // motion still re-resolve), and the tick keeps running after a leave.
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
    // gui_scale, audio_player, and the four *_hash env-attestation
    // keys) — do
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

    // Iteration mode. Toggled by plain `i` in warp's home (W marker view +
    // source audio view; no-op elsewhere). Session-only (off at load, lost on
    // app close); survives the W/P marker-view switch, but entering target
    // audio view (S->T) exits the mode through wipe_iter_state, so the mode
    // can never rest in target view. When true, flag_text_iter splices the
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
GuiRect top_menu_row_area(const AppState& a);
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
// FOUR CONSUMERS, re-derived by grep 2026-08-01, each stating the same
// "nothing pops mid-gesture" boundary from its own side:
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
//     so the one scrub act it runs must not fire into a live drag.
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

// THE REDESIGNED BUTTONS' ENABLED PREDICATE — one owner for the DISABLED FACE
// (row 2's third face) and for hoverability, mirroring each chord's OWN
// refusals rather than inventing a policy. Three readers: the painter (which
// stashes what it painted), the press claim (a disabled press is a consumed
// nothing — the chord is not dispatched), and main.cpp's staleness comparator.
//
// WHAT EACH ENTRY MIRRORS, read off the routes themselves:
//   * ALL FOUR row-2 chords drop at on_key's `app.loading || total <= 0` guard
//     (input_handler.cpp) and at the PER-TAB READ-ONLY GATE. The read-only
//     gate's allowlist (read_only_key_blocked, input_key_dispatch.cpp) admits
//     none of Ctrl+S, Ctrl+Z, Ctrl+Shift+Z or Ctrl+Alt+R — Ctrl+S explicitly
//     ("read-only means no save"), undo/redo explicitly, and Ctrl+Alt+R
//     structurally, its ctrl+alt combination matching no allowlist predicate.
//     So a locked tab greys the WHOLE toolbar, which is the truth the keys
//     already have.
//   * Undo / Redo additionally take history_step_actionable on their own stack
//     — the exact guard do_undo / do_redo run.
//   * Save takes its route's stable-state refusal, an empty warpmarkers_path
//     (GuiSaveOps::save). Its OTHER refusal — a numeric locale that is no
//     longer "C" — is deliberately NOT here: that is a mid-session dynamic
//     fault, not stable state, and greying a button on it would hide the one
//     stderr line that reports it.
//   * Render takes Ctrl+Alt+R's own first line, an empty source_audio_path.
//   * Row 1's Quit and row 3's tabs are ALWAYS enabled: Quit keeps its two
//     faces by ruling, and a tab has no disabled face at all. Their entries
//     exist so the vector is total over the roster and the comparator needs no
//     membership test.
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
    switch (b) {
        // Rows 1, 3 and 4 have NO DISABLED FACE AT ALL — row 4 by the
        // architect's design (he provided five states and no disabled one), rows
        // 1 and 3 by their face scope. Their presses always dispatch and the
        // CHORDS' OWN refusals answer: the read-only gate blocks the authoring
        // ones, the loading gate blocks everything, each arm keeps its own
        // guards. Inherited through on_key, never mirrored here — which is why
        // these are a plain `return true` and not a second copy of those gates.
        //
        // THE VIEW BAR'S "DISABLED" CROPS ARE THE UNFOCUSED WINDOW (architect
        // 2026-08-02), not a disabled button: they are the row-1/2 ground swap's
        // sibling on app.window_activated, a PAINT-ONLY variant of the whole
        // bar, and never this bit. So the row-1 claim above stays true in its own
        // terms — no button on this row has a disabled face — and the three join
        // the same arm.
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
        case RedesignButton::IconCommit:
            return true;
        case RedesignButton::Save:
        case RedesignButton::Undo:
        case RedesignButton::Redo:
        case RedesignButton::Render:
            break;
    }
    if (a.loading || total_frames <= 0) return false;
    if (active_view_state(a).read_only) return false;
    switch (b) {
        case RedesignButton::Save:
            return !a.warpmarkers_path.empty();
        case RedesignButton::Undo:
            return history_step_actionable(a, a.history.undo_stack);
        case RedesignButton::Redo:
            return history_step_actionable(a, a.history.redo_stack);
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
// MOMENTARY BY DESIGN, and therefore false here: Copy, Paste, Listen, Commit —
// each is an action that completes, with no state to stay lit for — and BPM,
// whose editor is a transient modal SESSION rather than a resting mode (it
// cannot rest open, and `m` never reaches dispatch while it is up), so lighting
// it would advertise a mode this product does not have.
inline bool redesign_button_selected(const AppState& a, RedesignButton b) {
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
        case RedesignButton::IconCommit:
            break;
    }
    return false;
}

// THE SHIFT-AUGMENTED BUTTONS — the ONE owner of "this button's chord comes in
// a pair the keyboard already spells, so a SHIFT-exact press reaches the twin".
// Exactly two carry it, and both for that reason: Render (Ctrl+Alt+R renders
// beside the source, Ctrl+Alt+Shift+R into a numbered _miscellaneous cell) and
// Paste (Ctrl+Alt+P pastes phase resets, Ctrl+Alt+Shift+P pastes with state).
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
    return b == RedesignButton::Render || b == RedesignButton::IconPaste;
}

// THE HOVER TOOLTIP'S TEXT — name and chord, kdenlive's pattern, one row per
// button that has one. It sits with the roster (rather than with the chord
// table in input_pointer.cpp) because BOTH the painter and the pointer read it,
// and because membership is the interesting part: a null `line1` means "this
// button has no tooltip", which is the whole story for the two that carry none.
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
// `line2` is the SHIFT LINE and is non-null on exactly the two shift-admitting
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
        // exception, scoped to exactly these two buttons.
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
        case RedesignButton::IconCommit: return {"Commit render (')", nullptr};
    }
    return {nullptr, nullptr};
}

// THE RENDER BUTTON'S ITERATION FACE — the ONE row on this whole surface whose
// text follows STATE rather than being a constant (architect 2026-08-02). With
// iteration mode on, Ctrl+Alt+R IS the sweep, so the button says the sweep: its
// LABEL reads "Render Iterations" and its hint is the matching ONE-LINE form.
//
// THE CAPITAL I IS DELIBERATE AND SCOPED TO THIS STRING (architect 2026-08-03,
// his explicit instruction): every other multi-word GUI label in the product
// stays sentence case ("Playback speed", "Center on focus", "Next marker") —
// this is the one named exception, not a precedent to copy outward or "fix".
//
// THE SHIFT LINE GOES WITH IT, and that is the same fact rather than a second
// decision: Ctrl+Alt+Shift+R is a consumed no-op in iteration mode (the refusal
// is in the render route, input_key_dispatch.cpp), so advertising a shift press
// there would advertise nothing. The rule the static_assert below states —
// the hint exists exactly where a shift press does something — therefore holds
// on this form too, not only on the constant table it overrides.
//
// BOTH STRINGS LIVE HERE, beside the constant table, so the label the button
// paints and the name its hint gives cannot drift into two different words.
inline constexpr const char* kRenderIterationsLabel = "Render Iterations";
inline RedesignTooltipText redesign_button_tooltip(const AppState& a,
                                                   RedesignButton b) {
    if (b == RedesignButton::Render && a.iteration_mode_enabled) {
        return {"Render Iterations (Ctrl+Alt+R)", nullptr};
    }
    return redesign_button_tooltip(b);
}

// The toolbar's LABEL, by the same bit and for the same reason. The constant
// per-button labels live with the painter's roster half (kToolbarButtons,
// paint_handler.cpp); this answers only "does this button override its own",
// which exactly one does. MEMBERSHIP IS UNCHANGED by either override: a button
// with a tooltip keeps it in both modes, and a button with a label keeps one.
inline const char* redesign_button_label(const AppState& a, RedesignButton b,
                                         const char* table_label) {
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
    (redesign_button_tooltip(RedesignButton::Save).line2 == nullptr) &&
    (redesign_button_tooltip(RedesignButton::IconCopy).line2 == nullptr),
    "the shift hint and the shift binding must name the same buttons");

// Hoverability = enabled, plus the tabs' one extra fact: THE SELECTED TAB HAS
// NO HOVER FACE (only the inactive one lights). Kept beside the predicate it
// extends and consulted only by the hover recompute, so "a disabled button
// never sets hovered" and "the selected tab never sets hovered" are one line
// each at one site rather than a condition smeared over the painter.
//
// ROW 4'S AND THE VIEW BAR'S SELECTED BUTTONS DO HOVER, and that asymmetry with
// the tabs is the crops': both ship a selected-hover state (the accent outline
// over the selected fill) and row 3 does not. So the carve-out below names the
// tabs alone; the icon row's radios and the view bar's three are hoverable in
// both states, and their already-selected press is refused in the ACTION (the
// chord table's `radio` flag), not in their hoverability.
inline bool redesign_button_hoverable(const AppState& a, int64_t total_frames,
                                      RedesignButton b) {
    // THE OPEN DROPDOWN OWNS THE POINTER OVER THE ROWS IT COVERS — rows 2, 3
    // and 4, which refuse the hover face for as long as it is up: a lit button
    // under a popup would advertise a click the popup is about to swallow. The
    // next motion after the close re-resolves them normally.
    //
    // ROW 1 IS EXEMPT (architect 2026-08-03, naming Quit), and the reason is
    // structural rather than a taste call: the popup box hangs from its anchor
    // button's BOTTOM edge, so it BEGINS below row 1 and cannot occlude any part
    // of it — the buttons it floats over are the lower rows' — and row 1 carries
    // no tooltips at all (redesign_button_tooltip owns that membership), so
    // lighting it arms nothing that could paint over the menu. A menu bar whose
    // buttons keep answering the pointer while a menu is up is also what the
    // pointer-side switch behaviour already assumes.
    // (The menu's OWN button keeps its lit pill through the paint condition at
    // the menu-row painter, which is not this bit — and the two agree by
    // construction, both asking for the one pill face.)
    if (a.dropdown.open() && !redesign_button_in_menu_row(b)) return false;
    if (!redesign_button_enabled(a, total_frames, b)) return false;
    if (b == RedesignButton::TabA) return a.active_tab_view != 'A';
    if (b == RedesignButton::TabB) return a.active_tab_view != 'B';
    return true;
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

// THE MARKER STEM AS A POINTER TARGET (architect 2026-08-01, at the row-5 live
// test): a press within kMarkerStemGrabPx of an ENABLED marker's stem column,
// IN THE WAVEFORM'S UPPER HALF, is that marker's click. Returns the marker index
// or -1.
//
// THE CALLER GATES IT PLAIN-EXACT (architect 2026-08-01, second pass) — stated
// here because it bounds what this function is for, and spelled at the one call
// site (on_button_press, input_pointer.cpp): SHIFT and CTRL bind to the FLAG
// ALONE. Both modifiers already own a waveform gesture at the very pixels a stem
// stands on (ctrl = the strip drag, shift = the region former), so a modified
// press near a stem is not a marker hit at all and falls through to the waveform
// underneath. This function is unchanged and unconditional; only its one caller
// decides when to ask.
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
// the same reason it is not visible. One fact, not two.
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
