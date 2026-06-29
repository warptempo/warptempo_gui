#include "app_state.h"

#include "audio.h"
#include "paint_handler.h"
#include "render.h"
#include "text_editor.h"
#include "frame_map_view.h"
#include "frame_map.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

SettingsSnapshot capture_current_settings(const AppState& app) {
    SettingsSnapshot s;
    s.engine_settings = app.engine_settings;
    // Trim is excluded from undo/redo history. The trim fields below are left
    // uncaptured, and the matching restores in undo.cpp (do_undo / do_redo),
    // the focus_restored_trim calls, and the settings-editor trim push are all
    // commented out under the same note, so undo/redo never touches trim.
    // It behaves like viewport state: the active tab is mirrored in app.trim,
    // saved to disk by settings_io, but absent from the undo stack. To roll back, grep
    // "excluded from undo/redo history" across src/gui and uncomment every
    // tagged line (this file, undo.cpp, settings_editor.cpp, input_trim.cpp,
    // input_handler.cpp), and re-enable the two focus_restored_trim calls.
    // s.trim_begin      = app.trim.begin_seconds;
    // s.trim_end        = app.trim.end_seconds;
    // s.has_trim_begin  = app.trim.has_begin;
    // s.has_trim_end    = app.trim.has_end;
    return s;
}

// Promoted from a lambda in main(). The AppState is reached through
// the explicit argument rather than a capture.
bool bottom_strip_wide(const AppState& app) {
    return app.prompt.active ||
           !app.queue_progress_text.empty() ||
           text_editor::is_active(app.settings_editor) ||
           // The BPM editor reuses top_flag_editor with the
           // BpmBracket kind but paints in the bottom strip, so it widens
           // the strip like the settings editor. A FlagPayload /
           // IterationBracket top_flag_editor edits over the flag in the
           // top strip and must NOT widen the bottom strip.
           (text_editor::is_active(app.top_flag_editor) &&
            app.top_flag_editor.kind == text_editor::Kind::BpmBracket);
}

// hit_test_* promoted from lambdas in main(). The captured `app` and
// `audio` references are now explicit arguments. The
// kMarkerHitHalfPx / kFlagFontSize constants resolve through app_state.h /
// paint_handler.h respectively.

int hit_test_marker_line(const AppState& app, const GuiAudio& audio,
                         int mouse_x) {
    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return -1;
    const int sr = audio.sample_rate();
    const int click_rel_x = mouse_x - area.x;
    const double vp = static_cast<double>(app.viewport_start_sample);
    const int64_t visible = samples_visible(app, audio);
    int best_hit = -1;
    int best_dist = kMarkerHitHalfPx + 1;
    const bool rv = app.render_view.enabled;
    // In render-view, the visible sub-view's
    // list drives hit-testing. 'P' reads phase reset time_seconds and
    // converts to source frames via the source sample rate
    // (matching source-view's phase reset branch).
    const bool rv_trans = rv && app.active_markers_view == 'P';
    const int n =
        rv_trans
            ? static_cast<int>(app.render_view.phase_resets.size())
            : rv
                ? static_cast<int>(app.render_view.markers.size())
                : (app.active_markers_view == 'P')
                    ? static_cast<int>(app.phase_reset_markers.markers().size())
                    : static_cast<int>(app.warpmarkers.markers().size());
    // Target view paints marker stems at map_source_to_target
    // translated positions; the hit test must walk the same frame_map so
    // mouse_x lands on the visually-drawn stem, not the marker's source-
    // frame position. compute_flag_hit_rects already does this on the
    // top strip; this mirrors that for the waveform-area marker line.
    std::vector<FrameMapSegment> target_frame_map;
    if (!rv && app.active_audio_view == 'T') {
        target_frame_map = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
    }
    const bool use_tmap = !target_frame_map.empty();
    for (int i = 0; i < n; ++i) {
        double ms;
        if (rv_trans) {
            ms = app.render_view.phase_resets[i].time_seconds *
                 static_cast<double>(sr);
        } else if (rv) {
            ms = app.render_view.markers[i].time_seconds *
                 static_cast<double>(sr);
        } else if (app.active_markers_view == 'P') {
            ms = app.phase_reset_markers.markers()[i].time_seconds *
                 static_cast<double>(sr);
        } else {
            ms = app.warpmarkers.markers()[i].time_seconds *
                 static_cast<double>(sr);
        }
        if (use_tmap) {
            const size_t q = (ms < 0.0)
                ? static_cast<size_t>(0)
                : static_cast<size_t>(std::llrint(ms));
            ms = map_source_to_target(q, target_frame_map);
        }
        if (ms < vp) continue;
        if (ms >= vp + static_cast<double>(visible)) continue;
        const int m_px = static_cast<int>(std::nearbyint((ms - vp) / spp));
        const int d = std::abs(m_px - click_rel_x);
        if (d <= kMarkerHitHalfPx && d < best_dist) {
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
    const int64_t visible = samples_visible(app, audio);

    // Same target-view translation as hit_test_marker_line: trim is stored
    // source-domain, painted at map_source_to_target columns in target view.
    std::vector<FrameMapSegment> target_frame_map;
    if (app.active_audio_view == 'T') {
        target_frame_map = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
    }
    const bool use_tmap = !target_frame_map.empty();

    auto bound_dist = [&](double seconds, bool present) -> int {
        if (!present) return kMarkerHitHalfPx + 1;
        double ms = std::nearbyint(seconds * sr_d);
        if (use_tmap) {
            const size_t q = (ms < 0.0)
                ? static_cast<size_t>(0)
                : static_cast<size_t>(std::llrint(ms));
            ms = map_source_to_target(q, target_frame_map);
        }
        if (ms < vp) return kMarkerHitHalfPx + 1;
        if (ms >= vp + static_cast<double>(visible)) return kMarkerHitHalfPx + 1;
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
    const int64_t visible = samples_visible(app, audio);

    // Same target-view translation as hit_test_trim_boundary so the chip
    // column lands where the stem (and chip) are painted in target view.
    std::vector<FrameMapSegment> target_frame_map;
    if (app.active_audio_view == 'T') {
        target_frame_map = build_target_view_frame_map(
            app, sr, static_cast<long>(audio.total_frames()));
    }
    const bool use_tmap = !target_frame_map.empty();

    const int kMiss = std::numeric_limits<int>::max();

    // Per-bound chip rect, computed exactly as render_trim_flags paints it via
    // the shared flag_chip_rect helper: text_left at the bound's integer pixel
    // column (top.x + round(x_raw)), the rect spanning
    // [round(text_left), round(text_left) + round(advance + 2*kFlagPadXPx)] —
    // the painted chip's left edge through its right pad, the same horizontal
    // geometry compute_flag_hit_rects derives for regular flags. The vertical band (top_upper_row_area) was checked above, so only
    // horizontal containment is tested here. Returns the bound's distance from
    // the press to its column when the press is inside the chip (for the
    // both-hit tie-break), else kMiss.
    auto chip_dist = [&](double seconds, bool present,
                         const char* /*glyph*/) -> int {
        if (!present) return kMiss;
        double ms = std::nearbyint(seconds * sr_d);
        if (use_tmap) {
            const size_t q = (ms < 0.0)
                ? static_cast<size_t>(0)
                : static_cast<size_t>(std::llrint(ms));
            ms = map_source_to_target(q, target_frame_map);
        }
        if (ms < vp) return kMiss;
        if (ms >= vp + static_cast<double>(visible)) return kMiss;
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
    // Render-view's phase reset sub-view paints no
    // flags; short-circuit to no-hit so click and hover paths see a
    // bare top strip.
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
    // (compute_flag_hit_rects with a non-null frame_map), so hit-test
    // must walk the same frame_map. Build it locally — same construction
    // as paint_handler's on_redraw, trim forced off. Empty in source-
    // view and render-view; the helper's nullptr-when-empty pass-through
    // below leaves those paths untouched.
    //
    // During a drag, route through the frozen frame_map captured at
    // begin_drag so hit-rect positions match the frozen-coord paint.
    std::vector<FrameMapSegment> target_frame_map;
    if (!app.render_view.enabled &&
        app.active_audio_view == 'T') {
        if (app.drag.active) {
            target_frame_map = app.drag.frozen_frame_map;
        } else {
            target_frame_map = build_target_view_frame_map(
                app, audio.sample_rate(),
                static_cast<long>(audio.total_frames()));
        }
    }
    const std::vector<FrameMapSegment>* tmap_arg =
        target_frame_map.empty() ? nullptr : &target_frame_map;
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
            top, app.render_view.markers,
            vp_start, vp_end, audio.sample_rate(), kFlagFontSize,
            nullptr, drag_overlay);
    } else if (app.active_markers_view == 'P') {
        rects = compute_phase_reset_flag_hit_rects(
            top, app.phase_reset_markers.markers(),
            vp_start, vp_end, audio.sample_rate(), kFlagFontSize,
            tmap_arg, drag_overlay);
    } else {
        // Warp hit-rects must track the bracketed flag width so
        // clicks land on the iteration-mode chip. This branch is reached
        // only in warp view (not render view, not 'P').
        rects = compute_flag_hit_rects(
            top, app.warpmarkers.markers(),
            vp_start, vp_end, audio.sample_rate(), kFlagFontSize,
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
        const auto& mv = app.render_view.markers;
        if (idx >= static_cast<int>(mv.size())) return false;
        const auto& m = mv[idx];
        return m.tempo_inherits || !m.label_ref.empty();
    }
    if (app.active_markers_view != 'W') return false;
    if (app.iteration_mode_enabled) return false;
    const auto& mv = app.warpmarkers.markers();
    if (idx >= static_cast<int>(mv.size())) return false;
    const auto& m = mv[idx];
    return m.tempo_inherits || !m.label_ref.empty();
}
