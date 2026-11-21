#include "cool_edit_paint.h"

#include "coverage_stamp.h"   // the coverage stamp's arithmetic
#include "materials.h"   // the flat look's rules (palette() through render.h)
#include "text_shape.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

// The rules are at the declarations (cool_edit_paint.h).


namespace {

// The pixel cairo's fill writes for an opaque `c` set by set_palette_source
// (cairo_set_source_rgb of its window color): each channel's 16-bit short,
// d x 65535 + 0.5 truncated, its high byte (cairo-color.c's
// _cairo_color_double_to_short, cairo-image-compositor.c's color_to_uint32)
// — the stamp's source word, so a pixel the stamp covers whole holds the
// bytes cairo's own opaque fill of that color leaves.
uint32_t cue_fill_pixel(GuiColor c) {
    const GuiColor d = display_color(c);
    const auto ch = [](double v) {
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;
        const uint16_t s = static_cast<uint16_t>(v * 65535.0 + 0.5);
        return static_cast<uint32_t>(s >> 8);
    };
    return 0xff000000u | (ch(d.r) << 16) | (ch(d.g) << 8) | ch(d.b);
}

// ONE PIXEL OF A SHAPE at coverage `a`: the word itself where the pixel is
// covered whole (blend_word's own answer at a == 1, by a store), nothing
// where it is not reached (its answer at a == 0 is the target's word), the
// blend between.
inline void stamp_pixel(uint32_t* at, uint32_t word, double a) {
    if (a >= 1.0)
        *at = word;
    else if (a > 0.0)
        *at = blend_word(*at, word, a);
}

} // namespace

// THE COVERAGE STAMP (the rule at the declaration, cool_edit_paint.h; the
// arithmetic coverage_stamp.h's).
void CoverageStampPass::begin(cairo_t* cr) {
    cr_        = cr;
    direct_    = direct_pixel_target(cr, target_);
    dirty_     = GuiRect{};
    dirty_any_ = false;
    for (KeptWord& k : kept_) k = KeptWord{};
    kept_next_ = 0;
    chip_read_ = false;
}

// A COLOR'S WORD, KEPT (the declaration's WHAT A PASS READS ONCE): the word
// cue_fill_pixel makes of `color`, from the pass's kept colors where one
// is this color to the bit, else made and kept in the oldest's place.
uint32_t CoverageStampPass::word_of(GuiColor color) {
    for (const KeptWord& k : kept_)
        if (k.set && k.color.r == color.r && k.color.g == color.g &&
            k.color.b == color.b)
            return k.word;
    KeptWord& slot = kept_[kept_next_];
    kept_next_ = (kept_next_ + 1) % kKeptWords;
    slot.set   = true;
    slot.color = color;
    slot.word  = cue_fill_pixel(color);
    return slot.word;
}

// A TONE'S WORD (StampTone's rule, materials.h): a plain color's own word;
// a mix the colors' words weighed per channel, the sum over the weights'
// total rounded once, half to even (nearbyint under the default rounding
// mode, the derivation's rounding, theme_roles.h).
namespace {
template <class WordOf>
uint32_t mixed_word(const StampTone& tone, WordOf&& word_of_color) {
    if (tone.plain()) return word_of_color(tone.first());
    double total = 0.0;
    double acc[3] = {0.0, 0.0, 0.0};
    for (int i = 0; i < tone.n; ++i) {
        const double wt = tone.weight[static_cast<std::size_t>(i)];
        const uint32_t w = word_of_color(tone.color[static_cast<std::size_t>(i)]);
        total += wt;
        for (int c = 0; c < 3; ++c)
            acc[c] += wt * static_cast<double>((w >> (16 - 8 * c)) & 0xFFu);
    }
    uint32_t out = 0xff000000u;
    for (int c = 0; c < 3; ++c) {
        const double v = std::nearbyint(total > 0.0 ? acc[c] / total : 0.0);
        out |= static_cast<uint32_t>(std::clamp(v, 0.0, 255.0)) << (16 - 8 * c);
    }
    return out;
}
} // namespace

uint32_t CoverageStampPass::word_of(const StampTone& tone) {
    return mixed_word(tone, [this](GuiColor c) { return word_of(c); });
}

uint32_t stamp_tone_word(const StampTone& tone) {
    return mixed_word(tone, [](GuiColor c) { return cue_fill_pixel(c); });
}

// THE INK'S SOURCE (the declaration): a plain tone through the palette's
// chokepoint; a mix its word's own bytes, already the window's (a byte n set
// as n / 255 is the byte cairo's fill stores, cue_fill_pixel's arithmetic).
void set_stamp_tone_source(cairo_t* cr, const StampTone& tone) {
    if (tone.plain()) {
        set_palette_source(cr, tone.first());
        return;
    }
    const uint32_t w = stamp_tone_word(tone);
    cairo_set_source_rgb(cr,
                         static_cast<double>((w >> 16) & 0xFFu) / 255.0,
                         static_cast<double>((w >> 8) & 0xFFu) / 255.0,
                         static_cast<double>(w & 0xFFu) / 255.0);
}

// THE CAIRO ROAD'S SOURCE for a tone: the ink's, from the pass's kept words.
void CoverageStampPass::set_tone_source(const StampTone& tone) {
    if (tone.plain()) {
        set_palette_source(cr_, tone.first());
        return;
    }
    const uint32_t w = word_of(tone);
    cairo_set_source_rgb(cr_,
                         static_cast<double>((w >> 16) & 0xFFu) / 255.0,
                         static_cast<double>((w >> 8) & 0xFFu) / 255.0,
                         static_cast<double>(w & 0xFFu) / 255.0);
}

void CoverageStampPass::grow_dirty(const GuiRect& box) {
    if (!dirty_any_) {
        dirty_     = box;
        dirty_any_ = true;
        return;
    }
    const int x0 = std::min(dirty_.x, box.x);
    const int y0 = std::min(dirty_.y, box.y);
    const int x1 = std::max(dirty_.x + dirty_.w, box.x + box.w);
    const int y1 = std::max(dirty_.y + dirty_.h, box.y + box.h);
    dirty_ = GuiRect{x0, y0, x1 - x0, y1 - y0};
}

void CoverageStampPass::rect(double x0, double y0, double x1, double y1,
                             StampTone color) {
    if (x1 <= x0 || y1 <= y0) return;
    if (!direct_) {
        // CAIRO'S ROAD: the same rect as a path, at cairo's default
        // antialiasing whatever the caller's context holds.
        cairo_save(cr_);
        cairo_set_antialias(cr_, CAIRO_ANTIALIAS_DEFAULT);
        cairo_new_path(cr_);
        cairo_rectangle(cr_, x0, y0, x1 - x0, y1 - y0);
        set_tone_source(color);
        cairo_fill(cr_);
        cairo_restore(cr_);
        return;
    }
    // THE STAMP: the rect in device px (the target's integral translation),
    // the cells it touches its floor through its ceiling, cut to each clip
    // box; a pixel's coverage is its column's covered length times its
    // row's (coverage_of_rect's product, in its order).
    const double dx0 = x0 + static_cast<double>(target_.dx);
    const double dx1 = x1 + static_cast<double>(target_.dx);
    const double dy0 = y0 + static_cast<double>(target_.dy);
    const double dy1 = y1 + static_cast<double>(target_.dy);
    const int bx0 = static_cast<int>(std::floor(dx0));
    const int bx1 = static_cast<int>(std::ceil(dx1));
    const int by0 = static_cast<int>(std::floor(dy0));
    const int by1 = static_cast<int>(std::ceil(dy1));
    const int fx0 = static_cast<int>(std::ceil(dx0));
    const int fx1 = static_cast<int>(std::floor(dx1));
    const uint32_t word = word_of(color);
    for (const GuiRect& r : target_.clip) {
        const int cx0 = std::max(bx0, r.x);
        const int cx1 = std::min(bx1, r.x + r.w);
        const int cy0 = std::max(by0, r.y);
        const int cy1 = std::min(by1, r.y + r.h);
        if (cx1 <= cx0 || cy1 <= cy0) continue;
        // THE WHOLE COLUMNS [wx0, wx1) of the rect inside this box: on a
        // row covered whole they are covered whole (both spans exactly 1),
        // so they take the word by a store; the columns an edge crosses and
        // every pixel of a row an edge crosses take the blend.
        const int wx0 = std::clamp(fx0, cx0, cx1);
        const int wx1 = std::clamp(fx1, wx0, cx1);
        for (int yy = cy0; yy < cy1; ++yy) {
            const double ay = coverage_of_span(yy, dy0, dy1);
            uint32_t* const row = reinterpret_cast<uint32_t*>(
                target_.data + static_cast<std::size_t>(yy) * target_.stride);
            if (ay >= 1.0) {
                for (int xx = cx0; xx < wx0; ++xx)
                    stamp_pixel(row + xx, word,
                                coverage_of_span(xx, dx0, dx1) * ay);
                std::fill(row + wx0, row + wx1, word);
                for (int xx = wx1; xx < cx1; ++xx)
                    stamp_pixel(row + xx, word,
                                coverage_of_span(xx, dx0, dx1) * ay);
            } else {
                for (int xx = cx0; xx < cx1; ++xx)
                    stamp_pixel(row + xx, word,
                                coverage_of_span(xx, dx0, dx1) * ay);
            }
        }
        grow_dirty(GuiRect{cx0, cy0, cx1 - cx0, cy1 - cy0});
    }
}

void CoverageStampPass::rounded_rect(double x0, double y0, double x1,
                                     double y1, double rl, double rr,
                                     StampTone color) {
    if (x1 <= x0 || y1 <= y0) return;
    if (!direct_) {
        // CAIRO'S ROAD: the same shape as a path — the straight edges and
        // each rounded corner's quarter arc — at cairo's default
        // antialiasing.
        cairo_save(cr_);
        cairo_set_antialias(cr_, CAIRO_ANTIALIAS_DEFAULT);
        cairo_new_path(cr_);
        const double half_pi = std::numbers::pi / 2.0;
        cairo_move_to(cr_, x0 + rl, y0);
        cairo_line_to(cr_, x1 - rr, y0);
        if (rr > 0.0) cairo_arc(cr_, x1 - rr, y0 + rr, rr, -half_pi, 0.0);
        cairo_line_to(cr_, x1, y1 - rr);
        if (rr > 0.0) cairo_arc(cr_, x1 - rr, y1 - rr, rr, 0.0, half_pi);
        cairo_line_to(cr_, x0 + rl, y1);
        if (rl > 0.0)
            cairo_arc(cr_, x0 + rl, y1 - rl, rl, half_pi, 2.0 * half_pi);
        cairo_line_to(cr_, x0, y0 + rl);
        if (rl > 0.0)
            cairo_arc(cr_, x0 + rl, y0 + rl, rl, 2.0 * half_pi, 3.0 * half_pi);
        cairo_close_path(cr_);
        set_tone_source(color);
        cairo_fill(cr_);
        cairo_restore(cr_);
        return;
    }
    // THE STAMP: the shape in device px, the cells it touches its floor
    // through its ceiling, cut to each clip box. ON A ROW COVERED WHOLE the
    // columns clear of both rounded ends — whole columns from the left
    // radius's reach to the right radius's — are covered whole and take the
    // word by a store; every other pixel its exact coverage
    // (coverage_of_rounded_rect), 0 leaving the target's word.
    const double dx0 = x0 + static_cast<double>(target_.dx);
    const double dx1 = x1 + static_cast<double>(target_.dx);
    const double dy0 = y0 + static_cast<double>(target_.dy);
    const double dy1 = y1 + static_cast<double>(target_.dy);
    const int bx0 = static_cast<int>(std::floor(dx0));
    const int bx1 = static_cast<int>(std::ceil(dx1));
    const int by0 = static_cast<int>(std::floor(dy0));
    const int by1 = static_cast<int>(std::ceil(dy1));
    const int fx0 = static_cast<int>(std::ceil(dx0 + rl));
    const int fx1 = static_cast<int>(std::floor(dx1 - rr));
    const uint32_t word = word_of(color);
    for (const GuiRect& r : target_.clip) {
        const int cx0 = std::max(bx0, r.x);
        const int cx1 = std::min(bx1, r.x + r.w);
        const int cy0 = std::max(by0, r.y);
        const int cy1 = std::min(by1, r.y + r.h);
        if (cx1 <= cx0 || cy1 <= cy0) continue;
        const int wx0 = std::clamp(fx0, cx0, cx1);
        const int wx1 = std::clamp(fx1, wx0, cx1);
        for (int yy = cy0; yy < cy1; ++yy) {
            uint32_t* const row = reinterpret_cast<uint32_t*>(
                target_.data + static_cast<std::size_t>(yy) * target_.stride);
            const bool whole_row = coverage_of_span(yy, dy0, dy1) >= 1.0;
            const int run0 = whole_row ? wx0 : cx1;
            const int run1 = whole_row ? wx1 : cx1;
            for (int xx = cx0; xx < run0; ++xx)
                stamp_pixel(row + xx, word,
                            coverage_of_rounded_rect(xx, yy, dx0, dy0, dx1,
                                                     dy1, rl, rr));
            std::fill(row + run0, row + run1, word);
            for (int xx = run1; xx < cx1; ++xx)
                stamp_pixel(row + xx, word,
                            coverage_of_rounded_rect(xx, yy, dx0, dy0, dx1,
                                                     dy1, rl, rr));
        }
        grow_dirty(GuiRect{cx0, cy0, cx1 - cx0, cy1 - cy0});
    }
}

// THE GENERIC STAMP (the declaration's): each row of the box inside each
// clip box, the skipped run untouched, every other pixel its coverage.
template <class Cov, class Skip>
void CoverageStampPass::stamp_each(int bx0, int by0, int bx1, int by1,
                                   uint32_t word, Cov&& cov, Skip&& skip) {
    for (const GuiRect& r : target_.clip) {
        const int cx0 = std::max(bx0, r.x);
        const int cx1 = std::min(bx1, r.x + r.w);
        const int cy0 = std::max(by0, r.y);
        const int cy1 = std::min(by1, r.y + r.h);
        if (cx1 <= cx0 || cy1 <= cy0) continue;
        for (int yy = cy0; yy < cy1; ++yy) {
            uint32_t* const row = reinterpret_cast<uint32_t*>(
                target_.data + static_cast<std::size_t>(yy) * target_.stride);
            int s0 = cx1, s1 = cx1;
            skip(yy, s0, s1);
            s0 = std::clamp(s0, cx0, cx1);
            s1 = std::clamp(s1, s0, cx1);
            for (int xx = cx0; xx < s0; ++xx)
                stamp_pixel(row + xx, word, cov(xx, yy));
            for (int xx = s1; xx < cx1; ++xx)
                stamp_pixel(row + xx, word, cov(xx, yy));
        }
        grow_dirty(GuiRect{cx0, cy0, cx1 - cx0, cy1 - cy0});
    }
}

namespace {
// A ROUNDED RECT'S PATH for the cairo road (the same quarter arcs as
// CoverageStampPass::rounded_rect's road), a new sub-path.
void rounded_rect_path(cairo_t* cr, double x0, double y0, double x1,
                       double y1, double rl, double rr) {
    const double half_pi = std::numbers::pi / 2.0;
    cairo_new_sub_path(cr);
    cairo_move_to(cr, x0 + rl, y0);
    cairo_line_to(cr, x1 - rr, y0);
    if (rr > 0.0) cairo_arc(cr, x1 - rr, y0 + rr, rr, -half_pi, 0.0);
    cairo_line_to(cr, x1, y1 - rr);
    if (rr > 0.0) cairo_arc(cr, x1 - rr, y1 - rr, rr, 0.0, half_pi);
    cairo_line_to(cr, x0 + rl, y1);
    if (rl > 0.0) cairo_arc(cr, x0 + rl, y1 - rl, rl, half_pi, 2.0 * half_pi);
    cairo_line_to(cr, x0, y0 + rl);
    if (rl > 0.0)
        cairo_arc(cr, x0 + rl, y0 + rl, rl, 2.0 * half_pi, 3.0 * half_pi);
    cairo_close_path(cr);
}
} // namespace

void CoverageStampPass::rounded_ring(double x0, double y0, double x1,
                                     double y1, double r, double bw,
                                     StampTone color) {
    if (x1 <= x0 || y1 <= y0 || bw <= 0.0) return;
    const double ri = std::max(r - bw, 0.0);
    const double ix0 = x0 + bw, iy0 = y0 + bw, ix1 = x1 - bw, iy1 = y1 - bw;
    if (!direct_) {
        // CAIRO'S ROAD: the outer and the padding box as two sub-paths, the
        // even-odd rule leaving the ring.
        cairo_save(cr_);
        cairo_set_antialias(cr_, CAIRO_ANTIALIAS_DEFAULT);
        cairo_set_fill_rule(cr_, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_new_path(cr_);
        rounded_rect_path(cr_, x0, y0, x1, y1, r, r);
        if (ix1 > ix0 && iy1 > iy0)
            rounded_rect_path(cr_, ix0, iy0, ix1, iy1, ri, ri);
        set_tone_source(color);
        cairo_fill(cr_);
        cairo_restore(cr_);
        return;
    }
    const double dx = static_cast<double>(target_.dx);
    const double dy = static_cast<double>(target_.dy);
    const double ox0 = x0 + dx, oy0 = y0 + dy, ox1 = x1 + dx, oy1 = y1 + dy;
    const double px0 = ix0 + dx, py0 = iy0 + dy, px1 = ix1 + dx, py1 = iy1 + dy;
    const bool inner = px1 > px0 && py1 > py0;
    const uint32_t word = word_of(color);
    stamp_each(
        static_cast<int>(std::floor(ox0)), static_cast<int>(std::floor(oy0)),
        static_cast<int>(std::ceil(ox1)), static_cast<int>(std::ceil(oy1)),
        word,
        [&](int xx, int yy) {
            const double co =
                coverage_of_rounded_rect(xx, yy, ox0, oy0, ox1, oy1, r, r);
            const double ci =
                inner ? coverage_of_rounded_rect(xx, yy, px0, py0, px1, py1,
                                                 ri, ri)
                      : 0.0;
            return std::clamp(co - ci, 0.0, 1.0);
        },
        [&](int yy, int& a, int& b) {
            // The padding box's whole pixels on this row: covered whole by
            // both shapes, so the ring covers none of them.
            if (!inner) return;
            const double band = ri;
            if (coverage_of_span(yy, py0 + band, py1 - band) >= 1.0) {
                a = static_cast<int>(std::ceil(px0));
                b = static_cast<int>(std::floor(px1));
            } else if (coverage_of_span(yy, py0, py1) >= 1.0) {
                a = static_cast<int>(std::ceil(px0 + band));
                b = static_cast<int>(std::floor(px1 - band));
            }
        });
}

void CoverageStampPass::rounded_rect_cut(double x0, double y0, double x1,
                                         double y1, double rl, double rr,
                                         double cx0, double cy0, double cx1,
                                         double cy1, StampTone color) {
    const double qx0 = std::max(x0, cx0), qx1 = std::min(x1, cx1);
    const double qy0 = std::max(y0, cy0), qy1 = std::min(y1, cy1);
    if (qx1 <= qx0 || qy1 <= qy0) return;
    if (!direct_) {
        // CAIRO'S ROAD: the shape's path under the cut rect as a clip.
        cairo_save(cr_);
        cairo_set_antialias(cr_, CAIRO_ANTIALIAS_DEFAULT);
        cairo_new_path(cr_);
        cairo_rectangle(cr_, qx0, qy0, qx1 - qx0, qy1 - qy0);
        cairo_clip(cr_);
        cairo_new_path(cr_);
        rounded_rect_path(cr_, x0, y0, x1, y1, rl, rr);
        set_tone_source(color);
        cairo_fill(cr_);
        cairo_restore(cr_);
        return;
    }
    const double dx = static_cast<double>(target_.dx);
    const double dy = static_cast<double>(target_.dy);
    const double sx0 = x0 + dx, sy0 = y0 + dy, sx1 = x1 + dx, sy1 = y1 + dy;
    const double kx0 = qx0 + dx, ky0 = qy0 + dy, kx1 = qx1 + dx, ky1 = qy1 + dy;
    const uint32_t word = word_of(color);
    stamp_each(
        static_cast<int>(std::floor(kx0)), static_cast<int>(std::floor(ky0)),
        static_cast<int>(std::ceil(kx1)), static_cast<int>(std::ceil(ky1)),
        word,
        [&](int xx, int yy) {
            const double cx = static_cast<double>(xx);
            const double cy = static_cast<double>(yy);
            return std::clamp(
                area_of_rounded_rect_in_rect(
                    sx0, sy0, sx1, sy1, rl, rr, std::max(cx, kx0),
                    std::max(cy, ky0), std::min(cx + 1.0, kx1),
                    std::min(cy + 1.0, ky1)),
                0.0, 1.0);
        },
        [](int, int&, int&) {});
}

void CoverageStampPass::rounded_rect_bands(double x0, double y0, double x1,
                                           double y1, double rl, double rr,
                                           double c0, double c1, GuiColor t0,
                                           GuiColor t1, GuiColor t2) {
    if (x1 <= x0 || y1 <= y0) return;
    const double k0 = std::clamp(c0, x0, x1);
    const double k1 = std::clamp(c1, k0, x1);
    const GuiColor tones[3] = {t0, t1, t2};
    if (!direct_) {
        // CAIRO'S ROAD: the shape's path once per region, under the
        // region's rect as a clip.
        const double lo[3] = {x0, k0, k1};
        const double hi[3] = {k0, k1, x1};
        for (int i = 0; i < 3; ++i) {
            if (hi[i] <= lo[i]) continue;
            cairo_save(cr_);
            cairo_set_antialias(cr_, CAIRO_ANTIALIAS_DEFAULT);
            cairo_new_path(cr_);
            cairo_rectangle(cr_, lo[i], y0, hi[i] - lo[i], y1 - y0);
            cairo_clip(cr_);
            cairo_new_path(cr_);
            rounded_rect_path(cr_, x0, y0, x1, y1, rl, rr);
            set_palette_source(cr_, tones[i]);
            cairo_fill(cr_);
            cairo_restore(cr_);
        }
        return;
    }
    // THE STAMP: rounded_rect's cells and whole run, each pixel's column
    // split at the two lines into the regions' parts; one part, the plain
    // stamp in its tone; more, each part's exact area of the shape.
    const double dx = static_cast<double>(target_.dx);
    const double dy = static_cast<double>(target_.dy);
    const double sx0 = x0 + dx, sy0 = y0 + dy, sx1 = x1 + dx, sy1 = y1 + dy;
    const double d0 = k0 + dx, d1 = k1 + dx;
    const int bx0 = static_cast<int>(std::floor(sx0));
    const int bx1 = static_cast<int>(std::ceil(sx1));
    const int by0 = static_cast<int>(std::floor(sy0));
    const int by1 = static_cast<int>(std::ceil(sy1));
    const int fx0 = static_cast<int>(std::ceil(sx0 + rl));
    const int fx1 = static_cast<int>(std::floor(sx1 - rr));
    const uint32_t words[3] = {word_of(t0), word_of(t1), word_of(t2)};
    for (const GuiRect& r : target_.clip) {
        const int cx0 = std::max(bx0, r.x);
        const int cx1 = std::min(bx1, r.x + r.w);
        const int cy0 = std::max(by0, r.y);
        const int cy1 = std::min(by1, r.y + r.h);
        if (cx1 <= cx0 || cy1 <= cy0) continue;
        const int wx0 = std::clamp(fx0, cx0, cx1);
        const int wx1 = std::clamp(fx1, wx0, cx1);
        for (int yy = cy0; yy < cy1; ++yy) {
            uint32_t* const row = reinterpret_cast<uint32_t*>(
                target_.data + static_cast<std::size_t>(yy) * target_.stride);
            const bool whole_row = coverage_of_span(yy, sy0, sy1) >= 1.0;
            const double py0 = static_cast<double>(yy);
            const double py1 = py0 + 1.0;
            for (int xx = cx0; xx < cx1; ++xx) {
                const double px0 = static_cast<double>(xx);
                const double px1 = px0 + 1.0;
                const double lo[3] = {px0, std::max(px0, d0),
                                      std::max(px0, d1)};
                const double hi[3] = {std::min(px1, d0), std::min(px1, d1),
                                      px1};
                int only = 0;
                int parts = 0;
                for (int i = 0; i < 3; ++i)
                    if (hi[i] > lo[i]) {
                        only = i;
                        ++parts;
                    }
                if (parts <= 1) {
                    if (whole_row && xx >= wx0 && xx < wx1)
                        row[xx] = words[only];
                    else
                        stamp_pixel(row + xx, words[only],
                                    coverage_of_rounded_rect(
                                        xx, yy, sx0, sy0, sx1, sy1, rl, rr));
                    continue;
                }
                double a[3] = {0.0, 0.0, 0.0};
                double cover = 0.0;
                for (int i = 0; i < 3; ++i) {
                    if (hi[i] <= lo[i]) continue;
                    a[i] = std::clamp(
                        area_of_rounded_rect_in_rect(sx0, sy0, sx1, sy1, rl,
                                                     rr, lo[i], py0, hi[i],
                                                     py1),
                        0.0, 1.0);
                    cover += a[i];
                }
                if (cover > 0.0)
                    row[xx] = blend_word_bands(row[xx], words, a, 3);
            }
        }
        grow_dirty(GuiRect{cx0, cy0, cx1 - cx0, cy1 - cy0});
    }
}

void CoverageStampPass::triangle(double ax, double ay, double h,
                                 StampTone tone) {
    if (h <= 0.0) return;
    if (!direct_) {
        // CAIRO'S ROAD: the same triangle as a path, at cairo's default
        // antialiasing.
        cairo_save(cr_);
        cairo_set_antialias(cr_, CAIRO_ANTIALIAS_DEFAULT);
        cairo_new_path(cr_);
        cairo_move_to(cr_, ax - h, ay - h);
        cairo_line_to(cr_, ax + h, ay - h);
        cairo_line_to(cr_, ax, ay);
        cairo_close_path(cr_);
        set_tone_source(tone);
        cairo_fill(cr_);
        cairo_restore(cr_);
        return;
    }
    // THE STAMP: the triangle in device px, its rows from its top edge's
    // row through the apex's. ON EACH ROW the pixels it can touch are those
    // within the half-width of the row's upper edge (the wider), cut at the
    // triangle's top, and the pixels it covers whole those within the
    // half-width of the row's lower edge (the narrower) on a row wholly
    // under the top edge: the whole run takes the word by a store, the
    // pixels beside it — the two slanted edges', and every pixel of the top
    // edge's and the apex's rows — their exact coverage
    // (coverage_of_triangle). A pixel the run's bounds misjudge by a
    // rounding is covered whole to that rounding, and takes the same bytes
    // either way.
    const double dax = ax + static_cast<double>(target_.dx);
    const double day = ay + static_cast<double>(target_.dy);
    const int by0 = static_cast<int>(std::floor(day - h));
    const int by1 = static_cast<int>(std::ceil(day));
    const int bx0 = static_cast<int>(std::floor(dax - h));
    const int bx1 = static_cast<int>(std::ceil(dax + h));
    const uint32_t word = word_of(tone);
    for (const GuiRect& r : target_.clip) {
        const int cx0 = std::max(bx0, r.x);
        const int cx1 = std::min(bx1, r.x + r.w);
        const int cy0 = std::max(by0, r.y);
        const int cy1 = std::min(by1, r.y + r.h);
        if (cx1 <= cx0 || cy1 <= cy0) continue;
        for (int yy = cy0; yy < cy1; ++yy) {
            // The row's two edges' heights above the apex.
            const double upper = day - static_cast<double>(yy);
            const double lower = upper - 1.0;
            const double reach = std::min(upper, h);
            const int tx0 = std::clamp(
                static_cast<int>(std::floor(dax - reach)), cx0, cx1);
            const int tx1 = std::clamp(
                static_cast<int>(std::ceil(dax + reach)), tx0, cx1);
            int wx0 = tx0;
            int wx1 = tx0;
            if (upper <= h && lower > 0.0) {
                wx0 = std::clamp(static_cast<int>(std::ceil(dax - lower)),
                                 tx0, tx1);
                wx1 = std::clamp(static_cast<int>(std::floor(dax + lower)),
                                 wx0, tx1);
            }
            uint32_t* const row = reinterpret_cast<uint32_t*>(
                target_.data + static_cast<std::size_t>(yy) * target_.stride);
            for (int xx = tx0; xx < wx0; ++xx)
                stamp_pixel(row + xx, word,
                            coverage_of_triangle(xx, yy, dax, day, h));
            std::fill(row + wx0, row + wx1, word);
            for (int xx = wx1; xx < tx1; ++xx)
                stamp_pixel(row + xx, word,
                            coverage_of_triangle(xx, yy, dax, day, h));
        }
        grow_dirty(GuiRect{cx0, cy0, cx1 - cx0, cy1 - cy0});
    }
}

// THE CHIP (the rule at THE CUE'S CHIP, cool_edit_paint.h; the height
// render.h's cue block, read at the pass's first chip): the one triangle of
// height H with its apex at (x, yb), in the fill.
void CoverageStampPass::chip(double x, double yb, StampTone fill) {
    if (!chip_read_) {
        chip_h_    = cue_chip_h_exact_px();
        chip_read_ = true;
    }
    triangle(x, yb, chip_h_, fill);
}

void CoverageStampPass::end() {
    if (dirty_any_)
        cairo_surface_mark_dirty_rectangle(target_.surface, dirty_.x, dirty_.y,
                                           dirty_.w, dirty_.h);
    dirty_any_ = false;
}

void paint_ce_cue_chip(cairo_t* cr, double x, double yb, StampTone fill) {
    CoverageStampPass pass;
    pass.begin(cr);
    pass.chip(x, yb, fill);
    pass.end();
}
