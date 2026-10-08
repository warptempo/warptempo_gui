#include "clearlooks_paint.h"

#include "chrome_spec.h"
// kRowHeightPx: the list row the selected cell's tones are recorded at.
#include "folder_overlay.h"
#include "clearlooks_derive.h"   // kListRowPx, the derivation's copy of it

#include <algorithm>
#include <array>
#include <cmath>

// THE DERIVATION'S LIST ROW IS THE OVERLAY'S (clearlooks_derive.h's
// kListRowPx: that header cannot include folder_overlay.h).
static_assert(clearlooks_derive::kListRowPx ==
              static_cast<int>(folder_overlay::kRowHeightPx));

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
// whose ends step in by a cell a row — three concentric rings round a filled
// middle. THE HALO (ring 0): the top lines on row 0 (`1.00` x 2 .. w − 3,
// `0.98` x 3 .. w − 4) with the `0.99` cells (1, 1) and (0, 2) — the corner
// the top's — then the inset gradient `ramp0` down columns 0 and w − 1 from
// row 3, and the bottom's mirror. THE BORDER (ring 1): the `0.6` lines on
// row 1 and down column 1 and the diagonal cell (2, 2), one shade all round.
// THE INNER BEVEL (ring 2): the inside highlight `1.18` x 4 .. w − 5 on row
// 2 and `1.1` y 4 .. h − 5 down column 2, the inside shadow `1.0` y 4 .. h − 5
// down column w − 3, the bottom's `0.92` x 4 .. w − 5 on row h − 3 — each
// run's end cell the "smooth effect"'s darker tone (`1.02`, `1.00`, `0.90`,
// `0.84`) and the corner cell (2, 2) the border's, so NO RUN TURNS THE CORNER:
// each stops at the corner's diagonal. Round THE FILL (the upper and lower
// gradients). Pressed, the halo is its bottom-right half alone (the bottom
// line and the right column's gradient), the bevel the inner shadow's top,
// left and bottom, the fill reaching the right ring.
// THE SCALABLE CHROME (architect 2026-10-07, his glass verdict on P1's cell
// rects: "pixelated" at 300 %; clearlooks_paint.h's head, rule 4) DRAWS EACH
// RING AS ONE SMOOTH PATH ROUND THE BOX — the straight runs and, at each
// corner, an arc tangent to both: an antialiased annulus between two rounded
// rects one W apart, the corners concentric about the point FOUR W in from
// the box's corner — the radius at which the art's three diagonal cells stand
// inside their rings (the halo's (1, 1) at 3.5 W from it, the border's (2, 2)
// at 2.1, the bevel's (3, 2) / (2, 3) at 1.6), so the halo's outer edge is
// radius 4, the border's 3, the bevel's 2 and the fill's 1. A RING'S TONES
// MEET ON RADIAL SEAMS through its corner's centre, WHERE THE OPS END THE
// RUNS (architect 2026-10-07 at 600 %, his glass verdict on the bevel's sides
// cut off by a row across the arc: the highlight "curves in ... abruptly — it
// looks like a notch ... turns and becomes a straight line parallel to the
// ground"): THE BEVEL'S AT 45 DEGREES (cbtn_sector_clip) — each side's line
// rounds half of each corner and ends on the diagonal, the next side's line
// taking the other half, as the ops end every run there; THE HALO'S where
// its top and bottom lines end, the ring's mid-line crossing rows 3 and
// h − 3 (the corner cells the top's and the bottom's, the ramp the straight
// sides'). The smooth effect's end cells and the art's in-between corner
// tones are the arcs' own antialiasing and carry no role (build.py
// CAPTION_BUTTON_DRAWN).
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
// Ring `k`'s corner centre, in device px in from each edge of the box (the
// outer path's: k lines, then its radius 4 − k).
double cbtn_corner_c(int k) {
    const double s = static_cast<double>(scaled_px(100)) / 100.0;
    return k * relief_line_px() + std::max(0.0, (4 - k) * s);
}
// THE TOP (or the bottom) SIDE'S SECTOR as the clip: the plane beyond the
// line joining the two corner centres `c` in from the box's edges, bounded at
// each corner by the RADIAL SEAM from the centre outward at `phi` radians
// off the horizontal — so a ring's top run and the part of each corner arc
// above its seam lie inside, the side runs outside.
void cbtn_sector_clip(cairo_t* cr, const GuiRect& b, double c, double phi,
                      bool top) {
    const double far = 2.0 * (b.w + b.h);
    const double sgn = top ? -1.0 : 1.0;
    const double xl = b.x + c, xr = b.x + b.w - c;
    const double yc = top ? b.y + c : b.y + b.h - c;
    const double dx = far * std::cos(phi), dy = sgn * far * std::sin(phi);
    cairo_new_path(cr);
    cairo_move_to(cr, xl - dx, yc + dy);
    cairo_line_to(cr, xl, yc);
    cairo_line_to(cr, xr, yc);
    cairo_line_to(cr, xr + dx, yc + dy);
    cairo_line_to(cr, xr + dx, yc + sgn * 2.0 * far);
    cairo_line_to(cr, xl - dx, yc + sgn * 2.0 * far);
    cairo_close_path(cr);
    cairo_clip(cr);
}
// `role` over the box `b` inside the top (or bottom) sector — under the
// caller's ring clip, that side's line and its halves of the two corners.
void cbtn_sector_fill(cairo_t* cr, const GuiRect& b, double c, double phi,
                      bool top, Role role) {
    cairo_save(cr);
    cbtn_sector_clip(cr, b, c, phi, top);
    paint_cell_rect(cr, b, tone(role));
    cairo_restore(cr);
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
    // THE INNER BEVEL, ring 2: the left side's tone round the ring and the
    // right side's over its right half (pressed, the left's over its left
    // half alone, the fill showing through the right side), then the top's
    // and the bottom's sectors over them — every seam at 45 degrees through
    // a corner centre, where the ops end the runs.
    {
        const double c = cbtn_corner_c(2);
        const GuiRect left{b.x, b.y, b.w / 2, b.h};
        const GuiRect right{b.x + b.w / 2, b.y, b.w - b.w / 2, b.h};
        cairo_save(cr);
        cbtn_ring_clip(cr, b, 2);
        if (f.bevel_right) {
            paint_cell_rect(cr, b, tone(f.bevel_left));
            paint_cell_rect(cr, right, tone(f.bevel_right));
        } else {
            paint_cell_rect(cr, left, tone(f.bevel_left));
        }
        cbtn_sector_fill(cr, b, c, M_PI_4, true, f.bevel_top);
        cbtn_sector_fill(cr, b, c, M_PI_4, false, f.bevel_bottom);
        cairo_restore(cr);
    }
    // THE BORDER, ring 1, one tone.
    {
        cairo_save(cr);
        cbtn_ring_clip(cr, b, 1);
        paint_cell_rect(cr, b, tone(f.border));
        cairo_restore(cr);
    }
    // THE HALO, ring 0: the side gradient down the rows the ops give it (row 3
    // to h − 3) with its end tones beyond, then the top's and the bottom's
    // sectors over it, their seams where the ring's mid-line crosses rows 3
    // and h − 3; pressed, its bottom-right half alone (the inset's own split,
    // along the diagonal from the bottom-left corner to the top-right).
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
        const int y3 = b.y + 3 * u, yb = b.y + b.h - 3 * u;
        const Role side1 = f.halo_side1 ? f.halo_side1 : f.halo_side0;
        paint_cell_rect(cr, GuiRect{b.x, b.y, b.w, y3 - b.y},
                        tone(f.halo_side0));
        paint_cl_ramp(cr, GuiRect{b.x, y3, b.w, yb - y3}, tone(f.halo_side0),
                      tone(side1));
        paint_cell_rect(cr, GuiRect{b.x, yb, b.w, b.y + b.h - yb}, tone(side1));
        const double c   = cbtn_corner_c(0);
        const double phi = std::asin(std::clamp((c - 3.0 * u) / (c - u / 2.0),
                                                0.0, 1.0));
        cbtn_sector_fill(cr, b, c, phi, true, f.halo_top);
        cbtn_sector_fill(cr, b, c, phi, false, f.halo_bottom);
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
// A filled <rectangle> (x, y, w, h): cells x..x + w − 1.
void glyph_fill(cairo_t* cr, int ox, int oy, int x, int y, int w, int h,
                Role role) {
    if (w <= 0 || h <= 0) return;
    paint_cell_rect(cr, cells(ox, oy, x, y, x + w - 1, y + h - 1), tone(role));
}
// A one-px horizontal <line> from cell x1 to cell x2 on row y: X's wide
// line of width 1 between the two pixel centres, BUTT-capped, so its last
// point is NOT drawn — cells x1 .. x2 − 1 (his capture 23-12-32: the Music
// Player's Maximise, whose line ends on the light frame's last column and
// leaves the outline's column under it dark). Drawing x2 as well put one
// cell of the light line on the outline beside the frame (Maximise's
// x2 = width − hpadding; the restored laptop's glass, 2026-10-07).
void glyph_hline(cairo_t* cr, int ox, int oy, int x1, int x2, int y,
                 Role role) {
    if (x2 <= x1) return;
    paint_cell_rect(cr, cells(ox, oy, x1, y, x2 - 1, y), tone(role));
}

// CLOSE'S CROSS — metacity's two <line> diagonals from cell (hp, vp) to cell
// (w − hp − 1, h − vp − 1) and from (hp, h − vp − 1) to (w − hp − 1, vp),
// drawn as ONE PATH PER TONE (architect 2026-10-07, his glass verdict on the
// cell-built cross: "glitched out, a lot of artifacting"): both diagonals
// stroked together from pixel centre to pixel centre, BUTT caps, `width` W px
// wide, each end run on `overrun` W px along its own diagonal, the stroke
// antialiased by the renderer (the head's rule 3). The centres are the device
// box's own (`bw` x `bh` device px for the w x h W box), so the cross is
// centred where metacity's paddings centre it at every scale.
void glyph_cross(cairo_t* cr, int ox, int oy, int w, int h, int bw, int bh,
                 int hp, int vp, double width, double overrun, Role role) {
    const double sx = static_cast<double>(bw) / w;
    const double sy = static_cast<double>(bh) / h;
    const double cx = ox + bw / 2.0, cy = oy + bh / 2.0;
    // Half the span between the end cells' centres, plus the overrun along
    // the diagonal (each axis takes overrun / sqrt 2).
    const double run = overrun / std::sqrt(2.0);
    const double hx  = (w / 2.0 - hp - 0.5 + run) * sx;
    const double hy  = (h / 2.0 - vp - 0.5 + run) * sy;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
    cairo_set_line_width(cr, width * std::sqrt(sx * sy));
    cairo_new_path(cr);
    cairo_move_to(cr, cx - hx, cy - hy);
    cairo_line_to(cr, cx + hx, cy + hy);
    cairo_move_to(cr, cx - hx, cy + hy);
    cairo_line_to(cr, cx + hx, cy - hy);
    set_palette_source(cr, tone(role));
    cairo_stroke(cr);
    cairo_restore(cr);
}

// THE GLYPH'S icon_size — metacity's `Bmin max (height − Bpad x 2)` (Bmin
// 7, Bpad 6), centred by the paddings hpadding = (width − icon_size) / 2 and
// vpadding = (height − icon_size) / 2. CLOSE'S icon_size IS THE CAPTURE'S
// PROPORTION OF THE BOX UNDER THE FIT, NOT METACITY'S Bmin FLOOR (architect
// 2026-10-07, his glass verdict at 600 %: "the X is a little too big ... the
// original is a good bit smaller"): at squeeze's 20-px box the formula gives
// 8 (his capture 23-12-32, the Music Player's Close: the light X 8 px, its
// dark ring round it, three px from the highlight's column), and at the fit's
// 16-W box Bmin held it at 7 where the proportion asks 16 x 8 / 20 = 6.4 — so
// the X takes the proportion, held EVEN as the cross's centre-to-centre lines
// want: 6, the X on cells 5 .. 10, its outline 4 .. 11, two W from the
// highlight's column (the capture's three of 20, fitted). MINIMISE TAKES THE
// SAME 6 (architect 2026-10-07 ~05:30, his glass: Minimise "looks too large
// — it's supposed to signify being tiny"; the capture's bar is 8 of the 20
// box, the same proportion as the X): its light bar 6 W on cells 5 .. 10,
// two W narrower than the Maximise box beside it where the capture draws
// the bar and the box the same width — the bar reading tiny is his call.
// MAXIMISE AND RESTORE KEEP METACITY'S 7, A RECORDED ASYMMETRY (the
// planner's ruling, 2026-10-07; his verdicts named the X and the bar),
// because below it their ops lose their window: at 6 Maximise's dark inner
// frame leaves a 2 x 1 slit of the fill where 7 (and squeeze's 8) leave
// 4 x 3, and Restore's collapses to a solid 2 x 1 bar where 7 leaves its
// 2 x 1 window.
constexpr double kClCaptionIconOfBox = 8.0 / 20.0;
int caption_icon_size(int h, ClCaptionGlyph g) {
    if (g != ClCaptionGlyph::Close && g != ClCaptionGlyph::Minimize)
        return std::max(7, h - 6 * 2);
    return 2 * static_cast<int>(std::nearbyint(h * kClCaptionIconOfBox / 2.0));
}

void paint_caption_glyph(cairo_t* cr, int ox, int oy, int w, int h,
                         ClCaptionGlyph g, bool full) {
    const int isz = caption_icon_size(h, g);
    const int hp  = (w - isz) / 2;
    const int vp  = (h - isz) / 2;
    const Role dark  = &GuiPalette::cl_cglyph_dark;
    const Role light = full ? &GuiPalette::cl_cglyph_light
                            : &GuiPalette::cl_cglyph_unfocused;
    switch (g) {
    case ClCaptionGlyph::Close: {
        // THE TWO TONES, EACH ONE PATH (glyph_cross): the cross the width-2
        // diagonals' (the theme's `2`); under it, focused, the 0.7 outline the
        // width-4 diagonals' (its `4`) RUN ONE W PAST EACH END, so the outline
        // stands one W round the cross on every side, its ends as its flanks
        // — what the theme's eight opaque <tint>s (2 x 1 and 1 x 2 cells at
        // each end) drew in cells: X's butt-capped wide line stops at the
        // end cell's centre, and the tints carried the outline past it (his
        // capture 23-12-32, the Music Player's Close: the light X 8 x 8, its
        // outline the one-px ring round it). Drawn as cells at 300 % they were
        // square ears; the stroke's run carries them, and they are retired
        // as P3 retired the caption buttons' hand-antialiasing corner tones —
        // no role was theirs (the outline's cl_cglyph_dark). THE THEME'S
        // WIDTH-1 LINES (the `+ 1`, from (hp, vp) to (w − hp, h − vp)) lie
        // on the width-2 lines' diagonals and inside them: in X they fill
        // the wide line's stair-stepped edges and their butt-capped last
        // point is not drawn; as antialiased strokes they would add only
        // one cell past each end (the pale spikes on his tablet crop
        // tmp/shots_1007b/tab_X_x12.png), so they are not drawn.
        const int bw = at(ox, w) - ox, bh = at(oy, h) - oy;
        if (full)
            glyph_cross(cr, ox, oy, w, h, bw, bh, hp, vp, 4.0, 1.0, dark);
        glyph_cross(cr, ox, oy, w, h, bw, bh, hp, vp, 2.0, 0.0, light);
        return;
    }
    case ClCaptionGlyph::Maximize:
        if (full) {
            glyph_frame(cr, ox, oy, hp - 1, vp - 1, w - hp * 2 + 1,
                        h - vp * 2 + 1, dark);
            glyph_frame(cr, ox, oy, hp + 1, vp + 2, w - hp * 2 - 3,
                        h - vp * 2 - 4, dark);
        }
        glyph_frame(cr, ox, oy, hp, vp, w - hp * 2 - 1, h - vp * 2 - 1, light);
        glyph_hline(cr, ox, oy, hp + 1, w - hp, vp + 1, light);
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
        glyph_hline(cr, ox, oy, hp + 2, w - hp - 2, vp + 2, light);
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

void paint_cl_ramp_across(cairo_t* cr, const GuiRect& r, GuiColor left,
                          GuiColor right) {
    if (r.w <= 0 || r.h <= 0) return;
    for (int i = 0; i < r.w; ++i) {
        const double t = r.w > 1 ? static_cast<double>(i) / (r.w - 1) : 0.0;
        const auto level = [t](double s, double e) {
            return std::clamp(std::nearbyint((s + (e - s) * t) * 255.0), 0.0,
                              255.0) / 255.0;
        };
        paint_cell_rect(cr, GuiRect{r.x + i, r.y, 1, r.h},
                        GuiColor{level(left.r, right.r),
                                 level(left.g, right.g),
                                 level(left.b, right.b)});
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

void paint_cl_menu(cairo_t* cr, const GuiRect& box, bool upward) {
    const int u = relief_line_px();
    const GuiPalette& pal = palette();
    const int top_y = upward ? box.y : box.y - u;   // the header's rule
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, box, pal.cl_menu_ground);
    paint_cell_rect(cr, GuiRect{box.x, top_y, box.w, u}, pal.cl_menu_frame);
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

namespace {

// THE ENGINE'S CHEVRON centred on the box `b`, turned by `turn` from its
// pointing-down path, for a bar `bar_w` W px thick (GtkRange's
// arrow-scaling 0.5 of the box), in cl_stepper_arrow.
void paint_cl_chevron(cairo_t* cr, const GuiRect& b, double turn, int bar_w) {
    const double s = static_cast<double>(scaled_px(100)) / 100.0;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_translate(cr, b.x + b.w / 2.0, b.y + b.h / 2.0);
    cairo_rotate(cr, turn);
    normal_arrow_path(cr, bar_w / 2, s);
    set_palette_source(cr, palette().cl_stepper_arrow);
    cairo_fill(cr);
    cairo_restore(cr);
}

// THE BAR'S WIDTH in W, the clearlooks spec's scroll_bar_px (the one source,
// chrome_spec.h; 2026-10-08): the gummy steppers' and slider's columns are
// authored across it.
constexpr int kClScrollBarW = kChromeSpecClearlooks.scroll_bar_px;

// ONE TRIM CAP (the rule at paint_cl_stepper): the ring's ramp runs down the
// cap, its outer corners the bar's ends, its inner edge where it meets the
// body.
void paint_cl_cap(cairo_t* cr, const GuiRect& b, bool left, bool pressed) {
    if (b.w <= 0 || b.h <= 0) return;
    const GuiPalette& pal = palette();
    const int    u   = relief_line_px();
    const double du  = u;
    const double rad = std::min<double>(scaled_px(live_chrome_spec().corner_radius_px),
                                        std::min(b.w, b.h) / 2.0);
    const unsigned corners = left ? (kTL | kBL) : (kTR | kBR);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    // THE RING, the box inside its outer rounded path: the one ramp from the
    // bar's light tone to its dark tone over the first kClCapRampPx rows,
    // then the dark tone flat to the bottom (the body's dark line continued)
    // — the turn ends inside the corner's arc, so the straight outer side is
    // one tone (the step-j ruling at kClCapRampPx). The clip's arcs are the
    // renderer's antialiasing.
    cairo_save(cr);
    rounded_path(cr, b.x, b.y, b.w, b.h, rad, corners);
    cairo_clip(cr);
    const int ramp_rows = std::min(scaled_px(kClCapRampPx) + 1, b.h);
    paint_cl_ramp(cr, GuiRect{b.x, b.y, b.w, ramp_rows},
                  pal.cl_separator_light, pal.cl_separator_dark);
    if (ramp_rows < b.h)
        paint_cell_rect(cr, GuiRect{b.x, b.y + ramp_rows, b.w, b.h - ramp_rows},
                        pal.cl_separator_dark);
    cairo_restore(cr);
    // THE FACE, one W in, its rounded corners concentric with the ring's:
    // the ground at rest, bg[ACTIVE] pressed.
    cairo_save(cr);
    rounded_path(cr, b.x + du, b.y + du, b.w - 2 * du, b.h - 2 * du,
                 std::max(0.0, rad - du), corners);
    cairo_clip(cr);
    paint_cell_rect(cr, GuiRect{b.x + u, b.y + u, b.w - 2 * u, b.h - 2 * u},
                    pressed ? pal.cl_stepper_pressed_face : pal.ground);
    cairo_restore(cr);
    // THE INNER EDGE: one W of the dark tone where the cap meets the body,
    // the lane's whole height (the begin cap's right column, the end cap's
    // left).
    paint_cell_rect(cr, GuiRect{left ? b.x + b.w - u : b.x, b.y, u, b.h},
                    pal.cl_separator_dark);
    cairo_restore(cr);
    paint_cl_chevron(cr, b, left ? M_PI_2 : -M_PI_2, kClScrollBarW);
}

// THE LIST BAR'S STEPS ACROSS ITS WIDTH (build.py's pixman_step_row): the
// stepper's ramp from column 0 to the width, the slider's from column 1 to
// the width less 2 — W column 8 of the 16 both.
constexpr int kListStepperStepW =
    clearlooks_derive::pixman_step_row(0, kClScrollBarW);
constexpr int kListSliderStepW =
    clearlooks_derive::pixman_step_row(1, kClScrollBarW - 2);

// A ramp across `r`'s columns from W column `c0` of a box at `ox` to the
// step column, then from it to W column `c1` (exclusive) — two recorded
// segments, each the one rule.
void paint_cl_ramp_across_split(cairo_t* cr, const GuiRect& r, int ox,
                                int c0, int step, int c1, GuiColor a0,
                                GuiColor a1, GuiColor b0, GuiColor b1) {
    const int x0 = at(ox, c0), xs = at(ox, step), x1 = at(ox, c1);
    paint_cl_ramp_across(cr, GuiRect{x0, r.y, xs - x0, r.h}, a0, a1);
    paint_cl_ramp_across(cr, GuiRect{xs, r.y, x1 - xs, r.h}, b0, b1);
}

} // namespace

void paint_cl_stepper(cairo_t* cr, const GuiRect& b, bool points_left,
                      bool pressed) {
    paint_cl_cap(cr, b, points_left, pressed);
}

void paint_cl_scrollbar_stepper(cairo_t* cr, const GuiRect& b, bool points_up,
                                bool pressed) {
    if (b.w <= 0 || b.h <= 0) return;
    const GuiPalette& pal = palette();
    const int    u   = relief_line_px();
    const double du  = u;
    const int    W   = kClScrollBarW;
    const double rad = std::min<double>(scaled_px(live_chrome_spec().corner_radius_px),
                                        std::min(b.w - 2 * du, b.h - 2 * du) / 2.0);
    const unsigned corners = points_up ? (kTL | kTR) : (kBL | kBR);
    const auto R = [&](GuiColor normal, GuiColor down) {
        return pressed ? down : normal;
    };
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    // THE FILL, clipped to its rounded rect one W in: the gummy ramp's two
    // segments across W columns 1 .. W − 2.
    cairo_save(cr);
    rounded_path(cr, b.x + du, b.y + du, b.w - 2 * du, b.h - 2 * du, rad,
                 corners);
    cairo_clip(cr);
    paint_cl_ramp_across_split(
        cr, GuiRect{b.x, b.y + u, b.w, b.h - 2 * u}, b.x, 1, kListStepperStepW,
        W - 1,
        R(pal.cl_scrollbar_stepper_normal_left_0, pal.cl_scrollbar_stepper_pressed_left_0),
        R(pal.cl_scrollbar_stepper_normal_left_1, pal.cl_scrollbar_stepper_pressed_left_1),
        R(pal.cl_scrollbar_stepper_normal_right_0, pal.cl_scrollbar_stepper_pressed_right_0),
        R(pal.cl_scrollbar_stepper_normal_right_1, pal.cl_scrollbar_stepper_pressed_right_1));
    // THE TOP-LEFT HIGHLIGHT (draw_top_left_highlight on the fill's rect):
    // up W column 1 from the bottom (less the radius on a rounded
    // bottom-left), round a rounded top-left, along W row 1 to the right
    // (less the radius on a rounded top-right), in the column's tone — the
    // ramp's first column baked; then the row's straight part over it, its
    // two baked segments.
    const GuiColor hl_col = R(pal.cl_scrollbar_stepper_normal_highlight_left_0,
                              pal.cl_scrollbar_stepper_pressed_highlight_left_0);
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
    set_palette_source(cr, hl_col);
    cairo_stroke(cr);
    cairo_restore(cr);
    {
        const int rx0 = (corners & kTL) ? static_cast<int>(std::ceil(left + rad))
                                        : b.x + u;
        const int rx1 = static_cast<int>(std::floor(right));
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_rectangle(cr, rx0, b.y + u, std::max(0, rx1 - rx0), u);
        cairo_clip(cr);
        paint_cl_ramp_across_split(
            cr, GuiRect{b.x, b.y + u, b.w, u}, b.x, 1, kListStepperStepW, W - 1,
            hl_col,
            R(pal.cl_scrollbar_stepper_normal_highlight_left_1,
              pal.cl_scrollbar_stepper_pressed_highlight_left_1),
            R(pal.cl_scrollbar_stepper_normal_highlight_right_0,
              pal.cl_scrollbar_stepper_pressed_highlight_right_0),
            R(pal.cl_scrollbar_stepper_normal_highlight_right_1,
              pal.cl_scrollbar_stepper_pressed_highlight_right_1));
        cairo_restore(cr);
    }
    cairo_restore(cr);   // the fill's clip
    // THE BORDER, the outer ring at the radius.
    rounded_path(cr, b.x + du / 2, b.y + du / 2, b.w - du, b.h - du, rad,
                 corners);
    cairo_set_line_width(cr, du);
    set_palette_source(cr, R(pal.cl_scrollbar_stepper_normal_border,
                             pal.cl_scrollbar_stepper_pressed_border));
    cairo_stroke(cr);
    cairo_restore(cr);
    paint_cl_chevron(cr, b, points_up ? M_PI : 0.0, kClScrollBarW);
}

void paint_cl_scroll_trough_v(cairo_t* cr, const GuiRect& bar) {
    if (bar.w <= 0 || bar.h <= 0) return;
    const int u = relief_line_px();
    const GuiPalette& pal = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, bar, pal.cl_trough_fill);
    // The shadow's two graded columns (W columns 1 and 2), the one ramp rule
    // turned.
    paint_cl_ramp_across(cr, GuiRect{bar.x + u, bar.y,
                                     at(bar.x, 3) - (bar.x + u), bar.h},
                         pal.cl_trough_shadow_0, pal.cl_trough_shadow_1);
    paint_cell_rect(cr, GuiRect{bar.x, bar.y, u, bar.h}, pal.cl_trough_border);
    paint_cell_rect(cr, GuiRect{bar.x + bar.w - u, bar.y, u, bar.h},
                    pal.cl_trough_border);
    cairo_restore(cr);
}

void paint_cl_scrollbar_slider(cairo_t* cr, const GuiRect& body) {
    if (body.w <= 0 || body.h <= 0) return;
    const GuiPalette& pal = palette();
    const int u = relief_line_px();
    const int W = kClScrollBarW;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // THE FILL: the gummy ramp's two segments across W columns 1 .. W − 2.
    paint_cl_ramp_across_split(cr, GuiRect{body.x, body.y + u, body.w, body.h - 2 * u},
                               body.x, 1, kListSliderStepW, W - 1,
                               pal.cl_scrollbar_slider_left_0,
                               pal.cl_scrollbar_slider_left_1,
                               pal.cl_scrollbar_slider_right_0,
                               pal.cl_scrollbar_slider_right_1);
    // THE HIGHLIGHT one W in on all four sides: its top and bottom rows the
    // baked ramp, its two columns the ramp's end tones.
    for (const int y : {body.y + u, body.y + body.h - 2 * u})
        paint_cl_ramp_across_split(cr, GuiRect{body.x, y, body.w, u}, body.x, 1,
                                   kListSliderStepW, W - 1,
                                   pal.cl_scrollbar_slider_highlight_left_0,
                                   pal.cl_scrollbar_slider_highlight_left_1,
                                   pal.cl_scrollbar_slider_highlight_right_0,
                                   pal.cl_scrollbar_slider_highlight_right_1);
    paint_cell_rect(cr, GuiRect{body.x + u, body.y + 2 * u, u, body.h - 4 * u},
                    pal.cl_scrollbar_slider_highlight_left_0);
    paint_cell_rect(cr, GuiRect{at(body.x, W - 2), body.y + 2 * u,
                                at(body.x, W - 1) - at(body.x, W - 2),
                                body.h - 4 * u},
                    pal.cl_scrollbar_slider_highlight_right_1);
    // THE BORDER, one W round the rect, square.
    const GuiColor border = pal.cl_scrollbar_slider_border;
    paint_cell_rect(cr, GuiRect{body.x, body.y, body.w, u}, border);
    paint_cell_rect(cr, GuiRect{body.x, body.y + body.h - u, body.w, u}, border);
    paint_cell_rect(cr, GuiRect{body.x, body.y, u, body.h}, border);
    paint_cell_rect(cr, GuiRect{body.x + body.w - u, body.y, u, body.h}, border);
    // THE THREE GRIPS at W rows L / 2 − 4, + 3 and + 6 of the slider's W
    // length L, across W columns 5 .. W − 6.
    const double s = static_cast<double>(scaled_px(100)) / 100.0;
    const int len_w = static_cast<int>(std::nearbyint(body.h / s));
    const int gx0 = at(body.x, 5), gx1 = at(body.x, W - 5);
    for (int k = 0; k < 3; ++k) {
        const int gy = at(body.y, len_w / 2 - 4 + 3 * k);
        paint_cell_rect(cr, GuiRect{gx0, gy, gx1 - gx0, at(gy, 1) - gy},
                        pal.cl_scrollbar_slider_grip);
    }
    cairo_restore(cr);
}

void paint_cl_slider(cairo_t* cr, const GuiRect& body) {
    if (body.w <= 0 || body.h <= 0) return;
    const GuiPalette& pal = palette();
    const int u = relief_line_px();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // The ground, then its top row light and its bottom row dark — the gummy
    // separator's own two roles, reused (the declaration says why that is no
    // derivation).
    paint_cell_rect(cr, body, pal.ground);
    paint_cell_rect(cr, GuiRect{body.x, body.y, body.w, u},
                    pal.cl_separator_light);
    paint_cell_rect(cr, GuiRect{body.x, body.y + body.h - u, body.w, u},
                    pal.cl_separator_dark);
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
