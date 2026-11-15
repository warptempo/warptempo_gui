#pragma once

#include <algorithm>
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
