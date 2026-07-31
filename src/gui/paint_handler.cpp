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

// On-screen paint handler: on_redraw and its per-strip paint passes, and
// on_resize. The off-screen surfaces
// these passes blit — the waveform plate and the flag-rect
// cache — are produced in waveform_cache.cpp. Trim paints live per frame
// (paint_trim), out of any cache.

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
                                             const GuiRect& top_strip) {
    // Flag annotations in the top strip. The fixed-width marker/phase-reset
    // flag shapes live on
    // flag_cache.surface (rebuilt from on_tick via maybe_rebuild_flag_cache);
    // this pass is a pure blit. (Trim's b/e chips left this cache for the live
    // paint_trim pass, which runs BEFORE the playheads — the z-order ruling.)
    // The flag shapes are textless; a marker's flag
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
    // The marker-text lane (top lane 2, between the trim chips and the flags).
    // THE OCCLUSION MODEL: the lane shows EVERY onscreen marker's text ambiently
    // when the whole visible set fits unoccluded at the 9-glyph budget, else it
    // falls back to the ONE-run arbitration (hover, else last-selected). Every
    // ambient run is CAPPED at the budget (truncation is permanent). Two OVERLAYS
    // paint on top of the ambient runs (each suppresses its own marker's ambient
    // run, then draws its replacement last — the same pattern): the TEXT-HOVER
    // EXPANSION (a run whose full text exceeds the budget, drawn in full while its
    // TEXT is hovered) and, above it, the FlagPayload editor box. All paint live
    // here (per-keystroke editor, per-motion hover, per-frame ambient set), after
    // the flag-cache blit — no cache, the live-overlay role the bottom strip's
    // editor/hover paints had before the lane existed. Each run centers its
    // monospace text over its marker's painted column and clamps it fully onscreen
    // (lane_text_left_x_at_frame) on a kBackground fill behind the run with no
    // border. The editor box flashes its fill kAccent on an invalid commit.
    const GuiRect lane      = top_marker_text_row_area(app);
    const double  baseline  = lane.y + monospace_text_row_baseline_offset();
    const double  advance   = monospace_advance();
    if (advance <= 0.0) return;

    const bool editor_active =
        text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::FlagPayload;
    const int editor_target = editor_active ? app.top_flag_editor.target : -1;

    // A run's marker is DISABLED — the glyph half of the opaque disabled cue
    // whose shape half is the flag's kMarkerDisabled pair. Runs are indexed in
    // the ACTIVE column's store (see LaneTextRun), so the verdict follows that
    // column: the warp side respects the label_ref cascade through
    // effective_disabled, the phase-reset side reads its bool (it has no
    // cascade) — the same split the selection walk's disabled-skip uses.
    const bool phase_reset_column = app.active_markers_view == 'P';
    auto run_disabled = [&](int idx) -> bool {
        if (idx < 0) return false;
        if (phase_reset_column) {
            const auto& pv = app.phaseresetmarkers.markers();
            return idx < static_cast<int>(pv.size()) && pv[idx].disabled;
        }
        const auto& mv = app.warpmarkers.markers();
        return idx < static_cast<int>(mv.size()) && effective_disabled(mv, idx);
    };

    // Per-run painter: kBackground fill exactly behind the run (AA off for a
    // crisp edge), then the display text — no border, no caret. Glyphs paint
    // kText, or the opaque kTextDisabled when the run's marker is disabled (a
    // color class, never an alpha fade). WIDTH uses the
    // run's glyph count (never txt.size(): a truncated run is 11 bytes / 9
    // glyphs), while cairo receives the whole UTF-8 display string (the toy API
    // draws U+2026 at the uniform mono advance). source_frame centers the run on
    // the marker's painted column (lane_text_left_x_at_frame), column-agnostic so
    // this needs no knowledge of which store the marker came from; a bad advance
    // or clamp yields left<0 and skips. source_frame is a DOUBLE so a mid-drag run
    // centers on the dragged member's free proposed position.
    auto paint_run = [&](double source_frame, const std::string& txt,
                         size_t glyphs, bool disabled) {
        if (glyphs == 0) return;
        const double left = lane_text_left_x_at_frame(
            app, audio, source_frame, glyphs);
        if (left < 0.0) return;
        const double run_w = static_cast<double>(glyphs) * advance;
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, kBackground.r, kBackground.g, kBackground.b);
        cairo_rectangle(cr, left, static_cast<double>(lane.y),
                        run_w, static_cast<double>(lane.h));
        cairo_fill(cr);
        cairo_restore(cr);
        text_display::draw_line(cr, left, baseline, txt,
                                disabled ? kTextDisabled : kText,
                                flag_font_size_px());
    };

    // Every ambient run (all-visible: the whole set; fallback: the 0/1 run),
    // resolved by the shared owner current_marker_lane_runs — the ONE arbitration
    // the unified marker hit resolver (marker_hit_at) also reads, so the painted
    // runs and the clickable runs cannot drift. Two overlay suppressions: while
    // the editor is open SKIP its own marker's ambient run (the editor box below
    // replaces it), and while a text-hover expansion is active SKIP the expanded
    // marker's capped run (the full-text run below replaces it). Both suppressed
    // markers' CAPPED runs still participated in the verdict.
    const LaneRunSet set = current_marker_lane_runs(app, audio);
    const int expanded_target = set.has_expanded ? set.expanded.marker_index : -1;
    for (const LaneTextRun& run : set.runs) {
        if (editor_active && run.marker_index == editor_target) continue;
        if (run.marker_index == expanded_target) continue;
        paint_run(run.source_frame, run.text, run.glyphs,
                  run_disabled(run.marker_index));
    }

    // The text-hover EXPANDED run paints LAST among the ambient runs (on top,
    // occluding the neighbors it overlaps — the one text occlusion), before the
    // editor box. Full text, centered on the marker's column exactly like its
    // capped run was. (Hover is cleared while any editor is open, so an expansion
    // and the editor box never coexist.)
    if (set.has_expanded)
        paint_run(set.expanded.source_frame, set.expanded.text,
                  set.expanded.glyphs,
                  run_disabled(set.expanded.marker_index));

    // The FlagPayload editor box LAST, overlaying any ambient run it overlaps.
    if (editor_active) {
        const text_editor::State& ed = app.top_flag_editor;
        // The caret-origin owner supplies the box's left x, centered on the
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
        // The open editor paints like an ordinary MARKER chip: kMarker fill +
        // kMarkerOutline ring — the one live marker pair, the same
        // rectangle/outline a selected chip paints too (selection only fills
        // a flag's triangle interior, which this box — a plain rectangle —
        // has none of). So the box reads as "this marker, open for editing"
        // instead of an invisible bg-on-bg box. On an invalid commit both
        // flash to kAccent — a color CHANGE on an already-visible box, not a
        // box appearing from nowhere. Text stays kText and readable on
        // kMarker (the same pairing a chip's context uses).
        box.fill            = ed.red ? kAccent        : kMarker;
        box.outline         = ed.red ? kAccentOutline : kMarkerOutline;
        box.text_color      = kText;
        box.has_selection   = text_editor::has_selection(ed);
        box.selection_start = text_editor::selection_start(ed);
        box.selection_end   = text_editor::selection_end(ed);
        box.cursor_visible  = text_editor::cursor_visible_now(ed);
        box.cursor_pos      = ed.cursor_pos;
        render_editor_text_box(cr, box);
        cairo_restore(cr);
    }
}

// -- GuiPaintHandler::paint_waveform_plate -------------------------------

void GuiPaintHandler::paint_waveform_plate(cairo_t* cr, const GuiRect& area) {
    // wf_cache.surface is produced by one of two paths, both of which
    // leave this paint path blit-only:
    //   1. Worker full render — maybe_enqueue_waveform_render
    //      dispatches a full-window render on GuiWaveformWorker,
    //      which swaps into wf_cache.surface on completion. Fires
    //      for UNDRIVEN changes — resize, the launch load, follow-scroll
    //      during playback — and as the on_tick backstop for any residual
    //      fingerprint drift (a warp_frame_map hash included). Map EDITS
    //      themselves are user-driven and take path 2.
    //   2. Synchronous full render — force_synchronous_waveform_rebuild
    //      renders the full window inline on the GUI thread for every
    //      USER-DRIVEN viewport change: zoom, center-on-playhead, the
    //      one-shot jumps, and panning/scrolling (which had its own
    //      incremental shift-and-strip path until 2026-07-26 — retired so
    //      a moving plate and a resting one come off one route).
    // The paint path is blit-only — it draws whatever pixels the
    // live surface currently holds. For worker-path renders that may
    // be a one- or two-frame-old viewport during the worker-rebuild
    // window; the synchronous path updates the plate in the same
    // frame, so it has no such lag. The flag layer closes
    // any mismatch by layering flags onto a surface keyed
    // off the same displayed-viewport.
    //
    // If wf_cache.surface is null (initial load, before the first
    // worker completion), the blit is skipped and the kCanvas
    // ground fill shows through. The user-visible difference is one
    // extra paint frame of empty canvas between load and first
    // waveform display, masked by the existing load-time progress
    // bar.
    //
    // BLIT-ONLY, AND NOTHING RECOLORS IT AFTER: the out-of-trim dim (a second
    // ink color masked through the plate's own alpha) is retired
    // with the opaque recolor model (architect 2026-07-26). The trim bridge bar
    // is the whole inside-the-window signal now, and the plate's pixels are
    // exactly what the renderer wrote, composited once over whichever ground —
    // kCanvas, or a kRegionCanvas recolor — the pass before
    // this one left. That is what makes ink over a highlighted span identical
    // to ink over plain canvas wherever coverage is full.
    //
    // The clip is the CONTENT band, not the full area: the area's top and
    // bottom rows are render_canvas's kLine border and no band-filling pass may
    // cover them. (The plate's own inset band leaves those rows transparent
    // anyway, so this is the structural statement of the rule rather than a
    // pixel change.)
    if (wf_cache.surface) {
        const GuiRect content = waveform_content_rect(area);
        cairo_save(cr);
        cairo_rectangle(cr, content.x, content.y, content.w, content.h);
        cairo_clip(cr);
        cairo_set_source_surface(cr, wf_cache.surface,
                                 area.x, area.y);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

// -- GuiPaintHandler::plate_viewport_basis / region_columns ----------

// See the declaration comment in paint_handler.h: the fp-recipe basis locked to
// the blitted plate while the worker rebuilds, with the live spp fallback when
// no plate has published a span yet.
GuiPaintHandler::PlateViewportBasis
GuiPaintHandler::plate_viewport_basis() const {
    PlateViewportBasis b;
    b.spp = wf_cache.fp_area_w > 0
        ? static_cast<double>(wf_cache.fp_vp_end - wf_cache.fp_vp_start) /
          static_cast<double>(wf_cache.fp_area_w)
        : current_samples_per_pixel(app, audio);
    b.vp_start = static_cast<double>(wf_cache.fp_vp_start);
    return b;
}

GuiPaintHandler::RegionColumns
GuiPaintHandler::region_columns(const PlateViewportBasis& basis) const {
    const int64_t lo = std::min(app.region.a_frame, app.region.b_frame);
    const int64_t hi = std::max(app.region.a_frame, app.region.b_frame);
    RegionColumns c;
    c.lo_col = static_cast<int>(std::nearbyint(
        (static_cast<double>(lo) - basis.vp_start) / basis.spp));
    c.hi_col = static_cast<int>(std::nearbyint(
        (static_cast<double>(hi) - basis.vp_start) / basis.spp));
    return c;
}

// -- GuiPaintHandler::paint_region_ground --------------------------------

// THE REGION HIGHLIGHT IS A GROUND RECOLOR (the Ableton model, architect
// 2026-07-26): the span's CANVAS becomes the opaque kRegionCanvas over the full
// content height. Called from on_redraw after render_canvas and BEFORE
// paint_waveform_plate, so the ARGB32 plate composites over the recolored
// ground and its antialiased fringes blend against it — the ink over a
// highlighted span is bit-identical to ink over plain canvas wherever coverage
// is full, and only the ground carries the highlight. The retired form was a
// translucent wash painted OVER the plate, which lifted the ink itself —
// exactly what the recolor model rejects.
// Session-only, nothing persisted; not part of the plate/flag caches — a direct
// per-frame pass, so no cache is involved. AA off, integer edges. The fill is
// clipped to the CONTENT band so it cannot cover the area's kLine border rows.
void GuiPaintHandler::paint_region_ground(cairo_t* cr, const GuiRect& area) {
    if (!app.region.active) return;
    if (area.w <= 0 || area.h <= 0) return;

    // Displayed-viewport recipe: the same fp_* fingerprint paint_playheads and
    // the overlay band use, so the ground stays locked to the blitted plate
    // while the worker rebuilds against a viewport change.
    const PlateViewportBasis basis = plate_viewport_basis();
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

    const GuiRect content = waveform_content_rect(area);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgb(cr, kRegionCanvas.r, kRegionCanvas.g,
                         kRegionCanvas.b);
    cairo_rectangle(cr, x0, static_cast<double>(content.y),
                    x1 - x0, static_cast<double>(content.h));
    cairo_fill(cr);
    cairo_restore(cr);
}

// -- GuiPaintHandler::phase_reset_overlay_band / its ring pass ------------

// Paint-only overlay width: two synthesis hops of target/output time, the
// scale of the reset's local effect — the stretch of output immediately
// following the reset over which the re-seeded phase takes hold before
// normal propagation resumes. A pure authoring aid with no engine meaning,
// consumed nowhere else in the product.
constexpr double kPhaseResetOverlayHops = 2.0;
const int64_t kPhaseResetOverlaySamples = static_cast<int64_t>(
    std::nearbyint(kPhaseResetOverlayHops * static_cast<double>(kRs)));

// Resolves the band shown ahead of the focused phase reset marker: a
// fixed-width forward span in target time starting at the marker's stem
// column, showing the stretch of output immediately following the reset over
// which the re-seeded phase takes hold. Paint-only: no persisted state,
// nothing on disk, no settings key, no undo interaction.
//
// THE GEOMETRY AND VISIBILITY OWNER, kept SEPARATE from its one consumer
// (paint_phase_reset_overlay_ring) rather than folded into it. It carries every
// visibility gate — view, focus, the multi-select suppression, the eligible-marker
// resolve, the sub-pixel and offscreen refusals — plus the clipped span, and
// Selection::phase_overlay_subject MIRRORS its selection-state gates — MINUS the
// geometry ones, which are not selection state — to decide when a subject change
// needs waveform damage and, since 2026-07-28, whether Space auditions the
// lead-in. One rule, so it stays one function; that mirror's own reader
// inventory lives at its declaration in selection.h. (It served a second pass,
// an opaque ground recolor under the plate, until the ring became the overlay's
// whole visual — architect 2026-07-27.)
//
// Painted in TARGET view, never source view, and this is a
// phase-reset-only surface with no warp sibling (naming-symmetry asymmetry,
// recorded here per CLAUDE.md). The span is a fixed target/output-domain
// width, so it is constant in target time. Source view would show a
// map-dependent, varying width — misrepresenting a constant span — so the
// overlay is not drawn there. The reset's local take-hold stretch is a
// phase-reset-only concept, so there is nothing on the warp axis to mirror.
GuiPaintHandler::PhaseResetOverlayBand
GuiPaintHandler::phase_reset_overlay_band(const GuiRect& area) const {
    PhaseResetOverlayBand out;
    // Visibility: always-on for the focused enabled marker while the global
    // W/P mode is on P, in target view; never source view. Everything
    // downstream is domain-agnostic.
    if (app.active_markers_view != 'P') return out;
    if (area.w <= 0 || area.h <= 0) return out;
    // The multi-select suppression (architect 2026-07-23): the overlay depicts ONE
    // focused reset's lead-in, a single-focus authoring aid, so a MULTI-select
    // (2+ members) suppresses it — the state is about a span of markers rather
    // than a single focus, and the overlay would clutter. (A singleton or empty
    // selection shows it as before; the multi-select builders all damage the
    // waveform, so the overlay's appear/disappear rides their damage.)
    //
    // NO REGION GATE HERE, and none is needed — THE DERIVATION, recorded once at
    // this site with Selection::phase_overlay_subject's mirror pointing here:
    // every region former DESELECTS at press (the plain upper-half waveform drag
    // and the shift waveform press are the only two — the inventory is at
    // RegionState, app_state.h), so a region rests ONLY beside an EMPTY
    // selection, and an empty selection carries no focused reset for this band
    // to annotate. A region and a subject cannot coexist, so no region test
    // could ever decide this band's visibility.
    if (app.selected_markers.size() >= 2) return out;

    // Paint sample: the exact expression render.cpp's file-local
    // frame_to_paint_sample uses, so marker and overlay can never disagree.
    double ms;
    {
        if (app.active_audio_view != 'T') return out;

        const auto& markers = app.phaseresetmarkers.markers();
        const int idx = app.last_selected_marker;
        if (idx < 0 || idx >= static_cast<int>(markers.size())) return out;
        const auto& marker = markers[idx];
        // Skip a disabled focused reset — a disabled phase reset paints no
        // overlay, reading its `disabled` bool directly (phase resets carry no
        // label cascade).
        if (marker.disabled) return out;

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
    // stays locked to the blitted plate while the worker
    // rebuilds against a viewport change.
    const PlateViewportBasis basis = plate_viewport_basis();
    const double spp = basis.spp;
    if (spp <= 0.0) return out;
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
    if (width_px < 1) return out;

    const int right_col = left_col + width_px;

    // The band spans columns [left_col, right_col): the stem's own column
    // (left_col) sits inside it, and the stems paint after both of the band's
    // passes, so the stem stays crisp on top of the left seam.
    double x0 = static_cast<double>(area.x + left_col);
    double x1 = static_cast<double>(area.x + right_col);

    // Horizontal clip to [area.x, area.x + area.w); the band shows whenever the
    // intersection is non-empty even if the stem column is off-screen left
    // (the tail can be visible while the stem is not).
    x0 = std::max(x0, static_cast<double>(area.x));
    x1 = std::min(x1, static_cast<double>(area.x + area.w));
    if (x1 <= x0) return out;

    out.valid = true;
    out.x0    = x0;
    out.x1    = x1;
    return out;
}

// THE OVERLAY RING — the phase-reset overlay's WHOLE visual (architect
// 2026-07-27): the band's 1px opaque kOverlayOutline border and nothing else,
// painted AFTER the plate. It is a BOUNDARY LINE, like the playheads and the
// stems, so an opaque line crossing waveform ink is correct and intended, and
// with no fill inside it the band now READS as the two edges of a span rather
// than as a tinted region. The CONTENT band bounds it, so the top and bottom
// runs sit inside the kLine border
// rather than on them. A vertical side is drawn only where the band's own edge
// is the true edge — both x0 and x1 come back already clipped to the area, so a
// band running past a viewport edge draws its border there too; that is the
// same flush-to-the-edge reading the trim bridge's clipped fill has, and the
// band is an aid rather than a hit target, so no sentinel machinery is needed.
void GuiPaintHandler::paint_phase_reset_overlay_ring(
    cairo_t* cr, const GuiRect& area) {
    const PhaseResetOverlayBand band = phase_reset_overlay_band(area);
    if (!band.valid) return;

    const GuiRect content = waveform_content_rect(area);
    const double w = band.x1 - band.x0;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgb(cr, kOverlayOutline.r, kOverlayOutline.g,
                         kOverlayOutline.b);
    const double y0 = static_cast<double>(content.y);
    const double h  = static_cast<double>(content.h);
    cairo_rectangle(cr, band.x0, y0, w, 1.0);            // top
    cairo_rectangle(cr, band.x0, y0 + h - 1.0, w, 1.0);  // bottom
    cairo_rectangle(cr, band.x0, y0, 1.0, h);            // left
    cairo_rectangle(cr, band.x1 - 1.0, y0, 1.0, h);      // right
    cairo_fill(cr);
    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_trim -----------------------------------------

// The LIVE trim pass (architect 2026-07-25 — trim z-order below the playhead):
// every trim pixel — both b/e chips, the bridge bar, the strip-crossing stem
// segments, and the waveform stem segments — paints here per frame, in the old
// trim-stem-cache slot (after the phase-reset overlay's ring, before
// paint_selected_stem and hence before every playhead element), so the playhead
// triangle sits OVER a trim stem crossing the triangle lane while marker flags
// stay above the playheads (the z-order flip untouched). "Markers over trim" is
// now STRUCTURAL pass order — trim < selected stem < playheads < flag blit —
// not an intra-cache paint convention; the two-segment stem join (strip segment
// from render_trim_flags, waveform segment from render_trim_stems) lives in
// this ONE pass instead of joining bit-exactly across two caches.
//
// BASIS: the FREE item-geometry owners — item_viewport_basis(app, audio)
// and displayed_or_live_target_map(app, audio) — feeding the shared geometry
// owners displayed_trim_ms / trim_bound_column / trim_bridge_gap /
// trim_chip_rect inside the two renderers, so paint stays column-coherent with
// hit_test_trim_chip / route_trim_chip_press, which read exactly that basis
// (paint == hit by shared owners). Deliberately NOT the member
// GuiPaintHandler::plate_viewport_basis(): that is the PLATE-fingerprint
// basis for plate-registered overlays, and the two differ inside the accepted
// resize item-only-promotion window — trim must ride the ITEM basis the chips'
// hit rects resolve on. The renderers' column math therefore divides the
// basis span by basis.area_w (the width the committed items were mapped
// against), which is why the waveform rect handed to them carries that width.
//
// COLD STATES (nothing promoted yet — first paint after load/adopt, the view
// toggle): the free accessors fall back to the LIVE viewport/map, so trim
// paints on the pre-first-publish frame too. Small intentional behavior
// change: the retired cached path SKIPPED its null cache surfaces there, so
// trim was absent for that one frame — the live pass paints it (an
// improvement, not byte-identical cold behavior).
//
// COORDINATES: both renderers take SCREEN-space rects (the top strip anchors
// at (0,0), so its screen and former cache-local coords coincide; the waveform
// rect carries its real screen x/y).
void GuiPaintHandler::paint_trim(cairo_t* cr, const GuiRect& area,
                                 const GuiRect& top_strip) {
    // No trim gate: the window is ALWAYS set (2026-07-30), so the chips, the
    // stems and the bridge bar simply always paint — at the full window the
    // chips rest on the song edges and the bar spans between them.
    if (area.w <= 0 || area.h <= 0) return;
    if (top_strip.w <= 0 || top_strip.h <= 0) return;

    // The ITEM basis (free owner; the member plate_viewport_basis is the other
    // epoch — see the header comment above).
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    if (basis.area_w <= 0 || basis.spp <= 0.0) return;

    // The item pixels' map: empty (identity) in source view, the committed
    // displayed map (live fallback cold) in target view — exactly
    // hit_test_trim_chip's selection.
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    const std::vector<WarpFrameMapSegment>* map_arg =
        dmap.empty() ? nullptr : &dmap;

    // Per-bound displayed-domain positions through the shared mapping owner
    // (displayed_trim_ms returns an integral-valued double; the int64 round
    // trip through TrimRange is exact, so trim_bound_column sees the same
    // value the hit sites pass). Both bounds are always meaningful.
    TrimRange trim{
        static_cast<int64_t>(displayed_trim_ms(app.trim.begin_frame, map_arg)),
        static_cast<int64_t>(displayed_trim_ms(app.trim.end_frame, map_arg))};

    // Waveform rect for the renderers: real screen origin/height, width =
    // basis.area_w (the committed item width — the column-mapping denominator
    // and the [0, wave_w) painter clip, keeping paint == hit through the
    // accepted resize window; equal to waveform_area(app).w at rest).
    const GuiRect wave_rect{area.x, area.y, basis.area_w, area.h};

    // Waveform stem segments first (verbatim geometry: hard-aliased 1-px
    // verticals, solid kTrimStem straight over the ink),
    // then the top-strip half (chips + bridge bar + strip stem segments, with
    // the side-aware offscreen sentinels and the effective-width clip inside
    // render_trim_flags). The two halves are geometrically disjoint and meet
    // at the waveform top edge, so their relative order is cosmetic.
    //
    // The chip lane's y-band is THREADED IN as top_upper_row_area(app) rather
    // than re-derived inside the painter: this is the same accessor
    // hit_test_trim_chip's y-gate and route_trim_chip_press' bridge y-gate
    // read, so the painted band and the clickable band have ONE owner and
    // cannot drift if the lanes above the chip row ever change.
    render_trim_stems(cr, wave_rect,
                      basis.vp_start_frame, basis.vp_end_frame, trim);
    render_trim_flags(cr, top_strip, top_upper_row_area(app), wave_rect,
                      basis.vp_start_frame, basis.vp_end_frame, trim);
}

// -- GuiPaintHandler::paint_selected_stem --------------------------------

// Selected-marker stem (architect 2026-07-25): the stem is the SINGLETON selection's focus
// visual — it marks where the playhead sits/would land on the one selected
// marker, and it ALWAYS paints for that marker, with NO exception at all (the
// active-region suppression it carried died 2026-07-30 with the SPAN FORM — the
// region is trim scratch, not a playhead, and outranks nothing).
// The visibility predicate is "exactly ONE marker selected"
// (+ the bounds checks below): the hover,
// lateral-gesture PIN, and tempo-drag arms are GONE as gates (the whole
// conditional-stem apparatus — stem_pin_*, the hover arm, the click-site stem
// damages — was harvested when the stem became unconditional). "Always" replaces
// every prior expiry semantic: the stem is hover-INDEPENDENT (a keyboard-only
// selection shows it too) and playback-INDEPENDENT (it persists through scrubs
// and auditions), because it is the selection's focus cue, not a working
// affordance. A marker GRAB is always a SINGLETON selection now (the arming press
// single-selects, and groups are never moved — architect 2026-07-29, the doctrine
// at the head of position_nudge.h), so a live position drag paints its stem
// with no gesture arm needed, and there is no group-grab case to reject beyond the
// size check below.
// The ONE non-selection
// input is the DragOverlay proposal override below: under a live POSITION drag —
// the only marker pointer gesture left, the W+target tempo drag having been deleted
// with the tempo-image family (marker_drag.h) — the
// stem tracks the flag 1:1 at the mid-gesture proposed frame.
// Painted in kSelectedStem — its OWN palette key (architect 2026-07-27), tuned
// independently of every flag fill and ring: a line run the full height of the
// waveform reads far louder than the same value does as a 1px border around a
// flag, so what is right for the ring is not right here — through
// render_playhead's line-only form (draw_triangle=false): one solid line
// straight over whatever it crosses, the waveform ink included (the former
// ink-notch two-tone is retired). It lives OUT of the stem cache as a per-frame
// one-column overlay
// over the plate; a disabled marker's stem is not recolored here (the flag's
// opaque disabled pair conveys it). The
// displayed paint basis (fp_vp_start + disp_spp + the displayed map) matches
// paint_playheads / the cached flags, so the stem lands on the flag's own column;
// the drag override reads the frozen displayed map its proposal was computed
// against. A focused GROUP (2+ selected) paints no stem — the members' kWaveform
// ink triangles plus the always-visible cursor landed on the focus are the
// group's cue (architect 2026-07-30, with the span form retired). The size check
// below is the whole rule — the stem is a SINGLETON visual, never a group's.
void GuiPaintHandler::paint_selected_stem(cairo_t* cr, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    // A single selected marker, else no stem. The stem is a SINGLETON visual and
    // nothing suppresses it: the region is trim scratch (a ground recolor), not a
    // playhead form, so a resting span leaves the stem exactly where it is.
    if (app.selected_markers.size() != 1) return;
    const int idx = *app.selected_markers.begin();
    if (idx < 0) return;

    // The one non-selection input: a live POSITION drag grabbing the active column
    // overrides the store frame with the mid-gesture proposed position so the stem
    // tracks the flag 1:1 (the only marker pointer gesture there is — see the
    // header).
    const bool drag_arm =
        app.drag.active && app.drag.drag_mode == app.active_markers_view;

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

    const PlateViewportBasis basis = plate_viewport_basis();
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

    // render_playhead draws only the 1px line here (draw_triangle=false), which
    // is exactly the stem; it column-culls px_x itself, so a stem off the visible
    // strip paints nothing. The triangle lane rect is still handed over — the
    // parameter is unconditional so no call site can drift from the lane owner.
    render_playhead(cr, area, top_triangle_row_area(app), px_x, kSelectedStem,
                    /*draw_triangle=*/false);
}

// -- GuiPaintHandler::paint_strip_drag_anchor ----------------------------

// Paints the strip-drag anchor stem (the Ableton pivot affordance) at the
// drag's current anchor column, full waveform height. Live only mid-gesture:
// gated on the drag being active AND past the moved threshold, so a bare press
// shows nothing and it vanishes the moment the drag ends (release / button loss /
// the force-end finalizer clear strip_drag before the next paint; Esc no longer
// ends a gesture at all). The anchor column is recomputed
// each frame from the persisted anchor_sample against the DISPLAYED viewport
// (wf_cache.fp_*), the same basis paint_region_ground and paint_playheads use,
// so the stem stays locked to the blitted plate while the worker rebuilds. The
// anchor lives in the active display domain (viewport_start + col*spp), so no
// warp map is walked. render_strip_anchor_stem clamps the column to the visible
// edges — an edge-pinned anchor draws the clamp itself.
void GuiPaintHandler::paint_strip_drag_anchor(cairo_t* cr, const GuiRect& area) {
    if (!app.strip_drag.active || !app.strip_drag.moved) return;
    if (area.w <= 0 || area.h <= 0) return;

    const PlateViewportBasis basis = plate_viewport_basis();
    const double spp = basis.spp;
    if (spp <= 0.0) return;
    const double vp_start = basis.vp_start;
    const int col = static_cast<int>(std::nearbyint(
        (app.strip_drag.anchor_sample - vp_start) / spp));
    render_strip_anchor_stem(cr, area, col);
}

// -- GuiPaintHandler::paint_playheads ------------------------------------

void GuiPaintHandler::paint_playheads(cairo_t* cr, const GuiRect& area) {
    // Use the displayed viewport AND its samples-per-pixel
    // (wf_cache.fp_vp_start, derived spp) so the cursor stays in
    // lockstep with the cached waveform / stem / flag layers during
    // the 1-2 paint frames while the worker rebuilds against a
    // viewport change. See declaration comment in app_state.h.
    const PlateViewportBasis basis = plate_viewport_basis();
    const double disp_spp = basis.spp;
    const double px_x = playhead_pixel_x(app, wf_cache.fp_vp_start, disp_spp);
    // The lane every playhead triangle — whole or split — is stamped in, from
    // the lane accessor rather than derived from the waveform top edge, so the
    // triangles ride the same band as the flag triangles beside them.
    const GuiRect tri_lane = top_triangle_row_area(app);

    // Playheads paint UNDER the marker flags (the Z-ORDER FLIP, architect
    // 2026-07-23 — see the paint-order block in on_redraw): the cursor line +
    // triangle passes beneath a marker flag sharing their column, so a cursor
    // resting on a marker sits hidden behind that marker's flag. The scanner line
    // stays waveform-only (no flag lane), so its stacking is unaffected; the
    // cursor still draws over the marker STEMS below it in the waveform. The
    // triangle indicator lives in the top strip, so render whenever either the
    // waveform or top strip is exposed; otherwise a flag-strip-only repaint would
    // erase the triangle.
    //
    // Paint order: scanner first (line only, gated on playhead_scanner_active),
    // then cursor (line + triangle). The cursor draws over the scanner on
    // overlap.
    if (app.playhead_scanner_active) {
        const double scan_px = scanner_pixel_x(app, wf_cache.fp_vp_start,
                                               disp_spp);
        render_playhead(cr, area, tri_lane, scan_px, kPlayheadScanner,
                        /*draw_triangle=*/false);
    }

    // THE CURSOR PLAYHEAD ALWAYS PAINTS (architect 2026-07-30): ONE playhead
    // form, drawn at the resting cursor column whatever the selection and
    // whatever the region are doing. The kPlayheadCursor 1px line + tip-down
    // triangle, painted solid straight over the plate ink; ONE color for both
    // forms.
    //
    // The three-way chain that used to live here is gone with the SPAN FORM: the
    // region is no longer a playhead at all (it is TRIM SCRATCH — a ground recolor
    // formed by the plain upper-half waveform drag and the shift waveform press,
    // previewed by the lower-half scrub press, consumed by `x`), so it dissolves
    // nothing and suppresses nothing, and the split half-triangle renderer is
    // deleted outright. The non-empty-selection suppression is
    // gone too: a cursor resting ON the focused marker is simply hidden behind
    // that marker's flag by the z-order flip, which is what the old else-arm was
    // spelling out by not painting — and when the arrows move the focused marker
    // the cursor rides along VISIBLY, which is the lane model's honest reading.
    // The region ground still paints under the plate (paint_region_ground); the
    // cursor line crosses it exactly as it crosses waveform ink.
    render_playhead(cr, area, tri_lane, px_x, kPlayheadCursor,
                    /*draw_triangle=*/true);
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
        lower_row.y + monospace_text_row_baseline_offset();
    const double upper_baseline =
        upper_row.y + monospace_text_row_baseline_offset();

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
    // done at the TOP of the frame, BEFORE any painting, so the flag cache this
    // frame blits (blit-only below) AND the overlays this
    // frame paints around it (the live trim pass, the marker-text lane, hover,
    // selection, the
    // playhead) all land on the SAME map — the one the committed items were
    // built against. Promoting at the frame's END instead let the overlays paint
    // against the OLD map on the very frame that first blit the rebuilt cache,
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
        // Promote the displayed VIEWPORT mirror in the SAME block (one promote,
        // one gen bump): the marker-text lane geometry advances to the fp_*
        // viewport the just-blitted flag cache was built against, in
        // lockstep with the map above.
        app.displayed_vp_start = app.staged_displayed_vp_start;
        app.displayed_vp_end   = app.staged_displayed_vp_end;
        app.displayed_area_w   = app.staged_displayed_area_w;
        app.staged_displayed_valid = false;
        ++app.displayed_map_gen;
        if (on_displayed_map_promoted) on_displayed_map_promoted();
    }

    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);

    render_background(cr, x, y, w, h);
    // THE GROUND SPLIT: the chrome erase above covers the whole exposed rect;
    // the waveform area then takes the lighter kCanvas ground. Unconditional and
    // ahead of every content branch, so a cold frame (loading, no audio, or a
    // null plate before the first worker publish) shows canvas where the
    // waveform will be rather than a chrome-colored hole. The outer clip already
    // bounds this to the exposed rect, so the full-rect fill costs nothing off
    // the damage. The rect is the EFFECTIVE-width waveform_area, so the <=15px
    // inert right gutter at a non-multiple-of-16 window stays chrome — it is
    // outside every grid-aligned surface and no waveform pixel ever paints there
    // (no gutter exists at 1920/2560/3840).
    {
        const GuiRect canvas = waveform_area(app);
        render_canvas(cr, canvas.x, canvas.y, canvas.w, canvas.h);
    }

    if (app.loading) {
        // Blank plate during load; the only feedback is the bottom-strip
        // upper-row status ("loading..."), the same slot renders use. Painted
        // here because the total>0 bottom-strip block below does not run while
        // loading (and total_frames is 0 on a cold launch).
        const GuiRect upper_row = bottom_upper_row_area(app);
        const double  upper_baseline =
            upper_row.y + monospace_text_row_baseline_offset();
        text_display::draw_line(
            cr, static_cast<double>(timestamp_pad_x()), upper_baseline,
            app.queue_progress_text, kText, flag_font_size_px());
    } else if (audio.total_frames() > 0) {
        const GuiRect area       = waveform_area(app);
        const GuiRect top_strip  = top_strip_area(app);
        const GuiRect exposed{x, y, w, h};
        const int     sr         = audio.sample_rate();

        // The live viewport / target-warp_frame_map computations live in the
        // cache rebuild paths (waveform via the worker, flags via
        // maybe_rebuild_flag_cache), not in on_redraw, which reads
        // wf_cache.fp_* for displayed-viewport inputs and treats the plate and
        // flag strips as blit-then-overlay paths. Trim is a live pass
        // (paint_trim) on the free item-basis owners.
        //
        // Final paint order (bottom to top of the stack): canvas ground + its
        // kLine border (painted above, unconditionally) -> region ground ->
        // waveform plate -> overlay ring -> LIVE
        // TRIM (chips + bridge bar + strip and waveform stem segments, one pass)
        // -> selected stem -> playheads (scanner + split/cursor) -> flag blit ->
        // marker-text lane / zoom ring -> strip-drag anchor -> bottom strip.
        // Three structural rulings live in this sequence:
        //   THE RECOLOR MODEL (architect 2026-07-26) — a highlight changes the
        //     GROUND, so the ONE ground recolor (the region's) paints BEFORE the
        //     plate and the ink composites over it. The phase-reset overlay
        //     contributes no ground at all (architect 2026-07-27): its 1px RING
        //     is its whole visual, and a boundary line paints AFTER the plate,
        //     crossing the ink like the stems do.
        //   THE Z-ORDER FLIP (architect 2026-07-23) — the cursor playhead (its
        //     line+triangle) passes UNDER
        //     marker flags, so a cursor resting on a marker sits hidden behind
        //     that marker's flag (identical 15-wide triangle geometry at the
        //     same column); the selected-marker focus triangles are GONE
        //     (architect 2026-07-25 — a singleton's focus is its STEM, a
        //     group's is its members' ink triangles plus the landed cursor).
        //   TRIM BELOW THE PLAYHEAD (architect 2026-07-25) — every trim pixel
        //     paints before every playhead element, so the playhead triangle
        //     sits over a trim stem crossing the triangle lane:
        //     trim < selected stem < playheads < marker flags.

        if (rects_intersect(exposed, area)) {
            // THE GROUND RECOLOR, under the plate. render_canvas already laid
            // the kCanvas ground for the whole area above; this repaints the
            // region's span of it opaquely, so the plate's ink and its
            // antialiased fringes composite against the recolored ground.
            paint_region_ground(cr, area);
            paint_waveform_plate(cr, area);
            // The overlay band's boundary ring — the phase-reset overlay's whole
            // visual — over the plate and under trim
            // and the stems, so the focused reset's own stem stays crisp on top
            // of the left seam.
            paint_phase_reset_overlay_ring(cr, area);
        }

        // LIVE TRIM PASS — the old trim-stem-cache slot, now covering ALL trim
        // pixels (waveform stems AND the strip's chips/bridge/stem segments).
        // Gated on EITHER half being exposed: render_background erased every
        // exposed top-strip pixel above, so a strip-only damage (hover text, a
        // flag change) must repaint the strip-resident trim pixels, and a
        // waveform-only damage the stem segments; the outer Cairo damage clip
        // bounds the actual work either way.
        if (rects_intersect(exposed, area) ||
            rects_intersect(exposed, top_strip)) {
            paint_trim(cr, area, top_strip);
        }

        if (rects_intersect(exposed, area)) {
            // Selected-marker stem over the plate + trim, under the playhead and
            // the flags: the single selected marker's focus column, live per-frame
            // (not cached), ALWAYS painted for a singleton selection (no
            // hover/pin/gesture condition).
            paint_selected_stem(cr, area);
        }

        // Playheads BEFORE the flag blit (Z-ORDER FLIP, architect 2026-07-23):
        // the scanner line stays waveform-only (triangle-free, no lane conflict —
        // its stacking vs the lanes is unaffected by this move), while the cursor
        // line+triangle now paints UNDER the
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
            paint_flag_annotations(cr, top_strip);
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
        // a strip drag the anchor stem sits OVER the playhead LINE (both are
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
