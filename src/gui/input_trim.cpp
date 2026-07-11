#include "input_handler.h"

#include "warp_frame_map_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

// Trim gestures (architect-ruled hardfail model; the full ruling sits at the
// TrimState store in app_state.h): begin and end are authored named roles.
// Every gesture clamps each bound to its own absolute walls — begin from
// frame 0 to EOF-1, end from frame 0 to EOF exactly (end-at-EOF is a valid
// render, so the GUI must represent it). There are no partner walls: a bound
// crosses its partner freely during any gesture and may rest inverted or
// equal; no gesture guards against zero-length windows. The per-bound wall
// split (begin EOF-1 vs end EOF) is a recorded trim-vs-marker asymmetry — it
// replaces the older no-walls asymmetry, now dead — sitting beside the marker
// nudges' single EOF wall. Every wall check is a plain integer compare
// — literally the load guard's comparison. The zero floor is subsumed by the
// walls but remains the reason the floor exists at all: a negative position
// is unrepresentable in the authored frame form the .settings file persists
// (parse_authored_frame rejects negatives as malformed) — a format-
// representability floor, not a spacing or validity rule. Past-EOF bounds
// are unreachable: the gesture walls forbid authoring one, and the load
// boundary (file_loader / CLI) hard-fails a past-EOF bound in a hand-edited
// .settings as adversarial input (a .settings applies only to its own
// audio). validate_trim_frames (trimmer.h) owns every trim refusal at the
// render boundary and the target-view gate, and stays the breach backstop
// for hand-edited artifacts.

namespace {
// x autoset places the far bound half of the visible span from the near bound:
// samples_visible / kTrimAutosetVisibleDivisor. Independent of the wheel
// divisor so the autoset span can be tuned on its own.
constexpr int64_t kTrimAutosetVisibleDivisor = 2;
}

// Plain x. Sets the begin bound at the playhead and autosets the end bound
// half of the visible span away. dir carries the asymmetry: Begin pushes End
// later, End pushes Begin earlier.
void GuiInputHandler::handle_trim_set_autoset(TrimSide side) {
    if (audio.total_frames() <= 0 || audio.sample_rate() <= 0) return;
    const int64_t dir = (side == TrimSide::Begin) ? 1 : -1;

    bool&    this_has    = (side == TrimSide::Begin) ? app.trim.has_begin   : app.trim.has_end;
    int64_t& this_bound  = (side == TrimSide::Begin) ? app.trim.begin_frame : app.trim.end_frame;
    bool&    other_has   = (side == TrimSide::Begin) ? app.trim.has_end     : app.trim.has_begin;
    int64_t& other_bound = (side == TrimSide::Begin) ? app.trim.end_frame   : app.trim.begin_frame;

    const int64_t cand_src =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);

    const int64_t offset =
        dir * std::max<int64_t>(
                  1, samples_visible(app, audio) / kTrimAutosetVisibleDivisor);

    // Per-bound absolute walls: begin at frame EOF-1, end at frame EOF. side
    // Begin sets begin here and places End as the partner; side End sets end
    // and places Begin.
    const int64_t total = audio.total_frames();
    const int64_t this_wall_frame  = (side == TrimSide::Begin) ? total - 1 : total;
    const int64_t other_wall_frame = (side == TrimSide::Begin) ? total : total - 1;

    // Store the exact frame — trim bounds are whole source frames held in
    // int64_t like marker positions; the .settings writer persists the exact
    // value as integer text (frame_format.h), so a saved bound reloads
    // bit-identically. The playhead is already an int64 frame, so the whole
    // path is integer arithmetic — no double ever enters.
    // Clamp the primary bound to its own wall: the playhead normally sits
    // inside the walls, so this is cheap insurance at the exact edge, keeping
    // the autoset consistent with every other trim gesture.
    int64_t this_frame = cand_src;
    if (this_frame < 0)               this_frame = 0;
    if (this_frame > this_wall_frame) this_frame = this_wall_frame;
    this_bound = this_frame;
    this_has   = true;
    const int64_t this_active =
        source_frame_to_active_domain(app, audio, cand_src);
    int64_t other_active = this_active + offset;
    // Partner placement clamp: [0, the partner's own wall mapped through
    // source_frame_to_active_domain] — the same per-bound wall every trim
    // gesture holds (End partner wall frame EOF, Begin partner wall frame
    // EOF-1), no longer a placement-only choice. The mapping is monotone, so
    // the active-domain clamp matches the source-domain wall.
    const int64_t other_wall_active =
        source_frame_to_active_domain(app, audio, other_wall_frame);
    if (other_active < 0)                 other_active = 0;
    if (other_active > other_wall_active) other_active = other_wall_active;
    other_bound = active_domain_to_source_frame(app, audio, other_active);
    other_has = true;

    // Snap the playhead onto the frame of the bound just set at it. The
    // bound stores an exact frame, so it can differ
    // from the live playhead sample only in target view via the integer
    // domain round trip. In target view the playback gate checks the
    // playhead against playback's domain offset — this same begin mapped to
    // target frames — so a playhead off the bound's frame can land a sample
    // short of the buffer start (playback.domain_begin()) and Space no-ops.
    // Playhead domain clamp, mirroring move_playhead_to (the domain ruling
    // lives there): a pin onto trim end at total rests at total - 1.
    {
        int64_t pin = source_frame_to_active_domain(app, audio, this_bound);
        const int64_t live_total = live_total_frames(app, audio);
        if (pin < 0) pin = 0;
        if (live_total > 0 && pin >= live_total) pin = live_total - 1;
        app.playhead_cursor_sample = pin;
    }

    app.trim_begin_selected = true;
    app.trim_end_selected   = true;
    app.last_selected_trim  = 'B';
    app.last_sel_group      = LastSelGroup::Trim;

    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
    // Trim commit site (trim pushes no history, so the flag is set here
    // rather than in undo.cpp's push funnel): the autoset can place equal
    // bounds at the head or tail edge, where the wall clamps collapse the
    // partner offset onto the primary bound.
    app.defect_series.pending_validation = PendingValidation::Commit;
}

void GuiInputHandler::handle_trim_set_begin_autoset() {
    handle_trim_set_autoset(TrimSide::Begin);
}


void GuiInputHandler::handle_trim_unset(TrimSide side) {
    bool&    this_has   = (side == TrimSide::Begin) ? app.trim.has_begin   : app.trim.has_end;
    int64_t& this_bound = (side == TrimSide::Begin) ? app.trim.begin_frame : app.trim.end_frame;
    bool&    this_sel   = (side == TrimSide::Begin) ? app.trim_begin_selected : app.trim_end_selected;
    if (!this_has) return;
    this_has   = false;
    this_bound = 0;
    this_sel     = false;  // an unset bound can't stay selected
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
    // Not a commit-funnel site: an unset cannot create a crossed-or-equal
    // pair — crossed needs both bounds set.
}

// Shift+x: clear both trim bounds unconditionally. Silent no-op when neither
// bound is set. Trim is gesture-owned and excluded from undo/redo history.
void GuiInputHandler::handle_trim_clear_both() {
    if (app.trim.has_begin || app.trim.has_end) {
        app.trim.has_begin      = false;
        app.trim.has_end        = false;
        app.trim.begin_frame  = 0;
        app.trim.end_frame    = 0;
        app.trim_begin_selected = false;
        app.trim_end_selected   = false;
        app.last_selected_trim  = 0;
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    }
}

// --- Trim boundary mouse gestures ---------------------------------------

void GuiInputHandler::select_trim_boundary(TrimHit which, bool additive) {
    if (which == TrimHit::None) return;
    bool& this_sel  = (which == TrimHit::Begin) ? app.trim_begin_selected
                                                : app.trim_end_selected;
    bool& other_sel = (which == TrimHit::Begin) ? app.trim_end_selected
                                                : app.trim_begin_selected;
    const char which_char = (which == TrimHit::Begin) ? 'B' : 'E';
    if (additive) {
        // Toggle this bound's membership; leave the other bound as-is.
        this_sel = !this_sel;
        app.last_selected_trim = this_sel ? which_char : 0;
    } else {
        // Single-select within the trim group: this bound on, other off.
        this_sel  = true;
        other_sel = false;
        app.last_selected_trim = which_char;
        // A fresh sole selection in the trim group drops marker selection —
        // orthogonal groups, but a single-select in one clears the other
        // (the symmetric counterpart of set_single_selection clearing trim).
        if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
            app.selected_markers.clear();
            app.last_selected_marker = -1;
            viewport.invalidate_top_strip();
        }
    }
    app.last_sel_group = LastSelGroup::Trim;
    viewport.invalidate_waveform_area();
}

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
    // handle_trim_set_autoset.
    const int64_t src_frame =
        active_domain_to_source_frame(app, audio, domain_frame);
    out_frame = static_cast<double>(src_frame);
    return true;
}

void GuiInputHandler::begin_trim_drag(TrimHit which, int mouse_x, bool both) {
    if (which == TrimHit::None) return;
    const bool is_begin = (which == TrimHit::Begin);
    if (is_begin ? !app.trim.has_begin : !app.trim.has_end) {
        return;
    }
    if (both && !(app.trim.has_begin && app.trim.has_end)) return;
    app.trim_drag.active       = true;
    app.trim_drag.is_begin     = is_begin;
    app.trim_drag.both         = both;
    app.trim_drag.moved        = false;
    app.trim_drag.orig_frame = is_begin ? app.trim.begin_frame
                                          : app.trim.end_frame;
    app.trim_drag.orig_begin_frame = app.trim.begin_frame;
    app.trim_drag.orig_end_frame   = app.trim.end_frame;
    // Grab anchor: the press position in source-domain frames. Motion moves
    // the bound by the cursor's displacement from here, so it tracks the grab
    // point with no snap (mirrors begin_drag's anchor_mouse_time_frame).
    // A bad conversion leaves anchor_frame at 0; harmless since the same
    // unusable state makes update_trim_drag early-return too.
    double anchor = 0.0;
    if (trim_mouse_x_to_source_frame(mouse_x, anchor))
        app.trim_drag.anchor_frame = anchor;
    app.last_sel_group         = LastSelGroup::Trim;
    if (both) {
        app.trim_begin_selected = true;
        app.trim_end_selected   = true;
        app.last_selected_trim  = is_begin ? 'B' : 'E';
        int64_t af = 0;
        if (trim_mouse_x_to_active_frame(mouse_x, af))
            app.trim_drag.anchor_active_frame = af;
    }
}

void GuiInputHandler::update_trim_drag(int mouse_x) {
    if (!app.trim_drag.active) return;
    if (audio.sample_rate() <= 0 || audio.total_frames() <= 0) return;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;

    // Anchor-relative motion: the dragged bound moves by the cursor's
    // displacement from the grab point, not to the absolute cursor column.
    // cursor_frame is converted identically to the begin-drag anchor, so
    // the bound stays the same distance under the cursor for the whole drag.
    double cursor_frame = 0.0;
    if (!trim_mouse_x_to_source_frame(mouse_x, cursor_frame)) return;
    const double delta_frames = cursor_frame - app.trim_drag.anchor_frame;

    if (app.trim_drag.both) {
        int64_t cur_active = 0;
        if (!trim_mouse_x_to_active_frame(mouse_x, cur_active)) return;
        const int64_t ob = source_frame_to_active_domain(
            app, audio, app.trim_drag.orig_begin_frame);
        const int64_t oe = source_frame_to_active_domain(
            app, audio, app.trim_drag.orig_end_frame);
        int64_t df = cur_active - app.trim_drag.anchor_active_frame;
        // Keep the grabbed bound — and the playhead pinned to it — inside the
        // visible pixel span, matching the playhead's own first/last-visible
        // clamp. Applied before the clip clamp so trim validity (0 / EOF) wins
        // in the rare case the window is wider than the viewport. This also
        // self-corrects a blind offscreen-halo grab: a bound grabbed up to
        // kMarkerHitHalfPx past an edge snaps into view on the first motion.
        const GuiRect area = waveform_area(app);
        const int64_t first_vis = app.viewport_start_sample;
        const int64_t last_vis  = app.viewport_start_sample +
            static_cast<int64_t>(std::nearbyint((area.w - 1) * spp));
        const int64_t grabbed = app.trim_drag.is_begin ? ob : oe;
        if (grabbed + df < first_vis) df = first_vis - grabbed;
        if (grabbed + df > last_vis)  df = last_vis  - grabbed;
        // Wall the rigid delta so BOTH bounds respect their own absolute
        // walls: floor 0 on each and per-bound ceilings — begin at frame
        // EOF-1, end at frame EOF — mapped through source_frame_to_active_
        // domain (monotone, so the active-domain clamp matches the source-
        // domain wall). Only the grabbed bound is viewport-clamped (above);
        // this wall clamp binds the partner too, so the partner no longer
        // slides past EOF under the rigid delta. Crossing stays free (no
        // partner wall).
        const int64_t begin_wall_active =
            source_frame_to_active_domain(app, audio, audio.total_frames() - 1);
        const int64_t end_wall_active =
            source_frame_to_active_domain(app, audio, audio.total_frames());
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
            const int64_t grabbed_src = app.trim_drag.is_begin ? nb : ne;
            // Playhead domain clamp, mirroring move_playhead_to (the
            // ruling lives there): a grabbed end riding at total pins the
            // playhead to total - 1.
            int64_t pin = source_frame_to_active_domain(app, audio, grabbed_src);
            const int64_t live_total = live_total_frames(app, audio);
            if (pin < 0) pin = 0;
            if (live_total > 0 && pin >= live_total) pin = live_total - 1;
            app.playhead_cursor_sample = pin;
            viewport.invalidate_waveform_area();
            viewport.invalidate_timestamp_area();
        }
        return;
    }

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
    // trail the bound past the edge; this makes the bound itself exact. This is
    // also what makes a blind offscreen-halo grab self-correcting: a bound
    // grabbed up to kMarkerHitHalfPx past an edge snaps into the visible strip
    // on the first motion. The bounds are active-domain while src_frame is
    // source, so inverse-translate the edges — monotonic, so the source clamp
    // matches the active-pixel one.
    const auto vb = viewport_marker_bounds(app, audio);
    const int64_t vp_lo = active_domain_to_source_frame(app, audio, vb.first);
    const int64_t vp_hi = active_domain_to_source_frame(app, audio, vb.second);
    if (src_frame < vp_lo) src_frame = vp_lo;
    if (src_frame > vp_hi) src_frame = vp_hi;

    // Structural wall, applied AFTER the viewport clamp so the wall wins
    // where both bind (matching the marker-drag model where structural walls
    // compose with the viewport gate): begin clamps to frame EOF-1, end to
    // frame EOF exactly (end-at-EOF is a valid render). No partner wall — the
    // bound crosses its partner freely and rests wherever released. The floor
    // 0 is already held by the viewport clamp (the visible strip starts at or
    // after frame 0), so the 0.0 format-representability floor holds by
    // construction here.
    const int64_t wall_hi = app.trim_drag.is_begin
        ? audio.total_frames() - 1
        : audio.total_frames();
    if (src_frame > wall_hi) src_frame = wall_hi;
    // Mid-gesture tracking value: int64 throughout (the store cannot hold a
    // fractional frame), but pointer-derived, not column-canonical — the
    // release in commit_trim_drag snaps a moved bound to its painted
    // column's authored time, superseding this value.
    const int64_t new_frame = src_frame;
    int64_t& field = app.trim_drag.is_begin ? app.trim.begin_frame
                                            : app.trim.end_frame;
    if (field != new_frame) {
        const bool first_motion = !app.trim_drag.moved;
        field = new_frame;
        app.trim_drag.moved = true;
        // First-motion selection collapse: a real drag focuses the whole
        // selection on the dragged bound. Delegated to select_trim_boundary
        // (non-additive) — the same helper a trim click uses — so the rule
        // (select the dragged bound, drop the opposite bound AND any
        // warp/phase-reset marker selection, make Trim the active group)
        // lives in one place. Motion-gated so a Ctrl+click without motion is
        // left to commit_trim_drag's no-motion toggle branch.
        if (first_motion) {
            select_trim_boundary(
                app.trim_drag.is_begin ? TrimHit::Begin : TrimHit::End,
                /*additive=*/false);
        }
        // Track the playhead on the dragged bound for the whole drag,
        // mirroring the warp marker-drag tracking in the motion handler
        // (right after apply_drag_motion): set app.playhead_cursor_sample
        // DIRECTLY rather than via move_playhead_to, so the viewport is
        // deliberately not followed — the user pans manually if the drag
        // runs past the edge. move_playhead_to would scroll when an
        // off-center grab pushes the bound a few pixels past the visible
        // edge; the marker drag never scrolls, and symmetry is the point.
        // Trim is a render-time cut and is NOT in build_target_view_warp_frame_map,
        // so the bound carries no deformation: new_frame (source-domain)
        // maps straight to the playhead, inverse-translated to target-domain
        // in target view. No predictor resync and no scanner-sample sync,
        // both matching the marker-drag block — trim drag stopped playback at
        // begin, so the scanner is inactive and a play reseeks from the
        // cursor. The invalidate_waveform_area below repaints the playhead
        // columns along with the moved trim shading.
        // Playhead domain clamp, mirroring move_playhead_to (the ruling
        // lives there): an end dragged to total pins the playhead to
        // total - 1.
        int64_t sample = source_frame_to_active_domain(app, audio, new_frame);
        const int64_t live_total = live_total_frames(app, audio);
        if (sample < 0) sample = 0;
        if (live_total > 0 && sample >= live_total) sample = live_total - 1;
        app.playhead_cursor_sample = sample;
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
        // pair's span may deform by up to one frame at release — the same
        // accepted behavior multi-marker drags have (the constant-gap phrasing
        // at TrimDragState describes the mid-gesture active-domain motion).
        // The map is the live cached one, as the trim nudge and trim-end
        // wheel anchor: markers freeze a pre-drag map because a warp drag
        // deforms it, but trim never enters build_target_view_warp_frame_map,
        // so the live map is stable across the drag. The trim stems paint
        // through the waveform cache's baked map (fp_warp_frame_map), which
        // can LAG the live cache inside an async waveform-rebuild window
        // (e.g. a trim grab immediately after a tempo commit); inside that
        // window stored-equals-shown holds only transiently, converging when
        // the rebuild lands — the same displayed-vs-live nuance the nudge and
        // wheel anchors share. The per-bound absolute walls
        // — begin 0..EOF-1, end 0..EOF exactly, plain integer compares —
        // re-apply AFTER the snap so the walls win over the pixel grid and a
        // wall-clamped release rests exactly on its wall. Degenerate paint
        // geometry (no strip width / zoom, unloaded audio) skips the snap and
        // keeps the tracked value: trim has no undo, so routing a bound
        // through the helpers' 0-fallback would be unrecoverable.
        const int sr = audio.sample_rate();
        if (sr > 0 && audio.total_frames() > 0 &&
            current_samples_per_pixel(app, audio) > 0.0) {
            const std::vector<WarpFrameMapSegment> no_map;
            const auto& map = (app.active_audio_view == 'T')
                ? target_view_warp_frame_map_cached(
                      app, sr,
                      static_cast<long>(audio.total_frames())).warp_frame_map
                : no_map;
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
                             audio.total_frames());
            // Keep the playhead pinned to the grabbed bound across the snap,
            // exactly as the motion handler pinned it all drag: a direct set
            // (no move_playhead_to, so no scroll), recomputing the same value
            // when the snap was a no-op. Playhead domain clamp, mirroring
            // move_playhead_to (the ruling lives there): trim end is legal
            // at total — an exclusive bound — so a commit releasing the end
            // on the total deliberately rests the playhead at total - 1.
            const int64_t grabbed_src = app.trim_drag.is_begin
                ? app.trim.begin_frame : app.trim.end_frame;
            int64_t pin = source_frame_to_active_domain(app, audio, grabbed_src);
            const int64_t live_total = live_total_frames(app, audio);
            if (pin < 0) pin = 0;
            if (live_total > 0 && pin >= live_total) pin = live_total - 1;
            app.playhead_cursor_sample = pin;
        }
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
        // Trim commit site (see handle_trim_set_autoset): the release of a
        // drag that moved a bound is the commit; the modal series opens on
        // the next tick, never mid-gesture.
        app.defect_series.pending_validation = PendingValidation::Commit;
    } else if (!app.trim_drag.both) {
        // Ctrl+press with no motion is a Ctrl+click: toggle the boundary's
        // selection (additive — coexists with marker selection).
        const TrimHit which = app.trim_drag.is_begin ? TrimHit::Begin
                                                      : TrimHit::End;
        select_trim_boundary(which, /*additive=*/true);
    }
    app.trim_drag = TrimDragState{};
}

void GuiInputHandler::delete_selected_trim() {
    if (app.trim_begin_selected && app.trim.has_begin) {
        handle_trim_unset(TrimSide::Begin);
    }
    if (app.trim_end_selected && app.trim.has_end) {
        handle_trim_unset(TrimSide::End);
    }
    app.trim_begin_selected = false;
    app.trim_end_selected   = false;
}

// Ctrl+Left / Ctrl+Right on the trim group. The sibling of
// nudge_selected_markers: pixel-column-anchored, exactly one painted
// column per press — the bound's currently painted column (the trim stem
// painter's own math) steps to the adjacent column and that column's
// time commits through snap_authored_frame, so the stored bound is a
// whole source frame; the one-column-per-press guarantee and its numeric
// rationale live in the comment at nudge_selected_markers. Each bound
// clamps to its own absolute walls — begin to frame EOF-1, end to frame
// EOF (floor 0 on both); this per-bound wall split is the current
// recorded trim-vs-marker asymmetry (marker nudges keep a single EOF
// wall, total frames minus one source frame). The integer walls win over
// the pixel grid, so a wall-clamped press rests exactly on its wall.
// There is still no partner wall: the bound crosses its partner freely.
// The render boundary owns trim validity.
void GuiInputHandler::nudge_selected_trim(int direction) {
    if (app.loading || audio.total_frames() <= 0) return;
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    // The nudge moves the playhead onto the bound; stop playback first.
    playback_lifecycle.stop_playback_if_playing();

    // Fine-tuning collapse, mirroring the marker nudge's collapse to the
    // focused marker: the nudge acts on ONE bound. The focused bound is the
    // one app.last_selected_trim names when that bound is selected and set;
    // otherwise the sole selected-and-set bound; otherwise there is nothing
    // to nudge.
    const bool begin_ok = app.trim_begin_selected && app.trim.has_begin;
    const bool end_ok   = app.trim_end_selected   && app.trim.has_end;
    TrimHit which = TrimHit::None;
    if (app.last_selected_trim == 'B' && begin_ok)      which = TrimHit::Begin;
    else if (app.last_selected_trim == 'E' && end_ok)   which = TrimHit::End;
    else if (begin_ok && !end_ok)                       which = TrimHit::Begin;
    else if (end_ok && !begin_ok)                       which = TrimHit::End;
    else return;
    // Collapse the selection to the focused bound via the same helper click
    // and drag use — it single-selects this bound, drops marker selection,
    // and keeps group Trim.
    select_trim_boundary(which, /*additive=*/false);

    int64_t& field = (which == TrimHit::Begin) ? app.trim.begin_frame
                                               : app.trim.end_frame;
    const int64_t cur = field;
    if (current_samples_per_pixel(app, audio) <= 0.0) return;

    // Marker-identical pixel anchoring (nudge_selected_markers' exact
    // shape): read the bound's currently painted column, target the
    // adjacent column, and commit that column's time — source view:
    // viewport start plus column times samples-per-pixel; target view:
    // the column's target-domain time inverse-mapped through the cached
    // map — through snap_authored_frame (inside authored_frame_at_column).
    const std::vector<WarpFrameMapSegment> no_map;
    const auto& map = (app.active_audio_view == 'T')
        ? target_view_warp_frame_map_cached(
              app, sr, static_cast<long>(audio.total_frames())).warp_frame_map
        : no_map;
    const int c = painted_column_of_source_frame(
        app, audio, static_cast<double>(cur), map);
    int64_t proposed = authored_frame_at_column(app, audio, c + direction, map);

    // Per-bound absolute walls (mirroring nudge_selected_markers' EOF wall
    // in shape — total frames minus one source frame — but clamping in
    // both views, where the marker nudge refuses in target view): begin
    // walls at frame EOF-1, end at frame EOF exactly — plain integer
    // compares, the load guard's own comparison, applied AFTER the column
    // snap so the walls win over the pixel grid. The zero floor is
    // the walls' lower end, kept for representability; there is still no
    // partner wall (see the function-head comment).
    const int64_t wall = (which == TrimHit::Begin)
        ? audio.total_frames() - 1
        : audio.total_frames();
    if (proposed < 0)    proposed = 0;
    if (proposed > wall) proposed = wall;
    if (proposed == cur) return;

    // Trim is gesture-owned and excluded from undo/redo history (as with every
    // other trim gesture; see handle_trim_clear_both).
    field = proposed;

    // The same invalidation set the trim drag's motion branch emits.
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
    // Trim commit site (see handle_trim_set_autoset): each nudge press is
    // its own commit, like the marker nudge's per-press history entry.
    app.defect_series.pending_validation = PendingValidation::Commit;

    // No viewport gate, mirroring the marker nudge (drags viewport-clamp the
    // grabbed item; nudges do not). Track the playhead onto the bound through
    // move_playhead_to's edge-follow path — at most one pixel of scroll,
    // keeping the bound just inside the edge.
    viewport.move_playhead_to(
        source_frame_to_active_domain(app, audio, field));
}

void GuiInputHandler::handle_trim_boundary_press(TrimHit which, bool ctrl,
                                                 bool shift, int mouse_x) {
    // The caller consumes a trim press only for recognized gestures: a
    // Ctrl-exact reposition-drag, a Ctrl+Shift move-both-bounds drag, or a
    // plain / Shift select+navigate. Alt is filtered upstream, so `ctrl` /
    // `shift` here are the exact chords and the else-branch is a
    // plain-or-Shift select.
    if (which == TrimHit::None) return;
    if (ctrl && shift) {
        begin_trim_drag(which, mouse_x, /*both=*/true);
        return;
    }
    if (ctrl) {
        begin_trim_drag(which, mouse_x);
        return;
    }
    select_trim_boundary(which, /*additive=*/shift);
    // Mirror the marker flag/stem click: a plain or Shift click on a trim
    // boundary moves the playhead cursor to that boundary, so trim flags
    // navigate exactly like marker flags. The Ctrl branch above is a
    // reposition-drag grab and intentionally does not move the playhead,
    // matching the Ctrl+marker reposition. The hit-test that routed here only
    // fires when the boundary exists, so trim_begin/end_frame is set.
    const int64_t src_sample = (which == TrimHit::Begin) ? app.trim.begin_frame
                                                         : app.trim.end_frame;
    const int64_t sample = source_frame_to_active_domain(app, audio, src_sample);
    viewport.move_playhead_to(sample);
}
