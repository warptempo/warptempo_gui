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
// nudge GuiPhaseResetMarkersOps::nudge_selected_phase_resets in either audio
// view, S+P and T+P) and
// the pointer marker DRAG act on ONE marker, the FOCUS: a 2+ selection COLLAPSES
// TO ITS FOCUS and the focus takes the step (here, in the shared prologue below),
// and a click on a member of a 2+ selection single-selects it like a click on
// any other marker, the drag then being an ordinary singleton drag
// (run_marker_click_act, input_pointer.cpp — the flag's click acts AT THE
// PRESS (2026-08-17), so the single-select stands before any crossing can
// begin the drag; marker_drag.cpp carries no group
// machinery at all). RULED OUT, do not re-propose: a per-member nudge (a
// proposal loop with a whole-press wall refusal and a same-column merge), a
// rigid group drag (an intersection-clamped shared delta with a
// grabbed-anchored rigid commit), and a pointer deferral (a press holding its
// click back so a group could seed). The press-time click is no deferral in
// the other direction: it acts for the USER's benefit and decides nothing
// extra.
//
// THE GROUP-VERB DOCTRINE, the general rule this ruling instances — stated ONCE
// here, every verb site stating only its own class plus a pointer:
//   * GROUP-CAPABLE where the members are INDEPENDENT of one another or the
//     selection's SPAN is merely READ: the value edits (Ctrl+D's disable toggle,
//     Ctrl+N's inherit toggle, the Up/Down cent step, Delete) and, reading the
//     span, `m`, bare P (the phase-reset copy), the
//     zoom framing, and the undo/redo + paste
//     restores (which DEFINE a group selection). CTRL+N'S MEMBERS ARE
//     INDEPENDENT ONCE EACH IS RESOLVED AGAINST THE PRE-STATE (architect
//     2026-11-19): the
//     pass -> owner freeze reads the resolved walk, and read over what the
//     members before it had just become a member's frozen tempo would depend
//     on the iteration order — so every member resolves against the store as
//     it stood before the press, and none reads another's new state
//     (GuiWarpMarkersOps::toggle_inherits' head, warpmarkers_ops.cpp);
//   * FOCUS-COLLAPSE where the act is POSITIONAL: all horizontal movement. One
//     step per member and one rigid delta for all of them are different
//     arrangements, and neither can be the honest reading of "move the
//     selection".
// THE ARCHITECT'S GENERAL RULE, verbatim (2026-07-29), which generalizes those two
// classes and is the authoritative statement for any FUTURE verb: "the rule is - if
// group is relatively cheap to implement, implement it. otherwise, collapse to last
// sel." A HYBRID THIRD FORM IS EXPLICITLY REJECTED: a verb is EITHER
// group-capable OR collapses to the focus with the playhead landed on it; there is
// no third answer, and "collapse to last selected" means exactly
// collapse_to_focused + the marker land, the shape below.
// THE COLLAPSE+LAND SITE, ONE (by grep over Selection::collapse_to_focused's
// callers): this prologue serving
// both position nudges. The singleton tempo step's call (warpmarkers_ops.cpp's
// Up/Down arm) is NOT one: its selection is already a singleton, so the
// collapse moves no focus and owes no land.
// `t` AND `c` CARRY THEIR GROUP (architect 2026-07-30): no group rests
// SPANLESS, a group's cue being its members' stems (the selection's, render.h's
// THE STATES) plus the always-visible cursor on the focus, so neither owes a
// collapse (`t` keeping its selection-gated land, `c` jumping to the focus).
// Bare `0` is a PURE VIEWPORT MOVE.
// REACHABILITY: the ONLY producers of a resting
// 2+ selection are the multi-select click (the ctrl toggle, the barrel's or
// the add-to-selection lamp's), the
// bpm-mode open, the propagate paste, and the undo/redo restores — and every
// group verb that remains is span-read or member-independent, so nothing that can
// rest a group can then move it.
//
// THE WALL POLICY, ONE RULE FOR THE WHOLE PRODUCT (architect 2026-07-30) — stated
// ONCE here, every arm carrying only its own class plus a pointer back:
//   * A SINGLETON STEP CLAMPS ONTO ITS WALL. A position nudge moves one
//     millisecond's FRAMES (or, on the phase-reset column, one
//     HOP of the engine's analysis lattice — below), so a press starting near
//     an edge can overshoot mid-step; clamping is what makes the song edges
//     EXACTLY REACHABLE by the arrow buttons, in every column — the last
//     press before a wall moves the remaining distance, and the next is
//     refused. The hop, being the
//     longer stride, clamps sooner than a millisecond does.
//     Both position twins clamp. The SINGLETON TEMPO STEP's constructive clamp
//     (adjust_tempo_cents, warpmarkers_ops.cpp) conforms
//     — it steps whole CENTS on the cent grid, so clamping and refusing
//     coincide there and the value simply stops at the bracket edge.
//   * A GROUP PRESS REFUSES WHOLE. This is NOT a wall policy — it is GROUP
//     RIGIDITY: clamping per member would pool members at the wall and deform
//     their relative values. The one group arm, the group tempo step
//     (adjust_tempo_cents_group), refuses the whole press and carries its own
//     group-rigidity justification at its site.
// POST-CLAMP IDENTITY IS A SILENT NO-OP in both position twins: the clamped
// target is compared against the current frame and an equal result writes
// NOTHING — no undo push, no damage, no playback stop. That check is what makes
// the keyboard stop rule's refusal gating (playback_lifecycle.h) EXACT for the
// nudges, with no recorded deviation.
//
// This file is the type-free flesh SHARED BY THE TWO POSITION NUDGE TWINS, all of
// it singleton-scoped: an identical guard prologue (which owns the collapse),
// one step owner with its two units, an identical WALL-REGIME MIDDLE and an
// identical commit tail. The middle (position_nudge_landing below) is a delta
// clamped into the marker's own wall headroom, over warp's identity domain and
// over phase's mapped domain alike; the Left / Right buttons compare against
// it as a const owner, so no caller-side copy can drift from it (the
// truthful-buttons rule). The STORE read stays in each twin (the landing
// takes a frame), and the EOF wall is one wall — total_frames - 1 is the
// single marker wall both columns share. These four
// free functions collapse only the type-free parts: no templates, no
// callbacks, no policy structs (the naming-symmetry doctrine resists
// genericity — this is plain extraction of the shared flesh).
//
// TWO UNITS OF STEP, ONE ROAD (architect 2026-09-21). The position nudge is
// BARE Left / Right alone — no horizontal ladder on any column, placement
// being graphical — and every press carries a HorizontalArrowStep
// (gui_input.h), ONE step in the ACTIVE COLUMN'S unit (horizontal_arrow_step,
// the one fork): ONE MILLISECOND on the warp column (architect 2026-11-15),
// or on the PHASE-RESET column a
// HOP, which moves the reset by exactly one hop of the engine's analysis
// lattice through phase_reset_hop_step_frame (warp_frame_map_view.h, the
// owner the iteration cells land through: a translation of the reset's target
// image by kRs, so Right then Left returns it where it started give or take a
// frame). THE P COLUMN'S ARROW UNIT IS A HOP because the render depends only
// on the window a reset seeds in, so a finer nudge there is almost always
// silent — the recorded exception between columns (horizontal_arrow_step's
// own comment, gui_input.h). The hop takes the prologue, the landing
// owner's walls, the
// post-clamp identity no-op and the commit tail exactly as the millisecond
// does; only the landing's step arithmetic forks, in position_nudge_landing,
// and neither unit makes a pixel claim.

// Result of the shared guard prologue.
struct PositionNudgePrologue {
    bool ok      = false;  // false: the press refuses (silent, navigation-class)
    bool off_edge = false; // !ok by the painted-edge term, which the twins
                           // card (kMarkerNudgeOffEdgeCard, marker_nudge_
                           // verdict's declaration, app_state.h)
    bool merge   = false;  // undo-coalesce verdict for this press
    int  focused = -1;     // app.last_selected_marker, validated against the
                           // active store by marker_nudge_actionable
};

// The shared guard prologue of the TWO position nudges, and the site that makes
// the gesture a FOCUS ACT. Order is IDENTICAL in every twin and preserved
// exactly: (1) THE WHOLE REFUSAL SET, asked as ONE predicate —
// marker_nudge_actionable (app_state.h), which is also the Left / Right
// buttons' marker-lane face arm, and whose terms are this prologue's own seven
// (loading / empty audio, an empty selection — unreachable from the
// dispatcher, which routes an empty selection to the waveform-lane playhead
// step and never reaches here, so it is a belt — a missing focus, a dead
// sample rate, a non-positive samples-per-pixel, a stale focused index, the
// ONLY index this gesture reads) plus THE MARKER'S OWN WALL, asked up front
// rather than left to the twins' post-clamp identity no-op. A
// 2+ selection passes the wall term unconditionally: it collapses and lands
// below, so the press changes the screen whatever the step then does (the full
// reasoning is at the predicate).
// Then (2) the undo-coalesce verdict: coalesce_gesture reads the press's own
// repeat bit, which `synthesized_repeat` carries down from the on_key event
// that reached the handler, to pick its arm — identity for a held key, the
// fixed tap window plus a subject test for a physical re-press — and it just
// has to run before record_gesture stamps the burst in the tail. IT SITS
// BEHIND EVERY REFUSAL AND AHEAD OF THE COLLAPSE, and both halves of that are
// load-bearing: behind, because A WALL NO-OP TOUCHES NOTHING (2026-08-31 — a
// physical press invalidates the stamp inside that call, so a wall test behind
// it would split an undo run at the KEY while the same press on the greyed
// BUTTON left it whole; the rule and its discriminator are recorded
// at the call site and at Undo::coalesce_gesture); ahead, because the tap arm
// compares the PRE-collapse selection against what record_gesture stamped
// POST-collapse in the tail.
// Then (3) THE COLLAPSE: a 2+ selection collapses to its focus and the playhead
// LANDS on that focus (collapse + land at the CALLER of
// Selection::collapse_to_focused, the convention that keeps the land at the site
// which hands the marker lane a focus). The step below is therefore always the
// SINGLETON op on `focused`, bit-for-bit a singleton selection's.
// THE PROLOGUE OWNS THE PRESS'S ONE PLAYBACK STOP, immediately ahead of the
// collapse: past (1) every press acts — a 2+ press collapses and lands, a
// singleton is past its wall and its step moves the marker — so the keyboard
// stop rule (playback_lifecycle.h) demands the stop there for both shapes, and
// the rule's refusal gating stays exact (a press that writes nothing refused
// at (1) and stopped nothing).
// A 2+ press that then refuses at its wall in the twin keeps the collapse, the
// land and the stop — the collapse is the press's own committed act,
// not a prelude to the step (its damage is collapse_to_focused's own, so a
// refused press repaints correctly). That is the 2+ press's path alone, the
// wall being a term of (1): a SINGLETON at its wall refuses ahead of everything,
// having nothing to collapse and nothing to stop.
// Every marker is nudgeable, including the one at time 0 — the parser resolver
// normalizes the resulting arrangement at render/preview time, there is no
// gesture pin.
//
// PLAYHEAD RULE (architect 2026-07-23): the
// playhead FOLLOWS the nudged marker through the step (the actual follow lands in
// finish_position_nudge) — it steps with the marker so a later Space
// auditions FROM the marker. It is a re-land, not a repair: nothing DIVERGES from
// the focus to be towed (the two playhead forms and the empty towed category are
// stated once at land_playhead_on_marker, input_pointer.cpp — do not restate them
// here). The lead-in workflow (parking the playhead upstream to audition the
// approach) is supplied by the audition scrub instead. The twins keep their own
// GestureKind (WarpNudge / PhaseResetNudge).
// `synthesized_repeat` is the dispatching key event's platform repeat bit,
// consumed by the coalesce verdict alone. `step` is the press's own ONE STEP
// IN THE ACTIVE COLUMN'S UNIT (horizontal_arrow_step, gui_input.h) — ±1
// millisecond on the warp column, ±1 hop on the phase-reset column — consumed by
// the wall term at (1) alone, which is what makes the refusal DIRECTIONAL,
// the marker at frame 0 refusing Left and taking Right. The store
// size is no parameter: the wall predicate reads the ACTIVE column's
// store, which is the calling twin's own (the dispatch picks the twin by that
// same bit), and owns the stale-index belt with it.
PositionNudgePrologue position_nudge_prologue(
    AppState& app, const GuiAudio& audio,
    GuiPlaybackLifecycle& playback_lifecycle, Selection& selection,
    Viewport& viewport, Undo& undo,
    GestureKind kind, bool synthesized_repeat, HorizontalArrowStep step);

// ONE MILLISECOND A PRESS (architect 2026-11-15, the no-grid arc: "the
// left-right nudge should be recalibrated to one millisecond left or right
// ... we would convert 1 millisecond to the nearest amount in frames, and
// that would be the nudge step"; "using nudge on a coarse zoom is kind of
// pointless, because the point of nudge is fine-tuning"). The warp column's
// step is A TIME, NOT A PLACE: millisecond_frames (warp_coincidence.h) frames
// of the displayed domain — 44 at 44.1 kHz, 48 at 48 kHz — at every zoom, the
// arithmetic at position_nudge_landing's millisecond arm. IT MAKES NO PIXEL
// CLAIM: on the tablet at 44.1 kHz a press moves the mark 44 / 46 of a px at
// the working zoom (the 46-frame working column), 44 / 184 of one two levels
// coarser and 44 / 23, under two, at the deepest zoom, and the mark is
// painted at its exact place, so every press shows — by that fraction. A
// held key or button repeats the one-millisecond press. Right then Left
// returns the marker to its frame exactly in the source home (integer
// arithmetic off the walls). THE PHASE-RESET COLUMN'S HOP (2026-09-21) is the
// other unit, the render's quantum under the LIVE map
// (phase_reset_hop_step_frame), untouched by the millisecond.
// DO NOT CARRY A PIXEL GUARANTEE TO ANY STEP: the steps — a millisecond, a
// hop, a cent — are each one unit of their own by definition.

// WHERE ONE POSITION NUDGE WOULD LAND A MARKER — THE WALL-REGIME MIDDLE, one
// owner for both columns (the stores are the CALLER's business — this takes
// a frame — and the EOF wall is the SAME wall in both columns, total_frames - 1, the
// single marker wall stated at drop_phase_reset_at_position). Given the
// marker's resting frame it returns the frame the press would commit:
//   (1) THE STEP, as a plain integer delta, forked on the step's UNIT and on
//       nothing else: MILLISECONDS moves the marker's displayed image by
//       `count` × millisecond_frames and lands it back through the
//       DISPLAYED map (ONE MILLISECOND A PRESS, above); HOPS, the
//       phase-reset column's arrow unit (architect 2026-09-21), takes `count`
//       hops through phase_reset_hop_step_frame over the LIVE map
//       (live_warp_frame_map — the render's, the map the iteration cells
//       land under), which may answer a frame off the piece;
//   (2) WALLS WIN BY CLAMPING, in the marker's own headroom [0 - orig,
//       wall - orig] — integer arithmetic, non-empty because every stored
//       marker rests in [0, wall] — so a press that would overshoot lands ON
//       the wall, exactly reachable. This is the UNIFIED WALL POLICY
//       (architect 2026-07-30: singleton steps clamp, group presses refuse
//       whole), stated once at the head of this file;
//   (3) the [0, wall] belt on the sum — provably dead (all-integer
//       int64 sums under the headroom clamp above), kept as cheap insurance so
//       a future edit cannot commit a wall-illegal frame, which would save a
//       load-fatal file.
// A DEGENERATE GEOMETRY ANSWERS `orig_frame` ITSELF (no audio, a dead sample
// rate, a non-positive samples-per-pixel): those are the prologue's own
// refusals, so the press writes nothing there, and answering the resting frame
// is how this owner says so to a caller that has not run the prologue. The
// TWINS reach it past those guards, so the arm is the FACE's alone in practice
// — the waveform lane's landing owner (playhead_arrow_step_landing) treats its
// degenerate state the same way.
// THREE READERS: the TWO nudge twins, whose committed frame this IS
// (GuiWarpMarkersOps::nudge_selected_markers and
// GuiPhaseResetMarkersOps::nudge_selected_phase_resets — each
// still owns its own store read and its post-clamp identity no-op), and
// marker_nudge_actionable (app_state.h), the Left / Right buttons' marker-lane
// wall term, which compares this landing against the resting frame.
//
// THE WALL VERDICT IS THE HEADROOM'S, in both units: a millisecond is at
// least one whole frame (millisecond_frames' floor; under a real map
// its image is at least a sixteenth of that, the value brackets' slope
// floor, still over a frame at every product rate), and a hop's landing
// leaves the resting seed
// window by exactly one in the pressed direction, so off a wall either step
// moves the marker by at least one frame, and after the clamp at (2)
// `landing == orig_frame` iff the headroom in that direction is zero — a step
// near a wall lands exactly ON the wall. The face and the act hand this owner
// the same step (marker_nudge_actionable, app_state.h).
int64_t position_nudge_landing(const AppState& app, const GuiAudio& audio,
                               int64_t orig_frame, HorizontalArrowStep step);

// The shared type-free COMMIT TAIL. Each twin calls this AFTER it has: run its
// regime middle, mutated its store, run reorder_markers_by_time +
// remap_marker_indices_after_reorder, collected touched_live, and done its own
// typed undo merge/push block (push_undo_warp / push_undo_phase_reset — those
// stay in the twins). The tail then, in order:
// (a) record_gesture — the burst's SETTLEMENT, which is why the twin's own
//     coalesce verdict rides down here as `merged`: it re-stamps this press's
//     kind for the next coalesce test, OR, on a merged press whose mutation
//     returned the stores to the burst entry's own snapshot, POPS that entry
//     (the byte-equal pop, architect 2026-09-01 — the rule and its whole
//     derivation are at Undo::record_gesture). Post-mutation by construction:
//     the twin has already written its store before it reaches this tail;
// (b) recompute_dirty;
// (c) invalidate_waveform_area — this full-waveform damage also OWNS the
//     nudged marker's STEM move: a nudge shifts that marker's frame, and its
//     stem repaints at the new column here. Every SELECTED marker stems,
//     enabled or disabled (2026-11-11), and the full-waveform damage covers
//     the moved one whatever its state;
// (d) invalidate_clock_area — the unified bottom row's clock cell, through
//     the playhead move at (e);
// (e) PLAYHEAD FOLLOW: move_playhead_to the nudged marker's committed frame
//     through the two-step placement basis (source_frame_to_active_domain —
//     identity in source view, warp's home and S+P, a real map in T+P;
//     committed_focused_frame is reorder-independent). The movement owner owns
//     the clamp, invalidation, and keep-visible edge-align, writing the
//     cursor field only (playback was stopped by the prologue, past its
//     refusal verdict and ahead of the first write, so this tail always runs
//     stopped).
// (f) THE CAMERA, the press's REQUIRED NudgeCamera (gui_input.h, no default;
//     at every zoom and on every column; the hold posture's answer,
//     nudge_camera, read at the dispatch, 2026-09-23): FollowEdge (the
//     posture dark) adds nothing to (e)'s keep-visible edge-align, so the
//     camera holds while the marker stays on screen and walks with it at the
//     edge once it would leave; HoldColumn (the posture an explicit centring
//     armed) runs
//     Viewport::hold_subject_column_after_nudge, which places the viewport so
//     the playhead (e) just landed paints at the exact place the marker held
//     before the nudge (prior_focused_frame, the twin's pre-write frame,
//     taken to its exact x on the displayed map and item_viewport_basis
//     — the painted basis — ahead of (e)'s edge-align; architect
//     2026-09-24, strictly as painted), clamped to the waveform's edge columns
//     (the rule at its definition, viewport.cpp). Every press that reaches this tail moved its
//     marker — the twins return on the post-clamp identity no-op first — so a
//     walled press moves no camera, and a held key's or button's repeats each
//     run the tail with the camera their chord carries.
// (g) THE A/B AUDITION: a position nudge ENDS it, unconditionally — (e) above
//     and the prologue's collapse land are both playhead movement owners and
//     end it there (the rule at Viewport::move_playhead_to, viewport.cpp), so
//     the nudge owes no call of its own.
// (h) target_render.trigger(): warp and phase reset positions are both
//     engine inputs, so each twin passes its own GuiTargetRender and a nudge
//     dirties the preview.
//
// NO SYNCHRONOUS RE-WARP is needed at either home: the warp nudge authors in
// warp's SOURCE home view, where the source waveform pixels do not depend on the
// warp map, and the phase nudge moves phase reset positions, which do not change
// the warp map either — so there is no displayed target plate to re-warp. The
// target preview still invalidates (a source-view warp edit changes the rendered
// target buffer), so the view-independent trigger stays.
//
// ORDERING: both twins take ONE order (push in the twin, then this tail:
// record, invalidate, playhead). The COMMITTED BYTES are the same whatever
// the order: the undo push/record read neither the playhead nor the selection (their
// snapshots capture the marker stores, engine settings, tab, and the hint indices
// only — not the cursor), the playhead follow does not read undo
// state.
//
// WHERE record_gesture SITS IN THE TAIL — a bounded accepted cost of the TAP
// ARM's clock (architect 2026-08-01), recorded in full so it is not re-derived
// as an oversight. The tail records THEN moves the playhead, so a
// possible SYNCHRONOUS kick_waveform_sync (the playhead move runs one when the
// follow shifts the viewport — the offscreen-focused case) falls AFTER the
// accepted-event timestamp record_gesture stamps. The window to the next press is
// therefore measured from before that render rather than after it, i.e. the render's
// own duration is spent out of the WINDOW: a manual re-tap landing in the last
// few milliseconds of it could open a fresh entry where a
// record-after-playhead ordering would have merged. ACCEPTED and not worth a
// reorder: the constant is a human-tapping interval, a discrete sync render is a
// small fraction of it, and the direction of the error is the SAFE one (an extra
// undo entry, never a wrong merge). The HELD-KEY arm is untouched — it consults no
// clock at all. One order serves both twins, so warp and phase cannot diverge,
// and record_gesture reads nothing the playhead move writes.
void finish_position_nudge(
    AppState& app, const GuiAudio& audio, Viewport& viewport, Undo& undo,
    GestureKind kind, bool merged, int64_t prior_focused_frame,
    int64_t committed_focused_frame, NudgeCamera camera,
    GuiTargetRender& target_render);
