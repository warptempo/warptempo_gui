#include "app_state.h"

#include "audio.h"
#include "gui_display_context.h"
#include "paint_handler.h"
#include "render.h"
#include "text_editor.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

SettingsSnapshot capture_current_settings(const AppState& app) {
    SettingsSnapshot s;
    s.engine_settings = app.engine_settings;
    // Trim is view state, gesture-owned and excluded from undo/redo history;
    // undo entries snapshot engine settings only.
    return s;
}

void remap_marker_indices_after_reorder(AppState& app,
                                        const std::vector<int>& old_to_new) {
    if (old_to_new.empty()) return;
    const int n = static_cast<int>(old_to_new.size());
    auto mapped = [&](int idx) {
        return (idx >= 0 && idx < n) ? old_to_new[idx] : idx;
    };
    std::set<int> remapped;
    for (int idx : app.selected_markers) remapped.insert(mapped(idx));
    app.selected_markers = std::move(remapped);
    app.last_selected_marker = mapped(app.last_selected_marker);
    if (app.drag.active) {
        // Pairing between dragging_markers and its parallel time vectors
        // (original_times / moveable_times) is positional by k, so an
        // in-place value remap keeps each index bound to its own times.
        // Nothing relies on ascending order of the remapped indices —
        // DragOverlay::effective_time scans linearly.
        for (int& idx : app.drag.dragging_markers) idx = mapped(idx);
        app.drag.hit_marker = mapped(app.drag.hit_marker);
    }
}

// hit_test_* promoted from lambdas in main(). The captured `app` and `audio`
// references are now explicit arguments. The kMarkerHitHalfPx constant
// resolves through app_state.h.

int hit_test_marker_line(const AppState& app, const GuiAudio& audio,
                         int mouse_x) {
    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return -1;
    const int click_rel_x = mouse_x - area.x;
    const double vp = static_cast<double>(app.viewport_start_sample);
    int best_hit = -1;
    int best_dist = kMarkerHitHalfPx + 1;
    const int n = (app.active_markers_view == 'P')
        ? static_cast<int>(app.phaseresetmarkers.markers().size())
        : static_cast<int>(app.warpmarkers.markers().size());
    // The mapped views paint marker stems at map_source_to_target
    // translated positions; the hit test must walk the same warp_frame_map so
    // mouse_x lands on the visually-drawn stem, not the marker's source-
    // frame position. compute_flag_hit_rects already does this on the
    // top strip; this mirrors that for the waveform-area marker line.
    // The active display context owns the composite view rule: its map is
    // the live target map in target view, and empty (identity) in source
    // view or when the target map cannot build.
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    const std::vector<WarpFrameMapSegment>* target_warp_frame_map =
        ctx.warp_frame_map->empty() ? nullptr : ctx.warp_frame_map;
    for (int i = 0; i < n; ++i) {
        int64_t src_sample =
            (app.active_markers_view == 'P')
                ? app.phaseresetmarkers.markers()[i].time_frame
                : app.warpmarkers.markers()[i].time_frame;
        double ms = static_cast<double>(src_sample);
        if (target_warp_frame_map) {
            const size_t q = (src_sample < 0)
                ? static_cast<size_t>(0)
                : static_cast<size_t>(src_sample);
            ms = std::nearbyint(map_source_to_target(q, *target_warp_frame_map));
        }
        // No viewport gate: the kMarkerHitHalfPx halo is the single reach
        // test, so a stem up to kMarkerHitHalfPx past either strip edge is
        // grabbable from the nearest onscreen pixels (the mouse is window-
        // bounded, so the reach is exactly the halo). m_px may be negative or
        // >= the strip width; the |d| <= kMarkerHitHalfPx test below is the
        // only reach rule. The arithmetic is int-safe far offscreen for any
        // in-wall position at every zoom.
        const int m_px = static_cast<int>(std::nearbyint((ms - vp) / spp));
        const int d = std::abs(m_px - click_rel_x);
        // Nearest wins; an exact distance tie (legal now that markers may
        // overlap exactly) goes to the higher index via <=. The stem
        // painters walk the store in ascending index order, so the later
        // equal-time marker paints last and sits visually on top — the
        // tie-break picks the marker the user sees.
        if (d <= kMarkerHitHalfPx && d <= best_dist) {
            best_dist = d;
            best_hit  = i;
        }
    }
    return best_hit;
}

TrimHit hit_test_trim_boundary(const AppState& app, const GuiAudio& audio,
                               int mouse_x) {
    // Trim bounds hit-test like markers in the AUTHORING views, against
    // the active A/B tab's live bounds.
    const bool has_begin = app.trim.has_begin;
    const bool has_end   = app.trim.has_end;
    if (!has_begin && !has_end) return TrimHit::None;
    const int64_t begin_frame = app.trim.begin_frame;
    const int64_t end_frame   = app.trim.end_frame;

    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return TrimHit::None;
    const int sr = audio.sample_rate();
    if (sr <= 0) return TrimHit::None;
    const int click_rel_x = mouse_x - area.x;
    const double vp = static_cast<double>(app.viewport_start_sample);

    // Same translation as hit_test_marker_line: trim is stored
    // source-domain, painted at map_source_to_target columns in the mapped
    // views. The context's map is empty (identity) in source view and the
    // live map in target view.
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    const std::vector<WarpFrameMapSegment>* target_warp_frame_map =
        ctx.warp_frame_map->empty() ? nullptr : ctx.warp_frame_map;

    auto bound_dist = [&](int64_t frame, bool present) -> int {
        if (!present) return kMarkerHitHalfPx + 1;
        const int64_t src_sample = frame;
        double ms = static_cast<double>(src_sample);
        if (target_warp_frame_map) {
            const size_t q = (src_sample < 0)
                ? static_cast<size_t>(0)
                : static_cast<size_t>(src_sample);
            ms = std::nearbyint(map_source_to_target(q, *target_warp_frame_map));
        }
        // No viewport gate: the kMarkerHitHalfPx halo is the single reach
        // test, uniform with markers. A bound up to kMarkerHitHalfPx past
        // either strip edge is grabbable from the nearest onscreen pixels.
        // The motivating case is the at-EOF end bound, which rests exactly one
        // pixel past the last visible column at maximum scroll; the gate made
        // it unreachable. b_px may be negative or >= the strip width.
        const int b_px = static_cast<int>(std::nearbyint((ms - vp) / spp));
        return std::abs(b_px - click_rel_x);
    };

    const int db = bound_dist(begin_frame, has_begin);
    const int de = bound_dist(end_frame, has_end);
    const bool begin_ok = db <= kMarkerHitHalfPx;
    const bool end_ok   = de <= kMarkerHitHalfPx;
    if (begin_ok && end_ok) return (db <= de) ? TrimHit::Begin : TrimHit::End;
    if (begin_ok) return TrimHit::Begin;
    if (end_ok)   return TrimHit::End;
    return TrimHit::None;
}

TrimHit hit_test_trim_chip(const AppState& app, const GuiAudio& audio,
                           int mouse_x, int mouse_y) {
    // Same bound sourcing as hit_test_trim_boundary: the active A/B tab's live
    // bounds in the AUTHORING views. Only set bounds are testable.
    const bool has_begin = app.trim.has_begin;
    const bool has_end   = app.trim.has_end;
    if (!has_begin && !has_end) return TrimHit::None;
    const int64_t begin_frame = app.trim.begin_frame;
    const int64_t end_frame   = app.trim.end_frame;

    // The b/e chips fill top_upper_row_area exactly (the painted
    // chip box top/height equal this row). A press outside that vertical band
    // is not on a chip — the column-based stem test handles the rest.
    const GuiRect row = top_upper_row_area(app);
    if (mouse_y < row.y || mouse_y >= row.y + row.h) return TrimHit::None;

    const GuiRect top = top_strip_area(app);
    // Effective waveform width: the trim chip's visibility cull must match the
    // painter's viewport extent (render_trim_flags maps against this width),
    // so a gutter column at a non-multiple-of-16 window is culled the same in
    // paint and hit-test. Equal to top.w at 1920/2560/3840.
    const int wave_w = waveform_area(app).w;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return TrimHit::None;
    const int sr = audio.sample_rate();
    if (sr <= 0) return TrimHit::None;
    const double vp = static_cast<double>(app.viewport_start_sample);

    // Same translation as hit_test_trim_boundary so the chip column lands
    // where the stem (and chip) are painted in the mapped views: the
    // context's map is empty (identity) in source view and the live map in
    // target view.
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    const std::vector<WarpFrameMapSegment>* target_warp_frame_map =
        ctx.warp_frame_map->empty() ? nullptr : ctx.warp_frame_map;

    // Build the same visible, sorted candidate list render_trim_flags paints.
    // The painter culls a bound whose column is outside the viewport, then
    // greedily elides the right chip when two chip rectangles overlap. Hit
    // testing must consume that final visible list: considering both raw
    // rectangles lets a click on the one painted chip select the elided bound,
    // while considering an off-viewport rectangle makes an unpainted chip
    // reachable at the strip edge.
    struct TrimChipHit {
        double  text_left;
        GuiRect rect;
        TrimHit which;
    };
    std::vector<TrimChipHit> chips;
    auto add_chip = [&](int64_t frame, bool present, TrimHit which) {
        if (!present) return;
        const int64_t src_sample = frame;
        double ms = static_cast<double>(src_sample);
        if (target_warp_frame_map) {
            const size_t q = (src_sample < 0)
                ? static_cast<size_t>(0)
                : static_cast<size_t>(src_sample);
            ms = std::nearbyint(map_source_to_target(q, *target_warp_frame_map));
        }
        const int64_t vp_end = app.viewport_start_sample +
            static_cast<int64_t>(std::nearbyint(spp * wave_w));
        if (ms < vp || ms >= static_cast<double>(vp_end)) return;
        const double x_raw = (ms - vp) / spp;
        const double text_left =
            static_cast<double>(top.x) + std::nearbyint(x_raw);
        // Width from the cached monospace advance via the shared helper — no
        // scratch-surface measurement (that was the residual edge drift: paint
        // measured on the window surface, hit on a 1x1 scratch surface, and the
        // integer width diverged by 1px). The glyph is one ASCII char ("b"/"e"),
        // so the count is 1. baseline_y is irrelevant to the x/w this test uses
        // (the vertical band was already checked via top_upper_row_area above),
        // so pass 0.0 — only rx/rw are read.
        const GuiRect cr_rect = flag_chip_rect(text_left, 1, 0.0);
        chips.push_back({text_left, cr_rect, which});
    };

    add_chip(begin_frame, has_begin, TrimHit::Begin);
    add_chip(end_frame,   has_end,   TrimHit::End);
    std::sort(chips.begin(), chips.end(),
              [](const TrimChipHit& a, const TrimChipHit& b) {
                  if (a.text_left != b.text_left)
                      return a.text_left < b.text_left;
                  return a.which == TrimHit::Begin && b.which == TrimHit::End;
              });

    const double hl_pad = flag_pad_x_px();
    double rightmost_right_edge = -1e18;
    for (const TrimChipHit& chip : chips) {
        if (chip.text_left < rightmost_right_edge + hl_pad) continue;
        rightmost_right_edge =
            chip.text_left + monospace_advance() + hl_pad;
        if (mouse_x >= chip.rect.x &&
            mouse_x < chip.rect.x + chip.rect.w) {
            return chip.which;
        }
    }
    return TrimHit::None;
}

int hit_test_flag(const AppState& app, const GuiAudio& audio,
                  int mouse_x, int mouse_y) {
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    const int64_t vp_start = app.viewport_start_sample;
    const int64_t vp_end = vp_start +
        static_cast<int64_t>(std::nearbyint(spp * area.w));
    // The mapped views' flags paint at translated positions
    // (compute_flag_hit_rects with a non-null warp_frame_map), so hit-test
    // must walk the same warp_frame_map. The active display context is the
    // base: its map is empty (identity) in source view, and in target view it
    // is the memoized target map paint also walks — stable for a drag's
    // lifetime, so hit-rect positions match the mid-drag paint.
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    const std::vector<WarpFrameMapSegment>* tmap_arg = nullptr;
    if (ctx.domain == GuiDisplayDomain::TargetLive &&
        !ctx.warp_frame_map->empty()) {
        tmap_arg = ctx.warp_frame_map;
    }
    DragOverlay drag_overlay_storage;
    const DragOverlay* drag_overlay = nullptr;
    if (app.drag.active) {
        drag_overlay_storage.indices = &app.drag.dragging_markers;
        drag_overlay_storage.times   = &app.drag.moveable_times;
        drag_overlay = &drag_overlay_storage;
    }
    // This two-way branch chain is the sole hit-rect builder: the chip
    // paint and this hit test share flag_chip_rect, so the rects computed
    // here are exactly the painted chip geometry.
    std::vector<FlagHitRect> rects;
    if (app.active_markers_view == 'P') {
        rects = compute_phase_reset_flag_hit_rects(
            top, area.w, app.phaseresetmarkers.markers(),
            vp_start, vp_end, audio.sample_rate(),
            tmap_arg, drag_overlay);
    } else {
        // Warp hit-rects must track the bracketed flag width so
        // clicks land on the iteration-mode chip.
        rects = compute_flag_hit_rects(
            top, area.w, app.warpmarkers.markers(),
            vp_start, vp_end, audio.sample_rate(),
            tmap_arg, drag_overlay,
            app.iteration_mode_enabled);
    }
    for (const auto& r : rects) {
        if (mouse_x >= r.x && mouse_x < r.x + r.w &&
            mouse_y >= r.y && mouse_y < r.y + r.h) {
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
    // Render resolution (resolve_warp_markers_for_render) drops disabled markers
    // outright and drops label refs whose definition is disabled (the
    // cascade). The popup must not report a tempo the render never applies,
    // so eligibility mirrors both drops here: a disabled marker is
    // ineligible, and a ref to a disabled definition is ineligible. A ref
    // whose definition is missing entirely stays eligible —
    // compute_hover_popup_text already yields an empty string for that case
    // and the display sites suppress empty popups, so it never surfaces a
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
