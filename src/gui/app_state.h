#pragma once

#include "engine_settings.h"
#include "gui_input.h"
#include "history_diff.h"
#include "playback.h"
#include "render_pipeline.h"
#include "render.h"
#include "settings_file.h"
#include "text_editor.h"
#include "measure_clipboard.h"
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
#include <optional>
#include <set>
#include <string>
#include <string_view>
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
// `c` command — at the level it stamped on the way out when its tab has one
// (ViewState::zoom_recall_level, architect 2026-08-18), at the working zoom
// when it does not — and `c` jumps to
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
// instead, of which the trim endcaps' — kTrimEndcapGrabPx /
// trim_endcap_grab_px() in render.h — is the one survivor: the flags are hit
// on their painted boxes with no halo at all, and the marker stems' grab
// constant died with their pointer surface (stems pointer-inert, 2026-08-12 —
// the record is at the retired hit_test_marker_stem's site below).
// The rule it carried outlives it and belongs to nothing in particular: a grab
// tolerance is NOT a spacing gap. Markers may sit arbitrarily close, overlap
// exactly, and cross during gestures; ordering degeneracy collapses at the
// render boundary, not at authoring time.)

// (kZoomStripPxPerLevel = 60.0 — the OVERVIEW LANE'S vertical zoom rate — is
// DELETED, producer-less (architect 2026-08-15): the lane lost its zoom
// entirely when the box became the subject of every lane gesture, so the
// dual-axis ctrl strip drag that was this constant's ONE reader is gone with
// its whole subsystem (the ruling is at OverviewDragState below). Its value
// was the vertical drag distance moving that gesture one continuous level;
// the calibration ladder recorded below survives it, being the MOUSE
// surface's, and the surviving rate constant is kNavZoomPxPerLevel.)

// THE NAV DRAG'S RATE, on its HORIZONTAL axis (architect 2026-08-14, the
// rotation: the ctrl phase reads dx where it read dy — the contract and the
// sign's derivation are at ScrollDragState below). Horizontal drag distance
// (px) that moves the navigation drag's zoom phase by one continuous level.
// THE PRODUCT'S ONLY DRAG-ZOOM RATE since 2026-08-15, the overview lane's own
// having gone with the lane's zoom; it was deliberately kept separate from
// that one while both existed, a px-per-level measured across ~1920 px of
// horizontal room being a different ergonomic from one measured across the
// lane's vertical, and one shared constant would have made a retune of either
// gesture silently move the other.
//
// THE PINCH IS THE DERIVATION, which is what matters and not the ladder of
// numbers behind it (architect 2026-08-14, from the rig, having driven both
// surfaces one after the other: "ninety pixels is still a little too fast
// compared to the touch screen; the touch screen rate of motion is very
// natural and the mouse is too fast... the waveform is clamped at five
// hundred, so let's try two hundred and forty, for a multiple of the sixty").
// THE GLASS IS THE REFERENCE and the desk is tuned to it: a pinch's level is
// log2 of the FINGER-GAP RATIO, so its px-per-level is not a constant at all —
// it depends where the gap starts — but at a comfortable gap one level costs
// roughly 150-250 px of single-finger travel, and the desk's rate has to sit
// inside that band. That band is what "the touch screen rate is very natural"
// was measuring, so the two surfaces move the view at about the same rate for
// the same hand motion, which is the whole point of the rotation that put the
// desk's zoom on the horizontal in the first place.
//
// 200 IS THE LANDING POINT INSIDE THE BAND, after 60, 90 and 240 were each
// driven from the rig: 240 reached the band but sat at its SLOW END, and the
// architect, having driven it, found it "a little too slow". 200 is the same
// derivation with the overshoot taken off, not a new one.
//
// WHAT IT COSTS, AND WHY THAT IS AFFORDABLE: the whole [kMinZoom, effective
// ceiling] span is roughly 3200 px of travel — over a screen and a half at the
// deployment size — and that is fine BECAUSE OF THE CAPTURE. The notional-x
// freeze (its record is at GuiPlatform::set_notional_x_frozen) is what makes
// the zoom phase's sideways travel unlimited: the pointer's notional position
// stops accumulating while the ctrl phase spends that travel on the level, so
// the hand never runs into a window wall and the span is reachable inside one
// gesture whatever the screen is.
//
// Architect-tunable on the rig, exactly as the vertical one is.
constexpr double kNavZoomPxPerLevel = 200.0;

// (THE DIRECTIONAL SEGMENT AXIS LOCK IS DELETED — architect 2026-08-14, the
// one-model ruling: PAN BY DEFAULT, ADD THE ZOOM MODIFIER AT ANY TIME, DROP
// IT AT ANY TIME — the zoom modifier being a SECOND FINGER on glass and CTRL
// on the desk, live MID-GESTURE in both directions. The lock answered "which
// axis did this drag mean?", and that question is no longer asked: the
// modifier answers it directly and reversibly, so the ctrl phase of the one
// navigation drag is ZOOM ONLY and the plain phase is PAN ONLY, each axis
// exact by construction rather than by classification (the drag's contract is
// at ScrollDragState below).
//
// THE CALIBRATION SUCCESSION, kept so the ladder is revivable and not re-run:
// every rung field-tested on the desk, 2026-08-12..14 — the cumulative
// off-axis curve f(D) = D*|D| / (|D| + knee) at knee 48, then 200, then 600
// (the travel-waking shape WAS the wall bug: a saturated wall drag is exactly
// where the hand keeps sweeping, so D accumulated and the arc dumped into
// zoom) -> the flat 0.08 damping factor (held the wall, still let a long arc
// drift the other axis) -> THE HARD PER-SEGMENT LOCK (a segment classified
// once at 8 px Chebyshev on a 45-degree diagonal, then 60 degrees favouring
// pan; the off axis a literal zero for the segment's life; a 75 ms motion
// pause re-aiming it) -> DELETED, made unnecessary by the live modifier. THE
// LOCK WAS NOT A FAILURE — the architect ratified it in the field ("zoom now
// works as expected") — it answered a question that stopped existing when the
// LIVE MODIFIER answered it directly and reversibly: pan by default, add the
// zoom modifier at any time, drop it at any time, so nothing has to infer
// which axis a drag meant.
// The two-finger finger-agreement variant of the same lock lived one day on
// glass and is recorded in touch.md's two-finger succession. The retired
// constants (kStripSegmentClassifyPx 8.0, kStripSegmentZoomAngleDeg 60.0,
// kStripSegmentPauseMs 75) and the ruled last resort they carried are git
// history; the generic 8 px press-becomes-drag gate below is a DIFFERENT job
// (kDragMovedThresholdPx — press-becomes-drag, not classification) and
// stands untouched.)

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

// THE REGION IS THE TRIM (architect 2026-08-18, uniting two loose ends into one
// state): the trim bar is 9 px — right for a mouse, unusable with a fingertip —
// so trim gained a SECOND, LARGE surface on the waveform, summoned when needed
// and dismissed after. There is no "set trim from region" any more, because
// SETTING THE REGION IS SETTING THE TRIM. One state, two painted surfaces: the
// bar that is always there, and this overlay, which is shown and hidden.
//
// DERIVE, DO NOT STORE, and everything else follows from it: the overlay is
// painted FROM THE TRIM every frame, exactly as the bar already is, and no span
// rests anywhere. The consequences are all free — a tempo change in target view
// re-derives the overlay on the next frame (nothing to invalidate), the two
// surfaces cannot drift, and hiding the overlay is a visibility bit while trim
// stays always-set. SO THIS STRUCT IS THAT BIT AND NOTHING ELSE; the span's
// owner is trim_overlay_span (below), which converts the resting trim bounds
// into the ACTIVE-domain frames the painter and the hit test both want.
// (The a_frame / b_frame pair this carried until 2026-08-18 — active-domain
// endpoints in drag order, normalized at read time — is deleted with the model
// that needed it: a free scratch span that bare `x` committed to trim. THE ACT
// went, not the KEY: with setting the region being setting the trim there was
// nothing left for `x` to commit, so the key was REPOINTED onto this struct's
// own show/hide toggle — handle_toggle_trim_region, which owns that record and
// is the one place to read it. Git holds the deleted model.)
//
// THE LIVE SWEEP IS THE ONE THING NOT AT REST, and it stores no span either:
// the former holds its ANCHOR alone (RegionDragState::anchor_frame) and writes
// the moving pair straight into the trim per motion event, ordered lo/hi, so
// even mid-gesture the overlay derives from the trim like everything else. The
// ruling is about the RESTING state; this gesture simply turned out to need
// nothing extra to keep.
//
// THREE MOTIONS WRITE THE TRIM ON THIS SURFACE, each routed through trim's OWN
// tail rather than re-deriving trim's rules here:
//   * a BOUND drag IS the single-bound endcap drag — it clamps INCLUSIVELY at
//     its partner, and a coincident release resets to the full window through
//     auto_clear_crossed_trim, which is the drag's own route to clearing the
//     trim and, since 2026-08-19, EVERY former's: the sweep's coincident
//     release lands on the same compare;
//   * a drag INSIDE is the BRIDGE drag (rigid delta, invariant gap, no partner
//     wall);
//   * a SWEEP — the shift+drag former and the touch region hold — is a direct
//     trim write under no width rule at all (write_trim_from_sweep,
//     input_trim.cpp), whose own coincident release reaches that same
//     whole-song reset.
// All three take the trim-write class whole: the setter's deselect, the
// trim-mutation playback stop, and the playhead parked at the new trim start AT
// THE RELEASE ONLY (a per-frame cursor chase would fight the gesture moving the
// bounds — the rule and its membership are at the head of input_trim.cpp).
//
// THE VISIBILITY BIT'S WRITERS. SHOWN by BARE `x` and the icon row's
// IconShowRegion button, one toggle over one act (handle_toggle_trim_region,
// input_trim.cpp), whose show half also brings the span into view; by THE
// SWEEP'S FIRST ACCEPTED TRIM WRITE through the one raise owner
// show_trim_region_overlay (input_handler.h, which carries its whole call-site
// inventory, the no-framing rule and the `h` carve-out) — its ONE caller, at
// the write since 2026-08-21 rather than at the arm, since the overlay derives
// from the RESTING trim and a press-time raise could only show the window the
// stroke was replacing (the 9 px band's three press claims had left that
// inventory on 2026-08-20, a lane touched by a pointer being its own display of
// the trim while the big surface exists for glass); and
// by the FILE LOAD, which resets this struct so a new piece starts hidden.
// HIDDEN by that same toggle, by THE SWEEP'S COMMIT (commit_region_sweep,
// input_pointer.cpp — unconditional at every end path, so the raise is
// bracketed by the stroke that earned it), and by clear_region_highlight,
// whose declaration (input_handler.h) states THE RULE: the overlay hides when
// the playhead's position in the music changes, when a marker is touched and
// when the sweep ends, and at no other time. HIDING DISCARDS NOTHING — the trim
// persists and re-showing restores an identical overlay, which is what makes
// the rule safe to state as a rule. The trim's other gestures never hide, and
// need no exclusion to say so: they write the cursor direct and so reach
// neither movement owner.
//
// ONLY THE WAVEFORM ANSWERS, by the architect's ruling: the RULER and the
// MARKER LANE stay plain navigation surface throughout, which is what keeps a
// pan and a zoom reachable while the overlay covers the waveform entirely. The
// hit verdict's owner is GuiInputHandler::region_manipulation_hit
// (input_pointer.cpp).
//
// READ-ONLY-LEGAL, exactly as the trim bar's own gestures are: trim is BAND,
// not authored content (the ruling at read_only_key_blocked).
struct RegionState {
    bool shown = false;   // the overlay's visibility — the whole of the state
};

// The hit verdict over the waveform overlay. Its owner is
// GuiInputHandler::region_manipulation_hit (input_pointer.cpp), which is
// meaningful only while app.region.shown; the two bounds are the DERIVED span's
// endpoints projected to columns on the DISPLAYED (plate) basis — the same
// basis the overlay paints on, through the painter's own region_columns owner,
// so a grabbed bound is exactly a painted one. BoundLo / BoundHi are the trim's
// begin and end respectively: a resting trim pair is always ordered (a crossed
// one resets to the full window at every commit), so left/right and begin/end
// are the same distinction here.
//
// THE GRAB BAND is trim_endcap_grab_px() per side — the SAME 10 px the trim
// endcaps and the overview box edges take, on purpose. OVERLAP resolves
// NEARER-BOUND-WINS with ties to the LO bound (hit_test_overview_endcap's own
// rule), which keeps both bounds reachable down to a 1 px span. Inside the span
// but outside both bands is Move. An OFFSCREEN bound is simply not grabbable:
// clamping it to the edge would manufacture a handle where nothing is painted.
enum class RegionHit { None, Move, BoundLo, BoundHi };

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
    // Full pre-drag marker state. Captured in begin_drag, which runs at the
    // THRESHOLD CROSSING rather than at the press (PendingMarkerPress above owns
    // that deferral and why it is exact), so commit_drag
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
    // 2026-07-20 decoupling): the CLICK ACT the arming PRESS ran
    // (run_marker_click_act — press-time again since 2026-08-17) LANDS the
    // playhead on the pressed marker (source_frame_to_active_domain then
    // clamp_playhead_to_live_domain), so it is coincident by construction, and the
    // mid-motion follow plus commit_drag's unconditional land keep it there —
    // the drag tows it UNCONDITIONALLY, so a later Space auditions FROM it. The
    // lead-in workflow (parking
    // the playhead upstream to audition the approach) that the decoupling
    // served is supplied by the audition scrub instead. Only the RESTING
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

// State for THE SWEEP — the region former, which since 2026-08-18 IS A DIRECT
// TRIM WRITE (the region is the trim; the model is at RegionState). Two
// entries, shift+drag on the desk and the hold-beat region hold on glass
// (architect 2026-08-12, the eighth glass ruling, PAN-PRIMARY: the plain drag
// is the grab-pan, so the sweep is the deliberate act and takes the secondary
// form on both devices). BOTH SURVIVE IN FULL and the hold is not the lesser
// half: a finger has no modifier, so the hold is the finger's ONLY route to a
// sweep.
//
// The SHIFT-exact PRESS on the NAVIGATION SURFACE — the WHOLE waveform, the
// RULER lane, and the MARKER lane's empty stretches (a shift press on a FLAG
// stays the range click) — does its press-time work (deselect-all, playhead
// placement, live-playback reseek — it never SELECTS a marker) and arms this
// drag; motion past the shared press-becomes-drag threshold
// (kDragMovedThresholdPx) writes the trim from the press frame to the pointer
// column, ordered, through the sweep's own trim writer (write_trim_from_sweep,
// input_trim.cpp — which enforces no width, only the song walls). THE PRESS ITSELF
// WRITES NO TRIM: a motionless shift click is the placement and nothing else.
// THE STROKE'S FIRST ACCEPTED TRIM WRITE SHOWS THE OVERLAY (architect
// 2026-08-19 for the raise, moved off the press 2026-08-21) through the one
// raise owner, at every entry but the `h` view's, which writes no trim: the
// surface being drawn on is visible while it is drawn, and since the overlay
// derives from the trim the sweep writes per motion event it tracks the rest of
// the stroke live. RAISING AT THE PRESS SHOWED THE WRONG REGION — the resting
// one the stroke was about to replace — which is why the raise sits at the
// write. THE SHOW IS BRACKETED BY THE STROKE (architect 2026-08-20):
// commit_region_sweep collapses it again at every end path, so a motionless
// shift click shows nothing at all and a stroke that draws a window leaves that
// window on the bar alone.
//
// THE ANCHOR IS THE WHOLE OF THE GESTURE'S GEOMETRY. Under derive-do-not-store
// there is no span field to extend: this holds the press frame, the trim holds
// the pair, and the overlay derives from the trim on every frame including the
// ones this gesture writes.
//
// Under SELECTION FLOWS DOWNWARD ONLY (architect 2026-07-23) the drag does NOT
// select the span's markers — the selection stays EMPTY from the press's
// deselect-all through release, and the trim writes deselect again on their own
// setter rule. THE DRAG CARRIES THE PLAYHEAD (architect 2026-07-30): each
// changed column writes the cursor to the MOVING endpoint, with no viewport
// scroll and no playback reseek per motion — and the RELEASE then parks it at
// the committed trim start, every trim write's own tail.
//
// THREE ARMS REACH arm_region_drag_at (membership re-derived 2026-08-18): the
// LIVE shift former above, through the one placement body
// (place_playhead_and_arm_region), the `h` history view's OWN shift former
// (handle_history_mode_press), which clears the MODE's focus + selection
// instead of the store selection and rides the same motion path, and the TOUCH
// REGION HOLD's begin (begin_touch_region — the pan zone's stretched window
// expiring at the beat), which forks on the mode into those same two recipes
// and drives the drag through the region hooks. THE `h` VIEW IS CARVED OUT OF
// THE TRIM WRITE EXPLICITLY (the motion path's own mode gate): that view
// promises the trim window is untouched throughout, so its sweep carries the
// playhead and writes nothing.
//
// THE RELEASE-TIME SLIVER DISSOLVE IS RETIRED with the free span it protected
// (2026-08-18): a jitter drag that crosses the gate and rests a two-pixel span
// no longer leaves a sliver highlight — it COMMITS a two-pixel trim, exactly as
// drawn. The minimum width floor that briefly widened such a stroke is retired
// too (architect 2026-08-19: the enforced minimum was distracting and too short
// to be worth its machinery), so the sweep has no width rule of any kind left —
// a stroke that collapses onto its own anchor clears the trim to the whole song
// at the release, and Shift+X is the way back from anything else.
//
// ESC DOES NOTHING TO A DRAG IN FLIGHT: pointer gestures have no cancel, so a
// mid-drag Esc is swallowed by the drag-modal gate and the sweep keeps writing
// under the pointer; any end commits what stands. This state was the first to
// lose its pre-press snapshot — the whole family followed. The rule is at the
// drag-modal gate (input_handler.cpp). Session-only, never undoable — trim is
// outside the undo stacks by ruling.
struct RegionDragState {
    bool    active       = false;
    bool    moved        = false;  // crossed the threshold into a real drag
    // Whether this sweep has actually WRITTEN a trim bound. It is the end
    // owner's commit gate (commit_region_sweep): a sweep that wrote nothing —
    // motionless, refused by geometry, or run inside the `h` view — owes no
    // commit tail, so no playhead parks and no render triggers behind it.
    bool    wrote_trim   = false;
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

// WHICH HALF OF A MARKER'S BOX A PRESS LANDED ON (2026-08-19). The flag and its
// blue MEASURE box are one clickable surface for press, drag and select — one
// marker, one rect — and this distinction exists for the DOUBLE-CLICK ALONE,
// which opens a different editor on each half: the flag span opens the PAYLOAD
// editor (warp only, its own gates), the measure span the MEASURE editor (both
// columns, both views). It is resolved from the painter's own published
// boundary (FlagHitRect::measure_boundary_x) and never re-derived, and it is
// stamped at the FIRST press so the pair of clicks agrees about what it is
// opening even if the box has since been repainted at a different width.
enum class MarkerClickSpan { Flag, Measure };

// THE MARKER FLAG'S PENDING PRESS — armed by the PLAIN flag-box press ALONE,
// AFTER the click has already acted (architect 2026-08-17: CONTENT ACTS THE
// MOMENT ITS IDENTITY IS CERTAIN — a flag press can only mean one thing, so
// the click act runs AT THE PRESS, and this record holds only the two things
// that genuinely belong to a later edge: the reposition DRAG the plain press
// may become, and the double-click SEED that only a motionless release may
// write. The whole model is at run_marker_click_act, input_pointer.cpp).
//
// THE PRESS ACTS AND ARMS. run_marker_click_act runs at the press (stop, the
// three-way selection fork, the land, the region hide, and the plain arm's
// double-click consume-open); the PLAIN shape then arms this record. SHIFT and
// CTRL arm nothing — they have no drag to become and their click has already
// committed — and a CONSUMED double-click open arms nothing either (the editor
// owns input, and the consume must preempt the drag arm). A MOTIONLESS RELEASE
// seeds the next Marker double-click candidate and nothing else (the seed is a
// release act by family rule — only the release knows the press stayed still).
// A CROSSING of kDragMovedThresholdPx (Chebyshev from the press; the one
// generic 8px gate shared by every press-becomes-drag surface) begins the
// reposition drag — the click's acts already stand from the press, so the
// crossing runs no act. A lost button, the force-end finalizer and the touch
// layer's ABNORMAL end (the motionless-hold upgrade) disarm and seed nothing —
// none of them is a clean click sequence, and the click itself is not theirs
// to take back: it committed at the press, and undo is the mitigation.
//
// SUPERSEDED IN BOTH DIRECTIONS, and each model was correct for its own rule:
// until 2026-08-15 the press committed the whole click and armed this state
// for the drag alone; the act-at-lift sweep of that day then deferred the
// whole click to the lift for all three modifier shapes ("all actions should
// be on mouse-up / finger-up"); and the architect's first real drive of that
// sweep reversed it here on 2026-08-17 — the flag double-click into the editor
// was "a tad slow compared to the Enter key", and the deferral's only defense
// (a double-click's second press becoming a drag) is a nonexistent use case:
// "if I'm double clicking specifically to do the double click action, I would
// never double click into a drag". The refined model: lift-deferral is
// justified ONLY by genuine press ambiguity — a press that routinely starts a
// drag, the navigation surfaces' deferred click — and a flag press is not one.
// (Older still: the GROUP-drag deferral of 2026-07-23, which withheld a click
// so begin_drag could seed the intact group, died 2026-07-29 with the group
// drag itself — groups are never moved, the doctrine at the head of
// position_nudge.h.)
//
// Deferring begin_drag to the crossing keeps its pre-drag snapshot (the undo
// payload) and its wall math exact — nothing mutates the store between press
// and crossing; the selection capture that used to be on that list was deleted
// with the cancels (2026-07-29).
//
// THE TWO AUTHORING GATES GUARD THE DRAG, NOT THE CLICK, and they live at the
// CROSSING: a read-only tab and an off-home column still select, still land
// the playhead and still open no editor (read-only protects the AUTHORED
// MUSICAL CONTENT — the marker stores and the engine settings — and a
// selection is navigation), so the plain arm itself is unconditional: even a
// press whose drag will refuse must arm, because the motionless release still
// owes the SEED.
//
// Session-only, never serialized. Cleared on the crossing (either the drag
// takes over or the arm is spent), on release / lost button, by the force-end
// finalizer, and on file load. Esc does nothing (pointer gestures have no
// cancel — the rule at the drag-modal gate in input_handler.cpp).
struct PendingMarkerPress {
    bool active         = false;
    int  marker         = -1; // marker index the press hit (active view's list)
    int  press_x        = 0;  // press position (window px): the gate + drag
    int  press_y        = 0;  //   anchor + the release-side seed's position
    // WHICH HALF OF THE BOX THE PRESS LANDED ON, stamped here at the press from
    // the painter's published boundary and carried to the double-click seed at
    // the motionless release — the same reason the POSITION is carried: the
    // seed describes the press, and only the release knows the press was a
    // click. It reaches nothing else; the drag this pending may become is one
    // gesture on one marker whatever half started it.
    MarkerClickSpan span = MarkerClickSpan::Flag;
};

// (No pending TEMPO drag, and no TempoDragState: the whole target-view tempo drag
// is DELETED, architect 2026-07-29 — the tempo surface is the bare Up/Down cent
// step alone. The delete list and the do-not-re-propose note live at the head of
// marker_drag.h. So the reposition drag above is the ONLY pointer marker gesture,
// and W+target has no pointer authoring gesture at all.)

// Pending trim cap/bridge drag, armed by a PLAIN (unmodified) left press in the
// top-strip TRIM BAR lane (an endcap rect, or the bar's inter-cap bridge span)
// and by the ctrl / ctrl+shift bound-set clicks, which arm the single-bound
// drag on the bound they just set — AT THEIR THRESHOLD CROSSING since 2026-08-15
// rather than at their press, the bound set having moved to the lift as the
// ONE surviving deferred click (PendingClickAct below owns that hand-over,
// and the drag it hands to is byte-for-byte the one it always was: the set runs
// first, at the press column, and this pending is armed on the bound it wrote).
// (The trim surface arc scattered these arms
// across the merged band's modifiers for one day, 2026-08-11..12 — the alt
// bridge press and the ctrl deferred-set pending died with the arc's revert.)
// The trim sibling of PendingMarkerPress: the press CLAIMS the cap/bridge geometry
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
    // ARMED FROM THE WAVEFORM OVERLAY rather than from the trim bar
    // (2026-08-18, when the region became the trim): the bound and bridge drags
    // are the SAME drags on a second surface, so the arm is the same pending —
    // but on that surface a MOTIONLESS press-release is not a manipulation at
    // all. It falls to the waveform's ORDINARY CLICK ACT, exactly what a press
    // one pixel outside the overlay would have done, which is what keeps the
    // hide-by-clicking escape reachable under a full-window overlay. The bar's
    // own presses leave this false and stay the consumed nothing they have been
    // since 2026-07-30. THE CLEAN RELEASE ALONE runs it: a lost button and the
    // force-end finalizer are not clicks (the standing abnormal-end rule).
    bool waveform_click_act = false;
};

// Which act a PendingClickAct is holding. None = nothing armed.
enum class PendingClickKind {
    None,
    TrimBoundSet,     // the trim bar's ctrl (begin) / ctrl+shift (end) bound set
};

// THE ONE SURVIVING DEFERRED CLICK — the trim bar's ctrl / ctrl+shift bound-set
// clicks, and nothing else (architect 2026-08-17: CONTENT ACTS THE MOMENT ITS
// IDENTITY IS CERTAIN — the pointer's lift-deferral is justified ONLY by
// genuine press ambiguity, a press that routinely starts a drag, and the
// bound-set press IS the endcap drag's arm, so it is exactly that. The
// act-at-lift sweep's other four acts of 2026-08-15 — the trim bar's framing
// double-click, the empty lane's create double-click and the `h` view's three
// diff-flag clicks — went back to acting AT THE PRESS with that ruling: a
// double-click's second press acts immediately on every double-click surface,
// "if I'm double clicking specifically to do the double click action, I would
// never double click into a drag", and a diff-flag press has no drag to become
// at all).
//
// THE SHAPE IS THE PRODUCT'S: the press ARMS a record carrying what the act
// will need (its press POINT and which bound the click writes); a MOTIONLESS
// release runs the set, RE-ASKING every live gate (they all live inside
// set_trim_bound_at_click); a CROSSING of kDragMovedThresholdPx runs the set
// and hands over to the endcap drag; and a lost button, the force-end
// finalizer and the touch layer's ABNORMAL end all commit nothing. Read
// PendingMarkerPress above for the neighbouring press-time model.
//
// THE ACT ACTS ON THE ARMED SUBJECT — the press column — and never on a re-hit
// at the release's coordinates (touch delivers the press at the finger's DOWN
// point and the release at its last position, and the press point is what the
// user aimed at, sub-threshold travel being jitter).
//
// THE CROSSING IS THE POINT OF THE DEFERRAL: it RUNS THE SET at the press
// column and then hands over to the endcap drag on the bound it just set, so a
// ctrl-press-and-drag is byte-for-byte the gesture it has always been (set at
// the click, then drag that bound live) and only the timing of a motionless
// click is the lift's. The set's whole tail travels as one unit — the
// strictly-inside refusal, the playback stop, the commit tail's playhead park
// (and NOT an overlay hide — the trim writes are that inventory's one excluded
// class since 2026-08-18), the setter's deselect — because they are one act
// (set_trim_bound_at_click).
//
// Session-only, never serialized. Cleared on the crossing (the trim drag takes
// over), on release / lost button, by the force-end finalizer, and on file
// load. Esc does nothing (pointer gestures have no cancel — the rule at the
// drag-modal gate in input_handler.cpp).
struct PendingClickAct {
    PendingClickKind kind = PendingClickKind::None;
    int  press_x  = 0;   // press position (window px): the gate + the act's column
    int  press_y  = 0;
    // Which bound the click writes (ctrl = begin, ctrl+shift = end).
    bool is_begin = false;

    bool active() const { return kind != PendingClickKind::None; }
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

// (THE DUAL-AXIS ZOOM/PAN STRIP DRAG IS DELETED WHOLE — its state, its arm and
// its per-event apply — architect 2026-08-15, redesigning the overview
// lane: "zoom is not what I'm looking for in the overview strip, because the
// overview strip can indirectly control zoom by directly controlling the
// outline box". The lane's gestures act ON THE BOX and the zoom follows from
// the box's span, so the CTRL press that armed this gesture there — its LAST
// entry — is gone and the subsystem went with it: the state and both of its
// bodies, the anchor stem's third producer, the lane's Zoom
// cursor arm, the gesture's term in any_pointer_gesture_active and the vertical
// rate constant kZoomStripPxPerLevel (the one name kept spellable here, since
// a constant is what someone would grep for before re-adding one).
//
// THE ENTRY SUCCESSION, each step a ruling, kept because the zoom-strip
// concept has now had three homes and been withdrawn from all of them: a
// dedicated zoom LANE (deleted 2026-07-31); the ctrl-exact WAVEFORM press;
// the RULER's own plain entry (born with row 5 as "the zoom strip reborn",
// deleted for good 2026-08-12 at the sixth glass ruling); the NAVIGATION
// SURFACE's ctrl press (which became the ONE nav drag's LIVE ZOOM MODIFIER on
// 2026-08-14 — ScrollDragState below — rather than being deleted, and is where
// a ctrl drag zooms today); and the OVERVIEW lane's press, plain at its
// landing, ctrl-exact from that evening's rework, gone now.
//
// WHAT THE GESTURE WAS, in one line, since the box drags inherited its
// application chokepoint and not its shape: dual-axis and INCREMENTAL off the
// live level and viewport, dx panning and dy zooming per motion event about a
// song anchor with the Ableton edge trick rebinding that anchor at the visible
// bounds, pointer-captured, stem-at-press. Viewport::apply_strip_drag_zoom —
// the chokepoint it was named for — STAYS: the nav drag's zoom phase, the
// touch pinch and the overview box's own EDGE drags all drive it.)

// THE PLAIN PRESS ON THE NAVIGATION SURFACE — a PENDING CLICK that becomes the
// GRAB-PAN (architect 2026-08-12, the eighth glass ruling, PAN-PRIMARY: pan is
// the most common gesture, so it takes the primary drag; the alt+drag that
// carried this machinery is DELETED, alt's pointer vocabulary now empty). The
// NAVIGATION SURFACE is the WHOLE waveform — BOTH HALVES since 2026-08-13, in
// every view — plus the RULER lane plus the MARKER lane's empty stretches (the
// lanes are "essentially an extension of the upper half" — his words — since
// the waveform-height clamp put them in easy reach). THE LOWER HALF JOINED
// when the press-time audition scrub moved to the lift (architect 2026-08-13:
// "the playhead scrub is an outlier. We do everything on lift the finger or on
// mouse up, but the playhead scrub, we do right on mouse down. We should remove
// that. And that should allow the dragging on the lower half of the waveform as
// well, the pan"), so the halves differ in ONE thing — WHICH ACT the motionless
// release runs — and in nothing else. A plain press anywhere on the surface
// arms this and DOES NOTHING ELSE — nothing pops at press anywhere now (the
// deferred-hide model, the one-day ruler former's own pattern generalized
// to the whole surface):
//   * a MOTIONLESS RELEASE (never crossed kDragMovedThresholdPx) runs THE
//     CLICK ACT at the press column, forked on the pressed half
//     (run_nav_click_act, input_pointer.cpp). UPPER half — everything the old
//     press-time placement did: deselect-all (the mode-focus clear in the `h`
//     view), the overlay hide, playhead to the column, live-playback reseek,
//     follow override. LOWER half — ONE AUDITION SCRUB ACT at the column (stop
//     a live session, else launch), which touches no selection, no region, no
//     cursor and no follow state: that pair of omissions is the halves' one
//     difference, read honestly as two, and both predate this ruling.
//     Playback state is read AT the
//     release — the press touched nothing, so the readings agree, and a
//     session that ended naturally under the hold is answered honestly.
//     THE PRESS COLUMN STAYS THE COLUMN THE USER AIMED AT because the FOLLOW
//     CHASE IS PAUSED for the press's whole life — the pre-paint gate refuses
//     under any live pointer gesture (this pending included) and under any
//     touch contact, so no autopager can slide the song beneath a held
//     column between the press and the release that converts it (main.cpp);
//   * CROSSING the threshold is the GRAB-PAN, the alt+drag machinery whole:
//     the pointer CAPTURE begins at the crossing (begin_strip_pointer_capture
//     — cursor-hide + lock, unbounded virtual travel while the viewport
//     clamps at the song walls; the cursor reappears at the pointer's
//     NOTIONAL position — never the travel ledger, a pan having no
//     anchor-stem override, and that position WRAPS edge to edge across the
//     waveform rather than pinning at a bound, so a pan of several screens
//     still leaves the cursor somewhere ordinary), the crossing event folds
//     the whole press→crossing delta (last_x stays at the press until then),
//     and each event pans 1:1 through scroll_viewport's funnel — which is
//     what suppresses follow for the session (the pan producer class at
//     follow_overridden_for_session). A PAN IS A PURE VIEWPORT MOVE: it moves
//     NO playhead, hides NO overlay and clears NO selection, seeds nothing.
// THE ZOOM MODIFIER IS CTRL, LIVE MID-GESTURE (architect 2026-08-14, the
// one-model ruling: PAN BY DEFAULT, ADD THE ZOOM MODIFIER AT ANY TIME, DROP
// IT AT ANY TIME — ctrl playing the second finger's part). THE TWO SURFACES
// ARE CO-EQUAL, in the warp / phase-reset sense: the LAPTOP goes first ("way
// more precise and much faster") and the TOUCH PANEL is the more natural one
// and the one available on the go — neither is the model the other
// translates, and asymmetry between them is ACCEPTED WHERE GENUINE, exactly
// as warp markers carry information where phase resets carry only placement.
// This is ONE drag with
// two phases, not two gestures, AND BOTH PHASES NOW READ THE SAME AXIS
// (architect 2026-08-14, THE ROTATION — from the rig: "we should rotate the
// axis of zoom, because on the touchpad zoom is also a horizontal pinch motion
// — it just happens to have two fingers"): while ctrl is up each event's dx
// pans 1:1, while ctrl is held each event's dx zooms
// (dx/kNavZoomPxPerLevel, about the seated pivot), and dy is DISCARDED IN
// BOTH. The vertical axis has left this drag entirely.
// THE MODIFIER CHANGES WHAT HORIZONTAL TRAVEL MEANS rather than which axis is
// live, and that is the whole argument: on glass ONE finger sliding sideways
// pans and TWO fingers sliding sideways pinch, so the desk's plain-drag /
// ctrl-drag pair reads as the same sentence the panel already speaks. THIS IS
// A GENUINE CONVERGENCE, not a symmetry chased for its own sake — the standing
// ruling is that asymmetry between the two surfaces is ACCEPTED WHERE GENUINE
// (the co-equality paragraph above), and what happened here is that the
// difference stopped being real: the same hand motion means the same thing on
// both, so there is nothing left to except.
// THE SIGN: RIGHT ZOOMS IN, LEFT ZOOMS OUT (`new_level = zoom_level -
// dx/kNavZoomPxPerLevel`, and a smaller level is deeper in). THE DERIVATION IS
// THE PINCH THIS DRAG STANDS IN FOR (architect 2026-08-14, from the rig, on
// the dominant hand's own evidence: "what it should really do is imitate what
// the right hand does, because I'm right handed — on the touch screen, for
// zoom, if I move my fingers apart, the right finger is moving to the right,
// and that zooms in"): take the DOMINANT HAND'S FINGER as the one the mouse
// imitates, and spreading the fingers apart moves it RIGHT and zooms IN while
// bringing them together moves it left and zooms OUT.
// THE SUPERSEDED DERIVATION IS KEPT VISIBLE because it is the
// plausible-sounding wrong answer someone will re-derive: it reasoned from the
// PAN — dragging LEFT advances the view forward through the piece, and a piece
// OPENS at full zoom out, so forward motion ought to mean in. IT IS OUTRANKED
// BY THE PINCH ARGUMENT BECAUSE THE ROTATION ITSELF CAME FROM THE PINCH: the
// axis moved onto the horizontal to speak the sentence the glass already
// speaks, so a sign taken from the pan would have had the two surfaces
// disagree on the one thing the rotation existed to make agree. The touch
// pinch needed no sign of its own either way, being a distance ratio.
// THE RATE IS ITS OWN CONSTANT since the rotation (kNavZoomPxPerLevel, above),
// and since 2026-08-15 it is the product's ONLY one: it was separate from the
// overview lane's vertical rate while that lane still zoomed, and the lane's
// zoom is gone (the record at the deleted kZoomStripPxPerLevel).
// THE ZOOM PHASE ALSO FREEZES THE POINTER'S OWN X, AND
// THAT IS A SECOND STATEMENT RATHER THAN A RESTATEMENT OF THE ARITHMETIC
// (architect 2026-08-14, from the rig: "I've been operating under the
// assumption that the zoom control would lock the x position... we need to
// clamp to zero horizontal movement on zoom"). THE ROTATION MADE THE FREEZE
// MORE NECESSARY, NOT LESS: the zoom phase now SPENDS its lateral travel on
// the level, and the pointer's position must not spend it a second time.
// Without the freeze a zoom would be CAPPED AT THE WINDOW'S WIDTH, because the
// notional position clamps into the surface where the travel ledger does not
// — so the freeze is exactly what keeps the zoom's travel unlimited, as the
// vertical axis was unlimited by having no notional coordinate at all.
// THE ORIGINAL PREMISE IS SUPERSEDED BUT THE HISTORY IS KEPT, because it is
// how the freeze came to exist and it is still true of the model it was
// written for: back then the zoom DISCARDED dx, the pointer's notional
// position went on accumulating every pixel of it, and because nothing on
// screen answered that travel it was invisible — so a later ctrl-down seated
// the pivot far from where the pointer was believed to be, and a zoom→pan
// switch's release restored the cursor out there too. The freeze is asserted
// at the crossing and at every ctrl edge and lives where the position is
// accumulated (GuiPlatform::set_notional_x_frozen); the TRAVEL LEDGER is
// untouched, so this changes no delta anywhere, including this drag's own.
// THE Y HAS NO
// TWIN, deliberately — there is no notional y to freeze, and the restore's
// press-row y is an unchanged ruling (the reasoning is recorded at
// GuiPlatform::notional_pointer_x_).
// THE MODE FOLLOWS THE MODIFIER AT ITS OWN EDGE
// (architect 2026-08-14, from the rig: "if I let go of control, the zoom stem
// should disappear. It doesn't disappear until I start moving the mouse") —
// ONE BODY, sync_nav_drag_mode, with TWO callers. The run loop's SETTLED-STATE
// TAIL answers the motionless edge, the pointer cursor's own owner and its own
// argument: a loop boundary is after every write, so one consumer there
// answers every route that can move the modifier, the keyboard-focus loss that
// moves it with no event to announce it included. The top of the MOTION ARM is
// not redundant with it: a dispatch batch can carry the modifiers event and
// then a motion with no loop tail in between, and that motion must apply in
// the mode the user is already holding. The superseded model synced in
// on_motion ALONE, which is why a released ctrl used to leave the stem
// standing and the capture's restore stamped Zoom until motion resumed. THE
// RE-SEAT RULE, one per direction, is what makes every switch jump-free:
//   * ctrl DOWN (pan -> zoom): ONE STEP, and CTRL MEANS ONE THING — the pivot
//     is SEATED at the pointer's current notional column, EVERY time, and
//     nothing else happens at the edge (architect 2026-08-14, undoing his own
//     ctrl-down pop of hours earlier: the hidden cursor WRAPS to the
//     waveform's opposite bound rather than pinning at the one it reached, so
//     it is never stranded out there for a pop to bring home —
//     GuiPlatform::notional_pointer_x_ carries that record). What is stored is
//     the SONG FRAME under that column (the seat and the withdrawn
//     persist-across-toggles experiment are recorded at anchor_sample below),
//     and the anchor stem paints there, at the edge itself (the ctrl-armed
//     press paints it from the PRESS, the stem-at-press ruling kept). The
//     level itself cannot jump — dx is a per-event delta off the LIVE level.
//   * ctrl UP (zoom -> pan): THE GESTURE'S ARITHMETIC re-seats nothing,
//     structurally — the pan is incremental on dx from last_x, which BOTH
//     phases keep current (the rotation put both phases on that one field, so
//     the rebase is now the same quantity on both sides of the edge), so the
//     first plain event pans from the pointer's own position; the stem erases at the edge, the capture's restore-x
//     override clears there and the restore kind re-stamps to Pan.
//     THE POINTER'S NOTIONAL POSITION DOES RE-SEAT, and it is the edge's one
//     lasting write: the gesture HANDS IT THE STEM'S COLUMN
//     (GuiPlatform::set_notional_pointer_x) before dropping the override, so
//     the fallback the drop falls back TO is the stem. The zoom phase froze
//     that position at the ctrl-down column while the stem SLID with the song
//     frame it holds — anywhere clamp_viewport_start saturates — and without
//     the handover the cursor's landing depended on the order the user lifted
//     ctrl and the button (override on one order, stranded pre-zoom column on
//     the other). With it both orders land on the stem, and the pan then
//     advances from there. Where nothing clamped the stem never left that
//     column, so the write is a no-op in effect and is deliberately not
//     conditional on a clamp.
// Transitions repeat freely within one hold — pan/zoom/pan as often as ctrl
// moves — over the ONE capture, begun at the 8px crossing whatever the mode
// (a ctrl click never blinks the cursor either, superseding the old zoom
// drag's capture-at-press) and untouched by every mode edge; only the
// restore x and the restore KIND ride the switches
// (set_strip_capture_restore_kind, so the cursor comes back as the phase the
// gesture ENDED in). THE ACT STAYS PRESS-TIME: `ctrl_entry` records the
// press's own modifier, and a ctrl-armed press runs NO deferred click act at
// its motionless release (a ctrl click was never the placement) while a
// plain-armed press runs it even if ctrl is down at the release — press-time
// modifiers arm the ACT, live modifiers steer the CONTINUOUS gesture. THE
// SCOPING ARGUMENT for reading ctrl live at all is recorded at
// AppState::ChromePress::shift (the modifiers-at-press rule's own site):
// that rule protects an ARMED ACT from changing under the user's finger, and
// a viewport gesture arms no act — nothing commits, nothing can surprise,
// and every part of it is undone by moving the other way — so a live
// modifier here is OUTSIDE the rule's scope, not an exception to it.
// Navigation-class: allowed in read-only. Ends: the clean release (the click
// act, or the moved drag's phase-appropriate finalize — the pan's
// predictor-resync, the zoom phase's final apply — plus the capture end), a
// lost button (a MOVED drag
// ends like release; an unmoved press is NOT a clean click — no act, no seed),
// the force-end finalizer (same asymmetry: a pending commits nothing), and
// file load. No cancel path exists.
struct ScrollDragState {
    bool   active   = false;
    bool   moved    = false;  // crossed the threshold into the drag
    // Press position (window px): the Chebyshev gate's reference AND the
    // deferred click act's column (the point the user aimed at; sub-threshold
    // travel is jitter).
    int    press_x  = 0;
    int    press_y  = 0;
    // Pointer x (px) at the previous motion event, kept current in BOTH
    // phases; stays at the press
    // until the crossing so the crossing folds the whole delta.
    int    last_x   = 0;
    // Armed inside the `h` history view: the click act is the MODE's land
    // (clear the mode focus + selection, no store-selection touch).
    bool   history  = false;
    // The press landed on the MARKER lane's empty stretch: a motionless
    // release seeds the EmptyLane double-click candidate (the marker-create
    // double-click's first half) beside its click act. The waveform and ruler
    // seed nothing — the lane is the one double-click surface here.
    bool   seed_empty_lane = false;
    // The press landed on the WAVEFORM'S LOWER HALF (2026-08-13): a motionless
    // release runs the AUDITION SCRUB at the press column instead of the
    // placement. Read ONCE, at the press, because the press point is what the
    // user aimed at — the same reason the click act runs at press_x. Mutually
    // exclusive with the two flags above by geometry, and false in the `h`
    // view, which has no scrub half.
    bool   scrub_release = false;
    // THE LIVE MODE (the one-model ruling above): true while the drag is in
    // its ZOOM phase — seeded from the press's own ctrl at the arm, then
    // synced from mods.ctrl at every MODIFIER EDGE and every motion event
    // (sync_nav_drag_mode, the one body). While true the anchor stem paints at
    // anchor_sample's live column and each event's dx zooms; while false each
    // event's dx pans — ONE axis, two meanings, since the rotation.
    // The RELEASE reads this bit and never re-asks ctrl: it cannot be stale
    // now that the edge itself syncs it, which re-stamps the capture's restore
    // KIND there too (and drops the stem's restore-x override on the way back
    // to the pan).
    bool   zooming  = false;
    // The press was the CTRL entry: the deferred click act is NOT armed (a
    // ctrl click is not the placement — press-time modifiers arm the act) and
    // the drag opens in the zoom phase with the pivot seated at the press.
    // Never rewritten after the arm.
    bool   ctrl_entry = false;
    // THE ZOOM PHASE'S PIVOT, AND IT IS A SONG POSITION — frames, double, in
    // the ACTIVE display domain — whose COLUMN is re-derived from the live
    // viewport at every zoom event, the frame held stationary under it while
    // the level changes. The deleted overview strip drag anchored this way
    // throughout, edge trick and all (its record above), and this gesture
    // anchors the same way again since 2026-08-14.
    //
    // THE SEAT IS SCREEN-BASED AND IS NOT WHAT CHANGED. THE ZOOM STEM IS
    // PLACED WHEREVER THE CURSOR IS WHEN CONTROL GOES DOWN, VISIBLE OR
    // INVISIBLE, AND THAT IS THE WHOLE RULE (architect 2026-08-14, from the
    // rig: "how about if we say that the zoom stem is placed wherever the
    // cursor happens to be when the user presses control? Simpler rule... as
    // far as the drag, pan, same rules as current — those are pretty intuitive
    // and work well"). RE-SEATED AT EVERY CTRL-DOWN at the pointer's NOTIONAL
    // COLUMN — the press for a ctrl-armed drag, each ctrl-down edge for a
    // plain-armed one — asking nothing about where the release will put the
    // cursor. What is WRITTEN here is the SONG FRAME under that column at that
    // instant (viewport_start + col·spp, the apply's own conversion, so the
    // seat and the phase's first event cannot disagree).
    // That column is a PROJECTION of the platform's one notional pointer
    // position, computed on demand at the seat (nav_notional_col,
    // input_pointer.cpp) and never accumulated here: the position is owned
    // where the raw events are, because a second clamped accumulation
    // advanced on the DELIVERY cadence cannot agree with it at a wall
    // (GuiPlatform::notional_pointer_x_ carries that record, codex round 17).
    //
    // WHAT THE PHASE HOLDS IS THE FRAME, AND THAT IS THE PART THAT CHANGED
    // (architect 2026-08-14, from the rig, reversing the pivot half of the
    // screen-column model he had ratified hours earlier — on his own evidence,
    // having found the one case it gets wrong: "pin the cursor to the song
    // position when zoom is happening, such that if the viewport clamps
    // because we've reached end of file or zero, the cursor stays in the song
    // position that it is doing the zooming from, so that any zoom is fully
    // reversible"). THIS IS NOT A REVERSAL OF THE SCREEN SEAT: the two models
    // name the same point for as long as nothing clamps — a zoom phase has no
    // pan term at all, so the frame under a fixed column cannot move on its
    // own — and they can therefore diverge ONLY where the viewport SATURATES
    // at frame 0 or at the right wall, which is why the difference took this
    // long to surface.
    // THE CASE HE FOUND, worked: zoom in near the left edge so frame 0 goes far
    // offscreen; release ctrl and pan until the pivot's content sits mid-screen;
    // hold ctrl again and drag UP (zoom out). The viewport walks left until
    // frame 0 comes back and CLAMPS — "which is good" — and from that instant a
    // SCREEN pivot is a lie: the song keeps sliding out from under a stem that
    // does not move, so reversing the drag zooms back in about a different part
    // of the piece ("the second drag motion is basically irreversible... if I
    // were to continue holding control and drag downward, I would be zooming
    // into a section much later than where I did the second zoom"). Holding the
    // FRAME instead slides the stem with the content it is anchored to, so the
    // reversed drag zooms back into the section it came from. STATED EXACTLY
    // (the arithmetic is at apply_nav_zoom_at): what is invariant for the phase
    // is the anchored FRAME, which off the walls makes an out-and-back drag
    // reproduce the earlier viewport outright, and AT a saturated wall — where
    // the viewport is determined by the level alone and cannot come back the
    // same way — preserves the FOCUS, which is the half the screen column lost.
    // THE EDGE TRICK COMES BACK WITH IT (the behaviour the architect named
    // admiringly: "as the part of the waveform that the stem is on moves off
    // screen, the stem would clamp — that was a very intuitive design"): a
    // pivot column outside [0, W-1] pins at the edge pixel and REBINDS this
    // field to that pixel's frame, so the zoom focus never leaves the screen.
    // Stated honestly, the rebind is the ONE thing that breaks exact
    // reversibility — it is a lasting mutation of the anchor — and it is
    // deliberate: it engages only once the anchored content has been pushed
    // OFF the visible span, where there is nothing left on screen to be
    // reversible about.
    // NO LIVE RE-SEAT INSIDE A ZOOM PHASE, and none is to be added — the
    // sentence still stands and now means something narrower. THE SEAT (this
    // field's frame) is written once per ctrl-down and never re-read from the
    // pointer during the phase, which is safe because the phase FREEZES the
    // pointer's x: it writes no notional position at all, so the clamp verdict
    // cannot change under it. THE COLUMN, by contrast, is re-derived every
    // event BY DESIGN — that is the whole model — and the EDGE REBIND is the
    // single route that rewrites the anchor mid-phase. A future dual-axis ctrl
    // phase, which would pan while zooming, breaks the freeze's premise, and
    // this is the sentence that has to be revisited if one is ever built.
    //
    // THE SUPERSEDED SEAT AND ITS FALSE PREMISE, recorded because a reader will
    // otherwise re-derive it: the seat briefly read the position the RELEASE
    // would restore the cursor to instead — the notional position while the
    // hand had room, the capture's start column once the travel ran out — on
    // the reasoning that a runaway drag's cursor "is going to end up
    // teleporting there anyways", so the pivot should meet it at the grab
    // column. THE PREMISE WAS FALSE EVEN THEN. That teleport was THE PAN'S
    // ANSWER ONLY: the zoom phase drives the stem override on every one of its
    // events and the override OUTRANKED the clamp fork, so a drag that zoomed
    // at all restored the cursor ON THE STEM and never reached the fork.
    // Seating the pivot at the grab column therefore did not agree with a
    // teleport that was going to happen — it MANUFACTURED one, by putting the
    // stem there for the cursor to follow.
    // THE QUESTION IS MOOT NOW EITHER WAY: the release has no fork left to
    // read. The hidden pointer WRAPS to the waveform's opposite bound instead
    // of pinning at the one it reached, so it is never stranded and the cursor
    // simply comes back where the virtual pointer is
    // (GuiPlatform::notional_pointer_x_).
    // Both the ctrl-down pop that briefly answered the same worry and the
    // teleport it was derived from are deleted, and the seat is the whole of
    // what ctrl does.
    // Meaningful only while `zooming`. A PERSISTENT pivot (seated once per
    // gesture and kept across every toggle) was built and WITHDRAWN the same
    // day, and the reason is recorded so it is not re-derived: it was a
    // workaround for the capture's UNBOUNDED position accumulator, which made
    // "where the pointer is" unanswerable after a long drag — a pointer 3000
    // px past the edge re-seated at the edge and stayed there through a
    // reversal. Clamping the notional position continuously fixed that at its
    // source, so the pivot can honestly follow the pointer again, which is
    // both the architect's first instinct and what the touch pinch already
    // does (it zooms about the fingers, wherever they are).
    // A SECOND SEAT OF THE SAME GESTURE LANDS WHERE THE FIRST DID unless the
    // hand panned in between, and that is the lateral freeze's doing (the
    // paragraph above the struct): without it the zoom phase's own sideways
    // travel would walk the notional column along, and the stem would jump at
    // the next ctrl-down. Since the rotation that travel is the zoom's OWN
    // INPUT rather than travel nothing answers, which strengthens the point
    // instead of weakening it — the level has already spent those pixels, and
    // letting the seat spend them again would move the pivot by exactly the
    // amount the user was zooming by.
    double anchor_sample = 0.0;
};

// THE OVERVIEW LANE'S OWN DRAG, AND THE BOX IS THE SUBJECT OF ALL OF IT
// (architect 2026-08-15, redesigning the lane: "zoom is not what I'm looking
// for in the overview strip, because the overview strip can indirectly control
// zoom by directly controlling the outline box" — superseding the 2026-08-12
// rework's ctrl arm, whose dual-axis strip drag is DELETED WHOLE, its record
// above ScrollDragState). THE WHOLE VOCABULARY IS ONE SENTENCE — every gesture
// here acts ON THE BOX and the zoom follows from the box's span — and IT IS
// IDENTICAL ON BOTH SURFACES because these drags are ABSOLUTE and
// CAPTURE-FREE: touch reaches every one of them through the ordinary pointer
// translation, with no touch code of its own (touch.md's lane paragraph).
// THREE kinds on one PLAIN left press, decided at the press by the box geometry
// (the painter's own derivation, overview_box_span — one owner, app_state.cpp),
// plus the outside press's TELEPORT, which is an ACT rather than a kind — it
// runs and then hands the same press to the Pan:
//   * EdgeBegin / EdgeEnd — the box outline's LEFT / RIGHT edge as a grab
//     handle (hit_test_overview_endcap, the trim endcaps' own inflated-band
//     model and grab width; the endcap claim OUTRANKS everything else on the
//     lane, plain only — and it is what covers the FULLY ZOOMED OUT case,
//     where the box fills the lane and there is no outside to press).
//     Dragging one MUTATES THE VIEWPORT SPAN: the dragged
//     edge's whole-song position follows the pointer column and the OPPOSITE
//     bound stays fixed — a zoom anchored at the far edge, applied per event
//     through Viewport::apply_strip_drag_zoom with the fixed bound as the
//     anchor (clamps: the song walls by column-clamping into the lane, the
//     max-zoom minimum span as the inclusive cannot-cross clamp at the
//     partner, the effective ceiling through the level pre-clamp +
//     clamp_viewport_start).
//   * Pan — INSIDE the box, and OUTSIDE it too once the teleport below has
//     run: the press grabs the box where it is, its grab-point
//     offset preserved, and the drag is THE BOX-FOLLOWS-POINTER PAN, PAN ONLY:
//     per motion event the viewport centers on (pointer's whole-song
//     position − grab_offset), X ONLY — the handler never reads dy, so vertical
//     motion is ignored structurally ("no cross axis allowance for up/down":
//     this pan has no zoom axis at all). A motionless release inside the box is
//     a consumed nothing, the lane's v1 rule standing.
//   * OUTSIDE the box — THE TELEPORT, AT THE PRESS, AND THEN THE PAN
//     (run_overview_teleport — the viewport CENTERS on the press column's
//     whole-song position through the scroll_viewport funnel, a pure viewport
//     move, zoom level unchanged). CONTENT ACTS THE MOMENT ITS IDENTITY IS
//     CERTAIN (architect 2026-08-17): an outside press can only mean the
//     teleport, so there is nothing for a lift to disambiguate and the
//     deferral it wore for two days (2026-08-15..17, the Pending kind, deleted
//     with that ruling) protected a nonexistent case. THE PRESS THEN ARMS THE
//     BOX PAN (architect 2026-08-18: "overview teleport should transition into
//     drag immediately if finger/pointer drags"), so a pointer or finger that
//     keeps moving keeps panning and a motionless release is the pan's own
//     consumed nothing. ACTING AND ARMING A DRAG IS NOT A CONTRADICTION OF THE
//     THIRD CLAUSE, and this is the shape it already has at the MARKER FLAG,
//     whose plain press runs run_marker_click_act and then arms
//     PendingMarkerPress: the deferral rule governs a press whose MEANING is
//     ambiguous until the lift, and this press's is not — it means teleport
//     either way, and the drag CONTINUES it rather than replacing it. The seat
//     is the inside-box arm itself, reached by fall-through, and the pan's
//     grab_offset is measured AFTER the teleport through the same expressions,
//     so it is near zero by construction and exact at the walls (the
//     derivation is at the press router, input_pointer.cpp). WHAT DOES NOT
//     COME BACK is the deleted outside-drag extension — dragging a BOUND from
//     outside the box (deleted 2026-08-15: "we can remove that, because the
//     threshold for the bounds is fine, the ten pixels on either side works,
//     it's a large enough threshold"), so A BOUND IS STILL DRAGGED BY ITS OWN
//     GRAB BAND AND NOWHERE ELSE. TOUCH IS WHY THE PRESS-TIME ACT IS SAFE
//     WHERE THE PRESS-TIME LANDING MODEL OF
//     2026-08-15 WAS NOT: the synthesized press is delivered only when the
//     disambiguation window RESOLVES to one finger, and a second finger inside
//     the window goes straight to Nav with no press ever delivered (the
//     platform's Pending arm), so a fast two-finger landing cannot fire the
//     teleport — the concern that moved the act to the lift is answered by the
//     window, not by the deferral.
// ABSOLUTE-POSITION DRAGS, the trim endcap model and not the deleted strip
// drag's: NO pointer capture, NO anchor stem, per-event synchronous rebuild
// through the family's clamp chokepoints.
// A SECOND CONTACT DURING A LIVE OVERVIEW DRAG IS A NO-OP, and nothing here
// claims it (architect 2026-08-15, with his own justification: it matches what
// a THIRD finger does on the waveform). The platform's own rule delivers it:
// a second finger arriving during a MOVED translation is ignored whole, the
// mid-gesture-finger-counts-do-not-mutate-a-committed-gesture family, so a live
// box or bound drag simply carries on. A MOTIONLESS hold is ignored on the same
// line and by the same door: the second-finger fork tests the THIN-LANE bit
// beside the moved latch, so nothing on this lane upgrades whatever the finger
// has done — the first finger's translation simply runs to its own lift (every
// act this lane owes has run by then: the outside press's teleport at the
// press, the pan per motion event, so the lift owes
// nothing). (The generic motionless-hold UPGRADE that
// this paragraph used to hand the lane is unreachable here twice over now: the
// thin-lane door refuses it, and the upgrade's end is the ABNORMAL one since
// codex round 19, which commits nothing rather than delivering the lane's
// motionless release. The lane's own two-finger refusal in
// apply_touch_nav_update is the SECOND door, for the pair that lands inside the
// disambiguation window and never reaches the pointer phase at all.)
// THE SIMULTANEOUS TWO-BOUND STRETCH — one finger per bound, both moving at
// once — WAS BUILT AND IS DELETED (2026-08-15, both on the architect's word:
// "a two-finger gesture would basically mean draw the bounds at each of the two
// fingers", then, after driving it through two rounds of fixes on the rig,
// "sometimes it works the way you describe it, and sometimes it flips and
// reverses the direction, it's very buggy; let's just make two-finger gestures
// no-op on the overview strip, it's tiny anyways"). TWO FINGERS ARE A NO-OP
// HERE now, and the refusal — not a fall-through — is what keeps a pair begun
// on the lane out of the waveform's pinch. THE LESSON IS RECORDED SO THE
// GESTURE IS NOT RE-PROPOSED ON A HUNCH: it was reachable only through the
// touch layer's one-finger translation, its surface had to be decided at the
// down point because a 26 px lane cannot hold a centroid, and even then the
// refusal remained centroid-based — three couplings for a gesture whose whole
// job the box's own three motions already do.
// Navigation-class: touches no playhead, no region, no selection, allowed in
// read-only and live in the `h` view (the lane's claim sits above the mode's
// gate). Follow suppression: the pan and the teleport ride scroll_viewport's
// funnel, the edge drags apply_strip_drag_zoom's either-axis term — the
// producer inventory at follow_overridden_for_session. Cursors: the box EDGES
// wear the trim endcaps' own pair and THE WHOLE REST OF THE LANE WEARS
// TrimResize (the bridge's own shape — the pan is an x-only slide of the whole
// span), inside the box and outside it alike, because the plain drag is that
// same pan everywhere: the cue names the DRAG a press arms, not the act it
// also runs, which is the marker flag box's own rule. (It was the ARROW
// outside the box from codex round 19 to 2026-08-18, correctly, under the
// map's standing rule that a point arming nothing shows the Arrow — the
// outside press armed nothing then. It arms the pan now.) Hover and drag
// alike, and
// EVERY LIVE DRAG KEEPS ITS CUE for the gesture's life, read from this record's
// own `kind` (the trim exception's rule — the edges took it at the lane rework
// and the PAN joined 2026-08-13, the architect closing the one live lane drag
// that fell back to the Arrow mid-slide; pointer_cursor_kind) — which is the
// second reason the band-wide answer is right: an outside press whose hover
// cue was the Arrow would flip to TrimResize at its own crossing, that same
// mid-slide flip. (The crossing's change to a GRABBED-BOUND arrow that this
// paragraph once described is a different thing and stays deleted: it was true
// only while an outside press extended the NEARER bound, and that extension
// went on 2026-08-15.) CTRL BINDS
// NOTHING ON THE LANE any more — it went with the strip drag — so a ctrl press
// is a consumed nothing and ctrl's hover answer is the Arrow, the map's own
// rule for a modifier that arms nothing.
// Cleared on button release / lost
// button, by the force-end finalizer, and on file load; pointer gestures
// have no cancel. (The Pending kind — the outside press's two-day lift
// deferral, 2026-08-15..17 — is deleted: the outside press acts at the press
// and then arms the ordinary Pan, so this state only ever holds a real drag
// and needs no kind for a press that has not decided yet.)
enum class OverviewDragKind { Pan, EdgeBegin, EdgeEnd };

struct OverviewDragState {
    bool active = false;
    bool moved  = false;   // crossed the threshold into a real drag
    OverviewDragKind kind = OverviewDragKind::Pan;
    int  press_x = 0;      // press position (window px): the Chebyshev gate
    int  press_y = 0;
    // Pan only: active-domain offset between the pressed column's whole-song
    // position and the viewport CENTER at the grab — the grab-point offset
    // inside the box. Each motion event centers on (pointer position − this).
    // An OUTSIDE press seats it the same way from the same expressions, after
    // its teleport has moved the viewport, so it lands near zero there.
    double grab_offset = 0.0;
    // Edge drags only: the FIXED (opposite) box edge's active-domain position,
    // captured at the press that grabbed an endcap — the one site that decides
    // an edge drag — and held for the drag's life: the per-event zoom's anchor
    // (anchor_x = that bound's own window column, 0 for the start, area.w for
    // the end). It is the PAINTED edge (overview_box_edge_samples, the box's
    // own owner), which differs from the raw viewport end at the right wall
    // alone — the reasoning is at seat_overview_edge_drag.
    double fixed_edge_sample = 0.0;
};

// THE TWO-FINGER PINCH'S SEATED PIVOT — the touch nav gesture's FIRST and only
// GUI-side record (architect 2026-08-14, from the rig, carrying the mouse's own
// song-anchored pivot onto glass: "when the two-finger touch is first
// registered, it picks the point on the waveform, and the zoom pivot stays
// there no matter where the two fingers move on the screen"). Until this the
// gesture kept nothing between frames — every frame was applied whole and
// forgotten, the pivot re-derived from the LIVE centroid each time — and that
// sentence is retired here rather than left standing.
//
// WHY IT COMES NOW, and it is exactly the case the mouse's own fix answered
// (ScrollDragState below): away from the walls a live centroid and a held frame
// name the same point, because a zoom has no pan term and the content under a
// fixed column cannot move on its own. AT A WALL — the viewport saturated at
// frame 0 or at the right edge — the view is determined by the level alone, so
// the song slides out from under a pivot pinned to the glass and pinching back
// out never returns what it came from. A HELD FRAME keeps its grip and lets its
// COLUMN slide across the glass instead, which is what makes the pinch
// reversible. THE PARITY ARGUMENT IS THE ARCHITECT'S OWN: "I imagine my right
// hand as being the same right hand on the touchpad, and then the left hand
// reaches for the control key and implicitly does the opposite... I'm keeping
// the same mental model on the laptop as I would like to have on the touchpad."
//
// LIFECYCLE (the body is apply_touch_nav_update, input_pointer.cpp):
//   * MEANINGFUL ONLY while a two-finger phase is live.
//   * SEATED AT THE FIRST TWO-FINGER FRAME THAT SURVIVES THE wheel_context
//     REFUSAL — so a frame the gesture refuses seats nothing, seating being a
//     navigation act — at the song frame under THAT frame's centroid column.
//     It is deliberately NOT gated on the frame APPLYING anything (2026-08-14,
//     correcting the shape this shipped with): the zoom-only ruling forces the
//     centroid delta to a literal zero, so two fingers landing and sliding
//     together produce nothing but exact-no-op frames, and gating on the apply
//     meant such a pinch seated nowhere and painted no stem until the finger
//     GAP first changed — by which time the centroid had drifted off the point
//     the fingers grabbed. The seat therefore sits ABOVE the exact-no-op return
//     and BELOW the refusal; the seat's VALUE is unchanged.
//   * CLEARED by any frame that ARRIVES not-two-finger (the downgrade to the
//     survivor's pan), refused or not — the clear leads the body while the
//     seat follows the refusal, the two halves deliberately on opposite sides
//     of it (the reasoning is at the site) — and at end_touch_nav, every end
//     included, so a later upgrade re-seats rather than inheriting a stale
//     anchor.
//   * AND AT EVERY WRITE OF THE ACTIVE VIEW STATE since codex round 20, ON THE
//     WRITERS THEMSELVES since round 21 — the S/T, W/P and A/B assignment sites,
//     with every command that reaches one (the `t`/`p`/Ctrl+Tab keys, the 1/2/3
//     selectors, the view bar, the S/T + W/P radios, the settings keys, the
//     propagate paste, undo/redo and both load-in-places) inheriting it by
//     composition rather than by remembering. THE FIELD IS A SONG FRAME IN THE
//     ACTIVE DOMAIN, and nothing stops a keyboard command, a mouse click or a
//     modal load from moving that domain with two fingers still down: the S/T
//     write is the sharp case (a source frame read as a target one), while a
//     W/P or A/B write leaves the number valid and clears on the fresh-grip
//     rule. The clear is free to a live pinch — its next frame seats afresh.
//     The membership, its derivation, the correctness / fresh-grip split and
//     the do-not-add-touch-to-any_pointer_gesture_active note all live at
//     clear_touch_zoom_seat's declaration (input_handler.h).
// THE COLUMN IS RE-DERIVED EVERY FRAME from the held frame against the live
// viewport, and a column pushed outside the waveform CLAMPS to the edge pixel
// and REBINDS this field to that pixel's content — apply_nav_zoom_at's pivot
// block exactly, edge trick included, which the stateless model did without
// because it had no persistent anchor for an off-screen column to corrupt.
// NO RE-JOIN WINDOW is built for a panel that drops a contact mid-pinch: the
// downgrade clears the seat and the next upgrade takes a fresh one, which is
// the architect's explicit ruling for the second time (touch.md's two-finger
// section carries the first and his reason).
// THE SEAT IS ALSO THE ANCHOR STEM'S GATE since 2026-08-14, the pinch being
// one of the stem's TWO producers since 2026-08-15 — it joined as the third and
// the overview lane's strip drag left (paint_strip_drag_anchor,
// paint_handler.cpp): the
// gesture record and nothing else, exactly the other producer's shape. BOTH
// EDGES OWE DAMAGE and neither is free: the SEAT damages at its own site (a
// seating frame need not apply anything at all under the ordering above, and
// even one that does can be dropped by apply_strip_drag_zoom's true-no-op
// return when the pinch begins saturated at a wall, so its stem would
// otherwise never appear), and the CLEAR
// through clear_touch_zoom_seat (a clear can land on a frame that applies
// nothing at all).
struct TouchNavZoomState {
    bool   seated        = false;
    double anchor_sample = 0.0;   // the held SONG frame (active domain)
};

// (The SCRUB has no drag state OF ITS OWN: since 2026-08-13 it rides
// ScrollDragState like every other act on the navigation surface — its ONE
// entry is the plain LOWER-HALF press's MOTIONLESS RELEASE, which runs one act
// through the one body, scrub_press_at (input_pointer.cpp). ITS PRESS-TIME
// DISPATCH IS DELETED (architect 2026-08-13, the two-halves ruling: "we do
// everything on lift the finger or on mouse up, but the playhead scrub, we do
// right on mouse down. We should remove that"), which is what let the lower
// half take the grab-pan, the region former and the pending click; the
// architect's own 2026-08-12 carve-out — "as soon as I click, it immediately
// starts to scrub" — is superseded by it. The BARE RIGHT full-height scrub of
// 2026-08-01 is DELETED (architect 2026-08-12, the eighth glass ruling: "that
// existed only to serve a very tall waveform, and we're shrinking the
// waveform") — the right button is fully unbound again, a right press a
// consumed nothing everywhere. The marker-text lane's own scrub was
// deleted (architect 2026-07-27, and the lane itself in row 5). The act
// is a ONE-SHOT (scrub_act_at: stop a live session,
// else start one at the clicked frame), issued once per click — a held press
// does nothing further and a drag past the threshold replaces the act with the
// pan (architect 2026-07-23, the Ableton model; the former
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
// records this candidate AT A MOTIONLESS RELEASE — one seed timing for all
// four surfaces since 2026-08-15, when the marker click moved to the lift and
// its press-time seed (the last of them) went with it; the NEXT press on the
// SAME surface,
// if it lands within kDoubleClickMs and kDoubleClickSlackPx of the recorded
// position AND (for Marker) targets the same marker, is consumed as that
// surface's double-click action instead of the single-click action. A drag that
// MOVED records nothing and clears any candidate. Surfaces:
//   TrimBar    -> the SPAN-FRAMING command on the trim bar lane, its whole band
//                 (run_span_framing_command: a proper trim sub-window, else the
//                 whole song), IN EVERY STATE since 2026-08-18 — the `h` history
//                 view ran the viewed checkpoint's DIFF SPAN on this same
//                 gesture from 2026-08-05 until the bar stopped displaying that
//                 span, and one gesture now carries one command everywhere.
//                 Target unused; both axes'
//                 slack compared. It REHOMED here from the deleted zoom lane
//                 (architect 2026-07-31). THE CONSUME ACTS AT THE PRESS
//                 (2026-08-17: a recognized second press acts immediately on
//                 every double-click surface — "a deliberate double-click never
//                 becomes a drag"; the one-day lift deferral of 2026-08-15 and
//                 its verdict-before-arm machinery are deleted). A consumed
//                 press seeds no TrimBarPressSeed (the family rule — a consumed
//                 press never seeds) but DOES still fall through to the band's
//                 cap/bridge arm, so a second press that then crosses into a
//                 trim drag proceeds from the framed view — the architect's
//                 accepted cost, trim's no-undo notwithstanding: "if I'm double
//                 clicking specifically to do the double click action, I would
//                 never double click into a drag". The
//                 seed is that lane's own press record (TrimBarPressSeed),
//                 not a strip-drag field: the lane arms a pending trim drag
//                 rather than a live one, so there is no drag state to hang it
//                 on and the motionless test is the release's own slack compare.
//   Marker     -> opens the marker's flag editor (target = marker index; both
//                 axes' slack compared). The marker's ONE pointer surface is
//                 its FLAG BOX (the painter's published rect — the stem
//                 surface died 2026-08-12 with the stems-inert ruling, and the
//                 marker-text lane's run in row 5). THE CONSUME ACTS AT THE
//                 PRESS (2026-08-17 — the editor opens Enter-fast again; the
//                 lift deferral was "a tad slow compared to the Enter key"),
//                 its three gates read live at that press, and a consumed open
//                 arms no drag and seeds nothing (the editor owns input). The
//                 SEED stays a release act: the motionless lift writes the next
//                 candidate at the PRESS coordinates so the pairing stays
//                 press-to-press. A press that becomes a real marker drag seeds
//                 nothing, the moved-drag rule, and needs no clear of its own:
//                 the press's own top-of-frame clear already emptied the field
//                 and nothing has re-seeded it under the held button.
//   EditorText -> selects the clicked character class's RUN (word / punctuation
//                 / whitespace) in the active text editor (target unused; both
//                 axes' slack compared).
//   EmptyLane  -> creates a marker at the clicked position on an EMPTY
//                 marker-lane spot, then SELECTS it and LANDS the playhead on
//                 it (architect 2026-07-23; select+land re-ratified 2026-08-12
//                 with the eighth glass ruling): the AUGMENTED drop, exactly
//                 what bare `s` performs (warp copy-previous / phase reset N/2
//                 lead-in — the drop itself single-selects and re-seats the
//                 playhead, so one body serves key and click), home-view and
//                 read-only gated silently — AT THE SECOND PRESS (2026-08-17,
//                 with the whole double-click family; the one-day lift deferral
//                 is deleted): the second press spends itself on the create and
//                 arms no pending, so it can never become a pan, and motion
//                 after it is dead — the create is undoable, and undo is its
//                 recovery. PLAIN presses only — a modified
//                 press on the lane is the shift former / the ctrl zoom or a
//                 consumed nothing, and seeds nothing. Seeded at the
//                 MOTIONLESS RELEASE of the lane's plain press (position-keyed,
//                 target unused, both axes' slack compared) beside that
//                 release's deferred click act — a press that crossed into the
//                 PAN seeds nothing, the TrimBar pattern (the seed rode the
//                 press while the press placed at press time, pre-2026-08-12).
// Cleared on file load, the moment an action fires, and — the KEYBOARD and
// WHEEL halves of the lifetime — at the TOP of every on_key AND on_wheel
// command: any keyboard command OR wheel frame between two
// clicks breaks EVERY candidate at those chokepoints, so a seed formed in one
// context can never consume in another after an intervening keypress (Esc
// included) or a wheel pan that moved content under the pointer. The
// pointer half is the on_button_press top-of-frame clear, the moved-drag clears,
// and the force-end finalizer's clear (a force-end is not a clean click
// sequence). Session-only.
struct DoubleClickCandidate {
    DoubleClickSurface surface = DoubleClickSurface::None;
    int64_t time_ms   = 0;      // CLOCK_MONOTONIC ms at the seeding press/release
    int     press_x   = 0;      // seed x: all four surfaces seed at a motionless
    int     press_y   = 0;      //   release, Marker with its PRESS coordinates
    int     target    = -1;     // marker index for Marker; unused otherwise
    // WHICH SPAN THE SEEDING PRESS LANDED ON (Marker only; unused otherwise).
    // The consume forks on THE SEED and not on the second press's own position,
    // so a pair of clicks straddling the seam opens the editor the FIRST one
    // named — the same "the seed decides" rule the target field already carries.
    MarkerClickSpan span = MarkerClickSpan::Flag;
};

// THE TRIM-BAR FRAMING DOUBLE-CLICK'S FIRST HALF, recorded at the press because
// only the RELEASE can tell a click from a drag. A plain trim-bar-lane press
// (any spot in the band — endcap, bridge, or bare ground; read-only included and
// the `h` history view too, the framing
// being pure navigation in both) records this; the left release seeds the TrimBar
// candidate when the pointer is still within kDoubleClickSlackPx of the recorded
// point and no trim drag went live. That slack IS the motionless test: it equals
// kDragMovedThresholdPx, so "never became a drag" and "never left the slack" are
// the same condition by construction — STILL TRUE with the framing consume back
// at the press (2026-08-17; it spent one day at the lift, 2026-08-15..17): the
// press that CONSUMES records no seed at all (a consumed press never seeds, the
// double-click family rule — it frames and falls through to the cap/bridge arm
// with no seed), so this record is only ever written by a NON-consuming press
// that armed the cap/bridge drag or claimed bare band, which are precisely
// the two the "no trim drag went live" clause was written for. Cleared at every left release (the release
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
// button the kdenlive rows carry, in painted order: row 1's File and
// Settings plus the view bar's three, row 3's two TABS, row 4's TWENTY-EIGHT
// view / mode / action buttons (the deleted toolbar row's four lead them since
// the 2026-08-12 relayout; the HISTORY OPENER, ITS TWO WALK RADIOS and ITS
// FOUR COMPANIONS close them since 2026-08-18), then the bottom row's SIXTEEN — the transport
// three, the FOUR SINGLE-MARKER VERBS with the MARKER MEASURE (2026-08-19) and
// ADD TO SELECTION (2026-08-18) behind them, the MARKER-WALK three
// (2026-08-15) and the four cardinal arrows. It exists ONCE, here, because
// it indexes
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
// and is the one exception, at redesign_button_enabled below). Row 1's FILE and
// SETTINGS are the roster's TWO non-chord entries (File joined them 2026-08-13,
// taking the slot the Quit button held; NAVIGATION was a third from 2026-08-02
// until its menu was deleted whole on 2026-08-15): each press TOGGLES ITS OWN
// DROPDOWN, which no keyboard chord does, and both are spelled at the menu claim
// rather than in the chord table.
//
// The enum ORDER is painted order, and redesign_button_index depends on the
// values staying 0..kRedesignButtonCount-1 contiguous (the tick comparator in
// main.cpp walks the range by index). The indices are DERIVED and never
// serialized, so inserting a button mid-roster (as Settings was) or REORDERING
// two of them (as row 1's Settings and Navigation were, 2026-08-03) renumbers
// the stash harmlessly.
enum class RedesignButton {
    // Row 1, the menu row: the two LEFT-FLOATING buttons, then the three of
    // the RIGHT-FLOATING view bar (2026-08-02) in their painted order — the
    // absolute view selectors S+W / T+P / T+W, which are bare 1/2/3.
    //
    // SETTINGS PAINTS LAST IN THE LEFT FLOAT (architect 2026-08-03, moving it
    // behind the NAVIGATION anchor that then sat between it and File). The float
    // is adjacent with no gap and its layout is a shaped-run walk, so the move
    // is an ORDER change and nothing else — no width, no padding, no anchor
    // expression follows it. This enum and the painter's kMenuButtons table are
    // the two places that carry the order, and they move together.
    //
    // THE NAVIGATION ANCHOR IS DELETED (architect 2026-08-15): its menu carried
    // SEVEN command rows and every one of them had grown a button — the four
    // zoom/framing rows since the 2026-08-12 relayout put the zoom group in the
    // icon row, the three marker/tab steppers since the bottom row's MARKER-WALK
    // GROUP landed the same day this ruling did — so the menu had become a
    // slower path to seven commands that all have buttons ("we should go ahead
    // and remove the navigation dropdown altogether"). Its removal cost the row
    // nothing structurally: the anchors were never a pair or a triple anywhere,
    // they are walked from kDropdownMenus.
    //
    // FILE HOLDS THE SLOT THE QUIT BUTTON HELD (architect 2026-08-13, with the
    // act-at-release conversion): row 1 paints no held face at all, so a Quit
    // button that acts at the LIFT gave no feedback while it was down and read
    // as broken — "we can create a File entry and move Quit into File's
    // dropdown; that's the standard way and kdenlive does it that way too". So
    // the Quit BUTTON is retired (it existed from 2026-07-31) and File is a
    // THIRD DROPDOWN ANCHOR whose one item is Quit; the CHORD Ctrl+Q is
    // untouched everywhere. Nothing else moved: File takes exactly Quit's slot,
    // so the painted order was File, Navigation, Settings and the roster's total
    // was unchanged — the split moved, 43 chords + 2 anchors becoming 42 + 3.
    // (The painted order was File, Settings from the Navigation anchor's
    // deletion on 2026-08-15 until 2026-08-20, and the split was back to two
    // anchors for those five days.)
    //
    // THE EDIT MENU IS THE THIRD ANCHOR SINCE 2026-08-20 (architect), painted
    // between File and Settings — the standard order, kdenlive's own. It is a
    // COMMAND MENU carrying all FIVE propagate commands (Ctrl+P, Ctrl+Alt+P,
    // Ctrl+Alt+Shift+P, Ctrl+/, Ctrl+Alt+/), and it is a RELOCATION rather than
    // an addition: IconCopy and IconPaste were deleted from the icon row in the
    // same ruling, so the five commands have exactly ONE pointer home and the
    // no-second-road doctrine is SATISFIED rather than amended. The anchor
    // count is three again for the first time since 2026-08-15, and nothing in
    // the dropdown machinery counts menus — kDropdownMenus grew one row.
    File, Edit, Settings, ViewSW, ViewTP, ViewTW,
    // Row 3, the tabs — TWO SLOTS, ALWAYS, AND THE A/B PAIR IN EVERY STATE
    // since 2026-08-18: they say "A" and "B", they light the active tab, they
    // carry their ordinary tooltips, and their Ctrl+Tab switches the active
    // navigation tab wherever it is pressed — the `h` history view included,
    // which is the architect's own ruling on it ("ctrl+tab should work as
    // normal in history view — it becomes essentially another view but in
    // mostly readonly mode").
    //
    // THE ROW WAS THE `h` VIEW'S WALK SELECTOR FROM 2026-08-05 TO 2026-08-18,
    // and the shape is recorded because it is an easy one to re-invent: while
    // the view stood the two slots read "Remote" and "Local", their selected
    // face marked the live WALK SOURCE instead of the live tab, they carried no
    // tooltip, their presses were routed to set_history_reading by a band claim
    // of the mode's own, and Ctrl+Tab cycled the walks rather than switching
    // tabs. (The row grew to four slots for the (walk source, reading) product
    // on 2026-08-07 and went back to two the following day, when the READING
    // left for its own toggle button in row 4 — HistoryCumulative below. TabC
    // and TabD are deleted whole with that arc.) THE WALK SELECTOR IS ROW 4'S
    // OWN RADIO PAIR NOW (HistoryWalkGit / HistoryWalkSession on bare `g`,
    // below), which is what freed this row: a repurposed surface was the only
    // reason the tabs ever stopped being tabs.
    TabA, TabB,
    // Row 4, the icon row, in painted order: the toolbar four (the deleted
    // row 2's Save / Undo / Redo / Render, the row's FIRST GROUP since the
    // 2026-08-12 grand relayout dissolved that lane — same chords, same face
    // machinery, the FACE now a glyph in the 32px box and the old labels
    // living on as the tooltips), the two view radio pairs, the TRIM GROUP
    // (the Show trim region button alone since 2026-08-18), the ZOOM GROUP
    // (2026-08-12, the architect's live placement "after the trim"), the
    // phase-reset clipboard pair with the three mode/editor buttons, the
    // render-entry pair with THE READ-ONLY TOGGLE, and THE ROW'S LAST GROUP —
    // the HISTORY OPENER leading its two WALK RADIOS and its four companions.
    //
    // THE 2026-08-18 ROSTER RELAYOUT is what gave the row that tail and took
    // its verbs away, both in the architect's own words: "move
    // drop/delete/disable/toggle inherit to bottom right row" and "move
    // cumulative/restore/older/newer to top icon row — place a separator
    // before the history button, and place cumulative/etc after the history
    // button". So the SINGLE-MARKER VERBS left this row for the bottom one
    // (they are the enum's bottom-row block now, keeping their Icon* names —
    // a roster id names the button, not the lane it happens to sit in), the
    // four HISTORY COMPANIONS came back up from it, and the opener left the
    // render-entry group to lead a separator-led group of its own again. The
    // same ruling deleted the TRIM SCISSORS whole ("remove the 'set trim from
    // region' icon"), leaving the Show trim region button alone in the trim group —
    // and that button INHERITED the scissors' chord (bare `x`) and their shift
    // admission hours later, when the region became the trim.
    //
    // NOTHING IN THIS ROW IS EVER HIDDEN (architect 2026-08-14: "no more
    // hiding/showing icons in top icon row"). Every member paints in every
    // state and what a mode refuses simply GREYS, through the roster's own
    // gate-mirroring disabled face — so the row's width is a constant and no
    // button can travel out from under the pointer that pressed it. The
    // two-level collapse rule of 2026-08-12..13 (redesign_button_collapsed,
    // redesign_button_mode_companion and the group-span walk that served it)
    // is DELETED WHOLE with that ruling, and the argument the 2026-08-13
    // group order was built on — keeping the history opener's x fixed across
    // the toggle — is answered by construction now rather than by placement.
    Save, Undo, Redo, Render,
    IconS, IconT, IconW, IconP,
    // THE SHOW TRIM REGION BUTTON (architect 2026-08-16 as "Show region",
    // repointed onto BARE `x` on 2026-08-18 and given its settled name on
    // 2026-08-19 — the one the enumerator carried throughout): THE TRIM GROUP'S ONE
    // MEMBER, the second VIEWPORT-CLASS act the architect's 2026-08-11 slot was
    // opened for, which took the lead later on 2026-08-16 ("reverse the order
    // of the icons — show region first, then the scissors") and is alone in the
    // group now that the scissors are retired ("remove the 'set trim from
    // region' icon"). The group keeps its separator and its place after the
    // warp/phase radios; only its second slot went, so the row lost one box and
    // one gap and no boundary moved.
    //
    // IT INHERITED THE SCISSORS' CHORD AND THEIR SHIFT ADMISSION, hours after
    // their button was deleted, because the region became the trim the same
    // day: bare `x` had SET THE TRIM FROM A REGION, setting the region IS
    // setting the trim now, and the architect gave the free key to the act that
    // needed a home ("we can say the show region is actually `x` now, and then
    // we can keep shift+x or long press on the icon as show full song trim").
    // Ctrl+Shift+X, its chord for those two days, is unbound again.
    //
    // A TOGGLE WITH A LAMP, where the 2026-08-16 ruling made it deliberately
    // MOMENTARY and stateless. The hole that ruling avoided cannot occur under
    // the derived model and the record is worth keeping because it is easy to
    // re-invent: the old lamp would have read a SPAN'S EXISTENCE, so a span
    // scrolled offscreen left the button lit with only a clearing press
    // available — "the region toggle is on, but the region view can't be
    // accessed because the toggle is already on". This lamp reads the overlay's
    // VISIBILITY and the show half ALWAYS FRAMES, so a lit button means the
    // overlay is on screen or one press from being re-shown there.
    //
    // SHIFT REACHES Shift+X, THE MAXIMIZER (redesign_button_shift_admits), by
    // shift-click or by a long press at kChromeShiftHoldMs — the admission the
    // scissors carried, for the reason it was written: without it a keyboardless
    // panel could set a trim window and never get back out of it.
    //
    // Always enabled, by the settled face policy — there is no refusal to
    // mirror; the `h` view's grey is the derived partition's and its derivation
    // is stated once at this button's case in redesign_button_enabled below.
    IconShowRegion,
    // THE ZOOM GROUP (2026-08-12, the grand relayout's roster commit): four
    // navigation chords in their own separator-led group after the trim group —
    // zoom in (bare `=`), zoom out (bare `-`), full zoom out (bare `0`,
    // whose ceiling arm runs the `c` command), and working-zoom center (bare
    // `c`). Every one is a momentary navigation act, never-grey per the
    // row's rule and LIVE in the `h` view (all four chords are on the mode's
    // allowlist or its own vocabulary, so the derived partition answers live
    // with nothing hand-listed). THE 2026-08-02 NO-DUPLICATE-COMMANDS RULING
    // IS SUPERSEDED FOR THESE FOUR by the architect's relayout order — the
    // Navigation dropdown kept its zoom rows beside them, and the buttons are
    // the touch rig's pointer home for the same commands. THE DUPLICATION IS
    // OVER SINCE 2026-08-15: that menu is deleted whole, precisely BECAUSE
    // these four buttons (and the bottom row's marker walk) had made every one
    // of its rows a second path to a command that already had one — so these
    // four are the zoom commands' pointer home outright now.
    IconZoomIn, IconZoomOut, IconZoomFitBest, IconZoomOriginal,
    // THE MASS-MARKER CATEGORY: the BPM opener (bare `m`), iteration mode
    // (bare `i`) and follow (bare `f`). All three are consumed in the `h` view
    // and all three GREY there (they were the one group the view dropped whole,
    // 2026-08-13..14). TWO OF THE THREE grey on a locked tab too — FOLLOW is
    // the exception, bare `f` being navigation and on the lock's allowlist.
    //
    // IT WAS FIVE UNTIL 2026-08-20, WHEN COPY AND PASTE LEFT WITH THE PROPAGATE
    // RELOCATION (architect): the five propagate commands — Ctrl+P,
    // Ctrl+Alt+P, Ctrl+Alt+Shift+P, Ctrl+/ and Ctrl+Alt+/ — took the new EDIT
    // MENU as their ONE pointer home, and IconCopy and IconPaste were deleted
    // with the move rather than left standing beside it. That is the
    // NO-SECOND-ROAD DOCTRINE SATISFIED rather than amended, and it is the zoom
    // group's history run in reverse: those four buttons killed the Navigation
    // MENU because the menu had become the second road; here the menu is the
    // road and the buttons are. THE BPM OPENER STAYS despite being used about
    // as rarely — the architect's stated aesthetic choice, recorded here
    // because the frequency argument alone would have taken it too.
    IconBpm, IconIter, IconFollow,
    // THE RENDER-ENTRY GROUP (architect 2026-08-14): listen (bare `l`), load
    // in place (`'`) and THE READ-ONLY TOGGLE, in the order he dictated ("make
    // the last section of the icon row: listen, load-in-place, readonly,
    // history"). THE FIRST TWO GREY ON A LOCKED TAB (2026-08-15): the lock
    // blocks bare `l` and `'` alike, `'` replacing the whole authored state.
    // The toggle does not — `o` is the escape chord.
    //
    // IT IS NO LONGER THE ROW'S LAST GROUP: the HISTORY OPENER left it on
    // 2026-08-18 to lead a separator-led group of its own again, which is what
    // the architect asked for when its four companions came back up from the
    // bottom row ("place a separator before the history button, and place
    // cumulative/etc after the history button"). The three that stay keep his
    // order and their separator.
    IconListen, IconLoadInPlace,
    // THE READ-ONLY TOGGLE (2026-08-14), the padlock's new home: it left the
    // TABS, where it was a per-tab slot, for a roster button that reports THE
    // ACTIVE TAB's read_only bit — bright closed padlock and a lit lamp when
    // the tab is locked, the open padlock unlit when it is not.
    //
    // THE MOVE IS WHAT MAKES IT ITS CHORD, with no exception: bare `o`
    // toggles the ACTIVE tab's read-only and nothing else, and this button
    // shows exactly that tab's state, so button-is-its-chord holds
    // literally. The superseded design left the padlock on the tabs and had
    // the INACTIVE tab's slot toggle a tab that is not active — an act no
    // chord in the product expresses, which would have needed a second writer
    // of read_only beside the key. Recorded so the harder variant is not
    // revived.
    IconReadOnly,
    // THE HISTORY GROUP — the row's LAST, and its own again since 2026-08-18
    // (architect: "place a separator before the history button, and place
    // cumulative/etc after the history button"). The OPENER leads it: bare
    // `h`, ruled with the mode itself on 2026-08-04 and landed with the commit
    // act, lit while the mode stands, its chord a toggle so the same click that
    // opened the view closes it. It held a separator-led group of exactly this
    // shape from 2026-08-04 until 2026-08-14, when the four companions left for
    // the bottom row and the opener joined the render-entry group in last
    // place; this ruling puts both halves back where they were.
    IconHistory,
    // THE TWO WALK RADIOS (architect 2026-08-18: "add two radio buttons after
    // history button, before cumulative"), which is where they sit — between
    // the opener and the cumulative toggle. They select WHICH WALK the lane
    // reads: GIT is the committed checkpoint history, SESSION is this session's
    // own undo/redo timeline read as states (GuiHistoryWalkSource, the model
    // they select between, is unchanged — Commit | Local; only the surface that
    // selects it moved here).
    //
    // ONE CHORD, BARE `g`, AND A RADIO PAIR OVER IT — the roster's established
    // shape, not a new one: TabA/TabB share Ctrl+Tab and IconS/IconT and
    // IconW/IconP share bare `t` and bare `p` on exactly these terms. The chord
    // is a TOGGLE over the two walks (the arm is at handle_history_mode_key)
    // and the `radio` flag in kToolbarChords is what makes a press on the half
    // that is ALREADY LIT a consumed nothing rather than a switch away from
    // what the user just clicked. Both go through the ONE switch owner
    // set_history_reading, so the key and the two buttons cannot come to mean
    // different things.
    //
    // THEY GREY OUTSIDE THE `h` VIEW AND ARE LIVE INSIDE IT, the icon row's
    // settled rule and the same answer their four neighbours give — bare `g` is
    // bound in exactly one place in the product and it is inside the view. Their
    // arm is the companions' own at redesign_button_enabled, which states why it
    // owns that fact rather than the derived partition.
    //
    // THEIR LAMP IS SCOPED TO THE VIEW, unlike the Cumulative toggle beside
    // them, and the contrast is the state's own: the READING is a program-
    // session preference that outlives every visit, while the WALK SOURCE is
    // per-visit state reset to Commit at every entry — so a lit "Git" outside
    // the view would advertise a selection that is not a live fact. Exactly one
    // is lit while the view stands and neither is outside it
    // (redesign_button_selected).
    //
    // THIS PAIR IS WHY ROW 3 IS THE A/B TABS AGAIN (the row's own entry above
    // carries the superseded shape): the walk had no surface of its own, so it
    // borrowed one.
    HistoryWalkGit, HistoryWalkSession,
    // THE FOUR COMPANIONS, behind the walk radios in the order they have always
    // held
    // — how the delta READS, what you can DO from inside the view, then where
    // you can STEP: the cumulative toggle (bare `u`), the revert act (Ctrl+H)
    // and the walk's older / newer steps (bare `,` and `.`).
    //
    // THEY GREY OUTSIDE THE `h` VIEW AND ARE LIVE INSIDE IT (2026-08-18), which
    // is this row's own settled rule — what a mode refuses simply GREYS — and a
    // REVIVAL of the arm they held 2026-08-05..15 for exactly this reason:
    // their keys are bound in one place in the product and it is inside the
    // view. That arm went plain-true on 2026-08-15 because the move to the
    // bottom row had stopped painting them outside the view at all, so no face
    // read it; THIS ROW HIDES NOTHING, so they paint in every state and the
    // face has to be honest again. The arm is at redesign_button_enabled and
    // says which owner it uses and why.
    //
    // TWO 2026-08-15 REVERSALS SURVIVE THAT REVIVAL UNTOUCHED and must not be
    // re-retired with it: REVERT keeps NO conditional in-view grey (the mode
    // admits Ctrl+H only while a diff flag stands, and the architect reversed
    // that face as a per-selection blink — redesign_button_is_history_companion
    // is what still lifts the four over the derived partition), and the
    // CUMULATIVE toggle keeps its unconditional lamp, which outside the view
    // now composes disabled+selected — a combination the shared face
    // expressions already handle.
    HistoryCumulative, HistoryRevert, HistoryOlder, HistoryNewer,
    // The BOTTOM ROW's transport cluster (row 8, architect-ratified
    // 2026-08-11, the touch arc's first surface; a tenant of the unified
    // bottom row directly under the waveform since the 2026-08-12 row
    // unification): permanent on every host — no touch mode, no flag, no
    // detection. SIXTEEN buttons in four groups, in painted order (the enum
    // order is the painted order, and the row paints below the top rows, so the
    // roster's tail is
    // the right home): the TRANSPORT at the row's left (skip-back = bare Home,
    // THE ONE PLAY/STOP BUTTON = bare Space, skip-forward = bare End) with the
    // CLOCK behind its separator, then — FLUSH AT THE RIGHT MARGIN, three
    // groups divided by two more separators — THE FOUR SINGLE-MARKER VERBS
    // (2026-08-18: drop = bare `s`, delete = Delete, disable = Ctrl+D, inherit
    // = Ctrl+N) WITH ADD TO SELECTION CLOSING THEM (bare `k`, the sticky ctrl
    // — later the same day), THE MARKER-WALK GROUP (2026-08-15 — previous = Shift+Tab,
    // next = Tab, walk both tabs = Ctrl+Shift+Tab), and the
    // four CARDINAL ARROWS — DOWN, UP, LEFT, RIGHT left-to-right since
    // 2026-08-14 (the architect's order; it was vim's left-down-up-right from
    // the row's first day) — which inherit the bare arrows' whole
    // semantics by dispatching through on_key like every other chord button.
    //
    // NOTHING ON THIS ROW SWAPS OR HIDES SINCE 2026-08-18. The four HISTORY
    // COMPANIONS took the arrows' four slots while the `h` view stood from
    // 2026-08-14 until then — one cluster of four painted at that anchor in
    // either state, the other publishing zero rects — and they went back to the
    // ICON ROW with the roster relayout ("move cumulative/restore/older/newer
    // to top icon row"). The swap machinery went with them whole rather than
    // being kept: the arrows paint unconditionally now, and the row's layout
    // has no mode term left in it anywhere.
    //
    // THE ROW GOT ITS SHAPE AT THE ARCHITECT'S LIVE LOOK (2026-08-15), and the
    // two halves are one ruling: the left cluster dropped to THREE and the
    // freed weight went to the right, because four buttons over there read as
    // crowded next to the arrows — "the more I think about it, the more
    // awkward it feels to have all of that right next to those three others".
    // (A ninth button — ESC, bare Escape, centered between the groups — shipped
    // with the row and was DELETED the same day at the architect's live pass:
    // "looks like a missing button with that cross out". The mid-render CANCEL
    // moved onto the RENDER button instead — the toolbar's stateful-face
    // precedent — and bare Esc stays keyboard-only.)
    //
    // PLAY AND STOP ARE ONE BUTTON WITH TWO FACES (architect 2026-08-15, at his
    // live look at the row): bare Space is ONE TOGGLE, so the roster carries
    // ONE member for it and the GLYPH and the TOOLTIP swap on the live audition
    // bit (playhead_scanner_active, the GUI-side playback mirror) —
    // media-playback-start while stopped, media-playback-stop while a session
    // runs. It is RENDER-IS-CANCEL's own shape (one button, one chord, a
    // stateful face driven by one bit), and the resolvers are
    // redesign_button_glyph_swapped + redesign_button_icon for the glyph and
    // redesign_button_tooltip's stateful overload for the words.
    //
    // THIS IS SIMPLER THAN WHAT IT REPLACES AND THAT IS THE POINT. The button
    // dispatches bare Space and Space toggles, so BUTTON-IS-ITS-CHORD HOLDS
    // EXACTLY and the Render-is-Cancel exception stays the roster's ONLY break
    // from it: there is no lit/unlit question, no press to consume, and no way
    // for the glyph to contradict the act.
    //
    // TWO SUPERSEDED SHAPES, both from earlier the same day and both recorded
    // because each was right for the problem in front of it — a problem this
    // collapse makes not exist:
    //   * THE ENABLED SPLIT (the morning's whole-row honesty ruling): Play dead
    //     while an audition ran and where a launch would refuse, Stop dead
    //     while none ran. The architect reversed it with the rest of the row's
    //     honest arms — "there's not a whole lot of value derived from the icon
    //     faces changing, and it is a little distracting... the user is
    //     expected to know that with the playhead outside trim it's not going
    //     to play in target view".
    //   * THE RADIO PAIR (his own fix for what that reversal exposed): the
    //     enabled split had been doing TWO jobs, and the second was the pair's
    //     DISAMBIGUATION — only the meaningful half was ever clickable. With
    //     the row always-on both halves went live and a press on Stop while
    //     stopped would have STARTED playback, so the pair became a radio
    //     ("just like the warp/phase or source/target buttons") and the
    //     wrong-direction press died at the claim. THE WHOLE PROBLEM WAS TWO
    //     BUTTONS OVER ONE CHORD, which is what this collapse removes: with one
    //     button there is no wrong half to press, so the `radio` flag and the
    //     pair's `redesign_button_selected` lamp are DELETED rather than kept.
    //     The generic radio consume is untouched — the S/T and W/P rows and the
    //     tabs still use it.
    //
    // THE FOUR ARROWS REPEAT WHILE HELD (architect 2026-08-16), the row's one
    // hold gesture: the first fire a hold beat after the press, then the
    // COMPOSITOR'S own key-repeat rate, so a held arrow BUTTON walks at exactly
    // the speed a held arrow KEY does. His reasoning is recorded at the arrows'
    // rows in kToolbarChords (input_pointer.cpp), which owns the `repeats`
    // column; the burst's state and its whole edge inventory are at
    // AppState::ChromePress.
    TransportSkipBack, TransportPlayStop, TransportSkipForward,
    // THE SINGLE-MARKER VERBS, THE RIGHT BLOCK'S FIRST GROUP since 2026-08-18
    // (architect: "move drop/delete/disable/toggle inherit to bottom right
    // row — place a separator to the left of previous marker icon, and place
    // drop/etc to its left"): drop (bare `s`), delete (`Delete`), the disable
    // toggle (`Ctrl+D`) and inherit/collapse (`Ctrl+N`). They opened a group of
    // their own in the ICON ROW from 2026-08-12 until this move, and NOTHING
    // ABOUT THEM CHANGED WITH THE ROW — the chords, the gates and both faces
    // came across whole.
    //
    // THEIR GATES ARE THE BUTTONS' OWN, NOT THE ROW'S, which is the one thing
    // to read twice here: the bottom row is otherwise lit unconditionally, and
    // these four are its exception in both directions. Authoring chords, so
    // their HOME-VIEW, empty-selection and occupied-frame refusals stay the
    // chords' own consumed no-ops (never-grey); the `h` view GREYS all four
    // through the derived partition, and so does the READ-ONLY LOCK, the
    // roster's second MODE statement (redesign_button_enabled's read-only arm,
    // 2026-08-15). They keep their Icon* names: a roster id names the button,
    // not the lane it sits in.
    IconMarkerDrop, IconMarkerDelete, IconMarkerDisable, IconMarkerInherit,
    // THE MARKER MEASURE — the verb group's fifth member since 2026-08-19,
    // seated after Toggle inherit and ahead of Add to Selection. Bare `/`,
    // minuet-scales (notes climbing a staff — the speech balloon it wore for
    // the field's one free-text day was swapped with the grammar on
    // 2026-08-20), and it opens the MEASURE EDITOR on the focused marker of the
    // active markers view.
    //
    // ITS ENABLED ARM HAS NO FOCUS TERM, deliberately. The
    // act refuses INTERNALLY when nothing is focused (a consumed no-op), and
    // the face stays lit: a grey that tracked the selection would blink at
    // interaction cadence, which is exactly the reasoning that took the four
    // cardinal arrows and the revert button always-on (the 2026-08-15
    // no-blink ruling). NO LAMP: it is an act, not a mode.
    //
    // IT IS NOT HOME-VIEW GATED, unlike the four verbs above it: measures are
    // the FOURTH ruled exception to the home-view binding (the inventory is at
    // active_column_authoring_allowed), so the button works on both columns in
    // both audio views. The `h` view greys it through the derived partition,
    // bare `/` being neither the mode's vocabulary nor on its allowlist.
    //
    // IT ADMITS SHIFT SINCE 2026-08-20 (redesign_button_shift_admits): a
    // shift-click or a kChromeShiftHoldMs long press reaches Shift+`/`, THE
    // SCORE-VIDEO JUMP — the other half of the field, and the half a
    // keyboardless panel would otherwise have no route to at all.
    //
    // AND THAT IS WHY THE READ-ONLY LOCK DOES NOT GREY IT (architect
    // 2026-08-20, moving it out of the lock's set the day it joined): the plain
    // half opens an editor over serialized content and the lock refuses it, but
    // the SHIFT half is navigation the lock allows, and a chrome face cannot
    // split. So the face keeps the legal half, a plain click on a locked tab
    // dispatches bare `/` into read_only_key_blocked and drops there like the
    // key, and the score jump stays reachable by finger. The full argument is
    // at this button's arm in redesign_button_enabled.
    IconMarkerMeasure,
    // ADD TO SELECTION — the verb group's SIXTH member, seated by the
    // architect himself (2026-08-18: "add group selection icon ('Add to
    // Selection') after toggle inherit, before the separator"). Bare `k`, the
    // edit-select glyph, and a MODE rather than an act: while it is lit a
    // plain flag click takes the CTRL BRANCH — toggle membership, land the
    // playhead on the focus the toggle leaves, keep the rest of the selection
    // — and the mode auto-clears at the first selection act that is not that
    // toggle. Nothing about the click is new; the mode only routes a plain
    // press into the branch ctrl+click already ran. The whole contract, the
    // shift rule and the clear list are at AppState::add_to_selection.
    //
    // WHY GLASS NEEDS IT, in the architect's own words (2026-08-18): "none of
    // the modifier-click vocabulary exists on touch, so buttons are how glass
    // gets selection work." A finger has no ctrl to hold, so the one gesture
    // that ACCUMULATES a selection was unreachable on the rig; a lit mode is
    // the pointer-only spelling of the same act, and the keyboard keeps the
    // modifier it always had.
    //
    // IT WEARS THE SELECTED FACE, being a mode — the roster's standing rule
    // and the lamp IconIter and IconFollow already wear, reading the live bit
    // its own chord flips. THE ROW HAS A LAMP AGAIN because of it, having had
    // none between the Cumulative toggle's departure earlier the same day and
    // this arrival.
    //
    // ITS GATES ARE ITS OWN, like the four verbs beside it, and they are NOT
    // theirs: the `h` view GREYS it through the derived partition (bare `k` is
    // neither the mode's vocabulary nor on its allowlist), while the READ-ONLY
    // LOCK LEAVES IT LIT — a selection is navigation, not authored content,
    // the same reasoning that keeps the trim gestures legal on a locked tab,
    // so bare `k` is on read_only_key_blocked's allowlist and this button is
    // not one of the lock's ten. It keeps the group's Icon* naming: a roster
    // id names the button, not the lane it sits in.
    IconAddToSelection,
    // THE MARKER-WALK GROUP (architect 2026-08-15, the row's new right
    // cluster, behind a separator and ahead of the four arrows): previous
    // marker (Shift+Tab), next marker (Tab) and walk both tabs
    // (Ctrl+Shift+Tab). Three buttons, THREE CHORDS — each is its own chord
    // through the ordinary table, with no hold, no double-click and no
    // modifier gesture on the surface.
    //
    // A DOUBLE-CLICK-MEANS-CTRL+SHIFT RULE WAS CONSIDERED AND DECLINED, and
    // the reason is mechanical rather than a preference: EVERY double-click
    // surface in this product acts on its FIRST click too, so a double-click
    // on "next" would step one marker AND THEN walk both tabs, landing the
    // user somewhere he did not ask to be. Making the first click not fire
    // would mean delaying EVERY single click by the double-click window, which
    // is exactly what the act-at-lift work exists to avoid.
    //
    // THEY ARE ALWAYS ENABLED, by the row's settled face policy (the ruling is
    // at redesign_button_enabled), and the `h` view's DERIVED partition needs
    // no hand entry for any of the three: ALL THREE ARE THE MODE'S OWN
    // VOCABULARY in there (history_mode_owns_key answers for each), so all
    // three stay lit and do the mode's own thing — bare Tab and Shift+Tab step
    // the diff-flag cycle, and Ctrl+Shift+Tab marches the pair over that same
    // cycle (2026-08-18, the architect: the chord "is just short for
    // 'tab, ctrl+tab, tab'", and only what Tab denotes changes with the
    // context). WALK BOTH TABS GREYED for the hours between the walk selector
    // leaving row 3 and that ruling.
    TransportWalkPrev, TransportWalkNext, TransportWalkBoth,
    TransportDown, TransportUp, TransportLeft, TransportRight
};
// THE ROSTER, re-derived by counting the enumerators above: SIX in row 1, two
// in row 3, TWENTY-SIX in row 4 and SIXTEEN in the bottom row — 50. Of those,
// FORTY-SEVEN carry a chord in kToolbarChords and THREE are the dropdown
// anchors (File, Edit and Settings), which is the split the chord table's own
// static_assert checks — 43 + 2 until 2026-08-13, when the Quit button left the
// chord table and File joined the anchors in its slot (the count did not move).
// 50 AT 2026-08-20'S PROPAGATE RELOCATION, and the arithmetic is a NET LOSS OF
// ONE over one addition and two deletions: the EDIT ANCHOR joined row 1 (a
// non-chord entry, the roster's first anchor GAIN since File's) while ICONCOPY
// and ICONPASTE were deleted whole from row 4 with their chord-table rows —
// 51 + 1 − 2, split 49 + 2 to 47 + 3. Ctrl+P and Ctrl+Alt+P are untouched on
// the keyboard; what moved is where a POINTER reaches them, and the two chords
// that had no button at all (the measure pair, Ctrl+/ and Ctrl+Alt+/) gained
// one for the first time. Not one command was removed, which is what makes this
// a path relocation rather than a feature deletion.
// 51 SINCE 2026-08-19: the MARKER MEASURE button joined the bottom row's
// marker-verb group on bare `/`, a pure chord addition — 50 + 1, split
// 48 + 2 to 49 + 2.
// 50 AT 2026-08-18'S THIRD ROSTER RULING: the two WALK RADIOS joined the
// icon row's history group between the opener and the cumulative toggle, both
// on bare `g` — two chord additions, 48 + 2, split 46 + 2 to 48 + 2. The same
// ruling took the WALK SELECTOR off row 3 and gave the tabs back to A/B, which
// cost the roster nothing: row 3 has always been two slots, and what moved was
// what they mean.
// 48 EARLIER THAT DAY, AT THE SECOND ROSTER RULING: ADD TO SELECTION joined the
// bottom row's marker-verb group on bare `k`, a pure chord addition — 47 + 1,
// split 45 + 2 to 46 + 2.
// 47 EARLIER THAT DAY, AT THE ROSTER RELAYOUT, and the arithmetic is one loss over
// three moves: the TRIM SCISSORS were deleted whole (48 − 1, split 46 + 2 to
// 45 + 2 — the chord table lost its row with the button and bare `x` is
// untouched), while the four SINGLE-MARKER VERBS moved to the bottom row and
// the four HISTORY COMPANIONS moved back up to row 4, both CHANGING ROWS rather
// than leaving the roster, so neither the total nor the split felt them.
// 48 SINCE 2026-08-16: the SHOW TRIM REGION button joined the trim group in
// row 4 (Ctrl+Shift+X then, bare `x` since 2026-08-18 — the repointing changed
// no count), a pure chord addition — 47 + 1, split 46 + 2.
// 47 SINCE 2026-08-15's SECOND ROW-1 RULING: the NAVIGATION ANCHOR left with its
// menu, which is the roster's first LOSS of a non-chord entry (48 - 1) and takes
// the split from 45 + 3 back to 45 + 2.
// 48 EARLIER THAT DAY, and it was TWO NET over three days' worth of arithmetic
// in one commit: the MARKER-WALK GROUP added three to the bottom row and the
// PLAY/STOP COLLAPSE took one away (46 - 1 + 3), the pair having been two
// buttons over the one bare-Space chord since the row's first day.
// 46 = 45 + THE READ-ONLY TOGGLE (2026-08-14, the padlock leaving the tabs);
// the same day's other two moves cost nothing, the four history companions
// having CHANGED ROWS rather than left the roster. 45 was the 2026-08-12 grand
// relayout's roster commit:
// 37 with the toolbar row's four MOVED into row 4 (no count change) plus the
// ZOOM GROUP's four and the SINGLE-MARKER VERBS' four. It was 37 from late
// 2026-08-11 (the trim button joining the transport row's eight, which had
// landed 36 = 28 + 9 − the same-day-deleted Esc button earlier that day); 28
// before that, and 29 before 2026-08-08, when row 3's compare-only pair was
// deleted and row 4 gained the Cumulative toggle.
inline constexpr int kRedesignButtonCount = 50;
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

// WHICH BUTTONS ARE ROW 1'S — the menu row's two plus the view bar's three,
// named beside the roster because that is where a reader meets the membership.
// The enum's order IS the painted order, so the five happen to be contiguous at
// its head; this says ROW rather than "index < 5" anyway, because the row is
// the fact and the contiguity is an accident of how the roster is written.
//
// ITS ONE CONSUMER IS THE DROPDOWN CLOSE RULE (on_motion's open-dropdown branch,
// input_pointer.cpp): while a menu is up, a pointer inside a row-1 button that is
// not a dropdown anchor CLOSES it, because only one button in that row is lit at
// a time. WHAT THAT LEAVES, re-derived from the two predicates rather than
// inherited: row 1 is SIX buttons and THREE of them are anchors, so the close
// rule covers THE VIEW BAR'S THREE alone — the same three it covered while
// Navigation was a third anchor and the same three since EDIT became one
// (2026-08-20). It was "Quit or the view bar's three" while the Quit button
// existed; the Navigation anchor's 2026-08-15 deletion moved this membership
// not at all and Edit's arrival moved it not at all either, an anchor JOINING
// or LEAVING the row being the one change this rule cannot feel — which is why
// the count above is re-derived here rather than the membership being edited.
// It was briefly the hover predicate's too — an exemption letting row 1
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
        case RedesignButton::File:
        case RedesignButton::Edit:
        case RedesignButton::Settings:
        case RedesignButton::ViewSW:
        case RedesignButton::ViewTP:
        case RedesignButton::ViewTW:
            return true;
        case RedesignButton::TabA:
        case RedesignButton::TabB:
        case RedesignButton::Save:
        case RedesignButton::Undo:
        case RedesignButton::Redo:
        case RedesignButton::Render:
        case RedesignButton::IconS:
        case RedesignButton::IconT:
        case RedesignButton::IconW:
        case RedesignButton::IconP:
        case RedesignButton::IconShowRegion:
        case RedesignButton::IconZoomIn:
        case RedesignButton::IconZoomOut:
        case RedesignButton::IconZoomFitBest:
        case RedesignButton::IconZoomOriginal:
        case RedesignButton::IconBpm:
        case RedesignButton::IconIter:
        case RedesignButton::IconFollow:
        case RedesignButton::IconListen:
        case RedesignButton::IconLoadInPlace:
        case RedesignButton::IconReadOnly:
        case RedesignButton::IconHistory:
        case RedesignButton::HistoryWalkGit:
        case RedesignButton::HistoryWalkSession:
        case RedesignButton::HistoryCumulative:
        case RedesignButton::HistoryRevert:
        case RedesignButton::HistoryOlder:
        case RedesignButton::HistoryNewer:
        case RedesignButton::TransportSkipBack:
        case RedesignButton::TransportPlayStop:
        case RedesignButton::TransportSkipForward:
        case RedesignButton::IconMarkerDrop:
        case RedesignButton::IconMarkerDelete:
        case RedesignButton::IconMarkerDisable:
        case RedesignButton::IconMarkerInherit:
        case RedesignButton::IconMarkerMeasure:
        case RedesignButton::IconAddToSelection:
        case RedesignButton::TransportWalkPrev:
        case RedesignButton::TransportWalkNext:
        case RedesignButton::TransportWalkBoth:
        case RedesignButton::TransportDown:
        case RedesignButton::TransportUp:
        case RedesignButton::TransportLeft:
        case RedesignButton::TransportRight:
            break;
    }
    return false;
}

// WHICH BUTTONS ARE THE BOTTOM ROW'S — SIXTEEN since 2026-08-19: the
// transport three, the FOUR SINGLE-MARKER VERBS that came down from the icon
// row on 2026-08-18 with the MARKER MEASURE and ADD TO SELECTION landing
// behind them, the MARKER-WALK GROUP's three (2026-08-15) and the four
// cardinal arrows (row 8's from 2026-08-11; tenants of the unified bottom row
// since 2026-08-12). The FOUR HISTORY COMPANIONS were members from 2026-08-14
// until the same relayout took them back up to the icon row. Named
// once because its consumers are all about the ROW'S HOME STRIP rather than
// about any one button: these pixels live in the BOTTOM strip, so every
// damage decision the other rows answer with invalidate_top_strip must answer
// with the bottom row's own rect for these sixteen. THE CONSUMERS, re-greped
// rather than inherited: the hover clear and the hover recompute
// (clear_redesign_button_hover / recompute_redesign_button_hover), the click
// face's arm and its erase (arm_redesign_press / take_chrome_press), the
// per-tick staleness comparator (main.cpp) and the tooltip, which also
// FLIPS ABOVE the button here (the lane rests on the WINDOW'S FOOT since the
// relayout's commit B, so there is nothing below it at all; it was the blank
// foot's own band, zero on a short window, for the afternoon before). Every
// one of them keys off THIS predicate rather than off a button list, so the
// four verbs started answering with the bottom lane and the four companions
// with the top strip the moment the membership above moved.
// A membership predicate like redesign_button_is_tab, deliberately NOT the
// exhaustive-switch shape: redesign_button_in_menu_row above is the roster's
// one classification chokepoint (a new button fails to compile there until its
// row is stated), and one chokepoint is enough.
inline constexpr bool redesign_button_in_transport_row(RedesignButton b) {
    switch (b) {
        case RedesignButton::TransportSkipBack:
        case RedesignButton::TransportPlayStop:
        case RedesignButton::TransportSkipForward:
        case RedesignButton::IconMarkerDrop:
        case RedesignButton::IconMarkerDelete:
        case RedesignButton::IconMarkerDisable:
        case RedesignButton::IconMarkerInherit:
        case RedesignButton::IconMarkerMeasure:
        case RedesignButton::IconAddToSelection:
        case RedesignButton::TransportWalkPrev:
        case RedesignButton::TransportWalkNext:
        case RedesignButton::TransportWalkBoth:
        case RedesignButton::TransportDown:
        case RedesignButton::TransportUp:
        case RedesignButton::TransportLeft:
        case RedesignButton::TransportRight:
            return true;
        default:
            return false;
    }
}

// WHICH BUTTONS ARE THE HISTORY COMPANIONS — the four that close the icon
// row's last group, behind the opener and the two WALK RADIOS that joined it
// between them later on 2026-08-18 (they were the bottom row's right
// cluster from 2026-08-14 until the 2026-08-18 relayout brought them back up;
// the membership never moved with the row). THE WALK RADIOS ARE DELIBERATELY
// NOT MEMBERS, and the reason is this predicate's one job: it lifts a button
// over the DERIVED `h` partition, and the radios need no lift — bare `g` is the
// mode's own vocabulary, so the partition already answers LIVE for them inside
// the view. Their RESTING grey is the same arm the four take at
// redesign_button_enabled, which is a different fact from this one. Named 2026-08-15
// for its ONE consumer, and the consumer is what the name has to earn: REVERT
// is the one member whose chord the mode admits CONDITIONALLY (on a diff flag
// standing), so the derived `h` partition would grey it INSIDE the view. This
// membership is what lifts the four over that partition at
// redesign_button_enabled — a one-line exception
// to a derived rule, named rather than spelled inline so the exception and the
// cluster are one fact. It is deliberately the FOUR and not just Revert: the
// ruling is about the cluster, and a future companion whose chord the mode
// gates conditionally must inherit it without a second edit.
//
// IT SAYS NOTHING ABOUT THE FACE OUTSIDE THE VIEW, and that is worth stating
// since 2026-08-18: the four grey out there again (the icon row's own mode
// rule, revived when they came back to a row that hides nothing), and that
// answer is the arm's own — this predicate only keeps the mode's conditional
// grey off them while the view stands.
inline constexpr bool redesign_button_is_history_companion(RedesignButton b) {
    switch (b) {
        case RedesignButton::HistoryCumulative:
        case RedesignButton::HistoryRevert:
        case RedesignButton::HistoryOlder:
        case RedesignButton::HistoryNewer:
            return true;
        default:
            return false;
    }
}

// WHICH BUTTONS ARE ROW 3'S — the two tabs, which are the A/B pair in every
// state since 2026-08-18. ONE consumer, re-greped rather than inherited: the
// HOVER CARVE-OUT (the selected tab has no hover face), which is about the ROW
// rather than about either slot. It had three more while the row was the `h`
// view's walk selector — a tooltip override, a label override and the mode's
// own press claim — and all three are deleted with that repurposing; the roster
// entry for TabA/TabB carries the record.
inline constexpr bool redesign_button_is_tab(RedesignButton b) {
    return b == RedesignButton::TabA || b == RedesignButton::TabB;
}

// (WHICH BUTTONS ARE ROW 4'S — `redesign_button_in_icon_row`, derived from the
// other three rows rather than listed — IS DELETED PRODUCER-LESS with the
// collapse rule it was written for, 2026-08-14. Its two readers were that
// rule's group arithmetic (which had to stop at the row's edges) and its
// scope test. The derivation itself is still how the ROSTER partitions —
// what is none of the other three rows is the icon row's, which is what moved
// the four history companions out of that row for free when they joined the
// bottom one — but nothing needs to ASK it any more: the icon row's painter
// walks its own table, and the bottom row's walks its two.)

// THE ICON ROW'S GROUP BOUNDARIES — true for the button that OPENS each
// separator-led group, which is this row's whole grouping vocabulary (4px, a
// 1px line, 4px; everything else in a group is 2px from its neighbour). The
// row's first group opens on Save and draws no line, the painter's walk
// suppressing the separator ahead of its first member rather than this
// predicate carrying a third state.
//
// IT LIVES HERE, BESIDE THE ROSTER, rather than as a column in the painter's
// table: it was hoisted for the collapse rule (which asked whole groups) and
// KEPT when that rule was deleted on 2026-08-14, because the dividers are a
// fact about the roster's order and this is where the order is stated. ONE
// reader now — paint_icon_row's layout walk.
//
// THE EIGHT GROUPS, in painted order: the toolbar four, the S/T radios, the
// W/P radios, THE TRIM GROUP (the Show trim region button, alone in it since
// the scissors were deleted on 2026-08-18 — it was opened by the scissors on
// 2026-08-11 as the architect's slot for viewport-class acts, filled by that
// button as the second such act on 2026-08-16 and led by it later
// the same day), the zoom four, the mass-marker acts, the render-entry group
// (listen, load-in-place, the read-only toggle) and THE HISTORY GROUP — the
// opener, its two WALK RADIOS and its four companions.
//
// STILL EIGHT WITH THE WALK RADIOS (later on 2026-08-18): they land INSIDE the
// history group, between the opener and the cumulative toggle, so the row gains
// two boxes and two 2px gaps and no boundary moves — a group's leader is the
// only thing this predicate names.
//
// EIGHT ACROSS THE 2026-08-18 RELAYOUT, and the two edits here are what kept it
// there rather than arithmetic that happened to cancel: the SINGLE-MARKER VERBS
// left the row for the bottom one, so IconMarkerDrop stopped opening a group,
// and the HISTORY OPENER left the render-entry group to lead one again, so
// IconHistory started. The scissors' deletion cost no boundary — they were the
// trim group's SECOND member, and a group survives losing one.
inline constexpr bool redesign_button_opens_icon_group(RedesignButton b) {
    switch (b) {
        case RedesignButton::Save:
        case RedesignButton::IconS:
        case RedesignButton::IconW:
        case RedesignButton::IconShowRegion:
        case RedesignButton::IconZoomIn:
        // THE MASS-MARKER GROUP'S LEADER IS THE BPM OPENER SINCE 2026-08-20,
        // and it moved because the group's first two members were DELETED with
        // the propagate relocation, not because the grouping changed: IconCopy
        // led it from the row's first day, and with copy and paste gone the
        // separator has to fall in front of whatever is first now or the
        // remaining three would merge into the zoom group.
        case RedesignButton::IconBpm:
        case RedesignButton::IconListen:
        case RedesignButton::IconHistory:
            return true;
        default:
            return false;
    }
}

// THE MENU ROW'S DROPDOWNS — WHICH ONE IS UP. There is ONE popup state in the
// product (AppState::dropdown below), and this names its content; `None` IS the
// closed state, which is what makes "two dropdowns are never open together"
// structural rather than an invariant to maintain: opening one is writing this
// field, and a field holds one value. FILE joined 2026-08-13 with the Quit
// button's retirement, and cost the shape nothing for exactly that reason —
// and NAVIGATION (2026-08-02) left the same way on 2026-08-15, deleted whole
// once every one of its seven rows had a button of its own. That the count has
// been two, three, two and three again without a single route changing is the
// shape's own proof: nothing here counts menus. EDIT joined 2026-08-20 with the
// propagate relocation and cost the shape nothing either.
enum class DropdownMenu { None, File, Edit, Settings };

// EVERY MENU THERE IS, in one place, so the routes that must walk them all —
// the press claim's anchor test, the hover switch, the armed hover open — walk
// this instead of naming a pair (or a triple). `None` is deliberately absent:
// it is the closed state, not a menu.
inline constexpr DropdownMenu kDropdownMenus[] = {
    DropdownMenu::File, DropdownMenu::Edit, DropdownMenu::Settings,
};

// WHICH BUTTON A MENU HANGS FROM. The dropdown is flush under the button that
// emits it (architect 2026-08-02), so the painter and the open edge's damage
// both need the anchor, and they must read ONE expression or the damaged band
// and the painted box could start on different rows of pixels.
inline constexpr RedesignButton dropdown_anchor_button(DropdownMenu m) {
    switch (m) {
        case DropdownMenu::File:     return RedesignButton::File;
        case DropdownMenu::Edit:     return RedesignButton::Edit;
        case DropdownMenu::Settings:
        case DropdownMenu::None:     break;
    }
    return RedesignButton::Settings;
}

// IS THIS BUTTON A MENU ANCHOR? DERIVED from the menu list above through the
// anchor owner, never a second list — so a menu added here is an anchor
// everywhere at once (the close rule's skip, the press claim, the history
// partition). The COLLAPSE walk asked it too until 2026-08-13, then asked
// `redesign_button_in_icon_row` instead; both are gone with that rule
// (2026-08-14), and a menu cannot vanish because nothing hides a button in
// these rows at all any more.
inline constexpr bool redesign_button_is_menu_anchor(RedesignButton b) {
    for (const DropdownMenu m : kDropdownMenus)
        if (dropdown_anchor_button(m) == b) return true;
    return false;
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

// THE COMMAND MENU'S ITEMS — the row type a menu of COMMANDS uses (the settings
// menu is the other kind, a list of keys to edit). It carries the row's LABEL,
// the accelerator column's text, the chord the release dispatches through
// on_key, and where a category parts.
//
// IT HAS TWO INSTANCES AGAIN SINCE 2026-08-20 — the FILE table below
// (2026-08-13) and the EDIT one beside it — and it was always going to: it was
// two from 2026-08-02 until 2026-08-15 (NAVIGATION was the other, and the
// first — this row type is its shape, which File then landed on with no edit to
// the painter or the release at all), one for five days, and two again. That
// the type survived its lone-instance stretch unchanged is what made the Edit
// menu a TABLE and nothing else. What it names is the KIND of menu, which is
// what the release body forks on; the kind is two-valued.
//
// EDIT'S ROWS ARE THIS TYPE'S FIRST ALT-BEARING ITEMS (`alt` has been in the
// struct since the type was written, and finish_dropdown_release has always
// composed the chord from all three bits, so nothing here needed changing —
// but no item had ever set it before, which is worth knowing if an alt row ever
// misbehaves).
struct CommandPopupItem {
    const char* label;
    const char* hotkey;   // the accelerator column's text, right-aligned
    GuiKey      key;
    bool        ctrl;
    bool        shift;
    bool        alt;
    bool        separator_before;
};

// THE FILE DROPDOWN'S ITEMS (architect 2026-08-13) — ONE ROW, "Quit", the
// standard home for it and where kdenlive keeps it. It is the whole menu by
// ruling ("File contains Quit and nothing else"): Save and Render stay the icon
// row's, and the menu is deliberately minimal. NO SEPARATOR — one category, and
// chrome around a single item would be chrome around nothing.
//
// THE ITEM IS ITS CHORD: Ctrl+Q dispatched through on_key, so the drag-modal
// hatch, the dirty prompt and the WM-close ordering are the keyboard route's own
// with no second body. Ctrl+Q is on the history view's own allowlist, so this
// menu works inside the `h` view exactly as it does outside it.
//
// IT DISPLAYS ITS HOTKEY, by explicit architect design and against nothing: the
// no-gesture-hints-in-UI preference is about hint PROSE inside labels, and the
// architect ordered kdenlive's accelerator column on the command menus (its own
// crop, dropdown_full_hotkeys.png, is the anatomy). Since 2026-08-15 this one
// row is the accelerator column's WHOLE producer — the Navigation menu, which
// the column was authored for, is deleted — so the column's metrics and its
// layout term survive on this row alone and are not producer-less. The crop's
// spelling convention is "modifiers spelled out with `+`", which "Ctrl+Q" is;
// the convention's OTHER half — a bare letter written UPPERCASE — has no
// instance left in the product and is recorded at RedesignTooltipText, where it
// used to be contrasted with the tooltips' lowercase rule.
//
// AN ITEM NEVER GREYS OUT AND NEVER REFUSES HERE, with NO EXCEPTION since
// 2026-08-15: a command that cannot act right now still dispatches and its own
// arm answers, which is the roster's standing buttons-never-grey rule ("one
// that cannot act right now simply does nothing, exactly like its key") applied
// one surface further out. THE RULE HELD ONE RULED EXCEPTION from 2026-08-08
// until the Navigation menu's deletion, and it is recorded because the argument
// is the good one and would apply again: "Walk both tabs" greyed INSIDE the `h`
// HISTORY VIEW, where Ctrl+Shift+Tab was not the A/B walk at all — the mode
// claimed it as the REVERSE cycle of its own walk-selector row — so an item left
// live would have dispatched a chord doing something else entirely under a label
// promising the walk. (The premise is gone since 2026-08-18: the walk selector
// has its own radio pair and the chord marches the pair in the view too, over
// the diff flags.) It greyed rather than lying, which is a difference in KIND
// from every other refusal on a command menu, all of which are "the same
// command, with nothing to act on". THAT ROW DIED WITH ITS MENU and the
// predicate that served it (dropdown_item_enabled) is deleted producer-less with
// its four readers; the rule above is complete again rather than having lost a
// clause, and a future item whose LABEL would lie in some mode needs the
// predicate back, not a grey bolted onto a caller.
inline constexpr CommandPopupItem kFilePopupItems[] = {
    {"Quit", "Ctrl+Q", GuiKeys::Q, true, false, false, false},
};
inline constexpr int kFilePopupItemCount =
    static_cast<int>(std::size(kFilePopupItems));

// THE EDIT DROPDOWN'S ITEMS (architect 2026-08-20) — THE FIVE PROPAGATE
// COMMANDS, in two categories over one separator: the PHASE RESET family's
// three, then the MEASURE family's two. The order inside each is the family's
// own — copy, then paste, then (where there is one) the variant paste — and the
// separator is the honest place the two categories part, exactly as the
// settings menu's is.
//
// THIS MENU IS THE FIVE COMMANDS' ONE POINTER HOME. IconCopy and IconPaste were
// deleted from the icon row in the same ruling, so nothing here duplicates a
// button: the no-second-road doctrine is SATISFIED rather than amended, which
// is the zoom group's history run in reverse (there, four BUTTONS took the
// Navigation menu's rows and the menu went; here the menu takes the buttons'
// commands and the buttons go). The architect's reason for the direction is
// FREQUENCY — all five are used a handful of times per session at most, which
// is what a menu is for and what a permanent icon slot is not — and the BPM
// opener's survival at similar frequency is his stated aesthetic choice,
// recorded at the roster enum rather than argued from here.
//
// THREE OF THE FIVE HAD NO POINTER HOME AT ALL BEFORE THIS. IconCopy and
// IconPaste carried Ctrl+P and Ctrl+Alt+P (the shifted paste-state chord riding
// IconPaste's shift admission, which left with it); Ctrl+/ and Ctrl+Alt+/, the
// measure pair, landed 2026-08-20 with no button by design, waiting for this
// menu. So the relocation NET-ADDS a pointer road to the measure propagate and
// removes none from the phase one.
//
// EVERY ROW DISPLAYS ITS HOTKEY, the accelerator column File's one row has
// carried alone since 2026-08-15 — the column now has six producers instead of
// one, and its metrics and layout term are unchanged. The spelling convention
// is the crop's: modifiers spelled out with `+`, and a non-letter key written
// as itself (`/`).
//
// AN ITEM NEVER GREYS, the standing rule stated in full at kFilePopupItems: a
// command that cannot act right now — wrong mode, wrong selection, an empty
// clipboard, a locked tab — still dispatches, and its own arm answers with the
// silent refusal the key gives. Every one of these five refuses silently by
// construction, so there is nothing here that could lie the way the deleted
// Navigation menu's one ruled exception could.
inline constexpr CommandPopupItem kEditPopupItems[] = {
    {"Copy phase resets",      "Ctrl+P",           GuiKeys::P,
     true,  false, false, false},
    {"Paste phase resets",     "Ctrl+Alt+P",       GuiKeys::P,
     true,  false, true,  false},
    {"Paste phase reset state", "Ctrl+Alt+Shift+P", GuiKeys::P,
     true,  true,  true,  false},
    {"Copy measures",          "Ctrl+/",           GuiKeys::Slash,
     true,  false, false, true},
    {"Paste measures",         "Ctrl+Alt+/",       GuiKeys::Slash,
     true,  false, true,  false},
};
inline constexpr int kEditPopupItemCount =
    static_cast<int>(std::size(kEditPopupItems));

// (THE NAVIGATION DROPDOWN'S ITEMS ARE DELETED — architect 2026-08-15. From
// 2026-08-02 kNavigationPopupItems held SEVEN rows in two categories over one
// separator: "Zoom in" `=`, "Zoom out" `-`, "Overview" `0` and "Center on focus"
// `C`, then "Next marker" Tab, "Previous marker" Shift+Tab and "Walk both tabs"
// Ctrl+Shift+Tab. Every row WAS an existing keyboard command dispatched through
// on_key exactly as a redesigned button dispatches its chord, which is the model
// the File menu inherited whole and which survives here.
//
// WHY IT WENT, and it is a duplication argument rather than a design reversal:
// the four zoom/framing rows had been duplicated by the icon row's ZOOM GROUP
// since the 2026-08-12 relayout — a duplication the architect ACCEPTED at the
// time, superseding his own 2026-08-02 no-duplicate-commands ruling for those
// four so the glass rig had a pointer home for them — and the three
// marker/tab steppers were duplicated by the bottom row's MARKER-WALK GROUP on
// 2026-08-15. With that the menu was a slower path to seven commands that all
// had buttons, and the accepted duplication had nothing left to buy: "we should
// go ahead and remove the navigation dropdown altogether". NOT ONE COMMAND WAS
// REMOVED — all seven keep both their key and a button, which is what makes this
// a path deletion.
//
// WHAT WENT WITH IT: the Navigation ANCHOR and its roster entry, the menu
// enumerator, dropdown_item_enabled and its four readers (the menu's "Walk both
// tabs" row inside the `h` view was that predicate's ONE producer — the full
// record, and the argument that would bring it back, are at kFilePopupItems
// above), and the two sampled disabled inks it drew with. WHAT DID NOT: the
// accelerator column, whose producer is now File's "Ctrl+Q" alone; the command
// menu KIND and this row type; and the `h`-view lockout's menu scope, whose
// reason is re-derived at toggle_dropdown.)

// The published-rect array's size: the widest menu decides it, so a menu that
// grows a row grows the array with no second edit. (File's one row cannot be
// the widest and is in the expression anyway — the rule is "the widest menu",
// not "the menus that happen to be long".)
inline constexpr int kDropdownMaxItemCount =
    std::max({kFilePopupItemCount, kEditPopupItemCount,
              kSettingsPopupItemCount});

// IS THIS A COMMAND MENU? The two kinds of menu differ in what a row DOES — a
// settings key to prefill, a chord to dispatch — and this names the second kind
// once, for the row lookup below and for the release's dispatch fork. It is a
// PREDICATE over the menu rather than a comparison at each site, which is what
// let the second command menu join it in one line: the kind is the fact the
// release forks on, and Edit landed on it 2026-08-20 exactly as anticipated.
inline constexpr bool dropdown_is_command_menu(DropdownMenu m) {
    return m == DropdownMenu::File || m == DropdownMenu::Edit;
}
// THE COMMAND ROW ITSELF — the ONE place that maps a command menu to its table,
// read by the shared view below and by the release body that dispatches the
// chord. Callers ask dropdown_is_command_menu first; a non-command menu answers
// with the File table's row, which no caller reaches. It forked between two
// tables until 2026-08-15, the fork went with the Navigation menu, and it is
// BACK since 2026-08-20 with the Edit menu — the parameter that was kept
// deliberately unread through that stretch is read again, and neither call site
// changed on either day, which is what keeping it bought.
inline constexpr const CommandPopupItem& command_popup_item(DropdownMenu m,
                                                            int i) {
    if (m == DropdownMenu::Edit)
        return kEditPopupItems[static_cast<size_t>(i)];
    return kFilePopupItems[static_cast<size_t>(i)];
}

// THE PAINTER'S AND THE GEOMETRY'S VIEW OF AN ITEM — what the menus share,
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
        case DropdownMenu::File:     return kFilePopupItemCount;
        case DropdownMenu::Edit:     return kEditPopupItemCount;
        case DropdownMenu::Settings: return kSettingsPopupItemCount;
        case DropdownMenu::None:     break;
    }
    return 0;
}
inline constexpr DropdownRow dropdown_row(DropdownMenu m, int i) {
    if (dropdown_is_command_menu(m)) {
        const CommandPopupItem& it = command_popup_item(m, i);
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

// THE CHROME ROSTER'S SHIFT LONG-PRESS BEAT (architect 2026-08-13) — how long a
// press must be HELD on a shift-admitting button (the membership is
// redesign_button_shift_admits alone, never a count) before its lift
// dispatches the SHIFTED twin instead of the plain act. It is what gives glass
// the shift acts: the road rig has no keyboard, so a finger could reach only
// half of each shifted pair. Read at exactly one site, the lift's chord build
// (finish_chrome_press_release, input_pointer.cpp), against the press stamp the
// arm carries (AppState::ChromePress::press_ms).
//
// IT READS kHoldBeatMs (gui_input.h), THE PRODUCT'S ONE HOLD BEAT — the same
// number the touch pan zone's region hold (kTouchRegionHoldMs) reads, matched
// by convention with the compositor's key-repeat delay, so every deliberate
// hold in the product crosses its threshold on one beat. The constant's own
// declaration carries that ruling, including why the keyboard's delay stays
// the compositor's rather than joining as a third copy.
//
// It rides NO SCALE, deliberately: a duration is not a length, so gui_scale has
// nothing to say about it (the same rule the drag-slop and disambiguation
// constants carry for their own reason — they model the hand, not the pixels).
//
// The beat is DELIBERATELY LONG relative to a click. A shifted act is the rarer
// one on every button that admits shift (the membership is
// redesign_button_shift_admits, never a count restated here), so the cost of an
// accidental hold must land on the
// rare act rather than on the common one; the beat is well past any ordinary
// click-and-lift and just short of the point where a user would assume the
// press was lost.
//
// IT PASSES SILENTLY AND IS RULED TO (architect 2026-08-13): the gesture is the
// TOUCH PANEL's, and tooltips do not show there, so a hint at the beat would
// ride a surface the gesture's only user never sees. No feedback is to be
// built for this constant; the ruling's home is the read site.
constexpr int64_t kChromeShiftHoldMs  = kHoldBeatMs;

// ONE generic Chebyshev pixel distance a press must travel before it becomes a
// DRAG (architect-tunable), shared by EVERY press-becomes-drag surface. THE
// LIST IS RE-DERIVED FROM THE GATES THEMSELVES (codex round 19 — it named
// "strip, region, trim, and the marker flag" long after the strip drag's
// deletion and the two 2026-08-15 additions; re-derived again 2026-08-18, when
// the region's editor was deleted into the trim drags it had been borrowing),
// and it is SIX states — five that latch their own `moved` in on_motion, plus
// one that resolves at the crossing without latching anything:
//   * ScrollDragState — THE ONE NAV DRAG, the pending click whose crossing
//     becomes the grab-pan or, with ctrl, the zoom (the capture begins at that
//     crossing);
//   * RegionDragState — THE SWEEP, the shift drag and the touch region hold,
//     through apply_region_drag_motion's own gate (a direct trim write since
//     2026-08-18; the overlay's move and bound drags are the TRIM pending
//     below, that gesture having become the trim's own drags on a second
//     surface rather than an editor of its own);
//   * OverviewDragState — the overview lane's box gestures (2026-08-12; the
//     lane's DELETED ctrl strip drag is what "strip" used to name here, and
//     this state is not it): the box pan and the two edge drags — an OUTSIDE
//     press is the pan too, after its own teleport at the press (the Pending
//     kind that deferred that teleport was a member for two days,
//     2026-08-15..17, and is deleted);
//   * PendingTrimDrag — the trim bar's endcap / bridge drag;
//   * PendingMarkerPress — the marker flag's PLAIN press, its click already
//     acted at the press (2026-08-17), whose crossing becomes the reposition
//     drag (the tempo flag was one more until the tempo drag's deletion,
//     2026-07-29);
//   * PendingClickAct — the trim bar's deferred bound-set click (the one
//     surviving lift act, 2026-08-17), which is the one member that latches NO
//     `moved` of its own: its crossing RESOLVES the arm outright, running the
//     set and handing over to the endcap drag above, so there is no moved
//     phase for it to be in.
// (A derived reader sat outside that list until 2026-08-18: the region
// former's SLIVER FLOOR, end_region_drag_min_size_check, measured a rested span
// against this same constant so that "never became a drag" and "never left the
// slack" were one number. It is DELETED with the free span it protected — the
// sweep writes the trim per motion event, so this threshold is once again only
// what it says it is: the line between a click and a drag. A jitter drag that
// crosses it commits the sliver it drew, nothing dissolving it and — since the
// minimum width floor's retirement, 2026-08-19 — nothing widening it either.)
// The TOUCH slop is a separate constant deliberately equal to it
// (kTouchSlopPx, platform_wayland.cpp — the platform sits below this header),
// which is what makes a quick finger drag cross both gates at once.
// UNIFIED to 8px (architect
// 2026-07-24: region felt too hair-trigger at the old 3, and the separate
// kMarkerDragMovedThresholdPx = 8 was folded into this one constant). Two
// rationales, now one story:
//  - CAPTURE-JITTER / DOUBLE-CLICK STARVATION (the captured nav drag, the
//    waveform region): under
//    pointer capture the relative-pointer stream delivers every sub-pixel sensor
//    tick as a motion event, so a physical click almost always rocks the sensor a
//    count or two; without this gate that jitter would mark every click as moved
//    and starve double-click detection, and on the waveform a click would become
//    a micro-region instead of plain playhead placement.
//  - MARKER GRAB SLOP (the marker flag): a flag must be easy to click
//    (select, or double-click to edit) without nudging it, and pixel-exact
//    fine-tuning lives on the bare Left/Right nudge rather than the drag — the
//    Ableton convention. 8px gives that slop; the other five surfaces inherit
//    it.
// One latch shape everywhere — a motion event below the threshold is ignored
// outright (moved stays false, no apply, the drag stays armed); once a drag,
// always a drag, so dragging back near the press has no dead zone. The NAV
// drag leaves last_x at the press until the crossing, so the crossing
// event folds the whole accumulated delta and no travel is lost (the two
// absolute-placement drags — overview and region edit — fold it by
// construction, placing per event rather than accumulating).
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
};

// In-window modal prompt state. When `active` is true, THE BOTTOM ROW IS THE
// PROMPT (paint_modal_dialog, 2026-08-13 — the row's tenants stand down whole
// and the lane carries the prompt's text at its left pad with one REAL BUTTON
// per response right-aligned at its right pad; the centered box of 2026-08-12
// is scrapped, and before that arc the prompts were a tier in this same row's
// status span).
// Keyboard input is owned by the prompt exactly as it always was: only the
// response keys (and Esc, which activates the rightmost response) do
// anything; everything else is swallowed. `response_keys` holds lowercase
// letters and the match is CASE-SENSITIVE on the codepoint (the rule is at
// the prompt dispatch, input_handler.cpp); the two non-letter responses match
// on the GuiKey (Delete, Escape) instead.
// `response_labels` are the BUTTONS' words, one per response, parallel to
// response_keys, IN PLAIN WORDS — "Save", "Discard", "Cancel", "Retry",
// "Yes", and "OK" on the dismiss-only error notice. The two key-named
// sentinels take the word their MEANING spells rather than their key's name:
// '\x7f' (Delete, the discard-and-proceed) is "Discard", '\x1b' (Escape) is
// "Cancel".
//
// THE BRACKETED ACCELERATOR SPELLING ("[S]ave", "[D]iscard") IS RETIRED FOR
// THE SECOND TIME AND WITH ITS REASON RECORDED, so it is not proposed a third
// (architect 2026-08-13, at his live look): it went with the bottom-strip
// status line on 2026-08-12, came back with the row hours later — the buttons
// being touch targets that could also name the letter that answers them — and
// he read the result plainly: "the brackets now look odd... a normal button
// would use an underline for the character... I guess what we could do is do
// tooltips for the buttons. So we don't do underscores or underline or
// brackets or anything like that." SO THE KEY IS NAMED ON THE BUTTON'S
// TOOLTIP, the product's own way of naming a chord (the roster's hint format
// verbatim — "Save (s)"), composed at modal_dialog_button_hint below from what
// the button DISPATCHES. NONE OF THIS EVER REACHED THE MATCHING:
// response_keys and the codepoint-exact lowercase compare are untouched
// through all three spellings, so a typed capital still does not answer.
// A PROMPT DOES HAVE AN ENTER ANSWER, AND IT IS THE PASSIVELY FOCUSED BUTTON
// (architect 2026-08-13, SUPERSEDING this block's own standing ruling — "no
// button wears the default face; this prompt system HAS no Enter answer,
// Return is not a response key and does nothing", the decision recorded here
// when the buttons landed and again at the focus ring). Every prompt is now
// RAISED with PASSIVE focus on its LAST button, and bare Enter or bare Space
// presses the focused button down and commits it on the key's release. What
// makes that safe is not the absence of a default but two facts that were not
// available when the old rule was written:
//   (i)  THE LAST BUTTON IS THE ESCAPE SENTINEL — the non-destructive answer,
//        by construction rather than by convention. All four raisers put
//        '\x1b' last: the unsaved-work prompt (Save / Discard / CANCEL), its
//        save-failed restatement (Retry / Discard / CANCEL), the paste
//        confirmation (Yes / CANCEL) and the error notice (OK, whose one
//        response IS the sentinel). So the key that answers without asking
//        answers the way Esc already does, and no destructive response is ever
//        one Enter away.
//   (ii) THE PAINTED GATE below already consumes every key until the prompt
//        has been on screen, so an Enter queued behind a raise answers
//        nothing — the exact hazard the old rule was reaching for, closed
//        structurally instead of by leaving the ring empty.
// The two focus STRENGTHS (passive, assigned; active, walked onto) are at
// AppState::modal_dialog_focus_active, which owns the model; the face ladder
// is paint_modal_dialog's.
//
// A PROMPT ANSWERS ONLY AFTER IT HAS PAINTED (2026-08-13, codex round 13 —
// the one lag span the in-window modal still had; unchanged in substance by
// the move onto the bottom row later that day, which moved the modal's
// rectangle and nothing about this gate). One dispatch batch is
// delivered whole (wl_display_dispatch_pending) before the loop paints, so a
// prompt can be RAISED and RECEIVE INPUT while the surface still shows the
// prior world: dirty work + an editor dialog up + Ctrl+Q and Delete in one
// batch tore the editor down, raised this prompt, and let the Delete answer
// Discard — the work gone, the question never seen. `painted` closes that
// span BY THE PAINTER'S OWN HAND: it is false at every raise (`present`
// below is the one route, so no raiser can forget it) and the ONE writer of
// true is paint_modal_dialog's prompt branch — the same surface, the same
// run-loop iteration, the same buffer the frame then commits. No
// synchronization machinery, no generation counter, no probe: that locality
// IS the mechanism, and it is precisely what a SECOND Wayland surface cannot
// have (the scrapped real-window modal needed five commits of
// tail-sync machinery and each round found another span; the record is in
// conventions.md).
// WHAT THE GATE COSTS: while `painted` is false the prompt consumes EVERY
// key — not just the response letters but Esc and Delete too (the prompt
// dispatch, input_handler.cpp), because a consumed no-answer is the only
// safe reading of input aimed at an unseen surface — and the pointer's
// response claim is gated on it as well (on_button_press; the veil still
// swallows the press, it just answers nothing). The claim needs the gate
// because the button RECTS the pointer reads are the previous paint's
// publication: an editor dialog's buttons publish a zero response key and the
// live-set test refuses them already, but a PROMPT REPLACING A PROMPT — the
// save-failed rung, the one such route — leaves rects whose Discard/Cancel
// keys are still live at coordinates the new (differently laid out) button row
// no longer uses, so the stale-rect press was answerable and destructive. The
// button HOVER face is deliberately not gated: a stale index mislights a
// button until THE NEXT DELIVERED MOTION, which is what re-runs
// update_modal_dialog_hover (the modal branches of on_motion are its only
// callers — it does not ride the per-tick roster recompute), and mislighting
// is all it can do.
// EDITOR DIALOGS ARE DELIBERATELY NOT GATED: a queued key types into the
// buffer, which is non-destructive and self-evident the moment the field
// paints, and the commit is a separate deliberate Enter. The destructive
// shape is the prompt's ONE-KEY ANSWERS, which is exactly what this bit
// guards. A frame deferral (both buffers in flight) simply keeps the bit
// false longer — keys consumed, which is the correct answer.
struct PromptState {
    bool                     active = false;
    bool                     painted = false;   // see the block above
    // THIS QUESTION'S SESSION ID, from the one modal session counter
    // (text_editor::next_session_id — homed there because five of the six
    // modal surfaces are editors; the whole contract is at its declaration).
    // A prompt REPLACING a prompt is a new raise and takes a new id, so the
    // stash comparison alone tells a stale publication from a live one and
    // needs no term about `painted` (which answers a different question: has
    // the user SEEN this surface).
    uint64_t                 session = 0;
    std::string              text;
    std::vector<char>        response_keys;     // lowercase
    std::vector<std::string> response_labels;   // plain words, e.g. "Save"
    DialogTrigger            trigger = DialogTrigger::CLOSE_WINDOW;

    // THE ONE ROUTE THAT PUTS A QUESTION ON THIS STATE — the three raisers
    // (the unsaved-work prompt, the error notice, the paste confirmation) and
    // the save-failed rung's in-place restatement, which is a raise as far as
    // this bit is concerned (a new question the user has not seen). Structural
    // rather than disciplinary: `painted` cannot be left true by a site that
    // forgot to clear it, because there is nowhere else to write the question.
    // The callers own their own damage (invalidate_all — the modal's rect
    // does not exist before the first paint, which the painted gate's own
    // reasoning depends on) and the modal playback stop.
    void present(std::string t,
                 std::vector<char> keys,
                 std::vector<std::string> labels,
                 DialogTrigger trig) {
        active          = true;
        painted         = false;
        session         = text_editor::next_session_id();
        text            = std::move(t);
        response_keys   = std::move(keys);
        response_labels = std::move(labels);
        trigger         = trig;
    }
};

// A MODAL BUTTON'S TOOLTIP TEXT (architect 2026-08-13, the ruling that retired
// the bracketed accelerators: "we just do a tooltip just like the regular icon
// tooltips"). The FORMAT IS THE ROSTER'S OWN — "<word> (<key>)", exactly
// "Set trim from region (x)" — and so is the accelerator's spelling, the one
// rule that table states for itself (redesign_button_tooltip, below): a bare
// letter is LOWERCASE, because it is the key AS TYPED, and here that is not
// merely a convention but the truth about the surface — the prompt match is
// codepoint-exact on the lowercase letter, so a capital would name a press
// that does not answer. Named keys are themselves ("Delete", "Escape",
// "Enter").
//
// THE KEY IS DERIVED FROM WHAT THE BUTTON DISPATCHES, never hand-listed beside
// the words: a PROMPT button carries its response char (the two sentinels
// '\x7f' and '\x1b' being Delete and Escape — PromptState's own mapping), an
// EDITOR button carries `editor_ok`, which IS the session's Enter-or-Escape.
// So a prompt that grows a response, or an editor button that changes which
// key it sends, cannot drift from its own hint.
inline std::string modal_dialog_button_hint(std::string_view word,
                                            char response_key,
                                            bool editor_ok) {
    std::string key;
    if (response_key == '\x7f')      key = "Delete";
    else if (response_key == '\x1b') key = "Escape";
    else if (response_key != 0)      key = std::string(1, response_key);
    else                             key = editor_ok ? "Enter" : "Escape";
    return std::string(word) + " (" + key + ")";
}

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

    // BARE `0`'s RETURN LEVEL (architect 2026-08-18): the zoom level the key was
    // most recently pressed at while still BELOW the effective ceiling. `0`
    // stamps it on the way out to full zoom-out and spends it when pressed again
    // already there, so the round trip is overview and then back to the
    // magnification the user left — the LEVEL only, not the window: the return
    // trip re-centers the way `c` centers rather than restoring the exact
    // viewport (architect, asked directly). EMPTY means nothing has been stamped
    // in this tab yet and the return arm is then the plain `c` command, which is
    // both the session's opening state and the project that opens ALREADY at
    // full zoom out. The slot can only be found empty at the ceiling when the
    // ceiling was reached some other way, since every arrival there by this key
    // stamps on the way. A stamp STRANDED above a ceiling that has since fallen
    // spends as an empty slot too, and is left standing rather than cleared —
    // the reasoning is at the spending arm, which is where the ceiling is known.
    // run_overview_command (input_handler.cpp) is the ONE writer: a manual
    // `=`/`-` step does not stamp, `c` does not, the wheel does not, no drag or
    // touch gesture does, and nothing clears it — that is what makes the round
    // trip predictable.
    // SESSION SCRATCH, DELIBERATELY ABSENT FROM kSettingsOrder (settings_io.cpp):
    // the one member of this struct that is NOT persisted. That table is an
    // explicit key list wired arm by arm — there is no reflective walk over this
    // struct — so a member with no key row is simply never written and never
    // read, which is exactly the ruling's "per tab, per session, not stored on
    // disk". Do not "fix" the omission by adding a row.
    // A LOAD IN PLACE DROPS IT with the rest of the band, no separate reset
    // needed: both `'` paths replace the whole ViewState through
    // view_state_from_settings_tab, which builds a fresh one, and a source load
    // seeds both tabs from a fresh ViewState too. A load in place is a
    // discontinuity (architect) — the stamped level described another piece.
    std::optional<double> zoom_recall_level;

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
    //     funnel for PageUp/PageDown, the PLAIN WHEEL's stepped pan, touchpad
    //     scroll, the PLAIN-DRAG grab-pan (both plain since 2026-08-12, the
    //     eighth glass ruling — pan-primary; the alt forms are deleted), and —
    //     since the overview lane's rework later that day — the lane's
    //     CLICK-TELEPORT (run_overview_teleport, the centering an outside-the-
    //     box press runs at the press since 2026-08-17: a
    //     pure viewport move of the pan class) and its BOX-FOLLOWS-POINTER PAN
    //     (apply_overview_drag_at's Pan arm, per event); plus
    //     Viewport::apply_strip_drag_zoom, which bypasses that
    //     funnel and suppresses on EITHER axis its callers write — its own
    //     viewport write AND its level write, the drag's zoom being SONG-ANCHORED
    //     and so carrying the view off the scanner the same way a pan does (a
    //     level change can leave the viewport start bit-identical, which is why
    //     the site tests both); the overview lane's EDGE drags ride that same
    //     site, their per-event zoom applying through it. Before this the flag's
    //     "manual-pan suppression" named a producer class that did not exist and
    //     panning away during playback was impossible with follow on (the
    //     default). A pure keyboard ZOOM is deliberately NOT a producer: it
    //     centers on the scanner during playback, so it never leaves the chase.
    //   * the PLACEMENT (place_playhead_at_click_column, input_pointer.cpp
    //     — the one body that writes this flag), which moves the cursor and
    //     reseeks. THREE ROUTES REACH IT (re-derived 2026-08-12, the membership
    //     matching reseek_keeping_alive's own at playback_lifecycle.cpp): the
    //     DEFERRED CLICK ACT at a plain navigation-surface press's motionless
    //     release (run_nav_click_act — the upper half, the ruler and the empty
    //     marker lane, live and `h`-view arms alike, though the view's cannot
    //     actually produce, playback being unreachable inside it since
    //     2026-08-05; the act's LOWER-HALF scrub arm is deliberately NOT a
    //     producer — it returns above the placement and overrides no follow,
    //     2026-08-13), the LIVE shift former's press, and the view's own shift
    //     former.
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
    // scrub act launches it independently of the cursor, from a clicked
    // frame). The cursor is
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

    // GUI rendering scale in PERCENT (the gui_scale setting; [50, 200], default
    // 100). 100 is the design baseline — 1920x1080, the one supported
    // resolution — 200 is the 4K case and 50 the half-size floor. A display
    // preference: not engine input, not authoring state, persisted on Ctrl+S,
    // applied at file load, and set through the settings editor
    // (`:gui_scale=`, no hotkey). LIVE since
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

    // ADD TO SELECTION — THE STICKY CTRL (architect 2026-08-18). While this
    // bit is set, a PLAIN FLAG CLICK takes the ctrl branch of the marker click
    // act: toggle the clicked marker's membership, land the playhead on the
    // focus the toggle leaves behind, and keep the rest of the selection. IT
    // IS NOT A NEW ACT — ctrl+click has always done exactly that, and the mode
    // is nothing but a second producer for the same branch (the fold is one
    // term at run_marker_click_act, input_pointer.cpp).
    //
    // WHY IT EXISTS, in the architect's words: "none of the modifier-click
    // vocabulary exists on touch, so buttons are how glass gets selection
    // work." A finger cannot hold ctrl, so accumulating a selection had no
    // spelling at all on the rig.
    //
    // SET BY ONE ROUTE — bare `k` / the bottom row's Add to Selection button,
    // which toggles it both ways (handle_mode_keys, input_key_dispatch.cpp).
    // The button's lamp reads THIS bit, so the face and the mode cannot drift.
    // SESSION SCRATCH, NEVER SERIALIZED, like shift_range_anchor beside it and
    // history_cumulative: no sidecar key, no undo entry, no snapshot field.
    //
    // SCOPE: FLAG HITS ONLY. run_marker_click_act is the one act owner and it
    // runs on a resolved marker hit and nothing else, so the empty marker
    // lane — its plain click, its double-click create — never sees this bit at
    // all. The `h` view's diff flags have their own mode-local multi-selection
    // and are a different press router entirely; the button greys in there and
    // bare `k` is consumed.
    //
    // SHIFT BEATS THE MODE, deliberately (the architect: "Shift+click needs no
    // rule here, because it has its own gesture"). The act's fork is
    // `if (ctrl) ... else if (shift)`, so a bare fold onto `ctrl` would make a
    // lit mode swallow a held shift; the fold carries `&& !shift` for exactly
    // that reason and a real shift+click still ranges while the mode stands.
    //
    // IT AUTO-CLEARS ON THE BOUNDARY THAT ENDS A PLAIN CTRL+CLICK'S EFFECT —
    // that effect being the accumulated membership, which any selection
    // REPLACE or CLEAR ends. Implemented as the SHIFT-RANGE ANCHOR'S OWN RULE
    // with the keeper swapped, which is what makes the two mirror images:
    // Selection::toggle_selection_membership — the mode's own act — KEEPS the
    // mode and clears the anchor, while Selection::select_range_from_anchor
    // keeps the anchor and clears the mode; every OTHER Selection mutator
    // clears BOTH. THE AUTHORITATIVE CLEAR LIST, re-derived by grepping every
    // Selection body rather than copied from the anchor's: set_single_selection,
    // clear_selection, collapse_to_focused, select_range_from_anchor and
    // sanitize_selection_after_restore (cycle_selection and the two marker
    // walks clear through set_single_selection; load_source_file's explicit
    // clear is belt over the clear_selection it already runs).
    // repair_last_selected is NOT one of them and must not become one: it is a
    // FOCUS repair reached only from inside the toggle, so clearing there
    // would make the mode die on the very act that defines it.
    // A marker REORDER does not clear it either — a bool has no index to go
    // stale, and remap_marker_indices_after_reorder carries the anchor through
    // rather than dissolving it for the same reason.
    // Riding the Selection chokepoint costs no inventory of its own and cannot
    // drift: a Selection mutator added later inherits the CLEAR by default,
    // which is the safe direction.
    bool          add_to_selection = false;

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

    // THE SWEEP's drag state (shift+drag on the desk, the region hold on glass).
    // Cleared on button release / lost button, by the force-end finalizer, and
    // on file load.
    RegionDragState region_drag;

    // The marker flag's PENDING PRESS, armed by the plain press AFTER its
    // click has acted (2026-08-17; the contract is at the type's declaration):
    // it holds only the reposition drag the press may become and the seed its
    // motionless release owes. Cleared on the threshold crossing, on button
    // release / lost button, by the force-end finalizer, and on file load.
    PendingMarkerPress pending_marker_press;

    // Pending trim endcap/bridge drag, armed by a plain trim-bar press OR by a
    // plain press on the waveform overlay's bounds and interior (the trim-drag
    // machinery begins only past the threshold; the second surface joined
    // 2026-08-18, when the region became the trim). Cleared on the threshold
    // crossing, on button release / lost button, by the force-end finalizer, and
    // on file load.
    PendingTrimDrag pending_trim_drag;

    // THE ONE SURVIVING DEFERRED CLICK (2026-08-17; the contract is at the
    // type's declaration): the trim bar's two bound sets, whose press IS the
    // endcap drag's arm — it becomes that drag past the
    // threshold. Cleared on the threshold
    // crossing, on button release / lost button, by the force-end finalizer, and
    // on file load.
    PendingClickAct pending_click;

    // THE TRIM REGION OVERLAY'S VISIBILITY — the whole of the region state
    // since 2026-08-18, the span itself being DERIVED from the trim every frame
    // (the model is at RegionState). IT HIDES WHEN THE PLAYHEAD'S POSITION IN
    // THE MUSIC CHANGES, WHEN A MARKER IS TOUCHED AND WHEN THE SWEEP ENDS, and
    // at no other time — the rule, its two movement owners, its other call
    // sites and everything it
    // deliberately leaves standing are stated once at clear_region_highlight
    // (input_handler.h). BARE `x` IS THE ONE MANUAL ROAD onto and off it, its
    // durable show and its durable hide (bare Esc hid it too until 2026-08-21,
    // when the second road was retired). The hides that stay IN PLACE rather
    // than going through the helper are `x`'s own and the FILE LOAD's, which
    // pairs it with a whole new piece.
    RegionState region;

    // Live trim boundary drag (endcap / inter-endcap bridge). Cleared on button
    // release / lost button, by the force-end finalizer (both COMMIT its live
    // bounds), and on file load.
    TrimDragState trim_drag;

    // Double-click candidate, shared by the trim-bar, flag, empty-lane and
    // editor-text surfaces (the surface tag prevents cross-firing). Seeded by a
    // motionless press-release on all four since 2026-08-15 (Marker seeded at
    // the PRESS until the marker click moved to the lift; the per-surface rule
    // is at DoubleClickSurface); cleared on file load and when
    // the double-click action fires.
    DoubleClickCandidate double_click;

    // The trim-bar framing double-click's press record (see TrimBarPressSeed).
    // Written by every plain trim-bar press, consumed by the next left release.
    TrimBarPressSeed trim_bar_press;

    // The navigation surface's plain press: the pending click / grab-pan
    // (contract at ScrollDragState). Cleared on button
    // release / lost button, by the force-end finalizer, and file load.
    ScrollDragState scroll_drag;

    // The overview lane's plain drag — the box pan
    // and the box-endcap edge
    // drags (contract at OverviewDragState; an outside press arms the pan here
    // too, after its teleport). Cleared on button release / lost
    // button, by the force-end finalizer, and on file load.
    OverviewDragState overview_drag;

    // The touch two-finger pinch's HELD PIVOT (contract at TouchNavZoomState).
    // Not a pointer gesture and so deliberately not in the pointer-gesture
    // clear lists: it is seated and cleared by the touch nav body itself and by
    // end_touch_nav, which the platform fires on every end — a finger lift,
    // wl_touch.cancel and touch-capability loss alike.
    TouchNavZoomState touch_nav_zoom;

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
    // writes instead is the armed chrome press (AppState::ChromePress), whose
    // Roster arm is the click face.
    //
    // `enabled` is the ENABLED VECTOR THE PAINTER LAST PAINTED, stashed beside
    // the rect for one reason: the facts it derives from (the undo/redo stacks,
    // the active tab's read-only flag, loading/blank) all change through routes
    // that damage nothing in the top strip, so the strip would keep showing a
    // stale face. main.cpp's per-tick comparator (beside the waveform
    // fingerprint dirty-detect) compares the LIVE vector against this stash and
    // pays one invalidate_top_strip on drift — one comparator site, no
    // per-mutation invalidate anywhere. It starts TRUE, so a cold roster settles
    // in one compare/paint pass. LAST PAINTED IS LITERAL (2026-08-15): the one
    // publisher refreshes these two bits only when the paint's clip actually
    // covers the button's pixels, so a row pass run under a narrow lane damage
    // cannot stamp the stash live over stale pixels and blind the comparator —
    // the mechanism record is at publish_button_face (paint_handler.cpp).
    // `selected` is the second stashed bit and rides the SAME comparator for the
    // same reason: `f` and `i` flip their flags with no top-strip damage at all,
    // so a toggled face would otherwise stay wrong until something else
    // repainted. (`t` and `p` do damage — they take the full sync rebuild — but
    // they go through the one comparator anyway rather than being trusted.)
    // `glyph_swapped` is the THIRD (2026-08-15) and rides it for the third time
    // over: a button wearing a SECOND GLYPH changes its pixels without moving
    // either of the other two bits, so the comparator was blind to it. Four
    // buttons have one — Save, Render, the read-only toggle and the collapsed
    // PLAY/STOP button, whose audition bit is written by six routes, several
    // damaging nothing wider than the clock cell. The predicate and the full
    // argument are at redesign_button_glyph_swapped.
    struct RedesignButtonFace {
        GuiRect rect{0, 0, 0, 0};
        bool    hovered       = false;
        bool    enabled       = true;
        bool    selected      = false;
        bool    glyph_swapped = false;
    };
    std::array<RedesignButtonFace, kRedesignButtonCount> redesign_buttons{};

    // (THE ACTIVE TAB'S LOCK RECT IS DELETED — architect 2026-08-14, "we
    // should move the icon out of the tab and into the icon row, then show the
    // current tab's readonly value". The tabs carried a permanent close-icon
    // slot from 2026-08-01 with a padlock in it, the ACTIVE tab's rect
    // published here for a press that dispatched bare `o`; the padlock is a
    // roster button in the icon row's last group now
    // (RedesignButton::IconReadOnly), so the tabs reserve no slot, draw no
    // lock and publish nothing, and ChromePress::Kind::TabLock went with the
    // rect.)

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
    // per diff flag, that lane's classes all stemming. Since the stems-inert
    // ruling (architect 2026-08-12) `marker_stems` is PAINT-ONLY: its two
    // readers are the per-frame stem painter (GuiPaintHandler::
    // paint_marker_stems) and the playhead's white-stem suppression decider
    // (GuiPaintHandler::playhead_stem_suppressed — a paint decision, not a
    // surface), the pointer never reads it (hit_test_marker_stem is deleted —
    // the record is at its retired site far below), and only `flag_hit_rects`
    // still answers clicks.
    //
    // THE INDEX DOMAIN FOLLOWS THE PAINTER: `marker_index` is a store index on
    // the live columns and an index into history_mode.flags in the mode. The
    // mode EDGES are what that costs — drop_lane_stash_across_history_edge
    // (input_key_dispatch.cpp) carries the argument and empties both.
    //
    // Cold (before the first rebuild) both are empty, so no flag is clickable
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

    // THE OPEN DIALOG EDITOR'S TEXT GEOMETRY — the painter-publishes-shaped-
    // geometry contract for the settings / load / commit-title / BPM editors,
    // which paint inside the MODAL's inset field since 2026-08-12 (they wrote
    // straight onto the status lane, row 9, from row 7 until then; the press
    // region is the published FIELD rect, modal_dialog.field, and it stays the
    // field alone now that the modal paints on the bottom row — the rest of
    // the lane is the row, not the editor).
    //
    // `text_origin_x` is the window x of PENDING's byte 0 — the field's own
    // left pad AND ITS HORIZONTAL SCROLL OFFSET are already spent in it (the
    // field travels to keep the caret visible since 2026-08-13; the rule is at
    // text_editor::State::view_offset_px) — and `byte_x` holds
    // pending.size()+1 pen offsets RELATIVE to that origin, so the pair reads
    // exactly like FlagEditorBox's and editor_byte_index_at searches either
    // the same way. That is what makes the click, the F2.1 text drag and the
    // double-click word select land on the right byte however far the field
    // has scrolled: byte 0 is where byte 0 PAINTS, never where the pad is.
    // Written by paint_modal_dialog: zeroed at the top of every run, filled by
    // whichever editor the dialog actually paints. That makes it a statement
    // about what is ON SCREEN — an editor the dialog's precedence hides (a
    // prompt is up) publishes nothing and takes no clicks, which is the
    // correct answer.
    struct DialogEditorText {
        bool                valid         = false;
        double              text_origin_x = 0.0;
        std::vector<double> byte_x;
    };
    DialogEditorText dialog_editor_text;

    // THE MODAL SURFACE'S PAINTED GEOMETRY. The prompts and the five dialog
    // editors paint ON THE BOTTOM ROW since 2026-08-13 (architect, scrapping
    // the centered box of 2026-08-12: "it looks sloppy — no compositor drop
    // shadow, and faking one wouldn't work"): while one stands the row's
    // tenants stand down whole and the modal has the lane. This is the
    // painter's stash of what was drawn, the roster model:
    // paint_modal_dialog rewrites it every run (zero/invalid when no dialog
    // stands), and the pointer path reads it instead of re-deriving layout.
    // `box` is the modal's whole SURFACE — the bottom row's lane, which is
    // what the hover invalidation and the damage ride; `field` is the editor
    // field's INTERIOR (zero for prompts) — the click-to-caret / text-drag
    // claim region; `buttons` are the answer buttons in painted order.
    //
    // A BUTTON CARRIES ITS DISPATCH, not a label: for a PROMPT,
    // `response_key` is the PromptState response char the click activates
    // (validated against the LIVE response_keys at dispatch); for an EDITOR,
    // `editor_ok` selects the session's own Enter (true) or Esc (false),
    // dispatched through the editor's one key route — button-is-its-chord.
    // `tooltip` is the composed hint the painter drew this button's word for
    // (modal_dialog_button_hint, above — the word plus the key that dispatch
    // names), stashed rather than re-composed because the WORD is the only
    // half the pointer path cannot see: the prompt's labels are its own and
    // the editors' two are literals. Every dialog button has one, so
    // membership needs no test beyond the index.
    //
    // PUBLISHED GEOMETRY MAY ONLY SELECT; LIVE STATE DECIDES (the doctrine,
    // recorded here once, 2026-08-13). The icon roster always obeyed it —
    // button-is-its-chord plus the live gates the chord meets — and these
    // buttons were the one outlier, their rects carrying baked command
    // payloads. Hit geometry stays the PAINTED stash (the displayed basis:
    // screen and hit agree always, the marker stems' delay-and-sync model of
    // 2026-07-24, whose recorded cold-start no-hit price is exactly the shape
    // the painted gate takes at PromptState), and the identity pair below —
    // `owner` and `session` — is that model's RE-VALIDATE-AT-DISPATCH half:
    // the painter stamps the stash with WHOSE geometry it is, and every site
    // that acts on it refuses a stash that does not name the surface currently
    // owning input. THERE IS NO INPUT LAG IN THIS, because nothing defers —
    // the mismatch span is one dispatch batch, reachable only by a queued
    // burst, which is exactly what must be refused.
    //
    // THE PAINTER IS THE ONE WRITER of both identity fields (`owner` Prompt on
    // the prompt branch, Editor on the editor branch, None in the no-dialog
    // reset arm beside the rest of the stash; `session` the id of whichever
    // surface it drew), and THE COMPARISON HAS ONE OWNER —
    // GuiInputHandler::modal_dialog_stash_current, read by the two press
    // claims, by the focus ring's route, and by the shared act
    // (dispatch_modal_dialog_button), each refusing a stash that does not name
    // the surface owning input as a consumed no-op. THE FIELD CLAIM NEEDS NO
    // TERM and deliberately has none: a prompt publishes a zero field and
    // leaves DialogEditorText.valid false, so the claim's own two tests
    // already answer the cross-class case — and the one case they do not, a
    // stale EDITOR field rect in the batch before the editor that replaced it
    // paints, DECIDES NOTHING: it places a caret in the live buffer, one
    // visible byte position out at worst, with no act behind it. The identity
    // gate is for the claims that ACT.
    //
    // THE TAG IS THE CLASS AND THE SESSION IS THE IDENTITY (the session joined
    // 2026-08-14, closing the round-15 finding that the tag alone cannot tell
    // one EDITOR session from the next). What is and is not reachable, stated
    // exactly: TWO DIALOG EDITORS ARE NEVER LIVE TOGETHER — every opener
    // refuses while another editor owns the keyboard — but a CLOSE AND AN OPEN
    // FIT IN ONE DISPATCH BATCH (Esc closes the commit-title editor, `'` opens
    // the load editor, no paint in between), and across that edge every face
    // index, both arms and the whole published geometry still carried an
    // `Editor` tag that matched the NEW session. A held Enter released into
    // that state dispatched the old editor's OK at an unseen dialog. THE
    // SESSION ID IS WHAT MAKES THE DIFFERENCE SAFE: one raise, one id, so the
    // stash a dead session published can never be read as the live one's, and
    // the painter's face-state reset — keyed on the session now — makes
    // editor-to-editor a change like any other.
    // THIS IS NOT THE PER-WINDOW GENERATION MACHINERY the real-window arc was
    // scrapped for: that was about RECONCILING TWO SURFACES, two toplevels
    // whose geometry could disagree for a whole frame. This is one integer in
    // one stash on one surface, compared where the stash is already read.
    enum class ModalDialogOwner { None, Prompt, Editor };
    struct ModalDialogButton {
        GuiRect     rect{0, 0, 0, 0};
        char        response_key = 0;   // prompt dialogs; 0 on editor dialogs
        bool        editor_ok    = false;  // editor dialogs; OK vs Cancel
        std::string tooltip;            // "<word> (<key>)"; never empty
    };
    struct ModalDialogGeometry {
        bool                           valid = false;
        ModalDialogOwner               owner = ModalDialogOwner::None;
        // The drawn surface's session id, 0 when nothing is published.
        uint64_t                       session = 0;
        GuiRect                        box{0, 0, 0, 0};
        GuiRect                        field{0, 0, 0, 0};
        std::vector<ModalDialogButton> buttons;
    };
    ModalDialogGeometry modal_dialog;

    // THE ONE ACTIVE DIALOG EDITOR'S SESSION ID, or 0 when none stands — and
    // THE AUTHORITATIVE MEMBERSHIP of the FIVE DIALOG-HOSTED editors (the
    // settings editor, the load editor, the commit-title editor, the measure
    // paste-offset editor since 2026-08-20, and the bpm bracket editor; the
    // top-strip flag editor is deliberately not one of them in EITHER of its
    // non-bracket kinds — FlagPayload or MeasureText, both of which paint in
    // the top strip). The predicate
    // GuiInputHandler::modal_dialog_editor_active is this id being non-zero,
    // and ITS declaration is the authoritative statement of what that
    // predicate is FOR and who calls it; this is where the five are NAMED, so
    // the set cannot drift between the two. At most one can be active at a
    // time (every opener refuses while another owns the keyboard), so the
    // order below is free.
    uint64_t dialog_editor_session() const {
        if (text_editor::is_active(settings_editor))
            return settings_editor.session;
        if (text_editor::is_active(load_editor))
            return load_editor.session;
        if (text_editor::is_active(commit_title_editor))
            return commit_title_editor.session;
        if (text_editor::is_active(measure_offset_editor))
            return measure_offset_editor.session;
        if (text_editor::is_active(top_flag_editor) &&
            top_flag_editor.kind == text_editor::Kind::BpmBracket)
            return top_flag_editor.session;
        return 0;
    }
    // THE SESSION THAT OWNS INPUT RIGHT NOW, 0 when no modal stands. The
    // prompt outranks every editor, which is the painter's own precedence (a
    // WM close can raise the unsaved-work prompt over a standing editor), so
    // this and paint_modal_dialog cannot disagree about whose geometry the
    // stash holds.
    uint64_t modal_dialog_live_session() const {
        return prompt.active ? prompt.session : dialog_editor_session();
    }

    // The hovered dialog button's index into modal_dialog.buttons, -1 none —
    // pointer-derived face state in the roster's own model (the hover walk
    // writes it, the painter reads it, a change damages the box). Cleared by
    // paint_modal_dialog's no-dialog arm alongside the stash, so a fresh
    // dialog cannot inherit the previous one's lit face.
    int modal_dialog_hovered = -1;

    // THE POINTER IS OVER THE EDITOR FIELD — the same pointer fact as the
    // index above, in the same model and written by the same walk
    // (update_modal_dialog_hover: one motion, one answer, one damage of the
    // stashed box), because the field grew a HOVER FACE when it took the
    // buttons' chrome (architect 2026-08-13: "the same outline — the breeze
    // blue highlight — when it's hovered and when it has the focus"). It is a
    // bool rather than a second index because there is exactly one field.
    // Resolved against modal_dialog.field, which a prompt publishes zero, so
    // this is false under a prompt by construction; reset with the three face
    // indices in paint_modal_dialog's no-dialog and owner-change arms.
    bool modal_dialog_field_hovered = false;

    // THE ARMED DIALOG BUTTON — the CLICK FACE and, unlike the roster's, THE
    // ACT'S OWN RECORD: these buttons act AT THE RELEASE (architect 2026-08-13,
    // "everything else acts on lift"), so this index is what a release
    // resolves against, not merely what is painted dark. A press on a dialog
    // button writes it; the release with the pointer STILL ON that same button
    // runs the act; anything else drops it and nothing dispatches.
    //
    // IT IS DELIBERATELY NOT THE ROSTER'S ARM (AppState::ChromePress): a
    // dialog button is not a roster member, so the two index spaces would have
    // to be sentinel-encoded into one field, and the two STATES overlap in
    // time. Neither surface can arm under the other — the veil swallows every
    // press outside the dialog's own field and buttons, with no exception
    // since 2026-08-13 (on_button_press, input_pointer.cpp), so no roster
    // press lands while a dialog stands — but a dialog raised MID-HOLD by a
    // key chord stands over a roster arm taken before it, which is why the
    // chrome lift re-asks the veil (finish_chrome_press_release). Since
    // 2026-08-13 the roster's
    // arm carries the SAME act-at-release lifetime as this one — this arm was
    // the model the chrome conversion copied — so the split is index-space
    // bookkeeping now, not a difference in rule; each keeps its own owner
    // (clear_redesign_button_press / clear_modal_dialog_press) on the shared
    // hard-end hook.
    //
    // THE ARM SURVIVES THE WHOLE HOLD AND TRACKS THE POINTER — THE FEINT
    // (architect 2026-08-13, at his live test, SUPERSEDING the "sliding off
    // cancels, and sliding back on does NOT re-arm: there is no new press"
    // note this block carried since the act moved to the release): "if the
    // user feints — clicks a button and then drags away before the mouse goes
    // up — then that button receives the passive focus as well. And while the
    // button is being held, but away from the button's hit area, the button
    // looks like a hover — a passive focus with the hover. And then when they
    // release it, it goes into being a passive focus."
    // So the arm is one index for the life of the hold and
    // `modal_dialog_press_inside` below is the pointer's answer about it:
    //   pointer ON the armed button   -> the PRESSED face; the release COMMITS
    //   pointer OFF it                -> the PASSIVE-plus-hover face; the
    //                                    release commits NOTHING and leaves
    //                                    the button PASSIVELY FOCUSED
    // Sliding back on restores the pressed face and a release there DOES
    // commit — the arm never died, so there is nothing to re-arm. The reason
    // is in the ruling itself: a button that keeps a lit face while held away
    // is still engaged, and a dead arm could not light anything.
    //
    // Its edges: the press claims write it (on_button_press's two dialog
    // gates, input_pointer.cpp), the release claims read and clear it, the
    // hover walk keeps it and rewrites `press_inside` instead
    // (update_modal_dialog_hover, which is also where the feint's passive
    // focus is assigned, on the leave edge), clear_modal_dialog_press drops it
    // on the pointer-leave / capability-loss edge (main.cpp's hook, beside the
    // roster's own clear), and paint_modal_dialog drops it with the stash
    // whenever the dialog closes or CHANGES. Every write damages the box.
    int modal_dialog_pressed = -1;

    // IS THE POINTER INSIDE THE ARMED BUTTON — the feint's other half, written
    // by the press claim (true: a press is inside what it hit) and by the
    // hover walk on every motion under a standing arm. Meaningless while
    // `modal_dialog_pressed` is -1 and reset with it, so the pair is read as
    // one fact: the PRESSED face and the committing release are
    // `pressed >= 0 && press_inside`, and the held-away face is
    // `pressed >= 0 && !press_inside`.
    bool modal_dialog_press_inside = false;

    // THE KEYBOARD FOCUS RING (architect 2026-08-13, part D of the modal
    // button ruling — kdenlive-sampled face, the navigation his own
    // derivation): the index into modal_dialog.buttons the keyboard is on, or
    // -1, WHOSE MEANING THE OWNER SELECTS — on an EDITOR dialog -1 is THE
    // FIELD, a real ring stop and where every editor opens (the user is there
    // to type, which is also what keeps the editors' keyboard contract
    // byte-identical); on a PROMPT -1 is nothing at all, because a prompt is
    // RAISED with its last button focused and its ring is its buttons alone,
    // wrapping. (The "a prompt opens with NO button focused" rule and the
    // no-focus stop in its cycle are RETIRED with the Enter answer's arrival
    // the same day; the supersession and what makes it safe are at
    // PromptState. -1 on a prompt is therefore a cold value only — the state
    // between a raise and the paint that assigns the focus, which the painted
    // gate makes unreachable by input.)
    //
    // THE RING: Tab cycles every stop including the field and SHIFT+TAB WALKS
    // IT BACKWARDS (architect 2026-08-13, in the live marker cycle's own
    // spellings — Shift+Tab and IsoLeftTab, the latter shift-agnostic), Left/
    // Right move
    // between BUTTONS only and are inert in the field (the arrows belong to
    // the text there, and the editors' own motion arm owns them), and bare
    // Enter / bare Space PRESS the focused button down for the act at their
    // release (modal_dialog_key_pressed, below). The one route is
    // route_modal_dialog_focus_key (input_key_dispatch.cpp), read by the
    // prompt gate and by route_modal_editor_key alike. ONE THING RANKS ABOVE
    // the ring's FORWARD Tab: an editor whose FIELD has an autocomplete offers
    // that completion first and reaches the ring only when it did not advance
    // the buffer — the one autocomplete model, stated at
    // route_modal_editor_key's Tab arm, which is also what makes -1 a stop Tab
    // can always leave. The reverse walk is offered to no completion, shift
    // meaning "go back" and never "complete".
    //
    // IT RESETS STRUCTURALLY, in paint_modal_dialog and nowhere else: with the
    // stash when no dialog stands, and ON ANY CHANGE OF THE STASH'S SESSION —
    // ONE test since 2026-08-14, covering every edge the three tests before it
    // named separately (prompt over editor, editor after prompt, a prompt
    // REPLACING a prompt at the save-failed rung) plus the one they missed,
    // EDITOR AFTER EDITOR: two dialog editors are never live together, but a
    // close and an open fit in one dispatch batch, and an owner tag cannot see
    // that (the full statement is at ModalDialogOwner). THE PROMPT'S RAISE
    // FOCUS IS ASSIGNED ON THAT SAME RESET, in the painter's prompt branch
    // once the buttons exist: reset then assign, one edge, so there is no
    // frame in which a standing prompt has no focus.
    //
    // AND THE INDEX IS ONLY MEANINGFUL WHILE THE STASH IS CURRENT, because it
    // names a slot in the painter's published button list. Between a raise and
    // its first paint it names the PREVIOUS surface's buttons, which is one
    // dispatch batch wide and reachable by a queued burst — so the keyboard
    // reads it through modal_dialog_focus_live (input_handler.h), never raw.
    // The raw field is the PAINTER'S and the TICK'S to read: the painter has
    // just reset it, and the blink tick is a frame-cadence cosmetic.
    int modal_dialog_focus = -1;

    // THE FOCUS'S STRENGTH (architect 2026-08-13, from kdenlive): PASSIVE
    // (false) or ACTIVE (true). Meaningless while modal_dialog_focus is -1 and
    // reset with it; the two are read as one fact, exactly as the press arm
    // and its inside bit are.
    //
    // BOTH STRENGTHS ACT — Enter and Space press the focused button whichever
    // it wears. What separates them is HOW THE FOCUS WAS ACQUIRED and what
    // the button then looks like:
    //   PASSIVE is ASSIGNED, never walked onto. Two producers, and they are
    //   the whole list: a PROMPT'S RAISE (the painter, onto the last button —
    //   the Escape sentinel; PromptState owns why that is the safe one) and a
    //   FEINT (a press that armed a button, then dragged off it —
    //   update_modal_dialog_hover's leave edge; the rule is at
    //   modal_dialog_pressed). A feint's assignment REPLACES whatever focus
    //   the dialog had, of either strength.
    //   ACTIVE is reached only by a DELIBERATE KEYBOARD WALK — Tab, its two
    //   reverse spellings, Left, Right — so its one producer is
    //   route_modal_dialog_focus_key's walk, which sets it on every landing.
    //   There is NO route back down to passive except a new passive
    //   assignment.
    // An EDITOR dialog opens with focus in the FIELD and no button focused at
    // all, so it opens with neither strength — unchanged.
    bool modal_dialog_focus_active = false;

    // THE KEYBOARD'S OWN PRESS ARM — the button bare Enter or bare Space is
    // holding down, or -1 (architect 2026-08-13, from kdenlive: "pressing
    // Enter when a button has focus pushes down the button. It doesn't
    // automatically commit the action... we move the playhead when the user
    // lifts up the mouse key, so we should do that here as well"). It is the
    // POINTER ARM'S TWIN and deliberately a separate index: the two can stand
    // together (a feint held with the mouse while the keyboard presses the
    // focused button), they die on different edges, and one field would have
    // to encode both.
    //
    // THE ACT IS AT THE PHYSICAL RELEASE, which is the only reason this state
    // exists: the press paints the button down and dispatches nothing, and the
    // release of THAT SAME KEY runs the act through the one shared dispatch
    // (dispatch_modal_dialog_button, input_pointer.cpp, which re-asks the
    // painted gate and the stash's identity exactly as the pointer's release
    // does).
    // `modal_dialog_key_pressed_key` is what the release matches on, so
    // releasing the OTHER of the two keys — Enter pressed, Space released —
    // resolves nothing.
    //
    // IT CANNOT FIRE TWICE UNDER A HOLD, by two independent facts: the key
    // never arms platform repeat while the focus is on a button
    // (repeat_eligible's own arm), and the press body refuses a delivery
    // carrying GuiInputState::synthesized_repeat outright — so even a repeat
    // armed before the focus moved cannot re-press or re-fire.
    //
    // MOVING THE FOCUS CANCELS THE ARM (2026-08-14, closing the round-15
    // finding that a Tab mid-hold left the arm on the button the user had
    // visibly left: focus OK, hold Space, Tab onto Cancel, release — and OK
    // committed). SO WHILE THIS IS >= 0 IT EQUALS modal_dialog_focus, an
    // invariant every reader may lean on: the pressed face and the focus ring
    // can never point at different buttons, and the release cannot commit a
    // button that is no longer focused. Its three cancel sites are the three
    // routes that move the focus at all — the ring's walk, the pointer FEINT's
    // passive assignment, and the editor act's return of the focus to the
    // field — each calling the one owner, clear_modal_dialog_key_press.
    // IT IS THE POINTER ARM'S RULE IN THE KEYBOARD'S OWN TERMS rather than a
    // copy of it, and the difference is the two inputs' own: the pointer can
    // paint a HELD-AWAY face and slide back onto the button, so its arm
    // survives the whole hold and the release away simply commits nothing (the
    // FEINT, at modal_dialog_pressed); the keyboard has nowhere to be but on
    // the focused button and cannot re-press a key that is already down, so
    // leaving cancels outright. Same verdict on both surfaces — nothing
    // commits a button the user has left — reached by each input's own shape.
    //
    // Its edges: the ring's press arms write it; on_key_release reads and
    // clears it; the three focus moves above cancel it; the painter drops it
    // with the rest of the face state on every edge that changes the dialog;
    // and the platform's keyboard-intent cancellation (keyboard leave,
    // keyboard-capability loss, a Super-swallowed press) drops it too: on the
    // first two because the release it is waiting for will never be delivered,
    // and on the Super-swallowed press because that press is an INTERVENING KEY
    // ARRIVAL — the release path is ungated on Super, so that member is
    // conservative rather than forced (main.cpp's hook body is the
    // authoritative list). Every write damages the box.
    int    modal_dialog_key_pressed     = -1;
    GuiKey modal_dialog_key_pressed_key = 0;

    // THE CLOCK'S RESERVED CELL, published by paint_bottom_strip (2026-08-11,
    // when the timestamp moved off the status line into the transport row's
    // centre in monospace; the row unification merged that row and the status
    // line into the one bottom row a day later, and the architect moved the
    // cell to the row's LEFT BLOCK, behind the transport's own separator, on
    // 2026-08-18). It is a PAINTER STASH in the
    // roster's own model — the rect
    // that was drawn, never re-measured elsewhere — because the cell's width is
    // a SHAPED specimen on the monospace face at the live size, which only the
    // painter is holding a scaled font for.
    //
    // ITS ONE CONSUMER IS clock_invalidate_rect, which hands it to every route
    // that moves the playhead or the scanner, so a clock advance dirties the
    // cell instead of the whole lane and the buttons' draws fall outside
    // on_redraw's clip. (The status cell's owner read its RIGHT EDGE as a
    // bound until 2026-08-13, when the status chain moved to the tab row and
    // that owner was deleted; nothing on this row is measured from the cell
    // any more.) Zero before the row's first paint, which the owner answers
    // with the whole lane — the first frame paints everything anyway — AND
    // ZERO WHILE THE ROW YIELDS TO A MODAL (2026-08-13): the clock is not
    // painted then, so the painter publishes no cell and the owner widens to
    // the lane, which is the modal's own surface.
    GuiRect clock_cell_rect{0, 0, 0, 0};

    // THE ARMED CHROME PRESS — the redesigned rows' ACT-AT-RELEASE record, and
    // the CLICK FACE with it (architect 2026-08-13, generalizing the modal
    // dialog buttons' model to the whole chrome roster: "Icon buttons are
    // triggered on mouse down, but they should be triggered on mouse up or
    // finger up for consistency"). A press on any chrome target ARMS this and
    // dispatches nothing; the release with the pointer ON that same target
    // runs the act; a release anywhere else — or a release under a prompt or
    // a non-admitting dialog veil — drops it and nothing dispatches. The
    // authoritative statement of the rule and its scope is
    // kdenlive-redesign.md's act-at-release section.
    //
    // `kind` names the TARGET CLASS, and ONE class arms since 2026-08-18:
    //   Roster — a chord-table button; `index` is the roster index.
    // The enum stays a Kind rather than collapsing into a bool because `None`
    // IS the unarmed state, which is what every reader tests.
    // (TWO OTHER KINDS ARE DELETED PRODUCER-LESS, each with the surface that
    // produced it. TabLock armed the active tab's padlock for bare `o` and went
    // with the padlock's move into the icon row, 2026-08-14 — the read-only
    // toggle is a roster button now and arms as Roster like every other chord
    // button. HistoryWalkTab armed the `h` view's walk-selector TABS, whose act
    // was set_history_reading rather than a chord, and went with that
    // repurposing on 2026-08-18: row 3 is the A/B pair in every state, so a
    // press on a tab arms as Roster and dispatches Ctrl+Tab like any other.)
    // (The THREE dropdown ANCHORS — File, Edit and Settings, a Navigation
    // anchor having left 2026-08-15 and Edit having arrived 2026-08-20 — are
    // deliberately NOT armed: their toggle is the recorded press-time
    // exception, and the reasoning is at their press claim in on_button_press.
    // The claim walks kDropdownMenus, so neither of those two changes touched
    // it or this note's membership.)
    //
    // `shift` is THE PRESS-TIME MODIFIER, carried with the arm because the
    // release deliberately does not re-read modifiers (the modal release's own
    // rule): the shift-admitting buttons must see the shift that was held when
    // the user PRESSED, and a shift tapped or dropped mid-hold changes
    // nothing.
    //
    // THE RULE'S SCOPE (architect-accepted 2026-08-14, stated here at the
    // rule's own site because the navigation drag now reads ctrl LIVE):
    // modifiers-at-the-press governs ARMED ACTS — the chrome roster's and the
    // modal buttons' press-to-commit spans — and exists so that an act the
    // user has armed cannot change under their finger between press and
    // commit. A CONTINUOUS VIEWPORT GESTURE arms no act: nothing commits,
    // nothing can surprise, and every part of it is undone by moving the
    // other way. So the one nav drag's live ctrl (pan while up, zoom while
    // held — ScrollDragState) is OUTSIDE this rule's scope, not an exception
    // to it: the chrome and modal rules stand exactly as written, and any
    // future PRESS-ARMED act must keep reading its modifiers at the press.
    // (The marker flag's pending click was that future act for two days,
    // 2026-08-15..17, carrying its own shift/ctrl; its click acts AT THE PRESS
    // again — PendingMarkerPress above — where the live modifiers ARE the
    // press-time modifiers, so it no longer carries any.)
    //
    // `press_ms` is THE PRESS'S OWN monotonic_ms() STAMP, and the whole of the
    // SHIFT LONG PRESS (architect 2026-08-13): held past kChromeShiftHoldMs on
    // a shift-admitting button, the lift dispatches the SHIFTED twin — the
    // waveform's region hold on the roster's surface, and the route by which
    // glass, having no keyboard, reaches the other half of each shifted pair.
    // It is stamped by EVERY arm (a press has a time whatever it landed on) and
    // read at ONE site, the lift's chord build, where it is ORed into the same
    // shift term the carried bit feeds — so a physical Shift+click and a long
    // press are two routes to one dispatch rather than two dispatches. The
    // elapsed span is measured at the RELEASE, so nothing polls and nothing
    // ticks, AND THE HOLD IS RULED TO NEED NO FEEDBACK (architect 2026-08-13):
    // it exists for the touch panel, where tooltips do not show at all, so the
    // one surface a hint could ride is invisible to the gesture's only user.
    // The rule is stated at the read site (finish_chrome_press_release).
    //
    // `inside` is THE FEINT'S BIT, the modal arm's `press_inside` on the
    // roster's surface: true from the press (a press is inside what it hit),
    // rewritten by recompute_redesign_button_hover on every motion and tick
    // under a standing arm. The pressed face paints only while it is true, so
    // sliding off un-presses the face, sliding back on restores it, and the
    // arm itself survives the whole hold. (The pre-2026-08-13 note here said
    // the face SURVIVES the pointer wandering off because the chord had
    // already fired at the press; the act moved to the release, so that
    // reasoning is dead and the face now tracks the pointer exactly as the
    // dialog buttons' does.) The RELEASE does not read this bit — it re-hits
    // the armed target's published rect at its own coordinates, the derive
    // doctrine; the bit serves the paint alone.
    //
    // Its edges: the band claims' arm route writes it (arm_redesign_press and
    // the tab row's two direct claims, input_pointer.cpp), the release takes
    // it whole at the top of on_button_release (take_chrome_press — armed or
    // not, so an early return cannot strand it), and
    // clear_redesign_button_press drops it on the pointer-leave /
    // capability-loss edge (main.cpp's hook): a pointer that has left the
    // window is on no button, and an act that has not happened yet must not
    // wait on a release that may never come. WHICH buttons paint the face is
    // the chord table's `click_face` column, not restated here — row 3's two
    // tabs set it false, keeping their own two faces.
    //
    // THE HOLD-REPEAT RIDES THIS ARM (architect 2026-08-16, reversing his own
    // 2026-08-13 deletion of the arrows' repeat: the touch panel has no
    // keyboard, so a held arrow BUTTON is the panel's only nudge run). A press
    // on a button whose chord row sets `repeats` — the bottom row's four
    // cardinal arrows and nothing else, kToolbarChords, input_pointer.cpp —
    // arms a synthesized-repeat burst alongside the act:
    //   * `repeat_due_ms` is the CLOCK_MONOTONIC stamp of the next fire, and 0
    //     means THIS ARM CARRIES NO BURST (an unarmed button or an ineligible
    //     context). The first fire is one kHoldBeatMs
    //     after the press — the product's ONE hold beat, so the button hold and
    //     the key hold cross their threshold together — and every later fire is
    //     THE COMPOSITOR'S OWN advertised key-repeat interval
    //     (GuiPlatform::key_repeat_period_ms, read per fire, 0 = the desktop
    //     has key repeat off and neither the keys nor these buttons repeat).
    //   * `repeat_fired` records that the burst has produced at least one act,
    //     and the LIFT reads it: a hold gives the stream and nothing extra, so
    //     a fired burst SUPPRESSES the lift's own act — the burst's fires
    //     already acted in its place (finish_chrome_press_release).
    // Both live ON THE ARM rather than beside it, and that is the whole of
    // their lifetime management: take_chrome_press carries them to the lift and
    // empties the field in one act, and every edge that drops the arm drops the
    // burst with it, with no second edge list to keep in step.
    //
    // THIS IS THE AUTHORITATIVE INVENTORY OF THE BURST'S ENDS (every site
    // carries its own member plus a pointer here):
    //   ARMING is judged under the PRESS-TIME context by the KEYBOARD'S OWN
    //   predicate SHARED, not mirrored — repeat_eligible, exactly what the
    //   platform's arming probe asks for a physical key — so the button's hold
    //   arms in precisely the contexts a held key would (arm_redesign_press).
    //   THE ARM'S OWN ENDS END THE BURST, all of them by construction: the
    //   release (take_chrome_press) and the pointer-leave / capability-loss
    //   clear (clear_redesign_button_press, main.cpp's hook).
    //   PER FIRE the tick re-asks four things and they answer differently on
    //   purpose: the pointer being ON the armed button's published rect PAUSES
    //   the schedule while it is off and resumes on return (the scrollbar-button
    //   rule); a dead `redesign_button_enabled` PAUSES too (the disabled-press
    //   consume's mirror); an advertised rate of 0 PAUSES (the desktop's key
    //   repeat is off, and repeat_info can re-arrive); and repeat_eligible
    //   going false DISARMS — a context that revoked the burst's eligibility
    //   ends it rather than parking it.
    //   ANY PHYSICAL KEY DELIVERY DISARMS — one edge, the platform's key
    //   arrival in main.cpp's set_on_key hook, upstream of on_key's synthetic
    //   callers (which include this burst's own tick fires — the reason the
    //   disarm cannot live at on_key's top). It is LOAD-BEARING FOR UNDO, not
    //   hand-feel: Undo::coalesce_gesture merges a synthesized repeat by KIND
    //   ALONE, with no subject test, on the premise that no command can run
    //   between a burst's opener and its repeats — and under press-time
    //   dispatch the PRESS edge spans that premise whole: a key RELEASE runs
    //   no command (the one release act, the modal Enter/Space commit, needs
    //   its own arming press, which that edge already caught — and a prompt
    //   standing disarms the burst anyway, through the tick's per-fire
    //   repeat_eligible re-ask). Modifier keys never reach the hook, so a
    //   shift or ctrl tapped mid-hold ends nothing.
    //   NO KEYBOARD-INTENT-CANCEL MEMBERSHIP, deliberately, where the
    //   pre-2026-08-13 form had one: this burst is POINTER intent and the
    //   hook's edges run no command (the reasoning is at
    //   set_keyboard_intent_cancel_hook, platform_wayland.h).
    //   NOTHING IS MIRRORED FOR THE POINTER-BUTTON AND WHEEL EDGES the old form
    //   also carried: the left button cannot press again while it is held, the
    //   other two buttons bind nothing anywhere in this product, and a wheel
    //   moves the viewport alone — none of the three can change the undo
    //   subject.
    // The firing body is tick_chrome_press_repeat (input_pointer.cpp).
    struct ChromePress {
        enum class Kind { None, Roster };
        Kind    kind          = Kind::None;
        int     index         = -1;
        bool    shift         = false;
        bool    inside        = true;
        int64_t press_ms      = 0;
        int64_t repeat_due_ms = 0;
        bool    repeat_fired  = false;
    };
    ChromePress chrome_press;

    // THE RENDER BUTTON IS CANCEL WHILE AN EXPLICIT RENDER ACT IS LIVE
    // (architect 2026-08-11: "change the render button into a cancel button
    // when there's something rendering... it doesn't need to exist while
    // nothing's rendering"; NARROWED the same day at his live look): the face
    // covers THE ACTS THAT WRITE FILES — the single Ctrl+Alt+R render, the
    // iteration sweep, the archival queue — and deliberately NOT the automatic
    // target-view preview updates ("Updating..."). HIS RATIONALE, recorded: a
    // preview needs no cancel surface because it overrides nothing and any
    // map-mutating interaction already cancels-and-redispatches it as the
    // user's own intent; Cancel exists to call off the explicit acts.
    // THE PREDICATE IS queue_running ALONE — re-derived from ownership, not
    // from the progress hint: queue_running's own writers are exactly the two
    // explicit dispatchers (dispatch_single_archival_render and
    // start_render_batch, cleared by finalize_render_run), and the preview
    // path (GuiTargetRender) never touches it, so the bit IS "an explicit
    // archival act is in flight" — the same scoping its declaration already
    // records the Esc handler using. This field mirrors it per tick
    // (tick_render_cancel_face, input_render_dispatch.cpp) for ONE remaining
    // reason: the TRANSITION DAMAGE — finalize_render_run flips queue_running
    // from an async completion with no top-strip damage of its own, and the
    // mirror's edge is what repaints the face (the drift-comparator pattern
    // for an input that vector cannot see).
    // FACE-MIRRORS-THE-ACT HONESTY, both halves at the click
    // (finish_chrome_press_release's Render arm — the lift, since the chrome
    // act moved to the release): a lift on a painted Cancel
    // never dispatches a render (the arm claims on THIS bit), and the ACT is
    // gated on the LIVE queue_running — so on the stale edge (the explicit
    // render finished, the click landed before the next tick) the lift is a
    // consumed no-op, and it can NEVER reach a preview session through
    // cancel_archival_session's wider is_busy branch: a preview that happens
    // to be the busy session when queue_running is false is exactly what the
    // face never advertised. Keyboard Esc's render-cancel binding is untouched
    // and keeps its own wider reach.
    // READERS, re-greped 2026-08-15: redesign_button_glyph_swapped (which is
    // where the GLYPH's condition lives now — redesign_button_icon reads that
    // predicate rather than this bit, so the swap and the face STASH the drift
    // comparator walks cannot disagree), the stateful tooltip overload, and
    // finish_chrome_press_release's Render
    // arm — the roster's ONE ruled exception to THE BUTTON IS ITS CHORD (the
    // divergence is recorded at that arm). The LABEL override left this
    // list with row 2's labeled faces at the 2026-08-12 relayout — no label is
    // painted for Render in any state, the words living on the tooltip alone —
    // and is itself deleted producer-less on 2026-08-18.
    bool render_cancel_face = false;

    // THE HOVER TOOLTIP'S TIMING STATE — the whole of it. `hover_ms` is the
    // CLOCK_MONOTONIC stamp of the moment a tooltip-bearing button became
    // hovered (0 = none is), written by the hover recompute; `visible` is what
    // the painter draws, flipped by the run loop's existing tick when the delay
    // comes due. No timer, no callback, no per-frame damage: the tick already
    // runs, it compares two numbers, and it damages ONCE on each edge.
    // `rect` is the painter's published tooltip box, needed only for damage
    // (nothing hit-tests a tooltip).
    // `owner` is the BUTTON the dwell belongs to. It is what makes "a fresh
    // dwell on each arrival" TRUE rather than merely intended: a single motion
    // can leave one tooltip-bearing button and enter the other in the same
    // recompute, and without the id the stamp would survive that change — the
    // second button would inherit however much of the first's dwell had
    // already elapsed, showing instantly if the first tooltip was already up.
    //
    // IT NAMES ONE OF TWO SURFACES since 2026-08-13, when the modal's buttons
    // took tooltips too (architect, retiring the bracketed accelerators: "we
    // just do a tooltip just like the regular icon tooltips"). THE ENCODING,
    // stated once here and nowhere restated: `surface` says WHICH index space
    // `index` lives in — Roster indexes the redesign roster
    // (redesign_buttons / RedesignButton), Dialog indexes
    // AppState::modal_dialog.buttons — and `index` < 0 means NO OWNER in
    // either. The pair is compared whole (the defaulted ==), which is what
    // keeps "a fresh dwell on each arrival" true ACROSS the two surfaces as
    // well as within one: index 0 of the roster and index 0 of a dialog are
    // different buttons and compare unequal. It is deliberately ONE field
    // rather than two parallel tooltip states — there is at most one dwell in
    // the product, one painter for it and one dwell clock, and a second state
    // would have to be kept mutually exclusive with the first by hand.
    //
    // WHO WRITES IT: the two hover walks, each for its own surface and each
    // through the one arming helper (GuiInputHandler::arm_tooltip_dwell) —
    // recompute_redesign_button_hover for the roster, update_modal_dialog_hover
    // for a standing dialog — plus hide_shift_tooltip, which clears it.
    struct RedesignTooltip {
        enum class Surface { Roster, Dialog };
        struct Owner {
            Surface surface = Surface::Roster;
            int     index   = -1;
            bool operator==(const Owner&) const = default;
        };
        int64_t hover_ms = 0;
        Owner   owner{};
        bool    visible  = false;
        GuiRect rect{0, 0, 0, 0};
    };
    RedesignTooltip redesign_tooltip;

    // THE MENU ROW'S DROPDOWN — ONE popup state for EVERY menu (Settings
    // since 2026-07-31, File since 2026-08-13, Edit since 2026-08-20;
    // Navigation was a third from
    // 2026-08-02 until its deletion 2026-08-15, and not one of those arrivals
    // or that departure needed a line in here — which is the shape's whole
    // point),
    // hanging under whichever button
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
    // release, by every close and by the same pointer-leave drop. The items
    // were the redesign's FIRST act-on-release surface (a menu triggers on
    // release by universal convention), the model the modal dialog buttons
    // took 2026-08-13 and the whole chrome roster took the same day.
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
    // 2026-08-03) the ANCHOR press that OPENS a menu — press any of the three,
    // hold, drag down into the menu that came up, and releasing on
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
    // from the row, the row answers the pointer alone — entering ANY anchor's
    // rect opens that anchor's menu with no click (on_motion's no-gesture tail,
    // open_menu_row_anchor_on_hover; a RESTING pointer only — a held primary
    // button refuses the open at the call site, codex round 2), which is what
    // every desktop's menu bar
    // does; the "pointer left the row" half is a separate entry with the
    // opposite guard list (update_menu_row_exit, at the top of on_motion). COLD,
    // an anchor answers a CLICK and nothing else, and that is the whole reason
    // the bit exists: a row that sprang a menu open at a pointer merely crossing
    // it, with no click ever given, would be a misfeature rather than this one.
    // ARMED BY toggle_dropdown's OPEN path, the single route that opens ANY
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
    // marker stands exactly where it stood. The bottom row's right-aligned
    // status corner (section C) carries the commit's position, its short SHA
    // and its `scale=` value.
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
    // refusal for free — a prompt, any of the seven editors, an open dropdown,
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
    // BARE ESC IS ADMITTED (architect 2026-08-04) AND ADDS NO ESC PLACE OF ITS
    // OWN. The bare-Esc inventory is the one enumerated at its dispatch point
    // (input_handler.cpp); the mode's allowlist merely stops dropping the key,
    // so the binding that can be live in here runs — the RENDER / BATCH CANCEL
    // (a render launched before `h`). The REGION HIDE was the other one until
    // 2026-08-21, when it retired.
    // It touches no authored state, which is why admitting it costs the frozen
    // now side nothing. With no render standing, Esc is a consumed nothing.
    //
    // WHAT IT REFUSES, and where: every state-mutating route is a consumed no-op
    // while it stands, through TWO gates and no scattered ifs — history_mode_-
    // key_blocked (the keyboard allowlist, which the redesigned buttons and the
    // File menu's one item pass through too, since both dispatch as chords
    // via on_key) and handle_history_mode_press (the pointer allowlist). Each
    // states its own admitted set at its definition. THE SETTINGS DROPDOWN is
    // shut out structurally instead, at toggle_dropdown: its six items all open
    // the settings editor, a modal this view has no place for, and refusing the
    // menu is one line where covering that one pointer bypass per item would be
    // several.
    //
    // THE FILE DROPDOWN OPENS IN HERE, so a popup and this mode DO stand
    // together and the old "never together" invariant is retired to the Settings
    // half. It costs the gates nothing: its one row is a CHORD dispatched
    // through on_key, so the two gates above answer per item exactly as they do
    // for a redesigned button — Ctrl+Q is on the allowlist. (The ruling is the
    // architect's 2026-08-08 one and it was made for the NAVIGATION menu, whose
    // seven rows the gates answered per item the same way: zoom in / out /
    // overview admitted by the allowlist, center-on-focus and the two marker
    // steps claimed by history_mode_owns_key as re-expressions over the diff
    // flags. ONE of its rows greyed instead of dispatching — "Walk both tabs",
    // whose Ctrl+Shift+Tab was then the mode's reverse walk-source cycle rather
    // than the A/B walk the label promised (the chord IS that march in the view
    // since 2026-08-18, over the diff flags) — and that was the menus' only per-item
    // disabled state ever. The menu is deleted whole as of 2026-08-15 and the
    // predicate with it; File inherited the ruling by construction when it
    // landed 2026-08-13, so what the ruling BUYS is unchanged.)
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
    //   LIT — the view bar's three (bare 1/2/3), the
    //   COMMIT-FACED SAVE (Ctrl+S, the act itself), the S/T + W/P radios (bare
    //   `t` / `p`), BOTH row-3 tabs (Ctrl+Tab, which the ALLOWLIST admits since
    //   2026-08-18 — the tabs switch tabs in here like everywhere else), the
    //   history button and the two WALK RADIOS
    //   (bare `h` and bare `g`, the mode's OWN vocabulary, which the derivation
    //   asks about
    //   first), the walk's two arrows (bare `,` / `.`, the same), the
    //   FILE anchor since 2026-08-13, whose one item is the Ctrl+Q this list
    //   used to name as the Quit BUTTON's (that button is retired; the chord is
    //   admitted exactly as it was). (The NAVIGATION anchor was lit here too
    //   from 2026-08-08 — its menu worked in here, and its one dead row greyed
    //   at the ITEM rather than through this partition, which knows only about
    //   buttons. It left the roster with its menu on 2026-08-15, and this list
    //   is a partition of the roster, so it simply has one fewer member.)
    //   TWO OF THE LIT ARE SESSION-CONDITIONAL IN THEIR FACE, each one decision
    //   serving the key and the face: Save greys with an empty head delta (or a
    //   checkpoint in flight), and the
    //   load-in-place opener greys on a walk with NO MEMBER — one term for both
    //   walks since 2026-08-09, the act loading the VIEWED member and a blank
    //   lane offering none. The Local walk cannot reach it on a live tab (U + R
    //   + 1 members, bound before the mode goes up), so the REMOTE walk is where
    //   it shows: a piece whose every checkpoint refuses the strict load opens
    //   the view at `0/0`, and that button is the one greyed there.
    //   REVERT WAS A THIRD UNTIL 2026-08-15 — Ctrl+H is still admitted only
    //   with a diff flag selected, so this partition still answers DEAD for it
    //   on an empty subject, but no face reads that answer any more: the
    //   architect reversed the grey because it tracked the diff-flag SELECTION
    //   and blinked at interaction cadence, the same argument that took the
    //   four cardinal arrows always-on, and redesign_button_enabled now lifts
    //   the four history companions over this partition entirely. The button is
    //   lit with an empty subject and the click is a consumed no-op, the
    //   roster's standing shape for a refusal; the record is at that arm.
    // The partition is
    // DERIVED
    // from the two gates above (plus the Settings anchor's toggle_dropdown
    // lockout, the one hand entry left) and
    // inventoried in one place — history_mode_disables_button, input_pointer.cpp
    // — and it is read live from `active` below, so leaving the mode restores
    // every face on the next frame with nothing latched.
    //
    // ROW 3'S TABS ARE ORDINARY TABS IN HERE (architect 2026-08-18: "ctrl+tab
    // should work as normal in history view — it becomes essentially another
    // view but in mostly readonly mode, with no playback since trim is not
    // mutable"). A Ctrl+Tab in the view is an ORDINARY A/B SWITCH — not a
    // special case and not a closer: the view stays up and re-reads the newly
    // active tab, which costs the mode NOTHING to re-bind because the two tabs
    // share one piece. tab_a and tab_b hold a VALUE-SHAPED BAND ALONE (viewport,
    // zoom, playhead, trim, read_only) and share the warp store, the phase-reset
    // store and the engine settings, while the delta's whole vocabulary is those
    // two stores plus `scale` — so the walks, the frozen now side and the head
    // delta describe the same piece on either tab.
    // (From 2026-08-05 to 2026-08-18 the tabs were the WALK SELECTOR instead —
    // two slots, "Remote" and "Local" — with Ctrl+Tab cycling the walk; the walk
    // has its own radio pair in row 4 now, and the row's record is at
    // RedesignButton::TabA. The row carried the walk-and-reading PRODUCT as four
    // slots for one day, 2026-08-07..08; the READING is row 4's own Cumulative
    // toggle, over a session bit that is not in this struct at all —
    // AppState::history_cumulative, which is why nothing here names it.)
    //
    // AND THERE ARE TWO WALKS TO SELECT BETWEEN SINCE 2026-08-07 (architect,
    // "the local history feature will be helpful for understanding undo/redo
    // history"): the COMMITTED history this mode was built on, and THE SESSION'S
    // OWN UNDO/REDO TIMELINE read through the identical delta machinery — every
    // state Ctrl+Z and Ctrl+Shift+Z can reach plus the live one, newest first
    // since 2026-08-08 (GuiHistoryLocalWalk, history_diff.h, owns the model and
    // the pairing derivation). The lane, the flags, the colours, the corner's `n/N`, the
    // walk's `,` / `.` and the diff-flag cycle are all SOURCE-AGNOSTIC — they
    // read the displayed delta and
    // the active walk's position, never a named walk. (The trim bar's span and
    // its framing double-click were on that list until 2026-08-18, when the bar
    // went back to displaying the tab's own trim window in the view and the
    // double-click went back to framing it: neither reads the delta at all
    // now.) THREE surfaces are not,
    // and each says why at its own site: the corner's SHA token (an undo entry
    // has no name), the `'` LOAD-IN-PLACE, which is LIVE ON BOTH WALKS since
    // 2026-08-08 wherever the active walk carries a member and FORKS ON THE
    // SOURCE — the editor asks for a commit spelling
    // on the Remote tab and a member NUMBER on the Local one, and the act behind
    // it is a different function per walk (the mode's two, at the opener and at
    // load_editor_commit) — and SAVE-AND-COMMIT, whose reach
    // and grey stay the commit walk's because the act publishes into the
    // repository. THE REVERT ACT IS LIVE ON LOCAL FLAGS and deliberately so: it
    // reads the painted flags' frames and then-side lines and knows nothing
    // about where they came from, so selecting part of one undo event and
    // putting just that part back is the feature working.
    //
    // ENTRY IS GATED ON THE COMMIT SIDE ALONE — the local walk RIDES the mode,
    // it does not carry it — and on that side it is the HEADER and the scan
    // ANSWERING, never the member count (2026-08-09): a piece the header cannot
    // place, or one whose history could not be read, cannot be opened to read
    // its undo stack, while a piece whose walk is merely EMPTY opens with the
    // Local tab reading normally beside a blank Remote lane.
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
    // ON THE LOCAL TAB it opens prefilled with the viewed member's displayed
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
    // and the mode's status corner KEEPS its cell throughout (2026-08-12: the
    // editor is a centered DIALOG painted over the row, not a tenant of it, so
    // there is no line to yield).
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
    // "the message is derived, not chosen"): a fourth modal editor (dialog-
    // hosted since 2026-08-12),
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
    // GuiHistoryCommitWorker; its four failing verdicts come back to THE
    // BOTTOM ROW'S CRITICAL SLOT (architect 2026-08-09, replacing the
    // acknowledge modal they raised until then: a critical failure must be
    // impossible to miss and impossible to hijack the keyboard with, so the
    // report is permanent and paint-only — critical_error_message, below), and
    // its two ESTABLISHED ones say what they have to say on stderr and CLEAR the
    // slot. The fourth failure is Unconfirmed, split out of NothingToCommit that
    // same day: an act that confirmed neither the content nor the publication
    // must not clear a standing report by claiming a clean ending.
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
    // routes that move it (membership re-derived 2026-08-12 — the ruler drag
    // is the mode's whole navigation surface under pan-primary): zoom, the
    // paged
    // scroll, the plain wheel's stepped pan and the overview command move
    // viewport_start_sample or zoom_level,
    // the
    // pointer's ONE nav drag moves both through its pan and ctrl zoom phases,
    // the mode's OWN
    // cursor-moving
    // acts land the playhead (the diff-flag click, the deferred click act, and
    // the
    // keyboard's Tab cycle / Home / End / `c` — which `0` reaches too, from full
    // zoom out), and since 2026-08-04 the admitted VIEW SWITCHES
    // move the two whole-file keys `active_audio_view=` and
    // `active_markers_view=` (`t` moving the per-tab band with them, the
    // playhead and viewport translating across the domain flip), and since
    // 2026-08-18 the admitted A/B TAB SWITCH moves `active_tab_view=` and swaps
    // both tabs' bands — every one of
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
        // WHICH WALK THE LANE IS READING (architect 2026-08-07) — selected by
        // the icon row's two WALK RADIOS on bare `g` since 2026-08-18 (row 3's
        // tabs carried it until then). GuiHistoryWalkSource (history_diff.h)
        // owns the pair's definitions and the local walk's whole model; what
        // lives here is the session's own state.
        //
        // COMMIT IS THE DEFAULT AT EVERY ENTRY, this plain member initializer
        // applied by the whole-struct machinery at both edges exactly as the
        // compare bit's is: a visit never inherits the last visit's walk.
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
        // edge and at the step): the window is the user's while the view stands
        // — and since 2026-08-18 that is EVERY edge, the entry's own framing
        // having gone with the view's navigation state.
        //
        // EVERY READER OF THE DISPLAYED DELTA PASSES THE PAIR, and since
        // 2026-08-07 they do so THROUGH ONE ACCESSOR (displayed_delta() below)
        // rather than by naming both halves: the reading is a (source, compare)
        // PAIR, and four call sites spelling that fork is four places for a
        // Local tab to keep showing commit flags. Its readers, re-derived by
        // grep on displayed_delta: the flag cache's rebuild (waveform_cache.cpp)
        // and the bottom strip's corner line — it had two more until 2026-08-18,
        // the mode's diff-span framing and paint_trim's diff-span substitution,
        // both deleted with the trim bar's return to the ordinary framing. The
        // ONE reader that deliberately does NOT is
        // head_delta_empty below, which names the COMMIT walk's index 0 and the
        // Cumulative reading explicitly, and says why.
        // The mode's OWN focus: an index into `flags` below, -1 for none. It is
        // NOT a marker index and touches no selection.
        //
        // EVERY SETTER (re-derived by grep 2026-08-12): the mode's own PLAIN
        // focus CLICK on a diff flag's box in the lane, its ONE pointer
        // surface since the stems-inert ruling
        // (focus_history_diff_flag); the LANE's two MODIFIED clicks, which focus
        // the flag they select (select_history_diff_flags_modified — flag
        // boxes alone); and the mode's own
        // bare Tab / Shift+Tab / IsoLeftTab cycle. Each sets it and lands the
        // playhead on that flag's frame, and nothing else writes it true.
        // ALL THREE CLICKS RUN AT THE PRESS (2026-08-17, reverting the
        // one-day lift deferral of 2026-08-15), on the index the press
        // resolved: the setters are the same three bodies.
        //
        // EVERY CLEARER, the whole list, and all but the last clear for ONE
        // reason: the value is an ordinal into the PAINTED list, so anything
        // that rebuilds that list would otherwise leave the highlight on an
        // unrelated flag.
        //   - each `,` / `.` step (handle_history_mode_key)
        //   - each WALK-OR-READING SWITCH (2026-08-05, set_history_reading):
        //     the two
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
        //   - bare HOME / END (2026-08-05), the mode's SHIFT FORMER's press,
        //     and the DEFERRED CLICK ACT at a motionless navigation-surface
        //     release (2026-08-12, the eighth glass ruling: the mode's plain
        //     press is the pending click / grab-pan over the whole waveform +
        //     ruler + empty lane stretches, and its motionless release runs
        //     the mode's land — run_nav_click_act's history arm — where the
        //     press-time placement used to; the PAN, a crossed plain drag,
        //     deliberately clears nothing), and these are
        //     the exception to the reason above: the list is untouched, but the
        //     playhead moves to a spot nothing marks — an end of the song, or the
        //     pressed column — LEAVING the focused flag, so the focus goes with
        //     it, the mode's analog of the live arms' selection clear. (The
        //     old empty-lane click's deliberate clear-and-land-nothing is
        //     SUPERSEDED by the deferred act, which clears AND places.)
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
        // EVERY SETTER: the two MODIFIED diff-flag clicks, ON THE LANE'S FLAG
        // BOXES and nowhere else (select_history_diff_flags_modified,
        // input_pointer.cpp — the stems are pointer-inert in every context
        // since 2026-08-12, and waveform/lane modifiers off a flag are gesture
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
        // Tab cycle and its bare `c` (handle_history_mode_key), and BOTH CLICK
        // BODIES — the plain focus click (focus_history_diff_flag) and the
        // modified pair (select_history_diff_flags_modified), which reads it for
        // the frame it lands on; the second was missing from this list before
        // 2026-08-15 and is a re-grep, not a new reader. Every one of them reads
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
        //
        // AND THE WINDOW HAS A SECOND EXIT since 2026-08-09: a run that reports
        // DONE having delivered NOTHING. The walk is then finished and empty —
        // a legal standing state, the view resting at `0/0` — and the answer is
        // no longer unknown but FALSE: with no eligible checkpoint to measure
        // against there is by definition everything to checkpoint, and the act
        // must be live or the empty walk could never gain its first member. Both
        // exits latch through the same one site.
        bool head_delta_empty = true;

        // Whether the bit above is an ANSWER rather than the resting default.
        // False until the walk has answered — either member 0 compared against
        // the frozen now side, or the run finishing with no member at all; the
        // one measurement site sets it and nothing clears it inside a visit (the
        // whole-struct reset at both edges does).
        bool head_delta_measured = false;

        // THE VIEW OWNS NO NAVIGATION STATE (architect 2026-08-18, "this
        // removes machinery"). It is a READING OF THE LANE over whatever window
        // the user is in, and the window is HIS throughout — entry and exit
        // included. Opening the view frames nothing and snapshots nothing;
        // leaving it restores nothing. So the viewport, the zoom and the
        // playhead simply stay where they were across both edges, and each A/B
        // TAB KEEPS ITS OWN BAND across a visit for free: one tab can stand
        // framed wide while the other stands zoomed, because entering and
        // leaving stopped normalizing the window.
        //
        // WHAT WENT, in one sentence, because "the view should restore where
        // you were" is an easy thing to re-invent: from 2026-08-05 to
        // 2026-08-18 the entry framed the whole song and stashed the live
        // viewport / zoom / playhead trio plus the audio view it was taken in
        // (entry_viewport_start_sample, entry_zoom_level,
        // entry_playhead_cursor_sample, entry_audio_view), and the one exit
        // owner put that trio back — translating the two frame-shaped values
        // through the warp map when a `t` inside the view had flipped the
        // domain, and re-landing a surviving selection on its focus after it.
        // All four fields, the framing call at the entry and the whole restore
        // are deleted — as, later the same day, are the mode's two framing
        // owners themselves, the trim bar's double-click having gone back to
        // the ordinary span framing.

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
    // gui_scale, audio_player, projects_repo) — do
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
    // Trim is a band authored purely by the ENDCAP / BRIDGE pointer drags — on
    // the 9 px bar and, since 2026-08-18, on the waveform OVERLAY that is this
    // same window painted a second time — the ctrl / ctrl+shift bound-set
    // clicks, the SWEEP (shift+drag or the touch region hold, which writes the
    // pair in one stroke under no width rule at all; it replaced the bare-`x`
    // set-from-region arm when the region became the trim, and `x` now shows
    // and hides the overlay and writes no trim at all), the
    // Shift+X MAXIMIZER (writes the full window), and the settings editor's
    // `:tab_X_trim_*=` commits — it is NOT part of the selection system (no
    // bound selection, no Tab stop, no Delete arm). It is ALWAYS SET: the full
    // ruling is at the TrimState store.
    TrimState trim;

    // The command prompt, THE BOTTOM ROW'S MODAL (paint_modal_dialog,
    // 2026-08-13 — the row's tenants stand down and the lane carries the
    // question, its responses real buttons). Active only when a close /
    // re-detect gesture fires while a confirmation is required.
    PromptState prompt;

    // Shared text-editor state for THREE editors distinguished by Kind: the
    // top-strip flag editor (Kind::FlagPayload — active when editing a warp
    // marker's payload, its text run and caret painted live ON THE FLAG ITSELF
    // since row 5's text-on-flag model: render_flag_editor_box unrolls the
    // marker's own box, which the flag pass therefore skips), the BPM editor
    // (Kind::BpmBracket), which paints as the BOTTOM ROW'S MODAL like the
    // other four dialog editors (2026-08-13), and the marker MEASURE editor
    // (Kind::MeasureText, since 2026-08-19), which paints in the top strip
    // like the flag editor and carries no red-flash edge of its own. The
    // editor owns the keyboard while active.
    text_editor::State top_flag_editor;
    // Last-painted cursor visibility, so the tick can detect a flip and
    // invalidate the top strip without redundant repaints.
    bool top_flag_editor_blink_last = false;

    // Settings-prompt editor. Opens on `;`, accepts a single `key=value`
    // line, writes to engine_settings on commit. Paints as the BOTTOM ROW'S
    // MODAL (2026-08-13); separate from top_flag_editor so the two paint
    // regions stay independent (the in-practice mutual exclusion comes from the
    // flag editor swallowing all keys while active).
    text_editor::State settings_editor;
    bool settings_editor_blink_last = false;

    // Load prompt editor. Opens on bare `'` from an authoring view,
    // takes a render entry's identifier relative to renders/
    // (`<batch_dir>/<basename>` or a globally-unique bare basename), and on
    // Enter loads that render's frozen sidecar recipe in place as the new
    // authoring baseline (GuiInputHandler::load_render_entry_in_place). A
    // dialog modal like
    // the settings editor; separate State so the two paint regions stay
    // independent.
    text_editor::State load_editor;
    bool load_editor_blink_last = false;

    // THE COMMIT-TITLE EDITOR (architect 2026-08-07), the fourth dialog
    // modal and the `h` history view's own: Ctrl+S while the view stands
    // opens it prefilled with the checkpoint's default message (`Update <id>`,
    // history_checkpoint_title's own spelling) and Enter runs the Save-and-
    // Commit act with whatever the buffer holds as the commit title. It
    // REPLACED the confirmation prompt that used to guard the act: the question
    // "shall I?" and the question "under what message?" are the same pause, and
    // only the second one carries information — a bare Enter is the old `y`.
    // Esc abandons with nothing written, and an empty or whitespace-only buffer
    // red-flashes rather than committing an unnamed checkpoint.
    // A dialog modal like the two above, with its own State so the paint
    // regions stay independent; it can only be open while the history mode
    // stands, which is what keeps it out of every other surface's way.
    text_editor::State commit_title_editor;
    bool commit_title_editor_blink_last = false;

    // THE MEASURE PASTE-OFFSET EDITOR (architect 2026-08-20), the FIFTH dialog
    // modal and the measure propagate's own: Ctrl+Alt+/ over exactly one
    // selected warp marker opens it seeded with `0`, and Enter applies the
    // clipboard onto the destination run with that many measures added to every
    // DIRECT measure it writes. It stands where the phase paste's CONFIRMATION
    // PROMPT stands in its own family, and for the commit-title editor's
    // reason: the pause is the same pause, and asking for the offset carries
    // information a bare yes/no does not — a bare Enter over the `0` seed IS
    // that prompt's `y`, pasting the repeat unshifted.
    //
    // ITS SUBJECT SLOT CARRIES THE PASTE ANCHOR (`State::target`, the
    // commit-title precedent, which parks a 0 there): the destination warp
    // marker index, seated at the open and read once at the commit.
    // Esc abandons with nothing written, and a buffer that is not one canonical
    // signed integer red-flashes rather than pasting — as does an offset that
    // would carry any pasted measure out of the [1, 99999] bracket, which
    // refuses the paste WHOLE rather than clamping or partially applying.
    // A dialog modal like the three above, with its own State so the paint
    // regions stay independent.
    text_editor::State measure_offset_editor;
    bool measure_offset_editor_blink_last = false;

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

    // THE CRITICAL SLOT — the status chain's LEFTMOST member (right-aligned in
    // the TAB ROW since 2026-08-13, leftmost-in-chain so it can
    // never be pushed off — the chain left-anchors when it overflows and C
    // clips instead; the layout is at paint_status_chain), and the product's
    // one
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
    // ONE THING CAN COVER IT, and covers it without touching it: a TAB drawn
    // over it. The chain paints FIRST and the tabs paint over it (architect
    // 2026-08-13: "if there is ever a resolution small enough that there's a
    // conflict, the tabs should win... but don't anticipate that"), so on a
    // window narrow enough for the two to meet the chip's box is the thing
    // that gives. That is accepted and deliberately not engineered around —
    // no clipping against the tabs, no reflow — and it costs the chip nothing,
    // the state being untouched and permanent. (A MODAL used to hide it
    // instead: the chain lived on the bottom row until 2026-08-13 and that row
    // yields whole to a dialog. The two surfaces no longer contend.)
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
    // on the four failing verdicts and clears it on the two ESTABLISHED ones —
    // never on an answer that merely failed to establish anything. Empty
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

    // Non-interactive status text — TIER 2 of the TAB ROW's status chain since
    // 2026-08-13 — giving the user visual feedback while no other UI is
    // updating. Driven by the shared batch
    // runner (the iteration/BPM sweeps), startup loading, Ctrl+Alt+R, and
    // target-preview updates — not a manual queue. Empty means "no status".
    // IT COEXISTS WITH prompt.active AS STATE, and since the chain left the
    // bottom row IT ALSO PAINTS THROUGH A MODAL: an archival render runs on,
    // so dirtying the project and pressing Ctrl+Q raises the close prompt over
    // a live run (the prompt cancels nothing), and the run's own completion can
    // rewrite or clear this string while the prompt stands — the prompt owns
    // the BOTTOM row and the chain owns a span of the TAB row, two surfaces
    // that no longer contend. (Until that move the row yielded whole to the
    // modal and the string was simply not painted while one stood.) NO
    // PRECEDENCE TIER IS INVOLVED either way: the old shared-cell ordering
    // (the prompt as the chain's first tier) is superseded structurally, and
    // no status write needs a modal test of its own.
    std::string queue_progress_text;

    // Transient one-line status message shown in the TAB ROW's status
    // span, one tier above the resolved readout (the row-7
    // chain; it was an appendix on the status line when the strip had two
    // rows and view letters, and a bottom-row tenant until the chain moved up
    // on 2026-08-13). Set by a
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

    // Measure propagate (W-mode Ctrl+/ copy, Ctrl+Alt+/ paste; architect
    // 2026-08-20). Single-slot session-only clipboard cleared on app exit, the
    // sibling above's shape and lifetime; the header carries what an entry
    // holds and why the feature is warp-column only. IT NEEDS NO ANCHOR FIELD
    // BESIDE IT, unlike the phase pair: the paste's destination anchor rides in
    // its own editor's subject slot (`text_editor::State::target`, the
    // commit-title precedent) for the one modal's lifetime, so there is no
    // second place for it to go stale.
    MeasureClipboard   measure_clipboard;

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
GuiRect top_tab_row_area(const AppState& a);
GuiRect top_icon_row_area(const AppState& a);
// THE OVERVIEW STRIP (top lane 3 since the relayout's commit B, 2026-08-12 —
// the Ableton model, ableton.png): the whole-song lane between the ICON ROW and
// the TRIM BAR, inside the centered block — min/max bars off the peaks pyramid,
// the viewport box, the playhead tick, and — since the lane rework later that
// day — the box-endcap edge drags, the click-teleport and the
// box-follows-pointer pan, ALL of them on the PLAIN press and all of them
// acting on the BOX (OverviewDragState; the ctrl strip drag that shared the
// lane until 2026-08-15 is deleted, and ctrl binds nothing here now).
// ONE fixed tiny height
// on every host (the ruling, the deleted min/max clamp pair and the 1px
// border-bottom are all at render.h's kOverviewHeightPx). It was a BOTTOM-strip
// lane under the unified row for the afternoon it landed;
// bottom_overview_row_area is this accessor's former name.
GuiRect top_overview_row_area(const AppState& a);
// GAP 1's band — the flexible blank window ground between the menu row and the
// centered block (the vertical rule, main.cpp). ONE consumer, the wheel-inert
// band list: the band lies inside top_strip_area, which is a pan surface, so it
// needs a band of its own to stay inert. Gap 2 (above the bottom row) has no
// accessor by the same reasoning inverted — it lies below every wheel area.
GuiRect top_flex_gap_area(const AppState& a);
// ROW 5's three lanes (2026-08-01), replacing the legacy
// chip / marker-text / flag / triangle four.
GuiRect top_trim_row_area(const AppState& a);
GuiRect top_ruler_row_area(const AppState& a);
// THE TWO LANES ARE SEPARATE INPUT SURFACES, and they answer differently: the
// TRIM BAR carries its own gestures (endcap / bridge drags, the ctrl and
// ctrl+shift bound-set clicks, the span-framing double-click), while the RULER
// is a member of the NAVIGATION SURFACE since 2026-08-12's eighth glass ruling
// — plain drag is the pending click / grab-pan, a motionless plain click the
// deferred playhead placement, SHIFT the one region former, CTRL that same
// drag's live zoom phase (2026-08-14's one-model ruling; it was the dual-axis
// strip drag for two days). (Its own dedicated strip-drag entry and the ruler-scoped region
// former it briefly carried are both gone: the entry was deleted for good
// earlier that day, and the former lived half a day before the ruling folded
// the ruler into the one vocabulary. top_trim_surface_area — the trim surface
// arc's merged trim-bar + ruler input band — lived between these accessors for
// one day, 2026-08-11..12, and was deleted whole with the arc's revert.)
GuiRect top_marker_row_area(const AppState& a);
// THE UNIFIED BOTTOM ROW (2026-08-12, rows 8 and 9 merged): the bottom
// strip's ONE lane (bottom lane 0), resting on the WINDOW'S FOOT since the
// relayout's commit B with GAP 2's blank ground between it and the waveform —
// the lane including its 1px
// border-top, and the content band under that border. (It was row 7's single
// status lane from 2026-08-01, one of two lanes while the transport row
// stood, 2026-08-11..12, the strip's whole surface at the unification, one of
// two again while the OVERVIEW STRIP sat below it that afternoon, and the
// strip's one lane from commit B; each flexible gap is strip geometry, not a
// lane.)
GuiRect bottom_row_area(const AppState& a);
GuiRect bottom_row_content_area(const AppState& a);
// THE OVERVIEW STRIP'S COLUMN MAPPING — one owner for the lane's
// frames-per-column, shared by the painter (the bars' basis, the box and the
// tick), the press claim (the drag anchor) and the tick's per-frame damage
// sites so no two of them scale differently. THE DATA IS THE SOURCE DOMAIN,
// ALWAYS (the ruled choice, recorded at paint_overview_strip): the lane spans
// the whole PIECE — audio.total_frames() over the lane's width — in EVERY
// view; target-domain values map through the warp frame map before they meet
// this scale. Returns 0.0 on degenerate geometry (no lane width / no audio).
double overview_samples_per_pixel(const AppState& a, const GuiAudio& audio);
// The lane column (offset from the lane's x) the tick painter draws an
// ACTIVE-DOMAIN position at: nearbyint the position, inverse-map it to its
// source frame in target view (the memoized active_domain_to_source_frame),
// divide by the lane scale, clamp into [0, lane.w - 1]. Shared by
// paint_overview_strip and the two per-frame scanner damage sites (main.cpp)
// so the damaged column IS the painted one. Returns -1 on degenerate
// geometry.
int overview_tick_column(const AppState& a, const GuiAudio& audio,
                         double active_position);
// The SONG POSITION at window x on the overview lane: (x - lane.x) * the
// lane scale, a source frame by construction — mapped INTO the active domain
// in target view (source_frame_to_active_domain) so the value is
// domain-correct for every consumer. Returns a frame position as a double.
// THREE call sites (re-greped 2026-08-18; the ctrl strip drag was a fourth
// until its deletion, 2026-08-15): the click-teleport's centering position,
// the box pan / edge drags' per-event pointer position — both of which
// column-clamp x into the lane first, the song walls by construction — and the
// box pan's grab-offset seat in the press router, whose x is inside the lane
// already, the claim's own rect having admitted it.
// IT IS A PURE SCALE, NOT A HIT TEST, AND x MAY LEGITIMATELY BE lane.x + lane.w
// (codex round 21): what it returns is the NEAR boundary of column x, so a
// caller wanting a column's FAR boundary — which is what an END bound is, the
// box span being half-open [x0, x1) — asks at x + 1, and at the right wall that
// is lane.w, the song end exactly. The edge-END drag is the one caller that
// does; the pan and the teleport want the bin's origin and ask at the column
// itself. The full invariant (the map and overview_box_span must agree at BOTH
// walls) is recorded at apply_overview_drag_at's mapping call, input_pointer.cpp.
double overview_anchor_sample_at_x(const AppState& a, const GuiAudio& audio,
                                   int x);
// THE BOX'S TWO EDGES AS ACTIVE-DOMAIN SAMPLES — the ONE owner of "where the
// box's edges are" before they become columns: the live viewport's start and
// its viewport_end_sample, the END clamped to live_total_frames. TWO readers:
// overview_box_span below (which turns them into the painted columns) and the
// edge drag's seat (seat_overview_edge_drag, input_pointer.cpp), which pivots
// on the fixed edge and must therefore pivot on the PAINTED one — the ruled
// right-wall grid rest may sit up to a pixel past the song end, which is the
// only place the raw and painted endpoints differ (the derivation is at the
// definition, app_state.cpp). Returns false on degenerate geometry (no
// waveform width, no spp) with the outputs untouched.
bool overview_box_edge_samples(const AppState& a, const GuiAudio& audio,
                               int64_t* out_begin, int64_t* out_end);
// THE VIEWPORT BOX'S LANE COLUMNS — the ONE owner of the box arithmetic,
// shared by the painter (paint_overview_strip's layer 3) and the lane's hit
// geometry (hit_test_overview_endcap below, the press claim's inside-the-box
// test) so a grabbed edge is exactly a painted one. Lane-relative half-open
// span [*x0, *x1): the LIVE viewport's span (start through
// viewport_end_sample at the current spp over the effective width),
// inverse-mapped to source columns in target view, wall-clamped, >= 1px.
// Returns false on degenerate geometry (no lane, no audio, no waveform
// width) with the outputs untouched.
bool overview_box_span(const AppState& a, const GuiAudio& audio,
                       int* out_x0, int* out_x1);
// (The box endcaps' hit test, hit_test_overview_endcap, is declared beside
// hit_test_trim_endcap below — it returns that family's TrimHit.)
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
// zoom/pan drag, a region-select drag, a REGION EDIT drag (the standing span's
// own move / bound gestures, 2026-08-15), an editor
// text drag, the MARKER FLAG'S PENDING PRESS (its click acted at the press —
// 2026-08-17 — but the drag it may become and the seed its release owes both
// need the world held still), THE ONE DEFERRED PENDING CLICK ACT
// (PendingClickAct — the trim bar's two bound sets, the one surviving lift act
// of 2026-08-17; it holds a whole unrun click, which is why it must be in
// flight here), or the pending trim drag
// (button held, watching for the threshold). (The scrub still
// has no entry of its own — it is a one-shot ACT, not a gesture — but since
// 2026-08-13 its press arms the navigation surface's PENDING CLICK like every
// other press on that surface, so a held lower-half press IS in flight here
// through scroll_drag, exactly as a held upper-half press is. The target-view
// TEMPO drag and its pending were on this list until
// 2026-07-29, when the whole tempo drag was deleted — see marker_drag.h.)
// SIX CONSUMERS, re-derived by grep 2026-08-12 (the follow chase joined; the
// eighth ruling's touch half had left FIVE, its mouse half FOUR — deleting the
// bare right-press scrub's gate — and the timer-free touch model had deleted
// begin_touch_trim_move the same day). EACH STATES THE SAME
// "nothing pops mid-gesture" BOUNDARY FROM ITS OWN SIDE, and they split into
// two kinds: the four INPUT-ROUTE consumers refuse events, and the run loop's
// TWO — the per-tick hover refresh and the pre-paint follow chase — pause the
// world's autonomous movers for the gesture's life:
//   * wheel_context (input_handler.cpp) — on_wheel's completed-detent gate and
//     the platform's per-frame sub-detent accumulator probe both route through
//     it, so a wheel cannot shift the viewport out from under a gesture (the
//     PENDING drags are included for exactly that: not out from under a press
//     before its drag begins either);
//   * repeat_eligible (input_key_dispatch.cpp) — a key held through a gesture
//     must not arm a repeat that fires once the gesture ends;
//   * pointer_cursor_kind's live-gesture refusal (input_pointer.cpp) — a cue
//     must not promise a press mid-drag — RANKED BELOW the trim-gesture arm,
//     the one gesture that keeps its own cursor (architect 2026-08-03; the
//     contract is at pointer_cursor_kind's declaration, input_handler.h);
//   * begin_touch_region's refusal (input_pointer.cpp, the eighth ruling's
//     touch half) — the region hold bypasses the press path, so it restates
//     the same no-second-writer boundary the press claims inherit by
//     ordering (the dead begin_touch_trim_move's own reading, revived with
//     the hook pattern);
//   * the run loop's per-tick redesign-button hover refresh (main.cpp) — an
//     active gesture FREEZES hover;
//   * the run loop's PRE-PAINT FOLLOW CHASE (main.cpp) — the autopager is the
//     product's one autonomous viewport mover, and every aiming gesture
//     converts a window column through the CURRENT viewport later than the
//     press it was aimed with, so the chase is PAUSED (it writes nothing here,
//     so this is no follow producer) for the gesture's whole life. Its touch
//     sibling term is the platform's own contact query, the Pending window
//     arming nothing this predicate can see; the argument is at the gate.
// (A CONSUMER lived here for one day of 2026-08-09 — the checkpoint's
// acknowledge modal, which asked this before raising itself from a worker's
// clock, a prompt over a live gesture having its release swallowed at the
// prompt's own gate. It went with the modal, which became a paint-only slot.
// Nothing ASYNCHRONOUS asks this question now.)
inline bool any_pointer_gesture_active(const AppState& app) {
    return app.drag.active ||
           app.trim_drag.active ||
           app.scroll_drag.active ||
           app.overview_drag.active ||
           app.region_drag.active ||
           app.editor_text_drag.active ||
           app.pending_marker_press.active ||
           app.pending_click.active() ||
           app.pending_trim_drag.active;
}

// architect ruling 2026-07-22: each marker column authors in its HOME view
// only — warp markers in source view, phase resets in target view. In the
// non-home view a column is display/navigation-only (selection, Tab and the
// selection-only readout all live — the retired hover popup and lane readouts
// are recorded at the HoverPopupState deletion note above;
// every placement/store mutation refuses silently,
// navigation-class, exactly the read-only-tab convention). The FOUR ruled
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
// column in either view. (4) THE MARKER MEASURE (architect 2026-08-19, the
// field rebranded from the marker comment 2026-08-20), the widest of the four
// and the only one that is BOTH COLUMNS AND BOTH VIEWS: a measure names where
// a marker sits IN THE SCORE rather than authoring a musical value — nothing
// in it reaches the engine, the frame map or the render fingerprint — so it is
// editable wherever the flag paints. Its three entry routes (bare `/`,
// the bottom-row button, the double-click on the blue box) consult this
// predicate nowhere; their one gate is READ-ONLY, which still refuses, a
// measure being serialized content. THE MEASURE PROPAGATE RIDES UNDER THIS
// SAME EXCEPTION (Ctrl+/ and Ctrl+Alt+/, 2026-08-20): it writes the same field
// through the same store path, so it is legal in both audio views and consults
// this predicate nowhere either — the phase reset propagate's own precedent,
// which likewise asks the mode and the selection and never this. It is
// WARP-COLUMN ONLY for a reason of its own, unrelated to the home-view binding
// (propagate matches destinations by LABEL, and only the warp column has
// labels — the ruling is at measure_clipboard.h). The phase column's measure
// double-click is
// therefore that column's FIRST pointer authoring gesture, measure-scoped and
// nothing wider (recorded at the router arm, run_marker_click_act). The list
// SHRANK to two on 2026-07-29, grew back to three on 2026-08-07 and to four on
// 2026-08-19: the
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

// THE TEMPO CENT STEP'S COLUMN GATE — bare Up / Down author TEMPO, and tempo
// is the WARP column's alone: a phase reset has no tempo to step. It is the
// first refusal in GuiWarpMarkersOps::adjust_tempo_cents (which the singleton
// and the group arm share), and it is deliberately NOT
// active_column_authoring_allowed above — the cent step is that predicate's
// ruled exception (1), reachable in W+target as well as W+source, so what it
// asks is the COLUMN and not the home view.
inline bool tempo_cent_step_column_allowed(const AppState& app) {
    return app.active_markers_view == 'W';
}

// THE TEMPO CENT STEP'S STABLE-STATE REFUSALS, composed: the column gate above
// plus the two subject refusals adjust_tempo_cents runs next — an empty
// selection and an invalid focus. ONE READER: the act's own leading refusal
// block (GuiWarpMarkersOps::adjust_tempo_cents, whose first three returns this
// predicate IS).
//
// IT HAD A SECOND READER FOR ONE EVENING — the bottom row's UP and DOWN arrow
// buttons, which mirrored it into their disabled face under the 2026-08-15
// whole-row honesty ruling ("the ENTIRE ROW should be accurate to the live
// state"), itself widening the 2026-08-13 mirror of the column gate alone.
// THE ARROWS DROPPED THE MIRROR THE SAME DAY (architect, ruling after the
// full-truthfulness experiment for the icon row was reversed unbuilt — the
// reasoning is recorded whole at redesign_button_enabled): every term this
// predicate adds beyond the column gate is a SELECTION fact, and the Up/Down
// pair is precisely where he watched that show — the pair nudges tempo in the
// WARP view and cannot act at all in the PHASE RESET one, so "every time I
// selected a marker, that pair would blink in and out, and it would be
// distracting". The predicate is unchanged and STAYS — the
// act's refusals are the same three returns they always were, and naming them
// once is what makes the act legible; only the face stopped reading it. What
// was already live-faced under the mirror, and still is, is the VALUE-shaped
// tail: a label ref, a pass marker in target view, the tempo bracket wall.
inline bool tempo_cent_step_actionable(const AppState& app) {
    return tempo_cent_step_column_allowed(app) &&
           !app.selected_markers.empty() && app.last_selected_marker >= 0;
}

// (THE HORIZONTAL ARROW STEP'S COMPOSED REFUSAL PREDICATE IS DELETED —
// horizontal_arrow_step_actionable, which lived here from the 2026-08-15
// whole-row honesty ruling until the architect's scoped-truth ruling the same
// day. It mirrored bare Left / Right's lane fork — the waveform-lane step
// always live, the marker-lane nudge refused off the focused column's home
// view and on a locked tab — into the bottom row's LEFT and RIGHT buttons, and
// the FACE was its only reader: the dispatch has always routed on its own
// fields (input_handler.cpp's marker-lane branch) and never called it. With
// the face arm gone it was producer-less, so it goes whole rather than resting
// as a definition nothing asks. Its reasoning was correct for the policy that
// then stood; what changed is the policy — every term it added past the empty
// selection is a per-selection fact, on a pair that is otherwise nearly always
// live ("left and right were always on because the playhead can always move"),
// so a rarely-true refusal bought a per-selection blink. The wall it never
// mirrored is still not mirrored: at frame 0 the left step cannot move and the
// button stays lit, which is the roster's standing treatment of walls.)

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
// THE OVERVIEW STRIP'S TICK EXTENDS THE SAME CADENCE SPLIT (2026-08-12): the
// lane's playhead tick mirrors the values above, so its damage takes the same
// two shapes — a NEW narrow per-frame pair for the scanner, the tick's own 1px
// lane columns at the pre-paint advance (main.cpp, through the one column
// owner overview_tick_column so the damaged column is the painted one; the
// heartbeat site carries no overview arm — its job is producing a paint, not
// naming movement), and the FULL-LANE shape for every discrete write, which
// needs no arm of its own: the lane sits inside the centered block since the
// relayout's commit B, so Viewport::invalidate_waveform_area's ONE rect
// (window top through the waveform bottom) contains it by construction, which
// retired that owner's dedicated overview rider. The tick maps through the
// ACTIVE domain to its SOURCE
// column, so it needs no plate basis at all — the lane's basis is the
// whole-song scale, which no async publish window can move.
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

// THE TRIM REGION OVERLAY'S SPAN, DERIVED AND NEVER STORED — the one owner of
// "where the overlay is", read fresh by everyone who needs it and kept by
// nobody (architect 2026-08-18: the region IS the trim; the model is at
// RegionState). It answers whatever the trim bounds say THIS FRAME, so a tempo
// change in target view, an undo that restores a map, a pan or a zoom all
// re-derive it with nothing to invalidate, and the overlay and the 9 px bar
// cannot drift because they are the same two numbers.
//
// THE BOUNDS ARE SOURCE FRAMES AND THE OVERLAY IS PAINTED IN THE ACTIVE DISPLAY
// DOMAIN, so each crosses through source_frame_to_active_domain (the identity
// in source view, the display map's forward hop in target) — the same one-way
// conversion every other source->display read takes — and then through the
// live-domain clamp, which is what keeps a bound at the domain's own wall
// rather than one past it at a fractional flush-right zoom.
//
// ORDERED ON THE WAY OUT. A resting trim pair is ordered by construction (a
// crossed or coincident one resets to the full window at every commit) and the
// display map is monotone, so the min/max only ever states that fact; the
// consumers — the painter's column projection and the hit test's two grab bands
// — want lo/hi and must not have to ask.
struct TrimOverlaySpan {
    int64_t lo = 0;   // active-domain frame of the trim BEGIN
    int64_t hi = 0;   // active-domain frame of the trim END
};

inline TrimOverlaySpan trim_overlay_span(const AppState& a,
                                         const GuiAudio& audio) {
    const int64_t b = clamp_playhead_to_live_domain(
        source_frame_to_active_domain(a, audio, a.trim.begin_frame), a, audio);
    const int64_t e = clamp_playhead_to_live_domain(
        source_frame_to_active_domain(a, audio, a.trim.end_frame), a, audio);
    TrimOverlaySpan s;
    s.lo = b < e ? b : e;
    s.hi = b < e ? e : b;
    return s;
}

// WHERE BARE Home / End WOULD LAND THE CURSOR — the two skip commands' one
// answer, hoisted 2026-08-15 so the three dispatch arms that jump share one
// arithmetic instead of hand-spelling it three times. `forward` selects End
// over Home. Defined in viewport.cpp beside the navigation range it reads.
//
// IT HAS NO FACE READER AND MUST NOT GAIN ONE (architect 2026-08-15). It was
// hoisted for the bottom row's two SKIP buttons as well — they greyed where the
// cursor already rested on the landing frame — and that half was ruled back out
// the same day: bare Home / End are not pure jumps (each also stops a live
// audition, clears the marker selection and hides the trim region overlay,
// even when the jump moves nothing), so a greyed skip promised less than its key
// delivers. The buttons answer a plain `true` now; the full record and the
// do-not-re-add line are at their case in redesign_button_enabled. This owner
// survives on the ACT's account alone — three callers, one spelling — which is
// why the hoist outlived the face it was first asked for. The header stayed
// here rather than moving to viewport.h so that history is stated once beside
// the predicate that no longer calls it.
//
// IT FORKS ON THE `h` VIEW, which is the whole reason it is a function rather
// than a pair of expressions: in the history view Home / End jump ABSOLUTE — 0
// and the live domain's last frame — deliberately not the trim bounds
// (architect 2026-08-05: the view reviews the WHOLE piece, so an End stopping
// at a trim bound would hide the flags past it), while the live arms take
// Viewport::trim_range, the navigation range owner. THE RETURNED FRAME IS
// PRE-CLAMPED through clamp_playhead_to_live_domain above. That clamp was first
// justified by the deleted face compare — an unclamped target could be a
// landing the resting cursor can never equal, and so a button that never greys
// — and it is KEPT on its own account now that no face reads it: this function
// names a LANDING, and a landing must be a frame the cursor can actually
// occupy. It costs the three act callers nothing, move_playhead_to clamping
// identically, so the arithmetic they adopted stayed byte-identical.
int64_t playhead_skip_landing_frame(const AppState& a, const GuiAudio& audio,
                                    bool forward);

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
// right-wall owner, hoisted out of the clamp_viewport_start chokepoint when the
// deleted strip drag's per-event pan clamp needed the same wall; that caller
// left with the gesture (2026-08-15) and the chokepoint is the one reader again.
int64_t max_viewport_start_grid(const AppState& a, const GuiAudio& audio);
std::pair<long long, long long> compute_trim_samples(
    const AppState& a, long long total_frames);
// THE BOTTOM ROW'S ONE RECT OWNER — the CLOCK CELL. Which owner a route wants
// is decided by WHICH PIXELS IT ERASES, the product's standing rule that damage
// follows the basis of what it repaints (playhead_pixel_x above states the same
// rule for the waveform's two bases).
//
//   clock_invalidate_rect — the reserved CLOCK CELL in the lane's left
//   block, and
//   nothing else on the row: every route that moves the PLAYHEAD or the
//   SCANNER, which is the only thing the clock reads. Reached through
//   Viewport::invalidate_clock_area, whose declaration carries the caller
//   inventory (viewport.h).
//
//   IT WIDENS TO THE WHOLE LANE WHILE A MODAL STANDS, by construction rather
//   than by a test of its own: the row yields whole to the modal, so its
//   painter publishes no clock cell, and a zero cell is already this owner's
//   honest widening.
//
// THE ROW'S OTHER TWO TENANTS NEED NO RECT FUNCTION. The transport buttons on
// the left and the arrow cluster flush at the right are the button tenants:
// their damage rides the roster machinery (the face writers and the tick
// comparator), exactly as the top rows' buttons do. The MODAL takes the LANE
// whole through Viewport::invalidate_modal_dialog_area, which spells
// bottom_row_area directly.
//
// (THE STATUS CELL'S OWNER LIVED HERE UNTIL 2026-08-13, when the architect
// moved the whole status chain — the four-tier ladder and the critical chip —
// into the TAB ROW. Its span ran from the clock's cell to the arrow cluster's
// left edge and it was this row's high-traffic string owner; the chain's home
// is Viewport::invalidate_status_chain_area now, and the two families that
// shared the old owner — the string writers and the dialog editors' repaint
// sites — are two populations with two owners, each inventoried at its own
// declaration in viewport.h. A route touching the clock AND the chain, as a
// load-in-place or an undo restore does, still calls both, spelling the two
// surfaces it dirties rather than widening to one rect over them.)
GuiRect clock_invalidate_rect(const AppState& a);
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
// It RETURNS WHETHER ANYTHING WAS STANDING, because some of those sites damage
// the window only when something actually changed (bare Home / End, the mode's
// shift former and its deferred click act — a face swap costs a repaint, and
// nothing swapping
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
// alone. Empty means Ctrl+H is a consumed no-op — history_mode_key_blocked's
// admission is conditional on this, and THAT KEY GATE IS THIS PREDICATE'S ONE
// READER SINCE 2026-08-15.
//
// IT HAD TWO, AND THE SUPERSEDED SHAPE IS KEPT BECAUSE IT WAS THE CLEANEST THIS
// ROSTER HAS HAD: the Revert button's face was DERIVED from that same admission
// rather than from a second spelling of it, so ONE decision both refused the
// chord and greyed the button, on the head-delta precedent exactly. THE
// ARCHITECT REVERSED THE FACE HALF ALONE, and his reason is the blink rather
// than the logic: this is a PURE READ OF THE MODE, so it moves with every click
// in the marker lane, and the glyph flipped on each one to report a selection
// the lane is already showing — the same argument that took the four cardinal
// arrows always-on. Revert is lit whatever this answers WHILE THE VIEW STANDS
// (redesign_button_enabled lifts the four history companions over the derived
// partition), a click on it with
// no subject is a consumed no-op, and the full record with the reversal's
// reasoning lives at that arm. OUTSIDE the view it is dead like its three
// neighbours, which is the companions' own resting arm and not this term. DO NOT GIVE THIS PREDICATE A FACE READER AGAIN.
inline bool history_mode_revert_subject_standing(
        const AppState::HistoryMode& mode) {
    return !mode.selection.empty() || mode.focus >= 0;
}

// (THE MENUS' ONE PER-ITEM DISABLED STATE IS DELETED — dropdown_item_enabled,
// 2026-08-08 to 2026-08-15, gone PRODUCER-LESS with the Navigation dropdown.
// It answered "is this dropdown item live", and FOUR readers went through it:
// the painter (which drew the greyed inks and no hover or press face), the
// popup's press claim, its hover recompute — which resolved a greyed row to NO
// ITEM, covering both faces with one line — and its release body's derive.
// Geometry was deliberately outside it: an item kept its row, its rect and its
// place in the layout whether it greyed or not (kdenlive's disabled rows do), so
// dropdown_item_at stayed the one geometric answer and this was the one
// enablement answer, asked beside it.
//
// IT HAD EXACTLY ONE PRODUCER FOR ITS WHOLE LIFE (architect 2026-08-08): the
// Navigation menu's "Walk both tabs" row while the `h` history view stood, where
// Ctrl+Shift+Tab was then the mode's own reverse walk-source cycle rather than
// the walk the label promised (the chord is that walk in the view too since
// 2026-08-18, marching the diff flags). The SETTINGS menu never had one (it does not open in that
// view — its anchor is refused at toggle_dropdown — and outside it its six items
// keep the never-grey rule, their own refusals answering) and neither did FILE
// (its one row is Ctrl+Q, admitted everywhere the menu can be opened, the
// history view included). So deleting the Navigation menu left the predicate
// answering an unconditional true at four sites, and the producer-less rule
// applies rather than a defensive keep: three readers dropped a term and the
// hover walk dropped its resolve-to-no-item line.
//
// THE RULING AND ITS ARGUMENT ARE KEPT AT kFilePopupItems, which owns the
// command menus' never-grey rule and now states it as COMPLETE rather than as
// having lost a clause. It also states what would bring this back: an item whose
// LABEL would lie in some mode — not an item that merely cannot act, which is
// what the never-grey rule is FOR. The two sampled disabled inks went with it,
// producer-less by the same grep (render.h's palette block carries their record
// and their derivations, both of which stay useful the day a menu greys again).)

// WOULD THIS BUTTON'S ACT BE CONSUMED BY THE `h` HISTORY VIEW? True for exactly
// the buttons the view refuses, false for the ones that still work in it.
// DERIVED FROM THE GATES, never hand-listed — the definition (input_pointer.cpp,
// beside the chord table it walks) asks history_mode_key_blocked about each
// button's own chord and hand-answers the THREE ANCHORS, which have none —
// Settings dead on the toggle_dropdown lockout, EDIT dead beside it since
// 2026-08-20 (every one of its five propagate rows is a chord the mode drops,
// so its menu would open onto nothing), File live since 2026-08-13, its
// menu opening in the view — and IT CARRIES THE AUTHORITATIVE PARTITION
// INVENTORY. (It hand-answered a fourth, Navigation, live from 2026-08-08 until
// that anchor's deletion 2026-08-15.) Read
// only while the mode stands (the caller below tests that), so it says nothing
// about any other state.
//
// IT TAKES THE WHOLE AppState because the gate it asks does: FOUR of that
// gate's admissions are conditional on state (re-derived 2026-08-09 — the commit
// act's, on head_delta_empty and on history_checkpoint_in_flight; the revert
// act's, on history_mode_revert_subject_standing above; and the load-in-place's,
// on the active walk carrying a member), so both readers must
// hand it the SAME state or the face
// and the key would answer differently. The caller passes `a` and
// restates none of its terms.
bool history_mode_disables_button(const AppState& app, RedesignButton b);

// (THE MODE-COLLAPSING ROSTER'S PREDICATE — redesign_button_collapsed, which
// answered "does the icon row's walk SKIP this button" — IS DELETED, architect
// 2026-08-14: "no more hiding/showing icons in top icon row". It carried two
// levels, the four history mode-companions at rest and the wholly-consumed
// groups right of the history opener inside the `h` view, and both are gone:
// the companions LEFT the row for the bottom one and everything the view
// refuses in this row now wears the ordinary dead face. (They came BACK on
// 2026-08-18 and wear that same dead face at rest — their arm is at
// redesign_button_enabled — which is the rule working rather than the
// collapse returning.) THERE IS NO HIDING LEFT IN THE PRODUCT AT ALL since
// that day: the bottom row's cluster SWAP was the last of it, and the modal's
// yield — where the row's tenants publish zero rects — is the one remaining
// site of that shape.)

// WOULD A PLAYBACK LAUNCH FROM `launch_pos` BE PLAYABLE? The launch body's
// refusal set, hoisted whole (2026-08-15). IT HAS ONE READER AND MUST NOT GAIN
// A SECOND OF THE FACE KIND: GuiPlaybackLifecycle::launch_playback_from, whose
// every silent refusal this IS and which CALLS IT — so this is a real
// producer's own predicate, not a producer-less leftover, and it stays exactly
// as it is. It was hoisted for a SECOND reader, the bottom row's PLAY button
// (redesign_button_enabled), which asked it about the resting cursor for the
// day the whole-row honesty ruling stood; the architect reversed that ruling
// the same day ("there's not a whole lot of value derived from the icon faces
// changing... the user is expected to know that with the playhead outside trim
// it's not going to play in target view") and only the FACE read went, the
// shape tempo_cent_step_actionable had taken hours earlier. DO NOT RE-ADD A
// FACE READER HERE without a new ruling: the row is always-on now apart from
// the `h` view's derived partition.
//
// TARGET VIEW refuses on three counts: no successful target render has
// populated the buffer yet (the check must live in the shared body — the scrub
// launch reaches it with no outer gate); a launch at or past
// `domain_end() - 1`, the two-frame remainder gate (a remainder of one frame
// is an isolated impulse, the audible pop — the rationale's full statement is
// the source arm's below); and a launch below `domain_begin()`. The bounds are
// THE BOUND BUFFER'S OWN, deliberately not app.trim: the preview buffer
// embodies the trim AT ITS RENDER, so during an in-flight re-render the bound
// domain is the truth the audio would actually play — asking the buffer is
// what makes the refusal agree with what is bound rather than with what was
// last asked for.
//
// SOURCE VIEW plays to the SONG's end (architect 2026-08-05 — the trim window
// does not bound source playback; Viewport::trim_range is the navigation range
// owner only), so the ONE refusal is the two-frame remainder gate against the
// song end: a launch from `total - 1` (End's landing spot) or past it no-ops
// silently. There is no lower gate but the domain's own — every producer hands
// in a clamped non-negative position, and play() floors a start below zero
// regardless. `launch_pos` is an already-formed int64 at its reader
// (toggle_playback's overflow-ordered pre-sum gate refuses before an undefined
// cursor + offset sum could reach the launch body), so these absolute compares
// need no overflow ordering of their own.
inline bool playback_launch_playable(const AppState& a,
                                     const GuiPlayback& playback,
                                     int64_t total_frames,
                                     int64_t launch_pos) {
    if (a.active_audio_view == 'T') {
        if (a.target_buffer_frames <= 0) return false;
        return launch_pos >= playback.domain_begin() &&
               launch_pos < playback.domain_end() - 1;
    }
    return launch_pos < total_frames - 1;
}

// THE REDESIGNED BUTTONS' ENABLED PREDICATE — one owner for the DISABLED FACE
// (row 2's third face, and every row's while the history view stands) and for
// hoverability. Three readers: the painter (which stashes what it painted), the
// press claim (a disabled press is a consumed nothing — the chord is not
// dispatched), and main.cpp's staleness comparator.
//
// THE POLICY, AT ITS RULED SHAPE (architect 2026-08-15, settling a question
// that moved three times that day): THE ROSTER TELLS THE TRUTH EXACTLY WHERE
// THE SCREEN DOES NOT ALREADY ANSWER THE QUESTION, and stays live-faced
// everywhere else — INCLUDING where a refusal is real. Three things are
// truthful and they are the whole list:
//   * the SAVE / UNDO / REDO group, the buttons he consults for truth;
//   * the `h` HISTORY VIEW'S partition — a MODE statement, derived;
//   * the per-tab READ-ONLY lock — the other MODE statement, new that day.
// EVERYTHING ELSE STAYS LIT, THE WHOLE BOTTOM ROW INCLUDED since that day's
// last ruling on it, in the architect's own words: "there's not a whole lot of
// value derived from the icon faces changing, and it is a little distracting.
// The whole premise of the GUI is that it expects strict user knowledge — the
// user is expected to know that with the playhead outside trim it's not going
// to play in target view." So the row's PLAY / STOP pair — the last arm the
// whole-row honesty ruling still had — answers plain `true` like the rest of
// it, and the `h` view's derived partition is the row's only grey (Play and
// Stop dead in there because Space is consumed, the two SKIPS lit because
// Home/End are the mode's own vocabulary — architect-confirmed, the record at
// their block below). The two MODE statements are what the pattern is
// built on and they are not interaction-cadence facts: a mode is entered
// deliberately, is invisible chrome state otherwise, and does not flicker —
// read-only changes only when `o` is pressed.
//
// HOW IT GOT HERE, kept because the reversals are the argument. The row
// scopes were the original rule ("do not invent refusal-predicting grey
// states"), overridden FOR THE BOTTOM ROW ALONE hours earlier that day when the
// architect ruled the whole transport row honest — AND THAT OVERRIDE IS ITSELF
// REVERSED NOW, in three steps, so the original rule stands over the bottom row
// again with no exception but the `h` view's derived partition; a
// FULL-TRUTHFULNESS EXPERIMENT for the icon row followed and was REVERSED
// UNBUILT once the cost
// exceptions were laid out — FOUR refusal classes cannot be computed per frame
// on a row that repaints on every hover (a per-frame resolver plus a frame-map
// build, an uncached directory tree walk, worker state the painter cannot
// reach, and two O(n) scans). THAT IS THE WHOLE REASONING: total truthfulness
// is UNREACHABLE here, so the choice was never truth-versus-policy but a
// SCOPED rule versus a rule with silent holes — and a rule with silent holes is
// worse, because it teaches the user that a lit button means something and then
// lies about the four cases nobody can see the seam of. The scoped rule is
// stated in one sentence and is exactly true.
//
// AND WHAT THE SCOPE DELIBERATELY EXCLUDES is the per-INTERACTION fact, which
// the architect had in front of him rather than in the abstract: the bottom
// row's UP and DOWN arrows nudge tempo in the WARP marker view and cannot act
// at all in the PHASE RESET one, so with the honest arm in place "every time I
// selected a marker, that pair would blink in and out, and it would be
// distracting". That is why the bottom row's RIGHT cluster — the four cardinal
// arrows, and the four history companions that supplanted them inside the view
// until the 2026-08-18 relayout —
// went back to a plain `true` the same day, giving up the selection-reading
// arms the whole-row honesty ruling had just given them. THE TWO SKIPS WENT
// BACK WITH THEM (architect 2026-08-15) on a DIFFERENT reason, stated in full
// at their arm below: their honest arm rested on a false premise — that bare
// Home / End are pure jumps — when in fact both also stop a live audition,
// clear the marker selection and hide the trim region overlay UNCONDITIONALLY, so
// a greyed skip promised LESS than its key delivers. AND PLAY / STOP WENT LAST,
// on the reason quoted at the top of this comment: the faces changing bought
// little and distracted, and the product expects strict user knowledge — the
// user knows a playhead outside the trim will not play in target view, so the
// glyph need not say it. THE WALLS ARE THE OTHER
// STANDING EXCLUSION and always were: a step into a wall is a consumed no-op by
// key and by click alike, and no button in this roster has ever mirrored one.
//
// WHAT EACH ENTRY MIRRORS, read off the routes themselves:
//   * ALL FOUR toolbar chords (Save / Undo / Redo / Render — icon-row members
//     since the 2026-08-12 relayout deleted their labeled row, the same
//     machinery under a glyph face) drop at on_key's
//     `app.loading || total <= 0` guard
//     (input_handler.cpp). THE PER-TAB READ-ONLY GATE NOW SPLITS THEM (architect
//     2026-08-07): read-only protects the AUTHORED MUSICAL CONTENT — the marker
//     stores and the engine settings — so its allowlist
//     (read_only_key_blocked, input_key_dispatch.cpp) admits Ctrl+S and
//     Ctrl+Alt+R (a save writes the state the tab already holds; a render reads
//     it) while still dropping Ctrl+Z and Ctrl+Shift+Z. So among these four a
//     locked tab greys UNDO AND REDO ALONE and leaves Save and Render live,
//     which is the truth the keys have — the term lives in the per-button
//     switch below rather than as a blanket line, because it is no longer a
//     blanket fact. (It was one until this ruling, when "a locked tab greys the
//     whole toolbar" was recorded here as code truth.) THE LOCK REACHES EIGHT
//     MORE BUTTONS since 2026-08-15 — the authoring chords it blocks that
//     still have a face to grey, spread across the icon row and the bottom one
//     since the 2026-08-18 relayout — and its own entry is below.
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
//   * THE READ-ONLY MODE STATEMENT (architect 2026-08-15): the buttons the
//     per-tab lock BLOCKS wear the dead face while it stands, so
//     the toggle makes the buttons it eats look the way the `h` view already
//     makes its own consumed buttons look. Their term is
//     `!active_view_state(a).read_only` AND NOTHING ELSE — no selection term,
//     no view term, no wall term — because this is a MODE statement and not the
//     truthfulness sweep: any interaction-cadence term added here would
//     reintroduce the blink the same ruling removed from the arrows. THE
//     MEMBERSHIP'S OWNER IS THE READ-ONLY ARM of the switch below, and its
//     members are chords read_only_key_blocked (input_key_dispatch.cpp) drops:
//     bare `s`, Delete, Ctrl+D, Ctrl+N, bare `m`, bare `i`, bare `l` and bare
//     `'` — EIGHT as of 2026-08-20. IT IS HAND-LISTED RATHER THAN DERIVED, and
//     the reason is that the allowlist alone is not the truth: some of its
//     answers do not survive the walk (below), and TWO CHANGES THE SAME DAY
//     made the point twice over. Ctrl+P and Ctrl+Alt+P left this list with
//     their BUTTONS (the Edit-menu relocation) though the lock still eats the
//     chords — blocked, with no face to grey. And THE MEASURE (bare `/`) left
//     it while its chord stayed blocked: its SHIFT half is the lock-legal
//     score-video jump and a chrome face cannot split, so the button is LIT on
//     a locked tab and the gate refuses the plain click, the Edit menu's own
//     shape. The mirror is a membership, never an equivalence, in both
//     directions.
//   * THE READ-ONLY-LEGAL BUTTONS ARE DELIBERATELY NOT GREYED — Save, Render,
//     the TRIM REGION toggle (2026-08-16 —
//     it writes no trim at all, only the overlay's visibility bit and then the
//     viewport), the S/T and
//     W/P radios, the zoom four, follow and the
//     read-only toggle, each one an allowlist entry in read_only_key_blocked.
//     (THE TRIM
//     SCISSORS were on this list, bare `x` being read-only-legal like every
//     trim gesture, until their button was deleted on 2026-08-18; the KEY is
//     still admitted and still on this button, which took it over the same
//     day, and so is its shifted twin.) Greying them would make
//     the face promise LESS than the key delivers, which is the 2026-08-07 band
//     ruling's own line: read-only protects the AUTHORED MUSICAL CONTENT and
//     nothing else.
//   * THE HISTORY FAMILY IS NOT ON THAT LIST and never was read-only-legal,
//     which is worth stating because the faces can look alike: none of its
//     chords is admitted by the allowlist. Bare `h`, `g`, `u`, `,` and `.` are
//     blocked VACUOUSLY — nothing the lock protects is behind any of them
//     (class (1) below) — and Ctrl+H the lock really does eat. The OPENER is
//     lit anyway on its own arm's terms; the two WALK RADIOS and the four
//     COMPANIONS are greyed at rest all the same, by `a.history_mode.active`
//     at their own arm below, which is a MODE fact and not the lock's.
//   * WHY THE READ-ONLY SET IS NOT DERIVED BY WALKING kToolbarChords THROUGH
//     read_only_key_blocked, the way history_mode_disables_button walks it
//     through the mode's gates — checked and rejected 2026-08-15, and recorded
//     so it is not tried again as an obvious cleanup. The walk diverges on the
//     buttons below, in two classes, and no total is stated for either — the
//     set moved twice on 2026-08-20 alone and both classes name their members
//     right here. (1) THE HISTORY FAMILY'S CHORDS ARE BLOCKED VACUOUSLY: bare
//     `h`, bare `g` (which BOTH walk radios carry), bare `u`, bare `,` and
//     bare `.` sit on no allowlist entry, so the walk reads a grey for six
//     buttons the lock has no say over. Bare `h` never reaches the gate at all
//     — handle_history_mode_key claims the toggle and returns from on_key
//     ABOVE it — while the other four DO reach it on every press outside the
//     view and bind nothing below it (handle_history_mode_key returns false
//     while the mode is down), so their "blocked" refuses nothing either way.
//     Knowing that means knowing on_key's dispatch ORDER, which no table
//     holds. (2) THE REST ARE
//     THE RULING'S OWN EXCLUSIONS: the four cardinal arrows (Up and Down are
//     blocked outright, Left and Right blocked only while a selection stands —
//     the exact per-selection blink this ruling removes), Revert, whose Ctrl+H
//     the lock really does eat but whose face the companions' own arm decides,
//     and since 2026-08-20 THE MEASURE, whose base chord the lock eats while
//     its shifted twin is admitted — one key, two answers, and a face that can
//     only give one. A derivation would therefore need an override list on top
//     of it, which is strictly worse than a list that says what it means and
//     names its owner.
//   * Row 1's three anchors and row 3's tabs answer true HERE: row 1 keeps its
//     two faces by ruling, and a tab has no disabled face of its own. Their
//     entries exist
//     so the vector is total over the roster and the comparator needs no
//     membership test. (The tabs answer true in EVERY state since 2026-08-05,
//     and the REASON changed on 2026-08-18 while the answer did not: the `h`
//     view greyed them for one day, then repurposed the row as its walk
//     selector — and now the walk has its own radio pair in row 4, so the tabs
//     are ordinary A/B tabs in there and their Ctrl+Tab is on the mode's own
//     ALLOWLIST, which is what makes the derived partition call them LIVE. The
//     mode line at the top of this body never fires for them either way, and
//     row 3 has no disabled face at all.)
// MODAL gates are deliberately absent: a prompt or a dialog editor
// swallows the PRESS at the pointer path's own veil, and a modal that
// greyed the chrome under it would be a fourth face nobody asked for (the
// HOVER faces do go dark under the veil — the hover walk's veil term,
// recompute_redesign_button_hover — but that is the pointer's fact, not a
// face state this predicate answers).
// THE FIRST SWITCH IS EXHAUSTIVE over the roster with NO `default` arm,
// deliberately: a new button then fails to compile here (-Wswitch) until it is
// classified, instead of silently inheriting some other button's answer. The
// second switch can take a `default` because the first has already returned for
// every id whose answer does not sit below the loading/blank guard: the
// toolbar four (Save / Undo / Redo / Render — icon-row members since the
// 2026-08-12 relayout, keeping their mirrored derivations), THE SET THE
// READ-ONLY LOCK GREYS (its membership is the read-only arm of the switch
// below, which owns it and is not counted here — it moved twice on 2026-08-20,
// losing the propagate pair with their buttons and the MEASURE to its
// lock-legal shift half), and, since 2026-08-15, THE BOTTOM ROW'S ALWAYS-ON
// MEMBERS (every chord on that row drops at on_key's loading/blank return, so
// their faces grey there too — and that guard is now the ONLY thing they all
// have to say; the set is the one that breaks out of the switch below, which
// owns it and is not counted here either).
// TWO PARAMETERS CAME AND WENT ON 2026-08-15 and the pattern is worth stating
// once, because it is the same one twice: a face arm was added, the object it
// needed was threaded in for it, the architect reversed the arm, and THE
// PARAMETER LEFT WITH ITS ONE PRODUCER RATHER THAN RESTING UNREAD.
//   * GuiAudio was the skips' — they called playhead_skip_landing_frame, which
//     needs the object rather than the frame count (this header only
//     FORWARD-DECLARES GuiAudio) — and left when the skips went back to a
//     plain `true`. The frame count is the only audio fact this predicate ever
//     wanted.
//   * GuiPlayback was PLAY'S — it read the bound preview buffer's domain
//     through playback_launch_playable, state that deliberately lives on
//     GuiPlayback rather than AppState (the domain anchor travels with the
//     playback bind; the record is at AppState::target_buffer_frames) — and
//     left with the pair's enabled split. publish_button_face
//     (paint_handler.cpp) gave up its own GuiPlayback parameter in the same
//     move, this having been its only reader.
inline bool redesign_button_enabled(const AppState& a,
                                    int64_t total_frames,
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
    //
    // ITS ONE EXCEPTION IS THE HISTORY COMPANIONS (architect 2026-08-15), and
    // it is scoped to what happens INSIDE the view: three of the four the
    // partition already answers LIVE for free (the mode owns bare `u`, `,` and
    // `.`), so the exception exists for REVERT alone, whose Ctrl+H the mode
    // admits only while a diff flag stands — a per-selection blink, which the
    // architect reversed as a face while leaving the chord's refusal exactly as
    // it was. The membership is
    // named once (redesign_button_is_history_companion) rather than spelled
    // here, and it is the cluster rather than Revert because the ruling is
    // about the cluster.
    //
    // THE FOUR ARE STILL GREY OUTSIDE THE VIEW and this line is not what says
    // so (2026-08-18, with their return to the icon row): this whole test is
    // inside `a.history_mode.active`, so it has no opinion about a button out
    // there at all. Their own arm below carries that answer and states why it
    // owns it.
    if (a.history_mode.active &&
        !redesign_button_is_history_companion(b) &&
        history_mode_disables_button(a, b)) {
        return false;
    }
    switch (b) {
        // Rows 1, 3 and 4 have NO DISABLED FACE OF THEIR OWN — row 4 by the
        // architect's design (he provided five states and no disabled one), rows
        // 1 and 3 by their face scope. (ROW 4 HAS FOUR EXCEPTIONS AGAIN SINCE
        // 2026-08-18 — the HISTORY COMPANIONS, whose keys are bound only inside
        // the `h` view and which grey at rest for that reason. They held the
        // same arm from 2026-08-05 (2026-08-08 for the Cumulative toggle) until
        // 2026-08-14, when they left this row for the BOTTOM one and became the
        // arrows' mode twin; the arm went plain-true on 2026-08-15 because down
        // there they were not painted outside the view at all. The relayout
        // brought them back to a row that hides nothing, and the arm with them.
        // The whole succession is at their own arm below.)
        // Their presses
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
        // there either. (EDIT joined the row and this arm 2026-08-20: an anchor
        // is a popup toggle and has nothing to refuse, so it is never-grey here
        // like its two siblings. Its `h`-view grey is the derived partition's,
        // hand-named at history_mode_disables_button.)
        case RedesignButton::File:
        case RedesignButton::Edit:
        case RedesignButton::Settings:
        case RedesignButton::ViewSW:
        case RedesignButton::ViewTP:
        case RedesignButton::ViewTW:
        case RedesignButton::TabA:
        case RedesignButton::TabB:
        case RedesignButton::IconS:
        case RedesignButton::IconT:
        case RedesignButton::IconW:
        case RedesignButton::IconP:
        // THE TRIM REGION TOGGLE MIRRORS NOTHING (2026-08-16, unchanged when
        // it became a toggle on 2026-08-18), and for a stronger reason than
        // the trim scissors it outlived: it HAS no refusal to mirror. Its one
        // act — show the overlay and frame it, or hide it — is always
        // meaningful on a loaded piece, and the case that would tempt a face
        // (an overlay already fully in view) is a harmless nothing rather than
        // a refusal, the framing owner's first arm simply writing no viewport.
        // The `h` view greys it through the derived partition above, RE-DERIVED
        // 2026-08-18 against the chord's repointing and unchanged by it: bare
        // `x` is neither history_mode_owns_key's own vocabulary nor on
        // history_mode_key_blocked's allowlist, so the mode consumes it and the
        // partition finds nothing to keep the face live. Nothing hand-listed —
        // which is also where trim's freeze in that view is expressed for this
        // button.
        case RedesignButton::IconShowRegion:
        // THE ZOOM GROUP MIRRORS NOTHING (2026-08-12): four navigation chords
        // that always mean something on a loaded file, and the loading/blank
        // guard is the family's shared answer below. LIVE in the `h` view —
        // the derived partition finds all four on the mode's allowlist or its
        // own vocabulary.
        case RedesignButton::IconZoomIn:
        case RedesignButton::IconZoomOut:
        case RedesignButton::IconZoomFitBest:
        case RedesignButton::IconZoomOriginal:
        // FOLLOW MIRRORS NOTHING: bare `f` toggles the chase in either
        // direction on any loaded piece, and the lock admits it (follow is
        // navigation, not authored content). Its lamp reports the state.
        case RedesignButton::IconFollow:
        // THE READ-ONLY TOGGLE MIRRORS NOTHING (2026-08-14): bare `o` is
        // always meaningful on a loaded piece — it locks a writable tab and
        // unlocks a locked one — so there is nothing to grey for. Its lamp,
        // not its enabled bit, is what reports the state
        // (redesign_button_selected below). The `h` view greys it through the
        // derived partition above, `o` being consumed in there.
        case RedesignButton::IconReadOnly:
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
        // THE READ-ONLY MODE STATEMENT (architect 2026-08-15) — the roster's
        // one grey that is not the `h` view's: EVERY ARM BELOW IS A CHORD
        // read_only_key_blocked (input_key_dispatch.cpp) drops on a locked tab,
        // so the lock LOOKS the way the history view already looks, which is
        // what the architect asked for.
        //
        // THE MIRROR IS A MEMBERSHIP AND NOT AN EQUIVALENCE, and 2026-08-20
        // gave it a member on each side, which is why no count is stated here.
        // The PROPAGATE PAIR (Ctrl+P, Ctrl+Alt+P) is still blocked by the gate
        // and is no longer here, its two buttons having left with the Edit-menu
        // relocation: blocked, with no face left to grey. THE MEASURE (bare
        // `/`) is also still blocked by the gate and is no longer here either,
        // for the opposite reason: its SHIFT half is the lock-legal score-video
        // jump, a chrome face cannot split, so the button stays LIT and the
        // gate refuses the plain click exactly as it refuses the plain key (the
        // ruling is at that button's own arm further down).
        //
        // IT IS NO LONGER ONE ROW'S: since the
        // 2026-08-18 relayout the four MARKER VERBS are the BOTTOM row's — and
        // they carry this term with them: a button's gates are the BUTTON's,
        // and the bottom row's always-on policy is what its other members take,
        // not a property of the lane. THE TERM IS THE BIT AND NOTHING
        // ELSE, deliberately: every one of these chords has further refusals of
        // its own (home view, an empty selection, an occupied frame, an empty
        // clipboard) and NONE of them is mirrored — those stay consumed no-ops
        // with a live face, because they change at interaction cadence and the
        // ruling at the head of this body is that a blinking glyph restates
        // what the screen already shows. Read-only is the opposite kind of
        // fact: a mode entered on purpose, invisible chrome state otherwise,
        // changing only when `o` is pressed.
        //
        // WHY THEY SIT ABOVE THE LOADING/BLANK GUARD rather than below it with
        // the other mirrored arms: this arm adds ONE term to what they already
        // answered and must add no second one. Dropping them past the guard
        // would also grey them during a load — arguably truthful, since their
        // chords drop at on_key's own loading return, but it is a change this
        // ruling did not make and would be the row's third policy.
        //
        // THE MEMBERSHIP IS HAND-LISTED AND ITS OWNER IS NAMED, not derived —
        // the walk that would derive it, and the buttons it gets wrong,
        // are recorded at the head of this body. A change to the allowlist must
        // be reflected here by hand; that is the accepted cost of the two
        // classes the walk cannot see.
        case RedesignButton::IconMarkerDrop:
        case RedesignButton::IconMarkerDelete:
        case RedesignButton::IconMarkerDisable:
        case RedesignButton::IconMarkerInherit:
        case RedesignButton::IconBpm:
        case RedesignButton::IconIter:
        case RedesignButton::IconListen:
        case RedesignButton::IconLoadInPlace:
            return !active_view_state(a).read_only;
        // THE BOTTOM ROW IS ALWAYS-ON EXCEPT FOR ITS OWN TWO GREYS (architect
        // 2026-08-15, his final ruling on this row after it moved three times
        // that day): its ALWAYS-ON MEMBERS break out of this switch
        // to take the loading/blank guard — every chord on the row drops at
        // on_key's `app.loading || total <= 0` return — and then answer a plain
        // `true`. HIS REASONING, kept in his own words because it is the whole
        // argument: "there's not a whole lot of value derived from the icon
        // faces changing, and it is a little distracting. The whole premise of
        // the GUI is that it expects strict user knowledge — the user is
        // expected to know that with the playhead outside trim it's not going
        // to play in target view."
        //
        // THIS SUPERSEDES THE WHOLE-ROW HONESTY RULING OF THAT MORNING ("the
        // ENTIRE ROW should be accurate to the live state and not lie by
        // showing something enabled that's not enabled"), which was narrowed
        // three times and then reversed outright — first off the left cluster,
        // then off the two SKIPS, and finally off PLAY and STOP, the last arms
        // standing. So the row scopes' original rule — do not invent
        // refusal-predicting grey states — is back in force here unbroken, and
        // the head of this body carries what the roster still does tell the
        // truth about.
        //
        // WHAT THE ROW ACTUALLY GREYS, re-derived since 2026-08-18 rather than
        // stated as "nothing but Play/Stop" (which was true while its roster
        // was ten and half of it was unpainted in the view). IN THE `h` VIEW,
        // all through the DERIVED partition at the top of this body: the
        // PLAY/STOP button (Space is consumed there), THE FOUR CARDINAL ARROWS
        // (bare Up/Down/Left/Right are neither the mode's vocabulary nor on its
        // allowlist — an answer that reached no pixel while the cluster swap
        // hid them, and reaches one now that they paint in every state), THE
        // FOUR SINGLE-MARKER VERBS, THE MARKER MEASURE and ADD TO SELECTION
        // (bare `k` and bare `/` are consumed in there like the verbs' four
        // chords) — ELEVEN of the sixteen. The two
        // SKIPS and the MARKER-WALK GROUP'S THREE stay lit, being the mode's
        // own absolute jumps, its diff-flag cycle and (since 2026-08-18) the
        // march that composes that cycle with the A/B switch.
        // OUTSIDE THE VIEW: the four VERBS on a locked tab, their own gate,
        // carried down from the icon row and stated at their arm above — and
        // NEITHER of the two buttons seated behind them is with them there:
        // ADD TO SELECTION because its chord is navigation, and THE MARKER
        // MEASURE because its SHIFT half is (architect 2026-08-20; the ruling
        // is at its own arm below).
        // Nothing else on the row has a resting grey, which is what this ruling
        // is about — the set that breaks out below.
        //
        // THE `h` HISTORY VIEW'S PARTITION AT THE TOP OF THIS BODY STILL
        // OUTRANKS EVERY WORD OF THIS, and the architect confirmed its split
        // explicitly — "making play and stop disabled in h history view, but
        // allowing home and end, that makes sense": Space is consumed in the
        // view, so THE PLAY/STOP BUTTON wears the dead face in there, while
        // Home/End are the mode's own vocabulary (the absolute jump to 0 / the
        // last frame), so the two skips stay LIT exactly as they are outside
        // it — as do the MARKER-WALK THREE, whose chords are the mode's
        // vocabulary too. All derived from the mode's gates, nothing
        // hand-listed — and it is why the row's always-on members break
        // DOWNWARD to a shared `true` instead of returning one here.
        //
        // PLAY AND STOP WERE THE ROW'S LAST TRUTHFUL ARMS, and what they lost
        // is the ENABLED split alone: no grey while an audition runs, none
        // where a launch would refuse, none while no audition runs. The
        // strict-user-knowledge line settles the TRUTH half of it: a running
        // audition is the moving scanner's own statement on screen.
        //
        // WHAT THE SPLIT ALSO CARRIED IS NOW MOOT RATHER THAN RELOCATED. The
        // enabled bit had been the PAIR's DISAMBIGUATION as well as its truth
        // face — only the meaningful half was ever clickable — so with both
        // halves live a press on Stop while stopped would have started
        // playback; the architect made the pair a RADIO for it, putting that
        // state on the SELECTED axis, and then COLLAPSED THE PAIR INTO ONE
        // BUTTON later the same day, which removes the wrong half entirely.
        // The radio flag and the lamp went with it. Nothing here reads the
        // audition bit and nothing does on the selected axis either: the bit
        // now picks the button's GLYPH and its TOOLTIP
        // (redesign_button_glyph_swapped, below), which is Render-is-Cancel's
        // own shape.
        //
        // playback_launch_playable SURVIVES AND MUST NOT GAIN A FACE READER —
        // it is launch_playback_from's own refusal set and that body calls it
        // (playback_lifecycle.cpp), so it is not producer-less; only the FACE
        // read went, the same shape tempo_cent_step_actionable kept hours
        // earlier when the arrows were reversed.
        //
        // THE ONE FACE-RELATED FIX FROM THIS ARC THAT STAYS is
        // publish_button_face's CLIP-COVERAGE TEST (paint_handler.cpp): the
        // stash means "last PAINTED", not "last computed", so a button whose
        // drawing the damage clip discarded must not refresh its cached face.
        // That is not a policy choice and is no part of this reversal — it is
        // what makes any face update at all reliable, and reverting it with the
        // policy would bring back the stale-row bug the arc opened with.
        //
        // THE FOUR CARDINAL ARROWS WERE REVERSED FIRST, hours earlier: they
        // had honest arms for one evening — the marker-lane fork and the cent
        // step's leading refusals — and the architect took them back the same
        // day. HIS REASONING, which is per-pair and worth keeping in his own
        // terms:
        //   * LEFT / RIGHT were always-on for a REASON rather than by omission
        //     — "left and right were always on because the playhead can always
        //     move". The horizontal step almost always acts, so mirroring the
        //     marker-lane nudge's refusal on top of it made a nearly-always-
        //     live pair start blinking per selection: a large behavioural cost
        //     for a rarely-true refusal.
        //   * UP / DOWN are the pair he actually watched blink, and they are
        //     the concrete case this reversal exists for — "in warp markers
        //     they can nudge the tempo up and down, but in phase resets they
        //     can't; in warp markers, every time I selected a marker, that pair
        //     would blink in and out, and it would be distracting". One pair,
        //     toggling on every selection in the W view and dead outright in
        //     the P view.
        // THE WALL IS DELIBERATELY NOT MIRRORED, and that is consistent rather
        // than a gap left to finish: at frame 0 the left step genuinely cannot
        // move — "although I suppose at zero, it can't move further left" — and
        // the button stays lit anyway, on the walk arrows' own standing
        // precedent (a wall step is a consumed no-op by key and by click
        // alike). Walls are the one refusal class this roster has never
        // mirrored anywhere; do not "complete" this arm with a wall term.
        // Their chords refuse exactly as they did; a click on a refusing arrow
        // is a consumed no-op, the roster's standing shape.
        //
        // THEY ARE PAINTED IN THE `h` VIEW SINCE 2026-08-18 — the cluster swap
        // that replaced them with the history companions went when those four
        // returned to the icon row — AND THEY WEAR THE DEAD FACE IN THERE,
        // which is a real face this arm never had to produce before. It is the
        // DERIVED partition's answer and nothing hand-listed: bare Up / Down /
        // Left / Right are neither the mode's own vocabulary
        // (history_mode_owns_key) nor on its allowlist
        // (history_mode_key_blocked), so the view consumes all four and greys
        // them exactly as it greys every other button whose act it consumes.
        // Truthful, and it costs no term here: the answer comes from the line
        // at the top of this body, which the arrows fall under like everything
        // else. (Under the swap it reached no pixel, which is why the arms
        // above discuss their RESTING face alone.)
        //
        // THE TWO SKIPS (bare Home / End) ARE UNTRUTHFUL TOO AND DELIBERATELY
        // SO (architect 2026-08-15), and they fall through to the same plain
        // `return true`. They had an honest arm for one revision — greying
        // where the playhead already rested on the frame the jump would land
        // on, compared against playhead_skip_landing_frame — and THAT ARM WAS
        // BUILT ON A FALSE PREMISE: bare Home / End are not pure jumps. Both
        // also STOP A LIVE AUDITION, CLEAR THE MARKER SELECTION (the marker
        // lane's exit repair) and HIDE THE TRIM REGION OVERLAY, unconditionally
        // and even when the jump itself moves nothing (the two live arms and
        // the `h` arm, input_key_dispatch.cpp). So a greyed skip made the FACE
        // PROMISE LESS THAN THE KEY DELIVERS — with the cursor parked on a trim
        // bound the key still stopped the audition, cleared the selection and
        // hid the overlay while the dead button could do none of it, which
        // is the exact drift the 2026-08-07 read-only band ruling names and the
        // one thing this predicate exists to prevent.
        // THE ARCHITECT'S OWN SECOND REASON, the one that generalizes: whether
        // the playhead sits on a trim bound is VISIBLE ON SCREEN, and the
        // roster only mirrors what the screen does not already answer — the
        // scoped rule at the head of this body, applied.
        // DO NOT RE-ADD A FACE TERM HERE. Any landing compare put back on these
        // two reintroduces exactly that divergence, and gating the ACT on a
        // no-op jump to make the face true instead would change behaviour,
        // which this predicate never does. playhead_skip_landing_frame SURVIVES
        // for the ACT's sake alone — three dispatch arms sharing one arithmetic
        // — and has no face reader by ruling; its own header says so.
        case RedesignButton::TransportSkipBack:
        case RedesignButton::TransportSkipForward:
        case RedesignButton::TransportLeft:
        case RedesignButton::TransportRight:
        case RedesignButton::TransportUp:
        case RedesignButton::TransportDown:
        case RedesignButton::TransportPlayStop:
        // THE MARKER-WALK GROUP TAKES THE ROW'S POLICY UNCHANGED (2026-08-15):
        // always-on below the loading/blank guard, with the `h` view's derived
        // partition the only thing that could grey any of them — and it greys
        // NONE, all three chords being the mode's OWN vocabulary in there (the
        // diff-flag cycle on bare Tab / Shift+Tab, and the march over that
        // cycle on Ctrl+Shift+Tab since 2026-08-18).
        // Their refusals outside the view are the keys' own consumed no-ops (an
        // empty marker store, a cycle with nowhere to go), exactly the class
        // the ruling at the head of this body refuses to mirror.
        case RedesignButton::TransportWalkPrev:
        case RedesignButton::TransportWalkNext:
        case RedesignButton::TransportWalkBoth:
        // THE MARKER MEASURE TAKES THE ROW'S POLICY (architect 2026-08-20,
        // moving it OUT of the read-only arm above where it sat for a day) —
        // THE ONE MARKER-FAMILY BUTTON LIT UNDER THE LOCK, and it is lit for a
        // reason its four neighbours have no version of: A CHROME FACE CANNOT
        // SPLIT, and this button's two halves answer the lock differently. Its
        // plain half opens the measure editor, which a locked tab refuses; its
        // SHIFT half is the SCORE-VIDEO JUMP, which a locked tab allows —
        // navigation that reads the piece and drives another process, the
        // family Ctrl+S and the render chords are in (the carve-out is
        // read_only_key_blocked's shift-exact is_score_video entry). One face
        // has to answer for both, and greying it would take the LEGAL half away
        // to advertise the refused one — which on the glass rig means the score
        // jump is simply unreachable on a locked tab, the whole reason this
        // ruling exists.
        //
        // SO THE FACE STAYS LIT AND THE GATE ANSWERS, which is not a new shape
        // at all: it is the EDIT MENU'S exactly (its items are never greyed —
        // the menu closes and the chord refuses at its own owner), and it is
        // this row's own standing doctrine — do not invent refusal-predicting
        // grey states, the ruling at the head of this body. A plain click on a
        // locked tab dispatches bare `/` and drops at the gate like the key; a
        // shift-click or a kChromeShiftHoldMs hold dispatches Shift+`/` and
        // acts. NOTHING ABOUT THE `h` VIEW CHANGES: the derived partition still
        // greys and consumes it in there, bare `/` being neither the mode's
        // vocabulary nor on its allowlist.
        case RedesignButton::IconMarkerMeasure:
        // ADD TO SELECTION TAKES THE ROW'S POLICY TOO (2026-08-18): its chord
        // authors nothing, so the READ-ONLY arm above deliberately does not
        // carry it —
        // a selection is navigation, the same ruling that keeps the trim
        // gestures legal on a locked tab. There is no refusal to mirror
        // either: bare `k` flips a session bit on any loaded piece, in either
        // column and either audio view. The `h` VIEW is the one thing that
        // greys it, through the DERIVED partition at the top of this body
        // (bare `k` is neither history_mode_owns_key's vocabulary nor on
        // history_mode_key_blocked's allowlist, so the mode consumes it and
        // the face follows) — nothing hand-listed. Its LAMP, not its enabled
        // bit, is what reports the mode (redesign_button_selected below).
        case RedesignButton::IconAddToSelection:
            break;
        // THE FOUR HISTORY COMPANIONS GREY OUTSIDE THE `h` VIEW AND ARE LIVE
        // INSIDE IT (2026-08-18) — the ICON ROW's own settled rule, which is
        // where they live again since the roster relayout: what a mode refuses
        // simply GREYS, and their four chords (bare `u`, Ctrl+H, bare `,` and
        // bare `.`) are bound in exactly one place in the product and it is
        // inside the view.
        //
        // THE OWNER IS THIS ARM AND NOT THE DERIVED PARTITION, deliberately,
        // and the reason is that the partition CANNOT ANSWER IT: the mode line
        // at the top of this body runs only while `a.history_mode.active`, so
        // it has nothing at all to say about a button outside the view — and
        // asked inside it, history_mode_owns_key claims three of these four as
        // the mode's OWN vocabulary and answers LIVE, which is the reverse of
        // the fact wanted here. One `a.history_mode.active` term states both
        // halves at once and cannot fall out of step with itself.
        //
        // IT IS A REVIVAL, and the ruling it revives is what a reader must not
        // re-retire. The four ANSWERED PLAIN TRUE from 2026-08-15 to
        // 2026-08-18, and that was right for the row they were on: the
        // 2026-08-14 move to the BOTTOM row put them in the cardinal arrows'
        // own slots under a cluster swap, so outside the view they were not
        // painted at all — they published zero rects — and the resting answer
        // reached no pixel, which made the arm below moot rather than wrong.
        // THE ICON ROW HIDES NOTHING, so the premise is gone: they paint in
        // every state now and the face has to be honest again. The arm is the
        // one they held 2026-08-05..15 (2026-08-08 for the Cumulative toggle),
        // for this same reason, restored verbatim in effect.
        //
        // REVERT'S IN-VIEW GREY IS NOT REVIVED WITH IT and the two are separate
        // rulings. That one was the DERIVED partition's: the view admits Ctrl+H
        // only while a diff flag stands (history_mode_revert_subject_standing),
        // so one decision refused the chord and greyed the button. THE
        // ARCHITECT REVERSED IT ON 2026-08-15 FOR THE BLINK, NOT THE LOGIC —
        // that admission tracks the diff-flag SELECTION, so the glyph flipped
        // on every click in the marker lane to report a selection the lane is
        // already showing — and redesign_button_is_history_companion is what
        // still lifts the four over that partition inside the view. The chord
        // is untouched and still refuses on an empty subject; only the face
        // stays quiet.
        //
        // THEY NEVER GREYED AT THE WALK'S WALLS EITHER, and that reasoning
        // survives intact as the smaller case of the same rule: stepping past
        // the oldest or newest member is a consumed no-op by key and by click
        // alike, and a face tracking the walk index would repaint the row on
        // every step to report what the `n/N` corner readout already shows.
        //
        // THE CUMULATIVE TOGGLE'S SELECTED FACE IS UNAFFECTED and was never
        // scoped this way: the reading is a session preference
        // (AppState::history_cumulative) that outlives every visit, so the lamp
        // reports it wherever the button is painted — which now includes every
        // frame outside the view, where it composes DISABLED + SELECTED. The
        // shared face expressions already handle that pair (the note at
        // paint_button, paint_handler.cpp): the disabled blend mixes the fill
        // and the line toward the ground rather than dropping them, so the
        // reading stays readable and dimmed.
        //
        // NO LOADING TERM IS NEEDED: the view cannot stand over a blank or
        // loading piece, so `false` is already this arm's answer there.
        //
        // THE TWO WALK RADIOS TAKE THIS SAME ARM (2026-08-18), for this same
        // reason and with one difference worth naming: bare `g` IS the mode's
        // own vocabulary, so the derived partition would answer LIVE for them
        // inside the view all by itself — what it cannot answer is the RESTING
        // grey out here, the partition running only while `active`. One term
        // states both halves for all six.
        case RedesignButton::HistoryWalkGit:
        case RedesignButton::HistoryWalkSession:
        case RedesignButton::HistoryOlder:
        case RedesignButton::HistoryNewer:
        case RedesignButton::HistoryRevert:
        case RedesignButton::HistoryCumulative:
            return a.history_mode.active;
        case RedesignButton::Save:
        case RedesignButton::Undo:
        case RedesignButton::Redo:
        case RedesignButton::Render:
            break;
    }
    if (a.loading || total_frames <= 0) return false;
    // THE READ-ONLY TERM IS UNDO'S AND REDO'S ALONE among the toolbar four
    // since 2026-08-07 (it stood
    // here as a blanket line over all four until then): the gate admits Ctrl+S
    // and Ctrl+Alt+R in a locked tab, so greying their buttons would be the face
    // promising less than the key delivers — the exact drift this predicate
    // exists to prevent. It stays a mirror of the gate, one arm per chord.
    switch (b) {
        // THE BOTTOM ROW'S ELEVEN ALWAYS-ON MEMBERS HAVE NO ARM HERE AT ALL
        // since
        // 2026-08-15 — they return a plain `true` from the `default` below, and
        // the ruling, the architect's reasoning and the three reversals that
        // got there are at the first switch's transport block above. (Its four
        // MARKER VERBS are not among them: their read-only term is in the arm
        // above, carried down from the icon row with the buttons on
        // 2026-08-18.) Nothing
        // on that row is to be given a face term again without a new ruling:
        // the pattern each attempt fell into was mirroring a refusal that
        // changes at INTERACTION cadence, which makes a glyph blink to restate
        // what the screen already shows.
        //
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
            // REACHED BY EXACTLY ELEVEN IDS, re-derived after the 2026-08-18
            // roster rulings — the bottom row's ALWAYS-ON members (the
            // transport three, the marker-walk three, the four arrows and ADD
            // TO SELECTION), which break
            // out of the first switch to take the loading/blank guard and have
            // nothing else to say (each of the four pairs held an honest arm
            // for a revision or two; the ruling and the three separate reasons
            // they were taken back are at that block). The row's other five —
            // the single-marker verbs and the MARKER MEASURE — return from the
            // read-only arm above like the six icon-row members they came down
            // with. Every other id returned above, from one switch or the
            // other.
            break;
    }
    return true;
}

// THE TOGGLED-ON ("selected") FACE'S PREDICATE — row 1's three view-bar
// buttons, row 3's tabs and row 4's SIX radios and SIX toggles (the two view
// pairs and the WALK PAIR that joined them on 2026-08-18; follow, iteration,
// the TRIM REGION toggle — a toggle again since 2026-08-18, its lamp reading
// the overlay's visibility — read-only, history, and the CUMULATIVE reading,
// which came back to this row with the history group the same day). THE
// BOTTOM ROW HAS EXACTLY ONE SUBJECT — ADD TO SELECTION, which landed there
// later the same day, hours after the relayout had left the row lampless: the
// Cumulative toggle was its one from 2026-08-14 until that relayout, and the
// Play / Stop radio pair was a
// second for hours on 2026-08-15, leaving with the same day's collapse of that
// pair into one button (its record is in the momentary arm below).
// Each reads
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
    // (ROW 3'S TABS HAD A MODE OVERRIDE HERE from 2026-08-05 to 2026-08-18,
    // ranked above this switch: while the `h` view stood the row was the WALK
    // SELECTOR and the lit slot marked the live walk SOURCE rather than the
    // live tab. The walk has its own radio pair below since 2026-08-18, so the
    // tabs read app.active_tab_view in every state and the override is gone
    // with the repurposing.)
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
        // THE TRIM REGION TOGGLE'S LAMP (2026-08-18), the same pattern as the
        // two above: it reads the OVERLAY'S VISIBILITY, which is exactly the
        // bit bare `x` flips, so the lit face and the surface on screen
        // cannot drift. IT IS A TOGGLE AGAIN, where the 2026-08-16 ruling made
        // this button deliberately MOMENTARY and stateless, and the hole that
        // ruling avoided cannot occur under the new model — which is the part
        // worth keeping, because the hole is easy to re-invent. The old design
        // lit the lamp from a SPAN'S EXISTENCE, so a span scrolled offscreen
        // left the button lit with only a clearing press available and the one
        // thing the user wanted out of reach. This lamp reads VISIBILITY, and
        // the show half ALWAYS FRAMES (bring_span_into_view), so a lit button
        // means the overlay is on screen or one press from being re-shown
        // there, and the unreachable state has no way to arise.
        case RedesignButton::IconShowRegion: return a.region.shown;
        // ADD TO SELECTION IS THE BOTTOM ROW'S ONE LAMP (2026-08-18), on the
        // same toggle pattern as the two above: it reads the live bit bare `k`
        // flips, so the lit face and the sticky ctrl cannot drift. IT IS THE
        // ONLY CUE THE MODE GETS, by ruling — the flags' own brightened face
        // already says which markers are selected, so there is no badge and no
        // glyph overlay anywhere. THE LAMP CAN COMPOSE WITH THE DEAD FACE in
        // the `h` view (a mode left on, then the view opened), which the
        // shared face expressions already handle: the disabled blend mixes
        // fill and line toward the ground rather than dropping them, exactly
        // as it does for the Cumulative toggle up in row 4.
        case RedesignButton::IconAddToSelection: return a.add_to_selection;
        // The iteration button's pattern exactly: a TOGGLE reading the live bit
        // its own chord flips, so the lamp and the mode cannot drift.
        case RedesignButton::IconHistory: return a.history_mode.active;
        // THE READ-ONLY LAMP (2026-08-14), the same toggle pattern over the
        // ACTIVE TAB's own bit — which is exactly what bare `o` flips, so the
        // lamp and the lock cannot drift. It is what carries the padlock's
        // old two-state face into this row: the glyph swaps closed/open with
        // the bit (redesign_button_icon, paint_handler.cpp) and the lit fill
        // says the same thing in the row's own vocabulary, which is how
        // iteration and follow already report a mode that is ON.
        case RedesignButton::IconReadOnly:
            return active_view_state(a).read_only;
        // THE CUMULATIVE READING'S LAMP (2026-08-08), the same pattern over a
        // bit that is NOT the mode's: history_cumulative is a program-session
        // preference, so this reads true wherever the session left it.
        // Publishing it unconditionally is the point — a mode term here would
        // make the row lie about the reading the moment the view closed. SINCE
        // 2026-08-18 THAT IS VISIBLE RATHER THAN THEORETICAL: the button is
        // back in the ICON ROW, which paints every member in every state, so
        // the lamp shows the session's reading outside the view too and
        // composes with the button's own resting grey — DISABLED + SELECTED,
        // dimmed by the shared blend rather than dropped, which is exactly
        // what a true-but-not-yours-right-now state should look like. (From
        // 2026-08-14 to 2026-08-18 it lived on the bottom row's swapped
        // cluster, painted inside the `h` view alone and publishing an empty
        // rect outside it, so this lamp reached no pixel out there at all.)
        case RedesignButton::HistoryCumulative: return a.history_cumulative;
        // THE WALK RADIOS' LAMP (2026-08-18), the pair's whole face: exactly
        // one of the two is lit while the view stands, which is what makes them
        // a radio pair and what the press claim's radio consume reads. THE MODE
        // TERM IS DELIBERATE, and it is the one thing that separates them from
        // the Cumulative toggle directly above: the READING is a program-session
        // preference that outlives every visit, so its lamp publishes
        // unconditionally, while the WALK SOURCE is per-visit state reset to
        // Commit at every entry — so lighting "Git" outside the view would
        // advertise a selection that is not a live fact. Outside the view both
        // are unlit and both are dead, which is the honest pair.
        case RedesignButton::HistoryWalkGit:
            return a.history_mode.active &&
                   a.history_mode.source == GuiHistoryWalkSource::Commit;
        case RedesignButton::HistoryWalkSession:
            return a.history_mode.active &&
                   a.history_mode.source == GuiHistoryWalkSource::Local;
        case RedesignButton::File:
        case RedesignButton::Edit:
        case RedesignButton::Settings:
        case RedesignButton::Save:
        case RedesignButton::Undo:
        case RedesignButton::Redo:
        case RedesignButton::Render:
        // THE ZOOM GROUP AND THE MARKER VERBS ARE MOMENTARY like copy and
        // paste (2026-08-12): each is an act that completes — a zoom step, a
        // centering, a drop, a delete — with no state to stay lit for. The
        // verbs took that answer to the BOTTOM ROW with them on 2026-08-18 and
        // it did not change: a lamp is the BUTTON's fact, not the lane's. The
        // disable toggle deliberately carries no lamp either: it acts on the
        // SELECTION, whose members' own flags show the state per marker,
        // and a single lamp could not say which.
        case RedesignButton::IconZoomIn:
        case RedesignButton::IconZoomOut:
        case RedesignButton::IconZoomFitBest:
        case RedesignButton::IconZoomOriginal:
        case RedesignButton::IconMarkerDrop:
        case RedesignButton::IconMarkerDelete:
        case RedesignButton::IconMarkerDisable:
        case RedesignButton::IconMarkerInherit:
        // THE MARKER MEASURE IS MOMENTARY TOO: it opens an editor and the
        // editor's own session is the state; there is no bit for a lamp.
        case RedesignButton::IconMarkerMeasure:
        case RedesignButton::IconBpm:
        case RedesignButton::IconListen:
        case RedesignButton::IconLoadInPlace:
        // THE REVERT BUTTON IS MOMENTARY TOO, and more plainly than the arrows:
        // it is an ACT, not a mode — it runs once and closes the view — so
        // there is no bit for a lamp to read. IT SAYS NOTHING WITH ITS ENABLED
        // FACE EITHER since 2026-08-15: that face greyed when nothing was
        // selected, and the architect reversed it because the grey tracked the
        // diff-flag SELECTION and blinked at interaction cadence — the argument
        // that took the four cardinal arrows always-on. The chord still refuses
        // on an empty subject and the click is then a consumed no-op; the
        // record is at the history companions' arm in redesign_button_enabled.
        case RedesignButton::HistoryRevert:
        // THE WALK'S TWO STEPS ARE MOMENTARY like copy and paste, not toggles
        // like follow and iteration: each is a step that completes, with no
        // state to stay lit for. WHERE the walk stands is the corner readout's
        // `n/N`, which says it in numbers; a lit arrow could only mean "you
        // pressed this", which the click face already says for as long as it is
        // true.
        case RedesignButton::HistoryOlder:
        case RedesignButton::HistoryNewer:
        // THE BOTTOM ROW IS MOMENTARY BUT FOR ITS ONE MODE (architect
        // 2026-08-15; it was momentary WHOLE for the hours of 2026-08-18
        // between the Cumulative toggle going back up to the icon row and ADD
        // TO SELECTION arriving, which is the row's lamp now — its arm is
        // above with the other toggles, where a mode's lamp belongs): the
        // two skips, the marker-walk three, the four arrows and the four
        // marker verbs are all acts
        // that complete, with no state to stay lit for — and so, since the
        // 2026-08-15 collapse, is the ONE PLAY/STOP BUTTON. It carried the
        // row's only lamp for the hours it was a RADIO PAIR, lit on
        // playhead_scanner_active so exactly one half was live at a time; with
        // the pair collapsed there is no wrong half to consume a press on, so
        // the lamp had nothing left to do and the audition bit picks the
        // button's GLYPH instead (redesign_button_glyph_swapped below). The
        // superseded lamp's own reasoning is at the roster entry, kept because
        // it was right about the problem it solved.
        case RedesignButton::TransportSkipBack:
        case RedesignButton::TransportPlayStop:
        case RedesignButton::TransportSkipForward:
        case RedesignButton::TransportWalkPrev:
        case RedesignButton::TransportWalkNext:
        case RedesignButton::TransportWalkBoth:
        case RedesignButton::TransportLeft:
        case RedesignButton::TransportDown:
        case RedesignButton::TransportUp:
        case RedesignButton::TransportRight:
            break;
    }
    return false;
}

// WHICH BUTTONS WEAR A GLYPH OTHER THAN THEIR TABLE ONE, and the LIVE FACT
// that decides it — the roster's ONE owner of the stateful-glyph CONDITIONS
// (2026-08-15). The glyphs themselves live with the painter
// (redesign_button_icon, paint_handler.cpp), which reads this rather than
// restating any of these bits, so the condition and the swap cannot drift.
//
// IT EXISTS FOR THE STALENESS COMPARATOR, and that is the whole reason it is a
// predicate rather than four conditions inline in the resolver: a face's
// stashed bits are what main.cpp's per-tick walk compares live against, and
// until now it compared ENABLED and SELECTED alone. That was enough while
// every stateful glyph rode a bit one of those two also moved — and it stopped
// being enough the moment PLAY/STOP collapsed onto a glyph swap, because
// playhead_scanner_active moves through six writers, several of which damage
// nothing wider than the CLOCK CELL (the natural end-of-song teardown, the
// click act's stop). Under a narrow lane damage the bottom row's painter runs
// but the button's pixels are clipped away, so without a stashed glyph term
// the row would keep showing the wrong transport glyph until something else
// damaged the whole lane — which is EXACTLY the bug this arc opened with
// (publish_button_face's clip-coverage record, paint_handler.cpp). Stashing
// this bit closes it by the same one mechanism, and closes a LATENT case with
// it: RENDER's mid-render Cancel glyph rides render_cancel_face, which moves
// neither of the other two bits, so its repaint had rested on the render
// routes' own damage alone.
//
// EVERY SUBJECT IS BINARY — two glyphs, one bit — which is what lets a single
// bool serve all four. A future button with THREE glyphs needs a wider stash,
// not another predicate beside this one.
//
// A NAMED LIST WITH A `default`, deliberately, and not the exhaustive shape
// redesign_button_in_menu_row takes: this is not a classification of the
// roster (where a new button must be forced to state its row) but a list of
// the four buttons that HAVE a second glyph, so the honest default for a new
// button is "wears its table icon".
inline bool redesign_button_glyph_swapped(const AppState& a, RedesignButton b) {
    switch (b) {
        // SAVE wears the commit glyph in the history view (where Ctrl+S IS the
        // checkpoint act) and while a checkpoint publishes.
        case RedesignButton::Save:
            return a.history_checkpoint_in_flight || a.history_mode.active;
        // RENDER wears the cancel glyph while an explicit render act is live.
        case RedesignButton::Render:
            return a.render_cancel_face;
        // THE READ-ONLY TOGGLE's table glyph is the CLOSED padlock, so the
        // swap is the OPEN one — true on a writable tab. The inversion is the
        // table's choice and not a statement about which state is ordinary.
        case RedesignButton::IconReadOnly:
            return !active_view_state(a).read_only;
        // THE TRANSPORT BUTTON wears media-playback-STOP while an audition
        // runs; its table glyph is media-playback-start.
        case RedesignButton::TransportPlayStop:
            return a.playhead_scanner_active;
        default:
            return false;
    }
}

// THE PRESSED FACE'S ONE TEST — is this roster button's click face down? True
// while the armed chrome press names it AND the pointer is inside it (the
// feint's bit; the whole arm contract is at AppState::ChromePress). The three
// painters that show a pressed interior read this instead of the arm's raw
// fields, so the inside term cannot be forgotten at one of them.
inline bool redesign_button_pressed_face(const AppState& a, RedesignButton b) {
    return a.chrome_press.kind == AppState::ChromePress::Kind::Roster &&
           a.chrome_press.inside &&
           a.chrome_press.index == redesign_button_index(b);
}

// THE SHIFT-AUGMENTED BUTTONS — the ONE owner of "this button's chord comes in
// a pair the keyboard already spells, so a SHIFT-exact press reaches the twin".
// FOUR carry it, each for that one reason: Render (Ctrl+Alt+R renders beside the
// source, Ctrl+Alt+Shift+R into a numbered _miscellaneous cell), Paste
// (Ctrl+Alt+P pastes phase resets, Ctrl+Alt+Shift+P pastes with state) and THE
// WALK'S TWO ARROWS since 2026-08-07, whose shifted twins are the walk's WALL
// JUMPS: bare `,` steps one checkpoint older and Shift+`,` goes to the oldest,
// bare `.` steps one newer and Shift+`.` goes to the newest
// (handle_history_mode_key, input_key_dispatch.cpp, owns both shapes; the arrows
// dispatch them through the one press body like every other button).
//
// THE FIFTH IS THE TRIM REGION TOGGLE, and it is the SAME ADMISSION ON A
// DIFFERENT BUTTON. The trim scissors carried it from 2026-08-15 until their
// button was retired on 2026-08-18, and the reason is a hole rather than a
// preference: the twin is Shift+X the MAXIMIZER (reset the trim to the whole
// song), and the admission superseded the 2026-08-11 "Shift+X stays
// keyboard-only" clause on the glass rig's account — the maximizer had no
// pointer route at all, so a keyboardless panel could set a trim window and
// never get back out of it. The scissors' deletion re-opened that hole for
// hours; the architect closed it the same day by REPOINTING their chord onto
// the Show trim region button (bare `x`) and moving the admission with it, so a
// SHIFT-CLICK or a LONG PRESS on that button is Shift+X. The pair is honest
// here in a way it was not on the scissors: `x` and Shift+X are the two halves
// of one trim surface — show the window, or throw it away.
//
// THIS STAYS THE STRUCTURAL FACT — "the keyboard spells a twin for this chord"
// — and is therefore stateless. Render's twin does NOTHING in iteration mode
// (Ctrl+Alt+Shift+R is a consumed no-op there, refused inside the render route),
// but the shift press still routes to it and is still swallowed by the one
// refusal rather than by a second gate here. What follows the mode is the
// advertised FACE: the tooltip's shift line, at the override below.
//
// It lives here rather than as a column in the press claim's chord table
// because it has THREE readers that must not drift: that table's shift rule,
// the TOOLTIP — the shift hint exists exactly where a shift press does
// something, so "which buttons admit shift" and "which buttons advertise it"
// are one fact by construction rather than two lists to keep in step — and,
// since 2026-08-13, THE SHIFT LONG PRESS, whose membership is this same
// predicate rather than a fourth list: a press held past kChromeShiftHoldMs
// reaches the twin exactly where a shift press does, which is what gives a
// keyboardless glass rig the shifted half of each pair (the beat's contract is
// at that constant, the arm's stamp at AppState::ChromePress::press_ms).
// (ICONPASTE LEFT THIS SET ON 2026-08-20 WITH ITS BUTTON. It admitted shift
// for Ctrl+Alt+Shift+P, the paste-state chord, which the EDIT MENU now carries
// as a row of its own — a menu item names its command outright, so the shifted
// twin needs no admission to be reachable and the long-press half has nothing
// left to serve either.)
// THE MARKER MEASURE JOINED IT THE SAME DAY, and for the hole the trim
// maximizer's admission was created to close: its twin is Shift+`/`, THE
// SCORE-VIDEO JUMP (score_video.h), and the road rig has no keyboard — without
// the admission a finger could open a measure editor and never jump to the
// measure it names. The pair is as honest here as `x`'s: one button, the two
// halves of one field — write down where this marker sits in the score, or go
// and look at it. THIS ADMISSION IS ALSO WHY THAT BUTTON IS LIT ON A LOCKED TAB
// while its four neighbours grey (architect 2026-08-20, the ruling at its arm
// in redesign_button_enabled below): a greyed button swallows the shift press
// with the plain one, which would have put the jump out of a keyboardless rig's
// reach on exactly the tabs a finished section gets locked on.
inline constexpr bool redesign_button_shift_admits(RedesignButton b) {
    return b == RedesignButton::Render ||
           b == RedesignButton::IconShowRegion ||
           b == RedesignButton::HistoryOlder ||
           b == RedesignButton::HistoryNewer ||
           b == RedesignButton::IconMarkerMeasure;
}

// THE HOVER TOOLTIP'S TEXT — name and chord, kdenlive's pattern, one row per
// button that has one. It sits with the roster (rather than with the chord
// table in input_pointer.cpp) because BOTH the painter and the pointer read it,
// and because membership is the interesting part: a null `line1` means "this
// button has no tooltip", and the buttons that carry none are the WHOLE MENU
// ROW — stated as the row rather than as a count, so a button added to row 1
// inherits the exclusion instead of falsifying a number. The switch's null arms
// are exactly redesign_button_in_menu_row's true arms — the same NAMES, and the
// count is deliberately not restated here: that predicate is the roster's one
// forced-classification site and owns the membership.
//
// THE MENU ROW CARRIES NO TOOLTIPS, and that is the RULE rather than a list of
// names (architect 2026-07-31): row 1's buttons are word labels that already
// say what they do — "File" and "Settings" open menus that name
// themselves — so a hint repeating the label would be noise. Stating it as
// the ROW's property is what let Navigation inherit the exclusion in 2026-08-02
// and File in 2026-08-13, neither needing to be remembered, and it is why
// Navigation's deletion on 2026-08-15 cost this table nothing but one arm. Every button on
// rows 3 and 4 and the bottom row
// has one; its icon or
// single letter is not self-describing.
//
// The names follow HELP's vocabulary so the hint and the manual agree.
//
// `line2` is the SHIFT LINE and is non-null on exactly the shift-admitting
// buttons, which is not a coincidence to be maintained: it is asserted against
// redesign_button_shift_admits below, so the hint cannot advertise a shift press
// that does nothing (or stay silent about one that does).
//
// THE ACCELERATOR'S SPELLING, one rule for this whole table (architect
// 2026-08-09): A BARE LETTER IS LOWERCASE — "(t)", "(p)", "(m)", "(h)", "(u)" —
// because it is the key AS TYPED, and a capital would name a shifted press this
// product does not bind on any of them. A CHORD KEEPS ITS CAPITAL and its
// spelled-out modifiers — "(Ctrl+S)", "(Ctrl+Alt+R)", "(Ctrl+Shift+Z)" — that
// being the chord vocabulary's own convention, which the rest of the product
// writes the same way. Non-letter keys are simply themselves: "(,)", "(.)",
// "(')". The table was mixed until this ruling (the eight row-4 bare letters
// were capitals) and is uniform now.
//
// IT WAS THIS SURFACE'S RULE AND NOT THE PRODUCT'S, and the contrast that made
// that worth saying has NO INSTANCE LEFT: the NAVIGATION DROPDOWN's accelerator
// column deliberately wrote a bare letter UPPERCASE ("C" for center-on-focus),
// architect-ordered from its own kdenlive crop (dropdown_full_hotkeys.png), so
// the two surfaces carried two sampled conventions and neither was evidence
// about the other. That menu is deleted (2026-08-15) and the only accelerator
// left in the product is the File menu's "Ctrl+Q" — a CHORD, which both
// conventions spell identically — so the uppercase half is recorded here rather
// than lost, and the day a command menu grows a bare-letter row it writes an
// UPPERCASE one from the crop rather than inheriting this table's lowercase.
struct RedesignTooltipText {
    const char* line1;   // nullptr -> no tooltip at all
    const char* line2;   // nullptr -> the one-line form
};
inline constexpr RedesignTooltipText redesign_button_tooltip(RedesignButton b) {
    switch (b) {
        // Row 1 — the menu row: no tooltips, per the rule above. The view bar's
        // three joined the exclusion with the row (2026-08-02): their labels are
        // the combinations themselves, so a hint could only restate them.
        case RedesignButton::File:
        case RedesignButton::Edit:
        case RedesignButton::Settings:
        case RedesignButton::ViewSW:
        case RedesignButton::ViewTP:
        case RedesignButton::ViewTW:     return {nullptr, nullptr};
        case RedesignButton::Save:       return {"Save (Ctrl+S)", nullptr};
        case RedesignButton::Undo:       return {"Undo (Ctrl+Z)", nullptr};
        case RedesignButton::Redo:       return {"Redo (Ctrl+Shift+Z)", nullptr};
        // THE SHIFT LINE NAMES THE OTHER FUNCTION (architect 2026-07-31), not
        // "for more": a hint that does not say what it gets you is not a hint.
        // It is also the standing no-gesture-hints preference's ONE ruled
        // exception, scoped to exactly the shift-admitting buttons —
        // redesign_button_shift_admits owns that membership and this comment
        // does not restate it (Paste was named here until its button left with
        // the 2026-08-20 propagate relocation, which is what a restated list
        // costs).
        case RedesignButton::Render:     return {"Render (Ctrl+Alt+R)",
                                                 "Press Shift for miscellaneous render."};
        // THE TABS CARRY NO LOCK ANY MORE (2026-08-14): the padlock is the
        // icon row's IconReadOnly button, which has its own row in this table
        // like every other roster member. (While the slot lived in the tab it
        // deliberately had no hint of its own, and the tab's did not mention
        // it — planner's call, 2026-08-01.)
        case RedesignButton::TabA:       return {"Tab A (Ctrl+Tab)", nullptr};
        case RedesignButton::TabB:       return {"Tab B (Ctrl+Tab)", nullptr};
        case RedesignButton::IconS:      return {"Source view (t)", nullptr};
        case RedesignButton::IconT:      return {"Target view (t)", nullptr};
        case RedesignButton::IconW:      return {"Warp markers (p)", nullptr};
        case RedesignButton::IconP:      return {"Phase resets (p)", nullptr};
        // SHOW TRIM REGION (the architect's own words, 2026-08-19 — the name
        // the enumerator has carried all along, with "trim" added once the
        // overlay became the trim itself; it read "Show region" from
        // 2026-08-16 and "Trim region" for one day in between), TWO LINES: its
        // twin IS Shift+X the maximizer, so the hint says so and the shift
        // admission and the line are the one fact the static_assert below
        // keeps together. The accelerator is a bare letter and so lowercase,
        // this table's rule. THE TOOLTIP NAMES A CONSTANT ACT WHILE THE LAMP
        // CARRIES THE STATE — the read-only toggle's settled precedent, and
        // this table's rows name a constant act at a constant chord — so a
        // show/hide toggle needs no second name any more than the padlock
        // does: it never swaps to "Hide", the lit face being what says which
        // press comes next, and there is no second glyph either (every
        // eye-shaped alternative collides with ViewHidden, which is already
        // IconMarkerDisable). The SHIFT LINE is the trim scissors' own words,
        // inherited with their admission.
        case RedesignButton::IconShowRegion:
            return {"Show trim region (x)", "Press Shift for the whole song."};
        // THE ZOOM GROUP (2026-08-12), all one-line: the names were aligned
        // with the Navigation dropdown's rows for the two they shared ("Zoom
        // in" / "Zoom out") and are kept verbatim now that the menu is deleted
        // (2026-08-15), the alignment having outlived its second surface; the
        // accelerators are the table's own convention — non-letter keys are
        // themselves, a bare letter is lowercase.
        case RedesignButton::IconZoomIn:
            return {"Zoom in (=)", nullptr};
        case RedesignButton::IconZoomOut:
            return {"Zoom out (-)", nullptr};
        case RedesignButton::IconZoomFitBest:
            return {"Full zoom out (0)", nullptr};
        case RedesignButton::IconZoomOriginal:
            return {"Center on focus (c)", nullptr};
        case RedesignButton::IconBpm:    return {"BPM editor (m)", nullptr};
        case RedesignButton::IconIter:   return {"Iteration mode (i)", nullptr};
        case RedesignButton::IconFollow: return {"Follow (f)", nullptr};
        case RedesignButton::IconListen: return {"Listen to renders (l)", nullptr};
        case RedesignButton::IconLoadInPlace:
            // "Load in place" not "Load render in place": the act loads A
            // STATE — a renders/ entry's sidecar set (the render name is
            // only the match key) or, in the history view, a commit's
            // sidecars or a member of the session's own timeline — so naming
            // "render" overclaims the surface.
            return {"Load in place (')", nullptr};
        // THE READ-ONLY TOGGLE (2026-08-14), one line: bare `o` toggles and
        // has no shifted twin. The TEXT IS CONSTANT while the glyph and the
        // lamp carry the state — this table's rows name a constant act at a
        // constant chord, and "Read-only" is the act's name in HELP's own
        // vocabulary whichever way the tab currently stands.
        case RedesignButton::IconReadOnly:
            return {"Read-only (o)", nullptr};
        // HELP's own vocabulary for the mode ("Checking history"), one line: the
        // key toggles and there is no shifted twin.
        case RedesignButton::IconHistory: return {"History (h)", nullptr};
        // THE TWO WALK RADIOS (2026-08-18), one line each: one chord for the
        // pair and no shifted twin on it. The words name the WALK — "Git" for
        // the committed checkpoint history, "Session" for this session's own
        // undo/redo timeline — rather than the model's Commit / Local, which is
        // the same surface-versus-model split the row-3 words carried while they
        // held this axis. Both hints show over a DEAD button outside the `h`
        // view, the tooltips-on-disabled ruling's own case (architect
        // 2026-08-07), exactly as their four neighbours' do.
        case RedesignButton::HistoryWalkGit:
            return {"Git (g)", nullptr};
        case RedesignButton::HistoryWalkSession:
            return {"Session (g)", nullptr};
        // THE CUMULATIVE TOGGLE, one line: the key toggles and has no shifted
        // twin. Like the three below it, this hint is reachable IN EVERY STATE
        // again since 2026-08-18: the four came back to the ICON ROW, which
        // paints every member always, so they carry a DEAD face outside the `h`
        // view and this hint over it — which is exactly the tooltips-on-
        // disabled ruling's own case (architect 2026-08-07, kdenlive's
        // behaviour: a disabled icon still explains itself). It bought nothing
        // for these four from 2026-08-15 to 2026-08-18, when they answered a
        // plain `true` on the bottom row's swapped cluster and were painted
        // inside the view alone.
        case RedesignButton::HistoryCumulative:
            return {"Cumulative (u)", nullptr};
        // THE WALK'S TWO STEPS, in the TWO-LINE form since 2026-08-07: their
        // shifted twins jump to the walk's walls, so the hint says so — the same
        // rule the static_assert below states, met by two more buttons. The
        // shift line deliberately does not name the member kind since
        // 2026-08-08: the Local walk's members are states of the session's own
        // undo/redo timeline, not checkpoints, so "checkpoint" would lie on half
        // the surface these arrows serve.
        // The tooltips-on-disabled ruling (architect 2026-08-07, kdenlive's own
        // behavior: a disabled icon still explains itself) governs these where
        // they rest disabled, which since 2026-08-18 is every frame outside the
        // `h` view. They were painted inside the view alone from 2026-08-12
        // (first as the icon row's collapsed four, then as the bottom row's
        // swapped cluster) until the relayout brought them back to a row that
        // hides nothing.
        case RedesignButton::HistoryOlder:
            return {"Older (,)", "Press Shift for oldest."};
        case RedesignButton::HistoryNewer:
            return {"Newer (.)", "Press Shift for newest."};
        // THE REVERT ACT, one line: the chord has no shifted twin. It was
        // GREYED inside the view whenever nothing was selected and showed this
        // hint there too, per the same ruling; since 2026-08-15 it is lit in
        // every frame the view STANDS (the architect's reversal of a grey that
        // tracked the diff-flag selection). The disabled ruling reaches it
        // again OUTSIDE the view since 2026-08-18, on the companions' revived
        // resting grey — a different fact from the one that was reversed, and
        // the two do not meet.
        case RedesignButton::HistoryRevert: return {"Revert (Ctrl+H)", nullptr};
        // THE BOTTOM ROW (2026-08-11 for the transport, 2026-08-15 for the
        // marker-walk group, 2026-08-18 for the four MARKER VERBS below).
        // ONE-LINE FORMS EXCEPT THE MEASURE, which took the row's first shift
        // admission on 2026-08-20 (the score-video jump) and carries the
        // two-line form at its own case below; the rest of the row admits no
        // shift press. The names are the ratified sentence-case labels, the
        // accelerators the table's own convention (non-letter keys are
        // themselves, and a CHORD keeps its capital and its spelled-out
        // modifiers).
        case RedesignButton::TransportSkipBack:
            return {"Go to start (Home)", nullptr};
        // THE PLAY/STOP BUTTON'S TEXT IS STATEFUL and this row is its STOPPED
        // form — the stateful overload below returns "Stop (Space)" while an
        // audition runs, Render's own pattern (a constant row for the ordinary
        // meaning, an override for the live one). It says the half the press
        // will DO rather than "Toggle playback", which is what the glyph is
        // saying in the same breath.
        case RedesignButton::TransportPlayStop:
            return {"Play (Space)", nullptr};
        case RedesignButton::TransportSkipForward:
            return {"Go to end (End)", nullptr};
        // THE SINGLE-MARKER VERBS (2026-08-12), all one-line, the acts named
        // plainly in HELP's vocabulary. None admits shift. They are the bottom
        // row's since 2026-08-18 and their rows did not change with the lane —
        // this table is keyed by id and carries no row of its own; it is kept
        // in painted order for the reader alone. THE TOOLTIPS-ON-DISABLED
        // RULING REACHES THEM HERE (architect 2026-08-07): these four are the
        // row's resting greys — in the `h` view and on a locked tab, both the
        // buttons' own gates — and a dead icon still explains itself. (The
        // MARKER MEASURE below them greys in the `h` view alone since
        // 2026-08-20; its own row says why.)
        case RedesignButton::IconMarkerDrop:
            return {"Drop marker (s)", nullptr};
        case RedesignButton::IconMarkerDelete:
            return {"Delete markers (Delete)", nullptr};
        case RedesignButton::IconMarkerDisable:
            return {"Disable markers (Ctrl+D)", nullptr};
        case RedesignButton::IconMarkerInherit:
            return {"Toggle inherit (Ctrl+N)", nullptr};
        // THE MARKER MEASURE (2026-08-19), the verb group's fifth, and TWO
        // LINES since 2026-08-20: its twin is Shift+`/`, the SCORE-VIDEO JUMP,
        // so the hint says so — the shift line naming the other FUNCTION, this
        // table's rule, and the shift admission and the line staying one fact
        // through the static_assert below. It is the standing no-gesture-hints
        // preference's ruled exception exactly as the other four are, and
        // nothing else here hints a gesture: the first line names the act and
        // stops. It greys in the `h` view alone — a locked tab leaves it LIT
        // since 2026-08-20, its shift half being lock-legal — and it still
        // explains itself in there, the tooltips-on-disabled ruling above.
        case RedesignButton::IconMarkerMeasure:
            return {"Measure (/)", "Press Shift for the score video."};
        // ADD TO SELECTION (2026-08-18), the verb group's SIXTH and a MODE
        // rather than an act — the hint names it in the architect's own words
        // and stops there. ONE LINE, no shift line (it admits no shift press)
        // and NO GESTURE HINT: the words never explain how to use the mode,
        // which is the product's standing rule about UI text. It greys in the
        // `h` view alone and still explains itself there, the
        // tooltips-on-disabled ruling above.
        case RedesignButton::IconAddToSelection:
            return {"Add to Selection (k)", nullptr};
        // THE MARKER-WALK GROUP (2026-08-15). "Previous marker" / "Next
        // marker" are HELP's own words for the bare Tab cycle; "Walk both
        // tabs" was the Navigation dropdown's own row for Ctrl+Shift+Tab, and
        // these three buttons are what made that menu a duplicate path and got
        // it deleted hours later — so the names outlived the surface they were
        // matched to, and are kept because they are the act's words. None admits shift —
        // Shift+Tab is the PREVIOUS button's own base chord rather than a
        // twin, which is Redo's shape (a shift press on it is a consumed
        // nothing, exactly as on Redo).
        case RedesignButton::TransportWalkPrev:
            return {"Previous marker (Shift+Tab)", nullptr};
        case RedesignButton::TransportWalkNext:
            return {"Next marker (Tab)", nullptr};
        case RedesignButton::TransportWalkBoth:
            return {"Walk both tabs (Ctrl+Shift+Tab)", nullptr};
        // THE FOUR ARROWS DROP THE ACCELERATOR, the table's one such family:
        // the key IS the direction, so "Left (Left)" would name the same word
        // twice — the hint keeps only the sentence-case direction. (Painted
        // order is down, up, left, right since 2026-08-14; this table is keyed
        // by id and carries no order of its own.)
        case RedesignButton::TransportDown:  return {"Down", nullptr};
        case RedesignButton::TransportUp:    return {"Up", nullptr};
        case RedesignButton::TransportLeft:  return {"Left", nullptr};
        case RedesignButton::TransportRight: return {"Right", nullptr};
    }
    return {nullptr, nullptr};
}

// THE TOOLBAR PAIR'S STATEFUL FACES — Save's and Render's, the two buttons
// whose MEANING is selected by a mode bit (architect 2026-08-02 for Render,
// 2026-08-04 for the history face, MOVED ONTO SAVE 2026-08-08). A THIRD
// STATEFUL HINT joined them 2026-08-15 and is deliberately NOT part of this
// pair's record: the bottom row's collapsed PLAY/STOP button, whose fork is
// the last arm of the body below. It differs in kind — the toolbar pair's
// words say which COMMAND the one chord currently is, while the transport's
// say which DIRECTION the one toggle will go — so it takes the shape and not
// the paragraph. SINCE THE
// 2026-08-12 RELAYOUT DISSOLVED ROW 2 the two are ICON buttons and the words
// live on their TOOLTIPS alone (the stateful overload below; the labeled
// faces and their four label constants — kRenderIterationsLabel,
// kSaveCommitLabel "Save and Commit", kSaveCommittingLabel "Committing...",
// kRenderCancelLabel "Cancel" — are DELETED producer-less with the row's
// painter; the architect's same-day ruling kept media-record for BOTH of
// Render's idle meanings, "the context makes it clear", so only the GLYPH
// SWAPS say state on the row now: Save wears VcsCommit in the history view
// and while a checkpoint publishes, Render wears DialogCancel mid-render —
// redesign_button_icon, paint_handler.cpp). The title-case exception record
// ("Save and Commit", capital S lowercase "and" capital C, architect-spelled
// 2026-08-04; "Render Iterations", capital I, 2026-08-03) survives on the
// tooltip strings that still carry the words. Each is a chord
// whose MEANING is selected by a mode bit, and the button's hint says
// whichever command it currently is:
//
//   SAVE, WITH THE HISTORY MODE STANDING → the vcs-commit icon and the
//   "Save and Commit (Ctrl+S)" hint: Ctrl+S there SAVES the piece beside its
//   source through this
//   very button's ordinary act and then commits the live state into the projects
//   repository as a checkpoint (run_history_commit, input_key_dispatch.cpp, owns
//   the order and the refusal). IT LIVES ON THIS BUTTON BECAUSE THE ACT IS
//   SAVE-FIRST BY DEFINITION
//   (architect 2026-08-08, correcting the Render hijack it shipped under): a
//   surface that runs the save first belongs on the save's own slot, and Render
//   went back to being a render in every mode.
//
//   SAVE, WITH A CHECKPOINT PUBLISHING → the same commit icon, DISABLED, in
//   EVERY view (the act outlives the view it was launched from), the hint
//   "Committing the checkpoint (Ctrl+S)". Ranked FIRST because it is the
//   outermost fact: while the worker
//   is writing the three sidecars no save may run at all (GuiSaveOps::save's own
//   term, whose mirror this face is). Three literal dots died with the label;
//   the hint spells the act in prose.
//
//   RENDER, WITH AN EXPLICIT RENDER ACT LIVE (the single render, the sweep,
//   the queue — never the automatic preview) → the dialog-cancel glyph, the
//   one-line "Cancel" hint,
//   and THE ROSTER'S ONE CHORD DIVERGENCE: its click runs the cancel act
//   itself (architect 2026-08-11 — the ruling, the rank over the iteration
//   hint and the bit are at AppState::render_cancel_face and the divergence
//   record at finish_chrome_press_release's Render arm; Ctrl+Alt+R on the keyboard
//   keeps its own kill-and-redispatch semantics unchanged).
//
//   RENDER, WITH ITERATION MODE ON (and nothing live) → the SAME media-record
//   glyph (the architect's 2026-08-12 ruling: no second render glyph, "the
//   context makes it clear") under the one-line "Render Iterations
//   (Ctrl+Alt+R)" hint — the TOOLTIP alone forks the two idle meanings. The
//   history mode gives Render NO face of its own any more: in
//   the view both render chords are consumed, so the button wears its ordinary
//   icon over the derived disabled face — visibly, the icon row hiding
//   nothing at all since 2026-08-14 (the 2026-08-12 relayout hid it in there,
//   and the 2026-08-13 revision already painted it again).
//
// THE TITLE CASE IS DELIBERATE AND SCOPED TO THE TWO HINT STRINGS carrying
// the old labels (architect
// 2026-08-03 for the capital I, 2026-08-04 for the capital C beside it): every
// other multi-word GUI label in the product stays sentence case ("Playback
// speed", "Center on focus", "Next marker") — these two are the named
// exceptions, not a precedent to copy outward or "fix". The joining word stays
// LOWERCASE ("and"), which is what title case means and what the architect
// spelled.
//
// RENDER'S SHIFT LINE GOES WITH ITS ITERATION FACE, and that is the same fact
// rather than a second decision: Ctrl+Alt+Shift+R is a consumed no-op in
// iteration mode (the refusal is in the render route, input_key_dispatch.cpp),
// so advertising a shift press there would advertise nothing. The rule the
// static_assert below states — the hint exists exactly where a shift press does
// something — therefore holds on this form too, not only on the constant table
// it overrides.
// (ROW 3'S TWO WALK-SELECTOR WORDS ARE DELETED — 2026-08-18, with the
// repurposing that produced them. From 2026-08-05 the tabs read "Remote" and
// "Local" while the `h` view stood, one word per GuiHistoryWalkSource, through
// two label constants and a redesign_button_label override that answered for
// both slots; the walk is the icon row's own radio pair now, so the tabs say
// "A" and "B" in every state and the painter shapes its own table label. The
// widths were the one thing that ruling had to check and it is why the tab
// painter still measures rather than assumes: the two words came out unequal —
// 76 + 58 px at 100% against "A"/"B" both sitting at the row minimum — which is
// what a label-sized tab bar does. The row carried the walk-and-reading PRODUCT
// as four self-labelled tabs, then as two labelled groups, for one day
// 2026-08-07..08; the READING is row 4's Cumulative toggle,
// RedesignButton::HistoryCumulative on bare `u`.)
inline RedesignTooltipText redesign_button_tooltip(const AppState& a,
                                                   RedesignButton b) {
    // (THE TABS' IN-VIEW SILENCE IS DELETED — 2026-08-18. While the `h` view
    // repurposed row 3 as its walk selector the two slots dropped their hints
    // entirely, on the view bar's reasoning: their labels WERE the thing a hint
    // would name, and "Tab A (Ctrl+Tab)" would have been a lie about the act.
    // Ctrl+Tab switches tabs in the view now, so the ordinary hint is true
    // there and the tabs carry it in every state.)
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
    // its ordinary two-line hint — what it does and where it works, which is
    // what every dead button in the product shows, the tooltips-on-disabled
    // ruling's own shape. (The clause named the FOUR RESTING-DISABLED ROW-4
    // BUTTONS as the example; they left row 4 on 2026-08-14 and stopped being
    // resting-disabled on 2026-08-15, so the general rule is named instead.)
    // RENDER'S MID-RENDER HINT, ranked above its iteration form like the
    // label: one line, "Cancel", NO KEY NAMED — deliberately. The act is the
    // button's own (the ruled chord divergence at
    // finish_chrome_press_release's Render arm), which runs the cancel BODY
    // rather than dispatching any key. (Naming Esc would also have LIED
    // whenever the trim region overlay was shown, the hide having ranked above
    // the cancel until 2026-08-21; that retirement removes the lie but not the
    // reason — the button is not a chord.) NO SHIFT LINE either: while the face is
    // Cancel a shift press cancels too — one face, one act — and the hint
    // exists only where shift does something DIFFERENT (the static_assert's
    // rule, met here by the stateful form exactly as iteration mode's already
    // does).
    if (b == RedesignButton::Render && a.render_cancel_face) {
        return {"Cancel", nullptr};
    }
    if (b == RedesignButton::Render && a.iteration_mode_enabled) {
        return {"Render Iterations (Ctrl+Alt+R)", nullptr};
    }
    // THE TRANSPORT BUTTON'S OTHER HALF (2026-08-15, the play/stop collapse):
    // one button over bare Space, so the hint names whichever act the press
    // will run — "Stop" while an audition is live, the constant table's "Play"
    // otherwise. The condition is redesign_button_glyph_swapped's, not a
    // second read of the audition bit, so the WORDS and the GLYPH can never
    // disagree about which half the button currently is. One line: bare Space
    // has no shifted twin.
    if (b == RedesignButton::TransportPlayStop &&
        redesign_button_glyph_swapped(a, b)) {
        return {"Stop (Space)", nullptr};
    }
    return redesign_button_tooltip(b);
}

// (A BUTTON'S LABEL OVERRIDE — redesign_button_label — IS DELETED PRODUCER-LESS
// on 2026-08-18. It answered "does this button override its own painted label",
// and the last thing that did was row 3's tabs under the history view's walk
// selector; with that gone the function was the identity, so the tab painter
// reads its own table label directly. The TOOLBAR PAIR's label arms had already
// gone with row 2's labeled painter, 2026-08-12 — Save's and Render's stateful
// WORDS live on the tooltip overload above and their stateful faces are the
// GLYPH swaps, redesign_button_icon.)

// THE SHIFT LINE EXISTS EXACTLY WHERE A SHIFT PRESS DOES SOMETHING. Checked at
// compile time so the two tables cannot drift: a button that gains a shifted
// chord without gaining the line (or the reverse) fails to build here.
static_assert(
    (redesign_button_tooltip(RedesignButton::Render).line2 != nullptr) ==
        redesign_button_shift_admits(RedesignButton::Render) &&
    (redesign_button_tooltip(RedesignButton::IconShowRegion).line2 !=
     nullptr) ==
        redesign_button_shift_admits(RedesignButton::IconShowRegion) &&
    (redesign_button_tooltip(RedesignButton::HistoryOlder).line2 !=
     nullptr) ==
        redesign_button_shift_admits(RedesignButton::HistoryOlder) &&
    (redesign_button_tooltip(RedesignButton::HistoryNewer).line2 !=
     nullptr) ==
        redesign_button_shift_admits(RedesignButton::HistoryNewer) &&
    (redesign_button_tooltip(RedesignButton::IconMarkerMeasure).line2 !=
     nullptr) ==
        redesign_button_shift_admits(RedesignButton::IconMarkerMeasure) &&
    (redesign_button_tooltip(RedesignButton::Save).line2 == nullptr) &&
    // THE NON-MEMBER EXAMPLES. Two of them, so the assert has a witness on
    // each side of the equivalence and cannot pass vacuously if the members
    // above are ever emptied. It was Save and IconCopy until 2026-08-20, when
    // IconCopy was deleted with the propagate relocation and IconBpm — its
    // group neighbour and now that group's leader — took the seat.
    (redesign_button_tooltip(RedesignButton::IconBpm).line2 == nullptr),
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
// swallow (rows 3 and 4, which it floats over) or a second lit button in a row
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

// TWO NOTES THE ZONE KEEPS, both about the term it deliberately does NOT
// carry — ENABLED, which the hover FACE adds at its one site and the hint does
// not:
//
// ROW 4'S AND THE VIEW BAR'S SELECTED BUTTONS DO HOVER, and
// that asymmetry with the tabs is the crops': both rows ship a selected-hover
// state (the accent outline over the selected fill) and row 3 does not. So the
// zone's carve-out names the tabs alone; the icon row's radios and toggles —
// the Cumulative one included, back on that row since 2026-08-18 — and the
// view bar's three are hoverable in
// both states, and a radio's already-selected press is refused in the ACTION
// (the chord table's `radio` flag), not in its hoverability. (The transport's
// Play / Stop pair was a fourth such radio for hours on 2026-08-15 and is one
// button with no lamp since that day's collapse.)
//
// AND redesign_button_hoverable — this zone AND the enabled term, one call —
// IS DELETED, found caller-less at the 2026-08-13 resolver sweep and dead
// since the 2026-08-07 tooltips-on-disabled ruling split its two consumers
// apart. The hover recompute is its only conceivable caller and cannot use it:
// it needs the zone answer BY ITSELF for the hint and the zone-plus-enabled
// answer for the face, so it reads this predicate once and adds the enabled
// term inline. Nothing was rewired; the composition it stood for is spelled at
// that site.

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
// SINCE 2026-08-19 THE BOX MAY INCLUDE A MEASURE BOX past the flag's own right
// edge (the flag continued in blue), and it is part of the same rect: one
// marker, one clickable surface for press, drag and select.
int hit_test_flag(const AppState& app, const GuiAudio& audio,
                  int mouse_x, int mouse_y);

// WHICH HALF of that box the point landed on — the topmost rect's published
// boundary compared against mouse_x, nothing more. Its ONE consumer is the
// marker press, which stamps the answer onto the double-click seed so the two
// halves can open two different editors (MarkerClickSpan states the whole
// rule). It answers Flag for a point that hits no flag at all, which is the
// harmless answer: a caller with no hit has nothing to fork. Same backward
// walk, same topmost-wins arbitration as hit_test_flag — literally the same
// walk, so the two can never disagree about which box was hit.
MarkerClickSpan hit_test_flag_span(const AppState& app, const GuiAudio& audio,
                                   int mouse_x, int mouse_y);

// (THE STEM AS A POINTER TARGET IS RETIRED — architect 2026-08-12, the seventh
// glass ruling: MARKER STEMS ARE POINTER-INERT IN ALL CONTEXTS, the flag box
// being the marker's one pointer surface ("I definitely don't want to be
// concerned about accidentally touching a marker"). hit_test_marker_stem — the
// 2026-08-01 second-surface owner: a plain upper-half press within
// kMarkerStemGrabPx of a painted stem's column was that item's click, on the
// live columns and, since 2026-08-05, on the `h` view's diff flags — is
// DELETED with both of its callers, and kMarkerStemGrabPx / marker_stem_grab_px
// with it. The question stayed open through the touch arc and was answered
// "stems stay" while the scrollbar plan lived; the waveform-height clamp
// (main.cpp's layout owner) keeps the flag lane in easy reach on every display,
// which is what the removal was waiting for. The stems still PAINT exactly as
// before — the marker_stems stash below is the stem PAINTER's input alone now.)

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

// THE OVERVIEW BOX ENDCAPS' HIT TEST (the lane rework, 2026-08-12): the
// viewport box outline's LEFT and RIGHT edges as grab handles, the trim
// endcaps' own model — each 1px edge column (overview_box_span, the painter's
// own derivation, so a grabbed edge is exactly a painted one) inflated by
// trim_endcap_grab_px() per side, y-gated to the overview lane. Overlap on a
// narrow box — grab tolerance alone can make the two bands meet — resolves
// NEAREST EDGE, with Begin winning the exact tie (the trim tie-break's
// shape). Returns TrimHit because the begin/end vocabulary IS this pair's
// meaning: the dragged edge extends ONE viewport bound exactly as a trim cap
// extends one trim bound, and the cursor map hands back the same
// TrimBoundBegin/End pair. Read by the lane's press claim (the endcap claim
// OUTRANKS everything else on the lane, plain only) and by
// pointer_cursor_kind's lane rows — two consumers, one verdict.
TrimHit hit_test_overview_endcap(const AppState& a, const GuiAudio& audio,
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
