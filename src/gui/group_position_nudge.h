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
// view; a 2+ selection moves every member by its own painted column (architect
// 2026-07-29 — the doctrine is at stepped_anchor_frame below), a singleton is
// the degenerate case. The twins share an identical guard prologue, an identical
// pixel-column step, and an identical commit tail; only the WALL-REGIME MIDDLE
// differs by ruled doctrine ("one regime per column at its home") — warp CLAMPS
// a singleton's step into its wall headroom (identity domain), phase REFUSES
// over the mapped domain (exact integer wall belt), and a GROUP refuses the
// whole press in BOTH twins (per-member clamping would pool members onto a wall
// frame) — and that middle stays in each twin VERBATIM. These three free
// functions collapse only the type-free parts: no templates, no callbacks,
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
// (a const query — coalesce_gesture keys off the press's own repeat bit, which
// `synthesized_repeat` carries down from the on_key event that reached the
// handler, and it just has to run before record_gesture stamps the kind in the
// tail); (6) bad
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
// focus/playhead PAIR from the destination tab's slot. It reproduces the stashed
// pair only when the restored selection is EMPTY; with a selection it LANDS on
// the restored focus instead, since a cross-tab edit can move that focus's image
// while its saved cursor stands still — the rule and the override are recorded at
// switch_active_tab_view_to.)
// The lead-in workflow (parking the playhead upstream
// to audition the approach) is supplied by the scrub surface instead. The twins
// keep their own GestureKind (WarpNudge / PhaseResetNudge).
// `synthesized_repeat` is the dispatching key event's platform repeat bit,
// consumed by the coalesce verdict alone.
GroupNudgePrologue group_position_nudge_prologue(
    AppState& app, const GuiAudio& audio,
    GuiPlaybackLifecycle& playback_lifecycle, Undo& undo,
    GestureKind kind, bool synthesized_repeat, int store_size);

// THE PER-ITEM pixel-column step, and the ONLY position derivation either nudge
// uses: read the item's currently painted column (painted_column_of_source_frame
// — the stem painters' own math against the displayed paint basis) and commit
// the frame at cf + direction (authored_frame_at_column, which funnels through
// snap_authored_frame, the one fractional-to-authored route). Returns the
// committed frame RAW — walls are NOT this helper's business: each twin applies
// its own regime to the result. A singleton calls it once; a 2+ selection calls
// it once PER MEMBER, each member on its own painted column — under that rule
// every moved item is its own anchor, and the name is read that way.
//
// THE ONE-COLUMN-PER-PRESS GUARANTEE and its numeric rationale live here, and
// they hold for EVERY moved item, singleton or group member. It is a
// GRID-FINENESS property, not a gesture-family property: the painted move is
// exactly the commanded column per press because the authored FRAME grid is
// finer than the pixel grid, so every adjacent-column target is representable in
// the authored domain (and re-anchoring to the column grid every press re-derives
// the pixel phase, so whole-frame rounding residue never accumulates — rounding
// each press independently would paint occasional 0 or 2 px jumps). In the PHASE
// nudge's mapped target home the deepest zoom gives at least 27.5625 / 16 = 1.72
// source frames per target pixel under the value brackets (tempo times both scales
// at least 0.25 * 0.5 * 0.5 = 1/16) at the 44100 sample-rate floor, so the
// whole-frame rounding error is just under 0.291 px (0.5 / 1.72265625 =
// 0.29025); in the WARP nudge's identity source home the bound is trivially
// stronger (a column is at least 27.5625 whole frames, error at most 0.5 frame,
// about 0.018 px). Either way each press still advances at least one whole frame.
//
// THE GROUP IS PER-MEMBER, NOT RIGID (architect 2026-07-29, reversing the
// rigid-group convention FOR THE KEYBOARD NUDGE ONLY): every selected member
// takes this same step on its own column, so every member moves EXACTLY one
// painted column per press, always, with zero residue. Visual determinism was
// chosen over store rigidity on a gesture the architect uses rarely, after the
// rigid form's own defect: a single delta derived by re-snapping the FOCUSED
// member absorbed that member's post-zoom sub-column phase, so the first press
// after any zoom moved the group by up to a column and a half and the group
// visibly SPLIT — some members crossed a column boundary on that delta and the
// rest did not. Deriving the delta from the grid's own column step would have
// fixed the split but left ~1/spp of members painting a 0- or 2-column step on
// any given press, which is inherent to one integer delta over a fractional
// pixel grid. Per-member snapping removes it entirely.
//
// THE POINTER GROUP DRAG IS UNAFFECTED and stays RIGID: the two group-move
// gestures now have different spacing semantics, deliberately. What the codex
// round-3 review called the per-member "spacing defect" was a defect against a
// rigidity claim this gesture no longer makes; it is the RULED behavior here and
// remains the defect in the drag, which still claims rigidity.
//
// THE ACCEPTED CONSEQUENCES, both of them spacing:
// (a) SPACING QUANTIZES TO THE PIXEL GRID. Each member's sub-column phase is
//     flattened on its FIRST press (a bounded move, under one column) and never
//     again — from press one every member is grid-aligned and steps exactly one
//     column. Non-cumulative.
// (b) MEMBERS SHARING A PAINTED COLUMN COLLAPSE ONTO ONE GRID FRAME and move
//     identically thereafter (undo restores the spacing; a later press does
//     not). At fine zoom the column is about 30 frames, sub-millisecond and
//     inaudible; at coarse zoom it can be thousands of frames, and an exact
//     frame tie goes LOUD at the render boundary (the normalization red flags,
//     the 1.00 collapse). This began as a planner acceptance of the architect's
//     per-member rule rather than a ruling on the collapse itself;
//     ARCHITECT-CONFIRMED 2026-07-29 (live-tested), the merge included.
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
// (a) record_gesture (re-stamps this press's kind for the next coalesce test);
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
// state, and the region follow reads only the post-mutation store/selection. ONE
// knowingly-accepted delta rides the unification, phase-only (the warp twin
// already had this shape) and harmless, recorded here so the next reader need
// not re-derive it; a second one dissolved with the coalesce clock and is kept
// below as (2) so it is not re-discovered as an open cost.
//
// (1) HOVER-POPUP ordering. move_playhead_to can conditionally recompute the hover
// after a viewport shift, and the undo push clears it; the phase twin historically
// recomputed-then-cleared while the warp twin cleared-then-maybe-recomputed, so the
// unified tail gives both the warp behavior — a nudge that shifts the viewport with
// the pointer resting on a hoverable marker now ends with the hover recomputed
// rather than cleared. Transient display state only (no committed bytes, no undo
// content, damage idempotent); accepted as improved twin symmetry.
//
// (2) WHERE record_gesture SITS IN THE TAIL — no longer a delta at all, recorded
// because it WAS one. The phase pre-image moved the playhead THEN recorded, the
// unified tail records THEN moves the playhead; while coalescing was timed, that
// reordering moved a possible SYNCHRONOUS kick_waveform_sync (move_playhead_to
// runs one when the follow shifts the viewport — the offscreen-focused case)
// outside the measured window, so a next burst press landing near the boundary
// could open a fresh undo entry where the pre-image would have merged. Coalescing
// is decided by REPEAT IDENTITY now (undo.h): a held key's continuation presses
// merge because they are synthesized repeats, with no clock to be inside or
// outside of, so the timing question this delta was about cannot arise and the
// accepted cost is simply gone. The ordering itself stands unchanged — the
// unified tail adopts the warp shape verbatim to minimize warp/phase divergence,
// and record_gesture reads nothing the playhead move writes.
void finish_group_position_nudge(
    AppState& app, const GuiAudio& audio, Viewport& viewport, Undo& undo,
    GestureKind kind, int64_t committed_focused_frame,
    GuiTargetRender& target_render);
