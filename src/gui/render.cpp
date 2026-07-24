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
#include <utility>
#include <vector>

// kFlagBottomLiftPx now lives in render.h so the strip lane geometry in
// main.cpp and the stem blit in paint_handler.cpp reference the same value.

// playhead_half_px() is the half-width (H - 1) of the code-generated
// inverted-triangle playhead mask (2H-1 wide, tip at column H-1); it lives
// in render.h as a single inline accessor shared by this TU's cull and
// main.cpp's invalidation.

namespace {

// Flag text mirrors the canonical line's PAYLOAD (post-pipe); metadata
// (b=/e=/#) never appears in it. The flag shape itself is textless — this is
// the base composer flag_text_iter wraps, and every marker-text-lane surface
// (the hover popup over an eligible pass/ref marker, and the Enter flag
// editor's seeded initial text) routes through that wrapper, so what they
// show mirrors this exactly.
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

// Fills and outlines ONE marker/phase-reset/trim flag SHAPE at `center_x` (the
// item's pixel column). `anchor` places the rectangle relative to that column:
// Center (markers/phase resets — straddles the column, may hang half offscreen)
// or LeftEdge/RightEdge (trim chips — a bound is an edge, so its chip sits fully
// to one side). The shape is the fixed-width rectangle
// in the flag lane [rx, flag_top, flag_w, rect_h] plus (when `with_triangle`)
// the tip-down triangle in the triangle lane directly beneath it, tip on the
// column at `tip_y` (= the waveform top edge). The two are ONE shape: the
// triangle's TOP is the rectangle's FULL width, so its slopes leave the rect's
// exact bottom corners and run to the tip with NO inward step — the outline
// flows continuously from the vertical sides straight into the diagonals (no
// horizontal seam, no 90-degree jog). The rectangle fills crisp (AA off); the
// triangle fills as an ANTIALIASED PATH whose base coincides with the rect's
// hard bottom edge, so the two share the full-width boundary row with no gap and
// the triangle's slope fill blends with the outline's slope stroke. The 1px
// `outline` runs the TRUE OUTSIDE only. ALIASING: axis-aligned edges (rect
// sides, top, base) use the +0.5 half-pixel convention for crisp 1px lines; the
// two diagonal slopes antialias (the relaxed rule — only verticals/horizontals
// are hard-aliased). Trim chips pass with_triangle=false — a plain rectangle, no
// triangle (Ableton's loop bounds carry none). `alpha` < 1 dims the whole shape
// as one cairo group (the disabled cue). The triangle is the identical geometry
// the cached playhead mask stamps, so a flag's triangle and the playhead's
// coincide when the cursor sits on it.
// Horizontal anchor of the shape's rectangle relative to `center_x`'s column.
// Center is the marker-flag default (and the playhead triangle). The edge modes
// serve the trim chips: a bound is an EDGE, not a point, so its chip reads as an
// edge handle sitting ON the bound column rather than straddling it — the begin
// chip's LEFT edge on the column (body rightward), the end chip's RIGHT edge on
// it (body leftward). This is the deliberate asymmetry vs marker flags, which
// center and may hang half offscreen.
enum class FlagHAnchor { Center, LeftEdge, RightEdge };

void paint_flag_shape(cairo_t* cr, double center_x,
                      double flag_top_d, double tri_top_d, double tip_y_d,
                      GuiColor fill, GuiColor outline,
                      bool with_triangle, double alpha,
                      FlagHAnchor anchor = FlagHAnchor::Center) {
    const int flag_w = flag_lane_w_px();

    const int cx     = static_cast<int>(std::round(center_x));
    // Rect left per anchor: Center straddles the column; LeftEdge puts the
    // rect's left column ON it; RightEdge puts the rect's RIGHTMOST column on it
    // (rx + flag_w - 1 == cx). cx is otherwise the triangle apex (Center only).
    const int rx =
        anchor == FlagHAnchor::LeftEdge  ? cx
      : anchor == FlagHAnchor::RightEdge ? cx - flag_w + 1
      :                                    cx - flag_w / 2;   // rect left
    const int rw     = flag_w;
    const int ry     = static_cast<int>(std::round(flag_top_d));
    const int rb     = static_cast<int>(std::round(tri_top_d)); // rect bottom = tri top
    const int tbot   = static_cast<int>(std::round(tip_y_d));   // triangle lane bottom

    // Triangle centerline = the rect's own center, and its base half-width comes
    // from the shared taper owner (flag_triangle_half_width_at at row 0 = the
    // full flag half-width). Deriving the base corners/apex from these keeps the
    // painted slope identical to hit_test_flag's triangle-lane slope. For the
    // marker flags (center anchor + odd width) the centerline lands exactly on
    // cx+0.5 and the base corners on rx / rx+rw, so the base shares the rect's
    // hard bottom edge with no gap or double-drawn seam.
    const double tri_cx    = static_cast<double>(rx) + rw / 2.0;
    const double tri_bhalf = flag_triangle_half_width_at(0.0);

    cairo_save(cr);
    const bool dim = alpha < 1.0;
    if (dim) cairo_push_group(cr);

    // Fill: rectangle crisp (AA off) then, if present, the triangle as an AA path
    // whose base is the rect's full-width bottom edge and whose apex is the
    // bottom-center column. The base row coincides with the rect's hard bottom
    // edge (rb), so the shared full-width boundary carries no gap or double-drawn
    // seam.
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgb(cr, fill.r, fill.g, fill.b);
    cairo_rectangle(cr, rx, ry, rw, rb - ry);
    cairo_fill(cr);
    cairo_restore(cr);
    if (with_triangle) {
        cairo_set_source_rgb(cr, fill.r, fill.g, fill.b);
        cairo_move_to(cr, tri_cx - tri_bhalf, static_cast<double>(rb));
        cairo_line_to(cr, tri_cx + tri_bhalf, static_cast<double>(rb));
        cairo_line_to(cr, tri_cx,             static_cast<double>(tbot));
        cairo_close_path(cr);
        cairo_fill(cr);
    }

    // Outline: the true outside as one closed polygon. Rect edges, then (for a
    // triangle-bearing flag) the two slopes directly from the rect's bottom
    // corners to the tip — no step, no horizontal seam.
    cairo_set_source_rgb(cr, outline.r, outline.g, outline.b);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, rx + 0.5,           ry + 0.5);          // top-left
    cairo_line_to(cr, rx + rw - 0.5,      ry + 0.5);          // top edge
    cairo_line_to(cr, rx + rw - 0.5,      rb - 0.5);          // right edge to rect bottom
    if (with_triangle) {
        cairo_line_to(cr, tri_cx,          tbot - 0.5);       // right slope to tip
        cairo_line_to(cr, rx + 0.5,        rb - 0.5);         // left slope up
    } else {
        cairo_line_to(cr, rx + 0.5,        rb - 0.5);         // rect bottom edge
    }
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
                     bool draw_line,
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
    // The 1px vertical line is suppressed for the grey selected-marker focus
    // triangle form (draw_line = false, triangle-only — architect 2026-07-23) as
    // well as when the column clips out; the triangle below is unaffected.
    if (draw_line && col >= 0.0 && col < static_cast<double>(area.w)) {
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

    // Inverted-triangle indicator: stamped from the cached ANTIALIASED A8 mask
    // (playhead_triangle_mask()) so the per-frame playhead redraw is a cheap
    // blit with the slope edge alphas already baked in. The mask is 2H-1 x H
    // (odd width) with the tip at column index H-1 (image-local); integer
    // division places that tip column at `area.x + col`. The triangle sits in
    // the TRIANGLE LANE directly above the waveform (dst_y = area.y - H): its
    // top row is the lane top and its tip (bottom row) lands one pixel above
    // the waveform top edge, where the marker/trim stems begin. This is the
    // same width and centered column as every marker/trim flag triangle, so when
    // the cursor sits on a marker the two coincide. Skipped for the scanner call
    // (draw_triangle=false): the triangle belongs to the cursor exclusively under
    // the split-playhead model. The clip band is the triangle lane; the vertical
    // line above spans only the waveform area, so the two never overlap.
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

void render_split_playhead(cairo_t* cr,
                           GuiRect area,
                           int left_col,
                           int right_col,
                           GuiColor color) {
    if (area.w <= 0 || area.h <= 0) return;

    // The one cached AA-baked tip-down mask (2H-1 wide, H tall). Its full-height
    // column — the tip column — is image index center = H-1, dividing the mask
    // into the left slope [0..center] and the right slope [center..2*center].
    cairo_surface_t* mask = playhead_triangle_mask();
    const int img_w  = cairo_image_surface_get_width(mask);
    const int img_h  = cairo_image_surface_get_height(mask);
    const int center = img_h - 1;

    // Triangle lane: top row at the lane top, tip one pixel above the waveform
    // top edge — identical to the unsplit playhead triangle.
    const double dst_y   = static_cast<double>(area.y - img_h);
    const double area_x0 = static_cast<double>(area.x);
    const double area_x1 = static_cast<double>(area.x + area.w);

    // Stamp one half: place the mask so its center column lands on `bound_col`,
    // then clip to this half's image columns [first_img_col..last_img_col]
    // (intersected with the waveform's horizontal span, so a bound near an edge
    // partial-renders and never leaks past the area). The clip selects the half;
    // the single mask blit supplies its baked slope alphas.
    auto stamp_half = [&](int bound_col, int first_img_col, int last_img_col) {
        const double dst_x =
            static_cast<double>(area.x + bound_col - center);
        double clip_x0 = dst_x + static_cast<double>(first_img_col);
        double clip_x1 = dst_x + static_cast<double>(last_img_col + 1);
        clip_x0 = std::max(clip_x0, area_x0);
        clip_x1 = std::min(clip_x1, area_x1);
        if (clip_x1 <= clip_x0) return;
        cairo_save(cr);
        cairo_rectangle(cr, clip_x0, dst_y,
                        clip_x1 - clip_x0, static_cast<double>(img_h));
        cairo_clip(cr);
        cairo_set_source_rgb(cr, color.r, color.g, color.b);
        cairo_mask_surface(cr, mask, dst_x, dst_y);
        cairo_restore(cr);
    };

    // Degenerate region (both bounds on one column): stamp the WHOLE mask once,
    // centered on the bound. Stamping the two halves here would land both on the
    // same dst_x and composite the shared center (tip) column twice under Cairo's
    // OVER operator — its partially-covered AA pixels are not idempotent, so the
    // tip would render more opaque than an ordinary cursor. One full stamp is
    // byte-identical to the single cursor triangle.
    if (left_col == right_col) {
        stamp_half(left_col, 0, img_w - 1);
        return;
    }
    // Left half: full-height edge on the left bound, slope flaring left.
    stamp_half(left_col, 0, center);
    // Right half: full-height edge on the right bound, slope flaring right.
    stamp_half(right_col, center, img_w - 1);
}

void render_strip_anchor_stem(cairo_t* cr, GuiRect area, int col,
                              cairo_surface_t* ink_plate) {
    if (area.w <= 0 || area.h <= 0) return;
    // The clamp is where the affordance lives: an anchor pushed to (or past) a
    // song edge pins to the edge column, so the stem draws exactly there.
    if (col < 0)          col = 0;
    if (col >= area.w)    col = area.w - 1;

    const double x_px = static_cast<double>(area.x) + col + 0.5;
    cairo_save(cr);
    cairo_set_source_rgb(cr, kStripAnchorStem.r, kStripAnchorStem.g,
                         kStripAnchorStem.b);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, x_px, static_cast<double>(area.y));
    cairo_line_to(cr, x_px, static_cast<double>(area.y + area.h));
    cairo_stroke(cr);
    // The dark ink notch: the same kBackground overdraw the marker stems apply
    // where the column crosses opaque waveform ink.
    if (ink_plate) {
        cairo_surface_flush(ink_plate);
        fill_column_ink_runs(cr, area.x, area.y, area.h, ink_plate, col);
    }
    cairo_restore(cr);
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

    // Stem geometry: the trim stem spans the waveform area, top at
    // waveform_area.y (where its b/e chip's structure ends above) down to the
    // waveform bottom — the same span the selected-marker stem (paint_selected_stem)
    // uses.
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
        // Clamp into the visible column range [0, W-1], matching render_trim_flags
        // (col_of): the inclusive END wall T-1 at full zoom-out rounds to column W
        // (one past the surface). Without the clamp this waveform stem segment
        // would land offscreen at W while the strip-crossing segment/chip sit at
        // the clamped W-1, breaking the one-column connection between them.
        int icol = static_cast<int>(std::nearbyint(x_raw));
        if (waveform_area.w > 0)
            icol = std::clamp(icol, 0, waveform_area.w - 1);
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
    const int chip_top  = top_strip_area.y + monospace_row_h();
    const int chip_h    = flag_lane_h_px();
    const int chip_bottom = chip_top + chip_h;
    // Waveform top edge in this (top-strip-local) coord system: the strip sits
    // at y=0, so it is the strip's own height. The strip-crossing stem segment
    // painted below runs from the chip's bottom edge down to here, where the
    // waveform-area trim stem (render_trim_stems) continues to the waveform
    // bottom — one unbroken 1px line at the bound column.
    const int wave_top = top_strip_area.y + top_strip_area.h;

    // Bound column (unclamped) and its viewport visibility. Columns are computed
    // unconditionally so the wash band spans between them even when a chip is
    // culled; chips and their stems draw only for a visible bound.
    auto col_of = [&](int64_t frame) {
        const double x_raw =
            (static_cast<double>(frame) -
             static_cast<double>(viewport_start_sample)) / samples_per_pixel;
        const int c = static_cast<int>(std::nearbyint(x_raw));
        // Clamp into the visible column range [0, W-1]. At full zoom-out the
        // inclusive END wall T-1 rounds to column W (one past the surface); left
        // unclamped, the right-edge-anchored end chip loses its bound-edge pixel
        // and outline to the cache clip and its stems fall entirely offscreen.
        // Clamping lands the wall on the last visible column so the chip stays
        // fully visible and connected. Begin/frame-0 already maps to column 0 and
        // is unaffected. hit_test_trim_chip clamps identically so paint and hit
        // stay column-identical.
        if (waveform_area.w > 0)
            return std::clamp(c, 0, waveform_area.w - 1);
        return c;
    };
    auto in_viewport = [&](int64_t frame) {
        const double ms = static_cast<double>(frame);
        return ms >= static_cast<double>(viewport_start_sample) &&
               ms <  static_cast<double>(viewport_end_sample);
    };
    const int begin_col = col_of(trim.begin);
    const int end_col   = col_of(trim.end);

    cairo_save(cr);

    // With both bounds set, a wash band fills the trim-chip-lane GAP between the
    // two edge-anchored chips: from the begin chip's inner (right) edge to the
    // end chip's inner (left) edge. The chips edge-anchor ON their bound columns
    // (begin left-edge, end right-edge, bodies facing inward), so this gap is the
    // span the pair (bridge) drag grabs — route_trim_chip_press tests strictly
    // between the two bound columns, and the chip single-hit consumes the chip
    // pixels first, so the effective grab region equals this gap. The wash is the
    // shared overlay pair (kOverlay / kOverlayAlpha, the phase reset overlay's
    // pair) with a 1px ring at ring strength (kOverlayOutlineAlpha) around it.
    // Columns are computed unconditionally (a chip's viewport cull must not
    // suppress the band). A gap exists only when the begin chip sits fully left
    // of the end chip (wide-enough, non-inverted span); an inverted or narrow
    // trim shows no bridge, its chips simply overlap.
    //
    // Offscreen-border rule: a gap edge follows its chip offscreen. When a bound
    // is IN the viewport its edge aligns with the drawn (clamped) chip; when it
    // has scrolled OFFSCREEN the edge is taken from the bound's TRUE unclamped
    // column (col_raw). The fill and ring are drawn at these raw positions with
    // NO [0,W-1] clamp, so an offscreen-side edge lands past the viewport edge
    // and is clipped by the cache surface: the wash fills flush to the edge (no
    // chip-width gap) and that side's ring border goes offscreen with the chip
    // rather than resting at the viewport edge. The top/bottom borders span the
    // full raw width and are clipped to the visible portion. An end bound at
    // EOF (T-1) stays in_viewport, so it uses the clamped column (a ~1px seam vs
    // the raw column, accepted), preserving the visible EOF chip's connection.
    if (has_begin && has_end && waveform_area.w > 0) {
        auto col_raw = [&](int64_t frame) {
            const double x_raw =
                (static_cast<double>(frame) -
                 static_cast<double>(viewport_start_sample)) / samples_per_pixel;
            return static_cast<int>(std::nearbyint(x_raw));
        };
        const int gap_lo =
            (in_viewport(trim.begin) ? begin_col : col_raw(trim.begin)) + chip_w;
        const int gap_hi =
            (in_viewport(trim.end) ? end_col : col_raw(trim.end)) - chip_w + 1;
        if (gap_hi > gap_lo) {
            cairo_set_source_rgba(cr, kOverlay.r, kOverlay.g,
                                  kOverlay.b, kOverlayAlpha);
            cairo_rectangle(cr, static_cast<double>(top_strip_area.x + gap_lo),
                            static_cast<double>(chip_top),
                            static_cast<double>(gap_hi - gap_lo),
                            static_cast<double>(chip_h));
            cairo_fill(cr);
            // 1px ring bordering the gap rect, AA off. Drawn at the raw gap
            // positions (no clamp) so an offscreen-side border is clipped away.
            const int rx = top_strip_area.x + gap_lo;
            const int rw = gap_hi - gap_lo;
            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            cairo_set_source_rgba(cr, kOverlay.r, kOverlay.g, kOverlay.b,
                                  kOverlayOutlineAlpha);
            cairo_rectangle(cr, rx, chip_top, rw, 1);              // top
            cairo_rectangle(cr, rx, chip_top + chip_h - 1, rw, 1); // bottom
            cairo_rectangle(cr, rx, chip_top, 1, chip_h);          // left
            cairo_rectangle(cr, rx + rw - 1, chip_top, 1, chip_h); // right
            cairo_fill(cr);
            cairo_restore(cr);
        }
    }

    // Strip-crossing stem segment for each visible bound: from the chip's bottom
    // edge down through the intervening lanes (marker text, flag, triangle) to
    // the waveform top, where render_trim_stems continues it to the waveform
    // bottom. The stem attaches at the bound column — the begin chip's leftmost
    // edge and the end chip's rightmost edge — so the chip's anchored edge and
    // its stem share one column and read as a single handle. Marker stems stay
    // waveform-only; this strip-crossing gap-closing segment is TRIM-only. 1px,
    // AA off (axis-aligned, +0.5), kTrimMarker.
    {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, kTrimMarker.r, kTrimMarker.g, kTrimMarker.b);
        cairo_set_line_width(cr, 1.0);
        auto paint_strip_stem = [&](int64_t frame) {
            if (!in_viewport(frame)) return;
            const double x_px =
                static_cast<double>(top_strip_area.x + col_of(frame)) + 0.5;
            cairo_move_to(cr, x_px, static_cast<double>(chip_bottom));
            cairo_line_to(cr, x_px, static_cast<double>(wave_top));
            cairo_stroke(cr);
        };
        if (has_begin) paint_strip_stem(trim.begin);
        if (has_end)   paint_strip_stem(trim.end);
        cairo_restore(cr);
    }

    // The b/e chips are TEXTLESS rectangles of the flag's exact width/height,
    // EDGE-ANCHORED on their bound columns (begin left-edge, end right-edge), no
    // triangle (Ableton's loop bounds carry none). Deliberate asymmetry vs marker
    // flags: a marker is a POINT (its flag centers and may hang half offscreen),
    // a trim bound is an EDGE (its chip sits fully to one side, so a bound at
    // frame 0 / EOF shows its chip fully onscreen). Build the visible list
    // carrying each chip's bound column and role, sorted by column ascending.
    struct TrimChip {
        int  col;
        bool is_begin;
    };
    std::vector<TrimChip> chips;
    if (has_begin && in_viewport(trim.begin))
        chips.push_back({begin_col, true});
    if (has_end && in_viewport(trim.end))
        chips.push_back({end_col, false});
    std::sort(chips.begin(), chips.end(),
              [](const TrimChip& a, const TrimChip& b) {
                  if (a.col != b.col) return a.col < b.col;
                  // Deterministic tie-break at an equal column: begin first, so
                  // the reverse paint below lands it on top (mirrors the hit
                  // test's forward-walk begin-first pick).
                  return a.is_begin && !b.is_begin;
              });

    // Overlapping chips occlude rather than elide (as the marker flags do):
    // paint the sorted list in REVERSE order so the leftmost lands on top. Trim
    // bounds are unselectable (recorded asymmetry), so there is no selected pass.
    // Each chip is a plain kTrimMarker rectangle with a kTrimMarkerOutline
    // border, no triangle; the bottom argument is unused for the rectangle shape.
    for (auto it = chips.rbegin(); it != chips.rend(); ++it) {
        const double center_x =
            static_cast<double>(top_strip_area.x + it->col);
        paint_flag_shape(cr, center_x,
                         static_cast<double>(chip_top),
                         static_cast<double>(chip_bottom),
                         static_cast<double>(chip_bottom),
                         kTrimMarker, kTrimMarkerOutline,
                         /*with_triangle=*/false, /*alpha=*/1.0,
                         it->is_begin ? FlagHAnchor::LeftEdge
                                      : FlagHAnchor::RightEdge);
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
                  const std::set<int>& red_set,
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
        // Color class priority: selection wins over red, red over default.
        const bool sel = selected_set.count(e.i) > 0;
        const bool red = !sel && red_set.count(e.i) > 0;
        const GuiColor fill    = sel ? kSelected
                               : red ? kAccent : kMarker;
        const GuiColor outline = sel ? kSelectedOutline
                               : red ? kAccentOutline : kMarkerOutline;
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

void render_phase_reset_flags(cairo_t* cr,
                            GuiRect top_strip_area,
                            int waveform_width,
                            const std::vector<GuiPhaseResetMarker>& phase_resets,
                            long long viewport_start_sample,
                            long long viewport_end_sample,
                            int sample_rate,
                            const std::set<int>& selected_set,
                            const std::set<int>& red_set,
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
        // Color class priority: selection wins over red, red over default.
        const bool sel = selected_set.count(e.i) > 0;
        const bool red = !sel && red_set.count(e.i) > 0;
        const GuiColor fill    = sel ? kSelected
                               : red ? kAccent : kMarker;
        const GuiColor outline = sel ? kSelectedOutline
                               : red ? kAccentOutline : kMarkerOutline;
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

// Build a fresh A8 tip-down triangle mask of height h (W = 2h-1). The triangle
// is filled as an ANTIALIASED cairo path — full-width top edge [0, W] down to the
// bottom-center apex (column (W-1)/2 = h-1) — so its two slopes carry baked gray
// edge alphas (the relaxed aliasing rule: diagonals may antialias). This is the
// tip-down triangle the playhead cursor stamps, the identical geometry the
// marker/trim flags path-fill in paint_flag_shape (at scale 1, H = 9, W = 17).
static cairo_surface_t* build_triangle_mask(int h) {
    const int w = 2 * h - 1;
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_A8, w, h);
    cairo_t* cr = cairo_create(s);
    // The surface is created transparent; fill the tip-down triangle path with
    // antialiasing on. On an A8 target only the source alpha matters, so a solid
    // alpha-1 source paints coverage 1 in the interior and the rasterizer's
    // fractional coverage along the two slopes.
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    cairo_move_to(cr, 0.0, 0.0);
    cairo_line_to(cr, static_cast<double>(w), 0.0);
    cairo_line_to(cr, static_cast<double>(w) / 2.0, static_cast<double>(h));
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    return s;
}

// Build (or return the cached) antialiased tip-down triangle mask for the
// current H, stamped by the playhead cursor's per-frame redraw.
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

double lane_text_left_x_at_frame(
    const AppState& app, const GuiAudio& audio,
    double source_frame, size_t glyph_count)
{
    const double advance = monospace_advance();
    if (advance <= 0.0) return -1.0;
    // The marker's painted pixel column (window coords) via the painters' own
    // math (painted_column_of_source_frame). BASIS CONTRACT: the lane run
    // annotates painted flag pixels, so it must read the SAME map those pixels
    // were painted with — displayed_or_live_target_map, the event-synchronized
    // basis the hit tests use (identity/empty in source view; in target view the
    // map the last committed frame's flag cache baked, with the live map as the
    // cold-state fallback). Reading the live map instead would center the run on
    // the NEW column during an async target-map republish while the flag still
    // paints at the OLD one, so the run would visibly jump off its flag until the
    // worker caught up. The frame is the marker's authored source frame; both
    // marker columns translate through this same map.
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const int col = painted_column_of_source_frame(
        app, audio, source_frame, map);
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

double lane_text_left_x(
    const AppState& app, const GuiAudio& audio,
    int marker_idx, size_t glyph_count)
{
    const auto& mv = app.warpmarkers.markers();
    if (marker_idx < 0 ||
        marker_idx >= static_cast<int>(mv.size())) return -1.0;
    return lane_text_left_x_at_frame(
        app, audio, static_cast<double>(mv[marker_idx].time_frame),
        glyph_count);
}

double flag_pending_text_left_x(
    const AppState& app, const GuiAudio& audio,
    int marker_idx)
{
    return lane_text_left_x(app, audio, marker_idx,
                            app.top_flag_editor.pending.size());
}

LaneTextRun current_marker_lane_run(const AppState& app, const GuiAudio& audio)
{
    // The non-editor arbitration paint_marker_text_lane's tiers own, factored
    // out verbatim so the paint pass and the unified marker hit resolver
    // (marker_hit_at) agree on one run. The FlagPayload editor case is resolved
    // by the callers before this point (it owns the lane alone); this covers
    // only the hover / last-selected tiers.
    LaneTextRun run;

    // Tier 1: the HOVERED marker's own value wins whenever a hover is showing.
    // recompute_hover_at_cursor already composed lane_text (flag_text_iter for a
    // warp marker, the "p" literal for a phase reset) and captured the hovered
    // marker's index and source_frame. No painted-column cull here — matching
    // paint, a shown hover always paints (subject only to the caller's advance
    // guard).
    if (!app.hover_popup.lane_text.empty()) {
        run.valid        = true;
        run.marker_index = app.hover_popup.marker_index;
        run.source_frame = static_cast<double>(app.hover_popup.source_frame);
        run.text         = app.hover_popup.lane_text;
        return run;
    }

    // Tier 2: else the LAST-SELECTED marker's own value, composed from the live
    // store the same way the hover composer does — flag_text_iter for a warp
    // marker, the "p" literal for a phase reset. The index is validated against
    // the active view's list.
    const int idx = app.last_selected_marker;
    if (idx < 0) return run;
    int64_t     src_f;
    std::string txt;
    if (app.active_markers_view == 'P') {
        const auto& pv = app.phaseresetmarkers.markers();
        if (idx >= static_cast<int>(pv.size())) return run;
        src_f = pv[idx].time_frame;
        txt   = "p";
    } else {
        const auto& mv = app.warpmarkers.markers();
        if (idx >= static_cast<int>(mv.size())) return run;
        src_f = mv[idx].time_frame;
        txt   = flag_text_iter(mv, idx, app.iteration_mode_enabled);
    }
    // During an active marker drag, center the run on the dragged member's live
    // proposed position (a free source-frame double) instead of the committed
    // store frame — the store is not mutated until commit, so the run would
    // otherwise lag at the pre-drag spot while the flag slides. A GROUP drag
    // seeds every selected member, so this is a MEMBERSHIP lookup (the
    // DragOverlay::effective_time shape), not moveable_times[0]: it substitutes
    // the proposed time for whichever dragged member this last-selected run
    // shows, and falls back to the committed frame for a non-member. From the
    // THRESHOLD CROSSING on (begin_drag focuses the grabbed marker, including a
    // wall-saturated drag with no moved motion) the focused (last-selected)
    // marker IS the grabbed one, so the run tracks the grabbed member; any other
    // member would too if it were focused.
    double display_src_f = static_cast<double>(src_f);
    if (app.drag.active) {
        DragOverlay overlay{&app.drag.dragging_markers,
                            &app.drag.moveable_times};
        display_src_f = overlay.effective_time(idx, display_src_f);
    }
    // Cull to the visible strip by the painted column exactly as the flags do
    // (a fully-offscreen marker paints no flag, so it shows no run). The column
    // basis is displayed_or_live_target_map, the same basis the flag/lane
    // painters and hit tests use.
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const int col = painted_column_of_source_frame(
        app, audio, display_src_f, map);
    if (col < 0 || col >= waveform_area(app).w) return run;

    run.valid        = true;
    run.marker_index = idx;
    run.source_frame = display_src_f;
    run.text         = std::move(txt);
    return run;
}

MarkerHit marker_hit_at(const AppState& app, const GuiAudio& audio,
                        int x, int y) {
    MarkerHit h;
    // The flag lane and the marker-text lane are disjoint y-bands, so at most
    // one of the two tests can hit; the flag test runs first only to settle
    // on_flag directly.
    const int flag = hit_test_flag(app, audio, x, y);
    if (flag >= 0) {
        h.index   = flag;
        h.on_flag = true;
        return h;
    }
    const LaneTextRun run = current_marker_lane_run(app, audio);
    if (!run.valid) return h;
    const double advance = monospace_advance();
    if (advance <= 0.0) return h;
    // left < 0 means the monospace advance is not yet measured — no run to hit.
    const double left = lane_text_left_x_at_frame(
        app, audio, run.source_frame, run.text.size());
    if (left < 0.0) return h;
    const GuiRect lane  = top_marker_text_row_area(app);
    const double  run_w = static_cast<double>(run.text.size()) * advance;
    if (y >= lane.y && y < lane.y + lane.h &&
        static_cast<double>(x) >= left &&
        static_cast<double>(x) <= left + run_w) {
        h.index = run.marker_index;
    }
    return h;
}
