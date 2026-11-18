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

void paint_ce_time_field(cairo_t* cr, const GuiRectF& f) {
    // THE FLAT KEY FACE, no ring (the declaration's rule), on the field's
    // real edges (the exact fill, render.h).
    paint_cell_rect(cr, f, palette().key_face);
}


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
                             GuiColor color) {
    if (x1 <= x0 || y1 <= y0) return;
    if (!direct_) {
        // CAIRO'S ROAD: the same rect as a path, at cairo's default
        // antialiasing whatever the caller's context holds.
        cairo_save(cr_);
        cairo_set_antialias(cr_, CAIRO_ANTIALIAS_DEFAULT);
        cairo_new_path(cr_);
        cairo_rectangle(cr_, x0, y0, x1 - x0, y1 - y0);
        set_palette_source(cr_, color);
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
                                     GuiColor color) {
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
        set_palette_source(cr_, color);
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

void CoverageStampPass::triangle(double ax, double ay, double h,
                                 GuiColor color) {
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
        set_palette_source(cr_, color);
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
    const uint32_t word = word_of(color);
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

// THE CHIP (the rule at THE CUE'S CHIP, cool_edit_paint.h; the lengths
// render.h's cue block, read at the pass's first chip): the outer of height
// H with its apex at (x, yb), then the fill, its apex the fill's drop above
// and its height the fill's.
void CoverageStampPass::chip(double x, double yb, GuiColor fill,
                             std::optional<GuiColor> outline) {
    if (!chip_read_) {
        chip_h_      = cue_chip_h_exact_px();
        chip_drop_   = cue_chip_fill_drop_px();
        chip_fill_h_ = cue_chip_fill_h_px();
        chip_read_   = true;
    }
    if (outline) triangle(x, yb, chip_h_, *outline);
    triangle(x, yb - chip_drop_, chip_fill_h_, fill);
}

void CoverageStampPass::end() {
    if (dirty_any_)
        cairo_surface_mark_dirty_rectangle(target_.surface, dirty_.x, dirty_.y,
                                           dirty_.w, dirty_.h);
    dirty_any_ = false;
}

void paint_ce_cue_chip(cairo_t* cr, double x, double yb, GuiColor fill,
                       std::optional<GuiColor> outline) {
    CoverageStampPass pass;
    pass.begin(cr);
    pass.chip(x, yb, fill, outline);
    pass.end();
}
