#pragma once

#include "app_state.h"
#include "playback_lifecycle.h"
#include "selection.h"
#include "undo.h"
#include "viewport.h"
#include "warp_frame_map.h"   // WarpFrameMapSegment

#include <cstdint>
#include <vector>

class GuiAudio;
struct GuiTargetRender;

// HORIZONTAL MOVEMENT IS A FOCUS ACT — GROUPS ARE NEVER MOVED (architect
// 2026-07-29). Both position nudges (bare Left/Right in the marker lane: the warp
// nudge GuiWarpMarkersOps::nudge_selected_markers in source home, the phase reset
// nudge GuiPhaseResetMarkersOps::nudge_selected_phase_resets in target home) and
// the pointer marker DRAG act on ONE marker, the FOCUS: a 2+ selection COLLAPSES
// TO ITS FOCUS and the focus takes the step (here, in the shared prologue below),
// and a press on a member of a 2+ selection single-selects it immediately like a
// press on any other marker, the drag then being an ordinary singleton drag
// (input_pointer.cpp's plain marker click; marker_drag.cpp carries no group
// machinery at all). The per-member nudge and the rigid group drag both existed
// and both died with this ruling — do not re-propose either: a proposal loop with
// a whole-press wall refusal and a same-column merge on one side, an
// intersection-clamped shared delta with a grabbed-anchored rigid commit on the
// other, and the pointer deferral (a press that held its click back so the group
// could seed) with them.
//
// THE GROUP-VERB DOCTRINE, the general rule this ruling instances — stated ONCE
// here, every verb site stating only its own class plus a pointer:
//   * GROUP-CAPABLE where the members are INDEPENDENT of one another or the
//     selection's SPAN is merely READ: the value edits (Ctrl+D's disable toggle,
//     the Up/Down cent step, Delete) and, reading the span, `m`, Ctrl+P, Space's
//     left-bound launch, `x`, the zoom framing, and the undo/redo + paste
//     restores (which DEFINE a group selection and its extent);
//   * FOCUS-COLLAPSE where the members are COUPLED to one another or the act is
//     POSITIONAL: Ctrl+N and all horizontal movement. Ctrl+N is coupled because
//     the pass -> owner freeze reads the RESOLVED walk — a member's frozen tempo
//     depends on what the members before it just became, so a group toggle's
//     result would depend on iteration order; positional acts are coupled because
//     one painted column per member and one rigid delta for all of them are
//     different arrangements, and neither can be the honest reading of "move the
//     selection".
// THE ARCHITECT'S GENERAL RULE, verbatim (2026-07-29), which generalizes those two
// classes and is the authoritative statement for any FUTURE verb: "the rule is - if
// group is relatively cheap to implement, implement it. otherwise, collapse to last
// sel." A HYBRID THIRD FORM IS EXPLICITLY REJECTED — the ruling that produced this
// sentence was a rejection of one: a 2+ selection left resting with no span, which
// three routes briefly produced and which draws no playhead cue at all (the cursor
// yields to a non-empty selection, the stem is singleton-only), was ruled out as "a
// hybrid third option that i did not ask for or ratify". So a verb is EITHER
// group-capable OR collapses to the focus with the playhead landed on it; there is
// no third answer, and "collapse to last selected" means exactly
// collapse_to_focused + land_playhead_on_marker, the shape below.
// THE COLLAPSE+LAND SITES, one form and FOUR of them (re-derived 2026-07-30): this
// prologue serving both position nudges, Ctrl+N (warpmarkers_ops.cpp), the S/T view
// switch `t` (input_handler.cpp — where the land is the tail's own domain re-express
// through the focus), and `c` (input_key_dispatch.cpp — collapse only, its jump
// already being a jump to the focus). The singleton tempo step's call
// (warpmarkers_ops.cpp's Up/Down arm) is NOT one: its selection is already a
// singleton, so the collapse moves no focus and owes no land.
// BARE `0` LEFT THIS LIST (architect 2026-07-30): the overview toggle was
// overscoped and is a PURE VIEWPORT MOVE again — it clears no region and collapses
// nothing, so a group and its span ride through it, exactly as they ride the zoom
// framing and every other viewport-only command. Its clear and its collapse died
// TOGETHER, deliberately: a clear alone would rest the spanless group this doctrine
// rejects.
// REACHABILITY, with the pointer deferral dead: the ONLY producers of a resting
// 2+ selection are the two multi-select clicks (shift-range, ctrl-toggle), the
// `m` bpm-mode open, the propagate paste, and the undo/redo restores — and every
// group verb that remains is span-read or member-independent, so nothing that can
// rest a group can then move it. Each of those five producers rests its group WITH
// its extent span (the clicks and `m` derive it, the paste and the restores define
// it), which is why a SPANLESS 2+ selection has no producer at all: the property
// holds by the PRODUCERS' OWN FORM, not by a distributed enforcement protocol —
// that protocol was deleted (architect 2026-07-29) and is not coming back.
//
// THE WALL POLICY, ONE RULE FOR THE WHOLE PRODUCT (architect 2026-07-30) — stated
// ONCE here, every arm carrying only its own class plus a pointer back:
//   * A SINGLETON STEP CLAMPS ONTO ITS WALL. A position nudge moves one painted
//     column = samples-per-pixel FRAMES, so a press starting near an edge can
//     overshoot mid-step; clamping is what makes the song edges EXACTLY REACHABLE
//     by keyboard, in both columns. Both position twins clamp (the warp twin
//     always did; the phase twin joined with this ruling, replacing a whole-press
//     refusal). The SINGLETON TEMPO STEP's constructive clamp
//     (adjust_tempo_cents, warpmarkers_ops.cpp) already conformed and is
//     untouched — it steps whole CENTS on the cent grid, so clamping and refusing
//     coincide there and the value simply stops at the bracket edge.
//   * A GROUP PRESS REFUSES WHOLE. This is NOT a wall policy — it is GROUP
//     RIGIDITY: clamping per member would pool members at the wall and deform
//     their relative values, which is the same reason the deleted per-member
//     group nudge refused the whole press. The one surviving group arm, the group
//     tempo step (adjust_tempo_cents_group), keeps that whole-press refusal and
//     its own group-rigidity justification at its site.
// POST-CLAMP IDENTITY IS A SILENT NO-OP in both position twins: the clamped
// target is compared against the current frame and an equal result writes
// NOTHING — no undo push, no damage, no playback stop. That check is what makes
// the keyboard stop rule's refusal gating (playback_lifecycle.h) EXACT for the
// nudges, with no recorded deviation left.
//
// This file is the type-free flesh SHARED BY THE TWO POSITION NUDGE TWINS, all of
// it singleton-scoped now: an identical guard prologue (which owns the collapse),
// an identical pixel-column step, and an identical commit tail. The WALL-REGIME
// MIDDLE is now the SAME SHAPE in both twins under the policy above — a delta
// clamped into the marker's own wall headroom, over warp's identity domain and
// over phase's mapped domain alike — but it stays spelled out in each twin
// VERBATIM, since each reads its own store and its own EOF wall. These three free
// functions collapse only the type-free parts: no templates, no callbacks, no
// policy structs (the naming-symmetry doctrine resists genericity — this is plain
// extraction of the shared flesh).

// Result of the shared guard prologue.
struct PositionNudgePrologue {
    bool ok      = false;  // false: the press refuses (silent, navigation-class)
    bool merge   = false;  // undo-coalesce verdict for this press
    int  focused = -1;     // app.last_selected_marker, validated in [0, store_size)
};

// The shared guard prologue of the two position nudges, and the site that makes
// the gesture a FOCUS ACT. Order is IDENTICAL in both twins and preserved
// exactly: (1) loading / empty-audio refusal; (2)
// empty-selection refusal — unreachable from the dispatcher, which routes an
// empty selection to the waveform-lane playhead step instead and never reaches
// here, so this is the belt; (3) no-focus
// refusal; (4) the undo-coalesce verdict, computed before the geometry refusals
// (coalesce_gesture keys off the press's own repeat bit, which
// `synthesized_repeat` carries down from the on_key event that reached the
// handler, and it just has to run before record_gesture stamps the kind in the
// tail); (5) bad
// sample-rate refusal; (6) non-positive samples-per-pixel refusal; (7) the
// focused-index stale refusal — the ONLY index this gesture reads, so the old
// belt over every selected index went with the group (nothing else is touched,
// and the collapse below drops the rest of the membership anyway).
// Then (8) THE COLLAPSE: a 2+ selection collapses to its focus and the playhead
// LANDS on that focus, the Ctrl+N shape (collapse + land at the CALLER of
// Selection::collapse_to_focused, the convention that keeps the land at the site
// which hands the marker lane a focus). The step below is therefore always the
// SINGLETON op on `focused`, bit-for-bit what a singleton selection always got.
// THE COLLAPSE ARM OWNS ITS OWN PLAYBACK STOP, immediately ahead of the collapse:
// a real collapse is a write and the land moves the cursor, so the keyboard stop
// rule (playback_lifecycle.h) demands the stop there — while a SINGLETON press
// carries no collapse and stops in its TWIN instead, past the post-clamp identity
// check and immediately ahead of the twin's first write. That placement is the
// rule's refusal gating made exact: a press that writes nothing stops nothing, and
// the only recorded deviation (an unconditional stop at this prologue's head) is
// gone.
// A press that then refuses at its wall keeps the collapse — the collapse is the
// press's own committed act, not a prelude to the step (its damage is
// collapse_to_focused's own, so a refused press repaints correctly), and the
// stop it paid stands with it.
// Every marker is nudgeable, including the one at time 0 — the parser resolver
// normalizes the resulting arrangement at render/preview time, there is no
// gesture pin.
//
// PLAYHEAD RULE (architect 2026-07-23, reversing the 2026-07-20 decoupling): the
// playhead FOLLOWS the nudged marker through the step (the actual follow lands in
// finish_position_nudge) — it steps with the marker so a later Space
// auditions FROM the marker. It is a re-land, not a repair: nothing DIVERGES from
// the focus to be towed (the two playhead forms and the empty towed category are
// stated once at land_playhead_on_marker, input_pointer.cpp — do not restate them
// here). The lead-in workflow (parking the playhead upstream to audition the
// approach) is supplied by the scrub surface instead. The twins keep their own
// GestureKind (WarpNudge / PhaseResetNudge).
// `synthesized_repeat` is the dispatching key event's platform repeat bit,
// consumed by the coalesce verdict alone.
PositionNudgePrologue position_nudge_prologue(
    AppState& app, const GuiAudio& audio,
    GuiPlaybackLifecycle& playback_lifecycle, Selection& selection,
    Viewport& viewport, Undo& undo,
    GestureKind kind, bool synthesized_repeat, int store_size);

// THE pixel-column step, and the ONLY position derivation either nudge uses:
// read the marker's currently painted column (painted_column_of_source_frame —
// the stem painters' own math against the displayed paint basis) and commit the
// frame at cf + direction (authored_frame_at_column, which funnels through
// snap_authored_frame, the one fractional-to-authored route). Returns the
// committed frame RAW — walls are NOT this helper's business: each twin applies
// its own regime to the result. Exactly one call per press, on the focus.
//
// THE ONE-COLUMN-PER-PRESS GUARANTEE and its numeric rationale live here. It is a
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
// ONE PAINTED COLUMN PER PRESS IS A POSITION-NUDGE PROPERTY AND NOTHING ELSE, and
// the trap it documents already bit once: the only other gesture that ever claimed
// it was the W+target bare Left/Right TEMPO-IMAGE STEP, which authored CENTS — a
// grid COARSER than the pixel grid on ordinary spans (one cent moves the image
// about 2+ px there) — so the guarantee INVERTED there and that gesture needed a
// minimum-directional-cent rule of its own. It is deleted (2026-07-29, with the
// whole tempo-image family — see marker_drag.h), and the surviving tempo surface,
// the bare Up/Down cent step, makes no pixel claim at all: it steps ONE CENT per
// press by definition. So do not carry this guarantee to any value gesture.
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
//     selected-marker stem's move: a nudge shifts the nudged marker's frame, and
//     its always-on focus stem (architect 2026-07-25) repaints at the new column
//     here. The selection is a singleton by the prologue's collapse, so there is
//     always exactly that one stem;
// (d) invalidate_timestamp_area;
// (e) PLAYHEAD FOLLOW: move_playhead_to the nudged marker's committed frame
//     through the two-step placement basis (source_frame_to_active_domain —
//     identity in warp's source home, a real map in phase's target home;
//     committed_focused_frame is reorder-independent). move_playhead_to owns the
//     clamp, invalidation, and keep-visible edge-align, writing the cursor field
//     only (playback was stopped by the twin, past its wall clamp and ahead of
//     its first write — and by the prologue's collapse arm before that on a 2+
//     press; either way this tail always runs stopped).
// (f) THE REGION: a position nudge is a POINT command (one flag standing in for
//     the cursor) and CLEARS any resting span, unconditionally and blind to
//     provenance — exactly like the marker click that selects that singleton (the
//     two playhead forms, at land_playhead_on_marker). There is no span-preserving
//     arm any more: the extent re-derive died with the group nudge, and a
//     SelectionExtent span cannot rest beside the singleton this tail always sees
//     (a membership replace takes it — clear_region_on_membership_replace).
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
// state, and the region clear reads only the region. ONE
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
void finish_position_nudge(
    AppState& app, const GuiAudio& audio, Viewport& viewport, Undo& undo,
    GestureKind kind, int64_t committed_focused_frame,
    GuiTargetRender& target_render);
