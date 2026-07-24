#include "paint_handler.h"

#include "render.h"
#include "text_display.h"
#include "text_editor.h"
#include "time_format.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"
#include "engine/engine_geometry.h"  // kRs

#include <algorithm>
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
                                             int /*sr*/) {
    // Flag annotations in the top strip. All flag pixels — the fixed-width
    // marker/phase-reset shapes and the b/e trim chips — live on
    // flag_cache.surface (rebuilt from on_tick via maybe_rebuild_flag_cache);
    // this pass is a pure blit. The flag shapes are textless; a marker's flag
    // payload text (and the hover popup) surface in the marker-text lane, painted
    // live per-frame in paint_marker_text_lane after this blit — the editing
    // target's flag paints here as an ordinary selected shape. Like the other
    // caches, the surface may be null on the very first paint after a load
    // (before the first rebuild fires); the blit is skipped and the background
    // shows through for that one frame.
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
}

// -- GuiPaintHandler::paint_marker_text_lane -----------------------------

void GuiPaintHandler::paint_marker_text_lane(cairo_t* cr) {
    // The marker-text lane (top lane 2, between the trim chips and the flags)
    // shows AT MOST ONE run, arbitrated in this precedence: the FlagPayload flag
    // editor beats everything (it owns the lane alone while open — recompute_
    // hover_at_cursor already clears the popup whenever any top_flag_editor is
    // active, and the editor-first return below is the explicit backstop); else
    // the HOVERED marker's own value while a hover is showing; else the LAST-
    // SELECTED marker's own value (the same last-selected notion the bottom-strip
    // readout falls back to — here shown for ANY marker type, not only pass/ref);
    // else nothing. All paint live here (per-keystroke editor, per-motion hover,
    // per-frame last-selected), after the flag-cache blit — no cache, the
    // live-overlay role the bottom strip's editor/hover paints had before the lane
    // existed. The run centers its monospace text over its marker's painted column
    // and clamps it fully onscreen (lane_text_left_x_at_frame) on a kBackground
    // fill behind the run with no border. The editor flashes its fill kAccent on
    // an invalid commit.
    const GuiRect lane      = top_marker_text_row_area(app);
    const double  baseline  = lane.y + monospace_row_baseline_offset();
    const double  advance   = monospace_advance();
    if (advance <= 0.0) return;

    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::FlagPayload) {
        const text_editor::State& ed = app.top_flag_editor;
        // The caret-origin owner supplies the run's left x, centered on the
        // marker and clamped onscreen, so paint and the click->byte caret math
        // share one origin.
        const double left = flag_pending_text_left_x(app, audio, ed.target);
        if (left < 0.0) return;   // invalid editor target
        cairo_save(cr);
        cairo_select_font_face(cr, "monospace",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, flag_font_size_px());
        EditorTextBox box;
        box.anchor_x        = left;
        box.baseline_y      = baseline;
        // The MM:SS.mmm| locked prefix is display-only and not shown in the
        // lane — only the editable payload paints, centered on the marker.
        box.prefix          = "";
        box.text            = ed.pending;
        box.hl_pad          = flag_glyph_inset_px();
        // The open editor paints like a SELECTED marker chip: kSelected fill +
        // kSelectedOutline ring (the same pair a selected chip paints), so the
        // box reads as "this marker, selected for editing" instead of an
        // invisible bg-on-bg box. On an invalid commit both flash to kAccent —
        // a color CHANGE on an already-visible box, not a box appearing from
        // nowhere. Text stays kText and readable on kSelected (the same pairing
        // a selected chip's context uses).
        box.fill            = ed.red ? kAccent        : kSelected;
        box.outline         = ed.red ? kAccentOutline : kSelectedOutline;
        box.text_color      = kText;
        box.has_selection   = text_editor::has_selection(ed);
        box.selection_start = text_editor::selection_start(ed);
        box.selection_end   = text_editor::selection_end(ed);
        box.cursor_visible  = text_editor::cursor_visible_now(ed);
        box.cursor_pos      = ed.cursor_pos;
        render_editor_text_box(cr, box);
        cairo_restore(cr);
        return;
    }

    // One-run painter: kBackground fill exactly behind the run (AA off for a
    // crisp edge), then the text — no border, no caret. source_frame centers the
    // run on the marker's painted column (lane_text_left_x_at_frame), column-
    // agnostic so this needs no knowledge of which store the marker came from; a
    // bad advance or clamp yields left<0 and skips. source_frame is a DOUBLE so a
    // mid-drag run can center on the grabbed marker's free proposed position (the
    // same fractional basis the flag overlay paints through — see tier 2).
    auto paint_run = [&](double source_frame, const std::string& txt) {
        if (txt.empty()) return;
        const double left = lane_text_left_x_at_frame(
            app, audio, source_frame, txt.size());
        if (left < 0.0) return;
        const double run_w = static_cast<double>(txt.size()) * advance;
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, kBackground.r, kBackground.g, kBackground.b);
        cairo_rectangle(cr, left, static_cast<double>(lane.y),
                        run_w, static_cast<double>(lane.h));
        cairo_fill(cr);
        cairo_restore(cr);
        text_display::draw_line(cr, left, baseline, txt, kText,
                                flag_font_size_px());
    };

    // Tiers 1 (the HOVERED marker's value) and 2 (else the LAST-SELECTED
    // marker's value, composed from the live store with the mid-drag
    // moveable_times substitution and the painted-column offscreen cull) are
    // resolved by the shared run resolver current_marker_lane_run — the ONE
    // arbitration the unified marker hit resolver (marker_hit_at) also reads,
    // so the painted run and the clickable run cannot drift. paint_run keeps
    // its own txt.empty() and left<0 guards, so consuming the resolved run here
    // is byte-identical to the inline tiers it replaced.
    const LaneTextRun run = current_marker_lane_run(app, audio);
    if (run.valid) paint_run(run.source_frame, run.text);
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

// -- GuiPaintHandler::displayed_viewport_basis / region_columns ----------

// See the declaration comment in paint_handler.h: the fp-recipe basis locked to
// the blitted plate while the worker rebuilds, with the live spp fallback when
// no plate has published a span yet.
GuiPaintHandler::DisplayedViewportBasis
GuiPaintHandler::displayed_viewport_basis() const {
    DisplayedViewportBasis b;
    b.spp = wf_cache.fp_area_w > 0
        ? static_cast<double>(wf_cache.fp_vp_end - wf_cache.fp_vp_start) /
          static_cast<double>(wf_cache.fp_area_w)
        : current_samples_per_pixel(app, audio);
    b.vp_start = static_cast<double>(wf_cache.fp_vp_start);
    return b;
}

GuiPaintHandler::RegionColumns
GuiPaintHandler::region_columns(const DisplayedViewportBasis& basis) const {
    const int64_t lo = std::min(app.region.a_frame, app.region.b_frame);
    const int64_t hi = std::max(app.region.a_frame, app.region.b_frame);
    RegionColumns c;
    c.lo_col = static_cast<int>(std::nearbyint(
        (static_cast<double>(lo) - basis.vp_start) / basis.spp));
    c.hi_col = static_cast<int>(std::nearbyint(
        (static_cast<double>(hi) - basis.vp_start) / basis.spp));
    return c;
}

// -- GuiPaintHandler::paint_region_wash ----------------------------------

// Paints the region-select span as a flat translucent brightening wash over
// the full waveform height. Called from on_redraw right after
// paint_waveform_plate, so it composites over the just-blitted plate AND the
// out-of-trim dim (a region inside a dimmed area lifts the dimmed pixels —
// accepted, it stays visible). Session-only, nothing persisted; not part of
// the plate/stem/flag caches — a direct per-frame overlay like the phase reset
// overlay and the out-of-trim dim, so no cache is involved. AA off on the edges
// like the other overlay rects.
void GuiPaintHandler::paint_region_wash(cairo_t* cr, const GuiRect& area) {
    if (!app.region.active) return;
    if (area.w <= 0 || area.h <= 0) return;

    // Displayed-viewport recipe: the same fp_* fingerprint paint_playheads and
    // paint_phase_reset_overlay use, so the wash stays locked to the blitted
    // plate while the worker rebuilds against a viewport change.
    const DisplayedViewportBasis basis = displayed_viewport_basis();
    if (basis.spp <= 0.0) return;

    // Endpoints normalized to [lo, hi] and mapped to columns via the shared
    // region_columns owner (the plain viewport transform — the endpoints already
    // live in the displayed domain, so no warp map is walked, unlike the phase
    // reset overlay whose source-frame marker crosses to target first).
    const RegionColumns cols = region_columns(basis);

    double x0 = static_cast<double>(area.x + cols.lo_col);
    double x1 = static_cast<double>(area.x + cols.hi_col);
    // Clamp to the visible strip; a span wholly offscreen paints nothing.
    x0 = std::max(x0, static_cast<double>(area.x));
    x1 = std::min(x1, static_cast<double>(area.x + area.w));
    if (x1 <= x0) return;

    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgba(cr, kPlayheadCursorLight.r, kPlayheadCursorLight.g,
                          kPlayheadCursorLight.b, kRegionWashAlpha);
    cairo_rectangle(cr, x0, static_cast<double>(area.y),
                    x1 - x0, static_cast<double>(area.h));
    cairo_fill(cr);
    cairo_restore(cr);
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
    // R3 suppression (architect 2026-07-23): the overlay depicts ONE focused
    // reset's lead-in, a single-focus authoring aid. Suppress it when the state
    // is about a SPAN rather than a single focus — a MULTI-select (2+ members) or
    // an active region — where the overlay would clutter. (A singleton or empty
    // selection with no region shows it as before; the states that toggle these
    // conditions — the multi-select builders and every region former/clear — all
    // damage the waveform, so the overlay's appear/disappear rides their damage.)
    if (app.selected_markers.size() >= 2) return;
    if (app.region.active) return;

    // Paint sample: the exact expression render.cpp's file-local
    // frame_to_paint_sample uses, so marker and overlay can never disagree.
    double ms;
    {
        if (app.active_audio_view != 'T') return;

        const auto& markers = app.phaseresetmarkers.markers();
        const int idx = app.last_selected_marker;
        if (idx < 0 || idx >= static_cast<int>(markers.size())) return;
        const auto& marker = markers[idx];
        // Skip a disabled focused reset — a disabled phase reset paints no
        // overlay, reading its `disabled` bool directly (phase resets carry no
        // label cascade).
        if (marker.disabled) return;

        // Map selection: the DISPLAYED paint basis (displayed_or_live_target_map
        // — the SAME map the flags, stems, drag overlay and riding playhead read,
        // falling back to the live cache when cold), so the overlay stays locked
        // to the reset it annotates even inside a worker publish window where the
        // displayed map lags the live cache. No map means identity (matching the
        // stem renderer's fallback). We are already known to be in target view.
        const std::vector<WarpFrameMapSegment>* tmap = nullptr;
        const std::vector<WarpFrameMapSegment>& m =
            displayed_or_live_target_map(app, audio);
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
    const DisplayedViewportBasis basis = displayed_viewport_basis();
    const double spp = basis.spp;
    if (spp <= 0.0) return;
    const double vp_start = basis.vp_start;

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
    // extent is the marker stem's exact span — the waveform top down to the
    // waveform bottom.
    const double y_top = static_cast<double>(area.y);
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
    // stem_cache.surface now carries the TRIM stems only (the marker stem became
    // the selected-marker live overlay paint_selected_stem — see maybe_rebuild_
    // stem_cache), rebuilt synchronously from on_tick. The paint path is blit-only.
    // Like the waveform cache, this surface may be null on the
    // very first paint after a load (before the first stem
    // rebuild fires); the blit is skipped and the background
    // shows through for that one frame. The surface's screen
    // origin is `marker_paint_rect.x, marker_paint_rect.y`
    // (i.e. area.x, area.y — the surface is waveform-height with no
    // overhang), matching the local-coord choice in maybe_rebuild_stem_cache.
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

// -- GuiPaintHandler::paint_selected_stem --------------------------------

// Selected-marker stem (round 4, architect 2026-07-23): the stem marks where the
// playhead sits/would land on the SINGLE selected marker, and shows ONLY while
// the user is interacting with THAT marker. Visibility predicate:
//   (0) exactly ONE marker selected (a multi-select paints no stem), and
//   one of:
//   (a) that marker is HOVERED (hover_popup.marker_index == the selected index),
//   (b) a live POSITION DRAG grabs it (drag.active on the active column) — painted
//       at the LIVE proposed position (moveable_times via the DragOverlay); the
//       drag suppresses hover, so this arm cannot lean on (a),
//   (c) the LATERAL-GESTURE PIN is live — this marker's lateral gesture (position
//       nudge/drag, the W+target tempo drag, or the W+target Alt+Left/Right
//       tempo-image step; the Alt+Up/Down tempo step does not stamp —
//       architect 2026-07-24, its own image is fixed by construction) has had no
//       DAMAGING command intervene since (the preserve re-stamps across damage-less
//       ones), so command_seq == stem_pin_command_seq && stem_pin_marker == it. The
//       pin is pure command adjacency now (the key-hold/timer model was
//       architect-reverted 2026-07-24); the on_tick reap zeroes stem_pin_marker
//       once a damaging command breaks adjacency, so paint tests adjacency alone
//       (see AppState::stem_pin_*), and
//   (d) a live TEMPO DRAG grabs it (tempo_drag.active, tempo_drag.marker == it) —
//       the store frame slides visually under the per-step re-warped map (below).
// Painted BLUE (kSelected — it marks the selected marker, like its flag; the grey
// belongs to the focus triangle painted ON each selected marker
// (paint_selected_marker_triangles), not this stem) through render_playhead's line-only
// form (draw_triangle=false, draw_line=true) — the plate ink-notch two-tone reads
// fine in any color (fill_column_ink_runs overdraws kBackground over opaque ink,
// color-independent; the blue line shows over the gaps). It lives OUT of the stem
// cache as a per-frame one-column overlay over the plate; a disabled marker is
// not dimmed here (the flag conveys it). The displayed paint basis (fp_vp_start +
// disp_spp + the displayed map) matches paint_playheads / the cached flags, so
// the stem lands on the flag's own column; the drag arm reads the frozen
// displayed map its proposal was computed against.
void GuiPaintHandler::paint_selected_stem(cairo_t* cr, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    // (0) A single selected marker, else no stem.
    if (app.selected_markers.size() != 1) return;
    const int idx = *app.selected_markers.begin();
    if (idx < 0) return;

    // Is this the active-column marker being dragged?
    const bool drag_arm =
        app.drag.active && app.drag.drag_mode == app.active_markers_view;
    // Is this the marker whose tempo is being dragged? A tempo drag never MOVES
    // the marker's store frame — it rewrites a predecessor's tempo, so the dragged
    // marker's IMAGE slides laterally under the changing map. The stem paints at
    // the store frame (the default eff_time; NOT the DragOverlay proposal, which is
    // position-only) through the displayed map, and the tempo drag's per-step
    // synchronous re-warps keep displayed == live at each event boundary, so the
    // stem rides the moving image naturally. W+target by construction, so the
    // active-column guard is implicit (warp column).
    const bool tempo_drag_arm =
        app.tempo_drag.active && app.tempo_drag.marker == idx;
    // Visibility arms (a) hover, (b) position drag, (c) lateral-gesture pin,
    // (d) tempo drag. The pin's liveness is pure command adjacency (the on_tick
    // reap zeroes stem_pin_marker once a DAMAGING command breaks adjacency;
    // damage-less commands re-stamp it through the preserve), so paint tests
    // adjacency only.
    // The hover arm is re-arm-latched: after a pointer marker-click, the stem
    // stays hidden while the pointer merely rests inside the clicked marker's
    // hit area, until a mouseout-then-return clears the latch (see
    // AppState::stem_hover_suppress_marker). The drag / tempo-drag / pin arms
    // below are untouched.
    const bool hover_arm =
        (app.hover_popup.marker_index == idx) &&
        !(stem_hover_suppress_active(app) &&
          app.stem_hover_suppress_marker == idx);
    const bool nudge_arm = (app.stem_pin_marker == idx &&
                            app.command_seq == app.stem_pin_command_seq);
    if (!hover_arm && !drag_arm && !nudge_arm && !tempo_drag_arm) return;

    // The marker's effective time: the live store frame, or — under a drag that
    // grabs it — the proposed mid-gesture position (a source-frame double) from
    // the DragOverlay, so the stem tracks the flag 1:1 during the drag.
    double eff_time = 0.0;
    if (app.active_markers_view == 'P') {
        const auto& pv = app.phaseresetmarkers.markers();
        if (idx >= static_cast<int>(pv.size())) return;
        eff_time = static_cast<double>(pv[idx].time_frame);
    } else {
        const auto& mv = app.warpmarkers.markers();
        if (idx >= static_cast<int>(mv.size())) return;
        eff_time = static_cast<double>(mv[idx].time_frame);
    }
    if (drag_arm) {
        DragOverlay ov;
        ov.indices = &app.drag.dragging_markers;
        ov.times   = &app.drag.moveable_times;
        eff_time = ov.effective_time(idx, eff_time);
    }

    const DisplayedViewportBasis basis = displayed_viewport_basis();
    const double disp_spp = basis.spp;
    if (disp_spp <= 0.0) return;
    const double vp_start = basis.vp_start;

    // Forward-map the (possibly fractional) source frame to the displayed axis
    // (identity in source view), the same shape render.cpp's frame_to_paint_sample
    // uses (nearbyint, then the map).
    double ms = std::nearbyint(eff_time);
    if (app.active_audio_view == 'T') {
        const std::vector<WarpFrameMapSegment>& dmap =
            displayed_or_live_target_map(app, audio);
        if (!dmap.empty()) {
            const size_t q = ms < 0.0 ? static_cast<size_t>(0)
                                      : static_cast<size_t>(ms);
            ms = std::nearbyint(map_source_to_target(q, dmap));
        }
    }
    const double px_x = (ms - vp_start) / disp_spp;

    // render_playhead draws only the 1px line + plate ink-notch here
    // (draw_triangle=false), which is exactly the stem; it column-culls px_x
    // itself, so a stem off the visible strip paints nothing.
    render_playhead(cr, area, px_x, kSelected,
                    /*draw_triangle=*/false,
                    /*draw_line=*/true,
                    /*ink_plate=*/wf_cache.surface);
}

// -- GuiPaintHandler::paint_selected_marker_triangles --------------------

// The GREY focus triangle painted ON every selected marker of the active column
// (architect 2026-07-23). Retires the R6 grey-triangle-at-the-cursor form: the
// focus cue is now attached to the selection itself, so undo/redo (which never
// moves the playhead) can no longer strand a grey triangle at a moved playhead.
// Painted AFTER the flag blit, so each grey triangle reads as sitting ON its
// marker's fused blue/red/default triangle; the SELECTION need not be a
// singleton (every member gets one). During a live marker DRAG the triangles
// track the LIVE proposed positions of ALL dragged members (the DragOverlay,
// exactly paint_selected_stem's drag arm), so a group drag shows every member's
// triangle riding the pointer; during a nudge the store already moved. Region
// active does NOT suppress these (a multimarker select shows wash + grey
// triangles). The displayed paint basis (fp_vp_start + derived disp_spp + the
// displayed target map forward-map) matches paint_selected_stem / paint_playheads
// so the triangle lands on the flag's own column even inside a worker publish
// window; render_playhead's triangle-only, stemless, plate-less form column-culls
// each px_x itself.
void GuiPaintHandler::paint_selected_marker_triangles(cairo_t* cr,
                                                      const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    if (app.selected_markers.empty()) return;

    const DisplayedViewportBasis basis = displayed_viewport_basis();
    const double disp_spp = basis.spp;
    if (disp_spp <= 0.0) return;
    const double vp_start = basis.vp_start;

    // Drag overlay: while a live marker drag grabs the active column, every
    // dragged member's triangle rides its proposed mid-gesture position.
    const bool drag_arm =
        app.drag.active && app.drag.drag_mode == app.active_markers_view;
    DragOverlay ov;
    if (drag_arm) {
        ov.indices = &app.drag.dragging_markers;
        ov.times   = &app.drag.moveable_times;
    }

    // Target view forward-maps each source frame to its target image (identity
    // in source view — no map walked). One map reference for the whole loop.
    const std::vector<WarpFrameMapSegment>* dmap =
        (app.active_audio_view == 'T')
            ? &displayed_or_live_target_map(app, audio)
            : nullptr;

    const bool phase = (app.active_markers_view == 'P');
    const size_t n = phase ? app.phaseresetmarkers.markers().size()
                           : app.warpmarkers.markers().size();

    for (int idx : app.selected_markers) {
        if (idx < 0 || idx >= static_cast<int>(n)) continue;  // stale-index skip
        double eff_time = phase
            ? static_cast<double>(app.phaseresetmarkers.markers()[idx].time_frame)
            : static_cast<double>(app.warpmarkers.markers()[idx].time_frame);
        if (drag_arm) eff_time = ov.effective_time(idx, eff_time);

        double ms = std::nearbyint(eff_time);
        if (dmap && !dmap->empty()) {
            const size_t q = ms < 0.0 ? static_cast<size_t>(0)
                                      : static_cast<size_t>(ms);
            ms = std::nearbyint(map_source_to_target(q, *dmap));
        }
        const double px_x = (ms - vp_start) / disp_spp;
        render_playhead(cr, area, px_x, kPlayheadCursorFocusGrey,
                        /*draw_triangle=*/true,
                        /*draw_line=*/false,
                        /*ink_plate=*/nullptr);
    }
}

// -- GuiPaintHandler::paint_strip_drag_anchor ----------------------------

// Paints the strip-drag anchor stem (the Ableton pivot affordance) at the
// drag's current anchor column, full waveform height. Live only mid-gesture:
// gated on the drag being active AND past the moved threshold, so a bare press
// shows nothing and it vanishes the moment the drag ends (release/Esc/button
// loss clear strip_drag before the next paint). The anchor column is recomputed
// each frame from the persisted anchor_sample against the DISPLAYED viewport
// (wf_cache.fp_*), the same basis paint_region_wash and paint_playheads use, so
// the stem stays locked to the blitted plate while the worker rebuilds. The
// anchor lives in the active display domain (viewport_start + col*spp), so no
// warp map is walked. render_strip_anchor_stem clamps the column to the visible
// edges — an edge-pinned anchor draws the clamp itself.
void GuiPaintHandler::paint_strip_drag_anchor(cairo_t* cr, const GuiRect& area) {
    if (!app.strip_drag.active || !app.strip_drag.moved) return;
    if (area.w <= 0 || area.h <= 0) return;

    const DisplayedViewportBasis basis = displayed_viewport_basis();
    const double spp = basis.spp;
    if (spp <= 0.0) return;
    const double vp_start = basis.vp_start;
    const int col = static_cast<int>(std::nearbyint(
        (app.strip_drag.anchor_sample - vp_start) / spp));
    render_strip_anchor_stem(cr, area, col, wf_cache.surface);
}

// -- GuiPaintHandler::paint_playheads ------------------------------------

void GuiPaintHandler::paint_playheads(cairo_t* cr, const GuiRect& area) {
    // Use the displayed viewport AND its samples-per-pixel
    // (wf_cache.fp_vp_start, derived spp) so the cursor stays in
    // lockstep with the cached waveform / stem / flag layers during
    // the 1-2 paint frames while the worker rebuilds against a
    // viewport change. See declaration comment in app_state.h.
    const DisplayedViewportBasis basis = displayed_viewport_basis();
    const double disp_spp = basis.spp;
    const double px_x = playhead_pixel_x(app, audio,
                                         wf_cache.fp_vp_start, disp_spp);

    // Playheads now paint UNDER the marker flags (the Z-ORDER FLIP, architect
    // 2026-07-23 — see the paint-order block in on_redraw): the cursor line +
    // triangle and the region split half-triangles pass beneath a marker flag
    // sharing their column, so a multimarker select's extent-region halves rest
    // hidden behind the earliest/latest members' flags. The scanner line stays
    // waveform-only (no flag lane), so its stacking is unaffected; the cursor
    // still draws over the marker STEMS below it in the waveform. The triangle
    // indicator lives in the top strip, so render whenever either the waveform or
    // top strip is exposed; otherwise a flag-strip-only repaint would erase the
    // triangle.
    //
    // Split-playhead paint order: scanner first (line only, gated
    // on playhead_scanner_active), then cursor (line + triangle).
    // The cursor draws over the scanner on overlap.
    //
    // A region does NOT touch the scanner — only the CURSOR dissolves into
    // the split half-triangles below. The scanner issues from and tracks the
    // audition normally, so region playback shows the moving scanner line
    // (launched from the region's left bound) alongside the two static split
    // half-triangles and the wash.
    if (app.playhead_scanner_active) {
        const double scan_px = scanner_pixel_x(app, audio,
                                               wf_cache.fp_vp_start,
                                               disp_spp);
        render_playhead(cr, area, scan_px, kPlayheadScanner,
                        /*draw_triangle=*/false,
                        /*draw_line=*/true,
                        /*ink_plate=*/wf_cache.surface);
    }

    // While a region-select is active the normal cursor playhead DISSOLVES —
    // neither its 1px vertical line nor its single triangle paints — and the
    // split playhead takes its place: two half-triangles, one on each region
    // bound. The bound columns use the SAME displayed-viewport recipe (disp_spp
    // + wf_cache.fp_vp_start) as paint_region_wash, so the halves' shared edges
    // land exactly on the wash's left/right edges. Region endpoints are
    // active-domain frames already in the displayed domain, so their column is
    // the plain viewport transform (no warp map walked, matching the wash). The
    // scanner is untouched — only the cursor splits.
    //
    // THIS BRANCH IS THE WASH<->CURSOR EXCLUSIVITY OWNER (the highlight IS the
    // playhead stretched out — the split halves are its two ends, so a wash and
    // a cursor must never co-display; architect 2026-07-23). The exclusivity is
    // structural, not per-former: paint_region_wash gates on the same
    // app.region.active this if/else branches on, and the CURSOR forms emit only
    // here — the region branch owns the split, the empty-selection else owns the
    // waveform-focus green (a NON-EMPTY selection paints NOTHING at the cursor now;
    // the grey focus triangle moved ONTO the selected markers, retiring the R6
    // grey stemless cursor). render_split_playhead has no other caller and the
    // green cursor is emitted only in the else below (paint_selected_stem /
    // paint_selected_marker_triangles also call render_playhead, but as marker
    // overlays, never as the cursor), so within any one frame the wash and the
    // cursor are mutually exclusive by state.
    // Across frames it holds because every
    // app.region write is paired with waveform-area damage at its site (the
    // formers, the clears, clear_region_highlight, the Esc pre_region restore,
    // the tick repair), so the frame that first paints one has already erased
    // the other — no stale co-display window exists. What CAN legitimately
    // co-display with the cursor is the out-of-trim DIM contrast (a resting
    // trim window with no active region — e.g. a lone bound, or after any
    // region clear): that is trim's own display, not the region highlight.
    if (app.region.active) {
        if (disp_spp > 0.0) {
            // Same displayed basis and region_columns owner as
            // paint_region_wash, so the split halves' shared edges land exactly
            // on the wash's left/right edges.
            const RegionColumns cols = region_columns(basis);
            render_split_playhead(cr, area, cols.lo_col, cols.hi_col,
                                  kPlayheadCursor);
        }
    } else if (app.selected_markers.empty()) {
        // WAVEFORM FOCUS (architect 2026-07-23): selection empty — the normal
        // breeze-green (kPlayheadCursor) 1px line + triangle, two-toned over the
        // plate ink.
        render_playhead(cr, area, px_x, kPlayheadCursor,
                        /*draw_triangle=*/true,
                        /*draw_line=*/true,
                        /*ink_plate=*/wf_cache.surface);
    }
    // else: selection non-empty, no region — NOTHING paints at the cursor
    // (architect 2026-07-23, retiring the R6 grey stemless cursor triangle). The
    // focus cue is the grey triangle painted ON each selected marker
    // (paint_selected_marker_triangles, after the flag blit); the resting cursor
    // is simply hidden while a selection is live. The scanner above is
    // unaffected — it launches from this resting cursor and paints its own color.
}

// -- GuiPaintHandler::paint_bottom_strip ---------------------------------

void GuiPaintHandler::paint_bottom_strip(cairo_t* cr, int sr) {
    // Bottom strip: TWO text rows. The status line lives on the lower (outer)
    // row and paints UNCONDITIONALLY — it is no longer the trailing else of a
    // chain, so it stays visible while an editor is open on the upper
    // (inner) row, letting the user keep their timestamp / S-T / W-P /
    // A-B bearings while typing. The upper row carries the transient /
    // modal chain in precedence order: prompt > queue > settings editor
    // > BPM editor > pass/ref hover readout. The prompt is a one-key-answer modal
    // and owns the upper row; status stays visible under it (harmless
    // context). (The hover readout is the resolved-tempo string for a pass /
    // label_ref marker; a marker's OWN value shows in the marker-text lane —
    // paint_marker_text_lane.) Each row's baseline is derived from its row rect, not
    // from the window bottom. (The former pan-strip row retired — pan lives on
    // the Alt+drag waveform grab and the zoom strip's horizontal drag axis.)
    const GuiRect lower_row = bottom_lower_row_area(app);
    const GuiRect upper_row = bottom_upper_row_area(app);

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
    //     (what the user hears), the cursor otherwise (the scanner is
    //     meaningful only while active, so the ternary takes the cursor at rest).
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
    } else {
        // The pass/ref resolved readout renders below every modal/progress tier,
        // driven by BOTH hover and selection. Simple rule: the HOVERED marker's
        // readout wins when present (compute_hover_popup_text, cached in
        // hover_popup at recompute); else the LAST-SELECTED marker's, computed
        // live here when it is an eligible pass/label_ref (popup_eligible_marker,
        // itself 'W'-view + non-iteration only). One readout, hover wins. Owners
        // and phase resets have nothing to resolve, so their strip stays clean
        // while their own value shows in the marker-text lane. The live
        // computation is the notice-free string (copy_payload is a hover-only
        // concern, so no out-param here).
        std::string readout = app.hover_popup.readout_text;
        if (readout.empty() &&
            popup_eligible_marker(app, app.last_selected_marker)) {
            readout = compute_hover_popup_text(
                slice_to_warp_markers(app.warpmarkers.markers()),
                app.last_selected_marker, sr, audio.total_frames());
        }
        if (!readout.empty()) {
            text_display::draw_line(
                cr, static_cast<double>(timestamp_pad_x()), upper_baseline,
                readout, kText, flag_font_size_px());
        }
    }
}

// -- GuiPaintHandler::on_redraw ------------------------------------------

void GuiPaintHandler::on_redraw(cairo_t* cr, int x, int y, int w, int h) {
    init_monospace_grid_metrics(cr);

    // Event-synchronized hit geometry, PROMOTE phase (ruling at the selector):
    // done at the TOP of the frame, BEFORE any painting, so the caches this
    // frame blits (stems/flags/chips are blit-only below) AND the overlays this
    // frame paints on top of them (the marker-text lane, hover, selection, the
    // playhead) all land on the SAME map — the one the committed items were
    // built against. Promoting at the frame's END instead let the overlays paint
    // against the OLD map on the very frame that first blit the rebuilt caches,
    // then advanced the map silently with no further damage, so those overlay
    // pixels could stay misplaced (and a stationary hover could keep naming a
    // marker whose flag had moved away). Promote the staged value the last item
    // rebuild left, once — staged_displayed_valid clears on the first damage rect
    // of the frame, so the remaining rects are no-ops; idle frames with no staged
    // value do nothing. A rebuild always invalidates its item region, so the
    // committing frame's damage always includes the items. No input dispatches
    // mid-loop (single-threaded) and the whole frame still commits atomically
    // after the loop in GuiPlatform::paint_one_frame, so a press only ever reads
    // the last COMMITTED frame's geometry — that guarantee is unchanged. Bump
    // displayed_map_gen so a silent geometry change is visible to hover identity.
    //
    // Refresh the hover identity against the JUST-promoted map, still before any
    // painting: the hook (recompute_hover_at_cursor, wired in main.cpp) hit-tests
    // the new flag positions and re-stamps lane_text/readout_text/copy_payload,
    // so the run/readout this frame paints — and any Ctrl+C landing after this
    // frame but before the next tick — reads the new identity, not the old map's
    // (the run could otherwise follow a marker to its new column though it is no
    // longer under the cursor). No input dispatches mid-paint (single-threaded),
    // so the recompute is safe here. The on_tick displayed_gen check remains the
    // BACKSTOP for a promotion-free store mutation; this hook owns the
    // promoting-frame case that the tick (running before the paint) cannot.
    if (app.staged_displayed_valid) {
        app.displayed_target_warp_frame_map =
            std::move(app.staged_displayed_target_warp_frame_map);
        app.staged_displayed_target_warp_frame_map.clear();
        app.staged_displayed_valid = false;
        ++app.displayed_map_gen;
        if (on_displayed_map_promoted) on_displayed_map_promoted();
    }

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
            // Region-select wash: over the plate and the out-of-trim dim,
            // under the phase reset overlay, stems, and playheads.
            paint_region_wash(cr, area);
        }

        // Markers: vertical stems in the waveform area, beneath the
        // playhead. Cairo's outer clip confines painting to `exposed`. Stems
        // now span the WAVEFORM AREA ONLY (top at `area.y`, down to
        // `area.y + area.h`) — no strip overhang; the flag+triangle structure
        // above (the flag cache) supplies the connection. The paint rect is
        // exactly the waveform area, matching the stem-cache surface height and
        // origin in maybe_rebuild_stem_cache.
        // Final paint order (top to bottom of the stack): waveform plate ->
        // region wash -> phase-reset overlay -> marker stems -> selected stem ->
        // playheads (scanner + split/cursor) -> flag blit -> grey
        // selected-marker focus triangles -> marker-text lane / zoom ring ->
        // strip-drag anchor -> bottom strip. The Z-ORDER FLIP (architect
        // 2026-07-23): the cursor playhead (green line+triangle) and the region
        // SPLIT half-triangles now pass UNDER marker flags, so on a multimarker
        // select the extent region's half-triangles rest hidden behind the
        // earliest/latest members' flags (identical 17-wide triangle geometry at
        // the same column) while the grey focus triangles paint OVER the flags.
        const GuiRect marker_paint_rect = area;
        if (rects_intersect(exposed, marker_paint_rect)) {
            // Over the plate and dim, under the stems: the phase reset
            // overlay lightens the span ahead of the focused phase reset,
            // then the stems paint on top so the focused stem stays crisp.
            paint_phase_reset_overlay(cr, area);
            paint_marker_stems(cr, marker_paint_rect);
            // Selected-marker stem over the plate + trim stems, under the
            // playhead and the flags (round 4): the single selected marker's
            // column, live per-frame (not cached), while it is hovered /
            // dragged / nudged.
            paint_selected_stem(cr, area);
        }

        // Playheads BEFORE the flag blit (Z-ORDER FLIP, architect 2026-07-23):
        // the scanner line stays waveform-only (triangle-free, no lane conflict —
        // its stacking vs the lanes is unaffected by this move), while the cursor
        // line+triangle and the region split half-triangles now paint UNDER the
        // marker flags that follow. flag_cache.surface is ARGB32, CLEAR-cleared
        // each rebuild and transparent outside the painted shapes, so the flag
        // blit composites source-over and never erases the playheads it does not
        // cover. Gated on area OR top_strip: the cursor line lives in the waveform
        // area, its triangle in the triangle lane (top strip).
        if (rects_intersect(exposed, area) ||
            rects_intersect(exposed, top_strip)) {
            paint_playheads(cr, area);
        }

        if (rects_intersect(exposed, top_strip)) {
            paint_flag_annotations(cr, top_strip, sr);
            // Grey selected-marker focus triangles OVER the flag blit (architect
            // 2026-07-23): each selected marker's grey triangle sits on its fused
            // flag triangle. Painted here so it beats the flag color; on a
            // multimarker select these ride the wash while the split half-triangles
            // stay hidden under the flags (see the flip note above).
            paint_selected_marker_triangles(cr, area);
            // Marker-text lane (top row 2): the hover popup and the flag
            // editor's live text, painted over the just-blitted flag cache —
            // the same layering role the bottom strip's hover/editor paints had.
            paint_marker_text_lane(cr);
            // Zoom-strip row (top row 0, at the window edge): painted on
            // top of the just-blitted flag cache, which is transparent over
            // this row (it carries no chips there). The ring is the row's
            // only paint; it is a live drag surface (see the strip-drag
            // routing in input_pointer.cpp) that also owns the double-click
            // zoom toggle.
            render_strip_row_ring(cr, top_zoom_row_area(app),
                                  waveform_area(app).w);
        }

        // Strip-drag anchor stem: over the plate/stems in the waveform area
        // only. It now paints AFTER the playheads (the flip moved them up), so
        // where the pivot column coincides with the cursor/scanner column during
        // a strip drag the grey anchor stem sits OVER the playhead LINE (both are
        // waveform verticals; the playhead's triangle lane is untouched, the
        // anchor carries no triangle). The anchor shows only mid-strip-drag, so
        // this overlap is transient and the pivot affordance reading on top is
        // acceptable. paint_marker_text_lane likewise ends up after the playheads
        // but on the non-overlapping text lane.
        if (rects_intersect(exposed, area)) {
            paint_strip_drag_anchor(cr, area);
        }

        const GuiRect bottom_strip = timestamp_invalidate_rect(app);
        if (rects_intersect(exposed, bottom_strip)) {
            paint_bottom_strip(cr, sr);
        }
    }

    cairo_restore(cr);

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
    // EOF included — and the hit test (hit_test_trim_chip) tests
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

    // A zoom level valid at the old width may exceed the per-file effective
    // ceiling at the new width. The level ceiling and the viewport clamp both
    // live in clamp_viewport_start now; the resize keeps only its TRIGGER role
    // and delegates. When the level actually moved the reflow changed spp under
    // the playback predictor, so re-anchor it.
    const double old_zoom = app.zoom_level;
    clamp_viewport_start(app, audio);
    if (app.zoom_level != old_zoom && playback.is_playing())
        playback.resync_predictor();
}
