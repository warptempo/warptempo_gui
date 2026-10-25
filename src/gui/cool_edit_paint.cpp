#include "cool_edit_paint.h"

#include "materials.h"   // the key's constants, the accent, the inks
#include "text_shape.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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


// THE TRIANGLE'S COVERAGE STAMP (2026-10-13, a cost cut that paints the same
// pixels: the tablet's profile of a pinch put 11 % of all CPU on the cues'
// antialiased triangle fills, cairo's scan converter run twice per marker
// for one shape). The shape is the same at every column: its vertices are
// col + 0.5 - 4.5u, col + 0.5 + 4.5u and col + 0.5 at whole rows from the
// whole-row top, so their fractional parts never change, and the shadow is
// the same shape one whole quantum right. Its antialiased COVERAGE is
// therefore computed ONCE per (quantum, rows) by cairo itself — the path
// filled in opaque white on a zeroed ARGB32 surface, the byte left being
// the coverage (cairo's lerp of white through c onto zero is c) — and each
// triangle is then composited through it with cairo's own arithmetic.
//
// THE IDENTITY, the one statement of it: an opaque solid filled with OVER
// and antialias DEFAULT on an ARGB32 image surface under a clip of boxes
// takes cairo's spans compositor (the clip clips the polygon, no clip path,
// so the renderer needs no clip) and its in-place renderer, which reduces
// the fill to the source (fill_reduces_to_source) and writes each span
// through _fill_xrgb32_lerp_opaque_spans: a coverage of 255 stores the
// pixel, any other nonzero coverage a stores lerp8x4(pixel, a, dst)
// (cairo-image-compositor.c, the same in 1.18.4 on the tablet and 1.18.6 on
// the laptop), the pixel the color's 16-bit shorts' high bytes
// (_cairo_color_compute_shorts, color_to_uint32). The coverage cairo's scan
// converter hands those spans is a function of the polygon's position
// relative to the pixel grid alone, which every column shares. The stamp
// writes exactly those words into the target's own pixels, the shadow
// first and then the color, as the two fills did — for a triangle whose box
// lies whole inside one of the clip's boxes, or meets none
// (cue_triangle_box_uncut: where the clip cuts the polygon cairo's coverage
// of the cut can differ, so a cut triangle fills through cairo).
// Verified byte for byte against cairo's fills over random grounds, under
// cutting and multi-box clips, triangles overlapping, at scales 100 to
// 400 % (2026-10-13). Taken only where direct_pixel_target holds (render.h);
// anything else fills through cairo as before. The one cached coverage is
// the GUI thread's (the flag cache and the frame's playhead head). At file
// scope so CueTrianglePass (cool_edit_paint.h) can hold the one it looked up.
struct CueTriangleCoverage {
    int u = 0, rows = 0;
    int dx = 0, dy = 0;   // the box's origin relative to (col, top)
    int w = 0, h = 0;
    std::vector<uint8_t> cov;
};

namespace {

// The triangle's path at axis `axis`, top `y`, offset `dx` — the one
// construction both the fill and the coverage read.
void cue_triangle_path(cairo_t* cr, double axis, double half, double y,
                       double depth, double dx) {
    cairo_new_path(cr);
    cairo_move_to(cr, axis - half + dx, y);
    cairo_line_to(cr, axis + half + dx, y);
    cairo_line_to(cr, axis + dx, y + depth);
    cairo_close_path(cr);
}

const CueTriangleCoverage& cue_triangle_coverage(int u, int rows) {
    static CueTriangleCoverage c;
    if (c.u == u && c.rows == rows && !c.cov.empty()) return c;
    c = CueTriangleCoverage{};
    c.u = u;
    c.rows = rows;
    // A box one device px past the shape on every side.
    const int reach = (2 * rows - 1) * u / 2 + 2;
    c.dx = -reach;
    c.dy = -1;
    c.w  = 2 * reach + 1;
    c.h  = rows * u + 2;
    cairo_surface_t* const s =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, c.w, c.h);
    cairo_t* const cr = cairo_create(s);
    const int col = -c.dx;
    const int top = -c.dy;
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cue_triangle_path(cr, col + 0.5, (2 * rows - 1) * u / 2.0, top,
                      rows * static_cast<double>(u), 0.0);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    const unsigned char* const data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    c.cov.resize(static_cast<std::size_t>(c.w) * static_cast<std::size_t>(c.h));
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x)
            c.cov[static_cast<std::size_t>(y * c.w + x)] =
                static_cast<uint8_t>(reinterpret_cast<const uint32_t*>(
                                         data + y * stride)[x] >> 24);
    cairo_surface_destroy(s);
    return c;
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

// cairo's lerp8x4, verbatim in arithmetic (cairo-image-compositor.c).
inline uint32_t cue_mul8x2_8(uint32_t a, uint8_t b) {
    const uint32_t t = (a & 0x00ff00ffu) * b + 0x007f007fu;
    return ((t + ((t >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
}
inline uint32_t cue_add8x2_8x2(uint32_t a, uint32_t b) {
    uint32_t t = a + b;
    t |= 0x01000100u - ((t >> 8) & 0x00ff00ffu);
    return t & 0x00ff00ffu;
}
inline uint32_t cue_lerp8x4(uint32_t src, uint8_t a, uint32_t dst) {
    const uint8_t na = static_cast<uint8_t>(~a);
    return cue_add8x2_8x2(cue_mul8x2_8(src, a), cue_mul8x2_8(dst, na)) |
           cue_add8x2_8x2(cue_mul8x2_8(src >> 8, a),
                          cue_mul8x2_8(dst >> 8, na))
               << 8;
}

// Whether the triangle's box lies whole inside one of the clip's boxes or
// meets none: a box the clip CUTS takes cairo's fill, whose polygon the clip
// clips — and cairo's coverage of the first column inside a clipped edge is
// not always the unclipped shape's (measured 2026-10-13: one byte, at the
// canvas column's left edge).
bool cue_triangle_box_uncut(const DirectPixelTarget& t, const GuiRect& box) {
    bool meets = false;
    for (const GuiRect& r : t.clip) {
        if (box.x + box.w <= r.x || box.x >= r.x + r.w ||
            box.y + box.h <= r.y || box.y >= r.y + r.h)
            continue;
        if (box.x >= r.x && box.x + box.w <= r.x + r.w && box.y >= r.y &&
            box.y + box.h <= r.y + r.h)
            return true;
        meets = true;
    }
    return !meets;
}

// One stamp of the coverage at device (col, top) in `pixel`, inside the
// target's clip boxes.
void stamp_cue_triangle(const DirectPixelTarget& t, const CueTriangleCoverage& c,
                        int col, int top, uint32_t pixel) {
    const int bx0 = col + c.dx, by0 = top + c.dy;
    for (const GuiRect& r : t.clip) {
        const int x0 = std::max(bx0, r.x);
        const int x1 = std::min(bx0 + c.w, r.x + r.w);
        const int y0 = std::max(by0, r.y);
        const int y1 = std::min(by0 + c.h, r.y + r.h);
        for (int y = y0; y < y1; ++y) {
            uint32_t* const row =
                reinterpret_cast<uint32_t*>(t.data + static_cast<std::size_t>(y) * t.stride);
            // The coverage row's own start, read at x − bx0 (never a pointer
            // formed before the array, 2026-10-13).
            const uint8_t* const cov =
                c.cov.data() + static_cast<std::size_t>((y - by0) * c.w);
            for (int x = x0; x < x1; ++x) {
                const uint8_t a = cov[x - bx0];
                if (a == 0) continue;
                row[x] = a == 0xff ? pixel : cue_lerp8x4(pixel, a, row[x]);
            }
        }
    }
}

// THE ORDINARY TRIANGLE ON CAIRO'S ROAD: the two antialiased fills, the
// shadow first — where the target is refused, and for a box the clip cuts.
void paint_cue_triangle_cairo(cairo_t* cr, int col, int top, GuiColor color,
                              GuiColor shadow) {
    const double u    = cue_unit_px();
    const int    rows = kProgramSpec.cue_triangle_rows;
    // THE AXIS IS THE STEM PIXEL'S CENTRE (architect 2026-10-09 ~14:35: "at
    // every scale, the marker triangle must be fully centered on the stem"):
    // the stem is one device px at `col`, so the axis is col + 0.5 and the
    // top edge's 2·rows − 1 quanta stand half to each side of it.
    const double axis = col + 0.5;
    const double half = (2 * rows - 1) * u / 2.0;   // 4.5u
    const double y    = top;
    // THE STAIRCASE'S ENVELOPE, offset `dx`: (axis − 4.5u, top),
    // (axis + 4.5u, top) and the apex (axis, top + 5u).
    const auto triangle = [&](double dx, GuiColor c) {
        cue_triangle_path(cr, axis, half, y, rows * u, dx);
        set_palette_source(cr, c);
        cairo_fill(cr);
    };
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    triangle(u, shadow);
    triangle(0.0, color);
    cairo_restore(cr);
}

// THE SPLIT TRIANGLE, always cairo's road (its group's arithmetic is not the
// stamp's): the rule at CueTrianglePass::split.
void paint_cue_split_triangle_cairo(cairo_t* cr, double axis, int top,
                                    GuiColor left, GuiColor right,
                                    GuiColor shadow) {
    const double u    = cue_unit_px();
    const int    rows = kProgramSpec.cue_triangle_rows;
    const double half = (2 * rows - 1) * u / 2.0;   // 4.5u, as above
    const double y    = top;
    const double apex = y + rows * u;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    // The shadow whole, one quantum right, as the unsplit triangle's.
    cairo_new_path(cr);
    cairo_move_to(cr, axis - half + u, y);
    cairo_line_to(cr, axis + half + u, y);
    cairo_line_to(cr, axis + u, apex);
    cairo_close_path(cr);
    set_palette_source(cr, shadow);
    cairo_fill(cr);
    // THE TWO HALVES SUMMED IN A GROUP, then laid over the shadow as one:
    // each half's antialiased coverage is ADDED, so the axis column takes
    // its share of each color with no seam of ground between them and the
    // outer edges carry one color alone (painting one half over the other
    // would leave a seam, or a fringe of the under color on the over half's
    // edges). THE GROUP IS CLIPPED TO ITS OWN BOX (2026-10-12): cairo sizes
    // the group's surface to the clip's extents, so an unclipped group was a
    // strip-sized scratch surface, zeroed and composited back, per pair (the
    // history view's pan / zoom cost). The box is the two halves' integer
    // bounds with one device px of margin for the antialiased coverage; the
    // picture is unchanged.
    cairo_rectangle(cr, std::floor(axis - half) - 1.0, y - 1.0,
                    std::ceil(axis + half) + 1.0 - (std::floor(axis - half) - 1.0),
                    std::ceil(apex) + 1.0 - (y - 1.0));
    cairo_clip(cr);
    cairo_push_group(cr);
    const auto half_tri = [&](double x_out, GuiColor c) {
        cairo_new_path(cr);
        cairo_move_to(cr, x_out, y);
        cairo_line_to(cr, axis, y);
        cairo_line_to(cr, axis, apex);
        cairo_close_path(cr);
        set_palette_source(cr, c);
        cairo_fill(cr);
    };
    half_tri(axis - half, left);
    cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
    half_tri(axis + half, right);
    cairo_pop_group_to_source(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_paint(cr);
    cairo_restore(cr);
}

} // namespace

// THE PASS (the rule and the identity at the declaration, cool_edit_paint.h).
void CueTrianglePass::begin(cairo_t* cr) {
    cr_         = cr;
    u_          = cue_unit_px();
    direct_     = direct_pixel_target(cr, target_);
    cov_        = direct_ ? &cue_triangle_coverage(
                                u_, kProgramSpec.cue_triangle_rows)
                          : nullptr;
    dirty_      = GuiRect{};
    dirty_any_  = false;
    flush_owed_ = false;
}

void CueTrianglePass::triangle(int col, int top, GuiColor color,
                               GuiColor shadow) {
    // THE STAMP where the target takes it (the identity is at
    // CueTriangleCoverage): the shadow one quantum right, then the color.
    if (direct_) {
        const CueTriangleCoverage& c = *cov_;
        const int dcol = col + target_.dx, dtop = top + target_.dy;
        // The box both fills reach (the shadow's quantum to the right).
        const GuiRect box{dcol + c.dx, dtop + c.dy, c.w + u_, c.h};
        if (cue_triangle_box_uncut(target_, box)) {
            if (flush_owed_) {
                cairo_surface_flush(target_.surface);
                flush_owed_ = false;
            }
            stamp_cue_triangle(target_, c, dcol + u_, dtop,
                               cue_fill_pixel(shadow));
            stamp_cue_triangle(target_, c, dcol, dtop, cue_fill_pixel(color));
            if (!dirty_any_) {
                dirty_     = box;
                dirty_any_ = true;
            } else {
                const int x0 = std::min(dirty_.x, box.x);
                const int y0 = std::min(dirty_.y, box.y);
                const int x1 = std::max(dirty_.x + dirty_.w, box.x + box.w);
                const int y1 = std::max(dirty_.y + dirty_.h, box.y + box.h);
                dirty_ = GuiRect{x0, y0, x1 - x0, y1 - y0};
            }
            return;
        }
    }
    fallback_begin();
    paint_cue_triangle_cairo(cr_, col, top, color, shadow);
    fallback_end();
}

void CueTrianglePass::split(double axis, int top, GuiColor left,
                            GuiColor right, GuiColor shadow) {
    fallback_begin();
    paint_cue_split_triangle_cairo(cr_, axis, top, left, right, shadow);
    fallback_end();
}

void CueTrianglePass::end() {
    if (dirty_any_)
        cairo_surface_mark_dirty_rectangle(target_.surface, dirty_.x, dirty_.y,
                                           dirty_.w, dirty_.h);
    dirty_any_ = false;
}

// THE HAND-OVER BEFORE A CAIRO FILL: the stamped words so far given back to
// cairo (marked dirty) before cairo reads or writes the buffer.
void CueTrianglePass::fallback_begin() {
    if (!direct_ || !dirty_any_) return;
    cairo_surface_mark_dirty_rectangle(target_.surface, dirty_.x, dirty_.y,
                                       dirty_.w, dirty_.h);
    dirty_any_ = false;
}

// THE HAND-OVER AFTER IT: the surface flushed before the next stamp writes
// (owed, so a pass whose last act is a cairo fill ends as that fill left it).
void CueTrianglePass::fallback_end() {
    if (direct_) flush_owed_ = true;
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
    set_palette_source(cr, hex(kBoneInk));
    text_shape::show_shaped_run(cr, run, x, baseline);
}
