#include "input_handler.h"

#include "frame_map_view.h"
#include "time_format.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

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
    const int sr = audio.sample_rate();
    if (audio.total_frames() <= 0 || sr <= 0) return;
    const double sr_d = static_cast<double>(sr);
    const int64_t dir = (side == TrimSide::Begin) ? 1 : -1;

    bool&   this_has      = (side == TrimSide::Begin) ? app.trim.has_begin     : app.trim.has_end;
    double& this_seconds  = (side == TrimSide::Begin) ? app.trim.begin_seconds : app.trim.end_seconds;
    bool&   other_has     = (side == TrimSide::Begin) ? app.trim.has_end       : app.trim.has_begin;
    double& other_seconds = (side == TrimSide::Begin) ? app.trim.end_seconds   : app.trim.begin_seconds;

    int64_t cand_src =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);
    const double cand_seconds =
        snap_to_timestamp_grid(static_cast<double>(cand_src) / sr_d);
    cand_src = static_cast<int64_t>(std::nearbyint(cand_seconds * sr_d));

    const int64_t live_total = live_total_frames(app, audio);
    const int64_t offset =
        dir * std::max<int64_t>(
                  1, samples_visible(app, audio) / kTrimAutosetVisibleDivisor);

    this_seconds = cand_seconds;
    this_has     = true;
    const int64_t this_active =
        source_frame_to_active_domain(app, audio, cand_src);
    int64_t other_active = this_active + offset;
    if (other_active < 0)          other_active = 0;
    if (other_active > live_total) other_active = live_total;
    other_seconds = snap_to_timestamp_grid(static_cast<double>(
        active_domain_to_source_frame(app, audio, other_active)) / sr_d);
    other_has = true;

    // EOF clamp, mirroring the regular-marker total_duration - eps rule that
    // drop_marker / drag / nudge enforce. The end bound is held eps off the
    // source end; the begin bound is held 2*eps, so the autoset gap can
    // compress to a single eps near EOF without the bounds crossing (begin to
    // end is one eps, end to EOF is one eps). eps is the same
    // zoom-based marker spacing epsilon. Clamping by role (begin / end) is
    // correct regardless of which side seeded the gesture.
    const double spp       = current_samples_per_pixel(app, audio);
    const double eps       = marker_hit_eps_seconds(spp, sr_d);
    const double total_dur = static_cast<double>(audio.total_frames()) / sr_d;

    // Stored-grid invariant: every value written into app.trim.begin_seconds /
    // end_seconds is on the millisecond grid the .settings format persists at,
    // so an authored bound equals its own reloaded value bit-for-bit. The eps
    // ceilings above are sub-grid quantities, so a raw clamp to total_dur - eps
    // would store off the grid and shift on the next save/reload. Snap the
    // clamped value back onto the grid — but snap-to-nearest can round the
    // value UP, past the ceiling, which would erase the very eps gap the clamp
    // exists to hold (end flush against EOF, or begin flush against end). So
    // when the snap lands above the ceiling, step down one grid unit to the
    // nearest grid point at or below it rather than rounding upward. Because
    // the two ceilings sit eps and 2*eps below EOF and eps exceeds the grid
    // unit for every source in the project's length domain, they floor to
    // distinct grid points and begin stays strictly below end.
    constexpr double kGrid = 0.001;
    const double end_ceiling = total_dur - eps;
    if (app.trim.end_seconds > end_ceiling) {
        double v = snap_to_timestamp_grid(end_ceiling);
        if (v > end_ceiling) v -= kGrid;
        app.trim.end_seconds = v;
    }
    double begin_ceiling = total_dur - 2.0 * eps;
    if (begin_ceiling < 0.0) begin_ceiling = 0.0;
    if (app.trim.begin_seconds > begin_ceiling) {
        double v = snap_to_timestamp_grid(begin_ceiling);
        if (v > begin_ceiling) v -= kGrid;
        app.trim.begin_seconds = v;
    }

    // Snap the playhead onto the bound just set at it. The bound is grid-
    // quantized by snap_to_timestamp_grid (millisecond resolution, required
    // for serialization), so it can sit up to half a grid step from the live
    // sub-grid playhead. In target view the playback gate maps the playhead
    // against target_buffer_start_frame — this same snapped begin mapped to
    // target frames — so an un-snapped playhead can land a sample short of the
    // buffer start and Space reads local < 0 and no-ops.
    const int64_t this_src =
        static_cast<int64_t>(std::nearbyint(this_seconds * sr_d));
    app.playhead_cursor_sample =
        source_frame_to_active_domain(app, audio, this_src);

    app.trim_begin_selected = true;
    app.trim_end_selected   = true;
    app.last_selected_trim  = 'B';
    app.last_sel_group      = LastSelGroup::Trim;

    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

void GuiInputHandler::handle_trim_set_begin_autoset() {
    handle_trim_set_autoset(TrimSide::Begin);
}


void GuiInputHandler::handle_trim_unset(TrimSide side) {
    bool&   this_has     = (side == TrimSide::Begin) ? app.trim.has_begin     : app.trim.has_end;
    double& this_seconds = (side == TrimSide::Begin) ? app.trim.begin_seconds : app.trim.end_seconds;
    bool&   this_sel     = (side == TrimSide::Begin) ? app.trim_begin_selected : app.trim_end_selected;
    if (!this_has) return;
    this_has     = false;
    this_seconds = 0.0;
    this_sel     = false;  // an unset bound can't stay selected
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

// Shift+x: clear both trim bounds unconditionally. Silent no-op when neither
// bound is set. Trim is gesture-owned and excluded from undo/redo history.
void GuiInputHandler::handle_trim_clear_both() {
    if (app.trim.has_begin || app.trim.has_end) {
        app.trim.has_begin      = false;
        app.trim.has_end        = false;
        app.trim.begin_seconds  = 0.0;
        app.trim.end_seconds    = 0.0;
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

bool GuiInputHandler::trim_mouse_x_to_source_seconds(int mouse_x,
                                                     double& out_seconds) {
    const int sr = audio.sample_rate();
    if (sr <= 0) return false;
    const double sr_d = static_cast<double>(sr);

    int64_t domain_frame = 0;
    if (!trim_mouse_x_to_active_frame(mouse_x, domain_frame)) return false;

    // Target view: the cursor column is an active-domain frame; the trim
    // store is source-domain. Inverse-translate at the boundary, mirroring
    // handle_trim_set_autoset.
    const int64_t src_frame =
        active_domain_to_source_frame(app, audio, domain_frame);
    out_seconds = static_cast<double>(src_frame) / sr_d;
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
    app.trim_drag.orig_seconds = is_begin ? app.trim.begin_seconds
                                          : app.trim.end_seconds;
    app.trim_drag.orig_begin_seconds = app.trim.begin_seconds;
    app.trim_drag.orig_end_seconds   = app.trim.end_seconds;
    // Grab anchor: the press position in source-domain seconds. Motion moves
    // the bound by the cursor's displacement from here, so it tracks the grab
    // point with no snap (mirrors begin_drag's anchor_mouse_time_seconds).
    // A bad conversion leaves anchor_seconds at 0; harmless since the same
    // unusable state makes update_trim_drag early-return too.
    double anchor = 0.0;
    if (trim_mouse_x_to_source_seconds(mouse_x, anchor))
        app.trim_drag.anchor_seconds = anchor;
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
    const int sr = audio.sample_rate();
    if (sr <= 0 || audio.total_frames() <= 0) return;
    const double sr_d = static_cast<double>(sr);
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;

    // Anchor-relative motion: the dragged bound moves by the cursor's
    // displacement from the grab point, not to the absolute cursor column.
    // cursor_seconds is converted identically to the begin-drag anchor, so
    // the bound stays the same distance under the cursor for the whole drag.
    double cursor_seconds = 0.0;
    if (!trim_mouse_x_to_source_seconds(mouse_x, cursor_seconds)) return;
    const double delta_seconds = cursor_seconds - app.trim_drag.anchor_seconds;

    const int64_t total = static_cast<int64_t>(audio.total_frames());

    if (app.trim_drag.both) {
        int64_t cur_active = 0;
        if (!trim_mouse_x_to_active_frame(mouse_x, cur_active)) return;
        const int64_t live_total = live_total_frames(app, audio);
        const int64_t ob = source_frame_to_active_domain(app, audio,
            static_cast<int64_t>(std::nearbyint(app.trim_drag.orig_begin_seconds * sr_d)));
        const int64_t oe = source_frame_to_active_domain(app, audio,
            static_cast<int64_t>(std::nearbyint(app.trim_drag.orig_end_seconds * sr_d)));
        int64_t df = cur_active - app.trim_drag.anchor_active_frame;
        // Keep the grabbed bound — and the playhead pinned to it — inside the
        // visible pixel span, matching the playhead's own first/last-visible
        // clamp. Applied before the clip clamp so trim validity (0 / EOF) wins
        // in the rare case the window is wider than the viewport.
        const GuiRect area = waveform_area(app);
        const int64_t first_vis = app.viewport_start_sample;
        const int64_t last_vis  = app.viewport_start_sample +
            static_cast<int64_t>(std::nearbyint((area.w - 1) * spp));
        const int64_t grabbed = app.trim_drag.is_begin ? ob : oe;
        if (grabbed + df < first_vis) df = first_vis - grabbed;
        if (grabbed + df > last_vis)  df = last_vis  - grabbed;
        // Rigid clamp in the active domain: begin >= 0, end <= live EOF minus
        // the eps gap, so the end bound never lands flush against the EOF (the
        // same margin the single-bound drag and the warp markers hold). The
        // grabbed bound's viewport clamp above is applied first; this validity
        // clamp wins. eps_frames is the kMarkerHitHalfPx-pixel gap at the
        // current zoom, in active-domain frames. Only the grabbed bound is
        // viewport-clamped; the partner rides the rigid delta to its data
        // limit, as before.
        const int64_t eps_frames = static_cast<int64_t>(
            std::nearbyint(static_cast<double>(kMarkerHitHalfPx) * spp));
        if (ob + df < 0) df = -ob;
        if (oe + df > live_total - eps_frames)
            df = (live_total - eps_frames) - oe;
        const int64_t nb_src = active_domain_to_source_frame(app, audio, ob + df);
        const int64_t ne_src = active_domain_to_source_frame(app, audio, oe + df);
        const double nb = snap_to_timestamp_grid(static_cast<double>(nb_src) / sr_d);
        const double ne = snap_to_timestamp_grid(static_cast<double>(ne_src) / sr_d);
        if (app.trim.begin_seconds != nb || app.trim.end_seconds != ne) {
            app.trim.begin_seconds = nb;
            app.trim.end_seconds   = ne;
            app.trim_drag.moved    = true;
            const int64_t grabbed_src = static_cast<int64_t>(
                std::nearbyint((app.trim_drag.is_begin ? nb : ne) * sr_d));
            app.playhead_cursor_sample =
                source_frame_to_active_domain(app, audio, grabbed_src);
            viewport.invalidate_waveform_area();
            viewport.invalidate_timestamp_area();
        }
        return;
    }

    // Single-bound: pre-drag frame plus the anchor-relative delta.
    const int64_t orig_f = static_cast<int64_t>(
        std::nearbyint(app.trim_drag.orig_seconds * sr_d));
    int64_t src_frame = orig_f +
        static_cast<int64_t>(std::nearbyint(delta_seconds * sr_d));

    // eps_frames is the kMarkerHitHalfPx-pixel gap (at the current zoom) the
    // trim drag holds off both the EOF and the other bound — the same eps the
    // warp drag uses (warpmarkers_ops.cpp ~429), so the b/e stems never reach
    // visual coincidence and the bound never lands flush against the EOF.
    // Hoisted above the data clamp so the EOF clamp can use it. This is the
    // trim-internal eps (begin vs end), orthogonal to warp/phase eps.
    const int64_t eps_frames = static_cast<int64_t>(
        std::nearbyint(static_cast<double>(kMarkerHitHalfPx) * spp));

    // Viewport clamp: keep the grabbed bound within the visible strip (pixel 0
    // through the last fully-visible pixel) so the drag can't push it
    // offscreen, where its precise location would be hidden. The cursor column
    // is already viewport-bound, but a grab a few pixels off the stem can
    // trail the bound past the edge; this makes the bound itself exact. The
    // bounds are active-domain while src_frame is source, so inverse-translate
    // the edges — monotonic, so the source clamp matches the active-pixel one.
    const auto vb = viewport_marker_bounds(app, audio);
    const int64_t vp_lo = active_domain_to_source_frame(app, audio, vb.first);
    const int64_t vp_hi = active_domain_to_source_frame(app, audio, vb.second);
    if (src_frame < vp_lo) src_frame = vp_lo;
    if (src_frame > vp_hi) src_frame = vp_hi;

    // Data clamp: 0 at the start, eps short of the EOF. Applied after the
    // viewport clamp so trim validity wins when the EOF is on-screen.
    if (src_frame < 0) src_frame = 0;
    if (src_frame > total - eps_frames) src_frame = total - eps_frames;

    // Clamp against the other bound: the dragged bound stops eps_frames short
    // of it, the tightest gap two regular marker stems can hold.
    if (app.trim_drag.is_begin) {
        if (app.trim.has_end) {
            const int64_t end_f = static_cast<int64_t>(
                std::nearbyint(app.trim.end_seconds * sr_d));
            if (src_frame > end_f - eps_frames) src_frame = end_f - eps_frames;
            if (src_frame < 0) src_frame = 0;
        }
    } else {
        if (app.trim.has_begin) {
            const int64_t begin_f = static_cast<int64_t>(
                std::nearbyint(app.trim.begin_seconds * sr_d));
            if (src_frame < begin_f + eps_frames) src_frame = begin_f + eps_frames;
            if (src_frame > total - eps_frames) src_frame = total - eps_frames;
        }
    }

    // Single-bound drag: snap the derived seconds the same way the group
    // branch above does, so the change-detection compare and the store
    // both operate on the grid value the bound will persist at.
    const double new_seconds =
        snap_to_timestamp_grid(static_cast<double>(src_frame) / sr_d);
    double& field = app.trim_drag.is_begin ? app.trim.begin_seconds
                                           : app.trim.end_seconds;
    if (field != new_seconds) {
        const bool first_motion = !app.trim_drag.moved;
        field = new_seconds;
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
        // Trim is a render-time cut and is NOT in build_target_view_frame_map,
        // so the bound carries no deformation: new_seconds (source-domain)
        // maps straight to the playhead, inverse-translated to target-domain
        // in target view. No predictor resync and no scanner-sample sync,
        // both matching the marker-drag block — trim drag stopped playback at
        // begin, so the scanner is inactive and a play reseeks from the
        // cursor. The invalidate_waveform_area below repaints the playhead
        // columns along with the moved trim shading.
        const int64_t src_sample = static_cast<int64_t>(
            std::nearbyint(new_seconds * sr_d));
        const int64_t sample = source_frame_to_active_domain(app, audio, src_sample);
        app.playhead_cursor_sample = sample;
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
    }
}

void GuiInputHandler::commit_trim_drag() {
    if (!app.trim_drag.active) return;
    if (app.trim_drag.moved) {
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
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
    // fires when the boundary exists, so trim_begin/end_seconds is set.
    const double sec = (which == TrimHit::Begin) ? app.trim.begin_seconds
                                                 : app.trim.end_seconds;
    const int sr = audio.sample_rate();
    const int64_t src_sample =
        static_cast<int64_t>(std::nearbyint(sec * static_cast<double>(sr)));
    const int64_t sample = source_frame_to_active_domain(app, audio, src_sample);
    viewport.move_playhead_to(sample);
}
