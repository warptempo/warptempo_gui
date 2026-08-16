#pragma once

#include "engine_settings.h"
#include "gui_input.h"
#include "history_diff.h"
#include "playback.h"
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
// `region.active = true`: the drag's motion path is the ONE FORMING writer since
// 2026-08-05, every entry reaching it through the one arm arm_region_drag_at, so
// the inventory IS arm_region_drag_at's callers plus the callers of the body that
// wraps it, place_playhead_and_arm_region. THE GREP HAS RETURNED TWO SITES SINCE
// 2026-08-15 and the second is NOT a former: handle_show_region's SEED, which
// copies the trim window in once (input_trim.cpp — it was bare `x`'s no-region
// arm for a day and is Ctrl+Shift+X's own since 2026-08-16). It arms no drag, so
// it is outside this inventory by kind rather than by oversight, and the
// sentence this line used to carry — "the ONE writer" flat — was falsified the
// day the seed landed.)
// THERE IS ONE FORMING GESTURE, THE REGION FORMER, in THREE entries
// (re-derived 2026-08-12 at the eighth glass ruling's touch half —
// PAN-PRIMARY: the plain drag
// on the navigation surface is the GRAB-PAN now, so the region is the
// deliberate secondary act — SHIFT on the desk, the REGION HOLD on glass):
//   * the SHIFT-exact press on the NAVIGATION SURFACE — the WHOLE waveform,
//     the RULER lane, and the MARKER lane's empty stretches (the lanes joined
//     the y-gate with the same ruling, being the upper half's extension; the
//     LOWER half left it that day and CAME BACK 2026-08-13 — "shift plus drag
//     to map out a region should also be allowed in the lower half, for
//     consistency, since the drag motions are allowed from the top half" —
//     which supersedes the eighth ruling's "no region sweep at all in the
//     lower half"; a shift press on a FLAG stays the range click). It runs the
//     one placement body
//     (place_playhead_and_arm_region): deselect-all, playhead at the clicked
//     column, live-playback reseek, then the arm — the region ANCHORS AT THE
//     CLICKED COLUMN (2026-08-05) and the drag carries the playhead on the
//     moving end, so it lands where the mouse releases. A motionless shift
//     click-release lands the playhead and rests NO region;
//   * the `h` HISTORY VIEW'S own SHIFT former, over the view's whole
//     navigation surface (the same rect as the live one since 2026-08-13 —
//     full waveform height plus the same two lanes — so the view is no longer
//     the exception it was), the same recipe with the MODE's focus clear in
//     place of the store deselect;
//   * the TOUCH REGION HOLD (the eighth ruling's touch half): a ~500 ms
//     one-finger hold on the same navigation surface (the touch pan zone)
//     expires into begin_touch_region, which FORKS on the mode exactly as
//     the shift press forks at its two claims — the live arm through the
//     same placement body, the mode arm through the same view-local recipe —
//     and the drag rides the same one motion path, so hold-then-drag sweeps
//     a region on glass and a motionless hold-lift is the placement.
// (The PLAIN entries — the upper-half press, the empty-lane parity press, the
// view's full-height press and the one-day RULER former — all LEFT this
// inventory 2026-08-12: a plain press on the navigation surface is the
// PENDING PAN now, ScrollDragState, whose motionless release runs the
// placement as a deferred CLICK ACT and arms no drag.)
// EVERY REGION FORMER DROPS THE SELECTION ITS SURFACE OWNS — the family rule,
// stated here and pointed at from the sites (architect-RATIFIED 2026-08-05,
// promoting what had been the coder's reading of the mode's arm into the
// ruling). The LIVE entries DESELECT at press/begin through the placement
// body,
// leaving the STORE selection EMPTY throughout the drag; the MODE entries
// clear THE MODE'S focus and diff-flag selection instead, through the one
// clearer that takes the pair, and touch no store selection at all by the
// view's own standing rule. So no former anywhere leaves a selection standing
// beside the span it is drawing, and the surface simply decides WHICH
// selection that is.
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
// carried. NOTHING BRANCHES ON WHERE A SPAN CAME FROM, which is what that enum's
// deletion bought and is the claim that matters here. The producers are TWO: the
// FORMER in its three entries above, and — since 2026-08-15, on its own chord
// since 2026-08-16 — THE SHOW-REGION SEED, which copies the trim window in
// once and then lets go (handle_show_region, input_trim.cpp). The sentence
// this paragraph used to end with, "a region has ONE origin now — the user drew
// it", was true from the span form's deletion until the seed existed and is
// superseded by the count rather than by the principle: a seeded span is
// indistinguishable from a drawn one to every reader, deliberately.
//
// Bare `x` is SET-ONLY and consumes THIS highlight: a live region trims to it,
// DESELECTS (the setter rule) and then CLEARS the region — its job is done. A
// DEGENERATE result — an inverse-mapped span coming out end <= begin — refuses
// rather than writing a pair the crossed-commit auto-clear would destroy (at
// handle_trim_x). WITH NO REGION `x` IS A CONSUMED NOTHING — it SEEDED one at
// the current trim window for a day (2026-08-15..16) and the architect moved
// that act onto its own chord, Ctrl+Shift+X (handle_show_region,
// input_trim.cpp), because one key had come to mean two unrelated things: show
// and commit. THE SEED IS THE SECOND HALF OF THE MODEL BELOW either way, and it
// is ONE-TIME rather than the retired continuous sync.
//
// CLEARED wholesale on: file load, the A/B tab switch, the S/T audio-view switch
// and the W/P marker-column switch (each flips the domain or the owning column out
// from under the span), the former's press/begin (it dissolves any resting
// highlight at mouse-down, before it knows whether the gesture is a click or a
// fresh region drag — via arm_region_drag_at, all three entries), the DEFERRED
// CLICK ACT at a plain navigation-surface press's motionless release
// (run_nav_click_act — the placement is a point command; THE PAN ITSELF NEVER
// CLEARS, a crossed plain drag being a pure viewport move, and THE SAME ACT'S
// SCRUB ARM leaves the region alone too, returning above the dissolve — that
// act being the region's PREVIEW gesture), the `h` view's
// three edges (the view-local rule above), and the kick validator's live-domain
// reclamp when a bound falls outside a shrunken domain. The full clear-site
// enumeration lives at clear_region_highlight's declaration (input_handler.h).
struct RegionState {
    bool    active  = false;
    int64_t a_frame = 0;   // the press-anchor endpoint
    int64_t b_frame = 0;   // the far (pointer) endpoint
};

// THE REGION IS A MANIPULABLE SPAN, NOT SWEEP-ONCE SCRATCH (architect
// 2026-08-15, uniting two loose ends — what the region is FOR, and how trim is
// manipulated on a 9 px lane a fingertip covers whole): a STANDING region
// carries the OVERVIEW BOX'S OWN THREE MOTIONS on the waveform — drag inside
// it to MOVE it, drag within a bound's grab band to move THAT bound, drag
// anywhere else to PAN as ever — and `x` still commits it to trim. The other
// half closes the loop: CTRL+SHIFT+X SHOWS one at the current trim window
// (handle_show_region, input_trim.cpp, and the icon row's IconShowRegion
// toggle), so the same three motions edit the trim itself on a surface hundreds
// of pixels tall instead of nine. THE SHOW ACT SPENT ONE DAY ON BARE `x`'s
// no-region arm (2026-08-15..16) and the architect split it off onto its own
// chord and its own button, one key having come to mean two unrelated things;
// the model is unchanged and only the show's spelling moved.
//
// WHY IT MATTERS BEYOND CONVENIENCE, and this is the argument to keep if
// anyone later proposes collapsing it: TRIM HAS NO UNDO, excluded from the
// stacks by ruling, so a mis-dragged endcap is unrecoverable except by
// re-dragging it. Under this model the region is free scratch that may be
// nudged as much as one likes and only `x` commits — which gives the one
// feature that cannot be taken back a PREVIEW STAGE.
//
// THE SEED CARRIES NO CONTRACT, and that distinction is the architect's own:
// the retired trim-window highlight sync was a CONTINUOUS INVARIANT binding
// the two together, while this is a ONE-TIME SEED — once shown, the region and
// the trim move independently and neither chases the other. This is not that
// ruling coming back.
//
// THE REGION'S LIFECYCLE IS DELIBERATELY UNCHANGED: it still dies to any
// playhead move or selection change (the inventory at clear_region_highlight,
// input_handler.h), the architect ruling that acceptable outright ("the region
// collapsing on an accidental tap is not a big deal") — and a CLICK inside the
// region is exactly that, which is also what rescues the degenerate case: a
// full-window region leaves nothing to pan and nothing to move until a single
// click destroys it.
//
// ONLY THE WAVEFORM ANSWERS, by his ruling: the RULER and the MARKER LANE stay
// plain navigation surface throughout, which is what keeps a pan and a zoom
// reachable while a region covers the waveform entirely.
//
// The hit verdict. Its owner is GuiInputHandler::region_manipulation_hit
// (input_pointer.cpp), which is meaningful only while app.region.active; the
// two bounds are the region's endpoints projected to columns on the DISPLAYED
// (plate) basis — the same basis the highlight paints on, through the
// painter's own region_columns owner, so a grabbed bound is exactly a painted
// one. BoundLo / BoundHi are the NORMALIZED (painted) endpoints rather than
// the stored a/b pair, which rests in DRAG order: what the user grabs is the
// left or the right edge of what is drawn.
enum class RegionHit { None, Move, BoundLo, BoundHi };

// THE REGION EDIT DRAG — the overview box's drag borrowed whole rather than
// reinvented (contract above; the shape is OverviewDragState's). ABSOLUTE and
// X-ONLY: per event the moved endpoint (or the whole span, its grab offset
// preserved) follows the pointer's column and dy is never read. NO CAPTURE, no
// stem, no playhead movement, no selection change, and IT DOES NOT CLEAR THE
// REGION — it is the region's own editor. IT COMMITS NOTHING TO TRIM; only `x`
// does.
//
// Clamped at the SONG WALLS. A Move that would run a bound past a wall stops
// there with the span INTACT — the overview box pan's own behaviour, which is
// also what makes a full-window region's Move a silent no-op.
//
// IT IS A DISPLAYED-BASIS GESTURE, the marker and trim drags' third sibling
// (codex round 20): the press's grab offset and every motion column convert on
// the PLATE basis the hit test and the painter share
// (GuiInputHandler::region_edit_frame_at_column), and the waveform worker's
// dispatch freeze and completion-drop gate name this drag with those two, so no
// job may publish a new basis mid-hold. It landed reading the LIVE viewport and
// spp instead — hit one span, moved another — which is what those two halves
// close.
//
// THE KIND FOLLOWS THE CROSSING. The motion normalizes min/max, so pushing one
// bound past its partner makes the grabbed point the OTHER bound; `kind` is
// rewritten from that same compare on every bound event, because it is the one
// owner of "which edge is this" and the live cursor reads it — a stale kind
// would leave the resize arrow naming the edge the hand used to be on.
//
// A MOTIONLESS PRESS-RELEASE ON THE REGION IS NOT A MANIPULATION: it falls to
// the waveform's ordinary click act, each half's own and unchanged — the UPPER
// half's playhead placement, which dissolves the span like every other point
// command, and the LOWER half's scrub, which deliberately does not (that is the
// region's PREVIEW gesture: click inside a span to audition it and the span
// rests). SO THE DEGENERATE CASE'S ESCAPE HATCH IS THE UPPER HALF'S CLICK, and
// it is a real one — a full-window region covers both halves. The architect
// ruled the fall-through deliberately, so the release arm runs the deferred act
// rather than nothing.
//
// AN ACCEPTED DEGENERATE REST: dragging one bound exactly onto the other leaves
// a span whose two endpoints share a painted column, which the highlight
// declines to draw (paint_region_ground's zero-width return) while the region
// stays ACTIVE. It is not engineered around: both grab bands still cover the
// spot and still wear their edge cursors, so the span can be pulled back open,
// and one upper-half click clears it. The region former's release-time sliver
// dissolve is deliberately NOT borrowed here — this gesture must never clear
// the span it edits.
//
// Cleared on button release / lost button, by the force-end finalizer, and on
// file load; pointer gestures have no cancel, so any end simply rests what the
// last applied event left.
enum class RegionEditKind { Move, BoundLo, BoundHi };

struct RegionEditDragState {
    bool           active = false;
    bool           moved  = false;   // crossed the threshold into a real drag
    RegionEditKind kind   = RegionEditKind::Move;
    int            press_x = 0;      // press position (window px): the gate,
    int            press_y = 0;      // and the motionless release's click column
    // Move only, both in ACTIVE-domain frames and captured at the press: the
    // offset from the press column's frame back to the span's LO bound (so the
    // grabbed spot stays under the pointer) and the span's WIDTH (hi - lo),
    // held for the drag's life so the wall clamp can slide the pair rigidly.
    int64_t        grab_offset = 0;
    int64_t        span_frames = 0;
    // Bound drags only: the FIXED partner bound's active-domain frame, captured
    // at the press — the edge drag's own shape, so the un-dragged end holds
    // bit-exactly whatever the moving one does.
    int64_t        fixed_frame = 0;
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
    // 2026-07-20 decoupling): the CLICK ACT the THRESHOLD CROSSING runs
    // immediately before begin_drag LANDS the playhead on
    // the pressed marker (source_frame_to_active_domain then
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

// State for the REGION FORMER — THE ONE REGION GESTURE, shift+drag on the
// desk and the ~500 ms region hold on glass
// (architect 2026-08-12, the eighth glass ruling, PAN-PRIMARY: the plain drag
// is the grab-pan now, so the region is the deliberate act and takes the
// secondary form on both devices). The SHIFT-exact PRESS
// on the NAVIGATION SURFACE — the WHOLE waveform, the RULER lane, and
// the MARKER lane's empty stretches (its y-gate GREW to the lanes with that
// ruling and shrank off the LOWER half, which came BACK 2026-08-13 with the
// two-halves ruling — the halves take the same drag motions, so they take the
// same former; a shift press on a FLAG stays the range click) — does its
// press-time work (deselect-all,
// playhead placement, live-playback reseek — it never SELECTS a marker),
// DISSOLVES any resting highlight at mouse-down, and arms this drag; motion
// past the shared press-becomes-drag threshold (kDragMovedThresholdPx) extends
// app.region from the press frame to the pointer column. Under SELECTION
// FLOWS DOWNWARD ONLY (architect 2026-07-23) the drag does NOT select the
// span's markers — the selection stays EMPTY from the press's deselect-all
// through release. THE DRAG CARRIES THE PLAYHEAD (architect 2026-07-30): each
// changed column writes the cursor to the MOVING endpoint, both arms through
// the one motion path, with no viewport scroll and no playback reseek per
// motion — which is what makes the playhead LAND WHERE THE MOUSE RELEASES. A
// sub-threshold press-release is the placement click (deselect + playhead at
// the column) and simply disarms — the highlight already dissolved at press,
// so there is no release-time collapse. THREE ARMS REACH arm_region_drag_at
// (membership re-derived 2026-08-12 at the touch half; the authoritative
// inventory is at
// RegionState): the LIVE shift former above, through the one placement body
// (place_playhead_and_arm_region), the `h` history view's OWN shift
// former (handle_history_mode_press), which clears the MODE's focus +
// selection instead of the store selection and rides the same motion path,
// and the TOUCH REGION HOLD's begin (begin_touch_region — the pan zone's
// stretched window expiring at the beat), which forks on the mode into
// those same two recipes and drives the drag through the region hooks.
// (The plain upper-half press, the empty-lane parity press and the one-day
// RULER former — with its deferred-dissolve `ruler` flag — all LEFT this list
// 2026-08-12: their plain presses are the PENDING PAN now, ScrollDragState,
// and their motionless clicks defer the placement to the release.) A
// completed drag rests the region on release UNLESS its final on-screen span
// is under the same kDragMovedThresholdPx gate — the gate latches once past
// the arm and never re-engages, so a jitter drag could otherwise rest a
// sliver, which dissolves like a click instead (end_region_drag_min_size_check,
// at both end points). The drag never touches the selection anywhere — the
// press's deselect was the committed act, and downward-only is structural
// (there is no selection write in the drag or at its ends). ESC DOES NOTHING
// TO A DRAG IN FLIGHT: pointer gestures have no cancel, so a mid-drag Esc is
// swallowed by the drag-modal gate and the drag keeps extending under the
// pointer; the release rests the region where it stands (under the sliver
// gate). Esc clears a RESTED span (architect 2026-07-30, the arm in on_key) —
// clear but never cancel, and the gate is what makes those two cases
// distinct. This state was the first to lose its pre-press snapshot — the
// whole family followed. The rule is at the drag-modal gate
// (input_handler.cpp). Session-only, never undoable.
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

// THE MARKER FLAG'S PENDING CLICK — armed by ANY of the flag box's three
// presses (plain, shift, ctrl), and it holds the WHOLE click back until the
// gesture resolves (architect 2026-08-15: "all actions should be on mouse-up /
// finger-up", the act-at-lift sweep reaching the flag; the whole model is at
// on_button_press's marker branch, input_pointer.cpp).
//
// THE PRESS ARMS AND COMMITS NOTHING. A MOTIONLESS RELEASE runs the click act
// (stop, the three-way selection fork, the land, the region clear, and the
// plain arm's double-click half). A CROSSING of kDragMovedThresholdPx
// (Chebyshev from the press; the one generic 8px gate shared by every
// press-becomes-drag surface) converts a PLAIN arm into the reposition drag —
// running the click act MINUS its double-click half first, so a drag's
// user-visible outcome is byte-identical to the press-time model's — while a
// SHIFT or CTRL arm that crosses commits NOTHING, there being no gesture for it
// to become. A lost button, the force-end finalizer and the touch layer's
// ABNORMAL end (the motionless-hold upgrade) all commit nothing. This is
// ScrollDragState's own shape and the overview lane's Pending, in the flag's
// vocabulary.
//
// THE PRESS-TIME MODIFIERS ARE CARRIED, not re-read at the lift: MODIFIERS ARE
// READ AT THE PRESS AND NEVER RE-READ (the modal release's own rule, spelled at
// ChromePress::shift), and it applies here without exception because a flag
// click IS an armed act — unlike a continuous viewport gesture, which arms no
// act, which is why the nav drag's LIVE ctrl is not a counter-example. The
// double-click VERDICT is carried for the same reason: it is a fact about the
// user's second press, decided at the moment its timing test is about (the
// snapshot at on_button_press's top has already been cleared by then). The
// GATES the verdict then meets are NOT carried and are re-asked live at the
// lift — the chrome lift's own rule: a tab lock or a view switch may change
// under a held button, and the LIFT decides.
//
// SUPERSEDED, AND CORRECT FOR ITS OWN MODEL: until 2026-08-15 the press
// committed the whole click (single-select, land, span collapse, double-click
// seed/consume) and armed this state for the DRAG alone, so "a release / lost
// button / force-end has nothing to complete and simply DISARMS" was true, and
// "shift never arms it" with it. The architect's counter-argument of the same
// sweep — "marker work keeps its immediacy: flag clicks act at press time,
// unlike the navigation surface's deferred click" — is superseded by his own
// later ruling, not deleted. (Older still: the GROUP-drag deferral of
// 2026-07-23, which withheld a click so begin_drag could seed the intact group,
// died 2026-07-29 with the group drag itself — groups are never moved, the
// doctrine at the head of position_nudge.h.)
//
// Deferring begin_drag to the crossing also keeps its pre-drag snapshot (the
// undo payload) and its wall math exact — nothing mutates the store between
// press and crossing; the selection capture that used to be on that list was
// deleted with the cancels (2026-07-29).
//
// THE TWO AUTHORING GATES GUARD THE DRAG, NOT THE CLICK, and they live at the
// CROSSING: a read-only tab and an off-home column still select, still land the
// playhead and still open no editor (read-only protects the AUTHORED MUSICAL
// CONTENT — the marker stores and the engine settings — and a selection is
// navigation), so the arm itself is unconditional for all three shapes.
//
// Session-only, never serialized. Cleared on the crossing (either the drag
// takes over or the arm is spent), on release / lost button, by the force-end
// finalizer, and on file load. Esc does nothing (pointer gestures have no
// cancel — the rule at the drag-modal gate in input_handler.cpp).
struct PendingMarkerPress {
    bool active         = false;
    int  marker         = -1; // marker index the press hit (active view's list)
    int  press_x        = 0;  // press position (window px): the gate + drag anchor
    int  press_y        = 0;
    // The press-time modifier shape, reproducing the three-way click fork at
    // the lift. Both false = the plain arm, the only one that becomes a drag.
    bool shift          = false;
    bool ctrl           = false;
    // The plain arm's double-click verdict, evaluated at the press against the
    // candidate snapshot and spent at the lift (consume = open the editor if
    // the three live gates pass; else seed a fresh candidate at the PRESS
    // point, which keeps the pairing press-to-press).
    bool double_click_consume = false;
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
// rather than at their press, the bound set having moved to the lift with the
// act-at-lift sweep's last four acts (PendingClickAct below owns that hand-over,
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
};

// Which act a PendingClickAct is holding. None = nothing armed.
enum class PendingClickKind {
    None,
    TrimBoundSet,     // the trim bar's ctrl (begin) / ctrl+shift (end) bound set
    TrimBarFraming,   // the trim bar's span-framing double-click, second press
    EmptyLaneCreate,  // the empty marker lane's create double-click, second press
    HistoryDiffFlag,  // the `h` view's three diff-flag clicks
};

// THE ACT-AT-LIFT SWEEP'S LAST FOUR ACTS, IN ONE RECORD (architect 2026-08-15,
// finishing "all actions should be on mouse-up / finger-up" past the marker flag
// that moved earlier the same day): the trim bar's two bound-set clicks, the
// trim bar's span-framing double-click, the empty marker lane's create
// double-click and the `h` view's three diff-flag clicks. NONE of them was ever
// RULED a press-time exception — they were simply never in the sweep's scope,
// which is what the marker flag's own conversion record says about itself.
//
// THE SHAPE IS THE PRODUCT'S, not a new one: the press ARMS a record carrying
// what the act will need (its press POINT, and the press-time MODIFIERS where an
// act forks on them — modifiers are read at the press and never re-read, the
// modal release's own rule at ChromePress::shift); a MOTIONLESS release runs the
// act, RE-ASKING every live gate; a CROSSING of kDragMovedThresholdPx either
// hands the arm to the gesture it can become or, where there is none, commits
// NOTHING and disarms (the overview Pending's rule); and a lost button, the
// force-end finalizer and the touch layer's ABNORMAL end all commit nothing.
// Read PendingMarkerPress above for the same contract in the flag's vocabulary.
//
// ONE RECORD FOR FOUR ACTS, and that is the round-21 lesson applied rather than
// a shortcut: four parallel structs would be four entries in EVERY list that
// must see an armed press — any_pointer_gesture_active, the drag-modal key gate,
// the wheel gate, the cursor map, the force-end finalizer, the file load — and
// "three spellings of one rule is exactly where a fourth gets forgotten" is a
// failure this project has already had. The four share one lifecycle exactly, so
// they share one field and one set of ends, and the KIND is read only where the
// act itself is run. It is NOT a reuse of PendingMarkerPress, which is a
// different subject with a different resolution: that record's plain arm becomes
// the reposition drag and carries a double-click VERDICT of its own, and a diff
// flag is not a store marker.
//
// THE ACTS ACT ON THE ARMED SUBJECT — the press column, and the armed flag index
// — and NEVER on a re-hit at the release's coordinates, for the three reasons
// recorded at run_marker_click_act (touch delivers the press at the finger's
// DOWN point and the release at its last position; the flag hit stash is not
// frozen under a pending; the navigation surface and the overview lane both act
// at their PRESS point, sub-threshold travel being jitter).
//
// THE TWO DOUBLE-CLICK KINDS CARRY A VERDICT-BEFORE-ARM RULE, which is the whole
// difficulty of the conversion and is stated here because it is a PROPERTY, not
// an implementation detail. Both consumes used to run at the press DELIBERATELY
// BEFORE the press's other arm ("consume-before-arm, so the second press never
// becomes a drag" / "...never becomes a pan"). The verdict is still taken AT THE
// PRESS — a verdict is not an act: it commits nothing and changes no state, it
// records what the user's second press MEANT at the moment the timing test is
// about, which the lift can no longer ask because on_button_press's top-of-frame
// clear has emptied the candidate field by then — and a TRUE verdict arms THIS
// record ALONE, arming neither the trim drag nor the nav pan. So the second
// press of a double-click still physically cannot become a drag.
// ON THE TRIM BAR THAT PROPERTY IS LOAD-BEARING AND NOT MERELY TIDY: TRIM HAS NO
// UNDO — it is excluded from the stacks by ruling — so if a true verdict also
// armed the endcap / bridge drag, a double-click whose second press drifted 8 px
// would silently move a trim bound with nothing to take it back. On the empty
// lane the stakes are lower (the create IS undoable) and the rule is kept anyway,
// so the two consumes read as ONE rule rather than two.
//
// THE ONE KIND THAT BECOMES A GESTURE IS TrimBoundSet, and its crossing is the
// marker flag's crossing exactly: it RUNS THE SET at the press column and then
// hands over to the endcap drag on the bound it just set, so a ctrl-press-and-drag
// is byte-for-byte the gesture it has always been (set at the click, then drag
// that bound live) and only the timing of a motionless click moved. The set's
// whole tail travels with it as one unit — the strictly-inside refusal, the
// playback stop, the commit tail's playhead park and region clear, the setter's
// deselect — because they are one act (set_trim_bound_at_click).
//
// Session-only, never serialized. Cleared on the crossing (either the trim drag
// takes over or the arm is spent), on release / lost button, by the force-end
// finalizer, and on file load. Esc does nothing (pointer gestures have no cancel
// — the rule at the drag-modal gate in input_handler.cpp).
struct PendingClickAct {
    PendingClickKind kind = PendingClickKind::None;
    int  press_x  = 0;   // press position (window px): the gate + the act's column
    int  press_y  = 0;
    // TrimBoundSet: which bound the click writes (ctrl = begin, ctrl+shift = end).
    bool is_begin = false;
    // HistoryDiffFlag: the index into app.history_mode.flags the press resolved.
    int  flag     = -1;
    // HistoryDiffFlag: the press-time modifier shape, reproducing that surface's
    // three-way fork at the lift (both false = the plain focus click).
    bool shift    = false;
    bool ctrl     = false;

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
// deferred-dissolve model, the one-day ruler former's own pattern generalized
// to the whole surface):
//   * a MOTIONLESS RELEASE (never crossed kDragMovedThresholdPx) runs THE
//     CLICK ACT at the press column, forked on the pressed half
//     (run_nav_click_act, input_pointer.cpp). UPPER half — everything the old
//     press-time placement did: deselect-all (the mode-focus clear in the `h`
//     view), region dissolve, playhead to the column, live-playback reseek,
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
//     NO playhead, clears NO region and NO selection, seeds nothing.
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
// FOUR kinds on one PLAIN left press, decided at the press by the box geometry
// (the painter's own derivation, overview_box_span — one owner, app_state.cpp):
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
//   * Pan — INSIDE the box: the press grabs the box where it is, its grab-point
//     offset preserved, and the drag is THE BOX-FOLLOWS-POINTER PAN, PAN ONLY:
//     per motion event the viewport centers on (pointer's whole-song
//     position − grab_offset), X ONLY — the handler never reads dy, so vertical
//     motion is ignored structurally ("no cross axis allowance for up/down":
//     this pan has no zoom axis at all). A motionless release inside the box is
//     a consumed nothing, the lane's v1 rule standing.
//   * Pending — OUTSIDE the box, and THE PRESS ARMS AND ACTS ON NOTHING. It is
//     the navigation surface's own pending click (ScrollDragState above) in
//     this lane's vocabulary, for the identical reason, and it resolves two
//     ways:
//       - A MOTIONLESS RELEASE runs THE TELEPORT at the PRESS column
//         (run_overview_teleport — the viewport CENTERS on that column's
//         whole-song position through the scroll_viewport funnel, a pure
//         viewport move, zoom level unchanged). THE ACT MOVED TO THE LIFT on
//         2026-08-15 with this redesign, which closes the last press-time act
//         ON THE NAVIGATION SURFACES — the act-at-lift sweep had excluded it BY
//         OMISSION, and the sweep reached the MARKER FLAG the same day
//         (PendingMarkerPress above) and then its LAST FOUR ACTS (PendingClickAct
//         above — the trim bar's two bound-set clicks and its framing
//         double-click, the empty lane's create double-click and the `h` view's
//         diff-flag clicks, all of which had been press-time by omission too).
//         WHAT IS STILL PRESS-TIME IS NOW EXACTLY WHAT IS RULED SO OR AWAITS A
//         RULING OF ITS OWN: every DISMISSAL (the standing rule — only the ACT
//         moved), the two dropdown ANCHORS (the recorded exception), and the two
//         DRAG STARTS that are not armed acts at all — the shift REGION FORMER's
//         placement press, whose playhead rides the sweep's moving end, and the
//         editor-text CARET press, a caret-at-press being what makes a text drag
//         a text drag. Do not re-derive that list from here
//         — grep the claims at their own sites —
//         and THE TOUCH CONSEQUENCE IS WHY IT IS WRONG rather than merely
//         inconsistent: two fingers can never land on the same frame, so a
//         press-time teleport necessarily fires on the FIRST finger, before any
//         second-finger gesture can exist. The press column is the point the
//         user aimed at, the deferred click act's own rule.
//       - CROSSING the drag threshold COMMITS NOTHING and the gesture simply
//         runs to its release, exactly as a force-ended pending commits
//         nothing. It resolved into a bound drag on the bound nearer the press
//         column for one commit, and THE ARCHITECT DELETED THAT EXTENSION
//         2026-08-15 — "we can remove that, because the threshold for the
//         bounds is fine, the ten pixels on either side works, it's a large
//         enough threshold" — so A BOUND IS DRAGGED BY ITS OWN GRAB BAND AND
//         NOWHERE ELSE. The crossing's one remaining effect is that it marks
//         the press MOVED, which takes the teleport off the release: the act
//         belongs to a motionless lift.
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
// has done — the first finger's translation simply runs to its own lift, where
// a motionless one still teleports. (The generic motionless-hold UPGRADE that
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
// wear the trim endcaps' own pair, the box's INTERIOR wears TrimResize (the
// bridge's own shape — the pan is an x-only slide of the whole span), and
// EVERYWHERE ELSE ON THE LANE IS THE ARROW since codex round 19, which is the
// map's own standing rule rather than an exception (a point arming nothing
// shows the Arrow: outside the box the press is a Pending, whose crossing
// commits nothing and whose only act is the teleport at a motionless lift, and
// a click carries no cue anywhere in the product). Hover and drag alike, and
// EVERY LIVE DRAG KEEPS ITS CUE for the gesture's life, read from this record's
// own `kind` (the trim exception's rule — the edges took it at the lane rework
// and the PAN joined 2026-08-13, the architect closing the one live lane drag
// that fell back to the Arrow mid-slide; a Pending wears that same Arrow for
// its whole span, and the crossing cannot change the kind, so the press-to-
// release span reads ONE cue; pointer_cursor_kind). (The band-wide TrimResize
// and the crossing's change to a grabbed-bound arrow that this paragraph
// described were true only while an outside press extended the NEARER bound,
// and that extension was deleted 2026-08-15 with the rest of the outside
// drag.) CTRL BINDS
// NOTHING ON THE LANE any more — it went with the strip drag — so a ctrl press
// is a consumed nothing and ctrl's hover answer is the Arrow, the map's own
// rule for a modifier that arms nothing.
// Cleared on button release / lost
// button, by the force-end finalizer, and on file load; pointer gestures
// have no cancel, and a force-ended PENDING commits nothing (a force-end is
// not a click, so no teleport runs — the pendings' own rule).
enum class OverviewDragKind { Pending, Pan, EdgeBegin, EdgeEnd };

struct OverviewDragState {
    bool active = false;
    bool moved  = false;   // crossed the threshold into a real drag
    OverviewDragKind kind = OverviewDragKind::Pan;
    int  press_x = 0;      // press position (window px): the Chebyshev gate,
                           // AND the Pending kind's teleport column
    int  press_y = 0;
    // Pan only: active-domain offset between the pressed column's whole-song
    // position and the viewport CENTER at the grab — the grab-point offset
    // inside the box. Each motion event centers on (pointer position − this).
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
//                 (run_span_framing_command: a live region, else a proper trim
//                 sub-window, else the whole song) — and, while the `h` HISTORY
//                 VIEW stands, the VIEWED CHECKPOINT'S DIFF SPAN instead
//                 (frame_viewed_commit_diff_span, 2026-08-05: one gesture on one
//                 band, two commands, the mode picking between them — at the
//                 LIFT since 2026-08-15, where every gate is re-asked live).
//                 Target unused; both axes'
//                 slack compared. It REHOMED here from the deleted zoom lane
//                 (architect 2026-07-31). THE VERDICT IS TAKEN AT THE PRESS AND
//                 SPENT AT THE LIFT (2026-08-15, the act-at-lift sweep's last
//                 four acts — PendingClickAct above): consume-before-arm became
//                 VERDICT-before-arm, which keeps the property it was written
//                 for — a true verdict arms the framing pending ALONE and never
//                 the cap/bridge drag, so the second click frames rather than
//                 grabs and, TRIM HAVING NO UNDO, an 8 px drift on it moves no
//                 bound. The
//                 seed is that lane's own press record (TrimBarPressSeed),
//                 not a strip-drag field: the lane arms a pending trim drag
//                 rather than a live one, so there is no drag state to hang it
//                 on and the motionless test is the release's own slack compare.
//   Marker     -> opens the marker's flag editor (target = marker index; both
//                 axes' slack compared). The marker's ONE pointer surface is
//                 its FLAG BOX (the painter's published rect — the stem
//                 surface died 2026-08-12 with the stems-inert ruling, and the
//                 marker-text lane's run in row 5). THE VERDICT IS TAKEN AT THE
//                 PRESS AND SPENT AT THE LIFT (2026-08-15): the press compares
//                 against the snapshot — the moment the timing test is about —
//                 and carries the boolean in PendingMarkerPress, while the
//                 motionless lift either opens the editor or SEEDS the next
//                 candidate, at the PRESS coordinates so the pairing stays
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
//                 read-only gated silently — AT THE LIFT since 2026-08-15, on
//                 the TrimBar entry's own verdict-before-arm rule and for the
//                 same reason at lower stakes (the create IS undoable): a true
//                 verdict arms the create pending ALONE, so the second press
//                 still never becomes a pan, and the two consumes read as one
//                 rule rather than two. PLAIN presses only — a modified
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
};

// THE TRIM-BAR FRAMING DOUBLE-CLICK'S FIRST HALF, recorded at the press because
// only the RELEASE can tell a click from a drag. A plain trim-bar-lane press
// (any spot in the band — endcap, bridge, or bare ground; read-only included and
// the `h` history view too, the framing
// being pure navigation in both) records this; the left release seeds the TrimBar
// candidate when the pointer is still within kDoubleClickSlackPx of the recorded
// point and no trim drag went live. That slack IS the motionless test: it equals
// kDragMovedThresholdPx, so "never became a drag" and "never left the slack" are
// the same condition by construction — STILL TRUE after the framing consume
// moved to the lift (2026-08-15), and re-checked then: the press that CONSUMES
// records no seed at all (it arms the framing pending and returns, exactly where
// the press-time consume returned), so this record is only ever written by a
// press that armed the cap/bridge drag or claimed bare band, which are precisely
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
// Settings plus the view bar's three, row 3's two TABS, row 4's twenty-seven
// view / mode / action buttons (the deleted toolbar row's four lead them since
// the 2026-08-12 relayout), then the bottom row's FOURTEEN — the transport
// three, the MARKER-WALK three (2026-08-15),
// the four cardinal arrows and the FOUR HISTORY COMPANIONS that replace those
// arrows while the `h` view stands (2026-08-14). It exists ONCE, here, because
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
    // (The painted order is File, Settings since the Navigation anchor's
    // deletion on 2026-08-15, and the split is back to two anchors.)
    File, Settings, ViewSW, ViewTP, ViewTW,
    // Row 3, the tabs — TWO SLOTS, ALWAYS. They are the A/B tabs ordinarily and
    // the `h` view's WALK SELECTOR while it stands, "Remote" and "Local"
    // (architect 2026-08-08): the row grew to four for the (walk source,
    // reading) product on 2026-08-07 and went back to two the following day,
    // when the READING left the row for its own toggle button in row 4
    // (HistoryCumulative below) and the tabs were left naming the walk alone.
    // TabC and TabD are deleted whole with that arc — enum, chord rows, faces,
    // tooltips and the painter's defs — so nothing publishes an empty rect in
    // this row any more.
    TabA, TabB,
    // Row 4, the icon row, in painted order: the toolbar four (the deleted
    // row 2's Save / Undo / Redo / Render, the row's FIRST GROUP since the
    // 2026-08-12 grand relayout dissolved that lane — same chords, same face
    // machinery, the FACE now a glyph in the 32px box and the old labels
    // living on as the tooltips), the two view radio pairs, the trim button's
    // group, the ZOOM GROUP and the SINGLE-MARKER VERBS (both 2026-08-12,
    // the architect's live placement "after the trim"), the phase-reset
    // clipboard pair with the three mode/editor buttons, and THE ROW'S LAST
    // GROUP — listen, load-in-place, THE READ-ONLY TOGGLE and the history
    // opener, in that order (architect 2026-08-14: "make the last section of
    // the icon row: listen, load-in-place, readonly, history").
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
    // THE TRIM BUTTON (2026-08-11, the trim surface arc): bare `x`, set trim
    // from region — the button IS its chord, so every rule `x` has, the button
    // has (refusal-gated: no region = a consumed nothing, degenerate result =
    // the same; region-consuming on success). Momentary, click face, no
    // selected state, never-grey per the icon row's rule (in the `h` view the
    // derived partition greys it, `x` being consumed there — nothing
    // hand-listed). It opens a NEW SEPARATOR-LED GROUP after the warp/phase
    // radios — the architect's placement ("place it after the warp/phase radio
    // buttons, create a new separator"), a group intended to collect
    // viewport-related acts later — and the group HAS its second member since
    // 2026-08-16.
    // ITS NO-REGION ARM CAME BACK TO A CONSUMED NOTHING and the round trip is
    // worth stating so nobody reads the line above as never having moved: for
    // one day (2026-08-15..16) `x` SEEDED a region there, and the architect
    // split that act onto its own chord and its own button because one key had
    // come to mean two unrelated things — show and commit. The seed is
    // IconShowRegion's now, whole.
    IconTrim,
    // THE SHOW-REGION BUTTON (architect 2026-08-16): Ctrl+Shift+X, the trim
    // group's second member and the second VIEWPORT-CLASS act the architect's
    // 2026-08-11 slot was opened for. ONE ACT, always meaningful: make sure a
    // region exists — seeding one at the current trim window's two bounds if
    // none stands — and then BRING IT INTO VIEW (bring_span_into_view, the
    // owner shared with the group undo restore). That is the whole command. IT
    // CLEARS NOTHING: the region's existing clearers are its whole lifecycle,
    // unchanged.
    //
    // IT IS MOMENTARY AND STATELESS BY RULING, not by omission, and the
    // superseded design is kept because the hole it had is easy to re-invent.
    // It was specified as a TOGGLE whose lamp read app.region.active — no new
    // bit, the roster's standing rule for a selected face — and the architect
    // found the flaw before it was built: "what if user draws a region, then
    // moves the viewport away via drag? The region toggle is on, but the region
    // view can't be accessed because the toggle is already on." A lamp derived
    // from the region's EXISTENCE says nothing about its VISIBILITY, so a span
    // scrolled offscreen left the button lit and the only press available
    // CLEARED it — making the one thing the user wanted unreachable through the
    // very face meant to help. HIS FIX REMOVES THE STATE rather than qualifying
    // the lamp: "maybe the button should not be a toggle, but a 'show region'
    // button, that can always be pressed outside of history. That way it's not
    // stuck having to track the region state — it's just a region-shower, and
    // that's it."
    //
    // SO IT NEEDS NO REFUSAL AND HAS NO STUCK STATE: with no region it makes
    // one and shows it, with a region already onscreen the framing owner's
    // first arm writes no viewport and the press is a harmless nothing, and
    // with a region offscreen it scrolls or zooms to it. Pressing it twice is
    // idempotent by construction.
    //
    // Always enabled, by the settled face policy — there is no refusal to
    // mirror. The `h` view greys it through the derived partition (Ctrl+Shift+X
    // is not on the mode's allowlist, so the mode consumes it exactly as it
    // consumes `x`), nothing hand-listed.
    IconShowRegion,
    // THE ZOOM GROUP (2026-08-12, the grand relayout's roster commit): four
    // navigation chords in their own separator-led group after the scissors —
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
    // THE SINGLE-MARKER VERBS (2026-08-12, the same order): drop (bare `s`),
    // delete (`Delete`), the disable toggle (`Ctrl+D`), and inherit/collapse
    // (`Ctrl+N`). Authoring chords, so their HOME-VIEW, empty-selection and
    // occupied-frame refusals are the chords' own consumed no-ops (never-grey),
    // the `h` view GREYS all four as it greys everything else in this row that
    // it refuses, and — since 2026-08-15 — SO DOES THE READ-ONLY LOCK, the
    // roster's second MODE statement (redesign_button_enabled's read-only arm;
    // the lock used to be in the never-grey list above and no longer is).
    IconMarkerDrop, IconMarkerDelete, IconMarkerDisable, IconMarkerInherit,
    // THE MASS-MARKER CATEGORY: copy phase resets (Ctrl+P), paste phase
    // resets (Ctrl+Alt+P), the BPM opener (bare `m`), iteration mode (bare
    // `i`) and follow (bare `f`). All five are consumed in the `h` view and
    // all five GREY there (they were the one group the view dropped whole,
    // 2026-08-13..14). FOUR OF THE FIVE grey on a locked tab too — FOLLOW is
    // the exception, bare `f` being navigation and on the lock's allowlist.
    IconCopy, IconPaste, IconBpm, IconIter, IconFollow,
    // THE ROW'S LAST GROUP (architect 2026-08-14): the two render-entry
    // buttons — listen (bare `l`) and load in place (`'`) — then THE READ-ONLY
    // TOGGLE and the HISTORY OPENER, which is the order he dictated and which
    // also lands the opener in last place. THE FIRST TWO GREY ON A LOCKED TAB
    // (2026-08-15): the lock blocks bare `l` and `'` alike, `'` replacing the
    // whole authored state. The toggle and the opener do not — `o` is the
    // escape chord and `h` never meets the gate.
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
    // THE HISTORY MODE'S BUTTON (2026-08-04), ruled with the mode itself and
    // landed with the commit act: bare `h`, lit while the mode stands. Its
    // chord toggles, so the same click that opened the view closes it. It had
    // a separator-led group of its own from 2026-08-04 until 2026-08-14, when
    // its four companions left the row for the bottom one and the architect
    // put the opener last.
    IconHistory,
    // The BOTTOM ROW's transport cluster (row 8, architect-ratified
    // 2026-08-11, the touch arc's first surface; a tenant of the unified
    // bottom row directly under the waveform since the 2026-08-12 row
    // unification): permanent on every host — no touch mode, no flag, no
    // detection. TEN buttons in three groups, in painted order (the enum
    // order is the painted order, and the row paints below the top rows, so the
    // roster's tail is
    // the right home): the TRANSPORT (skip-back = bare Home, THE ONE PLAY/STOP
    // BUTTON = bare Space, skip-forward = bare End), then THE MARKER-WALK
    // GROUP (2026-08-15 — previous = Shift+Tab, next = Tab, walk both tabs =
    // Ctrl+Shift+Tab), then a SEPARATOR, then the
    // four CARDINAL ARROWS — DOWN, UP, LEFT, RIGHT left-to-right since
    // 2026-08-14 (the architect's order; it was vim's left-down-up-right from
    // the row's first day) — which inherit the bare arrows' whole
    // semantics by dispatching through on_key like every other chord button,
    // and finally THE FOUR HISTORY COMPANIONS, which are the SAME SLOTS as
    // the arrows: the two clusters swap on the `h` view (below). The walk
    // group is NOT part of that swap and paints in both states.
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
    // THE FOUR ARROWS DO NOT REPEAT (architect 2026-08-13, deleting the
    // hold-repeat that shipped with the row; the physical arrow KEYS keep
    // their platform repeat). His reasoning is recorded at the arrows' rows in
    // kToolbarChords (input_pointer.cpp), which is where the machinery lived.
    TransportSkipBack, TransportPlayStop, TransportSkipForward,
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
    // no hand entry for them: all three keys are the mode's OWN vocabulary in
    // there (bare Tab and Shift+Tab are the diff-flag cycle, Ctrl+Shift+Tab the
    // reverse walk-source cycle), so history_mode_owns_key answers for them and
    // they stay lit and do the mode's own thing.
    TransportWalkPrev, TransportWalkNext, TransportWalkBoth,
    TransportDown, TransportUp, TransportLeft, TransportRight,
    // THE HISTORY COMPANIONS, THE ARROWS' MODE TWIN (architect 2026-08-14):
    // while the `h` view stands the bottom row's right cluster is these four
    // instead of the arrows — Cumulative, Revert, Older, Newer, the order
    // they held in the icon row's history group. It is a SWAP and not a
    // collapse: one cluster of four is painted at that anchor in either
    // state, so the slot count never changes and nothing on the row reflows.
    // OUTSIDE THE VIEW THEY DO NOT EXIST ANYWHERE — not painted, not greyed,
    // a zero published rect exactly as the row's tenants publish under a
    // modal — which is the whole of "hidden" left in the product; the ICON
    // ROW hides nothing at all any more.
    //
    // THEY ANSWER PLAIN TRUE since 2026-08-15 (redesign_button_enabled below),
    // dropping BOTH of their greys: the resting-disabled arm they had held
    // since 2026-08-05, whose reasoning — their keys are bound only inside the
    // view — went moot when the move to this row stopped painting them outside
    // it, and Revert's conditional in-view grey, which the architect reversed
    // as a per-selection blink. The full record, with the superseded reasoning
    // kept, is at that arm; the chords refuse exactly as they always did.
    HistoryCumulative, HistoryRevert, HistoryOlder, HistoryNewer
};
// THE ROSTER, re-derived by counting the enumerators above: FIVE in row 1, two
// in row 3, twenty-seven in row 4 and fourteen in the bottom row — 48. Of those,
// FORTY-SIX carry a chord in kToolbarChords and TWO are the dropdown anchors
// (File and Settings), which is the split the chord table's own
// static_assert checks — 43 + 2 until 2026-08-13, when the Quit button left the
// chord table and File joined the anchors in its slot (the count did not move).
// 48 SINCE 2026-08-16: the SHOW-REGION button joined the trim group in
// row 4 (Ctrl+Shift+X), a pure chord addition — 47 + 1, split 46 + 2.
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
inline constexpr int kRedesignButtonCount = 48;
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
// inherited: row 1 is five buttons and TWO of them are anchors, so the close
// rule covers THE VIEW BAR'S THREE alone — the same three it covered while
// Navigation was a third anchor (it was "Quit or the view bar's three" while
// the Quit button existed, and the Navigation anchor's 2026-08-15 deletion
// moved this membership not at all, an anchor leaving the row being the one
// change this rule cannot feel).
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
        case RedesignButton::IconTrim:
        case RedesignButton::IconShowRegion:
        case RedesignButton::IconZoomIn:
        case RedesignButton::IconZoomOut:
        case RedesignButton::IconZoomFitBest:
        case RedesignButton::IconZoomOriginal:
        case RedesignButton::IconMarkerDrop:
        case RedesignButton::IconMarkerDelete:
        case RedesignButton::IconMarkerDisable:
        case RedesignButton::IconMarkerInherit:
        case RedesignButton::IconCopy:
        case RedesignButton::IconPaste:
        case RedesignButton::IconBpm:
        case RedesignButton::IconIter:
        case RedesignButton::IconFollow:
        case RedesignButton::IconListen:
        case RedesignButton::IconLoadInPlace:
        case RedesignButton::IconReadOnly:
        case RedesignButton::IconHistory:
        case RedesignButton::HistoryCumulative:
        case RedesignButton::HistoryRevert:
        case RedesignButton::HistoryOlder:
        case RedesignButton::HistoryNewer:
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

// WHICH BUTTONS ARE THE BOTTOM ROW'S — the transport three, the MARKER-WALK
// GROUP's three (2026-08-15), the four cardinal
// arrows and, since 2026-08-14, the FOUR HISTORY COMPANIONS that take the
// arrows' slots inside the `h` view (row 8's from 2026-08-11; tenants of the
// unified bottom row since 2026-08-12). Named
// once because its consumers are all about the ROW'S HOME STRIP rather than
// about any one button: these pixels live in the BOTTOM strip, so every
// damage decision the other rows answer with invalidate_top_strip must answer
// with the bottom row's own rect for these ten — the hover recompute, the
// click-face arm and clear, the tick comparator, and the tooltip, which also
// FLIPS ABOVE the button here (the lane rests on the WINDOW'S FOOT since the
// relayout's commit B, so there is nothing below it at all; it was the blank
// foot's own band, zero on a short window, for the afternoon before).
// A membership predicate like redesign_button_is_tab, deliberately NOT the
// exhaustive-switch shape: redesign_button_in_menu_row above is the roster's
// one classification chokepoint (a new button fails to compile there until its
// row is stated), and one chokepoint is enough.
inline constexpr bool redesign_button_in_transport_row(RedesignButton b) {
    switch (b) {
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
        case RedesignButton::HistoryCumulative:
        case RedesignButton::HistoryRevert:
        case RedesignButton::HistoryOlder:
        case RedesignButton::HistoryNewer:
            return true;
        default:
            return false;
    }
}

// WHICH BUTTONS ARE THE HISTORY COMPANIONS — the four that take the cardinal
// arrows' slots on the bottom row while the `h` view stands. Named 2026-08-15
// for its ONE consumer, and the consumer is what the name has to earn: the
// architect's scoped-truth ruling makes the bottom row's RIGHT cluster
// untruthful and always enabled, and REVERT is the one member whose chord the
// mode admits CONDITIONALLY (on a diff flag standing), so the derived `h`
// partition would grey it inside the view. This membership is what lifts the
// four over that partition at redesign_button_enabled — a one-line exception
// to a derived rule, named rather than spelled inline so the exception and the
// cluster are one fact. It is deliberately the FOUR and not just Revert: the
// ruling is about the cluster, and a future companion whose chord the mode
// gates conditionally must inherit it without a second edit.
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

// WHICH BUTTONS ARE ROW 3'S — the two tabs, which are the A/B pair ordinarily
// and the `h` view's Remote / Local walk selector while it stands. Named once
// because FOUR places ask it and all four are about the ROW rather than about
// either slot: the hover carve-out (the selected tab has no hover face), the
// tooltip override (the walk selector carries none), the label override, and
// the press claim's walk over the row's hit rects.
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
// W/P radios, THE TRIM PAIR (the scissors and — since 2026-08-16 — the
// show-region button; the group was one member from 2026-08-11, opened as
// the architect's slot for viewport-class acts, and this is the second such
// act), the zoom four, the single-marker verbs, the mass-marker acts, and the
// row's last group — listen, load-in-place, the read-only toggle and the
// history opener (architect 2026-08-14, which is also what retired the history
// group's own boundary). EIGHT is unchanged: the new button JOINS a group
// rather than opening one, so it is deliberately absent from the switch below.
inline constexpr bool redesign_button_opens_icon_group(RedesignButton b) {
    switch (b) {
        case RedesignButton::Save:
        case RedesignButton::IconS:
        case RedesignButton::IconW:
        case RedesignButton::IconTrim:
        case RedesignButton::IconZoomIn:
        case RedesignButton::IconMarkerDrop:
        case RedesignButton::IconCopy:
        case RedesignButton::IconListen:
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
// been two, three and two again without a single route changing is the shape's
// own proof: nothing here counts menus.
enum class DropdownMenu { None, File, Settings };

// EVERY MENU THERE IS, in one place, so the routes that must walk them all —
// the press claim's anchor test, the hover switch, the armed hover open — walk
// this instead of naming a pair (or a triple). `None` is deliberately absent:
// it is the closed state, not a menu.
inline constexpr DropdownMenu kDropdownMenus[] = {
    DropdownMenu::File, DropdownMenu::Settings,
};

// WHICH BUTTON A MENU HANGS FROM. The dropdown is flush under the button that
// emits it (architect 2026-08-02), so the painter and the open edge's damage
// both need the anchor, and they must read ONE expression or the damaged band
// and the painted box could start on different rows of pixels.
inline constexpr RedesignButton dropdown_anchor_button(DropdownMenu m) {
    switch (m) {
        case DropdownMenu::File:     return RedesignButton::File;
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
// IT HAS ONE INSTANCE TODAY, the FILE table below (2026-08-13), and it stays a
// TYPE rather than collapsing into that table's fields: it was two instances
// from 2026-08-02 until 2026-08-15 (NAVIGATION was the other, and the first —
// this row type is its shape, which File then landed on with no edit to the
// painter or the release at all), and what it names is the KIND of menu, which
// is what the release body forks on. The kind is still two-valued.
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
// HISTORY VIEW, where Ctrl+Shift+Tab is not the A/B walk at all — the mode
// claims it as the REVERSE cycle of its own walk-selector row — so an item left
// live would have dispatched a chord doing something else entirely under a label
// promising the walk. It greyed rather than lying, which is a difference in KIND
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
    std::max({kFilePopupItemCount, kSettingsPopupItemCount});

// IS THIS A COMMAND MENU? The two kinds of menu differ in what a row DOES — a
// settings key to prefill, a chord to dispatch — and this names the second kind
// once, for the row lookup below and for the release's dispatch fork. It is a
// PREDICATE over the menu rather than a comparison at each site, and it stays
// one now that File is the kind's only member: the kind is the fact the release
// forks on, and a second command menu would join it here.
inline constexpr bool dropdown_is_command_menu(DropdownMenu m) {
    return m == DropdownMenu::File;
}
// THE COMMAND ROW ITSELF — the ONE place that maps a command menu to its table,
// read by the shared view below and by the release body that dispatches the
// chord. Callers ask dropdown_is_command_menu first; a non-command menu answers
// with the File table's row, which no caller reaches. It forked between two
// tables until 2026-08-15 and the fork went with the Navigation menu.
//
// `m` IS DELIBERATELY UNREAD AND SILENCED AT THE SITE (apply_nav_zoom_at's `y`
// parameter is the same shape and the same ruling): the callers ask this for
// "the row of THIS menu", which is the question a second command menu would
// answer differently, and dropping the parameter would make both call sites and
// this body change back the day one lands. What is producer-less is the FORK,
// and the fork is what went; the parameter is the interface the kind implies.
inline constexpr const CommandPopupItem& command_popup_item(DropdownMenu m,
                                                            int i) {
    (void)m;
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
// press must be HELD on one of the four shift-admitting buttons before its lift
// dispatches the SHIFTED twin instead of the plain act. It is what gives glass
// the shift acts: the road rig has no keyboard, so a finger could reach only
// half of each shifted pair. Read at exactly one site, the lift's chord build
// (finish_chrome_press_release, input_pointer.cpp), against the press stamp the
// arm carries (AppState::ChromePress::press_ms).
//
// IT IS ITS OWN CONSTANT AND NOT THE TOUCH REGION HOLD'S, though both rest at
// the architect's "~500ms" and both are hold beats. Three reasons, in order of
// weight: the region hold is a PLATFORM constant (platform_wayland.cpp's
// anonymous namespace, below the GUI model by design) and sharing it would
// mean hoisting a disambiguation deadline into the GUI's input layer; it is
// tuned for a DRAG gesture on the waveform — the mark at which a finger that
// has not moved stops being a possible pan and becomes a region sweep — while
// this one is tuned for a stationary press on a small button, so the two can
// want different numbers from the same glass session; and the surfaces have
// different futures (the region hold answers to the pan zone's two-deadline
// window, this to the roster). They are equal today by coincidence of the same
// ruling, and either may be retuned without the other.
//
// It rides NO SCALE, deliberately: a duration is not a length, so gui_scale has
// nothing to say about it (the same rule the drag-slop and disambiguation
// constants carry for their own reason — they model the hand, not the pixels).
//
// The beat is DELIBERATELY LONG relative to a click. A shifted act is the rarer
// one on all four buttons, so the cost of an accidental hold must land on the
// rare act rather than on the common one; 500ms is well past any ordinary
// click-and-lift and just short of the point where a user would assume the
// press was lost.
//
// IT PASSES SILENTLY AND IS RULED TO (architect 2026-08-13): the gesture is the
// TOUCH PANEL's, and tooltips do not show there, so a hint at the beat would
// ride a surface the gesture's only user never sees. No feedback is to be
// built for this constant; the ruling's home is the read site.
constexpr int64_t kChromeShiftHoldMs  = 500;

// ONE generic Chebyshev pixel distance a press must travel before it becomes a
// DRAG (architect-tunable), shared by EVERY press-becomes-drag surface. THE
// LIST IS RE-DERIVED FROM THE GATES THEMSELVES (codex round 19 — it named
// "strip, region, trim, and the marker flag" long after the strip drag's
// deletion and the two 2026-08-15 additions), and it is SEVEN states — six that
// latch their own `moved` in on_motion, plus one that resolves at the crossing
// without latching anything:
//   * ScrollDragState — THE ONE NAV DRAG, the pending click whose crossing
//     becomes the grab-pan or, with ctrl, the zoom (the capture begins at that
//     crossing);
//   * RegionDragState — the region FORMER, the shift drag and the touch
//     region hold, through apply_region_drag_motion's own gate;
//   * RegionEditDragState — the STANDING region's EDITOR (2026-08-15), the
//     move and the two bound drags. It shares a word with the former above
//     and is a different gesture: one DRAWS a span, the other MANIPULATES the
//     one already resting;
//   * OverviewDragState — the overview lane's box gestures (2026-08-12; the
//     lane's DELETED ctrl strip drag is what "strip" used to name here, and
//     this state is not it): the box pan, the two edge drags, and the Pending
//     outside press whose crossing commits nothing but still latches;
//   * PendingTrimDrag — the trim bar's endcap / bridge drag;
//   * PendingMarkerPress — the marker flag's pending CLICK, whose crossing
//     becomes the reposition drag on a plain arm and commits nothing on a
//     shift or ctrl one (the tempo flag was one more until the tempo drag's
//     deletion, 2026-07-29);
//   * PendingClickAct — the act-at-lift sweep's last four pending acts
//     (2026-08-15), which is the one member that latches NO `moved` of its own:
//     its crossing RESOLVES the arm outright, handing the TrimBoundSet kind to
//     the endcap drag above and spending the other three for nothing, so there
//     is no moved phase for it to be in.
// One derived reader sits outside that list: the region former's SLIVER FLOOR
// (end_region_drag_min_size_check) measures a rested span against this same
// constant, so "never became a drag" and "never left the slack" are one
// number. The TOUCH slop is a separate constant deliberately equal to it
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
    // (text_editor::next_session_id — homed there because four of the five
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
    //     CLICK-TELEPORT (run_overview_teleport, the centering that a motionless
    //     release outside the box runs since 2026-08-15: a
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

    // The marker flag's PENDING CLICK, armed by any of its three presses and
    // holding the whole click back to the lift (2026-08-15; the contract is at
    // the type's declaration). A plain arm becomes the reposition drag past the
    // threshold. Cleared on the threshold crossing, on button release / lost
    // button, by the force-end finalizer, and on file load.
    PendingMarkerPress pending_marker_press;

    // Pending trim endcap/bridge drag, armed by a plain trim-bar press (the
    // trim-drag machinery begins only past the threshold). Cleared on the
    // threshold crossing, on button release / lost button, by the force-end
    // finalizer, and on file load.
    PendingTrimDrag pending_trim_drag;

    // THE ACT-AT-LIFT SWEEP'S LAST FOUR PENDING ACTS, in one record (2026-08-15;
    // the contract, including why they share one field, is at the type's
    // declaration): the two trim bound sets, the trim bar's framing
    // double-click, the empty lane's create double-click and the `h` view's
    // diff-flag clicks. The TrimBoundSet kind becomes the endcap drag past the
    // threshold; the other three commit nothing there. Cleared on the threshold
    // crossing, on button release / lost button, by the force-end finalizer, and
    // on file load.
    PendingClickAct pending_click;

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

    // THE STANDING REGION'S OWN EDITOR — the move / bound drags a plain
    // waveform press arms inside a live span (contract at RegionEditDragState).
    // Cleared on button release / lost button, by the force-end finalizer, and
    // on file load. It writes only app.region; it never touches trim, the
    // playhead or the selection.
    RegionEditDragState region_edit_drag;

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

    // The overview lane's plain drag — the pending outside press, the box pan
    // and the box-endcap edge
    // drags (contract at OverviewDragState). Cleared on button release / lost
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

    // THE MODAL SURFACE'S PAINTED GEOMETRY. The prompts and the four dialog
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
    // THE AUTHORITATIVE MEMBERSHIP of the four DIALOG-HOSTED editors (the
    // settings editor, the load editor, the commit-title editor and the bpm
    // bracket editor; the top-strip flag editor in its FlagPayload kind is
    // deliberately not one of them). The predicate
    // GuiInputHandler::modal_dialog_editor_active is this id being non-zero,
    // and ITS declaration is the authoritative statement of what that
    // predicate is FOR and who calls it; this is where the four are NAMED, so
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
    // to be sentinel-encoded into one field, and the two arms can stand at
    // once in principle (the veil-admitted roster pair is pressable under an
    // editor dialog while its buttons are too). Since 2026-08-13 the roster's
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
    // keyboard-capability loss, a Super-swallowed press) drops it too, because
    // the release it is waiting for will never be delivered. Every write
    // damages the box.
    int    modal_dialog_key_pressed     = -1;
    GuiKey modal_dialog_key_pressed_key = 0;

    // THE CLOCK'S RESERVED CELL, published by paint_bottom_strip (2026-08-11,
    // when the timestamp moved off the status line into the transport row's
    // centre in monospace; the row unification merged that row and the status
    // line into the one bottom row a day later). It is a PAINTER STASH in the
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
    // `kind` names the TARGET CLASS, because two chrome surfaces arm and only
    // one of them is a roster button:
    //   Roster         — a chord-table button; `index` is the roster index.
    //   HistoryWalkTab — the `h` view's walk-selector tabs; `index` is TabA's
    //                    or TabB's roster index, and the act is
    //                    set_history_reading, not a chord.
    // (A THIRD KIND, TabLock, armed the active tab's padlock for bare `o` and
    // is DELETED with the padlock's move into the icon row, 2026-08-14: the
    // read-only toggle is a roster button now, so it arms as Roster like every
    // other chord button.)
    // (The three dropdown ANCHORS are deliberately NOT armed: their toggle is
    // the recorded press-time exception — the reasoning is at their press
    // claim in on_button_press.)
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
    // THE MARKER FLAG'S PENDING CLICK IS THAT FUTURE ACT, arrived 2026-08-15
    // (PendingMarkerPress above): it carries its own `shift` and `ctrl` for
    // exactly this reason, and it is an armed act rather than a continuous
    // gesture, so it takes the rule rather than the nav drag's exemption.
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
    // the chord table's `click_face` column, not restated here; HistoryWalkTab
    // paints no pressed face at all (row 3 keeps its own faces) and uses the
    // arm for the act alone.
    struct ChromePress {
        enum class Kind { None, Roster, HistoryWalkTab };
        Kind    kind     = Kind::None;
        int     index    = -1;
        bool    shift    = false;
        bool    inside   = true;
        int64_t press_ms = 0;
    };
    ChromePress chrome_press;

    // (THE ARROW BUTTONS' HOLD-REPEAT IS DELETED — architect 2026-08-13, with
    // the act-at-release conversion; the record is at the arrows' chord-table
    // rows, input_pointer.cpp. The physical arrow KEYS keep their platform
    // repeat unchanged.)

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
    // divergence is recorded at that arm). redesign_button_label LEFT this
    // list with row 2's labeled faces at the 2026-08-12 relayout, and the
    // clause naming it was stale until this re-grep: no label is painted for
    // Render in any state, the words living on the tooltip alone.
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

    // THE MENU ROW'S DROPDOWN — ONE popup state for BOTH menus (Settings
    // since 2026-07-31, File since 2026-08-13; Navigation was a third from
    // 2026-08-02 until its deletion 2026-08-15, and neither its arrival nor its
    // departure needed a line in here — which is the shape's whole point),
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
    // 2026-08-03) the ANCHOR press that OPENS a menu — press File or Settings,
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
    // whose Ctrl+Shift+Tab is this mode's reverse walk-tab cycle rather than the
    // A/B walk the label promised — and that was the menus' only per-item
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
    //   `t` / `p`), BOTH row-3 tabs and the history button (Ctrl+Tab and
    //   bare `h`, the mode's OWN vocabulary, which the derivation asks about
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
        // EVERY SETTER (re-derived by grep 2026-08-12): the mode's own PLAIN
        // focus CLICK on a diff flag's box in the lane, its ONE pointer
        // surface since the stems-inert ruling
        // (focus_history_diff_flag); the LANE's two MODIFIED clicks, which focus
        // the flag they select (select_history_diff_flags_modified — flag
        // boxes alone); and the mode's own
        // bare Tab / Shift+Tab / IsoLeftTab cycle. Each sets it and lands the
        // playhead on that flag's frame, and nothing else writes it true.
        // ALL THREE CLICKS RUN AT THE MOTIONLESS LIFT since 2026-08-15 (the
        // act-at-lift sweep's last four acts, PendingClickAct above), on the
        // index the PRESS resolved: the setters are the same three bodies and
        // only their timing moved.
        //
        // EVERY CLEARER, the whole list, and all but the last clear for ONE
        // reason: the value is an ordinal into the PAINTED list, so anything
        // that rebuilds that list would otherwise leave the highlight on an
        // unrelated flag.
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
    // Trim is a region authored purely by the plain trim-bar pointer drags
    // (single-bound endcap, inter-endcap bridge/pair), the ctrl /
    // ctrl+shift bound-set clicks, the bare-x set arm (a live region sets the
    // trim to it and consumes the span; with NO region `x` SHOWS one at the
    // current window and writes nothing — the seed, 2026-08-15), the
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

    // Shared text-editor state for two editors distinguished by Kind: the
    // top-strip flag editor (Kind::FlagPayload — active when editing a warp
    // marker's payload, its text run and caret painted live ON THE FLAG ITSELF
    // since row 5's text-on-flag model: render_flag_editor_box unrolls the
    // marker's own box, which the flag pass therefore skips) and the BPM editor
    // (Kind::BpmBracket), which paints as the BOTTOM ROW'S MODAL like the
    // other three dialog editors (2026-08-13). The editor
    // owns the keyboard while active.
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
// TWO consumers since the ctrl strip drag's deletion (2026-08-15; three from
// the lane rework until then): the click-teleport's centering position and the
// box pan / edge drags' per-event pointer position (both of which column-clamp
// x into the lane first — the song walls by construction).
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
// text drag, the MARKER FLAG'S PENDING CLICK, THE FOUR REMAINING PENDING CLICK
// ACTS (PendingClickAct — the trim bar's two bound sets and its framing
// double-click, the empty lane's create double-click, the `h` view's diff-flag
// clicks; 2026-08-15, when the act-at-lift sweep finished), or the pending trim
// drag
// (button held, watching for the threshold; the flag's pending holds a whole
// unrun click since 2026-08-15, which is why it must be in flight here, and the
// four above hold one for the same reason). (The scrub still
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
           app.region_drag.active || app.region_edit_drag.active ||
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

// WHERE BARE Home / End WOULD LAND THE CURSOR — the two skip commands' one
// answer, hoisted 2026-08-15 so the three dispatch arms that jump share one
// arithmetic instead of hand-spelling it three times. `forward` selects End
// over Home. Defined in viewport.cpp beside the navigation range it reads.
//
// IT HAS NO FACE READER AND MUST NOT GAIN ONE (architect 2026-08-15). It was
// hoisted for the bottom row's two SKIP buttons as well — they greyed where the
// cursor already rested on the landing frame — and that half was ruled back out
// the same day: bare Home / End are not pure jumps (each also stops a live
// audition, clears the marker selection and dissolves a resting region, even
// when the jump moves nothing), so a greyed skip promised less than its key
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
//   clock_invalidate_rect — the reserved CLOCK CELL at the lane's centre, and
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
// arrows always-on. Revert is now lit whatever this answers (it is a member of
// the bottom row's untruthful right cluster, redesign_button_enabled lifting
// the four history companions over the derived partition), a click on it with
// no subject is a consumed no-op, and the full record with the reversal's
// reasoning lives at that arm. DO NOT GIVE THIS PREDICATE A FACE READER AGAIN.
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
// Ctrl+Shift+Tab is the mode's own reverse walk-tab cycle rather than the walk
// the label promised. The SETTINGS menu never had one (it does not open in that
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
// button's own chord and hand-answers the TWO ANCHORS, which have none —
// Settings dead on the toggle_dropdown lockout, File live since 2026-08-13, its
// menu opening in the view — and IT CARRIES THE AUTHORITATIVE PARTITION
// INVENTORY. (It hand-answered a third, Navigation, live from 2026-08-08 until
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
// refuses in this row now wears the ordinary dead face. The only hiding left
// in the product is the bottom row's cluster SWAP, which publishes zero rects
// for whichever four are not painted — the same shape the modal's yield
// already used.)

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
// arrows, and the four history companions that supplant them inside the view —
// went back to a plain `true` the same day, giving up the selection-reading
// arms the whole-row honesty ruling had just given them. THE TWO SKIPS WENT
// BACK WITH THEM (architect 2026-08-15) on a DIFFERENT reason, stated in full
// at their arm below: their honest arm rested on a false premise — that bare
// Home / End are pure jumps — when in fact both also stop a live audition,
// clear the marker selection and dissolve a resting region UNCONDITIONALLY, so
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
//     whole toolbar" was recorded here as code truth.) THE LOCK REACHES TEN
//     MORE BUTTONS since 2026-08-15, the whole authoring vocabulary it blocks
//     on the icon row — its own entry is below.
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
//   * THE READ-ONLY MODE STATEMENT (architect 2026-08-15): the ten icon-row
//     buttons the per-tab lock BLOCKS wear the dead face while it stands, so
//     the toggle makes the buttons it eats look the way the `h` view already
//     makes its own consumed buttons look. Their term is
//     `!active_view_state(a).read_only` AND NOTHING ELSE — no selection term,
//     no view term, no wall term — because this is a MODE statement and not the
//     truthfulness sweep: any interaction-cadence term added here would
//     reintroduce the blink the same ruling removed from the arrows. THE
//     MEMBERSHIP'S OWNER IS read_only_key_blocked (input_key_dispatch.cpp) and
//     the ten are its answer over the chord table: bare `s`, Delete, Ctrl+D,
//     Ctrl+N, Ctrl+P, Ctrl+Alt+P, bare `m`, bare `i`, bare `l` and bare `'`.
//     IT IS HAND-LISTED RATHER THAN DERIVED, and the reason is that the
//     allowlist alone is not the truth — three of its answers do not survive
//     the walk (below).
//   * THE READ-ONLY-LEGAL BUTTONS ARE DELIBERATELY NOT GREYED — Save, Render,
//     the trim scissors, the SHOW-REGION button beside it (2026-08-16 —
//     it writes no trim at all, only the session's region scratch), the S/T and
//     W/P radios, the zoom four, follow, the
//     read-only toggle itself and the history opener. Greying them would make
//     the face promise LESS than the key delivers, which is the 2026-08-07 band
//     ruling's own line: read-only protects the AUTHORED MUSICAL CONTENT and
//     nothing else.
//   * WHY THE TEN ARE NOT DERIVED BY WALKING kToolbarChords THROUGH
//     read_only_key_blocked, the way history_mode_disables_button walks it
//     through the mode's gates — checked and rejected 2026-08-15, and recorded
//     so it is not tried again as an obvious cleanup. The walk diverges on NINE
//     buttons, in two classes. (1) FOUR CHORDS NEVER REACH THAT GATE: bare `h`,
//     bare `u`, bare `,` and bare `.` are claimed by handle_history_mode_key,
//     which returns from on_key ABOVE the read-only gate, so the allowlist's
//     "blocked" for them is VACUOUS rather than a refusal — and knowing that
//     means knowing on_key's dispatch ORDER, which no table holds. (2) FIVE ARE
//     THE RULING'S OWN EXCLUSIONS: the four cardinal arrows (Up and Down are
//     blocked outright, Left and Right blocked only while a selection stands —
//     the exact per-selection blink this ruling removes) and Revert, whose
//     Ctrl+H the lock really does eat but which is a member of the untruthful
//     right cluster. A derivation would therefore need an override list on top
//     of it, which is strictly worse than a list that says what it means and
//     names its owner.
//   * Row 1's two anchors and row 3's tabs answer true HERE: row 1 keeps its
//     two faces by ruling, and a tab has no disabled face of its own. Their
//     entries exist
//     so the vector is total over the roster and the comparator needs no
//     membership test. (The tabs answer true in EVERY state since 2026-08-05:
//     the history view, which greyed them for one day, repurposes the row as
//     its WALK selector instead, and the chord it gave that selector —
//     Ctrl+Tab, the mode's own cycle over the walk sources — is what makes the
//     derived partition call them LIVE, so the mode line at the top of this
//     body never fires for them either, and row 3 has no disabled face at all.)
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
// 2026-08-12 relayout, keeping their mirrored derivations), the TEN the
// read-only lock blocks, and, since 2026-08-15, the BOTTOM ROW'S TEN (every
// chord on that row drops at on_key's loading/blank return, so their faces grey
// there too — and that guard is now the ONLY thing all ten have to say; it was
// EIGHT for the hours between the row's face ruling and the same day's
// marker-walk group, which added three and collapsed play/stop into one).
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
    // ITS ONE EXCEPTION IS THE HISTORY COMPANIONS (architect 2026-08-15): the
    // bottom row's RIGHT cluster is untruthful and always enabled by the ruling
    // at the head of this body, and the four companions ARE that cluster inside
    // the view. Three of them the partition already answers LIVE for free (the
    // mode owns bare `u`, `,` and `.`), so the exception exists for REVERT
    // alone, whose Ctrl+H the mode admits only while a diff flag stands — a
    // per-selection blink, exactly what the ruling removes. The membership is
    // named once (redesign_button_is_history_companion) rather than spelled
    // here, and it is the cluster rather than Revert because the ruling is
    // about the cluster.
    if (a.history_mode.active &&
        !redesign_button_is_history_companion(b) &&
        history_mode_disables_button(a, b)) {
        return false;
    }
    switch (b) {
        // Rows 1, 3 and 4 have NO DISABLED FACE OF THEIR OWN — row 4 by the
        // architect's design (he provided five states and no disabled one), rows
        // 1 and 3 by their face scope. (ROW 4 HAS NO EXCEPTION LEFT TO COUNT.
        // THREE of its members were one until 2026-08-14 — the walk's older /
        // newer steps and the revert act, resting disabled because their keys
        // are bound only inside the history view, joined by the Cumulative
        // toggle in 2026-08-08 — and the four left this row for the BOTTOM one
        // that day, where they are the arrows' mode twin; they answer a plain
        // `true` since 2026-08-15. Both halves of the succession are recorded
        // at the history companions' arm below, which is where they live now.)
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
        // there either.
        case RedesignButton::File:
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
        // THE TRIM BUTTON MIRRORS NOTHING (2026-08-11): bare `x`'s refusals —
        // no region, a degenerate inverse-mapped pair — are consumed no-ops on
        // the key and stay so on the click, per the row's never-grey rule. The
        // `h` view greys it through the derived partition above (`x` is
        // consumed there), nothing hand-listed.
        case RedesignButton::IconTrim:
        // THE SHOW-REGION BUTTON MIRRORS NOTHING EITHER (2026-08-16), and for
        // a stronger reason than its neighbour: it HAS no refusal to mirror.
        // Its one act — ensure a region exists, then bring it into view — is
        // always meaningful on a loaded piece, and the case that would tempt a
        // face (a region already fully in view) is a harmless nothing rather
        // than a refusal, the framing owner's first arm simply writing no
        // viewport. The `h` view greys it through the derived partition above
        // (Ctrl+Shift+X is off the mode's allowlist, so the mode consumes it
        // exactly as it consumes `x`), nothing hand-listed.
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
        // THE READ-ONLY MODE STATEMENT (architect 2026-08-15) — the icon row's
        // one grey that is not the `h` view's: these TEN are exactly what
        // read_only_key_blocked (input_key_dispatch.cpp) drops on a locked tab,
        // so the lock now LOOKS the way the history view already looks, which
        // is what the architect asked for. THE TERM IS THE BIT AND NOTHING
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
        // the walk that would derive it, and the nine buttons it gets wrong,
        // are recorded at the head of this body. A change to the allowlist must
        // be reflected here by hand; that is the accepted cost of the two
        // classes the walk cannot see.
        case RedesignButton::IconMarkerDrop:
        case RedesignButton::IconMarkerDelete:
        case RedesignButton::IconMarkerDisable:
        case RedesignButton::IconMarkerInherit:
        case RedesignButton::IconCopy:
        case RedesignButton::IconPaste:
        case RedesignButton::IconBpm:
        case RedesignButton::IconIter:
        case RedesignButton::IconListen:
        case RedesignButton::IconLoadInPlace:
            return !active_view_state(a).read_only;
        // THE BOTTOM ROW IS ALWAYS-ON WHOLE, apart from the `h` view's derived
        // partition (architect 2026-08-15, his final ruling on this row after
        // it moved three times that day): all ten break out of this switch
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
        // THE `h` HISTORY VIEW'S PARTITION AT THE TOP OF THIS BODY STILL
        // OUTRANKS EVERY WORD OF THIS, and the architect confirmed its split
        // explicitly — "making play and stop disabled in h history view, but
        // allowing home and end, that makes sense": Space is consumed in the
        // view, so THE PLAY/STOP BUTTON wears the dead face in there, while
        // Home/End are the mode's own vocabulary (the absolute jump to 0 / the
        // last frame), so the two skips stay LIT exactly as they are outside
        // it — as do the MARKER-WALK THREE, whose chords are the mode's
        // vocabulary too. All derived from the mode's gates, nothing
        // hand-listed — and it is why these ten break DOWNWARD to a shared
        // `true` instead of returning one here.
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
        // is a consumed no-op, the roster's standing shape. (In the `h` view
        // they are not painted at all — the cluster swaps to the history
        // companions — so the partition's answer for them reaches no pixel.)
        //
        // THE TWO SKIPS (bare Home / End) ARE UNTRUTHFUL TOO AND DELIBERATELY
        // SO (architect 2026-08-15), and they fall through to the same plain
        // `return true`. They had an honest arm for one revision — greying
        // where the playhead already rested on the frame the jump would land
        // on, compared against playhead_skip_landing_frame — and THAT ARM WAS
        // BUILT ON A FALSE PREMISE: bare Home / End are not pure jumps. Both
        // also STOP A LIVE AUDITION, CLEAR THE MARKER SELECTION (the marker
        // lane's exit repair) and DISSOLVE A RESTING REGION, unconditionally
        // and even when the jump itself moves nothing (the two live arms and
        // the `h` arm, input_key_dispatch.cpp). So a greyed skip made the FACE
        // PROMISE LESS THAN THE KEY DELIVERS — with the cursor parked on a trim
        // bound the key still stopped the audition, cleared the selection and
        // dissolved the region while the dead button could do none of it, which
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
        // partition the only thing that could grey them — and it does not,
        // because all three chords are the mode's OWN vocabulary in there.
        // Their refusals outside the view are the keys' own consumed no-ops (an
        // empty marker store, a cycle with nowhere to go), exactly the class
        // the ruling at the head of this body refuses to mirror.
        case RedesignButton::TransportWalkPrev:
        case RedesignButton::TransportWalkNext:
        case RedesignButton::TransportWalkBoth:
            break;
        // THE FOUR HISTORY COMPANIONS ANSWER PLAIN TRUE (architect 2026-08-15):
        // they are the bottom row's RIGHT CLUSTER inside the `h` view — the
        // arrows' mode twin in the arrows' own slots — and the ruling at the
        // head of this body makes that cluster untruthful and always enabled.
        // They are ALSO the one arm the mode line at the top of this body is
        // told to skip (redesign_button_is_history_companion), which is what
        // the exception is for.
        //
        // TWO EARLIER RULES DIE HERE AND BOTH WERE RIGHT FOR THE POLICY THAT
        // THEN STOOD, so they are recorded rather than deleted:
        //   * THE RESTING-DISABLED FAMILY (2026-08-05 for the walk's two steps
        //     and Revert, 2026-08-08 for the Cumulative toggle): this arm read
        //     `a.history_mode.active`, on the reasoning that bare `,`, `.` and
        //     `u` are bound in exactly one place in the product
        //     (handle_history_mode_key) and Ctrl+H inside the view alone, so
        //     outside it there is no act and a live face would promise one.
        //     TRUE, AND NOW MOOT: since the 2026-08-14 move to this row the
        //     four are not PAINTED outside the view at all — the arrows hold
        //     the slots and these publish zero rects — so the resting answer
        //     reached no pixel and only ever fed the comparator's totality. A
        //     resting `true` is invisible in exactly the same way.
        //   * REVERT'S IN-VIEW GREY, which was not decided here but by the
        //     derived partition: the view's allowlist admits Ctrl+H only while
        //     a diff flag stands (history_mode_revert_subject_standing), so ONE
        //     decision both refused the chord and greyed the button — the
        //     cleanest shape this predicate has. THE ARCHITECT'S REASON FOR
        //     REVERSING IT IS THE BLINK, NOT THE LOGIC: that admission tracks
        //     the diff-flag SELECTION, so the glyph flipped on every click in
        //     the marker lane to report a selection the lane is already
        //     showing. The chord is untouched and still refuses with an empty
        //     subject; only the face stopped saying so.
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
        // reports it wherever the button is painted.
        case RedesignButton::HistoryOlder:
        case RedesignButton::HistoryNewer:
        case RedesignButton::HistoryRevert:
        case RedesignButton::HistoryCumulative:
            return true;
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
        // THE BOTTOM ROW HAS NO ARM HERE AT ALL since 2026-08-15 — all ten
        // of its buttons return a plain `true` from the `default` below, and
        // the ruling, the architect's reasoning and the three reversals that
        // got there are at the first switch's transport block above. Nothing
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
            // REACHED BY EXACTLY EIGHT IDS since 2026-08-15 — THE WHOLE BOTTOM
            // ROW, which breaks out of the first switch to take the
            // loading/blank guard and has nothing else to say (each of the four
            // pairs held an honest arm for a revision or two; the ruling and
            // the three separate reasons they were taken back are at that
            // block). Every other id returned above, from one switch or the
            // other.
            break;
    }
    return true;
}

// THE TOGGLED-ON ("selected") FACE'S PREDICATE — row 1's three view-bar
// buttons, row 3's tabs, row 4's four radios and four toggles (the two view
// pairs; follow, iteration, read-only, history) and the BOTTOM ROW'S ONE
// subject, the Cumulative toggle (a SECOND joined it for hours on 2026-08-15,
// the Play / Stop radio pair, and left with the same day's collapse of that
// pair into one button — its record is in the momentary arm below)
// — each reading
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
        // make the row lie about the reading the moment the view closed. It is
        // read only where the button is PAINTED, which since the 2026-08-14
        // move to the bottom row is inside the `h` view alone (outside it the
        // arrows hold the slot and this button publishes an empty rect). The
        // clause that used to end this note — the button's own resting-disabled
        // face saying the key is elsewhere — went with that arm on 2026-08-15;
        // the empty rect is what says it now.
        case RedesignButton::HistoryCumulative: return a.history_cumulative;
        case RedesignButton::File:
        case RedesignButton::Settings:
        case RedesignButton::Save:
        case RedesignButton::Undo:
        case RedesignButton::Redo:
        case RedesignButton::Render:
        // THE TRIM BUTTON IS MOMENTARY like copy and paste: set-trim-from-
        // region is an act that completes, with no state to stay lit for —
        // the window it wrote is the bar's own always-painted display.
        case RedesignButton::IconTrim:
        // AND SO IS SHOW-REGION BESIDE IT (architect 2026-08-16), which is the
        // point of that ruling rather than an omission. It was designed as a
        // TOGGLE with a lamp reading app.region.active, and the architect found
        // the hole before it was built: "what if user draws a region, then
        // moves the viewport away via drag? The region toggle is on, but the
        // region view can't be accessed because the toggle is already on." A
        // lamp derived from the region's existence LEAVES THE BUTTON LIT for a
        // region that has scrolled offscreen, so the only press available
        // CLEARS the span instead of bringing it back — the one thing the user
        // wanted made unreachable by the face that was supposed to help. HIS
        // FIX WAS TO REMOVE THE STATE, not to qualify the lamp: "maybe the
        // button should not be a toggle, but a 'show region' button, that can
        // always be pressed outside of history. That way it's not stuck having
        // to track the region state — it's just a region-shower, and that's
        // it." So it is one always-meaningful act — ensure a span exists, then
        // bring it into view — with no bit to read and nothing to stay lit for.
        case RedesignButton::IconShowRegion:
        // THE ZOOM GROUP AND THE MARKER VERBS ARE MOMENTARY like copy and
        // paste (2026-08-12): each is an act that completes — a zoom step, a
        // centering, a drop, a delete — with no state to stay lit for. The
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
        case RedesignButton::IconCopy:
        case RedesignButton::IconPaste:
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
        // THE BOTTOM ROW IS MOMENTARY WHOLE AGAIN (architect 2026-08-15): the
        // two skips, the marker-walk three and the four arrows are all acts
        // that complete, with no state to stay lit for — and so, since the
        // same day's collapse, is the ONE PLAY/STOP BUTTON. It carried the
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
// FIVE carry it, each for that one reason: Render (Ctrl+Alt+R renders beside the
// source, Ctrl+Alt+Shift+R into a numbered _miscellaneous cell), Paste
// (Ctrl+Alt+P pastes phase resets, Ctrl+Alt+Shift+P pastes with state), THE
// WALK'S TWO ARROWS since 2026-08-07, whose shifted twins are the walk's WALL
// JUMPS: bare `,` steps one checkpoint older and Shift+`,` goes to the oldest,
// bare `.` steps one newer and Shift+`.` goes to the newest
// (handle_history_mode_key, input_key_dispatch.cpp, owns both shapes; the arrows
// dispatch them through the one press body like every other button) — and,
// since 2026-08-15, THE TRIM SCISSORS, whose twin is Shift+X the MAXIMIZER
// (reset the trim to the whole song). Its 2026-08-11 "Shift+X stays
// keyboard-only" clause is SUPERSEDED and the reason is the glass rig: the
// maximizer had no pointer route at all, so a keyboardless panel could set a
// trim window and never get back out of it. The admission grants the 500 ms
// long press → shift act (kChromeShiftHoldMs) and the two-line hint with no new
// machinery.
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
inline constexpr bool redesign_button_shift_admits(RedesignButton b) {
    return b == RedesignButton::Render || b == RedesignButton::IconPaste ||
           b == RedesignButton::HistoryOlder ||
           b == RedesignButton::HistoryNewer ||
           b == RedesignButton::IconTrim;
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
        // exception, scoped to exactly the shift-admitting buttons — this one,
        // Paste, and the walk's two arrows since 2026-08-07.
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
        // THE TRIM BUTTON, in the TWO-LINE form since 2026-08-15: its shifted
        // twin is Shift+X the MAXIMIZER, admitted so the glass rig has a route
        // to it at all (the reason is at redesign_button_shift_admits). The
        // 2026-08-11 record that "bare `x` has no shifted twin ON THE BUTTON"
        // is superseded.
        case RedesignButton::IconTrim:
            return {"Set trim from region (x)",
                    "Press Shift for the whole song."};
        // THE SHOW-REGION BUTTON (2026-08-16), one line: the chord has no
        // shifted twin (Ctrl+Shift+X already spends the shift), so no hint
        // line and no shift admission — the static_assert below is what keeps
        // those two facts one. The accelerator keeps its capital and its
        // spelled-out modifiers, this table's rule for a CHORD. IT IS "REGION"
        // AND NOT "TRIM REGION" by the architect's own naming: it shows
        // WHATEVER region stands, and the trim window is only where it seeds
        // from when there is none.
        case RedesignButton::IconShowRegion:
            return {"Show region (Ctrl+Shift+X)", nullptr};
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
        // THE SINGLE-MARKER VERBS (2026-08-12), all one-line, the acts named
        // plainly in HELP's vocabulary. None admits shift.
        case RedesignButton::IconMarkerDrop:
            return {"Drop marker (s)", nullptr};
        case RedesignButton::IconMarkerDelete:
            return {"Delete markers (Delete)", nullptr};
        case RedesignButton::IconMarkerDisable:
            return {"Disable markers (Ctrl+D)", nullptr};
        case RedesignButton::IconMarkerInherit:
            return {"Toggle inherit (Ctrl+N)", nullptr};
        case RedesignButton::IconCopy:   return {"Copy phase resets (Ctrl+P)", nullptr};
        case RedesignButton::IconPaste:  return {"Paste phase resets (Ctrl+Alt+P)",
                                                 "Press Shift for paste phase state."};
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
        // THE CUMULATIVE TOGGLE, one line: the key toggles and has no shifted
        // twin. Like the three below it, this hint is only ever reachable
        // INSIDE the view: the four are painted nowhere else (the bottom row's
        // cluster swap), so the tooltips-on-disabled ruling reaches them only
        // on the frames the view stands — AND SINCE 2026-08-15 IT BUYS NOTHING
        // AT ALL FOR THIS CLUSTER, all four answering a plain `true` there
        // (Revert's conditional grey was the last thing it still bought, and
        // the architect reversed it as a per-selection blink). The hints stay
        // because they explain the buttons, not because anything greys.
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
        // behavior: a disabled icon still explains itself) still governs where
        // these are painted; since 2026-08-12 that is inside the view alone,
        // first as the icon row's collapsed four and since 2026-08-14 as the
        // bottom row's swapped cluster.
        case RedesignButton::HistoryOlder:
            return {"Older (,)", "Press Shift for oldest."};
        case RedesignButton::HistoryNewer:
            return {"Newer (.)", "Press Shift for newest."};
        // THE REVERT ACT, one line: the chord has no shifted twin. It was
        // GREYED inside the view whenever nothing was selected and showed this
        // hint there too, per the same ruling; since 2026-08-15 it is lit in
        // every frame the view stands (the architect's reversal of a grey that
        // tracked the diff-flag selection), so the hint is an ordinary enabled
        // one and the disabled ruling no longer reaches it.
        case RedesignButton::HistoryRevert: return {"Revert (Ctrl+H)", nullptr};
        // THE BOTTOM ROW (2026-08-11 for the transport, 2026-08-15 for the
        // marker-walk group), all one-line forms: no button on it admits
        // shift. The names are the ratified sentence-case labels, the
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
// ROW 3'S TWO WALK-SELECTOR WORDS, while the `h` view stands and the tabs
// select the walk instead of being the A/B pair (architect 2026-08-05 for the
// repurposing, 2026-08-08 for what the words say). Sentence case, the ordinary
// convention: these are ordinary labels and not the two named title-case
// exceptions. THE ROSTER'S ONLY LABEL CONSTANTS since the 2026-08-12 relayout
// deleted row 2's labeled faces (redesign_button_label serves the tab row
// alone now).
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
// (RedesignButton::HistoryCumulative, bare `u`), a MODE bit rather than a
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
    // its ordinary two-line hint — what it does and where it works, which is
    // what every dead button in the product shows, the tooltips-on-disabled
    // ruling's own shape. (The clause named the FOUR RESTING-DISABLED ROW-4
    // BUTTONS as the example; they left row 4 on 2026-08-14 and stopped being
    // resting-disabled on 2026-08-15, so the general rule is named instead.)
    // RENDER'S MID-RENDER HINT, ranked above its iteration form like the
    // label: one line, "Cancel", NO KEY NAMED — deliberately. The act is the
    // button's own (the ruled chord divergence at
    // finish_chrome_press_release's Render arm),
    // and naming Esc would lie whenever a region rests: bare Esc ranks the
    // region clear above the render cancel, so the key and the button part
    // company in exactly that state. NO SHIFT LINE either: while the face is
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

// A BUTTON'S LABEL, by the same bits and for the same reason. The constant
// per-button labels live with the painter's roster half (kTabs,
// paint_handler.cpp); this answers only "does this button override its
// own", which TWO now do — row 3's tabs on the history view's bit. THE
// TOOLBAR PAIR'S LABEL ARMS ARE DELETED PRODUCER-LESS (2026-08-12, the
// relayout): with row 2's labeled painter gone no label is painted for Save
// or Render in any state — their stateful words are the TOOLTIP overload's
// above, their stateful faces the GLYPH swaps (redesign_button_icon).
inline const char* redesign_button_label(const AppState& a, RedesignButton b,
                                         const char* table_label) {
    // THE TABS ARE THE WALK SELECTOR IN THE `h` VIEW, so they say which walk
    // they select rather than which tab they are; the READING is row 4's
    // Cumulative toggle since 2026-08-08 and has no label on this row at all.
    // The shaped-run layout the tab painter does absorbs the width change (both
    // words are wider than "A"/"B", and the tab's width has always been
    // max(minimum, shaped + 2*pad)). The order is the enum's, which is the
    // painted one, and the same one the press claim and the Ctrl+Tab cycle
    // read: Remote then Local.
    if (a.history_mode.active) {
        if (b == RedesignButton::TabA) return kCompareRemoteLabel;
        if (b == RedesignButton::TabB) return kCompareLocalLabel;
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
    (redesign_button_tooltip(RedesignButton::HistoryOlder).line2 !=
     nullptr) ==
        redesign_button_shift_admits(RedesignButton::HistoryOlder) &&
    (redesign_button_tooltip(RedesignButton::HistoryNewer).line2 !=
     nullptr) ==
        redesign_button_shift_admits(RedesignButton::HistoryNewer) &&
    (redesign_button_tooltip(RedesignButton::IconTrim).line2 != nullptr) ==
        redesign_button_shift_admits(RedesignButton::IconTrim) &&
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
// ROW 4'S, THE VIEW BAR'S AND THE BOTTOM ROW'S SELECTED BUTTONS DO HOVER, and
// that asymmetry with the tabs is the crops': all three ship a selected-hover
// state (the accent outline over the selected fill) and row 3 does not. So the
// zone's carve-out names the tabs alone; the icon row's radios, the view bar's
// three and the bottom row's Cumulative toggle are hoverable in
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
int hit_test_flag(const AppState& app, const GuiAudio& audio,
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
