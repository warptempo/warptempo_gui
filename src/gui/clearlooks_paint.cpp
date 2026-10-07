#include "clearlooks_paint.h"

#include "chrome_spec.h"
// kRowHeightPx: the list row the selected cell's tones are recorded at.
#include "folder_overlay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

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

// -- THE CAPTION BUTTONS' DRAW OPS (metacity-theme-1.xml's button_bg family) --
//
// One op of a button_bg draw_ops in the box's W px, w x h the box: a <line>
// of width 1 (inclusive cells x1..x2 by y1..y2) in `a`, or a vertical
// <gradient> over those cells from `a` at its first row to `b` at its last
// (paint_cl_ramp). The four tables below are the XML's ops in its order,
// their coordinates its expressions (width -> w, height -> h, its integer
// division), each colour the role tools/theme_catalog names for it (build.py
// caption_button_line_role: the state and the shade factor x 1000; a
// gradient its state and its index).
struct ClCaptionOp {
    bool ramp;
    int  x1, y1, x2, y2;
    Role a;
    Role b;
};

// metacity draw_ops "button_bg", in its order
std::array<ClCaptionOp, 29> cl_cbtn_focused_ops(int w, int h) {
    return {{
        {true, 0, 3, 0 + w - 1, 3 + h - 6 - 1, &GuiPalette::cl_cbtn_focused_ramp0_0, &GuiPalette::cl_cbtn_focused_ramp0_1},
        {false, 2, 0, w - 3, 0, &GuiPalette::cl_cbtn_focused_s1000, nullptr},
        {false, 1, 1, w - 2, 1, &GuiPalette::cl_cbtn_focused_s0990, nullptr},
        {false, 0, 2, w - 1, 2, &GuiPalette::cl_cbtn_focused_s0990, nullptr},
        {false, 3, 0, w - 4, 0, &GuiPalette::cl_cbtn_focused_s0980, nullptr},
        {false, 2, 1, w - 3, 1, &GuiPalette::cl_cbtn_focused_s0910, nullptr},
        {false, 1, 2, w - 2, 2, &GuiPalette::cl_cbtn_focused_s0900, nullptr},
        {false, 2, h - 1, w - 3, h - 1, &GuiPalette::cl_cbtn_focused_s1030, nullptr},
        {false, 1, h - 2, w - 2, h - 2, &GuiPalette::cl_cbtn_focused_s1000, nullptr},
        {false, 0, h - 3, w - 1, h - 3, &GuiPalette::cl_cbtn_focused_s1010, nullptr},
        {false, 3, h - 1, w - 4, h - 1, &GuiPalette::cl_cbtn_focused_s1060, nullptr},
        {false, 2, h - 2, w - 3, h - 2, &GuiPalette::cl_cbtn_focused_s1020, nullptr},
        {false, 1, h - 3, w - 2, h - 3, &GuiPalette::cl_cbtn_focused_s1030, nullptr},
        {false, 3, 1, w - 4, 1, &GuiPalette::cl_cbtn_focused_s0600, nullptr},
        {false, 3, h - 2, w - 4, h - 2, &GuiPalette::cl_cbtn_focused_s0600, nullptr},
        {false, 1, 3, 1, h - 4, &GuiPalette::cl_cbtn_focused_s0600, nullptr},
        {false, w - 2, 3, w - 2, h - 4, &GuiPalette::cl_cbtn_focused_s0600, nullptr},
        {false, 2, 2, w - 3, 2, &GuiPalette::cl_cbtn_focused_s0600, nullptr},
        {false, 2, h - 3, w - 3, h - 3, &GuiPalette::cl_cbtn_focused_s0600, nullptr},
        {false, 3, 2, w - 4, 2, &GuiPalette::cl_cbtn_focused_s1020, nullptr},
        {false, 2, 3, 2, h - 4, &GuiPalette::cl_cbtn_focused_s1000, nullptr},
        {false, w - 3, 3, w - 3, h - 4, &GuiPalette::cl_cbtn_focused_s0900, nullptr},
        {false, 4, 2, w - 5, 2, &GuiPalette::cl_cbtn_focused_s1180, nullptr},
        {false, 2, 4, 2, h - 5, &GuiPalette::cl_cbtn_focused_s1100, nullptr},
        {false, w - 3, 4, w - 3, h - 5, &GuiPalette::cl_cbtn_focused_s1000, nullptr},
        {true, 3, 3, 3 + w - 6 - 1, 3 + h / 2 - 1 - 1, &GuiPalette::cl_cbtn_focused_ramp1_0, &GuiPalette::cl_cbtn_focused_ramp1_1},
        {true, 3, h / 2, 3 + w - 6 - 1, h / 2 + h / 2 - 2 - 1, &GuiPalette::cl_cbtn_focused_ramp2_0, &GuiPalette::cl_cbtn_focused_ramp2_1},
        {false, 3, h - 3, w - 4, h - 3, &GuiPalette::cl_cbtn_focused_s0840, nullptr},
        {false, 4, h - 3, w - 5, h - 3, &GuiPalette::cl_cbtn_focused_s0920, nullptr},
    }};
}
// metacity draw_ops "button_bg_pressed", in its order
std::array<ClCaptionOp, 14> cl_cbtn_pressed_ops(int w, int h) {
    return {{
        {true, w - 2, 2, w - 2 + 1 - 1, 2 + h - 4 - 1, &GuiPalette::cl_cbtn_pressed_ramp0_0, &GuiPalette::cl_cbtn_pressed_ramp0_1},
        {true, w - 1, 3, w - 1 + 1 - 1, 3 + h - 6 - 1, &GuiPalette::cl_cbtn_pressed_ramp1_0, &GuiPalette::cl_cbtn_pressed_ramp1_1},
        {false, 2, h - 2, w - 3, h - 2, &GuiPalette::cl_cbtn_pressed_s1000, nullptr},
        {false, 3, h - 1, w - 4, h - 1, &GuiPalette::cl_cbtn_pressed_s1000, nullptr},
        {false, 3, 1, w - 4, 1, &GuiPalette::cl_cbtn_pressed_s0550, nullptr},
        {false, 3, h - 2, w - 4, h - 2, &GuiPalette::cl_cbtn_pressed_s0550, nullptr},
        {false, 1, 3, 1, h - 4, &GuiPalette::cl_cbtn_pressed_s0550, nullptr},
        {false, w - 2, 3, w - 2, h - 4, &GuiPalette::cl_cbtn_pressed_s0550, nullptr},
        {false, 2, 2, w - 3, 2, &GuiPalette::cl_cbtn_pressed_s0550, nullptr},
        {false, 2, h - 3, w - 3, h - 3, &GuiPalette::cl_cbtn_pressed_s0550, nullptr},
        {false, 3, 2, w - 4, 2, &GuiPalette::cl_cbtn_pressed_s0900, nullptr},
        {false, 2, 3, 2, h - 4, &GuiPalette::cl_cbtn_pressed_s0850, nullptr},
        {true, 3, 3, 3 + w - 5 - 1, 3 + h - 6 - 1, &GuiPalette::cl_cbtn_pressed_ramp2_0, &GuiPalette::cl_cbtn_pressed_ramp2_1},
        {false, 3, h - 3, w - 4, h - 3, &GuiPalette::cl_cbtn_pressed_s0900, nullptr},
    }};
}
// metacity draw_ops "button_bg_unfocused", in its order
std::array<ClCaptionOp, 29> cl_cbtn_unfocused_ops(int w, int h) {
    return {{
        {true, 0, 3, 0 + w - 1, 3 + h - 6 - 1, &GuiPalette::cl_cbtn_unfocused_ramp0_0, &GuiPalette::cl_cbtn_unfocused_ramp0_1},
        {false, 2, 0, w - 3, 0, &GuiPalette::cl_cbtn_unfocused_s0930, nullptr},
        {false, 1, 1, w - 2, 1, &GuiPalette::cl_cbtn_unfocused_s0920, nullptr},
        {false, 0, 2, w - 1, 2, &GuiPalette::cl_cbtn_unfocused_s0920, nullptr},
        {false, 3, 0, w - 4, 0, &GuiPalette::cl_cbtn_unfocused_s0910, nullptr},
        {false, 2, 1, w - 3, 1, &GuiPalette::cl_cbtn_unfocused_s0870, nullptr},
        {false, 1, 2, w - 2, 2, &GuiPalette::cl_cbtn_unfocused_s0860, nullptr},
        {false, 2, h - 1, w - 3, h - 1, &GuiPalette::cl_cbtn_unfocused_s0945, nullptr},
        {false, 1, h - 2, w - 2, h - 2, &GuiPalette::cl_cbtn_unfocused_s0930, nullptr},
        {false, 0, h - 3, w - 1, h - 3, &GuiPalette::cl_cbtn_unfocused_s0935, nullptr},
        {false, 3, h - 1, w - 4, h - 1, &GuiPalette::cl_cbtn_unfocused_s0960, nullptr},
        {false, 2, h - 2, w - 3, h - 2, &GuiPalette::cl_cbtn_unfocused_s0940, nullptr},
        {false, 1, h - 3, w - 2, h - 3, &GuiPalette::cl_cbtn_unfocused_s0950, nullptr},
        {false, 3, 1, w - 4, 1, &GuiPalette::cl_cbtn_unfocused_s0600, nullptr},
        {false, 3, h - 2, w - 4, h - 2, &GuiPalette::cl_cbtn_unfocused_s0600, nullptr},
        {false, 1, 3, 1, h - 4, &GuiPalette::cl_cbtn_unfocused_s0600, nullptr},
        {false, w - 2, 3, w - 2, h - 4, &GuiPalette::cl_cbtn_unfocused_s0600, nullptr},
        {false, 2, 2, w - 3, 2, &GuiPalette::cl_cbtn_unfocused_s0600, nullptr},
        {false, 2, h - 3, w - 3, h - 3, &GuiPalette::cl_cbtn_unfocused_s0600, nullptr},
        {false, 3, 2, w - 4, 2, &GuiPalette::cl_cbtn_unfocused_s1020, nullptr},
        {false, 2, 3, 2, h - 4, &GuiPalette::cl_cbtn_unfocused_s1000, nullptr},
        {false, w - 3, 3, w - 3, h - 4, &GuiPalette::cl_cbtn_unfocused_s0950, nullptr},
        {false, 4, 2, w - 5, 2, &GuiPalette::cl_cbtn_unfocused_s1200, nullptr},
        {false, 2, 4, 2, h - 5, &GuiPalette::cl_cbtn_unfocused_s1100, nullptr},
        {false, w - 3, 4, w - 3, h - 5, &GuiPalette::cl_cbtn_unfocused_s1050, nullptr},
        {true, 3, 3, 3 + w - 6 - 1, 3 + h / 2 - 1 - 1, &GuiPalette::cl_cbtn_unfocused_ramp1_0, &GuiPalette::cl_cbtn_unfocused_ramp1_1},
        {true, 3, h / 2, 3 + w - 6 - 1, h / 2 + h / 2 - 2 - 1, &GuiPalette::cl_cbtn_unfocused_ramp2_0, &GuiPalette::cl_cbtn_unfocused_ramp2_1},
        {false, 3, h - 3, w - 4, h - 3, &GuiPalette::cl_cbtn_unfocused_s0890, nullptr},
        {false, 4, h - 3, w - 5, h - 3, &GuiPalette::cl_cbtn_unfocused_s0970, nullptr},
    }};
}
// metacity draw_ops "button_bg_unfocused_pressed", in its order
std::array<ClCaptionOp, 14> cl_cbtn_unfocused_pressed_ops(int w, int h) {
    return {{
        {true, w - 2, 2, w - 2 + 1 - 1, 2 + h - 4 - 1, &GuiPalette::cl_cbtn_unfocused_pressed_ramp0_0, &GuiPalette::cl_cbtn_unfocused_pressed_ramp0_1},
        {true, w - 1, 3, w - 1 + 1 - 1, 3 + h - 6 - 1, &GuiPalette::cl_cbtn_unfocused_pressed_ramp1_0, &GuiPalette::cl_cbtn_unfocused_pressed_ramp1_1},
        {false, 2, h - 2, w - 3, h - 2, &GuiPalette::cl_cbtn_unfocused_pressed_s1050, nullptr},
        {false, 3, h - 1, w - 4, h - 1, &GuiPalette::cl_cbtn_unfocused_pressed_s1050, nullptr},
        {false, 3, 1, w - 4, 1, &GuiPalette::cl_cbtn_unfocused_pressed_s0550, nullptr},
        {false, 3, h - 2, w - 4, h - 2, &GuiPalette::cl_cbtn_unfocused_pressed_s0550, nullptr},
        {false, 1, 3, 1, h - 4, &GuiPalette::cl_cbtn_unfocused_pressed_s0550, nullptr},
        {false, w - 2, 3, w - 2, h - 4, &GuiPalette::cl_cbtn_unfocused_pressed_s0550, nullptr},
        {false, 2, 2, w - 3, 2, &GuiPalette::cl_cbtn_unfocused_pressed_s0550, nullptr},
        {false, 2, h - 3, w - 3, h - 3, &GuiPalette::cl_cbtn_unfocused_pressed_s0550, nullptr},
        {false, 3, 2, w - 4, 2, &GuiPalette::cl_cbtn_unfocused_pressed_s0800, nullptr},
        {false, 2, 3, 2, h - 4, &GuiPalette::cl_cbtn_unfocused_pressed_s0750, nullptr},
        {true, 3, 3, 3 + w - 5 - 1, 3 + h - 6 - 1, &GuiPalette::cl_cbtn_unfocused_pressed_ramp2_0, &GuiPalette::cl_cbtn_unfocused_pressed_ramp2_1},
        {false, 3, h - 3, w - 4, h - 3, &GuiPalette::cl_cbtn_unfocused_pressed_s0850, nullptr},
    }};
}

void paint_cl_ops(cairo_t* cr, std::span<const ClCaptionOp> ops, int ox,
                  int oy) {
    for (const ClCaptionOp& op : ops) {
        const GuiRect r = cells(ox, oy, op.x1, op.y1, op.x2, op.y2);
        if (op.ramp) paint_cl_ramp(cr, r, tone(op.a), tone(op.b));
        else         paint_cell_rect(cr, r, tone(op.a));
    }
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
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    if (focused) {
        if (pressed) paint_cl_ops(cr, cl_cbtn_pressed_ops(w, h), b.x, b.y);
        else         paint_cl_ops(cr, cl_cbtn_focused_ops(w, h), b.x, b.y);
    } else {
        if (pressed)
            paint_cl_ops(cr, cl_cbtn_unfocused_pressed_ops(w, h), b.x, b.y);
        else
            paint_cl_ops(cr, cl_cbtn_unfocused_ops(w, h), b.x, b.y);
    }
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
    const double du  = relief_line_px();
    const double rad = scaled_px(live_chrome_spec().corner_radius_px);
    const double ri  = std::max(0.0, rad - du);   // MAX (0, radius − 1)
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    paint_inset_ring(cr, r, rad + du);
    rounded_path(cr, r.x + 2 * du, r.y + 2 * du, r.w - 4 * du, r.h - 4 * du,
                 ri, kAll);
    set_palette_source(cr, palette().cl_base);
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
