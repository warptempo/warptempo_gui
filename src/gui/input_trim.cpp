#include "input_handler.h"

#include "gui_display_context.h"
#include "render.h"
#include "warp_frame_map_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

// Trim gestures (architect-ruled hardfail model; the full ruling sits at the
// TrimState store in app_state.h): begin and end are authored named roles, and
// THE WINDOW IS ALWAYS SET — no unset state, no lone bound (architect
// 2026-07-30). The FULL window [0, total-1] is the old unset state under a new
// spelling: it renders untrimmed and plays to the natural end, and Shift+X is
// how the user gets back to it.
// Every gesture clamps each bound to its absolute walls — frame 0 to EOF-1,
// the same wall both marker columns hold. All authored positions (both marker
// columns and both trim bounds) share the inclusive [0, total-1] domain — the
// end bound's old exclusive-at-total wall is retired. There are no partner
// walls: a bound crosses its partner freely during any gesture — but
// crossed/equal can no longer REST: every trim commit runs
// auto_clear_crossed_trim (below), so a commit landing on or across the
// partner RESETS the pair to the full window. Every wall check is a plain
// integer compare — literally the load guard's comparison.
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
// refusing). SILENT by design: the chips visibly JUMP TO THE SONG EDGES, which
// is the whole signal (it used to be the chips vanishing; with the window
// always set there is nothing to vanish into). The check is the exact integer
// compare end_frame <= begin_frame, run only at COMMIT — mid-gesture crossing
// stays free (nothing pops mid-gesture; update_trim_drag never calls this).
// Every trim commit site — the x set-from-region, the chip/bridge drag release,
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
        viewport.invalidate_timestamp_area();
        target_render.trigger();
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
// whole job was aiming this gesture, and nothing re-publishes one). THE SELECTION
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
// arm moved to Shift+X; x never widens). NO read-only check here: this pair is
// keyboard-only (the sole callers are the bare-x / Shift+X dispatch arms), and
// the keyboard gate — the ONE read-only guard on that path — leaves x off its
// allowlist, so a locked tab never reaches either function.
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
// shared trim commit tail (auto_clear_crossed_trim
// then the repaint/trigger) mirrors the other trim
// commits; the playhead is
// untouched (trim gestures never move it).
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
    auto_clear_crossed_trim();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
    // THE SETTER'S DESELECT (architect 2026-07-29): every trim setter empties the
    // selection as it commits. Then CONSUME THE SPAN (architect 2026-07-30): the
    // scratch region existed to aim this gesture and its job is done — the chips
    // and the bridge bar show the result from here on, and nothing re-publishes a
    // highlight. ONE clear, at the tail, past every refusal above.
    selection.clear_selection();
    clear_region_highlight(app, viewport);
    // AND THE PLAYHEAD LANDS ON THE TRIM START (architect 2026-07-30, live-test
    // refinement — the resting half of the drag's carry: "so on trim X move
    // playhead to trim start"). The drag walked the cursor along the span's
    // moving end; the commit seats it at the window's BEGIN, so what the user
    // hears next is the window they just made, from its start.
    // THE COMMITTED BEGIN, read back out of the store rather than reused from the
    // local: `begin` above is pre-tail, and the shared commit tail
    // (auto_clear_crossed_trim) is entitled to rewrite the pair. It cannot fire
    // here — the degenerate refusal upstream guarantees end > begin — so this is
    // the same value either way, and reading the store is what keeps that true if
    // the tail ever gains an arm.
    // The two-step placement basis and the DIRECT cursor write are
    // land_playhead_on_marker's (input_pointer.cpp, where the rule lives): source
    // frame -> active domain -> live-domain clamp, and NO viewport move, so a
    // trim start offscreen leaves the view exactly where the user left it.
    // PAST EVERY REFUSAL by construction (the no-region and degenerate returns
    // are far above), so a refused `x` moves no playhead. The waveform +
    // timestamp invalidates raised above cover this write: both are
    // position-fixed rects consumed at the next paint, so raising them ahead of
    // the write repaints the new value.
    app.playhead_cursor_sample = clamp_playhead_to_live_domain(
        source_frame_to_active_domain(app, audio, app.trim.begin_frame),
        app, audio);
}

// Shift+X MAXIMIZES the trim to the full window (architect 2026-07-25 for the
// binding, re-posed 2026-07-30 under always-set — the old "unset" is now
// [0, total-1], which renders untrimmed and plays to the natural end, so the
// user-visible act is unchanged and the chips simply rest at the song edges).
// One-shot, history-less like every trim mutation. No read-only check of its own
// (see handle_trim_x above: the keyboard gate owns that decision for both).
// Delegates WHOLE to handle_trim_clear_both — whose already-full identity guard
// makes a second Shift+X a natural silent no-op and whose tail owns the repaint
// (waveform + timestamp) and the target_render trigger. IT TOUCHES NO REGION AND
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
    // trim chips/stems are painted with; identity in source view), in full
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
    // No pre-drag playhead capture: trim drags never touch the playhead.
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
        // the SAME map the trim chips/stems are painted with (identity in source
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
        // single grab still requires a visible stem/chip at press, and the
        // single-bound path below keeps its viewport clamp), not blind MOTION
        // of a rigid pair the user visibly holds by its middle. The pair rides
        // the rigid delta bounded ONLY by the absolute walls below.
        //
        // Wall the rigid delta so BOTH bounds respect their absolute walls:
        // floor 0 on each and a shared ceiling at frame EOF-1 — mapped through
        // the displayed map (monotone, so the active-domain clamp matches the
        // source-domain wall). This binds both bounds, so neither slides past
        // EOF under the rigid delta. Crossing stays free (no partner wall).
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
            // Trim drags never move the playhead — the gesture is
            // playhead-independent. Motion updates the bounds and
            // repaints; the playhead stays where it is.
            viewport.invalidate_waveform_area();
            viewport.invalidate_timestamp_area();
            // THE DRAG IS A SETTER, so it DESELECTS (architect 2026-07-29) —
            // past the moved-bounds gate above, so a drag event that changes
            // nothing deselects nothing, and idempotent across the gesture's
            // later events. The publish half is gone with the trim-window
            // highlight (architect 2026-07-30): nothing is written to the region
            // here at all.
            // THE STOP LIVES HERE NOW (architect 2026-07-30): the chip/bridge
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
    // is already viewport-bound, but a grab a few pixels off the chip can
    // trail the bound past the edge; this makes the bound itself exact. The
    // grab can only begin on a visible bound (hit_test_trim_chip tests the
    // chip painted at a visible column), so this is a live tracking clamp, not a
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
    // unified authored domain. No partner wall — the bound crosses its partner
    // freely and rests wherever released. The floor 0 is already held by the
    // viewport clamp (the visible strip starts at or after frame 0), so the 0.0
    // format-representability floor holds by construction here.
    const int64_t wall_hi = audio.total_frames() - 1;
    if (src_frame > wall_hi) src_frame = wall_hi;
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
        // Trim drags never move the playhead — the gesture is playhead-
        // independent (a recorded difference from the marker drag, which
        // tracks its grabbed marker). Motion updates the bound and
        // repaints; the playhead stays where it is.
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
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
        // then-current map. That is the same displayed basis route_trim_chip_press's
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
            // Trim drags never move the playhead, so the
            // commit snaps the bounds only — there is no playhead pin/sync here.
        }
        // The release is the commit: a bound released on or across its
        // partner RESETS the pair to the song edges (crossed/equal cannot rest;
        // ruling at auto_clear_crossed_trim). Mid-drag crossing stayed free; the
        // playhead was never touched by the gesture.
        auto_clear_crossed_trim();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
        // THE RELEASE IS A COMMIT, so it takes the setter's deselect and the
        // trim-mutation stop like the motion events did (both already applied by
        // then in the moved case that reaches here — each is stated at every
        // accepted mutation rather than inferred from gesture order). There is
        // nothing to restore from in any case — the drag carries no snapshot at
        // all since 2026-07-29. The region is untouched: the release publishes no
        // highlight, that coupling having retired 2026-07-30.
        playback_lifecycle.stop_playback_if_playing();
        selection.clear_selection();
    }
    app.trim_drag = TrimDragState{};
}

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
// auto_clear_crossed_trim below is the shared commit tail every setter runs, kept
// for that shape and not because this route can fire it (exactly `x`'s
// arrangement). The reset rule itself is UNTOUCHED everywhere else — the drag
// release and the settings commit still reset a crossed pair to the song edges.
// The refusal is where it is — past the clamps, ahead of every write — because
// silently resetting the whole window on a mis-click is the outcome trim cannot
// afford (trim has no undo).
//
// Read-only refuses silently (trim authoring). History-less like every trim
// mutation; the repaint + target_render.trigger() tail mirrors the drag release.
// This function OWNS the press's playback stop — placed past every refusal above
// and immediately ahead of the bound write, so the ctrl / ctrl+shift press carries
// none of its own and a refused click leaves a live audition playing (the
// claim-keyed stop rule at on_button_press's top-strip paragraph, taken inside the
// gate). It DESELECTS at its tail, being a trim SETTER (architect 2026-07-29); it
// publishes no region, the trim-window highlight having retired 2026-07-30.
// Both bound-set clicks are this ONE function, so both deselect, and both deselect
// only PAST THE REFUSALS: a read-only tab, a degenerate audio/geometry state and a
// non-strictly-inside value all set nothing and leave the selection exactly as it
// was. The deselect RESTS in every case, including when this click ARMS a drag
// (set_trim_bound_at_click_then_arm_drag): that gesture has no cancel either, so
// its caller captures nothing (the no-cancel rule at the drag-modal gate,
// input_handler.cpp).
//
// RETURNS whether a bound was actually written — the wrapper's arm gate, so every
// refusal above suppresses the drag with no second spelling of the guard ladder.
// is_begin picks the bound: the ctrl trim-bar click sets begin, ctrl+shift sets
// end.
bool GuiInputHandler::set_trim_bound_at_click(bool is_begin, int mouse_x) {
    if (active_view_state(app).read_only) return false;   // trim authoring
    if (audio.total_frames() <= 0 || audio.sample_rate() <= 0) return false;
    if (current_samples_per_pixel(app, audio) <= 0.0) return false;
    const GuiRect area = waveform_area(app);
    if (area.w <= 0) return false;
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
    if (is_begin) { if (frame >= app.trim.end_frame)   return false; }
    else          { if (frame <= app.trim.begin_frame) return false; }
    // The act commits from here on, so THIS is where it stops a live audition
    // (architect 2026-07-27): the trim window is about to change under it, and
    // every refusal above — read-only, a degenerate audio/geometry state, a
    // non-strictly-inside value — has already returned without stopping anything.
    // The caller (the ctrl / ctrl+shift trim-bar press) carries no stop of its own
    // for exactly that reason. Ahead of the write, like every claim's stop.
    playback_lifecycle.stop_playback_if_playing();
    if (is_begin) {
        app.trim.begin_frame = frame;
    } else {
        app.trim.end_frame = frame;
    }
    auto_clear_crossed_trim();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
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
// THE ARM RIDES THE SET'S VERDICT: a refused set (read-only tab, degenerate
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

// Plain chip-row press trim routing — the PLAIN press's route into a trim drag,
// and one of TWO (re-derived by grepping arm_pending_trim_drag's callers,
// 2026-08-01): this router, and the ctrl / ctrl+shift BOUND-SET press above,
// which arms the same single-bound pending on the bound it has just written.
// The Alt pointer gesture retired wholesale, and the waveform stem grab with it:
// a bound is grabbed ONLY by its top-strip chip or the inter-chip bridge (the
// chip was already the unambiguous handle), leaving the waveform purely
// region/playhead. Arms a PendingTrimDrag rather than beginning the drag
// outright — the pending+threshold pattern the marker flag uses: the press
// CLAIMS the chip/bridge geometry, a motionless press-release commits nothing,
// and only once the pointer crosses kDragMovedThresholdPx does begin_trim_drag
// run and the existing single/pair drag machinery take over unchanged. A full
// ordered pair ALWAYS rests (the unset state died 2026-07-30), so the old
// pair-required gate is gone and the press is claimed purely on GEOMETRY.
// Returns true iff the press landed on trim chip
// geometry (a chip-rect single-bound hit, or the chip-row inter-chip bridge
// region) — armed or read-only-refused — so the caller CLAIMS the press (no
// fallback); false lets the caller fall through. Trim bounds are transparent to
// every OTHER chord (the caller gates this to the plain, unmodified press).
// Read-only claims WITHOUT arming (a silent return, no fallback). The two arms:
//   CHIP HIT: a chip-rect hit (hit_test_trim_chip, itself y-gated to the chip
//     row) arms that bound's single drag.
//   BRIDGE: else, a press whose y lies in the chip (upper) row band —
//     top_trim_row_area, the band the bridge bar spans between
//     the two chips — and whose column falls inside the shared trim_bridge_gap
//     interval (render.h) arms the pair drag. That is the SAME owner
//     render_trim_flags' bar uses, so the clickable band IS the painted bar
//     exactly (no reliance on the CHIP HIT above consuming pixels first — the
//     chip rects sit outside the gap either way), plus a [0, area_w) click gate so
//     the inert non-multiple-of-16 gutter cannot arm past the painted surface.
//     The bridge handle is the chip-ROW inter-chip span, NOT the whole strip
//     height: a top-strip press below the chip row (the marker flag row) is not
//     claimed and falls through to the caller's flag handling. Both bounds are
//     the subject (no grabbed-bound notion; the pair has no viewport clamp and,
//     like every trim gesture, never moves the playhead), so it always arms as
//     Begin structurally.
// The drags DESELECT and STOP a live audition at
// their FIRST ACCEPTED bound change (architect 2026-07-29 / 2026-07-30 — the
// press-time stop went with the highlight-only publish it accompanied); the
// PLAYHEAD is what they never
// touch, and the deselect RESTS — the gesture has no cancel to restore it from.
//
// The bridge-region bound columns come from the displayed MAP
// (displayed_or_live_target_map) AND the displayed VIEWPORT
// (item_viewport_basis) — the EXACT basis and owner chain the live trim
// pass (GuiPaintHandler::paint_trim) paints the chips/bar from every frame
// (displayed_trim_ms -> trim_bound_column -> trim_bridge_gap), so a hit lands
// on what is drawn BY SHARED OWNERS: paint and hit read the same functions on
// the same basis (the
// event-sync ruling at that selector). The remaining seams are all
// ACCEPTED:
// commit-to-scanout plus human reaction (irreducible — input responds to the
// previously presented frame), the COLD-STATE fallback (first paint, a view
// toggle, or just after load, live map until the first committed target frame),
// and the playhead-placement clicks (column-based, out of scope by ruling — a
// far subtler seam).
bool GuiInputHandler::route_trim_chip_press(int mouse_x, int mouse_y) {
    if (audio.total_frames() <= 0) return false;
    // Event-synchronized hit geometry, the VIEWPORT half (the ruling at the
    // header): the chip AND bridge pixels are painted live (paint_trim) on the
    // DISPLAYED basis, so both the
    // single-chip hit (hit_test_trim_chip, which takes its own displayed basis)
    // and the bridge column math below ride the SAME basis, never the live
    // viewport — else a press on the visible bridge during an async publish window
    // could fall through unclaimed (or a blank point falsely arm the pair drag).
    // Cold falls back to the live basis (see the accessor), matching the
    // painter's cold fallback.
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    if (basis.spp <= 0.0) return false;

    // Single-drag hit: the chip rect (hit_test_trim_chip, chip-row-gated).
    const TrimHit single = hit_test_trim_chip(app, audio, mouse_x, mouse_y);
    if (single != TrimHit::None) {
        // Read-only claims the press but never arms (no fallback).
        if (active_view_state(app).read_only) return true;
        arm_pending_trim_drag(single == TrimHit::Begin, /*both=*/false,
                              mouse_x, mouse_y);
        return true;
    }

    // Bridge (pair) drag: the CHIP ROW ONLY — a press whose y lies in the
    // top-strip upper-row band (top_trim_row_area, the exact band
    // hit_test_trim_chip y-gates on and the band the bridge bar
    // spans between the two chips) and whose column falls inside the painted bar's
    // gap between the two chips (both bounds always set, structurally). A
    // top-strip press BELOW that band — the marker flag row — is not the bridge
    // handle: it falls through to the caller's flag handling. The pair has no
    // grabbed-bound notion — both bounds are the subject, so it always arms as
    // Begin structurally (there is no nearer-bound pick, and the gesture never
    // moves the playhead). The gap interval uses the forward-map + column math on
    // the painted items' own map AND displayed viewport (the event-sync ruling
    // above), computed only on this path.
    const GuiRect row = top_trim_row_area(app);
    if (mouse_y >= row.y && mouse_y < row.y + row.h) {
        const GuiRect area = waveform_area(app);
        // click_rel_x is waveform-relative from the layout origin area.x (a
        // stable layout constant, not viewport-driven); the gap interval below is
        // 0-based columns in the SAME committed-width column space, so the
        // in-gap test compares like against like.
        const int click_rel_x = mouse_x - area.x;
        const std::vector<WarpFrameMapSegment>& dmap =
            displayed_or_live_target_map(app, audio);
        const std::vector<WarpFrameMapSegment>* map =
            dmap.empty() ? nullptr : &dmap;
        // Bridge hit interval = the PAINTED bar's gap EXACTLY, via the shared
        // owner trim_bridge_gap (render.h) — the SAME owner render_trim_flags'
        // bar uses — over the two bounds' TrimBoundColumns on the DISPLAYED basis
        // (vp_start_frame/vp_end_frame/area_w — the same triple the live trim
        // pass paints with), so the columns are exactly where the
        // bar is painted. The owner already handles the
        // offscreen-flush edges (no chip-width inset for an unpainted bound), so
        // this needs no min/max and no reliance on the chip single-hit consuming
        // the chip pixels first (the chip rects sit OUTSIDE the gap either way).
        auto bound_column = [&](int64_t frame) -> TrimBoundColumn {
            const double ms = displayed_trim_ms(frame, map);
            return trim_bound_column(ms, basis.vp_start_frame,
                                     basis.vp_end_frame, basis.area_w);
        };
        const TrimBoundColumn bc = bound_column(app.trim.begin_frame);
        const TrimBoundColumn ec = bound_column(app.trim.end_frame);
        const TrimBridgeGap gap =
            trim_bridge_gap(bc, ec, trim_endcap_w_px(), basis.area_w);
        // The [0, area_w) click gate — the SAME effective-width clip the bridge
        // PAINTER applies (render_trim_flags intersects its drawn extent with
        // [0, wave_w)): the inert non-multiple-of-16 right gutter (or a newly
        // exposed width over an older committed basis) NEITHER paints the bar NOR
        // arms, so paint == hit exactly there.
        if (click_rel_x >= 0 && click_rel_x < basis.area_w &&
            click_rel_x >= gap.lo && click_rel_x < gap.hi) {
            // Read-only claims the bridge region but never arms (no fallback).
            if (active_view_state(app).read_only) return true;
            arm_pending_trim_drag(/*is_begin=*/true, /*both=*/true,
                                  mouse_x, mouse_y);
            return true;
        }
    }
    return false;
}

// Arm the pending trim chip/bridge drag from a plain chip-row press. Mirrors
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
