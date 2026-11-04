#include "cool_edit_paint.h"

#include "materials.h"   // the flat look's constants
#include "text_shape.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// The rules are at the declarations (cool_edit_paint.h).

void paint_ce_time_field(cairo_t* cr, const GuiRect& f) {
    if (f.w <= 0 || f.h <= 0) return;
    // THE FLAT KEY FACE, no ring (the declaration's rule).
    paint_cell_rect(cr, f, hex(kKeyFace));
}


namespace {

// THE STAIRCASE'S ROW `r` (0 the top, rows − 1 the apex) as its half-open
// span of columns round the stem column `col`, in units of u: the apex row
// the stem's own cell, each row above it one cell wider on each side
// (paint_ce_cue_triangle's head). The one construction both roads read, for
// the fill's staircase and the outline's larger one alike.
struct CueRowSpan {
    int x0 = 0, x1 = 0;
};
CueRowSpan cue_row_span(int col, int r, int rows, int u) {
    const int k = rows - 1 - r;   // cells each side of the stem's
    return CueRowSpan{col - k * u, col + (k + 1) * u};
}

// The pixel cairo's fill writes for an opaque `c` set by set_palette_source
// (cairo_set_source_rgb of its window color): each channel's 16-bit short,
// d x 65535 + 0.5 truncated, its high byte (cairo-color.c's
// _cairo_color_double_to_short, cairo-image-compositor.c's color_to_uint32).
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

// CAIRO'S ROAD, one whole-cell rectangle per row: where the target is
// refused (the identity at CueTrianglePass, cool_edit_paint.h). One
// staircase of `rows` rows from `top`; an outlined chip is two calls, the
// outline's staircase first.
void paint_cue_cells_cairo(cairo_t* cr, int col, int top, int rows,
                           GuiColor color) {
    const int u = cue_triangle_unit_px();   // one device px
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_new_path(cr);
    for (int r = 0; r < rows; ++r) {
        const CueRowSpan sp = cue_row_span(col, r, rows, u);
        cairo_rectangle(cr, sp.x0, top + r * u, sp.x1 - sp.x0, u);
    }
    set_palette_source(cr, color);
    cairo_fill(cr);
    cairo_restore(cr);
}

} // namespace

// THE PASS (the rule and the identity at the declaration, cool_edit_paint.h).
void CueTrianglePass::begin(cairo_t* cr) {
    cr_        = cr;
    u_         = cue_triangle_unit_px();
    rows_      = cue_triangle_rows();
    top_rows_  = cue_chip_top_rows();
    chip_rows_ = cue_chip_rows();
    direct_    = direct_pixel_target(cr, target_);
    dirty_     = GuiRect{};
    dirty_any_ = false;
}

// One row's cells [x0, x1) at device row band [y, y + u) in `pixel`, inside
// the target's clip boxes, the dirty box grown by what was written.
void CueTrianglePass::stamp_span(int x0, int x1, int y, uint32_t pixel) {
    for (const GuiRect& r : target_.clip) {
        const int cx0 = std::max(x0, r.x);
        const int cx1 = std::min(x1, r.x + r.w);
        const int cy0 = std::max(y, r.y);
        const int cy1 = std::min(y + u_, r.y + r.h);
        if (cx1 <= cx0 || cy1 <= cy0) continue;
        for (int yy = cy0; yy < cy1; ++yy) {
            uint32_t* const row = reinterpret_cast<uint32_t*>(
                target_.data + static_cast<std::size_t>(yy) * target_.stride);
            std::fill(row + cx0, row + cx1, pixel);
        }
        const GuiRect box{cx0, cy0, cx1 - cx0, cy1 - cy0};
        if (!dirty_any_) {
            dirty_     = box;
            dirty_any_ = true;
        } else {
            const int ax0 = std::min(dirty_.x, box.x);
            const int ay0 = std::min(dirty_.y, box.y);
            const int ax1 = std::max(dirty_.x + dirty_.w, box.x + box.w);
            const int ay1 = std::max(dirty_.y + dirty_.h, box.y + box.h);
            dirty_ = GuiRect{ax0, ay0, ax1 - ax0, ay1 - ay0};
        }
    }
}

void CueTrianglePass::triangle(int col, int top, GuiColor fill,
                               std::optional<GuiColor> outline) {
    // THE OUTLINE'S STAIRCASE FIRST, from t rows above the fill's top, then
    // the fill's over it (the chip's construction, render.h's
    // cue_chip_rows): on both roads the later cells overwrite the earlier.
    const int otop = top - top_rows_ * u_;
    if (!direct_) {
        if (outline)
            paint_cue_cells_cairo(cr_, col, otop, chip_rows_, *outline);
        paint_cue_cells_cairo(cr_, col, top, rows_, fill);
        return;
    }
    // THE STAMP: the outline's cells, then the fill's.
    const int dcol = col + target_.dx;
    if (outline) {
        const int dtop = otop + target_.dy;
        const uint32_t op = cue_fill_pixel(*outline);
        for (int r = 0; r < chip_rows_; ++r) {
            const CueRowSpan sp = cue_row_span(dcol, r, chip_rows_, u_);
            stamp_span(sp.x0, sp.x1, dtop + r * u_, op);
        }
    }
    const int dtop = top + target_.dy;
    const uint32_t cp = cue_fill_pixel(fill);
    for (int r = 0; r < rows_; ++r) {
        const CueRowSpan sp = cue_row_span(dcol, r, rows_, u_);
        stamp_span(sp.x0, sp.x1, dtop + r * u_, cp);
    }
}

void CueTrianglePass::end() {
    if (dirty_any_)
        cairo_surface_mark_dirty_rectangle(target_.surface, dirty_.x, dirty_.y,
                                           dirty_.w, dirty_.h);
    dirty_any_ = false;
}

void paint_ce_cue_triangle(cairo_t* cr, int col, int top, GuiColor fill,
                           std::optional<GuiColor> outline) {
    CueTrianglePass pass;
    pass.begin(cr);
    pass.triangle(col, top, fill, outline);
    pass.end();
}
