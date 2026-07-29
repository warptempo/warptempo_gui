#include "app_state.h"

#include "audio.h"
#include "gui_display_context.h"
#include "paint_handler.h"
#include "render.h"
#include "text_editor.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

int64_t monotonic_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

SettingsSnapshot capture_current_settings(const AppState& app) {
    SettingsSnapshot s;
    s.engine_settings = app.engine_settings;
    // Trim is view state, gesture-owned and excluded from undo/redo history;
    // undo entries snapshot engine settings only.
    return s;
}

void remap_marker_indices_after_reorder(AppState& app, char column,
                                        const std::vector<int>& old_to_new) {
    if (old_to_new.empty()) return;
    const int n = static_cast<int>(old_to_new.size());
    auto mapped = [&](int idx) {
        return (idx >= 0 && idx < n) ? old_to_new[idx] : idx;
    };
    auto remap_set = [&](std::set<int>& s) {
        if (s.empty()) return;
        std::set<int> out;
        for (int idx : s) out.insert(mapped(idx));
        s = std::move(out);
    };
    remap_set(app.selected_markers);
    app.last_selected_marker = mapped(app.last_selected_marker);
    // THE PARKED COPIES follow too, in BOTH tabs and for the REORDERED COLUMN
    // ONLY. The marker stores and the map are GLOBAL while the selections are
    // per-tab and per-column, so a reorder in the live tab silently re-points
    // every parked selection at different rows unless it is remapped here —
    // that was a real defect, not a theoretical one: nudge a selected marker
    // across its neighbour in tab A and tab B's parked selection came back
    // pointing at whatever now occupies those slots. `column` names the store
    // that reordered ('W' warp / 'P' phase reset); the other column's slots are
    // untouched because its store did not move. The ACTIVE tab's own slot for
    // the active column is a stale mirror that the next stash boundary
    // overwrites, so remapping it changes nothing — it is done anyway to keep
    // the rule "every parked copy of the reordered column follows" free of
    // exceptions.
    for (ViewState* vs : {&app.tab_a, &app.tab_b}) {
        if (column == 'P') {
            remap_set(vs->phase_reset_selected);
            vs->phase_reset_last_selected = mapped(vs->phase_reset_last_selected);
        } else {
            remap_set(vs->warp_selected);
            vs->warp_last_selected = mapped(vs->warp_last_selected);
        }
    }
    if (app.drag.active) {
        // Pairing between dragging_markers and its parallel time vectors
        // (original_times / moveable_times) is positional by k, so an
        // in-place value remap keeps each index bound to its own times.
        // Nothing relies on ascending order of the remapped indices —
        // DragOverlay::effective_time scans linearly.
        for (int& idx : app.drag.dragging_markers) idx = mapped(idx);
        app.drag.hit_marker = mapped(app.drag.hit_marker);
        // grabbed_k is deliberately NOT remapped: it is a POSITION into the
        // parallel drag vectors (which stay positionally stable — values remap
        // in place, slots do not), not a store index like hit_marker.
    }
}

// hit_test_* promoted from lambdas in main(). The captured `app` and `audio`
// references are now explicit arguments. The kMarkerHitHalfPx constant
// resolves through app_state.h.

// Event-synchronized hit map (ruling at the declaration in app_state.h): in
// target view with a warm displayed map, the item hit tests decide against the
// map the LAST COMMITTED frame's flag pixels were painted with (promoted
// at that frame commit, not the offscreen rebuild or the plate publish);
// otherwise the live display context's map (source view = its identity/empty
// map, target-view cold = the live map until the first committed target frame).
const std::vector<WarpFrameMapSegment>&
displayed_or_live_target_map(const AppState& app, const GuiAudio& audio) {
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    if (ctx.domain == GuiDisplayDomain::TargetLive &&
        !app.displayed_target_warp_frame_map.empty()) {
        return app.displayed_target_warp_frame_map;
    }
    return *ctx.warp_frame_map;
}

// The viewport twin of displayed_or_live_target_map (full rationale at the
// declaration): the vp_start/vp_end/area_w the flag item cache was painted
// with on the last committed frame, so the marker/chip/lane geometry rides the
// same basis the flag/chip pixels do. The warm spp is (vp_end - vp_start) /
// area_w — the flags' OWN samples-per-pixel (span over the effective waveform
// width the item render used), exact on the committing frame. Cold (area_w == 0,
// nothing promoted yet) falls back to the live viewport span at the effective
// waveform width, bit-for-bit the pre-mirror hit-test live basis.
DisplayedViewportBasis displayed_viewport_basis(const AppState& app,
                                                const GuiAudio& audio) {
    DisplayedViewportBasis b;
    if (app.displayed_area_w > 0) {
        b.vp_start_frame = app.displayed_vp_start;
        b.vp_end_frame   = app.displayed_vp_end;
        b.area_w         = app.displayed_area_w;
    } else {
        const GuiRect area = waveform_area(app);
        const double  spp  = current_samples_per_pixel(app, audio);
        b.vp_start_frame = app.viewport_start_sample;
        b.vp_end_frame   = viewport_end_sample(b.vp_start_frame, spp, area.w);
        b.area_w         = area.w;
    }
    b.vp_start = static_cast<double>(b.vp_start_frame);
    b.spp = b.area_w > 0
        ? static_cast<double>(b.vp_end_frame - b.vp_start_frame) /
          static_cast<double>(b.area_w)
        : 0.0;
    return b;
}

TrimHit hit_test_trim_chip(const AppState& app, const GuiAudio& audio,
                           int mouse_x, int mouse_y) {
    // Trim bounds hit-test in the AUTHORING views against the active A/B tab's
    // live bounds. The sole consumer (route_trim_chip_press) routes here only
    // with the FULL pair set — a lone bound is gesture-inert —
    // so both bounds are guaranteed present past this early-out.
    if (!(app.trim.has_begin && app.trim.has_end)) return TrimHit::None;
    const int64_t begin_frame = app.trim.begin_frame;
    const int64_t end_frame   = app.trim.end_frame;

    // The b/e chips are SQUARES in the trim-chip lane (top_upper_row_area,
    // whose height is the chip width flag_lane_w_px()). A press outside that
    // vertical band is not on a chip.
    const GuiRect row = top_upper_row_area(app);
    if (mouse_y < row.y || mouse_y >= row.y + row.h) return TrimHit::None;

    const GuiRect top = top_strip_area(app);
    // Event-synchronized hit geometry, the VIEWPORT half: the b/e chip pixels
    // are painted live by the trim pass (GuiPaintHandler::paint_trim ->
    // render_trim_flags) on the DISPLAYED basis, NOT the live viewport. So the
    // chip columns must resolve on the SAME basis (displayed_viewport_basis)
    // — the same reason hit_test_flag does — else during an async publish window a
    // chip painted at the OLD column would be grabbed at the NEW/live column.
    // The visibility
    // cull matches the painter's viewport extent (the painter maps against
    // this same {span, width}), so a gutter column at a non-multiple-of-16 window
    // is culled the same in paint and hit-test. Cold falls back to the live
    // basis, matching the painter's cold fallback.
    const DisplayedViewportBasis basis = displayed_viewport_basis(app, audio);
    if (basis.spp <= 0.0) return TrimHit::None;
    const int     wave_w   = basis.area_w;
    const int64_t vp_start = basis.vp_start_frame;
    const int64_t vp_end   = basis.vp_end_frame;
    const int sr = audio.sample_rate();
    if (sr <= 0) return TrimHit::None;

    // Column translation so the chip column lands
    // where the stem (and chip) are painted in the mapped views: the map is
    // the item pixels' own via displayed_or_live_target_map (event-synchronized
    // hit geometry — the ruling at that selector), empty (identity) in source
    // view and the map the flag item cache baked when warm in target view
    // (the live trim pass paints its chips through the same selector).
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    const std::vector<WarpFrameMapSegment>* target_warp_frame_map =
        dmap.empty() ? nullptr : &dmap;

    // Build the same visible, sorted candidate list render_trim_flags paints.
    // The painter culls a bound whose column is outside the viewport, then
    // reverse-paints so the leftmost chip (b over e at an equal column) lands
    // on top. Hit testing walks the same sorted list FORWARD and returns the
    // first chip whose rect contains mouse_x = the topmost-painted chip, so a
    // click on the visible overlap grabs the chip the user sees on top.
    struct TrimChipHit {
        double  center_x;
        GuiRect rect;
        TrimHit which;
    };
    std::vector<TrimChipHit> chips;
    auto add_chip = [&](int64_t frame, TrimHit which) {
        // Map the authored source frame to the displayed domain and resolve its
        // column through the SAME owners the painter uses (render.h): the mapping
        // via displayed_trim_ms, the column via trim_bound_column against the
        // displayed-basis vp span (the painters' quantized-span denominator), the
        // chip rect via trim_chip_rect. So a hit lands on exactly the drawn chip.
        const double ms = displayed_trim_ms(frame, target_warp_frame_map);
        const TrimBoundColumn c =
            trim_bound_column(ms, vp_start, vp_end, wave_w);
        if (!c.in_viewport) return;
        const GuiRect cr_rect =
            trim_chip_rect(which == TrimHit::Begin, top.x, c.col, row);
        const double center_x = static_cast<double>(top.x + c.col);
        chips.push_back({center_x, cr_rect, which});
    };

    add_chip(begin_frame, TrimHit::Begin);
    add_chip(end_frame,   TrimHit::End);
    std::sort(chips.begin(), chips.end(),
              [](const TrimChipHit& a, const TrimChipHit& b) {
                  if (a.center_x != b.center_x)
                      return a.center_x < b.center_x;
                  // Deterministic tie-break at an equal column: Begin first, so
                  // the forward walk below returns it. The painted rectangles
                  // are identical there, so occlusion is not visually
                  // distinguishable; this only fixes which bound a click grabs.
                  return a.which == TrimHit::Begin && b.which == TrimHit::End;
              });

    // Forward walk = ascending-x = topmost-painted first. The first chip whose
    // [rect.x, rect.x + w) contains mouse_x is the one the user sees on top.
    for (const TrimChipHit& chip : chips) {
        if (mouse_x >= chip.rect.x &&
            mouse_x < chip.rect.x + chip.rect.w) {
            return chip.which;
        }
    }
    return TrimHit::None;
}

int hit_test_flag(const AppState& app, const GuiAudio& audio,
                  int mouse_x, int mouse_y) {
    const GuiRect top  = top_strip_area(app);
    // Event-synchronized hit geometry, the VIEWPORT half: the flag pixels are
    // painted from the item cache's committed fp_vp span over the effective
    // waveform width the render used (the flag cache's own basis), NOT the live
    // viewport. So the hit rects must build on the DISPLAYED basis
    // (displayed_viewport_basis, the twin of the displayed MAP below) — else
    // during an async plate-publish window a click over the visible OLD flag
    // would be tested at the NEW/live column, splitting the one marker item from
    // its lane run (which already rides the displayed basis). With the basis
    // triple the rects here are exactly the painted flag rectangles across the
    // publish window, not just at rest. Cold falls back to the live basis
    // bit-for-bit (see the accessor).
    const DisplayedViewportBasis basis = displayed_viewport_basis(app, audio);
    const int64_t vp_start = basis.vp_start_frame;
    const int64_t vp_end   = basis.vp_end_frame;
    const int     wave_w   = basis.area_w;
    // The mapped views' flags paint at translated positions
    // (compute_flag_hit_rects with a non-null warp_frame_map), so hit-test
    // must walk the same warp_frame_map — the item pixels' own via
    // displayed_or_live_target_map (event-synchronized hit geometry — the
    // ruling at that selector): empty (identity) in source view, the map the
    // flag item cache baked when warm in target view.
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    const std::vector<WarpFrameMapSegment>* tmap_arg =
        dmap.empty() ? nullptr : &dmap;
    DragOverlay drag_overlay_storage;
    const DragOverlay* drag_overlay = nullptr;
    if (app.drag.active) {
        drag_overlay_storage.indices = &app.drag.dragging_markers;
        drag_overlay_storage.times   = &app.drag.moveable_times;
        drag_overlay = &drag_overlay_storage;
    }
    // This two-way branch chain is the sole hit-rect builder: the flag paint
    // and this hit test share the fixed flag rectangle (via
    // compute_flag_hit_rects / iterate_visible_flags_impl) on the same displayed
    // viewport + width, so the rects computed here are exactly the painted flag
    // rectangles.
    // The lane rects the shapes were painted in, from the same accessors the
    // painter was handed (top_flag_row_area / top_triangle_row_area) — so the
    // rect built here is the painted rectangle vertically as well as
    // horizontally, whatever the strip's lane stack looks like.
    const FlagLaneRects lanes{top_flag_row_area(app), top_triangle_row_area(app)};
    std::vector<FlagHitRect> rects;
    if (app.active_markers_view == 'P') {
        rects = compute_phase_reset_flag_hit_rects(
            top, lanes, wave_w, app.phaseresetmarkers.markers(),
            vp_start, vp_end, audio.sample_rate(),
            tmap_arg, drag_overlay);
    } else {
        rects = compute_flag_hit_rects(
            top, lanes, wave_w, app.warpmarkers.markers(),
            vp_start, vp_end, audio.sample_rate(),
            tmap_arg, drag_overlay);
    }
    // A point hits a flag when it lies inside the flag RECTANGLE (the emitted
    // FlagHitRect) OR inside the fused tip-down TRIANGLE directly below it. The
    // triangle shares the rect's center as its vertical centerline; its top edge
    // is the rect bottom (r.y + r.h) and it tapers over playhead_triangle_h_px()
    // rows to the tip. Half-width per row comes from flag_triangle_half_width_at
    // — the SAME taper owner paint_flag_shape fills with — so the clickable slope
    // matches the painted one. The queried pixel is tested at its center
    // (+0.5, +0.5). The playhead triangle paints in the same lane but is not in
    // `rects`, so it is never a hit target; the marker test covers the triangle
    // even where the playhead visually overlaps it.
    const int tri_h = playhead_triangle_h_px();
    auto contains = [&](const FlagHitRect& r) -> bool {
        if (mouse_x >= r.x && mouse_x < r.x + r.w &&
            mouse_y >= r.y && mouse_y < r.y + r.h) {
            return true;
        }
        if (tri_h <= 0) return false;
        const double rb   = r.y + r.h;                       // triangle top
        const double tbot = rb + static_cast<double>(tri_h); // tip row (exclusive)
        const double py   = static_cast<double>(mouse_y) + 0.5;
        if (py < rb || py >= tbot) return false;
        const double hw  = flag_triangle_half_width_at(py - rb);
        const double cxl = r.x + r.w / 2.0;                  // triangle centerline
        const double px  = static_cast<double>(mouse_x) + 0.5;
        return std::fabs(px - cxl) <= hw;
    };

    // Mirror of the painters' z-order (render_flags / render_phase_reset_flags):
    // selected shapes paint above unselected, and within each class the leftmost
    // paints on top. Walk the rects TWICE — first the first-containing shape whose
    // marker is selected, else the first-containing shape unconditionally. rects
    // are emitted ascending-x, so each forward pass resolves to that class's
    // leftmost = topmost. Topmost = selected leftmost > leftmost. WYSIWYG for
    // every consumer (selection clicks, plain flag-drag reposition grabs, the
    // hover popup's target): the topmost-painted flag is what a click grabs. The
    // flags are now fixed-width shapes, so there is no editor-pending width to
    // track — the editing target's flag is an ordinary cached shape.
    for (const auto& r : rects) {
        if (contains(r) && app.selected_markers.count(r.marker_index)) {
            return r.marker_index;
        }
    }
    for (const auto& r : rects) {
        if (contains(r)) {
            return r.marker_index;
        }
    }
    return -1;
}

// Promoted from a lambda in main(). The captured `app`
// reference is now an explicit argument.
bool popup_eligible_marker(const AppState& app, int idx) {
    if (idx < 0) return false;
    if (app.active_markers_view != 'W') return false;
    if (app.iteration_mode_enabled) return false;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return false;
    const auto& m = mv[idx];
    // This gates the BOTTOM-STRIP resolved readout only — the marker-text lane
    // shows every hovered marker's own value regardless of eligibility. Render
    // resolution (resolve_warp_markers_for_render) drops disabled markers
    // outright and drops label refs whose definition is disabled (the
    // cascade). The readout must not report a tempo the render never applies,
    // so eligibility mirrors both drops here: a disabled marker is
    // ineligible, and a ref to a disabled definition is ineligible. A ref
    // whose definition is missing entirely stays eligible —
    // compute_hover_popup_text already yields an empty string for that case
    // and the display sites suppress an empty readout, so it never surfaces a
    // stale tempo.
    if (m.disabled) return false;
    if (!m.label_ref.empty()) {
        for (const auto& def : mv) {
            if (def.label_def == m.label_ref) {
                if (def.disabled) return false;
                break;
            }
        }
    }
    return m.tempo_inherits || !m.label_ref.empty();
}
