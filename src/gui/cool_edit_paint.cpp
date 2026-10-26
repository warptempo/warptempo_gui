#include "cool_edit_paint.h"

#include "materials.h"   // the key's constants, the accent, the inks
#include "text_shape.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// The rules are at the declarations (cool_edit_paint.h); the construction is
// the approved mock's, each W of it a
// rounded part here.

namespace {

// The mean of two words, per channel, truncated (numpy's integer mean of
// the mock's two rows).
uint32_t mean_word(uint32_t a, uint32_t b) {
    uint32_t out = 0;
    for (int s = 0; s <= 16; s += 8)
        out |= ((((a >> s) & 0xFF) + ((b >> s) & 0xFF)) / 2) << s;
    return out;
}

} // namespace

GuiRect key_led_rect(const GuiRect& r, int depth) {
    const int left = scaled_px(2, 1);
    const int up   = scaled_px(2, 1);
    const int w    = scaled_px(2, 1);
    const int h    = scaled_px(1, 1);
    const int y1   = r.y + r.h - 1;   // the outline's bottom row
    const GuiRect led{r.x + left, y1 - up - h + 1, w, h};
    // Inside the outline and above the foot at rest, or nothing.
    if (led.x + led.w > r.x + r.w - 2 || led.y < r.y + 3 ||
        led.y + led.h > r.y + r.h - 3)
        return GuiRect{};
    // THE LED RIDES WITH THE FACE, `depth` device px (the declaration).
    const int ride = depth * hairline_px();
    return GuiRect{led.x + ride, led.y + ride, led.w, led.h};
}

void paint_key_plain(cairo_t* cr, const GuiRect& r, int depth,
                     GuiKeyLed led) {
    if (r.w <= 0 || r.h <= 0) return;
    const int px = hairline_px();
    const bool down = depth > 0;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // EVERY TONE OF THE KEY IS FLAT, never a ground's (materials.h's KEY).
    const GuiMaterialScope flat(GuiMaterialRegion::Flat);
    const uint32_t face = down ? kKeyFacePressed : kKeyFace;
    paint_ink_rect(cr, r, hex(kKeyOutline));
    const GuiRect in{r.x + px, r.y + px, r.w - 2 * px, r.h - 2 * px};
    if (in.w > 0 && in.h > 0) {
        paint_ink_rect(cr, in, hex(face));
        if (down) {
            // THE HOLE'S WALL: `depth` device px of the foot tone along the
            // top row and the left column inside the outline, the face
            // filling the rest (the declaration's CHECKED and HELD rows).
            const int wall = std::min({depth * px, in.w, in.h});
            paint_ink_rect(cr, GuiRect{in.x, in.y, in.w, wall}, hex(kKeyFoot));
            paint_ink_rect(cr, GuiRect{in.x, in.y, wall, in.h}, hex(kKeyFoot));
        } else {
            // THE LIGHT TOP EDGE, the foot and the right column (the
            // declaration's order), each one device px a row or column.
            paint_ink_rect(cr, GuiRect{in.x, in.y, in.w, px}, hex(kKeyTopLight));
            if (in.h > 2 * px)
                paint_ink_rect(cr, GuiRect{in.x, in.y + px, in.w, px},
                               hex(mean_word(kKeyTopLight, face)));
            if (in.h > 4 * px)
                paint_ink_rect(cr, GuiRect{in.x, in.y + in.h - 2 * px, in.w, 2 * px},
                               hex(kKeyFoot));
            paint_ink_rect(cr, GuiRect{in.x + in.w - px, in.y, px, in.h},
                           hex(kKeyFoot));
        }
    }
    if (led != GuiKeyLed::None) {
        const GuiRect l = key_led_rect(r, depth);
        if (l.w > 0 && l.h > 0)
            paint_ink_rect(cr, l,
                           hex(led == GuiKeyLed::Lit ? materials::accent_color()
                                                     : kLedUnlit));
    }
    cairo_restore(cr);
}

int key_depth(bool pressed, bool checked) {
    return pressed ? 2 : checked ? 1 : 0;
}

void paint_key_at_depth(cairo_t* cr, const GuiRect& r, int depth,
                        GuiKeyLed led) {
    // A lamp key's patch carries its LED by depth (dead at rest, lit down,
    // the declaration's rows), so the lamp is the patch's key with the depth.
    if (materials::paint_key_patch(cr, r, depth, led != GuiKeyLed::None))
        return;
    paint_key_plain(cr, r, depth, led);
}

void paint_key(cairo_t* cr, const GuiRect& r, bool pressed, GuiKeyLed led) {
    // THE LED LIGHTS WHILE HELD (the declaration's HELD row); the depth from
    // the press arm first, then the lit LED (the CHECKED row).
    const int depth = key_depth(pressed, led == GuiKeyLed::Lit);
    if (pressed && led == GuiKeyLed::Unlit) led = GuiKeyLed::Lit;
    paint_key_at_depth(cr, r, depth, led);
}

void paint_ce_time_field(cairo_t* cr, const GuiRect& f, GuiColor dark,
                         GuiColor light) {
    if (f.w <= 0 || f.h <= 0) return;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    const GuiMaterialScope flat(GuiMaterialRegion::Flat);
    // THE GROUND BLACK (kTimeFieldGround), then the one-device-px sunken
    // ring, mitred (the head's rule).
    paint_ink_rect(cr, f, hex(kTimeFieldGround));
    paint_hairline_frame(cr, f, dark, light);
    cairo_restore(cr);
}


namespace {

// THE STAIRCASE'S ROW `r` (0 the top, rows − 1 the apex) as its half-open
// span of columns round the stem column `col`, in units of u: the apex row
// the stem's own cell, each row above it one cell wider on each side
// (paint_ce_cue_triangle's head). The one construction both roads read.
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

// CAIRO'S ROAD, one whole-cell rectangle per row, the shadow's first, `so`
// right and `so` down: where the target is refused (the identity at
// CueTrianglePass, cool_edit_paint.h). `split` the column from which
// `right` takes over (cells at or past it), or INT_MAX for one color.
void paint_cue_cells_cairo(cairo_t* cr, int col, int top, int rows, int so,
                           GuiColor left, GuiColor right, int split,
                           GuiColor shadow) {
    const int u = cue_triangle_unit_px();   // one device px
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_new_path(cr);
    for (int r = 0; r < rows; ++r) {
        const CueRowSpan sp = cue_row_span(col, r, rows, u);
        cairo_rectangle(cr, sp.x0 + so, top + r * u + so, sp.x1 - sp.x0, u);
    }
    set_palette_source(cr, shadow);
    cairo_fill(cr);
    const auto half = [&](GuiColor c, bool right_half) {
        cairo_new_path(cr);
        for (int r = 0; r < rows; ++r) {
            const CueRowSpan sp = cue_row_span(col, r, rows, u);
            const int x0 = right_half ? std::max(sp.x0, split) : sp.x0;
            const int x1 = right_half ? sp.x1 : std::min(sp.x1, split);
            if (x1 > x0) cairo_rectangle(cr, x0, top + r * u, x1 - x0, u);
        }
        set_palette_source(cr, c);
        cairo_fill(cr);
    };
    half(left, false);
    if (split != std::numeric_limits<int>::max()) half(right, true);
    cairo_restore(cr);
}

} // namespace

// THE PASS (the rule and the identity at the declaration, cool_edit_paint.h).
void CueTrianglePass::begin(cairo_t* cr) {
    cr_        = cr;
    u_         = cue_triangle_unit_px();
    rows_      = cue_triangle_rows();
    so_        = cue_label_shadow_offset_px();
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

void CueTrianglePass::triangle(int col, int top, GuiColor color,
                               GuiColor shadow) {
    cells(col, top, color, color, std::numeric_limits<int>::max(), shadow);
}

void CueTrianglePass::split(int col, int top, GuiColor left, GuiColor right,
                            GuiColor shadow) {
    // The cells right of the apex column take `right`, the apex column the
    // left's (the declaration's rule).
    cells(col, top, left, right, col + u_, shadow);
}

void CueTrianglePass::cells(int col, int top, GuiColor left, GuiColor right,
                            int split_x, GuiColor shadow) {
    if (!direct_) {
        paint_cue_cells_cairo(cr_, col, top, rows_, so_, left, right, split_x,
                              shadow);
        return;
    }
    // THE STAMP: the shadow's cells `so_` right and `so_` down, then the
    // color's.
    const bool two  = split_x != std::numeric_limits<int>::max();
    const int  dcol = col + target_.dx, dtop = top + target_.dy;
    const int  dsplit = two ? split_x + target_.dx : split_x;
    const uint32_t sh = cue_fill_pixel(shadow);
    const uint32_t lp = cue_fill_pixel(left);
    const uint32_t rp = cue_fill_pixel(right);
    for (int r = 0; r < rows_; ++r) {
        const CueRowSpan sp = cue_row_span(dcol, r, rows_, u_);
        stamp_span(sp.x0 + so_, sp.x1 + so_, dtop + r * u_ + so_, sh);
    }
    for (int r = 0; r < rows_; ++r) {
        const CueRowSpan sp = cue_row_span(dcol, r, rows_, u_);
        const int y = dtop + r * u_;
        stamp_span(sp.x0, std::min(sp.x1, dsplit), y, lp);
        if (two) stamp_span(std::max(sp.x0, dsplit), sp.x1, y, rp);
    }
}

void CueTrianglePass::end() {
    if (dirty_any_)
        cairo_surface_mark_dirty_rectangle(target_.surface, dirty_.x, dirty_.y,
                                           dirty_.w, dirty_.h);
    dirty_any_ = false;
}

void paint_ce_cue_triangle(cairo_t* cr, int col, int top, GuiColor color,
                           GuiColor shadow) {
    CueTrianglePass pass;
    pass.begin(cr);
    pass.triangle(col, top, color, shadow);
    pass.end();
}

void show_ce_label(cairo_t* cr, const text_shape::ShapedRun& run, double x,
                   double baseline) {
    // The cue label's shadow (the declaration's rule; show_cue_label,
    // render.cpp, paints the same pair under the cues).
    const double t = static_cast<double>(cue_label_shadow_offset_px());
    set_palette_source(cr, palette().ce_cue_shadow);
    text_shape::show_shaped_run(cr, run, x + t, baseline + t);
    set_palette_source(cr, hex(kBoneInk));
    text_shape::show_shaped_run(cr, run, x, baseline);
}
