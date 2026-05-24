#include "render.h"
#include "app_state.h"
#include "audio.h"
#include "time_format.h"
#include "timemap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace perf_counters {
    int wf_cols              = 0;
    int wf_pyramid_samples   = 0;
    int flag_measure         = 0;
    int flag_drawn           = 0;
    int flag_elided          = 0;
}

// kVPadExtraPx, kFlagBottomLiftPx, and kStemAboveWaveformPx now live in
// render.h so the iter/BPM popups in main.cpp and the stem blit in
// paint_handler.cpp can reference the same values.

// Half-width of the inverted-triangle playhead asset (19×10, tip at column 9).
// Mirrors the same-named constant in main.cpp's invalidation logic — both
// describe the same asset, but each TU holds its own copy because main.cpp's
// version is in an anonymous namespace.
constexpr int kPlayheadHalfPx = 9;

namespace {

// Flag text mirrors the canonical line's PAYLOAD (post-pipe). All
// metadata (b=/e=/#) is invisible in the rect; the `|` separator sits to
// the left of the rect, anchoring it to the marker column. Disabled
// markers are skipped entirely (no stem, no flag); color conveys
// selection (kSelected) and out-of-trim (dim()), not disabled state.
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
// source view (null/empty timemap) the result is the identity
// eff_time * sr. Callers that need an integer sample-frame for trim or
// viewport arithmetic apply their own nearbyint to the returned double.
static inline double sec_to_paint_sample(
    double eff_time,
    double sr,
    const std::vector<TimeMapSegment>* timemap) {
    if (timemap && !timemap->empty()) {
        const size_t src_frame = static_cast<size_t>(
            std::nearbyint(eff_time * sr));
        return map_source_to_target(src_frame, *timemap);
    }
    return eff_time * sr;
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
    const TrimRange& trim,
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
    // Stem emanates from the flag rect's left outline (bottom edge, same
    // column as the marker) and runs down to the waveform bottom. The
    // stroke tops out flush with the flag rect's bottom edge — chip and
    // stem read as one continuous unit. See kStemPaintTopPx in render.h.
    const double y_stem_top =
        static_cast<double>(waveform_area.y) - kStemPaintTopPx;
    const double y1 = static_cast<double>(waveform_area.y + waveform_area.h);

    cairo_save(cr);
    cairo_set_line_width(cr, 1.0);

    // Two passes — split by in-trim vs out-of-trim. Per-marker color
    // picks {kMarker, kSelected} from selected_set; the trim split lets
    // each pass apply (or skip) dim() once. Disabled markers are skipped
    // entirely (no stem). Per-marker stroke is fine at editor marker
    // counts; do not introduce 4-bucket batching without profiling.
    for (int pass = 0; pass < 2; pass++) {
        const bool out_of_trim_pass = (pass == 1);
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
            // (timemap non-null/non-empty); identity otherwise. Viewport and
            // trim are passed in the same domain (source in source view,
            // target in target view), so the comparisons stay consistent.
            const double ms = sec_to_paint_sample(eff_time, sr, timemap);
            if (ms < static_cast<double>(viewport_start_sample)) continue;
            if (ms >= static_cast<double>(viewport_end_sample)) continue;
            const int64_t pos = static_cast<int64_t>(std::nearbyint(ms));
            if (marker_out_of_trim(pos, trim) != out_of_trim_pass) continue;
            GuiColor c = selected_set.count(static_cast<int>(i)) > 0
                ? kSelected : kMarker;
            if (out_of_trim_pass) c = dim(c);
            cairo_set_source_rgb(cr, c.r, c.g, c.b);
            const double x_raw =
                (ms - static_cast<double>(viewport_start_sample))
                    / samples_per_pixel;
            const double x_px = waveform_area.x + std::round(x_raw) + 0.5;
            cairo_move_to(cr, x_px, y_stem_top);
            cairo_line_to(cr, x_px, y1);
            cairo_stroke(cr);
        }
    }

    cairo_restore(cr);
}

} // namespace

std::string flag_text_for_marker(const std::vector<GuiWarpMarker>& markers, int idx) {
    if (idx < 0 || idx >= static_cast<int>(markers.size())) return {};
    return flag_text(markers, idx);
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

void render_progress_bar(cairo_t* cr, int x, int y, int w, int h,
                         float progress_fraction) {
    if (progress_fraction <= 0.0f || w <= 0 || h <= 0) return;
    if (progress_fraction > 1.0f) progress_fraction = 1.0f;
    const int filled = static_cast<int>(progress_fraction * w + 0.5f);
    if (filled <= 0) return;
    cairo_save(cr);
    cairo_set_source_rgb(cr, 0.35, 0.35, 0.40);
    cairo_rectangle(cr, x, y, filled, h);
    cairo_fill(cr);
    cairo_restore(cr);
}

void render_waveform(cairo_t* cr,
                     GuiRect area,
                     const GuiAudio& audio,
                     int channel,
                     long long viewport_start_sample,
                     long long viewport_end_sample,
                     long long trim_begin_sample,
                     long long trim_end_sample,
                     GuiColor bright_color,
                     GuiColor dim_color,
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

    struct ColLine { double x, y0, y1; bool bright; };
    std::vector<ColLine> lines;
    lines.reserve(static_cast<size_t>(area.w));

    for (int i = 0; i < area.w; i++) {
        const double f0 = static_cast<double>(viewport_start_sample) +
                          (span * i)     / area.w;
        const double f1 = static_cast<double>(viewport_start_sample) +
                          (span * (i+1)) / area.w;
        // Target view: translate each column's [t0, t1) endpoint into
        // source-frame via the timemap so the pyramid read lands at the
        // matching authored audio. Source view: identity.
        const double g0 = timemap ? map_target_to_source(
                              static_cast<size_t>(f0 < 0.0 ? 0.0 : f0),
                              *timemap)
                                  : f0;
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

        const double y_top    = y_center - max_val * half_h;
        const double y_bottom = y_center - min_val * half_h;
        const double x_px     = area.x + i + 0.5;

        // Per spec: a column that straddles a trim boundary is assigned by
        // the midpoint of its DOMAIN-frame range. In source view that's
        // the source-sample midpoint; in target view it's the target-frame
        // midpoint. trim_*_sample is interpreted in the same domain as the
        // viewport, so the comparison is correct in both views (no extra
        // translation).
        const long long mid_domain =
            static_cast<long long>(std::nearbyint((f0 + f1) * 0.5));
        const bool bright = (mid_domain >= trim_begin_sample &&
                             mid_domain <  trim_end_sample);

        lines.push_back({x_px, y_top, y_bottom, bright});
    }

    cairo_save(cr);
    cairo_set_line_width(cr, 1.0);

    cairo_set_source_rgb(cr, bright_color.r, bright_color.g, bright_color.b);
    for (const auto& L : lines) {
        if (!L.bright) continue;
        cairo_move_to(cr, L.x, L.y0);
        cairo_line_to(cr, L.x, L.y1);
    }
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, dim_color.r, dim_color.g, dim_color.b);
    for (const auto& L : lines) {
        if (L.bright) continue;
        cairo_move_to(cr, L.x, L.y0);
        cairo_line_to(cr, L.x, L.y1);
    }
    cairo_stroke(cr);

    cairo_restore(cr);
}

void render_playhead(cairo_t* cr,
                     GuiRect area,
                     double  playhead_pixel_x,
                     GuiColor color,
                     cairo_surface_t* triangle_surface,
                     bool draw_triangle) {
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
    }

    // Inverted-triangle indicator: stamped from a hand-authored PNG mask so
    // every pixel is explicit (no rasterizer ambiguity). Asset is 17×9 with
    // the tip at column index 8 (image-local); integer division places that
    // tip column at `area.x + col`. The bottom row sits one pixel above
    // `area.y` so the stem stroke beginning at `area.y` is visually adjacent.
    // Skipped for the scanner call (draw_triangle=false): the triangle
    // belongs to the cursor exclusively under the split-playhead model.
    if (draw_triangle && triangle_surface) {
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
                    const TrimRange& trim,
                    const std::set<int>& selected_set,
                    const std::vector<TimeMapSegment>* timemap,
                    const DragOverlay* drag_overlay) {
    render_marker_stems_impl(
        cr, waveform_area, markers,
        viewport_start_sample, viewport_end_sample,
        sample_rate, trim, selected_set, timemap, drag_overlay,
        [&](int i) {
            return effective_disabled(markers, i);
        });
}

void render_editor_text_box(cairo_t* cr, const EditorTextBox& s) {
    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);

    double prefix_adv = 0.0;
    if (!s.prefix.empty()) {
        cairo_text_extents_t pre_ext;
        cairo_text_extents(cr, s.prefix.c_str(), &pre_ext);
        prefix_adv = pre_ext.x_advance;
    }
    const double editable_left = s.anchor_x + prefix_adv;

    cairo_text_extents_t text_ext;
    cairo_text_extents(cr, s.text.c_str(), &text_ext);

    const double bg_top =
        s.baseline_y + s.uniform_ext.y_bearing - s.hl_pad - kVPadExtraPx;
    const double bg_h =
        s.uniform_ext.height + 2.0 * s.hl_pad + 2.0 * kVPadExtraPx;

    // 1. Solid fill behind the editable region only.
    render_flag_text_bg_fill(cr, editable_left, text_ext.x_advance,
                             bg_top, bg_h, s.fill);

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
    //    the selected substring in the fill color for contrast.
    if (s.has_selection) {
        cairo_text_extents_t a_ext;
        cairo_text_extents(cr,
            s.text.substr(0, static_cast<size_t>(s.selection_start)).c_str(),
            &a_ext);
        cairo_text_extents_t b_ext;
        cairo_text_extents(cr,
            s.text.substr(0, static_cast<size_t>(s.selection_end)).c_str(),
            &b_ext);
        const double hi_x = editable_left + a_ext.x_advance;
        const double hi_w = b_ext.x_advance - a_ext.x_advance;
        cairo_set_source_rgb(cr,
            s.text_color.r, s.text_color.g, s.text_color.b);
        cairo_rectangle(cr, hi_x, bg_top, hi_w, bg_h);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, s.fill.r, s.fill.g, s.fill.b);
        cairo_move_to(cr, hi_x, s.baseline_y);
        cairo_show_text(cr,
            s.text.substr(static_cast<size_t>(s.selection_start),
                          static_cast<size_t>(s.selection_end -
                                              s.selection_start))
                .c_str());
    }

    // 5. Cursor (blink-gated), crisp single-pixel column.
    if (s.cursor_visible) {
        double cursor_x_offset = 0.0;
        if (s.cursor_pos > 0) {
            cairo_text_extents_t pext;
            cairo_text_extents(cr,
                s.text.substr(0,
                    static_cast<size_t>(s.cursor_pos)).c_str(),
                &pext);
            cursor_x_offset = pext.x_advance;
        }
        const double cur_x = std::round(editable_left + cursor_x_offset) + 0.5;
        cairo_set_source_rgb(cr,
            s.text_color.r, s.text_color.g, s.text_color.b);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, cur_x, bg_top);
        cairo_line_to(cr, cur_x, bg_top + bg_h);
        cairo_stroke(cr);
    }

    cairo_restore(cr);
}

namespace {

// Shared greedy-pack iteration used by both render_flags and
// compute_flag_hit_rects, and their phase-reset analogues. Invokes
// `emit(i, text_left, baseline_y, text, ext)` for each flag that survives
// elision, in left-to-right order. The cairo font face/size are assumed to
// already be set on `cr` by the caller. `text_left` is snapped to the
// marker's integer pixel column so the flag's left edge coincides with the
// marker/playhead column. `get_flag_text(i)` returns the marker's flag
// payload; an empty return is the "this marker has no visible flag" signal.
template <typename MarkerVec, typename FlagTextFn, typename Emit>
void iterate_visible_flags_impl(
    cairo_t* cr,
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
    // Place the rect's bottom edge exactly at the strip bottom (which is
    // the waveform area top, since the strips are contiguous). Solving the
    // rect formula (rect_bottom = baseline_y + y_bearing + height + hl_pad
    // + kVPadExtraPx) for baseline_y, using a representative monospace
    // measurement so the result tracks font metrics rather than a magic
    // constant.
    cairo_text_extents_t base_ext;
    cairo_text_extents(cr, "1.23*1.2345:a.aa", &base_ext);
    const double hl_pad_helper = kFlagInnerPadPx;
    const double baseline_y =
        static_cast<double>(top_strip_area.y + top_strip_area.h)
      - kFlagBottomLiftPx
      - base_ext.y_bearing
      - base_ext.height
      - hl_pad_helper
      - kVPadExtraPx;
    const double pad          = 4.0;

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
        if (text_left < rightmost_right_edge + pad) {
            if constexpr (kDebugPerf) perf_counters::flag_elided++;
            continue;
        }

        const std::string text = get_flag_text(static_cast<int>(i));
        if (text.empty()) continue;

        cairo_text_extents_t ext;
        cairo_text_extents(cr, text.c_str(), &ext);
        if constexpr (kDebugPerf) perf_counters::flag_measure++;

        emit(static_cast<int>(i), text_left, baseline_y, text, ext);
        rightmost_right_edge = text_left + ext.width;
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
    const cairo_text_extents_t& ext,
    const std::vector<GuiWarpMarker>& markers,
    const std::set<int>& selected_set,
    const TrimRange& trim,
    const FlagEditorOverlay& editor,
    const std::vector<TimeMapSegment>* timemap,
    const DragOverlay* drag_overlay,
    double sr_d,
    double hl_pad,
    const cairo_text_extents_t& uniform_ext) {
    (void)ext;  // editable text re-measured inside render_editor_text_box
    const bool is_selected = selected_set.count(i) > 0;
    const bool is_editing    = (i == editor.marker_index);
    const bool is_parse_fail = is_editing && editor.is_red;

    const double eff_time = drag_overlay
        ? drag_overlay->effective_time(i, markers[i].time_seconds)
        : markers[i].time_seconds;
    const double pos_ms = sec_to_paint_sample(eff_time, sr_d, timemap);
    const int64_t marker_pos =
        static_cast<int64_t>(std::nearbyint(pos_ms));
    const bool out_of_trim = marker_out_of_trim(marker_pos, trim);

    const std::string draw_text = is_editing ? editor.pending : text;

    // Fill table (Brief B.2): parse-fail > selected > default(kMarker).
    GuiColor fill_col;
    if (is_parse_fail)      fill_col = kAccent;
    else if (is_selected)   fill_col = kSelected;
    else                    fill_col = kMarker;
    if (out_of_trim) fill_col = dim(fill_col);

    EditorTextBox box;
    box.anchor_x        = text_left + hl_pad;
    box.baseline_y      = baseline_y;
    box.text            = draw_text;
    box.uniform_ext     = uniform_ext;
    box.hl_pad          = hl_pad;
    box.fill            = fill_col;
    box.text_color      = out_of_trim ? dim(kText) : kText;
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
                  const TrimRange& trim,
                  const FlagEditorOverlay& editor,
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

    const double hl_pad = kFlagInnerPadPx;
    const double sr_d   = static_cast<double>(sample_rate);

    cairo_text_extents_t uniform_ext;
    cairo_text_extents(cr, "1.23*1.2345:a.aa", &uniform_ext);

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
        cairo_text_extents_t ext;
    };
    std::vector<FlagEmit> emits;
    iterate_visible_flags_impl(cr, top_strip_area, markers,
                               viewport_start_sample, viewport_end_sample,
                               sample_rate, timemap, drag_overlay,
        [&](int i) {
            return flag_text(markers, i);
        },
        [&](int i, double text_left, double baseline_y,
            const std::string& text, const cairo_text_extents_t& ext) {
            // Stage C: skip-guard. The flag-cache rebuild passes the
            // FlagPayload-editor target through editor.marker_index so
            // this branch fires and the cache leaves a transparent hole
            // over the editor target's pixel column; the live editor
            // render owns those pixels via render_one_editor_flag.
            // Defensive against pre-Stage-C callers as well — when
            // editor.marker_index == -1 (the default), the guard never
            // fires, and behavior is identical to the pre-Stage-C path.
            if (editor.marker_index == i) return;
            emits.push_back({i, text_left, baseline_y, text, ext});
        });

    for (auto it = emits.rbegin(); it != emits.rend(); ++it) {
        paint_one_flag_with_overlay(cr, it->i, it->text_left, it->baseline_y,
                                    it->text, it->ext,
                                    markers, selected_set, trim, editor,
                                    timemap, drag_overlay, sr_d, hl_pad,
                                    uniform_ext);
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
    const TrimRange& trim,
    const FlagEditorOverlay& editor,
    const std::vector<TimeMapSegment>* timemap,
    const DragOverlay* drag_overlay) {
    if (editor.marker_index < 0) return;
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (sample_rate <= 0) return;

    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);

    const double hl_pad = kFlagInnerPadPx;
    const double sr_d   = static_cast<double>(sample_rate);

    cairo_text_extents_t uniform_ext;
    cairo_text_extents(cr, "1.23*1.2345:a.aa", &uniform_ext);

    iterate_visible_flags_impl(cr, top_strip_area, markers,
                               viewport_start_sample, viewport_end_sample,
                               sample_rate, timemap, drag_overlay,
        [&](int i) {
            return flag_text(markers, i);
        },
        [&](int i, double text_left, double baseline_y,
            const std::string& text, const cairo_text_extents_t& ext) {
            if (i != editor.marker_index) return;
            paint_one_flag_with_overlay(cr, i, text_left, baseline_y,
                                        text, ext,
                                        markers, selected_set, trim, editor,
                                        timemap, drag_overlay, sr_d, hl_pad,
                                        uniform_ext);
        });

    cairo_restore(cr);
}

namespace {

template <typename MarkerVec, typename FlagTextFn>
std::vector<FlagHitRect> compute_flag_hit_rects_impl(
    cairo_t* cr,
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

    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);

    // Mirror render_flags: uniform y/height for the hit rect so clicks
    // register consistently across flag types. Left edge + width match the
    // corrected visual highlight box so clicks anywhere inside the painted
    // background register on the marker.
    cairo_text_extents_t uniform_ext;
    cairo_text_extents(cr, "1.23*1.2345:a.aa", &uniform_ext);
    const double hl_pad = kFlagInnerPadPx;

    iterate_visible_flags_impl(cr, top_strip_area, markers,
                               viewport_start_sample, viewport_end_sample,
                               sample_rate, timemap, drag_overlay,
        std::forward<FlagTextFn>(get_flag_text),
        [&](int i, double text_left, double baseline_y,
            const std::string& /*text*/, const cairo_text_extents_t& ext) {
            FlagHitRect r;
            r.marker_index = i;
            r.x = text_left;
            r.y = baseline_y + uniform_ext.y_bearing - hl_pad - kVPadExtraPx;
            r.w = hl_pad + ext.x_bearing + ext.width + hl_pad;
            r.h = uniform_ext.height + 2 * hl_pad + 2 * kVPadExtraPx;
            out.push_back(r);
        });

    cairo_restore(cr);
    return out;
}

} // namespace

std::vector<FlagHitRect> compute_flag_hit_rects(
    cairo_t* cr,
    GuiRect top_strip_area,
    const std::vector<GuiWarpMarker>& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::vector<TimeMapSegment>* timemap,
    const DragOverlay* drag_overlay) {
    return compute_flag_hit_rects_impl(cr, top_strip_area, markers,
        viewport_start_sample, viewport_end_sample,
        sample_rate, font_size, timemap, drag_overlay,
        [&](int i) {
            return flag_text(markers, i);
        });
}

// ---------- Phase reset marker rendering (chunk S.2.2) ----------

namespace {

std::string phase_reset_flag_text(const GuiPhaseResetMarker& m) {
    (void)m;
    return "p";
}

} // namespace

void render_phase_reset_markers(cairo_t* cr,
                              GuiRect waveform_area,
                              const std::vector<GuiPhaseResetMarker>& phase_resets,
                              long long viewport_start_sample,
                              long long viewport_end_sample,
                              int sample_rate,
                              const TrimRange& trim,
                              const std::set<int>& selected_set,
                              const std::vector<TimeMapSegment>* timemap,
                              const DragOverlay* drag_overlay) {
    render_marker_stems_impl(
        cr, waveform_area, phase_resets,
        viewport_start_sample, viewport_end_sample,
        sample_rate, trim, selected_set, timemap, drag_overlay,
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
                            const TrimRange& trim,
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

    const double hl_pad = kFlagInnerPadPx;

    cairo_text_extents_t uniform_ext;
    cairo_text_extents(cr, "1.23*1.2345:a.aa", &uniform_ext);

    // Brief Y.5: collect-then-reverse-paint, mirroring render_flags.
    // Phase reset flags have no editor and thus no widening-text case, so
    // visually this is a no-op today (all bg-fills are kBackground; all
    // text rects are non-overlapping). Structurally it keeps every flag /
    // popup paint loop on the same leftmost-wins discipline so future
    // edits don't have to reason about which loop is which.
    struct PhaseResetEmit {
        int                  i;
        double               text_left;
        double               baseline_y;
        std::string          text;
        cairo_text_extents_t ext;
    };
    std::vector<PhaseResetEmit> emits;
    iterate_visible_flags_impl(cr, top_strip_area, phase_resets,
                               viewport_start_sample, viewport_end_sample,
                               sample_rate, timemap, drag_overlay,
        [&](int i) {
            return phase_reset_flag_text(phase_resets[i]);
        },
        [&](int i, double text_left, double baseline_y,
            const std::string& text, const cairo_text_extents_t& ext) {
            emits.push_back({i, text_left, baseline_y, text, ext});
        });

    const double sr = static_cast<double>(sample_rate);
    auto paint_one = [&](const PhaseResetEmit& e) {
        const bool is_selected = selected_set.count(e.i) > 0;
        // Effective time: drag overlay > live store. Keeps out-of-trim
        // dim consistent with displayed stem during drag.
        const double eff_time = drag_overlay
            ? drag_overlay->effective_time(
                  e.i, phase_resets[e.i].time_seconds)
            : phase_resets[e.i].time_seconds;
        // Translate per-marker frame to target-frame in target view so the
        // out-of-trim comparison runs in the same domain as `trim`.
        const double pos_ms = sec_to_paint_sample(eff_time, sr, timemap);
        const int64_t marker_pos =
            static_cast<int64_t>(std::nearbyint(pos_ms));
        const bool out_of_trim = marker_out_of_trim(marker_pos, trim);

        // Fill table (Brief B.2): no parse-fail state for phase resets,
        // so selected > default(kMarker). Always a solid chip.
        GuiColor fill_col = is_selected ? kSelected : kMarker;
        if (out_of_trim) fill_col = dim(fill_col);

        EditorTextBox box;
        box.anchor_x    = e.text_left + hl_pad;
        box.baseline_y  = e.baseline_y;
        box.text        = e.text;
        box.uniform_ext = uniform_ext;
        box.hl_pad      = hl_pad;
        box.fill        = fill_col;
        box.text_color  = out_of_trim ? dim(kText) : kText;
        render_editor_text_box(cr, box);

        if constexpr (kDebugPerf) perf_counters::flag_drawn++;
    };

    for (auto it = emits.rbegin(); it != emits.rend(); ++it) {
        paint_one(*it);
    }

    cairo_restore(cr);
}

std::vector<FlagHitRect> compute_phase_reset_flag_hit_rects(
    cairo_t* cr,
    GuiRect top_strip_area,
    const std::vector<GuiPhaseResetMarker>& phase_resets,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    double font_size,
    const std::vector<TimeMapSegment>* timemap,
    const DragOverlay* drag_overlay) {
    return compute_flag_hit_rects_impl(cr, top_strip_area, phase_resets,
        viewport_start_sample, viewport_end_sample,
        sample_rate, font_size, timemap, drag_overlay,
        [&](int i) {
            return phase_reset_flag_text(phase_resets[i]);
        });
}

namespace {
    double g_advance = 0.0;
    bool   g_metrics_initialized = false;
} // namespace

double monospace_advance() { return g_advance; }

void init_monospace_grid_metrics(cairo_t* cr) {
    if (g_metrics_initialized) return;
    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, kFlagFontSize);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, "M", &ext);
    g_advance = ext.x_advance;
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
    return text_left + kFlagInnerPadPx;
}
