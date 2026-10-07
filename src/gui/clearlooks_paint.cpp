#include "clearlooks_paint.h"

#include "chrome_spec.h"
// kRowHeightPx: the list row the selected cell's tones are recorded at.
#include "folder_overlay.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

using Role = GuiColor GuiPalette::*;

GuiColor tone(Role r) { return palette().*r; }

// A W-px coordinate of a box at device origin `o`, rounded at the element
// (the head's geometry rule).
int at(int o, int w_px) { return o + scaled_px(w_px); }

// A device rect from inclusive W-px cell bounds of a box at (ox, oy).
GuiRect cells(int ox, int oy, int x1, int y1, int x2, int y2) {
    const int xa = std::min(x1, x2), xb = std::max(x1, x2);
    const int ya = std::min(y1, y2), yb = std::max(y1, y2);
    const int dx = at(ox, xa), dy = at(oy, ya);
    return GuiRect{dx, dy, at(ox, xb + 1) - dx, at(oy, yb + 1) - dy};
}

// ge_cairo_rounded_rectangle: the corners in `corners` (tl tr br bl, bits
// 1 2 4 8) rounded at `radius`, the others square.
enum : unsigned { kTL = 1, kTR = 2, kBR = 4, kBL = 8, kAll = 15 };
void rounded_path(cairo_t* cr, double x, double y, double w, double h,
                  double radius, unsigned corners) {
    cairo_new_path(cr);
    if (radius <= 0.0 || corners == 0) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    if (corners & kTL) cairo_move_to(cr, x + radius, y);
    else               cairo_move_to(cr, x, y);
    if (corners & kTR)
        cairo_arc(cr, x + w - radius, y + radius, radius, M_PI * 1.5, M_PI * 2);
    else
        cairo_line_to(cr, x + w, y);
    if (corners & kBR)
        cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, M_PI * 0.5);
    else
        cairo_line_to(cr, x + w, y + h);
    if (corners & kBL)
        cairo_arc(cr, x + radius, y + h - radius, radius, M_PI * 0.5, M_PI);
    else
        cairo_line_to(cr, x, y + h);
    if (corners & kTL)
        cairo_arc(cr, x + radius, y + radius, radius, M_PI, M_PI * 1.5);
    else
        cairo_line_to(cr, x, y);
    cairo_close_path(cr);
}

// A one-W stroke of the current path in `role` (antialiased: its arcs are
// the renderer's).
void stroke_role(cairo_t* cr, Role role) {
    cairo_set_line_width(cr, relief_line_px());
    set_palette_source(cr, tone(role));
    cairo_stroke(cr);
}

// -- THE CAPTION BUTTONS' BOXES (metacity-theme-1.xml's button_bg family) ----
//
// metacity draws each button_bg as one-px <line>s and vertical <gradient>s
// whose ends step in by a cell a row — a staircase of three concentric rings
// round a filled middle: THE HALO (the outer ring, cell rows 0 / h − 1 and
// columns 0 / w − 1 — the top and bottom its two lines, the sides the
// gradient `ramp0` down columns 0 and w − 1), THE BORDER (ring 1, one shade
// all round) and THE INNER BEVEL (ring 2, a tone a side), round THE FILL
// (the upper and lower gradients). Pressed, the halo is its bottom-right half
// alone (the bottom line and the right column's gradient), the bevel the
// inner shadow's top, left and bottom, the fill reaching the right ring.
// THE SCALABLE CHROME (architect 2026-10-07, his glass verdict on P1's cell
// rects: "pixelated" at 300 %; clearlooks_paint.h's head, rule 4) DRAWS THE
// RINGS AS THE ARCS THE STAIRCASE STEPS ALONG: each ring an antialiased
// annulus between two rounded rects one W apart, the corners concentric
// about the point FOUR W in from the box's corner — the radius at which the
// art's three diagonal cells stand inside their rings (the halo's (1, 1) at
// 3.5 W from it, the border's (2, 2) at 2.1, the bevel's (3, 2) / (2, 3) at
// 1.6), so the halo's outer edge is radius 4, the border's 3, the bevel's 2
// and the fill's 1. The art's other corner cells (the in-between tones at the
// staircase's steps, metacity's hand antialiasing) are the arcs' own
// antialiasing and carry no role (build.py CAPTION_BUTTON_DRAWN). Each ring's
// sides take its lines' tones: across the top rows the top's, down the side
// rows the sides', across the bottom rows the bottom's — the corners split
// at the third row from each end, where the art's straight runs begin.
struct ClCbtnFace {
    bool pressed;
    Role halo_top, halo_side0, halo_side1, halo_bottom;
    Role border;
    Role bevel_top, bevel_left, bevel_right, bevel_bottom;   // null: the fill
    Role upper0, upper1, lower0, lower1;   // pressed: one gradient, upper only
};
#define CB(s, r) &GuiPalette::cl_cbtn_##s##_##r
const ClCbtnFace kCbtnFocused{false, CB(focused, s0980), CB(focused, ramp0_0),
    CB(focused, ramp0_1), CB(focused, s1060), CB(focused, s0600),
    CB(focused, s1180), CB(focused, s1100), CB(focused, s1000),
    CB(focused, s0920), CB(focused, ramp1_0), CB(focused, ramp1_1),
    CB(focused, ramp2_0), CB(focused, ramp2_1)};
const ClCbtnFace kCbtnUnfocused{false, CB(unfocused, s0910),
    CB(unfocused, ramp0_0), CB(unfocused, ramp0_1), CB(unfocused, s0960),
    CB(unfocused, s0600), CB(unfocused, s1200), CB(unfocused, s1100),
    CB(unfocused, s1050), CB(unfocused, s0970), CB(unfocused, ramp1_0),
    CB(unfocused, ramp1_1), CB(unfocused, ramp2_0), CB(unfocused, ramp2_1)};
// Pressed: the halo's top-right corner the gradient down column w − 2's first
// row (ramp0), its right side the gradient down column w − 1 (ramp1).
const ClCbtnFace kCbtnPressed{true, CB(pressed, ramp0_0), CB(pressed, ramp1_0),
    CB(pressed, ramp1_1), CB(pressed, s1000), CB(pressed, s0550),
    CB(pressed, s0900), CB(pressed, s0850), nullptr, CB(pressed, s0900),
    CB(pressed, ramp2_0), CB(pressed, ramp2_1), nullptr, nullptr};
const ClCbtnFace kCbtnUnfocusedPressed{true, CB(unfocused_pressed, ramp0_0),
    CB(unfocused_pressed, ramp1_0), CB(unfocused_pressed, ramp1_1),
    CB(unfocused_pressed, s1050), CB(unfocused_pressed, s0550),
    CB(unfocused_pressed, s0800), CB(unfocused_pressed, s0750), nullptr,
    CB(unfocused_pressed, s0850), CB(unfocused_pressed, ramp2_0),
    CB(unfocused_pressed, ramp2_1), nullptr, nullptr};
#undef CB

// The rounded rect `k` W in from the box `b`, its corners concentric about
// the point four W in (radius 4 − k), as a path.
void cbtn_ring_path(cairo_t* cr, const GuiRect& b, int k) {
    const double u = relief_line_px();
    const double s = static_cast<double>(scaled_px(100)) / 100.0;
    rounded_path(cr, b.x + k * u, b.y + k * u, b.w - 2 * k * u,
                 b.h - 2 * k * u, std::max(0.0, (4 - k) * s), kAll);
}
// The annulus of ring `k` (between the rounded rects k and k + 1) as the
// clip.
void cbtn_ring_clip(cairo_t* cr, const GuiRect& b, int k) {
    cbtn_ring_path(cr, b, k);            // a new path: the outer edge
    const double u = relief_line_px();
    const double s = static_cast<double>(scaled_px(100)) / 100.0;
    const double x = b.x + (k + 1) * u, y = b.y + (k + 1) * u;
    const double w = b.w - 2 * (k + 1) * u, h = b.h - 2 * (k + 1) * u;
    const double r = std::max(0.0, (3 - k) * s);
    cairo_new_sub_path(cr);              // the inner edge
    if (r <= 0.0) {
        cairo_rectangle(cr, x, y, w, h);
    } else {
        cairo_arc(cr, x + w - r, y + r, r, M_PI * 1.5, M_PI * 2);
        cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI * 0.5);
        cairo_arc(cr, x + r, y + h - r, r, M_PI * 0.5, M_PI);
        cairo_arc(cr, x + r, y + r, r, M_PI, M_PI * 1.5);
        cairo_close_path(cr);
    }
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_clip(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
}
// One ring's sides under its clip: the top rows [0, 3) in `top`, the rows
// between a ramp from `side0` to `side1` (or `side0` flat when `side1` is
// null), the bottom rows [h − 3, h) in `bottom` — the columns [x0, x1).
void cbtn_ring_sides(cairo_t* cr, const GuiRect& b, int x0, int x1, Role top,
                     Role side0, Role side1, Role bottom) {
    const int u  = relief_line_px();
    const int y3 = b.y + 3 * u, yb = b.y + b.h - 3 * u;
    paint_cell_rect(cr, GuiRect{x0, b.y, x1 - x0, y3 - b.y}, tone(top));
    if (side1) paint_cl_ramp(cr, GuiRect{x0, y3, x1 - x0, yb - y3}, tone(side0),
                             tone(side1));
    else       paint_cell_rect(cr, GuiRect{x0, y3, x1 - x0, yb - y3}, tone(side0));
    paint_cell_rect(cr, GuiRect{x0, yb, x1 - x0, b.y + b.h - yb}, tone(bottom));
}

void paint_cbtn_box(cairo_t* cr, const GuiRect& b, const ClCbtnFace& f) {
    const int u = relief_line_px();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    // THE FILL, inside ring 2: the upper and lower gradients over the rows
    // from ring 3 to the middle and from the middle down (metacity's
    // top_height / 2 arithmetic, h / 2); pressed, the one gradient. Its rect
    // reaches into ring 2, which is painted over it, so the pressed fill
    // shows through ring 2's right side.
    {
        cairo_save(cr);
        cairo_new_path(cr);
        cbtn_ring_path(cr, b, 2);
        cairo_clip(cr);
        const int x0 = b.x + 2 * u, x1 = b.x + b.w - 2 * u;
        const int y0 = b.y + 2 * u, y1 = b.y + b.h - 2 * u;
        if (f.pressed) {
            paint_cl_ramp(cr, GuiRect{x0, y0, x1 - x0, y1 - y0}, tone(f.upper0),
                          tone(f.upper1));
        } else {
            const int mid = at(b.y, live_chrome_spec().caption_button_h_px / 2);
            paint_cl_ramp(cr, GuiRect{x0, y0, x1 - x0, mid - y0}, tone(f.upper0),
                          tone(f.upper1));
            paint_cl_ramp(cr, GuiRect{x0, mid, x1 - x0, y1 - mid},
                          tone(f.lower0), tone(f.lower1));
        }
        cairo_restore(cr);
    }
    // THE INNER BEVEL, ring 2: the left and right columns, then the top and
    // bottom rows over them (the art's corner cells take the top's and the
    // bottom's tones).
    {
        cairo_save(cr);
        cbtn_ring_clip(cr, b, 2);
        const int x0 = b.x + 2 * u, x1 = b.x + b.w - 2 * u;
        const int y0 = b.y + 2 * u, y1 = b.y + b.h - 2 * u;
        const int mid = b.x + b.w / 2;
        paint_cell_rect(cr, GuiRect{x0, y0, mid - x0, y1 - y0}, tone(f.bevel_left));
        if (f.bevel_right)
            paint_cell_rect(cr, GuiRect{mid, y0, x1 - mid, y1 - y0},
                            tone(f.bevel_right));
        paint_cell_rect(cr, GuiRect{x0, y0, x1 - x0, u}, tone(f.bevel_top));
        paint_cell_rect(cr, GuiRect{x0, y1 - u, x1 - x0, u}, tone(f.bevel_bottom));
        cairo_restore(cr);
    }
    // THE BORDER, ring 1, one tone.
    {
        cairo_save(cr);
        cbtn_ring_clip(cr, b, 1);
        paint_cell_rect(cr, b, tone(f.border));
        cairo_restore(cr);
    }
    // THE HALO, ring 0: its top, its sides' gradient and its bottom;
    // pressed, its bottom-right half alone (the inset's own split, along the
    // diagonal from the bottom-left corner to the top-right).
    {
        cairo_save(cr);
        cbtn_ring_clip(cr, b, 0);
        if (f.pressed) {
            const double m = std::min(b.w, b.h);
            cairo_new_path(cr);
            cairo_move_to(cr, b.x, b.y + b.h);
            cairo_line_to(cr, b.x + m / 2, b.y + b.h - m / 2);
            cairo_line_to(cr, b.x + b.w - m / 2, b.y + m / 2);
            cairo_line_to(cr, b.x + b.w, b.y);
            cairo_line_to(cr, b.x + b.w, b.y + b.h);
            cairo_close_path(cr);
            cairo_clip(cr);
        }
        cbtn_ring_sides(cr, b, b.x, b.x + b.w, f.halo_top, f.halo_side0,
                        f.halo_side1, f.halo_bottom);
        cairo_restore(cr);
    }
    cairo_restore(cr);
}

// -- THE CAPTION GLYPHS (metacity-theme-1.xml's *_button_icon draw_ops) ------
//
// X's unfilled <rectangle> (x, y, w, h): its four edges, cells x..x + w by
// y..y + h, inclusive.
void glyph_frame(cairo_t* cr, int ox, int oy, int x, int y, int w, int h,
                 Role role) {
    paint_cell_rect(cr, cells(ox, oy, x, y, x + w, y), tone(role));
    paint_cell_rect(cr, cells(ox, oy, x, y + h, x + w, y + h), tone(role));
    paint_cell_rect(cr, cells(ox, oy, x, y, x, y + h), tone(role));
    paint_cell_rect(cr, cells(ox, oy, x + w, y, x + w, y + h), tone(role));
}
// A filled <rectangle> or an opaque <tint> (x, y, w, h): cells x..x + w − 1.
void glyph_fill(cairo_t* cr, int ox, int oy, int x, int y, int w, int h,
                Role role) {
    if (w <= 0 || h <= 0) return;
    paint_cell_rect(cr, cells(ox, oy, x, y, x + w - 1, y + h - 1), tone(role));
}
// A <line> of `width` W px from cell (x1, y1) to cell (x2, y2): an
// antialiased stroke between the cells' centres, butt caps (the scalable
// form of X's wide line; the head of paint_cl_caption_button).
void glyph_line(cairo_t* cr, int ox, int oy, int x1, int y1, int x2, int y2,
                int width, Role role) {
    const double u = relief_line_px();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    cairo_set_line_width(cr, width * u);
    cairo_new_path(cr);
    cairo_move_to(cr, at(ox, x1) + u / 2, at(oy, y1) + u / 2);
    cairo_line_to(cr, at(ox, x2) + u / 2, at(oy, y2) + u / 2);
    set_palette_source(cr, tone(role));
    cairo_stroke(cr);
    cairo_restore(cr);
}

void paint_caption_glyph(cairo_t* cr, int ox, int oy, int w, int h,
                         ClCaptionGlyph g, bool full) {
    // icon_size = Bmin `max` (height − Bpad x 2); the paddings centre it.
    const int isz = std::max(7, h - 6 * 2);
    const int hp  = (w - isz) / 2;
    const int vp  = (h - isz) / 2;
    const Role dark  = &GuiPalette::cl_cglyph_dark;
    const Role light = full ? &GuiPalette::cl_cglyph_light
                            : &GuiPalette::cl_cglyph_unfocused;
    switch (g) {
    case ClCaptionGlyph::Close:
        if (full) {
            glyph_line(cr, ox, oy, hp, vp, w - hp - 1, h - vp - 1, 4, dark);
            glyph_line(cr, ox, oy, hp, h - vp - 1, w - hp - 1, vp, 4, dark);
            glyph_fill(cr, ox, oy, hp, vp - 1, 2, 1, dark);
            glyph_fill(cr, ox, oy, hp - 1, vp, 1, 2, dark);
            glyph_fill(cr, ox, oy, w - hp - 2, vp - 1, 2, 1, dark);
            glyph_fill(cr, ox, oy, w - hp, vp, 1, 2, dark);
            glyph_fill(cr, ox, oy, hp, h - vp, 2, 1, dark);
            glyph_fill(cr, ox, oy, hp - 1, h - vp - 2, 1, 2, dark);
            glyph_fill(cr, ox, oy, w - hp - 2, h - vp, 2, 1, dark);
            glyph_fill(cr, ox, oy, w - hp, h - vp - 2, 1, 2, dark);
        }
        glyph_line(cr, ox, oy, hp, vp, w - hp - 1, h - vp - 1, 2, light);
        glyph_line(cr, ox, oy, hp, vp, w - hp, h - vp, 1, light);
        glyph_line(cr, ox, oy, hp, h - vp - 1, w - hp - 1, vp, 2, light);
        glyph_line(cr, ox, oy, hp, h - vp - 1, w - hp, vp - 1, 1, light);
        return;
    case ClCaptionGlyph::Maximize:
        if (full) {
            glyph_frame(cr, ox, oy, hp - 1, vp - 1, w - hp * 2 + 1,
                        h - vp * 2 + 1, dark);
            glyph_frame(cr, ox, oy, hp + 1, vp + 2, w - hp * 2 - 3,
                        h - vp * 2 - 4, dark);
        }
        glyph_frame(cr, ox, oy, hp, vp, w - hp * 2 - 1, h - vp * 2 - 1, light);
        paint_cell_rect(cr, cells(ox, oy, hp + 1, vp + 1, w - hp, vp + 1),
                        tone(light));
        return;
    case ClCaptionGlyph::Restore:
        if (full) {
            glyph_frame(cr, ox, oy, hp, vp, w - hp * 2 - 1, h - vp * 2 - 1,
                        dark);
            glyph_frame(cr, ox, oy, hp + 2, vp + 3, w - hp * 2 - 5,
                        h - vp * 2 - 6, dark);
        }
        glyph_frame(cr, ox, oy, hp + 1, vp + 1, w - hp * 2 - 3,
                    h - vp * 2 - 3, light);
        paint_cell_rect(cr, cells(ox, oy, hp + 2, vp + 2, w - hp - 2, vp + 2),
                        tone(light));
        return;
    case ClCaptionGlyph::Minimize:
        if (full)
            glyph_frame(cr, ox, oy, hp - 1, h - vp - 3, w - hp * 2 + 1, 3,
                        dark);
        glyph_fill(cr, ox, oy, hp, h - vp - 2, w - hp * 2, 2, light);
        return;
    }
}

// -- THE TOOL BUTTON'S FACES (build.py TOOL_BUTTON_STATES) -------------------
//
// One drawn state's roles: ACTIVE (shadow IN: the inset ring, the inner
// shadow, no highlight) or not (the shadow rings and the highlight); the
// ramp's two segments' ends, the border; the highlight's row and its column's
// two segments; the inner shadow's three rows, three columns (each two
// segments' ends) and nine corner cells.
struct ClGummyFace {
    bool active;
    Role upper0, upper1, lower0, lower1, border;
    Role hl_row, hl_upper0, hl_upper1, hl_lower0, hl_lower1;
    Role sh_row[3];
    Role sh_col[3][4];
    Role sh_corner[3][3];
};

#define CL_R(p, s) &GuiPalette::cl_button_##p##_##s
#define CL_COL(p, c)                                                        \
    {CL_R(p, shadow_col##c##_upper_0), CL_R(p, shadow_col##c##_upper_1),    \
     CL_R(p, shadow_col##c##_lower_0), CL_R(p, shadow_col##c##_lower_1)}
#define CL_ACTIVE_FACE(p)                                                   \
    ClGummyFace{true, CL_R(p, upper_0), CL_R(p, upper_1), CL_R(p, lower_0), \
                CL_R(p, lower_1), CL_R(p, border), nullptr, nullptr,        \
                nullptr, nullptr, nullptr,                                  \
                {CL_R(p, shadow_row0), CL_R(p, shadow_row1),                \
                 CL_R(p, shadow_row2)},                                     \
                {CL_COL(p, 0), CL_COL(p, 1), CL_COL(p, 2)},                 \
                {{CL_R(p, shadow_corner00), CL_R(p, shadow_corner01),       \
                  CL_R(p, shadow_corner02)},                                \
                 {CL_R(p, shadow_corner10), CL_R(p, shadow_corner11),       \
                  CL_R(p, shadow_corner12)},                                \
                 {CL_R(p, shadow_corner20), CL_R(p, shadow_corner21),       \
                  CL_R(p, shadow_corner22)}}}
const ClGummyFace kClHot{false, CL_R(hot, upper_0), CL_R(hot, upper_1),
                         CL_R(hot, lower_0), CL_R(hot, lower_1),
                         CL_R(hot, border), CL_R(hot, highlight_row),
                         CL_R(hot, highlight_upper_0),
                         CL_R(hot, highlight_upper_1),
                         CL_R(hot, highlight_lower_0),
                         CL_R(hot, highlight_lower_1), {}, {}, {}};
const ClGummyFace kClPressed     = CL_ACTIVE_FACE(pressed);
const ClGummyFace kClHotChecked  = CL_ACTIVE_FACE(hot_checked);
const ClGummyFace kClDeadChecked = CL_ACTIVE_FACE(dead_checked);
#undef CL_R
// The push button's faces (build.py PUSH_BUTTON_STATES), the same layout
// under cl_push_<state>_.
#define CL_R(p, s) &GuiPalette::cl_push_##p##_##s
#define CL_HIGHLIT_FACE(p)                                                  \
    ClGummyFace{false, CL_R(p, upper_0), CL_R(p, upper_1), CL_R(p, lower_0),\
                CL_R(p, lower_1), CL_R(p, border), CL_R(p, highlight_row),  \
                CL_R(p, highlight_upper_0), CL_R(p, highlight_upper_1),     \
                CL_R(p, highlight_lower_0), CL_R(p, highlight_lower_1),     \
                {}, {}, {}}
const ClGummyFace kClPushNormal   = CL_HIGHLIT_FACE(normal);
const ClGummyFace kClPushPressed  = CL_ACTIVE_FACE(pressed);
const ClGummyFace kClPushDisabled = CL_HIGHLIT_FACE(disabled);
#undef CL_HIGHLIT_FACE
#undef CL_ACTIVE_FACE
#undef CL_COL
#undef CL_R

// THE RING ROUND A GUMMY BUTTON (clearlooks_gummy_draw_button's first
// block): reliefstyle 1's two SHADOW rings of the parent's bg (a resting,
// sensitive, non-default button), the default button's ONE ring mix
// (parentbg, spot[1], 0.5), or clearlooks_draw_inset's ring (a pressed or a
// disabled button, and a pressed default one over its ring).
enum class ClRing { Shadow, Default, Inset };

// clearlooks_draw_inset on `r` at `radius` (device px): the ring's top-left
// half dark, its bottom-right half light, split along the diagonal through
// the box's middle (the engine's own clip polygons). Antialiased.
void paint_inset_ring(cairo_t* cr, const GuiRect& r, double radius) {
    const double du = relief_line_px();
    const double m  = std::min(r.w, r.h);
    for (int half = 0; half < 2; ++half) {
        cairo_save(cr);
        cairo_new_path(cr);
        cairo_move_to(cr, r.x, r.y + r.h);
        cairo_line_to(cr, r.x + m / 2, r.y + r.h - m / 2);
        cairo_line_to(cr, r.x + r.w - m / 2, r.y + m / 2);
        cairo_line_to(cr, r.x + r.w, r.y);
        if (half == 0) cairo_line_to(cr, r.x, r.y);
        else           cairo_line_to(cr, r.x + r.w, r.y + r.h);
        cairo_close_path(cr);
        cairo_clip(cr);
        rounded_path(cr, r.x + du / 2, r.y + du / 2, r.w - du, r.h - du,
                     radius, kAll);
        stroke_role(cr, half == 0 ? &GuiPalette::cl_button_inset_dark
                                  : &GuiPalette::cl_button_inset_light);
        cairo_restore(cr);
    }
}

// clearlooks_gummy_draw_button on the box `r` (the header's faces), `h_w`
// its W rows (the tool case's or the push button's, from the spec — the rows
// the import recorded the face's tones at): the ring (ClRing above), the
// gummy ramp over the box's rows 2 .. h − 3 with its step where pixman put
// it (row 2 + (n + 1) / 2, n = h − 4 rows; build.py's pixman_step_row), the
// border (`border`, or the face's own when null), then the highlight or the
// inner shadow; every one-W ring, line and shadow row a relief line.
void paint_gummy(cairo_t* cr, const GuiRect& r, int h_w, const ClGummyFace& f,
                 ClRing ring, Role border = nullptr) {
    const ChromeSpec& spec = live_chrome_spec();
    const int    u   = relief_line_px();
    const double du  = u;
    const double rad = scaled_px(spec.corner_radius_px);
    const int step   = at(r.y, 2 + (h_w - 4 + 1) / 2);
    const int bottom = r.y + r.h - 2 * u;      // the ramp's last row's end
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    switch (ring) {
    case ClRing::Shadow:
        // reliefstyle 1's two rings of the parent's bg, radius + 1.
        rounded_path(cr, r.x + du / 2, r.y + du / 2, r.w - du, r.h - du,
                     rad + du, kAll);
        stroke_role(cr, &GuiPalette::cl_button_ring_outer);
        rounded_path(cr, r.x + 1.5 * du, r.y + 1.5 * du, r.w - 2 * du,
                     r.h - 2 * du, rad + du, kAll);
        stroke_role(cr, &GuiPalette::cl_button_ring_inner);
        break;
    case ClRing::Default:
        // is_default's one ring, radius + 1, on the outer rows.
        rounded_path(cr, r.x + du / 2, r.y + du / 2, r.w - du, r.h - du,
                     rad + du, kAll);
        stroke_role(cr, &GuiPalette::cl_push_default_ring);
        break;
    case ClRing::Inset:
        paint_inset_ring(cr, r, rad + du);
        break;
    }
    // THE FILL, clipped to its rounded rect: the ramp, then (active) the
    // inner shadow's rows, columns and corners over it.
    cairo_save(cr);
    rounded_path(cr, r.x + 2 * du, r.y + 2 * du, r.w - 4 * du, r.h - 4 * du,
                 rad, kAll);
    cairo_clip(cr);
    const int fx = r.x + 2 * u;
    const int fw = r.w - 4 * u;
    const int top = f.active ? r.y + 5 * u : r.y + 2 * u;
    // (active, the ramp's first three rows are the shadow rows' own: the
    // shadow covers the fill's whole width there, so its segment starts
    // under them, at the rows the import recorded)
    paint_cl_ramp(cr, GuiRect{fx, top, fw, step - top}, tone(f.upper0),
                  tone(f.upper1));
    paint_cl_ramp(cr, GuiRect{fx, step, fw, bottom - step}, tone(f.lower0),
                  tone(f.lower1));
    if (f.active) {
        for (int k = 0; k < 3; ++k)
            paint_cell_rect(cr, GuiRect{fx, r.y + (2 + k) * u, fw, u},
                            tone(f.sh_row[k]));
        for (int c = 0; c < 3; ++c) {
            const int cx = fx + c * u;
            paint_cl_ramp(cr, GuiRect{cx, top, u, step - top},
                          tone(f.sh_col[c][0]), tone(f.sh_col[c][1]));
            paint_cl_ramp(cr, GuiRect{cx, step, u, bottom - step},
                          tone(f.sh_col[c][2]), tone(f.sh_col[c][3]));
            for (int k = 0; k < 3; ++k)
                paint_cell_rect(cr, GuiRect{cx, r.y + (2 + k) * u, u, u},
                                tone(f.sh_corner[k][c]));
        }
    }
    cairo_restore(cr);
    // THE BORDER.
    rounded_path(cr, r.x + 1.5 * du, r.y + 1.5 * du, r.w - 3 * du,
                 r.h - 3 * du, rad, kAll);
    stroke_role(cr, border != nullptr ? border : f.border);
    if (!f.active) {
        // THE TOP-LEFT HIGHLIGHT (clearlooks_draw_top_left_highlight on the
        // fill's rect): up the left column, round the top-left corner and
        // along the top row, in its row's baked tone; then the column's
        // straight part over it, its two baked segments.
        const double left = r.x + 2.5 * du, ltop = r.y + 2.5 * du;
        const double lbottom = r.y + r.h - 2 * du - rad;
        const double lright  = r.x + r.w - 2 * du - rad;
        cairo_save(cr);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        cairo_new_path(cr);
        cairo_move_to(cr, left, lbottom);
        if (rad > 0.0)
            cairo_arc(cr, left + rad, ltop + rad, rad, M_PI, M_PI * 1.5);
        else
            cairo_line_to(cr, left, ltop);
        cairo_line_to(cr, lright, ltop);
        stroke_role(cr, f.hl_row);
        cairo_restore(cr);
        const int ctop = static_cast<int>(std::ceil(ltop + rad));
        const int cbot = static_cast<int>(std::floor(lbottom));
        paint_cl_ramp(cr, GuiRect{fx, ctop, u, step - ctop}, tone(f.hl_upper0),
                      tone(f.hl_upper1));
        paint_cl_ramp(cr, GuiRect{fx, step, u, cbot - step}, tone(f.hl_lower0),
                      tone(f.hl_lower1));
    }
    cairo_restore(cr);
}

} // namespace

void paint_cl_ramp(cairo_t* cr, const GuiRect& r, GuiColor top,
                   GuiColor bottom) {
    if (r.w <= 0 || r.h <= 0) return;
    for (int i = 0; i < r.h; ++i) {
        const double t = r.h > 1 ? static_cast<double>(i) / (r.h - 1) : 0.0;
        const auto level = [t](double s, double e) {
            return std::clamp(std::nearbyint((s + (e - s) * t) * 255.0), 0.0,
                              255.0) / 255.0;
        };
        paint_cell_rect(cr, GuiRect{r.x, r.y + i, r.w, 1},
                        GuiColor{level(top.r, bottom.r),
                                 level(top.g, bottom.g),
                                 level(top.b, bottom.b)});
    }
}

void paint_cl_caption_band(cairo_t* cr, const GuiRect& lane, bool focused) {
    const int H = live_chrome_spec().caption_height_px;
    const auto rows = [&](int a, int b) {     // W rows [a, b) of the band
        return GuiRect{lane.x, at(lane.y, a), lane.w,
                       at(lane.y, b) - at(lane.y, a)};
    };
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    if (focused) {
        paint_cell_rect(cr, rows(0, 1), palette().cl_caption_edge);
        paint_cl_ramp(cr, rows(1, H / 2), palette().cl_caption_upper_0,
                      palette().cl_caption_upper_1);
        paint_cl_ramp(cr, rows(H / 2, H - 1), palette().cl_caption_lower_0,
                      palette().cl_caption_lower_1);
        paint_cell_rect(cr, rows(H - 1, H), palette().cl_caption_foot);
    } else {
        paint_cell_rect(cr, rows(0, 1), palette().cl_caption_unfocused_edge);
        paint_cell_rect(cr, rows(1, 2), palette().cl_caption_unfocused_light);
        paint_cl_ramp(cr, rows(2, H / 2),
                      palette().cl_caption_unfocused_upper_0,
                      palette().cl_caption_unfocused_upper_1);
        paint_cl_ramp(cr, rows(H / 2, H - 1),
                      palette().cl_caption_unfocused_lower_0,
                      palette().cl_caption_unfocused_lower_1);
        paint_cell_rect(cr, rows(H - 1, H), palette().cl_caption_unfocused_foot);
    }
    cairo_restore(cr);
}

void paint_cl_caption_button(cairo_t* cr, const GuiRect& b,
                             ClCaptionGlyph glyph, bool focused, bool pressed,
                             bool enabled) {
    const ChromeSpec& spec = live_chrome_spec();
    const int w = spec.caption_button_w_px;
    const int h = spec.caption_button_h_px;
    cairo_save(cr);
    paint_cbtn_box(cr, b, focused ? (pressed ? kCbtnPressed : kCbtnFocused)
                                  : (pressed ? kCbtnUnfocusedPressed
                                             : kCbtnUnfocused));
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_caption_glyph(cr, b.x, b.y, w, h, glyph, focused && enabled);
    cairo_restore(cr);
}

void paint_cl_menubar(cairo_t* cr, const GuiRect& lane) {
    const int u = relief_line_px();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cl_ramp(cr, GuiRect{lane.x, lane.y, lane.w, lane.h - u},
                  palette().cl_menubar_ramp_0, palette().cl_menubar_ramp_1);
    paint_cell_rect(cr, GuiRect{lane.x, lane.y + lane.h - u, lane.w, u},
                    palette().cl_menubar_shadow);
    cairo_restore(cr);
}

void paint_cl_menubar_item(cairo_t* cr, const GuiRect& lane, int x, int w) {
    const ChromeSpec& spec = live_chrome_spec();
    const int    u    = relief_line_px();
    const double du   = u;
    const int    head = spec.menu_row_head_px;
    const int    ih   = spec.menu_row_content_px + 1;    // height + 1
    const int    y    = at(lane.y, head);
    const int    h    = at(lane.y, head + ih) - y;
    const int    step = at(lane.y, head + (ih + 1) / 2);
    const double rad  = scaled_px(spec.corner_radius_px);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    rounded_path(cr, x + du / 2, y + du / 2, w - du, h - du, rad, kTL | kTR);
    cairo_save(cr);
    cairo_clip(cr);
    // The fill under the border's own rows too, so the corners' arcs
    // antialias over the item's colour: the upper segment's first stop on
    // the top border row, the lower segment's last on the bottom one.
    paint_cell_rect(cr, GuiRect{x, y, w, u}, palette().cl_menubaritem_upper_0);
    paint_cl_ramp(cr, GuiRect{x, y + u, w, step - y - u},
                  palette().cl_menubaritem_upper_0,
                  palette().cl_menubaritem_upper_1);
    paint_cl_ramp(cr, GuiRect{x, step, w, y + h - u - step},
                  palette().cl_menubaritem_lower_0,
                  palette().cl_menubaritem_lower_1);
    paint_cell_rect(cr, GuiRect{x, y + h - u, w, u},
                    palette().cl_menubaritem_lower_1);
    cairo_restore(cr);
    rounded_path(cr, x + du / 2, y + du / 2, w - du, h - du, rad, kTL | kTR);
    stroke_role(cr, &GuiPalette::cl_menubaritem_border);
    cairo_restore(cr);
}

void paint_cl_toolbar_band(cairo_t* cr, const GuiRect& band) {
    if (band.w <= 0 || band.h <= 0) return;
    const ChromeSpec& spec = live_chrome_spec();
    const int u = relief_line_px();
    // The step at the band's middle (build.py's pixman_step_row: row
    // (n + 1) / 2 of the n-row band, n even here).
    const int band_w = 2 * spec.icon_row_air_px + spec.toolbar_case_lead_px +
                       spec.toolbar_glyph_px + spec.toolbar_case_trail_y_px;
    const int step = at(band.y, (band_w + 1) / 2);
    const int foot = band.y + band.h - u;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, GuiRect{band.x, band.y, band.w, u},
                    palette().cl_toolbar_light);
    paint_cl_ramp(cr, GuiRect{band.x, band.y + u, band.w, step - band.y - u},
                  palette().cl_toolbar_upper_0, palette().cl_toolbar_upper_1);
    paint_cl_ramp(cr, GuiRect{band.x, step, band.w, foot - step},
                  palette().cl_toolbar_lower_0, palette().cl_toolbar_lower_1);
    paint_cell_rect(cr, GuiRect{band.x, foot, band.w, u},
                    palette().cl_toolbar_shadow);
    cairo_restore(cr);
}

void paint_cl_toolbar_separator(cairo_t* cr, int gap_x, int case_y,
                                int case_h) {
    const ChromeSpec& spec = live_chrome_spec();
    const int u     = relief_line_px();
    const int x     = gap_x + scaled_px(spec.toolbar_separator_x_px);
    const int inset = scaled_px(spec.toolbar_separator_inset_y_px);
    const GuiRect dark{x, case_y + inset, u, case_h - 2 * inset};
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, dark, palette().cl_separator_dark);
    paint_cell_rect(cr, GuiRect{x + u, dark.y, u, dark.h},
                    palette().cl_separator_light);
    cairo_restore(cr);
}

int paint_cl_tool_button(cairo_t* cr, const GuiRect& r, bool lamp,
                         bool pressed, bool hot, bool enabled) {
    const ChromeSpec& spec = live_chrome_spec();
    const int u   = relief_line_px();
    const int h_w = spec.toolbar_case_lead_px + spec.toolbar_glyph_px +
                    spec.toolbar_case_trail_y_px;
    if (!enabled) {
        if (!lamp) return 0;
        paint_gummy(cr, r, h_w, kClDeadChecked, ClRing::Inset);
        return u;
    }
    if (pressed)
        paint_gummy(cr, r, h_w, kClPressed, ClRing::Inset);
    else if (lamp)
        paint_gummy(cr, r, h_w, hot ? kClHotChecked : kClPressed,
                    ClRing::Inset);
    else if (hot)
        paint_gummy(cr, r, h_w, kClHot, ClRing::Shadow);
    return (pressed || lamp) ? u : 0;
}

int paint_cl_push_button(cairo_t* cr, const GuiRect& r, bool pressed,
                         bool enabled, bool is_default) {
    const int h_w = static_cast<int>(live_chrome_spec().push_button_box_px);
    if (!enabled) {
        paint_gummy(cr, r, h_w, kClPushDisabled, ClRing::Inset);
        return 0;
    }
    if (pressed) {
        paint_gummy(cr, r, h_w, kClPushPressed, ClRing::Inset,
                    is_default ? &GuiPalette::cl_push_pressed_default_border
                               : nullptr);
        return relief_line_px();
    }
    if (is_default)
        paint_gummy(cr, r, h_w, kClPushNormal, ClRing::Default,
                    &GuiPalette::cl_push_normal_default_border);
    else
        paint_gummy(cr, r, h_w, kClPushNormal, ClRing::Shadow);
    return 0;
}

void paint_cl_entry(cairo_t* cr, const GuiRect& r, bool focused) {
    paint_cl_entry(cr, r, focused, palette().cl_base);
}

void paint_cl_entry(cairo_t* cr, const GuiRect& r, bool focused,
                    GuiColor base) {
    const double du  = relief_line_px();
    const double rad = scaled_px(live_chrome_spec().corner_radius_px);
    const double ri  = std::max(0.0, rad - du);   // MAX (0, radius − 1)
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    paint_inset_ring(cr, r, rad + du);
    rounded_path(cr, r.x + 2 * du, r.y + 2 * du, r.w - 4 * du, r.h - 4 * du,
                 ri, kAll);
    set_palette_source(cr, base);
    cairo_fill(cr);
    if (focused) {
        rounded_path(cr, r.x + 2.5 * du, r.y + 2.5 * du, r.w - 5 * du,
                     r.h - 5 * du, ri, kAll);
        stroke_role(cr, &GuiPalette::cl_entry_focus_ring);
    } else {
        // Up the left column from the bottom less the radius, round the
        // top-left corner, along the top row to the right less the radius.
        cairo_save(cr);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        cairo_new_path(cr);
        cairo_move_to(cr, r.x + 2.5 * du, r.y + r.h - rad);
        if (ri > 0.0)
            cairo_arc(cr, r.x + 2.5 * du + ri, r.y + 2.5 * du + ri, ri, M_PI,
                      M_PI * 1.5);
        else
            cairo_line_to(cr, r.x + 2.5 * du, r.y + 2.5 * du);
        cairo_line_to(cr, r.x + r.w - rad, r.y + 2.5 * du);
        stroke_role(cr, &GuiPalette::cl_entry_shadow);
        cairo_restore(cr);
    }
    rounded_path(cr, r.x + 1.5 * du, r.y + 1.5 * du, r.w - 3 * du,
                 r.h - 3 * du, rad, kAll);
    stroke_role(cr, focused ? &GuiPalette::cl_entry_focus_border
                            : &GuiPalette::cl_entry_border);
    cairo_restore(cr);
}

void paint_cl_menu(cairo_t* cr, const GuiRect& box) {
    const int u = relief_line_px();
    const GuiPalette& pal = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, box, pal.cl_menu_ground);
    paint_cell_rect(cr, GuiRect{box.x, box.y - u, box.w, u}, pal.cl_menu_frame);
    paint_cell_rect(cr, GuiRect{box.x, box.y, u, box.h}, pal.cl_menu_frame);
    paint_cell_rect(cr, GuiRect{box.x + box.w - u, box.y, u, box.h},
                    pal.cl_menu_frame);
    paint_cell_rect(cr, GuiRect{box.x, box.y + box.h - u, box.w, u},
                    pal.cl_menu_frame);
    cairo_restore(cr);
}

void paint_cl_menu_item(cairo_t* cr, const GuiRect& item) {
    const int u = relief_line_px();
    const int h_w = live_chrome_spec().popup_item_height_px;
    const GuiPalette& pal = palette();
    // The step where pixman put it over the item's own rows (build.py:
    // row (n + 1) / 2 of the n-row item).
    const int step = at(item.y, (h_w + 1) / 2);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cl_ramp(cr, GuiRect{item.x, item.y + u, item.w, step - item.y - u},
                  pal.cl_menuitem_upper_0, pal.cl_menuitem_upper_1);
    paint_cl_ramp(cr, GuiRect{item.x, step, item.w, item.y + item.h - u - step},
                  pal.cl_menuitem_lower_0, pal.cl_menuitem_lower_1);
    paint_cell_rect(cr, GuiRect{item.x, item.y, item.w, u},
                    pal.cl_menuitem_border);
    paint_cell_rect(cr, GuiRect{item.x, item.y + item.h - u, item.w, u},
                    pal.cl_menuitem_border);
    paint_cell_rect(cr, GuiRect{item.x, item.y, u, item.h},
                    pal.cl_menuitem_border);
    paint_cell_rect(cr, GuiRect{item.x + item.w - u, item.y, u, item.h},
                    pal.cl_menuitem_border);
    cairo_restore(cr);
}

void paint_cl_menu_separator(cairo_t* cr, int x, int y, int w) {
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, GuiRect{x, y, w, relief_line_px()},
                    palette().cl_menu_separator);
    cairo_restore(cr);
}

void paint_cl_list(cairo_t* cr, const GuiRect& surf) {
    const int u = std::min({relief_line_px(), surf.w, surf.h});
    const GuiPalette& pal = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, surf, pal.cl_base);
    paint_cell_rect(cr, GuiRect{surf.x, surf.y, surf.w, u}, pal.cl_list_frame);
    paint_cell_rect(cr, GuiRect{surf.x, surf.y, u, surf.h}, pal.cl_list_frame);
    paint_cell_rect(cr, GuiRect{surf.x, surf.y + surf.h - u, surf.w, u},
                    pal.cl_list_frame);
    paint_cell_rect(cr, GuiRect{surf.x + surf.w - u, surf.y, u, surf.h},
                    pal.cl_list_frame);
    cairo_restore(cr);
}

void paint_cl_selected_cell(cairo_t* cr, const GuiRect& r, bool focused) {
    // The step where pixman put it over the row (build.py: row (n + 1) / 2
    // of the n-row row, the list row's own W height).
    const int h_w = static_cast<int>(folder_overlay::kRowHeightPx);
    const int step = at(r.y, (h_w + 1) / 2);
    const GuiPalette& pal = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    if (focused) {
        paint_cl_ramp(cr, GuiRect{r.x, r.y, r.w, step - r.y},
                      pal.cl_list_selected_upper_0, pal.cl_list_selected_upper_1);
        paint_cl_ramp(cr, GuiRect{r.x, step, r.w, r.y + r.h - step},
                      pal.cl_list_selected_lower_0, pal.cl_list_selected_lower_1);
    } else {
        paint_cl_ramp(cr, GuiRect{r.x, r.y, r.w, step - r.y},
                      pal.cl_list_selected_unfocused_upper_0,
                      pal.cl_list_selected_unfocused_upper_1);
        paint_cl_ramp(cr, GuiRect{r.x, step, r.w, r.y + r.h - step},
                      pal.cl_list_selected_unfocused_lower_0,
                      pal.cl_list_selected_unfocused_lower_1);
    }
    cairo_restore(cr);
}

// -- THE LANES, THE SCRUB, THE STATUS BAR AND THE FRAME (the painters round's
//    last part, 2026-10-07; the rules at the declarations) ---------------------

static_assert(kChromeSpecClearlooks.scrub_handle_box_px ==
                  kClScaleSliderLengthPx,
              "GTK's scrub grab is the slider itself: the spec's box is "
              "slider-length");

void paint_cl_trough(cairo_t* cr, const GuiRect& lane) {
    if (lane.w <= 0 || lane.h <= 0) return;
    const int u = relief_line_px();
    const GuiPalette& pal = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, lane, pal.cl_trough_fill);
    // The shadow's two graded rows (W rows 1 and 2), the one ramp rule.
    paint_cl_ramp(cr, GuiRect{lane.x, lane.y + u, lane.w,
                              at(lane.y, 3) - (lane.y + u)},
                  pal.cl_trough_shadow_0, pal.cl_trough_shadow_1);
    paint_cell_rect(cr, GuiRect{lane.x, lane.y, lane.w, u},
                    pal.cl_trough_border);
    paint_cell_rect(cr, GuiRect{lane.x, lane.y + lane.h - u, lane.w, u},
                    pal.cl_trough_border);
    cairo_restore(cr);
}

namespace {

// THE STEPPER'S STEP — the row where pixman put the gummy ramp's step over
// the bar's rows (build.py's pixman_step_row(0, kTrimLaneHeightPx)):
// (n + 1) / 2 of the n-row gradient from row 0, the lane's 16 giving 8.
int stepper_step_w() { return (kTrimLaneHeightPx + 1) / 2; }

// clearlooks_draw_normal_arrow's chevron at (0, 0) pointing DOWN, in device
// px for an arrow box `box_w` W px wide (the engine's width and height both
// GtkRange's arrow-scaling 0.5 of the stepper, truncated to whole px), `s`
// device px a W px.
void normal_arrow_path(cairo_t* cr, int box_w, double s) {
    const double h  = box_w;
    const double aw = std::min(h * 2.0 + std::max(1.0, std::ceil(h * 2.0 / 6.0 * 2.0) / 2.0) / 2.0,
                               static_cast<double>(box_w));
    const double l2 = std::max(1.0, std::ceil(aw / 6.0 * 2.0) / 2.0) / 2.0;
    const double ah = aw / 2.0 + l2;
    const double dy = -ah / 2.0;
    cairo_new_path(cr);
    cairo_move_to(cr, -aw / 2.0 * s, (dy + l2) * s);
    cairo_line_to(cr, (-aw / 2.0 + l2) * s, dy * s);
    cairo_arc_negative(cr, 0.0, (dy + ah - 2 * l2 - 2 * l2 * std::sqrt(2.0)) * s,
                       2 * l2 * s, M_PI_2 + M_PI_4, M_PI_4);
    cairo_line_to(cr, (aw / 2.0 - l2) * s, dy * s);
    cairo_line_to(cr, aw / 2.0 * s, (dy + l2) * s);
    cairo_line_to(cr, 0.0, (dy + ah) * s);
    cairo_close_path(cr);
}

} // namespace

void paint_cl_stepper(cairo_t* cr, const GuiRect& b, bool points_left,
                      bool pressed) {
    if (b.w <= 0 || b.h <= 0) return;
    const GuiPalette& pal = palette();
    const int    u   = relief_line_px();
    const double du  = u;
    const double rad = std::min<double>(scaled_px(live_chrome_spec().corner_radius_px),
                                        std::min(b.w - 2 * du, b.h - 2 * du) / 2.0);
    const unsigned corners = points_left ? (kTL | kBL) : (kTR | kBR);
    const auto R = [pressed](Role normal, Role down) {
        return palette().*(pressed ? down : normal);
    };
    const int step = at(b.y, stepper_step_w());
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    // THE FILL, clipped to its rounded rect one W in: the gummy ramp's two
    // segments over the rows 1 .. h − 2.
    cairo_save(cr);
    rounded_path(cr, b.x + du, b.y + du, b.w - 2 * du, b.h - 2 * du, rad,
                 corners);
    cairo_clip(cr);
    paint_cl_ramp(cr, GuiRect{b.x, b.y + u, b.w, step - b.y - u},
                  R(&GuiPalette::cl_stepper_normal_upper_0,
                    &GuiPalette::cl_stepper_pressed_upper_0),
                  R(&GuiPalette::cl_stepper_normal_upper_1,
                    &GuiPalette::cl_stepper_pressed_upper_1));
    paint_cl_ramp(cr, GuiRect{b.x, step, b.w, b.y + b.h - u - step},
                  R(&GuiPalette::cl_stepper_normal_lower_0,
                    &GuiPalette::cl_stepper_pressed_lower_0),
                  R(&GuiPalette::cl_stepper_normal_lower_1,
                    &GuiPalette::cl_stepper_pressed_lower_1));
    // THE TOP-LEFT HIGHLIGHT (draw_top_left_highlight on the fill's rect):
    // up column 1 from the bottom (less the radius on a rounded bottom-left),
    // round a rounded top-left, along row 1 to the right (less the radius on
    // a rounded top-right), in its row's baked tone; then the column's
    // straight part over it, its two baked segments recorded over the whole
    // column (rows 2 .. h − 2) and cut to the part this stepper draws.
    const double left = b.x + 1.5 * du, top = b.y + 1.5 * du;
    const double bottom = b.y + b.h - du - ((corners & kBL) ? rad : 0.0);
    const double right  = b.x + b.w - du - ((corners & kTR) ? rad : 0.0);
    cairo_save(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    cairo_new_path(cr);
    cairo_move_to(cr, left, bottom);
    if (corners & kTL)
        cairo_arc(cr, left + rad, top + rad, rad, M_PI, M_PI * 1.5);
    else
        cairo_line_to(cr, left, top);
    cairo_line_to(cr, right, top);
    cairo_set_line_width(cr, du);
    set_palette_source(cr, R(&GuiPalette::cl_stepper_normal_highlight_row,
                             &GuiPalette::cl_stepper_pressed_highlight_row));
    cairo_stroke(cr);
    cairo_restore(cr);
    {
        const int ctop = (corners & kTL)
                             ? static_cast<int>(std::ceil(top + rad))
                             : b.y + 2 * u;
        const int cbot = static_cast<int>(std::floor(bottom));
        cairo_save(cr);
        cairo_rectangle(cr, b.x + u, ctop, u, std::max(0, cbot - ctop));
        cairo_clip(cr);
        paint_cl_ramp(cr, GuiRect{b.x + u, b.y + 2 * u, u, step - b.y - 2 * u},
                      R(&GuiPalette::cl_stepper_normal_highlight_upper_0,
                        &GuiPalette::cl_stepper_pressed_highlight_upper_0),
                      R(&GuiPalette::cl_stepper_normal_highlight_upper_1,
                        &GuiPalette::cl_stepper_pressed_highlight_upper_1));
        paint_cl_ramp(cr, GuiRect{b.x + u, step, u, b.y + b.h - u - step},
                      R(&GuiPalette::cl_stepper_normal_highlight_lower_0,
                        &GuiPalette::cl_stepper_pressed_highlight_lower_0),
                      R(&GuiPalette::cl_stepper_normal_highlight_lower_1,
                        &GuiPalette::cl_stepper_pressed_highlight_lower_1));
        cairo_restore(cr);
    }
    cairo_restore(cr);   // the fill's clip
    // THE BORDER, the outer ring at the radius.
    rounded_path(cr, b.x + du / 2, b.y + du / 2, b.w - du, b.h - du, rad,
                 corners);
    cairo_set_line_width(cr, du);
    set_palette_source(cr, R(&GuiPalette::cl_stepper_normal_border,
                             &GuiPalette::cl_stepper_pressed_border));
    cairo_stroke(cr);
    // THE ARROW, centred on the box, the engine's rotation for its
    // direction.
    const double s = static_cast<double>(scaled_px(100)) / 100.0;
    cairo_save(cr);
    cairo_translate(cr, b.x + b.w / 2.0, b.y + b.h / 2.0);
    cairo_rotate(cr, points_left ? M_PI_2 : -M_PI_2);
    normal_arrow_path(cr, kTrimLaneHeightPx / 2, s);
    set_palette_source(cr, pal.cl_stepper_arrow);
    cairo_fill(cr);
    cairo_restore(cr);
    cairo_restore(cr);
}

void paint_cl_slider(cairo_t* cr, const GuiRect& body) {
    if (body.w <= 0 || body.h <= 0) return;
    const GuiPalette& pal = palette();
    const int u = relief_line_px();
    // The step over the slider's gradient from row 1 to row h − 2 (build.py's
    // pixman_step_row(1, kTrimLaneHeightPx − 2)): the first row whose centre
    // is at or past its middle.
    const int sstep = at(body.y, 1 + (kTrimLaneHeightPx - 3 + 1) / 2);
    const int top = body.y + 2 * u, bot = body.y + body.h - 2 * u;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // The fill inside the ring, the ring's rows and columns, the border.
    paint_cl_ramp(cr, GuiRect{body.x, top, body.w, sstep - top},
                  pal.cl_slider_upper_0, pal.cl_slider_upper_1);
    paint_cl_ramp(cr, GuiRect{body.x, sstep, body.w, bot - sstep},
                  pal.cl_slider_lower_0, pal.cl_slider_lower_1);
    for (const int cx : {body.x + u, body.x + body.w - 2 * u}) {
        paint_cl_ramp(cr, GuiRect{cx, top, u, sstep - top},
                      pal.cl_slider_ring_upper_0, pal.cl_slider_ring_upper_1);
        paint_cl_ramp(cr, GuiRect{cx, sstep, u, bot - sstep},
                      pal.cl_slider_ring_lower_0, pal.cl_slider_ring_lower_1);
    }
    paint_cell_rect(cr, GuiRect{body.x + u, body.y + u, body.w - 2 * u, u},
                    pal.cl_slider_ring_top);
    paint_cell_rect(cr, GuiRect{body.x + u, bot, body.w - 2 * u, u},
                    pal.cl_slider_ring_bottom);
    paint_cell_rect(cr, GuiRect{body.x, body.y, body.w, u}, pal.cl_slider_border);
    paint_cell_rect(cr, GuiRect{body.x, body.y + body.h - u, body.w, u},
                    pal.cl_slider_border);
    paint_cell_rect(cr, GuiRect{body.x, body.y, u, body.h}, pal.cl_slider_border);
    paint_cell_rect(cr, GuiRect{body.x + body.w - u, body.y, u, body.h},
                    pal.cl_slider_border);
    // THE GRIPS, where the body holds them (the declaration's 13 W).
    if (body.w >= scaled_px(7 + 2 * 3)) {
        const int mid = body.x + (body.w - u) / 2;
        const int gy0 = at(body.y, 5);
        const int gy1 = body.y + body.h - scaled_px(5);
        for (const int k : {-1, 0, 1})
            paint_cell_rect(cr, GuiRect{mid + k * scaled_px(3), gy0, u,
                                        gy1 - gy0},
                            pal.cl_slider_grip);
    }
    cairo_restore(cr);
}

void paint_cl_well_frame(cairo_t* cr, const GuiRect& area) {
    const int u = relief_line_px();
    const GuiPalette& pal = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, GuiRect{area.x, area.y, area.w, u}, pal.cl_list_frame);
    paint_cell_rect(cr, GuiRect{area.x, area.y + area.h - u, area.w, u},
                    pal.cl_list_frame);
    cairo_restore(cr);
}

void paint_cl_statusbar(cairo_t* cr, const GuiRect& lane) {
    const int top = scaled_px(kBottomRowBorderPx, 1);
    const int u   = relief_line_px();
    const GuiPalette& pal = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, GuiRect{lane.x, lane.y, lane.w, top},
                    pal.cl_separator_dark);
    paint_cell_rect(cr, GuiRect{lane.x, lane.y + top, lane.w, u},
                    pal.cl_separator_light);
    cairo_restore(cr);
}

namespace {

// THE SCALE TROUGH'S ROWS, top to bottom from its top `y`: the inset ring,
// the border, the ramp's kClScaleTroughPx − 4 rows, the border, the ring —
// each one-W line a relief line, the ramp's rows one rounded part.
struct ScaleTroughRows {
    int ring_top, border_top, ramp_top, ramp_bottom, border_bottom, ring_bottom,
        end;
};
ScaleTroughRows scale_trough_rows(int y) {
    const int u = relief_line_px();
    ScaleTroughRows r{};
    r.ring_top      = y;
    r.border_top    = y + u;
    r.ramp_top      = y + 2 * u;
    r.ramp_bottom   = r.ramp_top + scaled_px(kClScaleTroughPx - 4);
    r.border_bottom = r.ramp_bottom;
    r.ring_bottom   = r.border_bottom + u;
    r.end           = r.ring_bottom + u;
    return r;
}
// The thumb's rows above and below the trough (GTK centres the 7 in the 15).
int scale_thumb_margin_px() {
    return scaled_px((kClScaleSliderWidthPx - kClScaleTroughPx) / 2);
}
// The slider's radius in the engine (clearlooks_gummy_draw_slider's literal
// 2.5, and its highlight's 2.0), in W px.
constexpr double kClScaleSliderRadiusPx    = 2.5;
constexpr double kClScaleHighlightRadiusPx = 2.0;

// One part of the scale's trough on [x0, x1) (the declaration's anatomy).
void scale_trough_part(cairo_t* cr, int x0, int x1, int y, bool lower) {
    if (x1 <= x0) return;
    const int u = relief_line_px();
    const ScaleTroughRows r = scale_trough_rows(y);
    const GuiPalette& pal = palette();
    const GuiColor border = lower ? pal.cl_scale_lower_border
                                  : pal.cl_scale_upper_border;
    cairo_save(cr);
    cairo_rectangle(cr, x0, y, x1 - x0, r.end - y);
    cairo_clip(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    paint_inset_ring(cr, GuiRect{x0, y, x1 - x0, r.end - y}, 0.0);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    const int iw = x1 - x0 - 2 * u;
    paint_cell_rect(cr, GuiRect{x0 + u, r.border_top, iw, u}, border);
    paint_cell_rect(cr, GuiRect{x0 + u, r.border_bottom, iw, u}, border);
    paint_cell_rect(cr, GuiRect{x0 + u, r.ramp_top, u, r.ramp_bottom - r.ramp_top},
                    border);
    paint_cell_rect(cr, GuiRect{x1 - 2 * u, r.ramp_top, u,
                                r.ramp_bottom - r.ramp_top},
                    border);
    paint_cl_ramp(cr, GuiRect{x0 + 2 * u, r.ramp_top, x1 - x0 - 4 * u,
                              r.ramp_bottom - r.ramp_top},
                  lower ? pal.cl_scale_lower_0 : pal.cl_scale_upper_0,
                  lower ? pal.cl_scale_lower_1 : pal.cl_scale_upper_1);
    cairo_restore(cr);
}

} // namespace

int cl_scale_trough_h_px() {
    const ScaleTroughRows r = scale_trough_rows(0);
    return r.end;
}
int cl_scale_thumb_w_px() { return scaled_px(kClScaleSliderLengthPx); }
int cl_scale_thumb_h_px() {
    return 2 * scale_thumb_margin_px() + cl_scale_trough_h_px();
}

void paint_cl_scale_trough(cairo_t* cr, const GuiRect& trough, int split) {
    const int s = std::clamp(split, trough.x, trough.x + trough.w);
    scale_trough_part(cr, trough.x, s, trough.y, /*lower=*/true);
    scale_trough_part(cr, s, trough.x + trough.w, trough.y, /*lower=*/false);
}

void paint_cl_scale_thumb(cairo_t* cr, const GuiRect& box) {
    if (box.w <= 0 || box.h <= 0) return;
    const GuiPalette& pal = palette();
    const int    u   = relief_line_px();
    const double du  = u;
    const double s   = static_cast<double>(scaled_px(100)) / 100.0;
    const int    r3  = scaled_px(3);
    const int    right = box.x + box.w, bottom = box.y + box.h;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // THE SHADOW (draw_shadow at the button's radius 3): its last column from
    // row 3 down to the corner, baked over the ground and, across the
    // trough's rows, over each of them; the corner's arc; its last row.
    const GuiRect col{right - u, box.y + r3, u, bottom - r3 - (box.y + r3)};
    paint_cell_rect(cr, col, pal.cl_scale_shadow);
    const ScaleTroughRows tr = scale_trough_rows(box.y + scale_thumb_margin_px());
    paint_cell_rect(cr, GuiRect{col.x, tr.ring_top, u, u},
                    pal.cl_scale_shadow_inset_dark);
    paint_cell_rect(cr, GuiRect{col.x, tr.border_top, u, u},
                    pal.cl_scale_shadow_border);
    paint_cl_ramp(cr, GuiRect{col.x, tr.ramp_top, u, tr.ramp_bottom - tr.ramp_top},
                  pal.cl_scale_shadow_ramp_0, pal.cl_scale_shadow_ramp_1);
    paint_cell_rect(cr, GuiRect{col.x, tr.border_bottom, u, u},
                    pal.cl_scale_shadow_border);
    paint_cell_rect(cr, GuiRect{col.x, tr.ring_bottom, u, u},
                    pal.cl_scale_shadow_inset_light);
    paint_cell_rect(cr, GuiRect{box.x + r3, bottom - u, right - r3 - (box.x + r3), u},
                    pal.cl_scale_shadow);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    cairo_new_path(cr);
    cairo_arc(cr, right - r3, bottom - r3, r3 - du / 2, 0.0, M_PI_2);
    cairo_set_line_width(cr, du);
    set_palette_source(cr, pal.cl_scale_shadow);
    cairo_stroke(cr);
    // THE SLIDER one W in: its rows 0 .. h_s − 1 are the box's 1 .. h − 2.
    const GuiRect sl{box.x + u, box.y + u, box.w - 2 * u, box.h - 2 * u};
    const int hs   = kClScaleSliderWidthPx - 2;            // its W rows
    const int step = at(sl.y, 1 + (hs - 3 + 1) / 2);       // pixman_step_row(1, hs − 2)
    const double rad = kClScaleSliderRadiusPx * s;
    cairo_save(cr);
    rounded_path(cr, sl.x + du, sl.y + du, sl.w - 2 * du, sl.h - 2 * du,
                 std::max(0.0, rad - du), kAll);
    cairo_clip(cr);
    paint_cl_ramp(cr, GuiRect{sl.x, sl.y + u, sl.w, step - sl.y - u},
                  pal.cl_scale_thumb_upper_0, pal.cl_scale_thumb_upper_1);
    paint_cl_ramp(cr, GuiRect{sl.x, step, sl.w, sl.y + sl.h - u - step},
                  pal.cl_scale_thumb_lower_0, pal.cl_scale_thumb_lower_1);
    cairo_restore(cr);
    rounded_path(cr, sl.x + du / 2, sl.y + du / 2, sl.w - du, sl.h - du, rad,
                 kAll);
    set_palette_source(cr, pal.cl_scale_thumb_border);
    cairo_stroke(cr);
    // THE GRIPS (the engine's columns: w_s / 2 − 3 and two more three W
    // apart, an even slider shifting one W right and drawing two; rows 4 ..
    // h_s − 5, both ends full as his capture draws them).
    {
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        const int ws    = kClScaleSliderLengthPx - 2;
        const int shift = ws % 2 == 0 ? 1 : 0;
        const int gy0 = at(sl.y, 4), gy1 = at(sl.y, hs - 4);
        for (int i = 0, bx = ws / 2 - 3 + shift; i < 3 - shift; ++i, bx += 3)
            paint_cell_rect(cr, GuiRect{at(sl.x, bx), gy0, u, gy1 - gy0},
                            pal.cl_scale_thumb_grip);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    }
    // THE TOP-LEFT HIGHLIGHT at radius 2 on the fill's rect: row 1 and the
    // column's arc in the row's tone, the column's straight part its two
    // baked segments (rows 3 .. h_s − 4).
    {
        const double hr = kClScaleHighlightRadiusPx * s;
        const double left = sl.x + 1.5 * du, top = sl.y + 1.5 * du;
        const double lbottom = sl.y + sl.h - du - hr;
        const double lright  = sl.x + sl.w - du - hr;
        cairo_save(cr);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        cairo_new_path(cr);
        cairo_move_to(cr, left, top + hr);
        cairo_arc(cr, left + hr, top + hr, hr, M_PI, M_PI * 1.5);
        cairo_line_to(cr, lright, top);
        cairo_set_line_width(cr, du);
        set_palette_source(cr, pal.cl_scale_thumb_highlight_row);
        cairo_stroke(cr);
        cairo_restore(cr);
        // The two segments over the rows the import recorded them at (3 ..
        // step − 1, step .. h_s − 4), cut to the straight part the arc
        // leaves.
        const int ctop = static_cast<int>(std::ceil(top + hr));
        const int cbot = static_cast<int>(std::floor(lbottom));
        const int r3s  = at(sl.y, 3);
        const int rend = at(sl.y, hs - 3);
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_rectangle(cr, sl.x + u, ctop, u, std::max(0, cbot - ctop));
        cairo_clip(cr);
        paint_cl_ramp(cr, GuiRect{sl.x + u, r3s, u, step - r3s},
                      pal.cl_scale_thumb_highlight_upper_0,
                      pal.cl_scale_thumb_highlight_upper_1);
        paint_cl_ramp(cr, GuiRect{sl.x + u, step, u, rend - step},
                      pal.cl_scale_thumb_highlight_lower_0,
                      pal.cl_scale_thumb_highlight_lower_1);
        cairo_restore(cr);
    }
    cairo_restore(cr);
}

void paint_cl_window_frame(cairo_t* cr, int ox, int oy, int surface_w,
                           int surface_h, int frame_px, int caption_h,
                           bool focused) {
    if (surface_w <= 0 || surface_h <= 0 || frame_px <= 0) return;
    const GuiPalette& pal = palette();
    const int u = relief_line_px();
    const int f = frame_px;
    const int W = surface_w, H = surface_h;
    // THE TITLE BAR'S ROWS in device px (the declaration's mapping): the
    // band's are metacity's rows 0 and 1 (one-W lines) and 2 .. 3 (the
    // band's ground, f − 2u); the caption lane's row k is metacity's
    // kWindowFramePx + k, at the lane's top plus scaled_px(k) (P1's band
    // painter's own mapping) — so the gradients' split (metacity's
    // top_height / 2) and the foot (top_height − 1) land on the rows the
    // caption painter's at() puts them on, and `title_end`, one past the
    // foot, is the lane's foot.
    const int T_w       = kWindowFramePx + live_chrome_spec().caption_height_px;
    const int title_end = f + caption_h;
    const int split     = f + scaled_px(T_w / 2 - kWindowFramePx);
    const int foot      = f + scaled_px(T_w - 1 - kWindowFramePx);
    const auto fill = [&](int x0, int y0, int x1, int y1, GuiColor c) {
        paint_cell_rect(cr, GuiRect{ox + x0, oy + y0, x1 - x0, y1 - y0}, c);
    };
    const auto ramp = [&](int x0, int y0, int x1, int y1, GuiColor a,
                          GuiColor b) {
        paint_cl_ramp(cr, GuiRect{ox + x0, oy + y0, x1 - x0, y1 - y0}, a, b);
    };
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // window_bg.
    fill(0, 0, W, H, pal.ground);
    if (focused) {
        // bevel: the 3d frame below the title (0.88 right and bottom-inner,
        // 1.2 left-inner), the title's light row 1.18 and its side lines
        // (1.1 left, 0.95 right), the two gradients, the 0.7 foot, the 0.55
        // title outline and the 0.45 border outline below it.
        fill(u, H - 2 * u, W - u, H - u, pal.cl_frame_focused_bg0880);
        fill(W - 2 * u, title_end, W - u, H - u, pal.cl_frame_focused_bg0880);
        fill(u, title_end, 2 * u, H - u, pal.cl_frame_focused_bg1200);
        ramp(2 * u, 2 * u, W - 2 * u, split, pal.cl_frame_focused_ramp1_0,
             pal.cl_frame_focused_ramp1_1);
        ramp(2 * u, split, W - 2 * u, foot, pal.cl_frame_focused_ramp0_0,
             pal.cl_frame_focused_ramp0_1);
        fill(u, u, W - u, 2 * u, pal.cl_frame_focused_sel1180);
        fill(u, 2 * u, 2 * u, foot, pal.cl_frame_focused_sel1100);
        fill(W - 2 * u, 2 * u, W - u, foot, pal.cl_frame_focused_sel0950);
        fill(u, foot, W - u, title_end, pal.cl_frame_focused_sel0700);
        fill(0, 0, W, u, pal.cl_frame_focused_sel0550);
        fill(0, 0, u, foot, pal.cl_frame_focused_sel0550);
        fill(W - u, 0, W, foot, pal.cl_frame_focused_sel0550);
        fill(0, foot, u, H, pal.cl_frame_focused_bg0450);
        fill(W - u, foot, W, H, pal.cl_frame_focused_bg0450);
        fill(u, H - u, W - u, H, pal.cl_frame_focused_bg0450);
    } else {
        // bevel_unfocused: the 0.88 right and bottom-inner lines, the 1.05
        // light row, the 1.03 left line, the gradients, the 0.65 foot, and
        // the 0.55 outline round the whole window.
        fill(u, H - 2 * u, W - u, H - u, pal.cl_frame_unfocused_bg0880);
        fill(W - 2 * u, 2 * u, W - u, H - u, pal.cl_frame_unfocused_bg0880);
        fill(u, u, W - u, 2 * u, pal.cl_frame_unfocused_bg1050);
        fill(u, 2 * u, 2 * u, H - u, pal.cl_frame_unfocused_bg1030);
        ramp(2 * u, 2 * u, W - 2 * u, split, pal.cl_frame_unfocused_ramp1_0,
             pal.cl_frame_unfocused_ramp1_1);
        ramp(2 * u, split, W - 2 * u, foot, pal.cl_frame_unfocused_ramp0_0,
             pal.cl_frame_unfocused_ramp0_1);
        fill(u, foot, W - u, title_end, pal.cl_frame_unfocused_bg0650);
        fill(0, 0, W, u, pal.cl_frame_unfocused_bg0550);
        fill(0, H - u, W, H, pal.cl_frame_unfocused_bg0550);
        fill(0, 0, u, H, pal.cl_frame_unfocused_bg0550);
        fill(W - u, 0, W, H, pal.cl_frame_unfocused_bg0550);
    }
    // THE ROUNDED TOP CORNERS (the declaration): the cut cleared, then the
    // outline's arc at 4.5 W and the highlight's at 3.5 W about the centre
    // five W in, each one W wide, antialiased.
    const double s  = static_cast<double>(scaled_px(100)) / 100.0;
    const double du = u;
    const double c  = 5.0 * s;
    const GuiColor dark = focused ? pal.cl_frame_focused_sel0600
                                  : pal.cl_frame_unfocused_bg0550;
    const GuiColor hl_left  = focused ? pal.cl_frame_focused_sel1180
                                      : pal.cl_frame_unfocused_bg1050;
    const GuiColor hl_top_r = focused ? pal.cl_frame_focused_sel1160
                                      : pal.cl_frame_unfocused_bg1040;
    const GuiColor hl_side_r = focused ? pal.cl_frame_focused_sel0980
                                       : pal.cl_frame_unfocused_bg0880;
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    cairo_set_line_width(cr, du);
    for (const bool left : {true, false}) {
        // The corner's centre, and the corner's quarter in cairo's angles.
        const double cx = left ? ox + c : ox + W - c;
        const double cy = oy + c;
        const double a0 = left ? M_PI : M_PI * 1.5;
        const double a1 = a0 + M_PI_2;
        // The box the corner's cells stand in, five W square — wholly in
        // the band (every arc point has a coordinate under kWindowFramePx),
        // so the caption lane's own call draws none of it. The frame's lines
        // and the gradient already stand there; the cut clears everything
        // outside the circle of radius 5 W, and the arcs antialias over the
        // frame's own tones.
        const double bx = left ? ox : ox + W - c;
        cairo_save(cr);
        cairo_rectangle(cr, bx, oy, c, c);
        cairo_clip(cr);
        cairo_new_path(cr);
        cairo_rectangle(cr, bx, oy, c, c);
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, c, 0.0, 2 * M_PI);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_fill(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, c - du / 2, a0, a1);
        set_palette_source(cr, dark);
        cairo_stroke(cr);
        if (left) {
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, c - 1.5 * du, a0, a1);
            set_palette_source(cr, hl_left);
            cairo_stroke(cr);
        } else {
            // The top-right's two tones meet on the diagonal: the top's from
            // the top to 45 degrees, the side's from there down.
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, c - 1.5 * du, a0, a0 + M_PI_4);
            set_palette_source(cr, hl_top_r);
            cairo_stroke(cr);
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, c - 1.5 * du, a0 + M_PI_4, a1);
            set_palette_source(cr, hl_side_r);
            cairo_stroke(cr);
        }
        cairo_restore(cr);
    }
    cairo_restore(cr);
}
