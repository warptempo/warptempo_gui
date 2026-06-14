#include "render.h"
#include "app_state.h"
#include "audio.h"
#include "time_format.h"
#include "timemap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace perf_counters {
    int wf_cols              = 0;
    int wf_pyramid_samples   = 0;
    int flag_measure         = 0;
    int flag_drawn           = 0;
    int flag_elided          = 0;
}

// kFlagBottomLiftPx and kStemAboveWaveformPx now live in
// render.h so the iter/BPM popups in main.cpp and the stem blit in
// paint_handler.cpp can reference the same values.

// kPlayheadHalfPx is the half-width of the inverted-triangle playhead asset
// (19×10, tip at column 9); it now lives in render.h as a single inline
// constexpr shared by this TU's cull and main.cpp's invalidation.

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
        char tbuf[32];
        std::snprintf(tbuf, sizeof(tbuf), "%.2f", m.tempo_base);
        text = tbuf;
        if (!m.tempo_scale.empty()) {
            text += "*";
            text += m.tempo_scale;
        }
    }
    if (!m.label_def.empty()) {
        text += ":";
        text += m.label_def;
    }
    return text;
}

// Forward-translate a per-marker effective time (seconds) to the paint-
// sample position used by the stem, flag, and hit-rect loops. In target
// view (timemap non-null/non-empty) the source-frame is rounded with
// banker's nearbyint and looked up through map_source_to_target; in
// source view (null/empty timemap) the result is eff_time * sr rounded
// to the nearest frame with nearbyint, matching the integer frame the
// playhead cursor and the engine use, so the stem, chip, and hit rect
// share the cursor's column. Callers that need an integer sample-frame
// for trim or viewport arithmetic apply their own nearbyint to the
// returned double; rounding an already-integer-valued double is a no-op.
static inline double sec_to_paint_sample(
    double eff_time,
    double sr,
    const std::vector<TimeMapSegment>* timemap) {
    if (timemap && !timemap->empty()) {
        const size_t src_frame = static_cast<size_t>(
            std::nearbyint(eff_time * sr));
        return map_source_to_target(src_frame, *timemap);
    }
    return std::nearbyint(eff_time * sr);
}

// Shared stem-painting loop used by render_markers and
// render_phase_reset_markers. The only meaningful difference between the
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
    const std::vector<TimeMapSegment>* timemap,
    const DragOverlay* drag_overlay,
    IsVisuallyDisabled&& is_disabled) {
    if (waveform_area.w <= 0 || waveform_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (sample_rate <= 0) return;

    const double span = static_cast<double>(viewport_end_sample -
                                            viewport_start_sample);
    const double samples_per_pixel = span / static_cast<double>(waveform_area.w);
    if (samples_per_pixel <= 0.0) return;

    const double sr = static_cast<double>(sample_rate);
    // Stem emanates from the flag chip's bottom edge (same column as the
    // marker) and runs down to the waveform bottom. The stem is an extension
    // of the flag: its top originates at the chip bottom, not from an
    // independent waveform-relative offset. See flag_chip_bottom_y in render.h.
    const double y_stem_top = flag_chip_bottom_y(waveform_area, ChipRow::Lower);
    const double y1 = static_cast<double>(waveform_area.y + waveform_area.h);

    cairo_save(cr);
    cairo_set_line_width(cr, 1.0);

    // Per-marker color picks {kMarker, kSelected} from selected_set.
    // Disabled markers are skipped entirely (no stem). Brief C retired
    // the out-of-trim dim, so there is a single pass. Per-marker stroke
    // is fine at editor marker counts; do not introduce batching without
    // profiling.
    for (size_t i = 0; i < markers.size(); ++i) {
        if (is_disabled(static_cast<int>(i))) continue;
        const auto& m = markers[i];
        // Effective time: when a drag is active and this marker is
        // in the overlay, read its proposed time from the overlay
        // instead of the live store. The frozen timemap (passed in
        // via `timemap`) is the matching pre-drag coordinate system
        // for forward translation.
        const double eff_time = drag_overlay
            ? drag_overlay->effective_time(
                  static_cast<int>(i), m.time_seconds)
            : m.time_seconds;
        // Translate per-marker source-frame to target-frame in target view
        // (timemap non-null/non-empty); identity otherwise.
        const double ms = sec_to_paint_sample(eff_time, sr, timemap);
        if (ms < static_cast<double>(viewport_start_sample)) continue;
        if (ms >= static_cast<double>(viewport_end_sample)) continue;
        const GuiColor c = selected_set.count(static_cast<int>(i)) > 0
            ? kSelected : kMarker;
        cairo_set_source_rgb(cr, c.r, c.g, c.b);
        const double x_raw =
            (ms - static_cast<double>(viewport_start_sample))
                / samples_per_pixel;
        const double x_px = waveform_area.x + std::round(x_raw) + 0.5;
        cairo_move_to(cr, x_px, y_stem_top);
        cairo_line_to(cr, x_px, y1);
        cairo_stroke(cr);
    }

    cairo_restore(cr);
}

} // namespace

std::string flag_text_for_marker(const std::vector<GuiWarpMarker>& markers, int idx) {
    if (idx < 0 || idx >= static_cast<int>(markers.size())) return {};
    return flag_text(markers, idx);
}

// Brief D: the single iteration-aware text composer. Returns the plain
// flag_text for ineligible markers or when iteration mode is off; for an
// eligible owning marker with iteration on, splices the inline bracket
// after `tempo_base` and before any `*scale`/`:label`
// (e.g. `1.23+[+1.50, -0.50]*1.2345:a.aa`). All warp flag callers route
// through here so display, hit-rects, and the editor seed stay in sync.
std::string flag_text_iter(const std::vector<GuiWarpMarker>& markers,
                           int idx, bool iteration_on) {
    if (idx < 0 || idx >= static_cast<int>(markers.size())) return {};
    const auto& m = markers[idx];
    if (!iteration_on || !iter_popup_eligible_marker(m)) {
        return flag_text(markers, idx);
    }
    // Eligible owning marker (tempo_inherits == false, no label_ref):
    // tempo, then the bracket, then optional scale and label.
    char tbuf[32];
    std::snprintf(tbuf, sizeof(tbuf), "%.2f", m.tempo_base);
    std::string text = tbuf;
    text += format_iter_bracket_inline(m);
    if (!m.tempo_scale.empty()) {
        text += "*";
        text += m.tempo_scale;
    }
    if (!m.label_def.empty()) {
        text += ":";
        text += m.label_def;
    }
    return text;
}

double resolve_inherited_tempo(const std::vector<GuiWarpMarker>& markers, int index) {
    for (int i = index - 1; i >= 0; --i) {
        const auto& m = markers[i];
        if (!m.tempo_inherits && m.label_ref.empty()) {
            return m.tempo_base;
        }
    }
    return 1.0;
}

std::string resolve_inherited_tempo_scale(
    const std::vector<GuiWarpMarker>& markers, int index) {
    for (int i = index - 1; i >= 0; --i) {
        const auto& m = markers[i];
        if (!m.tempo_inherits && m.label_ref.empty()) {
            return m.tempo_scale;
        }
    }
    return {};
}

// X.7.8b-3: promoted from an inline function at the original
// main.cpp:112-200 anonymous namespace. Body is verbatim — no
// captures, no identifier rewrites needed.
std::string compute_hover_popup_text(
    const std::vector<GuiWarpMarker>& mv, int idx, int sample_rate) {
    if (idx < 0 || idx >= static_cast<int>(mv.size())) return "";
    const GuiWarpMarker& m = mv[idx];

    if (m.tempo_inherits) {
        // resolve_inherited_tempo walks backward from `walk-1`. Starting
        // at idx+1 lets it return idx's resolved tempo if idx happens to
        // be the only inheriting marker in front of an owning origin.
        const int walk = idx + 1;
        const double tval = resolve_inherited_tempo(mv, walk);
        const std::string sc = resolve_inherited_tempo_scale(mv, walk);
        char tbuf[32];
        std::snprintf(tbuf, sizeof(tbuf), "%.2f", tval);
        std::string out = "= ";
        out += tbuf;
        if (!sc.empty()) {
            out += "*";
            out += sc;
        }
        return out;
    }

    if (!m.label_ref.empty()) {
        int def_idx = -1;
        for (int i = 0; i < static_cast<int>(mv.size()); ++i) {
            if (mv[i].label_def == m.label_ref) {
                def_idx = i;
                break;
            }
        }
        if (def_idx < 0) return "";
        if (def_idx + 1 >= static_cast<int>(mv.size())) return "";
        if (idx     + 1 >= static_cast<int>(mv.size())) return "";
        const double sr_d = static_cast<double>(sample_rate);
        if (sr_d <= 0.0) return "";

        const double lr_src_dist =
            (mv[idx + 1].time_seconds - mv[idx].time_seconds) * sr_d;
        const double def_src_dist =
            (mv[def_idx + 1].time_seconds - mv[def_idx].time_seconds) * sr_d;
        if (def_src_dist <= 0.0 || lr_src_dist <= 0.0) return "";

        const GuiWarpMarker& def = mv[def_idx];
        double      def_base;
        std::string def_scale_str;
        bool        def_has_typed_scale;
        if (def.tempo_inherits) {
            // Pass-def: fall back to inheritance walk. The resolved tempo
            // is treated as a fully-effective number with no separate
            // typed scale (inheritance returns base*scale).
            def_base = resolve_inherited_tempo(mv, def_idx);
            def_scale_str = "";
            def_has_typed_scale = false;
        } else {
            def_base = def.tempo_base;
            def_scale_str = def.tempo_scale;
            def_has_typed_scale = !def_scale_str.empty();
        }
        double def_scale_val = 1.0;
        if (def_has_typed_scale) {
            try { def_scale_val = std::stod(def_scale_str); }
            catch (...) { def_scale_val = 1.0; }
        }
        const double def_eff_tempo = def_base * def_scale_val;
        if (def_base == 0.0 || def_eff_tempo == 0.0) return "";

        // settings.scale cancels in the engine's multiplier expression:
        //   multiplier = (lr_src_dist * def_eff_tempo)
        //              / (def_base * def_src_dist)
        const double multiplier =
            (lr_src_dist * def_eff_tempo) / (def_base * def_src_dist);
        const double combined_scale = def_has_typed_scale
            ? (def_scale_val * multiplier)
            : multiplier;

        char base_buf[32];
        std::snprintf(base_buf, sizeof(base_buf), "%.2f", def_base);
        char scale_buf[32];
        std::snprintf(scale_buf, sizeof(scale_buf), "%.4f", combined_scale);
        std::string out = "~= ";
        out += base_buf;
        out += "*";
        out += scale_buf;
        return out;
    }

    return "";
}

void render_background(cairo_t* cr, int x, int y, int w, int h) {
    cairo_save(cr);
    cairo_set_source_rgb(cr, kBackground.r, kBackground.g, kBackground.b);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_restore(cr);
}

void render_status_message(cairo_t* cr, GuiRect area, const char* msg) {
    if (!msg || area.w <= 0 || area.h <= 0) return;
    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, kFlagFontSize);
    // Monospace arithmetic for the width (glyph count * monospace_advance()),
    // the same convention as the flag and editor paths; centered horizontally
    // in `area` and vertically on its mid-line. Antialiasing stays on — this
    // is glyph text.
    const double text_w =
        static_cast<double>(std::strlen(msg)) * monospace_advance();
    const double tx = area.x + (area.w - text_w) * 0.5;
    const double ty = area.y + area.h * 0.5
                    + monospace_row_baseline_offset() - monospace_row_h() * 0.5;
    cairo_set_source_rgb(cr, kText.r, kText.g, kText.b);
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, msg);
    cairo_restore(cr);
}

void render_waveform(cairo_t* cr,
                     GuiRect area,
                     const GuiAudio& audio,
                     int channel,
                     long long viewport_start_sample,
                     long long viewport_end_sample,
                     GuiColor color,
                     const std::vector<TimeMapSegment>* timemap) {
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
    // In target view (timemap != nullptr) `samples_per_pixel` is in
    // target-frame units. Brief 1 accepts the resulting imprecision —
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
    // halving the timemap calls per column in target view (no-op in source view).
    double f_prev = static_cast<double>(viewport_start_sample);
    double g_prev = timemap ? map_target_to_source(
                        static_cast<size_t>(f_prev < 0.0 ? 0.0 : f_prev),
                        *timemap)
                            : f_prev;
    for (int i = 0; i < area.w; i++) {
        const double f1 = static_cast<double>(viewport_start_sample) +
                          (span * (i+1)) / area.w;
        // Target view: translate each column's [t0, t1) endpoint into
        // source-frame via the timemap so the pyramid read lands at the
        // matching authored audio. Source view: identity.
        const double g0 = g_prev;
        const double g1 = timemap ? map_target_to_source(
                              static_cast<size_t>(f1 < 0.0 ? 0.0 : f1),
                              *timemap)
                                  : f1;
        const long long s0 = static_cast<long long>(std::nearbyint(g0));
        long long       s1 = static_cast<long long>(std::nearbyint(g1));
        if (s1 <= s0) s1 = s0 + 1;

        const auto mm = audio.get_peak_range(channel, level, s0, s1);
        const double min_val = mm.first;
        const double max_val = mm.second;

        if constexpr (kDebugPerf) {
            perf_counters::wf_cols++;
            if (level <= 0) {
                perf_counters::wf_pyramid_samples +=
                    static_cast<int>(s1 - s0);
            } else {
                // Strides match audio.cpp's kStrides[].
                constexpr long long kCacheStrides[] = { 0, 32, 1024, 32768 };
                const long long stride = kCacheStrides[level];
                const long long i0 = s0 / stride;
                const long long i1 = (s1 + stride - 1) / stride;
                perf_counters::wf_pyramid_samples +=
                    static_cast<int>(i1 - i0);
            }
        }

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
                     cairo_surface_t* triangle_surface,
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
    if (playhead_pixel_x < -static_cast<double>(kPlayheadHalfPx)) return;
    if (playhead_pixel_x > static_cast<double>(area.w - 1 + kPlayheadHalfPx)) return;

    const double col  = std::floor(playhead_pixel_x + 0.5);
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
            const int plate_w = cairo_image_surface_get_width(ink_plate);
            const int plate_h = cairo_image_surface_get_height(ink_plate);
            const int icol    = static_cast<int>(col);
            // Silent, safe fallback: a non-ARGB32 plate or an out-of-range
            // column leaves the plain green line untouched.
            if (cairo_image_surface_get_format(ink_plate) == CAIRO_FORMAT_ARGB32
                && icol >= 0 && icol < plate_w) {
                const unsigned char* data =
                    cairo_image_surface_get_data(ink_plate);
                const int stride = cairo_image_surface_get_stride(ink_plate);
                const int y_max =
                    std::min(area.h, plate_h);  // exclusive upper bound

                // Collect contiguous ink runs down the column, then fill them
                // all in one batch. The plate is hard-aliased (alpha 0 or 255);
                // the >127 threshold is just a guard. Runs are emitted raw —
                // no smoothing, merging, or padding — so sparse line-art
                // material speckles green/background pixel by pixel, the
                // accepted behavior of the per-pixel variant. The union of the
                // green line above and these runs covers exactly the same
                // pixels the single stroke covered.
                cairo_set_source_rgb(cr, kBackground.r, kBackground.g,
                                     kBackground.b);
                int run_start = -1;
                for (int y = 0; y < y_max; ++y) {
                    const bool ink =
                        data[y * stride + icol * 4 + 3] > 127;
                    if (ink && run_start < 0) {
                        run_start = y;
                    } else if (!ink && run_start >= 0) {
                        cairo_rectangle(cr,
                                        static_cast<double>(area.x) + col,
                                        static_cast<double>(area.y + run_start),
                                        1.0,
                                        static_cast<double>(y - run_start));
                        run_start = -1;
                    }
                }
                if (run_start >= 0) {
                    cairo_rectangle(cr,
                                    static_cast<double>(area.x) + col,
                                    static_cast<double>(area.y + run_start),
                                    1.0,
                                    static_cast<double>(y_max - run_start));
                }
                cairo_fill(cr);
            }
        }
    }

    // Inverted-triangle indicator: stamped from a hand-authored PNG mask so
    // every pixel is explicit (no rasterizer ambiguity). Asset is 19x10 with
    // the tip at column index 9 (image-local); integer division places that
    // tip column at `area.x + col`. The triangle now sits INSIDE the top
    // waveform inset band (kWaveformInsetPx tall, equal to the asset height):
    // top row at area.y, tip (bottom row) at the first drawn sample row. The
    // samples are inset by exactly the asset height in
    // render_waveform_to_cache_surface, so the triangle's band and the
    // sample-free band coincide. Skipped for the scanner call
    // (draw_triangle=false): the triangle belongs to the cursor exclusively
    // under the split-playhead model. The scanner line therefore reads as
    // running ~kWaveformInsetPx longer than the cursor's, because the cursor's
    // top band is visually occupied by the triangle while the scanner's is a
    // bare line — both lines actually span the identical full area height; this
    // is expected, not a defect.
    if (draw_triangle && triangle_surface) {
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
                    const std::vector<TimeMapSegment>* timemap,
                    const DragOverlay* drag_overlay) {
    render_marker_stems_impl(
        cr, waveform_area, markers,
        viewport_start_sample, viewport_end_sample,
        sample_rate, selected_set, timemap, drag_overlay,
        [&](int i) {
            return effective_disabled(markers, i);
        });
}

void render_trim_stems(cairo_t* cr,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim,
                       bool has_begin,
                       bool begin_selected,
                       bool has_end,
                       bool end_selected) {
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

    auto paint_bound = [&](int64_t frame, bool selected) {
        const double ms = static_cast<double>(frame);
        if (ms < static_cast<double>(viewport_start_sample)) return;
        if (ms >= static_cast<double>(viewport_end_sample)) return;
        const GuiColor c = selected ? kSelected : kTrimMarker;
        cairo_set_source_rgb(cr, c.r, c.g, c.b);
        const double x_raw =
            (ms - static_cast<double>(viewport_start_sample))
                / samples_per_pixel;
        const double x_px = waveform_area.x + std::round(x_raw) + 0.5;
        cairo_move_to(cr, x_px, y_stem_top);
        cairo_line_to(cr, x_px, y1);
        cairo_stroke(cr);
    };

    if (has_begin) paint_bound(trim.begin, begin_selected);
    if (has_end)   paint_bound(trim.end, end_selected);

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
                       bool begin_selected,
                       bool has_end,
                       bool end_selected) {
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (!has_begin && !has_end) return;

    const double span = static_cast<double>(viewport_end_sample -
                                            viewport_start_sample);
    const double samples_per_pixel =
        span / static_cast<double>(top_strip_area.w);
    if (samples_per_pixel <= 0.0) return;

    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);

    const double hl_pad = kFlagPadXPx;

    // Chip bottom = the UPPER-row chip bottom; solve baseline_y exactly as
    // iterate_visible_flags_impl does for the lower row, one row higher. The
    // box is monospace_row_h() tall (Defect B) and baseline sits
    // monospace_row_baseline_offset() below its top, so placing the box bottom
    // (= baseline_y - baseline_off + row_h) at flag_chip_bottom_y gives this
    // baseline. The chip thus fills its full upper-row slot.
    const double baseline_y =
        flag_chip_bottom_y(waveform_area, ChipRow::Upper)
      - static_cast<double>(monospace_row_h())
      + monospace_row_baseline_offset();

    // Column placement mirrors render_trim_stems / render_flags: text_left at
    // the bound's integer pixel column, chip extends right via the shared
    // text-box primitive. Color mirrors the stem (selected ? kSelected :
    // kTrimMarker) so chip and stem are one continuous unit.
    // Build the visible-bounds list, sorted by painted column ascending.
    // Order is by position, NOT begin/end identity: in target view or a
    // degenerate inverted trim the painted order can differ from b/e.
    struct TrimChip {
        double      text_left;
        bool        selected;
        const char* glyph;
    };
    std::vector<TrimChip> chips;
    auto add_chip = [&](int64_t frame, bool selected, const char* glyph) {
        const double ms = static_cast<double>(frame);
        if (ms < static_cast<double>(viewport_start_sample)) return;
        if (ms >= static_cast<double>(viewport_end_sample)) return;
        const double x_raw =
            (ms - static_cast<double>(viewport_start_sample))
                / samples_per_pixel;
        const double text_left =
            static_cast<double>(top_strip_area.x) + std::round(x_raw);
        chips.push_back({text_left, selected, glyph});
    };
    if (has_begin) add_chip(trim.begin, begin_selected, "b");
    if (has_end)   add_chip(trim.end, end_selected, "e");
    std::sort(chips.begin(), chips.end(),
              [](const TrimChip& a, const TrimChip& b) {
                  return a.text_left < b.text_left;
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
        box.fill        = chip.selected ? kSelected : kTrimMarker;
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

    // Defect B (F.trim.3): the step-1 fill box fills its full row slot rather
    // than the tight glyph bounding box — that geometry now lives entirely
    // inside flag_chip_rect (height = cached monospace_row_h(), top = baseline
    // lifted by monospace_row_baseline_offset()), so baseline_y sits centered
    // in the row and the box bottom lands flush at the slot bottom. Callers
    // solve baseline_y so the box bottom coincides with flag_chip_bottom_y
    // (chips) or the row rect (bottom-strip editors).
    //
    // The cursor (step 5) and the selection highlight (step 4) span exactly the
    // glyph ink band (ascent-to-descent), no vertical padding. The band is
    // recovered from the two cached monospace metrics (exact inverses of how
    // init_monospace_grid_metrics built them: g_row_baseline_off = kFlagPadYPx
    // + ascent, g_row_h = round(font_height + 2*kFlagPadYPx)). The round() on
    // the row height can leak a sub-pixel into the derived descent; that is
    // cosmetically irrelevant here and saves adding a new metric accessor.
    const double bg_h        = static_cast<double>(monospace_row_h());
    const double ascent      = monospace_row_baseline_offset() - kFlagPadYPx;
    const double font_height = bg_h - 2.0 * kFlagPadYPx;
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
    //    hl_pad (the renderers pass anchor_x = text_left + kFlagPadXPx; prefix-
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
    const MarkerVec& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    const std::vector<TimeMapSegment>* timemap,
    const DragOverlay* drag_overlay,
    FlagTextFn&& get_flag_text,
    Emit&& emit) {
    const double span = static_cast<double>(viewport_end_sample -
                                            viewport_start_sample);
    const double samples_per_pixel = span / static_cast<double>(top_strip_area.w);
    if (samples_per_pixel <= 0.0) return;

    const double sr           = static_cast<double>(sample_rate);
    // Place the rect's bottom edge exactly at the flag chip bottom (the
    // single source of truth shared with the stem renderers). The strip
    // bottom is the waveform area top, since the strips are contiguous, so
    // flag_chip_bottom_y reads off that boundary. Defect B (F.trim.3): the box
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

    double rightmost_right_edge = -1e18;

    for (size_t i = 0; i < markers.size(); ++i) {
        const auto& m = markers[i];
        // Effective time: drag overlay > live store. The frozen timemap
        // supplied via `timemap` is the matching pre-drag coordinate
        // system for target-view forward translation.
        const double eff_time = drag_overlay
            ? drag_overlay->effective_time(
                  static_cast<int>(i), m.time_seconds)
            : m.time_seconds;
        // Translate per-marker source-frame to target-frame in target view
        // (timemap non-null/non-empty); identity otherwise. Pack/elision
        // walk left-to-right against post-translation positions.
        const double ms = sec_to_paint_sample(eff_time, sr, timemap);
        if (ms < static_cast<double>(viewport_start_sample)) continue;
        if (ms >= static_cast<double>(viewport_end_sample)) continue;

        const double x_raw =
            (ms - static_cast<double>(viewport_start_sample)) /
            samples_per_pixel;
        const double text_left =
            static_cast<double>(top_strip_area.x) + std::round(x_raw);
        // Elide only on genuine overlap: a candidate is dropped only when its
        // chip left edge (text_left - kFlagPadXPx) would fall left of the
        // previous chip's right edge. Adjacent chips may share an edge (touch)
        // without being elided — there is no inter-chip gutter. Reintroducing
        // one is a single added term on the right-hand side here.
        if (text_left < rightmost_right_edge + kFlagPadXPx) {
            if constexpr (kDebugPerf) perf_counters::flag_elided++;
            continue;
        }

        const std::string text = get_flag_text(static_cast<int>(i));
        if (text.empty()) continue;

        const double x_advance =
            static_cast<double>(text.length()) * monospace_advance();

        emit(static_cast<int>(i), text_left, baseline_y, text);
        rightmost_right_edge = text_left + x_advance + kFlagPadXPx;
    }
}

// Stage C: paint body shared between render_flags' paint_one lambda and
// render_one_editor_flag. Inputs (i, text_left, baseline_y, text, ext)
// match the iterate_visible_flags_impl emit signature; the remaining
// args carry through what was lambda-captured before. Behavior is
// byte-identical to the pre-Stage-C paint_one lambda inside render_flags.
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

    // Fill table (Brief B.2): parse-fail > selected > default(kMarker).
    // Brief C: trim membership no longer dims the chip.
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

    if constexpr (kDebugPerf) perf_counters::flag_drawn++;
}

} // namespace

void render_flags(cairo_t* cr,
                  GuiRect top_strip_area,
                  const std::vector<GuiWarpMarker>& markers,
                  long long viewport_start_sample,
                  long long viewport_end_sample,
                  int sample_rate,
                  double font_size,
                  const std::set<int>& selected_set,
                  const FlagEditorOverlay& editor,
                  const std::vector<TimeMapSegment>* timemap,
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

    const double hl_pad = kFlagPadXPx;

    // Brief Y.5: collect emit args during the left-to-right iterate pass,
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
    iterate_visible_flags_impl(top_strip_area, markers,
                               viewport_start_sample, viewport_end_sample,
                               sample_rate, timemap, drag_overlay,
        [&](int i) {
            return flag_text_iter(markers, i, iteration_on);
        },
        [&](int i, double text_left, double baseline_y,
            const std::string& text) {
            // Stage C: skip-guard. The flag-cache rebuild passes the
            // FlagPayload-editor target through editor.marker_index so
            // this branch fires and the cache leaves a transparent hole
            // over the editor target's pixel column; the live editor
            // render owns those pixels via render_one_editor_flag.
            // Defensive against pre-Stage-C callers as well — when
            // editor.marker_index == -1 (the default), the guard never
            // fires, and behavior is identical to the pre-Stage-C path.
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
    const std::vector<GuiWarpMarker>& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::set<int>& selected_set,
    const FlagEditorOverlay& editor,
    const std::vector<TimeMapSegment>* timemap,
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

    const double hl_pad = kFlagPadXPx;

    iterate_visible_flags_impl(top_strip_area, markers,
                               viewport_start_sample, viewport_end_sample,
                               sample_rate, timemap, drag_overlay,
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
    const MarkerVec& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::vector<TimeMapSegment>* timemap,
    const DragOverlay* drag_overlay,
    FlagTextFn&& get_flag_text) {
    std::vector<FlagHitRect> out;
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return out;
    if (viewport_end_sample <= viewport_start_sample) return out;
    if (sample_rate <= 0) return out;
    (void)font_size;

    // Mirror render_flags: uniform y/height for the hit rect so clicks
    // register consistently across flag types. The rect comes from the shared
    // flag_chip_rect helper, the same one render_editor_text_box fills, so the
    // painted chip and this hit rect are the same rectangle by construction
    // (Defect B / F-flaggeom).
    iterate_visible_flags_impl(top_strip_area, markers,
                               viewport_start_sample, viewport_end_sample,
                               sample_rate, timemap, drag_overlay,
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
    const std::vector<GuiWarpMarker>& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::vector<TimeMapSegment>* timemap,
    const DragOverlay* drag_overlay,
    bool iteration_on) {
    return compute_flag_hit_rects_impl(top_strip_area, markers,
        viewport_start_sample, viewport_end_sample,
        sample_rate, font_size, timemap, drag_overlay,
        [&](int i) {
            return flag_text_iter(markers, i, iteration_on);
        });
}

// ---------- Phase reset marker rendering (chunk S.2.2) ----------

namespace {

std::string phase_reset_flag_text(const GuiPhaseResetMarker&) {
    // The phase-reset chip is an invariable single `p`: the peak/heap/pass
    // phase-MODEL concept was removed once heap became the sole engine. One
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

    if constexpr (kDebugPerf) perf_counters::flag_drawn++;
}

} // namespace

void render_phase_reset_markers(cairo_t* cr,
                              GuiRect waveform_area,
                              const std::vector<GuiPhaseResetMarker>& phase_resets,
                              long long viewport_start_sample,
                              long long viewport_end_sample,
                              int sample_rate,
                              const std::set<int>& selected_set,
                              const std::vector<TimeMapSegment>* timemap,
                              const DragOverlay* drag_overlay) {
    render_marker_stems_impl(
        cr, waveform_area, phase_resets,
        viewport_start_sample, viewport_end_sample,
        sample_rate, selected_set, timemap, drag_overlay,
        [&](int i) {
            return phase_resets[i].disabled;
        });
}

void render_phase_reset_flags(cairo_t* cr,
                            GuiRect top_strip_area,
                            const std::vector<GuiPhaseResetMarker>& phase_resets,
                            long long viewport_start_sample,
                            long long viewport_end_sample,
                            int sample_rate,
                            double font_size,
                            const std::set<int>& selected_set,
                            const std::vector<TimeMapSegment>* timemap,
                            const DragOverlay* drag_overlay) {
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (sample_rate <= 0) return;

    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);

    const double hl_pad = kFlagPadXPx;

    // Brief Y.5: collect-then-reverse-paint, mirroring render_flags. With no
    // per-flag editor every visible flag paints straight into the cache.
    struct PhaseResetEmit {
        int                  i;
        double               text_left;
        double               baseline_y;
        std::string          text;
    };
    std::vector<PhaseResetEmit> emits;
    iterate_visible_flags_impl(top_strip_area, phase_resets,
                               viewport_start_sample, viewport_end_sample,
                               sample_rate, timemap, drag_overlay,
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
    const std::vector<GuiPhaseResetMarker>& phase_resets,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::vector<TimeMapSegment>* timemap,
    const DragOverlay* drag_overlay) {
    return compute_flag_hit_rects_impl(top_strip_area, phase_resets,
        viewport_start_sample, viewport_end_sample,
        sample_rate, font_size, timemap, drag_overlay,
        [&](int i) {
            return phase_reset_flag_text(phase_resets[i]);
        });
}

namespace {
    double g_advance = 0.0;
    int    g_row_h            = kRowHFallbackPx;
    double g_row_baseline_off = kRowBaselineOffFallbackPx;
    bool   g_metrics_initialized = false;
} // namespace

double monospace_advance() { return g_advance; }
int    monospace_row_h()   { return g_row_h; }
double monospace_row_baseline_offset() { return g_row_baseline_off; }

void init_monospace_grid_metrics(cairo_t* cr) {
    if (g_metrics_initialized) return;
    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, kFlagFontSize);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, "M", &ext);
    g_advance = ext.x_advance;
    cairo_font_extents_t fe;
    cairo_font_extents(cr, &fe);
    const double font_height = fe.ascent + fe.descent;
    g_row_h = static_cast<int>(std::nearbyint(
        font_height + 2.0 * kFlagPadYPx));
    g_row_baseline_off = kFlagPadYPx + fe.ascent;
    cairo_restore(cr);
    g_metrics_initialized = true;
}

double flag_pending_text_left_x(
    const AppState& app, const GuiAudio& audio,
    int marker_idx)
{
    const auto& mv = app.warpmarkers.markers();
    if (marker_idx < 0 ||
        marker_idx >= static_cast<int>(mv.size())) return -1.0;
    const GuiRect top = top_strip_area(app);
    if (top.w <= 0) return -1.0;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return -1.0;
    const int64_t vp_start = app.viewport_start_sample;
    const int64_t vp_end = vp_start +
        static_cast<int64_t>(std::nearbyint(spp * top.w));
    const double sr = static_cast<double>(audio.sample_rate());
    // Target view: forward-translate the marker's source-frame through a
    // freshly-built target-view timemap so the visible-range check and
    // x-position math match where render_flags actually paints the flag.
    // Empty / null timemap falls through to identity, matching the
    // render-side helpers' convention. Not reachable mid-drag (begin_drag
    // clears the editor; the click handler exits before any drag begins),
    // so a fresh build is correct and app.drag.frozen_timemap need not be
    // consulted.
    double ms = mv[marker_idx].time_seconds * sr;
    if (app.active_audio_view == 'T') {
        const auto tmap = build_target_view_timemap(
            app, audio.sample_rate(),
            static_cast<long>(audio.total_frames()));
        if (!tmap.empty()) {
            const size_t src_frame = static_cast<size_t>(
                std::nearbyint(mv[marker_idx].time_seconds * sr));
            ms = map_source_to_target(src_frame, tmap);
        }
    }
    if (ms <  static_cast<double>(vp_start)) return -1.0;
    if (ms >= static_cast<double>(vp_end))   return -1.0;
    const double samples_per_pixel =
        static_cast<double>(vp_end - vp_start) /
        static_cast<double>(top.w);
    const double x_raw =
        (ms - static_cast<double>(vp_start)) / samples_per_pixel;
    const double text_left =
        static_cast<double>(top.x) + std::nearbyint(x_raw);
    return text_left + kFlagPadXPx;
}
