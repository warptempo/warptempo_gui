#include "input_handler.h"

#include "gui_display_context.h"
#include "render.h"
#include "warp_frame_map_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Trim gestures (architect-ruled hardfail model; the full ruling sits at the
// TrimState store in app_state.h): begin and end are authored named roles, and
// THE WINDOW IS ALWAYS SET — no unset state, no lone bound (architect
// 2026-07-30). The FULL window [0, total-1] is the old unset state under a new
// spelling: it renders untrimmed and plays to the natural end, and Reset Trim
// (Shift+T, PRESETS' hold) is how the user gets back to it.
// Every gesture clamps each bound to its absolute walls — frame 0 to EOF-1,
// the same wall both marker columns hold. All authored positions (both
// marker columns and both trim bounds) share the inclusive [0, total-1]
// domain — the end bound's old exclusive-at-total wall is retired. Every wall
// check is a plain integer compare — literally the load guard's comparison.
// A BOUND SET BY A PEN PLACE IS THE FRAME THE CONVERSION FINDS THERE, THEN
// THE WALLS, NOTHING RAISING IT TO THE SOURCE'S END (architect 2026-11-19):
// in a mapped domain an end carried as far as it goes can rest a few frames
// short of total-1, a truthful sub-window, and Reset Trim is the direct road
// to the exact full window — the rule, its reason, its worked example and
// every road that writes total-1 at trim_bound_frame_at_x, the one owner.
//
// PARTNER WALLS ARE NOW PER-ROUTE (architect 2026-08-02, splitting what used to
// be one blanket "there are no partner walls" clause). What is unchanged
// everywhere is the REST rule: crossed/equal can never rest, because every trim
// commit runs auto_clear_crossed_trim (below) and a pair landing on or across
// itself RESETS to the full window. What differs is how far a gesture may push
// before that tail sees it:
//   THE SINGLE-BOUND DRAG CLAMPS AT ITS PARTNER — the handles "cannot move past
//     each other". The moving bound stops INCLUSIVELY on the resting one, so
//     mid-drag the store can hold begin == end but never a crossed pair. A
//     coincident release then falls into the commit rule above unchanged, which
//     makes dragging one handle onto the other the DRAG'S OWN ROUTE to clearing
//     the trim — the reset is the feature, not an accident. Expression and
//     rationale at the clamp itself, in update_trim_drag.
//   THE BOTH-BOUNDS (bridge) DRAG NEEDS NO PARTNER WALL and has none: it moves
//     the pair rigidly by one shared delta, so the gap between the bounds is
//     invariant for the whole gesture and there is nothing to cross.
//   THE BARREL'S BOUND-SET CLICK REFUSES AT ITS PARTNER (architect
//     2026-11-10): it moves WHICHEVER END CAP IS NEAREST the click BY
//     REAL DISTANCE ON THE BAR, a tie the begin's but right of both caps,
//     where it is the end's — inward from inside the trim,
//     outward from outside it — and a click within half a px of its cap's
//     own painted place, on the bound's own frame, or one whose
//     frame would land the bound on or past its partner (a midpoint tie
//     whose frame rounds onto the end's, where the caps stand under a px
//     apart),
//     is a consumed no-op that
//     writes nothing
//     (trim_bound_click). A clamp would move the bound somewhere the user
//     did not click; a refusal is the ruled answer there. IT ACTS AT THE LIFT
//     since 2026-08-15, and since 2026-08-17 it is the ONE click still
//     deferred (the press arms
//     PendingClickAct, app_state.h, and a CROSSING runs the set at the press
//     column and then hands over to that bound's endcap drag): the verdict
//     is asked live where the act runs, and it reads no clock — a long press
//     means nothing on the trim bar (the S Pen's side button being the Ctrl
//     bit, a tap with it held is the click).
//   THE TYPED AND LOADED ROUTES (the settings editor's `trim_*=` commits, and
//     the load) HAVE NO CLAMP AT ALL — a typed pair can still cross, and the
//     crossed/equal reset is exactly what catches it. The clamp is the DRAG's,
//     because a clamp is only meaningful where a bound is being pushed.
//   THE TRIM PRESETS LIST'S APPLY HAS NO CLAMP EITHER (2026-10-28 night): it
//     writes a stored pair whole, and its file's reader admits only a pair
//     inside the walls with begin < end strictly (trim_presets_file.h), so
//     the pair it writes is one a resting trim can hold.
//   THE SWEEP HAS NO PARTNER TO CLAMP AT — it writes BOTH bounds from one
//     anchor and one moving column — and needs none: it authors ANY span its
//     two ends describe, down to nothing at all, and a COINCIDENT release
//     falls into auto_clear_crossed_trim exactly as the single-bound drag's
//     does (the shared escape, below). The MINIMUM WIDTH FLOOR it carried
//     from 2026-08-18 to 2026-08-19 is retired — the record is at
//     write_trim_from_sweep.
// NO TRIM WRITER MOVES THE PLAYHEAD (architect 2026-11-07, session 88:
// "Setting the trim should not move the playhead — I usually use scrub now,
// so moving playhead is just distracting") — the one authoritative statement
// of the rule; other sites state their own class and point here. It
// REVERSES 2026-08-05's park at the new trim start and the sweep's carry of
// 2026-07-30: the cursor stays where it stood in the music, inside the new
// window or outside it. THE MEMBERSHIP, re-derived by grepping the routes
// that write a trim bound, none of which writes the cursor:
//   * the barrel's bound-set click (set_trim_bound_at_click), at its
//     motionless lift or its threshold crossing (2026-08-15), the nearest
//     end cap moved to the click (2026-11-10);
//   * the ENDCAP / BRIDGE drag (update_trim_drag per motion, commit_trim_drag
//     at the release), on the trim bar's caps and bridge;
//   * THE SWEEP (write_trim_from_sweep, below — the TRIM arm lamp's touch
//     drag, 2026-11-07): its begin deselects and arms and seats no playhead,
//     its motion writes the trim alone, its release commits through
//     commit_region_sweep;
//   * the SETTINGS EDITOR's `trim_*=` commits (settings_editor.cpp), the
//     active tab's through the tail, the inactive band's a parked pair;
//   * THE TRIM PRESETS LIST's APPLY (architect 2026-10-28 night,
//     GuiInputHandler::trim_presets_apply, input_key_dispatch.cpp): both
//     bounds from the preset at once, then the preset FRAMED
//     (run_span_framing_command, a camera move alone);
//   * the CROSSED / COINCIDENT RESETS (auto_clear_crossed_trim), a write to
//     the full window;
//   * Shift+T, PRESETS' hold, the maximizer (handle_trim_clear_both).
// The first six run the shared commit tail (commit_trim_mutation); the
// maximizer is its one deliberate non-caller. Each route's EXISTING regime
// stands — the trim-mutation playback stop (a stop moves no cursor) and the
// setter's deselect.
//
// NO DOMAIN MOVES UNDER THE CURSOR EITHER, so nothing re-seats it: the live
// domain (live_total_frames, the clamp at clamp_playhead_to_live_domain) is
// the source in source view and the LIVE WARP MAP'S target total in target
// view, and the trim enters neither — the map's key is the markers, the
// scale and the audio (target_view_warp_frame_map_cached). The trim crops
// only the bound preview buffer (GuiPlayback::domain_begin / domain_end, the
// render's post crop), so a cursor left outside a new window in target view
// meets the launch's own refusal (playback_launch_playable: the PLAY face
// greys, the state a placement outside the window always reached), and in
// source view plays on to the song's end as ever (the window does not bound
// source playback, 2026-08-05).
// (THE SET-FROM-REGION ACT, once a member, is gone: setting the region IS
// setting the trim, so that act no longer exists. The
// maximizer is unchanged and is the recovery route, trim having no undo.)
//
// EVERY TRIM ROUTE IS READ-ONLY-LEGAL (architect 2026-08-07). Read-only
// protects the AUTHORED MUSICAL CONTENT — the two marker stores and the engine
// settings — and trim is BAND: it lives in ViewState beside the viewport and the
// zoom, it has no undo, and it never dirties the session, which is what the
// gate's old "authoring mutation" classification of it was missing. So Reset Trim (Shift+T)
// is on the read-only allowlist, the endcap / bridge
// drags and the barrel's bound-set clicks carry no read-only refusal
// anywhere on their routes, the SWEEP carries none either (trim being band
// whichever surface writes it), and the
// settings editor's typed `trim_*=` arms commit in a locked tab. NOT ONE of
// the behaviors above changed with the
// admission — the bound-set click's verdict, the
// partner clamp, the setter's deselect and the
// trim-mutation playback stop are the same code taking the same decisions. The
// full ruling is at read_only_key_blocked (input_key_dispatch.cpp), the model's
// one authoritative home.
//
// THE ZERO FLOOR (frame 0 — not a span rule, and unrelated to the retired
// sweep width floor) is subsumed by the walls but remains the reason a lower
// clamp exists at all: a negative position is unrepresentable in the authored
// frame form the .settings file persists (parse_authored_frame rejects
// negatives as malformed) — a format-representability floor, not a spacing
// or validity rule. Past-EOF bounds are unreachable: the gesture walls
// forbid authoring one, and the load boundary (file_loader / CLI)
// hard-fails a past-EOF bound in a hand-edited .settings as adversarial
// input (a .settings applies only to its own audio). validate_trim_frames
// (trimmer.h) still authors the trim-validity vocabulary, but it sees
// SUB-WINDOWS only (a full window never reaches plan_trim) and a refusal at
// render time still means "render untrimmed" (do_render's fallback), not a
// refused render.
//
// THE BOUNDS STAY OUTSIDE THE SELECTION SYSTEM (architect 2026-07-18): never
// selected, never a Tab stop, no Delete, no keyboard nudge — Home / End are
// their navigation, and Reset Trim, the endcap / bridge drags, the sweep and the
// bound-set clicks are trim's whole interface. The flow is DOWNWARD ONLY: a
// setter EMPTIES the selection and nothing a trim route does ever selects; the
// playhead is neither an input nor an output of a trim route (the rule above,
// 2026-11-07): none reads it and none writes it. Any further trim-vs-marker asymmetry needs architect approval.

namespace {

// THE PARTNER CLAMP, the single-bound drag's
// (architect 2026-08-02: the trim handles "cannot move past each other"): the
// moving bound stops INCLUSIVELY on the resting partner — a begin clamps DOWN
// to the end, an end clamps UP to the begin — so the pair can hold begin == end
// mid-gesture but never a crossed shape. Value in, value out; the partner is
// read from the pair as it rests, which is the other, un-dragged field.
//
// ONE CALLER, the MOTION arm (update_trim_drag's single-bound path), which
// clamps the tracked candidate at every event; the release commits that
// value as it stands (commit_trim_drag takes no snap, architect 2026-11-15,
// the no-grid arc), so a bound pinned on its partner rests pinned and the
// ruled drag-onto-partner quick-clear reads the equality the motion left. The
// BRIDGE arm deliberately does not call
// this and must not: it moves the pair rigidly by one shared delta, so the gap
// is invariant and there is no partner wall by ruling (recorded at that arm).
int64_t clamp_trim_bound_at_partner(bool is_begin, int64_t v,
                                    const TrimState& trim) {
    const int64_t partner = is_begin ? trim.end_frame : trim.begin_frame;
    if (is_begin) {
        if (v > partner) v = partner;
    } else {
        if (v < partner) v = partner;
    }
    return v;
}

// THE TRIM FORMERS' ONE PLACE->BOUND LANDING, AND HOW EACH WALL IS REACHED
// (architect 2026-11-15, the no-grid arc, ruling B: "B, yes, also the exact
// frame there" — the trim's edges land on the frame under the pen's real
// place). The trim formers that land a bound at a place take its frame
// here: the sweep's two ends (sweep_trim_frame_at_x, the armed touch sweep)
// and the barrel's bound-set click (trim_bound_click). `x` is the pen's real
// place on the strip, waveform-relative, held by the caller into the strip's
// closed extent [0, w]. The frame is authored_frame_at_x_on_basis over the
// displayed-or-live target map on the item viewport basis — the displayed
// frame under x (displayed_frame_at_x, warp_frame_map_view.h, the pen acts'
// one conversion), in target view through the displayed map at full
// precision, snap_authored_frame the one rounding — with the absolute walls
// [0, total-1] after, plain integer compares.
//
// A TRIM BOUND SET BY A PEN PLACE IS THE SOURCE FRAME THE CONVERSION FINDS
// UNDER THAT PLACE, THEN THE WALLS; NOTHING RAISES IT TO THE SOURCE'S END —
// THE RULE'S ONE STATEMENT, for this landing and for the endcap and bridge
// drags (update_trim_drag), which point here (architect 2026-11-19: "The
// method for achieving a full song trim is the long press on the presets
// button. Anything else should not have a snap, and I'd rather have a
// truthful shortfall, meaning a truthful undercount, meaning a truthful trim
// that is just a few frames short of catching the EOF, than a snap"). A
// wall is reached only where the conversion itself answers past it.
//
// THE START: frame 0 stands at the strip's left edge wherever the view
// rests at the left wall (frame 0's image is exactly 0 in either view), and
// a pen at x = 0 answers it. THE END: at the right wall the strip's right
// edge stands at or past the DOMAIN's total by construction — the wall's
// lattice index k is the smallest with (k + w)·q >= total, the real span on
// the lattice's own index (max_viewport_start_grid, main.cpp), and at the
// whole-song zoom the start is 0, k = 0, with w·q >= total
// (fit_zoom_level's ceil), so the frame at x = w is (k + w)·q on the item
// basis (displayed_frame_at_x, origin 0 at both). IN SOURCE VIEW, and in a
// target view with no map, the displayed frame is the source frame, so a
// pen at or past the last frame's place, x >= (total − 1) / q − k less half
// a frame, answers a frame >= total-1 and the wall holds it on total-1 —
// the frame under the pen, and a sweep carried past the strip's right edge
// (held at x = w by its caller) rests its end there. IN A MAPPED DOMAIN the
// domain's total is the DISPLAYED total, the source end's ROUNDED image
// (live_total_frames), and the frame at x = w can stand short of the image
// of total-1, so its inverse — the bound — stops short of the end: a 4.00 x
// 2.00 speed-up of 1,843,203 source frames ends at target 230,400.375, its
// displayed total 230,400, and total-1 = 1,843,202 maps to 230,400.25; at
// Target FIT (w 2304, q = ceil(230,400 · 16 / 2304) / 16 = 100) the strip's
// right edge is 230,400 exactly, whose inverse is 1,843,200, so an end
// carried as far as it goes rests on 1,843,200, two frames short of
// total-1 — a truthful sub-window, rendered as one. The shortfall is under
// half the last section's ratio of source frames per displayed frame (the
// edge stands at most half a displayed frame left of the end's real
// image), none where that ratio is under 3, and none where the edge stands
// past the image of total-1 — the fit's ceil to a sixteenth decides it per
// piece and width: the same map on 1,801,603 frames (displayed total
// 225,200) fits at q = 97.75, its edge 225,216 past the image of total-1,
// so there the end rests on total-1. (In Source view at the whole-song zoom
// of the 1,843,203-frame piece q = 800.0625 and the edge is 1,843,344: the
// end rests on total-1.)
//
// THE ROADS THAT WRITE total-1, every one a wall or the full pair, none a
// raise: (a) RESET TRIM (PRESETS' hold), the direct road to the exact full
// window [0, total-1], writing it from any trim (handle_trim_clear_both);
// (b) THE CROSSED RESET, a commit whose end lands on or before its begin
// writing the same pair (auto_clear_crossed_trim); (c) THE BRIDGE PUSHED
// PAST THE RIGHT WALL, its proposal past the ceiling's displayed frame,
// which rests the end on total-1 and keeps the begin what the delta gives
// it — so a bridge on [0, 1,843,200] above, pushed right at Target FIT,
// lands the full pair, the delta clamped to 0 keeping the begin on 0
// (update_trim_drag's pair arm); (d) A PEN PLACE whose conversion answers
// at or past the last frame — in Source view, or in a target view with no
// map, every end carried to the strip's right edge at the right wall; in a
// mapped domain only where the edge stands past the image of total-1 —
// this landing, the endcap drag and the TRIM sweep's ends alike, the wall
// holding it; (e) the typed `trim_end=` line and a trim preset whose pair
// holds it, writing the frame they carry. [0, total-1] is the integer pair
// every full-window compare (trim_is_full_window — Reset Trim's
// already-full guard, the render's sub-window test, the crossed reset's
// one-frame exception) reads as full; a trim a few frames short of it is
// not the full window and renders as the sub-window it is, with no
// tolerance anywhere. Degenerate geometry is the callers' to refuse first.
int64_t trim_bound_frame_at_x(const AppState& app, const GuiAudio& audio,
                              double x) {
    const int64_t wall = audio.total_frames() - 1;
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    int64_t frame = authored_frame_at_x_on_basis(
        app, audio, x, map, basis.vp_start_frame, basis.spp,
        basis.lattice_origin);
    if (frame < 0)    frame = 0;
    if (frame > wall) frame = wall;
    return frame;
}

// THE TRIM DRAGS' ONE PLACE -> DISPLAYED FRAME CONVERSION (the endcap and the
// bridge drags' anchor and motion): the displayed frame under the pen's real
// place, unrounded (displayed_frame_at_x, the pen acts' one conversion), on
// THE ITEM BASIS (architect 2026-09-24, strictly as painted: the span the
// trim pass drew the bar and its ends on, the displayed map's twin, not the
// live viewport a worker job in flight at the press has moved ahead of the
// pixels; the freeze holds it from the aimed press to the release; cold, the
// live viewport by the basis's own contract). THE PLACE IS A WAVEFORM PLACE
// whatever surface the press came from, measured off waveform_area's x.
//
// THE PEN'S PLACE IS NOT HELD INTO THE STRIP (architect 2026-11-15, the
// no-grid arc): both drags follow the pen at the grab's own offset — the
// pair by the pen's displacement from its grab, the single bound by the
// frame under its TOWED PLACE, the pen's place less the grab's offset (on
// the identity, the same thing as the displacement; architect 2026-11-18)
// — so what they need of a pen past either end of the strip is its REAL
// place, and the conversion is linear, meaningful at any x. WHAT IS HELD IS
// THE BOUND'S PROPOSAL, by the motion
// arms (update_trim_drag): the single bound into the strip's own extent, the
// frame at x = 0 through the frame at x = w, then the song's walls, then its
// partner; the pair by its wall-held shared delta. Holding the pen instead
// would stop a bound short of the strip's edge by the grab's own offset: an
// end grabbed a fraction of a px right of its bound could then never be
// pushed the last frames to the frame at x = w (Sol's review, 2026-11-16).
// The roads with no
// grab offset — the bound-set click and the TRIM sweep's ends, whose place
// IS the frame — hold the place into [0, w] themselves
// (trim_bound_click, apply_region_drag_motion). False (out untouched) on
// unusable geometry.
bool trim_displayed_frame_at_mouse_x(const AppState& app,
                                     const GuiAudio& audio, double mouse_x,
                                     double& out_frame) {
    if (audio.total_frames() <= 0) return false;
    const GuiRect area = waveform_area(app);
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    if (basis.spp <= 0.0 || area.w <= 0) return false;
    out_frame = displayed_frame_at_x(
        mouse_x - static_cast<double>(area.x),
        static_cast<double>(basis.vp_start_frame), basis.spp,
        basis.lattice_origin);
    return true;
}

// A TRIM BOUND'S EXACT PLACE, waveform-relative, as the trim bar's painter
// has it (GuiPaintHandler::paint_trim's chain on the same item basis and
// displayed map: displayed_trim_ms, then trim_bound_column's unclamped
// x_raw — the slider box's place, not the grab's clamp). The endcap drag's
// grab offset is measured against it on the identity (begin_trim_drag; a
// mapped domain's against trim_bound_image_place_x below).
double trim_bound_place_x(const AppState& app, const GuiAudio& audio,
                          int64_t frame) {
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    return trim_bound_column(
        displayed_trim_ms(frame, map.empty() ? nullptr : &map),
        basis.vp_start_frame, basis.vp_end_frame, basis.lattice_origin,
        basis.area_w).x_raw;
}

// A TRIM BOUND'S UNROUNDED IMAGE'S PLACE, waveform-relative, in a MAPPED
// domain (architect 2026-11-18, send Y): trim_bound_place_x's chain on the
// same item basis and displayed map, the image taken straight from the one
// forward map (map_source_to_target) without displayed_trim_ms's rounding to
// a whole displayed frame. The endcap drag's mapped arm measures its grab
// offset against it (begin_trim_drag), so its towed place at zero travel is
// the bound's own image and the first motion continues from the bound.
double trim_bound_image_place_x(const AppState& app, const GuiAudio& audio,
                                int64_t frame) {
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    return trim_bound_column(
        map_source_to_target(static_cast<double>(frame), map),
        basis.vp_start_frame, basis.vp_end_frame, basis.lattice_origin,
        basis.area_w).x_raw;
}

} // namespace

// Reset the pair to the canonical FULL window for the loaded source — the
// field-level act shared verbatim by handle_trim_clear_both (the Shift+T
// maximizer) and the crossed-commit reset (auto_clear_crossed_trim) so the two
// can never drift. The seeding formula has ONE owner, full_trim_window
// (app_state.h), which is also what the load and the per-tab bands use. Fields
// only: no invalidation, no trigger — callers own their repaint tail.
void GuiInputHandler::reset_trim_to_full_window() {
    app.trim = full_trim_window(audio.total_frames());
}

// Architect ruling (2026-07-15, re-posed 2026-07-30 under always-set):
// crossed/equal trim bounds cannot REST — committing one bound onto or across
// the other RESETS BOTH bounds to the song edges, the trim sibling of the
// marker normalizations (ambiguous states resolve instead of resting or
// refusing). The bar's endcaps visibly JUMP TO THE SONG EDGES, and that is the
// whole signal — it used to be the old chips vanishing; with the window always
// set there is nothing to vanish into. A card said it too for one day
// (2026-08-30 to 2026-08-31, on the argument that the jump is easy to miss);
// the architect retired it
// under the arc's success rule, the endcaps at the extremes being the visible
// cue on every road and the full window having zero impact on the render. The
// check is the exact integer
// compare end_frame <= begin_frame, run only at COMMIT — nothing pops
// mid-gesture, and update_trim_drag never calls this.
//
// THE `<=` HALF CARRIES THE COINCIDENT DRAG (architect 2026-08-02). Since the
// single-bound drag clamps at its partner, the shape it hands this function is
// begin == end rather than a crossed pair — which this compare has always
// caught, so the drag's route to clearing the trim (drag one handle onto the
// other) is served by the rule exactly as written, with nothing added.
//
// AND SINCE 2026-08-19 THAT ESCAPE IS GLOBAL, still with nothing added (the
// architect, retiring the sweep's minimum width floor): the SWEEP can now
// author any span its two ends describe, including a coincident pair, and its
// release runs this same tail through commit_trim_mutation — so a stroke that
// ends where it began resets to the whole song exactly as a handle dragged
// onto its twin does. ONE OWNER, this compare, for every former: the endcap
// drag, the sweep, the bridge and the typed routes all reach the whole-song
// reset here rather than each spelling an escape of its own.
// Every trim commit site — the SWEEP's release, the endcap/bridge drag release,
// the bound-set click, the settings-editor `trim_*=` commit and the trim
// presets list's apply (whose preset pair is never crossed) — calls this
// after its mutation and before its invalidations, so the repaint shows the
// reset state. (The sweep took the retired set-from-region act's place in this
// list on 2026-08-18; the per-route inventory is at the head of this file.)
//
// THE ONE-FRAME EXCEPTION: on a one-frame source (load-legal) the canonical
// full pair is [0, 0], which trips the end <= begin compare. Recognizing the
// full window FIRST makes the reset a no-op there instead of an every-commit
// self-reset loop — the same precedence the render orchestrators use.
void GuiInputHandler::auto_clear_crossed_trim() {
    if (trim_is_full_window(app.trim, audio.total_frames())) return;
    if (app.trim.end_frame <= app.trim.begin_frame) {
        reset_trim_to_full_window();
        // AND THE RESET SAYS NOTHING (architect 2026-08-31, retiring the
        // one-day card "The trim bounds crossed; the window was reset to the
        // whole song"). The reset is a SUCCESS whose result is on screen: the
        // trim bar's endcaps snap to the song's extremes on every road into
        // here — the endcap drag, the bridge, the sweep, the bound-set click
        // and the settings editor's `trim_*=` commit alike — and the trim is
        // scratch viewport, render-modifying but benign, a full window having
        // zero impact on the output. So there is nothing to warn about and
        // nothing the screen fails to show. The RESET BEHAVIOR is untouched:
        // what left is the sentence, not the rule.
    }
}

// The shared trim commit tail — contract, the callers and the one
// deliberate non-caller at the declaration (input_handler.h).
void GuiInputHandler::commit_trim_mutation() {
    auto_clear_crossed_trim();
    viewport.invalidate_waveform_area();
    target_render.trigger();
    // A TRIM WRITE PUTS THE TRIM ARM LAMP OUT (architect 2026-11-17, send U:
    // a trim written by other means than the sweep the lamp armed — the
    // endcap and bridge drag's release, the bound-set click, a trim preset's
    // apply, the typed `trim_*=` commit — ends the posture as the sweep
    // does; AppState::trim_armed), TRIM alone, its face damaged by its
    // setter. The sweep's own release reaches here having put it out
    // already (commit_region_sweep's own rule), so this is a no-op there.
    if (app.trim_armed) set_trim_armed(false);
    // NO PLAYHEAD WRITE (architect 2026-11-07, session 88): the cursor stays
    // where it stood in the music, the rule at this file's head.
}

// SHIFT+T IS THE MAXIMIZER, PRESETS' HOLD (architect 2026-07-30 for the act,
// 2026-11-08 for the seat): it writes the
// FULL window [0, total-1] — the old "unset" outcome, now spelled as a real
// pair. The caller is handle_trim_maximize. Trim is gesture-owned and excluded
// from undo/redo history.
//
// THE ALREADY-FULL IDENTITY GUARD replaces the old has-a-bound refusal gate: a
// Reset Trim over an already-maximized window stops nothing, repaints nothing and
// triggers nothing, which keeps the refusal-gated stop rule exactly as it was.
// IT IS SILENT (architect 2026-08-31, retiring the one-day card "The trim
// already covers the whole song"): a benign one-dimensional refusal already at
// its state says nothing — the trim bar spans the window and the endcaps sit at
// its ends, so one glance at one place answers what the maximizer would have
// done. (Its one road is the PRESETS key's long press, Shift+T, since
// 2026-11-08.)
void GuiInputHandler::handle_trim_clear_both() {
    if (trim_is_full_window(app.trim, audio.total_frames())) {
        // Nothing to recover, nothing to say (the rule above).
    } else {
        // A TRIM MUTATION STOPS A LIVE AUDITION, IN BOTH VIEWS — the keyboard stop
        // rule at stop_playback_if_playing's declaration (playback_lifecycle.h).
        // Inside the identity guard, so an already-full Reset Trim stops nothing
        // (refusal-gated, like every claim's stop). Reset Trim is in the
        // trim-mutation class by the same 2026-07-30 ruling that made it the
        // maximizer.
        playback_lifecycle.stop_playback_if_playing();
        reset_trim_to_full_window();
        viewport.invalidate_waveform_area();
        target_render.trigger();
        // THE RESET IS A TRIM WRITE, so it puts the TRIM arm lamp out as the
        // shared tail does (architect 2026-11-17, send U; commit_trim_mutation)
        // — on this writing arm alone: an already-full reset writes nothing.
        if (app.trim_armed) set_trim_armed(false);
        // AND THE PLAYHEAD STAYS WHERE IT IS (architect 2026-11-07, session
        // 88: no trim write moves it, the rule at this file's head). The
        // maximizer is not a SETTER either — it deselects nothing.
    }
}

// THE SWEEP'S TRIM WRITE — one anchor, one moving column, and the trim written
// straight from the pair (architect 2026-08-18, the region IS the trim). Its
// one entry is the TRIM arm lamp's touch sweep on the waveform (architect
// 2026-11-07, session 87), which writes here per motion event, so a stroke
// sets the trim
// in one gesture, the trim bar and the canvas's sweep highlight being its
// live picture (the bar drawn at the view's scale; the highlight on the
// bar's span, edge for edge, architect 2026-11-12, sweep_highlight_columns,
// raised by the first accepted write here). THE WRITE'S REPAINT IS ITS
// CALLER'S (architect 2026-11-16, the beats; apply_region_drag_motion,
// input_pointer.cpp, which reads the highlight's span before this write and
// after it): the trim bar's lane whole and the cells the highlight's two
// edges moved across — what a write changes and nothing else. THE SET-FROM-REGION
// ACT was the old two-step's commit half — sweep a free span, then commit it —
// and the step it named is gone. The maximizer is unchanged and is the
// recovery route, trim having no undo.
//
// It is the SETTER's regime whole, taken at the FIRST ACCEPTED bound change
// exactly as the endcap/bridge drag takes it: the trim-mutation playback stop
// and the setter's deselect fire here, once, past the refusals; the shared
// commit tail belongs to the RELEASE (commit_region_sweep, input_pointer.cpp).
// THE PLAYHEAD IS NEVER WRITTEN (the rule at this file's head, 2026-11-07).
//
// NO DOMAIN HOP IN HERE: both endpoints arrive as whole SOURCE frames already
// inside the song walls, from the sweep's one place->trim route
// (sweep_trim_frame_at_x, below this function), so this function orders
// and writes and maps nothing. (The old set-from-region's hop
// — ACTIVE-domain ends crossed through active_domain_to_source_frame in here —
// took the CURSOR's value as the trim's, an authored write off the
// snap_authored_frame chokepoint's trim road every
// other trim former commits through: numerically the same frame in source
// view, but a second route.) Equality is the only collapse a stroke's two ends can
// produce, and an equal pair is a legal thing to write here: the release's
// commit tail turns it into the whole song (below).
//
// Returns whether a bound was actually written, which is the caller's commit
// gate: a refused write leaves the release owing no tail at all.
//
// THE SWEEP AUTHORS ANY SPAN, and its degenerate cases have no backstop of
// their own (architect 2026-08-19, RETIRING the MINIMUM WIDTH FLOOR that stood
// here for one day). The floor — kMinTrimSpanFrames, 65536 whole source frames,
// derived as the resolved tempo-scale ceiling (16) times the engine window kN
// so that a swept window's TARGET span could never fall below one analysis
// window at either end of the value bracket — widened any narrow stroke instead
// of refusing it. It is deleted whole: the architect found the enforced minimum
// distracting in the hand, and too short a span to be worth the machinery that
// held it. Nothing replaced it and nothing should — a floor here is exactly the
// rare-fault backstop the product removes rather than patches.
//
// SO A DEGENERATE SPAN IS LEGAL, and there are two of them, answered in two
// different places:
//   * A COINCIDENT PAIR (begin == end) is answered by the ENDCAP DRAG'S OWN
//     ESCAPE, now global: the release runs the shared commit tail, whose
//     auto_clear_crossed_trim resets the pair to the WHOLE SONG on the same
//     `end <= begin` compare that has always caught a handle dragged onto its
//     twin. One owner for every former; no arm was added here for the sweep,
//     and none is wanted.
//   * A TINY BUT NON-EMPTY PAIR simply rests and renders. It is the RENDER
//     boundary that owns whether such a window is honorable — validate_trim_frames
//     (trimmer.h) refuses only a pair whose TARGET span rounds below one output
//     sample, and a refusal there means "render untrimmed" (the orchestrators'
//     fallback), never a failed render. That is the honest backstop: the stage
//     that actually knows the geometry decides, at the moment it has the map,
//     instead of a gesture-time constant guessing on its behalf.
//
// (The floor's own retired argument for a FLAT COUNT OF SOURCE FRAMES rather
// than a duration — a duration paints a variable width in target view, where
// the resolved tempo-scale product stretches the same source window — is worth
// keeping only as a note to whoever proposes the next span rule here: any such
// rule must be one number in both views or it is two rules wearing one name.)

bool GuiInputHandler::write_trim_from_sweep(int64_t anchor_source,
                                            int64_t moving_source) {
    if (audio.total_frames() <= 0 || audio.sample_rate() <= 0) return false;

    // ORDER IS THE WHOLE OF THIS FUNCTION'S GEOMETRY since 2026-08-19 (the
    // walls are the producer's, applied before the ends arrive): the pair is
    // ordered and written as the two ends describe it, with no width rule of
    // any kind between them (the retired floor's record is at the head of this
    // function). A coincident pair is written like any other and the RELEASE
    // turns it into the whole song through the shared escape.
    const int64_t begin = std::min(anchor_source, moving_source);
    const int64_t end   = std::max(anchor_source, moving_source);
    if (app.trim.begin_frame == begin && app.trim.end_frame == end)
        return false;   // same pair: nothing to write, nothing to repaint

    // A TRIM MUTATION STOPS A LIVE AUDITION, IN BOTH VIEWS (the keyboard stop
    // rule at stop_playback_if_playing's declaration, playback_lifecycle.h) —
    // past the refusals and immediately ahead of the first write, and
    // idempotent across the gesture's later events like the endcap drag's.
    playback_lifecycle.stop_playback_if_playing();
    app.trim.begin_frame = begin;
    app.trim.end_frame   = end;
    // THE SETTER'S DESELECT (architect 2026-07-29): every trim setter empties
    // the selection as it commits. The sweep's press already deselected; this
    // is the write's own rule stated where every other setter states it, and it
    // is idempotent.
    selection.clear_selection();
    // THE PLAYHEAD DOES NOT MOVE (the rule at this file's head) and nothing
    // commits mid-gesture: the repaint — the bar's lane and the highlight's
    // moved edges — is the caller's, on this function's true answer (the
    // head of this body), and the
    // release runs the shared commit tail (the drag arms' own arrangement,
    // update_trim_drag below).
    return true;
}

// THE SWEEP'S ONE PLACE->TRIM ROUTE — the authored value a sweep's place
// yields, and the only producer of write_trim_from_sweep's two arguments (the
// arm's anchor, arm_region_drag_at; the motion path's moving end,
// apply_region_drag_motion). A SWEEP PLACE YIELDS ONE VALUE since
// 2026-11-07 (session 88: the sweep seats and carries no playhead, the rule
// at this file's head), this whole SOURCE frame for the TRIM: THE FRAME
// UNDER THE PEN'S REAL PLACE (architect 2026-11-15, the no-grid arc) through
// the family's one landing, trim_bound_frame_at_x (this file's anonymous
// namespace: the displayed frame under the place, the target view through
// the displayed map at full precision, snap_authored_frame the one
// double->authored conversion, the song walls AFTER in source space) —
// exactly the route the trim bar's bound-set click (trim_bound_click)
// commits through. `x` is waveform-relative, held by the caller into the
// strip's closed extent [0, w]. IT CONVERTS ON THE PAINTED VIEWPORT
// (architect 2026-09-24, strictly as painted): the ITEM viewport basis — the
// viewport the trim bar and the lane are painted on, the displayed map's
// twin — so a worker job in flight at the press does not move the picture
// the sweep lands on ahead of the pixels (cold, the basis is the live
// viewport by its own contract).
// Returns 0 on degenerate geometry (unloaded audio, no strip width), which no
// caller reaches: every arm refuses unloaded audio and the gutter, and the
// motion path returns on an empty strip.
int64_t GuiInputHandler::sweep_trim_frame_at_x(double x) const {
    if (audio.total_frames() <= 0) return 0;
    return trim_bound_frame_at_x(app, audio, x);
}

// Shift+T MAXIMIZES the trim to the full window — RESET TRIM (architect
// 2026-07-25 for the
// act, re-posed 2026-07-30 under always-set — the old "unset" is now
// [0, total-1], which renders untrimmed and plays to the natural end, so the
// user-visible act is unchanged and the endcaps simply rest at the song edges).
// One-shot, history-less like every trim mutation. No read-only check of its own
// (this file's header block: read-only does not reach trim at all since
// 2026-08-07, and the key is on the allowlist).
// Delegates WHOLE to handle_trim_clear_both — whose already-full identity guard
// makes a second Reset Trim a silent no-op (the ruling at that function) and whose
// tail owns the repaint
// (the waveform's) and the target_render trigger. IT TOUCHES NO REGION AND
// NO SELECTION: it is a trim MAXIMIZER, not a SETTER, so the setter-deselect
// rule does not reach it, and the gated region re-sync it used to carry died
// with the trim-window highlight itself (architect 2026-07-30). The trim bar
// shows the whole-song window on the next frame.
void GuiInputHandler::handle_trim_maximize() {
    handle_trim_clear_both();
}

// (BARE `[`, THE TRIM REGION TOGGLE — handle_toggle_trim_region and the icon
// row's Show trim region button — showed and hid the waveform overlay at rest
// from 2026-08-16 until the architect deleted it whole on 2026-09-22: he uses
// the tablet's pen, which reaches the trim bar. The bracket carries the
// Waveform Magnification lamp since.)

// --- Trim boundary pointer gestures -------------------------------------

// THE TRIM DRAGS' ACTIVE-DOMAIN FRAME UNDER THE PEN — the whole frame nearest
// the displayed frame under the pen's real place (trim_displayed_frame_at_
// mouse_x above, the conversion and its basis; llrint, the playhead roads'
// own rounding of an active-domain frame). ITS READER is the pair (bridge)
// drag, which keeps its gap in the domain the user sees: its anchor and its
// motion are both this value, so the rigid pair moves by the pen's own
// whole-frame travel. ACTIVE DOMAIN, NO FORK — consumers that need source
// frames cross through trim_mouse_x_to_source_frame's inverse map.
bool GuiInputHandler::trim_mouse_x_to_active_frame(double mouse_x,
                                                   int64_t& out_frame) {
    double g = 0.0;
    if (!trim_displayed_frame_at_mouse_x(app, audio, mouse_x, g))
        return false;
    out_frame = static_cast<int64_t>(std::llrint(g));
    return true;
}

bool GuiInputHandler::trim_mouse_x_to_source_frame(double mouse_x,
                                                    double& out_frame) {
    if (audio.sample_rate() <= 0) return false;

    double g = 0.0;
    if (!trim_displayed_frame_at_mouse_x(app, audio, mouse_x, g))
        return false;

    // Target view: the pen's displayed frame is an active-domain time; the
    // trim store is source-domain. Inverse-translate THE REAL DISPLAYED FRAME
    // (architect 2026-11-15, the no-grid arc: no whole active frame is taken
    // on the way) at the boundary through the DISPLAYED paint basis
    // (displayed_or_live_target_map — the SAME map the trim bar and its ends
    // are painted with; identity in source view), in full double precision
    // like the marker drag's anchor, on the map the bar is painted with,
    // stale or fresh; the single-bound motion's snap_authored_frame owns
    // the one fractional-to-authored rounding. THE TRIM'S ONE INVERSE: its
    // readers are the single-bound drag's — the anchor and the cursor of
    // the unmapped arm (an identity there, their difference the pen's pixel
    // travel times q), and the mapped arm's TOWED PLACE, whose source frame
    // is the bound (architect 2026-11-18: the frame whose image stands under
    // the cap's place, in every tempo section and past either end of the
    // map) — and the strip clamp's two edges (update_trim_drag). A PLACE
    // LEFT OF THE MAP'S START CONTINUES AT THE IDENTITY SLOPE, the mirror of
    // the map's own rule past its last segment (the map itself holds every
    // earlier time on its first frame): a place left of the piece answers a
    // frame left of the map's first, strictly monotone in the place, so a
    // towed place left of the strip's left edge answers a frame left of the
    // frame at x = 0, which the strip clamp holds there. The conversion is
    // the inverse map alone, and its answer is the bound's: nothing raises
    // it to the source's end (architect 2026-11-19, the rule at
    // trim_bound_frame_at_x's head), so where the strip's right edge stands
    // short of the image of total-1 its frame, and a bound held to it, stop
    // short of the end.
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    out_frame = map_target_to_source(g, dmap);
    if (!dmap.empty() && g < dmap.front().tgt_frame)
        out_frame = dmap.front().src_frame + (g - dmap.front().tgt_frame);
    return true;
}

void GuiInputHandler::begin_trim_drag(TrimHit which, double mouse_x,
                                      bool both) {
    if (which == TrimHit::None) return;
    const bool is_begin = (which == TrimHit::Begin);
    // (The old both-bounds-set backstop is gone with the unset state: a full
    // ordered pair always rests, so there is nothing left to require. The
    // router's own geometry gate is the only thing standing between a press and
    // this arm.)
    app.trim_drag.active       = true;
    app.trim_drag.is_begin     = is_begin;
    app.trim_drag.both         = both;
    app.trim_drag.moved        = false;
    // The drag's own origins, read from the store as it rests HERE — for a
    // bound-set-armed drag that includes the click-set the press already
    // committed, which is exactly the basis the mechanics want (the rigid pair
    // delta and commit's untouched-bound test). No set_click distinction survives:
    // it existed to override these into an Esc-restore origin, and pointer
    // gestures have no cancel (the rule at the drag-modal gate, input_handler.cpp).
    app.trim_drag.orig_frame = is_begin ? app.trim.begin_frame
                                          : app.trim.end_frame;
    app.trim_drag.orig_begin_frame = app.trim.begin_frame;
    app.trim_drag.orig_end_frame   = app.trim.end_frame;
    // No pre-drag playhead capture and nothing to restore: the drag never
    // moves the playhead (this file's header block), and pointer gestures
    // have no cancel.
    // Grab anchor: each arm captures exactly what its motion path consumes —
    // the pair path reads anchor_active_frame (active-domain, for the rigid
    // both-bounds delta); the single-bound path captures the press's real
    // place, its source-domain frame and the grab's pixel offset, of which
    // the UNMAPPED arm reads anchor_frame (motion applying the cursor's
    // source displacement from here — anchor-relative like the marker drag,
    // though that drag's anchor now lives in the active domain, see
    // DragState) and the MAPPED arm press_x and grab_offset_px (the towed
    // place, update_trim_drag). A bad conversion leaves the anchor at 0;
    // harmless since the same unusable state makes update_trim_drag
    // early-return too.
    if (both) {
        int64_t af = 0;
        if (trim_mouse_x_to_active_frame(mouse_x, af))
            app.trim_drag.anchor_active_frame = af;
    } else {
        double anchor = 0.0;
        if (trim_mouse_x_to_source_frame(mouse_x, anchor))
            app.trim_drag.anchor_frame = anchor;
        app.trim_drag.press_x = mouse_x;
        // THE GRAB'S PIXEL OFFSET (TrimDragState::grab_offset_px): the
        // press's place less the bound's place, both waveform-relative on
        // the item basis — the motion's towed place is the pen's less this
        // (the mapped arm its one reader; the identity's offset is written
        // too, so the field means the same in either domain).
        // The bound's place per domain, the fork update_trim_drag takes
        // (architect 2026-11-18, send Y): on the identity its exact place as
        // the trim bar's painter has it (trim_bound_place_x — there the
        // bound's image is its own frame, whole, and the painter's rounding
        // changes nothing); in a mapped domain its UNROUNDED image's place
        // (trim_bound_image_place_x), so the towed place at zero travel is
        // the bound's own image, whose inverse is the bound, and the first
        // motion continues from it — never from the rounded image's inverse,
        // which under a speed-up stands up to half the ratio's frames off
        // the bound, either side.
        const double press_place =
            mouse_x - static_cast<double>(waveform_area(app).x);
        if (displayed_or_live_target_map(app, audio).empty())
            app.trim_drag.grab_offset_px =
                press_place -
                trim_bound_place_x(app, audio, app.trim_drag.orig_frame);
        else
            app.trim_drag.grab_offset_px =
                press_place -
                trim_bound_image_place_x(app, audio, app.trim_drag.orig_frame);
    }
    // The BEGIN only captures drag state — no deselect and no stop here; both
    // ride the FIRST ACCEPTED bound change at motion/commit (architect
    // 2026-07-30), and the deselect then rests (nothing restores it — pointer
    // gestures have no cancel). A press that never moves commits no bound change
    // and is a consumed nothing.
    // (No damage for the pill's drag look here: it has stood since the
    // PRESS, arm_pending_trim_drag's, and the crossing changes nothing about
    // it; commit_trim_drag damages its end.)
}

void GuiInputHandler::update_trim_drag(double mouse_x) {
    if (!app.trim_drag.active) return;
    if (audio.sample_rate() <= 0 || audio.total_frames() <= 0) return;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;

    if (app.trim_drag.both) {
        // The DISPLAYED paint basis, hoisted for every translation this event —
        // the SAME map the trim bar/endcaps are painted with (identity in source
        // view), so the rigid pair tracks the pointer against WHAT IS PAINTED
        // even inside a worker publish window where the displayed map lags the
        // live one. map_source_to_target / map_target_to_source are the two hops
        // (identity on an empty map); the active-domain intermediates
        // nearbyint like source_frame_to_active_domain, and the source-authored
        // results funnel through snap_authored_frame like
        // active_domain_to_source_frame.
        const std::vector<WarpFrameMapSegment>& dmap =
            displayed_or_live_target_map(app, audio);
        int64_t cur_active = 0;
        if (!trim_mouse_x_to_active_frame(mouse_x, cur_active)) return;
        const int64_t ob = static_cast<int64_t>(std::nearbyint(
            map_source_to_target(
                static_cast<double>(app.trim_drag.orig_begin_frame), dmap)));
        const int64_t oe = static_cast<int64_t>(std::nearbyint(
            map_source_to_target(
                static_cast<double>(app.trim_drag.orig_end_frame), dmap)));
        int64_t df = cur_active - app.trim_drag.anchor_active_frame;
        // No viewport clamp on the pair path: blind partner — and blind pair —
        // motion is deliberate. The offscreen ruling forbids blind GRABS (a
        // single grab still requires a visible endcap at press, and the
        // single-bound path below keeps its viewport clamp), not blind MOTION
        // of a rigid pair the user visibly holds by its middle. The pair rides
        // the rigid delta bounded ONLY by the absolute walls below.
        //
        // Wall the rigid delta so BOTH bounds respect their absolute walls:
        // floor 0 on each and a shared ceiling at frame EOF-1 — mapped through
        // the displayed map (monotone, so the active-domain clamp is the
        // source-domain wall's own image). This binds both bounds, so neither
        // slides past EOF under the rigid delta. IT IS NOT THE WHOLE WALL,
        // THOUGH: the clamp is applied to the delta BEFORE the hop back to
        // source, and the map-back's own rounding can carry a bound one frame
        // past total-1 under a speed-up (the round trip is not the identity),
        // so the source-domain wall is re-asserted on the snapped values below.
        // NO PARTNER WALL IS NEEDED ON THIS ARM and none is applied: the pair
        // rides ONE shared delta, so the gap between
        // the bounds is invariant for the whole gesture and they cannot approach
        // each other at all. The partner clamp the 2026-08-02 ruling added
        // belongs to the single-bound arm below — the only one that can push a
        // handle toward its twin.
        // ONE CEILING, ONE LOCAL (2026-08-29). The two bounds had SEPARATE
        // walls when this clamp was written (2026-07-08): begin at frame
        // EOF-1, end at frame EOF, an end bound being exclusive then. The
        // domain unification put both bounds on the shared inclusive
        // [0, total-1] — an end stored at exactly total frames is adversarial
        // load-fatal now (marker_store_validate.h) — so the two expressions
        // became identical and stood as two locals with a note saying so. They
        // are one: a second local that must equal the first is a split waiting
        // to be re-read as a distinction.
        const int64_t wall_active = static_cast<int64_t>(std::nearbyint(
            map_source_to_target(
                static_cast<double>(audio.total_frames() - 1), dmap)));
        // A BOUND PUSHED PAST ITS WALL RESTS ON THE WALL'S OWN SOURCE FRAME,
        // ONE RULE AT BOTH WALLS (Sol's reviews, 2026-11-18) — A WALL, NEVER
        // A RAISE (architect 2026-11-19, the rule at trim_bound_frame_at_x's
        // head: nothing raises a bound to the source's end). The walls in the
        // active domain are ROUNDED images: the floor 0 is displayed 0, which
        // a begin rounding down to it shares with up to half the ratio's
        // source frames under a speed-up (a begin at source 5 under 16x
        // maps to 0.3125, displayed 0); the ceiling is the last frame's
        // rounded image, whose inverse can stop short of total-1 (Sol's 4.00
        // x 2.00 piece: total-1 = 1,801,602 maps to 225,200.25, the ceiling
        // 225,200, whose inverse is 1,801,600). So the push is read off the
        // PROPOSAL BEFORE THE CLAMPS below — the original's displayed frame
        // plus the pen's delta, a whole displayed frame — and only a proposal
        // PAST the wall's displayed frame is a push. Its source frame is past
        // the source's wall: the begin's below displayed 0 is below frame 0
        // (displayed 0 is source 0, the map seeded at {0, 0}); the end's,
        // at least the ceiling plus one, stands more than half a displayed
        // frame past the image of total-1, so its inverse (the map strictly
        // monotone, the identity slope past its last segment) is past
        // total-1 — and the wall holds it there: the begin on frame 0, the
        // end on total-1. An end whose proposal stands ON the ceiling is no
        // push, and rests where the inverse of its proposal puts it (on
        // Sol's piece 1,801,600, two short of total-1: the truthful
        // shortfall; the roads to the exact full window are listed at
        // trim_bound_frame_at_x's head). The
        // push is told apart from a pen that has not pushed even where the
        // clamp leaves the delta 0 (a bound already painted on its wall; the
        // zero-delta rule below would otherwise keep it where it rests):
        // a bound merely standing on its wall's displayed frame, the pen not
        // past it (no travel, or travel that the clamp does not cut), keeps what the zero-delta rule or the map gives it, so a
        // bridge motion that leaves both bounds inside the song never lands
        // either on a wall. THE PARTNER of the pushed bound keeps what the
        // zero-delta rule (a delta clamped to 0) or the map (a delta the
        // clamp shortened) gives it, at either wall alike, so the pair's gap
        // can grow by the frames the pushed bound crosses — all inside the
        // wall's one displayed frame, within half a displayed frame of it
        // (1/40 px at most, q >= 20): the same accepted deformation as the
        // source-wall belt's below. The two pushes are exclusive (the pair's
        // displayed gap oe - ob is at most the ceiling, so one delta cannot
        // carry the begin under 0 and the end over the ceiling). In Source
        // view (an empty map) the begin's push is a strict no-op: ob is
        // orig_begin exactly and the round trip is the identity, so the
        // clamp's df = -ob already lands nb on snap(0) = 0, or keeps an
        // orig_begin of 0 when the clamp leaves the delta 0 — frame 0 both.
        const bool begin_pushed_past_wall = ob + df < 0;
        const bool end_pushed_past_wall = oe + df > wall_active;
        if (ob + df < 0)            df = -ob;
        if (oe + df < 0)            df = -oe;
        if (ob + df > wall_active)  df = wall_active - ob;
        if (oe + df > wall_active)  df = wall_active - oe;
        // snap_authored_frame lands each result on a whole int64 frame (the
        // single fractional-to-authored route). These are the pair's exact
        // dragged frames, and the values the release commits as they stand
        // (commit_trim_drag takes no snap, architect 2026-11-15).
        // A ZERO DELTA MOVES NEITHER BOUND (Sol's review, 2026-11-18): the
        // pair stays exactly as the drag found it, the originals and not
        // their round trip through the map — which under a speed-up is not
        // the identity (1,801,602 -> 225,200 -> 1,801,600 on Sol's 4.00 x
        // 2.00 piece), so a bridge pressed and dragged straight down past
        // the grab gate would otherwise write the trim with no travel. The
        // pair rides its delta off the originals, so a pen brought back to
        // its press's frame restores them exactly. The one exception is a
        // bound pushed past its wall (above), which rests on the wall's
        // source frame — the begin on 0, the end on total-1 — even when the
        // wall clamps the delta to 0.
        int64_t nb = (df == 0) ? app.trim_drag.orig_begin_frame
                               : snap_authored_frame(map_target_to_source(
                                     static_cast<double>(ob + df), dmap));
        int64_t ne = (df == 0) ? app.trim_drag.orig_end_frame
                               : snap_authored_frame(map_target_to_source(
                                     static_cast<double>(oe + df), dmap));
        // THE SOURCE-WALL BELT, both ends (2026-09-02): the active-domain
        // clamp above bounds the DELTA, and the hop back plus
        // snap_authored_frame's rounding can still land a bound at total or
        // total+1 under any speed-up — so the store would hold a value outside
        // the shared inclusive [0, total-1] domain for the gesture's life, with
        // only the release's own re-wall to end it. These are plain integer
        // compares on the already-snapped values, the walls' own form
        // everywhere: no second rounding site, and no partner term (the pair's
        // gap is invariant, so a belt that shortens the rigid delta by a frame
        // is the same accepted deformation the release's snap carries).
        const int64_t source_wall = audio.total_frames() - 1;
        if (begin_pushed_past_wall) nb = 0;
        if (end_pushed_past_wall) ne = source_wall;
        if (nb < 0) nb = 0;
        if (ne < 0) ne = 0;
        if (nb > source_wall) nb = source_wall;
        if (ne > source_wall) ne = source_wall;
        if (app.trim.begin_frame != nb || app.trim.end_frame != ne) {
            app.trim.begin_frame = nb;
            app.trim.end_frame   = ne;
            app.trim_drag.moved    = true;
            // THE PLAYHEAD DOES NOT MOVE, here or at the release (the rule
            // at this file's head): motion updates the bounds and repaints
            // THE TRIM BAR'S LANE, WHOLE, AND NOTHING ELSE (architect
            // 2026-11-16, the beats: a motion's frame repaints what it
            // moved, so it is a small repaint, less of the chrome redrawn
            // and uploaded; the GPU draws every frame whole,
            // GuiPlatform::present). The bar is the trim's one
            // painter while no sweep stands (paint_trim; the canvas's
            // highlight is the sweep's alone, sweep_highlight_columns), and
            // the lane is taken whole because the bar's hit stash
            // republishes only under a clip that covers it
            // (AppState::trim_bar_hit, paint_trim's gate) — which also holds
            // the trough and the pill, real-edged, in one clip
            // rectangle. The keys whose face reads the trim are the tick's
            // comparator's (main.cpp); the release's commit keeps the whole
            // waveform area (commit_trim_mutation).
            viewport.invalidate_rect(top_trim_row_area(app));
            // THE DRAG IS A SETTER, so it DESELECTS (architect 2026-07-29) —
            // past the moved-bounds gate above, so a drag event that changes
            // nothing deselects nothing, and idempotent across the gesture's
            // later events. The publish half is gone with the trim-window
            // highlight (architect 2026-07-30): nothing is written to the region
            // here at all.
            // THE STOP LIVES HERE NOW (architect 2026-07-30): the endcap/bridge
            // drag used to stop at the PRESS, beside a highlight-only publish
            // that has since retired outright, so the stop moved to the FIRST
            // ACCEPTED TRIM MUTATION — this arm — where a trim change actually
            // happens. A trim mutation stops a live audition in both views (the
            // keyboard stop rule, stated at stop_playback_if_playing's
            // declaration, playback_lifecycle.h); the call is refusal-gated and
            // idempotent, so the drag's later events cost one early return each.
            playback_lifecycle.stop_playback_if_playing();
            selection.clear_selection();
        }
        return;
    }

    // THE SINGLE BOUND'S PROPOSAL, ONE ARM PER DOMAIN (architect 2026-11-18,
    // send X's decision, built in send Y: IN TARGET VIEW A DRAGGED TRIM CAP
    // STANDS UNDER THE PEN FOR THE WHOLE DRAG, at the grab's own offset — ON
    // SCREEN IS AS PAINTED, the promise Source view already kept). The fork
    // is the displayed paint basis's map (displayed_or_live_target_map, the
    // map the trim bar and its caps are painted with): empty — Source view,
    // or a target view with no map — the identity; else a mapped domain.
    // The pair path above works in the active domain and reads neither arm.
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    int64_t src_frame = app.trim_drag.orig_frame;
    if (dmap.empty()) {
        // THE UNMAPPED ARM — anchor-relative motion: the dragged bound moves
        // by the cursor's displacement from the grab point, not to the
        // absolute cursor column. cursor_frame is converted identically to
        // the begin-drag anchor, so the bound stays the same distance under
        // the cursor for the whole drag: on the identity the source
        // displacement is the pen's pixel travel times q, and the cap tracks
        // the pen exactly.
        double cursor_frame = 0.0;
        if (!trim_mouse_x_to_source_frame(mouse_x, cursor_frame)) return;
        const double delta_frames = cursor_frame - app.trim_drag.anchor_frame;

        // Pre-drag frame plus the anchor-relative delta. The pointer-derived
        // delta rounds once into the integer domain through
        // snap_authored_frame — the value lands in an authored store field
        // below, so the conversion goes through the single double-to-authored
        // chokepoint like every other authored write; everything after is
        // int64 arithmetic. A drag with no travel (delta 0) keeps its bound
        // as found.
        src_frame = app.trim_drag.orig_frame +
            snap_authored_frame(delta_frames);
    } else if (mouse_x != app.trim_drag.press_x) {
        // THE MAPPED ARM — THE CAP'S PLACE IS THE TOWED PLACE (architect
        // 2026-11-18): the pen's place less the grab's pixel offset
        // (TrimDragState::grab_offset_px, the press less the place of the
        // bound's UNROUNDED image, trim_bound_image_place_x), so its
        // displayed frame is the bound's image plus the pen's displayed
        // travel. THE BOUND IS THE SOURCE FRAME
        // WHOSE IMAGE STANDS THERE: the towed place mapped back through the
        // trim's one inverse (trim_mouse_x_to_source_frame — the displayed
        // map, and left of the map's start the identity slope) and rounded
        // once through snap_authored_frame, nothing raising it to the
        // source's end (architect 2026-11-19, the rule at
        // trim_bound_frame_at_x's head); the strip clamp, the wall and the
        // partner below as for every proposal. At the right the strip clamp
        // holds an end dragged as far as it goes on the frame at x = w,
        // which is total-1 only where that edge's inverse answers it (the
        // head's worked example rests it two frames short at Target FIT); at
        // the left a towed place left of the map's start continues at the
        // identity slope (the inverse's head) and the strip clamp holds it
        // on the frame at x = 0, frame 0 at the left wall. A source-frame
        // DELTA cannot
        // serve here: it takes the inverse map's slope where the PEN stands,
        // not where the bound stands, so with the pen in another tempo
        // section than the bound, or past the map's end, the cap would fall
        // behind or run ahead of the pen. The towed place's own frame has
        // neither fault, in every section and past either end of the map.
        // WHAT THE CAP DOES: the trim bar paints it at its frame's image
        // ROUNDED to a whole displayed frame (trim_bound_place_x), so it
        // stands within half a displayed frame of the towed place's frame
        // plus the image of half a source frame — (0.5 + 0.5 x slope)
        // displayed frames, the slope the displayed frames per source frame
        // in the bound's section — at every travel, the grab's own included,
        // before the strip clamp, the walls and the partner
        // (THE ERROR'S ONE STATEMENT; TrimDragState points here). In px at
        // q >= 20 (kDeepestZoomMinFramesPerPx): (0.5 + 0.5 x slope) / 20,
        // 0.125 px through slope 4 (tempo 0.25 alone) and 0.425 px at the
        // slowest supported section, slope 16 (tempo 0.25 x marker scale
        // 0.5 x piece scale 0.5, value_format.h's floors, all legal at
        // once), under 0.05 px under a speed-up (slope < 1). Under a
        // speed-up several source frames share one painted place and the
        // bound steps among them under a still cap;
        // under a slow-down one source frame spans its ratio of displayed
        // frames and the cap steps by that width, the grid's own truth,
        // never lag. THE FIRST MOTION CONTINUES FROM THE BOUND: at zero
        // travel the towed place's frame is the bound's image and its inverse
        // the bound to floating error, so a sub-pixel travel answers the
        // bound or its neighbor on the travel's side, never a frame against
        // it — a bound on the source's end included: on Sol's 720,007-frame
        // piece under 16x an end on 720,006 (image 45,000.375, painted on the
        // displayed total 45,000) drifting left 0.0078125 px at q = 20 tows
        // to 45,000.21875, whose inverse 720,003.5 rounds to 720,004, under a
        // still cap — the truthful frame under the pen, and no longer the
        // full window (the roads back to it at trim_bound_frame_at_x's
        // head). THE PEN AT ITS
        // PRESS'S X HAS NOT TRAVELLED
        // (TrimDragState::press_x), and the bound stays as found — the else
        // of this arm: a press that never moves writes nothing, not even the
        // round trip's floating hair, and a crossing made by vertical travel
        // alone commits nothing.
        const double towed_x = mouse_x - app.trim_drag.grab_offset_px;
        double source = 0.0;
        if (!trim_mouse_x_to_source_frame(towed_x, source)) return;
        src_frame = snap_authored_frame(source);
    }

    // Viewport clamp: keep the grabbed bound within THE STRIP'S OWN EXTENT,
    // the frame at its left edge (x = 0) through the frame at its right edge
    // (x = w) — so the drag can't push it offscreen, where its precise
    // location would be hidden, AND AN END DRAGGED AS FAR AS IT GOES REACHES
    // THE FRAME AT THE STRIP'S RIGHT EDGE, the structural wall below holding
    // it on total-1 where that frame is past it (architect 2026-11-15, the
    // no-grid arc; architect 2026-11-19, nothing raising it to the source's
    // end: trim_bound_frame_at_x's head; the
    // last fully-visible pixel's left edge, the bound of the marker drag's
    // viewport_marker_bounds, would stop the end a column short of it).
    // THIS, THE BOUND'S PROPOSAL, IS WHAT IS HELD — NOT THE PEN (the
    // conversion's head, trim_displayed_frame_at_mouse_x): the proposal above
    // is the frame under the TOWED PLACE, the pen's place less the grab's
    // offset, wherever the pen has gone — on the identity the bound's own
    // frame plus the pen's real displacement from its grab, which is that
    // frame; in a mapped domain the source frame whose image stands at the
    // towed place, in every tempo section and past either end of the map
    // (architect 2026-11-18) — so FOR ANY GRAB OFFSET g (the grab's place
    // less the bound's, in px) the proposal reaches the frame at x = w once
    // the pen stands at w + g, and the frame at x = 0 once it stands at g,
    // in either view. At the right wall and at the
    // whole-song zoom the displayed frame at x = w is at or past the
    // domain's total ((k + w)·q >= total, trim_bound_frame_at_x's head): in
    // Source view that is the source total, so the end rests on total-1
    // there; in a mapped domain the domain's total is the source end's
    // rounded image and `vp_hi` below is that edge's inverse, which can
    // stand a few frames short of total-1 (the head's worked example: at
    // Target FIT, 1,843,200 of 1,843,202), and the end rests there, the
    // truthful shortfall. At the left wall the frame at x = 0 is frame 0.
    // THE PEN'S TRAVEL PAST THE EDGE IS
    // THE GRAB'S OWN OFFSET, the towed place reaching x = w once the pen
    // stands at w + g — AND NO PEN PLACE STANDS RIGHT OF x = w: the strip is
    // the panel's whole width (waveform_area x 0, edge to edge), so the
    // glass ends at the strip's right edge and the end's towed place reaches
    // the frame there only from a grab left of the end's own place (g < 0).
    // An end zone ends one px right of its bound (trim_endcap_rect), so a
    // grab anywhere in it but its last px carries g < 0 and the pen reaches
    // that frame on the glass's last px or before; a begin zone starts on
    // its bound, so g >= 0 and the pen reaches frame 0 inside the strip (in
    // a mapped domain g is measured off the bound's unrounded image, which
    // stands within half a displayed frame, 1/40 px at most, of the painted
    // place the zone stands on, so each figure moves by that much).
    // A NARROW WINDOW'S END ZONE stands edge to edge right of the begin's
    // (trim_endcap_rect's rule), its right edge up to two zones' width (40
    // device px on the tablet) right of the begin's place, so it stands
    // partly or wholly right of the end's own place — a grab there carries
    // g > 0 and cannot tow the end to the strip edge's frame — and near the
    // strip's right edge the lane cuts it to its visible stretch; once the
    // begin stands within one zone of that edge the zone is wholly past it
    // and the end has no zone at all (render_trim_flags publishes none). THE
    // RULE'S
    // CONSEQUENCE, accepted: in Source view at the whole-song zoom of a
    // 1,801,603-frame piece (q = 782), a window [1,790,000, 1,801,602]
    // stands at x 2289.00 .. 2303.84 (the end's grab place held on 2303),
    // its end zone 2309.00 .. 2329.00, off the glass whole, and a press on
    // the pill lands in the begin's zone; such an end is reached by zooming
    // in until the window is two zones wide, when its zone stands on the
    // end's own place again, or by the begin's stroke first, widening the
    // window from the left. THE
    // GRAB CAN BEGIN ON A BOUND JUST OFF SCREEN (architect 2026-10-05): a
    // button sliding off an edge is grabbed by its visible columns
    // (hit_test_trim_endcap tests what the painter published), its bound up
    // to a button's width past that edge — so the clamp's edge on that side
    // widens to the bound's own resting frame, the drag never pushing it
    // farther off than it began and never pulling it in by a jump at the
    // first motion. The bounds are active-domain while
    // src_frame is source, so inverse-translate the edges through the DISPLAYED
    // paint basis (the same map the tracked bound rode above; monotonic, so the
    // source clamp matches the active-pixel one).
    const GuiRect strip = waveform_area(app);
    double strip_lo = 0.0;
    double strip_hi = 0.0;
    if (!trim_mouse_x_to_source_frame(static_cast<double>(strip.x),
                                      strip_lo) ||
        !trim_mouse_x_to_source_frame(static_cast<double>(strip.x + strip.w),
                                      strip_hi))
        return;
    const int64_t vp_lo = snap_authored_frame(strip_lo);
    const int64_t vp_hi = snap_authored_frame(strip_hi);
    const int64_t lo = std::min(vp_lo, app.trim_drag.orig_frame);
    const int64_t hi = std::max(vp_hi, app.trim_drag.orig_frame);
    if (src_frame < lo) src_frame = lo;
    if (src_frame > hi) src_frame = hi;

    // Structural wall, applied AFTER the viewport clamp so the wall wins
    // where both bind (matching the marker-drag model where structural walls
    // compose with the viewport gate): both bounds clamp to frame EOF-1, the
    // unified authored domain. The floor 0 is already held by the
    // viewport clamp (the visible strip starts at or after frame 0, and a
    // resting bound is never negative), so the 0.0
    // format-representability floor holds by construction here.
    const int64_t wall_hi = audio.total_frames() - 1;
    if (src_frame > wall_hi) src_frame = wall_hi;

    // THE PARTNER IS A WALL, FOR THIS DRAG ONLY (architect 2026-08-02: the trim
    // handles "cannot move past each other"). The moving bound clamps
    // INCLUSIVELY at the resting partner's frame through the one owner
    // (clamp_trim_bound_at_partner, above) — a begin drag stops at the
    // end, an end drag stops at the begin — so the handle can land exactly ON
    // its partner and never beyond it. This is the one route that gained a
    // partner wall; the header block above carries the split.
    //
    // LANDING ON THE PARTNER IS A REACHABLE, MEANINGFUL OUTCOME rather than an
    // edge case to avoid: a coincident release falls into the EXISTING commit
    // rule untouched — auto_clear_crossed_trim's `end <= begin` compare resets
    // the pair to the full window — so dragging one handle onto the other IS
    // the drag's own route to clearing the trim, which is exactly what the
    // architect asked for ("if they are set coincident, make trim 0 to EOF").
    // No second arm was added for it and none is wanted: the equal case was
    // always inside that compare.
    //
    // APPLIED LAST, so the partner wins over both the viewport clamp and the
    // absolute wall. The composition is order-independent in fact — the partner
    // is itself wall-held, so clamping to it can only ever pull the bound
    // INWARD — but last is where it belongs by meaning: an offscreen partner
    // must still stop the handle, and the viewport clamp above must not be able
    // to hand back a frame past it.
    src_frame = clamp_trim_bound_at_partner(app.trim_drag.is_begin,
                                            src_frame, app.trim);
    // THE BOUND'S EXACT DRAGGED FRAME: int64 throughout (the store cannot
    // hold a fractional frame), the bound's own frame plus the pen's real
    // travel — and the value the release commits as it stands
    // (commit_trim_drag takes no snap, architect 2026-11-15).
    const int64_t new_frame = src_frame;
    int64_t& field = app.trim_drag.is_begin ? app.trim.begin_frame
                                            : app.trim.end_frame;
    if (field != new_frame) {
        field = new_frame;
        app.trim_drag.moved = true;
        // THE PLAYHEAD DOES NOT MOVE (the pair arm's twin above; a recorded
        // difference from the marker drag, which tracks its grabbed marker):
        // motion updates the bound and repaints the trim bar's lane, whole,
        // and nothing else (the pair arm's twin above, where the rule is).
        viewport.invalidate_rect(top_trim_row_area(app));
        // THE DRAG IS A SETTER, so it DESELECTS (the pair arm's twin above) —
        // past the moved-bound gate, and idempotent across the gesture's later
        // events; it publishes nothing, the trim-window highlight having retired.
        // The STOP rides the same first-accepted-mutation placement the pair arm
        // records in full.
        playback_lifecycle.stop_playback_if_playing();
        selection.clear_selection();
    }
}

void GuiInputHandler::commit_trim_drag() {
    if (!app.trim_drag.active) return;
    if (app.trim_drag.moved) {
        // THE RELEASE COMMITS THE BOUNDS AS THE MOTION LEFT THEM (architect
        // 2026-11-15, the no-grid arc, ruling B: a moved bound rests on its
        // exact dragged frame — the frame its own motion arm tracked under
        // the pen, whole already — and takes no snap to a painted column).
        // So nothing is re-judged here: the absolute walls, the single-bound
        // arm's partner clamp (a handle lands exactly ON its partner and
        // never beyond it) and the pair arm's wall-held shared delta are the
        // motion arms' own, applied at every event (update_trim_drag), and
        // the values they wrote are the ones that rest. A BOUND PINNED ON
        // ITS PARTNER STAYS PINNED — no snap can carry it off — so the ruled
        // drag-onto-partner quick-clear ("if they are set coincident, make
        // trim 0 to EOF") reads the equality at the tail below as the motion
        // left it. An untouched bound keeps its stored value bit-exact, and
        // an end dragged as far as it goes rests where the motion's strip
        // clamp and wall left it — total-1 where the strip edge's frame is
        // past it, else that frame (trim_bound_frame_at_x's head).
        // The release is the commit: a bound released on or across its
        // partner RESETS the pair to the song edges (crossed/equal cannot rest;
        // ruling at auto_clear_crossed_trim). The playhead stays where it is
        // (the rule and its membership are in this file's header).
        //
        // UNCHANGED BY THE 2026-08-02 PARTNER CLAMP, deliberately. The clamp
        // narrowed what the single-bound arm can hand this tail — ON the partner
        // instead of across it — and `end <= begin` already covered the equal
        // case, so a coincident release resets exactly as a crossed one used to.
        // That is the whole implementation of "if they are set coincident, make
        // trim 0 to EOF": no new arm here, and none wanted. The bridge arm and
        // the typed routes can still hand it other shapes, which is the other
        // reason the compare stays as it is.
        commit_trim_mutation();
        // THE RELEASE IS A COMMIT, so it takes the setter's deselect and the
        // trim-mutation stop like the motion events did (both already applied by
        // then in the moved case that reaches here — each is stated at every
        // accepted mutation rather than inferred from gesture order). There is
        // nothing to restore from in any case — the drag carries no snapshot at
        // all since 2026-07-29. The release publishes no highlight of its own,
        // that coupling having retired 2026-07-30.
        playback_lifecycle.stop_playback_if_playing();
        selection.clear_selection();
    }
    // The pill returns to its rest look (arm_pending_trim_drag's damage, the
    // other edge; trim_span_dragging, app_state.h).
    viewport.invalidate_rect(top_trim_row_area(app));
    app.trim_drag = TrimDragState{};
}

// -- THE BOUND-SET CLICK: ONE ACT IN TWO FUNCTIONS ------------------------
// This block is the contract for both — trim_bound_click, which DECIDES
// (which bound, the frame, every refusal), and set_trim_bound_at_click,
// which ACTS (the stop, the write, the commit tail, the deselect).
//
// THE BARREL CLICK ON THE TRIM BAR MOVES WHICHEVER END CAP IS NEAREST THE
// PEN (architect 2026-11-10; the begin for a plain barrel click and the end
// for one held past the hold delay until then, which "doesn't work very
// well"). The click moves one bound of the resting window to THE FRAME
// UNDER THE PEN'S REAL PLACE (architect 2026-11-15, the no-grid arc, ruling
// B): the place maps to a source frame through the family's one landing
// (trim_bound_frame_at_x: authored_frame_at_x_on_basis over the DISPLAYED
// paint map on the ITEM viewport basis, the absolute walls [0, total-1]
// after), the click held into the strip's closed extent, so a click at the
// strip's right edge at the right wall sets the frame there — total-1 in
// Source view, in a mapped domain the edge's inverse, which can stand a few
// frames short (trim_bound_frame_at_x's head: nothing raises a bound to the
// source's end, architect 2026-11-19).
//
// NEAREST IS DECIDED ONCE, IN THE DECIDER, BY REAL DISTANCE ON THE BAR
// (architect 2026-11-10, on screen is as painted: the cap nearest the PEN, by
// distance on the bar; the distance real since the no-grid arc): the bound
// whose cap's exact place is the nearer to the click's real place,
// A TIE GOING TO THE BEGIN — EXCEPT RIGHT OF BOTH CAPS,
// WHERE IT IS THE END'S (such a tie stands only where the two caps share
// one place: the click is outside the trim, and the cap on its side is the
// end; the midpoint tie between two caps stays the begin's). A cap's
// place is the place the trim bar's painter publishes its grab at — the
// painter's own owner chain (displayed_trim_ms -> trim_bound_column's x,
// render.h) on the same item
// viewport basis and displayed map the click's place converts on, so the
// two are one coordinate, waveform-relative on one lattice; an off-screen
// cap keeps its own place past the lane's edge, so it is always the
// farther. In target view over a tempo change the nearer cap in frames can
// be the farther on glass, which is why frames do not decide. So a click
// INSIDE the trim pulls the nearer cap in to it, and a click OUTSIDE the
// trim moves the near cap OUTWARD to it (left of the begin the begin, right
// of the end the end) — a trim one frame wide included, which widens to
// the left by a click left of it and to the right by a click right of it.
// NO LONG PRESS MEANS ANYTHING ON THE TRIM BAR: the
// verdict reads no clock.
//
// THREE CONSUMED NO-OPS, ALL SILENT — nothing writes, nothing deselects,
// nothing stops, the caller arms no drag, no card — asked in this order.
//
// A CLICK EXACTLY ON ITS CAP IS A CONSUMED NO-OP, AND "ON ITS CAP" IS THE
// PIXEL THE CAP'S PAINTED PLACE HOLDS (architect 2026-11-10, on screen is as
// painted; in reals since the no-grid arc: the click within half a device
// px of the nearest cap's exact place). The pen is on the cap and there is
// nothing to move on glass — writing the fraction of a px between them
// would stop the play, clear the selection and trigger a render
// for a move nobody can see. THE ONE ACCEPTED CONSEQUENCE: a click on the
// cap standing at the lane's last (or first) column does not
// re-author the bound to total-1 (or 0); the maximize (Reset Trim) does,
// and so does the endcap drag where the strip edge's frame reaches the wall
// (trim_bound_frame_at_x's head).
//
// THE SAME FRAME: another place can hold the nearest bound's own resting
// frame (in Source view, and in a Target view with no map, every place at
// and past the last frame's lands on total-1 by the wall, while in a mapped
// Target view the frame under the strip's right edge can stand a few frames
// short of it — trim_bound_frame_at_x's head; at a zoom where a px is under
// a frame, a stretch of places shares one), and there is nothing to write.
//
// THE PARTNER WALL: the nearest bound's new frame would land ON or PAST its
// partner (a begin at or past the end, an end at or before the begin). WHAT
// STILL REACHES IT under the nearest verdict is ONE CASE: at a zoom where
// the two caps stand under a px apart, a midpoint tie between them (the
// begin's) whose clicked frame rounds onto the end's. There the two bounds
// are a frame apart and the click does nothing: never the other
// bound, never a second verdict. The end's arm is its mirror and has no
// producer found; both stay, because trim has no undo and the compare is
// two integers. So this route never produces a crossed or
// coincident pair and never reaches the crossed-commit reset with one (
// resetting the whole window on a mis-click is the outcome trim
// cannot afford): commit_trim_mutation below carries the shared commit
// tail every setter runs (auto_clear_crossed_trim first), kept for that
// shape and not because this route can fire it. (The begin / end clicks'
// card for this wall, 2026-08-01, went with those clicks.)
//
// READ-ONLY DOES NOT REFUSE (architect 2026-08-07): the clicks are trim, which
// is band rather than authored content, and a locked tab sets a bound
// exactly as a writable one does. History-less like every trim
// mutation; the repaint + target_render.trigger() tail mirrors the drag release.
// The act OWNS the click's playback stop — placed past every refusal
// and immediately ahead of the bound write, so the barrel press carries
// none of its own and a refused click leaves a live audition playing (the
// claim-keyed stop rule at on_button_press's top-strip paragraph, taken inside the
// gate).
//
// IT RUNS AT THE LIFT, NOT AT THE PRESS (architect 2026-08-15, the act-at-lift
// sweep finishing: "all actions should be on mouse-up / finger-up"). The
// barrel press ARMS PendingClickAct (app_state.h) and commits nothing; the
// MOTIONLESS LIFT runs the act at the PRESS COLUMN, and a THRESHOLD CROSSING
// runs it and then hands over to the endcap drag of the bound it wrote,
// through set_trim_bound_at_click_then_arm_drag below. Every gate the act
// carries is decided when it runs — TRIM HAVING NO UNDO. A lost button, the
// force-end finalizer and the touch layer's abnormal end leave the bound
// WHERE IT WAS. It DESELECTS at its tail, being a trim SETTER (architect
// 2026-07-29), and only PAST THE REFUSALS: a degenerate audio/geometry
// state, a click on its own cap's column, one on the bound's own frame and
// the partner wall set nothing and leave
// the selection exactly as it was. The deselect RESTS in every case, including when this
// click ARMS a drag (set_trim_bound_at_click_then_arm_drag): that gesture
// has no cancel either, so its caller captures nothing (the no-cancel rule
// at the drag-modal gate, input_handler.cpp).
//
// THE ACT RETURNS THE CLICK IT COMMITTED — which bound and its frame — or
// nothing where it wrote nothing: the wrapper's arm gate and the bound its
// drag takes, so every refusal suppresses the drag with no second spelling
// of the guard ladder and no second nearest verdict.

// THE DECISION HALF (hoisted 2026-08-03; the nearest verdict's one home
// since 2026-11-10): which bound the click would move and the frame it
// would write, or nullopt on every refusal — a degenerate audio/geometry
// state, a click on its cap's painted column, a click on the bound's own
// frame, and the partner wall. Nothing here
// mutates: the caller below owns the stop, the write and the tail.
std::optional<GuiInputHandler::TrimBoundClick>
GuiInputHandler::trim_bound_click(double mouse_x) const {
    if (audio.total_frames() <= 0 || audio.sample_rate() <= 0)
        return std::nullopt;
    if (current_samples_per_pixel(app, audio) <= 0.0) return std::nullopt;
    const GuiRect area = waveform_area(app);
    if (area.w <= 0) return std::nullopt;
    // THE PAINTED VIEWPORT (architect 2026-09-24, strictly as painted): the
    // column converts on the item basis, the displayed map's twin — the span
    // the trim bar under the pointer was drawn on, which the freeze holds
    // from this press to either end (pending_click is a member) — never the
    // live viewport a worker job in flight at the press has already moved.
    // Cold, the basis is the live viewport by its own contract.
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    if (basis.spp <= 0.0) return std::nullopt;
    // THE CLICK'S REAL PLACE on the strip (architect 2026-11-15, the no-grid
    // arc), held into the strip's closed extent [0, w].
    const double click_x = std::clamp(
        mouse_x - static_cast<double>(area.x), 0.0,
        static_cast<double>(area.w));
    // The landing — the frame under that place — and the walls are the
    // family's one owner, trim_bound_frame_at_x (this file's anonymous
    // namespace).
    const int64_t frame = trim_bound_frame_at_x(app, audio, click_x);
    // THE NEAREST END CAP, BY REAL DISTANCE ON THE BAR (see the header;
    // architect 2026-11-15, the no-grid arc): each resting bound's EXACT
    // PLACE as the trim bar's painter draws it — its own two owners with its
    // own arguments (GuiPaintHandler::paint_trim: displayed_trim_ms over the
    // displayed-or-live map, null in source view, then trim_bound_column's
    // `x` on the item basis's integer span and width) — in the coordinate
    // `click_x` is in. An in-view cap is held onto the lane's last column as
    // its grab zone is published (the EOF-wall clamp, the grabs'; the pill
    // is drawn from the unclamped place, under a px from it there), an
    // off-screen one keeps its raw place. A tie is the
    // begin's, except right of both caps — only where they share one place —
    // where it is the end's, the cap on the click's side.
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const std::vector<WarpFrameMapSegment>* const map_arg =
        map.empty() ? nullptr : &map;
    const auto cap_x = [&](int64_t bound_frame) -> double {
        return trim_bound_column(
            displayed_trim_ms(bound_frame, map_arg), basis.vp_start_frame,
            basis.vp_end_frame, basis.lattice_origin, basis.area_w).x;
    };
    const double begin_x  = cap_x(app.trim.begin_frame);
    const double end_x    = cap_x(app.trim.end_frame);
    const double to_begin = std::fabs(click_x - begin_x);
    const double to_end   = std::fabs(click_x - end_x);
    const bool is_begin =
        to_begin < to_end || (to_begin == to_end && click_x <= end_x);
    // ON ITS OWN CAP, AS PAINTED (see the header; architect 2026-11-10): the
    // click stands within half a device px of the chosen cap's exact place —
    // the pixel the cap's place holds — so there is nothing to move on
    // glass, whatever frame that place would author. Ahead of the two frame
    // tests, which a click a fraction of a px off the bound's frame would
    // pass.
    if ((is_begin ? to_begin : to_end) < 0.5) return std::nullopt;
    // THE SAME FRAME (see the header): another place holding the chosen
    // bound's own resting frame writes nothing either. On a one-frame source
    // (canonical full window [0, 0]) every click ends at one of these two.
    if (frame == (is_begin ? app.trim.begin_frame : app.trim.end_frame))
        return std::nullopt;
    // THE PARTNER WALL (see the header), exact integer compares against the
    // partner as it rests, in the source domain the store and `frame` live
    // in. The begin's arm is reached by one case, a midpoint tie whose
    // frame rounds onto the end's where the caps stand under a px apart; the
    // end's arm is its mirror, with no producer found, kept with it — trim
    // has no undo and the compare is two integers. Silent.
    if (is_begin ? frame >= app.trim.end_frame
                 : frame <= app.trim.begin_frame)
        return std::nullopt;
    return TrimBoundClick{is_begin, frame};
}

std::optional<GuiInputHandler::TrimBoundClick>
GuiInputHandler::set_trim_bound_at_click(double mouse_x) {
    // EVERY REFUSAL IS THE DECIDER'S (trim_bound_click above) — the
    // degenerate audio/geometry states, the click on its own cap's painted
    // column, the click on the bound's own frame and the
    // partner wall — AND
    // EACH IS SILENT: in a degenerate state there is no bar under the pointer
    // to press, a cap already standing where the click asked is its
    // own answer, and at the wall no cap can be told from the other on
    // glass. So this function is exactly the act.
    const std::optional<TrimBoundClick> decided = trim_bound_click(mouse_x);
    if (!decided) return std::nullopt;
    // The act commits from here on, so THIS is where it stops a live audition
    // (architect 2026-07-27): the trim window is about to change under it, and
    // every refusal above has already returned without stopping anything.
    // The caller (the barrel's trim-bar press) carries no stop of its own
    // for exactly that reason. Ahead of the write, like every claim's stop.
    playback_lifecycle.stop_playback_if_playing();
    if (decided->is_begin) {
        app.trim.begin_frame = decided->frame;
    } else {
        app.trim.end_frame = decided->frame;
    }
    commit_trim_mutation();
    // THE SETTER'S DESELECT (see the header). Past every refusal above, so only a
    // click that actually set a bound deselects. It publishes no highlight — the
    // trim-window region retired 2026-07-30.
    selection.clear_selection();
    return decided;
}

// The barrel trim-bar bound set's THRESHOLD CROSSING: set the nearest bound
// at the press's place (set_trim_bound_at_click above) AND arm the
// single-bound trim drag on the bound just set, so the gesture continues
// exactly like an ENDCAP press's does. NOTHING is stashed: the click-set is
// committed when made (trim is history-less) and pointer gestures have no
// cancel.
//
// ITS ONE CALLER IS THE CROSSING since 2026-08-15 (on_motion's pending-click
// branch), where it used to be the PRESS: the act-at-lift sweep moved the set to
// the lift, so this pair is what a barrel press becomes only once the pointer
// has actually travelled. THE MARKER FLAG'S CROSSING IS THE MODEL — run the
// click act, then begin the drag — the set landing at the press's place and
// the drag anchoring there. A motionless lift runs the set ALONE, through
// the act owner, and arms no drag at all.
//
// IT ARMS THE EXISTING DRAG AND BUILDS NO SECOND ONE — arm_pending_trim_drag, the
// same pending an endcap press arms, so the threshold crossing, begin_trim_drag's
// anchor capture, the live sync, the first-accepted-change deselect/stop and the
// release commit are all the drag's own rules, unchanged.
//
// THE ARM RIDES THE SET'S VERDICT, BOTH HALVES OF IT: a refused set (a
// degenerate audio/geometry state, a click on its own cap's column or on
// the bound's own frame, the partner
// wall) arms nothing,
// so a consumed no-op stays a consumed no-op with no drag left hanging on a bound
// that never moved; and THE BOUND THE DRAG TAKES IS THE BOUND THE SET MOVED
// — the nearest cap at the press's place, decided once at the down's own
// column where the set runs (architect 2026-11-10), never a second verdict
// here. A set that succeeded left an ORDERED pair by construction, so there
// is always something to drag.
void GuiInputHandler::set_trim_bound_at_click_then_arm_drag(double mouse_x,
                                                            double mouse_y) {
    const std::optional<TrimBoundClick> set = set_trim_bound_at_click(mouse_x);
    if (!set) return;
    arm_pending_trim_drag(set->is_begin, /*both=*/false, mouse_x, mouse_y);
}

// Plain trim-bar press routing — the PLAIN press's route into a trim drag,
// and one of TWO arm_pending_trim_drag callers (re-derived 2026-08-12, the
// trim surface arc's revert — the arc's extra arms, the alt bridge press and
// the ctrl deferred-set pending, lived one day, 2026-08-11..12, and are
// deleted whole): this router, and the ctrl BOUND SET's THRESHOLD
// CROSSING above (a PRESS until 2026-08-15, when the set moved to the lift),
// which arms the same single-bound pending on the bound it has just written.
// The Alt pointer gesture retired wholesale, and the waveform stem grab with it:
// a bound is grabbed ONLY by its top-strip ENDCAP — the trim lane's arrow
// button at that end of the thumb (architect 2026-10-03) — or by the bar's
// inter-cap bridge span (the bound's own mark was already the unambiguous handle),
// leaving the waveform purely
// region/playhead. Arms a PendingTrimDrag rather than beginning the drag
// outright — the pending+threshold pattern the marker flag uses: the press
// CLAIMS the endcap/bridge geometry, a motionless press-release commits nothing,
// and only once the pointer crosses grab_moved_threshold_px() does begin_trim_drag
// run and the existing single/pair drag machinery take over unchanged. A full
// ordered pair ALWAYS rests (the unset state died 2026-07-30), so the old
// pair-required gate is gone and the press is claimed purely on GEOMETRY.
// Returns true iff the press landed on trim
// geometry (an endcap-rect single-bound hit, or the bar's inter-cap bridge
// span), so the caller CLAIMS the press (no fallback); false lets the caller
// fall through. Trim bounds are transparent to every OTHER chord (the caller
// gates this to the plain, unmodified press).
//
// THIS ROUTER CARRIES NO READ-ONLY CHECK, and must not grow one — and since
// 2026-08-07 neither does anything above it: the band's sole read-only defense
// (the plain trim-bar press's own return, input_pointer.cpp) was DELETED with
// the reclassification of trim as band rather than authored content, so a locked
// tab arms the endcap and bridge drags exactly as a writable one does. The two
// internal returns this function once carried had already been deleted in
// 2026-08-02 as unreachable; nothing replaced them, and nothing should.
// The two arms:
//   CAP HIT: a press on a painted arrow button, the button's own rect and no
//     tolerance (hit_test_trim_endcap, itself y-gated to the trim bar lane),
//     arms that bound's single drag.
//   BRIDGE: else, a press on the bar's inter-cap span (point_in_trim_bridge_span,
//     app_state.h — the shared owner, which reads the painted lane's y-gate and
//     the painted bridge interval, already clipped to the painter's
//     [0, area_w)) arms the pair
//     drag. The bridge handle is the TRIM BAR lane's inter-cap span, NOT the whole
//     strip height: a top-strip press below that lane — the ruler (the region
//     former's band), then the marker lane — is not claimed here at all; each
//     of those bands has its own claim in on_button_press. Both bounds are
//     the subject (no grabbed-bound notion; the pair has no viewport clamp and,
//     like every trim gesture, never moves the playhead), so it always arms as
//     Begin structurally.
// BOTH ARMS ARE SHARED OWNERS AND NEITHER IS SPELLED HERE (hit_test_trim_endcap,
// point_in_trim_bridge_span). The drags DESELECT and STOP a live audition at
// their FIRST ACCEPTED bound change (architect 2026-07-29 / 2026-07-30 — the
// press-time stop went with the highlight-only publish it accompanied); the
// PLAYHEAD is what they never
// touch, and the deselect RESTS — the gesture has no cancel to restore it from.
//
// BOTH ARMS READ WHAT IS PAINTED (architect 2026-09-24, strictly as-painted):
// the two predicates test the trim painter's own stash (AppState::trim_bar_hit)
// — the caps and bridge the live trim pass (GuiPaintHandler::paint_trim) last
// DREW on the displayed map and viewport through its owner chain
// (displayed_trim_ms -> trim_bound_column -> trim_endcap_rect /
// trim_bridge_gap) — so a hit lands on what is drawn BY PUBLICATION, even in
// the frame between a trim write and its repaint. Nothing is derived here.
// The remaining seams are all ACCEPTED: commit-to-scanout plus human reaction
// (irreducible — input responds to the previously presented frame), and the
// COLD state (nothing painted yet, nothing grabbable). The playhead-placement
// clicks (column-based) are no longer among them: since 2026-09-24 (architect,
// strictly as-painted) they convert through item_viewport_basis, the same
// painted twin every aimed gesture here reads, so they no longer trail the
// live viewport.
bool GuiInputHandler::route_trim_bar_press(double mouse_x, double mouse_y) {
    if (audio.total_frames() <= 0) return false;
    // Single-drag hit: the endcap rect (hit_test_trim_endcap, trim-lane-gated).
    const TrimHit single = hit_test_trim_endcap(app, mouse_x, mouse_y);
    if (single != TrimHit::None) {
        arm_pending_trim_drag(single == TrimHit::Begin, /*both=*/false,
                              mouse_x, mouse_y);
        return true;
    }
    // Bridge (pair) drag. The pair has no grabbed-bound notion — both bounds are
    // the subject, so it always arms as Begin structurally (there is no
    // nearer-bound pick, and the gesture never moves the playhead).
    if (point_in_trim_bridge_span(app, mouse_x, mouse_y)) {
        arm_pending_trim_drag(/*is_begin=*/true, /*both=*/true,
                              mouse_x, mouse_y);
        return true;
    }
    return false;
}

// Arm the pending trim endcap/bridge drag from a plain TRIM-BAR press. Mirrors
// PendingMarkerPress: nothing mutates the trim store yet — begin_trim_drag runs
// only when on_motion sees the pointer cross grab_moved_threshold_px() from the
// press. is_begin names the single bound (Begin for a bridge/pair drag); both
// distinguishes the single vs the pair.
void GuiInputHandler::arm_pending_trim_drag(bool is_begin, bool both,
                                            double press_x, double press_y) {
    app.pending_trim_drag = PendingTrimDrag{};
    app.pending_trim_drag.active   = true;
    app.pending_trim_drag.is_begin = is_begin;
    app.pending_trim_drag.both     = both;
    app.pending_trim_drag.press_x  = press_x;
    app.pending_trim_drag.press_y  = press_y;
    // THE PILL TAKES ITS DRAG LOOK AT THE PRESS (architect 2026-11-17,
    // session 140, ruling 4: Arc's slider:active, `selected_fill`;
    // trim_span_dragging, app_state.h): every arm — an end's grab or the
    // body — damages the lane now, and the look stands through the crossing
    // into the drag until the gesture's end damages it again —
    // commit_trim_drag for a drag, disarm_pending_trim_drag for a press that
    // never crossed. The lane is the damage rect at both edges: it holds the
    // pill whatever the drag did to the bounds meanwhile.
    viewport.invalidate_rect(top_trim_row_area(app));
    // Five fields, no captures: the pre-gesture selection + region this used to
    // copy existed for an Esc-cancel, and pointer gestures have no cancel
    // (2026-07-29 — the rule at the drag-modal gate, input_handler.cpp). The drag
    // this may become deselects at its first published bound and keeps that
    // deselect.
}

// THE PENDING'S END WITHOUT A CROSSING — the lift, the lost button (the touch
// layer's hard end included) and the force-end finalizer, each of which
// commits nothing: disarm, and return the pill to its rest look (the other
// edge of arm_pending_trim_drag's damage). The crossing does not come here:
// it hands the arm to begin_trim_drag, and the drag look stands.
void GuiInputHandler::disarm_pending_trim_drag() {
    if (app.pending_trim_drag.active)
        viewport.invalidate_rect(top_trim_row_area(app));
    app.pending_trim_drag = PendingTrimDrag{};
}

// (THE TOUCH TRIM MOVE lived here 2026-08-11..12 — the fourth glass session's
// hold-a-beat gesture, three bodies driving this machinery from the
// platform's trim-move hooks. Deleted whole at the timer-free ruling and NOT
// revived when the disambiguation window returned the same day (the sixth
// glass ruling): on glass the trim bar is an ordinary pointer surface — a
// finger or the pen landing there is the pointer on contact (2026-09-25) and
// reaches the endcap/bridge machinery through the ordinary press path like
// every other pointer gesture; closed_questions.md carries the ruling.)
