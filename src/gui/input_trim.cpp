#include "input_handler.h"

#include "warp_frame_map_view.h"
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

    // Absolute EOF ceilings: the end bound clamps to total_duration
    // exactly, the begin bound to one millisecond grid unit below the end
    // bound (keeping begin < end representable on the stored grid). No
    // eps margins — a trim window that is authorably tiny renders with
    // incomplete OLA fades, or refuses as a degenerate window at the
    // render boundary; both are accepted by ruling. Clamping by role
    // (begin / end) is correct regardless of which side seeded the
    // gesture.
    const double total_dur = static_cast<double>(audio.total_frames()) / sr_d;

    // Stored-grid invariant: every value written into app.trim.begin_seconds /
    // end_seconds is on the millisecond grid the .settings format persists at,
    // so an authored bound equals its own reloaded value bit-for-bit.
    // total_dur is generally off the grid, so snap the clamped end value
    // back onto it — and because snap-to-nearest can round UP past EOF,
    // step down one grid unit when it does, landing on the nearest grid
    // point at or below total_dur. Canonical millisecond doubles are not
    // closed under +-kGrid (binary 0.001 arithmetic drifts by ULPs), so
    // every stored grid-step result is wrapped in snap_to_timestamp_grid;
    // snapping a value within ULPs of a grid point returns the canonical
    // double and cannot jump a full grid unit, so the step semantics are
    // unchanged.
    constexpr double kGrid = 0.001;
    if (app.trim.end_seconds > total_dur) {
        double v = snap_to_timestamp_grid(total_dur);
        if (v > total_dur) v = snap_to_timestamp_grid(v - kGrid);
        app.trim.end_seconds = v;
    }
    double begin_ceiling =
        snap_to_timestamp_grid(app.trim.end_seconds - kGrid);
    if (begin_ceiling < 0.0) begin_ceiling = 0.0;
    if (app.trim.begin_seconds > begin_ceiling) {
        app.trim.begin_seconds = begin_ceiling;
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
        // Rigid clamp in the active domain: begin >= 0, end <= live EOF —
        // the absolute range only; the end bound may land flush against
        // the EOF. The grabbed bound's viewport clamp above is applied
        // first; this validity clamp wins. Only the grabbed bound is
        // viewport-clamped; the partner rides the rigid delta to its data
        // limit, as before.
        if (ob + df < 0) df = -ob;
        if (oe + df > live_total) df = live_total - oe;
        const int64_t nb_src = active_domain_to_source_frame(app, audio, ob + df);
        const int64_t ne_src = active_domain_to_source_frame(app, audio, oe + df);
        const double nb = snap_to_timestamp_grid(static_cast<double>(nb_src) / sr_d);
        double ne = snap_to_timestamp_grid(static_cast<double>(ne_src) / sr_d);
        // Stored-grid invariant: the snap can round the end bound up past
        // EOF by a sub-grid amount; step down one grid unit so the stored
        // value stays at or below total_duration.
        {
            const double total_dur =
                static_cast<double>(total) / sr_d;
            constexpr double kGrid = 0.001;
            if (ne > total_dur) ne = snap_to_timestamp_grid(ne - kGrid);
        }
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

    // Data clamp: the absolute range only — 0 at the start, EOF at the
    // end; the bound may land flush against either. Applied after the
    // viewport clamp so trim validity wins when the EOF is on-screen.
    if (src_frame < 0) src_frame = 0;
    if (src_frame > total) src_frame = total;

    // Single-bound drag: snap the derived seconds the same way the group
    // branch above does, so the change-detection compare and the store
    // both operate on the grid value the bound will persist at. Then
    // apply the grid-quantized ceilings: the end bound clamps to
    // total_duration (snap-to-nearest can round up past EOF by a
    // sub-grid amount — step down one grid unit when it does), and the
    // begin bound to one grid unit below the end bound, keeping
    // begin < end representable. No eps gaps — an authorably tiny trim
    // window renders with incomplete OLA fades or refuses as degenerate
    // at the render boundary, both accepted by ruling.
    double new_seconds =
        snap_to_timestamp_grid(static_cast<double>(src_frame) / sr_d);
    constexpr double kGrid = 0.001;
    if (app.trim_drag.is_begin) {
        if (app.trim.has_end) {
            const double begin_ceiling =
                snap_to_timestamp_grid(app.trim.end_seconds - kGrid);
            if (new_seconds > begin_ceiling) {
                new_seconds = (begin_ceiling < 0.0) ? 0.0 : begin_ceiling;
            }
        }
    } else {
        const double total_dur = static_cast<double>(total) / sr_d;
        if (new_seconds > total_dur)
            new_seconds = snap_to_timestamp_grid(new_seconds - kGrid);
        if (app.trim.has_begin) {
            const double end_floor =
                snap_to_timestamp_grid(app.trim.begin_seconds + kGrid);
            if (new_seconds < end_floor) new_seconds = end_floor;
        }
    }
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
        // Trim is a render-time cut and is NOT in build_target_view_warp_frame_map,
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

// Ctrl+Left / Ctrl+Right on the trim group. The sibling of
// nudge_selected_markers: one pixel of time per press, walls-only,
// refuse-or-cap by the direction of travel. Trim differs from the marker
// nudge in two designed ways, marked at their sites below — the millisecond
// grid is the step floor (trim persists on the grid), and the partner bound
// is a wall (trim behaves like a viewport, so its bounds keep a grid gap and
// cannot coincide).
void GuiInputHandler::nudge_selected_trim(int direction) {
    if (app.loading || audio.total_frames() <= 0) return;
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    // The nudge moves the playhead onto the bound; stop playback first.
    playback_lifecycle.stop_playback_if_playing();
    const double sr_d = static_cast<double>(sr);

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

    double& field = (which == TrimHit::Begin) ? app.trim.begin_seconds
                                              : app.trim.end_seconds;
    const double cur = field;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;
    const double total_dur = static_cast<double>(audio.total_frames()) / sr_d;
    constexpr double kGrid = 0.001;

    // Step one pixel of time at the current zoom, per view. Source view steps
    // the source-domain seconds directly; target view steps the bound's
    // displayed (active-domain) position by one pixel and inverse-translates
    // back to source, matching how the marker nudge steps one target pixel and
    // how every trim gesture crosses the domain boundary.
    double proposed;
    if (app.active_audio_view == 'T') {
        const int64_t src_frame =
            static_cast<int64_t>(std::nearbyint(cur * sr_d));
        const int64_t active = source_frame_to_active_domain(app, audio, src_frame);
        double active_new =
            std::nearbyint(static_cast<double>(active) +
                           static_cast<double>(direction) * spp);
        if (active_new < 0.0) active_new = 0.0;
        const int64_t back_src = active_domain_to_source_frame(
            app, audio, static_cast<int64_t>(active_new));
        proposed = static_cast<double>(back_src) / sr_d;
    } else {
        proposed = cur + static_cast<double>(direction) * spp / sr_d;
    }

    // Stored-grid invariant: every value written into the trim store is on the
    // millisecond grid the .settings format persists at. The millisecond grid
    // is therefore the nudge's floor — a deliberate asymmetry against the
    // marker nudge's sub-millisecond full-double fidelity, because trim
    // persists on the grid. When the snapped proposal equals the current value
    // (a pixel finer than the grid at deep zoom), step one grid unit in the
    // travel direction instead, routing the grid arithmetic through the snap
    // as every trim site does.
    double proposed_grid = snap_to_timestamp_grid(proposed);
    if (proposed_grid == cur) {
        proposed_grid = snap_to_timestamp_grid(
            cur + static_cast<double>(direction) * kGrid);
    }

    // Walls, consulting only the bound in the direction of travel (the marker
    // nudge's rule): the wall is a ceiling when moving later, a floor when
    // moving earlier. Values match the trim drag's single-bound clamps. The
    // partner bound is deliberately a wall: trim bounds keep their
    // cannot-coincide grid gap (trim behaves like a viewport, not a marker),
    // one grid unit apart on the stored grid.
    double wall;
    if (which == TrimHit::Begin) {
        if (direction > 0) {
            // begin moving later: the begin ceiling. With an end bound, one
            // grid unit below it; without one, the source end, snapped and
            // stepped down one grid unit when the snap rounds past EOF (the
            // same round-past-EOF handling the drag applies to the end cap).
            // The render boundary owns refusing a begin at or past the source
            // end; this mirrors the drag's data clamp and does not pre-guard.
            if (app.trim.has_end) {
                wall = snap_to_timestamp_grid(app.trim.end_seconds - kGrid);
                if (wall < 0.0) wall = 0.0;
            } else {
                wall = snap_to_timestamp_grid(total_dur);
                if (wall > total_dur) wall = snap_to_timestamp_grid(wall - kGrid);
            }
        } else {
            wall = 0.0;   // begin moving earlier: zero.
        }
    } else {
        if (direction > 0) {
            // end moving later: total duration, snapped with the same
            // round-past-EOF step-down.
            wall = snap_to_timestamp_grid(total_dur);
            if (wall > total_dur) wall = snap_to_timestamp_grid(wall - kGrid);
        } else {
            // end moving earlier: one grid unit above the begin bound; else
            // zero.
            wall = app.trim.has_begin
                ? snap_to_timestamp_grid(app.trim.begin_seconds + kGrid)
                : 0.0;
        }
    }
    // Refuse (silent no-op) when there is no room; otherwise cap the step at
    // the wall so repeated presses creep flush onto it.
    if (direction > 0) {
        if (cur >= wall) return;
        if (proposed_grid > wall) proposed_grid = wall;
    } else {
        if (cur <= wall) return;
        if (proposed_grid < wall) proposed_grid = wall;
    }
    if (proposed_grid == cur) return;

    // Trim is gesture-owned and excluded from undo/redo history (as with every
    // other trim gesture; see handle_trim_clear_both).
    field = proposed_grid;

    // The same invalidation set the trim drag's motion branch emits.
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();

    // No viewport gate, mirroring the marker nudge (drags viewport-clamp the
    // grabbed item; nudges do not). Track the playhead onto the bound through
    // move_playhead_to's edge-follow path — at most one pixel of scroll,
    // keeping the bound just inside the edge.
    const int64_t new_src =
        static_cast<int64_t>(std::nearbyint(field * sr_d));
    viewport.move_playhead_to(
        source_frame_to_active_domain(app, audio, new_src));
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
