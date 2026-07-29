#include "input_handler.h"

#include "gui_display_context.h"
#include "render.h"
#include "warp_frame_map_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

// Trim gestures (architect-ruled hardfail model; the full ruling sits at the
// TrimState store in app_state.h): begin and end are authored named roles.
// Every gesture clamps each bound to its absolute walls — frame 0 to EOF-1,
// the same wall both marker columns hold. All authored positions (both marker
// columns and both trim bounds) share the inclusive [0, total-1] domain — the
// end bound's old exclusive-at-total wall is retired. There are no partner
// walls: a bound crosses its partner freely during any gesture — but
// crossed/equal can no longer REST: every trim commit runs
// auto_clear_crossed_trim (below), so a commit landing on or across the
// partner destroys both bounds. Every wall check is a plain integer compare —
// literally the load guard's comparison.
// The zero floor is subsumed by the walls but remains the reason the floor
// exists at all: a negative position is unrepresentable in the authored
// frame form the .settings file persists (parse_authored_frame rejects
// negatives as malformed) — a format-representability floor, not a spacing
// or validity rule. Past-EOF bounds are unreachable: the gesture walls
// forbid authoring one, and the load boundary (file_loader / CLI)
// hard-fails a past-EOF bound in a hand-edited .settings as adversarial
// input (a .settings applies only to its own audio). validate_trim_frames
// (trimmer.h) still authors the trim-validity vocabulary, but a refusal at
// render time now means "render untrimmed" (do_render's fallback), not a
// refused render.

// The clear-both field resets, shared verbatim by handle_trim_clear_both (the
// Shift+X key's clear arm) and the crossed-commit auto-clear (auto_clear_crossed_trim)
// so the two clears can never drift. Fields only: no invalidation, no trigger —
// callers own their repaint tail.
void GuiInputHandler::clear_trim_bounds() {
    app.trim.has_begin      = false;
    app.trim.has_end        = false;
    app.trim.begin_frame    = 0;
    app.trim.end_frame      = 0;
}

// Architect ruling (2026-07-15): crossed/equal trim bounds cannot REST —
// committing one bound onto or across the other destroys BOTH bounds, the
// trim sibling of the marker normalizations (ambiguous states resolve
// instead of resting or refusing). SILENT by design: the chips visibly
// disappear, which is the whole signal. The check is the exact integer
// compare end_frame <= begin_frame, run only when both bounds are set, and
// only at COMMIT — mid-gesture crossing stays free (nothing pops
// mid-gesture; update_trim_drag never calls this). Every trim commit site —
// the x set-from-region, the chip/bridge drag release, and the settings-editor
// `:trim_*=` commit — calls this after its mutation and before its
// invalidations, so the repaint shows the cleared state.
void GuiInputHandler::auto_clear_crossed_trim() {
    if (app.trim.has_begin && app.trim.has_end &&
        app.trim.end_frame <= app.trim.begin_frame) {
        clear_trim_bounds();
    }
}

// Clear both trim bounds unconditionally. Silent no-op when neither bound is
// set. The caller is handle_trim_shift_x (the Shift+X unset arm; architect
// 2026-07-25 split it off x, which is now set-only). Trim is gesture-owned and
// excluded from undo/redo history.
void GuiInputHandler::handle_trim_clear_both() {
    if (app.trim.has_begin || app.trim.has_end) {
        clear_trim_bounds();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    }
}

// Bare x is SET-ONLY (architect 2026-07-25, reversing the 2026-07-20 x-branch
// ruling's clear arm — the unset moved to the shift-exact Shift+X binding,
// handle_trim_shift_x below): x branches on the HIGHLIGHT, not the trim, with no
// context-awareness beyond that (the playhead plays no part, and there are no
// positional rules). A live REGION (the drag-painted span, active-domain frames)
// always TRIMS to it — begin at the span's lo, end at its hi — overwriting any
// existing bounds (the new span simply replaces them; x re-trims even over an
// existing trim) — and the highlight is KEPT (architect 2026-07-23, reversing
// the 2026-07-20 consume ruling): under the trim<->REGION coupling the trim
// window and the highlight now agree, so after x they coincide — the tail runs the
// SETTER's publish so the region and window rest coupled through the
// one owner (provenance RegionProvenance::TrimWindow) — and THE SELECTION IS
// CLEARED with it: x is a TrimWindow SETTER, and every setter deselects
// (architect 2026-07-29 — the rule, the setter list, and the routes that do NOT
// deselect live at sync_region_to_trim_window's declaration, input_handler.h).
// The COINCIDENT-IMAGE arm of the sync is gone (2026-07-29), so a full pair now
// DOES imply an active TrimWindow region — a window whose two images round onto one
// target frame rests ACTIVE at that one column — and x GAINS A REFUSAL for exactly
// that shape: a DEGENERATE RESULT (the inverse-mapped, wall-clamped pair coming out
// end <= begin — the coincident window's two identical stored endpoints inverse-map
// to one source frame, reachable around 16x bracket-legal compression) makes x a
// silent no-op that writes NOTHING, because the alternative is destruction:
// auto_clear_crossed_trim reads a degenerate write as crossed and silently destroys
// the authored pair, and trim has no undo (planner-decided 2026-07-29 pending
// architect confirmation — the alternative was accepting silent pair destruction;
// the deleted arm used to prevent this by clearing the highlight, which made x
// no-op on the no-region test instead). A zero-length window is not authorable, so
// there is nothing for x to set. The refusal is the FIRST thing past the clamps,
// ahead of every write, so a refused x touches neither trim, region, nor selection.
// NO region → x is a SILENT NO-OP (the clear
// arm moved to Shift+X; x never unsets). NO read-only check here: this pair is
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
// can fire it. The refusal is provenance-BLIND, testing the pair x would write
// rather than where the span came from, so it also covers a narrow FREE span over
// stretched audio, whose inverse-mapped endpoints can land on one source frame.
// The bounds are read into locals ABOVE the deselect deliberately: the
// span x is trimming to may be a SelectionExtent one, which the deselect takes
// with the membership. The
// shared trim commit tail (auto_clear_crossed_trim
// then the repaint/trigger, then the coupling sync) mirrors the other trim
// commits; the playhead is
// untouched (trim gestures never move it).
void GuiInputHandler::handle_trim_x() {
    if (audio.total_frames() <= 0 || audio.sample_rate() <= 0) return;
    // No live region → silent no-op: x is set-only, and the unset is Shift+X's
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
    // pair it destroys — the one silent, unrecoverable outcome trim cannot afford.
    // Nothing is touched: trim, region and selection all rest (the deselect is
    // downstream, so the refusal is refusal-gated like every other trim claim).
    if (end <= begin) return;
    app.trim.begin_frame = begin;
    app.trim.has_begin   = true;
    app.trim.end_frame   = end;
    app.trim.has_end     = true;
    auto_clear_crossed_trim();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
    // The coupling tail (architect 2026-07-23): the highlight is KEPT — the sync
    // re-derives the REGION (provenance TrimWindow) from the just-set window, so
    // region and trim rest coupled through the one owner. The sync always takes its
    // SET arm from here: the degenerate refusal above is what guarantees a full,
    // non-crossed pair stands at this point. THE SETTER'S
    // PUBLISH (architect 2026-07-29): deselect FIRST, then sync — x has taken the
    // selection's span for the trim, so nothing is left to own it.
    deselect_and_sync_trim_window_highlight();
}

// Shift+X UNSETS the trim (architect 2026-07-25 — the unset arm x used to own
// when no region was live; x is now set-only). One-shot, history-less like every
// trim mutation. No read-only check of its own (see handle_trim_x above: the
// keyboard gate owns that decision for both). Delegates to
// handle_trim_clear_both — whose has_begin||has_end guard makes an already-empty
// trim a natural no-op and whose tail owns the repaint (waveform + timestamp) and
// the target_render trigger. The REGION is touched only through
// handle_trim_clear_both's own repaint tail's downstream: the clear does NOT run
// the coupling sync here because it is not a trim COMMIT — but a resting
// TrimWindow-provenance highlight would then outlive its destroyed window, so run
// sync_highlight_to_trim_window afterward too. The sync's no-window arm
// (has_begin && has_end both false after the clear) clears the REGION: a
// TrimWindow region (the trim's own highlight) dissolves with the window it
// mirrored, which is exactly right — but so would any Free/SelectionExtent region,
// since sync_region_to_trim_window's else arm assigns RegionState{} regardless of
// provenance. A Free/SelectionExtent region is NOT trim's to clear, so gate the
// sync on TrimWindow provenance: only a highlight the trim itself owns is torn
// down with the window; a selection-extent or free region rests untouched.
// Shift+X is a trim CLEARER, not a SETTER, so it keeps the BARE sync and does NOT
// deselect (architect 2026-07-29 — the setter list and the rule are at
// sync_region_to_trim_window's declaration, input_handler.h). Nothing hinges on
// that: the TrimWindow highlight it tears down was resting beside an EMPTY
// selection anyway, by the same ruling's invariant.
void GuiInputHandler::handle_trim_shift_x() {
    const bool had_trim_window =
        app.region.active &&
        app.region.provenance == RegionProvenance::TrimWindow;
    handle_trim_clear_both();
    // Only a TrimWindow highlight is trim's to dissolve. Re-sync it so a resting
    // window-derived region cannot outlive the window; Free/SelectionExtent
    // regions are left alone (downward-only: trim never touches a non-trim region).
    if (had_trim_window) sync_highlight_to_trim_window();
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
    // Any trim drag — single or pair — requires BOTH bounds set: a lone bound
    // is gesture-inert. The router already gates on the full pair, so this is
    // the structural backstop.
    if (!(app.trim.has_begin && app.trim.has_end)) return;
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
    // The BEGIN only captures drag state — no region write and no deselect here;
    // the region highlight syncs to the window at motion/commit (the lane-click
    // coupling), and THOSE publishes are the setter's, so they carry the deselect,
    // which then rests (nothing restores it — pointer gestures have no cancel).
    // A press that never moves commits no bound change
    // (its motionless release runs the R4.5 click sync instead).
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
            // R7 live-sync: the region highlight tracks the moving window (both
            // bounds stay set through the drag; the coupling helper normalizes
            // crossed bounds). THE DRAG IS A SETTER, so its live sync DESELECTS
            // (architect 2026-07-29 — rule at sync_region_to_trim_window's
            // declaration): past the moved-bounds gate above, so a drag event that
            // changes nothing deselects nothing, and idempotent across the
            // gesture's later events.
            deselect_and_sync_trim_window_highlight();
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
        // R7 live-sync: the region highlight tracks the moving window (both
        // bounds stay set through the single-bound drag; crossing is normalized
        // in the coupling helper). THE DRAG IS A SETTER, so its live sync
        // DESELECTS (the pair arm's twin above; rule at
        // sync_region_to_trim_window's declaration) — past the moved-bound gate,
        // and idempotent across the gesture's later events.
        deselect_and_sync_trim_window_highlight();
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
        // partner destroys both bounds (crossed/equal cannot rest; ruling at
        // auto_clear_crossed_trim). Mid-drag crossing above stayed free; the
        // playhead was never touched by the gesture.
        auto_clear_crossed_trim();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
        // R7 release-sync: a surviving pair re-syncs the REGION against the
        // snapped bounds; a dissolved pair (crossed commit) clears the region (the
        // window is gone) — the coupling helper owns both outcomes. THE RELEASE IS
        // THE SETTER'S LAST PUBLISH, so it deselects like the motion events did
        // (already empty by then in the moved case that reaches here — the deselect
        // is stated at every publish rather than inferred from gesture order). A
        // RELEASE is a commit, so nothing is restored here; the snapshot the drag
        // carried dies with the struct reset below.
        deselect_and_sync_trim_window_highlight();
    }
    app.trim_drag = TrimDragState{};
}

// The lane-click model's trim<->REGION coupling (architect 2026-07-23; amended
// to region-only, SELECTION FLOWS DOWNWARD ONLY). Trim is region-related — the
// region sets the trim — so this follows the same downward-only rule: TRIM
// DOESN'T SELECT MARKERS, ONLY THE REGION. No arm here ever adds or removes a
// member; the DESELECT that every trim SETTER now carries lives one level up, at
// deselect_and_sync_trim_window_highlight and its callers (the rule and the
// setter list are at this function's declaration, input_handler.h). The PLAYHEAD
// this never touches at all.
// TWO ARMS (architect 2026-07-29, the coincident middle arm deleted):
//   * BOTH BOUNDS SET -> the region takes the trim window's active-domain extent
//     (each source bound through source_frame_to_active_domain, clamped to a
//     playable frame; bounds may be crossed mid-drag, so normalize lo/hi, the map
//     is monotone) with TrimWindow provenance — COINCIDENT IMAGES INCLUDED. A
//     window whose two source bounds round onto ONE target frame (bracket-legal
//     compression, ~16x) rests ACTIVE at that one column: the degenerate paint
//     shape is already owned elsewhere — paint_region_ground draws nothing at zero
//     width and render_split_playhead has an explicit one-column branch stamping
//     the single whole cursor triangle — and the undo group restore already rests
//     an active zero-width extent on that same shape (undo.cpp). So a full pair
//     DOES imply an active TrimWindow region.
//   * LONE / NO TRIM -> no window -> clear the REGION.
// Read-only-safe (the region is navigation).
//
// THE SELECTION IS STILL NOT UNTOUCHED, but the reason inverted (architect
// 2026-07-29): the clear arm's shared tail below carries the
// never-rest-2+-without-a-span collapse, and the TRIM routes can no longer reach
// it — every setter empties the selection before publishing, so a trim teardown
// meets an empty selection. The tail's live callers are the ones that are NOT
// setters: Shift+X and the settings-editor trim commit, which re-sync a window
// they did not claim. NOTHING RESTORES A TRIM SETTER'S DESELECT: the trim
// gestures' pre-gesture snapshots are deleted with every other cancel capture
// (2026-07-29 — pointer gestures have no cancel, the rule at the drag-modal gate
// in input_handler.cpp), so a chip drag's deselect rests like `x`'s does.
//
// Defined as a FREE function so the group tempo gestures (MarkerDragOps /
// GuiWarpMarkersOps) can RE-SYNC a TrimWindow region from app.trim's SOURCE-frame
// bounds through the NEW live map after a tempo edit (FIX C) — reachable through
// input_handler.h beside set_region_to_selection_extent. GuiInputHandler wraps it
// twice: sync_highlight_to_trim_window is the bare pass-through the CLEARERS and
// MAINTAINERS use, and deselect_and_sync_trim_window_highlight is the SETTERS'
// form, which deselects ahead of the same call.
void sync_region_to_trim_window(AppState& app, const GuiAudio& audio,
                                Selection& selection, Viewport& viewport) {
    // Pre-state for the damage calibration at the tail (see there). Four fields
    // are the whole visible identity of the region: whether it paints, where its
    // two bounds are, and which provenance it carries.
    const bool             was_active     = app.region.active;
    const int64_t          was_a          = app.region.a_frame;
    const int64_t          was_b          = app.region.b_frame;
    const RegionProvenance was_provenance = app.region.provenance;
    if (app.trim.has_begin && app.trim.has_end) {
        const int64_t a = clamp_playhead_to_live_domain(
            source_frame_to_active_domain(app, audio, app.trim.begin_frame),
            app, audio);
        const int64_t b = clamp_playhead_to_live_domain(
            source_frame_to_active_domain(app, audio, app.trim.end_frame),
            app, audio);
        // COINCIDENT IMAGES ARE NOT A CASE HERE (architect 2026-07-29): the arm
        // that cleared the highlight when lo == hi is DELETED, so a window
        // compressed onto one target frame publishes a one-column region like any
        // other. Its two former justifications are both spent — the sliver rule's
        // "degenerate spans never rest" was about DRAG-FORMED spans a user aims,
        // not about an authored window's image; and the older "protect the pair
        // from a bare x that inverse-maps lo==hi to an equal pair" died with the
        // 2026-07-25 set-only re-split (x never unsets). The paint shape needs
        // nothing: paint_region_ground draws no ground at zero width and
        // render_split_playhead stamps its explicit one-column form.
        app.region.active     = true;
        app.region.a_frame    = std::min(a, b);
        app.region.b_frame    = std::max(a, b);
        // Trim-DERIVED provenance: the region is the trim's own highlight, not any
        // selection's extent — which since 2026-07-29 is the whole story, because
        // every trim SETTER deselects and so a TrimWindow region rests only beside
        // an EMPTY selection (the rule at this function's declaration). The
        // provenance still routes the trim/highlight coupling itself: Shift+X and
        // the settings-editor trim maintainers act on a TrimWindow region and leave
        // a Free one alone.
        app.region.provenance = RegionProvenance::TrimWindow;
    } else {
        // No window (lone / no trim): clear the REGION (the ONE clear arm left, and
        // the only route into the collapse tail below).
        // A LONE bound still paints its chip
        // and its stem — but no bridge bar, which needs the pair — and the cursor
        // playhead stays painted beside it. That is deliberate: the
        // highlight<->cursor exclusivity (paint_playheads) is scoped to the
        // REGION highlight, and a half-authored trim has no window to highlight
        // (the R4 no-window rule).
        app.region = RegionState{};
    }
    // DAMAGE, CALIBRATED FOR A HIGH-FREQUENCY PATH. This is the one full-band
    // waveform invalidate on a per-MOTION-EVENT route (every trim drag event
    // calls here), so unlike the rare discrete commands it does not get to pay
    // full damage unconditionally: skip it when the region's visible identity is
    // bit-identical to what it was AND the collapse below will not fire. Both
    // conditions matter — an unchanged region still needs damage when the
    // collapse changes which flag carries the focus cue. The common skip is the
    // arm nobody notices: no window, no region, nothing to repaint, called on
    // every event of a drag that dissolved its pair. Any real change — a moved
    // bound, an activation, a clear, a provenance flip — damages exactly as
    // before.
    const bool region_unchanged =
        app.region.active     == was_active &&
        app.region.a_frame    == was_a &&
        app.region.b_frame    == was_b &&
        app.region.provenance == was_provenance;
    const bool will_collapse =
        !app.region.active && app.selected_markers.size() >= 2;
    if (!region_unchanged || will_collapse) viewport.invalidate_waveform_area();
    // A 2+ SELECTION NEVER RESTS WITHOUT A SPAN (architect 2026-07-29; the rule
    // and its site list live at clear_region_highlight's declaration,
    // input_handler.h). The single clear arm above lands here, and the callers that
    // can still bring a 2+ selection to it are the NON-SETTERS — Shift+X and the
    // settings-editor trim commit, which tear down or maintain a window they did
    // not claim. Every trim SETTER empties the selection before it publishes
    // (deselect_and_sync_trim_window_highlight), so from those routes this test is
    // false by construction. A group left beside no span would be point form with
    // no point, so it collapses to its FOCUS, where the playhead already rests.
    // The SET arm leaves an active region, so the test below skips it.
    if (will_collapse) selection.collapse_to_focused();
}

void GuiInputHandler::sync_highlight_to_trim_window() {
    sync_region_to_trim_window(app, audio, selection, viewport);
}

// THE TrimWindow SETTER'S PUBLISH: deselect, then sync. Every route that SETS the
// trim window's highlight calls this instead of the bare sync above — the trim-bar
// click is the span-form sibling of the plain waveform click's deselect-all
// ("clicking trim means ready to move on", architect 2026-07-29), so a TrimWindow
// region rests ONLY beside an EMPTY selection. The rule, the setter list, and the
// clearer/maintainer routes that keep the bare sync are stated once at
// sync_region_to_trim_window's declaration (input_handler.h).
// ORDER: the deselect runs FIRST, so the sync writes its region against an already
// empty selection — clear_selection takes any SelectionExtent span with the
// membership it belonged to (clear_region_on_membership_replace), and the sync's
// never-span-less collapse tail can no longer fire on a setter route at all.
// PLACEMENT is each caller's: this goes past every one of that route's refusals
// (read-only bound sets, the no-resting-pair gate, degenerate geometry, a drag
// event that moved no bound), so a claim that publishes no highlight deselects
// nothing. Idempotent under repetition — a drag's later events re-deselect an
// already-empty selection for the cost of one early return.
void GuiInputHandler::deselect_and_sync_trim_window_highlight() {
    selection.clear_selection();
    sync_region_to_trim_window(app, audio, selection, viewport);
}

// R4.6: set ONE trim bound at the clicked column — ADJUST-ONLY (architect
// 2026-07-23): the click requires an EXISTING full pair and refuses silently
// otherwise, matching the standing every-trim-pointer-gesture-requires-the-pair
// convention. The clicks ADJUST a resting window, never create one from
// nothing — the window is created by region→x, and the settings editor remains
// the deliberate lone-bound route. The column maps to a source
// frame through authored_frame_at_column over the DISPLAYED paint map — the same
// release-snap basis commit_trim_drag uses (its snap_moved_bound goes
// source_frame -> painted_column -> authored_frame; a click carries the column
// directly). The absolute walls [0, total-1] apply after the snap, then the
// shared crossed-commit auto-clear (a bound onto/across its partner dissolves
// both). History-less like every trim mutation; the repaint + trigger tail
// mirrors the drag release. Read-only refuses silently (trim authoring). This
// function OWNS the press's playback stop — placed past every refusal above
// and immediately ahead of the bound write, so a refused click never stops a
// live audition (the claim-keyed stop rule at on_button_press's top-strip
// paragraph, taken inside the gate). The
// coupling sync runs afterward against whatever the trim now is (the surviving
// pair -> the REGION highlight takes the window; a crossed dissolve -> the region
// cleared) — and it is a SETTER's publish, so it DESELECTS FIRST (architect
// 2026-07-29; the rule and the setter list at sync_region_to_trim_window's
// declaration). Both bound-set clicks are this one function, so both deselect, and
// both deselect only PAST THE REFUSALS above: a read-only tab and a missing
// resting pair set nothing and leave the selection exactly as it was. The
// deselect RESTS in every case, including when this click ARMS a drag
// (set_trim_bound_at_click_then_arm_drag): that gesture has no cancel either, so
// its caller captures nothing (2026-07-29 — the no-cancel rule at the drag-modal
// gate, input_handler.cpp).
void GuiInputHandler::set_trim_bound_at_click(bool is_begin, int mouse_x) {
    if (active_view_state(app).read_only) return;   // trim authoring
    // Adjust-only pair gate (see the header comment): no resting window, no
    // bound-set click.
    if (!(app.trim.has_begin && app.trim.has_end)) return;
    if (audio.total_frames() <= 0 || audio.sample_rate() <= 0) return;
    if (current_samples_per_pixel(app, audio) <= 0.0) return;
    const GuiRect area = waveform_area(app);
    if (area.w <= 0) return;
    int col = mouse_x - area.x;
    if (col < 0)         col = 0;
    if (col >= area.w)   col = area.w - 1;
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    int64_t frame = authored_frame_at_column(app, audio, col, dmap);
    const int64_t wall = audio.total_frames() - 1;
    if (frame < 0)    frame = 0;
    if (frame > wall) frame = wall;
    // The act commits from here on, so THIS is where it stops a live audition
    // (architect 2026-07-27): the trim window is about to change under it,
    // and every refusal above — read-only, no resting pair, a degenerate
    // audio/geometry state — has already returned without stopping anything.
    // The caller (the ctrl / ctrl+shift chip-row press) carries no stop of its
    // own for exactly that reason. Ahead of the write, like every claim's stop.
    playback_lifecycle.stop_playback_if_playing();
    if (is_begin) {
        app.trim.begin_frame = frame;
        app.trim.has_begin   = true;
    } else {
        app.trim.end_frame = frame;
        app.trim.has_end   = true;
    }
    auto_clear_crossed_trim();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
    // The lane-click coupling, in its SETTER form (see the header): deselect, then
    // keep the REGION highlight agreeing with the window (both bounds -> window; a
    // dissolved/lone result -> cleared). Past every refusal above, so only a click
    // that actually set a bound deselects.
    deselect_and_sync_trim_window_highlight();
}

// Round 3 (architect 2026-07-23): the ctrl / ctrl+shift chip-row bound-set press
// sets the bound at the click AND arms the single-bound trim drag on it, so
// motion past the threshold drags it live (a motionless release rests the
// click-set). NOTHING IS SNAPSHOTTED (2026-07-29): the pre-press pair, the
// selection and the region were all captured here for an Esc-cancel, and pointer
// gestures have no cancel — the rule is at the drag-modal gate in
// input_handler.cpp. The click-set is a COMMITTED act the moment it is made (trim
// is history-less, so nothing takes it back), and the drag it arms commits its own
// bounds at its release. The
// set itself owns the read-only / missing-pair refusal; the drag arms only when
// the set kept a full writable pair.
void GuiInputHandler::set_trim_bound_at_click_then_arm_drag(bool is_begin,
                                                            int mouse_x,
                                                            int mouse_y) {
    const bool had_pair = app.trim.has_begin && app.trim.has_end;
    set_trim_bound_at_click(is_begin, mouse_x);
    // Arm only when a full writable pair survived the set: read-only / a missing
    // pair set nothing (had_pair false OR the set refused), and a crossed
    // click-set dissolved both bounds (auto_clear_crossed_trim) leaving nothing
    // to drag.
    if (!had_pair) return;
    if (active_view_state(app).read_only) return;
    if (!(app.trim.has_begin && app.trim.has_end)) return;  // crossed -> dissolved
    app.pending_trim_drag = PendingTrimDrag{};
    app.pending_trim_drag.active            = true;
    app.pending_trim_drag.is_begin          = is_begin;
    app.pending_trim_drag.both              = false;
    app.pending_trim_drag.press_x           = mouse_x;
    app.pending_trim_drag.press_y           = mouse_y;
}

// Plain chip-row press trim routing — the sole pointer route into a trim drag.
// The Alt pointer gesture retired wholesale, and the waveform stem grab with it:
// a bound is grabbed ONLY by its top-strip chip or the inter-chip bridge (the
// chip was already the unambiguous handle), leaving the waveform purely
// region/playhead. Arms a PendingTrimDrag rather than beginning the drag
// outright — the pending+threshold pattern the marker flag uses: the press
// CLAIMS the chip/bridge geometry, a motionless press-release commits nothing,
// and only once the pointer crosses kDragMovedThresholdPx does begin_trim_drag
// run and the existing single/pair drag machinery take over unchanged. Every
// trim drag requires the FULL pair set; a lone bound is gesture-inert —
// transparent to the press, which falls through to the caller's flag handling.
// Returns true iff both bounds are set AND the press landed on trim chip
// geometry (a chip-rect single-bound hit, or the chip-row inter-chip bridge
// region) — armed or read-only-refused — so the caller CLAIMS the press (no
// fallback); false lets the caller fall through. Trim bounds are transparent to
// every OTHER chord (the caller gates this to the plain, unmodified press).
// Read-only claims WITHOUT arming (a silent return, no fallback). The two arms:
//   CHIP HIT: a chip-rect hit (hit_test_trim_chip, itself y-gated to the chip
//     row) arms that bound's single drag.
//   BRIDGE: else, a press whose y lies in the chip (upper) row band —
//     top_upper_row_area, the band the bridge bar spans between
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
// Both arms presuppose the full pair (the gate above): a lone bound arms
// nothing — it is gesture-inert. The drags sync the REGION highlight to the
// moving window (the lane-click coupling) through the SETTER's publish, which
// DESELECTS at its first moved bound (architect 2026-07-29 — the rule at
// sync_region_to_trim_window's declaration); the PLAYHEAD is what they never
// touch, and the deselect RESTS — the gesture has no cancel to restore it from.
//
// The bridge-region bound columns come from the displayed MAP
// (displayed_or_live_target_map) AND the displayed VIEWPORT
// (displayed_viewport_basis) — the EXACT basis and owner chain the live trim
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
    const DisplayedViewportBasis basis = displayed_viewport_basis(app, audio);
    if (basis.spp <= 0.0) return false;
    // Every trim drag needs the full pair set. With a lone bound, trim
    // contributes NO pointer geometry at all — the press is transparent and
    // falls through to the caller's flag handling.
    if (!(app.trim.has_begin && app.trim.has_end)) return false;

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
    // top-strip upper-row band (top_upper_row_area, the exact band
    // hit_test_trim_chip y-gates on and the band the bridge bar
    // spans between the two chips) and whose column falls inside the painted bar's
    // gap between the two chips (both bounds guaranteed set by the gate above). A
    // top-strip press BELOW that band — the marker flag row — is not the bridge
    // handle: it falls through to the caller's flag handling. The pair has no
    // grabbed-bound notion — both bounds are the subject, so it always arms as
    // Begin structurally (there is no nearer-bound pick, and the gesture never
    // moves the playhead). The gap interval uses the forward-map + column math on
    // the painted items' own map AND displayed viewport (the event-sync ruling
    // above), computed only on this path.
    const GuiRect row = top_upper_row_area(app);
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
            trim_bridge_gap(bc, ec, flag_lane_w_px(), basis.area_w);
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
