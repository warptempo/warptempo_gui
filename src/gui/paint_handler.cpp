#include "paint_handler.h"

#include "render.h"
#include "text_display.h"
#include "text_editor.h"
#include "time_format.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"
#include "engine/engine_geometry.h"  // kRs

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// On-screen paint handler: on_redraw and its per-strip paint passes, the
// out-of-trim geometry they use, and on_resize. The off-screen surfaces
// these passes blit — the waveform plate and the marker-stem and flag-rect
// caches — are produced in waveform_cache.cpp.

// The settings-prompt editor and the BPM editor paint the same
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
    cairo_set_font_size(cr, flag_font_size_px());

    EditorTextBox box;
    box.anchor_x        = anchor_x;
    box.baseline_y      = baseline_y;
    box.prefix          = prefix;
    box.text            = ed.pending;
    // hl_pad is the glyph inset (ring + pad); anchor_x here is the caller-solved
    // glyph origin, so this back-derivation keeps the invisible ring geometry
    // consistent with the chip renderers even though the box body reads as plain
    // light text on the dark strip.
    box.hl_pad          = flag_glyph_inset_px();
    // The normal-state ring and fill are both the background color, so the box
    // body is the same as a chip's but invisible — light text on dark bg; the
    // red flash colors match a parse-fail chip.
    box.fill            = ed.red ? kAccent        : kBackground;
    box.outline         = ed.red ? kAccentOutline : kBackground;
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
    // Iteration ranges render inside the flags themselves, and the BPM
    // editor and hover readout render in the bottom strip, so nothing in
    // this pass is dark. Like the other caches, the
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
    const std::vector<WarpFrameMapSegment>* tmap_disp =
        (wf_cache.fp_target && !wf_cache.fp_warp_frame_map.empty())
            ? &wf_cache.fp_warp_frame_map : nullptr;

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

    // Live editor flag: only paints in W marker-view (the FlagPayload editor
    // isn't available in the 'P' path). The cache leaves a hole over the editor
    // target via the skip-guard in render_flags, so this fill is mandatory
    // whenever overlay.marker_index >= 0 — otherwise that flag's pixels would
    // be missing entirely.
    if (overlay.marker_index >= 0 &&
        app.active_markers_view != 'P') {
        render_one_editor_flag(
            cr, top_strip, waveform_area(app).w,
            app.warpmarkers.markers(),
            vp_start_disp, vp_end_disp, sr,
            flag_font_size_px(),
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
    // wf_cache.surface is produced by one of three paths, all of which
    // leave this paint path blit-only:
    //   1. Worker full render — maybe_enqueue_waveform_render
    //      dispatches a full-window render on GuiWaveformWorker,
    //      which swaps into wf_cache.surface on completion. Fires
    //      on the on_tick backstop and on non-pan viewport changes
    //      (zoom, center-on-playhead, follow-scroll), plus resize,
    //      the launch load, and target-view warp_frame_map changes.
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

// -- GuiPaintHandler::paint_phase_reset_overlay ---------------------------

// Paint-only overlay width: two synthesis hops of target/output time, the
// scale of the reset's local effect — the stretch of output immediately
// following the reset over which the re-seeded phase takes hold before
// normal propagation resumes. A pure authoring aid with no engine meaning,
// consumed nowhere else in the product.
constexpr double kPhaseResetOverlayHops = 2.0;
const int64_t kPhaseResetOverlaySamples = static_cast<int64_t>(
    std::nearbyint(kPhaseResetOverlayHops * static_cast<double>(kRs)));

// Paints a transient translucent rectangle ahead of the focused phase reset
// marker: a fixed-width forward span in target time starting at the marker's
// stem column, showing the stretch of output immediately following the reset
// over which the re-seeded phase takes hold. Paint-only: no persisted state,
// nothing on disk, no settings key, no undo interaction; render_background
// refills the exposed region every frame, so the fill composites over fresh
// pixels and never accumulates.
//
// Painted in TARGET view, never source view, and this is a
// phase-reset-only surface with no warp sibling (naming-symmetry asymmetry,
// recorded here per CLAUDE.md). The span is a fixed target/output-domain
// width, so it is constant in target time. Source view would show a
// map-dependent, varying width — misrepresenting a constant span — so the
// overlay is not drawn there. The reset's local take-hold stretch is a
// phase-reset-only concept, so there is nothing on the warp axis to mirror.
void GuiPaintHandler::paint_phase_reset_overlay(
    cairo_t* cr, const GuiRect& area) {
    // Visibility: always-on for the focused enabled marker while the global
    // W/P mode is on P, in target view; never source view. Everything
    // downstream is domain-agnostic.
    if (app.active_markers_view != 'P') return;
    if (area.w <= 0 || area.h <= 0) return;

    // Paint sample: the exact expression render.cpp's file-local
    // frame_to_paint_sample uses, so marker and overlay can never disagree.
    double ms;
    {
        if (app.active_audio_view != 'T') return;

        const auto& markers = app.phaseresetmarkers.markers();
        const int idx = app.last_selected_marker;
        if (idx < 0 || idx >= static_cast<int>(markers.size())) return;
        const auto& marker = markers[idx];
        // Mirror the phase-reset stem renderer, which skips disabled markers
        // entirely (render_phaseresetmarkers' is_disabled reads `disabled`).
        if (marker.disabled) return;

        // Map selection: the memoized target display map, walked identically at
        // rest and mid-drag (the frozen-coordinate regime keeps the drag out of
        // the store, so the cache cannot rebuild while a drag is in flight). No
        // map means identity (matching the stem renderer's fallback). We are
        // already known to be in target view.
        const std::vector<WarpFrameMapSegment>* tmap = nullptr;
        const auto& m = target_view_warp_frame_map_cached(
            app, static_cast<long>(audio.sample_rate()),
            static_cast<long>(audio.total_frames())).warp_frame_map;
        if (!m.empty()) tmap = &m;

        // Effective time: during a phase-reset-mode drag, read the focused
        // marker's proposed time through the DragOverlay (same construction
        // as hit_test_flag). A warp-mode drag's indices refer to the warp
        // list, so guard on drag_mode 'P'; otherwise use the live store's
        // time.
        double eff_time = marker.time_frame;
        if (app.drag.active && app.drag.drag_mode == 'P') {
            DragOverlay ov;
            ov.indices = &app.drag.dragging_markers;
            ov.times   = &app.drag.moveable_times;
            eff_time = ov.effective_time(idx, marker.time_frame);
        }

        if (tmap && !tmap->empty()) {
            const size_t src_frame =
                static_cast<size_t>(std::nearbyint(eff_time));
            ms = std::nearbyint(map_source_to_target(src_frame, *tmap));
        } else {
            ms = std::nearbyint(eff_time);
        }
    }

    // Displayed-viewport recipe: same as paint_playheads, so the overlay
    // stays locked to the blitted plate and the stem cache while the worker
    // rebuilds against a viewport change.
    const double spp = wf_cache.fp_area_w > 0
        ? static_cast<double>(wf_cache.fp_vp_end - wf_cache.fp_vp_start) /
          static_cast<double>(wf_cache.fp_area_w)
        : current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;
    const double vp_start = static_cast<double>(wf_cache.fp_vp_start);

    // Columns: left_col uses the same std::nearbyint-to-int placement the stem
    // renderer uses, so the overlay's left edge stays on the marker's column.
    // right_col is a fixed whole-pixel offset ahead of it, so the far edge
    // tracks the marker in lockstep instead of wobbling by independent
    // per-endpoint rounding. width_px is the overlay span banker's-rounded to
    // whole pixels — an approximate but rigid forward extent, which beats an
    // exact but jittering one (the span is an authoring aid, not an engine
    // point).
    const int left_col =
        static_cast<int>(std::nearbyint((ms - vp_start) / spp));
    const int width_px = static_cast<int>(std::nearbyint(
        static_cast<double>(kPhaseResetOverlaySamples) / spp));

    // Too-zoomed-out: if the fixed forward extent rounds below one pixel,
    // paint nothing at all — no sliver, no clamped minimum.
    if (width_px < 1) return;

    const int right_col = left_col + width_px;

    // Rectangle spans columns [left_col, right_col): the stem's own column
    // (left_col) sits under the rectangle, and the stems paint after the
    // overlay, so the stem stays crisp on top of the left seam. Vertical
    // extent is the marker stem's exact span — the lower-row chip bottom
    // down to the waveform bottom.
    const double y_top = flag_chip_bottom_y(area, ChipRow::Lower);
    const double y_bottom = static_cast<double>(area.y + area.h);
    double x0 = static_cast<double>(area.x + left_col);
    double x1 = static_cast<double>(area.x + right_col);

    // Horizontal clip to [area.x, area.x + area.w); draw whenever the
    // intersection is non-empty even if the stem column is off-screen left
    // (the tail can be visible while the stem is not).
    x0 = std::max(x0, static_cast<double>(area.x));
    x1 = std::min(x1, static_cast<double>(area.x + area.w));
    if (x1 <= x0) return;

    // Flat translucent fill, integer pixel edges, no blur or plate masking,
    // so it lightens background and waveform pixels alike (including
    // already-dimmed out-of-trim pixels — that layering is intended).
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgba(cr, kOverlay.r,
                          kOverlay.g, kOverlay.b,
                          kOverlayAlpha);
    cairo_rectangle(cr, x0, y_top, x1 - x0, y_bottom - y_top);
    cairo_fill(cr);
    cairo_restore(cr);
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
                        /*draw_triangle=*/false,
                        /*ink_plate=*/wf_cache.surface);
    }
    render_playhead(cr, area, px_x, kPlayheadCursor,
                    /*draw_triangle=*/true,
                    /*ink_plate=*/wf_cache.surface);
}

// -- GuiPaintHandler::paint_bottom_strip ---------------------------------

void GuiPaintHandler::paint_bottom_strip(cairo_t* cr, int sr) {
    // Bottom strip: three rows of equal height mirroring the top strip — two
    // text rows plus the inert pan-strip row nearest the waveform (painted as a
    // ring below). The status line lives on the lower (outer) row and
    // paints UNCONDITIONALLY — it is no longer the trailing else of a
    // chain, so it stays visible while an editor is open on the upper
    // (inner) row, letting the user keep their timestamp / S-T / W-P /
    // A-B bearings while typing. The upper row carries the transient /
    // modal chain in precedence order: prompt > queue > settings editor
    // > BPM editor > hover readout. The prompt is a one-key-answer modal
    // and owns the upper row; status stays visible under it (harmless
    // context). Each row's baseline is derived from its row rect, not
    // from the window bottom.
    const GuiRect lower_row = bottom_lower_row_area(app);
    const GuiRect upper_row = bottom_upper_row_area(app);

    // Inert pan-strip row (bottom row 2, innermost, adjacent to the waveform):
    // a full-width ring matching the top zoom-strip row. No content yet —
    // gestures arrive in a later phase. Painted first so the text rows below
    // (which sit at different y bands) are unaffected.
    render_strip_row_ring(cr, bottom_pan_row_area(app), waveform_area(app).w);

    const double lower_baseline =
        lower_row.y + monospace_row_baseline_offset();
    const double upper_baseline =
        upper_row.y + monospace_row_baseline_offset();

    // --- Lower row: status line (always on). One assembled field
    //     drawn in a single pass; elements are space-separated and
    //     paint uniformly in kText: timestamp, S/T, W/P, A/B, then the
    //     literal "(read-only)" token when the active A/B tab carries the
    //     read-only flag.
    //
    //     The dirty * and transient message appendices follow the
    //     tokens — they are status, not view letters.
    //
    //     sr is the loaded file's sample rate and the playhead samples are
    //     source-frames. Split-playhead: track the scanner during playback
    //     (what the user hears), the cursor otherwise (equal by invariant when
    //     the scanner is inactive).
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
        if (app.dirty) {
            assembled += ' ';
            assembled += '*';
        }
        if (!app.transient_status_message.empty()) {
            assembled += ' ';
            assembled += app.transient_status_message;
        }

        text_display::draw_line(
            cr, static_cast<double>(timestamp_pad_x()), lower_baseline,
            assembled, kText, flag_font_size_px());
    }

    // --- Upper row: transient / modal chain. ---
    if (app.prompt.active) {
        // Plain tier: the prompt text and its response labels assembled
        // into one string joined by single ' ' characters and drawn in a
        // single pass. Single space between tokens: two spaces never appear
        // in GUI output, and modals use exactly one.
        std::string assembled = app.prompt.text;
        for (const auto& label : app.prompt.response_labels) {
            assembled += ' ';
            assembled += label;
        }
        text_display::draw_line(
            cr, static_cast<double>(timestamp_pad_x()), upper_baseline,
            assembled, kText, flag_font_size_px());
    } else if (!app.queue_progress_text.empty()) {
        text_display::draw_line(
            cr, static_cast<double>(timestamp_pad_x()), upper_baseline,
            app.queue_progress_text, kText, flag_font_size_px());
    } else if (text_editor::is_active(app.settings_editor)) {
        // Settings prompt overlay: "setting: <pending>"
        // through the shared bottom-strip editor helper. Fill is
        // kBackground normally, kAccent on parse failure (handled
        // inside the helper).
        render_bottom_strip_editor(cr, app.settings_editor,
                                   kSettingsEditorPrefix,
                                   static_cast<double>(timestamp_pad_x()),
                                   upper_baseline);
    } else if (text_editor::is_active(app.commit_editor)) {
        // Render-commit prompt overlay: "commit: ./renders/<pending>"
        // through the same shared bottom-strip editor helper as the settings
        // branch above. Fill is kBackground normally, kAccent on an
        // unresolved / bad commit (handled inside the helper).
        render_bottom_strip_editor(cr, app.commit_editor,
                                   kCommitEditorPrefix,
                                   static_cast<double>(timestamp_pad_x()),
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
                                   static_cast<double>(timestamp_pad_x()),
                                   upper_baseline);
    } else if (app.hover_popup.visible) {
        // The hover dwell renders below every modal/progress tier.
        // cached_text is the resolved-tempo string from
        // compute_hover_popup_text.
        text_display::draw_line(
            cr, static_cast<double>(timestamp_pad_x()), upper_baseline,
            app.hover_popup.cached_text, kText, flag_font_size_px());
    }
}

// -- GuiPaintHandler::on_redraw ------------------------------------------

void GuiPaintHandler::on_redraw(cairo_t* cr, int x, int y, int w, int h) {
    init_monospace_grid_metrics(cr);

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
            cr, static_cast<double>(timestamp_pad_x()), upper_baseline,
            app.queue_progress_text, kText, flag_font_size_px());
    } else if (audio.total_frames() > 0) {
        const GuiRect area       = waveform_area(app);
        const GuiRect top_strip  = top_strip_area(app);
        const GuiRect exposed{x, y, w, h};
        const int     sr         = audio.sample_rate();

        // The live viewport / target-warp_frame_map / trim computations
        // live in the cache rebuild paths (waveform via the worker, stems
        // via maybe_rebuild_stem_cache, flags via maybe_rebuild_flag_cache),
        // not in on_redraw, which reads wf_cache.fp_* for displayed-viewport
        // inputs and treats every strip as a blit-then-overlay path.

        if (rects_intersect(exposed, area)) {
            paint_waveform_plate(cr, area);
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
            // Over the plate and dim, under the stems: the phase reset
            // overlay lightens the span ahead of the focused phase reset,
            // then the stems paint on top so the focused stem stays crisp.
            paint_phase_reset_overlay(cr, area);
            paint_marker_stems(cr, marker_paint_rect);
        }

        if (rects_intersect(exposed, top_strip)) {
            paint_flag_annotations(cr, top_strip, sr);
            // Inert zoom-strip row (top row 0, at the window edge): painted on
            // top of the just-blitted flag cache, which is transparent over
            // this row (it carries no chips there). No content yet — gestures
            // arrive in a later phase.
            render_strip_row_ring(cr, top_zoom_row_area(app),
                                  waveform_area(app).w);
        }

        if (rects_intersect(exposed, area) ||
            rects_intersect(exposed, top_strip)) {
            paint_playheads(cr, area);
        }

        const GuiRect bottom_strip = timestamp_invalidate_rect(app);
        if (rects_intersect(exposed, bottom_strip)) {
            paint_bottom_strip(cr, sr);
        }
    }

    cairo_restore(cr);

    // Event-synchronized hit geometry, PROMOTE phase (ruling at the selector):
    // this frame has now blitted the current item caches (stems/flags/chips are
    // blit-only above), so advance the hit map to what the frame commits.
    // Promote the staged value the last item rebuild left, once — idle frames
    // with no staged value do nothing. on_redraw runs once per damage rect, but
    // the whole frame commits atomically after this loop in
    // GuiPlatform::paint_one_frame, and a rebuild always invalidates its item
    // region, so the committing frame's damage always includes the items:
    // promoting on the first rect of that frame lands the slot on this frame's
    // pixels. No input dispatches mid-loop (single-threaded), so a press only
    // ever reads the last COMMITTED frame's geometry.
    if (app.staged_displayed_valid) {
        app.displayed_target_warp_frame_map =
            std::move(app.staged_displayed_target_warp_frame_map);
        app.staged_displayed_target_warp_frame_map.clear();
        app.staged_displayed_valid = false;
    }

    // Force any pending Cairo ops out to the X server. The subsequent flush
    // in GuiPlatform::dispatch_event is then a cheap no-op.
    cairo_surface_flush(cairo_get_target(cr));
}

// -- Out-of-trim geometry (displayed trim + dim rects) -------------------

GuiPaintHandler::DisplayedTrim
GuiPaintHandler::compute_displayed_trim() const {
    DisplayedTrim out;

    // has-set bits come live from the active tab's trim.
    out.has_begin      = app.trim.has_begin;
    out.has_end        = app.trim.has_end;

    // Positions read LIVE from app state (no waveform-cache coupling): trim
    // no longer affects waveform pixels, so they must follow the cursor every
    // motion tick rather than lagging a worker-completion swap. Target-view
    // positions map through the displayed warp_frame_map (wf_cache.fp_warp_frame_map) — the
    // same coordinate system the marker stems use — which trim does not
    // perturb, so it is stable across a trim drag.
    //
    // Positions are the AUTHORED frames, per side, unclamped and unordered
    // (NOT compute_trim_samples, whose per-side [0, total] clamp serves
    // playback ranges): stems and chips paint at the authored spot — past
    // EOF included — and the hit tests (hit_test_trim_boundary/_chip) test
    // the same authored positions, so paint and pick stay in agreement.
    // Bounds may be inverted mid-gesture (crossed cannot rest; this runs
    // per frame); the helper is position-only and needs no order (the
    // dim-rect consumer applies its own inverted-window rule).
    std::pair<long long, long long> t{0, audio.total_frames()};
    if (app.trim.has_begin) {
        t.first = app.trim.begin_frame;
    }
    if (app.trim.has_end) {
        t.second = app.trim.end_frame;
    }
    if (wf_cache.fp_target && !wf_cache.fp_warp_frame_map.empty()) {
        t.first = static_cast<long long>(std::nearbyint(
            map_source_to_target(
                static_cast<double>(t.first < 0 ? 0 : t.first),
                wf_cache.fp_warp_frame_map)));
        t.second = static_cast<long long>(std::nearbyint(
            map_source_to_target(
                static_cast<double>(t.second < 0 ? 0 : t.second),
                wf_cache.fp_warp_frame_map)));
    }
    out.begin = t.first;
    out.end   = t.second;
    return out;
}

GuiPaintHandler::OutOfTrimRects
GuiPaintHandler::compute_out_of_trim_rects(const GuiRect& area) const {
    OutOfTrimRects out;
    if (area.w <= 0) return out;

    // Frames in the same paint domain the trim stems use. begin/end are
    // already mapped through the displayed warp_frame_map in target view.
    const DisplayedTrim dtrim = compute_displayed_trim();
    if (!dtrim.has_begin && !dtrim.has_end) return out;

    // Inverted trim does not dim: with both bounds set and begin strictly
    // later than end (compared here in the displayed domain the rects are
    // computed in) there is no coherent window to shade, so no dim rects at
    // all — no negative-width shading, no misleading window. Inverted is a
    // MID-GESTURE-only state now (crossed/equal cannot rest — the commit
    // auto-clear destroys the pair), so this paints the free crossing
    // during a drag; the stems and chips still paint at their authored
    // positions. Equal bounds are NOT inverted: the two rects meet at the
    // shared stem naturally.
    if (dtrim.has_begin && dtrim.has_end && dtrim.begin > dtrim.end) {
        return out;
    }

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

// -- GuiPaintHandler::on_resize ------------------------------------------

void GuiPaintHandler::on_resize(int w, int h) {
    app.width  = w;
    app.height = h;
    if (app.loading || audio.total_frames() <= 0) return;

    // A numeric zoom level may have been valid at the old width but show
    // more samples than the file at the new width — promote to fit-file.
    // live_total_frames returns the warp_frame_map-derived deformed total in
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
