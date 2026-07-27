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

// Fills and outlines ONE marker/phase-reset/trim flag SHAPE at `center_x` (the
// item's pixel column). `anchor` places the rectangle relative to that column:
// Center (markers/phase resets — straddles the column, may hang half offscreen)
// or LeftEdge (rect left column ON `center_x`). Trim chips call with LeftEdge and
// `center_x` = the edge-anchored rect left from trim_chip_rect (which owns the
// begin/end edge asymmetry), so a chip's begin/end handedness lives THERE, not
// here. The shape is the fixed-width rectangle
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
// triangle (Ableton's loop bounds carry none). The shape is always OPAQUE: a
// disabled marker is a color class (the kMarkerDisabled pair its caller
// resolves), not a faded one, so the cairo-group dim this once took an `alpha`
// for is gone. The triangle is the identical geometry the cached playhead mask
// stamps, so a flag's triangle and the playhead's coincide when the cursor sits
// on it.
// Horizontal anchor of the shape's rectangle relative to `center_x`'s column.
// Center is the marker-flag default (and the playhead triangle) — straddles the
// column and may hang half offscreen. LeftEdge puts the rect's LEFT column ON
// `center_x`; it serves the trim chips, which pass the already-edge-anchored
// rect left from trim_chip_rect (the begin/end asymmetry owner), so both begin
// and end chips are painted LeftEdge at their resolved rect.x.
enum class FlagHAnchor { Center, LeftEdge };

void paint_flag_shape(cairo_t* cr, double center_x,
                      double flag_top_d, double tri_top_d, double tip_y_d,
                      GuiColor fill, GuiColor outline,
                      bool with_triangle,
                      FlagHAnchor anchor = FlagHAnchor::Center) {
    const int flag_w = flag_lane_w_px();

    const int cx     = static_cast<int>(std::round(center_x));
    // Rect left per anchor: Center straddles the column; LeftEdge puts the
    // rect's left column ON it. cx is otherwise the triangle apex (Center only).
    const int rx =
        anchor == FlagHAnchor::LeftEdge ? cx
      :                                   cx - flag_w / 2;   // rect left
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

// -- Trim bound geometry owners (audit C1) -------------------------------
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
    const int flag_w = flag_lane_w_px();
    const int abs_col = strip_x + col;
    GuiRect r;
    // Begin left-edge-anchored (rect left ON the column); end right-edge-anchored
    // (rightmost pixel ON the column). Y-band from the chip lane `row`.
    r.x = is_begin ? abs_col : abs_col - flag_w + 1;
    r.y = row.y;
    r.w = flag_w;
    r.h = row.h;
    return r;
}

void render_trim_stems(cairo_t* cr,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim,
                       bool has_begin,
                       bool has_end) {
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

    // Trim chip lane (top-strip lane 0): the edge-most lane, so its top is the
    // outer gap kFlagBottomLiftPx inward from the strip's top, and its height is
    // the flag height. Screen
    // and top-strip-local coords coincide (the top strip sits at y=0), so this
    // is exactly top_upper_row_area(app), the band the bridge hit test gates on.
    const int chip_w    = flag_lane_w_px();
    const int chip_top  = top_strip_area.y + static_cast<int>(kFlagBottomLiftPx);
    const int chip_h    = flag_lane_h_px();
    const int chip_bottom = chip_top + chip_h;
    // Waveform top edge in this (top-strip-local) coord system: the strip sits
    // at y=0, so it is the strip's own height. The strip-crossing stem segment
    // painted below runs from the chip's bottom edge down to here, where the
    // waveform-area trim stem (render_trim_stems) continues to the waveform
    // bottom — one unbroken 1px line at the bound column.
    const int wave_top = top_strip_area.y + top_strip_area.h;

    // One shared resolver call per bound (frames pre-mapped by the live trim
    // pass through displayed_trim_ms), read four ways: .col (clamped), .col_raw
    // (unclamped, the offscreen sentinel input), .in_viewport (visibility), and
    // .side (offscreen left/right, the sentinel selector — trim_bridge_gap).
    // Computed unconditionally so the bridge bar spans between the bounds even
    // when a chip is culled; chips and their stems draw only for a visible bound.
    const TrimBoundColumn bc = trim_bound_column(
        static_cast<double>(trim.begin), viewport_start_sample,
        viewport_end_sample, waveform_area.w);
    const TrimBoundColumn ec = trim_bound_column(
        static_cast<double>(trim.end), viewport_start_sample,
        viewport_end_sample, waveform_area.w);
    const int begin_col = bc.col;
    const int end_col   = ec.col;

    cairo_save(cr);

    // With both bounds set, the BRIDGE BAR fills the trim-chip-lane GAP between
    // the two edge-anchored chips: from the begin chip's inner (right) edge to the
    // end chip's inner (left) edge. The chips edge-anchor ON their bound columns
    // (begin left-edge, end right-edge, bodies facing inward), so this gap is the
    // span the pair (bridge) drag grabs — route_trim_chip_press tests the SAME gap
    // interval (the shared trim_bridge_gap owner below), so the clickable band is
    // exactly the painted bar. The bar carries the trim family's BRIGHT pair — an
    // opaque kTrimBar fill with a 1px kTrimBarOutline ring — because it is the
    // sole "this is the trim window" signal; the chips beside it stay calm.
    // Columns are computed
    // unconditionally (a chip's viewport cull must not suppress the band). A gap
    // exists only when the begin chip sits fully left of the end chip
    // (wide-enough, non-inverted span); an inverted or narrow trim shows no
    // bridge (trim_bridge_gap returns hi <= lo), its chips simply overlap.
    //
    // Offscreen-border rule: a gap edge follows its chip offscreen. The gap
    // interval comes from the shared owner trim_bridge_gap (the SAME owner
    // route_trim_chip_press' bridge hit consumes, so paint and hit cannot drift):
    // an in_viewport bound bounds the gap at its drawn chip's inner edge; an
    // OFFSCREEN bound (no chip) runs the bar FLUSH via a side-specific sentinel
    // (keyed on the bound's SIDE, not col_raw — the rounding seam), pushing that
    // edge STRICTLY past the visible range. The interval is RAW; the PAINTER clips
    // the DRAWN extent to the effective width [0, wave_w) (below) — the inert
    // gutter never paints, so paint == hit exactly. The flush fill therefore stops
    // at the edge with no chip-width gap (even a BARELY-offscreen bound), and an
    // offscreen side's ring border is not drawn at all (its raw edge column is
    // outside [0, wave_w)), so it goes offscreen with the absent chip rather than
    // floating in a blank strip or the gutter. An end bound at EOF (T-1) stays
    // in_viewport, so it uses the clamped column (a ~1px seam vs the raw column,
    // accepted), preserving the visible EOF chip's connection.
    if (has_begin && has_end && waveform_area.w > 0) {
        const TrimBridgeGap gap =
            trim_bridge_gap(bc, ec, chip_w, waveform_area.w);
        // The gap interval is RAW (offscreen sentinels intact — they carry the
        // border semantics); the PAINTER is the visible-interval clip owner. Clip
        // the DRAWN extent to the EFFECTIVE width [0, wave_w): only strip
        // backgrounds and damage use the full top_strip.w, so the inert
        // non-multiple-of-16 gutter [wave_w, strip_w) must NOT paint — this makes
        // paint == hit exact (the router's [0, area_w) gate refuses the gutter).
        const int clip_lo = std::max(gap.lo, 0);
        const int clip_hi = std::min(gap.hi, waveform_area.w);
        if (clip_hi > clip_lo) {
            cairo_set_source_rgb(cr, kTrimBar.r, kTrimBar.g, kTrimBar.b);
            cairo_rectangle(cr, static_cast<double>(top_strip_area.x + clip_lo),
                            static_cast<double>(chip_top),
                            static_cast<double>(clip_hi - clip_lo),
                            static_cast<double>(chip_h));
            cairo_fill(cr);
            // 1px ring, AA off. The top/bottom runs span the CLIPPED width; the
            // side borders draw at the RAW gap edge columns (gap.lo, gap.hi-1) and
            // ONLY when that column lies in [0, wave_w) — an offscreen side edge
            // draws NO border (it goes offscreen with the absent chip), never a
            // floating border pinned to the clip boundary.
            const int rx = top_strip_area.x + clip_lo;
            const int rw = clip_hi - clip_lo;
            const int left_col  = gap.lo;
            const int right_col = gap.hi - 1;
            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            cairo_set_source_rgb(cr, kTrimBarOutline.r, kTrimBarOutline.g,
                                 kTrimBarOutline.b);
            cairo_rectangle(cr, rx, chip_top, rw, 1);              // top
            cairo_rectangle(cr, rx, chip_top + chip_h - 1, rw, 1); // bottom
            if (left_col >= 0 && left_col < waveform_area.w)
                cairo_rectangle(cr, top_strip_area.x + left_col,
                                chip_top, 1, chip_h);              // left
            if (right_col >= 0 && right_col < waveform_area.w)
                cairo_rectangle(cr, top_strip_area.x + right_col,
                                chip_top, 1, chip_h);              // right
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
    // AA off (axis-aligned, +0.5), kTrimStem — the same stem color the
    // waveform-side segment takes, so the joined line is one color end to end.
    {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, kTrimStem.r, kTrimStem.g, kTrimStem.b);
        cairo_set_line_width(cr, 1.0);
        auto paint_strip_stem = [&](const TrimBoundColumn& c) {
            if (!c.in_viewport) return;
            const double x_px =
                static_cast<double>(top_strip_area.x + c.col) + 0.5;
            cairo_move_to(cr, x_px, static_cast<double>(chip_bottom));
            cairo_line_to(cr, x_px, static_cast<double>(wave_top));
            cairo_stroke(cr);
        };
        if (has_begin) paint_strip_stem(bc);
        if (has_end)   paint_strip_stem(ec);
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
    if (has_begin && bc.in_viewport)
        chips.push_back({begin_col, true});
    if (has_end && ec.in_viewport)
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
    // Each chip is a plain kTrimChip rectangle with a kTrimChipOutline border,
    // no triangle; the bottom argument is unused for the rectangle shape. The
    // calm pair, not the bar's bright one — the chips are the handles, the bar
    // between them is the window.
    const GuiRect chip_row{top_strip_area.x, chip_top, waveform_area.w, chip_h};
    for (auto it = chips.rbegin(); it != chips.rend(); ++it) {
        // The chip rect (edge-anchored begin/end) comes from the shared owner
        // trim_chip_rect — the SAME rule hit_test_trim_chip uses. paint_flag_shape
        // then draws the rectangle with its left column ON rect.x (LeftEdge means
        // "rect left = center_x"), so the begin/end asymmetry lives only in
        // trim_chip_rect. chip_bottom is the tri-top row (no triangle here).
        const GuiRect r =
            trim_chip_rect(it->is_begin, top_strip_area.x, it->col, chip_row);
        paint_flag_shape(cr, static_cast<double>(r.x),
                         static_cast<double>(chip_top),
                         static_cast<double>(chip_bottom),
                         static_cast<double>(chip_bottom),
                         kTrimChip, kTrimChipOutline,
                         /*with_triangle=*/false,
                         FlagHAnchor::LeftEdge);
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
        // THREE color classes, resolved in priority order: DISABLED wins over
        // red and default, red over default. Each is one opaque fill/outline
        // pair and nothing composes with anything — a disabled marker no longer
        // combines with a color class, it IS one. SELECTION CONTRIBUTES NO
        // COLOR: it only orders the two paint passes below (selected shapes
        // above unselected), so a selected flag paints exactly the pair it would
        // paint unselected. That is why `red` tests only `!dis` — a selected
        // marker whose render normalizes to 1.00 keeps its red cue rather than
        // having it masked by a selection color.
        const bool dis = effective_disabled(markers, e.i);
        const bool red = !dis && red_set.count(e.i) > 0;
        const GuiColor fill    = dis ? kMarkerDisabled
                               : red ? kAccent : kMarker;
        const GuiColor outline = dis ? kMarkerDisabledOutline
                               : red ? kAccentOutline : kMarkerOutline;
        paint_flag_shape(cr, e.center_x, g.flag_top, g.tri_top, g.tip_y,
                         fill, outline, /*with_triangle=*/true);
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
        // The identical three-class ladder render_flags resolves: disabled wins
        // over red, red over default, and selection contributes no color at all
        // (it only orders the two paint passes below), so `red` tests only
        // `!dis` and a selected reset keeps its normalization cue. A phase reset
        // carries no label_ref cascade, so its disabled verdict is the bool
        // itself.
        const bool dis = phase_resets[e.i].disabled;
        const bool red = !dis && red_set.count(e.i) > 0;
        const GuiColor fill    = dis ? kMarkerDisabled
                               : red ? kAccent : kMarker;
        const GuiColor outline = dis ? kMarkerDisabledOutline
                               : red ? kAccentOutline : kMarkerOutline;
        paint_flag_shape(cr, e.center_x, g.flag_top, g.tri_top, g.tip_y,
                         fill, outline, /*with_triangle=*/true);
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
    // math. BASIS CONTRACT: the lane run annotates painted flag pixels, so it
    // must read the SAME basis those pixels were painted with — BOTH halves of
    // that basis. (1) The displayed MAP (displayed_or_live_target_map): the
    // event-synchronized map the hit tests use — identity/empty in source view;
    // in target view the map the last committed frame's flag cache baked. (2) The
    // displayed VIEWPORT (displayed_viewport_basis): the vp_start/spp the same
    // committed frame's flag cache baked, NOT the live viewport. Reading the live
    // map OR the live viewport would center the run on the NEW column during an
    // async republish while the flag still paints at the OLD one, so the run would
    // visibly jump off its flag until the worker caught up (a real improvement for
    // the fallback run's mid-follow-scroll centering, not an accident). The frame
    // is the marker's authored source frame; both marker columns translate through
    // this same map.
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const DisplayedViewportBasis basis = displayed_viewport_basis(app, audio);
    const int col = painted_column_of_source_frame_on_basis(
        app, audio, source_frame, map, basis.vp_start, basis.spp);
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

namespace {

// The 9-glyph ambient budget: the "N.NN:a.NN" base form. A composed value longer
// than 9 glyphs displays as its first 8 bytes plus the UTF-8 ellipsis (U+2026 =
// "\xe2\x80\xa6"), 11 bytes / 9 glyphs. Composed text is ASCII by construction
// (printable-ASCII keyboard insert, lowercase-ASCII label grammar), so 8 bytes
// == 8 glyphs and the ellipsis is a collision-free truncation marker no
// clipboard route can author. DISPLAY-ONLY: the store, sidecars, editor seed,
// hover fields, and the copy payload never see it. Sets `glyphs` to the display
// glyph count.
constexpr size_t kLaneAmbientGlyphBudget = 9;

void cap_lane_run_text(std::string& text, size_t& glyphs) {
    if (text.size() > kLaneAmbientGlyphBudget) {
        text = text.substr(0, 8) + "\xe2\x80\xa6";
        glyphs = kLaneAmbientGlyphBudget;
    } else {
        glyphs = text.size();
    }
}

// The FALLBACK single run — the one-run arbitration: tier 1 the HOVERED marker's
// value, else tier 2 the LAST-SELECTED marker's value composed from the live
// store, with the mid-drag DragOverlay substitution and the painted-column
// offscreen cull the flags apply. TRUNCATION IS PERMANENT (architect): the
// fallback run CAPS at the 9-glyph budget too, so a dragged/selected truncated
// marker stays truncated here — it does NOT spell out in full when the verdict
// fails. The text-hover EXPANSION (applied to the returned set) is the sole
// route back to the full text, and only while the run's TEXT is hovered.
LaneTextRun current_marker_lane_run_fallback(const AppState& app,
                                             const GuiAudio& audio) {
    LaneTextRun run;

    // Tier 1: the HOVERED marker's own value wins whenever a hover is showing.
    // recompute_hover_at_cursor already composed lane_text (flag_text_iter for a
    // warp marker, kPhaseResetLaneToken for a phase reset) and captured the hovered
    // marker's index and source_frame. No painted-column cull here — matching
    // paint, a shown hover always paints (subject only to the caller's advance
    // guard).
    if (!app.hover_popup.lane_text.empty()) {
        run.valid        = true;
        run.marker_index = app.hover_popup.marker_index;
        run.source_frame = static_cast<double>(app.hover_popup.source_frame);
        run.text         = app.hover_popup.lane_text;
        cap_lane_run_text(run.text, run.glyphs);
        return run;
    }

    // Tier 2: else the LAST-SELECTED marker's own value, composed from the live
    // store the same way the hover composer does — flag_text_iter for a warp
    // marker, kPhaseResetLaneToken for a phase reset. The index is validated
    // against the active view's list.
    const int idx = app.last_selected_marker;
    if (idx < 0) return run;
    int64_t     src_f;
    std::string txt;
    if (app.active_markers_view == 'P') {
        const auto& pv = app.phaseresetmarkers.markers();
        if (idx >= static_cast<int>(pv.size())) return run;
        src_f = pv[idx].time_frame;
        txt   = kPhaseResetLaneToken;
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
    // (a fully-offscreen marker paints no flag, so it shows no run). The basis is
    // the displayed MAP and the displayed VIEWPORT, the same pair the flag/lane
    // painters and hit tests use.
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const DisplayedViewportBasis basis = displayed_viewport_basis(app, audio);
    const int col = painted_column_of_source_frame_on_basis(
        app, audio, display_src_f, map, basis.vp_start, basis.spp);
    if (col < 0 || col >= waveform_area(app).w) return run;

    run.valid        = true;
    run.marker_index = idx;
    run.source_frame = display_src_f;
    run.text         = std::move(txt);
    cap_lane_run_text(run.text, run.glyphs);
    return run;
}

// Wrap the fallback single run in a LaneRunSet (all_visible = false).
LaneRunSet lane_run_set_fallback(const AppState& app, const GuiAudio& audio) {
    LaneRunSet set;
    set.all_visible = false;
    const LaneTextRun run = current_marker_lane_run_fallback(app, audio);
    if (run.valid) set.runs.push_back(run);
    return set;
}

// THE TEXT-HOVER EXPANSION (applied to the base set in BOTH modes): when the
// pointer hovers a marker's rendered TEXT RUN — not its flag (hover_popup.on_flag
// == false) — and that marker's FULL composed text (hover_popup.lane_text, which
// the recompose keeps uncapped) exceeds the budget, expand that run to the full
// text. The expanded run reuses its ambient run's source_frame so it centers on
// the SAME column (the expanded rect always CONTAINS the capped one — both center
// on that column and the expanded is wider — so a pointer in the capped rect is
// in the expanded rect, the hysteresis latch the recompute convergence relies
// on). It paints LAST / hits FIRST (LaneRunSet contract). The verdict is already
// decided on the capped widths; this never touches it. kPhaseResetLaneToken is
// well under the budget, so a reset never reaches this.
// Only expands a marker actually IN the base set (onscreen)
// — a hover is always over an onscreen run, but the search also fixes the exact
// centering frame.
void apply_hover_expansion(LaneRunSet& set, const AppState& app) {
    if (app.hover_popup.on_flag) return;               // hovering the flag, not the run
    const int idx = app.hover_popup.marker_index;
    if (idx < 0) return;
    if (app.hover_popup.lane_text.size() <= kLaneAmbientGlyphBudget) return;
    for (const LaneTextRun& r : set.runs) {
        if (r.marker_index != idx) continue;
        set.expanded.valid        = true;
        set.expanded.marker_index = idx;
        set.expanded.source_frame = r.source_frame;    // same column as its capped run
        set.expanded.text         = app.hover_popup.lane_text;   // FULL (uncapped)
        set.expanded.glyphs       = set.expanded.text.size();    // ASCII: byte == glyph
        set.has_expanded          = true;
        return;
    }
}

} // namespace

// The base run set (capped, mode decided) WITHOUT the text-hover expansion —
// current_marker_lane_runs wraps this and applies apply_hover_expansion to the
// result, so both the all-visible and fallback returns pick up the expansion at
// one site.
static LaneRunSet resolve_base_lane_run_set(const AppState& app,
                                            const GuiAudio& audio)
{
    // The occlusion arbitration the paint pass and the unified marker hit
    // resolver (marker_hit_at) share (contract at the declaration). The open
    // FlagPayload editor is NOT handled here — it is an overlay resolved by the
    // paint pass (its marker's capped ambient run still participates in the
    // verdict; the editor's full-width box never does).
    const double advance = monospace_advance();
    // Font not measured yet — the whole geometry is undefined. Return the empty
    // fallback shape; paint/hit keep their own advance guards.
    if (advance <= 0.0) return lane_run_set_fallback(app, audio);

    const GuiRect area = waveform_area(app);
    if (area.w <= 0) return lane_run_set_fallback(app, audio);

    // The displayed MAP + VIEWPORT basis the flag pixels were painted with, so
    // the visible-set cull, the run columns, and the verdict all read the same
    // basis the flags do (see lane_text_left_x_at_frame's basis contract).
    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const std::vector<WarpFrameMapSegment>* map_arg =
        map.empty() ? nullptr : &map;
    const DisplayedViewportBasis basis = displayed_viewport_basis(app, audio);
    if (basis.spp <= 0.0) return lane_run_set_fallback(app, audio);

    // The flags' own half-offscreen cull (iterate_visible_flags_impl): a flag may
    // hang up to half its width offscreen; cull only when FULLY offscreen. Sample
    // space, on the displayed basis — the EXACT committed vp span (vp_start_frame/
    // vp_end_frame, the flag cache's own fp_vp span), the same {span, width} the
    // flags divided by, so the cull matches the flags' visibility on the
    // committing frame, not just at rest.
    const double vp_end   = static_cast<double>(basis.vp_end_frame);
    const double half_flag =
        static_cast<double>(flag_lane_w_px()) / 2.0;
    const double cull_lo  = basis.vp_start - half_flag * basis.spp;
    const double cull_hi  = vp_end + half_flag * basis.spp;

    // Positions ride the DragOverlay when a drag is active (every dragged
    // member's run tracks its live proposed position, matching the flags).
    DragOverlay overlay_storage{&app.drag.dragging_markers,
                                &app.drag.moveable_times};
    const DragOverlay* overlay = app.drag.active ? &overlay_storage : nullptr;
    const bool is_phase = (app.active_markers_view == 'P');

    // Compose the visible set: every active-column marker whose flag paints, its
    // capped display run, centered on the displayed column.
    LaneRunSet set;
    auto add_visible = [&](int idx, int64_t time_frame, std::string text) {
        const double eff_time = overlay
            ? overlay->effective_time(idx, static_cast<double>(time_frame))
            : static_cast<double>(time_frame);
        const double ms = frame_to_paint_sample(eff_time, map_arg);
        if (ms < cull_lo || ms > cull_hi) return;   // fully offscreen — no flag
        LaneTextRun run;
        run.valid        = true;
        run.marker_index = idx;
        run.source_frame = eff_time;
        run.text         = std::move(text);
        cap_lane_run_text(run.text, run.glyphs);
        set.runs.push_back(std::move(run));
    };

    if (is_phase) {
        const auto& pv = app.phaseresetmarkers.markers();
        for (size_t i = 0; i < pv.size(); ++i)
            add_visible(static_cast<int>(i), pv[i].time_frame,
                        kPhaseResetLaneToken);
    } else {
        const auto& mv = app.warpmarkers.markers();
        for (size_t i = 0; i < mv.size(); ++i)
            add_visible(static_cast<int>(i), mv[i].time_frame,
                        flag_text_iter(mv, static_cast<int>(i),
                                       app.iteration_mode_enabled));
    }

    // Empty visible set: nothing to show either way — fall through to the
    // fallback shape (its arbitration also finds nothing onscreen, so behavior
    // is identical; the fallback keeps the tier code as the ONE owner of "no
    // ambient set" too).
    if (set.runs.empty()) return lane_run_set_fallback(app, audio);

    // THE VERDICT: pass iff no two capped runs' rects overlap. Each rect's left
    // comes from the shared placement owner (clamped fully onscreen), width =
    // glyphs * advance. Sort by left; a right edge is HALF-OPEN, so right(a) ==
    // left(b) (abutting, gap 0) is legal and passes.
    struct RunRect { double left; double right; };
    std::vector<RunRect> rects;
    rects.reserve(set.runs.size());
    for (const LaneTextRun& r : set.runs) {
        const double left = lane_text_left_x_at_frame(
            app, audio, r.source_frame, r.glyphs);
        const double width = static_cast<double>(r.glyphs) * advance;
        rects.push_back({left, left + width});
    }
    std::sort(rects.begin(), rects.end(),
              [](const RunRect& a, const RunRect& b) { return a.left < b.left; });
    for (size_t i = 1; i < rects.size(); ++i) {
        if (rects[i - 1].right > rects[i].left) {
            // Occlusion — fall back to the one-run arbitration EXACTLY.
            return lane_run_set_fallback(app, audio);
        }
    }

    set.all_visible = true;
    return set;
}

LaneRunSet current_marker_lane_runs(const AppState& app, const GuiAudio& audio)
{
    // Resolve the capped base set (mode decided on the capped widths), then layer
    // the text-hover expansion on top — one application site covering both modes.
    LaneRunSet set = resolve_base_lane_run_set(app, audio);
    apply_hover_expansion(set, app);
    return set;
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
    const double advance = monospace_advance();
    if (advance <= 0.0) return h;
    const GuiRect lane = top_marker_text_row_area(app);
    if (y < lane.y || y >= lane.y + lane.h) return h;   // y-band already half-open

    const LaneRunSet set = current_marker_lane_runs(app, audio);
    // The expanded run (text-hover expansion) paints on top, so it is hit FIRST —
    // a point over a neighbor's occluded pixels resolves to the expanded run
    // (WYSIWYG). Its rect CONTAINS the marker's capped rect, so once expanded the
    // pointer stays inside it across the whole capped area and beyond — the
    // hysteresis latch the hover convergence relies on. HALF-OPEN, like the
    // all-visible runs.
    if (set.has_expanded) {
        const LaneTextRun& e = set.expanded;
        const double left = lane_text_left_x_at_frame(
            app, audio, e.source_frame, e.glyphs);
        if (left >= 0.0) {
            const double run_w = static_cast<double>(e.glyphs) * advance;
            if (static_cast<double>(x) >= left &&
                static_cast<double>(x) < left + run_w) {
                h.index = e.marker_index;
                return h;
            }
        }
    }
    if (set.all_visible) {
        // Every run's rect is disjoint by construction (the verdict passed), so
        // order is irrelevant; test with HALF-OPEN x intervals [left, left+w) so
        // two abutting runs (right(a) == left(b)) cannot double-hit.
        for (const LaneTextRun& run : set.runs) {
            const double left = lane_text_left_x_at_frame(
                app, audio, run.source_frame, run.glyphs);
            if (left < 0.0) continue;   // advance not measured (already guarded)
            const double run_w = static_cast<double>(run.glyphs) * advance;
            if (static_cast<double>(x) >= left &&
                static_cast<double>(x) < left + run_w) {
                h.index = run.marker_index;
                return h;
            }
        }
        return h;
    }
    // Fallback (0-or-1 run): today's CLOSED interval test, byte-identical.
    if (set.runs.empty()) return h;
    const LaneTextRun& run = set.runs.front();
    const double left = lane_text_left_x_at_frame(
        app, audio, run.source_frame, run.glyphs);
    if (left < 0.0) return h;
    const double run_w = static_cast<double>(run.glyphs) * advance;
    if (static_cast<double>(x) >= left &&
        static_cast<double>(x) <= left + run_w) {
        h.index = run.marker_index;
    }
    return h;
}
