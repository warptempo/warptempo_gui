#include "cde_paint.h"

#include "chrome_spec.h"

#include <algorithm>
#include <cmath>
#include <utility>

// THE CDE PAINTERS' BODIES (the rules at cde_paint.h's head and at each
// declaration; every length the chrome spec's, every tone a role).

namespace {

// A one-W ring on `r`, `tl` on its top and left, `br` on its bottom and
// right, mitred (paint_relief_frame).
void ring(cairo_t* cr, const GuiRect& r, GuiColor tl, GuiColor br) {
    paint_relief_frame(cr, r, tl, br);
}

// `r` less one relief line on every side (empty when it cannot).
GuiRect inner(const GuiRect& r) {
    const int lw = relief_line_px();
    return GuiRect{r.x + lw, r.y + lw, std::max(0, r.w - 2 * lw),
                   std::max(0, r.h - 2 * lw)};
}

// THE CAPTION BOX'S TONES: the title's set while active, the inactive
// frame's otherwise (the head).
struct FrameTones {
    GuiColor face, ts, bs;
};
FrameTones caption_tones(bool active) {
    const GuiPalette& pal = palette();
    return active ? FrameTones{pal.caption_active, pal.cde_title_ts,
                               pal.cde_title_bs}
                  : FrameTones{pal.caption_inactive, pal.cde_inactive_ts,
                               pal.cde_inactive_bs};
}

// A raised one-W ring round the face on `r` in the frame's tones — the
// caption's boxes and their glyphs' bars.
void raised_bar(cairo_t* cr, const GuiRect& r, const FrameTones& t) {
    paint_cell_rect(cr, r, t.face);
    ring(cr, r, t.ts, t.bs);
}

} // namespace

// -- THE BEVELS ----------------------------------------------------------------

void paint_cde_raised(cairo_t* cr, const GuiRect& r) {
    ring(cr, r, palette().hilight, palette().shadow);
}

void paint_cde_sunken(cairo_t* cr, const GuiRect& r) {
    ring(cr, r, palette().shadow, palette().hilight);
}

void paint_cde_armed(cairo_t* cr, const GuiRect& r) {
    paint_cell_rect(cr, r, palette().cde_select);
    paint_cde_sunken(cr, r);
}

void paint_cde_field(cairo_t* cr, const GuiRect& r, GuiColor ground) {
    paint_cell_rect(cr, r, ground);
    ring(cr, r, palette().cde_field_bs, palette().cde_field_ts);
}

// -- THE CAPTION ---------------------------------------------------------------

void paint_cde_caption_box(cairo_t* cr, const GuiRect& b,
                           bool active, bool pressed) {
    const FrameTones t = caption_tones(active);
    paint_cell_rect(cr, b, t.face);
    if (pressed) ring(cr, b, t.bs, t.ts);
    else         ring(cr, b, t.ts, t.bs);
}

void paint_cde_caption_glyph(cairo_t* cr, const GuiRect& b, CdeCaptionBox which,
                             bool active, bool enabled) {
    // THE FOOTPRINTS in Windows px (the declaration): the bar 10 x 4, the
    // small square 4 x 4, the large square 10 x 10; the title box has none.
    int gw = 0, gh = 0;
    switch (which) {
        case CdeCaptionBox::WindowMenu: gw = 10; gh = 4;  break;
        case CdeCaptionBox::Minimize:   gw = 4;  gh = 4;  break;
        case CdeCaptionBox::Maximize:   gw = 10; gh = 10; break;
        case CdeCaptionBox::Title:      return;
    }
    const ChromeSpec& spec = live_chrome_spec();
    // Centred by integer division of Windows px, each part rounded at the
    // element (scaled_px's rule).
    const int gx = b.x + scaled_px((spec.caption_button_w_px - gw) / 2);
    const int gy = b.y + scaled_px((spec.caption_button_h_px - gh) / 2);
    const GuiRect g{gx, gy, scaled_px(gw, 2 * relief_line_px()),
                    scaled_px(gh, 2 * relief_line_px())};
    const FrameTones t = caption_tones(active);
    if (enabled) {
        raised_bar(cr, g, t);
        return;
    }
    // Motif's stipple of the bar (the declaration), its phase at the bar's
    // top-left, the box's face showing through the dropped half.
    paint_stipple_begin(cr, g);
    raised_bar(cr, g, t);
    paint_stipple_end(cr, g.x, g.y);
}

// -- THE SCROLL BAR ------------------------------------------------------------

void paint_cde_trough(cairo_t* cr, const GuiRect& bar) {
    paint_cell_rect(cr, bar, palette().cde_select);
    paint_cde_sunken(cr, bar);
}

void paint_cde_slider(cairo_t* cr, const GuiRect& body, bool horizontal) {
    const int lw = relief_line_px();
    const GuiRect r = horizontal
        ? GuiRect{body.x, body.y + lw, body.w, std::max(0, body.h - 2 * lw)}
        : GuiRect{body.x + lw, body.y, std::max(0, body.w - 2 * lw), body.h};
    if (r.w <= 0 || r.h <= 0) return;
    paint_cell_rect(cr, r, palette().ground);
    paint_cde_raised(cr, r);
}

void paint_cde_arrow(cairo_t* cr, const GuiRect& b, CdeArrowDir dir,
                     bool pressed) {
    // THE TRIANGLE'S BOX (the declaration): the cap less one W on every
    // side — the trough's ring on the bar's end and its two long sides, one
    // W of trough at the slider's side — whichever way the arrow points.
    paint_cde_arrow_triangle(cr, inner(b), dir, pressed);
}

void paint_cde_arrow_triangle(cairo_t* cr, const GuiRect& t, CdeArrowDir dir,
                              bool pressed) {
    const int lw = relief_line_px();
    if (t.w <= 0 || t.h <= 0) return;
    const double x0 = t.x, y0 = t.y, x1 = t.x + t.w, y1 = t.y + t.h;
    const double xm = x0 + t.w / 2.0, ym = y0 + t.h / 2.0;
    // The three vertices and, per edge, whether it is a light edge (the
    // declaration's rule: light where the edge faces up or left).
    struct Pt { double x = 0.0, y = 0.0; };
    Pt p[3] = {};
    bool light[3] = {};   // edge i runs from p[i] to p[(i + 1) % 3]
    switch (dir) {
        case CdeArrowDir::Left:
            p[0] = {x0, ym}; p[1] = {x1, y0}; p[2] = {x1, y1};
            light[0] = true;  light[1] = false; light[2] = false;
            break;
        case CdeArrowDir::Right:
            p[0] = {x0, y0}; p[1] = {x1, ym}; p[2] = {x0, y1};
            light[0] = true;  light[1] = false; light[2] = true;
            break;
        case CdeArrowDir::Up:
            p[0] = {xm, y0}; p[1] = {x1, y1}; p[2] = {x0, y1};
            light[0] = false; light[1] = false; light[2] = true;
            break;
        case CdeArrowDir::Down:
            p[0] = {x0, y0}; p[1] = {x1, y0}; p[2] = {xm, y1};
            light[0] = true;  light[1] = false; light[2] = true;
            break;
    }
    const GuiPalette& pal = palette();
    const GuiColor light_tone = pressed ? pal.shadow : pal.hilight;
    const GuiColor dark_tone  = pressed ? pal.hilight : pal.shadow;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    const auto triangle = [&] {
        cairo_new_path(cr);
        cairo_move_to(cr, p[0].x, p[0].y);
        cairo_line_to(cr, p[1].x, p[1].y);
        cairo_line_to(cr, p[2].x, p[2].y);
        cairo_close_path(cr);
    };
    // THE FACE, then the edges inside it: each edge stroked two W wide along
    // the triangle's outline under a clip to the triangle, so one W of the
    // stroke lies inside and the rest is cut; the dark edges first, the light
    // ones over them (the declaration's corner rule).
    triangle();
    set_palette_source(cr, pal.ground);
    cairo_fill(cr);
    triangle();
    cairo_clip(cr);
    cairo_set_line_width(cr, 2.0 * lw);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    for (int pass = 0; pass < 2; ++pass) {
        const bool want_light = pass == 1;
        for (int i = 0; i < 3; ++i) {
            if (light[i] != want_light) continue;
            const Pt& a = p[i];
            const Pt& c = p[(i + 1) % 3];
            cairo_new_path(cr);
            cairo_move_to(cr, a.x, a.y);
            cairo_line_to(cr, c.x, c.y);
            set_palette_source(cr, want_light ? light_tone : dark_tone);
            cairo_stroke(cr);
        }
    }
    cairo_restore(cr);
}

// -- THE SCALE -----------------------------------------------------------------

void paint_cde_scale_trough(cairo_t* cr, const GuiRect& trough) {
    paint_cell_rect(cr, trough, palette().cde_select);
    paint_cde_sunken(cr, trough);
}

void paint_cde_scale_slider(cairo_t* cr, const GuiRect& box) {
    if (box.w <= 0 || box.h <= 0) return;
    paint_cell_rect(cr, box, palette().ground);
    paint_cde_raised(cr, box);
}

// -- THE WINDOW'S FRAME ---------------------------------------------------------

void paint_cde_window_frame(cairo_t* cr, int surface_w, int surface_h,
                            int frame_px, bool focused) {
    if (frame_px <= 0 || surface_w <= 0 || surface_h <= 0) return;
    const int f  = frame_px;
    const int w  = surface_w;
    const int h  = surface_h;
    const int lw = relief_line_px();
    const FrameTones t = caption_tones(focused);
    // The face across the band.
    paint_cell_rect(cr, GuiRect{0, 0, w, f}, t.face);
    paint_cell_rect(cr, GuiRect{0, h - f, w, f}, t.face);
    paint_cell_rect(cr, GuiRect{0, f, f, h - 2 * f}, t.face);
    paint_cell_rect(cr, GuiRect{w - f, f, f, h - 2 * f}, t.face);
    // THE OUTER SHADOW, two lines on the surface's edge (two concentric
    // one-line rings, each mitred, so the two tones meet on one diagonal),
    // and THE INNER SHADOW, one sunken line on the band's inner edge.
    const GuiRect outer{0, 0, w, h};
    ring(cr, outer, t.ts, t.bs);
    ring(cr, inner(outer), t.ts, t.bs);
    const int in = f - lw;
    if (in > 0 && w > 2 * in && h > 2 * in)
        ring(cr, GuiRect{in, in, w - 2 * in, h - 2 * in}, t.bs, t.ts);
    // THE CORNER PIECES (the declaration): on each of the eight runs a
    // groove, a dark line then a light one, whose seam stands `L` from the
    // run's corner — the frame plus the caption lane, dtwm's corner — the
    // dark line across the band from the outer shadow's second line to the
    // inner shadow, the light one to the band's inner edge, the outermost
    // line unbroken.
    const int L = f + caption_row_h_px();
    if (w < 2 * (L + lw) || h < 2 * (L + lw)) return;
    const int dark_lo = lw, dark_hi = f - lw;   // depth into the band
    const int light_lo = lw, light_hi = f;
    // The top and bottom runs: a seam column `sx`, the depth measured down
    // from the top edge or up from the bottom one.
    const auto horizontal = [&](int sx, bool top) {
        const auto rows = [&](int lo, int hi) {
            return top ? std::pair{lo, hi - lo} : std::pair{h - hi, hi - lo};
        };
        const auto [dy, dh] = rows(dark_lo, dark_hi);
        const auto [ly, lh] = rows(light_lo, light_hi);
        paint_cell_rect(cr, GuiRect{sx - lw, dy, lw, dh}, t.bs);
        paint_cell_rect(cr, GuiRect{sx, ly, lw, lh}, t.ts);
    };
    // The left and right runs: a seam row `sy`, the depth measured in from
    // the left edge or from the right one.
    const auto vertical = [&](int sy, bool left) {
        const auto cols = [&](int lo, int hi) {
            return left ? std::pair{lo, hi - lo} : std::pair{w - hi, hi - lo};
        };
        const auto [dx, dw] = cols(dark_lo, dark_hi);
        const auto [lx, lwid] = cols(light_lo, light_hi);
        paint_cell_rect(cr, GuiRect{dx, sy - lw, dw, lw}, t.bs);
        paint_cell_rect(cr, GuiRect{lx, sy, lwid, lw}, t.ts);
    };
    horizontal(L, true);
    horizontal(w - L, true);
    horizontal(L, false);
    horizontal(w - L, false);
    vertical(L, true);
    vertical(h - L, true);
    vertical(L, false);
    vertical(h - L, false);
}
