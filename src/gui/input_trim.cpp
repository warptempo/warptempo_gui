#include "input_handler.h"

#include "gui_display_context.h"
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

namespace {
// x autoset places the far bound half of the visible span from the near bound:
// samples_visible / kTrimAutosetVisibleDivisor. Independent of the wheel
// divisor so the autoset span can be tuned on its own.
constexpr int64_t kTrimAutosetVisibleDivisor = 2;
}

// Sets the begin bound at the playhead and autosets the end bound half of the
// visible span LATER. Begin-only by construction: the sole caller is
// handle_trim_x's set arm, which always places begin and pushes end later, so
// there is no side parameter — the partner is always End.
void GuiInputHandler::handle_trim_set_begin_autoset() {
    if (audio.total_frames() <= 0 || audio.sample_rate() <= 0) return;

    const int64_t cand_src =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);

    // End is placed half the visible span LATER than begin.
    const int64_t offset =
        std::max<int64_t>(
            1, samples_visible(app, audio) / kTrimAutosetVisibleDivisor);

    // Absolute walls: both bounds clamp to frame EOF-1 (the unified authored
    // domain [0, total-1]).
    const int64_t total = audio.total_frames();
    const int64_t begin_wall_frame = total - 1;
    const int64_t end_wall_frame   = total - 1;

    // Store the exact frame — trim bounds are whole source frames held in
    // int64_t like marker positions; the .settings writer persists the exact
    // value as integer text (frame_format.h), so a saved bound reloads
    // bit-identically. The playhead is already an int64 frame, so the whole
    // path is integer arithmetic — no double ever enters.
    // Clamp begin to its own wall: the playhead normally sits inside the walls,
    // so this is cheap insurance at the exact edge, keeping the autoset
    // consistent with every other trim gesture.
    int64_t begin_frame = cand_src;
    if (begin_frame < 0)                begin_frame = 0;
    if (begin_frame > begin_wall_frame) begin_frame = begin_wall_frame;
    app.trim.begin_frame = begin_frame;
    app.trim.has_begin   = true;

    const int64_t begin_active =
        source_frame_to_active_domain(app, audio, cand_src);
    int64_t end_active = begin_active + offset;
    // Partner (end) placement clamp: [0, end's own wall mapped through
    // source_frame_to_active_domain] — the unified wall (frame EOF-1) every
    // trim gesture holds. The mapping is monotone, so the active-domain clamp
    // matches the source-domain wall.
    const int64_t end_wall_active =
        source_frame_to_active_domain(app, audio, end_wall_frame);
    if (end_active < 0)               end_active = 0;
    if (end_active > end_wall_active) end_active = end_wall_active;
    app.trim.end_frame = active_domain_to_source_frame(app, audio, end_active);
    app.trim.has_end   = true;

    // Snap the playhead onto the frame of the begin bound just set at it. The
    // bound stores an exact frame, so it can differ
    // from the live playhead sample only in target view via the integer
    // domain round trip. In target view the playback gate checks the
    // playhead against playback's domain offset — this same begin mapped to
    // target frames — so a playhead off the bound's frame can land a sample
    // short of the buffer start (playback.domain_begin()) and Space no-ops.
    // Playhead domain clamp through playhead_image_of_authored_frame (the
    // domain ruling).
    app.playhead_cursor_sample =
        playhead_image_of_authored_frame(app, audio, app.trim.begin_frame);

    // The autoset is a commit: the partner (end) clamp at the tail wall, or a
    // target-view domain round-trip that compresses the offset below one
    // source frame, can land the pair equal, and equal cannot rest. Runs before
    // the invalidations below so the repaint shows the cleared state.
    auto_clear_crossed_trim();

    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}


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
// the x autoset, the Alt drag release, the Ctrl+Alt+wheel end-move, and
// the settings-editor `:trim_*=` commit — calls this after its mutation and
// before its invalidations, so the repaint shows the cleared state.
void GuiInputHandler::auto_clear_crossed_trim() {
    if (app.trim.has_begin && app.trim.has_end &&
        app.trim.end_frame <= app.trim.begin_frame) {
        clear_trim_bounds();
    }
}

// Clear both trim bounds unconditionally. Silent no-op when neither bound is
// set. The caller is handle_trim_x's clear arm. Trim is gesture-owned and
// excluded from undo/redo history.
void GuiInputHandler::handle_trim_clear_both() {
    if (app.trim.has_begin || app.trim.has_end) {
        clear_trim_bounds();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    }
}

// Bare x, context-aware. Playhead exactly on either set bound, or strictly
// inside a fully-set trim pair, clears both bounds; anywhere else sets begin at
// the playhead and autosets end. The coincidence test compares the playhead
// against each bound's playhead_image_of_authored_frame — exactly the value
// landing on a bound assigns the playhead (Home/End are the keyboard routes,
// clicking the bound's column the pointer route; bounds are not Tab stops) —
// because the forward/inverse pair is not a round trip on compressed target
// segments, so "on the bound" means "at the position landing on the bound puts
// the playhead", reachable by construction. The inside test uses the
// UNCLAMPED images (strict betweenness; the edges belong to the coincidence
// arm), needs BOTH bounds (an area). In source view the forward map is
// identity, so the begin compare is the exact integer compare. A single set
// bound clears only by that coincidence, any other position autosets over it.
// Clearing routes through handle_trim_clear_both so the one clear+repaint tail
// is shared.
void GuiInputHandler::handle_trim_x() {
    if (audio.total_frames() <= 0 || audio.sample_rate() <= 0) return;
    const int64_t ph = app.playhead_cursor_sample;
    int64_t begin_active = 0, end_active = 0;
    if (app.trim.has_begin)
        begin_active = source_frame_to_active_domain(app, audio,
                                                     app.trim.begin_frame);
    if (app.trim.has_end)
        end_active = source_frame_to_active_domain(app, audio,
                                                   app.trim.end_frame);
    const bool on_bound =
        (app.trim.has_begin &&
         ph == playhead_image_of_authored_frame(app, audio, app.trim.begin_frame)) ||
        (app.trim.has_end &&
         ph == playhead_image_of_authored_frame(app, audio, app.trim.end_frame));
    const bool inside =
        app.trim.has_begin && app.trim.has_end &&
        ph > begin_active && ph < end_active;
    if (on_bound || inside) { handle_trim_clear_both(); return; }
    handle_trim_set_begin_autoset();
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
    // store is source-domain. Inverse-translate at the boundary, mirroring
    // handle_trim_set_begin_autoset.
    const int64_t src_frame =
        active_domain_to_source_frame(app, audio, domain_frame);
    out_frame = static_cast<double>(src_frame);
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
    app.trim_drag.orig_frame = is_begin ? app.trim.begin_frame
                                          : app.trim.end_frame;
    app.trim_drag.orig_begin_frame = app.trim.begin_frame;
    app.trim_drag.orig_end_frame   = app.trim.end_frame;
    // No pre-drag playhead capture: trim drags never touch the playhead.
    // Grab anchor: each arm captures exactly what its motion path consumes —
    // the pair path reads anchor_active_frame (active-domain, for the rigid
    // both-bounds delta); the single-bound path reads anchor_frame (source-
    // domain press position, motion applying the cursor's displacement from
    // here, mirroring begin_drag's anchor_mouse_time_frame). A bad conversion
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
    // The press only captures drag state; trim drags never touch selection at
    // all (trim is not part of the selection system). A press that never moves
    // commits nothing.
}

void GuiInputHandler::update_trim_drag(int mouse_x) {
    if (!app.trim_drag.active) return;
    if (audio.sample_rate() <= 0 || audio.total_frames() <= 0) return;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;

    if (app.trim_drag.both) {
        int64_t cur_active = 0;
        if (!trim_mouse_x_to_active_frame(mouse_x, cur_active)) return;
        const int64_t ob = source_frame_to_active_domain(
            app, audio, app.trim_drag.orig_begin_frame);
        const int64_t oe = source_frame_to_active_domain(
            app, audio, app.trim_drag.orig_end_frame);
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
        // source_frame_to_active_domain (monotone, so the active-domain clamp
        // matches the source-domain wall). This binds both bounds, so neither
        // slides past EOF under the rigid delta. Crossing stays free (no
        // partner wall).
        const int64_t begin_wall_active =
            source_frame_to_active_domain(app, audio, audio.total_frames() - 1);
        const int64_t end_wall_active =
            source_frame_to_active_domain(app, audio, audio.total_frames() - 1);
        if (ob + df < 0)                 df = -ob;
        if (oe + df < 0)                 df = -oe;
        if (ob + df > begin_wall_active) df = begin_wall_active - ob;
        if (oe + df > end_wall_active)   df = end_wall_active - oe;
        // active_domain_to_source_frame already lands on whole int64 frames,
        // so the path stays integer arithmetic end to end. These are
        // mid-gesture tracking values; the release in commit_trim_drag snaps
        // each moved bound to its painted column's authored time.
        int64_t nb = active_domain_to_source_frame(app, audio, ob + df);
        int64_t ne = active_domain_to_source_frame(app, audio, oe + df);
        if (nb < 0) nb = 0;
        if (ne < 0) ne = 0;
        if (app.trim.begin_frame != nb || app.trim.end_frame != ne) {
            app.trim.begin_frame = nb;
            app.trim.end_frame   = ne;
            app.trim_drag.moved    = true;
            // Trim drags never move the playhead — like the trim wheel, the
            // gesture is playhead-independent. Motion updates the bounds and
            // repaints; the playhead stays where it is.
            viewport.invalidate_waveform_area();
            viewport.invalidate_timestamp_area();
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
    // is already viewport-bound, but a grab a few pixels off the stem can
    // trail the bound past the edge; this makes the bound itself exact. The
    // grab can only begin on a visible bound (hit_test_trim_boundary gates to
    // visible columns), so this is a live tracking clamp, not a correction for
    // an offscreen grab. The bounds are active-domain while src_frame is
    // source, so inverse-translate the edges — monotonic, so the source clamp
    // matches the active-pixel one.
    const auto vb = viewport_marker_bounds(app, audio);
    const int64_t vp_lo = active_domain_to_source_frame(app, audio, vb.first);
    const int64_t vp_hi = active_domain_to_source_frame(app, audio, vb.second);
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
        // Trim drags never move the playhead — like the trim wheel, the gesture
        // is playhead-independent (a recorded difference from the marker drag,
        // which tracks its grabbed marker). Motion updates the bound and
        // repaints; the playhead stays where it is.
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
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
        // The map is the display context's own — identity in source view,
        // the live cached map in target view — as the trim-end wheel move
        // anchors: markers freeze a pre-drag map because a
        // warp drag deforms it, but trim never enters
        // build_target_view_warp_frame_map, so the live map is stable
        // across the drag. The trim stems paint
        // through the waveform cache's baked map (fp_warp_frame_map), which
        // can LAG the live cache inside an async waveform-rebuild window
        // (e.g. a trim grab immediately after a tempo commit); inside that
        // window stored-equals-shown holds only transiently, converging when
        // the rebuild lands — the same displayed-vs-live nuance the trim-end
        // wheel anchor shares. The absolute walls
        // — both bounds 0..EOF-1, plain integer compares —
        // re-apply AFTER the snap so the walls win over the pixel grid and a
        // wall-clamped release rests exactly on its wall. Degenerate paint
        // geometry (no strip width / zoom, unloaded audio) skips the snap and
        // keeps the tracked value: trim has no undo, so routing a bound
        // through the helpers' 0-fallback would be unrecoverable.
        const int sr = audio.sample_rate();
        if (sr > 0 && audio.total_frames() > 0 &&
            current_samples_per_pixel(app, audio) > 0.0) {
            const auto& map = *active_display_context(app, audio).warp_frame_map;
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
            // Trim drags never move the playhead (like the trim wheel), so the
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
    }
    app.trim_drag = TrimDragState{};
}

void GuiInputHandler::wheel_move_trim_end(GuiMouseButton button, int count) {
    // Refused in read-only (the trim-end move was read-only-mobile while
    // trim was one unified setting across both tabs; with per-tab trim that
    // rationale is gone, so the bound refuses exactly like the marker tempo
    // nudge, a silent no-op).
    if (active_view_state(app).read_only) return;
    // The Ctrl+Alt+wheel entry is selection-free, so this owns the trim
    // presence refusal: moving a nonexistent bound would write the stale
    // end_frame field. The end-move, like every trim gesture, requires the
    // FULL pair set; a lone bound is gesture-inert, so both has_begin and
    // has_end are required.
    if (!(app.trim.has_begin && app.trim.has_end)) return;
    const int sr = audio.sample_rate();
    if (audio.total_frames() <= 0 || sr <= 0) return;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;
    // Pixel-column-anchored end-move, marker-identical to the
    // nudges (the derivation and the exact-painted-move rationale
    // live at nudge_selected_markers): read the end bound's
    // currently painted column, step it by the wheel's
    // whole-column step, and commit that column's time — source
    // view: viewport start plus column times samples-per-pixel;
    // target view: the column's target-domain time inverse-mapped
    // through the display context's cached map — through
    // snap_authored_frame
    // (inside authored_frame_at_column), so the stored bound is a
    // whole source frame. The step keeps its samples_visible /
    // kTrimEndWheelDivisor magnitude, expressed as whole pixel
    // columns per detent so each detent's painted move is exact
    // and no sub-column residue accumulates across detents. The
    // end bound clamps to its absolute walls — floor 0,
    // ceiling at frame EOF-1 (the unified authored domain);
    // plain integer compares, the load
    // guard's own comparison, applied AFTER the column snap so
    // the walls win over the pixel grid. There is no
    // partner wall — the end bound crosses the begin bound freely
    // and the begin bound is untouched here — but each wheel frame
    // is a commit, so a move landing the end on or before the
    // begin destroys both bounds (auto_clear_crossed_trim). The
    // zero floor is the walls' lower end, kept for
    // representability — a negative position is unrepresentable in
    // the authored frame form the .settings file persists.
    const int64_t step = std::max<int64_t>(
        1, samples_visible(app, audio) / kTrimEndWheelDivisor);
    const int64_t step_cols = std::max<int64_t>(
        1, static_cast<int64_t>(std::nearbyint(
               static_cast<double>(step) / spp)));
    const int64_t dcols =
        (button == GuiMouseButton::WheelUp ? -step_cols : +step_cols) *
        count;
    const auto& map = *active_display_context(app, audio).warp_frame_map;
    const int c = painted_column_of_source_frame(
        app, audio, static_cast<double>(app.trim.end_frame), map);
    int64_t v = authored_frame_at_column(
        app, audio, c + static_cast<int>(dcols), map);
    if (v < 0) v = 0;
    const int64_t end_wall = audio.total_frames() - 1;
    if (v > end_wall) v = end_wall;
    app.trim.end_frame = v;
    // Commit auto-clear (ruling at auto_clear_crossed_trim), before
    // the invalidations so the repaint shows the cleared state.
    auto_clear_crossed_trim();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

// Alt left press trim routing, consulted by the Alt+drag branch AFTER its
// marker hit test misses. Every trim drag requires the FULL pair set; a lone
// bound is gesture-inert — transparent to the press, which falls through to
// pan/no-op like a plain press over a bound. Returns true iff both bounds are
// set AND the press landed on trim geometry (a stem/chip single-bound hit, or
// the top-strip inter-chip pair region) — armed or not — so the caller CLAIMS
// the press (no pan fallback); false lets the caller fall through to the pan.
// Markers BEAT trim: the caller runs the marker
// hit test first, so a marker within its halo standing in a trim zone wins (the
// contested bound still has its dedicated upper-row chip as an unambiguous
// handle). Trim bounds are transparent to every OTHER chord (the plain/Shift
// press path never consults trim). Read-only claims WITHOUT arming (a silent
// return, no pan fallback). The three arms:
//   WAVEFORM: a press within kMarkerHitHalfPx of a SET bound's painted column
//     (visible columns only, via hit_test_trim_boundary) begins that bound's
//     single drag. The waveform between-region deliberately does NOT pair-drag
//     under Alt — it stays the caller's pan (pan is Alt's core waveform gesture,
//     and a trim span can cover the whole view); the pair handle is the
//     top-strip span between the chips. So an unclaimed waveform press pans.
//   TOP STRIP: a chip-rect hit (hit_test_trim_chip) begins that bound's single
//     drag; else, the press column strictly between the two bound columns
//     begins the pair drag — both bounds are the subject (no grabbed-bound
//     notion; the pair has no viewport clamp and, like every trim gesture,
//     never moves the playhead).
// Both arms presuppose the full pair (the gate above): a lone bound arms
// nothing — it is gesture-inert. Trim drags never touch selection.
//
// The pair-region bound columns come from displayed_or_live_target_map — the
// map the on-screen chips were painted with on the LAST COMMITTED frame (the
// item rebuilds only STAGE; the paint pass PROMOTES at the committing frame, not
// at the earlier plate publish) — so an Alt hit lands on what is drawn: the
// routing decision flips at the exact instant the on-screen items flip (the
// event-sync ruling at that selector). What was the open live-vs-painted window
// for item hits is CLOSED to commit granularity. The remaining seams are all
// ACCEPTED: commit-to-scanout plus human reaction (irreducible — input responds
// to the previously presented frame), the COLD-STATE fallback (first paint, a
// view toggle, or just after load, live map until the first committed target
// frame), and the playhead-placement clicks (column-based, out of scope by
// ruling — a far subtler seam).
bool GuiInputHandler::route_trim_alt_press(int mouse_x, int mouse_y,
                                           bool inside_top) {
    if (audio.total_frames() <= 0) return false;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return false;
    // Every trim drag needs the full pair set. With a lone bound, trim
    // contributes NO pointer geometry at all — the press is transparent and
    // falls through to the caller's pan/no-op, exactly like a plain press over
    // a bound.
    if (!(app.trim.has_begin && app.trim.has_end)) return false;

    // Single-drag hit: the waveform uses the visible-column halo test; the top
    // strip uses the chip rect. Both surfaces keep their single arms.
    const TrimHit single = inside_top
        ? hit_test_trim_chip(app, audio, mouse_x, mouse_y)
        : hit_test_trim_boundary(app, audio, mouse_x);
    if (single != TrimHit::None) {
        // Read-only claims the press but never arms (no pan fallback), exactly
        // as the marker reposition arm refuses read-only.
        if (active_view_state(app).read_only) return true;
        begin_trim_drag(single, mouse_x);
        return true;
    }

    // Pair drag: TOP STRIP ONLY — the press strictly between the two chips'
    // columns (both bounds are guaranteed set by the gate above). The pair has
    // no grabbed-bound notion —
    // both bounds are the subject, so it always arms as Begin structurally
    // (there is no nearer-bound pick, and the gesture never moves the playhead).
    // The bound columns use the same forward-map + column math
    // hit_test_trim_boundary uses, on the painted items' own map (the
    // displayed_or_live_target_map ruling above), computed only on this path.
    if (inside_top) {
        const GuiRect area = waveform_area(app);
        const int click_rel_x = mouse_x - area.x;
        const double vp = static_cast<double>(app.viewport_start_sample);
        const std::vector<WarpFrameMapSegment>& dmap =
            displayed_or_live_target_map(app, audio);
        const std::vector<WarpFrameMapSegment>* map =
            dmap.empty() ? nullptr : &dmap;
        auto bound_col = [&](int64_t frame) -> int {
            double ms = static_cast<double>(frame);
            if (map) {
                const size_t q = (frame < 0) ? static_cast<size_t>(0)
                                             : static_cast<size_t>(frame);
                ms = std::nearbyint(map_source_to_target(q, *map));
            }
            return static_cast<int>(std::nearbyint((ms - vp) / spp));
        };
        const int bcol = bound_col(app.trim.begin_frame);
        const int ecol = bound_col(app.trim.end_frame);
        const int lo = std::min(bcol, ecol);
        const int hi = std::max(bcol, ecol);
        if (click_rel_x > lo && click_rel_x < hi) {
            // Read-only claims the pair region but never arms (no pan fallback).
            if (active_view_state(app).read_only) return true;
            begin_trim_drag(TrimHit::Begin, mouse_x, /*both=*/true);
            return true;
        }
    }
    return false;
}
