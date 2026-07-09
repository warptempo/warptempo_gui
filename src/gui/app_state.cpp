#include "app_state.h"

#include "audio.h"
#include "paint_handler.h"
#include "render.h"
#include "text_editor.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"

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
// references are now explicit arguments. The kMarkerHitHalfPx / flag_font_size_px()
// constants resolve through app_state.h / paint_handler.h respectively.

int hit_test_marker_line(const AppState& app, const GuiAudio& audio,
                         int mouse_x) {
    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return -1;
    const int sr = audio.sample_rate();
    const int click_rel_x = mouse_x - area.x;
    const double vp = static_cast<double>(app.viewport_start_sample);
    int best_hit = -1;
    int best_dist = kMarkerHitHalfPx + 1;
    const bool rv = app.render_view.enabled;
    // In render-view, the visible sub-view's list drives hit-testing. 'P'
    // reads phase reset time_seconds and converts to source frames via the
    // source sample rate (matching source-view's phase reset branch).
    const bool rv_phase_reset = rv && app.active_markers_view == 'P';
    const int n =
        rv_phase_reset
            ? static_cast<int>(app.render_view.phase_resets.size())
            : rv
                ? static_cast<int>(app.render_view.warp_markers.size())
                : (app.active_markers_view == 'P')
                    ? static_cast<int>(app.phaseresetmarkers.markers().size())
                    : static_cast<int>(app.warpmarkers.markers().size());
    // Target view paints marker stems at map_source_to_target
    // translated positions; the hit test must walk the same warp_frame_map so
    // mouse_x lands on the visually-drawn stem, not the marker's source-
    // frame position. compute_flag_hit_rects already does this on the
    // top strip; this mirrors that for the waveform-area marker line.
    const std::vector<WarpFrameMapSegment>* target_warp_frame_map = nullptr;
    if (!rv && app.active_audio_view == 'T') {
        // target_view_warp_frame_map_cached stores segments in app.target_warp_frame_map_cache.warp_frame_map,
        // which outlives this call. The reference remains valid until a
        // changed-key rebuild, and this function does not mutate markers,
        // scale, or audio identity while holding it.
        const auto& m = target_view_warp_frame_map_cached(
            app, sr, static_cast<long>(audio.total_frames())).warp_frame_map;
        if (!m.empty()) target_warp_frame_map = &m;
    }
    for (int i = 0; i < n; ++i) {
        int64_t src_sample;
        if (rv_phase_reset) {
            src_sample = static_cast<int64_t>(std::nearbyint(
                app.render_view.phase_resets[i].time_seconds *
                static_cast<double>(sr)));
        } else if (rv) {
            src_sample = static_cast<int64_t>(std::nearbyint(
                app.render_view.warp_markers[i].time_seconds *
                static_cast<double>(sr)));
        } else if (app.active_markers_view == 'P') {
            src_sample = static_cast<int64_t>(std::nearbyint(
                app.phaseresetmarkers.markers()[i].time_seconds *
                static_cast<double>(sr)));
        } else {
            src_sample = static_cast<int64_t>(std::nearbyint(
                app.warpmarkers.markers()[i].time_seconds *
                static_cast<double>(sr)));
        }
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
    // Trim editing is a source-view authoring gesture against the active
    // A/B tab; render-view has its own (trim-less) display.
    if (app.render_view.enabled) return TrimHit::None;
    if (!app.trim.has_begin && !app.trim.has_end) return TrimHit::None;

    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return TrimHit::None;
    const int sr = audio.sample_rate();
    if (sr <= 0) return TrimHit::None;
    const double sr_d = static_cast<double>(sr);
    const int click_rel_x = mouse_x - area.x;
    const double vp = static_cast<double>(app.viewport_start_sample);

    // Same target-view translation as hit_test_marker_line: trim is stored
    // source-domain, painted at map_source_to_target columns in target view.
    const std::vector<WarpFrameMapSegment>* target_warp_frame_map = nullptr;
    if (app.active_audio_view == 'T') {
        const auto& m = target_view_warp_frame_map_cached(
            app, sr, static_cast<long>(audio.total_frames())).warp_frame_map;
        if (!m.empty()) target_warp_frame_map = &m;
    }

    auto bound_dist = [&](double seconds, bool present) -> int {
        if (!present) return kMarkerHitHalfPx + 1;
        const int64_t src_sample = static_cast<int64_t>(
            std::nearbyint(seconds * sr_d));
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

    const int db = bound_dist(app.trim.begin_seconds, app.trim.has_begin);
    const int de = bound_dist(app.trim.end_seconds, app.trim.has_end);
    const bool begin_ok = db <= kMarkerHitHalfPx;
    const bool end_ok   = de <= kMarkerHitHalfPx;
    if (begin_ok && end_ok) return (db <= de) ? TrimHit::Begin : TrimHit::End;
    if (begin_ok) return TrimHit::Begin;
    if (end_ok)   return TrimHit::End;
    return TrimHit::None;
}

TrimHit hit_test_trim_chip(const AppState& app, const GuiAudio& audio,
                           int mouse_x, int mouse_y) {
    // Same gating as hit_test_trim_boundary: source-view authoring against
    // the active A/B tab, only set bounds are testable.
    if (app.render_view.enabled) return TrimHit::None;
    if (!app.trim.has_begin && !app.trim.has_end) return TrimHit::None;

    // The b/e chips fill top_upper_row_area exactly (the painted
    // chip box top/height equal this row). A press outside that vertical band
    // is not on a chip — the column-based stem test handles the rest.
    const GuiRect row = top_upper_row_area(app);
    if (mouse_y < row.y || mouse_y >= row.y + row.h) return TrimHit::None;

    const GuiRect top = top_strip_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return TrimHit::None;
    const int sr = audio.sample_rate();
    if (sr <= 0) return TrimHit::None;
    const double sr_d = static_cast<double>(sr);
    const double vp = static_cast<double>(app.viewport_start_sample);

    // Same target-view translation as hit_test_trim_boundary so the chip
    // column lands where the stem (and chip) are painted in target view.
    const std::vector<WarpFrameMapSegment>* target_warp_frame_map = nullptr;
    if (app.active_audio_view == 'T') {
        const auto& m = target_view_warp_frame_map_cached(
            app, sr, static_cast<long>(audio.total_frames())).warp_frame_map;
        if (!m.empty()) target_warp_frame_map = &m;
    }

    const int kMiss = std::numeric_limits<int>::max();

    // Per-bound chip rect, computed exactly as render_trim_flags paints it via
    // the shared flag_chip_rect helper: text_left at the bound's integer pixel
    // column (top.x + round(x_raw)), the rect spanning
    // [round(text_left), round(text_left) + round(advance + 2*flag_pad_x_px())] —
    // the painted chip's left edge through its right pad, the same horizontal
    // geometry compute_flag_hit_rects derives for regular flags. The vertical band (top_upper_row_area) was checked above, so only
    // horizontal containment is tested here. Returns the bound's distance from
    // the press to its column when the press is inside the chip (for the
    // both-hit tie-break), else kMiss.
    auto chip_dist = [&](double seconds, bool present,
                         const char* /*glyph*/) -> int {
        if (!present) return kMiss;
        const int64_t src_sample = static_cast<int64_t>(
            std::nearbyint(seconds * sr_d));
        double ms = static_cast<double>(src_sample);
        if (target_warp_frame_map) {
            const size_t q = (src_sample < 0)
                ? static_cast<size_t>(0)
                : static_cast<size_t>(src_sample);
            ms = std::nearbyint(map_source_to_target(q, *target_warp_frame_map));
        }
        // No viewport pre-gate: a chip whose box is partially visible at a
        // strip edge hit-tests naturally through the horizontal-containment
        // check below. A fully offscreen box misses by containment, which is
        // correct — the stem halo (hit_test_trim_boundary) is the reach path
        // for those.
        const double x_raw = (ms - vp) / spp;
        const double text_left =
            static_cast<double>(top.x) + std::round(x_raw);
        // Width from the cached monospace advance via the shared helper — no
        // scratch-surface measurement (that was the residual edge drift: paint
        // measured on the window surface, hit on a 1x1 scratch surface, and the
        // integer width diverged by 1px). The glyph is one ASCII char ("b"/"e"),
        // so the count is 1. baseline_y is irrelevant to the x/w this test uses
        // (the vertical band was already checked via top_upper_row_area above),
        // so pass 0.0 — only rx/rw are read.
        const GuiRect cr_rect = flag_chip_rect(text_left, 1, 0.0);
        const int rx = cr_rect.x;
        const int rw = cr_rect.w;
        if (mouse_x < rx || mouse_x >= rx + rw) return kMiss;
        return std::abs(mouse_x - rx);
    };

    const int db = chip_dist(app.trim.begin_seconds, app.trim.has_begin, "b");
    const int de = chip_dist(app.trim.end_seconds,   app.trim.has_end,   "e");

    // Overlapping chips: the renderer elides the right one, but guard the tie
    // by preferring the bound whose column is nearer the press, mirroring
    // hit_test_trim_boundary.
    if (db != kMiss && de != kMiss) return (db <= de) ? TrimHit::Begin
                                                      : TrimHit::End;
    if (db != kMiss) return TrimHit::Begin;
    if (de != kMiss) return TrimHit::End;
    return TrimHit::None;
}

int hit_test_flag(const AppState& app, const GuiAudio& audio,
                  int mouse_x, int mouse_y) {
    // Render-view's phase reset sub-view paints no flags; short-circuit to
    // no-hit so click and hover paths see a bare top strip.
    if (app.render_view.enabled &&
        app.active_markers_view == 'P') {
        return -1;
    }
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    const int64_t vp_start = app.viewport_start_sample;
    const int64_t vp_end = vp_start +
        static_cast<int64_t>(std::nearbyint(spp * area.w));
    // Target view's flags paint at translated positions
    // (compute_flag_hit_rects with a non-null warp_frame_map), so hit-test
    // must walk the same warp_frame_map. Build it locally — same construction
    // as paint_handler's on_redraw, trim forced off. Empty in source-
    // view and render-view; the helper's nullptr-when-empty pass-through
    // below leaves those paths untouched.
    //
    // During a drag, route through the frozen warp_frame_map captured at
    // begin_drag so hit-rect positions match the frozen-coord paint.
    const std::vector<WarpFrameMapSegment>* tmap_arg = nullptr;
    if (!app.render_view.enabled &&
        app.active_audio_view == 'T') {
        if (app.drag.active) {
            if (!app.drag.frozen_warp_frame_map.empty())
                tmap_arg = &app.drag.frozen_warp_frame_map;
        } else {
            const auto& m = target_view_warp_frame_map_cached(
                app, audio.sample_rate(),
                static_cast<long>(audio.total_frames())).warp_frame_map;
            if (!m.empty()) tmap_arg = &m;
        }
    }
    DragOverlay drag_overlay_storage;
    const DragOverlay* drag_overlay = nullptr;
    if (app.drag.active) {
        drag_overlay_storage.indices = &app.drag.dragging_markers;
        drag_overlay_storage.times   = &app.drag.moveable_times;
        drag_overlay = &drag_overlay_storage;
    }
    std::vector<FlagHitRect> rects;
    if (app.render_view.enabled) {
        rects = compute_flag_hit_rects(
            top, app.render_view.warp_markers,
            vp_start, vp_end, audio.sample_rate(), flag_font_size_px(),
            nullptr, drag_overlay);
    } else if (app.active_markers_view == 'P') {
        rects = compute_phase_reset_flag_hit_rects(
            top, app.phaseresetmarkers.markers(),
            vp_start, vp_end, audio.sample_rate(), flag_font_size_px(),
            tmap_arg, drag_overlay);
    } else {
        // Warp hit-rects must track the bracketed flag width so
        // clicks land on the iteration-mode chip. This branch is reached
        // only in warp view (not render view, not 'P').
        rects = compute_flag_hit_rects(
            top, app.warpmarkers.markers(),
            vp_start, vp_end, audio.sample_rate(), flag_font_size_px(),
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
    if (app.render_view.enabled) {
        // In render-view, hover popups apply against the loaded
        // render's warpmarkers regardless of the pre-toggle mode.
        // Iteration-mode is forced off on toggle-in so its gate is
        // implicitly satisfied here too.
        const auto& mv = app.render_view.warp_markers;
        if (idx >= static_cast<int>(mv.size())) return false;
        const auto& m = mv[idx];
        return m.tempo_inherits || !m.label_ref.empty();
    }
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
