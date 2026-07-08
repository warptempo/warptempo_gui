#include "input_handler.h"

#include "warp_frame_map_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

// Trim gestures (architect-ruled hardfail model; the full ruling sits at the
// TrimState store in app_state.h): begin and end are authored named roles that
// may cross during any gesture and rest inverted or equal; no gesture guards
// against the partner bound, zero-length windows, or EOF. The only absolute
// clamp is the 0.0 format-representability floor — negative time is
// unrepresentable in the MM:SS.mmm timestamp grammar the .settings file
// persists, so nothing negative may be stored; it is not a spacing or
// validity rule. validate_trim_frames (trimmer.h) owns every trim refusal at
// the render boundary and the target-view gate.

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

    const int64_t cand_src =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);
    const double cand_seconds = static_cast<double>(cand_src) / sr_d;

    const int64_t live_total = live_total_frames(app, audio);
    const int64_t offset =
        dir * std::max<int64_t>(
                  1, samples_visible(app, audio) / kTrimAutosetVisibleDivisor);

    // Store the exact double seconds — trim seconds are full doubles like
    // marker times; the .settings writer rounds through format_timestamp at
    // save time (a saved bound reloads within half a millisecond, accepted).
    this_seconds = cand_seconds;
    this_has     = true;
    const int64_t this_active =
        source_frame_to_active_domain(app, audio, cand_src);
    int64_t other_active = this_active + offset;
    // Partner placement clamp: [0, live_total] is where the autoset CHOOSES
    // to put the far bound — a placement decision for a bound the user did
    // not position, not a wall against user motion (gestures on either bound
    // move it freely afterwards).
    if (other_active < 0)          other_active = 0;
    if (other_active > live_total) other_active = live_total;
    other_seconds = static_cast<double>(
        active_domain_to_source_frame(app, audio, other_active)) / sr_d;
    other_has = true;

    // Snap the playhead onto the frame of the bound just set at it. The
    // bound stores exact double seconds, so its rounded frame can differ
    // from the live playhead sample by rounding (and in target view by the
    // integer domain round trip). In target view the playback gate maps the
    // playhead against target_buffer_start_frame — this same begin mapped to
    // target frames — so a playhead off the bound's frame can land a sample
    // short of the buffer start and Space reads local < 0 and no-ops.
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

    if (app.trim_drag.both) {
        int64_t cur_active = 0;
        if (!trim_mouse_x_to_active_frame(mouse_x, cur_active)) return;
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
        // Only the grabbed bound is viewport-clamped; the partner rides the
        // rigid delta wherever that puts it — past EOF included (legal in
        // memory and on disk, refused only at render). The 0.0 format-
        // representability floor below is the sole absolute clamp on what
        // is stored (see the file-head comment).
        const int64_t nb_src = active_domain_to_source_frame(app, audio, ob + df);
        const int64_t ne_src = active_domain_to_source_frame(app, audio, oe + df);
        double nb = static_cast<double>(nb_src) / sr_d;
        double ne = static_cast<double>(ne_src) / sr_d;
        if (nb < 0.0) nb = 0.0;
        if (ne < 0.0) ne = 0.0;
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

    // No partner ceiling/floor and no zero/EOF data clamp: the bound crosses
    // its partner freely and rests wherever released, past EOF included —
    // the render boundary owns validity. The stored value is the exact
    // double seconds; the viewport clamp above keeps src_frame non-negative
    // (the visible strip starts at or after frame 0), so the 0.0 format-
    // representability floor holds by construction here.
    const double new_seconds = static_cast<double>(src_frame) / sr_d;
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
// nudge_selected_markers: one pixel of time per press at full double
// precision (sub-millisecond at deep zoom, matching the marker nudge's
// fidelity). No walls at all — the bound crosses its partner freely and may
// step past EOF; this is a recorded trim-vs-marker asymmetry (marker nudges
// keep their zero / EOF walls, trim keeps none — the render boundary owns
// trim validity). The only clamp is the 0.0 format-representability floor
// (see the file-head comment).
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

    // Step one pixel of time at the current zoom, per view, keeping full
    // double precision. Target view is marker-identical (the exact shape of
    // nudge_selected_markers' target branch): project the stored seconds
    // through the live target-view map, step in the target double domain,
    // llrint, inverse-map back at full precision. Source view is plain
    // seconds arithmetic.
    double proposed;
    if (app.active_audio_view == 'T') {
        const auto& target_warp_frame_map = target_view_warp_frame_map_cached(
            app, sr, static_cast<long>(audio.total_frames())).warp_frame_map;
        const double t_tgt = map_source_to_target(
            std::nearbyint(cur * sr_d), target_warp_frame_map);
        const double t_tgt_new = t_tgt + static_cast<double>(direction) * spp;
        const double q = (t_tgt_new < 0.0)
            ? 0.0
            : static_cast<double>(std::llrint(t_tgt_new));
        proposed = map_target_to_source(q, target_warp_frame_map) / sr_d;
    } else {
        proposed = cur + static_cast<double>(direction) * spp / sr_d;
    }

    // 0.0 format-representability floor only — no partner wall, no EOF wall
    // (see the function-head comment).
    if (proposed < 0.0) proposed = 0.0;
    if (proposed == cur) return;

    // Trim is gesture-owned and excluded from undo/redo history (as with every
    // other trim gesture; see handle_trim_clear_both).
    field = proposed;

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
