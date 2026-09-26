#include "render.h"
#include "app_state.h"
#include "audio.h"
#include "gui_display_context.h"
#include "gui_font.h"
#include "text_shape.h"
#include "value_format.h"
#include "warp_frame_map_view.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// kFlagBottomLiftPx now lives in render.h so the strip lane geometry in
// main.cpp and the stem blit in paint_handler.cpp reference the same value.

// playhead_half_px() is the half-width of the playhead column's reach; it lives
// in render.h as a single inline accessor shared by this TU's cull and
// main.cpp's invalidation, with its provenance and its authored value stated at
// the definition.

// THE NUMERIC RUN — the derived base and its whole deviation chain — is
// warp_tempo_run (warpmarkers.h), which both flag composers below and the
// SERIALIZER share. It stood here as a private twin of the serializer's own
// loop for one day, 2026-09-18/19: what the deviation chain promises is that
// the flag says what the file holds, and one body is what makes that true
// rather than a coincidence of two.

// Flag text mirrors the canonical line's PAYLOAD (post-pipe); metadata
// (b=/e=/#) never appears in it, and neither does the iteration bracket —
// the bounds are the two cells beside the flag, each with its own editor.
// This is the ONE composer for warp flag text and it CUTS NOTHING: the flag
// editor seeds from it (enter_top_flag_edit), so a commit cannot lose what
// the store holds. What the box paints is flag_display_text below. The
// contract is at the declaration (render.h).
//
// Variants:
//   label_ref              → "a.42"
//   inherit, no def        → "pass"
//   inherit, with def      → "pass:a.42"
//   owning, no scale       → "1.23"
//   owning, with a chain   → "1.23+0.01-0.02"
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
        // Serializer forms (the tempo run straight from integer cents, scale
        // min-4 padded shortest round trip) — the flag paints the stored
        // value at full precision, exactly the serializer's bytes.
        text = warp_tempo_run(m);
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

// THE PAINTED FORM (architect 2026-09-19). The contract — what is cut, what
// never is, and why the seam measurement must read this and not its uncut
// sibling — is at the declaration (render.h). The scale is the only cut, and
// the truncation marker says so; with no scale there is nothing to cut and
// the whole payload paints, label definition included.
std::string flag_display_text(const std::vector<GuiWarpMarker>& markers,
                              int idx) {
    const auto& m = markers[idx];

    if (!m.label_ref.empty()) {
        return m.label_ref;
    }

    std::string text = m.tempo_inherits ? std::string("pass")
                                        : warp_tempo_run(m);
    if (!m.tempo_scale.has_value()) {
        if (!m.label_def.empty()) {
            text += ":";
            text += m.label_def;
        }
        return text;
    }
    // A scale is spelled min-4, so it is always longer than the cap and the
    // marker always follows — which is why the `||` below is not a
    // one-armed test in practice; it is there because the rule is "the marker
    // stands for whatever was cut", and a label definition is cut here too.
    const std::string scale = format_value_double(*m.tempo_scale, 4);
    text += "*";
    text += scale.substr(0, kMarkerFlagScaleGlyphs);
    if (scale.size() > kMarkerFlagScaleGlyphs || !m.label_def.empty())
        text += kMarkerLabelTruncationMarker;
    return text;
}

namespace {

// Forward-translate a per-marker effective position (a source-frame
// double) to the paint-sample position used by the stem, flag, and
// hit-rect loops. In target view (warp_frame_map
// non-null/non-empty) the source-frame is rounded with banker's
// nearbyint and looked up through map_source_to_target, and that lookup
// is itself rounded with nearbyint; in source view (null/empty
// warp_frame_map) the result is the frame double rounded with nearbyint.
// Both branches return the same integer displayed frame the playhead
// cursor stores (the active-domain translators apply the same
// nearbyint), so the stem, endcap, hit rect, and playhead share a column
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
                     const WaveformGainCurve* gain_or_null,
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

    // THE VISUAL MAGNIFICATION, a function of source time: each column's
    // OUTER scale is the derived curve's gain at the column's centre source
    // frame, its INNER scale the compressor's scale at the same frame, and
    // both take the expander's multiplier over the column's working columns.
    // The contract (the two bars' order and inks, the coarse-zoom centre rule
    // and the expander's smallest-reduction rule) is at this function's
    // declaration; the arithmetic is one multiply and ONE clamp per tip.
    // IT SCALES PIXELS ONLY — nothing this function touches is audio.
    const auto magnified_tip = [](double raw, double scale) {
        double v = raw * scale;
        if (v < -1.0) v = -1.0;
        if (v >  1.0) v =  1.0;
        return v;
    };

    // Each column is written straight into the plate's pixel words, and a
    // column is ONE HARD BAR: its own raw min/max interval, floored to rows and
    // filled inclusively with the opaque ink word (with the lamp lit, the
    // outer bar goes down first in the plate's word and the inner over it in
    // the core's — the rule is at this function's declaration).
    // There is no
    // interior/edge split, no fractional coverage, and no inter-column
    // connectivity of any kind — a spike stands alone, exactly as in a classic
    // min/max renderer.
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
    // THE PREMULTIPLIED WORDS, each built once per call through the one word
    // owner (argb32_opaque_word, render.h — its byte-order and rounding
    // contract lives there): the plate's ink (the row-6 constant), worn by
    // the dark lamp's raw bar and the lit lamp's outer, and the foreground
    // (kWaveformForegroundInk) worn by the lit lamp's inner (built always,
    // written only when lit).
    const uint32_t ink_word = argb32_opaque_word(kWaveformInk);
    const uint32_t fg_word  = argb32_opaque_word(kWaveformForegroundInk);

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
    // call regenerates, and each column is written once by its one bar with
    // the lamp dark, and lit by its outer bar and then its inner, which
    // overwrites the outer where they overlap, the order the magnification rule wants
    // (the max-compositing the tip segments needed went with them).
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
        const int x = area.x + i;

        // THE BAR. The column's tips — its maximum -> top tip, its minimum ->
        // bottom tip, in float rows, never snapped — are clamped to this
        // channel's rows BEFORE any row index is derived, so a clipped interval
        // cannot address outside the band; then both ends are floored and the
        // bar filled inclusively with `word`. r0 == r1 for any sub-pixel
        // interval, which is the >=1px floor stated at the top of this
        // function, and it holds for both bars. The regime split (thin vs tall)
        // went with the tip segments: there is one rendering for every column
        // now, however small its interval. One writer for both bars, so the
        // outer and the inner cannot disagree about the geometry.
        const auto fill_bar = [&](double tip_min, double tip_max, uint32_t word) {
            if (x < col_lo || x >= col_hi) return;
            double yt = y_center - tip_max * half_h;
            double yb = y_center - tip_min * half_h;
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
            for (int y = r0; y <= r1; ++y) put(x, y, word);
        };

        // LIT: THE OUTER FIRST, the column's raw extremes times the curve's
        // gain at the column's centre source frame and the expander's largest
        // multiplier over the working columns [s0, s1) spans, in the
        // background ink; THEN THE INNER over it, the same extremes times the
        // compressor's scale at the same frame and the same multiplier, in
        // the foreground ink. Each is clamped to the sample domain [-1, 1] BEFORE
        // it becomes rows: the clamp is what makes a magnified forte clip
        // flat against the lane's edges instead of running off into row
        // arithmetic (the inner's scale is at most 1, so its clamp is a
        // no-op kept for the one shape). A PICTURE gain: the samples
        // themselves are untouched, here and everywhere. Replace-writes:
        // where the two overlap the inner wins.
        // DARK: the raw bar at scale 1.0, where the clamp is a no-op (raw
        // peaks already rest in range), so the dark plate is the plate this
        // writer always drew.
        if (gain_or_null) {
            const int64_t centre = (s0 + s1) / 2;
            const double e = static_cast<double>(
                waveform_expander_multiplier_over(*gain_or_null, s0, s1));
            const double outer = waveform_gain_at(*gain_or_null, centre) * e;
            const double inner = waveform_inner_scale_at(*gain_or_null, centre) * e;
            fill_bar(magnified_tip(mm.first, outer),
                     magnified_tip(mm.second, outer), ink_word);
            fill_bar(magnified_tip(mm.first, inner),
                     magnified_tip(mm.second, inner), fg_word);
        } else {
            fill_bar(magnified_tip(mm.first, 1.0),
                     magnified_tip(mm.second, 1.0), ink_word);
        }

        g_prev = g1;
    }

    // Last CPU write is done — hand the buffer back to cairo.
    cairo_surface_mark_dirty(dest);
}

void render_playhead(cairo_t* cr,
                     GuiRect area,
                     double  playhead_pixel_x,
                     GuiColor color) {
    if (area.w <= 0 || area.h <= 0) return;
    // A coarse cull in the column's own reach (playhead_half_px, the
    // half-width the narrow invalidation uses); the column gate below decides
    // whether any pixel lands.
    if (playhead_pixel_x < -static_cast<double>(playhead_half_px())) return;
    if (playhead_pixel_x > static_cast<double>(area.w + playhead_half_px())) return;

    // THE COLUMN IS THE NEAREST LATTICE POINT WHILE THE PLATE'S BAR IN IT IS A
    // CELL, AND THE HALF-COLUMN BIAS THAT LEAVES IS ACCEPTED (architect
    // 2026-09-02, the truthfulness deep dive's item N, a coarse-zoom cost).
    // The playhead — and the marker stems, endcaps and flags, which all reach
    // their column through the same nearbyint rule (frame_to_paint_sample at
    // the head of this file) — paints AT a lattice point, while the bar drawn
    // in that column covers the frames AFTER it, [g(c), g(c+1)) in the plate
    // loop above. So a point and the bar under it can disagree by up to half a
    // column: invisible at the working zoom, and only at coarse zooms does the
    // line fall on the wrong side of a transient (up to ~0.47 s at the full
    // zoom-out of a 30-minute movement, where one column is nearly a second
    // wide). The POINT model is what is load-bearing and it is not traded for
    // the cell rule here: every column→frame landing in the product rides it,
    // with the worst-case round-trip residue derived as 0.345 px in
    // zoom-viewport-strip.md. Reading the bar as [g(c) − spp/2, g(c) + spp/2)
    // is the alternative that was declined.
    const double col  = std::nearbyint(playhead_pixel_x);
    const double x_px = area.x + col + 0.5;

    cairo_save(cr);
    // The 1px vertical line paints whenever its column is a paintable grid
    // point: [0, w], GRID POINT w INCLUDED (architect 2026-09-26) — it lies in
    // the permanent right gutter (waveform_area, main.cpp), which exists so a
    // frame in the song's last half-column, rounding one past the last
    // column, still paints at its true point rather than vanishing or being
    // pulled inward. Column-gated otherwise, so it never leaks into an
    // adjacent region.
    // ONE SOLID LINE, straight over whatever it crosses — waveform ink included.
    // A saturated stem over the dark ink reads without any cut, so there is no
    // two-tone overdraw here (see the declaration for the retirement).
    if (col >= 0.0 && col <= static_cast<double>(area.w)) {
        cairo_set_source_rgb(cr, color.r, color.g, color.b);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, x_px, area.y);
        cairo_line_to(cr, x_px, area.y + area.h);
        cairo_stroke(cr);
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
// One column formula, one mapping helper, one endcap rect, one bridge-gap owner.
// See render.h for the full rationale (the UNIFIED displayed basis — the
// painter decides against the committed viewport and the hit sites read what
// it publishes, the event-sync ruling; the quantized-span denominator; the
// EOF-wall clamp; and the side-aware bridge sentinels).

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
    // where vp_end itself was derived as vp_start + wave_w·q
    // (viewport_end_sample), so this is q exactly.
    const double span = static_cast<double>(vp_end - vp_start);
    const double samples_per_pixel = span / static_cast<double>(wave_w);
    // The one rounding, on the caller's UNIFIED displayed basis (this file's
    // header block above): displayed_column_at, warp_frame_map_view.h.
    out.col_raw = displayed_column_at(displayed_ms,
                                      static_cast<double>(vp_start),
                                      samples_per_pixel);
    out.col = out.col_raw;
    if (wave_w > 0)
        out.col = std::clamp(out.col, 0, wave_w - 1);
    return out;
}

TrimBridgeGap trim_bridge_gap(const TrimBoundColumn& begin,
                              const TrimBoundColumn& end, int endcap_w,
                              int wave_w) {
    // Contract (4x2 table) at the declaration. A PAINTED (InView) endcap bounds the
    // gap at its inner edge (inset by endcap_w — the room the endcap occupies); an
    // OFFSCREEN bound paints no endcap, so the bar runs FLUSH. The offscreen arms
    // key on the bound's SIDE (not col_raw, which cannot tell the side across the
    // rounding seam), and use side-specific SENTINELS so an offscreen edge lands
    // STRICTLY past the visible range — never col 0 / col wave_w-1 — which is what
    // makes the flush fill AND the offscreen ring-border clip hold.
    TrimBridgeGap g;
    switch (begin.side) {
        case TrimBoundSide::InView:
            g.lo = begin.col + endcap_w; break;
        case TrimBoundSide::OffLeft:
            g.lo = std::min(begin.col_raw, -1); break;      // strictly < 0
        case TrimBoundSide::OffRight:
            g.lo = std::max(begin.col_raw, wave_w); break;  // >= wave_w -> empty
    }
    switch (end.side) {
        case TrimBoundSide::InView:
            g.hi = end.col - endcap_w + 1; break;
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

GuiRect trim_endcap_rect(bool is_begin, int strip_x, int col, GuiRect row) {
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
                       GuiRect trim_bar,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim,
                       TrimBarHit* out_hit) {
    // COLD FIRST, so every early return below publishes "nothing grabbable"
    // over a lane that painted no bar (the contract at the declaration).
    if (out_hit) *out_hit = TrimBarHit{};
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (trim_bar.w <= 0 || trim_bar.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (waveform_area.w <= 0) return;

    // Both bounds resolve through the ONE shared column owner:
    // .col is the clamped column, .side the offscreen verdict. The
    // bar spans between them even when a bound is culled, so the columns are
    // computed unconditionally.
    const TrimBoundColumn bc = trim_bound_column(
        static_cast<double>(trim.begin), viewport_start_sample,
        viewport_end_sample, waveform_area.w);
    const TrimBoundColumn ec = trim_bound_column(
        static_cast<double>(trim.end), viewport_start_sample,
        viewport_end_sample, waveform_area.w);

    const int lane_x   = trim_bar.x;
    const int lane_w   = waveform_area.w;   // the effective width
    const int lane_y   = trim_bar.y;
    const int lane_h   = trim_bar.h;
    // The lane is the crop's 10 rows times kTrimBarScalePercent (back at 100
    // since the seventh glass ruling, 2026-08-12, so 10 at 100% scale — one
    // more than the 9 the crop measured before the 2026-09-16 flip to
    // kdenlive's own orientation added its shared bottom border row;
    // render.h carries the one-commit 150 experiment's record): the border
    // and the bevel pair keep their crop heights whatever the factor, so any
    // extra rows land in the face band alone.
    const int border_h = std::min(trim_lane_border_h_px(), lane_h);
    const int body_h   = lane_h - border_h;  // the crop's rows 0..8: bevel + face
    const int bevel_h  = std::min(trim_bevel_h_px(), body_h);
    const int face_h   = body_h - bevel_h;   // the crop's rows 2..8, grown
    const int hi_h     = bevel_h / 2;        // the row against the face: the lighter shade
    const int lo_h     = bevel_h - hi_h;     // the lane's outer row: the darker one

    cairo_save(cr);
    cairo_rectangle(cr, lane_x, lane_y, lane_w, lane_h);
    cairo_clip(cr);

    // ONE PAINTER FOR A SURFACE'S WHOLE COLUMN RUN: the two bevel rows at the
    // lane's TOP — darker then lighter, kdenlive's own orientation since the
    // 2026-09-16 flip (this file painted them at the bottom, lighter then
    // darker, before that day) — then the face rows below them, all
    // pixel-bound integer fills (crisp by construction, no stroke, no
    // antialiasing anywhere in this lane). The shared bottom border is NOT
    // this lambda's: it is one fill across the whole lane, painted once after
    // every surface below has had its turn (below).
    auto surface = [&](int x0, int w, GuiColor face, GuiColor hi, GuiColor lo) {
        if (w <= 0) return;
        if (lo_h > 0) {
            cairo_set_source_rgb(cr, lo.r, lo.g, lo.b);
            cairo_rectangle(cr, x0, lane_y, w, lo_h);
            cairo_fill(cr);
        }
        if (hi_h > 0) {
            cairo_set_source_rgb(cr, hi.r, hi.g, hi.b);
            cairo_rectangle(cr, x0, lane_y + lo_h, w, hi_h);
            cairo_fill(cr);
        }
        cairo_set_source_rgb(cr, face.r, face.g, face.b);
        cairo_rectangle(cr, x0, lane_y + lo_h + hi_h, w, face_h);
        cairo_fill(cr);
    };

    // GROUND everywhere first, then the window's BAR over it, then the endcaps
    // over that — painting back to front means no run has to know what its
    // neighbour is, and an inverted or degenerate window simply leaves the
    // ground showing.
    surface(lane_x, lane_w, kRedesignContentGround,
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
    // Both caps come from the ONE rect owner (trim_endcap_rect), and THE
    // PUBLICATION RIDES THE FILLS (TrimBarHit, render.h): each cap is stashed
    // from the rect it was just painted with, so the painted cap and the
    // grabbable cap describe the same edge — the hit side adds only its stated
    // grab tolerance — and the hit reads the pixels rather than a second
    // derivation of them.
    if (bc.in_viewport) {
        const GuiRect r = trim_endcap_rect(true, lane_x, bc.col, trim_bar);
        surface(r.x, r.w, kTrimLaneEndcap, kTrimCapBevelHi, kTrimCapBevelLo);
        if (out_hit) out_hit->begin = {true, lane_x + bc.col, r};
    }
    if (ec.in_viewport) {
        const GuiRect r = trim_endcap_rect(false, lane_x, ec.col, trim_bar);
        surface(r.x, r.w, kTrimLaneEndcap, kTrimCapBevelHi, kTrimCapBevelLo);
        if (out_hit) out_hit->end = {true, lane_x + ec.col, r};
    }

    // THE MIDPOINT MARK IS THE CROP, BLITTED VERBATIM (architect 2026-08-01, who
    // overlaid row_5_lane_1_trim_middle.png on the running GUI and ruled it
    // implemented exactly; RE-FLIPPED with the rest of the lane on 2026-09-16,
    // to kdenlive's own orientation — the crop file itself is the old one
    // flipped vertically, verified pixel for pixel). The 9x9 crop (the shared
    // bottom border row is the LANE's, painted once below for every surface
    // including this one, never the tile's own) is a LANE-HEIGHT TILE, and
    // every pixel of it is already one of this lane's own surfaces:
    //
    //   row 0      #94b0c0  kTrimCapBevelLo    the endcap bevel pair, verbatim,
    //   row 1      #9dbbcb  kTrimCapBevelHi    now at the tile's TOP
    //   rows 2..8  #97b4c4  kTrimLaneEndcap    the tile's face
    //   cols 2..6 } #2f6888 kTrimLaneBar       the inner square, inset 2px,
    //   rows 2..6 }                            flush UNDER the bevel
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
    // paint: no shrink, no clamp of the TILE. (The INNER SQUARE's height is a
    // separate matter — it IS clamped, to keep the tile's BOTTOM rim (the TOP
    // rim before the 2026-09-16 flip moved the square to hang under the bevel
    // instead of on it) from collapsing at small scales; that rule and its
    // reasoning live at the paint site below.)
    {
        const int tile  = trim_middle_size_px();
        const int inset = trim_middle_inset_px();
        const int clear = trim_middle_clear_px();
        // THE INNER SQUARE'S WIDTH IS THE PARTITION'S REMAINDER, never its own
        // rounding (codex round 3, 2026-08-10 — the tab lock slot's fix applied
        // to the crop's other composite). The crop's ring is symmetric,
        // inset + inner + inset == tile, and it USED to be three independent
        // nearbyints: the left rim was `inset` and the right rim was whatever
        // tile - inset - inner happened to leave, so the two disagreed at 71
        // legal scales (at 75% the ring read 2 left / 1 right, at 62% 1 left /
        // 2 right — a mark that is visibly off-centre in a 6px tile). Derived,
        // both rims ARE `inset` at every scale by construction and the mark is
        // centred by arithmetic. Byte-identical where the ring already closed:
        // 9 - 2*2 == 5 at 100%, 14 - 2*3 == 8 at 150%, 18 - 2*4 == 10 at 200%.
        // The >= 1 guard mirrors the height's below; measured, it never fires
        // in [50, 350] (the tightest tile is 4 columns at 50%, giving 2).
        const int inner_w_raw = tile - 2 * inset;
        const int inner_w = inner_w_raw < 1 ? 1 : inner_w_raw;
        const TrimBridgeGap gap =
            trim_bridge_gap(bc, ec, trim_endcap_w_px(), lane_w);
        const int vis_lo = std::max(gap.lo, 0);
        const int vis_hi = std::min(gap.hi, lane_w);
        // THE BRIDGE'S PUBLICATION is this same visible interior — the bar's
        // stretch between the caps' inner edges, clipped to the lane's painted
        // width — so the pair drag's handle and the midpoint mark's room are
        // one interval, and the caps sit outside it by the gap's own inset.
        if (out_hit) {
            out_hit->published = true;
            out_hit->lane      = GuiRect{lane_x, lane_y, lane_w, lane_h};
            out_hit->bridge_lo = lane_x + vis_lo;
            out_hit->bridge_hi = lane_x + vis_hi;
        }
        const TrimBoundColumn mc = trim_bound_column(
            (static_cast<double>(trim.begin) + static_cast<double>(trim.end)) *
                0.5,
            viewport_start_sample, viewport_end_sample, lane_w);
        const int x_lo = mc.col - tile / 2;    // waveform-relative, inclusive
        const int x_hi = x_lo + tile;          // exclusive
        if (mc.in_viewport && inner_w <= face_h &&
            x_lo >= vis_lo + clear && x_hi <= vis_hi - clear) {
            // The tile's own column run, bevel included — the endcap surface at
            // the midpoint, which is what rows 0..8 of the crop are.
            surface(lane_x + x_lo, tile, kTrimLaneEndcap,
                    kTrimCapBevelHi, kTrimCapBevelLo);
            // The inner square, at the crop's own offsets. It hangs FLUSH
            // UNDER the bevel since the 2026-09-16 flip — crop rows 2..6 of a
            // 2..8 face, immediately below the bevel pair — which is the
            // relationship that scales with the lane, and it insets from the
            // tile's left by the crop's 2px.
            //
            // THE BOTTOM RIM IS CLAMPED INTO EXISTENCE (codex round 1,
            // 2026-08-10, with the gui_scale floor 100->50; the rim this
            // clamp protects moved from the top to the bottom with the
            // 2026-09-16 flip — the reasoning and the arithmetic are
            // untouched, only which edge of the face the square hangs from).
            // The bottom rim is the one length here that is NOT handed over
            // by the partition — the square hangs flush under the bevel, so
            // the rim is whatever face_h - inner_h leaves, and face_h is the
            // LANE's arithmetic while inner_h is the TILE's. Nothing holds
            // the two apart: wherever the derived width reaches face_h the
            // difference is 0, the square runs to the face's own bottom row,
            // and the endcap-coloured rim of the ruled silhouette vanishes
            // with no metric having gone to zero.
            //
            // SO THE HEIGHT GIVES WAY AND THE RIM DOES NOT: inner_h caps the
            // square's height at face_h - 1, keeping one face row below it —
            // the accepted trade where it binds, the rim being the
            // load-bearing silhouette feature where the squareness is not.
            // THE WIDTH IS UNTOUCHED BY
            // THE CLAMP: it is the partition's own remainder above, so the two
            // side rims stay exactly `inset` even where the height gives way,
            // and the square still hangs FLUSH UNDER THE BEVEL.
            //
            // A FLOOR, NOT A RESHAPE: at 100% and above the clamp never binds
            // (5 against face_h 7 at 100%, 10 against 14 at 200%, 20 against 28
            // at 400% — zero binding scales in [100, 400]), so every pixel there
            // is what it was. (With
            // kTrimBarScalePercent at the one-commit 150, 2026-08-12, the taller
            // face made this a pure backstop at every legal scale; the factor's
            // return to 100 restored the 50..74 binding band the clamp was
            // written for.) The
            // degenerate arm below face_h <= 1 is unreachable in [50, 350] and
            // skips THE SQUARE ALONE — never the tile, whose own paint-or-not
            // verdict is the fit test above and is unchanged.
            // THE HEIGHT RIDES THE DERIVED WIDTH and keeps its own clamp, which
            // is a MEASURED choice rather than a preference: the alternative
            // spelling — deriving the height as face_h - inset, the vertical
            // mirror of the width's derivation — produces the IDENTICAL value
            // at all 351 legal scales, and this one needs no extra guard (a min
            // is bounded where a subtraction is not). Either way the bottom rim
            // comes out exactly `inset` at every scale in [50, 350], so the
            // crop's vertical relationship is now a consequence of the
            // partition rather than a coincidence of two roundings.
            const int inner_h = inner_w < face_h - 1 ? inner_w : face_h - 1;
            if (inner_h > 0) {
                cairo_set_source_rgb(cr, kTrimLaneBar.r, kTrimLaneBar.g,
                                     kTrimLaneBar.b);
                cairo_rectangle(cr, lane_x + x_lo + inset,
                                lane_y + bevel_h, inner_w, inner_h);
                cairo_fill(cr);
            }
        }
    }

    // THE SHARED BOTTOM BORDER (architect 2026-09-16, the flip's new crop
    // row_5_lane_1_trim_bottomborder.png): ONE fill across the WHOLE lane
    // width, painted LAST so it sits over every surface above it — the
    // ground, the bar, the endcaps and the midpoint tile alike, none of which
    // owns this row on its own. The clip at the top of this function already
    // bounds it to the lane, so the rectangle below can run the full width
    // with no further clamping.
    if (border_h > 0) {
        cairo_set_source_rgb(cr, kTrimLaneBottomBorder.r,
                             kTrimLaneBottomBorder.g, kTrimLaneBottomBorder.b);
        cairo_rectangle(cr, lane_x, lane_y + body_h, lane_w, border_h);
        cairo_fill(cr);
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
    // The LEFT cull's width bound in pixels — how far left of the viewport a box
    // may open and still reach into it. A caller-supplied number rather than a
    // derivation here because the callers do not share one width family: the
    // marker columns pass marker_flag_max_width_px(iteration_on), a constant
    // bound the display composers' own grammars guarantee, while the history mode's
    // diff lane does not truncate at all and derives its bound from the commit's
    // own longest label. A bound over-admits a handful of offscreen items per
    // frame and never drops a visible one, so the only requirement is that it
    // not UNDER-state.
    double cull_width_px,
    Emit&& emit) {
    const double span = static_cast<double>(viewport_end_sample -
                                            viewport_start_sample);
    // Map columns against the EFFECTIVE waveform width, not the strip's own
    // full width, so a flag shares the marker stem's samples-per-pixel and
    // stays column-aligned with it at every window width (the two widths
    // always differ, by the permanent right gutter, waveform_area).
    const double samples_per_pixel =
        span / static_cast<double>(waveform_width);
    if (samples_per_pixel <= 0.0) return;

    // THE CULL IS ASYMMETRIC BECAUSE THE BOX IS. A flag opens at its column and
    // runs RIGHTWARD, so a marker to the LEFT of the viewport can still reach
    // into it (by up to a full box width) while a marker PAST the right edge
    // can show nothing at all. The left margin is a width BOUND rather than
    // the real width, which is not known until the label is shaped; the caller
    // supplies it (see cull_width_px above).
    //
    // THE RIGHT BOUND IS THE ROUNDED COLUMN, AND GRID POINT w PAINTS
    // (architect 2026-09-26, the permanent right gutter — waveform_area,
    // main.cpp): the waveform has one more paintable grid point than it has
    // columns, so a marker whose column rounds to waveform_width — the song's
    // last half-column at the right wall, or any frame within half a column
    // past the displayed end — paints its flag from the gutter's first column
    // (running off the window if it must), publishes its hit rect there
    // exactly as painted, and publishes its stem there, the same column the
    // playhead at that frame paints on. A marker whose column rounds PAST
    // waveform_width is culled: it has no grid point on screen.
    const double cull_lo = static_cast<double>(viewport_start_sample) -
                           cull_width_px * samples_per_pixel;
    for (size_t i = 0; i < markers.size(); ++i) {
        const auto& m = markers[i];
        const double eff_time = drag_overlay
            ? drag_overlay->effective_time(
                  static_cast<int>(i), m.time_frame)
            : m.time_frame;
        const double ms =
            frame_to_paint_sample(eff_time, warp_frame_map);
        if (ms < cull_lo) continue;

        const double x_raw =
            (ms - static_cast<double>(viewport_start_sample)) /
            samples_per_pixel;
        const double col = std::nearbyint(x_raw);
        if (col > static_cast<double>(waveform_width)) continue;
        const double left_x = static_cast<double>(top_strip_area.x) + col;

        emit(static_cast<int>(i), left_x);
    }
}

// (THE SHARED LABEL CAP IS GONE, 2026-09-19. cap_marker_label kept the first
// nine bytes of whatever a column composed and appended the truncation
// marker; the WARP payload was its only real subject — the phase-reset token
// is five bytes — and once a tempo could
// carry a chain, cutting by a byte count would have eaten the very terms the
// feature exists to show. The cut now lives inside the one composer that
// knows what it is cutting, flag_display_text above, and the phase-reset
// column's label reaches the pass whole.)

// The two bound cells an eligible flag paints while iteration mode is on, or
// nothing. The lambda form each column hands render_flag_boxes_impl answers
// this — the warp column through warp_iter_cells and the phase-reset column
// through phase_iter_cells (2026-09-09, when the mode grew its second
// column) — and the two differ in their COMPOSER alone, cents against hops.
struct IterCellText {
    bool        present = false;
    // IS THIS MARKER A TIE FOLLOWER (architect 2026-09-19) — one of several
    // markers the sweep treats as ONE AXIS, and not the tie's leader. Its two
    // cells show THE LEADER'S bracket (the strings below are composed off the
    // governing marker, so tied members cannot disagree) and paint GREYED,
    // wearing the palette's DISABLED blend because the cell is not this
    // marker's to author. It is an ordinary content input like the strings and
    // reaches the painter the same way; the flag cache needs no field for it,
    // the tie living in the store whose GENERATION the fingerprint already
    // carries.
    bool        follower = false;
    std::string lower;
    std::string upper;
};

// The cells of warp marker `i` under `iteration_on` — the ONE spelling of
// "which flags paint cells" (iter_popup_eligible_marker, the sweep's own
// eligibility) and of what they say (format_iter_bound_cell), shared by the
// flag pass and by the bound editor's anchor (committed_cell_seam_off), so the
// resting box and the field that opens past it cannot disagree about where
// the cells end.
//
// `iteration_on && iter_popup_eligible_marker` IS marker_paints_iter_cells
// (app_state.h) spelled across this painter's parameter boundary: the Tab
// walk asks that predicate whether a marker has bound cells to stop on, and
// the two readings compose the same two owners — the caller's one
// iteration_column_lit read (waveform_cache.cpp, over the column this pass
// paints) and the eligibility predicate below. This body cannot call it
// because the flag painter takes no AppState at all, every content input
// arriving as an explicit argument that the flag cache fingerprints.
static IterCellText warp_iter_cells(const std::vector<GuiWarpMarker>& markers,
                                    int i, bool iteration_on) {
    IterCellText c;
    if (!iteration_on || !iter_popup_eligible_marker(markers, i)) return c;
    // THE BRACKET IS THE GOVERNING MARKER'S (iter_bracket_governor,
    // warpmarkers.h): its own for an untied marker or a tie's leader, THE
    // LEADER'S for a follower — one owner for the walk, so the cells and
    // every act that reads a bound name the same bracket.
    const GuiWarpMarker& m = iter_bracket_governor(markers, i);
    c.present  = true;
    c.follower = marker_is_tie_follower(markers, i);
    c.lower    = format_iter_bound_cell(m, MarkerCell::Lower);
    c.upper    = format_iter_bound_cell(m, MarkerCell::Upper);
    return c;
}

// The cells of phase reset `i` under `iteration_on` — warp_iter_cells' twin
// (2026-09-09), off this column's own eligibility
// (phase_reset_iter_eligible_marker, phaseresetmarkers.h: every reset is a
// carrier, so the verdict is the disabled bit) and its own composer
// (format_phase_iter_bound_cell — the signed integer hop, `+0` for a blank
// bracket). Shared by the flag pass and the bound field's anchor
// (committed_cell_seam_off)
// for the same reason its twin is: the resting boxes and the field that opens
// past them cannot disagree about where the cells end. Its composition is
// marker_paints_iter_cells' on this column, and the note at its twin above
// covers why the painter spells it rather than calling it.
static IterCellText phase_iter_cells(
    const std::vector<GuiPhaseResetMarker>& phase_resets, int i,
    bool iteration_on) {
    IterCellText c;
    if (!iteration_on || !phase_reset_iter_eligible_marker(phase_resets, i))
        return c;
    // The governing reset's hops, the warp body's own rule on this column
    // (phase_iter_bracket_governor, phaseresetmarkers.h).
    const GuiPhaseResetMarker& m = phase_iter_bracket_governor(phase_resets, i);
    c.present  = true;
    c.follower = marker_is_tie_follower(phase_resets, i);
    c.lower    = format_phase_iter_bound_cell(m, MarkerCell::Lower);
    c.upper    = format_phase_iter_bound_cell(m, MarkerCell::Upper);
    return c;
}

// The two cells LAID OUT on `font`: each one's shaped run, each one's FILL
// width (the flag's own two pads plus the token) and the whole run's painted
// extent — both seam columns and both fills — or all zeroes where no cells
// paint. THE ONE MEASURER. The flag pass, the bound field's anchor
// (committed_cell_seam_off) and the editor's RE-PAINT of whatever rides its
// unrolled right edge all lay them out through this one body, off the one
// composer and the one font, so no two of them can disagree about where a
// cell begins or how wide it is.
struct IterCellLayout {
    bool                  present = false;
    text_shape::ShapedRun lower_run;
    text_shape::ShapedRun upper_run;
    int                   lower_w = 0;   // fill width: two pads + the token
    int                   upper_w = 0;
    int                   span_w  = 0;   // both seams + both fills, 0 with none
};

static IterCellLayout measure_iter_cells(cairo_scaled_font_t* font,
                                         const IterCellText& cells) {
    IterCellLayout l;
    if (!cells.present) return l;
    const int pads = marker_flag_pad_left_px() + marker_flag_pad_right_px();
    l.present   = true;
    l.lower_run = text_shape::shape_text_run(font, cells.lower);
    l.upper_run = text_shape::shape_text_run(font, cells.upper);
    l.lower_w   = pads + static_cast<int>(std::nearbyint(l.lower_run.width_px));
    l.upper_w   = pads + static_cast<int>(std::nearbyint(l.upper_run.width_px));
    l.span_w    = 2 * marker_flag_border_px() + l.lower_w + l.upper_w;
    return l;
}

// The resolved paint of ONE marker flag: the three surfaces plus the stem.
struct FlagFace {
    GuiColor fill;
    GuiColor edge;
    GuiColor border;
    GuiColor label;
    GuiColor stem;
    bool     has_stem;
};

// THE COLOR-CLASS LADDER, one owner for both marker columns (the full
// statement is at render_flags' declaration): disabled wins outright, then
// red, then the
// default pair with selection swapping it for the bright one — and the DISABLED
// arm runs that same red-then-selection ladder INSIDE ITSELF to pick the pair it
// blends, so selection lifts a disabled marker exactly as it lifts a live one
// (architect 2026-08-01). RED IS ONE OF THE PAIRS since 2026-09-16 (architect):
// it has a rest pair and a bright one and takes the lift on both sides like
// every other class, the ladder's ORDER being what keeps the cue — a red
// marker is red at either brightness.
//
// THE DISABLED FACE'S LABEL DIMS AGAINST THE FLAG, NOT AGAINST THE LANE. Every
// SHAPE surface takes its fraction of itself over the lane ground, as ruled.
// The LABEL takes a fraction of itself through the same mix_color owner but
// toward the surface it actually sits on — the already-blended fill, whichever
// pair produced it, so a selected disabled marker's label dims against ITS OWN
// brighter flag — because that is what
// the redesign's disabled-label rule says ("a fraction of itself over the row's
// CURRENT ground", render.h) and the label's ground here is the flag, not the
// lane. THE MECHANISM IS UNTOUCHED BY THE 2026-08-20 BLACK RULING; what changed
// twice that day is the ENDPOINT and then the FRACTION. The ink entering the
// blend is kMarkerFlagLabel (#000000) rather than the old #fcfcfc, so a dimmed
// label resolves DARKER than its dimmed flag where it used to resolve lighter —
// and the surfaces' 25% was calibrated to hold a near-WHITE label back, which
// is the opposite correction, so at that fraction the black label sank almost
// into the flag (~#2f2438, ~1.21:1). The LABEL NOW TAKES ITS OWN FRACTION,
// kMarkerDisabledLabelMix, while the shapes keep kMarkerDisabledMix untouched;
// both constants and the ceiling arithmetic behind the new value live at their
// declarations. What the argument for blending toward the FLAG rather than the
// LANE buys is unchanged by any of it: toward the lane the label would ignore
// which pair produced the flag it sits on. Neither is a fade — both resolve to
// an opaque color before cairo sees them, which is the point of the no-alpha
// rule when flags overlap.
// WHICH COLUMN'S DEFAULT/SELECTED PAIR THIS FACE WEARS (architect 2026-09-15,
// retold the same day on the naming-symmetry ruling: warp is never the
// unmarked default, so this is a REQUIRED argument at every call, never a
// defaulted bool). The phase-reset flag box paints in the column's BLUE —
// Breeze's highlight #3daee9 sampled, the other three RECORDED DERIVATIONS
// off it (kPhaseResetFlagFill/Edge/FillSel/EdgeSel, render.h); the warp flag
// box and the warp column's bound cells stay on kMarkerFlagFill's purple,
// and THE PHASE-RESET COLUMN'S BOUND CELLS WEAR ITS BLUE (architect
// 2026-09-21, superseding the 2026-09-15 purple-on-either-column choice: the
// cells wear their own column's hue) — every bound-cell call site passes the
// face of the column the cells belong to, the same `column_face` its flag box
// takes. (A third face, the magnification level markers column's orange,
// stood from 2026-09-15 until that column's deletion, architect 2026-09-23.)
enum class FlagColumnFace { Warp, PhaseReset };

// The default and selected pair of one column's flag box — the one place the
// two columns' palettes are selected, so the live arm and the disabled arm
// below cannot pick differently.
static void flag_column_pair(FlagColumnFace column_face, bool selected,
                             GuiColor& fill, GuiColor& edge) {
    switch (column_face) {
        case FlagColumnFace::Warp:
            fill = selected ? kMarkerFlagFillSel : kMarkerFlagFill;
            edge = selected ? kMarkerFlagEdgeSel : kMarkerFlagEdge;
            return;
        case FlagColumnFace::PhaseReset:
            fill = selected ? kPhaseResetFlagFillSel : kPhaseResetFlagFill;
            edge = selected ? kPhaseResetFlagEdgeSel : kPhaseResetFlagEdge;
            return;
    }
    fill = kMarkerFlagFill;
    edge = kMarkerFlagEdge;
}

FlagFace resolve_flag_face(bool disabled, bool red, bool selected,
                           FlagColumnFace column_face) {
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
        // RED TAKES THE LIFT TOO since 2026-09-16 (architect), mirroring the
        // live red class, which gained a rest pair and a selected pair that
        // day: the cue is the HUE, which the swap never touches, so a selected
        // disabled red marker is the disabled rendition of the BRIGHT red and
        // reads red and switched off at once. The pair is chosen on the SAME
        // `selected` bit the column pair below reads — one question, four
        // classes.
        GuiColor base_fill;
        GuiColor base_edge;
        if (red) {
            base_fill = selected ? kMarkerFlagFillRedSel : kMarkerFlagFillRed;
            base_edge = selected ? kMarkerFlagEdgeRedSel : kMarkerFlagEdgeRed;
        } else {
            flag_column_pair(column_face, selected, base_fill, base_edge);
        }
        f.fill  = mix_color(base_fill, kRedesignContentGround,
                            kMarkerDisabledMix);
        f.edge  = mix_color(base_edge, kRedesignContentGround,
                            kMarkerDisabledMix);
        // THE BORDER DIMS WITH THE REST (architect 2026-08-02, overturning the
        // structural-edge reading it shipped with the same day): same mix owner,
        // same kMarkerDisabledMix fraction, same base — the marker lane's own
        // ground — so it is the identical operation the two lines above take,
        // applied to the one border colour. It has NO per-class variant to pick,
        // which is the whole difference from fill and edge: the ladder above
        // chooses WHICH pair to damp, and there is only ever one border to damp.
        //
        // "DIMS" HERE MEANS DAMPED TOWARD THE GROUND, NOT DARKENED. The border
        // is DARKER than the lane ground (#131516 against #202326), so 25% of
        // itself over that ground moves it UP to ~#1d1f22 — it loses contrast
        // with the lane exactly as the fill loses contrast with it, which is the
        // property the disabled face is after. A reader expecting "dimmer =
        // darker" would mis-read the direction and try to fix it.
        f.border = mix_color(kMarkerFlagBorder, kRedesignContentGround,
                             kMarkerDisabledMix);
        f.label = mix_color(kMarkerFlagLabel, f.fill, kMarkerDisabledLabelMix);
        f.stem  = f.fill;
        f.has_stem = false;      // NO STEM EVER for a disabled marker
        return f;
    }
    if (red) {
        // THE REST PAIR AT REST, THE BRIGHT PAIR SELECTED (architect
        // 2026-09-16): red joins the shape the three column pairs already
        // have, read on this same `selected` bit — which, at the flag pass's
        // own rule, is true for the marker's ADDRESSED CELL alone. The class
        // ladder above is untouched, so the cue is never masked: a selected
        // red marker is still red, only brighter.
        f.fill  = selected ? kMarkerFlagFillRedSel : kMarkerFlagFillRed;
        f.edge  = selected ? kMarkerFlagEdgeRedSel : kMarkerFlagEdgeRed;
        // FULL-STRENGTH BORDER on every LIVE class, red and selected included,
        // and that is the precise mirror of what fill and edge do rather than a
        // second rule: the live arms damp nothing, so the border they take is
        // its own colour. Only the disabled arm blends, on all three surfaces at
        // once. The border is still class-INVARIANT across the live ladder — it
        // varies on the disabled axis alone.
        f.border = kMarkerFlagBorder;
        f.label = kMarkerFlagLabel;
        // THE STEM FOLLOWS THE SELECTION BIT AS THE FILL DOES (architect
        // 2026-09-23): the bright fill when selected, the class's own REST
        // stem kMarkerStemRed otherwise.
        f.stem  = selected ? kMarkerFlagFillRedSel : kMarkerStemRed;
        f.has_stem = true;
        return f;
    }
    flag_column_pair(column_face, selected, f.fill, f.edge);
    f.border = kMarkerFlagBorder;   // live: undamped, like the red arm above
    f.label = kMarkerFlagLabel;
    // THE STEM WEARS THE FILL, SELECTION INCLUDED (architect 2026-09-23: "make
    // the stems the same colour as the highlighted flag when a flag is
    // selected, so that it stands out" — at a coarse zoom among many markers,
    // the playhead is found by looking up and the selected stems by looking
    // down). The column's selected pair's fill when selected, its calm fill at
    // rest, on both columns.
    f.stem = f.fill;
    f.has_stem = true;
    return f;
}

} // namespace

// The phase-reset lead-in ring's colour (declaration in render.h): the ladder
// above asked for a LIVE reset's stem on the same class and selection bits the
// flag pass hands it, so the ring can never pick a colour its stem would not —
// the rest colour at rest, the bright fill when selected (architect
// 2026-09-23: the ring and the stem are one object and brighten together). It
// stands outside the file's anonymous namespace so paint_handler.cpp reaches
// it; the ladder it calls stays file-local.
GuiColor phase_reset_stem_color(bool red, bool selected) {
    return resolve_flag_face(/*disabled=*/false, red, selected,
                             FlagColumnFace::PhaseReset).stem;
}

namespace {

// THE MARKER'S BOXES IN PAINTED ORDER, RANKED: the flag box, then the lower
// bound cell, the upper bound cell. That is the one
// left-to-right order this pass paints in, the editor's riding run re-paints
// in, and FlagHitRect's two boundaries collapse along — and ranking it is
// what lets ONE COMPARISON express the suppression for both editor kinds
// (SuppressedBox, render.h): a box belongs to this pass iff it stands LEFT of
// the edited one. Past the last rank sits kFlagBoxRankNone, the answer for
// every marker no editor stands on.
static int flag_box_rank(MarkerCell c) {
    switch (c) {
        case MarkerCell::Payload: return 0;
        case MarkerCell::Lower:   return 1;
        case MarkerCell::Upper:   return 2;
    }
    return 0;
}
static constexpr int kFlagBoxRankNone = 3;

// ONE BOUND CELL PAINTED — the flag CONTINUED rightward: the seam column
// standing OUTSIDE the fill on its left, the fill, its 1px top edge over that
// fill, then the token on the flag's own left pad. Aliased throughout, like
// every box in this lane.
//
// `closes` ADDS THE RUN'S CLOSING COLUMN (architect 2026-09-25): one
// `face.border` column just past the fill, painted iff this cell is the LAST
// box of its marker's run, so a run ends on a border as it begins on one and
// every interior seam stays the next box's own single left column.
//
// IT HAS TWO CALLERS AND THAT IS THE POINT: the cached flag pass paints the
// resting cells with it, and the payload editor's own painter re-paints them
// with it at the unrolled field's right edge (render_flag_editor_box), so a
// cell cannot read one way at rest and another under the editor. The FACE is
// the caller's — each cell resolves its own through the class ladder, the
// selected pair belonging to the addressed cell alone — and the seam rides
// `face.border`, which the ladder damps with the rest of a disabled marker.
static void paint_iter_bound_cell(cairo_t* cr, const GuiRect& lane, int seam_x,
                                  int fill_w, int border_w, int edge_h,
                                  int pad_l, double baseline,
                                  const text_shape::ShapedRun& run,
                                  const FlagFace& face, bool closes) {
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgb(cr, face.border.r, face.border.g, face.border.b);
    cairo_rectangle(cr, seam_x, lane.y, border_w, lane.h);
    cairo_fill(cr);
    if (closes) {
        cairo_rectangle(cr, seam_x + border_w + fill_w, lane.y, border_w,
                        lane.h);
        cairo_fill(cr);
    }
    cairo_set_source_rgb(cr, face.fill.r, face.fill.g, face.fill.b);
    cairo_rectangle(cr, seam_x + border_w, lane.y, fill_w, lane.h);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, face.edge.r, face.edge.g, face.edge.b);
    cairo_rectangle(cr, seam_x + border_w, lane.y, fill_w, edge_h);
    cairo_fill(cr);
    cairo_restore(cr);
    cairo_set_source_rgb(cr, face.label.r, face.label.g, face.label.b);
    text_shape::show_shaped_run(
        cr, run, static_cast<double>(seam_x + border_w + pad_l), baseline);
}

// The one body both columns' painters call. `label_of(i)` composes the
// marker's display text and `disabled_of(i)` answers its column's disabled
// question (the warp side's label_ref cascade, the phase-reset side's bare
// bool); the lambdas survive because the
// columns hold different marker types.
template <typename MarkerVec, typename LabelFn, typename DisabledFn,
          typename CellsFn>
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
    // The two iteration bound cells marker i paints, or none (IterCellText):
    // the warp column answers through warp_iter_cells and the phase-reset
    // column through phase_iter_cells, each off its own eligibility and its
    // own composer.
    CellsFn&& cells_of,
    std::vector<FlagHitRect>* out_hit_rects,
    std::vector<MarkerStem>* out_stems,
    const std::vector<WarpFrameMapSegment>* warp_frame_map,
    const DragOverlay* drag_overlay,
    // THE ONE BOX THIS PASS DOES NOT PAINT (SuppressedBox, render.h): the
    // marker whose marker-lane editor stands, and WHICH of its boxes that
    // editor is standing in for. It replaced two separate indices the
    // day the bound field stopped being the odd one out: the rule is now ONE
    // COMPARISON against flag_box_rank, so the editor kinds are cases of one
    // model rather than separate arms. At most one box is ever
    // suppressed, the marker-lane editors being one text_editor::State.
    SuppressedBox suppressed,
    // Reaches the LEFT CULL only — it widens the width bound by the two bound
    // cells. Which flags paint cells is the cells lambda's business, so this
    // body never asks whether a given marker is eligible.
    bool iteration_on,
    // The focus and its addressed cell (render_flags' declaration): the one
    // selected marker whose bright cell is not the payload, or -1.
    int focus_marker,
    MarkerCell focus_cell,
    // WHICH COLUMN'S FLAG BOX THIS IS, REQUIRED rather than defaulted (the
    // naming-symmetry ruling: warp is never the unmarked default) — render_flags
    // passes `FlagColumnFace::Warp`, render_phase_reset_flags passes
    // `FlagColumnFace::PhaseReset` (the phase-reset blue,
    // kPhaseResetFlagFill/Edge/FillSel/EdgeSel, render.h). It reaches the
    // resting flag-box face below AND THE TWO BOUND CELLS (architect
    // 2026-09-21: the cells wear their own column's hue — purple on W, blue
    // on P).
    FlagColumnFace column_face) {
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
    // for the whole loop. What the family resolves to is the backend's, through
    // the one face owner (gui_font.h).
    gui_select_font_face(cr, GuiFontFamily::Sans);
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
                               // `iteration_on` widens the bound by the iter
                               // bracket's own glyphs, which the payload's
                               // own worst case does not cover; the reasoning
                               // is at the bound.
                               marker_flag_max_width_px(iteration_on),
        [&](int i, double left_x) {
            // THE LABEL LAMBDA COMPOSES THE PAINTED FORM ITSELF — each
            // column's own, and the cut (where there is one) is inside it.
            const std::string text = label_of(i);
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, text);

            const int bx = static_cast<int>(std::nearbyint(left_x));
            const int bw = pad_l + pad_r +
                static_cast<int>(std::nearbyint(run.width_px));

            // WHICH OF THIS MARKER'S BOXES THIS PASS PAINTS. Everything LEFT
            // of the edited box stands at rest — nothing moves on that side of
            // an open field — and nothing from the edited box rightward is
            // drawn here, because those boxes RIDE THE FIELD'S RIGHT EDGE and
            // are painted, and published, by render_flag_editor_box instead
            // (the one model, SuppressedBox). A marker no editor stands on
            // ranks past every box and paints its whole run.
            const int suppressed_rank =
                i == suppressed.marker_index ? flag_box_rank(suppressed.cell)
                                             : kFlagBoxRankNone;
            const auto pass_paints = [&](MarkerCell c) {
                return flag_box_rank(c) < suppressed_rank;
            };

            // THE TWO BOUND CELLS (architect 2026-09-04), when the marker
            // paints them and no field is standing in for them. Each cell is a
            // seam column plus a fill of two pads and the shaped token, and
            // `cells_span_w` — the extent of what is actually PAINTED here —
            // is what the hit rect sits past, 0 with no
            // cells so it needs no arm of its own. The cells go one at a
            // time now rather than as a pair: under the UPPER bound's field the
            // lower cell stands and the upper yields, which is the whole point
            // of ranking the boxes.
            const IterCellText cells = cells_of(i);
            const bool paint_lower =
                cells.present && pass_paints(MarkerCell::Lower);
            const bool paint_upper =
                cells.present && pass_paints(MarkerCell::Upper);
            // Nothing is shaped for a marker whose cells do not paint: the one
            // measurer takes empty cells and answers all zeroes. Under the
            // UPPER field it shapes one token it will not paint — the measurer
            // lays the pair out together, which is exactly what makes it ONE
            // measurer, and the cost is a single five-byte run on a single
            // marker for as long as that field stands.
            const IterCellLayout cl =
                measure_iter_cells(font, paint_lower ? cells : IterCellText{});
            const int lower_w = paint_lower ? cl.lower_w : 0;
            const int upper_w = paint_upper ? cl.upper_w : 0;
            const int cells_span_w = (paint_lower ? border_w + lower_w : 0) +
                                     (paint_upper ? border_w + upper_w : 0);
            // THE RUN'S CLOSING COLUMN (architect 2026-09-25): one border
            // column past the run's LAST box, so a short later flag's tail
            // never blends into a long earlier one's. This pass paints it iff
            // it paints the run's last box — no field stands on this marker —
            // because under an open field the run's end is the editor's to
            // paint (the field itself or its riding boxes), and a closing
            // column here would double the field's own left seam into two.
            const bool pass_closes = suppressed_rank == kFlagBoxRankNone;
            const int  close_w     = pass_closes ? border_w : 0;
            // The lower cell's seam column and the upper cell's seam column —
            // where each cell paints, and the two boundaries the hit rect
            // publishes. An ABSENT box's boundary collapses onto the RECT'S
            // RIGHT EDGE (`run_end`, the closing column included), whether it
            // is absent at rest or standing in an open field: absent boxes are
            // always the run's tail, so this is "onto the next" spelled once,
            // and the closing column reads as the last box it closes.
            const int lower_x   = bx + bw;
            const int upper_x   = paint_lower ? lower_x + border_w + lower_w
                                              : lower_x;
            const int run_end   = bx + bw + cells_span_w + close_w;

            // RED IS COMPUTED INDEPENDENTLY OF DISABLED, unlike the old
            // three-pair ladder where `red` tested `!dis` because disabled had
            // its own opaque PAIR and could not show a hue underneath. Disabled
            // is a BLEND of the marker's own class now, so "which class" is a
            // real question and the answer is the one it belongs to: a disabled
            // red marker blends the red class's own pair — the rest one or,
            // on a selected marker's addressed cell, the bright one — and
            // stays recognisably red.
            // Disabled still WINS — it decides the blend and the missing stem —
            // it just no longer erases the hue.
            const bool dis = disabled_of(i);
            const bool red = red_set.count(i) > 0;
            const bool sel = selected_set.count(i) > 0;
            // THE SELECTED PAIR IS ONE CELL'S (architect 2026-09-05, "light
            // the colour of only the flag that's clicked"): a selected marker
            // paints its ADDRESSED cell in the selected pair and its other
            // cells in its ordinary class pair. The addressed cell is the
            // payload for every selected marker but the focus, whose
            // addressed cell is the axis — and where the axis names a cell
            // this marker does not paint (a bound cell on an owner disabled
            // after its press), the payload is
            // bright, so a selected marker always shows its selection
            // somewhere. Disabled and red blend as they always did, cell by
            // cell through the same ladders; the border reads the class alone
            // and the stem the flag box's fill, so the payload face carries
            // both for the marker (a marker whose addressed cell is a bound
            // cell keeps its rest stem, as its flag box keeps its rest fill).
            MarkerCell bright = i == focus_marker ? focus_cell
                                                  : MarkerCell::Payload;
            // THE FALLBACK ASKS WHETHER THE BRIGHT CELL IS SHOWN AT ALL, by
            // this pass OR by the open field standing in for it — never merely
            // whether THIS pass paints it. A field paints the box it edits and
            // asks this very question of its own cell
            // (render_flag_editor_box), so the marker's selection shows there;
            // taking the brightness onto the flag box as well would light two
            // boxes at once, and would light the flag under every bound
            // field — exactly the odd-one-out the one graphic model
            // retired. What is left for the fallback is a cell that does not
            // exist anywhere: a bound cell on an owner disabled after its
            // press. Then the payload is bright, so a
            // selected marker always shows its selection, and shows it once.
            const bool bright_cell_shown =
                bright == MarkerCell::Payload ||
                (i == suppressed.marker_index && bright == suppressed.cell) ||
                cells.present;
            if (!bright_cell_shown) bright = MarkerCell::Payload;
            const auto cell_selected = [&](MarkerCell c) {
                return sel && c == bright;
            };
            const FlagFace face =
                resolve_flag_face(dis, red, cell_selected(MarkerCell::Payload),
                                  column_face);

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
            // and exactly the committed run, to the column, where it is not)
            // and false the moment the user replaces that auto-selected text
            // with something shorter. The overlay then
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
            // Suppressing the box takes ITS HIT RECT WITH IT (the argument is at
            // the publish below). THE STEM IS WHAT SURVIVES: it still paints and
            // still publishes, anchored at the flag's left column, and the editor
            // unrolls from that same column — so the marker keeps its stem for
            // the whole session exactly as it does when idle, and keeps the stem
            // click that goes with it.
            //
            // ONLY THE PAYLOAD FIELD REACHES THIS: the flag box is the marker's
            // LEFTMOST box, so it is suppressed by the payload editor alone,
            // and under a bound field it paints exactly as it does
            // at rest.
            if (pass_paints(MarkerCell::Payload)) {
                // Border, then box, then top edge — all AA-off so the 1px band
                // is exactly one row and the box's columns are exactly one
                // column each.
                //
                // THE BORDER IS OUTSIDE THE FILL, one column LEFT of the frame
                // column (the geometry clause and the clip-at-the-left-edge
                // answer are at marker_flag_border_px, render.h). Its colour
                // comes off the resolved FACE like the fill's and the edge's —
                // kMarkerFlagBorder on every live class, damped by the one
                // disabled blend when the marker is disabled. It is drawn first
                // only for reading order: the two rectangles are disjoint.
                //
                // OVERLAP READS ONE COLUMN EARLIER NOW. A later marker's box
                // covers an earlier one's tail from its BORDER, so two flags a
                // box-width apart butt up as border-against-fill instead of
                // fill-against-fill — which is the whole point of a border in
                // this lane and is why later-over-earlier stays the entire
                // occlusion model. AND THE RUN CLOSES ON ONE TOO (architect
                // 2026-09-25): a short later flag standing over a long earlier
                // one ends on its closing column, so the earlier tail emerging
                // past it is ruled off rather than blending fill into fill.
                cairo_save(cr);
                cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
                cairo_set_source_rgb(cr, face.border.r, face.border.g,
                                     face.border.b);
                cairo_rectangle(cr, bx - border_w, lane.y, border_w, lane.h);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, face.fill.r, face.fill.g, face.fill.b);
                cairo_rectangle(cr, bx, lane.y, bw, lane.h);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, face.edge.r, face.edge.g, face.edge.b);
                cairo_rectangle(cr, bx, lane.y, bw, edge_h);
                cairo_fill(cr);
                // THE CLOSING COLUMN ON A CELL-LESS RUN: the flag box is the
                // run's last box, so it closes on its own face's border. With
                // cells the upper cell closes instead (paint_iter_bound_cell's
                // `closes`), and the flag's right side is the lower cell's
                // one-pixel seam.
                if (pass_closes && !paint_lower) {
                    cairo_set_source_rgb(cr, face.border.r, face.border.g,
                                         face.border.b);
                    cairo_rectangle(cr, bx + bw, lane.y, close_w, lane.h);
                    cairo_fill(cr);
                }
                cairo_restore(cr);

                // The label, on the run just measured — same font, same glyphs,
                // so the box width and the painted text cannot disagree.
                cairo_set_source_rgb(cr, face.label.r, face.label.g,
                                     face.label.b);
                text_shape::show_shaped_run(
                    cr, run, static_cast<double>(bx + pad_l), baseline);
            }

            // THE BOUND CELLS: the flag CONTINUED rightward, twice, in the
            // flag's OWN class — fill, top edge, seam column and ink all off
            // the same ladder, so a cell reads as another payload of the same
            // flag and not as a second surface (a bound cell wears
            // its own column's hue — purple on the warp column, the phase-reset
            // blue on the phase-reset column, architect 2026-09-21). Each cell resolves
            // its own face, because the selected pair is
            // the addressed cell's alone (above). The seam is the flag's own
            // left-border column laid on each cell's left edge. No budget and
            // no truncation: the token is
            // fixed-width by grammar (kIterCellGlyphs). The lower cell first,
            // then the upper — the bracket's own order. The ANATOMY is the one
            // cell painter's (paint_iter_bound_cell), which the payload
            // editor's re-paint calls with the same faces.
            if (paint_lower) {
                const auto paint_cell = [&](int seam_x, int fill_w,
                                            const text_shape::ShapedRun& crun,
                                            MarkerCell which, bool closes) {
                    paint_iter_bound_cell(
                        cr, lane, seam_x, fill_w, border_w, edge_h, pad_l,
                        baseline, crun,
                        // THE CELLS WEAR THEIR OWN COLUMN'S HUE (architect
                        // 2026-09-21, superseding the 2026-09-15 purple on
                        // either column): the same `column_face` this pass's
                        // flag box takes — purple on W, blue on P.
                        //
                        // A TIE FOLLOWER'S CELLS TAKE THE DISABLED FACE
                        // (architect 2026-09-19): they show the LEADER's
                        // numbers and are not this marker's to author, which
                        // is exactly what the palette's disabled blend
                        // already says — the class's own pair damped toward
                        // the lane ground at kMarkerDisabledMix, its ink at
                        // kMarkerDisabledLabelMix, no new colour and no
                        // second rule. The marker's own `dis` is false
                        // wherever `follower` is true (a disabled marker is
                        // no tie member and paints no cells at all), so the
                        // OR cannot double-damp; the FLAG BOX above is
                        // untouched and keeps its live class, the grey being
                        // about the cells alone.
                        resolve_flag_face(dis || cells.follower, red,
                                          cell_selected(which),
                                          column_face),
                        closes);
                };
                // The lower cell never closes a run this pass paints: with no
                // field standing the upper cell follows it, and under the
                // UPPER field the field abuts it on its own left seam.
                paint_cell(lower_x, lower_w, cl.lower_run, MarkerCell::Lower,
                           /*closes=*/false);
                // The upper cell is the lower's own right-hand neighbour, so it
                // paints iff nothing has taken it: the UPPER bound's field is
                // the one thing that does, and then the run simply ends here.
                // Painted, it is the run's last box and carries the closing
                // column (pass_closes is true exactly then).
                if (paint_upper)
                    paint_cell(upper_x, upper_w, cl.upper_run,
                               MarkerCell::Upper, pass_closes);
            }

            // THE SUPPRESSED BOX PUBLISHES NO HIT RECT EITHER (codex 2026-08-02,
            // correcting this pass's first suppression): the rect must match the
            // pixels, which is this stash's whole doctrine, and a box that is not
            // painted has no extent to claim. The earlier reasoning — that the
            // entry was unreachable because the press path resolves
            // app.flag_editor_box first — held only while the editor was at
            // least as WIDE as the committed flag, which is precisely the width
            // assumption this pass removed from the painting. A narrowed editor
            // left a visually BLANK tail between its right edge and the old
            // committed width, and a press there closed the editor as an outside
            // press and then fell through to hit_test_flag IN THE SAME PRESS
            // (input_pointer.cpp), selecting, landing, seeding a double-click or
            // arming a drag from pixels where nothing was drawn.
            //
            // THE FLAG IS CLICKABLE AGAIN ON THE FRAME IT REAPPEARS, and the
            // ordering is what makes that safe rather than a lost click: the
            // press that closes the editor is already consumed, and closing
            // changes the flag cache's suppression fields, which miss its
            // fingerprint and rebuild it — one render_flags pass that paints the
            // box and republishes its rect together, since they are the same
            // pass. So the NEXT press sees a rect that exists exactly where a box
            // now does. There is no frame with one and not the other in either
            // direction.
            //
            // THE STEM IS THE DELIBERATE EXCEPTION and still publishes below: it
            // is still PAINTED for the whole editing session, so it is
            // legitimately hit-testable by the same paint-equals-claim rule that
            // takes the box's rect away.
            //
            // AND THE RIDING BOXES ARE THE SECOND EXCEPTION SINCE 2026-09-05, on
            // that same rule rather than against it: whatever stands right of an
            // open field is painted for the whole session — at that field's right
            // edge, by the editor's own painter — so it is legitimately
            // clickable, and it is THAT painter which publishes its rect, at the
            // pixels it put it on (FlagEditorBox::riding_cells, render.h). What
            // this pass suppresses is the geometry it does not draw; what it must
            // never do is claim it, and neither must anyone else.
            //
            // A MARKER UNDER A BOUND FIELD PUBLISHES HERE ALL THE
            // SAME, because its flag box is still painted: the rect below covers
            // what this pass drew — the flag, plus the lower cell under the upper
            // field — and stops at the edited box's own seam, where the field
            // begins. The two publications are disjoint by construction, so a
            // point falls in exactly one of them.
            if (out_hit_rects && pass_paints(MarkerCell::Payload)) {
                // THE RECT IS THE WHOLE BOX, BORDER INCLUDED — a click on the
                // border is a click on the flag. It is the painted extent, as
                // this stash always was; the border merely made the extent one
                // column wider than the fill.
                //
                // AND THE BOUND CELLS ARE PART OF THAT
                // EXTENT: the rect widens over them, SEAM COLUMNS INCLUDED, so
                // every span is ORDINARY FLAG SURFACE for press, drag and
                // select — one marker, one clickable box. What forks on the
                // span is the press's ADDRESSED CELL and the DOUBLE-CLICK'S
                // editor (hit_test_flag_cell, app_state.cpp), and both fork on
                // the boundaries published beside the rect rather than on a
                // re-derivation, so paint and hit cannot drift. A box standing
                // in an open FIELD contributes nothing and its boundary falls
                // onto the next painted box's: an UPPER cell in its field
                // puts its boundary at the lower cell's end, a
                // LOWER cell in its field puts both at the flag's own
                // right edge — so the claim shrinks to exactly the boxes this
                // pass drew, and the rect's right edge is the last of them.
                //
                // EACH BOUNDARY IS ITS BOX'S SEAM COLUMN: a press ON a seam
                // reads as the box the seam introduces. On a cell-less flag
                // every boundary equals the rect's own right edge, so no point
                // can fall past it.
                //
                // THE CLOSING COLUMN IS INSIDE THE RECT (2026-09-25), its last
                // column, and reads as the box it closes: past the upper
                // boundary on a run with cells (Upper), and under a cell-less
                // flag's two boundaries, which sit at `run_end` past it
                // (Payload). Under an open field the pass paints no closing
                // column and `run_end` is the edited box's own seam, as before.
                FlagHitRect r;
                r.marker_index = i;
                r.x = static_cast<double>(bx - border_w);
                r.y = static_cast<double>(lane.y);
                r.w = static_cast<double>(run_end - (bx - border_w));
                r.h = static_cast<double>(lane.h);
                r.iter_lower_boundary_x =
                    static_cast<double>(paint_lower ? lower_x : run_end);
                r.iter_upper_boundary_x =
                    static_cast<double>(paint_upper ? upper_x : run_end);
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

// THE ONE DERIVATION of the standing suppression (the contract is at the
// declaration in render.h). It is deliberately not three call sites asking the
// editor state three ways: the flag pass that SKIPS a box, the flag cache that
// KEYS the frame on it and the editor painter that DRAWS in its place all read
// this one body, so none of them can believe a different box is being edited.
SuppressedBox suppressed_flag_box(const AppState& app) {
    const text_editor::State& ed = app.top_flag_editor;
    if (!text_editor::is_active(ed)) return SuppressedBox{};
    SuppressedBox s;
    switch (ed.kind) {
        case text_editor::Kind::FlagPayload:
            s.cell = MarkerCell::Payload; break;
        case text_editor::Kind::IterBound:
            // The session's own side bit, given its cell name at the one place
            // that names it (iter_bound_editor_side, app_state.h).
            s.cell = iter_bound_editor_side(ed); break;
        default:
            // Every other kind edits somewhere else entirely (the BpmBracket
            // and the dialog fields paint in the bottom row's modal), so the
            // marker lane suppresses nothing for them.
            return SuppressedBox{};
    }
    s.marker_index = ed.target;
    return s;
}

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
                  int focus_marker,
                  MarkerCell focus_cell,
                  std::vector<FlagHitRect>* out_hit_rects,
                  std::vector<MarkerStem>* out_stems,
                  const std::vector<WarpFrameMapSegment>* warp_frame_map,
                  const DragOverlay* drag_overlay,
                  SuppressedBox suppressed) {
    render_flag_boxes_impl(
        cr, top_strip_area, lanes, waveform_width, markers,
        viewport_start_sample, viewport_end_sample, sample_rate,
        selected_set, red_set,
        // THE PAINTED COMPOSER (flag_display_text, render.h): the tempo's
        // base and its whole chain, the scale capped to `*N.NN`, and the
        // bounds are the two cells beside it. The editor seeds from the UNCUT
        // sibling, flag_text, so nothing a commit reads passes through here.
        [&](int i) { return flag_display_text(markers, i); },
        // The warp column's disabled verdict follows the label_ref cascade.
        [&](int i) { return effective_disabled(markers, i); },
        // The two bound cells, on exactly the markers the sweep reads
        // (warp_iter_cells above), and none outside the mode.
        [&](int i) { return warp_iter_cells(markers, i, iteration_on); },
        out_hit_rects, out_stems, warp_frame_map, drag_overlay,
        suppressed, iteration_on,
        focus_marker, focus_cell,
        FlagColumnFace::Warp);
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
                            bool iteration_on,
                            int focus_marker,
                            MarkerCell focus_cell,
                            std::vector<FlagHitRect>* out_hit_rects,
                            std::vector<MarkerStem>* out_stems,
                            const std::vector<WarpFrameMapSegment>* warp_frame_map,
                            const DragOverlay* drag_overlay,
                            SuppressedBox suppressed) {
    render_flag_boxes_impl(
        cr, top_strip_area, lanes, waveform_width, phase_resets,
        viewport_start_sample, viewport_end_sample, sample_rate,
        selected_set, red_set,
        // A phase reset authors no payload, so its flag carries the display-only
        // token (render.h owns it and what it reads). It reaches the pass
        // whole: the shared byte cap is gone (2026-09-19) and this column has
        // nothing to cut — the token is five bytes by ruling.
        [&](int) { return std::string(kPhaseResetLaneToken); },
        // No label_ref cascade on this column — the bool is the whole verdict.
        [&](int i) { return phase_resets[i].disabled; },
        // THE TWO BOUND CELLS, on exactly the resets the sweep reads
        // (phase_iter_cells above), and none outside the mode. The bracket is
        // a HOP bracket here — the reset's position walked along the analysis
        // lattice — so the cells carry the signed integer and no decimals,
        // which is what tells the two columns' cells apart at a glance.
        [&](int i) { return phase_iter_cells(phase_resets, i, iteration_on); },
        out_hit_rects, out_stems, warp_frame_map, drag_overlay,
        // THE BOUND CELLS ARE THE ONLY BOXES THIS COLUMN SUPPRESSES, and the
        // asymmetry is real rather than an oversight (the warp/phase-reset
        // symmetry rule, conventions.md): the BOUND editor is both columns'
        // since 2026-09-09, while the PAYLOAD editor is a
        // WARP-column surface by its own open gates — a phase reset authors
        // no payload line, its flag carrying a display-only token — so no
        // phase-reset flag can ever be the edited one for that kind. THE FORK
        // IS HERE rather than at the caller because
        // this painter owns its column's asymmetry: a suppression naming the
        // payload is dropped, so a warp target index can never be applied to
        // this store.
        suppressed.cell == MarkerCell::Payload ? SuppressedBox{} : suppressed,
        iteration_on,
        focus_marker, focus_cell,
        FlagColumnFace::PhaseReset);
}

void render_history_diff_flags(
        cairo_t* cr,
        GuiRect top_strip_area,
        FlagLaneRects lanes,
        int waveform_width,
        const std::vector<HistoryDiffFlag>& flags,
        long long viewport_start_sample,
        long long viewport_end_sample,
        int focus_index,
        const std::set<int>& selected,
        std::vector<FlagHitRect>* out_hit_rects,
        std::vector<MarkerStem>* out_stems,
        const std::vector<WarpFrameMapSegment>* warp_frame_map) {
    // The same clear-first contract the two marker painters carry: this pass is
    // the SOLE producer of both stashes while the history mode stands, so a
    // frame that paints nothing must leave nothing claimable behind.
    if (out_hit_rects) out_hit_rects->clear();
    if (out_stems)     out_stems->clear();
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;

    const GuiRect lane = lanes.marker_lane;
    if (lane.h <= 0) return;

    cairo_save(cr);
    // The redesign's one sans face, set once for the pass — the text_shape
    // precondition (shape with the font you paint with), exactly as
    // render_flag_boxes_impl sets it, through the one face owner (gui_font.h).
    gui_select_font_face(cr, GuiFontFamily::Sans);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    const int    pad_l    = marker_flag_pad_left_px();
    const int    pad_r    = marker_flag_pad_right_px();
    const int    edge_h   = marker_flag_edge_h_px();
    const int    border_w = marker_flag_border_px();
    const double baseline = static_cast<double>(lane.y) +
                            static_cast<double>(marker_flag_baseline_px());

    // THE LEFT CULL'S BOUND, DERIVED FROM THIS COMMIT'S OWN TEXT rather than
    // from the marker lane's own worst case, because THESE LABELS ARE NOT CUT.
    // The live lane truncates because a marker label is free text the user types
    // and a runaway one would swamp its neighbours; a diff flag's label is the
    // SIDECAR'S OWN TOKEN with a three-byte sign prefix, and cutting it would
    // throw away the one thing the flag exists to show — a `[-]chorus=1.05`
    // cut at nine bytes would read `[-]choru...`, which names neither
    // the label nor the value. So the text prints whole and the bound follows
    // it: one byte per em is the same over-estimate marker_flag_max_width_px
    // makes (no ASCII glyph on this face advances a full em at these sizes), and
    // an over-estimate is exactly what a cull bound must be.
    double widest_bytes = 0.0;
    for (const HistoryDiffFlag& f : flags) {
        const double n = static_cast<double>(f.removed_text.size() +
                                             f.added_text.size());
        if (n > widest_bytes) widest_bytes = n;
    }
    const double cull_width_px =
        widest_bytes * redesign_font_size_px() +
        2.0 * static_cast<double>(pad_l + pad_r) +
        // THREE border columns: the box's own at its left, the SEAM DIVIDER a
        // changed pair carries between its halves (2026-08-20), and the
        // flag's CLOSING column at its right (2026-09-25). A bound must never
        // under-state (the left one merely over-admits by a column), and the
        // widest flag in a commit may be a pair.
        3.0 * static_cast<double>(border_w);

    iterate_visible_flags_impl(
        top_strip_area, waveform_width, flags,
        viewport_start_sample, viewport_end_sample,
        warp_frame_map,
        // NO DRAG OVERLAY: the mode consumes every authoring gesture, so no
        // marker drag can be in flight while this pass runs — and a diff flag is
        // not a marker in any store, so nothing could index it anyway.
        /*drag_overlay=*/nullptr,
        cull_width_px,
        [&](int i, double left_x) {
            const HistoryDiffFlag& f = flags[static_cast<std::size_t>(i)];
            // THE MODE'S OWN FOCUS AND ITS OWN SELECTION, never the live one:
            // either lights the flag, and BOTH HALVES of a changed pair take
            // their own class's selected pair together — a double flag is one
            // item, so it lights as one. The two are ONE face by ruling (the
            // declaration says why), so this is an OR rather than a ladder.
            const bool focused =
                (i == focus_index) || (selected.count(i) != 0);

            text_shape::ShapedRun run_removed;
            text_shape::ShapedRun run_added;
            int w_removed = 0;
            int w_added   = 0;
            if (f.removed) {
                run_removed = text_shape::shape_text_run(font, f.removed_text);
                w_removed = pad_l + pad_r +
                    static_cast<int>(std::nearbyint(run_removed.width_px));
            }
            if (f.added) {
                run_added = text_shape::shape_text_run(font, f.added_text);
                w_added = pad_l + pad_r +
                    static_cast<int>(std::nearbyint(run_added.width_px));
            }
            // THE SEAM DIVIDER between the two halves, and ONLY when there
            // ARE two: a purely removed or purely added flag is one field with
            // no seam to rule (2026-08-20's experiment; the rationale is at the
            // paint below).
            const int seam_w = (w_removed > 0 && w_added > 0) ? border_w : 0;
            const int bw = w_removed + seam_w + w_added;
            // A flag with neither half is not constructible by the resolver
            // above; the guard keeps a degenerate one from publishing a
            // zero-width claim.
            if (bw <= 0) return;

            const int bx = static_cast<int>(std::nearbyint(left_x));

            // THE DISABLED AXIS, ONE EFFECTIVE BIT PER COMMIT SIDE (architect
            // 2026-08-22, the cascade joining the same day the axis landed).
            // Each half asks its OWN side's EFFECTIVE verdict — the removed
            // half `then_effective_disabled`, the added half
            // `now_effective_disabled`, each resolved within its own side's
            // full warp set at the delta (phase resets have no cascade, so
            // their local bit filled these verbatim) — so the DIM is the live
            // lane's truth per commit: a label ref whose same-side definition
            // is disabled dims here exactly as its live marker does, while the
            // '#' in the LABEL text stays the line's verbatim local byte (the
            // text/face split at HistoryDiffFlag). A
            // disable TOGGLE paints one dimmed half beside one full-strength one
            // and the direction of the toggle reads straight off the flag. Each
            // bit is meaningful exactly when its half is painted; the guards
            // below are the half's own `w_* > 0`, so a bit resting at false on a
            // half that does not exist is never consulted.
            const bool removed_disabled = f.then_effective_disabled;
            const bool added_disabled   = f.now_effective_disabled;

            // THE PAIR IS CHOSEN FIRST AND DAMPED SECOND, which is the live
            // lane's own composition order (resolve_flag_face): the focus swap
            // picks the bright pair, then the disabled blend runs over the
            // RESULT, so a focused disabled half lifts exactly as a focused live
            // one does and still reads switched off. The blend is the live arm's
            // expression verbatim — the class colour at kMarkerDisabledMix over
            // kRedesignContentGround, the marker lane's own ground — applied to
            // this lane's inks. No constant is born here: the derivation is the
            // one already ruled, reaching a second set of colours.
            GuiColor removed_fill =
                focused ? kHistoryRemovedFillSel : kHistoryRemovedFill;
            GuiColor removed_edge =
                focused ? kHistoryRemovedEdgeSel : kHistoryRemovedEdge;
            GuiColor added_fill =
                focused ? kHistoryAddedFillSel : kHistoryAddedFill;
            GuiColor added_edge =
                focused ? kHistoryAddedEdgeSel : kHistoryAddedEdge;
            if (removed_disabled) {
                removed_fill = mix_color(removed_fill, kRedesignContentGround,
                                         kMarkerDisabledMix);
                removed_edge = mix_color(removed_edge, kRedesignContentGround,
                                         kMarkerDisabledMix);
            }
            if (added_disabled) {
                added_fill = mix_color(added_fill, kRedesignContentGround,
                                       kMarkerDisabledMix);
                added_edge = mix_color(added_edge, kRedesignContentGround,
                                       kMarkerDisabledMix);
            }
            // THE LABEL DIMS FROM BLACK TOWARD ITS OWN HALF'S DIMMED FILL, the
            // live lane's disabled-label rule verbatim: a fraction of the ink
            // over the surface it actually sits on, at the LABEL's own
            // kMarkerDisabledLabelMix rather than the surfaces' fraction. THE
            // HALVES ARE INDEPENDENT because this lane paints TWO runs, one
            // inside each half's own box — so no pair-wide fallback is needed and
            // a toggle's dimmed half carries a dimmed label beside a
            // full-strength one.
            const GuiColor removed_label =
                removed_disabled
                    ? mix_color(kMarkerFlagLabel, removed_fill,
                                kMarkerDisabledLabelMix)
                    : kMarkerFlagLabel;
            const GuiColor added_label =
                added_disabled
                    ? mix_color(kMarkerFlagLabel, added_fill,
                                kMarkerDisabledLabelMix)
                    : kMarkerFlagLabel;

            // THE TWO BORDER COLUMNS DIM BY WHAT EACH BELONGS TO. The box's own
            // left border stands OUTSIDE the leftmost half's fill and is that
            // half's face element (the live lane's anatomy, where the border
            // takes the disabled blend with fill and edge), so it dims with that
            // half — the removed one on a pair or a removed-only flag, the added
            // one on an added-only flag. The SEAM divider belongs to NEITHER half
            // alone: it separates them, so it dims only when BOTH are disabled,
            // which keeps a toggle's divider at full strength against the
            // full-strength half it abuts. Both take the same blend the live
            // border takes — kMarkerFlagBorder at kMarkerDisabledMix over the
            // lane ground, which moves that near-black UP toward the ground
            // rather than down (the direction note is at kMarkerFlagBorder).
            const bool left_half_disabled =
                (w_removed > 0) ? removed_disabled : added_disabled;
            const GuiColor box_border =
                left_half_disabled
                    ? mix_color(kMarkerFlagBorder, kRedesignContentGround,
                                kMarkerDisabledMix)
                    : kMarkerFlagBorder;
            const GuiColor seam_ink =
                (removed_disabled && added_disabled)
                    ? mix_color(kMarkerFlagBorder, kRedesignContentGround,
                                kMarkerDisabledMix)
                    : kMarkerFlagBorder;
            // THE CLOSING COLUMN (architect 2026-09-25, the live lane's rule:
            // every flag's run ends on one border column) is the RIGHTMOST
            // PAINTED HALF's face element, so it dims with that half — the
            // added one on a pair or an added-only flag, the removed one on a
            // removed-only flag — the mirror of box_border's pick.
            const bool right_half_disabled =
                (w_added > 0) ? added_disabled : removed_disabled;
            const GuiColor close_border =
                right_half_disabled
                    ? mix_color(kMarkerFlagBorder, kRedesignContentGround,
                                kMarkerDisabledMix)
                    : kMarkerFlagBorder;

            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            // ONE BORDER COLUMN AT THE BOX'S LEFT, outside the fill — the
            // live lane's own geometry. It marks where the FLAG starts, and a
            // changed pair is one flag, which is why the seam between the
            // halves carried nothing until 2026-08-20; the column now standing
            // there is a different statement in the same ink (the experiment,
            // at the halves' paint below) and does not make the pair two flags:
            // one rect, one focus, one revert. IT TAKES THE DISABLED BLEND WITH
            // THE HALF IT STANDS AGAINST (architect 2026-08-22, retiring the
            // undamped reading this column shipped with — that reading said the
            // disabled blend was a live-marker face and this lane painted none,
            // and the lane carries the disabled axis now): the pick is at
            // box_border above.
            cairo_set_source_rgb(cr, box_border.r, box_border.g, box_border.b);
            cairo_rectangle(cr, bx - border_w, lane.y, border_w, lane.h);
            cairo_fill(cr);
            // The halves, left (removed / red) then right (added / green). Each
            // takes its own fill for the lane's full height and its own 1px top
            // edge over its own width: the edge runs HORIZONTALLY and so is
            // never a divider — it cannot separate two things standing side by
            // side.
            //
            // THE SEAM CARRIES A DIVIDER — A STANDING RULING (2026-08-20,
            // hardened 2026-09-02; it landed as a trial). The halves met
            // fill-to-fill from this lane's
            // first day, on the reading that a changed pair is ONE flag and the
            // border marks where a flag starts. The architect then found the
            // pair reading at two DEPTHS on the glass: adjacent saturated hues
            // produce chromostereopsis, and red against green is the strongest
            // case of it in this palette. The
            // mitigation under trial is the same 1px kMarkerFlagBorder column
            // the flag's own left border is — a dark rule between the fields
            // instead of a hue boundary doing the work alone. IT DIMS ONLY WHEN
            // BOTH HALVES DO (2026-08-22, at seam_ink above): the divider
            // belongs to neither half by itself.
            if (w_removed > 0) {
                cairo_set_source_rgb(cr, removed_fill.r, removed_fill.g,
                                     removed_fill.b);
                cairo_rectangle(cr, bx, lane.y, w_removed, lane.h);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, removed_edge.r, removed_edge.g,
                                     removed_edge.b);
                cairo_rectangle(cr, bx, lane.y, w_removed, edge_h);
                cairo_fill(cr);
            }
            if (seam_w > 0) {
                cairo_set_source_rgb(cr, seam_ink.r, seam_ink.g, seam_ink.b);
                cairo_rectangle(cr, bx + w_removed, lane.y, seam_w, lane.h);
                cairo_fill(cr);
            }
            if (w_added > 0) {
                cairo_set_source_rgb(cr, added_fill.r, added_fill.g,
                                     added_fill.b);
                cairo_rectangle(cr, bx + w_removed + seam_w, lane.y, w_added,
                                lane.h);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, added_edge.r, added_edge.g,
                                     added_edge.b);
                cairo_rectangle(cr, bx + w_removed + seam_w, lane.y, w_added,
                                edge_h);
                cairo_fill(cr);
            }
            cairo_set_source_rgb(cr, close_border.r, close_border.g,
                                 close_border.b);
            cairo_rectangle(cr, bx + bw, lane.y, border_w, lane.h);
            cairo_fill(cr);
            cairo_restore(cr);

            // THE LANE'S INK, not the redesign's: a diff flag wears this
            // lane's whole anatomy, so it wears its black text too (the ruling
            // and the per-class contrast table are at kMarkerFlagLabel). SET PER
            // HALF since 2026-08-22 rather than once for the box: each half's ink
            // is its own dimmed-or-not resolution (removed_label / added_label
            // above), which is what lets a disable toggle show a dimmed label on
            // one side of the seam and a full one on the other.
            if (w_removed > 0) {
                cairo_set_source_rgb(cr, removed_label.r, removed_label.g,
                                     removed_label.b);
                text_shape::show_shaped_run(
                    cr, run_removed, static_cast<double>(bx + pad_l), baseline);
            }
            if (w_added > 0) {
                cairo_set_source_rgb(cr, added_label.r, added_label.g,
                                     added_label.b);
                text_shape::show_shaped_run(
                    cr, run_added,
                    static_cast<double>(bx + w_removed + seam_w + pad_l),
                    baseline);
            }

            if (out_hit_rects) {
                // THE WHOLE BOX, BOTH BORDERS INCLUDED (the left one and the
                // closing one, 2026-09-25), and a changed pair claims as
                // ONE rect — which is what makes the mode's focus click land on
                // one item however wide it is painted.
                FlagHitRect r;
                r.marker_index = i;
                r.x = static_cast<double>(bx - border_w);
                r.y = static_cast<double>(lane.y);
                r.w = static_cast<double>(bw + 2 * border_w);
                r.h = static_cast<double>(lane.h);
                // NO BOUND CELLS IN THIS MODE, so every
                // boundary is the rect's own right edge and no point can fall
                // past it: the view paints the delta's own two-tone flag and
                // nothing else. The live lane's cells have no twin here:
                // an iteration bracket is session-only and never in a commit,
                // so a diff flag has no bounds to show and the mode's
                // `h`-refused arrows nothing to step.
                r.iter_lower_boundary_x = r.x + r.w;
                r.iter_upper_boundary_x = r.x + r.w;
                out_hit_rects->push_back(r);
            }
            if (out_stems) {
                // THE STEM READS THE CLASS AND THE FOCUS SWAP — the live
                // lane's rule (architect 2026-09-23: the stem follows the
                // selection bit as the fill does), and here the class is "does
                // the commit still have this line": a removed or CHANGED entry
                // stems red (deference to the old, the architect's ruling for
                // the pair), a purely added one green. The colour is the
                // class's fill — its Sel fill on a focused or selected flag —
                // and it is never damped: a stem either paints its class or is
                // absent.
                //
                // AND IT READS THE DISABLED AXIS (architect 2026-08-22), on the
                // SINGLE-half flags alone. A removed-only or added-only flag
                // whose one side is disabled publishes NO ENTRY AT ALL — the live
                // lane's rule verbatim, expressed the live lane's way, as an
                // absent entry rather than a bit the consumer re-decides
                // (MarkerStem's contract). A CHANGED PAIR ALWAYS KEEPS ITS STEM,
                // whichever of its halves are disabled: the pair is not a line in
                // a switched-off state, it is a live EDIT being displayed, and a
                // disable toggle is precisely the edit whose stem must not
                // vanish. That is why the test below is on the SINGLE halves
                // and never on both effective bits at once.
                const bool pair = (w_removed > 0 && w_added > 0);
                const bool single_disabled =
                    !pair && (w_removed > 0 ? removed_disabled
                                            : added_disabled);
                if (!single_disabled) {
                    out_stems->push_back(
                        MarkerStem{i, static_cast<double>(bx),
                                   f.removed
                                       ? (focused ? kHistoryRemovedFillSel
                                                  : kHistoryRemovedFill)
                                       : (focused ? kHistoryAddedFillSel
                                                  : kHistoryAddedFill)});
                }
            }
        });

    cairo_restore(cr);
}

namespace {
    // Current GUI scale, in PERCENT. Set by set_gui_scale_percent from the two
    // application points (gui_main's startup read of the device config, and the
    // settings editor's gui_scale commit). EVERY painted pixel quantity in the product reads it
    // through gui_scale_factor().
    int    g_gui_scale_percent = 100;
} // namespace

void   set_gui_scale_percent(int percent) { g_gui_scale_percent = percent; }

int    gui_scale_percent() { return g_gui_scale_percent; }
double gui_scale_factor()  {
    return static_cast<double>(g_gui_scale_percent) / 100.0;
}

namespace {
    // The waveform's configured maximum height in AUTHORED px — the device
    // config's `max_waveform_height`, 0 meaning no maximum. Installed by
    // set_max_waveform_height_px at the scale's two application points (the
    // contract is at the declaration, render.h). 500 is construction state,
    // the templates' value; startup installs the config's before any read.
    int    g_max_waveform_height_px = 500;
} // namespace

void set_max_waveform_height_px(int authored_px) {
    g_max_waveform_height_px = authored_px;
}
int waveform_max_h_px() {
    if (g_max_waveform_height_px <= 0) return std::numeric_limits<int>::max();
    return scaled_px(g_max_waveform_height_px, 1);
}

// (THE TIP-DOWN TRIANGLE MASK IS GONE — 2026-08-02. build_triangle_mask,
// playhead_triangle_mask and their two file-scope cache globals built an
// antialiased A8 silhouette (2H-1 by H) that render_playhead's draw_triangle
// branch stamped; row 5 retired the cursor triangle for the MARKER lane's
// aliased head and left every caller passing false, so the whole cluster was
// unreachable. The geometry it anchored survives at its exact values in
// render.h — waveform_inset_px() and playhead_half_px(), each spelling its own
// derivation now.)

// -- The flag editor's unrolled box ---------------------------------------

// WHERE ONE BOUND CELL'S SEAM COLUMN STANDS on the committed flag, as an
// offset from the flag fill's left edge — the bound editor's anchor, and the
// whole of what that anchor needs. Off the same eligibility, the same tokens
// and the same font the flag pass lays the cells out with (warp_iter_cells or
// phase_iter_cells, `phase` deciding which — the bound editor is both columns'
// since 2026-09-09), so the field opens on exactly the column the resting
// cell's seam stands on.
//
// IT ANSWERS WHERE, NOT HOW WIDE. It used to publish the cell's fill width too,
// which the field was PINNED to; that pin is retired (architect 2026-09-05 —
// the bound field takes the payload field's one width rule, its box
// being its own two pads and its own run like it), so the width has no
// reader left and the struct went with it. The lower cell's anchor is the flag's
// own right edge and the upper's is past the lower cell, which is the only
// thing the side decides here; where no cells paint at all the answer is the
// flag's right edge, unreachable because the open asked (enter_iter_bound_edit)
// and a keyboard-modal editor freezes the mode bit.
static int committed_cell_seam_off(const AppState& app,
                                   cairo_scaled_font_t* font, bool phase,
                                   int idx, MarkerCell side,
                                   bool iteration_on) {
    const std::vector<GuiWarpMarker>&       mv  = app.warpmarkers.markers();
    const std::vector<GuiPhaseResetMarker>& pmv =
        app.phaseresetmarkers.markers();
    // THE PAINTED COMPOSER ON EACH COLUMN, never the uncut one: this shapes
    // exactly what the flag pass shaped, or the field would open at a column
    // no cell stands on (the declaration of flag_display_text says why this
    // is the one place a wrong composer hides).
    const std::string label = phase ? std::string(kPhaseResetLaneToken)
                                    : flag_display_text(mv, idx);
    const text_shape::ShapedRun run = text_shape::shape_text_run(font, label);
    const int pads   = marker_flag_pad_left_px() + marker_flag_pad_right_px();
    const int flag_w = pads + static_cast<int>(std::nearbyint(run.width_px));
    const IterCellLayout cl = measure_iter_cells(
        font, phase ? phase_iter_cells(pmv, idx, iteration_on)
                    : warp_iter_cells(mv, idx, iteration_on));
    if (cl.present && side == MarkerCell::Upper)
        return flag_w + marker_flag_border_px() + cl.lower_w;
    return flag_w;
}

// The contract (the face, the unclamped position, the non-const AppState) is
// at the declaration in render.h. What follows is the mechanics.
void render_flag_editor_box(cairo_t* cr, AppState& app, const GuiAudio& audio) {
    // The publication is unconditional: every run that finds no open editor
    // writes the invalid state, so a closed session can never leave the pointer
    // path a stale box to grab.
    FlagEditorBox& out = app.flag_editor_box;
    out = FlagEditorBox{};

    text_editor::State& ed = app.top_flag_editor;
    if (!text_editor::is_active(ed)) return;
    // TWO KINDS PAINT IN THE MARKER LANE AND THEY ARE ONE MODEL. FlagPayload
    // unrolls the flag ITSELF to hold the payload; IterBound opens ONE BOUND
    // CELL as the field. In every case the edited box yields in the cached pass, the field is the
    // width of its own content, the boxes LEFT of it stand exactly where they
    // rest and the boxes RIGHT of it ride the field's edge (SuppressedBox,
    // render.h — the whole ruling). The BpmBracket kind paints in the bottom
    // row's modal instead and returns here.
    const bool bound_kind   = (ed.kind == text_editor::Kind::IterBound);
    const bool payload_kind = (ed.kind == text_editor::Kind::FlagPayload);
    if (!payload_kind && !bound_kind) return;

    // The PAYLOAD editor is a WARP-COLUMN surface by its own open gates, in
    // EITHER audio view since 2026-08-24 (the home-view binding's fifth ruled
    // exception, active_column_authoring_allowed, app_state.h) — which costs
    // this painter nothing, the column below being resolved on the DISPLAYED
    // basis and the live map like every other lane item. The BOUND editor is
    // both columns' since 2026-09-09 (the phase-reset
    // column carries an iteration bracket of its own), so its store is the
    // ACTIVE column's — the column the open route resolved the index against.
    // A target index the store has since shrunk past is the only failure
    // shape, and it simply paints nothing.
    const bool phase = bound_kind && app.active_markers_view == 'P';
    const std::vector<GuiWarpMarker>&       mv  = app.warpmarkers.markers();
    const std::vector<GuiPhaseResetMarker>& pmv = app.phaseresetmarkers.markers();
    const int idx = ed.target;
    const int store_n = phase ? static_cast<int>(pmv.size())
                              : static_cast<int>(mv.size());
    if (idx < 0 || idx >= store_n) return;
    const int64_t marker_frame =
        phase ? pmv[static_cast<size_t>(idx)].time_frame
              : mv[static_cast<size_t>(idx)].time_frame;

    const GuiRect lane = top_marker_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    cairo_save(cr);
    // The redesign's sans, set once through the one face owner (gui_font.h) —
    // shape and paint on ONE scaled font, the text_shape precondition. Nothing
    // below changes the size.
    gui_select_font_face(cr, GuiFontFamily::Sans);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    // THE FULL, UNTRUNCATED pending — the unroll's whole point. The scale cap
    // is a PAINTED-FLAG rule; an editor shows what it is editing.
    const text_shape::ShapedRun run =
        text_shape::shape_text_run(font, ed.pending);
    std::vector<double> byte_x =
        text_shape::byte_offsets_px(run, ed.pending.size());

    const int pad_l    = marker_flag_pad_left_px();
    const int pad_r    = marker_flag_pad_right_px();
    const int edge_h   = marker_flag_edge_h_px();
    const int border_w = marker_flag_border_px();
    // THE CARET'S COLUMN, AND WHERE EVERY FIELD FINDS IT. The caret at
    // end-of-text stands one column past the last glyph, so a field must own a
    // column its run does not — and IT BORROWS THAT COLUMN FROM ITS OWN RIGHT
    // PAD rather than buying one, on both kinds alike (architect
    // 2026-09-05: "all flag editors should work under the same principle
    // graphically ... graphically to the user it should be transparent
    // switching between the comments, the bounds and the main payload; the
    // main difference should be the colour of the comments and the syntax").
    // So the box below is exactly two pads plus its run, which is exactly what
    // the resting box it stands in for is: at the open — where the run is the
    // committed text on the same font — the payload field IS the flag and
    // the bound field IS its cell, and nothing riding past the field steps sideways when it opens. What moves
    // afterwards is what is TYPED, ON EVERY KIND ALIKE (architect 2026-09-05,
    // retiring the bound field's pin to its cell — "the two editors on the
    // opposite ends behaving one way and the bounds one in the middle behaving
    // in a different way makes the whole thing seem hacked together"): the box
    // grows and shrinks with the pending run, an emptied field is two pads —
    // the smallest field there is, on every kind — and the marker's boxes to
    // the right of this one ride the edge. There was no fixed-width grammar to
    // pin a bound field to in any case: this face is PROPORTIONAL, so `-3.75`
    // and `+1.00` are not the same width.
    // The pad is two authored pixels and the caret one, so the borrow leaves
    // the fill a pad on that side at every scale but the smallest, where both
    // floor to one column: the viewport then ends on the box's own right edge
    // and the caret's column is the box's last fill column — visible ink, not
    // a lost one, so the travel arithmetic below needs no floor of its own.
    // The TEXT VIEWPORT is what widens by the borrowed column; the box never
    // does.
    // One authored pixel, scaled like every other row-5 length, with the
    // tree's own per-metric floor: it rounds to 0 at gui_scale 50, which would
    // leave the caret no column to stand in.
    const int caret_px = scaled_px(1.0, 1);

    const int run_w = static_cast<int>(std::nearbyint(run.width_px));
    // THE BOX IS ITS TWO PADS AND ITS RUN, AND NOTHING BOUNDS IT — not the
    // lane, not the window. The LANE-WIDTH CAP that stood here went with the
    // position clamp below (architect 2026-09-06): it was a WIDTH rule kept
    // for a POSITION rule's sake — a box no wider than the lane can always be
    // slid fully on-window — and with the field standing wherever its own box
    // stands, a field wider than the window simply runs off the edge, which is
    // the same truthful answer the cap existed to avoid giving. Nothing else
    // read it: the text viewport, the view offset and the riding run all
    // derive from `box_w` rather than from the lane.
    const int box_w = pad_l + run_w + pad_r;

    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    const int col = painted_column_of_source_frame_on_basis(
        app, audio, static_cast<double>(marker_frame),
        map, basis.vp_start, basis.spp);
    const GuiRect area = waveform_area(app);

    // ITERATION MODE ADDS THE TWO BOUND CELLS TO THE COMMITTED FLAG, so the
    // anchors below must ask under the same verdict the flag pass paints
    // under: THE COLUMN'S, not the mode's bare bit (iteration_column_lit,
    // app_state.h — architect 2026-09-10, the lamp is lit for the column it
    // was pressed in and the cells live there alone; the live column is
    // stable for the whole life of the lamp, the W/P switch being one of the
    // acts the iteration lock refuses). `phase` above is this editor's
    // column, the payload editor being the warp column's by its own open
    // gates, so the two anchors measure exactly the boxes the cached pass
    // painted.
    const bool iteration_on = iteration_column_lit(app, phase ? 'P' : 'W');
    // EVERY FIELD OPENS WHERE ITS CELL SITS. The BOUND field opens past its
    // own cell's seam divider (committed_cell_seam_off), so the field's fill
    // begins on exactly the column the resting cell's fill begins on (the
    // divider itself is painted below, outside this fill on its left, which
    // is that border's own geometry), the marker's boxes to its LEFT
    // standing where they are. The PAYLOAD field opens on the marker's own
    // column, the flag unrolling from itself. In every case the boxes to the
    // RIGHT of the field yield to it and RIDE ITS EDGE below, which at the open
    // is exactly where they rest: the field buys no column of its own (the
    // borrow above), so `bx + box_w` is the committed box's own right edge
    // until something is typed.
    //
    // THE FIELD'S CELL — which of the marker's boxes this field stands in for —
    // is read off the ONE derivation the flag pass's suppression is read off
    // (suppressed_flag_box), so the box that yields and the box that opens have
    // exactly one answer between them in the product. It is the payload's own
    // Payload for the flag editor and the session's side for a bound editor.
    const MarkerCell field_cell = suppressed_flag_box(app).cell;
    const int anchor_off =
        bound_kind ? committed_cell_seam_off(app, font, phase, idx,
                                             field_cell, iteration_on) +
                         border_w
                   : 0;

    // NO FIELD IS CLAMPED, ON ANY KIND, AT EITHER EDGE (architect 2026-09-06,
    // on the clamp this line used to carry: "let's get rid of that, that's a
    // good catch … leave its position truthful, don't clamp it, don't do
    // anything"). The box opened at the seam above and then slid left or right
    // to keep itself whole on-window — a rule written for the payload editor
    // when the flag was the only box on the row, and one the ONE GRAPHIC MODEL
    // cannot survive: at a lane edge the slide walked the field left OVER the
    // lower cell or the flag box that the cached pass is still painting at
    // rest, so the boxes to its left no longer stood where they rest and the
    // field no longer stood in its own box's slot — the two things the model
    // promises. So the field opens where its box IS and stays there.
    //
    // THE WINDOW IS THE CLIP AND IT NEEDS NO CALL: this lane spans the window
    // (strip_row_rect anchors every lane at x 0 with the window's own width),
    // and this painter draws straight onto the window surface, so a box past
    // either edge falls off it exactly as a cached flag box does off the
    // strip-width surface the flag pass paints into. A field cut off at an
    // edge is READ BY PANNING THE VIEWPORT: the marker-lane editors are
    // pointer- and wheel-transparent, so the wheel and the grab-pan work while
    // one stands and the field travels with its marker, which is the same
    // answer the row gives for a flag box that is half off the edge at rest.
    const int bx = area.x + col + anchor_off;

    // The text VIEWPORT inside the box: the band the run is clipped to, and the
    // width the view offset is measured against. The caret column belongs to it
    // (a caret at the end must be inside the clip to be seen), and THIS IS
    // WHERE THE BORROW IS SPENT, on every kind: the viewport reaches one column
    // INTO the right pad — the column the box above deliberately does not buy —
    // so a caret at end-of-text is inside the clip with the run standing
    // exactly where the committed text stands. Where the pad has floored to the
    // caret's own width (gui_scale 50) the borrow takes the whole pad and the
    // viewport ends on the box's right edge, the caret's column being the box's
    // last fill column.
    const double view_x0 = static_cast<double>(bx + pad_l);
    const int view_pad_r = std::max(pad_r - caret_px, 0);
    const double view_x1 = static_cast<double>(bx + box_w - view_pad_r);
    const double view_w  = view_x1 - view_x0;

    // THE MINIMAL-TRAVEL VIEW OFFSET (the field's contract is at
    // State::view_offset_px). Scroll only as far as the caret demands, in
    // whichever direction it left the VIEWPORT, then clamp to the run's own
    // travel — so a caret walking right pushes the view right one glyph at a
    // time and walking back left pulls it back the same way, never jumping.
    // The caret's own column is reserved at the right edge, so the comparison
    // is against (view_w - caret) rather than view_w: a caret at end-of-text
    // stops with its column inside the clip instead of half past it.
    //
    // WHAT IT STILL HAS TO DO HERE IS SUB-PIXEL, AND IT IS NOT NOTHING. The
    // box holds this field's WHOLE run by construction (two pads and the run,
    // no cap), so no long buffer travels on this surface any more — but
    // `box_w` takes the run's width to the nearest whole column and can round
    // DOWN by as much as half of one, which leaves the caret's reserved column
    // sitting exactly on the clip's right edge with no ink of its own. The
    // travel below pulls the origin back by that remainder and the caret is
    // seen. (The DIALOG field, whose box is a fixed width a long buffer really
    // does not fit, is where the glyph-by-glyph travel lives — the same rule
    // applied at paint_modal_dialog to a surface that needs all of it.)
    //
    // IT KEEPS THE CARET INSIDE THE FIELD, NEVER INSIDE THE WINDOW. With the
    // field standing at its own box wherever that box is, a field at a lane
    // edge is cut off and its caret can be cut off with it; the answer to that
    // is the VIEWPORT PAN (the ruling at `bx` above), not a scroll of the text
    // inside a box that is already showing all of it.
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
    // pair with this lane's own kMarkerFlagFillRedSel / kMarkerFlagEdgeRedSel —
    // the red class's BRIGHT pair, which since 2026-09-16 is what "the one
    // invalid red" names: that ruling gave the class a calm REST pair for a
    // resting coincident marker and kept the bright one for the flash, so an
    // invalid commit is as loud as it ever was and can never be mistaken for
    // the marker's own resting class. The three DIALOG editors flash this
    // same pair (as this box's anatomy on the bottom strip from 2026-08-02, and
    // as the dialog FIELD's recolor since 2026-08-12), so there is no
    // second red to contrast against (see the declaration). It overrides the
    // whole pair because a failed commit must read as a state of THIS box and
    // not as a marker that suddenly normalized.
    const bool dis = phase ? pmv[static_cast<size_t>(idx)].disabled
                           : effective_disabled(mv, idx);
    // The class's red is the COLUMN'S OWN paint cue, the set the resting flag
    // pass for this column reads, so the field and the boxes riding it wear
    // the red their resting twins wear on every column.
    const bool red_class =
        phase
            ? phase_reset_red_flag_set_cached(app).red.count(idx) > 0
            : warp_red_flag_set_cached(
                  app, audio.sample_rate(),
                  static_cast<long>(audio.total_frames())).red.count(idx) > 0;
    // THE SELECTED PAIR IS THE ADDRESSED CELL'S ALONE (the flag pass's own
    // rule, render_flags): this field is bright iff the marker is selected
    // and the cell it edits is the focus's addressed cell — which every
    // open makes true by seating the cell it edits, and is still read off
    // the state rather than assumed. Every box in the riding run below asks
    // the same question of its own cell, which is why the flag pass's own
    // fallback is written to ignore suppression: the field is where the
    // marker's selection shows while its box is being edited.
    const bool sel = app.selected_markers.count(idx) > 0;
    const MarkerCell bright = idx == app.last_selected_marker
                                  ? app.addressed_cell : MarkerCell::Payload;
    const auto cell_selected = [&](MarkerCell c) { return sel && c == bright; };
    // EVERY FIELD WEARS ITS OWN COLUMN'S HUE, because it IS its box unrolled:
    // the open editor must read as the same flag or cell, only wider, which
    // is the whole surface's promise. The payload editor is a warp-column
    // surface by its own open gates, so the only field this reaches on the
    // phase-reset column is a BOUND field, and it wears the phase-reset blue
    // as the resting cell does (architect 2026-09-21, superseding the
    // 2026-09-15 purple on either column).
    const FlagColumnFace column_face =
        phase ? FlagColumnFace::PhaseReset : FlagColumnFace::Warp;
    FlagFace face = resolve_flag_face(dis, red_class, cell_selected(field_cell),
                                      column_face);
    // The border column the box wears: the flag's own for the payload editor,
    // and the SEAM DIVIDER for the bound field — the
    // same constant, the same width, the same face.border, standing on the
    // same column the resting box's divider stands on. Both are "the border
    // outside the fill on its left"; only which seam it marks differs.
    const int left_border_w = border_w;
    // DOES THE FIELD CLOSE THE RUN (architect 2026-09-25: every marker's run
    // ends on ONE border column on its rightmost box)? Iff nothing rides past
    // it — the UPPER field, the marker's last box by rank, or a payload field
    // on a marker with no cells. Otherwise the last riding box closes (below)
    // and the field's right side is the first riding cell's own one-pixel
    // seam, never a closing column plus a seam. The answer is the riding
    // run's own composer output (`ride_text`, read again below), so the two
    // cannot disagree about which box ends the run.
    const int  field_rank = flag_box_rank(field_cell);
    const IterCellText ride_text =
        field_rank >= flag_box_rank(MarkerCell::Upper) ? IterCellText{}
        : phase ? phase_iter_cells(pmv, idx, iteration_on)
                : warp_iter_cells(mv, idx, iteration_on);
    const bool ride_cells    = ride_text.present;
    const int  field_close_w = ride_cells ? 0 : border_w;
    if (ed.red) {
        face.fill  = kMarkerFlagFillRedSel;
        face.edge  = kMarkerFlagEdgeRedSel;
        // THE FLASH TAKES THE UNDAMPED BORDER TOO, and for the same reason it
        // takes the undamped fill: the override replaces the resolved face
        // WHOLE with the live red class's, because a failed commit must read as
        // a state of this box rather than as the marker's own class. Leaving
        // the border blended while the fill went full-strength would be the one
        // half-applied surface — a DISABLED marker's editor (reachable:
        // enter_top_flag_edit gates on the store index alone) would flash a
        // bright red box behind a dimmed border.
        face.border = kMarkerFlagBorder;
        face.label = kMarkerFlagLabel;
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
    // missing on open and come back on commit. (The DIALOG editors' invalid
    // flash carried this same flag-box anatomy from 2026-08-02 until
    // 2026-08-12, when it became the dialog FIELD's recolor in the same red
    // pair — paint_modal_dialog; the flag editor's box is the anatomy's one
    // editor tenant now.)
    //
    // The border sits OUTSIDE the fill like the flag's, so nothing the text
    // viewport or the view offset computed above moves: box_w, view_x0 and
    // view_x1 are all fill-relative, and at the window's left edge this column
    // simply falls off the surface exactly as a flag's does. Its COLOUR comes
    // off the resolved face, so a DISABLED marker's open editor carries the
    // damped border its idle flag carries — the editor opens on any store index
    // (enter_top_flag_edit), disabled included, so this is a live path and not a
    // defensive one.
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    if (left_border_w > 0) {
        cairo_set_source_rgb(cr, face.border.r, face.border.g, face.border.b);
        cairo_rectangle(cr, bx - left_border_w, lane.y, left_border_w, lane.h);
        cairo_fill(cr);
    }
    cairo_set_source_rgb(cr, face.fill.r, face.fill.g, face.fill.b);
    cairo_rectangle(cr, bx, lane.y, box_w, lane.h);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, face.edge.r, face.edge.g, face.edge.b);
    cairo_rectangle(cr, bx, lane.y, box_w, edge_h);
    cairo_fill(cr);
    // THE CLOSING COLUMN, when the field is the run's last box: the field's
    // own face.border (the red flash's undamped one included — it is a state
    // of this box), standing just past the fill, OUTSIDE the text viewport, so
    // nothing the viewport or the view offset computed moves.
    if (field_close_w > 0) {
        cairo_set_source_rgb(cr, face.border.r, face.border.g, face.border.b);
        cairo_rectangle(cr, bx + box_w, lane.y, field_close_w, lane.h);
        cairo_fill(cr);
    }
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

    // 2. The selection highlight, then 3. the text — THE ACCENT UNDER THE LABEL
    //    WHITE, the product's one selection pairing since 2026-08-28 (the
    //    palette block at kMarkerFlagLabel's neighbour carries the ruling and
    //    its recorded cost). The dialog editors' field paints the same two
    //    colours; this surface differs only in that its UNSELECTED run is the
    //    lane's black rather than that same white, which is why the selected
    //    substring here needs a second show and the field's does not.
    //
    //    IT REPLACES THE WHITE FIELD / BLACK TEXT BAND of 2026-08-20, and
    //    kMarkerEditorSelectionBand went with it. That band was kdenlive's
    //    text-input precedent; the ruling took Breeze Light's selection
    //    instead, for every editor at once.
    //
    //    THE SELECTED SUBSTRING IS THE WHOLE RUN RE-SHOWN UNDER A CLIP, never
    //    a run shaped from the substring alone: shaping the selected bytes on
    //    their own could kern the first glyph differently and shift the ink
    //    sideways under a band whose edges came from byte_x. THE CLIP TAKES THE
    //    BAND'S OWN ROUNDED COLUMNS, not the fractional byte_x pair the band
    //    rounded FROM, so the white ink starts and stops exactly where the blue
    //    does.
    //
    //    ONE INK PER PIXEL, AND THAT IS WHY THE BLACK RUN IS CLIPPED TOO
    //    (2026-08-28, off the architect's own screenshot of a selected word
    //    wearing a dingy grey halo). The black pass used to run UNCLIPPED
    //    under the white one, so every pixel of a selected glyph's
    //    ANTIALIASED EDGE was painted twice: black at partial coverage first,
    //    darkening the blue underneath it, then white at that same partial
    //    coverage over the already-darkened blue — a grey rim no ink in the
    //    palette names. A file manager paints each pixel once, and so does
    //    this now: with a selection standing, THE BLACK RUN IS CLIPPED TO THE
    //    COMPLEMENT OF THE BAND inside the text viewport and the white run to
    //    the band, two disjoint regions whose union is the whole viewport, so
    //    no pixel is painted by both inks and every edge pixel antialiases
    //    against exactly the ground it sits on. THE COMPLEMENT IS THREE
    //    RECTANGLES, not two, because THE BAND IS NOT THE LANE: it starts
    //    under the box's 1px top edge (band_y), so besides the columns left
    //    and right of it there is the strip ABOVE it, where a tall glyph's
    //    ascender keeps the black it has always had. With no selection the
    //    black run paints unclipped, exactly as before.
    //
    //    (The deleted pass this is NOT: the old two-tone re-show painted the
    //    selected glyphs in `face.fill` — saturated ink ANTIALIASED AGAINST A
    //    WHITE band, which fringed every edge pale and read as washed out.
    //    Near-white on the accent has no such blend to make.)
    //
    //    Both edges come from byte_x, so the highlight cannot drift off the
    //    glyphs it marks however proportional they are.
    const bool has_sel = text_editor::has_selection(ed);
    const size_t s0 = static_cast<size_t>(text_editor::selection_start(ed));
    const size_t s1 = static_cast<size_t>(text_editor::selection_end(ed));
    const int ix0 =
        static_cast<int>(std::nearbyint(text_origin_x + byte_x[s0]));
    const int ix1 =
        static_cast<int>(std::nearbyint(text_origin_x + byte_x[s1]));
    // THE BAND'S WIDTH IN COLUMNS, resolved once: the fill below, the white
    // run's clip and the black run's complement all read this one expression,
    // so the three cannot disagree by a pixel. A selection whose glyphs carry
    // no advance still marks one column.
    const int band_w = (ix1 > ix0) ? (ix1 - ix0) : 1;
    if (has_sel) {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, kRedesignAccent.r, kRedesignAccent.g,
                             kRedesignAccent.b);
        cairo_rectangle(cr, ix0, band_y, band_w, band_h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // THE RUN, SHOWN ONCE PER REGION (the ruling in the block above): the
    // whole run in the lane's black off the band, the whole run again in the
    // label white on it, neither reaching a pixel the other painted.
    cairo_set_source_rgb(cr, face.label.r, face.label.g, face.label.b);
    if (!has_sel) {
        text_shape::show_shaped_run(cr, run, text_origin_x, baseline);
    } else {
        cairo_save(cr);
        // The band's complement inside the viewport, as ONE clip path: the
        // columns left of the band, the columns right of it, and the top-edge
        // strip above it. A part with nothing in it is left out rather than
        // added empty — an empty rectangle is a no-op in a fill but not
        // obviously so in a clip path, and a selection that fills the viewport
        // is meant to leave the black run nothing at all.
        const double band_x0 = static_cast<double>(ix0);
        const double band_x1 = static_cast<double>(ix0 + band_w);
        if (band_x0 > view_x0) {
            cairo_rectangle(cr, view_x0, static_cast<double>(lane.y),
                            band_x0 - view_x0, static_cast<double>(lane.h));
        }
        if (view_x0 + view_w > band_x1) {
            cairo_rectangle(cr, band_x1, static_cast<double>(lane.y),
                            (view_x0 + view_w) - band_x1,
                            static_cast<double>(lane.h));
        }
        if (band_y > lane.y) {
            cairo_rectangle(cr, band_x0, static_cast<double>(lane.y),
                            static_cast<double>(band_w),
                            static_cast<double>(band_y - lane.y));
        }
        cairo_clip(cr);
        text_shape::show_shaped_run(cr, run, text_origin_x, baseline);
        cairo_restore(cr);

        cairo_save(cr);
        cairo_rectangle(cr, ix0, band_y, band_w, band_h);
        cairo_clip(cr);
        cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                             kRedesignLabel.b);
        text_shape::show_shaped_run(cr, run, text_origin_x, baseline);
        cairo_restore(cr);
    }

    // 4. The caret: a blink-gated filled integer column at the cursor's own
    //    byte boundary, AA off — the same crisp-column convention the
    //    retired monospace box used, on a shaped position instead of a grid one.
    //    IT IS INK, NOT FIELD, so it stays `face.label` black wherever it
    //    lands, over the accent band included — black reads on #3daee9, and a
    //    caret that changed colour on crossing a selection edge would be
    //    stating something about the selection rather than about the cursor.
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

    // THE MARKER'S BOXES TO THE RIGHT OF THE FIELD RIDE ITS EDGE — in the flag
    // pass's own left-to-right order, each wearing its resting anatomy. WHAT
    // RIDES FOLLOWS FROM WHICH BOX THE FIELD STANDS IN FOR (the one graphic
    // model, SuppressedBox in render.h): the payload field carries the two
    // bound cells, the LOWER-bound field carries the upper cell, and the
    // UPPER-bound field carries nothing, the upper cell being the marker's
    // rightmost box.
    //
    // THEY MUST NOT SIMPLY VANISH for the length of an edit: the cells are the
    // marker's range, which he is often typing AGAINST (architect 2026-09-05,
    // "the cells should stand"). So the cached pass drops
    // them and they paint HERE instead, from the field's right edge, sliding
    // with it as it grows and shrinks so THE ROW READS AS IT READS AT REST. At
    // the open the run does not move at all — the field buys no caret column,
    // so its right edge is the committed box's own — and it moves afterwards
    // only by what is typed.
    //
    // Each box wears its resting anatomy through the resting painter
    // (paint_iter_bound_cell), and each asks the selected-cell question its
    // resting twin asks —
    // which answers no on every one of them under every kind, each open having
    // seated the axis on the cell it edits, so they wear the class pair while
    // the field wears the bright one.
    //
    // IT IS PUBLISHED AS A SECOND RECT, NEVER FOLDED INTO `box`, and that rect
    // is a FLAG HIT RECT — the same shape and the same two boundaries the
    // flag pass publishes for a resting run, because THE RIDING CELLS ARE THE
    // MARKER'S OWN CELLS FOR THE POINTER TOO (architect 2026-09-05). A press
    // on one closes this editor as any outside press does and then acts on the
    // cell it landed on, so the run has to answer "which marker, which cell"
    // exactly as the resting boxes answer it — one walk, one idiom, no second
    // derivation (the contract is at FlagEditorBox::riding_cells). Folding it
    // into `box` would be the wrong union all the same: the caret / text-drag
    // claim seats a caret for ANY press inside `box` and the cursor map shows
    // the I-beam over exactly that rect, so a run folded in would map presses
    // on painted cell ink to payload bytes and promise text
    // editing where none is.
    // WHAT RIDES, BY RANK: every box standing right of the field's own. The
    // UPPER-bound field's rank is the last, so nothing rides under it. THE
    // STORE BELOW IS THE FIELD'S OWN COLUMN since 2026-09-09, when the bound
    // editor became both columns': the payload editor is warp-only by its
    // open gates, and `phase` already answered that question for the box
    // above.
    if (ride_cells) {
        // THE RUN'S SEAM COLUMNS ARE THE MARKER'S CLASS BORDER, never the
        // field's: `face` above may be the RED FLASH, which is a state of the
        // box being typed into and of nothing else, while these boxes keep
        // their resting anatomy — so on a disabled marker their seams stay the
        // damped column its flag carries (each cell's own resolved face
        // carries the same border, the ladder having no per-class variant of
        // it).

        // The cells, off the ONE composer and the ONE measurer the flag pass
        // reads (`ride_text`, composed above), so the re-paint cannot show a
        // different token or a different width from the cell it stands in
        // for. Nothing is shaped where no cell rides (the UPPER field, or a
        // marker with no cells), this branch not running — and where the
        // LOWER field stands it lays out one token that will not paint, the
        // pair being one measurement, which is the same one-run cost the flag
        // pass pays on that marker.
        const IterCellText& cells = ride_text;
        const IterCellLayout cl = measure_iter_cells(font, cells);
        // A TIE FOLLOWER'S RIDING CELLS GREY exactly as its resting ones do
        // (2026-09-19): they are the same boxes at a different x, off the same
        // composer, so the face composes the same term — and it comes from the
        // composer's own answer rather than a second walk of the tie.
        const bool cell_dis = dis || cells.follower;
        const bool ride_lower =
            cl.present && field_rank < flag_box_rank(MarkerCell::Lower);
        const bool ride_upper =
            cl.present && field_rank < flag_box_rank(MarkerCell::Upper);

        // THE RUN'S TWO SEAM COLUMNS, accumulated left to right from the
        // field's own right edge — the same walk the flag pass makes from the
        // flag's, and the same collapse rule: a box that is not in the run
        // leaves its boundary standing where the next one begins, so no point
        // can answer a box with no ink. With everything riding this is exactly
        // the flag pass's `lower_x` / `upper_x` with the field's
        // width in the flag's place.
        const int run_x0 = bx + box_w;
        int cursor_x = run_x0;
        const int lower_seam = cursor_x;
        if (ride_lower) {
            paint_iter_bound_cell(
                cr, lane, cursor_x, cl.lower_w, border_w, edge_h, pad_l,
                baseline, cl.lower_run,
                // Its own column's hue, as the field it rides (architect
                // 2026-09-21).
                resolve_flag_face(cell_dis, red_class,
                                  cell_selected(MarkerCell::Lower),
                                  column_face),
                // Never the run's last box: the upper cell rides after it on
                // every kind that carries the lower.
                /*closes=*/false);
            cursor_x += border_w + cl.lower_w;
        }
        const int upper_seam = cursor_x;
        if (ride_upper) {
            paint_iter_bound_cell(
                cr, lane, cursor_x, cl.upper_w, border_w, edge_h, pad_l,
                baseline, cl.upper_run,
                // Its own column's hue, as the lower cell just above.
                resolve_flag_face(cell_dis, red_class,
                                  cell_selected(MarkerCell::Upper),
                                  column_face),
                // THE RUN'S LAST BOX, so it closes the run (2026-09-25) —
                // the resting run's own ending, at the field's edge.
                /*closes=*/true);
            cursor_x += border_w + cl.upper_w + border_w;
        }

        // THE RUN IS PUBLISHED AS A FLAG HIT RECT, keyed to the marker being
        // edited: its rect is the WHOLE re-painted run's painted extent, every
        // seam divider and the closing column included (the latter past the
        // upper boundary, so it answers Upper, the box it closes) — the same paint-equals-claim rule the flag
        // rects take — and its two boundaries are the seam columns
        // accumulated above, so hit_test_flag_cell's walk answers
        // Upper or Lower over exactly the pixels that show one, and
        // answers
        // nothing at all for a box that stayed behind in the lane pass. No
        // point in the rect can answer Payload: the run begins ON the lower
        // boundary (which, where the lower cell does not ride, is where the
        // first riding box begins), and the payload's own box is the FIELD,
        // published as `box` and claimed by the caret. Nothing is published
        // where nothing rode — the cold marker_index -1 and the zero rect,
        // which contains no point.
        const int run_w = cursor_x - run_x0;
        if (run_w > 0) {
            FlagHitRect& r = out.riding_cells;
            r.marker_index          = idx;
            r.x                     = static_cast<double>(run_x0);
            r.y                     = static_cast<double>(lane.y);
            r.w                     = static_cast<double>(run_w);
            r.h                     = static_cast<double>(lane.h);
            r.iter_lower_boundary_x = static_cast<double>(lower_seam);
            r.iter_upper_boundary_x = static_cast<double>(upper_seam);
            // EVERY CASE IS ONE EXPRESSION, the accumulator having already
            // done the collapsing: under a bound field the boundaries of the
            // boxes that stayed behind sit at the run's own left edge, so no
            // point answers them.
        }
    }

    cairo_restore(cr);   // the font state

    out.valid         = true;
    // THE PUBLISHED BOX IS THE PAINTED BOX, BORDER INCLUDED — the same rule the
    // flags' hit rects take: a press on the border is a press on this editor,
    // and it maps to byte 0 through the nearest-boundary search exactly as a
    // press on the left pad does (the box-is-the-claim clause at FlagEditorBox).
    // A bound field's border is the SEAM DIVIDER and is published with it,
    // so a press on that column seats the caret at byte 0 — which agrees with
    // the resting cell, where the same column reads as that cell rather than
    // as the box left of it.
    //
    // A FIELD THAT CLOSES THE RUN PUBLISHES ITS CLOSING COLUMN TOO
    // (2026-09-25), the box's last column: a press there seats the caret at
    // the end through the same nearest-boundary search a press on the right
    // pad takes, and the I-beam covers every column the field painted.
    out.box           = GuiRect{bx - left_border_w, lane.y,
                                box_w + left_border_w + field_close_w,
                                lane.h};
    out.text_origin_x = text_origin_x;
    out.byte_x        = std::move(byte_x);
}
