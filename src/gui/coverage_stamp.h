#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// THE CHROME'S COVERAGE ARITHMETIC (architect 2026-11-14, send J: "the
// anti-aliasing I would like to commit to that as well, the full
// anti-aliasing"; "I would expect snapping nothing to integers and leaving
// them rounding to yield decent results"): the pure functions the chrome's
// coverage stamp (CoverageStampPass, cool_edit_paint.h) paints a time-placed
// mark with — A PIXEL'S EXACT COVERAGE by a shape with real edges, and THE
// BLEND of one opaque source word into one target word at that coverage.
// PURE ARITHMETIC ON PLAIN NUMBERS: no cairo, no AppState, no project
// header, so a host driver includes this file alone.

// A PIXEL'S COVERAGE BY A REAL SPAN, one axis: pixel `c` is the cell
// [c, c + 1), and the span [lo, hi] covers of it the length
//     clamp(min(c + 1, hi) − max(c, lo), 0, 1)
// — 1 on a cell inside the span, 0 on one outside it, the covered part of
// the cell an edge crosses. A span on whole edges answers 1 or 0 alone.
inline double coverage_of_span(int c, double lo, double hi) {
    const double cell = static_cast<double>(c);
    return std::clamp(std::min(cell + 1.0, hi) - std::max(cell, lo), 0.0,
                      1.0);
}

// A PIXEL'S COVERAGE BY A RECT WITH REAL EDGES [x0, x1] x [y0, y1]: the
// product of its two axes' covered lengths, the exact area of the pixel
// (px, py) inside the rect.
inline double coverage_of_rect(int px, int py, double x0, double y0,
                               double x1, double y1) {
    return coverage_of_span(px, x0, x1) * coverage_of_span(py, y0, y1);
}

// THE BLEND OF ONE WORD: the opaque source word `s` (cairo's ARGB32 word of
// an opaque color, alpha 255) laid over the target's premultiplied ARGB32
// word `d` at the coverage `a` in [0, 1] — for each of the four channels,
// the alpha's included,
//     out = floor(d · (1 − a) + s · a + 0.5)
// each product and each sum one double operation, in that order (a
// statement each below, so no compiler setting fuses a product into a sum).
// a == 1 GIVES s EXACTLY (a whole-cell stamp's word) AND a == 0 GIVES d
// (nothing written). One rule for an opaque target (the backbuffer's lanes)
// and for a clear or partly covered one (a cache surface blitted later): it
// is premultiplied source-over of the source at alpha a, so a clear target
// takes (s · a, 255 · a).
inline uint32_t blend_word(uint32_t d, uint32_t s, double a) {
    const double keep = 1.0 - a;
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const double dc   = static_cast<double>((d >> shift) & 0xffu);
        const double sc   = static_cast<double>((s >> shift) & 0xffu);
        const double kept = dc * keep;
        const double laid = sc * a;
        const double sum  = kept + laid;
        out |= static_cast<uint32_t>(sum + 0.5) << shift;
    }
    return out;
}

// A PIXEL'S COVERAGE BY THE DOWN-POINTING 45° TRIANGLE (architect
// 2026-11-14, send J: the chip and the ruler's head, "I think the chips
// should also go anti-aliased"): the triangle of APEX (ax, ay) AND HEIGHT h,
// y down — its top edge the row line y = ay − h from ax − h to ax + h, its
// two sides at 45° meeting at the apex below:
//     { (x, y) : ay − h <= y <= ay, |x − ax| <= ay − y }.
// THE ANSWER IS THE EXACT AREA of the pixel (px, py) — the cell [px, px + 1]
// x [py, py + 1] — inside it, in [0, 1].
// THE CLOSED FORM, in the apex's own coordinates u = x − ax and v = ay − y
// (v the height above the apex), where the triangle is |u| <= v <= h: over
// the pixel's column u in [a, b] = [px − ax, px + 1 − ax], the covered
// length of its rows v in [B, T] — T = min(ay − py, h) the lower of the
// cell's upper edge and the triangle's top, B = max(ay − py − 1, 0) the
// cell's lower edge, no lower than the apex — is clamp(T − |u|, 0, T − B),
// a clamped ramp of slope 1 either side of the apex. Its integral over
// [a, b] is the area, taken in two pieces at u = 0 through the ramp's own
// antiderivative R(w) = ∫ clamp(w, 0, D) dw (0 below 0, w² / 2 to D,
// D · w − D² / 2 past it; D = T − B): the left piece R(T + min(b, 0)) −
// R(T + a), the right piece R(T − max(a, 0)) − R(T − b). EXACT in real
// arithmetic — the integrand is piecewise linear and each piece is
// integrated in closed form, no sampling — and in doubles every operand is
// small (the pixel's offsets from the apex; px − ax and ay − py are exact
// subtractions), so the answer carries a few last-place roundings of
// numbers under a few tens.
// PURE, as its siblings above: plain numbers in, a number out.
namespace coverage_detail {
// R(w) for the ramp clamp(w, 0, d), d >= 0 — each product and sum a
// statement, as blend_word's.
inline double clamped_ramp_area(double w, double d) {
    if (w <= 0.0) return 0.0;
    if (w >= d) {
        const double full = d * w;
        const double half = 0.5 * d;
        const double corner = half * d;
        return full - corner;
    }
    const double sq = w * w;
    return 0.5 * sq;
}
} // namespace coverage_detail
inline double coverage_of_triangle(int px, int py, double ax, double ay,
                                   double h) {
    const double a = static_cast<double>(px) - ax;
    const double b = (static_cast<double>(px) + 1.0) - ax;
    const double top = std::min(ay - static_cast<double>(py), h);
    const double bot =
        std::max(ay - (static_cast<double>(py) + 1.0), 0.0);
    const double d = top - bot;
    if (d <= 0.0) return 0.0;
    double area = 0.0;
    const double left_end = std::min(b, 0.0);
    if (left_end > a) {
        const double hi = coverage_detail::clamped_ramp_area(top + left_end, d);
        const double lo = coverage_detail::clamped_ramp_area(top + a, d);
        area += hi - lo;
    }
    const double right_start = std::max(a, 0.0);
    if (b > right_start) {
        const double hi =
            coverage_detail::clamped_ramp_area(top - right_start, d);
        const double lo = coverage_detail::clamped_ramp_area(top - b, d);
        area += hi - lo;
    }
    return std::clamp(area, 0.0, 1.0);
}

// A PIXEL'S COVERAGE BY A ROUNDED RECT (architect 2026-11-17, session 140,
// ruling 4: the trim bar's span is Arc's scrollbar slider, A PILL, its
// edges at real places — the seat inside each bound, 2026-11-18 — at exact
// coverage): the rect [x0, x1] x [y0, y1] with
// real edges whose LEFT two corners are quarter disks of radius `rl` and
// whose RIGHT two are quarter disks of radius `rr` — a radius of 0 a square
// corner, each radius at most half the rect's width and half its height (the
// caller's clamp), so no two corners' cut-outs meet. THE ANSWER IS THE EXACT
// AREA of the pixel (px, py) inside it, in [0, 1]: the rect's coverage
// (coverage_of_rect) less, at every rounded corner whose square the pixel
// meets, the part of the pixel in the corner's square and outside its
// quarter disk — that square's part of the cell less the disk's part of it
// (area_of_disk_in_rect: the disk's portion inside the corner's square is
// exactly the quarter disk). EXACT in real arithmetic, every piece a closed
// form (the circle's strip integral, an arcsine); in doubles a few
// last-place roundings of numbers under a few tens.
namespace coverage_detail {
// ∫₀ᵗ √(r² − u²) du, t held to [−r, r]: (t·√(r² − t²) + r²·asin(t / r)) / 2.
inline double disk_strip_area(double t, double r) {
    const double c = std::clamp(t, -r, r);
    const double rr = r * r;
    const double cc = c * c;
    const double root = std::sqrt(std::max(rr - cc, 0.0));
    const double tri = c * root;
    const double arc = rr * std::asin(c / r);
    const double sum = tri + arc;
    return 0.5 * sum;
}
// ∫ over u in [u0, u1] of clamp(v, −s(u), s(u)), s(u) = √(r² − u²) on
// |u| <= r and 0 beyond — the signed part of the chord at u between the
// disk's center row and the row v. clamp(v, −s, s) = sign(v) · min(|v|, s),
// and min(|v|, s(u)) is |v| on |u| <= w = √(r² − v²) (where the chord
// reaches past the row; nowhere when |v| >= r) and s(u) beyond.
inline double disk_signed_column_integral(double u0, double u1, double v,
                                          double r) {
    if (u1 <= u0) return 0.0;
    const double av = std::abs(v);
    const double w = av < r ? std::sqrt(r * r - av * av) : 0.0;
    const double i0 = std::max(u0, -w);
    const double i1 = std::min(u1, w);
    const double whole = disk_strip_area(u1, r) - disk_strip_area(u0, r);
    double inside = 0.0;   // ∫ s over [i0, i1], replaced by |v| there
    double flat = 0.0;
    if (i1 > i0) {
        inside = disk_strip_area(i1, r) - disk_strip_area(i0, r);
        flat = av * (i1 - i0);
    }
    const double mag = whole - inside + flat;
    return v < 0.0 ? -mag : mag;
}
} // namespace coverage_detail

// THE AREA OF THE DISK of center (cx, cy) and radius r INSIDE THE RECT
// [x0, x1] x [y0, y1] (0 for an empty rect or radius): over the columns the
// two share, the chord's length between the rect's two rows, as the
// difference of the two rows' signed integrals
// (coverage_detail::disk_signed_column_integral).
inline double area_of_disk_in_rect(double cx, double cy, double r, double x0,
                                   double y0, double x1, double y1) {
    if (r <= 0.0 || x1 <= x0 || y1 <= y0) return 0.0;
    const double u0 = std::max(x0 - cx, -r);
    const double u1 = std::min(x1 - cx, r);
    if (u1 <= u0) return 0.0;
    const double hi =
        coverage_detail::disk_signed_column_integral(u0, u1, y1 - cy, r);
    const double lo =
        coverage_detail::disk_signed_column_integral(u0, u1, y0 - cy, r);
    return std::max(hi - lo, 0.0);
}

inline double coverage_of_rounded_rect(int px, int py, double x0, double y0,
                                       double x1, double y1, double rl,
                                       double rr) {
    double cov = coverage_of_rect(px, py, x0, y0, x1, y1);
    if (cov <= 0.0) return 0.0;
    const double cx0 = static_cast<double>(px);
    const double cy0 = static_cast<double>(py);
    const double cx1 = cx0 + 1.0;
    const double cy1 = cy0 + 1.0;
    // One corner: its square [sx0, sx1] x [sy0, sy1] and its disk's center
    // (dx, dy), radius r.
    const auto cut = [&](double r, double sx0, double sx1, double sy0,
                         double sy1, double dx, double dy) {
        if (r <= 0.0) return;
        const double ix0 = std::max(cx0, sx0);
        const double ix1 = std::min(cx1, sx1);
        const double iy0 = std::max(cy0, sy0);
        const double iy1 = std::min(cy1, sy1);
        if (ix1 <= ix0 || iy1 <= iy0) return;
        const double part = (ix1 - ix0) * (iy1 - iy0);
        const double disk = area_of_disk_in_rect(dx, dy, r, ix0, iy0, ix1, iy1);
        cov -= part - disk;
    };
    cut(rl, x0, x0 + rl, y0, y0 + rl, x0 + rl, y0 + rl);
    cut(rl, x0, x0 + rl, y1 - rl, y1, x0 + rl, y1 - rl);
    cut(rr, x1 - rr, x1, y0, y0 + rr, x1 - rr, y0 + rr);
    cut(rr, x1 - rr, x1, y1 - rr, y1, x1 - rr, y1 - rr);
    return std::clamp(cov, 0.0, 1.0);
}

// THE AREA OF A ROUNDED RECT INSIDE A RECT (architect 2026-11-19, send Z:
// the entry's progress line, Arc's entry progress, cut at its real end): the
// rounded rect of coverage_of_rounded_rect's shape — [x0, x1] x [y0, y1],
// left corners of radius `rl`, right of `rr`, the same clamp owed — inside
// the rect [qx0, qx1] x [qy0, qy1], in the same closed forms: the two
// rects' overlap less, at every rounded corner whose square meets it, the
// part of that square in the overlap outside its quarter disk. Over a pixel's
// unit cell it is that pixel's coverage; over the cell cut to a narrower
// rect, the coverage of the shape cut there.
inline double area_of_rounded_rect_in_rect(double x0, double y0, double x1,
                                           double y1, double rl, double rr,
                                           double qx0, double qy0,
                                           double qx1, double qy1) {
    const double ox0 = std::max(x0, qx0);
    const double ox1 = std::min(x1, qx1);
    const double oy0 = std::max(y0, qy0);
    const double oy1 = std::min(y1, qy1);
    if (ox1 <= ox0 || oy1 <= oy0) return 0.0;
    double area = (ox1 - ox0) * (oy1 - oy0);
    const auto cut = [&](double r, double sx0, double sx1, double sy0,
                         double sy1, double dx, double dy) {
        if (r <= 0.0) return;
        const double ix0 = std::max(ox0, sx0);
        const double ix1 = std::min(ox1, sx1);
        const double iy0 = std::max(oy0, sy0);
        const double iy1 = std::min(oy1, sy1);
        if (ix1 <= ix0 || iy1 <= iy0) return;
        const double part = (ix1 - ix0) * (iy1 - iy0);
        const double disk = area_of_disk_in_rect(dx, dy, r, ix0, iy0, ix1, iy1);
        area -= part - disk;
    };
    cut(rl, x0, x0 + rl, y0, y0 + rl, x0 + rl, y0 + rl);
    cut(rl, x0, x0 + rl, y1 - rl, y1, x0 + rl, y1 - rl);
    cut(rr, x1 - rr, x1, y0, y0 + rr, x1 - rr, y0 + rr);
    cut(rr, x1 - rr, x1, y1 - rr, y1, x1 - rr, y1 - rr);
    return std::max(area, 0.0);
}
