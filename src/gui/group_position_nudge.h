#pragma once

#include "app_state.h"
#include "playback_lifecycle.h"
#include "undo.h"
#include "viewport.h"
#include "warp_frame_map.h"   // WarpFrameMapSegment

#include <cstdint>
#include <vector>

class GuiAudio;
struct GuiTargetRender;

// Shared type-free flesh of the two GROUP POSITION nudges — the warp nudge
// (GuiWarpMarkersOps::nudge_selected_markers, source home view) and the phase
// reset nudge (GuiPhaseResetMarkersOps::nudge_selected_phase_resets, target home
// view). Both are the bare Left/Right press with a non-empty selection (the
// marker lane), each authoring its own column in its home
// view; a 2+ selection nudges rigidly, a singleton is the degenerate case. The
// twins share an identical guard prologue, an identical pixel-column anchor
// step, and an identical commit tail; only the WALL-REGIME MIDDLE differs by
// ruled doctrine ("one regime per column at its home") — warp CLAMPS the rigid
// integer delta into the member wall-headroom intersection (identity domain),
// phase REFUSES all-or-nothing over the mapped domain (unclamped inverse + exact
// integer wall belt) — and that middle stays in each twin VERBATIM. These three
// free functions collapse only the type-free parts: no templates, no callbacks,
// no policy structs (the naming-symmetry doctrine resists genericity — this is
// plain extraction of the shared flesh).

// Result of the shared guard prologue.
struct GroupNudgePrologue {
    bool ok      = false;  // false: the press refuses (silent, navigation-class)
    bool merge   = false;  // undo-coalesce verdict for this press
    int  focused = -1;     // app.last_selected_marker, validated in [0, store_size)
};

// The shared guard prologue of the two group position nudges. Order is IDENTICAL
// in both twins and preserved exactly: (1) loading / empty-audio refusal;
// (2) stop_playback_if_playing FIRST — this is a fine-tuning authoring gesture
// (the bare Left/Right marker-lane press is the only caller path); (3)
// empty-selection refusal — unreachable from the dispatcher, which routes an
// empty selection to the waveform-lane playhead step instead and never reaches
// here, so this is the belt; (4) no-focus
// refusal; (5) the undo-coalesce verdict, computed before the geometry refusals
// (a const query — coalesce_gesture keys off command adjacency, app.command_seq
// bumped once at the on_key dispatch entry that reached the handler, and it just
// has to run before record_gesture stamps this command in the tail); (6) bad
// sample-rate refusal; (7) non-positive samples-per-pixel refusal; (8) the
// stale-index belt over every selected index; (9) focused-index stale refusal.
// Every marker is nudgeable, including the one at time 0 — the parser resolver
// normalizes the resulting arrangement at render/preview time, there is no
// gesture pin.
//
// PLAYHEAD RULE (architect 2026-07-23, reversing the 2026-07-20 decoupling): the
// playhead FOLLOWS the focused marker through the nudge (the actual follow lands
// in finish_group_position_nudge) — it steps with the marker so a later Space
// auditions FROM the marker. THE TOWED SET IS EMPTY (architect 2026-07-28): under
// the marker-lane-owns-the-playhead rule — stated in full, with its landing-site
// enumeration, at land_playhead_on_marker in input_pointer.cpp — a land is ALWAYS
// on the focus, so every focus-setting route leaves the playhead already
// coincident with the focused marker and this follow never has a divergence to
// close. The last three exceptions were the multi-select clicks (shift-range,
// Ctrl+click toggle-ADD, Ctrl+click toggle-REMOVE), which landed at the earliest
// selected while focusing elsewhere; that ruling was reversed and they now land
// on their own focus like everything else. The follow stays, because the nudge
// MOVES the focused marker and the playhead must move with it — it is a
// re-land, not a repair. (Ctrl+Tab is in no list at all: it restores a stored
// focus/playhead PAIR from the destination tab's slot, reproducing whatever those
// two were when they were stashed.)
// The lead-in workflow (parking the playhead upstream
// to audition the approach) is supplied by the scrub surface instead. The twins
// keep their own GestureKind (WarpNudge / PhaseResetNudge).
GroupNudgePrologue group_position_nudge_prologue(
    AppState& app, const GuiAudio& audio,
    GuiPlaybackLifecycle& playback_lifecycle, Undo& undo,
    GestureKind kind, int store_size);

// The shared pixel-column ANCHOR step: read the focused item's currently painted
// column (painted_column_of_source_frame — the stem painters' own math against
// the displayed paint basis) and commit the frame at cf + direction
// (authored_frame_at_column, which funnels through snap_authored_frame, the one
// fractional-to-authored route). Returns the committed frame RAW — walls are NOT
// this helper's business: the warp twin clamps its rigid delta afterward, the
// phase twin applies its own all-or-nothing refusal.
//
// THE ONE-COLUMN-PER-PRESS GUARANTEE and its numeric rationale live here. It is a
// GRID-FINENESS property, not a gesture-family property: the anchor's painted move
// is exactly the commanded column per press because the authored FRAME grid is
// finer than the pixel grid, so every adjacent-column target is representable in
// the authored domain (and re-anchoring to the column grid every press re-derives
// the pixel phase, so whole-frame rounding residue never accumulates — rounding
// each press independently would paint occasional 0 or 2 px jumps). In the PHASE
// nudge's mapped target home the deepest zoom gives at least 27.5625 / 16 = 1.72
// source frames per target pixel under the value brackets (tempo times both scales
// at least 0.25 * 0.5 * 0.5 = 1/16) at the 44100 sample-rate floor, so the
// whole-frame rounding error is just under 0.291 px (0.5 / 1.72265625 =
// 0.29025); in the WARP nudge's identity
// source home the bound is trivially stronger (a column is at least 27.5625 whole
// frames, error at most 0.5 frame, about 0.018 px). Either way each press still
// advances at least one whole frame.
//
// The contrast is the point, documenting a trap that already bit once: the
// W+target bare Left/Right TEMPO-IMAGE STEP (MarkerDragOps::step_tempo_image) is
// NOT a consumer of this guarantee and cannot be — it authors CENTS, a grid
// COARSER than the pixel grid on ordinary spans (one cent moves the image about
// 2+ px there), so the guarantee INVERTS and the minimum-step rule at
// step_tempo_image owns that gesture's step size. One painted column per press
// applies to the POSITION nudges only.
int64_t stepped_anchor_frame(
    const AppState& app, const GuiAudio& audio,
    const std::vector<WarpFrameMapSegment>& map,
    int64_t orig_frame, int direction);

// The shared type-free COMMIT TAIL. Each twin calls this AFTER it has: run its
// regime middle, mutated its store, run reorder_markers_by_time +
// remap_marker_indices_after_reorder, collected touched_live, and done its own
// typed undo merge/push block (push_undo_warp with affects_persistence=true /
// push_undo_phase_reset — those stay in the twins). The tail then, in order:
// (a) record_gesture (re-stamps this command for the next coalesce test);
// (b) recompute_dirty;
// (c) invalidate_waveform_area — this full-waveform damage also OWNS the
//     selected-marker stem's move: a nudge shifts the focused SINGLETON's frame,
//     and its always-on focus stem (architect 2026-07-25) repaints at the new
//     column here. A 2+ selection paints no stem (its cue is the extent
//     region's recolored ground), so nothing to move there;
// (d) invalidate_timestamp_area;
// (e) PLAYHEAD FOLLOW: move_playhead_to the focused item's committed frame
//     through the two-step placement basis (source_frame_to_active_domain —
//     identity in warp's source home, a real map in phase's target home;
//     committed_focused_frame is reorder-independent). move_playhead_to owns the
//     clamp, invalidation, and keep-visible edge-align, writing the cursor field
//     only (playback was stopped in the prologue).
// (f) THE REGION, BY THE PLAYHEAD'S TWO FORMS. A GROUP nudge is a SPAN gesture:
//     an active SelectionExtent region re-derives to the moved group's extent —
//     MAINTAIN only, never CREATE; an inactive / Free / TrimWindow region is
//     untouched (position gestures do not re-sync TrimWindow, and the trim's
//     source-frame bounds are unmoved by either nudge anyway). The
//     SelectionExtent provenance survives the nudge (no membership replace, and
//     the reorder remap does not clear), so the gate is reliable. A SINGLETON
//     nudge is instead a POINT command and CLEARS any resting region, exactly
//     like the marker click that selects that singleton.
// (g) target_render.trigger.
//
// NO SYNCHRONOUS RE-WARP is needed at either home: the warp nudge authors in
// warp's SOURCE home view, where the source waveform pixels do not depend on the
// warp map, and the phase nudge moves phase reset positions, which do not change
// the warp map either — so there is no displayed target plate to re-warp. The
// target preview still invalidates (a source-view warp edit changes the rendered
// target buffer), so the view-independent trigger stays.
//
// ORDERING: the two twins historically ordered {undo push, playhead}
// slightly differently (warp: push -> record -> ... -> playhead -> region;
// phase: playhead -> push -> record -> ... -> region). The unified order
// gives both the WARP shape (push in the twin, then this tail: record,
// invalidate, playhead, region). The COMMITTED BYTES are identical for every
// input: the undo push/record read neither the playhead nor the selection (their
// snapshots capture the marker stores, engine settings, tab, and the hint indices
// only — not the cursor), move_playhead_to does not read undo
// state, and the region follow reads only the post-mutation store/selection. TWO
// knowingly-accepted deltas ride the unification, both phase-only (the warp twin
// already had this shape) and both harmless, recorded here so the next reader need
// not re-derive them.
//
// (1) HOVER-POPUP ordering. move_playhead_to can conditionally recompute the hover
// after a viewport shift, and the undo push clears it; the phase twin historically
// recomputed-then-cleared while the warp twin cleared-then-maybe-recomputed, so the
// unified tail gives both the warp behavior — a nudge that shifts the viewport with
// the pointer resting on a hoverable marker now ends with the hover recomputed
// rather than cleared. Transient display state only (no committed bytes, no undo
// content, damage idempotent); accepted as improved twin symmetry.
//
// (2) THE COALESCE CLOCK (touches the undo ENTRY). Undo::record_gesture stamps
// gesture_steady_ms() into the coalescer, and the same move_playhead_to can run a
// SYNCHRONOUS kick_waveform_sync when the follow shifts the viewport (the
// offscreen-focused case). The phase pre-image moved the playhead THEN recorded
// (clock stamped AFTER that rebuild); the unified tail records THEN moves the
// playhead (clock stamped BEFORE it). So when the focused reset is offscreen the
// rebuild no longer sits inside the measured 500 ms coalesce window, and a next
// burst press landing near that boundary can open a fresh undo entry where the
// pre-image would have merged. Worst case: one extra undo entry — a second Ctrl+Z.
// Accepted and architect-ratified (2026-07-24): the unified tail adopts the warp
// shape verbatim to minimize warp/phase divergence — the twins now behave MORE
// alike than before — and the coalesce window's anchor point is a heuristic, not a
// recorded contract (deliberate sub-500 ms presses are how bursts are actually
// played).
void finish_group_position_nudge(
    AppState& app, const GuiAudio& audio, Viewport& viewport, Undo& undo,
    GestureKind kind, int64_t committed_focused_frame,
    GuiTargetRender& target_render);
