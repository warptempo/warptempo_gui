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
    cairo_set_source_rgb(cr, kCanvas.r, kCanvas.g, kCanvas.b);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    // The 1px kLine border, taken FROM the area: the topmost and bottommost
    // pixel rows of the same rect, painted in the same pass as the ground so
    // the two can never disagree about where the area ends. Every band-filling
    // pass that follows clips to waveform_content_rect (the rows between them),
    // so nothing covers the border but the deliberate full-height 1px verticals
    // (playheads, stems). Integer-edged rects with AA off, the crisp-line
    // convention. A degenerate area (h <= 2) draws no border rather than
    // overlapping them.
    if (h > 2) {
        cairo_set_source_rgb(cr, kLine.r, kLine.g, kLine.b);
        cairo_rectangle(cr, x, y, w, 1);
        cairo_rectangle(cr, x, y + h - 1, w, 1);
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
    // statistics discontinuity that the tip segments carry at most one column
    // further. Aesthetic only.
    const auto level_for_column = [&](double src_width) {
        return audio.level_for_span(warp_frame_map ? src_width
                                                   : samples_per_pixel);
    };

    const double y_center = area.y + area.h * 0.5;
    const double half_h   = area.h * 0.5;

    // Each column is written straight into the plate's pixel words. A column
    // reduces to two TIPS (raw max -> top, raw min -> bottom) and paints in two
    // parts, in this order:
    //   1. ITS INTERIOR, if TALL (tips more than kThinIntervalPx apart): opaque
    //      rows between the tips with FRACTIONAL COVERAGE on the two boundary
    //      rows, REPLACE-written into the cleared column. A THIN column has no
    //      interior.
    //   2. ITS TWO CONNECTING SEGMENTS back to the previous column — top tip to
    //      top tip, bottom tip to bottom tip — MAX-composited. These are the
    //      silhouette and the only inter-column connectivity there is; the
    //      raw-to-raw bridge that used to widen intervals into each other is
    //      retired, because it joined a spike to a short neighbour with a hard
    //      block instead of an antialiased slope. A thin column keeps BOTH
    //      segments — its subpixel extent renders as a soft partial-coverage
    //      band, and the two meet only at exactly zero extent.
    // Max-compositing is what makes the order safe: an interior's opaque pixels
    // stay opaque no matter what segment crosses them afterwards.
    //
    // THE PREMULTIPLIED WORD TABLE: one ink colour per render, so the 256
    // possible coverage bytes have only 256 possible words. cairo ARGB32 is a
    // native-endian 32-bit quantity — (A<<24)|(R<<16)|(G<<8)|B written as a
    // uint32_t word is correct on any byte order, which indexing bytes would
    // not be. Channels are PREMULTIPLIED by the coverage, as ARGB32 requires.
    // Entry 255 is the opaque interior word; entry 0 is never written (a
    // zero-coverage row writes nothing at all).
    // THE REGIME THRESHOLD, in screen rows. A column whose own raw min/max spans
    // more than this has envelope mass and fills an interior; at or below it
    // there is no interior and the column's two TIP SEGMENTS are its whole
    // rendering, drawing its subpixel extent as a soft partial-coverage band.
    // Both segments run at any nonzero extent — they reduce to one only when the
    // tips are exactly equal. 1.0 is the natural split (below one pixel there is
    // nothing to fill) and it is the tuning knob if the two regimes ever want to
    // meet somewhere else.
    constexpr double kThinIntervalPx = 1.0;

    const double ink_r = color.r * 255.0;
    const double ink_g = color.g * 255.0;
    const double ink_b = color.b * 255.0;
    uint32_t cov_word[256];
    for (int a = 0; a < 256; ++a) {
        const double f = static_cast<double>(a) / 255.0;
        cov_word[a] =
            (static_cast<uint32_t>(a) << 24) |
            (static_cast<uint32_t>(std::lround(f * ink_r)) << 16) |
            (static_cast<uint32_t>(std::lround(f * ink_g)) <<  8) |
            (static_cast<uint32_t>(std::lround(f * ink_b)));
    }
    const uint32_t opaque_word = cov_word[255];

    // Row bounds: this channel's band, intersected with the surface.
    int y_lo = area.y;
    int y_hi = area.y + area.h;          // exclusive
    if (y_lo < 0)      y_lo = 0;
    if (y_hi > surf_h) y_hi = surf_h;
    if (y_hi <= y_lo) return;

    // COLUMNS THIS CALL OWNS. Every segment deposit is clipped to them, so a
    // halo's share of the offscreen column beyond the range is dropped by
    // construction rather than by happening to fall off the surface — and a
    // partial render, if one is ever reintroduced, cannot bleed into a
    // neighbour's columns.
    int col_lo = area.x;
    int col_hi = area.x + area.w;
    if (col_lo < 0)      col_lo = 0;
    if (col_hi > surf_w) col_hi = surf_w;
    if (col_hi <= col_lo) return;

    // Write one pixel word, REPLACING what is there. Row/column bounds are
    // already established by the envelope callers below; this is their single
    // store site. Replace is correct for them because the caller cleared every
    // column this call regenerates, and each envelope column is written once.
    const auto put = [&](int x, int y, uint32_t word) {
        auto* px = reinterpret_cast<uint32_t*>(
            surf_data + static_cast<size_t>(y) * surf_stride);
        px[x] = word;
    };

    // -- The tip-polyline segment writers ---------------------------------
    //
    // MAX-COVERAGE COMPOSITING, the standard rule for Wu-style overlap, and the
    // rule for EVERY segment write so the regime is uniform. A segment spans two
    // columns, so it necessarily writes into column x-1, which was already
    // rendered — a replace there would punch holes in the neighbour wherever the
    // new coverage is lower. Taking the max instead can only add ink. The
    // existing alpha byte IS the table index that produced the pixel (every word
    // on the plate comes from cov_word), so the read-modify-write needs no
    // separate coverage buffer. Bounds are enforced here, once, for every
    // segment write: a deposit outside the channel band or the surface is
    // dropped rather than clamped, since clamping would pile a segment's tail
    // onto the band edge.
    const auto blend_max = [&](int x, int y, double cov) {
        if (!(cov > 0.0)) return;
        int a = static_cast<int>(std::lround(cov * 255.0));
        if (a <= 0) return;
        if (a > 255) a = 255;
        if (x < col_lo || x >= col_hi) return;
        if (y < y_lo || y >= y_hi) return;
        auto* px = reinterpret_cast<uint32_t*>(
            surf_data + static_cast<size_t>(y) * surf_stride);
        if (a > static_cast<int>(px[x] >> 24)) px[x] = cov_word[a];
    };

    // One unit of coverage at a fractional ROW position, split between the two
    // rows a 1px-thick line centred there would touch. The line spans
    // [y-0.5, y+0.5), so with u = y-0.5 the split is (1-frac) to floor(u) and
    // frac to floor(u)+1 — which correctly puts ALL the ink in one row when the
    // centre sits at that row's midpoint. Total deposited is exactly 1.0.
    //
    // THE CENTRE IS CLAMPED INTO THE BAND FIRST, to [y_lo+0.5, y_hi-0.5]. A tip
    // sitting exactly on a rail is reachable in ordinary material — PCM -1.0
    // maps to the bottom rail exactly — and splitting there would throw half the
    // unit out of band and render the rail row at half alpha. Clamping the
    // centre instead lands the whole unit in the one in-band row, which is what
    // a rail-hugging line should look like. (The steep WALK below does NOT
    // clamp: dropping a diagonal's out-of-band tail is geometrically right, and
    // it can no longer starve a column now that endpoints deposit separately.)
    const auto deposit_v = [&](int x, double y, double weight) {
        const double lo_c = static_cast<double>(y_lo) + 0.5;
        const double hi_c = static_cast<double>(y_hi) - 0.5;
        double cy = y;
        if (cy < lo_c) cy = lo_c;
        if (cy > hi_c) cy = hi_c;
        const double u  = cy - 0.5;
        const double fr = std::floor(u);
        blend_max(x, static_cast<int>(fr),     weight * (1.0 - (u - fr)));
        blend_max(x, static_cast<int>(fr) + 1, weight * (u - fr));
    };
    // The same split on the horizontal axis, for the steep walk below.
    const auto deposit_h = [&](double x, int y, double weight) {
        const double u  = x - 0.5;
        const double fc = std::floor(u);
        blend_max(static_cast<int>(fc),     y, weight * (1.0 - (u - fc)));
        blend_max(static_cast<int>(fc) + 1, y, weight * (u - fc));
    };

    // A Wu-style antialiased segment between two ADJACENT columns' tips. dx is
    // always exactly 1 column; dy is unbounded, since a transient can step
    // hundreds of rows between one column and the next.
    //
    // ENDPOINTS FIRST, ALWAYS: each endpoint column gets a full one-unit
    // vertical split at its own tip. That is not an optimisation — the row walk
    // alone leaves an endpoint at whatever fraction its row phase happens to
    // give, so a V-vertex (a centre with both neighbours just past it) could
    // composite to about half alpha and read as a dropout. Depositing the unit
    // unconditionally also removes the old shallow/steep discontinuity at
    // |dy| == 1: the walk is no longer a different regime, just the extra rows
    // that exist when the segment spans more than one.
    //
    // THEN THE DIAGONAL: when the segment spans more than a row, walk the rows
    // it crosses and split each row's unit between columns x-1 and x by where
    // the segment crosses that row's centre. Every column a segment touches
    // therefore carries at least one full pixel-equivalent at every slope and at
    // the band rails, so flat material softens to a hairline but can never fade
    // out or vanish.
    const auto draw_segment = [&](int xa, double ya, int xb, double yb_) {
        deposit_v(xa, ya,  1.0);
        deposit_v(xb, yb_, 1.0);

        const double dys = yb_ - ya;
        if (std::fabs(dys) <= 1.0) return;   // no in-between rows to walk

        const double x0 = static_cast<double>(xa) + 0.5;
        const double x1 = static_cast<double>(xb) + 0.5;
        // Bound the row walk to the band before iterating — a steep segment can
        // otherwise sweep far more rows than the channel has. The x
        // interpolation still uses the UNCLAMPED endpoints, so the visible part
        // of the segment keeps the true slope.
        double ylo = std::min(ya, yb_);
        double yhi = std::max(ya, yb_);
        if (ylo < static_cast<double>(y_lo)) ylo = static_cast<double>(y_lo);
        if (yhi > static_cast<double>(y_hi)) yhi = static_cast<double>(y_hi);
        if (yhi < ylo) return;
        const int r_first = static_cast<int>(std::floor(ylo));
        const int r_last  = static_cast<int>(std::floor(yhi));
        for (int r = r_first; r <= r_last; ++r) {
            double t = ((static_cast<double>(r) + 0.5) - ya) / dys;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            deposit_h(x0 + t * (x1 - x0), r, 1.0);
        }
    };

    // Global column c's display-domain edge, AS THE LATTICE POINT ITSELF:
    // g(k0+c) = nearbyint((k0+c)*spp), bit-for-bit the integer
    // clamp_viewport_start's grid() lambda produces. THE QUANTIZE LIVES HERE,
    // once, so every consumer — both halos, the loop, and the carried-endpoint
    // chain — receives the same already-rounded lattice point and BOTH VIEWS
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

    // THE TWO HALOS. Both edge columns have a neighbour this call does not draw,
    // and each needs it for the same reason: a drawn column's ink comes from the
    // segments on BOTH its sides, so an edge column missing one is under-covered
    // relative to the same audio rendered interior — and it pops during a pan.
    //   LEFT  (global column col0-1, read before the loop): supplies the PREVIOUS
    //     TIPS the first drawn column's incoming segments run back to.
    //   RIGHT (global column col0+area.w, read after the loop): supplies the tips
    //     the LAST drawn column's outgoing segments run forward to. Only its
    //     share of the last column lands — the deposits aimed at the offscreen
    //     column itself are outside the owned range and dropped.
    // Both are SAMPLE-SPAN reads through this same basis, never read-backs of
    // painted pixels, and both pick their level from their own span exactly as a
    // drawn column does.
    //
    // THE TWO WALL CLAMPS MIRROR. Left, at the song wall: the span is clamped to
    // start at source frame 0, and if nothing remains (hs1 <= hs0 — the viewport
    // sitting at frame 0) the halo is EMPTY and the first column simply has no
    // left neighbour. Right, at the EOF wall: the span is clamped to END at
    // total_frames, and if nothing remains (rs1 <= rs0 — the last column already
    // reaching EOF) that halo is EMPTY too and the last column keeps exactly the
    // ink it has, which is the flush-right rest's contract.
    bool   have_prev  = false;
    // The previous column's TIPS in float rows: its raw maximum mapped to the
    // top tip, its raw minimum to the bottom. These are the only carry the
    // connectivity needs now — the retired bridge's prev_raw_* widen state went
    // with it.
    double prev_top_y = 0.0;
    double prev_bot_y = 0.0;

    // g_prev is the running left edge in SOURCE frames. Seeding it from the halo
    // column's left edge means the carried-endpoint chain (column i's left edge
    // is column i-1's right edge, so each edge is translated once, not twice)
    // simply starts one column earlier in target view.
    const double g_halo0 = to_source(edge_at(static_cast<long long>(col0) - 1));
    double       g_prev  = to_source(edge_at(static_cast<long long>(col0)));
    {
        long long hs0 = static_cast<long long>(std::nearbyint(g_halo0));
        const long long hs1 = static_cast<long long>(std::nearbyint(g_prev));
        if (hs0 < 0) hs0 = 0;
        if (hs1 > hs0) {
            // The halo picks its level from its own span, exactly like a drawn
            // column, so it reads the same statistics its neighbour would.
            const int hlevel = level_for_column(g_prev - g_halo0);
            const auto hm = audio.get_peak_range(channel, hlevel, hs0, hs1);
            prev_top_y = y_center - static_cast<double>(hm.second) * half_h;
            prev_bot_y = y_center - static_cast<double>(hm.first)  * half_h;
            have_prev  = true;
        }
    }

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

        // THE COLUMN'S TIPS: raw maximum -> top tip, raw minimum -> bottom tip,
        // in float rows, never snapped. THIN means the two are within
        // kThinIntervalPx of each other: no envelope mass to fill, so the two
        // tip segments alone render the column, as a soft partial-coverage band
        // across its subpixel extent (they meet only at exactly zero extent).
        // That is the only thing the regime distinction decides now — whether an
        // interior gets filled.
        const double cur_top_y = y_center - raw_max * half_h;
        const double cur_bot_y = y_center - raw_min * half_h;
        const bool   cur_thin  = (cur_bot_y - cur_top_y) <= kThinIntervalPx;

        // 1. INTERIOR FIRST (tall only), REPLACE-written into the cleared
        //    column. Its extent is this column's OWN raw interval: there is no
        //    bridge widening it toward a neighbour any more, so a tall column
        //    next to a short one no longer grows a hard solid block down to it.
        if (!cur_thin && x >= 0 && x < surf_w) {
            double yt = cur_top_y;
            double yb = cur_bot_y;
            // Clamp to this channel's rows before any row index is derived, so
            // a clipped interval cannot address outside the band.
            const double row_lo = static_cast<double>(y_lo);
            const double row_hi = static_cast<double>(y_hi);   // exclusive
            if (yt < row_lo) yt = row_lo;
            if (yb < row_lo) yb = row_lo;
            if (yt > row_hi) yt = row_hi;
            if (yb > row_hi) yb = row_hi;

            const int row_t = static_cast<int>(std::floor(yt));
            const int row_b = static_cast<int>(std::floor(yb));

            // Interior: fully opaque rows strictly between the two edges.
            int i0 = row_t + 1;
            int i1 = row_b - 1;
            if (i0 < y_lo)     i0 = y_lo;
            if (i1 > y_hi - 1) i1 = y_hi - 1;
            for (int y = i0; y <= i1; ++y) put(x, y, opaque_word);

            // Top edge: the fraction of row_t that lies below yt.
            if (row_t >= y_lo && row_t <= y_hi - 1) {
                const double cov = static_cast<double>(row_t + 1) - yt;
                const int    a   = static_cast<int>(std::lround(cov * 255.0));
                if (a > 0) put(x, row_t, cov_word[a < 255 ? a : 255]);
            }
            // Bottom edge: the fraction of row_b that lies above yb.
            if (row_b >= y_lo && row_b <= y_hi - 1) {
                const double cov = yb - static_cast<double>(row_b);
                const int    a   = static_cast<int>(std::lround(cov * 255.0));
                if (a > 0) put(x, row_b, cov_word[a < 255 ? a : 255]);
            }
        }

        // 2. THEN THE TWO TIP SEGMENTS back to the previous column — top tip to
        //    top tip, bottom tip to bottom tip. These are the silhouette, and
        //    they are the ONLY inter-column connectivity at every regime: a
        //    spike beside a short column is joined by two steep antialiased
        //    diagonals rather than the retired bridge's hard block, which is
        //    what puts a soft slope on a spike's SIDES and not just its tip.
        //    A thin column keeps both: they render its subpixel extent as a soft
        //    band, and are skipped down to one only when the tips are EXACTLY
        //    equal (a zero-extent column, where the second traversal would
        //    deposit nothing new).
        //
        //    ORDER AND COMPOSITING: segments run AFTER this column's interior
        //    and MAX-composite, so an opaque interior pixel stays opaque and a
        //    segment can only add ink. That arithmetic — not the paint order —
        //    is what keeps a segment entering an envelope from eroding it.
        if (have_prev) {
            draw_segment(x - 1, prev_top_y, x, cur_top_y);
            // Skip the second traversal only when BOTH endpoints coincide with
            // the first — then it would deposit nothing the top segment has not
            // already deposited. Any nonzero extent at either end still draws
            // both, which is what renders a subpixel column as a band.
            if (prev_bot_y != prev_top_y || cur_bot_y != cur_top_y)
                draw_segment(x - 1, prev_bot_y, x, cur_bot_y);
        } else if (cur_thin) {
            // A thin FIRST column with no halo (the song wall) has no segment
            // to carry its ink, so it deposits its own unit. A tall one needs
            // nothing: its interior is already painted.
            deposit_v(x, cur_top_y, 1.0);
            deposit_v(x, cur_bot_y, 1.0);
        }

        prev_top_y    = cur_top_y;
        prev_bot_y    = cur_bot_y;
        have_prev     = true;
        g_prev        = g1;
    }

    // THE RIGHT HALO (see the halo contract above): the last drawn column's
    // outgoing segments, without which it carries only half the ink an interior
    // column gets from the same audio and shifts under a pan. The carried-edge
    // chain already left g_prev at the last column's RIGHT edge — global column
    // col0+area.w's left edge — so this costs one more map call in target view.
    // Only the deposits landing on the last drawn column survive the owned-column
    // clip; the ones aimed at the offscreen column are dropped.
    if (have_prev) {
        const double rg0 = g_prev;
        const double rg1 =
            to_source(edge_at(static_cast<long long>(col0) + area.w + 1));
        const long long rs0 = static_cast<long long>(std::nearbyint(rg0));
        long long       rs1 = static_cast<long long>(std::nearbyint(rg1));
        // EOF-wall clamp, the mirror of the song wall's clamp-to-0: a span that
        // begins at or past total has nothing to read, so rs1 <= rs0 and the
        // halo is empty.
        const long long total = static_cast<long long>(audio.total_frames());
        if (rs1 > total) rs1 = total;
        if (rs1 > rs0) {
            const int rlevel = level_for_column(rg1 - rg0);
            const auto rm = audio.get_peak_range(channel, rlevel, rs0, rs1);
            const double rtop = y_center - static_cast<double>(rm.second) * half_h;
            const double rbot = y_center - static_cast<double>(rm.first)  * half_h;
            const int    xl   = area.x + area.w - 1;
            draw_segment(xl, prev_top_y, xl + 1, rtop);
            if (prev_bot_y != prev_top_y || rbot != rtop)
                draw_segment(xl, prev_bot_y, xl + 1, rbot);
        }
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

    // Inverted-triangle indicator: stamped from the cached ANTIALIASED A8 mask
    // (playhead_triangle_mask()) so the per-frame playhead redraw is a cheap
    // blit with the slope edge alphas already baked in. The mask is 2H-1 x H
    // (odd width) with the tip at column index H-1 (image-local); integer
    // division places that tip column at `area.x + col`. The triangle sits in
    // the TRIANGLE LANE — dst_y is the LANE RECT's top (`triangle_lane.y`, from
    // top_triangle_row_area), not `area.y - H` re-derived from the waveform top
    // edge, so the stamp follows the lane the accessors report wherever the
    // strip's gaps put it. Its top row is the lane top and its tip (bottom row)
    // lands one pixel above the waveform top edge, where the marker/trim stems
    // begin, because the lane stack rests that lane flush on the waveform; the
    // mask height equals the lane height by construction (both are
    // playhead_triangle_h_px()). This is the
    // same width and centered column as every marker/trim flag triangle, so when
    // the cursor sits on a marker the two coincide. Skipped for the scanner call
    // (draw_triangle=false): the triangle belongs to the cursor exclusively.
    // The clip band is the triangle lane; the vertical
    // line above spans only the waveform area, so the two never overlap.
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
    cairo_set_source_rgb(cr, kStripAnchorStem.r, kStripAnchorStem.g,
                         kStripAnchorStem.b);
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

void render_trim_stems(cairo_t* cr,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim) {
    if (waveform_area.w <= 0 || waveform_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;

    const double span = static_cast<double>(viewport_end_sample -
                                            viewport_start_sample);
    const double samples_per_pixel = span / static_cast<double>(waveform_area.w);
    if (samples_per_pixel <= 0.0) return;

    // Stem geometry: the trim stem spans the waveform area, top at
    // waveform_area.y (where its b/e chip's structure ends above) down to the
    // waveform bottom — the same span the marker stems (paint_marker_stems)
    // use.
    const double y_stem_top = static_cast<double>(waveform_area.y);
    const double y1 = static_cast<double>(waveform_area.y + waveform_area.h);

    cairo_save(cr);
    cairo_set_line_width(cr, 1.0);

    auto paint_bound = [&](int64_t frame) {
        // `frame` is already the displayed-domain position (the live trim pass
        // pre-mapped it through displayed_trim_ms — the hit sites' mapping
        // owner). Column + visibility come from the shared resolver; the
        // clamped column keeps the waveform stem, strip stem, and chip on one
        // column (the EOF-wall clamp — see trim_bound_column).
        const TrimBoundColumn c = trim_bound_column(
            static_cast<double>(frame), viewport_start_sample,
            viewport_end_sample, waveform_area.w);
        if (!c.in_viewport) return;
        cairo_set_source_rgb(cr, kTrimStem.r, kTrimStem.g, kTrimStem.b);
        const double x_px = waveform_area.x + c.col + 0.5;
        cairo_move_to(cr, x_px, y_stem_top);
        cairo_line_to(cr, x_px, y1);
        cairo_stroke(cr);
    };

    // Both bounds always paint: the window is always set (2026-07-30), so the
    // per-bound has-gates are gone. At the full window the stems stand on the
    // song edges.
    paint_bound(trim.begin);
    paint_bound(trim.end);

    cairo_restore(cr);
}

// THE TRIM LANE (top lane 4, row 5 of the redesign). The b/e CHIPS and the
// bridge bar are gone: the lane is now one continuous BAR spanning the trim
// window with a 2px ENDCAP standing at each bound, on kdenlive's own anatomy
// (row_5_lane_1_trim_*.png).
//
// THREE SURFACES, NINE ROWS, AND A BEVEL. Every column of the lane belongs to
// exactly one of three surfaces — ground outside the window, bar inside it,
// endcap at a bound — and each paints its own color in rows 0..6 and its OWN
// two-row bevel in rows 7 and 8 (a lighter shade then a darker one). The bevel
// is not derivable: the three measured pairs fit neither a constant delta nor a
// constant mix toward white or black, so the six constants are sampled and a
// fourth surface would force the question rather than inherit a wrong formula
// (the record is at their declaration in render.h).
//
// NO CENTER GRIP — ruled off. The old bridge bar's bright kTrimBar/kTrimBarOutline
// pair and the square chips retire with this painter; their colors.conf keys
// stay in the tree and simply go inert, per the standing shrink ruling.
//
// PAINT AND HIT ARE ONE OWNER: the endcaps below come from trim_chip_rect, the
// same rect hit_test_trim_chip reads (inflated there by its stated grab
// tolerance, the lane's one deliberate paint/hit difference), and the bar's
// grabbable span is trim_bridge_gap measured between those same caps. The
// routing is otherwise unchanged from the chip row — the bound drags, the
// bridge drag, the ctrl / ctrl+shift bound sets, the framing double-click seed
// and every read-only refusal all kept their bodies and only changed geometry.
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
    const double editable_left = editor_text_glyph0_x(s.anchor_x, s.prefix);

    cairo_text_extents_t text_ext;
    cairo_text_extents(cr, s.text.c_str(), &text_ext);

    // The step-1 fill box fills its whole BOX rect rather than the tight glyph
    // bounding box — that geometry lives entirely inside flag_chip_rect (height
    // = monospace_text_box_h(), top = the lane top the baseline offset recovers,
    // inset downward by text_box_margin_px()), so baseline_y sits centered in
    // the box and the box sits centered in its lane with a clear margin above
    // and below. Callers solve baseline_y as lane.y +
    // monospace_text_row_baseline_offset(); the box lands inside that lane.
    //
    // The cursor (step 5) and the selection highlight (step 4) span exactly the
    // glyph ink band (ascent-to-descent), no vertical padding. The band is
    // recovered by INVERTING the BOX formula, term for term — never the LANE's,
    // which carries a margin the box does not: the lane-top baseline offset is
    // text_box_margin_px() + text_box_pad_px() + flag_pad_y_px() +
    // kChipOutlinePx + ascent, and the box height is 2*text_box_pad_px() +
    // nearbyint(font_height + 2*flag_pad_y_px()) + 2*kChipOutlinePx — so every
    // pad and margin the metrics added comes back off here. Taking bg_h from the
    // LANE instead would inflate the derived font_height by both margins and
    // mis-span the band. These three lines MUST move with the box/lane formulas.
    // The nearbyint on the box height can leak a sub-pixel into the derived
    // descent; that is cosmetically irrelevant here and saves adding a new
    // metric accessor.
    const double bg_h        = static_cast<double>(monospace_text_box_h());
    const double ascent      = monospace_text_row_baseline_offset()
                             - text_box_margin_px() - text_box_pad_px()
                             - flag_pad_y_px() - kChipOutlinePx;
    const double font_height = bg_h - 2.0 * text_box_pad_px()
                             - 2.0 * flag_pad_y_px()
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
    // The band is CLAMPED to the BOX's fill interior [fr.y + kChipOutlinePx,
    // fr.y + fr.h - kChipOutlinePx] so a cursor/highlight rect can never punch
    // through the ring top or bottom (the standing rule) — and, since fr is the
    // margin-inset box, never into the lane's margin either. text_box_pad_px()
    // cancels flag_pad_y_px() exactly, so the band lands ON the interior edges
    // and the clamp is a no-op whenever the box height rounds cleanly; it stays
    // as the guard for the sub-pixel the box's own nearbyint can leak at other
    // font sizes. The antialiased glyph text and the selection substring repaint are
    // NOT clamped — their extreme leading rows are blank, the ring paints
    // first, and only these filled rects could otherwise show through it.
    // std::nearbyint, the project's one fractional->integer pixel conversion.
    int band_y0 = static_cast<int>(std::nearbyint(glyph_top));
    int band_y1 = static_cast<int>(std::nearbyint(glyph_top + glyph_h));
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
        // std::nearbyint, the project's one fractional->integer pixel
        // conversion, the same one the band rows above take.
        const int hx0 = static_cast<int>(std::nearbyint(hi_x));
        const int hx1 = static_cast<int>(std::nearbyint(hi_x + hi_w));
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
    //    not the full step-1 slot. cur_col is the nearbyint'd column; the
    //    former round(x)+0.5 half-pixel was a stroke-aliasing device, unneeded
    //    for a filled integer rectangle.
    if (s.cursor_visible) {
        const double cursor_x_offset = s.cursor_pos * monospace_advance();
        // An integer one-pixel rectangle at cur_col occupies exactly the
        // cursor column with AA off; the former round(x)+0.5 half-pixel was a
        // stroke-aliasing device and is no longer needed. std::nearbyint, the
        // project's one fractional->integer pixel conversion.
        const int cur_col =
            static_cast<int>(std::nearbyint(editable_left + cursor_x_offset));
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
    // into it (by up to a full box width) while a marker at or past the right
    // edge can show nothing at all. The left margin is the width BOUND
    // (marker_flag_max_width_px) rather than the real width, which is not known
    // until the label is shaped — a bound over-admits a handful of offscreen
    // markers per frame and never drops a visible one.
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
        if (ms > cull_hi) continue;

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
// default pair with selection swapping it for the bright one.
//
// THE DISABLED FACE'S LABEL DIMS AGAINST THE FLAG, NOT AGAINST THE LANE. Every
// SHAPE surface takes 25% of itself over the lane ground, as ruled. The LABEL
// takes the same 25%-of-itself through the same mix_color owner but toward the
// surface it actually sits on — the already-blended fill — because that is what
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
        // The class the marker WOULD paint, blended. Red keeps its own hue
        // through the blend rather than collapsing to the default one, so a
        // disabled red marker is still recognisably red.
        const GuiColor base_fill = red ? kMarkerFlagFillRed : kMarkerFlagFill;
        const GuiColor base_edge = red ? kMarkerFlagEdgeRed : kMarkerFlagEdge;
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
    const DragOverlay* drag_overlay) {
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

            // Box then top edge, both AA-off so the 1px band is exactly one
            // row and the box's sides are exactly one column.
            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            cairo_set_source_rgb(cr, face.fill.r, face.fill.g, face.fill.b);
            cairo_rectangle(cr, bx, lane.y, bw, lane.h);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, face.edge.r, face.edge.g, face.edge.b);
            cairo_rectangle(cr, bx, lane.y, bw, edge_h);
            cairo_fill(cr);
            cairo_restore(cr);

            // The label, on the run just measured — same font, same glyphs, so
            // the box width and the painted text cannot disagree.
            cairo_set_source_rgb(cr, face.label.r, face.label.g, face.label.b);
            text_shape::show_shaped_run(
                cr, run, static_cast<double>(bx + pad_l), baseline);

            if (out_hit_rects) {
                FlagHitRect r;
                r.marker_index = i;
                r.x = static_cast<double>(bx);
                r.y = static_cast<double>(lane.y);
                r.w = static_cast<double>(bw);
                r.h = static_cast<double>(lane.h);
                out_hit_rects->push_back(r);
            }
            if (out_stems && face.has_stem) {
                out_stems->push_back(
                    MarkerStem{static_cast<double>(bx), face.stem});
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
                  const DragOverlay* drag_overlay) {
    render_flag_boxes_impl(
        cr, top_strip_area, lanes, waveform_width, markers,
        viewport_start_sample, viewport_end_sample, sample_rate,
        selected_set, red_set,
        // The ONE composer the flag paint, the editor seed and the copy payload
        // all share, so a flag shows exactly what its editor would open with.
        [&](int i) { return flag_text_iter(markers, i, iteration_on); },
        // The warp column's disabled verdict follows the label_ref cascade.
        [&](int i) { return effective_disabled(markers, i); },
        out_hit_rects, out_stems, warp_frame_map, drag_overlay);
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
        out_hit_rects, out_stems, warp_frame_map, drag_overlay);
}

namespace {
    // Current GUI font size, in points. Set by set_gui_font_size_pt from
    // the two application points (file load, the settings-editor font_size
    // commit); every derived pixel quantity (text px size, scale
    // factor, scaled pads, triangle height) reads it through the accessors
    // below.
    double g_font_size_pt = kDefaultFontSizePt;
    // Current GUI scale, in PERCENT. Set by set_gui_scale_percent from the SAME
    // three application points that push the font size (file load, the settings
    // editor's gui_scale commit, the adopt). The redesigned rows' pixel
    // quantities read it through gui_scale_factor(); the monospace surfaces do
    // not — the two axes are independent (the ruling is at the accessor's
    // declaration in render.h).
    int    g_gui_scale_percent = 100;
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
void   set_gui_scale_percent(int percent) { g_gui_scale_percent = percent; }
double gui_scale_factor()  {
    return static_cast<double>(g_gui_scale_percent) / 100.0;
}
double flag_font_size_px() { return g_font_size_pt * 96.0 / 72.0; }

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

double monospace_advance() { return g_advance; }

// g_row_h / g_row_baseline_off — the measured UNPADDED glyph slot and its
// baseline — are deliberately accessor-less: nothing outside this file has any
// use for the bare slot, so the box/lane accessors below read the globals
// directly and the slot never becomes a metric a lane could take by mistake.
//
// The text BOX: that measured slot plus kTextBoxPadPx on each side.
// The text LANE: that box plus kTextBoxMarginPx on each side — the margin is
// empty lane outside the ring, so the lane is strictly taller than the box.
// The lane-top baseline offset carries both terms, since it is measured from
// the LANE top while the glyphs sit in the box the margin pushed down. All
// three are derived rather than cached so a font_size change needs no second
// measure.
int monospace_text_box_h() {
    return g_row_h + 2 * static_cast<int>(text_box_pad_px());
}
int monospace_text_row_h() {
    return monospace_text_box_h() + 2 * static_cast<int>(text_box_margin_px());
}
double monospace_text_row_baseline_offset() {
    return g_row_baseline_off + text_box_pad_px() + text_box_margin_px();
}

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
    // The UNPADDED pair, which no lane takes: the text BOX adds kTextBoxPadPx
    // per side on top of it (monospace_text_box_h) and the text LANE another
    // kTextBoxMarginPx per side (monospace_text_row_h). The outline ring sits
    // outside the padding, so both formulas add 2*kChipOutlinePx /
    // kChipOutlinePx: the slot is font_height + 2*flag_pad_y_px() +
    // 2*kChipOutlinePx tall, and the baseline drops by flag_pad_y_px() +
    // kChipOutlinePx + ascent from the top.
    g_row_h = static_cast<int>(std::nearbyint(
        font_height + 2.0 * flag_pad_y_px())) + 2 * kChipOutlinePx;
    g_row_baseline_off = flag_pad_y_px() + kChipOutlinePx + fe.ascent;
    cairo_restore(cr);
    g_measured_font_px = px;
}

double measured_monospace_font_px() { return g_measured_font_px; }

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

    const int pad_l  = marker_flag_pad_left_px();
    const int pad_r  = marker_flag_pad_right_px();
    const int edge_h = marker_flag_edge_h_px();
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

    // 1. The box: fill, then the 1px top edge — AA off, exactly as a flag.
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
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
    //    the monospace box already uses: the selected span fills with the label
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
    //    monospace box uses, on the shaped position instead of a grid one.
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
    out.box           = GuiRect{bx, lane.y, box_w, lane.h};
    out.text_origin_x = text_origin_x;
    out.byte_x        = std::move(byte_x);
}
