#include "cool_edit_paint.h"

#include "text_shape.h"

#include <cmath>

// The rules are at the declarations (cool_edit_paint.h); the construction is
// the approved mock's (tmp/mocks/cool_edit/mock_ce.py), each W of it a
// rounded part here.

namespace {

void cell(cairo_t* cr, int x, int y, int w, int h, uint32_t word) {
    paint_cell_rect(cr, GuiRect{x, y, w, h}, hex(word));
}
void cell(cairo_t* cr, int x, int y, int w, int h, GuiColor c) {
    paint_cell_rect(cr, GuiRect{x, y, w, h}, c);
}

uint32_t lerp_word(uint32_t a, uint32_t b, double t) {
    uint32_t out = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
        const double ca = static_cast<double>((a >> shift) & 0xFF);
        const double cb = static_cast<double>((b >> shift) & 0xFF);
        const auto v = static_cast<uint32_t>(std::nearbyint(ca + (cb - ca) * t));
        out |= v << shift;
    }
    return out;
}

// The shadow ramp's tone at step i of n (its two ends exact).
uint32_t shadow_at(int i, int n) {
    const double t = n > 1 ? static_cast<double>(i) / (n - 1) : 0.0;
    return lerp_word(kCeCaseShadowFirst, kCeCaseShadowLast, t);
}

// The seat's face over the g x g square at (x, y): stop i on the seat's W
// row i, its device rows the rounded edges i·g/20 to (i+1)·g/20 (the
// ramp's rule, cool_edit_paint.h).
void paint_face_ramp(cairo_t* cr, int x, int y, int g) {
    const int m = static_cast<int>(kCeFaceRamp.size());
    for (int i = 0; i < m; ++i) {
        const int y0 = static_cast<int>(
            std::nearbyint(static_cast<double>(i) * g / m));
        const int y1 = static_cast<int>(
            std::nearbyint(static_cast<double>(i + 1) * g / m));
        if (y1 > y0)
            cell(cr, x, y + y0, g, y1 - y0,
                 kCeFaceRamp[static_cast<std::size_t>(i)]);
    }
}

} // namespace

int paint_ce_case(cairo_t* cr, const GuiRect& r, bool down) {
    if (r.w <= 0 || r.h <= 0) return 0;
    const int lw = program_line_px();
    const int g  = r.w - 3 * lw;   // the glyph seat: the case less its lines
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    if (down) {
        // THE OUTER RING, black on the top row and left column and F8F8F8
        // on the bottom row and right column, MITRED at its top-right and
        // bottom-left corner blocks (the head's diagonal rule: a two-tone
        // ring, as the chrome's sunken edges are, accepted 2026-10-09); 808080
        // inside the black, square (one tone, its partner the face); the
        // ramp on the seat one line further in.
        paint_relief_frame(cr, r, hex(kCeRingOuter), hex(kCeRingLight));
        cell(cr, r.x + lw, r.y + lw, lw + g, lw, kCeRingMid);
        cell(cr, r.x + lw, r.y + lw, lw, lw + g, kCeRingMid);
        paint_face_ramp(cr, r.x + 2 * lw, r.y + 2 * lw, g);
        cairo_restore(cr);
        return lw;
    }
    paint_face_ramp(cr, r.x + lw, r.y + lw, g);
    // THE HIGHLIGHT / SHADOW RING inside the black line, MITRED at its
    // top-right and bottom-left corner blocks (the head's diagonal rule):
    // the highlight's square L, the shadow's first tone over it as the
    // mitred L — Cool Edit's measured ADAEAF junction pixel is exactly the
    // two tones' half-and-half, the diagonal at one capture px — then the
    // shadow's ramp over the ring's straight runs, each step one device row
    // or column, the corner blocks left as the mitre drew them.
    paint_relief_frame(cr, GuiRect{r.x, r.y, r.w - lw, r.h - lw},
                       hex(kCeCaseHighlight), hex(kCeCaseShadowFirst));
    for (int i = 0; i < g; ++i) {
        const uint32_t s = shadow_at(i, g);
        cell(cr, r.x + lw + g, r.y + lw + i, lw, 1, s);
        cell(cr, r.x + lw + i, r.y + lw + g, 1, lw, s);
    }
    // The shadow's own corner (one tone, square), then the black line on
    // the right and bottom (one tone, square).
    cell(cr, r.x + lw + g, r.y + lw + g, lw, lw, kCeCaseCornerShadow);
    cell(cr, r.x + 2 * lw + g, r.y, lw, r.h, kCeCaseOuter);
    cell(cr, r.x, r.y + 2 * lw + g, r.w, lw, kCeCaseOuter);
    cairo_restore(cr);
    return 0;
}

void paint_ce_band_ground(cairo_t* cr, const GuiRect& band) {
    if (band.w <= 0 || band.h <= 0) return;
    const int lw = program_line_px();
    const GuiPalette& p = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cell(cr, band.x, band.y, band.w, lw, p.ce_dark);
    cell(cr, band.x, band.y + lw, band.w, band.h - 2 * lw, p.ce_recess);
    cell(cr, band.x, band.y + band.h - lw, band.w, lw, p.ce_hilight);
    cairo_restore(cr);
}

void paint_ce_pane(cairo_t* cr, const GuiRect& band, int x0, int x1) {
    const int lw = program_line_px();
    const int w = x1 - x0;
    if (w <= 4 * lw || band.h <= 5 * lw) return;
    const GuiPalette& p = palette();
    const int top  = band.y + lw;                 // the light top row
    const int foot = band.y + band.h - lw;        // past the dark foot row
    // THE OUTER RING — the light top row and left column, the groove's dark
    // column and the dark foot row — and THE INNER RING inside it — the face
    // (no line) and the mid right column and mid foot row: the two-line
    // raised edge, each ring MITRED at its top-right and bottom-left corner
    // blocks, the two diagonals one 45-degree line (paint_relief_frame,
    // render.cpp; the head's diagonal rule). Cool Edit's one mid pixel atop
    // the groove (METRICS §1.2) is the outer ring's top-right block at one
    // capture px; the reading's one change to his pixels is the light left
    // column's last cell, the outer ring's own where Cool Edit drew the mid
    // row through it (accepted 2026-10-09).
    const GuiRect outer{x0, top, w, foot - top};
    const GuiRect inner{x0 + lw, top + lw, w - 2 * lw, foot - top - 2 * lw};
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, inner, p.face);
    paint_relief_frame(cr, outer, p.ce_hilight, p.ce_dark);
    paint_relief_frame(cr, inner, p.face, p.ce_mid);
    cairo_restore(cr);
}

void paint_ce_band_dark_column(cairo_t* cr, const GuiRect& band, int x) {
    const int lw = program_line_px();
    if (band.h <= 2 * lw) return;
    const GuiPalette& p = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cell(cr, x, band.y + lw, lw, band.h - 2 * lw, p.ce_dark);
    cairo_restore(cr);
}

void paint_ce_dock_bar(cairo_t* cr, const GuiRect& bar) {
    if (bar.w <= 0 || bar.h <= 0) return;
    const int lw = program_line_px();
    const GuiPalette& p = palette();
    const int mid_y = bar.y + bar.h - 2 * lw;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cell(cr, bar.x, bar.y, bar.w, bar.h, p.face);
    // The light / mid ring down to the mid row, MITRED (the head's rule);
    // the dark row under it one tone, square.
    paint_relief_frame(cr, GuiRect{bar.x, bar.y, bar.w, mid_y + lw - bar.y},
                       p.ce_hilight, p.ce_mid);
    cell(cr, bar.x, mid_y + lw, bar.w, lw, p.ce_dark);
    cairo_restore(cr);
}

void paint_ce_gripper(cairo_t* cr, int x, int case_y, int case_h) {
    const int lw = program_line_px();
    const int reach = scaled_px(kProgramSpec.gripper_reach_px);
    const int y0 = case_y - reach;
    const int h  = case_h + 2 * reach;
    const GuiPalette& p = palette();
    // ONE ETCHED PAIR at column `cx`: the light column on rows [y0, y0 + h)
    // and the mid column right of it one line lower — a two-line STAIRCASE
    // at each end, drawn as its diagonal (the head's rule): each end cut
    // along the 45-degree line through the two end cells' like corners (the
    // top-left corners at the head, the bottom-right at the foot), so the
    // pair is a parallelogram that keeps the staircase's two outermost
    // corners and each end cell its half toward the column. The two share
    // their pixel-aligned seam; the cuts are antialiased against the face.
    const auto pair = [&](int cx) {
        const double a = cx, b = cx + lw, c = cx + 2 * lw;
        const double t = y0, e = y0 + h;
        cairo_new_path(cr);
        cairo_move_to(cr, a, t);
        cairo_line_to(cr, b, t + lw);
        cairo_line_to(cr, b, e);
        cairo_line_to(cr, a, e - lw);
        cairo_close_path(cr);
        set_palette_source(cr, p.ce_hilight);
        cairo_fill(cr);
        cairo_move_to(cr, b, t + lw);
        cairo_line_to(cr, c, t + 2 * lw);
        cairo_line_to(cr, c, e + lw);
        cairo_line_to(cr, b, e);
        cairo_close_path(cr);
        set_palette_source(cr, p.ce_mid);
        cairo_fill(cr);
    };
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    pair(x);
    pair(x + 3 * lw);
    cairo_restore(cr);
}

void paint_ce_end_bar(cairo_t* cr, int x, int y, int h) {
    const int lw = program_line_px();
    const int face = scaled_px(kProgramSpec.end_bar_face_px);
    const GuiPalette& p = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // The light / mid ring, MITRED (the head's rule); the dark column right
    // of it one tone, square.
    paint_relief_frame(cr, GuiRect{x, y, 2 * lw + face, h}, p.ce_hilight,
                       p.ce_mid);
    cell(cr, x + 2 * lw + face, y, lw, h, p.ce_dark);
    cairo_restore(cr);
}

void paint_ce_time_field(cairo_t* cr, const GuiRect& f) {
    if (f.w <= 0 || f.h <= 0) return;
    const GuiPalette& p = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cell(cr, f.x, f.y, f.w, f.h, p.ce_mid);
    // The field-dark / field-light ring, MITRED (the head's rule).
    paint_relief_frame(cr, f, p.ce_field_dark, p.ce_field_light);
    cairo_restore(cr);
}

void paint_ce_cue_triangle(cairo_t* cr, int col, int top, GuiColor color) {
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
        cairo_new_path(cr);
        cairo_move_to(cr, axis - half + dx, y);
        cairo_line_to(cr, axis + half + dx, y);
        cairo_line_to(cr, axis + dx, y + rows * u);
        cairo_close_path(cr);
        set_palette_source(cr, c);
        cairo_fill(cr);
    };
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    triangle(u, palette().ce_cue_shadow);
    triangle(0.0, color);
    cairo_restore(cr);
}

void show_ce_label(cairo_t* cr, const text_shape::ShapedRun& run, double x,
                   double baseline) {
    const double lw = static_cast<double>(program_line_px());
    set_palette_source(cr, palette().ce_dark);
    text_shape::show_shaped_run(cr, run, x + lw, baseline + lw);
    set_palette_source(cr, palette().ce_label);
    text_shape::show_shaped_run(cr, run, x, baseline);
}
