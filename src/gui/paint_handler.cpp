#include "paint_handler.h"

#include "render.h"
#include "text_display.h"
#include "text_editor.h"
#include "time_format.h"
#include "frame_map_view.h"
#include "waveform_worker.h"
#include "frame_map.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

// Paint cluster. Method bodies are byte-identical to the lambdas
// they replaced in main.cpp (set_on_redraw at the original main.cpp:999
// and set_on_resize at the original main.cpp:1892). The only changes are:
//
//   - Capture-by-reference of `app`, `audio`, `playback`, `wf_cache`,
//     `gui` is now reference-member access on `this`. Identifier spelling
//     is identical so nothing else changes inside the bodies.
//   - `bottom_strip_wide()` (the old lambda capture) is replaced with the
//     free-function form `bottom_strip_wide(app)` declared in app_state.h.

// -- render_waveform_to_cache_surface ------------------------------------
//
// Extracted from on_redraw's inline cairo_create/cairo_destroy
// block (the body that lived between fingerprint-check and blit). Runs on
// the waveform worker thread when the main path goes through GuiWaveformWorker;
// the function itself is thread-agnostic — it touches only the dest surface
// the caller passed in, the audio handle's peak pyramid (read-only after
// load), and the frame_map snapshot the caller built. perf_counters
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
    const std::vector<FrameMapSegment>* frame_map_or_null) {
    if (!dest || area_w <= 0 || area_h <= 0) return;

    cairo_t* ccr = cairo_create(dest);
    // Clear to transparent — the pixmap's background fill shows through
    // wherever the waveform strokes don't paint.
    cairo_save(ccr);
    cairo_set_operator(ccr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(ccr);
    cairo_restore(ccr);
    // Samples draw into an inset sub-rect of the full-height cache surface:
    // kWaveformInsetPx clear at top and bottom (the top band holds the cursor
    // triangle; the bottom mirrors it so the waveform is centered in its area).
    // The surface itself is still area_w x area_h and is blitted at area.y, so
    // the cache fingerprint, stem cache, and blit are unaffected — the inset is
    // a property of sample drawing only.
    const int inset_h = area_h - 2 * kWaveformInsetPx;
    if (inset_h <= 0) { cairo_destroy(ccr); return; }
    const GuiRect cache_area{0, kWaveformInsetPx, area_w, inset_h};
    if (channel_count == 1) {
        render_waveform(ccr, cache_area, audio, 0,
                        vp_start, vp_end,
                        kWaveform,
                        frame_map_or_null);
    } else if (channel_count >= 2) {
        // Channel gap removed (kChannelGapPx deleted): the 1972 Krips material
        // is effectively never unity, so the two channels' inner excursions do
        // not visually collide at the shared midline; a plain halve of the
        // inset region is clean. The two channels share the single inset band
        // (inset first, then split), so 10px stays clear above the top channel
        // and below the bottom channel, with the channels meeting at the inset
        // region's vertical center.
        const int ch_h = cache_area.h / 2;
        const GuiRect ch0{0, cache_area.y, cache_area.w, ch_h};
        const GuiRect ch1{0, cache_area.y + ch_h, cache_area.w, ch_h};
        render_waveform(ccr, ch0, audio, 0,
                        vp_start, vp_end,
                        kWaveform,
                        frame_map_or_null);
        render_waveform(ccr, ch1, audio, 1,
                        vp_start, vp_end,
                        kWaveform,
                        frame_map_or_null);
    }
    cairo_destroy(ccr);
}

// -- render_waveform_strip_to_cache_surface ------------------------------
//
// Incremental-pan strip render. Redraws only the [strip_x, strip_x+strip_w)
// column of the plate (full height, including the inset bands) and leaves
// every other column untouched — the caller has already memmove'd the
// reusable pixels into place, and this fills the newly exposed edge.
//
// vp_start_full / vp_end_full describe the WHOLE plate's displayed viewport
// (not the strip's). The strip's own sample range is derived from them so the
// strip columns land at the exact frames a full-plate render at this viewport
// would produce; the shifted pixels and the freshly rendered strip then meet
// seamlessly at the strip boundary. Mirrors render_waveform_to_cache_surface's
// inset + mono/stereo split, restricted to the strip columns and clipped so a
// 1px stroke cannot bleed past the strip edge into the reused pixels.
//
// Runs inline on the GUI thread (the strip is at most a window wide; see
// pan_waveform_incremental's over-a-window fallback), so unlike the worker
// render it touches the LIVE wf_cache.surface directly. Safe because the pan
// path only takes this branch when the worker is idle.
static void render_waveform_strip_to_cache_surface(
    cairo_surface_t* dest,
    int area_w,
    int area_h,
    int strip_x,
    int strip_w,
    int channel_count,
    const GuiAudio& audio,
    int64_t vp_start_full,
    int64_t vp_end_full,
    const std::vector<FrameMapSegment>* frame_map_or_null) {
    if (!dest || area_w <= 0 || area_h <= 0) return;
    if (strip_w <= 0 || strip_x < 0 || strip_x + strip_w > area_w) return;
    if (vp_end_full <= vp_start_full) return;

    const double disp_spp =
        static_cast<double>(vp_end_full - vp_start_full) / area_w;
    const int64_t strip_vp_start = vp_start_full +
        static_cast<int64_t>(std::nearbyint(disp_spp * strip_x));
    const int64_t strip_vp_end   = vp_start_full +
        static_cast<int64_t>(std::nearbyint(disp_spp * (strip_x + strip_w)));

    cairo_t* ccr = cairo_create(dest);

    // Clear only the strip column (full height, incl. the inset bands) so the
    // shifted-in pixels in the rest of the plate are left intact.
    cairo_save(ccr);
    cairo_rectangle(ccr, strip_x, 0, strip_w, area_h);
    cairo_clip(ccr);
    cairo_set_operator(ccr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(ccr);
    cairo_restore(ccr);

    // Re-clip for the strokes so render_waveform's 1px line width cannot bleed
    // out of the strip into the reused columns.
    cairo_save(ccr);
    cairo_rectangle(ccr, strip_x, 0, strip_w, area_h);
    cairo_clip(ccr);

    const int inset_h = area_h - 2 * kWaveformInsetPx;
    if (inset_h <= 0) { cairo_restore(ccr); cairo_destroy(ccr); return; }
    if (channel_count == 1) {
        const GuiRect a{strip_x, kWaveformInsetPx, strip_w, inset_h};
        render_waveform(ccr, a, audio, 0,
                        strip_vp_start, strip_vp_end,
                        kWaveform, frame_map_or_null);
    } else if (channel_count >= 2) {
        const int ch_h = inset_h / 2;
        const GuiRect ch0{strip_x, kWaveformInsetPx, strip_w, ch_h};
        const GuiRect ch1{strip_x, kWaveformInsetPx + ch_h, strip_w, ch_h};
        render_waveform(ccr, ch0, audio, 0,
                        strip_vp_start, strip_vp_end,
                        kWaveform, frame_map_or_null);
        render_waveform(ccr, ch1, audio, 1,
                        strip_vp_start, strip_vp_end,
                        kWaveform, frame_map_or_null);
    }
    cairo_restore(ccr);
    cairo_destroy(ccr);
}

// F2: the settings-prompt editor and the BPM editor paint the same
// bottom-strip text box through render_editor_text_box, differing only
// in the prefix and which text_editor::State they read. This is the one
// body both branches share. It takes the row geometry (anchor_x,
// baseline_y) the caller already solved (upper_baseline) rather than
// computing a row of its own, so the two call sites stay the single
// source for where the bottom-strip editor sits.
static void render_bottom_strip_editor(cairo_t* cr,
                                       const text_editor::State& ed,
                                       const char* prefix,
                                       double anchor_x,
                                       double baseline_y) {
    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, kFlagFontSize);

    EditorTextBox box;
    box.anchor_x        = anchor_x;
    box.baseline_y      = baseline_y;
    box.prefix          = prefix;
    box.text            = ed.pending;
    box.hl_pad          = kFlagPadXPx;
    box.fill            = ed.red ? kAccent : kBackground;
    box.text_color      = kText;
    box.has_selection   = text_editor::has_selection(ed);
    box.selection_start = text_editor::selection_start(ed);
    box.selection_end   = text_editor::selection_end(ed);
    box.cursor_visible  = text_editor::cursor_visible_now(ed);
    box.cursor_pos      = ed.cursor_pos;
    render_editor_text_box(cr, box);

    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_flag_annotations -----------------------------

void GuiPaintHandler::paint_flag_annotations(cairo_t* cr,
                                             const GuiRect& top_strip,
                                             int sr) {
    // Flag annotations in the top strip. The steady-state flag-rect pixels
    // live on flag_cache.surface (rebuilt from on_tick via
    // maybe_rebuild_flag_cache); this blits the cache, then paints the
    // per-frame live work — the FlagPayload editor's pending text + cursor.
    // The old floating iter/BPM/hover popup surfaces are gone; their
    // presentations re-homed — iteration ranges render into the flags
    // themselves, and the BPM editor and hover readout render in the bottom
    // strip — so nothing in this pass is dark. Like the other caches, the
    // surface may be null on the very first paint after a load (before the
    // first rebuild fires); the blit is skipped and the background shows
    // through for that one frame.
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

    // Drag-time position overlay. Active for the duration of a ctrl-drag;
    // non-null only when app.drag.active. Threaded into render_one_editor_flag
    // so the editor flag tracks the dragged marker's proposed (moveable_times)
    // position.
    DragOverlay drag_overlay_storage;
    const DragOverlay* drag_overlay = nullptr;
    if (app.drag.active) {
        drag_overlay_storage.indices = &app.drag.dragging_markers;
        drag_overlay_storage.times   = &app.drag.moveable_times;
        drag_overlay = &drag_overlay_storage;
    }

    // Displayed-viewport locals for live items that must align with the
    // cached flag pixels. The cache renders against wf_cache.fp_*; the live
    // editor flag and popup anchor math reads these *_disp locals so it
    // agrees with the cache during the worker's 1-2 frame rebuild window
    // (after a viewport gesture, before the worker's swap).
    const int64_t  vp_start_disp = wf_cache.fp_vp_start;
    const int64_t  vp_end_disp   = wf_cache.fp_vp_end;
    const std::vector<FrameMapSegment>* tmap_disp =
        (wf_cache.fp_target && !wf_cache.fp_frame_map.empty())
            ? &wf_cache.fp_frame_map : nullptr;

    // Built once, threaded into the live render_one_editor_flag call below.
    // Reads only app.top_flag_editor, which has no view-domain distinction.
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
    }

    // Live editor flag: only paints in W marker-view and not render-view
    // (FlagPayload editor isn't available in either 'P' or render-view paths).
    // The cache leaves a hole over the editor target via the skip-guard in
    // render_flags, so this fill is mandatory whenever overlay.marker_index
    // >= 0 — otherwise that flag's pixels would be missing entirely.
    if (overlay.marker_index >= 0 &&
        !app.render_view.enabled &&
        app.active_markers_view != 'P') {
        render_one_editor_flag(
            cr, top_strip,
            app.warpmarkers.markers(),
            vp_start_disp, vp_end_disp, sr,
            kFlagFontSize,
            app.selected_markers,
            overlay,
            tmap_disp,
            drag_overlay,
            app.iteration_mode_enabled &&
                app.active_markers_view == 'W');
    }
}

// -- GuiPaintHandler::paint_waveform_plate -------------------------------

void GuiPaintHandler::paint_waveform_plate(cairo_t* cr, const GuiRect& area) {
    // The synchronous rebuild that used to live in this
    // block is gone. wf_cache.surface is now produced by one of
    // three paths, all of which leave this paint path blit-only:
    //   1. Worker full render — maybe_enqueue_waveform_render
    //      dispatches a full-window render on GuiWaveformWorker,
    //      which swaps into wf_cache.surface on completion. Fires
    //      on the on_tick backstop and on non-pan viewport changes
    //      (zoom, center-on-playhead, follow-scroll), plus resize,
    //      reload, and target-view frame_map changes.
    //   2. Incremental shift-and-strip — a pure horizontal pan
    //      (scroll_viewport) calls pan_waveform_incremental, which
    //      shifts the existing plate pixels by the pan delta and
    //      synchronously renders only the newly-exposed edge strip.
    //      Pans bypass the worker entirely; this is the fast path
    //      that keeps fast scrolling continuous.
    //   3. Synchronous full render — force_synchronous_waveform_
    //      rebuild renders the full window inline on the GUI thread,
    //      as does the pan_waveform_incremental fallback when a
    //      single pan exceeds a window width (nothing to shift).
    // The paint path is blit-only — it draws whatever pixels the
    // live surface currently holds. For worker-path renders that may
    // be a one- or two-frame-old viewport during the worker-rebuild
    // window; the incremental pan path updates the plate in the same
    // frame, so it has no such lag. The stem and flag layers close
    // any mismatch by layering markers and flags onto surfaces keyed
    // off the same displayed-viewport.
    //
    // If wf_cache.surface is null (initial load, before the first
    // worker completion), the blit is skipped and the background
    // fill shows through. The user-visible difference is one
    // extra paint frame of background between load and first
    // waveform display, masked by the existing load-time progress
    // bar.
    if (wf_cache.surface) {
        cairo_save(cr);
        cairo_rectangle(cr, area.x, area.y, area.w, area.h);
        cairo_clip(cr);
        cairo_set_source_surface(cr, wf_cache.surface,
                                 area.x, area.y);
        cairo_paint(cr);
        cairo_restore(cr);

        // Out-of-trim dim, composited live over the just-blitted
        // plate (which is trim-agnostic). The dim is the
        // kWaveformDimmed color masked through the plate surface's
        // own alpha: opaque sample pixels are recolored — at the
        // exact tuned RGB, no blend — and the transparent gaps are
        // left as background. ATOP would key on the WINDOW's alpha,
        // which is already opaque post-blit, so it can't serve as the
        // sample mask and fills the whole rect solid; the plate
        // surface's alpha can. We clip to the LIVE out-of-trim
        // rect(s) (trim + viewport), so a trim drag tracks the stem
        // frame-for-frame with no plate rebuild, then mask the dim
        // color through the plate (blitted at (area.x, area.y), so
        // the mask uses the same origin). OVER + mask is exactly
        // "color where the plate is opaque, within the clip" — no
        // operator change. cairo_save/restore brackets the clip so
        // the marker, stem, flag, and playhead passes that follow run
        // unclipped with the default OVER.
        const OutOfTrimRects dim = compute_out_of_trim_rects(area);
        if (dim.has_left || dim.has_right) {
            cairo_save(cr);
            if (dim.has_left)
                cairo_rectangle(cr, dim.left.x, dim.left.y,
                                dim.left.w, dim.left.h);
            if (dim.has_right)
                cairo_rectangle(cr, dim.right.x, dim.right.y,
                                dim.right.w, dim.right.h);
            cairo_clip(cr);
            cairo_set_source_rgb(cr, kWaveformDimmed.r,
                                 kWaveformDimmed.g, kWaveformDimmed.b);
            cairo_mask_surface(cr, wf_cache.surface, area.x, area.y);
            cairo_restore(cr);
        }
    }
}

// -- GuiPaintHandler::paint_marker_stems ---------------------------------

void GuiPaintHandler::paint_marker_stems(cairo_t* cr,
                                         const GuiRect& marker_paint_rect) {
    // The marker stems live on stem_cache.surface,
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
}

// -- GuiPaintHandler::paint_playheads ------------------------------------

void GuiPaintHandler::paint_playheads(cairo_t* cr, const GuiRect& area) {
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

    // Playhead drawn last so its stem and triangle paint over any
    // marker connector pixels they share a column with — the
    // playhead must never be occluded by marker stems or flag
    // annotations. The triangle indicator lives in the top
    // strip, so render whenever either the waveform or top strip is
    // exposed; otherwise a flag-strip-only repaint would erase the
    // triangle.
    //
    // Split-playhead paint order: scanner first (line only, gated
    // on playhead_scanner_active), then cursor (line + triangle).
    // The cursor draws over the scanner on overlap.
    if (app.playhead_scanner_active) {
        const double scan_px = scanner_pixel_x(app, audio,
                                               wf_cache.fp_vp_start,
                                               disp_spp);
        render_playhead(cr, area, scan_px, kPlayheadScanner,
                        gui.playhead_triangle_surface(),
                        /*draw_triangle=*/false,
                        /*ink_plate=*/wf_cache.surface);
    }
    render_playhead(cr, area, px_x, kPlayheadCursor,
                    gui.playhead_triangle_surface(),
                    /*draw_triangle=*/true,
                    /*ink_plate=*/wf_cache.surface);
}

// -- GuiPaintHandler::paint_debug_hit_rects ------------------------------

void GuiPaintHandler::paint_debug_hit_rects(cairo_t* cr,
                                            const GuiRect& area,
                                            const GuiRect& top_strip,
                                            int sr) {
    // Recompute hit rects EXACTLY as hit_test_flag does — live viewport,
    // not the displayed cache fingerprint the paint above used. If these
    // strokes are offset from the painted chips, the divergence is the
    // viewport/coordinate space, not the chip-rect formula.
    const double dbg_spp = current_samples_per_pixel(app, audio);
    const int64_t dbg_vp_start = app.viewport_start_sample;
    const int64_t dbg_vp_end = dbg_vp_start +
        static_cast<int64_t>(std::nearbyint(dbg_spp * area.w));

    std::vector<FrameMapSegment> dbg_tmap;
    if (!app.render_view.enabled &&
        app.active_audio_view == 'T') {
        if (app.drag.active) {
            dbg_tmap = app.drag.frozen_frame_map;
        } else {
            dbg_tmap = build_target_view_frame_map(
                app, sr,
                static_cast<long>(audio.total_frames()));
        }
    }
    const std::vector<FrameMapSegment>* dbg_tmap_arg =
        dbg_tmap.empty() ? nullptr : &dbg_tmap;

    DragOverlay dbg_drag_storage;
    const DragOverlay* dbg_drag = nullptr;
    if (app.drag.active) {
        dbg_drag_storage.indices = &app.drag.dragging_markers;
        dbg_drag_storage.times   = &app.drag.moveable_times;
        dbg_drag = &dbg_drag_storage;
    }

    std::vector<FlagHitRect> dbg_rects;
    if (app.render_view.enabled) {
        dbg_rects = compute_flag_hit_rects(
            top_strip, app.render_view.markers,
            dbg_vp_start, dbg_vp_end, sr, kFlagFontSize,
            nullptr, dbg_drag);
    } else if (app.active_markers_view == 'P') {
        dbg_rects = compute_phase_reset_flag_hit_rects(
            top_strip, app.phase_reset_markers.markers(),
            dbg_vp_start, dbg_vp_end, sr, kFlagFontSize,
            dbg_tmap_arg, dbg_drag);
    } else {
        dbg_rects = compute_flag_hit_rects(
            top_strip, app.warpmarkers.markers(),
            dbg_vp_start, dbg_vp_end, sr, kFlagFontSize,
            dbg_tmap_arg, dbg_drag,
            app.iteration_mode_enabled);
    }

    // Stroke each hit rect in bright magenta, 1px, half-pixel aligned so
    // a 1px offset from the chip fill is unambiguous. The stroke sits
    // ON the rect boundary: left edge at rect.x, right edge at
    // rect.x+rect.w. Compare against the chip fill's painted span.
    cairo_save(cr);
    cairo_set_source_rgb(cr, 1.0, 0.0, 1.0);
    cairo_set_line_width(cr, 1.0);
    for (const auto& r : dbg_rects) {
        cairo_rectangle(cr, r.x + 0.5, r.y + 0.5,
                        r.w - 1.0, r.h - 1.0);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
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
        // Blank plate during load; the only feedback is the bottom-strip
        // upper-row status ("loading..."), the same slot renders use. Painted
        // here because the total>0 bottom-strip block below does not run while
        // loading (and total_frames is 0 on a cold launch).
        const GuiRect upper_row = bottom_upper_row_area(app);
        const double  upper_baseline =
            upper_row.y + monospace_row_baseline_offset();
        text_display::draw_line(
            cr, static_cast<double>(kTimestampPadX), upper_baseline,
            app.queue_progress_text, kText, kFlagFontSize);
    } else if (audio.total_frames() > 0) {
        const GuiRect area       = waveform_area(app);
        const GuiRect top_strip  = top_strip_area(app);
        const GuiRect exposed{x, y, w, h};
        const int     sr         = audio.sample_rate();

        // Live viewport / target-frame_map / trim computations
        // that used to drive on_redraw's render_flags / render_markers
        // calls have moved into the cache rebuild paths (waveform via
        // the worker, stems via maybe_rebuild_stem_cache, flags
        // via maybe_rebuild_flag_cache). on_redraw now reads
        // wf_cache.fp_* for displayed-viewport inputs and treats every
        // strip as a blit-then-overlay path.

        {
            const auto wf0 = clock::now();
            if (rects_intersect(exposed, area)) {
                paint_waveform_plate(cr, area);
            }
            const auto wf1 = clock::now();
            t_waveform_ms =
                std::chrono::duration<double, std::milli>(wf1 - wf0).count();
        }

        // Markers: vertical stems in the waveform area, beneath the
        // playhead. Cairo's outer clip confines painting to `exposed`.
        // Gate against the actual stem pixel range: stems emanate from the
        // chip bottom at `area.y - stem_cache_overhang_px()` (the tallest is
        // the upper-row trim stem) and run down to `area.y + area.h`. Must
        // match the stem-cache surface height and origin in
        // maybe_rebuild_stem_cache. Top-strip damage above the stems' tops
        // (popup edits, hover popup, cursor blink) would otherwise pay for an
        // empty marker pass.
        const int stem_overhang = stem_cache_overhang_px();
        const GuiRect marker_paint_rect{
            area.x,
            area.y - stem_overhang,
            area.w,
            area.h + stem_overhang
        };
        if (rects_intersect(exposed, marker_paint_rect)) {
            const auto m0 = clock::now();
            paint_marker_stems(cr, marker_paint_rect);
            const auto m1 = clock::now();
            t_markers_ms =
                std::chrono::duration<double, std::milli>(m1 - m0).count();
        }

        if (rects_intersect(exposed, top_strip)) {
            const auto f0 = clock::now();
            paint_flag_annotations(cr, top_strip, sr);
            const auto f1 = clock::now();
            t_flags_ms =
                std::chrono::duration<double, std::milli>(f1 - f0).count();
        }

        if (rects_intersect(exposed, area) ||
            rects_intersect(exposed, top_strip)) {
            const auto p0 = clock::now();
            paint_playheads(cr, area);
            const auto p1 = clock::now();
            t_playhead_ms =
                std::chrono::duration<double, std::milli>(p1 - p0).count();
        }

        if constexpr (kDebugHitRects) {
            paint_debug_hit_rects(cr, area, top_strip, sr);
        }

        // Bottom strip: two text rows of equal height mirroring
        // the top strip. The status line lives on the lower (outer) row and
        // paints UNCONDITIONALLY — it is no longer the trailing else of a
        // chain, so it stays visible while an editor is open on the upper
        // (inner) row, letting the user keep their timestamp / S-T / W-P /
        // A-B bearings while typing. The upper row carries the transient /
        // modal chain in precedence order: prompt > queue > settings editor
        // > BPM editor > hover readout. The prompt is a one-key-answer modal
        // and owns the upper row; status stays visible under it (harmless
        // context). Each row's baseline is derived from its row rect, not
        // from the window bottom.
        const GuiRect bottom_strip = timestamp_invalidate_rect(app);
        if (rects_intersect(exposed, bottom_strip)) {
            const GuiRect lower_row = bottom_lower_row_area(app);
            const GuiRect upper_row = bottom_upper_row_area(app);
            const double lower_baseline =
                lower_row.y + monospace_row_baseline_offset();
            const double upper_baseline =
                upper_row.y + monospace_row_baseline_offset();

            // --- Lower row: status line (always on). One assembled field
            //     drawn in a single pass; elements (timestamp, S/T, W/P,
            //     A/B, render-view filename, dirty *, transient message) are
            //     space-separated and paint uniformly in kText. Read-only on
            //     the active A/B tab is the literal "(read-only)" token.
            //
            //     In source-view, sr is the loaded file's sample rate and the
            //     playhead samples are source-frames. In render-view the
            //     active `audio` is the render; its sr is what the engine
            //     wrote and the playhead is render-frame coords. The same
            //     arithmetic suffices. Split-playhead: track the scanner
            //     during playback (what the user hears), the cursor otherwise
            //     (equal by invariant when the scanner is inactive).
            {
                const int64_t ts_sample = app.playhead_scanner_active
                    ? app.playhead_scanner_sample
                    : app.playhead_cursor_sample;
                double seconds = 0.0;
                if (sr > 0) {
                    seconds = static_cast<double>(ts_sample) /
                              static_cast<double>(sr);
                }
                if (seconds < 0.0) seconds = 0.0;
                if (seconds > 5999.999) seconds = 5999.999;

                std::string assembled = format_timestamp(seconds);
                if (!app.render_view.enabled) {
                    assembled += ' ';
                    assembled += (app.active_audio_view == 'T'
                                    ? 'T' : 'S');
                    assembled += ' ';
                    assembled += app.active_markers_view;
                    assembled += ' ';
                    assembled += app.active_tab_view;
                    if (active_view_state(app).read_only) {
                        assembled += ' ';
                        assembled += "(read-only)";
                    }
                } else if (app.render_view.index >= 0 &&
                           app.render_view.index <
                               static_cast<int>(
                                   app.render_view.list.size())) {
                    const auto& e =
                        app.render_view.list[app.render_view.index];
                    assembled += ' ';
                    assembled += e.batch_folder.filename().string();
                    assembled += '/';
                    assembled += e.basename;
                    assembled += ".wav";
                }
                if (app.dirty) {
                    assembled += ' ';
                    assembled += '*';
                }
                if (!app.transient_status_message.empty()) {
                    assembled += ' ';
                    assembled += app.transient_status_message;
                }

                const auto s0 = clock::now();
                text_display::draw_line(
                    cr, static_cast<double>(kTimestampPadX), lower_baseline,
                    assembled, kText, kFlagFontSize);
                const auto s1 = clock::now();
                t_ts_ms =
                    std::chrono::duration<double, std::milli>(s1 - s0).count();
            }

            // --- Upper row: transient / modal chain. ---
            if (app.prompt.active) {
                // Plain tier: the prompt text followed by its response
                // labels, each chained off the measured advance returned
                // by draw_line so no separate measurement pass is needed.
                const double label_gap = kTabLetterGapPx * 2.0;
                double cursor_x = static_cast<double>(kTimestampPadX);
                cursor_x += text_display::draw_line(
                    cr, cursor_x, upper_baseline, app.prompt.text,
                    kText, kFlagFontSize);
                cursor_x += label_gap;
                for (const auto& label : app.prompt.response_labels) {
                    cursor_x += text_display::draw_line(
                        cr, cursor_x, upper_baseline, label,
                        kText, kFlagFontSize) + label_gap;
                }
            } else if (!app.queue_progress_text.empty()) {
                text_display::draw_line(
                    cr, static_cast<double>(kTimestampPadX), upper_baseline,
                    app.queue_progress_text, kText, kFlagFontSize);
            } else if (text_editor::is_active(app.settings_editor)) {
                // Settings prompt overlay: "setting: <pending>"
                // through the shared bottom-strip editor helper. Fill is
                // kBackground normally, kAccent on parse failure (handled
                // inside the helper).
                render_bottom_strip_editor(cr, app.settings_editor,
                                           kSettingsEditorPrefix,
                                           static_cast<double>(kTimestampPadX),
                                           upper_baseline);
            } else if (text_editor::is_active(app.top_flag_editor) &&
                       app.top_flag_editor.kind ==
                           text_editor::Kind::BpmBracket) {
                // BPM editor overlay, through the same bottom-strip
                // editor helper as the settings branch above. top_flag_editor
                // with kind==BpmBracket only ever paints here, never over the
                // flag in the top strip.
                render_bottom_strip_editor(cr, app.top_flag_editor,
                                           kBpmEditorPrefix,
                                           static_cast<double>(kTimestampPadX),
                                           upper_baseline);
            } else if (app.hover_popup.visible) {
                // The floating hover popup paint was deleted but the dwell
                // mechanism kept; it now lives as the lowest-priority
                // upper-row branch. cached_text is the
                // resolved-tempo string from compute_hover_popup_text.
                text_display::draw_line(
                    cr, static_cast<double>(kTimestampPadX), upper_baseline,
                    app.hover_popup.cached_text, kText, kFlagFontSize);
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

// -- Waveform-worker dirty-detect and completion -------------------------
//
// maybe_enqueue_waveform_render: called from on_tick. Computes the desired
// waveform fingerprint (mirrors the input computation on_redraw does), and
// either dispatches a fresh job, sets the supersede slot, or no-ops.
//
// The input-computation block lives in compute_waveform_render_inputs(): it
// is the single source of truth for the desired waveform fingerprint, and
// is also consumed by force_synchronous_waveform_rebuild(). on_redraw's
// consumer derivation must stay in sync with the helper the same way it
// tracked the prior inline block.

GuiPaintHandler::WaveformRenderInputs
GuiPaintHandler::compute_waveform_render_inputs() const {
    WaveformRenderInputs in;
    if (app.loading || audio.total_frames() <= 0) return in;

    const GuiRect area = waveform_area(app);
    if (area.w <= 0 || area.h <= 0) return in;

    const double  spp      = current_samples_per_pixel(app, audio);
    const int64_t vp_start = app.viewport_start_sample;
    const int64_t vp_end   = vp_start +
        static_cast<int64_t>(std::nearbyint(spp * area.w));
    const int     sr       = audio.sample_rate();

    const bool is_target = (app.active_audio_view == 'T') &&
                           !app.render_view.enabled;
    std::vector<FrameMapSegment> target_frame_map;
    uint64_t target_frame_map_hash = 0;
    if (is_target) {
        if (app.drag.active) {
            target_frame_map = app.drag.frozen_frame_map;
        } else {
            const TargetMapCache& c =
                target_view_map_cached(app, sr,
                    static_cast<long>(audio.total_frames()));
            target_frame_map      = c.frame_map;       // job needs an owned snapshot
            target_frame_map_hash = c.hash;
        }
    }

    in.vp_start      = vp_start;
    in.vp_end        = vp_end;
    in.area_w        = area.w;
    in.area_h        = area.h;
    in.is_target     = is_target;
    in.frame_map_hash  = target_frame_map_hash;
    in.channel_count = audio.render_channels();
    in.frame_map       = std::move(target_frame_map);
    in.valid         = true;
    return in;
}

void GuiPaintHandler::maybe_enqueue_waveform_render() {
    WaveformRenderInputs in = compute_waveform_render_inputs();
    if (!in.valid) return;

    // Drag-freeze gate: during a target-view drag the frame_map-derived
    // inputs are excluded from the dirty-detect comparison, so non-drag
    // viewport changes (which would still update pending_fp_* if they
    // happened) trigger a render but pure drag-motion does not.
    const bool drag_freeze = in.is_target && app.drag.active;

    auto fingerprint_differs = [&](
        int64_t fp_vp_s, int64_t fp_vp_e,
        int     fp_aw,   int     fp_ah,
        long long fp_ag, bool    fp_t,
        uint64_t fp_h) -> bool {
        if (fp_ag != app.audio_generation) return true;
        if (fp_vp_s != in.vp_start)        return true;
        if (fp_vp_e != in.vp_end)          return true;
        if (fp_aw   != in.area_w)          return true;
        if (fp_ah   != in.area_h)          return true;
        if (fp_t    != in.is_target)       return true;
        if (!drag_freeze) {
            if (fp_h  != in.frame_map_hash)     return true;
        }
        return false;
    };

    const bool diff_vs_pending = fingerprint_differs(
        wf_cache.pending_fp_vp_start,
        wf_cache.pending_fp_vp_end,
        wf_cache.pending_fp_area_w,
        wf_cache.pending_fp_area_h,
        wf_cache.pending_fp_audio_gen,
        wf_cache.pending_fp_target,
        wf_cache.pending_fp_frame_map_hash);

    if (!diff_vs_pending) return;

    // We need to enqueue (or supersede an in-flight job). Build the job's
    // input snapshot now; the supersede slot stores the same struct shape
    // so we can hand it directly to dispatch from on_waveform_render_done.

    if (waveform_worker.is_busy()) {
        wf_cache.supersede             = true;
        wf_cache.supersede_vp_start    = in.vp_start;
        wf_cache.supersede_vp_end      = in.vp_end;
        wf_cache.supersede_area_w      = in.area_w;
        wf_cache.supersede_area_h      = in.area_h;
        wf_cache.supersede_audio_gen   = app.audio_generation;
        wf_cache.supersede_target      = in.is_target;
        wf_cache.supersede_frame_map_hash = in.frame_map_hash;
        wf_cache.supersede_frame_map     = std::move(in.frame_map);
        return;
    }

    // Idle: dispatch immediately. Reuse pending_surface if dimensions
    // match; recreate on mismatch (window resize, first allocation).
    if (!wf_cache.pending_surface ||
        wf_cache.pending_width  != in.area_w ||
        wf_cache.pending_height != in.area_h) {
        if (wf_cache.pending_surface) {
            cairo_surface_destroy(wf_cache.pending_surface);
            wf_cache.pending_surface = nullptr;
        }
        wf_cache.pending_surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, in.area_w, in.area_h);
        wf_cache.pending_width  = in.area_w;
        wf_cache.pending_height = in.area_h;
    }

    WaveformJob job;
    job.vp_start       = in.vp_start;
    job.vp_end         = in.vp_end;
    job.area_w         = in.area_w;
    job.area_h         = in.area_h;
    job.audio_gen      = app.audio_generation;
    job.target         = in.is_target;
    job.frame_map_hash   = in.frame_map_hash;
    // Stash a copy of the frame_map on the pending slot so the
    // stem cache can read it at completion-swap time. The job consumes
    // the original by move; the copy stays on the cache.
    wf_cache.pending_fp_frame_map = in.frame_map;
    job.frame_map        = std::move(in.frame_map);
    job.surface        = wf_cache.pending_surface;
    job.channel_count  = in.channel_count;
    job.audio          = &audio;

    wf_cache.pending_fp_vp_start    = in.vp_start;
    wf_cache.pending_fp_vp_end      = in.vp_end;
    wf_cache.pending_fp_area_w      = in.area_w;
    wf_cache.pending_fp_area_h      = in.area_h;
    wf_cache.pending_fp_audio_gen   = app.audio_generation;
    wf_cache.pending_fp_target      = in.is_target;
    wf_cache.pending_fp_frame_map_hash = in.frame_map_hash;

    waveform_worker.dispatch(std::move(job),
        [this](bool ok) { on_waveform_render_done(ok); });
}

void GuiPaintHandler::on_waveform_render_done(bool ok) {
    if (!ok) {
        std::fprintf(stderr,
            "warptempo_gui: waveform worker reported failure; will retry "
            "on next tick\n");
        wf_cache.supersede = false;
        wf_cache.supersede_frame_map.clear();
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
        job.area_w         = sw;
        job.area_h         = sh;
        job.audio_gen      = wf_cache.supersede_audio_gen;
        job.target         = wf_cache.supersede_target;
        job.frame_map_hash   = wf_cache.supersede_frame_map_hash;
        // Thread the supersede frame_map into both the job and
        // pending_fp_frame_map, the same way the idle-path dispatch does.
        // Copy first, then move into the job — the cache keeps a
        // displayable copy for the post-completion stem rebuild.
        wf_cache.pending_fp_frame_map = wf_cache.supersede_frame_map;
        job.frame_map        = std::move(wf_cache.supersede_frame_map);
        job.surface        = wf_cache.pending_surface;
        job.channel_count  = audio.render_channels();
        job.audio          = &audio;

        wf_cache.pending_fp_vp_start    = wf_cache.supersede_vp_start;
        wf_cache.pending_fp_vp_end      = wf_cache.supersede_vp_end;
        wf_cache.pending_fp_area_w      = sw;
        wf_cache.pending_fp_area_h      = sh;
        wf_cache.pending_fp_audio_gen   = wf_cache.supersede_audio_gen;
        wf_cache.pending_fp_target      = wf_cache.supersede_target;
        wf_cache.pending_fp_frame_map_hash = wf_cache.supersede_frame_map_hash;

        wf_cache.supersede = false;
        wf_cache.supersede_frame_map.clear();

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
    wf_cache.fp_area_w       = wf_cache.pending_fp_area_w;
    wf_cache.fp_area_h       = wf_cache.pending_fp_area_h;
    wf_cache.fp_audio_gen    = wf_cache.pending_fp_audio_gen;
    wf_cache.fp_target       = wf_cache.pending_fp_target;
    wf_cache.fp_frame_map_hash = wf_cache.pending_fp_frame_map_hash;
    // Publish the in-flight job's frame_map to the displayed slot
    // so the next maybe_rebuild_stem_cache reads the same coordinate
    // system the just-blitted waveform pixels were rendered against.
    std::swap(wf_cache.fp_frame_map,     wf_cache.pending_fp_frame_map);
    wf_cache.dirty           = false;

    // Invalidate the waveform area so the next paint blits the new
    // pixels. Matches the rect Viewport::invalidate_waveform_area uses.
    const GuiRect a = waveform_area(app);
    gui.invalidate_region(0, 0, app.width, a.y + a.h);
}

// -- Synchronous waveform rebuild (discrete marker-cycle jump) -----------
//
// Tab / Shift+Tab / Ctrl+Shift+Tab routes through here from the input
// handler. The async worker rebuilds the waveform one frame late, so the
// same-tick stem/flag rebuilds key off the lagging wf_cache.fp_* and the
// selection rectangle on the newly focused marker blinks across the
// worker window. Forcing a sync render + fp publish here makes the
// stem/flag caches converge against the final viewport this tick.
//
// Writing into wf_cache.surface directly (not pending_surface + swap) is
// safe only because wait_until_idle() ran first — the worker is Idle and
// holds no reference to the live surface. Do not reorder the drain after
// the render.

// Synchronous-repaint rule (the waveform-layer coherence invariant):
//
// The waveform plate and the marker / playhead / dim / stem / flag overlays are
// separate paint layers. The overlays are computed inline from live state and
// paint on the next frame; the plate is the expensive layer. If a one-shot
// state change updates the overlays inline but defers the plate to the async
// worker, the overlays jump to their new positions one or two frames before the
// plate catches up — a cross-layer desync that surfaced as zoom lag, the A/B-tab
// and Tab recenter jump, the source/target toggle smear, and the render-view
// enter/exit jump.
//
// The rule, realized three ways — render the correct frame before painting:
//   1. One-shot discrete viewport/view jumps render synchronously, through this
//      function. The jumps this governs: zoom, center-on-playhead, the
//      viewport-shift playhead moves (Home / End and navigate-to-marker), the
//      A/B tab switch, the source/target toggle, render-view enter / exit /
//      navigate, and undo / redo. They arrive at a bounded rate: pointer detents
//      coalesce to one action per pointer frame, and key repeat is compositor-
//      throttled, so a full inline render per event is affordable. The pyramid
//      bounds per-column cost, so the render is O(area_width) at any zoom level.
//   2. The one sustained pointer gesture (pan / scroll) uses the incremental
//      shift-and-strip path (pan_waveform_incremental) — also synchronous in
//      frame, just a partial render. The built-in touchpad emits a high-rate
//      continuous stream a full-render-per-event model cannot keep up with, so
//      pan must NOT be converted to a full sync render. The over-a-window
//      fast-flick fallback in pan_waveform_incremental already drops to this
//      full sync rebuild.
//   3. The async worker (maybe_enqueue_waveform_render) is the backstop for
//      changes the user is not actively driving: resize, file load / reload,
//      target-view marker drags (frozen during the drag, re-rendered on the
//      worker at release), follow_scroll_if_needed during playback, and the
//      on_tick safety net that catches residual fingerprint drift.
//
// This is NOT "make everything synchronous." Async earns its keep for the
// touchpad torrent and for undriven / playback-adjacent changes; the rule is
// only that a one-shot jump must not paint its overlays against a stale plate.
//
// Parts 2 and 3 of this arc wire the remaining call sites above; this commit
// realizes zoom and the mechanism.
void GuiPaintHandler::force_synchronous_waveform_rebuild() {
    const WaveformRenderInputs in = compute_waveform_render_inputs();
    if (!in.valid) return;

    // Drain any in-flight worker job so we own the cache surfaces.
    // Cancels a Running job and synchronously consumes a
    // CompletionPending one (same primitive file_loader uses before
    // swapping audio). After this the worker is Idle and will not touch
    // wf_cache surfaces underneath us.
    waveform_worker.wait_until_idle();

    // Clear any stale supersede request: wait_until_idle may have
    // cancelled a job whose supersede slot was set; we are about to
    // publish the current viewport ourselves, so the slot must not
    // re-dispatch an old one on a later tick.
    wf_cache.supersede = false;
    wf_cache.supersede_frame_map.clear();

    // Render into the LIVE surface directly. Reuse-or-recreate on
    // dimension mismatch, mirroring the dispatch path.
    if (!wf_cache.surface ||
        wf_cache.width  != in.area_w ||
        wf_cache.height != in.area_h) {
        if (wf_cache.surface) {
            cairo_surface_destroy(wf_cache.surface);
            wf_cache.surface = nullptr;
        }
        wf_cache.surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, in.area_w, in.area_h);
        wf_cache.width  = in.area_w;
        wf_cache.height = in.area_h;
    }

    render_waveform_to_cache_surface(
        wf_cache.surface,
        in.area_w, in.area_h,
        in.channel_count,
        audio,
        in.vp_start, in.vp_end,
        in.frame_map.empty() ? nullptr : &in.frame_map);

    // Publish the displayed fingerprint NOW so this same tick's
    // maybe_rebuild_stem_cache / maybe_rebuild_flag_cache read the
    // current viewport. Keep pending_fp_* in lockstep so the next
    // maybe_enqueue_waveform_render sees no diff and does not
    // re-dispatch the same target on the worker.
    wf_cache.fp_vp_start     = in.vp_start;
    wf_cache.fp_vp_end       = in.vp_end;
    wf_cache.fp_area_w       = in.area_w;
    wf_cache.fp_area_h       = in.area_h;
    wf_cache.fp_audio_gen    = app.audio_generation;
    wf_cache.fp_target       = in.is_target;
    wf_cache.fp_frame_map_hash = in.frame_map_hash;
    wf_cache.fp_frame_map      = in.frame_map;

    wf_cache.pending_fp_vp_start     = in.vp_start;
    wf_cache.pending_fp_vp_end       = in.vp_end;
    wf_cache.pending_fp_area_w       = in.area_w;
    wf_cache.pending_fp_area_h       = in.area_h;
    wf_cache.pending_fp_audio_gen    = app.audio_generation;
    wf_cache.pending_fp_target       = in.is_target;
    wf_cache.pending_fp_frame_map_hash = in.frame_map_hash;
    wf_cache.pending_fp_frame_map      = in.frame_map;

    wf_cache.dirty = false;

    const GuiRect a = waveform_area(app);
    gui.invalidate_region(0, 0, app.width, a.y + a.h);
}

// -- Incremental pan (shift-and-strip) -----------------------------------
//
// Pan fast-path: instead of re-rendering the whole window on the worker for a
// pure horizontal pan, shift the already-rendered plate by the pixel delta and
// render only the thin newly-exposed edge strip inline. O(strip) per frame
// instead of O(window), so the pipeline keeps pace with fast touchpad scroll.
//
// Routed here from Viewport::scroll_viewport (a pure pan — spp and view are
// unchanged) via the request_waveform_pan_ callback. new_vp_start is the
// post-clamp app.viewport_start_sample in the displayed domain (source frames
// in source view, target frames in target view).
//
// Target view uses this path too: a pan is a translation in the DISPLAYED
// (target) domain, the plate is uniformly indexed in that domain
// (render_waveform maps column i -> vp_start + spp*i, then target->source via
// the frame_map), and the frame_map is invariant across a pan (marker/scale edits
// rebuild it and stay on the worker path, caught by the fp_frame_map_hash gate
// below). So a uniform pixel shift is exactly as correct in target view as in
// source view.
//
// This is an optimization layered over the worker backstop: every exit that is
// not a clean shift falls back to the worker (maybe_enqueue) or a synchronous
// full render, and the on_tick dirty-check re-renders if the fingerprint ever
// drifts.
void GuiPaintHandler::pan_waveform_incremental(int64_t new_vp_start) {
    const WaveformRenderInputs in = compute_waveform_render_inputs();
    if (!in.valid) { maybe_enqueue_waveform_render(); return; }

    // Fallbacks (section 5): anything that is not a clean translate of the
    // live plate goes to the worker / full-render path.
    //  - no plate yet (just after load)
    //  - worker mid-render: leave it to the worker; superseding keeps the
    //    latest viewport without racing a swap against our in-place shift
    //  - active drag: the frame_map is frozen / mid-deformation
    //  - dimension mismatch (resize since the plate was rendered)
    //  - view / frame_map mismatch: not a pure pan (e.g. 't' toggle, marker edit)
    if (!wf_cache.surface ||
        waveform_worker.is_busy() ||
        app.drag.active ||
        wf_cache.fp_area_w != in.area_w ||
        wf_cache.fp_area_h != in.area_h ||
        wf_cache.width     != in.area_w ||
        wf_cache.height    != in.area_h ||
        wf_cache.fp_target       != in.is_target ||
        wf_cache.fp_frame_map_hash != in.frame_map_hash ||
        wf_cache.fp_audio_gen    != app.audio_generation) {
        maybe_enqueue_waveform_render();
        return;
    }

    const int64_t old_vp_start = wf_cache.fp_vp_start;
    const int64_t old_vp_end   = wf_cache.fp_vp_end;
    const int     plate_w      = wf_cache.fp_area_w;
    const double  disp_spp =
        static_cast<double>(old_vp_end - old_vp_start) / plate_w;
    if (disp_spp <= 0.0) { maybe_enqueue_waveform_render(); return; }

    const int delta_px = static_cast<int>(
        std::nearbyint(static_cast<double>(new_vp_start - old_vp_start) /
                       disp_spp));

    // Sub-pixel move: nothing to redraw, just advance the plate bookkeeping so
    // the dim/cursor/markers track the new viewport and the dirty-check no-ops.
    if (delta_px == 0) {
        wf_cache.fp_vp_start         = in.vp_start;
        wf_cache.fp_vp_end           = in.vp_end;
        wf_cache.pending_fp_vp_start = in.vp_start;
        wf_cache.pending_fp_vp_end   = in.vp_end;
        // Plate bookkeeping advanced to the new viewport; bring the overlay
        // caches with it so stems / flags / dim do not lag the plate.
        maybe_rebuild_stem_cache();
        maybe_rebuild_flag_cache();
        const GuiRect a = waveform_area(app);
        gui.invalidate_region(0, 0, app.width, a.y + a.h);
        return;
    }

    // Over-a-full-window pan: nothing to reuse. Synchronous full render
    // guarantees a correct frame (the rare fast-flick case), and keeps the
    // inline strip work strictly bounded to at most a window width.
    if (delta_px >= plate_w || delta_px <= -plate_w) {
        force_synchronous_waveform_rebuild();
        // force_synchronous_waveform_rebuild publishes the fingerprint but
        // leaves the stem / flag rebuild to a later tick; do it now so the
        // fast-flick frame is fully consistent rather than relying on an
        // on_tick that may not run before the next paint.
        maybe_rebuild_stem_cache();
        maybe_rebuild_flag_cache();
        return;
    }

    // Shift the plate in place. Content moves opposite the viewport: panning
    // toward later audio (delta_px > 0) slides pixels left and exposes the
    // right edge; panning toward earlier audio exposes the left edge.
    cairo_surface_flush(wf_cache.surface);
    unsigned char* data = cairo_image_surface_get_data(wf_cache.surface);
    const int stride     = cairo_image_surface_get_stride(wf_cache.surface);
    const int surf_h     = cairo_image_surface_get_height(wf_cache.surface);
    if (!data) { maybe_enqueue_waveform_render(); return; }

    int strip_x = 0;
    int strip_w = 0;
    if (delta_px > 0) {
        const int shift = delta_px;
        const size_t move_bytes =
            static_cast<size_t>(plate_w - shift) * 4;
        for (int row = 0; row < surf_h; ++row) {
            unsigned char* p = data + static_cast<size_t>(row) * stride;
            std::memmove(p, p + static_cast<size_t>(shift) * 4, move_bytes);
        }
        strip_x = plate_w - shift;
        strip_w = shift;
    } else {
        const int shift = -delta_px;
        const size_t move_bytes =
            static_cast<size_t>(plate_w - shift) * 4;
        for (int row = 0; row < surf_h; ++row) {
            unsigned char* p = data + static_cast<size_t>(row) * stride;
            std::memmove(p + static_cast<size_t>(shift) * 4, p, move_bytes);
        }
        strip_x = 0;
        strip_w = shift;
    }
    cairo_surface_mark_dirty(wf_cache.surface);

    // Render the newly exposed edge strip at the new viewport. in.vp_end is
    // the full plate's displayed end (== new_vp_start + the preserved span,
    // since spp and area_w are unchanged), so the strip columns map to the
    // identical frames a full render would produce.
    render_waveform_strip_to_cache_surface(
        wf_cache.surface,
        in.area_w, in.area_h,
        strip_x, strip_w,
        in.channel_count,
        audio,
        in.vp_start, in.vp_end,
        in.frame_map.empty() ? nullptr : &in.frame_map);

    // Advance the plate's viewport bookkeeping. fp_vp_start / disp_spp key the
    // live dim composite, markers, flags, and the cursor; pending_fp_* mirrors
    // it so the on_tick dirty-check sees the fingerprint already satisfied and
    // does not redundantly re-render the whole window. Everything else
    // (area, target, frame_map, audio_gen) is unchanged by a pure pan and was
    // verified equal to in.* by the fallback gate above.
    wf_cache.fp_vp_start         = in.vp_start;
    wf_cache.fp_vp_end           = in.vp_end;
    wf_cache.pending_fp_vp_start = in.vp_start;
    wf_cache.pending_fp_vp_end   = in.vp_end;

    // The plate advanced synchronously in this event. Rebuild the stem and
    // flag caches now, against the just-published fingerprint, so the overlay
    // layers (stems, flags, and the dim they paint under markers) move in
    // lockstep with the plate. Without this they lag until the next on_tick
    // dirty-check, and a continuous drag shows the markers and their dim
    // trailing the waveform by a step. Both rebuilds are fingerprint-guarded
    // and cheap; this mirrors force_synchronous_waveform_rebuild, which
    // likewise publishes the fingerprint so the same-tick stem/flag rebuild
    // reads the new viewport.
    maybe_rebuild_stem_cache();
    maybe_rebuild_flag_cache();

    const GuiRect a = waveform_area(app);
    gui.invalidate_region(0, 0, app.width, a.y + a.h);
}

// -- Marker stem cache dirty-detect and rebuild --------------------------
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
// into the FlagCache fingerprint to avoid distributing a
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

GuiPaintHandler::DisplayedTrim
GuiPaintHandler::compute_displayed_trim() const {
    DisplayedTrim out;
    const bool rve = app.render_view.enabled;

    // has-set + selected bits come live from the project trim; render view
    // forces them off (the render waveform has no trim).
    out.has_begin      = !rve && app.trim.has_begin;
    out.has_end        = !rve && app.trim.has_end;
    out.begin_selected = out.has_begin && app.trim_begin_selected;
    out.end_selected   = out.has_end   && app.trim_end_selected;

    // Positions read LIVE from app state (no waveform-cache coupling): trim
    // no longer affects waveform pixels, so they must follow the cursor every
    // motion tick rather than lagging a worker-completion swap. Target-view
    // positions map through the displayed frame_map (wf_cache.fp_frame_map) — the
    // same coordinate system the marker stems use — which trim does not
    // perturb, so it is stable across a trim drag.
    const int sr = audio.sample_rate();
    std::pair<long long, long long> t;
    if (rve) {
        t = {0, audio.total_frames()};
    } else if (wf_cache.fp_target) {
        const auto src_trim = compute_trim_samples(
            app, sr, audio.total_frames());
        if (!wf_cache.fp_frame_map.empty()) {
            const long long t0 = static_cast<long long>(std::nearbyint(
                map_source_to_target(
                    static_cast<size_t>(src_trim.first),
                    wf_cache.fp_frame_map)));
            const long long t1 = static_cast<long long>(std::nearbyint(
                map_source_to_target(
                    static_cast<size_t>(src_trim.second),
                    wf_cache.fp_frame_map)));
            t = {t0, t1};
        } else {
            t = src_trim;
        }
    } else {
        t = compute_trim_samples(app, sr, audio.total_frames());
    }
    out.begin = t.first;
    out.end   = t.second;
    return out;
}

GuiPaintHandler::OutOfTrimRects
GuiPaintHandler::compute_out_of_trim_rects(const GuiRect& area) const {
    OutOfTrimRects out;
    if (area.w <= 0) return out;

    // Frames in the same paint domain the trim stems use (render view forces
    // has_begin/has_end off, so the early-out below covers it). begin/end are
    // already mapped through the displayed frame_map in target view.
    const DisplayedTrim dtrim = compute_displayed_trim();
    if (!dtrim.has_begin && !dtrim.has_end) return out;

    // LIVE viewport + live samples-per-pixel (NOT wf_cache.fp_*): during a
    // trim drag the viewport is static so this equals the displayed viewport
    // and the dim edge sits on the stem; keeping it live means the dim never
    // waits on the plate's async rebuild to recolor.
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return out;
    const int64_t vp_start = app.viewport_start_sample;
    const int x_lo = area.x;
    const int x_hi = area.x + area.w;

    auto frame_to_x = [&](int64_t frame) -> int {
        double x = area.x +
            std::nearbyint((static_cast<double>(frame) - vp_start) / spp);
        if (x < x_lo) x = x_lo;
        if (x > x_hi) x = x_hi;
        return static_cast<int>(x);
    };

    if (dtrim.has_begin) {
        const int x_begin = frame_to_x(dtrim.begin);
        if (x_begin > x_lo) {
            out.has_left = true;
            out.left = GuiRect{x_lo, area.y, x_begin - x_lo, area.h};
        }
    }
    if (dtrim.has_end) {
        const int x_end = frame_to_x(dtrim.end);
        if (x_end < x_hi) {
            out.has_right = true;
            out.right = GuiRect{x_end, area.y, x_hi - x_end, area.h};
        }
    }
    return out;
}

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
    // geometry note in StemCache's class comment. The overhang
    // is the TALLER trim value (stem_cache_overhang_px = kStemAboveWaveformPx
    // + row_h + gap) so the upper-row trim stem is not clipped at its top;
    // marker stems land transparently lower in the same surface.
    const int overhang = stem_cache_overhang_px();
    const int surface_w = area.w;
    const int surface_h = area.h + overhang;

    // Displayed-viewport inputs: read from wf_cache.fp_*, not app state.
    const int64_t  vp_start     = wf_cache.fp_vp_start;
    const int64_t  vp_end       = wf_cache.fp_vp_end;
    const bool     is_target    = wf_cache.fp_target;
    const uint64_t frame_map_hash = wf_cache.fp_frame_map_hash;
    const long long audio_gen   = wf_cache.fp_audio_gen;

    // Marker-driven inputs: read live from app state.
    const long long warp_gen   = app.warpmarkers.generation();
    const long long phase_gen  = app.phase_reset_markers.generation();
    const uint64_t  drag_hash  = hash_drag_overlay(app.drag);
    const bool     drag_active = app.drag.active;
    const char     mv          = app.active_markers_view;
    const bool     rve         = app.render_view.enabled;
    const uint64_t sel_hash    = hash_selection(app.selected_markers,
                                                app.last_selected_marker);

    // Trim boundary stems. Positions ride trim_begin / trim_end
    // (displayed domain), has-set + selected bits from the active A/B tab.
    // Computed by the shared helper so the flag cache's b/e chips read the
    // exact same values (chip + stem are one unit).
    const DisplayedTrim dtrim   = compute_displayed_trim();
    const bool trim_has_begin   = dtrim.has_begin;
    const bool trim_has_end     = dtrim.has_end;
    const bool trim_begin_sel   = dtrim.begin_selected;
    const bool trim_end_sel     = dtrim.end_selected;
    const int64_t trim_begin    = dtrim.begin;
    const int64_t trim_end      = dtrim.end;

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
        stem_cache.fp_frame_map_hash            == frame_map_hash &&
        stem_cache.fp_warpmarker_generation   == warp_gen &&
        stem_cache.fp_phase_reset_generation  == phase_gen &&
        stem_cache.fp_drag_overlay_hash       == drag_hash &&
        stem_cache.fp_drag_active             == drag_active &&
        stem_cache.fp_active_markers_view     == mv &&
        stem_cache.fp_render_view_enabled     == rve &&
        stem_cache.fp_selection_hash          == sel_hash &&
        stem_cache.fp_trim_has_begin          == trim_has_begin &&
        stem_cache.fp_trim_has_end            == trim_has_end &&
        stem_cache.fp_trim_begin_selected     == trim_begin_sel &&
        stem_cache.fp_trim_end_selected       == trim_end_sel;

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

    // Local rect translates the screen-coord stem geometry into the cache
    // surface's coordinate system. Setting local.y = overhang puts the
    // waveform top at that offset, so the TALLEST stem (the upper-row trim
    // stem, top = local.y - kFlagBottomLiftPx - (row_h + gap)) lands at
    // surface y = 0, marker stems (top = local.y - kFlagBottomLiftPx) land
    // transparently lower, and y1 = overhang + area.h = surface_h. The blit
    // at on_redraw time positions the surface at screen y = area.y - overhang
    // so everything lands correctly.
    const GuiRect local_area{
        0,
        overhang,
        surface_w,
        area.h
    };
    const TrimRange trim_struct{trim_begin, trim_end};
    const int sr = audio.sample_rate();

    // Target-view stems consume the displayed frame_map (the one baked
    // into the live waveform pixels), not a freshly-built one — keeps
    // stem positions consistent with the displayed waveform during the
    // worker's rebuild window.
    const std::vector<FrameMapSegment>* tmap_arg =
        (is_target && !wf_cache.fp_frame_map.empty())
            ? &wf_cache.fp_frame_map : nullptr;

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

    // Trim boundary stems, painted in both 'W' and 'P' views.
    // Positions are the displayed-domain trim frames (already translated);
    // the has-set / selected bits decide which stems draw and in what
    // color.
    // Painted BEFORE the regular marker stems so that
    // where a trim bound and a regular marker share a column the regular
    // stem (painted last on this shared surface) sits in front; the taller
    // trim stem reads as "underneath," reachable by its hotkey.
    render_trim_stems(
        ccr, local_area, vp_start, vp_end,
        trim_struct,
        trim_has_begin, trim_begin_sel,
        trim_has_end, trim_end_sel,
        wf_cache.surface);

    if (mv == 'P') {
        const auto& list = rve
            ? app.render_view.phase_resets
            : app.phase_reset_markers.markers();
        render_phase_reset_markers(
            ccr, local_area, list,
            vp_start, vp_end, sr,
            app.selected_markers, tmap_arg, drag_overlay,
            wf_cache.surface);
    } else {
        const auto& list = rve
            ? app.render_view.markers
            : app.warpmarkers.markers();
        render_markers(
            ccr, local_area, list,
            vp_start, vp_end, sr,
            app.selected_markers, tmap_arg, drag_overlay,
            wf_cache.surface);
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
    stem_cache.fp_frame_map_hash              = frame_map_hash;
    stem_cache.fp_warpmarker_generation     = warp_gen;
    stem_cache.fp_phase_reset_generation    = phase_gen;
    stem_cache.fp_drag_overlay_hash         = drag_hash;
    stem_cache.fp_drag_active               = drag_active;
    stem_cache.fp_active_markers_view       = mv;
    stem_cache.fp_render_view_enabled       = rve;
    stem_cache.fp_selection_hash            = sel_hash;
    stem_cache.fp_trim_has_begin            = trim_has_begin;
    stem_cache.fp_trim_has_end              = trim_has_end;
    stem_cache.fp_trim_begin_selected       = trim_begin_sel;
    stem_cache.fp_trim_end_selected         = trim_end_sel;
    stem_cache.dirty                        = false;

    // Invalidate the stem region. Viewport-driven invalidations
    // already cover this strip, but pure marker-store edits (warp_gen /
    // phase_gen bumps) don't pass through the viewport's invalidator —
    // damage the strip explicitly so the next paint blits the new
    // pixels. Idempotent against the waveform's own damage.
    gui.invalidate_region(
        0,
        area.y - overhang,
        app.width,
        surface_h);
}

// -- Flag-rect cache dirty-detect and rebuild ----------------------------
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

    // Displayed-viewport inputs from wf_cache.fp_*. Warp/phase flags are
    // positioned at marker times only. The b/e trim chips also ride this
    // strip, so the displayed-domain trim positions + has/selected bits (from
    // the shared helper, identical to the stem cache's) are now part of the
    // flag cache's identity.
    const int64_t  vp_start     = wf_cache.fp_vp_start;
    const int64_t  vp_end       = wf_cache.fp_vp_end;
    const bool     is_target    = wf_cache.fp_target;
    const uint64_t frame_map_hash = wf_cache.fp_frame_map_hash;
    const long long audio_gen   = wf_cache.fp_audio_gen;

    // Marker-driven inputs from app state.
    const long long warp_gen   = app.warpmarkers.generation();
    const long long phase_gen  = app.phase_reset_markers.generation();
    const uint64_t  drag_hash  = hash_drag_overlay(app.drag);
    const uint64_t  sel_hash   = hash_selection(
                                     app.selected_markers,
                                     app.last_selected_marker);
    const char      mv         = app.active_markers_view;
    const bool      rve        = app.render_view.enabled;
    // Iteration mode only affects warp-view (non-render) flags;
    // render view resets the toggle off, so this is false there.
    const bool      iter_on    = app.iteration_mode_enabled &&
                                 mv == 'W' && !rve;

    // FlagPayload editor target drives the skip-guard (cache leaves a
    // hole for the live editor render to fill). The iter/BPM
    // popup-editor outline-suppression channel was removed along with
    // the popup surfaces; the IterationBracket / BpmBracket kinds no
    // longer feed the cache fingerprint.
    // FlagPayload (W view) drives the skip-guard: the cache leaves a hole for
    // the live editor render to fill. P view has no per-flag editor.
    int flag_target = -1;
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::FlagPayload) {
        flag_target = app.top_flag_editor.target;
    }

    // Displayed-domain trim state for the b/e chips (shared helper,
    // same values the stem cache paints its stems at).
    const DisplayedTrim dtrim = compute_displayed_trim();

    const bool matches =
        flag_cache.surface &&
        flag_cache.fp_audio_gen               == audio_gen &&
        flag_cache.fp_vp_start                == vp_start &&
        flag_cache.fp_vp_end                  == vp_end &&
        flag_cache.fp_area_w                  == surface_w &&
        flag_cache.fp_area_h                  == surface_h &&
        flag_cache.fp_target                  == is_target &&
        flag_cache.fp_frame_map_hash            == frame_map_hash &&
        flag_cache.fp_warpmarker_generation   == warp_gen &&
        flag_cache.fp_phase_reset_generation  == phase_gen &&
        flag_cache.fp_drag_overlay_hash       == drag_hash &&
        flag_cache.fp_selection_hash          == sel_hash &&
        flag_cache.fp_active_markers_view     == mv &&
        flag_cache.fp_render_view_enabled     == rve &&
        flag_cache.fp_flag_editor_target      == flag_target &&
        flag_cache.fp_iteration_mode_enabled  == iter_on &&
        flag_cache.fp_trim_begin              == dtrim.begin &&
        flag_cache.fp_trim_end                == dtrim.end &&
        flag_cache.fp_trim_has_begin          == dtrim.has_begin &&
        flag_cache.fp_trim_has_end            == dtrim.has_end &&
        flag_cache.fp_trim_begin_selected     == dtrim.begin_selected &&
        flag_cache.fp_trim_end_selected       == dtrim.end_selected;

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
    const int sr = audio.sample_rate();

    const std::vector<FrameMapSegment>* tmap_arg =
        (is_target && !wf_cache.fp_frame_map.empty())
            ? &wf_cache.fp_frame_map : nullptr;

    DragOverlay drag_overlay_storage;
    const DragOverlay* drag_overlay = nullptr;
    if (app.drag.active) {
        drag_overlay_storage.indices = &app.drag.dragging_markers;
        drag_overlay_storage.times   = &app.drag.moveable_times;
        drag_overlay = &drag_overlay_storage;
    }

    // Cache overlay: marker_index activates the skip-guard for the
    // FlagPayload editor target. Other fields stay defaulted — pending
    // text, cursor state, selection range live in the live editor
    // render only.
    FlagEditorOverlay cache_overlay;
    cache_overlay.marker_index        = flag_target;

    if (rve) {
        if (mv == 'P') {
            render_phase_reset_flags(
                ccr, local_top_strip,
                app.render_view.phase_resets,
                vp_start, vp_end, sr,
                kFlagFontSize,
                app.selected_markers,
                nullptr,
                drag_overlay);
        } else {
            render_flags(ccr, local_top_strip,
                         app.render_view.markers,
                         vp_start, vp_end, sr,
                         kFlagFontSize,
                         app.selected_markers,
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
            tmap_arg,
            drag_overlay);
    } else {
        render_flags(ccr, local_top_strip,
                     app.warpmarkers.markers(),
                     vp_start, vp_end, sr,
                     kFlagFontSize,
                     app.selected_markers,
                     cache_overlay,
                     tmap_arg,
                     drag_overlay,
                     iter_on);
    }

    // The b/e trim chips cap their stems in the upper top row. Painted
    // in both 'W' and 'P' views (like the stems); the dtrim has-bits force
    // them off in render view, so render_trim_flags early-returns there. The
    // real waveform_area sets the upper-row chip bottom; the top strip's
    // screen origin equals the cache surface origin (0,0), so local_top_strip
    // and the real waveform rect need no translation.
    render_trim_flags(
        ccr, local_top_strip, waveform_area(app),
        vp_start, vp_end, kFlagFontSize,
        TrimRange{dtrim.begin, dtrim.end},
        dtrim.has_begin, dtrim.begin_selected,
        dtrim.has_end, dtrim.end_selected);

    cairo_destroy(ccr);

    flag_cache.fp_audio_gen               = audio_gen;
    flag_cache.fp_vp_start                = vp_start;
    flag_cache.fp_vp_end                  = vp_end;
    flag_cache.fp_area_w                  = surface_w;
    flag_cache.fp_area_h                  = surface_h;
    flag_cache.fp_target                  = is_target;
    flag_cache.fp_frame_map_hash            = frame_map_hash;
    flag_cache.fp_warpmarker_generation   = warp_gen;
    flag_cache.fp_phase_reset_generation  = phase_gen;
    flag_cache.fp_drag_overlay_hash       = drag_hash;
    flag_cache.fp_selection_hash          = sel_hash;
    flag_cache.fp_active_markers_view     = mv;
    flag_cache.fp_render_view_enabled     = rve;
    flag_cache.fp_flag_editor_target      = flag_target;
    flag_cache.fp_iteration_mode_enabled  = iter_on;
    flag_cache.fp_trim_begin              = dtrim.begin;
    flag_cache.fp_trim_end                = dtrim.end;
    flag_cache.fp_trim_has_begin          = dtrim.has_begin;
    flag_cache.fp_trim_has_end            = dtrim.has_end;
    flag_cache.fp_trim_begin_selected     = dtrim.begin_selected;
    flag_cache.fp_trim_end_selected       = dtrim.end_selected;
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
    // live_total_frames returns the frame_map-derived deformed total in
    // target view so the cap is consistent with the deformed timeline's
    // length.
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
