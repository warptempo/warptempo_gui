#include "render.h"
#include "app_state.h"
#include "audio.h"
#include "time_format.h"
#include "value_format.h"
#include "warp_frame_map_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// kFlagBottomLiftPx and kStemAboveWaveformPx now live in
// render.h so the iter/BPM popups in main.cpp and the stem blit in
// paint_handler.cpp can reference the same values.

// playhead_half_px() is the half-width (H - 1) of the code-generated
// inverted-triangle playhead mask (2H-1 wide, tip at column H-1); it lives
// in render.h as a single inline accessor shared by this TU's cull and
// main.cpp's invalidation.

namespace {

// Flag text mirrors the canonical line's PAYLOAD (post-pipe). All
// metadata (b=/e=/#) is invisible in the rect; the `|` separator sits to
// the left of the rect, anchoring it to the marker column. Disabled
// markers are skipped entirely (no stem, no flag); color conveys
// selection (kSelected), not disabled state.
//
// Variants:
//   label_ref              → "a.42"
//   inherit, no def        → "pass"
//   inherit, with def      → "pass:a.42"
//   owning, no scale       → "1.23"
//   owning, with scale     → "1.23*1.2345"
//   def, no scale          → "1.23:a.03"
//   def, with scale        → "1.23*1.2345:a.03"
std::string flag_text(const std::vector<GuiWarpMarker>& markers, int idx) {
    const auto& m = markers[idx];

    if (!m.label_ref.empty()) {
        return m.label_ref;
    }

    std::string text;
    if (m.tempo_inherits) {
        text = "pass";
    } else {
        // Serializer forms (tempo straight from integer cents via
        // format_tempo_cents, scale min-4 padded shortest round trip) — the
        // flag paints the stored value at full precision, exactly the
        // serializer's bytes.
        text = format_tempo_cents(m.tempo_cents);
        if (m.tempo_scale.has_value()) {
            text += "*";
            text += format_value_double(*m.tempo_scale, 4);
        }
    }
    if (!m.label_def.empty()) {
        text += ":";
        text += m.label_def;
    }
    return text;
}

// Forward-translate a per-marker effective position (a source-frame
// double) to the paint-sample position used by the stem, flag, and
// hit-rect loops. In target view (warp_frame_map
// non-null/non-empty) the source-frame is rounded with banker's
// nearbyint and looked up through map_source_to_target, and that lookup
// is itself rounded with nearbyint; in source view (null/empty
// warp_frame_map) the result is the frame double rounded with nearbyint.
// Both branches return the same integer displayed frame the playhead
// cursor stores (the active-domain translators apply the same
// nearbyint), so the stem, chip, hit rect, and playhead share a column
// in every view. Painting from the fractional map_source_to_target value
// placed the stem one pixel off the playhead whenever rounding the
// target frame crossed a pixel-column boundary. Callers that need an
// integer sample-frame for trim or viewport arithmetic apply their own
// nearbyint to the returned double; rounding an already-integer-valued
// double is a no-op.
static inline double frame_to_paint_sample(
    double eff_frame,
    const std::vector<WarpFrameMapSegment>* warp_frame_map) {
    if (warp_frame_map && !warp_frame_map->empty()) {
        const size_t src_frame = static_cast<size_t>(
            std::nearbyint(eff_frame));
        return std::nearbyint(map_source_to_target(src_frame, *warp_frame_map));
    }
    return std::nearbyint(eff_frame);
}

// Caller must cairo_surface_flush(ink_plate) before calling. Collect
// contiguous opaque ink runs down one plate column and overdraw them as
// kBackground 1px rectangles at dest_x + icol, dest_y + run start.
void fill_column_ink_runs(cairo_t* cr, int dest_x, int dest_y, int area_h,
                          cairo_surface_t* ink_plate, int icol) {
    if (!ink_plate) return;
    if (cairo_image_surface_get_format(ink_plate) != CAIRO_FORMAT_ARGB32) return;
    const int plate_w = cairo_image_surface_get_width(ink_plate);
    const int plate_h = cairo_image_surface_get_height(ink_plate);
    if (icol < 0 || icol >= plate_w) return;
    const unsigned char* data = cairo_image_surface_get_data(ink_plate);
    const int stride  = cairo_image_surface_get_stride(ink_plate);
    const int y_max   = std::min(area_h, plate_h);

    cairo_set_source_rgb(cr, kBackground.r, kBackground.g, kBackground.b);
    int run_start = -1;
    for (int y = 0; y < y_max; ++y) {
        const bool ink = data[y * stride + icol * 4 + 3] > 127;
        if (ink && run_start < 0) {
            run_start = y;
        } else if (!ink && run_start >= 0) {
            cairo_rectangle(cr,
                            static_cast<double>(dest_x + icol),
                            static_cast<double>(dest_y + run_start),
                            1.0,
                            static_cast<double>(y - run_start));
            run_start = -1;
        }
    }
    if (run_start >= 0) {
        cairo_rectangle(cr,
                        static_cast<double>(dest_x + icol),
                        static_cast<double>(dest_y + run_start),
                        1.0,
                        static_cast<double>(y_max - run_start));
    }
    cairo_fill(cr);
}

// Shared stem-painting loop used by render_markers and
// render_phaseresetmarkers. The only meaningful difference between the
// two callers is how visual-disability is computed: warp markers walk
// the label_ref cascade via `effective_disabled`, phase resets read
// `disabled` directly. That asymmetry is exposed here as a predicate
// `is_disabled(i)`; everything else (viewport math, drag overlay, target
// translation, two-pass in-trim/out-of-trim split, integer-pixel snap)
// is identical for both marker kinds.
template <typename MarkerVec, typename IsVisuallyDisabled>
void render_marker_stems_impl(
    cairo_t* cr,
    GuiRect waveform_area,
    const MarkerVec& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    const std::set<int>& selected_set,
    const std::vector<WarpFrameMapSegment>* warp_frame_map,
    const DragOverlay* drag_overlay,
    IsVisuallyDisabled&& is_disabled,
    cairo_surface_t* ink_plate) {
    if (waveform_area.w <= 0 || waveform_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (sample_rate <= 0) return;

    const double span = static_cast<double>(viewport_end_sample -
                                            viewport_start_sample);
    const double samples_per_pixel = span / static_cast<double>(waveform_area.w);
    if (samples_per_pixel <= 0.0) return;

    // Stem emanates from the flag chip's bottom edge (same column as the
    // marker) and runs down to the waveform bottom. The stem is an extension
    // of the flag: its top originates at the chip bottom, not from an
    // independent waveform-relative offset. See flag_chip_bottom_y in render.h.
    const double y_stem_top = flag_chip_bottom_y(waveform_area, ChipRow::Lower);
    const double y1 = static_cast<double>(waveform_area.y + waveform_area.h);

    cairo_save(cr);
    cairo_set_line_width(cr, 1.0);
    // The waveform plate is stable during this loop; flush once before all
    // per-column ink-run scans.
    if (ink_plate) cairo_surface_flush(ink_plate);

    // Per-marker color picks {kMarker, kSelected} from selected_set.
    // Disabled markers are skipped entirely (no stem). The out-of-trim
    // dim was retired, so there is a single pass. Per-marker stroke
    // is fine at editor marker counts; do not introduce batching without
    // profiling.
    for (size_t i = 0; i < markers.size(); ++i) {
        if (is_disabled(static_cast<int>(i))) continue;
        const auto& m = markers[i];
        // Effective time: when a drag is active and this marker is
        // in the overlay, read its proposed time from the overlay
        // instead of the live store. The warp_frame_map passed in is the
        // display cache's target map (stable for the drag's lifetime), the
        // matching coordinate system for forward translation.
        const double eff_time = drag_overlay
            ? drag_overlay->effective_time(
                  static_cast<int>(i), m.time_frame)
            : m.time_frame;
        // Translate per-marker source-frame to the displayed axis: map in
        // target view (warp_frame_map non-null/non-empty), identity
        // otherwise.
        const double ms =
            frame_to_paint_sample(eff_time, warp_frame_map);
        if (ms < static_cast<double>(viewport_start_sample)) continue;
        if (ms >= static_cast<double>(viewport_end_sample)) continue;
        const GuiColor c = selected_set.count(static_cast<int>(i)) > 0
            ? kSelected : kMarker;
        cairo_set_source_rgb(cr, c.r, c.g, c.b);
        const double x_raw =
            (ms - static_cast<double>(viewport_start_sample))
                / samples_per_pixel;
        const int icol = static_cast<int>(std::nearbyint(x_raw));
        const double x_px = waveform_area.x + icol + 0.5;
        cairo_move_to(cr, x_px, y_stem_top);
        cairo_line_to(cr, x_px, y1);
        cairo_stroke(cr);
        fill_column_ink_runs(cr, waveform_area.x, waveform_area.y,
                             waveform_area.h, ink_plate, icol);
    }

    cairo_restore(cr);
}

} // namespace

// The single iteration-aware text composer. Returns the plain flag_text for
// ineligible markers or when iteration mode is off; for an eligible owning
// marker with iteration on, splices the inline bracket after the tempo and
// before any `*scale`/`:label` (e.g. `1.23+[+1.50,-0.50]*1.2345:a.aa`). All
// warp flag callers route through here so display, hit-rects, and the editor
// seed stay in sync.
std::string flag_text_iter(const std::vector<GuiWarpMarker>& markers,
                           int idx, bool iteration_on) {
    if (idx < 0 || idx >= static_cast<int>(markers.size())) return {};
    const auto& m = markers[idx];
    if (!iteration_on || !iter_popup_eligible_marker(m)) {
        return flag_text(markers, idx);
    }
    // Eligible owning marker (tempo_inherits == false, no label_ref):
    // tempo, then the bracket, then optional scale and label. Values print
    // in the same serializer forms as flag_text.
    std::string text = format_tempo_cents(m.tempo_cents);
    text += format_iter_bracket_inline(m);
    if (m.tempo_scale.has_value()) {
        text += "*";
        text += format_value_double(*m.tempo_scale, 4);
    }
    if (!m.label_def.empty()) {
        text += ":";
        text += m.label_def;
    }
    return text;
}

void render_background(cairo_t* cr, int x, int y, int w, int h) {
    cairo_save(cr);
    cairo_set_source_rgb(cr, kBackground.r, kBackground.g, kBackground.b);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_restore(cr);
}

void render_waveform(cairo_t* cr,
                     GuiRect area,
                     const GuiAudio& audio,
                     int channel,
                     long long viewport_start_sample,
                     long long viewport_end_sample,
                     GuiColor color,
                     const std::vector<WarpFrameMapSegment>* warp_frame_map) {
    if (area.w <= 0 || area.h <= 2) return;
    if (viewport_end_sample <= viewport_start_sample) return;

    const int num_levels = audio.num_levels();
    if (num_levels <= 0) return;

    const double span = static_cast<double>(viewport_end_sample -
                                            viewport_start_sample);
    const double samples_per_pixel = span / static_cast<double>(area.w);

    // Cache layout (must match audio.cpp): level 0 = raw samples;
    // levels 1, 2, 3 = stride 32, 1024, 32768. Pick the coarsest cache
    // level whose stride is <= spp; below stride 32, fall through to raw.
    //
    // In target view (warp_frame_map != nullptr) `samples_per_pixel` is in
    // target-frame units. The resulting imprecision is accepted —
    // tempo-compressed regions paint from a coarser pyramid level than
    // they "should," and stretched regions from a finer one. Aesthetic,
    // not functional.
    int level;
    if      (samples_per_pixel >= 32768.0) level = 3;
    else if (samples_per_pixel >= 1024.0)  level = 2;
    else if (samples_per_pixel >= 32.0)    level = 1;
    else                                   level = 0;
    if (level > num_levels - 1) level = num_levels - 1;

    const double y_center = area.y + area.h * 0.5;
    const double half_h   = area.h * 0.5;

    // Each column becomes a 1px-wide integer-row rectangle so the plate is
    // binary: every pixel is either the color at full alpha or fully
    // transparent, no antialiased tips. Per-column min/max extents stay raw
    // and unsmoothed; only the y endpoints are snapped to whole pixel rows.
    struct ColRect { int x, y0, y1; };
    std::vector<ColRect> rects;
    rects.reserve(static_cast<size_t>(area.w));

    const int y_lo = area.y;
    const int y_hi = area.y + area.h;

    // Column i's left edge (f0) is column i-1's right edge (f1) — the same
    // expression yields the same double, so its translation is the same too.
    // Carry the prior column's right edge forward instead of retranslating it,
    // halving the warp_frame_map calls per column in target view (no-op in source view).
    double f_prev = static_cast<double>(viewport_start_sample);
    double g_prev = warp_frame_map ? map_target_to_source(
                        static_cast<size_t>(f_prev < 0.0 ? 0.0 : f_prev),
                        *warp_frame_map)
                            : f_prev;
    for (int i = 0; i < area.w; i++) {
        const double f1 = static_cast<double>(viewport_start_sample) +
                          (span * (i+1)) / area.w;
        // Target view: translate each column's [t0, t1) endpoint into
        // source-frame via the warp_frame_map so the pyramid read lands at the
        // matching authored audio. Source view: identity.
        const double g0 = g_prev;
        const double g1 = warp_frame_map ? map_target_to_source(
                              static_cast<size_t>(f1 < 0.0 ? 0.0 : f1),
                              *warp_frame_map)
                                  : f1;
        const long long s0 = static_cast<long long>(std::nearbyint(g0));
        long long       s1 = static_cast<long long>(std::nearbyint(g1));
        if (s1 <= s0) s1 = s0 + 1;

        const auto mm = audio.get_peak_range(channel, level, s0, s1);
        const double min_val = mm.first;
        const double max_val = mm.second;

        int y0 = static_cast<int>(std::lround(y_center - max_val * half_h));
        int y1 = static_cast<int>(std::lround(y_center - min_val * half_h));
        // Any signal keeps at least one pixel.
        if (y1 <= y0) y1 = y0 + 1;
        // Clamp to the waveform area's pixel rows.
        if (y0 < y_lo) y0 = y_lo;
        if (y0 > y_hi) y0 = y_hi;
        if (y1 < y_lo) y1 = y_lo;
        if (y1 > y_hi) y1 = y_hi;

        rects.push_back({area.x + i, y0, y1});
        g_prev = g1;
    }

    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

    cairo_set_source_rgb(cr, color.r, color.g, color.b);
    for (const auto& R : rects) {
        if (R.y1 <= R.y0) continue;
        cairo_rectangle(cr, R.x, R.y0, 1, R.y1 - R.y0);
    }
    cairo_fill(cr);

    cairo_restore(cr);
}

void render_playhead(cairo_t* cr,
                     GuiRect area,
                     double  playhead_pixel_x,
                     GuiColor color,
                     bool draw_triangle,
                     cairo_surface_t* ink_plate) {
    if (area.w <= 0 || area.h <= 0) return;
    // Allow partial render at file start / end: the triangle's nearer
    // half stays onscreen even when the tip column itself has clipped
    // past the area edge. The 1px line is column-gated below so it
    // doesn't leak into adjacent regions; the triangle stamp is clipped
    // to the area's horizontal span. This keeps the playhead's visual
    // center aligned with its true frame position rather than snapping
    // it inward at the rightmost samples.
    if (playhead_pixel_x < -static_cast<double>(playhead_half_px())) return;
    if (playhead_pixel_x > static_cast<double>(area.w - 1 + playhead_half_px())) return;

    const double col  = std::nearbyint(playhead_pixel_x);
    const double x_px = area.x + col + 0.5;

    cairo_save(cr);
    if (col >= 0.0 && col < static_cast<double>(area.w)) {
        cairo_set_source_rgb(cr, color.r, color.g, color.b);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, x_px, area.y);
        cairo_line_to(cr, x_px, area.y + area.h);
        cairo_stroke(cr);

        // Two-tone overdraw: where this column crosses opaque waveform ink,
        // recolor the green line to kBackground so it reads as a dark notch
        // cut through the light fill (DAW-style), while it stays green over
        // the dark background and the transparent gaps. The displayed plate
        // is the single source of ink truth — its per-sample alpha is the
        // exact mask the out-of-trim dim pass already uses, so this needs no
        // new geometry and is correct across all views and the shifted plate.
        if (ink_plate) {
            cairo_surface_flush(ink_plate);
            const int icol    = static_cast<int>(col);
            fill_column_ink_runs(cr, area.x, area.y, area.h, ink_plate, icol);
        }
    }

    // Inverted-triangle indicator: stamped from the code-generated A8 mask
    // (playhead_triangle_mask()) so every pixel is explicit — alpha is
    // strictly 0 or 255, no rasterizer ambiguity. The mask is 2H-1 x H
    // (odd width) with the tip at column index H-1 (image-local); integer
    // division places that tip column at `area.x + col`. The triangle sits
    // INSIDE the top waveform inset band (waveform_inset_px() tall, equal to
    // the mask height by construction — both are playhead_triangle_h_px()):
    // top row at area.y, tip (bottom row) at the first drawn sample row. The
    // samples are inset by exactly the mask height in
    // render_waveform_to_cache_surface, so the triangle's band and the
    // sample-free band coincide. Skipped for the scanner call
    // (draw_triangle=false): the triangle belongs to the cursor exclusively
    // under the split-playhead model. The scanner line therefore reads as
    // running ~waveform_inset_px() longer than the cursor's, because the
    // cursor's top band is visually occupied by the triangle while the
    // scanner's is a bare line — both lines actually span the identical full
    // area height; this is expected, not a defect.
    if (draw_triangle) {
        cairo_surface_t* triangle_surface = playhead_triangle_mask();
        const int img_w = cairo_image_surface_get_width(triangle_surface);
        const int img_h = cairo_image_surface_get_height(triangle_surface);
        const double dst_x = static_cast<double>(area.x + col - img_w / 2);
        const double dst_y = static_cast<double>(area.y);
        cairo_rectangle(cr,
                        static_cast<double>(area.x),
                        dst_y,
                        static_cast<double>(area.w),
                        static_cast<double>(img_h));
        cairo_clip(cr);
        cairo_set_source_rgb(cr, color.r, color.g, color.b);
        cairo_mask_surface(cr, triangle_surface, dst_x, dst_y);
    }
    cairo_restore(cr);
}

void render_markers(cairo_t* cr,
                    GuiRect waveform_area,
                    const std::vector<GuiWarpMarker>& markers,
                    long long viewport_start_sample,
                    long long viewport_end_sample,
                    int sample_rate,
                    const std::set<int>& selected_set,
                    const std::vector<WarpFrameMapSegment>* warp_frame_map,
                    const DragOverlay* drag_overlay,
                    cairo_surface_t* ink_plate) {
    render_marker_stems_impl(
        cr, waveform_area, markers,
        viewport_start_sample, viewport_end_sample,
        sample_rate, selected_set, warp_frame_map,
        drag_overlay,
        [&](int i) {
            return effective_disabled(markers, i);
        },
        ink_plate);
}

void render_trim_stems(cairo_t* cr,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim,
                       bool has_begin,
                       bool has_end,
                       cairo_surface_t* ink_plate) {
    if (waveform_area.w <= 0 || waveform_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (!has_begin && !has_end) return;

    const double span = static_cast<double>(viewport_end_sample -
                                            viewport_start_sample);
    const double samples_per_pixel = span / static_cast<double>(waveform_area.w);
    if (samples_per_pixel <= 0.0) return;

    // Same stem geometry as render_marker_stems_impl, but the trim stem
    // originates one row higher: its top is the UPPER-row chip bottom (the
    // b/e flag), so the stem reads as flag-plus-stem like a marker, only
    // taller. The extra length is automatic via ChipRow::Upper.
    const double y_stem_top = flag_chip_bottom_y(waveform_area, ChipRow::Upper);
    const double y1 = static_cast<double>(waveform_area.y + waveform_area.h);

    cairo_save(cr);
    cairo_set_line_width(cr, 1.0);
    if (ink_plate) cairo_surface_flush(ink_plate);

    auto paint_bound = [&](int64_t frame) {
        const double ms = static_cast<double>(frame);
        if (ms < static_cast<double>(viewport_start_sample)) return;
        if (ms >= static_cast<double>(viewport_end_sample)) return;
        cairo_set_source_rgb(cr, kTrimMarker.r, kTrimMarker.g, kTrimMarker.b);
        const double x_raw =
            (ms - static_cast<double>(viewport_start_sample))
                / samples_per_pixel;
        const int icol = static_cast<int>(std::nearbyint(x_raw));
        const double x_px = waveform_area.x + icol + 0.5;
        cairo_move_to(cr, x_px, y_stem_top);
        cairo_line_to(cr, x_px, y1);
        cairo_stroke(cr);
        fill_column_ink_runs(cr, waveform_area.x, waveform_area.y,
                             waveform_area.h, ink_plate, icol);
    };

    if (has_begin) paint_bound(trim.begin);
    if (has_end)   paint_bound(trim.end);

    cairo_restore(cr);
}

void render_trim_flags(cairo_t* cr,
                       GuiRect top_strip_area,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       double font_size,
                       const TrimRange& trim,
                       bool has_begin,
                       bool has_end) {
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (!has_begin && !has_end) return;

    const double span = static_cast<double>(viewport_end_sample -
                                            viewport_start_sample);
    // Map columns against the EFFECTIVE waveform width (this rect's own .w),
    // not the strip's full width, so the b/e chips share the trim stems'
    // samples-per-pixel and stay column-aligned with them at every window
    // width (they differ only at a non-multiple-of-16 window, where the
    // waveform floors to an effective width; a no-op at 1920/2560/3840).
    const double samples_per_pixel =
        span / static_cast<double>(waveform_area.w);
    if (samples_per_pixel <= 0.0) return;

    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);

    const double hl_pad = flag_pad_x_px();

    // Chip bottom = the UPPER-row chip bottom; solve baseline_y exactly as
    // iterate_visible_flags_impl does for the lower row, one row higher. The
    // box is monospace_row_h() tall and baseline sits
    // monospace_row_baseline_offset() below its top, so placing the box bottom
    // (= baseline_y - baseline_off + row_h) at flag_chip_bottom_y gives this
    // baseline. The chip thus fills its full upper-row slot.
    const double baseline_y =
        flag_chip_bottom_y(waveform_area, ChipRow::Upper)
      - static_cast<double>(monospace_row_h())
      + monospace_row_baseline_offset();

    // With both bounds set, TWO 1px horizontal connectors run between the two
    // chips, bracketing the chip row: one along the window's topmost pixel row
    // (device row 0 — the flag cache's top-strip origin equals the screen
    // origin (0,0), so device row 0 is the window's top edge) and one along the
    // chip box's bottom pixel row. Together they bracket the Alt pair-drag's
    // grab band, whose y-gate is exactly the chip row (top_upper_row_area) — the
    // span strictly between the two b/e chips. The chip box bottom is
    // flag_chip_bottom_y(..., ChipRow::Upper), so its bottommost pixel row
    // centers at that minus 0.5. Both are stroked before the chip-box loop, so
    // the chips overpaint where they stand and each line reads as connecting a
    // chip edge. Both columns are computed unconditionally with add_chip's own
    // x_raw math (NOT via add_chip, whose viewport cull must not suppress the
    // lines: with one or both chips offscreen the connectors still span the
    // visible part).
    if (has_begin && has_end && waveform_area.w > 0) {
        auto col_of = [&](int64_t frame) {
            const double x_raw =
                (static_cast<double>(frame) -
                 static_cast<double>(viewport_start_sample)) / samples_per_pixel;
            return static_cast<int>(std::nearbyint(x_raw));
        };
        // min/max spans the pair either way — mid-gesture the displayed domain
        // can invert begin/end. Clamp each end into the mapped waveform width
        // (the width the chips map against); an empty span (both bounds off the
        // same side collapse to one clamped column) skips the strokes. The
        // geometry (lo/hi span, color, width) is shared: two strokes, one
        // computation.
        const int wmax = waveform_area.w - 1;
        const int lo = std::clamp(std::min(col_of(trim.begin), col_of(trim.end)),
                                  0, wmax);
        const int hi = std::clamp(std::max(col_of(trim.begin), col_of(trim.end)),
                                  0, wmax);
        if (hi > lo) {
            // Stroke each line at its row center's +0.5 (the 1px crisp-line
            // convention, aliased like the stems' 1px lines): device row 0 for
            // the top line, and the chip box's bottommost pixel row —
            // flag_chip_bottom_y minus one, so its center is
            // flag_chip_bottom_y - 0.5 — for the bottom line.
            const double bottom_y =
                flag_chip_bottom_y(waveform_area, ChipRow::Upper) - 0.5;
            cairo_set_source_rgb(cr, kTrimMarker.r, kTrimMarker.g, kTrimMarker.b);
            cairo_set_line_width(cr, 1.0);
            cairo_move_to(cr, static_cast<double>(top_strip_area.x + lo), 0.5);
            cairo_line_to(cr, static_cast<double>(top_strip_area.x + hi), 0.5);
            cairo_stroke(cr);
            cairo_move_to(cr, static_cast<double>(top_strip_area.x + lo),
                          bottom_y);
            cairo_line_to(cr, static_cast<double>(top_strip_area.x + hi),
                          bottom_y);
            cairo_stroke(cr);
        }
    }

    // Column placement mirrors render_trim_stems / render_flags: text_left at
    // the bound's integer pixel column, chip extends right via the shared
    // text-box primitive. Color is kTrimMarker (the stem's), so chip and stem
    // are one continuous unit.
    // Build the visible-bounds list, sorted by painted column ascending.
    // Order is by position, NOT begin/end identity: in target view or a
    // degenerate inverted trim the painted order can differ from b/e.
    struct TrimChip {
        double      text_left;
        const char* glyph;
    };
    std::vector<TrimChip> chips;
    auto add_chip = [&](int64_t frame, const char* glyph) {
        const double ms = static_cast<double>(frame);
        if (ms < static_cast<double>(viewport_start_sample)) return;
        if (ms >= static_cast<double>(viewport_end_sample)) return;
        const double x_raw =
            (ms - static_cast<double>(viewport_start_sample))
                / samples_per_pixel;
        const double text_left =
            static_cast<double>(top_strip_area.x) + std::nearbyint(x_raw);
        chips.push_back({text_left, glyph});
    };
    if (has_begin) add_chip(trim.begin, "b");
    if (has_end)   add_chip(trim.end, "e");
    std::sort(chips.begin(), chips.end(),
              [](const TrimChip& a, const TrimChip& b) {
                  if (a.text_left != b.text_left)
                      return a.text_left < b.text_left;
                  // Equal-column bounds use the insertion order's visible
                  // identity explicitly: begin paints, end is elided. The hit
                  // test applies the same tie-break before its elision walk.
                  return std::strcmp(a.glyph, b.glyph) < 0;
              });

    // Greedy-pack elision, identical to iterate_visible_flags_impl: walk
    // left-to-right, elide a candidate only on genuine overlap (its chip
    // left edge falls left of the previous chip's right edge). Adjacent
    // chips may touch without eliding; on overlap the RIGHT chip drops.
    // Each chip's right edge uses its own glyph advance — the height
    // reference string above is only for vertical metrics.
    double rightmost_right_edge = -1e18;
    for (const TrimChip& chip : chips) {
        if (chip.text_left < rightmost_right_edge + hl_pad) continue;
        // Chip advance is the shared monospace arithmetic (glyph count *
        // monospace_advance()), the same convention every other flag path
        // uses, rather than a per-chip cairo_text_extents. The glyph field is
        // ASCII monospace, so the two are equal by construction.
        const double glyph_adv =
            static_cast<double>(std::strlen(chip.glyph)) * monospace_advance();
        EditorTextBox box;
        box.anchor_x    = chip.text_left + hl_pad;
        box.baseline_y  = baseline_y;
        box.text        = chip.glyph;
        box.hl_pad      = hl_pad;
        box.fill        = kTrimMarker;
        box.text_color  = kText;
        render_editor_text_box(cr, box);
        rightmost_right_edge = chip.text_left + glyph_adv + hl_pad;
    }

    cairo_restore(cr);
}

void render_editor_text_box(cairo_t* cr, const EditorTextBox& s) {
    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);

    // Prefix is monospace ASCII like the rest of the box; its advance is
    // exact arithmetic (glyph count * monospace_advance()), matching the flag
    // paths, with no transient cairo_text_extents over s.prefix.
    const double prefix_adv =
        static_cast<double>(s.prefix.length()) * monospace_advance();
    const double editable_left = s.anchor_x + prefix_adv;

    cairo_text_extents_t text_ext;
    cairo_text_extents(cr, s.text.c_str(), &text_ext);

    // The step-1 fill box fills its full row slot rather than the tight glyph
    // bounding box — that geometry now lives entirely inside flag_chip_rect
    // (height = cached monospace_row_h(), top = baseline lifted by
    // monospace_row_baseline_offset()), so baseline_y sits centered in the row
    // and the box bottom lands flush at the slot bottom. Callers solve
    // baseline_y so the box bottom coincides with flag_chip_bottom_y (chips)
    // or the row rect (bottom-strip editors).
    //
    // The cursor (step 5) and the selection highlight (step 4) span exactly the
    // glyph ink band (ascent-to-descent), no vertical padding. The band is
    // recovered from the two cached monospace metrics (exact inverses of how
    // init_monospace_grid_metrics built them: g_row_baseline_off = flag_pad_y_px()
    // + ascent, g_row_h = round(font_height + 2*flag_pad_y_px())). The round() on
    // the row height can leak a sub-pixel into the derived descent; that is
    // cosmetically irrelevant here and saves adding a new metric accessor.
    const double bg_h        = static_cast<double>(monospace_row_h());
    const double ascent      = monospace_row_baseline_offset() - flag_pad_y_px();
    const double font_height = bg_h - 2.0 * flag_pad_y_px();
    const double descent     = font_height - ascent;
    const double glyph_top   = s.baseline_y - ascent;
    const double glyph_h     = ascent + descent;

    // Snap the shared glyph ink band to integer pixel rows once, so the
    // selection highlight (step 4) and the cursor (step 5) both fill crisp
    // integer-edged rectangles with antialiasing off — the same anti-aliased-
    // tip defect corrected in render_waveform. Glyph text (steps 2-3) keeps
    // antialiasing and is untouched.
    const int band_y0 = static_cast<int>(std::lround(glyph_top));
    const int band_y1 = static_cast<int>(std::lround(glyph_top + glyph_h));
    const int band_h  = (band_y1 > band_y0) ? (band_y1 - band_y0) : 1;

    // 1. Solid fill behind the editable region, from the single source of
    //    truth (flag_chip_rect), so the painted chip and the hit rect are the
    //    same rectangle. text_left is the glyph paint x = editable_left -
    //    hl_pad (the renderers pass anchor_x = text_left + flag_pad_x_px(); prefix-
    //    bearing editors have their editable text begin past the prefix, and
    //    the fill still covers exactly the editable glyph run, which is what
    //    the chip rect measures).
    const double chip_text_left = editable_left - s.hl_pad;
    const GuiRect fr =
        flag_chip_rect(chip_text_left, s.text.length(), s.baseline_y);
    if (fr.w > 0 && fr.h > 0) {
        cairo_save(cr);
        cairo_set_source_rgb(cr, s.fill.r, s.fill.g, s.fill.b);
        cairo_rectangle(cr, fr.x, fr.y, fr.w, fr.h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // 2. Optional static prefix, drawn on the canvas to the left of the box.
    if (!s.prefix.empty()) {
        cairo_set_source_rgb(cr,
            s.text_color.r, s.text_color.g, s.text_color.b);
        cairo_move_to(cr, s.anchor_x, s.baseline_y);
        cairo_show_text(cr, s.prefix.c_str());
    }

    // 3. Editable text.
    cairo_set_source_rgb(cr,
        s.text_color.r, s.text_color.g, s.text_color.b);
    cairo_move_to(cr, editable_left, s.baseline_y);
    cairo_show_text(cr, s.text.c_str());

    // 4. Selection swap: fill the selected range with text_color, repaint
    //    the selected substring in the fill color for contrast. The highlight
    //    band is the integer-snapped glyph ink band (band_y0 / band_h) with
    //    AA off, distinct from the full-slot step-1 fill; the horizontal
    //    extent is snapped too (hx0 / hw). hi_x / hi_w (the exact glyph-run
    //    extent from monospace arithmetic) still position the antialiased
    //    substring repaint.
    if (s.has_selection) {
        const double adv  = monospace_advance();
        const double hi_x = editable_left + s.selection_start * adv;
        const double hi_w = (s.selection_end - s.selection_start) * adv;
        const int hx0 = static_cast<int>(std::lround(hi_x));
        const int hx1 = static_cast<int>(std::lround(hi_x + hi_w));
        const int hw  = (hx1 > hx0) ? (hx1 - hx0) : 1;
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr,
            s.text_color.r, s.text_color.g, s.text_color.b);
        cairo_rectangle(cr, hx0, band_y0, hw, band_h);
        cairo_fill(cr);
        cairo_restore(cr);
        cairo_set_source_rgb(cr, s.fill.r, s.fill.g, s.fill.b);
        cairo_move_to(cr, hi_x, s.baseline_y);
        cairo_show_text(cr,
            s.text.substr(static_cast<size_t>(s.selection_start),
                          static_cast<size_t>(s.selection_end -
                                              s.selection_start))
                .c_str());
    }

    // 5. Cursor (blink-gated): a crisp one-pixel-wide integer rectangle, AA
    //    off, spanning the integer-snapped glyph ink band (band_y0 / band_h),
    //    not the full step-1 slot. cur_col is the rounded column; the former
    //    round(x)+0.5 half-pixel was a stroke-aliasing device, unneeded for a
    //    filled integer rectangle.
    if (s.cursor_visible) {
        const double cursor_x_offset = s.cursor_pos * monospace_advance();
        // An integer one-pixel rectangle at cur_col occupies exactly the
        // cursor column with AA off; the former round(x)+0.5 half-pixel was a
        // stroke-aliasing device and is no longer needed.
        const int cur_col =
            static_cast<int>(std::round(editable_left + cursor_x_offset));
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr,
            s.text_color.r, s.text_color.g, s.text_color.b);
        cairo_rectangle(cr, cur_col, band_y0, 1, band_h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    cairo_restore(cr);
}

namespace {

// Shared greedy-pack iteration used by both render_flags and
// compute_flag_hit_rects, and their phase-reset analogues. Invokes
// `emit(i, text_left, baseline_y, text)` for each flag that survives
// elision, in left-to-right order. `text_left` is snapped to the
// marker's integer pixel column so the flag's left edge coincides with the
// marker/playhead column. `get_flag_text(i)` returns the marker's flag
// payload; an empty return is the "this marker has no visible flag" signal.
// The chip advance is the cached monospace arithmetic (glyph count times
// monospace_advance()), not a per-flag cairo_text_extents — for the ASCII
// monospace chip strings the two are equal by construction, and the
// arithmetic needs no cairo context, which is what lets every flag path run
// without one.
template <typename MarkerVec, typename FlagTextFn, typename Emit>
void iterate_visible_flags_impl(
    GuiRect top_strip_area,
    int waveform_width,
    const MarkerVec& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    const std::vector<WarpFrameMapSegment>* warp_frame_map,
    const DragOverlay* drag_overlay,
    FlagTextFn&& get_flag_text,
    Emit&& emit) {
    const double span = static_cast<double>(viewport_end_sample -
                                            viewport_start_sample);
    // Map columns against the EFFECTIVE waveform width, not the strip's own
    // full width, so a chip shares the marker stem's samples-per-pixel and
    // stays column-aligned with it at every window width (they diverge only
    // when the two widths differ — a non-multiple-of-16 window, where the
    // waveform floors to an effective width and leaves an inert right gutter;
    // at 1920/2560/3840 the two widths are equal and this is a no-op). The
    // strip rect still supplies the chip's x origin and vertical placement.
    const double samples_per_pixel =
        span / static_cast<double>(waveform_width);
    if (samples_per_pixel <= 0.0) return;

    // Place the rect's bottom edge exactly at the flag chip bottom (the
    // single source of truth shared with the stem renderers). The strip
    // bottom is the waveform area top, since the strips are contiguous, so
    // flag_chip_bottom_y reads off that boundary. The box
    // is monospace_row_h() tall and the baseline sits
    // monospace_row_baseline_offset() below its top, so solving for the box
    // bottom (= baseline_y - baseline_off + row_h) at flag_chip_bottom_y gives
    // this baseline. The chip fills its full lower-row slot rather than the
    // tight glyph extent.
    const GuiRect waveform_area_for_chip{
        top_strip_area.x,
        top_strip_area.y + top_strip_area.h,
        top_strip_area.w,
        0};
    const double baseline_y =
        flag_chip_bottom_y(waveform_area_for_chip, ChipRow::Lower)
      - static_cast<double>(monospace_row_h())
      + monospace_row_baseline_offset();

    // Candidates iterate in VISUAL x order, not store order. During an
    // Alt+drag the store is frozen (positions come from the DragOverlay),
    // so once the dragged chip crosses a neighbor the store walk's
    // ascending-x assumption is false and the greedy pack below would keep
    // eliding the wrong chip — the dragged marker's flag vanished until
    // drop. Collect the visible candidates with their overlay-effective
    // paint positions and stable-sort by position; std::stable_sort over
    // the ascending store indices makes the store index the tiebreaker for
    // exactly-equal positions, so elision stays deterministic. At rest,
    // store order equals x order and the sort is a no-op reorder.
    struct FlagCandidate {
        int    i;
        double ms;
    };
    std::vector<FlagCandidate> candidates;
    candidates.reserve(markers.size());
    for (size_t i = 0; i < markers.size(); ++i) {
        const auto& m = markers[i];
        // Effective time: drag overlay > live store. The warp_frame_map
        // supplied is the display cache's target map (stable for the drag's
        // lifetime), the matching coordinate system for target-view forward
        // translation.
        const double eff_time = drag_overlay
            ? drag_overlay->effective_time(
                  static_cast<int>(i), m.time_frame)
            : m.time_frame;
        // Translate per-marker source-frame to the displayed axis (map in
        // target view, identity otherwise). Pack/elision walk
        // left-to-right against post-translation positions.
        const double ms =
            frame_to_paint_sample(eff_time, warp_frame_map);
        if (ms < static_cast<double>(viewport_start_sample)) continue;
        if (ms >= static_cast<double>(viewport_end_sample)) continue;
        candidates.push_back({static_cast<int>(i), ms});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const FlagCandidate& a, const FlagCandidate& b) {
                         return a.ms < b.ms;
                     });

    double rightmost_right_edge = -1e18;

    for (const FlagCandidate& cand : candidates) {
        const int    i  = cand.i;
        const double ms = cand.ms;

        const double x_raw =
            (ms - static_cast<double>(viewport_start_sample)) /
            samples_per_pixel;
        const double text_left =
            static_cast<double>(top_strip_area.x) + std::nearbyint(x_raw);
        // Elide only on genuine overlap: a candidate is dropped only when its
        // chip left edge (text_left - flag_pad_x_px()) would fall left of the
        // previous chip's right edge. Adjacent chips may share an edge (touch)
        // without being elided — there is no inter-chip gutter. Reintroducing
        // one is a single added term on the right-hand side here.
        if (text_left < rightmost_right_edge + flag_pad_x_px()) {
            continue;
        }

        const std::string text = get_flag_text(i);
        if (text.empty()) continue;

        const double x_advance =
            static_cast<double>(text.length()) * monospace_advance();

        emit(i, text_left, baseline_y, text);
        rightmost_right_edge = text_left + x_advance + flag_pad_x_px();
    }
}

// Paint body shared between render_flags' paint_one lambda and
// render_one_editor_flag. Inputs (i, text_left, baseline_y, text, ext)
// match the iterate_visible_flags_impl emit signature; the remaining
// args carry through what was lambda-captured before.
void paint_one_flag_with_overlay(
    cairo_t* cr,
    int i,
    double text_left,
    double baseline_y,
    const std::string& text,
    const std::set<int>& selected_set,
    const FlagEditorOverlay& editor,
    double hl_pad) {
    const bool is_selected = selected_set.count(i) > 0;
    const bool is_editing    = (i == editor.marker_index);
    const bool is_parse_fail = is_editing && editor.is_red;

    const std::string draw_text = is_editing ? editor.pending : text;

    // Fill table: parse-fail > selected > default(kMarker).
    // Trim membership no longer dims the chip.
    GuiColor fill_col;
    if (is_parse_fail)      fill_col = kAccent;
    else if (is_selected)   fill_col = kSelected;
    else                    fill_col = kMarker;

    EditorTextBox box;
    box.anchor_x        = text_left + hl_pad;
    box.baseline_y      = baseline_y;
    box.text            = draw_text;
    box.hl_pad          = hl_pad;
    box.fill            = fill_col;
    box.text_color      = kText;
    box.has_selection   = is_editing && editor.has_selection;
    box.selection_start = editor.selection_start;
    box.selection_end   = editor.selection_end;
    box.cursor_visible  = is_editing && editor.cursor_visible;
    box.cursor_pos      = editor.cursor_pos;
    render_editor_text_box(cr, box);
}

} // namespace

void render_flags(cairo_t* cr,
                  GuiRect top_strip_area,
                  int waveform_width,
                  const std::vector<GuiWarpMarker>& markers,
                  long long viewport_start_sample,
                  long long viewport_end_sample,
                  int sample_rate,
                  double font_size,
                  const std::set<int>& selected_set,
                  const FlagEditorOverlay& editor,
                  const std::vector<WarpFrameMapSegment>* warp_frame_map,
                  const DragOverlay* drag_overlay,
                  bool iteration_on) {
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (sample_rate <= 0) return;

    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);

    const double hl_pad = flag_pad_x_px();

    // Collect emit args during the left-to-right iterate pass,
    // then paint the collected list in REVERSE order. The pack rule inside
    // iterate_visible_flags_impl still elides right-of-collision flags
    // (leftmost wins), and reverse paint order makes the leftmost flag's
    // pixels land on top — so when an editor's pending text grows past its
    // original flag width into the right neighbor's territory, the
    // editor's bg-fill and text occlude the right neighbor instead of
    // being overwritten by it. In all static (no-edit) states the bg-fills
    // are kBackground and text rects don't overlap, so reverse paint order
    // produces pixels identical to forward order.
    struct FlagEmit {
        int                  i;
        double               text_left;
        double               baseline_y;
        std::string          text;
    };
    std::vector<FlagEmit> emits;
    iterate_visible_flags_impl(top_strip_area, waveform_width, markers,
                               viewport_start_sample, viewport_end_sample,
                               warp_frame_map, drag_overlay,
        [&](int i) {
            return flag_text_iter(markers, i, iteration_on);
        },
        [&](int i, double text_left, double baseline_y,
            const std::string& text) {
            // Skip-guard. The flag-cache rebuild passes the
            // FlagPayload-editor target through editor.marker_index so
            // this branch fires and the cache leaves a transparent hole
            // over the editor target's pixel column; the live editor
            // render owns those pixels via render_one_editor_flag.
            // Defensive for callers without an editor target as well — when
            // editor.marker_index == -1 (the default), the guard never
            // fires, and every visible flag paints into the cache.
            if (editor.marker_index == i) return;
            emits.push_back({i, text_left, baseline_y, text});
        });

    for (auto it = emits.rbegin(); it != emits.rend(); ++it) {
        paint_one_flag_with_overlay(cr, it->i, it->text_left, it->baseline_y,
                                    it->text, selected_set, editor,
                                    hl_pad);
    }

    cairo_restore(cr);
}

void render_one_editor_flag(
    cairo_t* cr,
    GuiRect top_strip_area,
    int waveform_width,
    const std::vector<GuiWarpMarker>& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::set<int>& selected_set,
    const FlagEditorOverlay& editor,
    const std::vector<WarpFrameMapSegment>* warp_frame_map,
    const DragOverlay* drag_overlay,
    bool iteration_on) {
    if (editor.marker_index < 0) return;
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (sample_rate <= 0) return;

    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);

    const double hl_pad = flag_pad_x_px();

    iterate_visible_flags_impl(top_strip_area, waveform_width, markers,
                               viewport_start_sample, viewport_end_sample,
                               warp_frame_map, drag_overlay,
        [&](int i) {
            return flag_text_iter(markers, i, iteration_on);
        },
        [&](int i, double text_left, double baseline_y,
            const std::string& text) {
            if (i != editor.marker_index) return;
            paint_one_flag_with_overlay(cr, i, text_left, baseline_y,
                                        text, selected_set, editor,
                                        hl_pad);
        });

    cairo_restore(cr);
}

namespace {

template <typename MarkerVec, typename FlagTextFn>
std::vector<FlagHitRect> compute_flag_hit_rects_impl(
    GuiRect top_strip_area,
    int waveform_width,
    const MarkerVec& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    const std::vector<WarpFrameMapSegment>* warp_frame_map,
    const DragOverlay* drag_overlay,
    FlagTextFn&& get_flag_text) {
    std::vector<FlagHitRect> out;
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return out;
    if (viewport_end_sample <= viewport_start_sample) return out;
    if (sample_rate <= 0) return out;

    // Mirror render_flags: uniform y/height for the hit rect so clicks
    // register consistently across flag types. The rect comes from the shared
    // flag_chip_rect helper, the same one render_editor_text_box fills, so the
    // painted chip and this hit rect are the same rectangle by construction.
    iterate_visible_flags_impl(top_strip_area, waveform_width, markers,
                               viewport_start_sample, viewport_end_sample,
                               warp_frame_map, drag_overlay,
        std::forward<FlagTextFn>(get_flag_text),
        [&](int i, double text_left, double baseline_y,
            const std::string& text) {
            const GuiRect cr_rect =
                flag_chip_rect(text_left, text.length(), baseline_y);
            FlagHitRect r;
            r.marker_index = i;
            r.x = cr_rect.x;
            r.y = cr_rect.y;
            r.w = cr_rect.w;
            r.h = cr_rect.h;
            out.push_back(r);
        });

    return out;
}

} // namespace

std::vector<FlagHitRect> compute_flag_hit_rects(
    GuiRect top_strip_area,
    int waveform_width,
    const std::vector<GuiWarpMarker>& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    const std::vector<WarpFrameMapSegment>* warp_frame_map,
    const DragOverlay* drag_overlay,
    bool iteration_on) {
    return compute_flag_hit_rects_impl(top_strip_area, waveform_width, markers,
        viewport_start_sample, viewport_end_sample,
        sample_rate, warp_frame_map, drag_overlay,
        [&](int i) {
            return flag_text_iter(markers, i, iteration_on);
        });
}

// ---------- Phase reset marker rendering ----------

namespace {

std::string phase_reset_flag_text(const GuiPhaseResetMarker&) {
    // The phase-reset chip is an invariable single `p`: heap is the sole
    // engine, so there is no peak/heap/pass phase model to distinguish. One
    // cell wide, so the shared glyph-count width math yields a 1-cell chip.
    return "p";
}

// Plain painter for a phase-reset flag. Two states only: selected fill
// kSelected, otherwise default fill kMarker. There is no per-flag editor,
// so no pending/cursor/selection/parse-fail handling.
void paint_one_phase_reset_flag(
    cairo_t* cr,
    int i,
    double text_left,
    double baseline_y,
    const std::string& text,
    const std::set<int>& selected_set,
    double hl_pad) {
    const bool is_selected = selected_set.count(i) > 0;

    EditorTextBox box;
    box.anchor_x        = text_left + hl_pad;
    box.baseline_y      = baseline_y;
    box.text            = text;
    box.hl_pad          = hl_pad;
    box.fill            = is_selected ? kSelected : kMarker;
    box.text_color      = kText;
    render_editor_text_box(cr, box);
}

} // namespace

void render_phaseresetmarkers(cairo_t* cr,
                              GuiRect waveform_area,
                              const std::vector<GuiPhaseResetMarker>& phase_resets,
                              long long viewport_start_sample,
                              long long viewport_end_sample,
                              int sample_rate,
                              const std::set<int>& selected_set,
                              const std::vector<WarpFrameMapSegment>* warp_frame_map,
                              const DragOverlay* drag_overlay,
                              cairo_surface_t* ink_plate) {
    render_marker_stems_impl(
        cr, waveform_area, phase_resets,
        viewport_start_sample, viewport_end_sample,
        sample_rate, selected_set, warp_frame_map,
        drag_overlay,
        [&](int i) {
            return phase_resets[i].disabled;
        },
        ink_plate);
}

void render_phase_reset_flags(cairo_t* cr,
                            GuiRect top_strip_area,
                            int waveform_width,
                            const std::vector<GuiPhaseResetMarker>& phase_resets,
                            long long viewport_start_sample,
                            long long viewport_end_sample,
                            int sample_rate,
                            double font_size,
                            const std::set<int>& selected_set,
                            const std::vector<WarpFrameMapSegment>* warp_frame_map,
                            const DragOverlay* drag_overlay) {
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (sample_rate <= 0) return;

    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);

    const double hl_pad = flag_pad_x_px();

    // Collect-then-reverse-paint, mirroring render_flags. With no
    // per-flag editor every visible flag paints straight into the cache.
    struct PhaseResetEmit {
        int                  i;
        double               text_left;
        double               baseline_y;
        std::string          text;
    };
    std::vector<PhaseResetEmit> emits;
    iterate_visible_flags_impl(top_strip_area, waveform_width, phase_resets,
                               viewport_start_sample, viewport_end_sample,
                               warp_frame_map, drag_overlay,
        [&](int i) {
            return phase_reset_flag_text(phase_resets[i]);
        },
        [&](int i, double text_left, double baseline_y,
            const std::string& text) {
            emits.push_back({i, text_left, baseline_y, text});
        });

    for (auto it = emits.rbegin(); it != emits.rend(); ++it) {
        paint_one_phase_reset_flag(
            cr, it->i, it->text_left, it->baseline_y, it->text,
            selected_set, hl_pad);
    }

    cairo_restore(cr);
}

std::vector<FlagHitRect> compute_phase_reset_flag_hit_rects(
    GuiRect top_strip_area,
    int waveform_width,
    const std::vector<GuiPhaseResetMarker>& phase_resets,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    const std::vector<WarpFrameMapSegment>* warp_frame_map,
    const DragOverlay* drag_overlay) {
    return compute_flag_hit_rects_impl(top_strip_area, waveform_width, phase_resets,
        viewport_start_sample, viewport_end_sample,
        sample_rate, warp_frame_map, drag_overlay,
        [&](int i) {
            return phase_reset_flag_text(phase_resets[i]);
        });
}

namespace {
    // Current GUI font size, in points. Set by set_gui_font_size_pt from
    // the two application points (file load, the settings-editor font_size
    // commit); every derived pixel quantity (text px size, scale
    // factor, scaled pads, triangle height) reads it through the accessors
    // below.
    double g_font_size_pt = kDefaultFontSizePt;
    double g_advance = 0.0;
    int    g_row_h            = kRowHFallbackPx;
    double g_row_baseline_off = kRowBaselineOffFallbackPx;
    // Pixel size the grid metrics were last measured at; negative until the
    // first measure. init_monospace_grid_metrics re-measures whenever this
    // differs from the current flag_font_size_px(), so a font_size change
    // picks up fresh metrics on the next frame.
    double g_measured_font_px = -1.0;
    // Cached playhead triangle mask (A8, 2H-1 x H) and the H it was built
    // at; regenerated by playhead_triangle_mask() when H changes.
    cairo_surface_t* g_playhead_triangle   = nullptr;
    int              g_playhead_triangle_h = 0;
} // namespace

void   set_gui_font_size_pt(double pt) { g_font_size_pt = pt; }
double gui_font_scale()    { return g_font_size_pt / kDefaultFontSizePt; }
double flag_font_size_px() { return g_font_size_pt * 96.0 / 72.0; }

// Build (or return the cached) playhead triangle mask for the current H.
// The mask is the exact code equivalent of the retired 19x10 PNG asset,
// generalized to H: row y (0-based from the top) spans columns y through
// W-1-y inclusive, so each row is two pixels narrower than the one above,
// from full width W = 2H-1 down to a single tip pixel at column (W-1)/2.
// Alpha is strictly 0 or 255 — the A8 buffer is filled directly, no
// rasterizer, no partial coverage. At scale 1 (H = 10, W = 19) this
// reproduces the retired asset bit-for-bit.
cairo_surface_t* playhead_triangle_mask() {
    const int h = playhead_triangle_h_px();
    if (g_playhead_triangle && g_playhead_triangle_h == h) {
        return g_playhead_triangle;
    }
    if (g_playhead_triangle) {
        cairo_surface_destroy(g_playhead_triangle);
        g_playhead_triangle = nullptr;
    }
    const int w = 2 * h - 1;
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_A8, w, h);
    unsigned char* data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    // The surface is created zeroed; only the opaque triangle interior is
    // written.
    for (int y = 0; y < h; ++y) {
        for (int x = y; x <= w - 1 - y; ++x) {
            data[y * stride + x] = 0xFF;
        }
    }
    cairo_surface_mark_dirty(s);
    g_playhead_triangle   = s;
    g_playhead_triangle_h = h;
    return g_playhead_triangle;
}

double monospace_advance() { return g_advance; }
int    monospace_row_h()   { return g_row_h; }
double monospace_row_baseline_offset() { return g_row_baseline_off; }

void init_monospace_grid_metrics(cairo_t* cr) {
    const double px = flag_font_size_px();
    if (g_measured_font_px == px) return;
    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, px);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, "M", &ext);
    g_advance = ext.x_advance;
    cairo_font_extents_t fe;
    cairo_font_extents(cr, &fe);
    const double font_height = fe.ascent + fe.descent;
    g_row_h = static_cast<int>(std::nearbyint(
        font_height + 2.0 * flag_pad_y_px()));
    g_row_baseline_off = flag_pad_y_px() + fe.ascent;
    cairo_restore(cr);
    g_measured_font_px = px;
}

double flag_pending_text_left_x(
    const AppState& app, const GuiAudio& audio,
    int marker_idx)
{
    const auto& mv = app.warpmarkers.markers();
    if (marker_idx < 0 ||
        marker_idx >= static_cast<int>(mv.size())) return -1.0;
    // Effective waveform width (largest multiple of 16 not exceeding the
    // window width), matching render_one_editor_flag / compute_flag_hit_rects:
    // an off-view marker past the effective right edge sits in the inert
    // gutter and must read as not-visible, not as gutter geometry.
    const GuiRect area = waveform_area(app);
    if (area.w <= 0) return -1.0;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return -1.0;
    const int64_t vp_start = app.viewport_start_sample;
    const int64_t vp_end = vp_start +
        static_cast<int64_t>(std::nearbyint(spp * area.w));
    // Target view: forward-translate the marker's source-frame through a
    // freshly-built target-view warp_frame_map so the visible-range check and
    // x-position math match where render_flags actually paints the flag.
    // Empty / null warp_frame_map falls through to identity, matching the
    // render-side helpers' convention. Not reachable mid-drag (begin_drag
    // clears the editor; the click handler exits before any drag begins),
    // so a fresh build here is correct — it returns the same target map the
    // display cache would.
    const int64_t src_sample = mv[marker_idx].time_frame;
    double ms = static_cast<double>(src_sample);
    if (app.active_audio_view == 'T') {
        const auto& target_warp_frame_map = target_view_warp_frame_map_cached(
            app, audio.sample_rate(),
            static_cast<long>(audio.total_frames())).warp_frame_map;
        if (!target_warp_frame_map.empty()) {
            const size_t src_frame = (src_sample < 0)
                ? static_cast<size_t>(0)
                : static_cast<size_t>(src_sample);
            ms = std::nearbyint(map_source_to_target(src_frame, target_warp_frame_map));
        }
    }
    if (ms <  static_cast<double>(vp_start)) return -1.0;
    if (ms >= static_cast<double>(vp_end))   return -1.0;
    const double samples_per_pixel =
        static_cast<double>(vp_end - vp_start) /
        static_cast<double>(area.w);
    const double x_raw =
        (ms - static_cast<double>(vp_start)) / samples_per_pixel;
    const double text_left =
        static_cast<double>(area.x) + std::nearbyint(x_raw);
    return text_left + flag_pad_x_px();
}
