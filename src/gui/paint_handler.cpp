#include "paint_handler.h"

#include "render.h"
#include "text_display.h"
#include "text_editor.h"
#include "time_format.h"
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
    const std::vector<TimeMapSegment>* timemap_or_null) {
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
                        timemap_or_null);
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
                        timemap_or_null);
        render_waveform(ccr, ch1, audio, 1,
                        vp_start, vp_end,
                        kWaveform,
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
        // strip as a blit-then-overlay path.

        // Drag-time position overlay. Active for the duration of a
        // ctrl-drag; non-null only when app.drag.active. Threaded into
        // render_one_editor_flag so the editor flag tracks the dragged
        // marker's proposed (moveable_times) position.
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
        // FlagPayload editor's pending text + cursor. Brief B2: the
        // hover / iter / BPM popup paint paths and their dispatch are
        // deleted; iter and BPM modes are presentation-dark until D / E
        // re-home their surfaces.
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
            const std::vector<TimeMapSegment>* tmap_disp =
                (wf_cache.fp_target && !wf_cache.fp_timemap.empty())
                    ? &wf_cache.fp_timemap : nullptr;

            // Built once, threaded into the live render_one_editor_flag
            // call below. Reads only app.top_flag_editor, which has no
            // view-domain distinction.
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

            // Stage C: the flag-rect pass has moved into the cache
            // rebuild above. What's left here is live work — the
            // FlagPayload editor's pending text + cursor (which would
            // otherwise drag the cache fingerprint on every keystroke
            // and blink flip). Brief B2: the hover / iter / BPM popup
            // surfaces are deleted; iteration and BPM modes are
            // presentation-dark until D / E re-home their entries, and
            // the hover dwell mechanism still runs but has no on-screen
            // reader until Brief F.
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
                    overlay,
                    tmap_disp,
                    drag_overlay,
                    app.iteration_mode_enabled &&
                        app.active_markers_view == 'W');
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

        // Bottom strip (Brief F): two text rows of equal height mirroring
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
                if (!app.render_view_enabled) {
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
                } else if (app.render_view_index >= 0 &&
                           app.render_view_index <
                               static_cast<int>(
                                   app.render_view_list.size())) {
                    const auto& e =
                        app.render_view_list[app.render_view_index];
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
                // Settings prompt overlay (Brief B.2): "setting: <pending>"
                // through the shared editor text-box primitive. The fill is
                // kBackground normally, kAccent on parse failure. The
                // "setting: " prefix sits to the left of the box.
                cairo_save(cr);
                cairo_select_font_face(cr, "monospace",
                                       CAIRO_FONT_SLANT_NORMAL,
                                       CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size(cr, kFlagFontSize);

                EditorTextBox box;
                box.anchor_x        = static_cast<double>(kTimestampPadX);
                box.baseline_y      = upper_baseline;
                box.prefix          = "setting: ";
                box.text            = app.settings_editor.pending;
                box.hl_pad          = kFlagPadXPx;
                box.fill            = app.settings_editor.red
                                          ? kAccent : kBackground;
                box.text_color      = kText;
                box.has_selection   =
                    text_editor::has_selection(app.settings_editor);
                box.selection_start =
                    text_editor::selection_start(app.settings_editor);
                box.selection_end   =
                    text_editor::selection_end(app.settings_editor);
                box.cursor_visible  =
                    text_editor::cursor_visible_now(app.settings_editor);
                box.cursor_pos      = app.settings_editor.cursor_pos;
                render_editor_text_box(cr, box);

                cairo_restore(cr);
            } else if (text_editor::is_active(app.top_flag_editor) &&
                       app.top_flag_editor.kind ==
                           text_editor::Kind::BpmBracket) {
                // Brief E: BPM editor overlay. Same bottom-strip primitive
                // and shape as the settings-editor branch above (F2 folds
                // both into one row helper), differing only in the "bpm: "
                // prefix and the editor it reads. top_flag_editor with
                // kind==BpmBracket only ever paints here, never over the flag
                // in the top strip. Fill is kBackground normally, kAccent on
                // parse failure.
                cairo_save(cr);
                cairo_select_font_face(cr, "monospace",
                                       CAIRO_FONT_SLANT_NORMAL,
                                       CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size(cr, kFlagFontSize);

                EditorTextBox box;
                box.anchor_x        = static_cast<double>(kTimestampPadX);
                box.baseline_y      = upper_baseline;
                box.prefix          = "bpm: ";
                box.text            = app.top_flag_editor.pending;
                box.hl_pad          = kFlagPadXPx;
                box.fill            = app.top_flag_editor.red
                                          ? kAccent : kBackground;
                box.text_color      = kText;
                box.has_selection   =
                    text_editor::has_selection(app.top_flag_editor);
                box.selection_start =
                    text_editor::selection_start(app.top_flag_editor);
                box.selection_end   =
                    text_editor::selection_end(app.top_flag_editor);
                box.cursor_visible  =
                    text_editor::cursor_visible_now(app.top_flag_editor);
                box.cursor_pos      = app.top_flag_editor.cursor_pos;
                render_editor_text_box(cr, box);

                cairo_restore(cr);
            } else if (app.hover_popup.visible) {
                // B2 deleted the floating hover popup paint but kept the
                // dwell mechanism; Brief F gives it a home as the
                // lowest-priority upper-row branch. cached_text is the
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

// -- Stage A: waveform-worker dirty-detect and completion ----------------
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

    in.vp_start      = vp_start;
    in.vp_end        = vp_end;
    in.area_w        = area.w;
    in.area_h        = area.h;
    in.is_target     = is_target;
    in.timemap_hash  = target_timemap_hash;
    in.channel_count = audio.render_channels();
    in.timemap       = std::move(target_timemap);
    in.valid         = true;
    return in;
}

void GuiPaintHandler::maybe_enqueue_waveform_render() {
    WaveformRenderInputs in = compute_waveform_render_inputs();
    if (!in.valid) return;

    // Drag-freeze gate: during a target-view drag the timemap-derived
    // inputs are excluded from the dirty-detect comparison, so non-drag
    // viewport changes (which would still update pending_fp_* if they
    // happened) trigger a render but pure drag-motion does not. See the
    // original brief 3b comment in on_redraw.
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
            if (fp_h  != in.timemap_hash)     return true;
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
        wf_cache.pending_fp_timemap_hash);

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
        wf_cache.supersede_timemap_hash = in.timemap_hash;
        wf_cache.supersede_timemap     = std::move(in.timemap);
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
    job.timemap_hash   = in.timemap_hash;
    // Stage B: stash a copy of the timemap on the pending slot so the
    // stem cache can read it at completion-swap time. The job consumes
    // the original by move; the copy stays on the cache.
    wf_cache.pending_fp_timemap = in.timemap;
    job.timemap        = std::move(in.timemap);
    job.surface        = wf_cache.pending_surface;
    job.channel_count  = in.channel_count;
    job.audio          = &audio;

    wf_cache.pending_fp_vp_start    = in.vp_start;
    wf_cache.pending_fp_vp_end      = in.vp_end;
    wf_cache.pending_fp_area_w      = in.area_w;
    wf_cache.pending_fp_area_h      = in.area_h;
    wf_cache.pending_fp_audio_gen   = app.audio_generation;
    wf_cache.pending_fp_target      = in.is_target;
    wf_cache.pending_fp_timemap_hash = in.timemap_hash;

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
    wf_cache.supersede_timemap.clear();

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
        in.timemap.empty() ? nullptr : &in.timemap);

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
    wf_cache.fp_timemap_hash = in.timemap_hash;
    wf_cache.fp_timemap      = in.timemap;

    wf_cache.pending_fp_vp_start     = in.vp_start;
    wf_cache.pending_fp_vp_end       = in.vp_end;
    wf_cache.pending_fp_area_w       = in.area_w;
    wf_cache.pending_fp_area_h       = in.area_h;
    wf_cache.pending_fp_audio_gen    = app.audio_generation;
    wf_cache.pending_fp_target       = in.is_target;
    wf_cache.pending_fp_timemap_hash = in.timemap_hash;
    wf_cache.pending_fp_timemap      = in.timemap;

    wf_cache.dirty = false;

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

GuiPaintHandler::DisplayedTrim
GuiPaintHandler::compute_displayed_trim() const {
    DisplayedTrim out;
    const bool rve = app.render_view_enabled;

    // has-set + selected bits come live from the active A/B tab; render view
    // forces them off (the render waveform has no trim).
    const ViewState& tvs = active_view_state(app);
    out.has_begin      = !rve && tvs.has_trim_begin;
    out.has_end        = !rve && tvs.has_trim_end;
    out.begin_selected = out.has_begin && tvs.trim_begin_selected;
    out.end_selected   = out.has_end   && tvs.trim_end_selected;

    // Positions read LIVE from app state (no waveform-cache coupling): trim
    // no longer affects waveform pixels, so they must follow the cursor every
    // motion tick rather than lagging a worker-completion swap. Target-view
    // positions map through the displayed timemap (wf_cache.fp_timemap) — the
    // same coordinate system the marker stems use — which trim does not
    // perturb, so it is stable across a trim drag.
    const int sr = audio.sample_rate();
    std::pair<long long, long long> t;
    if (rve) {
        t = {0, audio.total_frames()};
    } else if (wf_cache.fp_target) {
        const auto src_trim = compute_trim_samples(
            app, sr, audio.total_frames());
        if (!wf_cache.fp_timemap.empty()) {
            const long long t0 = static_cast<long long>(std::nearbyint(
                map_source_to_target(
                    static_cast<size_t>(src_trim.first),
                    wf_cache.fp_timemap)));
            const long long t1 = static_cast<long long>(std::nearbyint(
                map_source_to_target(
                    static_cast<size_t>(src_trim.second),
                    wf_cache.fp_timemap)));
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
    // geometry note in StemCache's class comment. After F.trim the overhang
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
    const uint64_t timemap_hash = wf_cache.fp_timemap_hash;
    const long long audio_gen   = wf_cache.fp_audio_gen;

    // Marker-driven inputs: read live from app state.
    const long long warp_gen   = app.warpmarkers.generation();
    const long long phase_gen  = app.phase_reset_markers.generation();
    const uint64_t  drag_hash  = hash_drag_overlay(app.drag);
    const bool     drag_active = app.drag.active;
    const char     mv          = app.active_markers_view;
    const bool     rve         = app.render_view_enabled;
    const uint64_t sel_hash    = hash_selection(app.selected_markers,
                                                app.last_selected_marker);

    // Brief C: trim boundary stems. Positions ride trim_begin / trim_end
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
        stem_cache.fp_timemap_hash            == timemap_hash &&
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

    // Brief C: trim boundary stems, painted in both 'W' and 'P' views.
    // Positions are the displayed-domain trim frames (already translated);
    // the has-set / selected bits decide which stems draw and in what
    // color.
    // F.trim.2 Defect 3: painted BEFORE the regular marker stems so that
    // where a trim bound and a regular marker share a column the regular
    // stem (painted last on this shared surface) sits in front; the taller
    // trim stem reads as "underneath," reachable by its hotkey.
    render_trim_stems(
        ccr, local_area, vp_start, vp_end,
        trim_struct,
        trim_has_begin, trim_begin_sel,
        trim_has_end, trim_end_sel);

    if (mv == 'P') {
        const auto& list = rve
            ? app.render_view_phase_resets
            : app.phase_reset_markers.markers();
        render_phase_reset_markers(
            ccr, local_area, list,
            vp_start, vp_end, sr,
            app.selected_markers, tmap_arg, drag_overlay);
    } else {
        const auto& list = rve
            ? app.render_view_markers
            : app.warpmarkers.markers();
        render_markers(
            ccr, local_area, list,
            vp_start, vp_end, sr,
            app.selected_markers, tmap_arg, drag_overlay);
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

    // Displayed-viewport inputs from wf_cache.fp_*. Warp/phase flags are
    // positioned at marker times only. F.trim adds the b/e trim chips to this
    // strip, so the displayed-domain trim positions + has/selected bits (from
    // the shared helper, identical to the stem cache's) are now part of the
    // flag cache's identity.
    const int64_t  vp_start     = wf_cache.fp_vp_start;
    const int64_t  vp_end       = wf_cache.fp_vp_end;
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
    // Brief D: iteration mode only affects warp-view (non-render) flags;
    // render view resets the toggle off, so this is false there.
    const bool      iter_on    = app.iteration_mode_enabled &&
                                 mv == 'W' && !rve;

    // FlagPayload editor target drives the skip-guard (cache leaves a
    // hole for the live editor render to fill). Brief B2: the iter/BPM
    // popup-editor outline-suppression channel was removed along with
    // the popup surfaces; the IterationBracket / BpmBracket kinds no
    // longer feed the cache fingerprint.
    int flag_target = -1;
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind ==
            text_editor::Kind::FlagPayload) {
        flag_target = app.top_flag_editor.target;
    }

    // F.trim: displayed-domain trim state for the b/e chips (shared helper,
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
        flag_cache.fp_timemap_hash            == timemap_hash &&
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
                app.render_view_phase_resets,
                vp_start, vp_end, sr,
                kFlagFontSize,
                app.selected_markers,
                nullptr,
                drag_overlay);
        } else {
            render_flags(ccr, local_top_strip,
                         app.render_view_markers,
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

    // F.trim: the b/e trim chips cap their stems in the upper top row. Painted
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
    flag_cache.fp_timemap_hash            = timemap_hash;
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
