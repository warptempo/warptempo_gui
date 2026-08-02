#include "render.h"
#include "app_state.h"
#include "audio.h"
#include "gui_display_context.h"
#include "text_shape.h"
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
// (the FLAG itself, truncated at the nine-glyph budget, and the Enter flag
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

void render_canvas(cairo_t* cr, int x, int y, int w, int h) {
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // ROW 6: the ground is the CROP's #12312b, hard-coded (kWaveformCanvas).
    // This was the one paint site of the old tunable `canvas` key, which is why
    // that key went inert here and was deleted outright with the rest of the
    // colors.conf system on 2026-08-02 (the record is at the palette header).
    cairo_set_source_rgb(cr, kWaveformCanvas.r, kWaveformCanvas.g,
                         kWaveformCanvas.b);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    // THE BORDER, taken FROM the area: its topmost and bottommost rows, painted
    // in the same pass as the ground so the two can never disagree about where
    // the area ends. Row 6 made it 2px of pure black (it was 1px of the tunable
    // grey #686a6c, whose last paint site this was); the shape is unchanged, and
    // waveform_content_rect — the band every band-filling pass clips to — reads
    // the same waveform_border_px, so the two cannot drift. Nothing covers the
    // border but the deliberate full-height 1px verticals (playheads, stems),
    // which is their recorded z-intent and survives row 6 unchanged. Integer-
    // edged rects with AA off, the crisp-line convention. An area too short to
    // carry both borders draws neither rather than overlapping them.
    const int border = waveform_border_px();
    if (h > 2 * border) {
        cairo_set_source_rgb(cr, kWaveformBorder.r, kWaveformBorder.g,
                             kWaveformBorder.b);
        cairo_rectangle(cr, x, y, w, border);
        cairo_rectangle(cr, x, y + h - border, w, border);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

void render_waveform(cairo_surface_t* dest,
                     GuiRect area,
                     int col0,
                     const GuiAudio& audio,
                     int channel,
                     const WaveformBasis& basis,
                     GuiColor color,
                     const std::vector<WarpFrameMapSegment>* warp_frame_map) {
    if (!dest) return;
    if (area.w <= 0 || area.h <= 2) return;
    if (basis.full_width <= 0) return;
    // The lattice step must be numeric and positive; a degenerate zoom refuses
    // here exactly as the old empty-viewport check did.
    if (!(basis.spp > 0.0)) return;

    const int num_levels = audio.num_levels();
    if (num_levels <= 0) return;

    // ARGB32 ONLY: the writer stores 32-bit premultiplied words, so any other
    // format would be silently misinterpreted. Both plate surfaces are created
    // CAIRO_FORMAT_ARGB32 (waveform_cache.cpp); this is the guard that keeps
    // that true. Geometry comes from the surface itself — the stride accessor,
    // never width*4, since cairo is free to pad rows.
    if (cairo_image_surface_get_format(dest) != CAIRO_FORMAT_ARGB32) return;
    // Flush BEFORE the first CPU access so any pending cairo drawing (the
    // caller's CLEAR of the columns this call regenerates) has landed in the
    // buffer. Paired with the cairo_surface_mark_dirty after the last write.
    cairo_surface_flush(dest);
    unsigned char* const surf_data = cairo_image_surface_get_data(dest);
    if (!surf_data) return;
    const int surf_stride = cairo_image_surface_get_stride(dest);
    const int surf_w      = cairo_image_surface_get_width(dest);
    const int surf_h      = cairo_image_surface_get_height(dest);
    if (surf_w <= 0 || surf_h <= 0) return;

    // THE AUTHORING LATTICE (see WaveformBasis). Recover the viewport's lattice
    // index with the SAME expression clamp_viewport_start uses to snap onto it,
    // so a resting viewport round-trips exactly; an off-lattice mid-gesture
    // viewport quantizes to its nearest rest. Columns are then indexed globally
    // from k0, which is what makes a pan a pure index shift — a column's frames
    // depend on k0+c and nothing else, never on which window drew it.
    const double samples_per_pixel = basis.spp;
    double k0d = std::nearbyint(static_cast<double>(basis.vp_start) /
                                samples_per_pixel);
    if (!(k0d >= 0.0)) k0d = 0.0;          // also rejects NaN
    const long long k0 = static_cast<long long>(k0d);

    // PYRAMID LEVEL IS CHOSEN PER COLUMN, from that column's own mapped SOURCE
    // width, through the one level-choosing owner (GuiAudio::level_for_span —
    // the stride ladder lives there and nothing here knows it).
    //
    // Source view: the mapped width is the basis spp for EVERY column — one
    // uniform value, so per-column selection provably yields the identical
    // level throughout and this is not a behavior change (only the denser
    // ladder is). It is passed as the exact spp rather than a per-column
    // rounded span precisely so that invariance holds by construction and
    // cannot wobble across a stride threshold on a rounding tie.
    //
    // Target view: the width is the column's TRUE local mapped span (g1 - g0),
    // which is what the read actually costs. The old single pick came from the
    // viewport-wide TARGET-domain spp while the reads are source-domain, and
    // the legal local slope reaches 16x (tempo 4 * marker scale 2 * settings
    // scale 2) — so a tempo-compressed column could read a far finer level than
    // its span warranted, up to hundreds of samples in one column. Selecting
    // from the column's own span restores the intended per-column bound (<=5
    // pairs or <=16 raw samples, unconditionally — the statement and its proof
    // live at GuiAudio::level_for_span) and is
    // strictly MORE accurate than the global estimate it replaces.
    //
    // Consequence, accepted: where the map slope crosses a stride threshold,
    // adjacent target-view columns may read different levels, a per-column
    // statistics discontinuity — now confined to the one column that reads it,
    // since no segment carries anything into a neighbour. Aesthetic only.
    const auto level_for_column = [&](double src_width) {
        return audio.level_for_span(warp_frame_map ? src_width
                                                   : samples_per_pixel);
    };

    const double y_center = area.y + area.h * 0.5;
    const double half_h   = area.h * 0.5;

    // Each column is written straight into the plate's pixel words, and a
    // column is ONE HARD BAR: its own raw min/max interval, floored to rows and
    // filled inclusively with the opaque ink word. There is no interior/edge
    // split, no fractional coverage, and no inter-column connectivity of any
    // kind — a spike stands alone, exactly as in a classic min/max renderer.
    //
    // THE ANTIALIASED RENDERER IS DELETED (architect 2026-08-01, after the
    // side-by-side against a snapshotted AA binary: "subtle but noticeable — I
    // prefer without it"). What went is named here so its absence reads as a
    // decision rather than an omission: the Wu tip polylines and their
    // max-coverage compositing, the fractional boundary rows, the 256-entry
    // premultiplied coverage table, and BOTH EDGE HALOS. The technique is
    // recorded in docs/engineering/waveform_antialiasing_retired.md.
    //
    // THE >=1px NEVER-FADE FLOOR SURVIVES, as integer geometry rather than as a
    // unit deposit: floor(top) and floor(bot) coincide for any sub-pixel
    // interval, so the inclusive fill always writes at least one row and flat or
    // silent material draws a hairline instead of fading out.
    //
    // THE HALOS WENT BECAUSE THEIR REASON WENT. They existed so an EDGE column
    // would carry the same ink an interior column gets — under the segment
    // model a column's ink came from the segments on both its sides, so a
    // missing offscreen neighbour under-covered it and it popped during a pan.
    // A bar depends on nothing but its own interval, so every column is now
    // self-contained and there is nothing for an offscreen neighbour to
    // contribute: pan invariance strengthened rather than weakened here.
    //
    // THE PREMULTIPLIED WORD: one ink colour per render, so there is exactly one
    // word to write. cairo ARGB32 is a native-endian 32-bit quantity —
    // (A<<24)|(R<<16)|(G<<8)|B written as a uint32_t is correct on any byte
    // order, which indexing bytes would not be. Channels are PREMULTIPLIED, as
    // ARGB32 requires; at full coverage that is the ink itself.
    const uint32_t opaque_word =
        (UINT32_C(255) << 24) |
        (static_cast<uint32_t>(std::lround(color.r * 255.0)) << 16) |
        (static_cast<uint32_t>(std::lround(color.g * 255.0)) <<  8) |
        (static_cast<uint32_t>(std::lround(color.b * 255.0)));

    // Row bounds: this channel's band, intersected with the surface.
    int y_lo = area.y;
    int y_hi = area.y + area.h;          // exclusive
    if (y_lo < 0)      y_lo = 0;
    if (y_hi > surf_h) y_hi = surf_h;
    if (y_hi <= y_lo) return;

    // COLUMNS THIS CALL OWNS. Every write is clipped to them, so a partial
    // render, if one is ever reintroduced, cannot bleed into a neighbour's
    // columns. (They also bound the write to the surface: both ends are clamped
    // into [0, surf_w) here, once, instead of at every store.)
    int col_lo = area.x;
    int col_hi = area.x + area.w;
    if (col_lo < 0)      col_lo = 0;
    if (col_hi > surf_w) col_hi = surf_w;
    if (col_hi <= col_lo) return;

    // Write one pixel word, REPLACING what is there. Row/column bounds are
    // established by the bar writer below; this is its single store site.
    // Replace is unambiguously correct now: the caller cleared every column this
    // call regenerates, and each column is written exactly once by exactly one
    // bar (the max-compositing the tip segments needed went with them).
    const auto put = [&](int x, int y, uint32_t word) {
        auto* px = reinterpret_cast<uint32_t*>(
            surf_data + static_cast<size_t>(y) * surf_stride);
        px[x] = word;
    };

    // Global column c's display-domain edge, AS THE LATTICE POINT ITSELF:
    // g(k0+c) = nearbyint((k0+c)*spp), bit-for-bit the integer
    // clamp_viewport_start's grid() lambda produces. THE QUANTIZE LIVES HERE,
    // once, so every consumer — the loop and the carried-endpoint chain —
    // receives the same already-rounded lattice point and BOTH VIEWS
    // consume the identical integer. Rounding here rather than downstream is
    // what makes the target-view path honest: to_source used to truncate the
    // raw product through its size_t cast, so target view mapped
    // floor((k0+c)*spp) — a frame below the documented g(k0+c) whenever the
    // fraction would have rounded up, with ties following truncation instead of
    // banker's rounding. The pan invariant held either way (floor of a lattice
    // point is still a pure function of the global index), but the geometry sat
    // off the lattice this contract declares.
    const auto edge_at = [&](long long c) {
        return std::nearbyint(static_cast<double>(k0 + c) * samples_per_pixel);
    };
    // Display-domain lattice point -> source frame. `f` arrives INTEGRAL from
    // edge_at, so the size_t cast below is exact, not a second quantization.
    // Source view is the identity: the value is already g(k0+c), and the
    // caller's nearbyint on it is idempotent. Target view maps exactly that same
    // g(k0+c) through the warp_frame_map, so the pyramid read lands at the
    // matching authored audio. Negative display positions clamp at 0 (the map
    // takes an unsigned frame); callers treat a wholly-left-of-zero span as
    // empty rather than relying on this clamp.
    const auto to_source = [&](double f) {
        return warp_frame_map
                   ? map_target_to_source(
                         static_cast<size_t>(f < 0.0 ? 0.0 : f), *warp_frame_map)
                   : f;
    };

    // THE RUNNING LEFT EDGE in SOURCE frames, and the carried-endpoint chain it
    // serves: column i's left edge IS column i-1's right edge, so each edge is
    // translated through the map once rather than twice. It seeds at the FIRST
    // DRAWN column's own left edge — the halo column that used to seed it one
    // step earlier is gone with the segments (the deletion note is at the top of
    // this function).
    double g_prev = to_source(edge_at(static_cast<long long>(col0)));

    for (int i = 0; i < area.w; i++) {
        const long long c  = static_cast<long long>(col0) + i;
        const double    f1 = edge_at(c + 1);
        const double    g0 = g_prev;
        const double    g1 = to_source(f1);

        const long long s0 = static_cast<long long>(std::nearbyint(g0));
        long long       s1 = static_cast<long long>(std::nearbyint(g1));
        if (s1 <= s0) s1 = s0 + 1;

        const int level = level_for_column(g1 - g0);
        const auto mm = audio.get_peak_range(channel, level, s0, s1);
        const double raw_min = mm.first;
        const double raw_max = mm.second;

        const int x = area.x + i;

        // THE COLUMN'S TIPS: raw maximum -> top tip, raw minimum -> bottom
        // tip, in float rows, never snapped — then floored to rows for the bar.
        // The regime split (thin vs tall) went with the tip segments: there is
        // one rendering for every column now, however small its interval.
        const double cur_top_y = y_center - raw_max * half_h;
        const double cur_bot_y = y_center - raw_min * half_h;

        // THE BAR. Clamp to this channel's rows BEFORE any row index is derived,
        // so a clipped interval cannot address outside the band; then floor both
        // ends and fill inclusively. r0 == r1 for any sub-pixel interval, which
        // is the >=1px floor stated at the top of this function.
        if (x >= col_lo && x < col_hi) {
            double yt = cur_top_y;
            double yb = cur_bot_y;
            const double row_lo = static_cast<double>(y_lo);
            const double row_hi = static_cast<double>(y_hi);   // exclusive
            if (yt < row_lo) yt = row_lo;
            if (yb < row_lo) yb = row_lo;
            if (yt > row_hi) yt = row_hi;
            if (yb > row_hi) yb = row_hi;
            int r0 = static_cast<int>(std::floor(yt));
            int r1 = static_cast<int>(std::floor(yb));
            if (r0 < y_lo)     r0 = y_lo;
            if (r1 > y_hi - 1) r1 = y_hi - 1;
            for (int y = r0; y <= r1; ++y) put(x, y, opaque_word);
        }

        g_prev = g1;
    }

    // Last CPU write is done — hand the buffer back to cairo.
    cairo_surface_mark_dirty(dest);
}

void render_playhead(cairo_t* cr,
                     GuiRect area,
                     GuiRect triangle_lane,
                     double  playhead_pixel_x,
                     GuiColor color,
                     bool draw_triangle) {
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
    // The 1px vertical line paints whenever its column is onscreen (it is
    // column-gated only, so it never leaks into an adjacent region); the triangle
    // below is independently gated by draw_triangle.
    // ONE SOLID LINE, straight over whatever it crosses — waveform ink included.
    // A saturated stem over the dark ink reads without any cut, so there is no
    // two-tone overdraw here (see the declaration for the retirement).
    if (col >= 0.0 && col < static_cast<double>(area.w)) {
        cairo_set_source_rgb(cr, color.r, color.g, color.b);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, x_px, area.y);
        cairo_line_to(cr, x_px, area.y + area.h);
        cairo_stroke(cr);
    }

    // Inverted-triangle indicator, stamped from the cached ANTIALIASED A8 mask
    // (playhead_triangle_mask()): the mask is 2H-1 x H (odd width) with the tip
    // at column index H-1 (image-local), and dst_y is the passed lane rect's own
    // top rather than `area.y - H` re-derived from the waveform edge.
    //
    // NO CALLER TAKES THIS BRANCH. Row 5 retired the cursor triangle along with
    // the triangle lane it stamped into, and its successor is the ruler lane's
    // ALIASED HEAD, which paint_ruler_row draws because the head's pre-blended
    // tick crossing needs the tick columns. Every call site passes
    // draw_triangle=false. The branch and its mask are kept rather than ripped
    // out mid-arc — they are the one place the triangle geometry still lives —
    // and the lane parameter stays unconditional so a call that ever revives it
    // cannot forget which band it lands in.
    if (draw_triangle) {
        cairo_surface_t* triangle_surface = playhead_triangle_mask();
        const int img_w = cairo_image_surface_get_width(triangle_surface);
        const int img_h = cairo_image_surface_get_height(triangle_surface);
        const double dst_x = static_cast<double>(area.x + col - img_w / 2);
        const double dst_y = static_cast<double>(triangle_lane.y);
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

void render_strip_anchor_stem(cairo_t* cr, GuiRect area, int col) {
    if (area.w <= 0 || area.h <= 0) return;
    // The clamp is where the affordance lives: an anchor pushed to (or past) a
    // song edge pins to the edge column, so the stem draws exactly there.
    if (col < 0)          col = 0;
    if (col >= area.w)    col = area.w - 1;

    const double x_px = static_cast<double>(area.x) + col + 0.5;
    cairo_save(cr);
    // THE ANCHOR STEM IS THE PLAYHEAD'S WHITE (architect 2026-08-01, at the
    // row-6 live look): kPlayheadStem #fcfcfc, hard-coded per the redesign's
    // colour ruling, superseding the dim tunable grey #686a6c this drew in —
    // whose ONE paint site this was, which is what left its config key unread
    // and, a day later, deleted with the whole tunable palette. The affordance
    // is deliberately no longer "less loud
    // than a marker stem": it is a position line during a gesture, and the
    // product's position lines are this white.
    cairo_set_source_rgb(cr, kPlayheadStem.r, kPlayheadStem.g, kPlayheadStem.b);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, x_px, static_cast<double>(area.y));
    cairo_line_to(cr, x_px, static_cast<double>(area.y + area.h));
    cairo_stroke(cr);
    cairo_restore(cr);
}

// -- Trim bound geometry owners -------------------------------------------
// One column formula, one mapping helper, one chip rect, one bridge-gap owner.
// See render.h for the full rationale (the UNIFIED displayed basis — both
// painters and hit sites decide against the same committed viewport, the
// event-sync ruling; the quantized-span denominator; the EOF-wall clamp; and the
// side-aware bridge sentinels).

TrimBoundColumn trim_bound_column(double displayed_ms,
                                  long long vp_start, long long vp_end,
                                  int wave_w) {
    TrimBoundColumn out;
    out.ms = displayed_ms;
    // The unrounded verdict — both in_viewport AND the offscreen SIDE come from
    // this one compare, so col_raw's rounding seam (a barely-off-left ms rounding
    // to col_raw == 0) never decides the side.
    const bool at_or_past_left = displayed_ms >= static_cast<double>(vp_start);
    const bool before_right    = displayed_ms <  static_cast<double>(vp_end);
    out.in_viewport = at_or_past_left && before_right;
    out.side = out.in_viewport ? TrimBoundSide::InView
             : (at_or_past_left ? TrimBoundSide::OffRight
                                : TrimBoundSide::OffLeft);
    // The painters' quantized-span denominator: (vp_end - vp_start)/wave_w,
    // where the hit sites' vp_end itself was derived via nearbyint(spp*wave_w).
    const double span = static_cast<double>(vp_end - vp_start);
    const double samples_per_pixel = span / static_cast<double>(wave_w);
    const double x_raw =
        (displayed_ms - static_cast<double>(vp_start)) / samples_per_pixel;
    out.col_raw = static_cast<int>(std::nearbyint(x_raw));
    out.col = out.col_raw;
    if (wave_w > 0)
        out.col = std::clamp(out.col, 0, wave_w - 1);
    return out;
}

TrimBridgeGap trim_bridge_gap(const TrimBoundColumn& begin,
                              const TrimBoundColumn& end, int chip_w,
                              int wave_w) {
    // Contract (4x2 table) at the declaration. A PAINTED (InView) chip bounds the
    // gap at its inner edge (inset by chip_w — the room the chip occupies); an
    // OFFSCREEN bound paints no chip, so the bar runs FLUSH. The offscreen arms
    // key on the bound's SIDE (not col_raw, which cannot tell the side across the
    // rounding seam), and use side-specific SENTINELS so an offscreen edge lands
    // STRICTLY past the visible range — never col 0 / col wave_w-1 — which is what
    // makes the flush fill AND the offscreen ring-border clip hold.
    TrimBridgeGap g;
    switch (begin.side) {
        case TrimBoundSide::InView:
            g.lo = begin.col + chip_w; break;
        case TrimBoundSide::OffLeft:
            g.lo = std::min(begin.col_raw, -1); break;      // strictly < 0
        case TrimBoundSide::OffRight:
            g.lo = std::max(begin.col_raw, wave_w); break;  // >= wave_w -> empty
    }
    switch (end.side) {
        case TrimBoundSide::InView:
            g.hi = end.col - chip_w + 1; break;
        case TrimBoundSide::OffRight:
            g.hi = std::max(end.col_raw + 1, wave_w + 1); break;  // > wave_w
        case TrimBoundSide::OffLeft:
            g.hi = std::min(end.col_raw + 1, 0); break;           // <= 0 -> empty
    }
    return g;
}

double displayed_trim_ms(int64_t frame,
                         const std::vector<WarpFrameMapSegment>* map) {
    double ms = static_cast<double>(frame);
    if (map && !map->empty()) {
        const double q = static_cast<double>(frame < 0 ? 0 : frame);
        ms = std::nearbyint(map_source_to_target(q, *map));
    }
    return ms;
}

GuiRect trim_chip_rect(bool is_begin, int strip_x, int col, GuiRect row) {
    const int cap_w = trim_endcap_w_px();
    const int abs_col = strip_x + col;
    GuiRect r;
    // Begin left-edge-anchored (rect left ON the column); end right-edge-anchored
    // (rightmost pixel ON the column) — the SAME edge rule the square chips
    // used, so a bound's mark still stands on the column the bound occupies.
    // Only the WIDTH changed with row 5: the endcap is 2px where the chip was a
    // flag-width square. Y-band from the trim lane `row`.
    r.x = is_begin ? abs_col : abs_col - cap_w + 1;
    r.y = row.y;
    r.w = cap_w;
    r.h = row.h;
    return r;
}

void render_trim_flags(cairo_t* cr,
                       GuiRect top_strip_area,
                       GuiRect chip_row,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim) {
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (chip_row.w <= 0 || chip_row.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (waveform_area.w <= 0) return;

    // Both bounds resolve through the ONE shared column owner, exactly as the
    // chips did: .col is the clamped column, .side the offscreen verdict. The
    // bar spans between them even when a bound is culled, so the columns are
    // computed unconditionally.
    const TrimBoundColumn bc = trim_bound_column(
        static_cast<double>(trim.begin), viewport_start_sample,
        viewport_end_sample, waveform_area.w);
    const TrimBoundColumn ec = trim_bound_column(
        static_cast<double>(trim.end), viewport_start_sample,
        viewport_end_sample, waveform_area.w);

    const int lane_x   = chip_row.x;
    const int lane_w   = waveform_area.w;   // the effective width, as the chips used
    const int lane_y   = chip_row.y;
    const int lane_h   = chip_row.h;
    const int bevel_h  = std::min(trim_bevel_h_px(), lane_h);
    const int face_h   = lane_h - bevel_h;  // rows 0..6 at 100%
    const int hi_h     = bevel_h / 2;       // row 7: the lighter shade
    const int lo_h     = bevel_h - hi_h;    // row 8: the darker one

    cairo_save(cr);
    cairo_rectangle(cr, lane_x, lane_y, lane_w, lane_h);
    cairo_clip(cr);

    // ONE PAINTER FOR A SURFACE'S WHOLE COLUMN RUN: the face rows, then the two
    // bevel rows, all pixel-bound integer fills (crisp by construction, no
    // stroke, no antialiasing anywhere in this lane).
    auto surface = [&](int x0, int w, GuiColor face, GuiColor hi, GuiColor lo) {
        if (w <= 0) return;
        cairo_set_source_rgb(cr, face.r, face.g, face.b);
        cairo_rectangle(cr, x0, lane_y, w, face_h);
        cairo_fill(cr);
        if (hi_h > 0) {
            cairo_set_source_rgb(cr, hi.r, hi.g, hi.b);
            cairo_rectangle(cr, x0, lane_y + face_h, w, hi_h);
            cairo_fill(cr);
        }
        if (lo_h > 0) {
            cairo_set_source_rgb(cr, lo.r, lo.g, lo.b);
            cairo_rectangle(cr, x0, lane_y + face_h + hi_h, w, lo_h);
            cairo_fill(cr);
        }
    };

    // GROUND everywhere first, then the window's BAR over it, then the endcaps
    // over that — painting back to front means no run has to know what its
    // neighbour is, and an inverted or degenerate window simply leaves the
    // ground showing.
    surface(lane_x, lane_w, kRedesignTabGround,
            kTrimGroundBevelHi, kTrimGroundBevelLo);

    // THE BAR SPANS THE WINDOW, and it follows a bound OFFSCREEN rather than
    // stopping short: an out-of-view bound means the window continues past that
    // edge, so the bar runs flush to it. The clip above trims the overhang.
    const int bar_lo = (bc.side == TrimBoundSide::OffLeft)  ? 0 : bc.col;
    const int bar_hi = (ec.side == TrimBoundSide::OffRight) ? lane_w : ec.col + 1;
    if (bar_hi > bar_lo) {
        surface(lane_x + bar_lo, bar_hi - bar_lo, kTrimLaneBar,
                kTrimBarBevelHi, kTrimBarBevelLo);
    }

    // THE ENDCAPS stand ON their bound columns, bodies facing INWARD (the begin
    // cap starts at its column, the end cap ends on its own), which is the same
    // edge-anchoring the chips used — so a bound's painted mark still sits on
    // the column the bound actually occupies. A culled bound paints no cap: it
    // has no column on screen to stand on, and the bar's flush edge is what says
    // the window continues past the view.
    // Both caps come from the ONE rect owner the hit test reads
    // (trim_chip_rect), so the painted cap and the grabbable cap describe the
    // same edge — the hit side adds only its stated grab tolerance.
    if (bc.in_viewport) {
        const GuiRect r = trim_chip_rect(true, lane_x, bc.col, chip_row);
        surface(r.x, r.w, kTrimLaneEndcap, kTrimCapBevelHi, kTrimCapBevelLo);
    }
    if (ec.in_viewport) {
        const GuiRect r = trim_chip_rect(false, lane_x, ec.col, chip_row);
        surface(r.x, r.w, kTrimLaneEndcap, kTrimCapBevelHi, kTrimCapBevelLo);
    }

    // THE MIDPOINT MARK IS THE CROP, BLITTED VERBATIM (architect 2026-08-01, who
    // overlaid row_5_lane_1_trim_middle.png on the running GUI and ruled it
    // implemented exactly). The 9x9 crop is a LANE-HEIGHT TILE, and every pixel
    // of it is already one of this lane's own surfaces:
    //
    //   rows 0..6  #97b4c4  kTrimLaneEndcap    the tile's face
    //   cols 2..6 } #2f6888 kTrimLaneBar       the inner square, inset 2px,
    //   rows 2..6 }                            flush on the face's bottom row
    //   row 7      #9dbbcb  kTrimCapBevelHi    the endcap bevel pair, verbatim
    //   row 8      #94b0c0  kTrimCapBevelLo
    //
    // So the tile is EXACTLY AN ENDCAP-COLOURED COLUMN RUN with a bar-coloured
    // square punched into it, and it paints through the SAME `surface` lambda
    // the caps do — four constants reused, none invented. On our dark bar it
    // reads as the light square RING with the dark centre the mockup shows
    // (tmp/screenshots/kdenlive/redesign/row_5_lane_1_trim_middle_example.png).
    // The earlier 5x5 single-colour square and its recorded deviation are gone:
    // that deviation existed only because one flat fill could carry one half of
    // a two-colour crop, and the tile carries both.
    //
    // Painted last, over the bar — and, where the window is narrow enough for
    // them to meet, it would sit over an endcap's face too, though the clearance
    // rule below means that cannot actually happen.
    //
    // INFORMATIONAL ONLY. It publishes no rect, claims no hit area and changes
    // no routing: the bar's press / pair-drag / span-framing double-click all
    // read the same bands they always did, and a click on the tile is a click
    // on the bar. It is paint and nothing else.
    //
    // THE MIDPOINT IS THE WINDOW'S, not the visible bar's: the two bounds'
    // midpoint goes through the SAME trim_bound_column owner the bar's own
    // edges use, on the same displayed basis, so the mark sits on the column
    // the window's middle actually occupies and scrolls off the view with it
    // rather than sliding to the middle of whatever is on screen. The tile and
    // its inner square share that centre — at 100% the tile spans the midpoint
    // column ±4 and the square ±2, both centred on it.
    //
    // IT PAINTS ONLY WHERE IT FITS, a clean binary verdict on integer columns
    // (so it cannot flicker — no hysteresis, none needed) and the ONLY thing
    // that hides it: the TILE's whole extent must sit inside the visible
    // interior BETWEEN the endcaps (trim_bridge_gap, the shared owner, clamped
    // to the effective width) with a clearance each side. Recomputed for the
    // 9px tile, so the interior it needs grew with the mark; the clearance
    // matters more than it did, the tile's face being the endcaps' own colour
    // and merging with a cap it touched. Below the threshold it simply does not
    // paint: no shrink, no clamp.
    {
        const int tile  = trim_middle_size_px();
        const int inner = trim_middle_inner_px();
        const int inset = trim_middle_inset_px();
        const int clear = trim_middle_clear_px();
        const TrimBridgeGap gap =
            trim_bridge_gap(bc, ec, trim_endcap_w_px(), lane_w);
        const int vis_lo = std::max(gap.lo, 0);
        const int vis_hi = std::min(gap.hi, lane_w);
        const TrimBoundColumn mc = trim_bound_column(
            (static_cast<double>(trim.begin) + static_cast<double>(trim.end)) *
                0.5,
            viewport_start_sample, viewport_end_sample, lane_w);
        const int x_lo = mc.col - tile / 2;    // waveform-relative, inclusive
        const int x_hi = x_lo + tile;          // exclusive
        if (mc.in_viewport && inner <= face_h &&
            x_lo >= vis_lo + clear && x_hi <= vis_hi - clear) {
            // The tile's own column run, bevel included — the endcap surface at
            // the midpoint, which is what rows 0..8 of the crop are.
            surface(lane_x + x_lo, tile, kTrimLaneEndcap,
                    kTrimCapBevelHi, kTrimCapBevelLo);
            // The inner square, at the crop's own offsets. It hangs from the
            // FACE's bottom edge — crop rows 2..6 of a 0..6 face, flush on the
            // bevel — which is the relationship that scales with the lane, and
            // it insets from the tile's left by the crop's 2px.
            cairo_set_source_rgb(cr, kTrimLaneBar.r, kTrimLaneBar.g,
                                 kTrimLaneBar.b);
            cairo_rectangle(cr, lane_x + x_lo + inset,
                            lane_y + face_h - inner, inner, inner);
            cairo_fill(cr);
        }
    }

    cairo_restore(cr);
}

namespace {

// Shared flag iteration used by render_flags and its phase-reset analogue.
// Invokes `emit(i, left_x)` for EVERY visible marker IN STORE ORDER — which is
// also the PAINT order, and therefore the occlusion order: LATER OVER EARLIER,
// with no other occlusion management of any kind (row 5, 2026-08-01). The
// ascending-x stable sort that used to run here is GONE with the z-order it
// served: the old flags lifted selected shapes above unselected and tie-broke by
// column, and both of those rules retired when selection became a colour swap
// and the marker-text lane's arbitration was deleted.
//
// THE PAINT/HIT INVARIANT. `left_x` — the marker's painted pixel column — is
// computed ONCE here and is the box's LEFT EDGE (the composite shows the stem
// standing on that same column). The painter fills from it, the hit rect is
// published from it, and the stem is published at it, so all three are one
// number by construction.
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

    // THE CULL IS ASYMMETRIC BECAUSE THE BOX IS. A flag opens at its column and
    // runs RIGHTWARD, so a marker to the LEFT of the viewport can still reach
    // into it (by up to a full box width) while a marker AT OR PAST the right
    // edge can show nothing at all. The left margin is the width BOUND
    // (marker_flag_max_width_px) rather than the real width, which is not known
    // until the label is shaped — a bound over-admits a handful of offscreen
    // markers per frame and never drops a visible one.
    //
    // THE RIGHT BOUND IS EXCLUSIVE, like every other viewport-end compare in
    // this tree. `ms == viewport_end_sample` maps to left_x == waveform_width —
    // the first column of the INERT RIGHT GUTTER that a non-multiple-of-16
    // window leaves beside the effective waveform width. At 1920 there is no
    // gutter and the box simply fell off the surface, but at a gutter width the
    // flag painted there AND published a clickable hit rect there, so a marker
    // sitting exactly on the displayed end was visible and selectable outside
    // every grid-aligned surface. "At or past the right edge shows nothing" is
    // the stated rule; this is it spelled.
    const double cull_lo = static_cast<double>(viewport_start_sample) -
                           marker_flag_max_width_px() * samples_per_pixel;
    const double cull_hi = static_cast<double>(viewport_end_sample);
    for (size_t i = 0; i < markers.size(); ++i) {
        const auto& m = markers[i];
        const double eff_time = drag_overlay
            ? drag_overlay->effective_time(
                  static_cast<int>(i), m.time_frame)
            : m.time_frame;
        const double ms =
            frame_to_paint_sample(eff_time, warp_frame_map);
        if (ms < cull_lo) continue;
        if (ms >= cull_hi) continue;   // exclusive — see the cull note above

        const double x_raw =
            (ms - static_cast<double>(viewport_start_sample)) /
            samples_per_pixel;
        const double left_x =
            static_cast<double>(top_strip_area.x) + std::nearbyint(x_raw);

        emit(static_cast<int>(i), left_x);
    }
}

// Cap a marker label at the nine-glyph budget — the contract, the byte/glyph
// identity and the display-only rule all live at kMarkerLabelGlyphBudget
// (render.h). Eight bytes plus U+2026.
std::string cap_marker_label(std::string text) {
    if (text.size() > kMarkerLabelGlyphBudget) {
        text = text.substr(0, kMarkerLabelGlyphBudget - 1) + "\xe2\x80\xa6";
    }
    return text;
}

// The resolved paint of ONE marker flag: the three surfaces plus the stem.
struct FlagFace {
    GuiColor fill;
    GuiColor edge;
    GuiColor label;
    GuiColor stem;
    bool     has_stem;
};

// THE COLOR-CLASS LADDER, one owner for both marker columns (the full statement
// is at render_flags' declaration): disabled wins outright, then red, then the
// default pair with selection swapping it for the bright one — and the DISABLED
// arm runs that same red-then-selection ladder INSIDE ITSELF to pick the pair it
// blends, so selection lifts a disabled marker exactly as it lifts a live one
// (architect 2026-08-01) and red refuses the lift on both sides alike.
//
// THE DISABLED FACE'S LABEL DIMS AGAINST THE FLAG, NOT AGAINST THE LANE. Every
// SHAPE surface takes 25% of itself over the lane ground, as ruled. The LABEL
// takes the same 25%-of-itself through the same mix_color owner but toward the
// surface it actually sits on — the already-blended fill, whichever pair
// produced it, so a selected disabled marker's label dims against ITS OWN
// brighter flag — because that is what
// the redesign's disabled-label rule says ("a fraction of itself over the row's
// CURRENT ground", render.h) and the label's ground here is the flag, not the
// lane. The numbers are why it matters: blending the label toward the LANE
// gives #575757 on a #3f304a flag, a contrast ratio of 1.7 that is not text any
// more; blending it toward the FLAG gives ~#6e6377 at 2.1, which reads as
// dimmed-but-present. Neither is a fade — both resolve to an opaque color
// before cairo sees them, which is the point of the no-alpha rule when flags
// overlap.
FlagFace resolve_flag_face(bool disabled, bool red, bool selected) {
    FlagFace f;
    if (disabled) {
        // The class the marker WOULD paint, blended — the LIVE LADDER RUN
        // WHOLE and then damped, which is why the three arms below are the
        // live arms in the live order. Red keeps its own hue through the blend
        // rather than collapsing to the default one, so a disabled red marker
        // is still recognisably red.
        //
        // SELECTION REACHES THE DISABLED FACE (architect 2026-08-01: the same
        // brightness lift as a regular marker's, "including the border
        // color"). The pair fed into the blend is the SELECTED pair, so a
        // selected disabled marker is the disabled RENDITION OF THE SELECTED
        // FACE — fill and edge both, through the ONE blend, so the lift is
        // exactly the live swap's with the disabled damping applied to it and
        // there is no second brightness rule to keep in step. It cannot
        // resurrect the pre-row-5 masking defect either: the swap happens
        // INSIDE the blend, so the face stays a 25%-of-itself-over-the-ground
        // colour and still reads switched off.
        //
        // RED TAKES NO LIFT, selected or not, mirroring the live red class,
        // which has no selected pair by ruling: a red marker's face is its
        // normalization cue, and selection must not mask it on a disabled
        // marker any more than on a live one.
        GuiColor base_fill;
        GuiColor base_edge;
        if (red) {
            base_fill = kMarkerFlagFillRed;
            base_edge = kMarkerFlagEdgeRed;
        } else if (selected) {
            base_fill = kMarkerFlagFillSel;
            base_edge = kMarkerFlagEdgeSel;
        } else {
            base_fill = kMarkerFlagFill;
            base_edge = kMarkerFlagEdge;
        }
        f.fill  = mix_color(base_fill, kRedesignTabGround, kMarkerDisabledMix);
        f.edge  = mix_color(base_edge, kRedesignTabGround, kMarkerDisabledMix);
        f.label = mix_color(kRedesignLabel, f.fill, kMarkerDisabledMix);
        f.stem  = f.fill;
        f.has_stem = false;      // NO STEM EVER for a disabled marker
        return f;
    }
    if (red) {
        f.fill  = kMarkerFlagFillRed;
        f.edge  = kMarkerFlagEdgeRed;
        f.label = kRedesignLabel;
        f.stem  = kMarkerStemRed;
        f.has_stem = true;
        return f;
    }
    f.fill  = selected ? kMarkerFlagFillSel : kMarkerFlagFill;
    f.edge  = selected ? kMarkerFlagEdgeSel : kMarkerFlagEdge;
    f.label = kRedesignLabel;
    // The stem reads the CLASS ALONE, never the selection bit: a selected
    // default marker keeps the calm #9b59b6 stem (the architect's explicit
    // rule), so only the flag brightens.
    f.stem  = kMarkerFlagFill;
    f.has_stem = true;
    return f;
}

// The one body both columns' painters call. `label_of(i)` composes the marker's
// display text and `disabled_of(i)` answers its column's disabled question (the
// warp side's label_ref cascade, the phase-reset side's bare bool).
template <typename MarkerVec, typename LabelFn, typename DisabledFn>
void render_flag_boxes_impl(
    cairo_t* cr,
    GuiRect top_strip_area,
    FlagLaneRects lanes,
    int waveform_width,
    const MarkerVec& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    const std::set<int>& selected_set,
    const std::set<int>& red_set,
    LabelFn&& label_of,
    DisabledFn&& disabled_of,
    std::vector<FlagHitRect>* out_hit_rects,
    std::vector<MarkerStem>* out_stems,
    const std::vector<WarpFrameMapSegment>* warp_frame_map,
    const DragOverlay* drag_overlay,
    int suppress_box_index) {
    if (out_hit_rects) out_hit_rects->clear();
    if (out_stems)     out_stems->clear();
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (sample_rate <= 0) return;

    const GuiRect lane = lanes.marker_lane;
    if (lane.h <= 0) return;

    cairo_save(cr);
    // THE REDESIGN'S SANS FACE, set ONCE for the whole pass: every label is
    // shaped and painted at this one size on this one scaled font, which is the
    // text_shape precondition (shape with the font you paint with). Nothing
    // below changes the size, so the borrowed scaled-font pointer stays valid
    // for the whole loop.
    cairo_select_font_face(cr, "sans",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    const int    pad_l    = marker_flag_pad_left_px();
    const int    pad_r    = marker_flag_pad_right_px();
    const int    edge_h   = marker_flag_edge_h_px();
    const int    border_w = marker_flag_border_px();
    const double baseline = static_cast<double>(lane.y) +
                            static_cast<double>(marker_flag_baseline_px());

    iterate_visible_flags_impl(top_strip_area, waveform_width, markers,
                               viewport_start_sample, viewport_end_sample,
                               warp_frame_map, drag_overlay,
        [&](int i, double left_x) {
            const std::string text = cap_marker_label(label_of(i));
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, text);

            const int bx = static_cast<int>(std::nearbyint(left_x));
            const int bw = pad_l + pad_r +
                static_cast<int>(std::nearbyint(run.width_px));

            // RED IS COMPUTED INDEPENDENTLY OF DISABLED, unlike the old
            // three-pair ladder where `red` tested `!dis` because disabled had
            // its own opaque PAIR and could not show a hue underneath. Disabled
            // is a BLEND of the marker's own class now, so "which class" is a
            // real question and the answer is the one it belongs to: a disabled
            // red marker blends the RED pair and stays recognisably red.
            // Disabled still WINS — it decides the blend and the missing stem —
            // it just no longer erases the hue.
            const bool dis = disabled_of(i);
            const bool red = red_set.count(i) > 0;
            const bool sel = selected_set.count(i) > 0;
            const FlagFace face = resolve_flag_face(dis, red, sel);

            // THE EDITED MARKER'S BOX IS NOT PAINTED HERE — the open editor
            // owns every pixel of it (render_flag_editor_box, which paints the
            // same face at the same lane y: the flag unrolled).
            //
            // THIS IS A COVERAGE FIX, NOT AN OPTIMIZATION (bug, architect
            // 2026-08-02: "typing leaves the old text painted"). The overlay
            // used to be drawn straight over this box on the assumption that it
            // always covered it, which held only while the editor's text was at
            // least as wide as the committed label — true at open (the editor
            // shows the FULL payload where the label is capped at nine glyphs,
            // plus caret room) and false the moment the user replaces that
            // auto-selected text with something shorter. The overlay then
            // shrank while THIS box kept its committed width, and the tail of
            // the cached label stayed on screen to the right of the editor —
            // read as a stale-pixel/invalidation fault, but the damage was
            // always correct (the whole strip repaints on every keystroke) and
            // the stale ink was this pass's, one z-layer down.
            //
            // WHAT IS SKIPPED IS THE WHOLE BOX, ITS LEFT BORDER INCLUDED: the
            // editor paints the flag entire (border, fill, top edge), so
            // leaving this pass's border standing would put a dark column
            // beside — or, at the editor's left clamp, inside — a box that
            // already draws its own.
            //
            // Suppressing the BOX ONLY is deliberate. The stem below still
            // publishes and still paints: it anchors at the flag's left column
            // and the editor unrolls from that same column, so the marker keeps
            // its stem for the whole session exactly as it does when idle. The
            // hit rect publishes too — the press path resolves an open editor
            // through app.flag_editor_box before it ever walks these rects
            // (active_editor_text, input_pointer.cpp), so the entry is
            // unreachable while the editor is up rather than wrong.
            if (i != suppress_box_index) {
                // Border, then box, then top edge — all AA-off so the 1px band
                // is exactly one row and the box's columns are exactly one
                // column each.
                //
                // THE BORDER IS OUTSIDE THE FILL, one column LEFT of the frame
                // column, in the class-invariant kMarkerFlagBorder (the
                // geometry clause and the clip-at-the-left-edge answer are at
                // marker_flag_border_px, render.h). It is drawn first only for
                // reading order: the two rectangles are disjoint.
                //
                // OVERLAP READS ONE COLUMN EARLIER NOW. A later marker's box
                // covers an earlier one's tail from its BORDER, so two flags a
                // box-width apart butt up as border-against-fill instead of
                // fill-against-fill — which is the whole point of a border in
                // this lane and is why later-over-earlier stays the entire
                // occlusion model.
                cairo_save(cr);
                cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
                cairo_set_source_rgb(cr, kMarkerFlagBorder.r,
                                     kMarkerFlagBorder.g, kMarkerFlagBorder.b);
                cairo_rectangle(cr, bx - border_w, lane.y, border_w, lane.h);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, face.fill.r, face.fill.g, face.fill.b);
                cairo_rectangle(cr, bx, lane.y, bw, lane.h);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, face.edge.r, face.edge.g, face.edge.b);
                cairo_rectangle(cr, bx, lane.y, bw, edge_h);
                cairo_fill(cr);
                cairo_restore(cr);

                // The label, on the run just measured — same font, same glyphs,
                // so the box width and the painted text cannot disagree.
                cairo_set_source_rgb(cr, face.label.r, face.label.g,
                                     face.label.b);
                text_shape::show_shaped_run(
                    cr, run, static_cast<double>(bx + pad_l), baseline);
            }

            if (out_hit_rects) {
                // THE RECT IS THE WHOLE BOX, BORDER INCLUDED — a click on the
                // border is a click on the flag. It is the painted extent, as
                // this stash always was; the border merely made the extent one
                // column wider than the fill.
                FlagHitRect r;
                r.marker_index = i;
                r.x = static_cast<double>(bx - border_w);
                r.y = static_cast<double>(lane.y);
                r.w = static_cast<double>(bw + border_w);
                r.h = static_cast<double>(lane.h);
                out_hit_rects->push_back(r);
            }
            if (out_stems && face.has_stem) {
                // THE STEM STAYS ON THE FILL'S LEFTMOST COLUMN — bx, the
                // marker's own frame column, unchanged by the border standing
                // to its left (the architect's explicit clause, spelled at
                // marker_flag_border_px).
                out_stems->push_back(
                    MarkerStem{i, static_cast<double>(bx), face.stem});
            }
        });

    cairo_restore(cr);
}

} // namespace

void render_flags(cairo_t* cr,
                  GuiRect top_strip_area,
                  FlagLaneRects lanes,
                  int waveform_width,
                  const std::vector<GuiWarpMarker>& markers,
                  long long viewport_start_sample,
                  long long viewport_end_sample,
                  int sample_rate,
                  const std::set<int>& selected_set,
                  const std::set<int>& red_set,
                  bool iteration_on,
                  std::vector<FlagHitRect>* out_hit_rects,
                  std::vector<MarkerStem>* out_stems,
                  const std::vector<WarpFrameMapSegment>* warp_frame_map,
                  const DragOverlay* drag_overlay,
                  int editing_marker_index) {
    render_flag_boxes_impl(
        cr, top_strip_area, lanes, waveform_width, markers,
        viewport_start_sample, viewport_end_sample, sample_rate,
        selected_set, red_set,
        // The ONE composer the flag paint, the editor seed and the copy payload
        // all share, so a flag shows exactly what its editor would open with.
        [&](int i) { return flag_text_iter(markers, i, iteration_on); },
        // The warp column's disabled verdict follows the label_ref cascade.
        [&](int i) { return effective_disabled(markers, i); },
        out_hit_rects, out_stems, warp_frame_map, drag_overlay,
        editing_marker_index);
}

void render_phase_reset_flags(cairo_t* cr,
                            GuiRect top_strip_area,
                            FlagLaneRects lanes,
                            int waveform_width,
                            const std::vector<GuiPhaseResetMarker>& phase_resets,
                            long long viewport_start_sample,
                            long long viewport_end_sample,
                            int sample_rate,
                            const std::set<int>& selected_set,
                            const std::set<int>& red_set,
                            std::vector<FlagHitRect>* out_hit_rects,
                            std::vector<MarkerStem>* out_stems,
                            const std::vector<WarpFrameMapSegment>* warp_frame_map,
                            const DragOverlay* drag_overlay) {
    render_flag_boxes_impl(
        cr, top_strip_area, lanes, waveform_width, phase_resets,
        viewport_start_sample, viewport_end_sample, sample_rate,
        selected_set, red_set,
        // A phase reset authors no payload, so its flag carries the display-only
        // token (render.h owns it and the reason for its width).
        [&](int) { return std::string(kPhaseResetLaneToken); },
        // No label_ref cascade on this column — the bool is the whole verdict.
        [&](int i) { return phase_resets[i].disabled; },
        out_hit_rects, out_stems, warp_frame_map, drag_overlay,
        // NO SUPPRESSION ON THIS COLUMN, and the asymmetry is real rather than
        // an oversight (the warp/phase-reset symmetry rule, conventions.md):
        // the flag editor is a WARP-column surface by its own open gates —
        // render_flag_editor_box returns early on anything but a FlagPayload
        // editor and reads app.warpmarkers — so no phase-reset flag can ever be
        // the edited one. If a phase-reset payload editor is ever added, this
        // is the line it changes.
        /*suppress_box_index=*/-1);
}

namespace {
    // Current GUI scale, in PERCENT. Set by set_gui_scale_percent from the
    // three application points (file load, the settings editor's gui_scale
    // commit, the adopt). EVERY painted pixel quantity in the product reads it
    // through gui_scale_factor().
    int    g_gui_scale_percent = 100;
    // Cached triangle masks and the H each was built at; regenerated by their
    // accessors when H changes. The playhead mask is the full triangle
    // (2H-1 x H), stamped centered on the column at the playhead cursor and at
    // every marker/trim flag's triangle.
    cairo_surface_t* g_playhead_triangle   = nullptr;
    int              g_playhead_triangle_h = 0;
} // namespace

void   set_gui_scale_percent(int percent) { g_gui_scale_percent = percent; }
double gui_scale_factor()  {
    return static_cast<double>(g_gui_scale_percent) / 100.0;
}

// Build a fresh A8 tip-down triangle mask of height h (W = 2h-1). The triangle
// is filled as an ANTIALIASED cairo path — full-width top edge [0, W] down to the
// bottom-center apex (column (W-1)/2 = h-1) — so its two slopes carry baked gray
// edge alphas (the relaxed aliasing rule: diagonals may antialias). This is the
// tip-down triangle the playhead cursor stamps, the identical geometry the
// marker/trim flags that path-filled the same triangle are gone (row 5).
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

// -- The flag editor's unrolled box ---------------------------------------

// The contract (the face, the clamp, the view truncation, the non-const
// AppState) is at the declaration in render.h. What follows is the mechanics.
void render_flag_editor_box(cairo_t* cr, AppState& app, const GuiAudio& audio) {
    // The publication is unconditional: every run that finds no open editor
    // writes the invalid state, so a closed session can never leave the pointer
    // path a stale box to grab.
    FlagEditorBox& out = app.flag_editor_box;
    out = FlagEditorBox{};

    text_editor::State& ed = app.top_flag_editor;
    if (!text_editor::is_active(ed)) return;
    if (ed.kind != text_editor::Kind::FlagPayload) return;

    // The flag editor is a WARP-column, source-home surface by its own open
    // gates, so the marker is a warp marker and its class ladder is the warp
    // one. A target index the store has since shrunk past is the only failure
    // shape, and it simply paints nothing.
    const std::vector<GuiWarpMarker>& mv = app.warpmarkers.markers();
    const int idx = ed.target;
    if (idx < 0 || idx >= static_cast<int>(mv.size())) return;

    const GuiRect lane = top_marker_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    cairo_save(cr);
    // The redesign's sans, set once — shape and paint on ONE scaled font, the
    // text_shape precondition. Nothing below changes the size.
    cairo_select_font_face(cr, "sans",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    // THE FULL, UNTRUNCATED pending — the unroll's whole point. The nine-glyph
    // budget is a LABEL rule; an editor shows what it is editing.
    const text_shape::ShapedRun run =
        text_shape::shape_text_run(font, ed.pending);
    std::vector<double> byte_x =
        text_shape::byte_offsets_px(run, ed.pending.size());

    const int pad_l    = marker_flag_pad_left_px();
    const int pad_r    = marker_flag_pad_right_px();
    const int edge_h   = marker_flag_edge_h_px();
    const int border_w = marker_flag_border_px();
    // CARET ROOM. The caret at end-of-text stands one column past the last
    // glyph, so the box must own a column the run does not; without it the
    // caret would sit on the right pad or, at the clamp, off the box entirely.
    // One authored pixel, scaled like every other row-5 length.
    const int caret_w = static_cast<int>(std::nearbyint(1.0 * gui_scale_factor()));
    const int caret_px = caret_w < 1 ? 1 : caret_w;

    const int run_w = static_cast<int>(std::nearbyint(run.width_px));
    int box_w = pad_l + run_w + caret_px + pad_r;

    // THE CLAMP, in two stages. First the box is capped at the LANE's own width
    // — a payload wider than the window cannot be shown whole, and this is
    // where the view starts truncating instead. Then the box's left edge, which
    // wants to be the marker's painted column (the flag's own left edge, so the
    // box unrolls FROM the flag rather than jumping), slides left far enough to
    // keep the right edge on-window.
    if (box_w > lane.w) box_w = lane.w;

    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    const int col = painted_column_of_source_frame_on_basis(
        app, audio, static_cast<double>(mv[idx].time_frame),
        map, basis.vp_start, basis.spp);
    const GuiRect area = waveform_area(app);

    const double min_left = static_cast<double>(lane.x);
    const double max_left = static_cast<double>(lane.x + lane.w - box_w);
    double box_left = static_cast<double>(area.x + col);
    if (max_left <= min_left) box_left = min_left;
    else if (box_left < min_left) box_left = min_left;
    else if (box_left > max_left) box_left = max_left;
    const int bx = static_cast<int>(std::nearbyint(box_left));

    // The text VIEWPORT inside the box: the band the run is clipped to, and the
    // width the view offset is measured against. The caret column belongs to it
    // (a caret at the end must be inside the clip to be seen), which is why
    // caret room is added to the viewport and not just to the box.
    const double view_x0 = static_cast<double>(bx + pad_l);
    const double view_x1 = static_cast<double>(bx + box_w - pad_r);
    const double view_w  = view_x1 - view_x0;

    // THE MINIMAL-TRAVEL VIEW OFFSET (the field's contract is at
    // State::view_offset_px). Scroll only as far as the caret demands, in
    // whichever direction it left the window, then clamp to the run's own
    // travel — so a caret walking right pushes the view right one glyph at a
    // time and walking back left pulls it back the same way, never jumping.
    // The caret's own column is reserved at the right edge, so the comparison
    // is against (view_w - caret) rather than view_w: a caret at end-of-text
    // stops with its column inside the clip instead of half past it.
    const int cursor_pos =
        std::clamp(ed.cursor_pos, 0, static_cast<int>(ed.pending.size()));
    const double caret_off = byte_x[static_cast<size_t>(cursor_pos)];
    const double travel_w  = view_w - static_cast<double>(caret_px);
    double vo = ed.view_offset_px;
    if (caret_off - vo < 0.0)        vo = caret_off;
    if (caret_off - vo > travel_w)   vo = caret_off - travel_w;
    const double max_vo = run.width_px + static_cast<double>(caret_px) - view_w;
    if (vo > max_vo) vo = max_vo;
    if (vo < 0.0)    vo = 0.0;
    ed.view_offset_px = vo;

    const double text_origin_x = view_x0 - vo;
    const double baseline = static_cast<double>(lane.y) +
                            static_cast<double>(marker_flag_baseline_px());

    // THE MARKER'S OWN FACE, through the one class ladder — so the open editor
    // is visibly the same flag, only wider. The red flash overrides the whole
    // pair (this lane's red, not the bottom strip's chip red — see the
    // declaration), because a failed commit must read as a state of THIS box
    // and not as a marker that suddenly normalized.
    const bool dis = effective_disabled(mv, idx);
    const bool red_class =
        warp_red_flag_set_cached(
            app, audio.sample_rate(),
            static_cast<long>(audio.total_frames())).red.count(idx) > 0;
    const bool sel = app.selected_markers.count(idx) > 0;
    FlagFace face = resolve_flag_face(dis, red_class, sel);
    if (ed.red) {
        face.fill  = kMarkerFlagFillRed;
        face.edge  = kMarkerFlagEdgeRed;
        face.label = kRedesignLabel;
    }

    // 1. The box: the 1px left border, the fill, then the 1px top edge — AA
    //    off, exactly as a flag.
    //
    // THE EDITOR CARRIES THE BORDER TOO, and the argument is the one this whole
    // surface rests on: the open editor IS the marker's flag, unrolled, and
    // "opening an editor changes the flag's SIZE and nothing else about how it
    // reads" (the declaration's own contract). A border the idle flag draws and
    // the editor dropped would break exactly that promise at the moment the two
    // are most directly compared — the flag is suppressed underneath and this
    // box stands in its place, on its column, one column of which would go
    // missing on open and come back on commit. It is also the form the BOTTOM
    // editors take now (render_bottom_strip_editor's invalid flash, 2026-08-02),
    // so the product's two editor painters draw one box anatomy between them.
    //
    // The border sits OUTSIDE the fill like the flag's, so nothing the clamp,
    // the text viewport or the view offset computed above moves: box_w, view_x0
    // and view_x1 are all fill-relative, and at the left clamp this column
    // simply falls off the surface exactly as a flag's does.
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgb(cr, kMarkerFlagBorder.r, kMarkerFlagBorder.g,
                         kMarkerFlagBorder.b);
    cairo_rectangle(cr, bx - border_w, lane.y, border_w, lane.h);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, face.fill.r, face.fill.g, face.fill.b);
    cairo_rectangle(cr, bx, lane.y, box_w, lane.h);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, face.edge.r, face.edge.g, face.edge.b);
    cairo_rectangle(cr, bx, lane.y, box_w, edge_h);
    cairo_fill(cr);
    cairo_restore(cr);

    // The caret / selection band: the box interior under the top edge. A text
    // field's caret spans its whole field, and here the field IS the box, so
    // this needs no font-extent solve — the top edge is the only row it must
    // stay clear of.
    const int band_y = lane.y + edge_h;
    const int band_h = lane.h - edge_h;

    // Everything from here paints CLIPPED to the text viewport, so a scrolled
    // run, its selection and its caret all stop at the pads instead of bleeding
    // over the box edge into the neighbouring flags.
    cairo_save(cr);
    cairo_rectangle(cr, view_x0, static_cast<double>(lane.y),
                    view_w, static_cast<double>(lane.h));
    cairo_clip(cr);

    // 2. The selection highlight, then 3. the text — the two-tone convention
    //    convention the retired monospace box used: the selected span fills with the label
    //    colour and its glyphs repaint in the box fill for contrast. Both edges
    //    come from byte_x, so the highlight cannot drift off the glyphs it
    //    marks however proportional they are.
    const bool has_sel = text_editor::has_selection(ed);
    if (has_sel) {
        const size_t s0 = static_cast<size_t>(text_editor::selection_start(ed));
        const size_t s1 = static_cast<size_t>(text_editor::selection_end(ed));
        const double hx0 = text_origin_x + byte_x[s0];
        const double hx1 = text_origin_x + byte_x[s1];
        const int ix0 = static_cast<int>(std::nearbyint(hx0));
        const int ix1 = static_cast<int>(std::nearbyint(hx1));
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, face.label.r, face.label.g, face.label.b);
        cairo_rectangle(cr, ix0, band_y, (ix1 > ix0) ? (ix1 - ix0) : 1, band_h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    cairo_set_source_rgb(cr, face.label.r, face.label.g, face.label.b);
    text_shape::show_shaped_run(cr, run, text_origin_x, baseline);
    if (has_sel) {
        // The selected substring repainted in the FILL colour, clipped to the
        // highlight's own span. Re-showing the whole run under a clip keeps the
        // glyph positions bit-identical to the pass above — shaping the
        // substring separately could kern its first glyph differently.
        const size_t s0 = static_cast<size_t>(text_editor::selection_start(ed));
        const size_t s1 = static_cast<size_t>(text_editor::selection_end(ed));
        cairo_save(cr);
        cairo_rectangle(cr, text_origin_x + byte_x[s0],
                        static_cast<double>(lane.y),
                        byte_x[s1] - byte_x[s0],
                        static_cast<double>(lane.h));
        cairo_clip(cr);
        cairo_set_source_rgb(cr, face.fill.r, face.fill.g, face.fill.b);
        text_shape::show_shaped_run(cr, run, text_origin_x, baseline);
        cairo_restore(cr);
    }

    // 4. The caret: a blink-gated filled integer column at the cursor's own
    //    byte boundary, AA off — the same crisp-column convention the
    //    retired monospace box used, on a shaped position instead of a grid one.
    if (text_editor::cursor_visible_now(ed)) {
        const int cx =
            static_cast<int>(std::nearbyint(text_origin_x + caret_off));
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, face.label.r, face.label.g, face.label.b);
        cairo_rectangle(cr, cx, band_y, caret_px, band_h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    cairo_restore(cr);   // the text-viewport clip
    cairo_restore(cr);   // the font state

    out.valid         = true;
    // THE PUBLISHED BOX IS THE PAINTED BOX, BORDER INCLUDED — the same rule the
    // flags' hit rects take: a press on the border is a press on this editor,
    // and it maps to byte 0 through the nearest-boundary search exactly as a
    // press on the left pad does (the box-is-the-claim clause at FlagEditorBox).
    out.box           = GuiRect{bx - border_w, lane.y, box_w + border_w, lane.h};
    out.text_origin_x = text_origin_x;
    out.byte_x        = std::move(byte_x);
}
