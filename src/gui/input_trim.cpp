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
// x key's clear arm) and the crossed-commit auto-clear (auto_clear_crossed_trim)
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
// set. The caller is handle_trim_x's no-region branch. Trim is gesture-owned
// and excluded from undo/redo history.
void GuiInputHandler::handle_trim_clear_both() {
    if (app.trim.has_begin || app.trim.has_end) {
        clear_trim_bounds();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    }
}

// Bare x branches on the HIGHLIGHT, not the trim, with no
// context-awareness beyond that: the playhead plays no part, and there are no
// positional rules. A live REGION (the drag-painted span, active-domain frames)
// always TRIMS to it — begin at the span's lo, end at its hi — overwriting any
// existing bounds (the new span simply replaces them; x re-trims even over an
// existing trim) — and the highlight is KEPT (architect 2026-07-23, reversing
// the 2026-07-20 consume ruling): under the trim<->REGION coupling the trim
// window and the highlight now agree, so after x they coincide — the tail runs
// sync_highlight_to_trim_window so the region and window rest coupled through the
// one owner (provenance RegionProvenance::TrimWindow — the selection is never
// touched). Two distinct clears: CROSSED AUTHORED BOUNDS (auto_clear) destroy the
// trim window AND the highlight; COINCIDENT MAPPED IMAGES (bracket-legal
// compression rounding both bounds to one target frame) leave the AUTHORED window
// untouched and clear only the HIGHLIGHT — so `has_begin && has_end` does NOT
// imply an active TrimWindow region. NO region → x CLEARS the trim via
// handle_trim_clear_both, whose has_begin||has_end guard makes no-trim a natural
// no-op (nothing to clear, no highlight to clear either — the region is inactive
// in this branch by definition). Read-only refuses silently BEFORE anything,
// leaving the region untouched (trim authoring).
//
// Set-from-region: normalize the span at read time (endpoints rest in drag
// order), inverse-map each active-domain endpoint to a source frame through
// active_domain_to_source_frame (identity in source view, the target-view
// inverse the trim gestures already use, funnelling through snap_authored_frame
// once) — the map is monotone, so lo/hi order survives — then clamp to the
// shared [0, total-1] walls. A span collapsing to end <= begin after the snap
// destroys both bounds via auto_clear_crossed_trim (the standing rule); the
// coupling sync then clears the REGION with the window (the selection is left
// alone). The shared trim commit tail (auto_clear_crossed_trim
// then the repaint/trigger, then the coupling sync) mirrors the other trim
// commits; the playhead is
// untouched (trim gestures never move it).
void GuiInputHandler::handle_trim_x() {
    if (audio.total_frames() <= 0 || audio.sample_rate() <= 0) return;
    // x is trim authoring in both directions: read-only refuses silently,
    // and a resting region is left as it is.
    if (active_view_state(app).read_only) return;

    // No live region → clear the trim. handle_trim_clear_both owns the
    // has_begin||has_end guard and the repaint/trigger tail, so no-trim is a
    // natural no-op; the region is inactive here by definition, nothing to clear.
    if (!app.region.active) {
        handle_trim_clear_both();
        return;
    }

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
    // region and trim rest coupled through the one owner (the selection is never
    // touched); a crossed-collapse dissolve (auto_clear above) clears the region
    // with the window.
    sync_highlight_to_trim_window();
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
    // Default: a plain chip drag. The bound-set crossing (input_pointer.cpp)
    // sets this true AND overrides orig_begin/orig_end to the pre-press pair
    // AFTER this call, so an Esc undoes the click-set too (R3). Reset here so a
    // prior gesture's value can never leak (the struct is also fully reset at
    // every drag end, so this is belt-and-braces).
    app.trim_drag.set_click    = false;
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
    // The BEGIN only captures drag state — no region write here; the region
    // highlight syncs to the window at motion/commit (the lane-click coupling;
    // the selection is never touched). A press that never moves commits no bound change
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
            // crossed bounds — the selection is never touched).
            sync_highlight_to_trim_window();
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
        // in the coupling helper — the selection is never touched).
        sync_highlight_to_trim_window();
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
        // identity in source view), the SAME map the trim stems paint through:
        // the waveform cache's baked map (fp_warp_frame_map) is what the on-screen
        // stems are drawn with, and displayed_or_live_target_map returns exactly
        // that (falling back to the live cache only when cold), so the release
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
        // window is gone) — the coupling helper owns both outcomes (the selection
        // is never touched).
        sync_highlight_to_trim_window();
    }
    app.trim_drag = TrimDragState{};
}

// The lane-click model's trim<->REGION coupling (architect 2026-07-23; amended
// to region-only, SELECTION FLOWS DOWNWARD ONLY). Trim is region-related — the
// region sets the trim — so this follows the same downward-only rule: TRIM
// DOESN'T SELECT MARKERS, ONLY THE REGION. The marker SELECTION is the master
// state; trim gestures never touch it, exactly as they never touch the PLAYHEAD.
// Both bounds set with SEPARATED images -> the region takes the trim window's
// active-domain extent (each source bound through source_frame_to_active_domain,
// clamped to a playable frame; bounds may be crossed mid-drag, so normalize lo/hi,
// the map is monotone) with TrimWindow provenance. Both bounds set but COINCIDENT
// images (lo == hi under bracket-legal compression) -> clear the HIGHLIGHT only,
// leaving the AUTHORED window untouched (a zero-width region would let x destroy
// the pair — see the branch). Lone / no trim -> no window -> clear the REGION.
// So `has_begin && has_end` does NOT imply an active TrimWindow region. Every arm
// leaves the SELECTION alone. Read-only-safe (the region is navigation). The
// playhead and the selection are never touched.
//
// Defined as a FREE function so the group tempo gestures (MarkerDragOps /
// GuiWarpMarkersOps) can RE-SYNC a TrimWindow region from app.trim's SOURCE-frame
// bounds through the NEW live map after a tempo edit (FIX C) — reachable through
// input_handler.h beside set_region_to_selection_extent. GuiInputHandler's
// sync_highlight_to_trim_window is a thin wrapper over it (all its callers keep
// working).
void sync_region_to_trim_window(AppState& app, const GuiAudio& audio,
                                Viewport& viewport) {
    if (app.trim.has_begin && app.trim.has_end) {
        const int64_t a = clamp_playhead_to_live_domain(
            source_frame_to_active_domain(app, audio, app.trim.begin_frame),
            app, audio);
        const int64_t b = clamp_playhead_to_live_domain(
            source_frame_to_active_domain(app, audio, app.trim.end_frame),
            app, audio);
        const int64_t lo = std::min(a, b);
        const int64_t hi = std::max(a, b);
        if (lo == hi) {
            // COINCIDENT IMAGES -> take the CLEAR arm, do NOT publish a
            // zero-width region (the standing sliver rule: degenerate spans never
            // rest). Distinct legal source bounds can round to the SAME target
            // frame (16x compression is bracket-legal), and an active lo==hi
            // region would let a bare x inverse-map the degenerate span to an
            // EQUAL pair that auto_clear_crossed_trim silently DESTROYS (trim is
            // outside undo). A window whose image collapses below one target frame
            // cannot be honestly highlighted, so clear the HIGHLIGHT only — the
            // TRIM ITSELF is untouched (its authored source-frame bounds stand),
            // and clearing drops the LIVE provenance. Recovery depends on the
            // caller: within a tempo DRAG the grab-time trim intent
            // (TempoDragState::grab_trim_highlight) re-syncs the window BACK on the
            // next event whose images re-separate, and Esc restores the captured
            // pre-drag region verbatim; for a RESTING clear (a drag RELEASED while
            // coincident, or a one-shot group STEP that reads live provenance) the
            // user re-clicks the chip row once the images re-separate. Either way,
            // x with no highlight takes its documented no-highlight clear branch
            // (WYSIWYG — no hairline wash, no silent pair destruction).
            app.region = RegionState{};
        } else {
            app.region.active     = true;
            app.region.a_frame    = lo;
            app.region.b_frame    = hi;
            // Trim-DERIVED provenance: a 2+ marker selection may sit beside this
            // region, but it is NOT that selection's extent — the tempo gestures
            // must never snap it to the selection extent. Instead they RE-SYNC a
            // TrimWindow region from app.trim's source-frame bounds through the new
            // map (this very function re-run), so the wash tracks the chips/stems
            // across a tempo edit. This is the provenance that lets the
            // trim/highlight coupling and the selection-extent follows both hold.
            app.region.provenance = RegionProvenance::TrimWindow;
        }
    } else {
        // No window (lone / no trim): clear the REGION only (the selection is
        // untouched — trim never mutates it). A LONE bound still paints its chip,
        // stem, and one-sided out-of-trim dim — a bright completed-window span
        // that can READ as a highlight — and the cursor playhead stays painted
        // beside it. That is deliberate: the wash<->cursor exclusivity
        // (paint_playheads) is scoped to the REGION highlight, and a
        // half-authored trim has no window to highlight (the R4 no-window rule).
        app.region = RegionState{};
    }
    viewport.invalidate_waveform_area();
}

void GuiInputHandler::sync_highlight_to_trim_window() {
    sync_region_to_trim_window(app, audio, viewport);
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
// mirrors the drag release. Read-only refuses silently (trim authoring). The
// coupling sync runs afterward against whatever the trim now is (the surviving
// pair -> the REGION highlight takes the window; a crossed dissolve -> the region
// cleared; the selection is never touched).
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
    // The lane-click coupling: keep the REGION highlight agreeing with the
    // window (both bounds -> window; a dissolved/lone result -> cleared; the
    // selection is never touched).
    sync_highlight_to_trim_window();
}

// Round 3 (architect 2026-07-23): the ctrl / ctrl+shift chip-row bound-set press
// sets the bound at the click AND arms the single-bound trim drag on it, so
// motion past the threshold drags it live (a motionless release rests the
// click-set). Snapshot the PRE-PRESS pair first so an Esc undoes the whole
// gesture — the click-set included — via the pending's preset_* -> the drag's
// orig_* Esc-restore origin (see PendingTrimDrag / TrimDragState set_click). The
// set itself owns the read-only / missing-pair refusal; the drag arms only when
// the set kept a full writable pair.
void GuiInputHandler::set_trim_bound_at_click_then_arm_drag(bool is_begin,
                                                            int mouse_x,
                                                            int mouse_y) {
    const bool had_pair    = app.trim.has_begin && app.trim.has_end;
    const int64_t pre_begin = app.trim.begin_frame;
    const int64_t pre_end   = app.trim.end_frame;
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
    app.pending_trim_drag.set_click         = true;
    app.pending_trim_drag.preset_begin_frame = pre_begin;
    app.pending_trim_drag.preset_end_frame   = pre_end;
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
//     top_upper_row_area, the band the translucent bridge block spans between
//     the two chips — and whose column is strictly between the two BOUND
//     columns arms the pair drag. The chips edge-anchor ON their bound columns
//     (begin left-edge, end right-edge, bodies facing inward), so this
//     between-columns test is a superset of the visible gap; the CHIP HIT above
//     is tested first and consumes the chip pixels, so the effective bridge grab
//     equals the wash gap that render_trim_flags paints (paint and press match).
//     The bridge handle is the chip-ROW inter-chip span, NOT the whole strip
//     height: a top-strip press below the chip row (the marker flag row) is not
//     claimed and falls through to the caller's flag handling. Both bounds are
//     the subject (no grabbed-bound notion; the pair has no viewport clamp and,
//     like every trim gesture, never moves the playhead), so it always arms as
//     Begin structurally.
// Both arms presuppose the full pair (the gate above): a lone bound arms
// nothing — it is gesture-inert. The drags sync the REGION highlight to the
// moving window (the lane-click coupling, sync_highlight_to_trim_window); the
// PLAYHEAD and the SELECTION are what they never touch.
//
// The bridge-region bound columns come from the displayed MAP
// (displayed_or_live_target_map) AND the displayed VIEWPORT
// (displayed_viewport_basis) — the map + vp_start/vp_end/effective-width the
// on-screen chips were painted with on the LAST COMMITTED frame (the item
// rebuilds only STAGE; the paint pass PROMOTES both halves at the committing
// frame, not at the earlier plate publish) — so a hit lands on what is drawn:
// the routing decision flips at the exact instant the on-screen items flip (the
// event-sync ruling at that selector). What was the open live-vs-painted window
// for item hits is CLOSED to commit granularity. The remaining seams are all
// ACCEPTED:
// commit-to-scanout plus human reaction (irreducible — input responds to the
// previously presented frame), the COLD-STATE fallback (first paint, a view
// toggle, or just after load, live map until the first committed target frame),
// and the playhead-placement clicks (column-based, out of scope by ruling — a
// far subtler seam).
bool GuiInputHandler::route_trim_chip_press(int mouse_x, int mouse_y) {
    if (audio.total_frames() <= 0) return false;
    // Event-synchronized hit geometry, the VIEWPORT half (the ruling at the
    // header): the chip AND bridge pixels are painted from the flag cache's
    // committed fp_vp span over the effective width the render used, so both the
    // single-chip hit (hit_test_trim_chip, which takes its own displayed basis)
    // and the bridge column math below ride the DISPLAYED basis, never the live
    // viewport — else a press on the visible bridge during an async publish window
    // could fall through unclaimed (or a blank point falsely arm the pair drag).
    // Cold falls back to the live basis (see the accessor).
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
    // hit_test_trim_chip y-gates on and the band the translucent bridge block
    // spans between the two chips) and whose column falls inside the painted wash
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
        // Bridge hit interval = the PAINTED wash gap EXACTLY, via the shared owner
        // trim_bridge_gap (render.h) — the SAME owner render_trim_flags' wash uses
        // — over the two bounds' TrimBoundColumns on the DISPLAYED basis
        // (vp_start_frame/vp_end_frame/area_w — the flag cache's own committed
        // fp_vp span and effective width), so the columns are exactly where the
        // wash was painted on the committing frame. The owner already handles the
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
        const TrimBridgeGap gap = trim_bridge_gap(bc, ec, flag_lane_w_px());
        // The [0, area_w) click gate: the inert non-multiple-of-16 right gutter
        // (or a newly exposed width over an older committed cache) has no painted
        // wash, so a click there must NOT arm a pair drag past the surface.
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
}
