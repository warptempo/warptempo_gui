#include "render.h"
#include "app_state.h"
#include "audio.h"
#include "gui_display_context.h"
#include "time_format.h"
#include "value_format.h"
#include "warp_frame_map_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// kFlagBottomLiftPx now lives in render.h so the strip lane geometry in
// main.cpp and the stem blit in paint_handler.cpp reference the same value.

// playhead_half_px() is the half-width (H - 1) of the code-generated
// inverted-triangle playhead mask (2H-1 wide, tip at column H-1); it lives
// in render.h as a single inline accessor shared by this TU's cull and
// main.cpp's invalidation.

namespace {

// Flag text mirrors the canonical line's PAYLOAD (post-pipe). All
// metadata (b=/e=/#) is invisible in the rect; the `|` separator sits to
// the left of the rect, anchoring it to the marker column. Color conveys
// selection (kSelected); a disabled marker still paints its flag, half-triangle,
// and (for the last-selected marker) its stem, dimmed under kDisabledMarkerAlpha.
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

// Paints the LAST-SELECTED marker's stem, used by render_markers and
// render_phaseresetmarkers. The stem is the last-selected marker's emphasis: it
// paints for exactly `last_selected` (when valid, visible, and in this column's
// list) and no other marker — the blue flag highlight still marks the whole
// selection, but only its anchor grows a stem. The only meaningful difference
// between the two callers is how visual-disability is computed: warp markers walk
// the label_ref cascade via `effective_disabled`, phase resets read `disabled`
// directly. That asymmetry is exposed here as a predicate `is_disabled(i)`;
// everything else (viewport math, drag overlay, target translation, integer-pixel
// snap) is identical for both marker kinds.
//
// The stem spans the WAVEFORM AREA ONLY (top at waveform_area.y, down to the
// bottom) with no strip overhang — the flag+triangle structure above (a
// separate cache surface) supplies the visual connection, its triangle tip
// touching the stem's start. A DISABLED last-selected marker's stem is drawn as
// a PLAIN dimmed line under kDisabledMarkerAlpha: the marker's triangle lives in
// the flag structure now, so stem and triangle no longer share pixels and there
// is no see-through class to guard against — the group/skip composite retired.
template <typename MarkerVec, typename IsVisuallyDisabled>
void render_marker_stems_impl(
    cairo_t* cr,
    GuiRect waveform_area,
    const MarkerVec& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    int last_selected,
    const std::vector<WarpFrameMapSegment>* warp_frame_map,
    const DragOverlay* drag_overlay,
    IsVisuallyDisabled&& is_disabled,
    cairo_surface_t* ink_plate) {
    if (waveform_area.w <= 0 || waveform_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (sample_rate <= 0) return;
    if (last_selected < 0 ||
        last_selected >= static_cast<int>(markers.size())) return;

    const double span = static_cast<double>(viewport_end_sample -
                                            viewport_start_sample);
    const double samples_per_pixel = span / static_cast<double>(waveform_area.w);
    if (samples_per_pixel <= 0.0) return;

    // Stem rises at the marker's pixel column and runs the full waveform area,
    // top at waveform_area.y (where the flag structure's triangle tip touches)
    // down to the waveform bottom.
    const double y_stem_top = static_cast<double>(waveform_area.y);
    const double y1 = static_cast<double>(waveform_area.y + waveform_area.h);

    const int i = last_selected;
    const auto& m = markers[i];
    // Effective time: when a drag is active and this marker is in the overlay,
    // read its proposed time from the overlay instead of the live store. The
    // warp_frame_map passed in is the display cache's target map (stable for the
    // drag's lifetime), the matching coordinate system for forward translation.
    const double eff_time = drag_overlay
        ? drag_overlay->effective_time(i, m.time_frame)
        : m.time_frame;
    // Translate per-marker source-frame to the displayed axis: map in target
    // view (warp_frame_map non-null/non-empty), identity otherwise.
    const double ms = frame_to_paint_sample(eff_time, warp_frame_map);
    if (ms < static_cast<double>(viewport_start_sample)) return;
    if (ms >= static_cast<double>(viewport_end_sample)) return;

    const double x_raw =
        (ms - static_cast<double>(viewport_start_sample)) / samples_per_pixel;
    const int icol = static_cast<int>(std::nearbyint(x_raw));
    const double x_px = waveform_area.x + icol + 0.5;
    // The last-selected marker is by construction a member of the selection, so
    // its stem color is always kSelected; a disabled one dims plainly.
    const bool disabled = is_disabled(i);

    cairo_save(cr);
    cairo_set_line_width(cr, 1.0);
    if (ink_plate) cairo_surface_flush(ink_plate);

    if (disabled)
        cairo_set_source_rgba(cr, kSelected.r, kSelected.g, kSelected.b,
                              kDisabledMarkerAlpha);
    else
        cairo_set_source_rgb(cr, kSelected.r, kSelected.g, kSelected.b);
    cairo_move_to(cr, x_px, y_stem_top);
    cairo_line_to(cr, x_px, y1);
    cairo_stroke(cr);
    // The dark ink notch is painted at full opacity over the stem.
    fill_column_ink_runs(cr, waveform_area.x, waveform_area.y,
                         waveform_area.h, ink_plate, icol);

    cairo_restore(cr);
}

// Fills and outlines ONE marker/phase-reset/trim flag SHAPE, centered on
// `center_x` (the item's pixel column). The shape is the fixed-width rectangle
// in the flag lane [rx, flag_top, flag_w, rect_h] plus (when `with_triangle`)
// the tip-down triangle in the triangle lane directly beneath it, tip on the
// column at `tip_y` (= the waveform top edge). The two are ONE shape: the union
// is filled in `fill`, and a 1px `outline` runs on the TRUE OUTSIDE only — rect
// left/top/right, then from the rect's bottom corners INWARD to the triangle's
// top corners and down the two slopes to the tip, so there is NO horizontal seam
// where the rect meets the triangle (the 1px inset per side makes the transition
// continuous). Axis-aligned edges use the +0.5 half-pixel convention for crisp
// 1px lines; the two diagonal slopes stroke antialiased. Trim chips pass
// with_triangle=false — a plain rectangle, no triangle (Ableton's loop bounds
// carry none). `alpha` < 1 dims the whole shape as one cairo group (the disabled
// cue). The triangle fill reuses the shared playhead_triangle_mask() so a flag's
// triangle and the playhead's coincide bit-for-bit when the cursor sits on it.
void paint_flag_shape(cairo_t* cr, double center_x,
                      double flag_top_d, double tri_top_d, double tip_y_d,
                      GuiColor fill, GuiColor outline,
                      bool with_triangle, double alpha) {
    const int flag_w = flag_lane_w_px();
    const int tri_h  = playhead_triangle_h_px();
    const int tri_w  = 2 * tri_h - 1;

    const int cx     = static_cast<int>(std::round(center_x));
    const int rx     = cx - flag_w / 2;                 // rect left
    const int rw     = flag_w;
    const int ry     = static_cast<int>(std::round(flag_top_d));
    const int rb     = static_cast<int>(std::round(tri_top_d)); // rect bottom = tri top
    const int tbot   = static_cast<int>(std::round(tip_y_d));   // triangle lane bottom
    const int tri_left  = cx - tri_w / 2;
    const int tri_right = tri_left + tri_w;             // exclusive right edge

    cairo_save(cr);
    const bool dim = alpha < 1.0;
    if (dim) cairo_push_group(cr);

    // Fill: rectangle (crisp, AA off) then, if present, the triangle via the
    // shared tip-down mask stamped left-edge on tri_left, tip on the column.
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgb(cr, fill.r, fill.g, fill.b);
    cairo_rectangle(cr, rx, ry, rw, rb - ry);
    cairo_fill(cr);
    cairo_restore(cr);
    if (with_triangle) {
        cairo_set_source_rgb(cr, fill.r, fill.g, fill.b);
        cairo_mask_surface(cr, playhead_triangle_mask(),
                           static_cast<double>(tri_left),
                           static_cast<double>(rb));
    }

    // Outline: the true outside as one closed polygon. Rect edges + (for a
    // triangle-bearing flag) the 1px inward steps at the rect bottom corners
    // and the two slopes to the tip. No horizontal seam across the full width.
    cairo_set_source_rgb(cr, outline.r, outline.g, outline.b);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, rx + 0.5,           ry + 0.5);          // top-left
    cairo_line_to(cr, rx + rw - 0.5,      ry + 0.5);          // top edge
    cairo_line_to(cr, rx + rw - 0.5,      rb - 0.5);          // right edge to rect bottom
    if (with_triangle) {
        cairo_line_to(cr, tri_right - 0.5, rb - 0.5);         // step inward
        cairo_line_to(cr, cx + 0.5,        tbot - 0.5);       // right slope to tip
        cairo_line_to(cr, tri_left + 0.5,  rb - 0.5);         // left slope up
    }
    cairo_line_to(cr, rx + 0.5,           rb - 0.5);          // rect bottom-left
    cairo_close_path(cr);                                     // up the left edge
    cairo_stroke(cr);

    if (dim) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, alpha);
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
    // division places that tip column at `area.x + col`. The triangle sits in
    // the TRIANGLE LANE directly above the waveform (dst_y = area.y - H): its
    // top row is the lane top and its tip (bottom row) lands one pixel above
    // the waveform top edge, where the marker/trim stems begin. This is the
    // SAME mask, same width, and same centered column as every marker/trim flag
    // triangle, so when the cursor sits on a marker the two coincide
    // bit-for-bit. Skipped for the scanner call (draw_triangle=false): the
    // triangle belongs to the cursor exclusively under the split-playhead
    // model. The clip band is the triangle lane; the vertical line above spans
    // only the waveform area, so the two never overlap.
    if (draw_triangle) {
        cairo_surface_t* triangle_surface = playhead_triangle_mask();
        const int img_w = cairo_image_surface_get_width(triangle_surface);
        const int img_h = cairo_image_surface_get_height(triangle_surface);
        const double dst_x = static_cast<double>(area.x + col - img_w / 2);
        const double dst_y = static_cast<double>(area.y - img_h);
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
                    int last_selected,
                    const std::vector<WarpFrameMapSegment>* warp_frame_map,
                    const DragOverlay* drag_overlay,
                    cairo_surface_t* ink_plate) {
    // Only the last-selected marker's stem lives on the waveform (stem-cache)
    // surface now. Every marker's frame tick (its flag triangle) moved into the
    // flag structure (flag cache, top strip), so the stem cache paints just the
    // one stem. A disabled last-selected marker's stem dims plainly.
    const auto is_disabled = [&](int i) { return effective_disabled(markers, i); };
    render_marker_stems_impl(
        cr, waveform_area, markers,
        viewport_start_sample, viewport_end_sample,
        sample_rate, last_selected, warp_frame_map,
        drag_overlay, is_disabled, ink_plate);
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

    // Same stem geometry as render_marker_stems_impl: the trim stem spans the
    // waveform area, top at waveform_area.y (where its b/e chip's structure ends
    // above) down to the waveform bottom.
    const double y_stem_top = static_cast<double>(waveform_area.y);
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

    // Trim chip lane (top-strip lane 1): directly below the zoom lane, so its
    // top is one row height inward, and its height is the flag height. Screen
    // and top-strip-local coords coincide (the top strip sits at y=0), so this
    // is exactly top_upper_row_area(app), the band the bridge hit test gates on.
    const int chip_w    = flag_lane_w_px();
    const int chip_half = chip_w / 2;
    const int chip_top  = top_strip_area.y + monospace_row_h();
    const int chip_h    = flag_lane_h_px();
    const int chip_bottom = chip_top + chip_h;

    cairo_save(cr);

    // With both bounds set, a wash band fills the trim-chip-lane span BETWEEN
    // the two chip columns — the visual affordance of the pair (bridge) drag's
    // inter-chip grab region (the span strictly between the two bound columns,
    // exactly what route_trim_chip_press tests). The wash is the shared overlay
    // pair (kOverlay / kOverlayAlpha, the same the phase reset overlay uses),
    // plus a 1px ring at ring strength (kOverlay at kOverlayOutlineAlpha): the
    // top/bottom edges run along the lane's own rows and the two vertical edges
    // sit BESIDE the chips (each visible chip's inner edge). Painted BEFORE the
    // chip rectangles so the chips overpaint the band's ends and it reads as
    // spanning between them. Both columns are computed unconditionally (a chip's
    // own viewport cull must not suppress the band when a chip is offscreen).
    if (has_begin && has_end && waveform_area.w > 0) {
        auto col_of = [&](int64_t frame) {
            const double x_raw =
                (static_cast<double>(frame) -
                 static_cast<double>(viewport_start_sample)) / samples_per_pixel;
            return static_cast<int>(std::nearbyint(x_raw));
        };
        // Positional min/max: mid-gesture the displayed domain can invert
        // begin/end, so "left"/"right" are BY COLUMN. col_l / col_r are the
        // UNCLAMPED center columns; lo/hi are clamped into the mapped waveform
        // width for the visible wash span.
        const int wmax = waveform_area.w - 1;
        const int col_l = std::min(col_of(trim.begin), col_of(trim.end));
        const int col_r = std::max(col_of(trim.begin), col_of(trim.end));
        const int lo = std::clamp(col_l, 0, wmax);
        const int hi = std::clamp(col_r, 0, wmax);
        if (hi > lo) {
            cairo_set_source_rgba(cr, kOverlay.r, kOverlay.g,
                                  kOverlay.b, kOverlayAlpha);
            cairo_rectangle(cr, static_cast<double>(top_strip_area.x + lo),
                            static_cast<double>(chip_top),
                            static_cast<double>(hi - lo),
                            static_cast<double>(chip_h));
            cairo_fill(cr);
            // 1px ring, AA off. Top/bottom span the clamped band; the two
            // vertical edges abut each visible chip's inner side (centered chip
            // footprint [col - chip_half, col - chip_half + chip_w)). A right
            // bound exactly 1px offscreen (col_r == wmax + 1, the EOF
            // flush-right state) still gets its right edge, like before.
            const int rx = top_strip_area.x + lo;
            const int rw = hi - lo;
            const int ry = chip_top;
            const int rh = chip_h;
            if (rw > 0 && rh > 0) {
                cairo_save(cr);
                cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
                cairo_set_source_rgba(cr, kOverlay.r, kOverlay.g, kOverlay.b,
                                      kOverlayOutlineAlpha);
                cairo_rectangle(cr, rx, ry, rw, 1);                 // top
                cairo_rectangle(cr, rx, ry + rh - 1, rw, 1);        // bottom
                // Left vertical: at the left chip's right (inner) edge, when the
                // left bound is onscreen and the edge sits left of the right
                // chip's inner edge.
                const int left_inner  = col_l - chip_half + chip_w;
                const int right_inner = col_r - chip_half;
                if (col_l >= 0 && col_l <= wmax && left_inner < right_inner - 1) {
                    const int lvx =
                        top_strip_area.x + std::clamp(left_inner, 0, wmax);
                    cairo_rectangle(cr, lvx, ry, 1, rh);            // left
                }
                // Right vertical: at the right chip's left (inner) edge minus 1,
                // when the right bound is onscreen or exactly 1px offscreen.
                if (col_r <= wmax + 1) {
                    const int rvx =
                        top_strip_area.x + std::clamp(right_inner - 1, 0, wmax);
                    cairo_rectangle(cr, rvx, ry, 1, rh);            // right
                }
                cairo_fill(cr);
                cairo_restore(cr);
            }
        }
    }

    // The b/e chips are TEXTLESS rectangles of the flag's exact width/height,
    // centered on their bound columns, no triangle (Ableton's loop bounds carry
    // none). Build the visible-bounds list sorted by painted column ascending;
    // order is by position, NOT begin/end identity (target view or an inverted
    // mid-gesture trim can reorder them).
    struct TrimChip {
        double center_x;
    };
    std::vector<TrimChip> chips;
    auto add_chip = [&](int64_t frame) {
        const double ms = static_cast<double>(frame);
        if (ms < static_cast<double>(viewport_start_sample)) return;
        if (ms >= static_cast<double>(viewport_end_sample)) return;
        const double x_raw =
            (ms - static_cast<double>(viewport_start_sample))
                / samples_per_pixel;
        const double center_x =
            static_cast<double>(top_strip_area.x) + std::nearbyint(x_raw);
        chips.push_back({center_x});
    };
    if (has_begin) add_chip(trim.begin);
    if (has_end)   add_chip(trim.end);
    std::sort(chips.begin(), chips.end(),
              [](const TrimChip& a, const TrimChip& b) {
                  return a.center_x < b.center_x;
              });

    // Overlapping chips occlude rather than elide (as the marker flags do):
    // paint the sorted list in REVERSE order so the leftmost lands on top. Trim
    // bounds are unselectable (recorded asymmetry), so there is no selected
    // pass — one reverse pass is the whole occlusion order. Each chip is a plain
    // kTrimMarker rectangle with a kTrimMarkerOutline border, no triangle
    // (with_triangle = false); the bottom argument is unused for the rectangle
    // shape.
    for (auto it = chips.rbegin(); it != chips.rend(); ++it) {
        paint_flag_shape(cr, it->center_x,
                         static_cast<double>(chip_top),
                         static_cast<double>(chip_bottom),
                         static_cast<double>(chip_bottom),
                         kTrimMarker, kTrimMarkerOutline,
                         /*with_triangle=*/false, /*alpha=*/1.0);
    }

    cairo_restore(cr);
}

void render_strip_row_ring(cairo_t* cr, const GuiRect& row, int waveform_width) {
    // Inert full-width ring around a strip row's bounding box: 1px edges in
    // kOverlay at ring strength (kOverlayOutlineAlpha), antialias off — the
    // same crisp-edge style the trim pair-drag band's ring uses, with no wash
    // fill. Spans the EFFECTIVE waveform width (x from the row's left edge),
    // not the strip's full width, so the <=15px right gutter at a
    // non-multiple-of-16 window stays outside the ring, matching every other
    // grid-aligned surface.
    if (row.w <= 0 || row.h <= 0 || waveform_width <= 0) return;
    const int rw = std::min(waveform_width, row.w);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgba(cr, kOverlay.r, kOverlay.g, kOverlay.b,
                          kOverlayOutlineAlpha);
    cairo_rectangle(cr, row.x, row.y, rw, 1);                 // top
    cairo_rectangle(cr, row.x, row.y + row.h - 1, rw, 1);     // bottom
    cairo_rectangle(cr, row.x, row.y, 1, row.h);              // left
    cairo_rectangle(cr, row.x + rw - 1, row.y, 1, row.h);     // right
    cairo_fill(cr);
    cairo_restore(cr);
}

void render_editor_text_box(cairo_t* cr, const EditorTextBox& s, double alpha) {
    cairo_save(cr);
    // When dimming (a disabled marker's chip), render the whole box into a
    // group and composite it once with `alpha` so the ring, fill, and glyphs
    // dim uniformly rather than per-element. At full opacity the group is
    // skipped and the paint path is byte-identical to the undimmed form.
    const bool dim = alpha < 1.0;
    if (dim) cairo_push_group(cr);
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
    // and the box bottom lands flush at the slot bottom. Callers (the
    // bottom-strip editors) solve baseline_y so the box bottom coincides with
    // their row rect.
    //
    // The cursor (step 5) and the selection highlight (step 4) span exactly the
    // glyph ink band (ascent-to-descent), no vertical padding. The band is
    // recovered from the two cached monospace metrics (exact inverses of how
    // init_monospace_grid_metrics built them: g_row_baseline_off = flag_pad_y_px()
    // + kChipOutlinePx + ascent, g_row_h = round(font_height + 2*flag_pad_y_px())
    // + 2*kChipOutlinePx). The round() on the row height can leak a sub-pixel
    // into the derived descent; that is cosmetically irrelevant here and saves
    // adding a new metric accessor.
    const double bg_h        = static_cast<double>(monospace_row_h());
    const double ascent      = monospace_row_baseline_offset() - flag_pad_y_px()
                             - kChipOutlinePx;
    const double font_height = bg_h - 2.0 * flag_pad_y_px()
                             - 2.0 * kChipOutlinePx;
    const double descent     = font_height - ascent;
    const double glyph_top   = s.baseline_y - ascent;
    const double glyph_h     = ascent + descent;

    // Chip rect (fill + ring) from the single source of truth (flag_chip_rect),
    // so the painted chip and the hit rect are the same rectangle. chip_text_left
    // is the chip's left edge = editable_left - hl_pad (the renderers pass
    // anchor_x = text_left + flag_glyph_inset_px(); prefix-bearing editors have
    // their editable text begin past the prefix, and the fill still covers
    // exactly the editable glyph run, which is what the chip rect measures).
    const double chip_text_left = editable_left - s.hl_pad;
    const GuiRect fr =
        flag_chip_rect(chip_text_left, s.text.length(), s.baseline_y);

    // Snap the shared glyph ink band to integer pixel rows once, so the
    // selection highlight (step 4) and the cursor (step 5) both fill crisp
    // integer-edged rectangles with antialiasing off — the same anti-aliased-
    // tip defect corrected in render_waveform. Glyph text (steps 2-3) keeps
    // antialiasing and is untouched.
    //
    // The band is CLAMPED to the fill interior [fr.y + kChipOutlinePx,
    // fr.y + fr.h - kChipOutlinePx]: with the negative pad_y the ring now
    // overlaps the band's outermost rows, so an unclamped cursor/highlight rect
    // would punch through the ring top and bottom. Clamping keeps both inside
    // the ring (the standing rule). The antialiased glyph text and the selection
    // substring repaint are NOT clamped — their extreme leading rows are blank,
    // the ring paints first, and only these filled rects could otherwise show
    // through it.
    int band_y0 = static_cast<int>(std::lround(glyph_top));
    int band_y1 = static_cast<int>(std::lround(glyph_top + glyph_h));
    const int band_lo = fr.y + kChipOutlinePx;
    const int band_hi = fr.y + fr.h - kChipOutlinePx;
    band_y0 = std::clamp(band_y0, band_lo, band_hi);
    band_y1 = std::clamp(band_y1, band_lo, band_hi);
    const int band_h  = (band_y1 > band_y0) ? (band_y1 - band_y0) : 1;

    // 1. Solid fill behind the editable region: the full rect (the outline ring)
    //    in s.outline, then the inner rect inset by kChipOutlinePx on every side
    //    in s.fill. fr already includes the ring (flag_chip_rect), so the ring is
    //    the outer kChipOutlinePx band left exposed. The glyph ink band (and thus
    //    cursor/selection) is clamped inside the ring above. The bottom-strip
    //    editors pass kBackground for both, so their ring is invisible and the
    //    box reads as light text on the strip.
    //
    //    Both fills paint with antialiasing OFF: integer-edged rects, the same
    //    crisp-edge convention as steps 4/5 and render_waveform — the default AA
    //    softened the ring edges. The surrounding cairo_save/restore brackets the
    //    AA state.
    if (fr.w > 0 && fr.h > 0) {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, s.outline.r, s.outline.g, s.outline.b);
        cairo_rectangle(cr, fr.x, fr.y, fr.w, fr.h);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, s.fill.r, s.fill.g, s.fill.b);
        cairo_rectangle(cr, fr.x + kChipOutlinePx, fr.y + kChipOutlinePx,
                        fr.w - 2 * kChipOutlinePx,
                        fr.h - 2 * kChipOutlinePx);
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

    if (dim) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, alpha);
    }

    cairo_restore(cr);
}

namespace {

// Shared flag iteration used by render_flags / compute_flag_hit_rects and their
// phase-reset analogues. Invokes `emit(i, center_x)` for EVERY visible marker,
// in ascending painted-x order (equal columns tie-break by ascending store
// index via the stable sort below). There is no elision: overlapping shapes
// occlude instead.
//
// THE PAINT/HIT INVARIANT. `center_x` — the marker's painted pixel column — is
// computed ONCE here, and every flag is CENTERED on it (rectangle in the flag
// lane, triangle in the triangle lane, both centered; the triangle tip marks
// the frame). Both the painter (render_flags / render_phase_reset_flags) AND
// the hit-rect builder (compute_flag_hit_rects_impl) consume this one center_x,
// so the clickable flag rectangle and the painted flag are the same rectangle
// by construction. No cairo context is needed — center_x is pure viewport
// arithmetic and the flag width is fixed (flag_lane_w_px()).
//
// Occlusion model: the emit order is ascending x. The painters paint the
// collected list in TWO REVERSE passes keyed on selection — unselected first,
// then selected — so a selected shape lands above every unselected one and,
// within each class, the leftmost (lowest store index on an equal column) lands
// on top; the hit walk runs FORWARD in two matching passes (selected first,
// then unconditional). Consistency invariant across the paint and hit paths:
// topmost = the leftmost SELECTED flag when any selected flag contains the
// point, else the leftmost flag (lowest index on ties).
template <typename MarkerVec, typename Emit>
void iterate_visible_flags_impl(
    GuiRect top_strip_area,
    int waveform_width,
    const MarkerVec& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    const std::vector<WarpFrameMapSegment>* warp_frame_map,
    const DragOverlay* drag_overlay,
    Emit&& emit) {
    const double span = static_cast<double>(viewport_end_sample -
                                            viewport_start_sample);
    // Map columns against the EFFECTIVE waveform width, not the strip's own
    // full width, so a flag shares the marker stem's samples-per-pixel and
    // stays column-aligned with it at every window width (they diverge only
    // when the two widths differ — a non-multiple-of-16 window; at
    // 1920/2560/3840 they are equal and this is a no-op).
    const double samples_per_pixel =
        span / static_cast<double>(waveform_width);
    if (samples_per_pixel <= 0.0) return;

    // Candidates iterate in VISUAL x order, not store order. During a
    // marker drag the store is frozen (positions come from the DragOverlay),
    // so once the dragged flag crosses a neighbor the store walk's ascending-x
    // assumption is false. Collect the visible candidates with their
    // overlay-effective paint positions and stable-sort by position; the stable
    // sort makes the store index the tiebreaker for exactly-equal positions, so
    // the occlusion z-order the painters derive stays deterministic. At rest,
    // store order equals x order and the sort is a no-op reorder.
    struct FlagCandidate {
        int    i;
        double ms;
    };
    std::vector<FlagCandidate> candidates;
    candidates.reserve(markers.size());
    // A flag may hang up to HALF offscreen at the viewport edges (like the
    // playhead triangle always did): cull only when the shape is fully
    // offscreen. The horizontal half-footprint is half the flag width.
    const double half_flag =
        static_cast<double>(flag_lane_w_px()) / 2.0;
    const double cull_lo = static_cast<double>(viewport_start_sample) -
                           half_flag * samples_per_pixel;
    const double cull_hi = static_cast<double>(viewport_end_sample) +
                           half_flag * samples_per_pixel;
    for (size_t i = 0; i < markers.size(); ++i) {
        const auto& m = markers[i];
        const double eff_time = drag_overlay
            ? drag_overlay->effective_time(
                  static_cast<int>(i), m.time_frame)
            : m.time_frame;
        const double ms =
            frame_to_paint_sample(eff_time, warp_frame_map);
        if (ms < cull_lo) continue;
        if (ms > cull_hi) continue;
        candidates.push_back({static_cast<int>(i), ms});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const FlagCandidate& a, const FlagCandidate& b) {
                         return a.ms < b.ms;
                     });

    for (const FlagCandidate& cand : candidates) {
        const int    i  = cand.i;
        const double ms = cand.ms;

        const double x_raw =
            (ms - static_cast<double>(viewport_start_sample)) /
            samples_per_pixel;

        // Center the flag on the marker's pixel column — see the paint/hit
        // invariant above. No edge clamp; a flag near an edge hangs up to half
        // offscreen exactly as its column dictates.
        const double center_x =
            static_cast<double>(top_strip_area.x) + std::nearbyint(x_raw);

        emit(i, center_x);
    }
}

// Resolves the flag lane / triangle lane / tip Y from the top strip rect. The
// top strip sits at screen y=0, so screen and top-strip-local coords coincide;
// the triangle lane is the innermost lane (flush on the waveform), the flag lane
// directly above it. The waveform top edge is top_strip_area.y + .h.
struct FlagLaneY {
    double flag_top;   // flag-lane top (rectangle top)
    double tri_top;    // triangle-lane top (= flag-lane bottom / rect bottom)
    double tip_y;      // triangle-lane bottom (= waveform top edge, triangle tip)
};
FlagLaneY flag_lane_geometry(const GuiRect& top_strip_area) {
    const double wf_top =
        static_cast<double>(top_strip_area.y + top_strip_area.h);
    const double tri_h  = static_cast<double>(playhead_triangle_h_px());
    const double flag_h = static_cast<double>(flag_lane_h_px());
    FlagLaneY g;
    g.tip_y    = wf_top;
    g.tri_top  = wf_top - tri_h;
    g.flag_top = wf_top - tri_h - flag_h;
    return g;
}

} // namespace

void render_flags(cairo_t* cr,
                  GuiRect top_strip_area,
                  int waveform_width,
                  const std::vector<GuiWarpMarker>& markers,
                  long long viewport_start_sample,
                  long long viewport_end_sample,
                  int sample_rate,
                  const std::set<int>& selected_set,
                  const std::vector<WarpFrameMapSegment>* warp_frame_map,
                  const DragOverlay* drag_overlay) {
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (sample_rate <= 0) return;

    cairo_save(cr);

    const FlagLaneY g = flag_lane_geometry(top_strip_area);

    // Collect flag centers in ascending-x order, then paint in TWO reverse
    // passes keyed on selection: UNSELECTED in reverse, then SELECTED in
    // reverse. So every selected shape lands above every unselected one, and
    // within each class the leftmost (lowest-index on ties) paints last = on
    // top. Selection drives the flag-cache fingerprint, so a selection change
    // rebuilds the cache and this z-order follows.
    struct FlagEmit {
        int    i;
        double center_x;
    };
    std::vector<FlagEmit> emits;
    iterate_visible_flags_impl(top_strip_area, waveform_width, markers,
                               viewport_start_sample, viewport_end_sample,
                               warp_frame_map, drag_overlay,
        [&](int i, double center_x) {
            emits.push_back({i, center_x});
        });

    auto paint_emit = [&](const FlagEmit& e) {
        const bool sel = selected_set.count(e.i) > 0;
        const GuiColor fill    = sel ? kSelected : kMarker;
        const GuiColor outline = sel ? kSelectedOutline : kMarkerOutline;
        const double alpha = effective_disabled(markers, e.i)
            ? kDisabledMarkerAlpha : 1.0;
        paint_flag_shape(cr, e.center_x, g.flag_top, g.tri_top, g.tip_y,
                         fill, outline, /*with_triangle=*/true, alpha);
    };
    for (auto it = emits.rbegin(); it != emits.rend(); ++it)
        if (!selected_set.count(it->i)) paint_emit(*it);
    for (auto it = emits.rbegin(); it != emits.rend(); ++it)
        if (selected_set.count(it->i)) paint_emit(*it);

    cairo_restore(cr);
}

namespace {

template <typename MarkerVec>
std::vector<FlagHitRect> compute_flag_hit_rects_impl(
    GuiRect top_strip_area,
    int waveform_width,
    const MarkerVec& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    const std::vector<WarpFrameMapSegment>* warp_frame_map,
    const DragOverlay* drag_overlay) {
    std::vector<FlagHitRect> out;
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return out;
    if (viewport_end_sample <= viewport_start_sample) return out;
    if (sample_rate <= 0) return out;

    // The hit rect is the flag RECTANGLE only (the triangle is not a hit
    // target), sized and placed EXACTLY as paint_flag_shape draws the
    // rectangle: centered on the column, width flag_lane_w_px(), top/bottom the
    // flag lane. One rect per VISIBLE flag (no elision), emitted in ascending-x
    // order, so overlapping flags yield overlapping rects; the caller
    // (hit_test_flag) resolves an overlap with two forward passes mirroring the
    // painters' two reverse passes — the leftmost SELECTED containing rect, else
    // the leftmost containing rect = the topmost-painted flag.
    const FlagLaneY g = flag_lane_geometry(top_strip_area);
    const int flag_w = flag_lane_w_px();
    const int ry     = static_cast<int>(std::round(g.flag_top));
    const int rh     = static_cast<int>(std::round(g.tri_top)) - ry;
    iterate_visible_flags_impl(top_strip_area, waveform_width, markers,
                               viewport_start_sample, viewport_end_sample,
                               warp_frame_map, drag_overlay,
        [&](int i, double center_x) {
            const int cx = static_cast<int>(std::round(center_x));
            FlagHitRect r;
            r.marker_index = i;
            r.x = cx - flag_w / 2;
            r.y = ry;
            r.w = flag_w;
            r.h = rh;
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
    const DragOverlay* drag_overlay) {
    return compute_flag_hit_rects_impl(top_strip_area, waveform_width, markers,
        viewport_start_sample, viewport_end_sample,
        sample_rate, warp_frame_map, drag_overlay);
}

// ---------- Phase reset marker rendering ----------

void render_phaseresetmarkers(cairo_t* cr,
                              GuiRect waveform_area,
                              const std::vector<GuiPhaseResetMarker>& phase_resets,
                              long long viewport_start_sample,
                              long long viewport_end_sample,
                              int sample_rate,
                              int last_selected,
                              const std::vector<WarpFrameMapSegment>* warp_frame_map,
                              const DragOverlay* drag_overlay,
                              cairo_surface_t* ink_plate) {
    // Only the last-selected phase reset's stem lives on the waveform surface;
    // every phase reset's frame tick (its flag triangle) moved into the flag
    // structure (flag cache). A disabled last-selected stem dims plainly (the
    // bool read directly — no cascade).
    const auto is_disabled = [&](int i) { return phase_resets[i].disabled; };
    render_marker_stems_impl(
        cr, waveform_area, phase_resets,
        viewport_start_sample, viewport_end_sample,
        sample_rate, last_selected, warp_frame_map,
        drag_overlay, is_disabled, ink_plate);
}

void render_phase_reset_flags(cairo_t* cr,
                            GuiRect top_strip_area,
                            int waveform_width,
                            const std::vector<GuiPhaseResetMarker>& phase_resets,
                            long long viewport_start_sample,
                            long long viewport_end_sample,
                            int sample_rate,
                            const std::set<int>& selected_set,
                            const std::vector<WarpFrameMapSegment>* warp_frame_map,
                            const DragOverlay* drag_overlay) {
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (sample_rate <= 0) return;

    cairo_save(cr);

    const FlagLaneY g = flag_lane_geometry(top_strip_area);

    // Collect-then-paint in TWO reverse passes keyed on selection, mirroring
    // render_flags: UNSELECTED in reverse, then SELECTED in reverse, so every
    // selected shape lands above every unselected one and within each class the
    // leftmost (lowest-index on ties) paints last = on top.
    struct PhaseResetEmit {
        int    i;
        double center_x;
    };
    std::vector<PhaseResetEmit> emits;
    iterate_visible_flags_impl(top_strip_area, waveform_width, phase_resets,
                               viewport_start_sample, viewport_end_sample,
                               warp_frame_map, drag_overlay,
        [&](int i, double center_x) {
            emits.push_back({i, center_x});
        });

    auto paint_emit = [&](const PhaseResetEmit& e) {
        const bool sel = selected_set.count(e.i) > 0;
        const GuiColor fill    = sel ? kSelected : kMarker;
        const GuiColor outline = sel ? kSelectedOutline : kMarkerOutline;
        const double alpha = phase_resets[e.i].disabled
            ? kDisabledMarkerAlpha : 1.0;
        paint_flag_shape(cr, e.center_x, g.flag_top, g.tri_top, g.tip_y,
                         fill, outline, /*with_triangle=*/true, alpha);
    };
    for (auto it = emits.rbegin(); it != emits.rend(); ++it)
        if (!selected_set.count(it->i)) paint_emit(*it);
    for (auto it = emits.rbegin(); it != emits.rend(); ++it)
        if (selected_set.count(it->i)) paint_emit(*it);

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
        sample_rate, warp_frame_map, drag_overlay);
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
    // Cached triangle masks and the H each was built at; regenerated by their
    // accessors when H changes. The playhead mask is the full triangle
    // (2H-1 x H), stamped centered on the column at the playhead cursor and at
    // every marker/trim flag's triangle.
    cairo_surface_t* g_playhead_triangle   = nullptr;
    int              g_playhead_triangle_h = 0;
} // namespace

void   set_gui_font_size_pt(double pt) { g_font_size_pt = pt; }
double gui_font_scale()    { return g_font_size_pt / kDefaultFontSizePt; }
double flag_font_size_px() { return g_font_size_pt * 96.0 / 72.0; }

// Build a fresh A8 tip-down triangle mask of height h (W = 2h-1). Row y (0-based
// from the top) spans columns y through W-1-y inclusive, so each row is two
// pixels narrower than the one above, from full width W down to a single tip
// pixel at column (W-1)/2 = h-1 in the bottom row. Alpha is strictly 0 or 255 —
// the A8 buffer is filled directly, no rasterizer, no partial coverage. This is
// the full tip-down triangle shared by the playhead cursor and every
// marker/trim flag (at scale 1, H = 8, W = 15).
static cairo_surface_t* build_triangle_mask(int h) {
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
    return s;
}

// Build (or return the cached) tip-down triangle mask for the current H. The
// one mask serves the playhead cursor and every marker/trim flag triangle.
cairo_surface_t* playhead_triangle_mask() {
    const int h = playhead_triangle_h_px();
    if (g_playhead_triangle && g_playhead_triangle_h == h) {
        return g_playhead_triangle;
    }
    if (g_playhead_triangle) {
        cairo_surface_destroy(g_playhead_triangle);
        g_playhead_triangle = nullptr;
    }
    g_playhead_triangle   = build_triangle_mask(h);
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
    // The chip height IS the row metric, and the outline ring sits outside the
    // padding, so both formulas add 2*kChipOutlinePx / kChipOutlinePx: the row
    // is font_height + 2*flag_pad_y_px() + 2*kChipOutlinePx tall, and the
    // baseline drops by flag_pad_y_px() + kChipOutlinePx + ascent from the top.
    g_row_h = static_cast<int>(std::nearbyint(
        font_height + 2.0 * flag_pad_y_px())) + 2 * kChipOutlinePx;
    g_row_baseline_off = flag_pad_y_px() + kChipOutlinePx + fe.ascent;
    cairo_restore(cr);
    g_measured_font_px = px;
}

double lane_text_left_x(
    const AppState& app, const GuiAudio& audio,
    int marker_idx, size_t glyph_count)
{
    const auto& mv = app.warpmarkers.markers();
    if (marker_idx < 0 ||
        marker_idx >= static_cast<int>(mv.size())) return -1.0;
    const double advance = monospace_advance();
    if (advance <= 0.0) return -1.0;
    // The marker's painted pixel column (window coords) via the painters' own
    // math (painted_column_of_source_frame) against the active display
    // context's map — identity in source view, the live cached target map in
    // target view — so the lane run centers on exactly the column the flag
    // paints on.
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    const int col = painted_column_of_source_frame(
        app, audio, static_cast<double>(mv[marker_idx].time_frame),
        *ctx.warp_frame_map);
    const GuiRect area = waveform_area(app);
    const double center_x = static_cast<double>(area.x + col);
    const double run_w = static_cast<double>(glyph_count) * advance;
    // Center over the column, then clamp the whole run fully onscreen within
    // the lane (unlike the flags, the lane text never hangs off an edge). A run
    // wider than the lane pins to the left edge.
    const GuiRect lane = top_marker_text_row_area(app);
    const double min_left = static_cast<double>(lane.x);
    const double max_left = static_cast<double>(lane.x + lane.w) - run_w;
    double left = center_x - run_w / 2.0;
    if (max_left <= min_left) {
        left = min_left;
    } else {
        if (left < min_left) left = min_left;
        if (left > max_left) left = max_left;
    }
    return left;
}

double flag_pending_text_left_x(
    const AppState& app, const GuiAudio& audio,
    int marker_idx)
{
    return lane_text_left_x(app, audio, marker_idx,
                            app.top_flag_editor.pending.size());
}
