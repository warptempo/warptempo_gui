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
// spelling: it renders untrimmed and plays to the natural end, and Shift+X is
// how the user gets back to it.
// Every gesture clamps each bound to its absolute walls — frame 0 to EOF-1,
// the same wall both marker columns hold. All authored positions (both marker
// columns and both trim bounds) share the inclusive [0, total-1] domain — the
// end bound's old exclusive-at-total wall is retired. Every wall check is a
// plain integer compare — literally the load guard's comparison.
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
//   THE CTRL / CTRL+SHIFT BOUND-SET CLICKS ARE STRICTER STILL, and were before
//     this ruling: a click that would rest ON or past its partner is a consumed
//     no-op that writes nothing (the strictly-inside guard at
//     handle_trim_set_bound). A clamp would move the bound somewhere the user
//     did not click; a refusal is the ruled answer there.
//   THE TYPED AND LOADED ROUTES (the settings editor's `:trim_*=` commits, and
//     the load) HAVE NO CLAMP AT ALL — a typed pair can still cross, and the
//     crossed/equal reset is exactly what catches it. The clamp is the DRAG's,
//     because a clamp is only meaningful where a bound is being pushed.
//   `x` KEEPS ITS DEGENERATE REFUSAL (an inverse-mapped pair coming out
//     end <= begin writes NOTHING), which is a refusal for the same
//     no-undo reason the bound-set clicks have one.
// EVERY TRIM WRITE PARKS THE PLAYHEAD AT THE NEW TRIM START (architect
// 2026-08-05, generalizing `x`'s own 2026-07-30 land to the whole family — the
// one authoritative statement of the rule; other sites state their own class
// and point here). ONE OWNER, park_playhead_at_trim_start (below), which reads
// the COMMITTED begin out of the store and lands through
// land_playhead_on_marker's placement basis with NO viewport move. THE
// MEMBERSHIP, re-derived by grepping the routes that write a trim bound:
//   * `x` (handle_trim_x) — the PRECEDENT, unchanged in effect;
//   * the CTRL / CTRL+SHIFT bound-set clicks (set_trim_bound_at_click);
//   * the ENDCAP / BRIDGE drag, AT ITS RELEASE ONLY (commit_trim_drag): the
//     motion arm deliberately parks nothing, a per-frame playhead chase being
//     a cursor fighting the gesture that is moving it;
//   * the SETTINGS EDITOR's `:trim_*=` ACTIVE-tab commits (settings_editor.cpp),
//     which move the cursor from inside a modal editor — accepted for
//     uniformity: a typed commit is a commit, and the editor's own surface is
//     the bottom strip, not the waveform. The INACTIVE-band arm parks nothing:
//     it writes a PARKED pair, not the live window;
//   * the CROSSED / COINCIDENT RESETS, with no arm of their own — a reset IS a
//     trim write, to the full window, so reading the committed begin parks the
//     playhead at frame 0 by construction;
//   * `Shift+X`, the maximizer (handle_trim_clear_both), inside its already-full
//     identity guard, so a refused maximize moves nothing.
// The first five reach it through the shared commit tail (commit_trim_mutation);
// Shift+X, which is a non-caller of that tail by design, calls the park itself.
// EVERY REFUSAL STAYS A REFUSAL: the strictly-inside consumed no-op, `x`'s
// degenerate-result refusal and Shift+X's identity guard all return above the
// park, so nothing moves. The move rides each route's EXISTING regime — the
// trim-mutation playback stop and the setter's deselect are unchanged and stay
// where they are — and adds the region clear every playhead-moving command
// takes (the clear-site rule at clear_region_highlight, input_handler.h).
//
// EVERY TRIM ROUTE IS READ-ONLY-LEGAL (architect 2026-08-07). Read-only
// protects the AUTHORED MUSICAL CONTENT — the two marker stores and the engine
// settings — and trim is BAND: it lives in ViewState beside the viewport and the
// zoom, it has no undo, and it never dirties the session, which is what the
// gate's old "authoring mutation" classification of it was missing. So `x` and
// `Shift+X` are on the keyboard allowlist, the endcap / bridge drags and the
// ctrl / ctrl+shift bound-set clicks carry no read-only refusal anywhere on
// their routes, the settings editor's typed `trim_*=` arms commit in a locked
// tab, and the trim CURSOR cues promise all of it because they read those same
// routes' own deciders. NOT ONE of the behaviors above changed with the
// admission — the degenerate-result refusal, the strictly-inside guard, the
// partner clamp, the setter's deselect, the playhead park and the
// trim-mutation playback stop are the same code taking the same decisions. The
// full ruling is at read_only_key_blocked (input_key_dispatch.cpp), the model's
// one authoritative home.
//
// The zero floor is subsumed by the walls but remains the reason the floor
// exists at all: a negative position is unrepresentable in the authored
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

namespace {

// THE PARTNER CLAMP, one owner for the single-bound drag's two applications
// (architect 2026-08-02: the trim handles "cannot move past each other"): the
// moving bound stops INCLUSIVELY on the resting partner — a begin clamps DOWN
// to the end, an end clamps UP to the begin — so the pair can hold begin == end
// mid-gesture but never a crossed shape. Value in, value out; the partner is
// read from the pair as it rests, which is the other, un-dragged field.
//
// TWO CALLERS, and the duality is the point: the MOTION arm (update_trim_drag's
// single-bound path) clamps the tracked candidate, and the COMMIT arm
// (commit_trim_drag) re-clamps AFTER the release column snap, whose
// round-tripped value can come off the partner by up to a grid span — the
// [96, 100] sliver that silently broke the ruled drag-onto-partner quick-clear
// when the two applications were two independent spellings. One expression now,
// so the invariant cannot fork again. The BRIDGE arm deliberately does not call
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

} // namespace

// Reset the pair to the canonical FULL window for the loaded source — the
// field-level act shared verbatim by handle_trim_clear_both (the Shift+X
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
// refusing). SILENT by design: the bar's endcaps visibly JUMP TO THE SONG
// EDGES, which
// is the whole signal (it used to be the old chips vanishing; with the window
// always set there is nothing to vanish into). The check is the exact integer
// compare end_frame <= begin_frame, run only at COMMIT — nothing pops
// mid-gesture, and update_trim_drag never calls this.
//
// THE `<=` HALF CARRIES THE COINCIDENT DRAG (architect 2026-08-02). Since the
// single-bound drag clamps at its partner, the shape it hands this function is
// begin == end rather than a crossed pair — which this compare has always
// caught, so the drag's route to clearing the trim (drag one handle onto the
// other) is served by the rule exactly as written, with nothing added.
// Every trim commit site — the x set-from-region, the endcap/bridge drag release,
// the bound-set click and the settings-editor `:trim_*=` commit — calls this
// after its mutation and before its invalidations, so the repaint shows the
// reset state.
//
// THE ONE-FRAME EXCEPTION: on a one-frame source (load-legal) the canonical
// full pair is [0, 0], which trips the end <= begin compare. Recognizing the
// full window FIRST makes the reset a no-op there instead of an every-commit
// self-reset loop — the same precedence the render orchestrators use.
void GuiInputHandler::auto_clear_crossed_trim() {
    if (trim_is_full_window(app.trim, audio.total_frames())) return;
    if (app.trim.end_frame <= app.trim.begin_frame) {
        reset_trim_to_full_window();
    }
}

// EVERY TRIM WRITE PARKS THE PLAYHEAD AT THE NEW TRIM START — the contract and
// the reset argument are at the declaration (input_handler.h); the per-route
// inventory is in this file's header block.
void GuiInputHandler::park_playhead_at_trim_start() {
    // The two-step placement basis and the DIRECT cursor write are
    // land_playhead_on_marker's (input_pointer.cpp, where the rule lives):
    // source frame -> active domain -> live-domain clamp, and NO viewport move.
    // THE COMMITTED BEGIN, read out of the store rather than from a caller's
    // local: the commit tail's auto_clear_crossed_trim is entitled to have
    // rewritten the pair, and reading the store is what makes a reset park at
    // the full window's own start (frame 0) with no second arm.
    app.playhead_cursor_sample = clamp_playhead_to_live_domain(
        source_frame_to_active_domain(app, audio, app.trim.begin_frame),
        app, audio);
    // THE CLOCK RIDES THE WRITE (2026-08-11, the row-8 cell): row 8's clock
    // shows this cursor whenever no scanner is active, and since the
    // timestamp left the status line the two callers' own damage (waveform +
    // status lane) no longer covers it. The call sits HERE, beside the one
    // cursor write, rather than copied per caller — this helper is the trim
    // family's single playhead writer (both trim-commit callers ride it),
    // the same damage-beside-the-write shape land_playhead_on_source_frame
    // carries. Caller inventory at Viewport::invalidate_clock_area
    // (viewport.h).
    viewport.invalidate_clock_area();
    // A PLAYHEAD-MOVING COMMAND TAKES A RESTING SPAN WITH IT (the clear-site
    // rule at clear_region_highlight, input_handler.h). For `x` this is also
    // the CONSUMPTION of the span it just read; for every other trim write it
    // is the ordinary navigation clear. The helper's own !active guard makes
    // the common no-region path free and a second call idempotent.
    clear_region_highlight(app, viewport);
}

// The shared trim commit tail — contract, the four callers and the one
// deliberate non-caller at the declaration (input_handler.h).
void GuiInputHandler::commit_trim_mutation() {
    auto_clear_crossed_trim();
    viewport.invalidate_waveform_area();
    viewport.invalidate_status_row_area();
    target_render.trigger();
    // THE PARK IS LAST, past the invalidations on purpose: both raised rects
    // are position-fixed and consumed at the next paint, so raising them ahead
    // of the cursor write is what repaints the new value (the placement `x`
    // has used since 2026-07-30, now shared by every setter).
    park_playhead_at_trim_start();
}

// Shift+X IS THE MAXIMIZER (architect 2026-07-30): it writes the FULL window
// [0, total-1] — the old "unset" outcome, now spelled as a real pair. The
// caller is handle_trim_shift_x. Trim is gesture-owned and excluded from
// undo/redo history.
//
// THE ALREADY-FULL IDENTITY GUARD replaces the old has-a-bound refusal gate: a
// Shift+X over an already-maximized window stops nothing, repaints nothing and
// triggers nothing — a silent no-op, which keeps the refusal-gated stop rule
// exactly as it was.
void GuiInputHandler::handle_trim_clear_both() {
    if (!trim_is_full_window(app.trim, audio.total_frames())) {
        // A TRIM MUTATION STOPS A LIVE AUDITION, IN BOTH VIEWS — the keyboard stop
        // rule at stop_playback_if_playing's declaration (playback_lifecycle.h).
        // Inside the identity guard, so an already-full Shift+X stops nothing
        // (refusal-gated, like every claim's stop). `Shift+X` is in the
        // trim-mutation class alongside `x` by the same 2026-07-30 ruling that
        // made it the maximizer.
        playback_lifecycle.stop_playback_if_playing();
        reset_trim_to_full_window();
        viewport.invalidate_waveform_area();
        viewport.invalidate_status_row_area();
        target_render.trigger();
        // AND THE PLAYHEAD PARKS AT THE NEW TRIM START (architect 2026-08-05):
        // the maximizer writes the full window, whose start is frame 0, so this
        // is where a Shift+X leaves the cursor. INSIDE the identity guard like
        // the stop above, so a refused maximize moves nothing. Shift+X is not a
        // SETTER — it still deselects nothing — but the park rides every trim
        // WRITE, which this is; the two rules have different memberships and
        // that difference is deliberate.
        park_playhead_at_trim_start();
    }
}

// Bare x is SET-ONLY (architect 2026-07-25, reversing the 2026-07-20 x-branch
// ruling's clear arm — the MAXIMIZE moved to the shift-exact Shift+X binding,
// handle_trim_shift_x below): x branches on the HIGHLIGHT, not the trim, with no
// context-awareness beyond that (the playhead is an OUTPUT of this gesture since
// 2026-07-30 — it lands on the committed trim start at the tail — never an input
// to it, and there are no positional rules). A live REGION (the drag-painted span, active-domain frames)
// always TRIMS to it — begin at the span's lo, end at its hi — overwriting any
// existing bounds (the new span simply replaces them; x re-trims even over an
// existing trim) — and the highlight is CONSUMED: x CLEARS the region at its tail
// (architect 2026-07-30, with the trim-window highlight sync retired — the span's
// whole job was aiming this gesture, and nothing re-publishes one; the clear is
// the SHARED tail's since 2026-08-05, where it is the ordinary
// playhead-moving-command clear and here doubles as the consumption). THE SELECTION
// IS CLEARED TOO: x is a trim SETTER, and every setter deselects (architect
// 2026-07-29).
// x GAINS A REFUSAL for the degenerate shape: a DEGENERATE RESULT (the
// inverse-mapped, wall-clamped pair coming out
// end <= begin — reachable for a narrow span around 16x bracket-legal
// compression, whose two endpoints inverse-map
// to one source frame) makes x a
// silent no-op that writes NOTHING, because the alternative is destruction:
// auto_clear_crossed_trim reads a degenerate write as crossed and silently resets
// the authored pair to the song edges, and trim has no undo (ARCHITECT-CONFIRMED
// 2026-07-29 ("correct") — the alternative was accepting silent pair loss).
// A zero-length window is not authorable, so
// there is nothing for x to set. The refusal is the FIRST thing past the clamps,
// ahead of every write, so a refused x touches neither trim, region, nor selection.
// NO region → x is a SILENT NO-OP (the maximize
// arm moved to Shift+X; x never widens). NO read-only check here, and there is
// nothing left for one to do: since 2026-08-07 the keyboard gate ADMITS bare `x`
// and Shift+X, trim being band rather than authored content (this file's header
// block), so a locked tab runs both exactly as a writable one does. The absence
// of a check here predates the admission — it was unreachable duplication while
// the gate blocked the keys — and it is now the ruling itself.
//
// Set-from-region: normalize the span at read time (endpoints rest in drag
// order), inverse-map each active-domain endpoint to a source frame through
// active_domain_to_source_frame (identity in source view, the target-view
// inverse the trim gestures already use, funnelling through snap_authored_frame
// once) — the map is monotone, so lo/hi order survives (equality is the only
// collapse it can produce) — then clamp to the shared [0, total-1] walls. A span
// collapsing to end <= begin at that point is REFUSED (see above), which is why
// this route can no longer reach auto_clear_crossed_trim at all: x writes only
// non-degenerate pairs now, so the shared commit tail's auto-clear is structural
// here — kept because every trim commit runs the same tail, not because this route
// can fire it. The refusal tests the pair x would write rather than where the
// span came from, so it covers a narrow span over
// stretched audio whose inverse-mapped endpoints land on one source frame. The
// shared trim commit tail (commit_trim_mutation —
// auto_clear_crossed_trim, the repaint/trigger, then the playhead park and the
// region clear) is the other trim commits' tail verbatim: this route's own
// 2026-07-30 land is now the rule every trim write takes.
void GuiInputHandler::handle_trim_x() {
    if (audio.total_frames() <= 0 || audio.sample_rate() <= 0) return;
    // No live region → silent no-op: x is set-only, and the maximize is Shift+X's
    // (handle_trim_shift_x). A resting trim is left exactly as it is.
    if (!app.region.active) return;

    // Live region → trim to it, overwriting any existing bounds.
    const int64_t lo_active = std::min(app.region.a_frame, app.region.b_frame);
    const int64_t hi_active = std::max(app.region.a_frame, app.region.b_frame);
    const int64_t wall = audio.total_frames() - 1;
    int64_t begin = active_domain_to_source_frame(app, audio, lo_active);
    int64_t end   = active_domain_to_source_frame(app, audio, hi_active);
    if (begin < 0)    begin = 0;
    if (begin > wall) begin = wall;
    if (end < 0)      end = 0;
    if (end > wall)   end = wall;
    // DEGENERATE RESULT → SILENT REFUSAL, on the exact pair this would write and
    // ahead of every write (see the header): end <= begin means there is no
    // authorable window here, and writing it would hand auto_clear_crossed_trim a
    // pair it resets to the song edges — the one silent, unrecoverable outcome
    // trim cannot afford.
    // Nothing is touched: trim, region and selection all rest (the deselect is
    // downstream, so the refusal is refusal-gated like every other trim claim).
    if (end <= begin) return;
    // A TRIM MUTATION STOPS A LIVE AUDITION, IN BOTH VIEWS (architect 2026-07-30 —
    // the keyboard stop rule, stated at stop_playback_if_playing's declaration,
    // playback_lifecycle.h): the window this is about to rewrite is the window a
    // live audition is playing out, so a session left running would keep reading
    // bounds the paint has already replaced. REFUSAL-GATED like every other
    // claim's stop — past the
    // no-region and degenerate-result returns above, immediately ahead of the first
    // write. In TARGET view the stop was already incidental (target_render.trigger()
    // below halts playback on every T-view mutation); doing it here makes it
    // explicit, gives the scanner its proper column teardown, and costs nothing —
    // the trigger's own stop then finds a stopped session, and this call is itself
    // guarded, so any doubling is one early return.
    playback_lifecycle.stop_playback_if_playing();
    app.trim.begin_frame = begin;
    app.trim.end_frame   = end;
    // THE PLAYHEAD LAND AND THE SPAN'S CONSUMPTION ARE BOTH THE COMMIT TAIL'S
    // NOW (architect 2026-08-05, generalizing this route's own 2026-07-30
    // rule to every trim write): commit_trim_mutation ends in
    // park_playhead_at_trim_start, which seats the cursor on the committed
    // BEGIN — so what the user hears next is the window they just made, from
    // its start — and clears the region, which for `x` alone is the CONSUMPTION
    // the architect ruled ("the scratch existed to aim this gesture"). Both
    // sit past every refusal above by construction (the no-region and
    // degenerate returns are far above), so a refused `x` moves no playhead and
    // eats no span.
    commit_trim_mutation();
    // THE SETTER'S DESELECT (architect 2026-07-29): every trim setter empties the
    // selection as it commits. Past every refusal above, like the tail's park.
    selection.clear_selection();
}

// Shift+X MAXIMIZES the trim to the full window (architect 2026-07-25 for the
// binding, re-posed 2026-07-30 under always-set — the old "unset" is now
// [0, total-1], which renders untrimmed and plays to the natural end, so the
// user-visible act is unchanged and the endcaps simply rest at the song edges).
// One-shot, history-less like every trim mutation. No read-only check of its own
// (see handle_trim_x above: read-only does not reach trim at all since
// 2026-08-07, and both keys are on the allowlist).
// Delegates WHOLE to handle_trim_clear_both — whose already-full identity guard
// makes a second Shift+X a natural silent no-op and whose tail owns the repaint
// (waveform + status lane) and the target_render trigger. IT TOUCHES NO REGION AND
// NO SELECTION: it is a trim MAXIMIZER, not a SETTER, so the setter-deselect
// rule does not reach it, and the gated region re-sync it used to carry died
// with the trim-window highlight itself (architect 2026-07-30) — a scratch span
// is the user's, not trim's to dissolve.
void GuiInputHandler::handle_trim_shift_x() {
    handle_trim_clear_both();
}

// --- Trim boundary mouse gestures ---------------------------------------

bool GuiInputHandler::trim_mouse_x_to_active_frame(int mouse_x,
                                                   int64_t& out_frame) {
    if (audio.total_frames() <= 0) return false;
    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return false;

    int rel = mouse_x - area.x;
    if (rel < 0) rel = 0;
    if (rel >= area.w) rel = area.w - 1;
    out_frame = app.viewport_start_sample +
        static_cast<int64_t>(std::nearbyint(rel * spp));
    return true;
}

bool GuiInputHandler::trim_mouse_x_to_source_frame(int mouse_x,
                                                    double& out_frame) {
    if (audio.sample_rate() <= 0) return false;

    int64_t domain_frame = 0;
    if (!trim_mouse_x_to_active_frame(mouse_x, domain_frame)) return false;

    // Target view: the cursor column is an active-domain frame; the trim
    // store is source-domain. Inverse-translate at the boundary through the
    // DISPLAYED paint basis (displayed_or_live_target_map — the SAME map the
    // trim bar/endcaps are painted with; identity in source view), in full
    // double precision like the marker drag's anchor, so the tracked bound
    // stays locked to the pointer under any map, stale or fresh, and the
    // release column-snap in commit_trim_drag owns the single
    // fractional-to-authored rounding.
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    out_frame = map_target_to_source(
        static_cast<double>(domain_frame), dmap);
    return true;
}

void GuiInputHandler::begin_trim_drag(TrimHit which, int mouse_x, bool both) {
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
    // No pre-drag playhead capture and nothing to restore: the drag moves the
    // playhead exactly once, at its RELEASE, through the commit tail's park
    // (this file's header block), and pointer gestures have no cancel.
    // Grab anchor: each arm captures exactly what its motion path consumes —
    // the pair path reads anchor_active_frame (active-domain, for the rigid
    // both-bounds delta); the single-bound path reads anchor_frame (source-
    // domain press position, motion applying the cursor's displacement from
    // here — anchor-relative like the marker drag, though that drag's anchor
    // now lives in the active domain, see DragState). A bad conversion
    // leaves the anchor at 0; harmless since the same unusable state makes
    // update_trim_drag early-return too.
    if (both) {
        int64_t af = 0;
        if (trim_mouse_x_to_active_frame(mouse_x, af))
            app.trim_drag.anchor_active_frame = af;
    } else {
        double anchor = 0.0;
        if (trim_mouse_x_to_source_frame(mouse_x, anchor))
            app.trim_drag.anchor_frame = anchor;
    }
    // The BEGIN only captures drag state — no deselect and no stop here; both
    // ride the FIRST ACCEPTED bound change at motion/commit (architect
    // 2026-07-30), and the deselect then rests (nothing restores it — pointer
    // gestures have no cancel). A press that never moves commits no bound change
    // and is a consumed nothing.
}

void GuiInputHandler::update_trim_drag(int mouse_x) {
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
        // the displayed map (monotone, so the active-domain clamp matches the
        // source-domain wall). This binds both bounds, so neither slides past
        // EOF under the rigid delta. NO PARTNER WALL IS NEEDED ON THIS ARM and
        // none is applied: the pair rides ONE shared delta, so the gap between
        // the bounds is invariant for the whole gesture and they cannot approach
        // each other at all. The partner clamp the 2026-08-02 ruling added
        // belongs to the single-bound arm below — the only one that can push a
        // handle toward its twin.
        // begin_wall_active and end_wall_active compute the identical
        // expression — shape-residue of a retired per-bound wall split, now
        // that the ceiling is shared; a future pass may collapse them to one
        // local (comments-only wave, so left as two here).
        const int64_t begin_wall_active = static_cast<int64_t>(std::nearbyint(
            map_source_to_target(
                static_cast<double>(audio.total_frames() - 1), dmap)));
        const int64_t end_wall_active = static_cast<int64_t>(std::nearbyint(
            map_source_to_target(
                static_cast<double>(audio.total_frames() - 1), dmap)));
        if (ob + df < 0)                 df = -ob;
        if (oe + df < 0)                 df = -oe;
        if (ob + df > begin_wall_active) df = begin_wall_active - ob;
        if (oe + df > end_wall_active)   df = end_wall_active - oe;
        // snap_authored_frame lands each result on a whole int64 frame (the
        // single fractional-to-authored route). These are mid-gesture tracking
        // values; the release in commit_trim_drag snaps each moved bound to its
        // painted column's authored time.
        int64_t nb = snap_authored_frame(
            map_target_to_source(static_cast<double>(ob + df), dmap));
        int64_t ne = snap_authored_frame(
            map_target_to_source(static_cast<double>(oe + df), dmap));
        if (nb < 0) nb = 0;
        if (ne < 0) ne = 0;
        if (app.trim.begin_frame != nb || app.trim.end_frame != ne) {
            app.trim.begin_frame = nb;
            app.trim.end_frame   = ne;
            app.trim_drag.moved    = true;
            // MID-GESTURE THE PLAYHEAD DOES NOT MOVE: the park at the new
            // trim start is the RELEASE's, run once by the commit tail, so a
            // cursor chase never fights the drag that is moving the bounds.
            // Motion updates the bounds and repaints; the playhead waits.
            viewport.invalidate_waveform_area();
            viewport.invalidate_status_row_area();
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

    // Anchor-relative motion (single-bound path only): the dragged bound moves
    // by the cursor's displacement from the grab point, not to the absolute
    // cursor column. cursor_frame is converted identically to the begin-drag
    // anchor, so the bound stays the same distance under the cursor for the
    // whole drag. The pair path above works in the active domain and never
    // consumes this source-domain conversion, so it is computed only here.
    double cursor_frame = 0.0;
    if (!trim_mouse_x_to_source_frame(mouse_x, cursor_frame)) return;
    const double delta_frames = cursor_frame - app.trim_drag.anchor_frame;

    // Single-bound: pre-drag frame plus the anchor-relative delta. The
    // mouse-derived delta rounds once into the integer domain through
    // snap_authored_frame — the value lands in an authored store field
    // below, so the conversion goes through the single double-to-authored
    // chokepoint like every other authored write; everything after is
    // int64 arithmetic.
    int64_t src_frame = app.trim_drag.orig_frame +
        snap_authored_frame(delta_frames);

    // Viewport clamp: keep the grabbed bound within the visible strip (pixel 0
    // through the last fully-visible pixel) so the drag can't push it
    // offscreen, where its precise location would be hidden. The cursor column
    // is already viewport-bound, but a grab a few pixels off the endcap can
    // trail the bound past the edge; this makes the bound itself exact. The
    // grab can only begin on a visible bound (hit_test_trim_endcap tests the
    // endcap painted at a visible column), so this is a live tracking clamp, not a
    // correction for an offscreen grab. The bounds are active-domain while
    // src_frame is source, so inverse-translate the edges through the DISPLAYED
    // paint basis (the same map the tracked bound rode above; monotonic, so the
    // source clamp matches the active-pixel one).
    const auto vb = viewport_marker_bounds(app, audio);
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    const int64_t vp_lo = snap_authored_frame(
        map_target_to_source(static_cast<double>(vb.first), dmap));
    const int64_t vp_hi = snap_authored_frame(
        map_target_to_source(static_cast<double>(vb.second), dmap));
    if (src_frame < vp_lo) src_frame = vp_lo;
    if (src_frame > vp_hi) src_frame = vp_hi;

    // Structural wall, applied AFTER the viewport clamp so the wall wins
    // where both bind (matching the marker-drag model where structural walls
    // compose with the viewport gate): both bounds clamp to frame EOF-1, the
    // unified authored domain. The floor 0 is already held by the
    // viewport clamp (the visible strip starts at or after frame 0), so the 0.0
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
    // Mid-gesture tracking value: int64 throughout (the store cannot hold a
    // fractional frame), but pointer-derived, not column-canonical — the
    // release in commit_trim_drag snaps a moved bound to its painted
    // column's authored time, superseding this value.
    const int64_t new_frame = src_frame;
    int64_t& field = app.trim_drag.is_begin ? app.trim.begin_frame
                                            : app.trim.end_frame;
    if (field != new_frame) {
        field = new_frame;
        app.trim_drag.moved = true;
        // MID-GESTURE THE PLAYHEAD DOES NOT MOVE (the pair arm's twin above;
        // a recorded difference from the marker drag, which tracks its grabbed
        // marker): the park at the new trim start is the RELEASE's. Motion
        // updates the bound and repaints; the playhead waits.
        viewport.invalidate_waveform_area();
        viewport.invalidate_status_row_area();
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
        // Release-time column snap, the marker commit_drag shape: each bound
        // the drag actually MOVED snaps to the time of the pixel column it is
        // painted at — the stem painter's own math via
        // painted_column_of_source_frame / authored_frame_at_column (which
        // funnels through snap_authored_frame) — so the stored value is the
        // whole frame of the shown column: stored equals shown, in both views
        // at all zooms. An untouched bound keeps its stored value bit-exact
        // (commit_drag's moved-only rule); on a rigid two-bound drag each
        // moved bound anchors to its OWN painted column independently, so the
        // pair's span may deform by up to one frame at release — an accepted
        // release-snap consequence (the constant-gap phrasing at TrimDragState
        // describes the mid-gesture active-domain motion).
        // The map is the DISPLAYED paint basis (displayed_or_live_target_map —
        // identity in source view), the SAME map the live trim pass
        // (paint_trim) draws the on-screen
        // stems through (falling back to the live cache only when cold), so the release
        // column-snaps against WHAT IS PAINTED and stored equals shown even inside
        // an async waveform-rebuild window where the displayed map lags the live
        // cache — the residual case now that the target-view warp_frame_map edits
        // all re-warp synchronously (the full inventory lives at
        // Viewport::kick_waveform_sync; warp placement edits author in source view
        // under the home-view binding, where the displayed map is identity): a
        // worker job dispatched by a
        // viewport change and still in flight across the grab, carrying the
        // then-current map. That is the same displayed basis route_trim_bar_press's
        // hit test and the drag mechanics above all read. The absolute walls
        // — both bounds 0..EOF-1, plain integer compares —
        // re-apply AFTER the snap so the walls win over the pixel grid and a
        // wall-clamped release rests exactly on its wall. Degenerate paint
        // geometry (no strip width / zoom, unloaded audio) skips the snap and
        // keeps the tracked value: trim has no undo, so routing a bound
        // through the helpers' 0-fallback would be unrecoverable.
        const int sr = audio.sample_rate();
        if (sr > 0 && audio.total_frames() > 0 &&
            current_samples_per_pixel(app, audio) > 0.0) {
            const std::vector<WarpFrameMapSegment>& map =
                displayed_or_live_target_map(app, audio);
            const auto snap_moved_bound = [&](int64_t& field, int64_t orig,
                                              int64_t wall) {
                if (field == orig) return;  // untouched: bit-exact, no snap
                const int c = painted_column_of_source_frame(
                    app, audio, static_cast<double>(field), map);
                int64_t v = authored_frame_at_column(app, audio, c, map);
                if (v < 0)    v = 0;
                if (v > wall) v = wall;
                field = v;
            };
            snap_moved_bound(app.trim.begin_frame,
                             app.trim_drag.orig_begin_frame,
                             audio.total_frames() - 1);
            snap_moved_bound(app.trim.end_frame,
                             app.trim_drag.orig_end_frame,
                             audio.total_frames() - 1);
            // THE PARTNER CLAMP RUNS AGAIN HERE, AFTER THE SNAP — the same
            // clamp_trim_bound_at_partner owner the motion arm applies — and it
            // has to: the snap above is what defeats the mid-drag clamp.
            // update_trim_drag stops the moving bound exactly ON its partner,
            // but the partner is an ARBITRARY resting frame that need not sit
            // on the painted authoring grid — so round-tripping the coincident
            // value through painted_column_of_source_frame /
            // authored_frame_at_column lands it on its column's own frame,
            // which is at or BEFORE where it was, and the two bounds come apart
            // again by up to one grid span. That silently broke the ruled
            // drag-onto-partner quick-clear: with an end resting at 100 on a
            // 16-frame grid, a begin dragged onto it committed as [96, 100] — a
            // sliver auto_clear_crossed_trim's `end <= begin` compare does not
            // recognise — instead of the full window the architect ruled ("if
            // they are set coincident, make trim 0 to EOF"). Re-clamping
            // restores the equality the compare needs.
            //
            // BOTH ARMS, not just the visibly broken one. Only the begin arm
            // showed the sliver: the snap always pulls a value DOWN, so an END
            // dragged onto its begin overshot to end < begin and was rescued by
            // the same crossed compare, clearing by accident rather than by the
            // stated rule. That is the clamp's own invariant ("the handle can
            // land exactly ON its partner and never beyond it") being violated
            // and then covered up, so both arms live in the one owner and
            // neither depends on the rescue.
            //
            // AFTER THE ABSOLUTE WALL IS SAFE, the same order-independence
            // argument the mid-drag clamp records: the partner is itself
            // wall-held, so clamping to it only ever pulls the bound INWARD and
            // can never push it back outside [0, EOF-1] that snap_moved_bound
            // just enforced.
            //
            // SINGLE-BOUND ARM ONLY. The bridge drag moves both bounds by one
            // shared delta and has no partner wall by ruling (the gap is
            // invariant mid-gesture, so there is nothing to cross); adding a
            // clamp there would invent a rule for a gesture that cannot need
            // one. Its own release-snap deformation is the accepted ±1 frame the
            // block comment above records.
            if (!app.trim_drag.both) {
                int64_t& moved = app.trim_drag.is_begin ? app.trim.begin_frame
                                                        : app.trim.end_frame;
                moved = clamp_trim_bound_at_partner(app.trim_drag.is_begin,
                                                    moved, app.trim);
            }
            // The snap is the BOUNDS' alone — there is no playhead pin or
            // sync here. The park at the committed trim start comes after,
            // from the shared commit tail below, so it reads snapped values.
        }
        // The release is the commit: a bound released on or across its
        // partner RESETS the pair to the song edges (crossed/equal cannot rest;
        // ruling at auto_clear_crossed_trim). THIS IS ALSO THE GESTURE'S ONE
        // PLAYHEAD MOVE: the tail below parks the cursor at the committed trim
        // start (the rule and its membership are in this file's header), and a
        // reset lands it at frame 0 because that is the full window's start.
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
        // all since 2026-07-29. A resting region goes with the playhead park in
        // the tail above, the clear every playhead-moving command takes; the
        // release still publishes no highlight of its own, that coupling having
        // retired 2026-07-30.
        playback_lifecycle.stop_playback_if_playing();
        selection.clear_selection();
    }
    app.trim_drag = TrimDragState{};
}

// -- THE BOUND-SET CLICK: ONE ACT IN TWO FUNCTIONS ------------------------
// This block is the contract for both — trim_bound_click_frame, which DECIDES
// (every refusal, and the frame a click would write), and set_trim_bound_at_click,
// which ACTS (the stop, the write, the commit tail, the deselect). They were one
// function until the pointer cursor needed the verdict without the act.
//
// Set ONE trim bound (begin or end) at the clicked column — REINSTATED architect
// 2026-08-01 with a NEW strict refusal, after a one-day retirement (the form is
// 853c2c4's, restored onto the redesigned TRIM BAR rather than the chip row it
// grew up on). The click moves one bound of the resting window: the column maps
// to a source frame through authored_frame_at_column over the DISPLAYED paint map
// — the same release-snap basis commit_trim_drag uses (its snap_moved_bound goes
// source_frame -> painted_column -> authored_frame; a click carries the column
// directly). The absolute walls [0, total-1] apply after the snap.
// ADJUST-ONLY is a statement about what the click DOES — it moves one bound of the
// window that always rests — rather than a condition it tests, the pair gate
// having died with the unset state (2026-07-30).
//
// THE STRICTLY-INSIDE GUARD (architect 2026-08-01, and the ONE thing that is not
// a restoration): a clicked value that would rest EQUAL TO or PAST its partner —
// a new begin >= the resting end, a new end <= the resting begin — is a CONSUMED
// NO-OP. Nothing writes, nothing deselects, nothing stops, and the caller arms no
// drag. STRICTLY INSIDE IS THE ONLY ACCEPTED SET, so these two routes can never
// produce a crossed pair and never reach the crossed-commit reset with one:
// commit_trim_mutation below carries the shared commit tail every setter runs
// (auto_clear_crossed_trim first), kept
// for that shape and not because this route can fire it (exactly `x`'s
// arrangement). The reset rule itself is UNTOUCHED everywhere else — the drag
// release and the settings commit still reset a crossed pair to the song edges.
// The refusal is where it is — past the clamps, ahead of every write — because
// silently resetting the whole window on a mis-click is the outcome trim cannot
// afford (trim has no undo).
//
// READ-ONLY DOES NOT REFUSE (architect 2026-08-07): the clicks are trim, which
// is band rather than authored content, so this route's first gate — the read-
// only bit — was deleted with the reclassification and a locked tab sets a bound
// exactly as a writable one does. History-less like every trim
// mutation; the repaint + target_render.trigger() tail mirrors the drag release.
// This function OWNS the press's playback stop — placed past every refusal above
// and immediately ahead of the bound write, so the ctrl / ctrl+shift press carries
// none of its own and a refused click leaves a live audition playing (the
// claim-keyed stop rule at on_button_press's top-strip paragraph, taken inside the
// gate). It DESELECTS at its tail, being a trim SETTER (architect 2026-07-29); it
// publishes no region, the trim-window highlight having retired 2026-07-30.
// Both bound-set clicks are this ONE function, so both deselect, and both deselect
// only PAST THE REFUSALS: a degenerate audio/geometry state and a
// non-strictly-inside value both set nothing and leave the selection exactly as it
// was. The deselect RESTS in every case, including when this click ARMS a drag
// (set_trim_bound_at_click_then_arm_drag): that gesture has no cancel either, so
// its caller captures nothing (the no-cancel rule at the drag-modal gate,
// input_handler.cpp).
//
// RETURNS whether a bound was actually written — the wrapper's arm gate, so every
// refusal above suppresses the drag with no second spelling of the guard ladder.
// is_begin picks the bound: the ctrl trim-bar click sets begin, ctrl+shift sets
// end.

// THE DECISION HALF, hoisted whole (architect 2026-08-03) so the pointer CURSOR
// can ask what this click would do without a second copy of the derivation: the
// cue over the trim bar with ctrl held names the BEGIN bound only where the click
// would actually set it, and falls to the Arrow on every refusal below — a
// degenerate audio/geometry state, and above all the
// strictly-inside guard, whose whole purpose is that a click landing on or past
// its partner does nothing. Returns the frame the click WOULD write, or nullopt.
// Nothing here mutates: the caller below owns the stop, the write and the tail.
//
// THE READ-ONLY BIT WAS THIS FUNCTION'S FIRST GATE UNTIL 2026-08-07 and is now
// deleted, not moved: trim is band, so the two ctrl cues promise the set in a
// locked tab because the set actually happens there (the file header's ruling).
std::optional<int64_t> GuiInputHandler::trim_bound_click_frame(
    bool is_begin, int mouse_x) const {
    if (audio.total_frames() <= 0 || audio.sample_rate() <= 0)
        return std::nullopt;
    if (current_samples_per_pixel(app, audio) <= 0.0) return std::nullopt;
    const GuiRect area = waveform_area(app);
    if (area.w <= 0) return std::nullopt;
    int col = mouse_x - area.x;
    if (col < 0)         col = 0;
    if (col >= area.w)   col = area.w - 1;
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    int64_t frame = authored_frame_at_column(app, audio, col, dmap);
    const int64_t wall = audio.total_frames() - 1;
    if (frame < 0)    frame = 0;
    if (frame > wall) frame = wall;
    // THE STRICTLY-INSIDE GUARD (see the header). Exact integer compares against
    // the partner as it rests, in the SOURCE domain both the store and `frame`
    // already live in — no mapping, no tolerance. On a one-frame source (canonical
    // full window [0, 0]) both arms refuse every click, correctly: there is no
    // strictly-inside value to author there.
    if (is_begin) { if (frame >= app.trim.end_frame)   return std::nullopt; }
    else          { if (frame <= app.trim.begin_frame) return std::nullopt; }
    return frame;
}

bool GuiInputHandler::set_trim_bound_at_click(bool is_begin, int mouse_x) {
    // EVERY REFUSAL IS THE DECIDER'S (trim_bound_click_frame above) — the
    // degenerate audio/geometry states and the strictly-inside guard — so
    // this function is exactly the act, and the cursor cue that asks the same
    // question cannot answer it differently.
    const std::optional<int64_t> decided = trim_bound_click_frame(is_begin,
                                                                 mouse_x);
    if (!decided) return false;
    const int64_t frame = *decided;
    // The act commits from here on, so THIS is where it stops a live audition
    // (architect 2026-07-27): the trim window is about to change under it, and
    // every refusal above — a degenerate audio/geometry state, a
    // non-strictly-inside value — has already returned without stopping anything.
    // The caller (the ctrl / ctrl+shift trim-bar press) carries no stop of its own
    // for exactly that reason. Ahead of the write, like every claim's stop.
    playback_lifecycle.stop_playback_if_playing();
    if (is_begin) {
        app.trim.begin_frame = frame;
    } else {
        app.trim.end_frame = frame;
    }
    commit_trim_mutation();
    // THE SETTER'S DESELECT (see the header). Past every refusal above, so only a
    // click that actually set a bound deselects. It publishes no highlight — the
    // trim-window region retired 2026-07-30.
    selection.clear_selection();
    return true;
}

// The ctrl / ctrl+shift trim-bar bound-set press: set the bound at the click
// (set_trim_bound_at_click above) AND arm the single-bound trim drag on the bound
// just set, so motion past the threshold drags it live exactly like an ENDCAP
// press does, while a motionless release rests the click-set. NOTHING is stashed:
// the click-set is committed when made (trim is history-less) and pointer gestures
// have no cancel.
//
// IT ARMS THE EXISTING DRAG AND BUILDS NO SECOND ONE — arm_pending_trim_drag, the
// same pending an endcap press arms, so the threshold crossing, begin_trim_drag's
// anchor capture, the live sync, the first-accepted-change deselect/stop and the
// release commit are all the drag's own rules, unchanged.
//
// THE ARM RIDES THE SET'S VERDICT: a refused set (a degenerate
// audio/geometry state, or a value not strictly inside its partner) arms nothing,
// so a consumed no-op stays a consumed no-op with no drag left hanging on a bound
// that never moved. The old pair-survival checks that used to gate the arm died
// with the unset state (2026-07-30) and are not needed under the new guard either:
// a set that succeeded left an ORDERED pair by construction, so there is always
// something to drag. is_begin picks the bound (ctrl=begin, ctrl+shift=end).
void GuiInputHandler::set_trim_bound_at_click_then_arm_drag(bool is_begin,
                                                            int mouse_x,
                                                            int mouse_y) {
    if (!set_trim_bound_at_click(is_begin, mouse_x)) return;
    arm_pending_trim_drag(is_begin, /*both=*/false, mouse_x, mouse_y);
}

// Plain trim-bar press routing — the PLAIN press's route into a trim drag,
// and one of TWO (re-derived by grepping arm_pending_trim_drag's callers,
// 2026-08-01): this router, and the ctrl / ctrl+shift BOUND-SET press above,
// which arms the same single-bound pending on the bound it has just written.
// The Alt pointer gesture retired wholesale, and the waveform stem grab with it:
// a bound is grabbed ONLY by its top-strip ENDCAP or by the bar's inter-cap
// bridge span (the bound's own mark was already the unambiguous handle),
// leaving the waveform purely
// region/playhead. Arms a PendingTrimDrag rather than beginning the drag
// outright — the pending+threshold pattern the marker flag uses: the press
// CLAIMS the endcap/bridge geometry, a motionless press-release commits nothing,
// and only once the pointer crosses kDragMovedThresholdPx does begin_trim_drag
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
//   CAP HIT: an endcap-rect hit (hit_test_trim_endcap, itself y-gated to the trim
//     bar lane) arms that bound's single drag.
//   BRIDGE: else, a press on the bar's inter-cap span (point_in_trim_bridge_span,
//     app_state.h — the shared owner, which carries the trim-lane y-gate, the
//     trim_bridge_gap interval and the painter's [0, area_w) clip) arms the pair
//     drag. The bridge handle is the TRIM BAR lane's inter-cap span, NOT the whole
//     strip height: a top-strip press below that lane — the ruler, then the
//     marker lane — is not claimed and falls through to the caller's ruler /
//     flag handling. Both bounds are
//     the subject (no grabbed-bound notion; the pair has no viewport clamp and,
//     like every trim gesture, moves the playhead only at its release, through
//     the commit tail's park), so it always arms as
//     Begin structurally.
// BOTH ARMS ARE SHARED OWNERS AND NEITHER IS SPELLED HERE, which is what lets the
// pointer CURSOR promise exactly what this router claims: the zone map calls the
// same two predicates (pointer_cursor_kind, input_pointer.cpp), so a point that
// arms nothing shows the Arrow by construction rather than by proximity.
// The drags DESELECT and STOP a live audition at
// their FIRST ACCEPTED bound change (architect 2026-07-29 / 2026-07-30 — the
// press-time stop went with the highlight-only publish it accompanied); the
// PLAYHEAD is what they never
// touch, and the deselect RESTS — the gesture has no cancel to restore it from.
//
// BOTH ARMS DECIDE ON THE DISPLAYED BASIS — the displayed MAP
// (displayed_or_live_target_map) AND the displayed VIEWPORT
// (item_viewport_basis), the EXACT basis and owner chain the live trim
// pass (GuiPaintHandler::paint_trim) paints the bar and its endcaps from every frame
// (displayed_trim_ms -> trim_bound_column -> trim_bridge_gap), so a hit lands
// on what is drawn BY SHARED OWNERS: paint and hit read the same functions on
// the same basis (the
// event-sync ruling at that selector). Each predicate carries that basis itself,
// which is why nothing is derived here. The remaining seams are all
// ACCEPTED:
// commit-to-scanout plus human reaction (irreducible — input responds to the
// previously presented frame), the COLD-STATE fallback (first paint, a view
// toggle, or just after load, live map until the first committed target frame),
// and the playhead-placement clicks (column-based, out of scope by ruling — a
// far subtler seam).
bool GuiInputHandler::route_trim_bar_press(int mouse_x, int mouse_y) {
    if (audio.total_frames() <= 0) return false;
    // Single-drag hit: the endcap rect (hit_test_trim_endcap, trim-lane-gated).
    const TrimHit single = hit_test_trim_endcap(app, audio, mouse_x, mouse_y);
    if (single != TrimHit::None) {
        arm_pending_trim_drag(single == TrimHit::Begin, /*both=*/false,
                              mouse_x, mouse_y);
        return true;
    }
    // Bridge (pair) drag. The pair has no grabbed-bound notion — both bounds are
    // the subject, so it always arms as Begin structurally (there is no
    // nearer-bound pick, and the gesture moves the playhead only at its
    // release, through the commit tail's park).
    if (point_in_trim_bridge_span(app, audio, mouse_x, mouse_y)) {
        arm_pending_trim_drag(/*is_begin=*/true, /*both=*/true,
                              mouse_x, mouse_y);
        return true;
    }
    return false;
}

// Arm the pending trim endcap/bridge drag from a plain trim-bar press. Mirrors
// PendingMarkerDrag: nothing mutates the trim store yet — begin_trim_drag runs
// only when on_motion sees the pointer cross kDragMovedThresholdPx from the
// press. is_begin names the single bound (Begin for a bridge/pair drag); both
// distinguishes the single vs the pair.
void GuiInputHandler::arm_pending_trim_drag(bool is_begin, bool both,
                                            int press_x, int press_y) {
    app.pending_trim_drag = PendingTrimDrag{};
    app.pending_trim_drag.active   = true;
    app.pending_trim_drag.is_begin = is_begin;
    app.pending_trim_drag.both     = both;
    app.pending_trim_drag.press_x  = press_x;
    app.pending_trim_drag.press_y  = press_y;
    // Five fields, no captures: the pre-gesture selection + region this used to
    // copy existed for an Esc-cancel, and pointer gestures have no cancel
    // (2026-07-29 — the rule at the drag-modal gate, input_handler.cpp). The drag
    // this may become deselects at its first published bound and keeps that
    // deselect.
}
