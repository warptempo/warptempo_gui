#include "paint_handler.h"

#include "render.h"
#include "text_display.h"
#include "text_editor.h"
#include "timemap.h"
#include "waveform_worker.h"
#include "engine/stft_container.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

// X.7.8a: paint cluster. Method bodies are byte-identical to the lambdas
// they replaced in main.cpp (set_on_redraw at the original main.cpp:999
// and set_on_resize at the original main.cpp:1892). The only changes are:
//
//   - Capture-by-reference of `app`, `audio`, `playback`, `wf_cache`,
//     `gui` is now reference-member access on `this`. Identifier spelling
//     is identical so nothing else changes inside the bodies.
//   - `bottom_strip_wide()` (the old lambda capture) is replaced with the
//     free-function form `bottom_strip_wide(app)` declared in app_state.h.
//
// IterPopupHit / BpmPopupHit and the compute_*_popup_hits helpers below
// were also extracted out of main.cpp's anonymous namespace because paint
// uses them and other (non-paint) main.cpp callsites reach them through
// the same paint_handler.h include.

// -- compute_iter_popup_hits / compute_bpm_popup_hits --------------------
//
// Bodies copied verbatim from the original main.cpp anonymous-namespace
// definitions; the only change is removing `inline` (these now have
// external linkage as paint_handler.cpp is their sole TU of definition).

std::vector<IterPopupHit> compute_iter_popup_hits(
    cairo_t* cr,
    GuiRect top_strip_area,
    const std::vector<GuiWarpMarker>& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::vector<TimeMapSegment>* timemap,
    const DragOverlay* drag_overlay) {
    std::vector<IterPopupHit> out;
    auto rects = compute_flag_hit_rects(
        cr, top_strip_area, markers,
        viewport_start_sample, viewport_end_sample,
        sample_rate, font_size, timemap, drag_overlay);
    if (rects.empty()) return out;

    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    // The widest possible iteration text drives a uniform hit-rect width
    // so popups don't visibly jiggle in size as values change. Matches
    // the [%+0.2f,%+0.2f] format with single-digit integer parts.
    cairo_text_extents_t uniform_ext;
    cairo_text_extents(cr, "[+0.00,+0.00]", &uniform_ext);
    const double hl_pad = kFlagInnerPadPx;

    // Greedy left-to-right elision over popup positions. Brief Y.4 sub-bug
    // B: collision is computed against the popup's actual painted-text
    // width plus 2 * kFlagInnerPadPx — i.e., the on-screen extent of the
    // bg-fill rect, not the uniform [+0.00,+0.00] hit_rect.w. The hit_rect
    // stays uniform-width so click targets are stable as values change;
    // pack and paint are separate concerns. With this rule, two adjacent
    // owning markers whose painted popup texts (e.g. "[ ]") don't actually
    // overlap will both render, even if their uniform hit rects do — which
    // matches the flag pack in iterate_visible_flags_impl. No editor exemption.
    const double pop_pad = 4.0;
    double rightmost_right_edge = -1e18;
    for (const auto& r : rects) {
        const int idx = r.marker_index;
        if (idx < 0 || idx >= static_cast<int>(markers.size())) continue;
        if (!iter_popup_eligible_marker(markers[idx])) continue;
        IterPopupHit h;
        h.marker_index = idx;
        h.flag_rect.x = static_cast<int>(std::lround(r.x));
        h.flag_rect.y = static_cast<int>(std::lround(r.y));
        h.flag_rect.w = static_cast<int>(std::lround(r.w));
        h.flag_rect.h = static_cast<int>(std::lround(r.h));
        h.text = format_iter_bracket_text(markers[idx]);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, h.text.c_str(), &ext);
        const int popup_w =
            static_cast<int>(std::ceil(uniform_ext.x_advance + 2 * hl_pad));
        const int popup_h = h.flag_rect.h;
        h.hit_rect.x = h.flag_rect.x;
        h.hit_rect.y = h.flag_rect.y -
            static_cast<int>(std::lround(kIterPopupVerticalGapPx)) -
            popup_h;
        h.hit_rect.w = popup_w;
        h.hit_rect.h = popup_h;

        // Pack collision uses the painted-extent width (matches the bg-
        // fill rect from sub-bug A), not h.hit_rect.w. By construction
        // the pack rule and the visual occlusion rule agree.
        const double pack_w = ext.x_advance + 2.0 * hl_pad;
        const double left = static_cast<double>(h.hit_rect.x);
        if (left < rightmost_right_edge + pop_pad) continue;
        rightmost_right_edge = left + pack_w;
        out.push_back(h);
    }
    cairo_restore(cr);
    return out;
}

std::vector<BpmPopupHit> compute_bpm_popup_hits(
    cairo_t* cr,
    GuiRect top_strip_area,
    const std::vector<GuiWarpMarker>& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::vector<TimeMapSegment>* timemap,
    const DragOverlay* drag_overlay) {
    std::vector<BpmPopupHit> out;
    auto rects = compute_flag_hit_rects(
        cr, top_strip_area, markers,
        viewport_start_sample, viewport_end_sample,
        sample_rate, font_size, timemap, drag_overlay);
    if (rects.empty()) return out;

    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    cairo_text_extents_t uniform_ext;
    cairo_text_extents(cr, "99@[999,999]", &uniform_ext);
    const double hl_pad = kFlagInnerPadPx;

    const double pop_pad = 4.0;
    double rightmost_right_edge = -1e18;
    for (const auto& r : rects) {
        const int idx = r.marker_index;
        if (idx < 0 || idx >= static_cast<int>(markers.size())) continue;
        if (!bpm_popup_eligible_marker(markers[idx])) continue;
        if (!markers[idx].bpm_is_popup_owner) continue;
        BpmPopupHit h;
        h.marker_index = idx;
        h.flag_rect.x = static_cast<int>(std::lround(r.x));
        h.flag_rect.y = static_cast<int>(std::lround(r.y));
        h.flag_rect.w = static_cast<int>(std::lround(r.w));
        h.flag_rect.h = static_cast<int>(std::lround(r.h));
        h.text = format_bpm_bracket_text(markers[idx]);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, h.text.c_str(), &ext);
        const int popup_w =
            static_cast<int>(std::ceil(uniform_ext.x_advance + 2 * hl_pad));
        const int popup_h = h.flag_rect.h;
        h.hit_rect.x = h.flag_rect.x;
        h.hit_rect.y = h.flag_rect.y -
            static_cast<int>(std::lround(kIterPopupVerticalGapPx)) -
            popup_h;
        h.hit_rect.w = popup_w;
        h.hit_rect.h = popup_h;

        const double pack_w = ext.x_advance + 2.0 * hl_pad;
        const double left = static_cast<double>(h.hit_rect.x);
        if (left < rightmost_right_edge + pop_pad) continue;
        rightmost_right_edge = left + pack_w;
        out.push_back(h);
    }
    cairo_restore(cr);
    return out;
}

// -- render_waveform_to_cache_surface ------------------------------------
//
// Stage A: extracted from on_redraw's inline cairo_create/cairo_destroy
// block (the body that lived between fingerprint-check and blit). Runs on
// the waveform worker thread when the main path goes through GuiWaveformWorker;
// the function itself is thread-agnostic — it touches only the dest surface
// the caller passed in, the audio handle's peak pyramid (read-only after
// load), and the timemap snapshot the caller built. perf_counters
// increments inside render_waveform fire from the worker thread when
// kDebugPerf=true; see the comment in render.h.

void render_waveform_to_cache_surface(
    cairo_surface_t* dest,
    int area_w,
    int area_h,
    int channel_count,
    const GuiAudio& audio,
    int64_t vp_start,
    int64_t vp_end,
    int64_t trim_begin,
    int64_t trim_end,
    const std::vector<TimeMapSegment>* timemap_or_null) {
    if (!dest || area_w <= 0 || area_h <= 0) return;

    cairo_t* ccr = cairo_create(dest);
    // Clear to transparent — the pixmap's background fill shows through
    // wherever the waveform strokes don't paint.
    cairo_save(ccr);
    cairo_set_operator(ccr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(ccr);
    cairo_restore(ccr);
    const GuiRect cache_area{0, 0, area_w, area_h};
    if (channel_count == 1) {
        render_waveform(ccr, cache_area, audio, 0,
                        vp_start, vp_end,
                        trim_begin, trim_end,
                        kWaveform, dim(kWaveform),
                        timemap_or_null);
    } else if (channel_count >= 2) {
        const int ch_h = (cache_area.h - kChannelGapPx) / 2;
        const GuiRect ch0{0, 0, cache_area.w, ch_h};
        const GuiRect ch1{0, ch_h + kChannelGapPx,
                          cache_area.w, ch_h};
        render_waveform(ccr, ch0, audio, 0,
                        vp_start, vp_end,
                        trim_begin, trim_end,
                        kWaveform, dim(kWaveform),
                        timemap_or_null);
        render_waveform(ccr, ch1, audio, 1,
                        vp_start, vp_end,
                        trim_begin, trim_end,
                        kWaveform, dim(kWaveform),
                        timemap_or_null);
    }
    cairo_destroy(ccr);
}

// -- GuiPaintHandler::on_redraw ------------------------------------------

void GuiPaintHandler::on_redraw(cairo_t* cr, int x, int y, int w, int h) {
    using clock = std::chrono::steady_clock;
    const auto t_start = clock::now();

    init_monospace_grid_metrics(cr);

    if constexpr (kDebugPerf) perf_counters::reset();

    double t_waveform_ms = 0.0;
    double t_markers_ms  = 0.0;
    double t_flags_ms    = 0.0;
    double t_playhead_ms = 0.0;
    double t_ts_ms       = 0.0;
    double t_dirty_ms    = 0.0;
    double t_flush_ms    = 0.0;

    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);

    render_background(cr, x, y, w, h);

    if (app.loading) {
        const int bar_y = app.height - kProgressBarHeight;
        render_progress_bar(cr, 0, bar_y, app.width, kProgressBarHeight,
                            app.load_progress);
    } else if (audio.total_frames() > 0) {
        const GuiRect area       = waveform_area(app);
        const GuiRect top_strip  = top_strip_area(app);
        const GuiRect exposed{x, y, w, h};
        const int     sr         = audio.sample_rate();

        // Stage C: live viewport / target-timemap / trim computations
        // that used to drive on_redraw's render_flags / render_markers
        // calls have moved into the cache rebuild paths (waveform via
        // Stage A's worker, stems via maybe_rebuild_stem_cache, flags
        // via maybe_rebuild_flag_cache). on_redraw now reads
        // wf_cache.fp_* for displayed-viewport inputs and treats every
        // strip as a blit-then-overlay path. is_target stays as a live
        // signal because the popup branch below dispatches on it.
        const bool is_target = (app.active_audio_view == 'T') &&
                               !app.render_view_enabled;

        // Drag-time position overlay. Active for the duration of a
        // ctrl-drag; non-null only when app.drag.active. Threaded into
        // render_one_editor_flag and the popup paint paths below so the
        // editor flag and popups track the dragged marker's proposed
        // (moveable_times) position.
        DragOverlay drag_overlay_storage;
        const DragOverlay* drag_overlay = nullptr;
        if (app.drag.active) {
            drag_overlay_storage.indices = &app.drag.dragging_markers;
            drag_overlay_storage.times   = &app.drag.moveable_times;
            drag_overlay = &drag_overlay_storage;
        }

        {
            const auto wf0 = clock::now();

            // Stage A: the synchronous rebuild that used to live in this
            // block now runs on GuiWaveformWorker, kicked off from on_tick
            // via maybe_enqueue_waveform_render. The paint path is
            // blit-only — it draws whatever pixels the live surface
            // currently holds (which may be from a one- or two-frame-old
            // viewport during the worker-rebuild window; Stages B and C
            // close that mismatch by layering markers and flags onto
            // surfaces keyed off the same displayed-viewport).
            //
            // If wf_cache.surface is null (initial load, before the first
            // worker completion), the blit is skipped and the background
            // fill shows through. The user-visible difference is one
            // extra paint frame of background between load and first
            // waveform display, masked by the existing load-time progress
            // bar.
            if (wf_cache.surface && rects_intersect(exposed, area)) {
                cairo_save(cr);
                cairo_rectangle(cr, area.x, area.y, area.w, area.h);
                cairo_clip(cr);
                cairo_set_source_surface(cr, wf_cache.surface,
                                         area.x, area.y);
                cairo_paint(cr);
                cairo_restore(cr);
            }

            const auto wf1 = clock::now();
            t_waveform_ms =
                std::chrono::duration<double, std::milli>(wf1 - wf0).count();
        }

        // Markers: vertical stems in the waveform area, beneath the
        // playhead. Cairo's outer clip confines painting to `exposed`.
        // Gate against the actual stem pixel range: stems emanate from
        // the flag rect's left outline at `area.y - kStemAboveWaveformPx`
        // and run down to `area.y + area.h`. Top-strip damage above the
        // stems' tops (popup edits, hover popup, cursor blink) would
        // otherwise pay for an empty marker pass.
        const GuiRect marker_paint_rect{
            area.x,
            area.y - static_cast<int>(kStemAboveWaveformPx),
            area.w,
            area.h + static_cast<int>(kStemAboveWaveformPx)
        };
        if (rects_intersect(exposed, marker_paint_rect)) {
            const auto m0 = clock::now();
            // Stage B: the marker stems live on stem_cache.surface,
            // rebuilt synchronously from on_tick via
            // maybe_rebuild_stem_cache. The paint path is blit-only.
            // Like the waveform cache, this surface may be null on the
            // very first paint after a load (before the first stem
            // rebuild fires); the blit is skipped and the background
            // shows through for that one frame. The surface's screen
            // origin is `marker_paint_rect.x, marker_paint_rect.y`
            // (i.e. area.x, area.y - kStemAboveWaveformPx), matching
            // the local-coord choice in maybe_rebuild_stem_cache.
            if (stem_cache.surface) {
                cairo_save(cr);
                cairo_rectangle(cr,
                                marker_paint_rect.x,
                                marker_paint_rect.y,
                                marker_paint_rect.w,
                                marker_paint_rect.h);
                cairo_clip(cr);
                cairo_set_source_surface(cr, stem_cache.surface,
                                         marker_paint_rect.x,
                                         marker_paint_rect.y);
                cairo_paint(cr);
                cairo_restore(cr);
            }
            const auto m1 = clock::now();
            t_markers_ms =
                std::chrono::duration<double, std::milli>(m1 - m0).count();
        }

        // Use the displayed viewport AND its samples-per-pixel
        // (wf_cache.fp_vp_start, derived spp) so the cursor stays in
        // lockstep with the cached waveform / stem / flag layers during
        // the 1-2 paint frames while the worker rebuilds against a
        // viewport change. See declaration comment in app_state.h.
        const double disp_spp = wf_cache.fp_area_w > 0
            ? static_cast<double>(wf_cache.fp_vp_end - wf_cache.fp_vp_start) /
              static_cast<double>(wf_cache.fp_area_w)
            : current_samples_per_pixel(app, audio);
        const double px_x = playhead_pixel_x(app, audio,
                                             wf_cache.fp_vp_start, disp_spp);

        // Flag annotations in the top strip. After Stage C the steady-
        // state flag-rect pixels live on flag_cache.surface (rebuilt
        // from on_tick via maybe_rebuild_flag_cache); on_redraw blits
        // the cache and then paints the per-frame live work — the
        // FlagPayload editor's pending text + cursor, and the hover /
        // iter / BPM popups. Source view calls paint_popups(nullptr);
        // target view calls paint_popups(tmap_disp). The displayed-
        // viewport timemap (tmap_disp = wf_cache.fp_timemap) keeps
        // popup anchors aligned with the cached flag rects during the
        // waveform worker's rebuild window.
        if (rects_intersect(exposed, top_strip)) {
            const auto f0 = clock::now();

            // Stage C: the steady-state flag-rect pixels are cached on
            // flag_cache.surface, rebuilt synchronously from on_tick via
            // maybe_rebuild_flag_cache. The paint path blits the cache
            // first; the per-frame live work that follows is the
            // FlagPayload editor flag (pending text + cursor) and the
            // hover / iter / BPM popups. Like the other caches, the
            // surface may be null on the very first paint after a load
            // (before the first rebuild fires); the blit is skipped and
            // the background shows through for that one frame.
            if (flag_cache.surface) {
                cairo_save(cr);
                cairo_rectangle(cr, top_strip.x, top_strip.y,
                                top_strip.w, top_strip.h);
                cairo_clip(cr);
                cairo_set_source_surface(cr, flag_cache.surface,
                                         top_strip.x, top_strip.y);
                cairo_paint(cr);
                cairo_restore(cr);
            }

            // Stage C: displayed-viewport locals for live items that
            // must align with the cached flag pixels. The cache renders
            // against wf_cache.fp_*; the live editor flag and popup
            // anchor math reads these *_disp locals so it agrees with
            // the cache during the worker's 1-2 frame rebuild window
            // (after a viewport gesture, before the worker's swap).
            const int64_t  vp_start_disp = wf_cache.fp_vp_start;
            const int64_t  vp_end_disp   = wf_cache.fp_vp_end;
            const TrimRange trim_struct_disp{
                wf_cache.fp_trim_begin, wf_cache.fp_trim_end};
            const std::vector<TimeMapSegment>* tmap_disp =
                (wf_cache.fp_target && !wf_cache.fp_timemap.empty())
                    ? &wf_cache.fp_timemap : nullptr;

            // Brief 3a: shared hover / iter / BPM popup paint, parameterized
            // on the timemap pointer. Source-view callers pass nullptr;
            // target-view callers pass tmap_disp. Body is a verbatim
            // lift of the W-mode popup blocks that previously lived inside
            // the source-view `else` branch, with the timemap forwarded into
            // compute_flag_hit_rects / compute_iter_popup_hits /
            // compute_bpm_popup_hits. The render-view branch keeps its own
            // hover popup paint (which reads from app.render_view_markers)
            // because render-view never goes through this lambda.
            // Stage C: viewport / trim references inside the lambda read
            // the *_disp locals above so popup anchors track the cached
            // flag rects (lagging the live state by 1-2 frames during a
            // viewport gesture).
            const auto paint_popups =
                [&](const std::vector<TimeMapSegment>* timemap) {
                if (app.hover_popup.visible &&
                    !app.iteration_mode_enabled) {
                    const auto& mv = app.warpmarkers.markers();
                    const int hidx = app.hover_popup.marker_index;
                    const bool eligible =
                        (hidx >= 0 &&
                         hidx < static_cast<int>(mv.size()) &&
                         (mv[hidx].tempo_inherits ||
                          !mv[hidx].label_ref.empty()) &&
                         !app.hover_popup.cached_text.empty());
                    if (eligible) {
                        auto rects = compute_flag_hit_rects(
                            cr, top_strip, mv,
                            vp_start_disp, vp_end_disp, sr, kFlagFontSize,
                            timemap, drag_overlay);
                        GuiRect anchor{0, 0, 0, 0};
                        for (const auto& r : rects) {
                            if (r.marker_index == hidx) {
                                anchor.x = static_cast<int>(std::lround(r.x)) +
                                           static_cast<int>(kFlagInnerPadPx);
                                anchor.y = static_cast<int>(std::lround(r.y));
                                anchor.w = static_cast<int>(std::lround(r.w));
                                anchor.h = static_cast<int>(std::lround(r.h));
                                break;
                            }
                        }
                        if (anchor.w > 0 && anchor.h > 0) {
                            const int64_t pos = static_cast<int64_t>(
                                std::nearbyint(
                                    mv[hidx].time_seconds *
                                    static_cast<double>(sr)));
                            const bool oot =
                                marker_out_of_trim(pos, trim_struct_disp);
                            text_display::State td;
                            td.anchor   = anchor;
                            td.content  = app.hover_popup.cached_text;
                            td.visible  = true;
                            td.color    = oot ? dim(kText) : kText;
                            text_display::render(cr, td, kFlagFontSize);
                        }
                    }
                }

                if (app.iteration_mode_enabled) {
                    const auto& mv = app.warpmarkers.markers();
                    auto hits = compute_iter_popup_hits(
                        cr, top_strip, mv,
                        vp_start_disp, vp_end_disp, sr, kFlagFontSize,
                        timemap, drag_overlay);
                    const bool editor_on_iter =
                        text_editor::is_active(app.top_flag_editor) &&
                        app.top_flag_editor.kind ==
                            text_editor::Kind::IterationBracket;
                    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
                        const auto& h = *it;
                        GuiRect anchor{
                            h.flag_rect.x +
                                static_cast<int>(kFlagInnerPadPx),
                            h.flag_rect.y,
                            h.flag_rect.w,
                            h.flag_rect.h
                        };
                        const int64_t pos = static_cast<int64_t>(
                            std::nearbyint(
                                mv[h.marker_index].time_seconds *
                                static_cast<double>(sr)));
                        const bool oot =
                            marker_out_of_trim(pos, trim_struct_disp);
                        if (editor_on_iter &&
                            app.top_flag_editor.target == h.marker_index) {
                            const std::string& pending =
                                app.top_flag_editor.pending;
                            cairo_save(cr);
                            cairo_select_font_face(cr, "monospace",
                                CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_NORMAL);
                            cairo_set_font_size(cr, kFlagFontSize);
                            cairo_text_extents_t pext;
                            cairo_text_extents(cr, pending.c_str(), &pext);
                            cairo_text_extents_t uext;
                            cairo_text_extents(cr, "[+0.00,+0.00]", &uext);
                            const double hl_pad = kFlagInnerPadPx;
                            const double bg_w =
                                pext.x_advance + 2.0 * hl_pad;
                            const double bg_x =
                                static_cast<double>(anchor.x) - hl_pad;
                            const double bg_y =
                                static_cast<double>(h.hit_rect.y);
                            const double bg_h =
                                static_cast<double>(h.hit_rect.h);
                            render_flag_text_bg_fill(cr,
                                static_cast<double>(anchor.x),
                                pext.x_advance, bg_y, bg_h);
                            GuiColor bg_col = app.top_flag_editor.red
                                ? kAccent : kMarker;
                            if (oot) bg_col = dim(bg_col);
                            cairo_set_source_rgb(cr,
                                bg_col.r, bg_col.g, bg_col.b);
                            const double sx = std::round(bg_x) + 0.5;
                            const double sy = std::round(bg_y) + 0.5;
                            const int sw = static_cast<int>(
                                std::round(bg_w));
                            const int sh = static_cast<int>(
                                std::round(bg_h));
                            cairo_set_line_width(cr, 1.0);
                            cairo_rectangle(cr, sx, sy,
                                static_cast<double>(sw),
                                static_cast<double>(sh));
                            cairo_stroke(cr);

                            const double baseline_y =
                                static_cast<double>(anchor.y)
                              - kIterPopupVerticalGapPx
                              - kIterPopupVPadExtraPx
                              - (uext.height + uext.y_bearing);
                            const GuiColor txt = oot ? dim(kText) : kText;
                            cairo_set_source_rgb(cr,
                                txt.r, txt.g, txt.b);
                            cairo_move_to(cr,
                                static_cast<double>(anchor.x), baseline_y);
                            cairo_show_text(cr, pending.c_str());

                            if (text_editor::has_selection(
                                    app.top_flag_editor)) {
                                const int sel_a = text_editor::selection_start(
                                    app.top_flag_editor);
                                const int sel_b = text_editor::selection_end(
                                    app.top_flag_editor);
                                cairo_text_extents_t a_ext;
                                cairo_text_extents(cr,
                                    pending.substr(0,
                                        static_cast<size_t>(sel_a)).c_str(),
                                    &a_ext);
                                cairo_text_extents_t b_ext;
                                cairo_text_extents(cr,
                                    pending.substr(0,
                                        static_cast<size_t>(sel_b)).c_str(),
                                    &b_ext);
                                const double hi_x =
                                    static_cast<double>(anchor.x) +
                                    a_ext.x_advance;
                                const double hi_w =
                                    b_ext.x_advance - a_ext.x_advance;
                                cairo_set_source_rgb(cr,
                                    txt.r, txt.g, txt.b);
                                cairo_rectangle(cr, hi_x, bg_y,
                                                hi_w, bg_h);
                                cairo_fill(cr);
                                const GuiColor bg_swap =
                                    oot ? dim(kBackground) : kBackground;
                                cairo_set_source_rgb(cr,
                                    bg_swap.r, bg_swap.g, bg_swap.b);
                                cairo_move_to(cr, hi_x, baseline_y);
                                cairo_show_text(cr,
                                    pending.substr(
                                        static_cast<size_t>(sel_a),
                                        static_cast<size_t>(sel_b - sel_a))
                                        .c_str());
                            }

                            if (text_editor::cursor_visible_now(
                                    app.top_flag_editor)) {
                                std::string left = pending.substr(
                                    0, static_cast<size_t>(
                                        app.top_flag_editor.cursor_pos));
                                cairo_text_extents_t lext;
                                cairo_text_extents(cr, left.c_str(), &lext);
                                const double cx =
                                    static_cast<double>(anchor.x) +
                                    lext.x_advance;
                                cairo_set_source_rgb(cr,
                                    txt.r, txt.g, txt.b);
                                cairo_set_line_width(cr, 1.0);
                                cairo_move_to(cr, cx, bg_y);
                                cairo_line_to(cr, cx, bg_y + bg_h);
                                cairo_stroke(cr);
                            }
                            cairo_restore(cr);
                        } else {
                            cairo_save(cr);
                            cairo_select_font_face(cr, "monospace",
                                CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_NORMAL);
                            cairo_set_font_size(cr, kFlagFontSize);
                            cairo_text_extents_t hext;
                            cairo_text_extents(cr, h.text.c_str(), &hext);
                            render_flag_text_bg_fill(cr,
                                static_cast<double>(anchor.x),
                                hext.x_advance,
                                static_cast<double>(h.hit_rect.y),
                                static_cast<double>(h.hit_rect.h));
                            cairo_restore(cr);

                            text_display::State td;
                            td.anchor   = anchor;
                            td.content  = h.text;
                            td.visible  = true;
                            td.color    = oot ? dim(kText) : kText;
                            text_display::render(cr, td, kFlagFontSize);
                        }
                    }
                }

                if (app.bpm_mode_enabled) {
                    const auto& mv = app.warpmarkers.markers();
                    auto hits = compute_bpm_popup_hits(
                        cr, top_strip, mv,
                        vp_start_disp, vp_end_disp, sr, kFlagFontSize,
                        timemap, drag_overlay);
                    const bool editor_on_bpm =
                        text_editor::is_active(app.top_flag_editor) &&
                        app.top_flag_editor.kind ==
                            text_editor::Kind::BpmBracket;
                    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
                        const auto& h = *it;
                        GuiRect anchor{
                            h.flag_rect.x +
                                static_cast<int>(kFlagInnerPadPx),
                            h.flag_rect.y,
                            h.flag_rect.w,
                            h.flag_rect.h
                        };
                        const int64_t pos = static_cast<int64_t>(
                            std::nearbyint(
                                mv[h.marker_index].time_seconds *
                                static_cast<double>(sr)));
                        const bool oot =
                            marker_out_of_trim(pos, trim_struct_disp);
                        if (editor_on_bpm &&
                            app.top_flag_editor.target == h.marker_index) {
                            const std::string& pending =
                                app.top_flag_editor.pending;
                            cairo_save(cr);
                            cairo_select_font_face(cr, "monospace",
                                CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_NORMAL);
                            cairo_set_font_size(cr, kFlagFontSize);
                            cairo_text_extents_t pext;
                            cairo_text_extents(cr, pending.c_str(), &pext);
                            cairo_text_extents_t uext;
                            cairo_text_extents(cr, "99@[999,999]", &uext);
                            const double hl_pad = kFlagInnerPadPx;
                            const double bg_w =
                                pext.x_advance + 2.0 * hl_pad;
                            const double bg_x =
                                static_cast<double>(anchor.x) - hl_pad;
                            const double bg_y =
                                static_cast<double>(h.hit_rect.y);
                            const double bg_h =
                                static_cast<double>(h.hit_rect.h);
                            render_flag_text_bg_fill(cr,
                                static_cast<double>(anchor.x),
                                pext.x_advance, bg_y, bg_h);
                            GuiColor bg_col = app.top_flag_editor.red
                                ? kAccent : kMarker;
                            if (oot) bg_col = dim(bg_col);
                            cairo_set_source_rgb(cr,
                                bg_col.r, bg_col.g, bg_col.b);
                            const double sx = std::round(bg_x) + 0.5;
                            const double sy = std::round(bg_y) + 0.5;
                            const int sw = static_cast<int>(
                                std::round(bg_w));
                            const int sh = static_cast<int>(
                                std::round(bg_h));
                            cairo_set_line_width(cr, 1.0);
                            cairo_rectangle(cr, sx, sy,
                                static_cast<double>(sw),
                                static_cast<double>(sh));
                            cairo_stroke(cr);

                            const double baseline_y =
                                static_cast<double>(anchor.y)
                              - kIterPopupVerticalGapPx
                              - kIterPopupVPadExtraPx
                              - (uext.height + uext.y_bearing);
                            const GuiColor txt = oot ? dim(kText) : kText;
                            cairo_set_source_rgb(cr,
                                txt.r, txt.g, txt.b);
                            cairo_move_to(cr,
                                static_cast<double>(anchor.x), baseline_y);
                            cairo_show_text(cr, pending.c_str());

                            if (text_editor::has_selection(
                                    app.top_flag_editor)) {
                                const int sel_a = text_editor::selection_start(
                                    app.top_flag_editor);
                                const int sel_b = text_editor::selection_end(
                                    app.top_flag_editor);
                                cairo_text_extents_t a_ext;
                                cairo_text_extents(cr,
                                    pending.substr(0,
                                        static_cast<size_t>(sel_a)).c_str(),
                                    &a_ext);
                                cairo_text_extents_t b_ext;
                                cairo_text_extents(cr,
                                    pending.substr(0,
                                        static_cast<size_t>(sel_b)).c_str(),
                                    &b_ext);
                                const double hi_x =
                                    static_cast<double>(anchor.x) +
                                    a_ext.x_advance;
                                const double hi_w =
                                    b_ext.x_advance - a_ext.x_advance;
                                cairo_set_source_rgb(cr,
                                    txt.r, txt.g, txt.b);
                                cairo_rectangle(cr, hi_x, bg_y,
                                                hi_w, bg_h);
                                cairo_fill(cr);
                                const GuiColor bg_swap =
                                    oot ? dim(kBackground) : kBackground;
                                cairo_set_source_rgb(cr,
                                    bg_swap.r, bg_swap.g, bg_swap.b);
                                cairo_move_to(cr, hi_x, baseline_y);
                                cairo_show_text(cr,
                                    pending.substr(
                                        static_cast<size_t>(sel_a),
                                        static_cast<size_t>(sel_b - sel_a))
                                        .c_str());
                            }

                            if (text_editor::cursor_visible_now(
                                    app.top_flag_editor)) {
                                std::string left = pending.substr(
                                    0, static_cast<size_t>(
                                        app.top_flag_editor.cursor_pos));
                                cairo_text_extents_t lext;
                                cairo_text_extents(cr, left.c_str(), &lext);
                                const double cx =
                                    static_cast<double>(anchor.x) +
                                    lext.x_advance;
                                cairo_set_source_rgb(cr,
                                    txt.r, txt.g, txt.b);
                                cairo_set_line_width(cr, 1.0);
                                cairo_move_to(cr, cx, bg_y);
                                cairo_line_to(cr, cx, bg_y + bg_h);
                                cairo_stroke(cr);
                            }
                            cairo_restore(cr);
                        } else {
                            cairo_save(cr);
                            cairo_select_font_face(cr, "monospace",
                                CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_NORMAL);
                            cairo_set_font_size(cr, kFlagFontSize);
                            cairo_text_extents_t hext;
                            cairo_text_extents(cr, h.text.c_str(), &hext);
                            render_flag_text_bg_fill(cr,
                                static_cast<double>(anchor.x),
                                hext.x_advance,
                                static_cast<double>(h.hit_rect.y),
                                static_cast<double>(h.hit_rect.h));
                            cairo_restore(cr);

                            text_display::State td;
                            td.anchor   = anchor;
                            td.content  = h.text;
                            td.visible  = true;
                            td.color    = oot ? dim(kText) : kText;
                            text_display::render(cr, td, kFlagFontSize);
                        }
                    }
                }
            };

            // Built once, threaded into both source-view and target-view
            // warp render_flags calls below. Reads only app.top_flag_editor,
            // which has no view-domain distinction.
            FlagEditorOverlay overlay;
            if (text_editor::is_active(app.top_flag_editor) &&
                app.top_flag_editor.kind ==
                    text_editor::Kind::FlagPayload) {
                overlay.marker_index   = app.top_flag_editor.target;
                overlay.pending        = app.top_flag_editor.pending;
                overlay.cursor_pos     = app.top_flag_editor.cursor_pos;
                overlay.is_red         = app.top_flag_editor.red;
                overlay.cursor_visible =
                    text_editor::cursor_visible_now(
                        app.top_flag_editor);
                overlay.has_selection =
                    text_editor::has_selection(
                        app.top_flag_editor);
                overlay.selection_start =
                    text_editor::selection_start(
                        app.top_flag_editor);
                overlay.selection_end =
                    text_editor::selection_end(
                        app.top_flag_editor);
            } else if (text_editor::is_active(app.top_flag_editor) &&
                       app.top_flag_editor.kind ==
                           text_editor::Kind::IterationBracket) {
                overlay.popup_editor_target =
                    app.top_flag_editor.target;
            } else if (text_editor::is_active(app.top_flag_editor) &&
                       app.top_flag_editor.kind ==
                           text_editor::Kind::BpmBracket) {
                // Brief X.2: same flag-rect highlight suppression as
                // iter — the popup above owns the highlight. Modes are
                // mutually exclusive so the shared popup_editor_target
                // channel is safe.
                overlay.popup_editor_target =
                    app.top_flag_editor.target;
            }

            // Stage C: the flag-rect pass has moved into the cache
            // rebuild above. What's left here is live work — the
            // FlagPayload editor's pending text + cursor (which would
            // otherwise drag the cache fingerprint on every keystroke
            // and blink flip), and the hover / iter / BPM popups.
            //
            // Live editor flag: only paints in W marker-view and not
            // render-view (FlagPayload editor isn't available in either
            // 'P' or render-view paths). The cache leaves a hole over
            // the editor target via the skip-guard in render_flags, so
            // this fill is mandatory whenever overlay.marker_index >= 0
            // — otherwise that flag's pixels would be missing entirely.
            if (overlay.marker_index >= 0 &&
                !app.render_view_enabled &&
                app.active_markers_view != 'P') {
                render_one_editor_flag(
                    cr, top_strip,
                    app.warpmarkers.markers(),
                    vp_start_disp, vp_end_disp, sr,
                    kFlagFontSize,
                    app.selected_markers,
                    trim_struct_disp,
                    overlay,
                    tmap_disp,
                    drag_overlay);
            }

            // Live popups. Phase-reset markers are not popup-eligible,
            // so the 'P' marker-view branches paint nothing here.
            if (app.render_view_enabled) {
                // V.A3b hover popup paint, render-view variant. Mirrors
                // the source-view branch in paint_popups but reads from
                // app.render_view_markers. Iteration popups are
                // suppressed by iteration_mode_enabled being forced
                // false on entry to render-view; BPM popups likewise.
                if (app.active_markers_view != 'P' &&
                    app.hover_popup.visible) {
                    const auto& mv = app.render_view_markers;
                    const int hidx = app.hover_popup.marker_index;
                    const bool eligible =
                        (hidx >= 0 &&
                         hidx < static_cast<int>(mv.size()) &&
                         (mv[hidx].tempo_inherits ||
                          !mv[hidx].label_ref.empty()) &&
                         !app.hover_popup.cached_text.empty());
                    if (eligible) {
                        auto rects = compute_flag_hit_rects(
                            cr, top_strip, mv,
                            vp_start_disp, vp_end_disp, sr, kFlagFontSize,
                            nullptr, drag_overlay);
                        GuiRect anchor{0, 0, 0, 0};
                        for (const auto& r : rects) {
                            if (r.marker_index == hidx) {
                                anchor.x = static_cast<int>(
                                    std::lround(r.x)) +
                                    static_cast<int>(kFlagInnerPadPx);
                                anchor.y = static_cast<int>(
                                    std::lround(r.y));
                                anchor.w = static_cast<int>(
                                    std::lround(r.w));
                                anchor.h = static_cast<int>(
                                    std::lround(r.h));
                                break;
                            }
                        }
                        if (anchor.w > 0 && anchor.h > 0) {
                            const int64_t pos = static_cast<int64_t>(
                                std::nearbyint(
                                    mv[hidx].time_seconds *
                                    static_cast<double>(sr)));
                            const bool oot =
                                marker_out_of_trim(pos, trim_struct_disp);
                            text_display::State td;
                            td.anchor   = anchor;
                            td.content  = app.hover_popup.cached_text;
                            td.visible  = true;
                            td.color    = oot ? dim(kText) : kText;
                            text_display::render(cr, td,
                                                 kFlagFontSize);
                        }
                    }
                }
            } else if (is_target) {
                if (app.active_markers_view != 'P') {
                    // Brief 3a: hover / iter / BPM popups in target view.
                    // tmap_disp is the displayed-viewport timemap (live
                    // wf_cache.fp_timemap); popup anchors track the cached
                    // flag rects throughout the rebuild window.
                    paint_popups(tmap_disp);
                }
            } else if (app.active_markers_view != 'P') {
                paint_popups(nullptr);
            }
            const auto f1 = clock::now();
            t_flags_ms =
                std::chrono::duration<double, std::milli>(f1 - f0).count();
        }

        // Playhead drawn last so its stem and triangle paint over any
        // marker connector pixels they share a column with — the brief
        // mandates the playhead never be occluded by marker stems or
        // flag annotations. The triangle indicator lives in the top
        // strip, so render whenever either the waveform or top strip is
        // exposed; otherwise a flag-strip-only repaint would erase the
        // triangle.
        //
        // Split-playhead paint order: scanner first (line only, gated
        // on playhead_scanner_active), then cursor (line + triangle).
        // The cursor draws over the scanner on overlap.
        if (rects_intersect(exposed, area) ||
            rects_intersect(exposed, top_strip)) {
            const auto p0 = clock::now();
            if (app.playhead_scanner_active) {
                const double scan_px = scanner_pixel_x(app, audio,
                                                       wf_cache.fp_vp_start,
                                                       disp_spp);
                render_playhead(cr, area, scan_px, kPlayheadScanner,
                                gui.playhead_triangle_surface(),
                                /*draw_triangle=*/false);
            }
            render_playhead(cr, area, px_x, kPlayheadCursor,
                            gui.playhead_triangle_surface(),
                            /*draw_triangle=*/true);
            const auto p1 = clock::now();
            t_playhead_ms =
                std::chrono::duration<double, std::milli>(p1 - p0).count();
        }

        // Bottom strip: either the prompt overlay (when active) or
        // the regular elements (timestamp / tab letter / dirty / render
        // -view filename). The prompt is modal — while active, it
        // owns the strip and the regular elements are not visible.
        const GuiRect ts = timestamp_invalidate_rect(app.height, app.width);
        if (rects_intersect(exposed, ts)) {
            const int baseline_y = app.height - kTimestampBaselineFromBottom;
            if (app.prompt.active) {
                cairo_save(cr);
                cairo_set_source_rgb(cr, kText.r, kText.g, kText.b);
                cairo_select_font_face(cr, "monospace",
                                       CAIRO_FONT_SLANT_NORMAL,
                                       CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size(cr, kFlagFontSize);
                cairo_move_to(cr, kTimestampPadX, baseline_y);
                cairo_show_text(cr, app.prompt.text.c_str());
                cairo_text_extents_t pext;
                cairo_text_extents(cr, app.prompt.text.c_str(), &pext);
                const double label_gap = kTabLetterGapPx * 2.0;
                double cursor_x = static_cast<double>(kTimestampPadX) +
                                  pext.x_advance + label_gap;
                for (const auto& label : app.prompt.response_labels) {
                    cairo_move_to(cr, cursor_x, baseline_y);
                    cairo_show_text(cr, label.c_str());
                    cairo_text_extents_t lext;
                    cairo_text_extents(cr, label.c_str(), &lext);
                    cursor_x += lext.x_advance + label_gap;
                }
                cairo_restore(cr);
            } else if (!app.queue_progress_text.empty()) {
                cairo_save(cr);
                cairo_set_source_rgb(cr, kText.r, kText.g, kText.b);
                cairo_select_font_face(cr, "monospace",
                                       CAIRO_FONT_SLANT_NORMAL,
                                       CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size(cr, kFlagFontSize);
                cairo_move_to(cr, kTimestampPadX, baseline_y);
                cairo_show_text(cr, app.queue_progress_text.c_str());
                cairo_restore(cr);
            } else if (text_editor::is_active(app.settings_editor)) {
                // Settings prompt overlay: "setting: <pending>" with a
                // blink-gated 1-px cursor bar, optional selection swap,
                // and an unconditional 1-px stroke whose color toggles
                // on app.settings_editor.red. Mirrors the flag editor's
                // iter-popup paint shape: bg fill, then stroke, then
                // text with selection swap on top, then cursor. The
                // non-red stroke is kBackground (invisible against the
                // canvas) — the cost of structural symmetry with the
                // flag editor primitive. The tab letter, dirty dot,
                // and render-view filename are suppressed for the
                // duration of the edit.
                cairo_save(cr);
                cairo_select_font_face(cr, "monospace",
                                       CAIRO_FONT_SLANT_NORMAL,
                                       CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size(cr, kFlagFontSize);

                const std::string prefix  = "setting: ";
                const std::string& pending = app.settings_editor.pending;

                cairo_text_extents_t pre_ext;
                cairo_text_extents(cr, prefix.c_str(), &pre_ext);
                cairo_text_extents_t pend_ext;
                cairo_text_extents(cr, pending.c_str(), &pend_ext);
                cairo_text_extents_t uniform_ext;
                cairo_text_extents(cr, "Mg", &uniform_ext);

                const double pending_x =
                    static_cast<double>(kTimestampPadX) + pre_ext.x_advance;
                const double hl_pad = kFlagInnerPadPx;
                const double bg_top = baseline_y + uniform_ext.y_bearing -
                                      hl_pad - kVPadExtraPx;
                const double bg_h   = uniform_ext.height +
                                      2.0 * hl_pad + 2.0 * kVPadExtraPx;

                // Canvas-bg fill behind pending text and outline.
                render_flag_text_bg_fill(cr, pending_x, pend_ext.x_advance,
                                         bg_top, bg_h);

                // Unconditional 1-px stroke; kAccent on parse failure,
                // otherwise kBackground (invisible against the canvas).
                GuiColor stroke_col = app.settings_editor.red
                    ? kAccent : kBackground;
                const double sx = std::round(pending_x - hl_pad) + 0.5;
                const double sy = std::round(bg_top) + 0.5;
                const int    sw = static_cast<int>(
                    std::round(pend_ext.x_advance + 2.0 * hl_pad));
                const int    sh = static_cast<int>(std::round(bg_h));
                cairo_set_source_rgb(cr,
                    stroke_col.r, stroke_col.g, stroke_col.b);
                cairo_set_line_width(cr, 1.0);
                cairo_rectangle(cr, sx, sy,
                    static_cast<double>(sw),
                    static_cast<double>(sh));
                cairo_stroke(cr);

                // Static prefix.
                cairo_set_source_rgb(cr, kText.r, kText.g, kText.b);
                cairo_move_to(cr,
                    static_cast<double>(kTimestampPadX), baseline_y);
                cairo_show_text(cr, prefix.c_str());

                // Pending text.
                cairo_move_to(cr, pending_x, baseline_y);
                cairo_show_text(cr, pending.c_str());

                // Selection swap: kText fill over the selected pixel
                // range, then re-paint the selected substring in
                // kBackground for contrast. Mirrors the flag editor's
                // iter-popup site.
                if (text_editor::has_selection(app.settings_editor)) {
                    const int sel_a = text_editor::selection_start(
                        app.settings_editor);
                    const int sel_b = text_editor::selection_end(
                        app.settings_editor);
                    cairo_text_extents_t a_ext;
                    cairo_text_extents(cr,
                        pending.substr(0,
                            static_cast<size_t>(sel_a)).c_str(),
                        &a_ext);
                    cairo_text_extents_t b_ext;
                    cairo_text_extents(cr,
                        pending.substr(0,
                            static_cast<size_t>(sel_b)).c_str(),
                        &b_ext);
                    const double hi_x = pending_x + a_ext.x_advance;
                    const double hi_w = b_ext.x_advance - a_ext.x_advance;
                    cairo_set_source_rgb(cr, kText.r, kText.g, kText.b);
                    cairo_rectangle(cr, hi_x, bg_top, hi_w, bg_h);
                    cairo_fill(cr);
                    cairo_set_source_rgb(cr,
                        kBackground.r, kBackground.g, kBackground.b);
                    cairo_move_to(cr, hi_x, baseline_y);
                    cairo_show_text(cr,
                        pending.substr(
                            static_cast<size_t>(sel_a),
                            static_cast<size_t>(sel_b - sel_a))
                            .c_str());
                }

                // Cursor bar (blink-gated).
                if (text_editor::cursor_visible_now(app.settings_editor)) {
                    cairo_text_extents_t lext;
                    cairo_text_extents(cr,
                        pending.substr(0,
                            static_cast<size_t>(
                                app.settings_editor.cursor_pos)).c_str(),
                        &lext);
                    const double cx = pending_x + lext.x_advance;
                    cairo_set_source_rgb(cr, kText.r, kText.g, kText.b);
                    cairo_set_line_width(cr, 1.0);
                    cairo_move_to(cr, cx, bg_top);
                    cairo_line_to(cr, cx, bg_top + bg_h);
                    cairo_stroke(cr);
                }

                cairo_restore(cr);
            } else {
                // In source-view, sr is the loaded file's sample rate
                // and the playhead samples are in source-frames. In
                // render-view the active `audio` is the render, so its
                // sr is what the engine wrote out — but the playhead is
                // in render-frame coords. Render-view timestamp is
                // render-domain (zero at render sample 0); source-time
                // and render-time advance at different rates because
                // of warping, so the same arithmetic suffices.
                //
                // Split-playhead: track the scanner during playback
                // (what the user is hearing) and the cursor otherwise.
                // The two are equal by invariant when the scanner is
                // inactive, so the conditional only matters during
                // playback.
                const int64_t ts_sample = app.playhead_scanner_active
                    ? app.playhead_scanner_sample
                    : app.playhead_cursor_sample;
                double seconds = 0.0;
                if (sr > 0) {
                    seconds = static_cast<double>(ts_sample) /
                              static_cast<double>(sr);
                }
                {
                    const auto s0 = clock::now();
                    render_timestamp(cr, kTimestampPadX, baseline_y,
                                     seconds, kText);
                    const auto s1 = clock::now();
                    t_ts_ms =
                        std::chrono::duration<double, std::milli>(s1 - s0).count();
                }

                // Brief 3a: three single-letter indicators between the
                // timestamp and the dirty asterisk, in domain-axis-first
                // order: S/T (view-domain) → W/P (markers view) →
                // A/B (tab within markers view). All three suppressed in
                // render-view, matching the original A/B suppression
                // (Tab key is gated out there, `t` is a silent no-op,
                // and W/P meaning is render-view's own sub-view).
                const double tw = measure_timestamp_width(cr, seconds);
                double right_after_indicators =
                    static_cast<double>(kTimestampPadX) + tw;
                if (!app.render_view_enabled) {
                    cairo_save(cr);
                    cairo_set_source_rgb(cr, kText.r, kText.g, kText.b);
                    cairo_select_font_face(cr, "monospace",
                                           CAIRO_FONT_SLANT_NORMAL,
                                           CAIRO_FONT_WEIGHT_NORMAL);
                    cairo_set_font_size(cr, kFlagFontSize);

                    const double st_x =
                        right_after_indicators + kTabLetterGapPx;
                    const char st_buf[2] = {
                        app.active_audio_view == 'T' ? 'T' : 'S',
                        '\0'
                    };
                    cairo_text_extents_t st_ext;
                    cairo_text_extents(cr, st_buf, &st_ext);
                    cairo_move_to(cr, st_x, baseline_y);
                    cairo_show_text(cr, st_buf);
                    right_after_indicators = st_x + st_ext.x_advance;

                    const double wp_x =
                        right_after_indicators + kTabLetterGapPx;
                    const char wp_buf[2] = { app.active_markers_view, '\0' };
                    cairo_text_extents_t wp_ext;
                    cairo_text_extents(cr, wp_buf, &wp_ext);
                    cairo_move_to(cr, wp_x, baseline_y);
                    cairo_show_text(cr, wp_buf);
                    right_after_indicators = wp_x + wp_ext.x_advance;

                    const double ab_x =
                        right_after_indicators + kTabLetterGapPx;
                    const char ab_buf[2] = { app.active_tab_view, '\0' };
                    cairo_text_extents_t ab_ext;
                    cairo_text_extents(cr, ab_buf, &ab_ext);
                    // Read-only dim: half-blend the A/B glyph toward the
                    // background so the locked-out state is visible
                    // without changing the active-tab marker (no glyph
                    // change). Resets back to kText after the glyph so
                    // the dirty-dot below renders at full strength.
                    const bool ab_dim = active_view_state(app).read_only;
                    if (ab_dim) {
                        const GuiColor c = dim(kText);
                        cairo_set_source_rgb(cr, c.r, c.g, c.b);
                    }
                    cairo_move_to(cr, ab_x, baseline_y);
                    cairo_show_text(cr, ab_buf);
                    if (ab_dim) {
                        cairo_set_source_rgb(cr, kText.r, kText.g, kText.b);
                    }
                    right_after_indicators = ab_x + ab_ext.x_advance;

                    cairo_restore(cr);
                }

                // Render-view filename. Flowed into the left-anchored
                // sequence after the indicator letters (which are
                // suppressed in render-view, so in practice the
                // filename sits right after the timestamp). Painted
                // before the dirty asterisk so the asterisk stays the
                // last element in the strip across all views.
                if (app.render_view_enabled &&
                    app.render_view_index >= 0 &&
                    app.render_view_index <
                        static_cast<int>(app.render_view_list.size())) {
                    const auto& e =
                        app.render_view_list[app.render_view_index];
                    const std::string label =
                        e.batch_folder.filename().string() + "/" +
                        e.basename + ".wav";
                    cairo_save(cr);
                    cairo_set_source_rgb(cr, kText.r, kText.g, kText.b);
                    cairo_select_font_face(cr, "monospace",
                                           CAIRO_FONT_SLANT_NORMAL,
                                           CAIRO_FONT_WEIGHT_NORMAL);
                    cairo_set_font_size(cr, kFlagFontSize);
                    cairo_text_extents_t ext;
                    cairo_text_extents(cr, label.c_str(), &ext);
                    const double fx =
                        right_after_indicators + kTabLetterGapPx;
                    cairo_move_to(cr, fx, baseline_y);
                    cairo_show_text(cr, label.c_str());
                    cairo_restore(cr);
                    right_after_indicators = fx + ext.x_advance;
                }

                // Dirty asterisk. Painted last so it stays the trailing
                // element of the strip across every view — after
                // S/T/W/P/A/B in source/target, after the filename in
                // render-view. Cannot be cleared from within
                // render-view (Ctrl+S is gated out by the render-view
                // input gate); it remains visible as a reminder that
                // source-view authoring state is unsaved.
                if (app.dirty) {
                    const auto d0 = clock::now();
                    const double cx = right_after_indicators + kTabLetterGapPx;
                    cairo_save(cr);
                    cairo_set_source_rgb(cr, kText.r, kText.g, kText.b);
                    cairo_select_font_face(cr, "monospace",
                                           CAIRO_FONT_SLANT_NORMAL,
                                           CAIRO_FONT_WEIGHT_NORMAL);
                    cairo_set_font_size(cr, kFlagFontSize);
                    cairo_text_extents_t star_ext;
                    cairo_text_extents(cr, "*", &star_ext);
                    cairo_move_to(cr, cx, baseline_y);
                    cairo_show_text(cr, "*");
                    cairo_restore(cr);
                    right_after_indicators = cx + star_ext.x_advance;
                    const auto d1 = clock::now();
                    t_dirty_ms =
                        std::chrono::duration<double, std::milli>(d1 - d0).count();
                }

                // Transient one-line status message. Painted after the
                // dirty asterisk; cleared on the next keyboard press
                // (see input_handler on_key top). Mirrors the
                // dirty-asterisk paint shape exactly so the trailing
                // element flows after whatever cursor position landed.
                if (!app.transient_status_message.empty()) {
                    const double mx =
                        right_after_indicators + kTabLetterGapPx;
                    cairo_save(cr);
                    cairo_set_source_rgb(cr, kText.r, kText.g, kText.b);
                    cairo_select_font_face(cr, "monospace",
                                           CAIRO_FONT_SLANT_NORMAL,
                                           CAIRO_FONT_WEIGHT_NORMAL);
                    cairo_set_font_size(cr, kFlagFontSize);
                    cairo_text_extents_t msg_ext;
                    cairo_text_extents(cr,
                        app.transient_status_message.c_str(), &msg_ext);
                    cairo_move_to(cr, mx, baseline_y);
                    cairo_show_text(cr,
                        app.transient_status_message.c_str());
                    cairo_restore(cr);
                    right_after_indicators = mx + msg_ext.x_advance;
                }
            }
        }
    }

    cairo_restore(cr);

    // Force any pending Cairo ops out to the X server so the flush cost
    // is captured here rather than attributed elsewhere. The subsequent
    // flush in GuiPlatform::dispatch_event is a cheap no-op.
    {
        const auto fl0 = clock::now();
        cairo_surface_flush(cairo_get_target(cr));
        const auto fl1 = clock::now();
        t_flush_ms =
            std::chrono::duration<double, std::milli>(fl1 - fl0).count();
    }

    const auto t_end = clock::now();
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(t_end - t_start).count();

    if constexpr (kDebugPerf) {
        if (elapsed_ms > 3.0) {
            double e2e_ms = -1.0;
            if (app.last_input_event_time.time_since_epoch().count() != 0) {
                e2e_ms = std::chrono::duration<double, std::milli>(
                    t_end - app.last_input_event_time).count();
            }
            std::fprintf(stderr,
                "[dbg perf] total=%.2f ms waveform=%.2f markers=%.2f "
                "flags=%.2f playhead=%.2f ts=%.2f dirty=%.2f flush=%.2f "
                "pixel_area=%dx%d wf_cols=%d wf_pyramid_samples=%d "
                "flag_measure=%d flag_drawn=%d flag_elided=%d "
                "e2e=%.2f\n",
                elapsed_ms, t_waveform_ms, t_markers_ms, t_flags_ms,
                t_playhead_ms, t_ts_ms, t_dirty_ms, t_flush_ms,
                w, h,
                perf_counters::wf_cols, perf_counters::wf_pyramid_samples,
                perf_counters::flag_measure, perf_counters::flag_drawn,
                perf_counters::flag_elided,
                e2e_ms);
        }
    }

    if constexpr (kDebugPerf) {
        if (elapsed_ms > app.stats_max_redraw_ms)
            app.stats_max_redraw_ms = elapsed_ms;
        if (elapsed_ms > 1.0) app.stats_over_1ms_count++;
        const double since_last = std::chrono::duration<double>(
            t_end - app.stats_last_report).count();
        if (since_last >= 1.0) {
            if (app.stats_max_redraw_ms > 1.0) {
                std::fprintf(stderr,
                    "[warptempo_gui] redraw max=%.2fms in last %.1fs "
                    "(%d redraws > 1ms)\n",
                    app.stats_max_redraw_ms, since_last,
                    app.stats_over_1ms_count);
            }
            app.stats_max_redraw_ms = 0.0;
            app.stats_over_1ms_count = 0;
            app.stats_last_report = t_end;
        }
    }
}

// -- Stage A: waveform-worker dirty-detect and completion ----------------
//
// maybe_enqueue_waveform_render: called from on_tick. Computes the desired
// waveform fingerprint (mirrors the input computation on_redraw does), and
// either dispatches a fresh job, sets the supersede slot, or no-ops.
//
// The input-computation block is duplicated with on_redraw on purpose: it
// is the single source of truth for the waveform fingerprint. Keep them in
// sync — when on_redraw's target_timemap / trim derivation changes, this
// function changes the same way.

void GuiPaintHandler::maybe_enqueue_waveform_render() {
    if (app.loading || audio.total_frames() <= 0) return;

    const GuiRect area     = waveform_area(app);
    if (area.w <= 0 || area.h <= 0) return;

    const double  spp      = current_samples_per_pixel(app, audio);
    const int64_t vp_start = app.viewport_start_sample;
    const int64_t vp_end   = vp_start +
        static_cast<int64_t>(std::nearbyint(spp * area.w));
    const int     sr       = audio.sample_rate();

    const bool is_target = (app.active_audio_view == 'T') &&
                           !app.render_view_enabled;
    std::vector<TimeMapSegment> target_timemap;
    uint64_t target_timemap_hash = 0;
    if (is_target) {
        if (app.drag.active) {
            target_timemap = app.drag.frozen_timemap;
        } else {
            TimemapBuildInput tmin;
            tmin.markers      = resolve_markers_for_render(
                                     app.warpmarkers.markers());
            tmin.scale        = app.engine_settings.scale;
            tmin.sample_rate  = sr;
            tmin.total_frames = static_cast<long>(audio.total_frames());
            tmin.has_trim_begin = false;
            tmin.trim_begin_sec = 0.0;
            tmin.has_trim_end   = false;
            tmin.trim_end_sec   = 0.0;
            TimemapBuildResult tmres;
            if (build_timemaps(tmin, tmres)) {
                target_timemap.reserve(tmres.standard.size());
                uint64_t h = 0xcbf29ce484222325ULL;
                for (const auto& s : tmres.standard) {
                    target_timemap.push_back(TimeMapSegment{
                        s.src_frame, s.tgt_frame});
                    h ^= static_cast<uint64_t>(s.src_frame);
                    h *= 0x100000001b3ULL;
                    h ^= static_cast<uint64_t>(s.tgt_frame);
                    h *= 0x100000001b3ULL;
                }
                target_timemap_hash = h;
            }
        }
    }

    std::pair<long long, long long> trim;
    if (app.render_view_enabled) {
        trim = {0, audio.total_frames()};
    } else if (is_target) {
        const auto src_trim = compute_trim_samples(
            app, sr, audio.total_frames());
        if (!target_timemap.empty()) {
            const long long t0 = static_cast<long long>(std::nearbyint(
                map_source_to_target(
                    static_cast<size_t>(src_trim.first),
                    target_timemap)));
            const long long t1 = static_cast<long long>(std::nearbyint(
                map_source_to_target(
                    static_cast<size_t>(src_trim.second),
                    target_timemap)));
            trim = {t0, t1};
        } else {
            trim = src_trim;
        }
    } else {
        trim = compute_trim_samples(app, sr, audio.total_frames());
    }
    const int64_t trim_begin = trim.first;
    const int64_t trim_end   = trim.second;

    const int channel_count = audio.render_channels();

    // Drag-freeze gate: during a target-view drag the timemap-derived
    // inputs are excluded from the dirty-detect comparison, so non-drag
    // viewport changes (which would still update pending_fp_* if they
    // happened) trigger a render but pure drag-motion does not. See the
    // original brief 3b comment in on_redraw.
    const bool drag_freeze = is_target && app.drag.active;

    auto fingerprint_differs = [&](
        int64_t fp_vp_s, int64_t fp_vp_e,
        int64_t fp_tb,   int64_t fp_te,
        int     fp_aw,   int     fp_ah,
        long long fp_ag, bool    fp_t,
        uint64_t fp_h) -> bool {
        if (fp_ag != app.audio_generation) return true;
        if (fp_vp_s != vp_start)           return true;
        if (fp_vp_e != vp_end)             return true;
        if (fp_aw   != area.w)             return true;
        if (fp_ah   != area.h)             return true;
        if (fp_t    != is_target)          return true;
        if (!drag_freeze) {
            if (fp_tb != trim_begin)          return true;
            if (fp_te != trim_end)            return true;
            if (fp_h  != target_timemap_hash) return true;
        }
        return false;
    };

    const bool diff_vs_pending = fingerprint_differs(
        wf_cache.pending_fp_vp_start,
        wf_cache.pending_fp_vp_end,
        wf_cache.pending_fp_trim_begin,
        wf_cache.pending_fp_trim_end,
        wf_cache.pending_fp_area_w,
        wf_cache.pending_fp_area_h,
        wf_cache.pending_fp_audio_gen,
        wf_cache.pending_fp_target,
        wf_cache.pending_fp_timemap_hash);

    if (!diff_vs_pending) return;

    // We need to enqueue (or supersede an in-flight job). Build the job's
    // input snapshot now; the supersede slot stores the same struct shape
    // so we can hand it directly to dispatch from on_waveform_render_done.

    if (waveform_worker.is_busy()) {
        wf_cache.supersede             = true;
        wf_cache.supersede_vp_start    = vp_start;
        wf_cache.supersede_vp_end      = vp_end;
        wf_cache.supersede_trim_begin  = trim_begin;
        wf_cache.supersede_trim_end    = trim_end;
        wf_cache.supersede_area_w      = area.w;
        wf_cache.supersede_area_h      = area.h;
        wf_cache.supersede_audio_gen   = app.audio_generation;
        wf_cache.supersede_target      = is_target;
        wf_cache.supersede_timemap_hash = target_timemap_hash;
        wf_cache.supersede_timemap     = std::move(target_timemap);
        return;
    }

    // Idle: dispatch immediately. Reuse pending_surface if dimensions
    // match; recreate on mismatch (window resize, first allocation).
    if (!wf_cache.pending_surface ||
        wf_cache.pending_width  != area.w ||
        wf_cache.pending_height != area.h) {
        if (wf_cache.pending_surface) {
            cairo_surface_destroy(wf_cache.pending_surface);
            wf_cache.pending_surface = nullptr;
        }
        wf_cache.pending_surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, area.w, area.h);
        wf_cache.pending_width  = area.w;
        wf_cache.pending_height = area.h;
    }

    WaveformJob job;
    job.vp_start       = vp_start;
    job.vp_end         = vp_end;
    job.trim_begin     = trim_begin;
    job.trim_end       = trim_end;
    job.area_w         = area.w;
    job.area_h         = area.h;
    job.audio_gen      = app.audio_generation;
    job.target         = is_target;
    job.timemap_hash   = target_timemap_hash;
    // Stage B: stash a copy of the timemap on the pending slot so the
    // stem cache can read it at completion-swap time. The job consumes
    // the original by move; the copy stays on the cache.
    wf_cache.pending_fp_timemap = target_timemap;
    job.timemap        = std::move(target_timemap);
    job.surface        = wf_cache.pending_surface;
    job.channel_count  = channel_count;
    job.audio          = &audio;

    wf_cache.pending_fp_vp_start    = vp_start;
    wf_cache.pending_fp_vp_end      = vp_end;
    wf_cache.pending_fp_trim_begin  = trim_begin;
    wf_cache.pending_fp_trim_end    = trim_end;
    wf_cache.pending_fp_area_w      = area.w;
    wf_cache.pending_fp_area_h      = area.h;
    wf_cache.pending_fp_audio_gen   = app.audio_generation;
    wf_cache.pending_fp_target      = is_target;
    wf_cache.pending_fp_timemap_hash = target_timemap_hash;

    waveform_worker.dispatch(std::move(job),
        [this](bool ok) { on_waveform_render_done(ok); });
}

void GuiPaintHandler::on_waveform_render_done(bool ok) {
    if (!ok) {
        std::fprintf(stderr,
            "warptempo_gui: waveform worker reported failure; will retry "
            "on next tick\n");
        wf_cache.supersede = false;
        wf_cache.supersede_timemap.clear();
        // Make sure the next maybe_enqueue tick sees the live fingerprint
        // as dirty so we retry. The simplest way is to mark pending_fp_*
        // dirty by resetting audio_gen — comparison will mismatch.
        wf_cache.pending_fp_audio_gen = -1;
        return;
    }

    // Supersede path: a viewport change happened mid-render. Discard the
    // just-completed pending pixels (they'll be overwritten by the next
    // render — no swap, no invalidate) and dispatch a fresh job built
    // from the supersede slot. The pending_surface dimensions may differ
    // from supersede_area_*, so reuse-or-recreate the same way the
    // idle-path does.
    if (wf_cache.supersede) {
        const int sw = wf_cache.supersede_area_w;
        const int sh = wf_cache.supersede_area_h;

        if (!wf_cache.pending_surface ||
            wf_cache.pending_width  != sw ||
            wf_cache.pending_height != sh) {
            if (wf_cache.pending_surface) {
                cairo_surface_destroy(wf_cache.pending_surface);
                wf_cache.pending_surface = nullptr;
            }
            if (sw > 0 && sh > 0) {
                wf_cache.pending_surface = cairo_image_surface_create(
                    CAIRO_FORMAT_ARGB32, sw, sh);
                wf_cache.pending_width  = sw;
                wf_cache.pending_height = sh;
            }
        }

        WaveformJob job;
        job.vp_start       = wf_cache.supersede_vp_start;
        job.vp_end         = wf_cache.supersede_vp_end;
        job.trim_begin     = wf_cache.supersede_trim_begin;
        job.trim_end       = wf_cache.supersede_trim_end;
        job.area_w         = sw;
        job.area_h         = sh;
        job.audio_gen      = wf_cache.supersede_audio_gen;
        job.target         = wf_cache.supersede_target;
        job.timemap_hash   = wf_cache.supersede_timemap_hash;
        // Stage B: thread the supersede timemap into both the job and
        // pending_fp_timemap, the same way the idle-path dispatch does.
        // Copy first, then move into the job — the cache keeps a
        // displayable copy for the post-completion stem rebuild.
        wf_cache.pending_fp_timemap = wf_cache.supersede_timemap;
        job.timemap        = std::move(wf_cache.supersede_timemap);
        job.surface        = wf_cache.pending_surface;
        job.channel_count  = audio.render_channels();
        job.audio          = &audio;

        wf_cache.pending_fp_vp_start    = wf_cache.supersede_vp_start;
        wf_cache.pending_fp_vp_end      = wf_cache.supersede_vp_end;
        wf_cache.pending_fp_trim_begin  = wf_cache.supersede_trim_begin;
        wf_cache.pending_fp_trim_end    = wf_cache.supersede_trim_end;
        wf_cache.pending_fp_area_w      = sw;
        wf_cache.pending_fp_area_h      = sh;
        wf_cache.pending_fp_audio_gen   = wf_cache.supersede_audio_gen;
        wf_cache.pending_fp_target      = wf_cache.supersede_target;
        wf_cache.pending_fp_timemap_hash = wf_cache.supersede_timemap_hash;

        wf_cache.supersede = false;
        wf_cache.supersede_timemap.clear();

        waveform_worker.dispatch(std::move(job),
            [this](bool ok2) { on_waveform_render_done(ok2); });
        return;
    }

    // Swap the pending surface into the live slot. Cairo surface ownership
    // transfers cleanly via pointer swap; no flush needed because the
    // worker's cairo_destroy(ccr) committed the surface fully.
    std::swap(wf_cache.surface,        wf_cache.pending_surface);
    std::swap(wf_cache.width,          wf_cache.pending_width);
    std::swap(wf_cache.height,         wf_cache.pending_height);

    wf_cache.fp_vp_start     = wf_cache.pending_fp_vp_start;
    wf_cache.fp_vp_end       = wf_cache.pending_fp_vp_end;
    wf_cache.fp_trim_begin   = wf_cache.pending_fp_trim_begin;
    wf_cache.fp_trim_end     = wf_cache.pending_fp_trim_end;
    wf_cache.fp_area_w       = wf_cache.pending_fp_area_w;
    wf_cache.fp_area_h       = wf_cache.pending_fp_area_h;
    wf_cache.fp_audio_gen    = wf_cache.pending_fp_audio_gen;
    wf_cache.fp_target       = wf_cache.pending_fp_target;
    wf_cache.fp_timemap_hash = wf_cache.pending_fp_timemap_hash;
    // Stage B: publish the in-flight job's timemap to the displayed slot
    // so the next maybe_rebuild_stem_cache reads the same coordinate
    // system the just-blitted waveform pixels were rendered against.
    std::swap(wf_cache.fp_timemap,     wf_cache.pending_fp_timemap);
    wf_cache.dirty           = false;

    // Invalidate the waveform area so the next paint blits the new
    // pixels. Matches the rect Viewport::invalidate_waveform_area uses.
    const GuiRect a = waveform_area(app);
    gui.invalidate_region(0, 0, app.width, a.y + a.h);
}

// -- Stage B: marker stem cache dirty-detect and rebuild -----------------
//
// Called from on_tick AFTER maybe_enqueue_waveform_render. Reads displayed-
// viewport inputs from wf_cache.fp_* (the LIVE waveform fingerprint — the
// post-swap viewport, not necessarily the current app state); reads
// marker-driven inputs from app state directly. Diverging fingerprint
// triggers a synchronous offscreen rebuild + region invalidation.

namespace {

// FNV-1a over the live drag-overlay state. Folded into the StemCache
// fingerprint in place of the old AppState::drag_overlay_generation
// callsite bump counter — hashing the data directly removes the
// requirement that every future mutation site of app.drag remember to
// bump a counter. Cost is dominated by the loop over moveable_times,
// which at observed selection sizes (0–5) is a handful of nanoseconds.
uint64_t hash_drag_overlay(const DragState& d) {
    uint64_t h = 0xcbf29ce484222325ULL;
    h ^= static_cast<uint64_t>(d.active ? 1 : 0);
    h *= 0x100000001b3ULL;
    h ^= static_cast<uint64_t>(d.dragging_markers.size());
    h *= 0x100000001b3ULL;
    for (int idx : d.dragging_markers) {
        h ^= static_cast<uint64_t>(idx);
        h *= 0x100000001b3ULL;
    }
    // moveable_times is parallel to dragging_markers; equal-length by
    // invariant. memcpy each double's bit pattern into a uint64 so the
    // floating-point representation is captured exactly (no equality /
    // NaN considerations).
    for (double t : d.moveable_times) {
        uint64_t bits;
        std::memcpy(&bits, &t, sizeof(bits));
        h ^= bits;
        h *= 0x100000001b3ULL;
    }
    return h;
}

// FNV-1a over the live selection set + last-selected anchor. Folded
// into the FlagCache fingerprint (Stage C) to avoid distributing a
// generation-bump across the fifteen mutation sites of selected_markers.
uint64_t hash_selection(const std::set<int>& s,
                        int last_selected) {
    uint64_t h = 0xcbf29ce484222325ULL;
    h ^= static_cast<uint64_t>(s.size());
    h *= 0x100000001b3ULL;
    for (int idx : s) {
        h ^= static_cast<uint64_t>(idx);
        h *= 0x100000001b3ULL;
    }
    h ^= static_cast<uint64_t>(last_selected);
    h *= 0x100000001b3ULL;
    return h;
}

} // namespace

void GuiPaintHandler::maybe_rebuild_stem_cache() {
    if (app.loading || audio.total_frames() <= 0) return;

    // No live waveform yet → no stems. The first stem rebuild happens
    // after the first waveform-completion swap (which sets
    // wf_cache.fp_audio_gen >= 0); until then the displayed-viewport
    // fields hold defaults that wouldn't agree with anything sensible
    // on the marker side anyway.
    if (wf_cache.fp_audio_gen < 0) return;

    const GuiRect area = waveform_area(app);
    if (area.w <= 0 || area.h <= 0) return;

    // Surface includes the stem overhang above the waveform — see the
    // geometry note in StemCache's class comment.
    const int surface_w = area.w;
    const int surface_h = area.h + static_cast<int>(kStemAboveWaveformPx);

    // Displayed-viewport inputs: read from wf_cache.fp_*, not app state.
    const int64_t  vp_start     = wf_cache.fp_vp_start;
    const int64_t  vp_end       = wf_cache.fp_vp_end;
    const int64_t  trim_begin   = wf_cache.fp_trim_begin;
    const int64_t  trim_end     = wf_cache.fp_trim_end;
    const bool     is_target    = wf_cache.fp_target;
    const uint64_t timemap_hash = wf_cache.fp_timemap_hash;
    const long long audio_gen   = wf_cache.fp_audio_gen;

    // Marker-driven inputs: read live from app state.
    const long long warp_gen   = app.warpmarkers.generation();
    const long long phase_gen  = app.phase_reset_markers.generation();
    const uint64_t  drag_hash  = hash_drag_overlay(app.drag);
    const bool     drag_active = app.drag.active;
    const char     mv          = app.active_markers_view;
    const bool     rve         = app.render_view_enabled;

    const bool matches =
        stem_cache.surface &&
        stem_cache.fp_audio_gen               == audio_gen &&
        stem_cache.fp_vp_start                == vp_start &&
        stem_cache.fp_vp_end                  == vp_end &&
        stem_cache.fp_trim_begin              == trim_begin &&
        stem_cache.fp_trim_end                == trim_end &&
        stem_cache.fp_area_w                  == surface_w &&
        stem_cache.fp_area_h                  == surface_h &&
        stem_cache.fp_target                  == is_target &&
        stem_cache.fp_timemap_hash            == timemap_hash &&
        stem_cache.fp_warpmarker_generation   == warp_gen &&
        stem_cache.fp_phase_reset_generation  == phase_gen &&
        stem_cache.fp_drag_overlay_hash       == drag_hash &&
        stem_cache.fp_drag_active             == drag_active &&
        stem_cache.fp_active_markers_view     == mv &&
        stem_cache.fp_render_view_enabled     == rve;

    if (matches) return;

    // Reuse-or-recreate the surface on dimension change.
    if (!stem_cache.surface ||
        stem_cache.width  != surface_w ||
        stem_cache.height != surface_h) {
        if (stem_cache.surface) {
            cairo_surface_destroy(stem_cache.surface);
            stem_cache.surface = nullptr;
        }
        stem_cache.surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, surface_w, surface_h);
        stem_cache.width  = surface_w;
        stem_cache.height = surface_h;
    }

    cairo_t* ccr = cairo_create(stem_cache.surface);
    cairo_save(ccr);
    cairo_set_operator(ccr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(ccr);
    cairo_restore(ccr);

    // Local rect translates the screen-coord stem geometry into the
    // cache surface's coordinate system. render_markers computes
    // y_stem_top = waveform_area.y - kStemAboveWaveformPx and
    // y1 = waveform_area.y + waveform_area.h. Setting local.y =
    // kStemAboveWaveformPx makes y_stem_top = 0 (top of surface) and
    // y1 = kStemAboveWaveformPx + area.h = surface_h (bottom of surface).
    // The blit at on_redraw time positions the surface at screen y =
    // area.y - kStemAboveWaveformPx so the stem overhang lands correctly.
    const GuiRect local_area{
        0,
        static_cast<int>(kStemAboveWaveformPx),
        surface_w,
        area.h
    };
    const TrimRange trim_struct{trim_begin, trim_end};
    const int sr = audio.sample_rate();

    // Target-view stems consume the displayed timemap (the one baked
    // into the live waveform pixels), not a freshly-built one — keeps
    // stem positions consistent with the displayed waveform during the
    // worker's rebuild window.
    const std::vector<TimeMapSegment>* tmap_arg =
        (is_target && !wf_cache.fp_timemap.empty())
            ? &wf_cache.fp_timemap : nullptr;

    // Drag overlay: pass through only when a drag is live. During a
    // drag the fingerprint mismatches every tick on the drag-overlay
    // hash alone (moveable_times[k] changes on every motion event), so
    // this rebuild reads the live moveable_times each pass.
    DragOverlay drag_overlay_storage;
    const DragOverlay* drag_overlay = nullptr;
    if (drag_active) {
        drag_overlay_storage.indices = &app.drag.dragging_markers;
        drag_overlay_storage.times   = &app.drag.moveable_times;
        drag_overlay = &drag_overlay_storage;
    }

    if (mv == 'P') {
        const auto& list = rve
            ? app.render_view_phase_resets
            : app.phase_reset_markers.markers();
        render_phase_reset_markers(
            ccr, local_area, list,
            vp_start, vp_end, sr,
            trim_struct, tmap_arg, drag_overlay);
    } else {
        const auto& list = rve
            ? app.render_view_markers
            : app.warpmarkers.markers();
        render_markers(
            ccr, local_area, list,
            vp_start, vp_end, sr,
            trim_struct, tmap_arg, drag_overlay);
    }

    cairo_destroy(ccr);

    stem_cache.fp_audio_gen                 = audio_gen;
    stem_cache.fp_vp_start                  = vp_start;
    stem_cache.fp_vp_end                    = vp_end;
    stem_cache.fp_trim_begin                = trim_begin;
    stem_cache.fp_trim_end                  = trim_end;
    stem_cache.fp_area_w                    = surface_w;
    stem_cache.fp_area_h                    = surface_h;
    stem_cache.fp_target                    = is_target;
    stem_cache.fp_timemap_hash              = timemap_hash;
    stem_cache.fp_warpmarker_generation     = warp_gen;
    stem_cache.fp_phase_reset_generation    = phase_gen;
    stem_cache.fp_drag_overlay_hash         = drag_hash;
    stem_cache.fp_drag_active               = drag_active;
    stem_cache.fp_active_markers_view       = mv;
    stem_cache.fp_render_view_enabled       = rve;
    stem_cache.dirty                        = false;

    // Invalidate the stem region. Viewport-driven invalidations
    // already cover this strip, but pure marker-store edits (warp_gen /
    // phase_gen bumps) don't pass through the viewport's invalidator —
    // damage the strip explicitly so the next paint blits the new
    // pixels. Idempotent against the waveform's own damage.
    gui.invalidate_region(
        0,
        area.y - static_cast<int>(kStemAboveWaveformPx),
        app.width,
        surface_h);
}

// -- Stage C: flag-rect cache dirty-detect and rebuild -------------------
//
// Mirrors maybe_rebuild_stem_cache: same wf_cache.fp_* coupling for the
// displayed-viewport half of the fingerprint; same live-app-state reads
// for the marker-driven half (with selection + editor target additions).
// The cache holds every flag rect EXCEPT the FlagPayload-editor target
// (skipped via the render_flags skip-guard so the live editor render in
// on_redraw owns those pixels — keeps the cache fingerprint independent
// of pending-text width and cursor blink).

void GuiPaintHandler::maybe_rebuild_flag_cache() {
    if (app.loading || audio.total_frames() <= 0) return;

    // No live waveform yet → no flags. Same pre-first-completion guard as
    // the stem cache uses; until wf_cache.fp_audio_gen comes up after the
    // first worker swap, the displayed-viewport fields hold defaults that
    // wouldn't agree with anything sensible.
    if (wf_cache.fp_audio_gen < 0) return;

    const GuiRect top_strip = top_strip_area(app);
    if (top_strip.w <= 0 || top_strip.h <= 0) return;

    const int surface_w = top_strip.w;
    const int surface_h = top_strip.h;

    // Displayed-viewport inputs from wf_cache.fp_*.
    const int64_t  vp_start     = wf_cache.fp_vp_start;
    const int64_t  vp_end       = wf_cache.fp_vp_end;
    const int64_t  trim_begin   = wf_cache.fp_trim_begin;
    const int64_t  trim_end     = wf_cache.fp_trim_end;
    const bool     is_target    = wf_cache.fp_target;
    const uint64_t timemap_hash = wf_cache.fp_timemap_hash;
    const long long audio_gen   = wf_cache.fp_audio_gen;

    // Marker-driven inputs from app state.
    const long long warp_gen   = app.warpmarkers.generation();
    const long long phase_gen  = app.phase_reset_markers.generation();
    const uint64_t  drag_hash  = hash_drag_overlay(app.drag);
    const uint64_t  sel_hash   = hash_selection(
                                     app.selected_markers,
                                     app.last_selected_marker);
    const char      mv         = app.active_markers_view;
    const bool      rve        = app.render_view_enabled;

    // Editor targets. The FlagPayload editor's target drives the skip-
    // guard (cache leaves a hole for the live editor render to fill).
    // The iter/BPM popup editor's target drives outline suppression on
    // the underlying flag rect (cache paints the flag without its
    // selected outline so the popup above is the only highlighted
    // element). Modes are mutually exclusive per text_editor::Kind.
    int popup_target = -1;
    int flag_target  = -1;
    if (text_editor::is_active(app.top_flag_editor)) {
        switch (app.top_flag_editor.kind) {
            case text_editor::Kind::FlagPayload:
                flag_target = app.top_flag_editor.target;
                break;
            case text_editor::Kind::IterationBracket:
            case text_editor::Kind::BpmBracket:
                popup_target = app.top_flag_editor.target;
                break;
            default:
                break;
        }
    }

    const bool matches =
        flag_cache.surface &&
        flag_cache.fp_audio_gen               == audio_gen &&
        flag_cache.fp_vp_start                == vp_start &&
        flag_cache.fp_vp_end                  == vp_end &&
        flag_cache.fp_trim_begin              == trim_begin &&
        flag_cache.fp_trim_end                == trim_end &&
        flag_cache.fp_area_w                  == surface_w &&
        flag_cache.fp_area_h                  == surface_h &&
        flag_cache.fp_target                  == is_target &&
        flag_cache.fp_timemap_hash            == timemap_hash &&
        flag_cache.fp_warpmarker_generation   == warp_gen &&
        flag_cache.fp_phase_reset_generation  == phase_gen &&
        flag_cache.fp_drag_overlay_hash       == drag_hash &&
        flag_cache.fp_selection_hash          == sel_hash &&
        flag_cache.fp_active_markers_view     == mv &&
        flag_cache.fp_render_view_enabled     == rve &&
        flag_cache.fp_popup_editor_target     == popup_target &&
        flag_cache.fp_flag_editor_target      == flag_target;

    if (matches) return;

    if (!flag_cache.surface ||
        flag_cache.width  != surface_w ||
        flag_cache.height != surface_h) {
        if (flag_cache.surface) {
            cairo_surface_destroy(flag_cache.surface);
            flag_cache.surface = nullptr;
        }
        flag_cache.surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, surface_w, surface_h);
        flag_cache.width  = surface_w;
        flag_cache.height = surface_h;
    }

    cairo_t* ccr = cairo_create(flag_cache.surface);
    cairo_save(ccr);
    cairo_set_operator(ccr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(ccr);
    cairo_restore(ccr);

    // top_strip_area is anchored at (0, 0), so the local rect equals the
    // surface rect. The blit at on_redraw time positions the surface back
    // at screen (0, 0).
    const GuiRect local_top_strip{0, 0, surface_w, surface_h};
    const TrimRange trim_struct{trim_begin, trim_end};
    const int sr = audio.sample_rate();

    const std::vector<TimeMapSegment>* tmap_arg =
        (is_target && !wf_cache.fp_timemap.empty())
            ? &wf_cache.fp_timemap : nullptr;

    DragOverlay drag_overlay_storage;
    const DragOverlay* drag_overlay = nullptr;
    if (app.drag.active) {
        drag_overlay_storage.indices = &app.drag.dragging_markers;
        drag_overlay_storage.times   = &app.drag.moveable_times;
        drag_overlay = &drag_overlay_storage;
    }

    // Cache overlay: popup_editor_target suppresses the iter/BPM-popup
    // target's selection outline; marker_index activates the skip-guard
    // for the FlagPayload editor target. Other fields stay defaulted —
    // pending text, cursor state, selection range live in the live
    // editor render only.
    FlagEditorOverlay cache_overlay;
    cache_overlay.popup_editor_target = popup_target;
    cache_overlay.marker_index        = flag_target;

    if (rve) {
        if (mv == 'P') {
            render_phase_reset_flags(
                ccr, local_top_strip,
                app.render_view_phase_resets,
                vp_start, vp_end, sr,
                kFlagFontSize,
                app.selected_markers,
                trim_struct,
                nullptr,
                drag_overlay);
        } else {
            render_flags(ccr, local_top_strip,
                         app.render_view_markers,
                         vp_start, vp_end, sr,
                         kFlagFontSize,
                         app.selected_markers,
                         trim_struct,
                         cache_overlay,
                         nullptr,
                         drag_overlay);
        }
    } else if (mv == 'P') {
        render_phase_reset_flags(
            ccr, local_top_strip,
            app.phase_reset_markers.markers(),
            vp_start, vp_end, sr,
            kFlagFontSize,
            app.selected_markers,
            trim_struct,
            tmap_arg,
            drag_overlay);
    } else {
        render_flags(ccr, local_top_strip,
                     app.warpmarkers.markers(),
                     vp_start, vp_end, sr,
                     kFlagFontSize,
                     app.selected_markers,
                     trim_struct,
                     cache_overlay,
                     tmap_arg,
                     drag_overlay);
    }

    cairo_destroy(ccr);

    flag_cache.fp_audio_gen               = audio_gen;
    flag_cache.fp_vp_start                = vp_start;
    flag_cache.fp_vp_end                  = vp_end;
    flag_cache.fp_trim_begin              = trim_begin;
    flag_cache.fp_trim_end                = trim_end;
    flag_cache.fp_area_w                  = surface_w;
    flag_cache.fp_area_h                  = surface_h;
    flag_cache.fp_target                  = is_target;
    flag_cache.fp_timemap_hash            = timemap_hash;
    flag_cache.fp_warpmarker_generation   = warp_gen;
    flag_cache.fp_phase_reset_generation  = phase_gen;
    flag_cache.fp_drag_overlay_hash       = drag_hash;
    flag_cache.fp_selection_hash          = sel_hash;
    flag_cache.fp_active_markers_view     = mv;
    flag_cache.fp_render_view_enabled     = rve;
    flag_cache.fp_popup_editor_target     = popup_target;
    flag_cache.fp_flag_editor_target      = flag_target;
    flag_cache.dirty                      = false;

    gui.invalidate_region(top_strip.x, top_strip.y,
                          top_strip.w, top_strip.h);
}

// -- GuiPaintHandler::on_resize ------------------------------------------

void GuiPaintHandler::on_resize(int w, int h) {
    app.width  = w;
    app.height = h;
    if (app.loading || audio.total_frames() <= 0) return;

    // A numeric zoom level may have been valid at the old width but show
    // more samples than the file at the new width — promote to fit-file.
    // live_total_frames returns target_view_total_frames in target view
    // so the cap is consistent with the deformed timeline's length.
    const int max_num = max_valid_numeric_level(
        waveform_area(app).w, live_total_frames(app, audio), audio.sample_rate());
    if (app.zoom_level != kFitFileLevel) {
        if (max_num < 0 || app.zoom_level > max_num) {
            app.zoom_level = kFitFileLevel;
            app.viewport_start_sample = 0;
            if (playback.is_playing()) playback.resync_predictor();
        }
    }
    clamp_viewport_start(app, audio);
}
