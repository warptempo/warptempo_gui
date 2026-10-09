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
        // F8F8F8 under all, so its bottom row and right column stand; the
        // black top row and left column, 808080 inside them, the ramp on
        // the seat one line further in.
        cell(cr, r.x, r.y, r.w, r.h, kCeRingLight);
        cell(cr, r.x, r.y, 2 * lw + g, lw, kCeRingOuter);
        cell(cr, r.x, r.y, lw, 2 * lw + g, kCeRingOuter);
        cell(cr, r.x + lw, r.y + lw, lw + g, lw, kCeRingMid);
        cell(cr, r.x + lw, r.y + lw, lw, lw + g, kCeRingMid);
        paint_face_ramp(cr, r.x + 2 * lw, r.y + 2 * lw, g);
        cairo_restore(cr);
        return lw;
    }
    paint_face_ramp(cr, r.x + lw, r.y + lw, g);
    // The highlight along the top row and down the left column.
    cell(cr, r.x, r.y, lw + g, lw, kCeCaseHighlight);
    cell(cr, r.x, r.y + lw, lw, g, kCeCaseHighlight);
    // The shadow ramp down the right column and across the bottom row.
    for (int i = 0; i < g; ++i) {
        const uint32_t s = shadow_at(i, g);
        cell(cr, r.x + lw + g, r.y + lw + i, lw, 1, s);
        cell(cr, r.x + lw + i, r.y + lw + g, 1, lw, s);
    }
    // The junctions, then the black line on the right and bottom.
    cell(cr, r.x + lw + g, r.y, lw, lw, kCeCaseCornerLight);
    cell(cr, r.x, r.y + lw + g, lw, lw, kCeCaseCornerLight);
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
    if (w <= 2 * lw || band.h <= 5 * lw) return;
    const GuiPalette& p = palette();
    const int top    = band.y + lw;                 // the light top row
    const int face_y = top + lw;
    const int mid_y  = band.y + band.h - 3 * lw;    // the foot's mid row
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cell(cr, x0, face_y, w, mid_y - face_y, p.face);
    cell(cr, x0, top, w, lw, p.ce_hilight);
    cell(cr, x0, face_y, lw, mid_y - face_y, p.ce_hilight);
    cell(cr, x1 - lw, face_y, lw, mid_y - face_y, p.ce_mid);
    cell(cr, x0, mid_y, w, lw, p.ce_mid);
    cell(cr, x0, mid_y + lw, w, lw, p.ce_dark);
    cairo_restore(cr);
}

void paint_ce_band_dark_column(cairo_t* cr, const GuiRect& band, int x,
                               bool mitre) {
    const int lw = program_line_px();
    if (band.h <= 2 * lw) return;
    const GuiPalette& p = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cell(cr, x, band.y + lw, lw, band.h - 2 * lw, p.ce_dark);
    if (mitre) cell(cr, x, band.y + lw, lw, lw, p.ce_mid);
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
    cell(cr, bar.x, bar.y, bar.w - lw, lw, p.ce_hilight);
    cell(cr, bar.x, bar.y + lw, lw, mid_y - bar.y - lw, p.ce_hilight);
    cell(cr, bar.x + bar.w - lw, bar.y, lw, mid_y - bar.y, p.ce_mid);
    cell(cr, bar.x, mid_y, bar.w, lw, p.ce_mid);
    cell(cr, bar.x, mid_y + lw, bar.w, lw, p.ce_dark);
    cairo_restore(cr);
}

void paint_ce_gripper(cairo_t* cr, int x, int case_y, int case_h) {
    const int lw = program_line_px();
    const int reach = scaled_px(kProgramSpec.gripper_reach_px);
    const int y0 = case_y - reach;
    const int h  = case_h + 2 * reach;
    const GuiPalette& p = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cell(cr, x, y0, lw, h, p.ce_hilight);
    cell(cr, x + 3 * lw, y0, lw, h, p.ce_hilight);
    cell(cr, x + lw, y0 + lw, lw, h, p.ce_mid);
    cell(cr, x + 4 * lw, y0 + lw, lw, h, p.ce_mid);
    cairo_restore(cr);
}

void paint_ce_end_bar(cairo_t* cr, int x, int y, int h) {
    const int lw = program_line_px();
    const int face = scaled_px(kProgramSpec.end_bar_face_px);
    const GuiPalette& p = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cell(cr, x, y, lw, h - lw, p.ce_hilight);
    cell(cr, x, y, lw + face, lw, p.ce_hilight);
    cell(cr, x + lw + face, y, lw, h, p.ce_mid);
    cell(cr, x + 2 * lw + face, y, lw, h, p.ce_dark);
    cell(cr, x, y + h - lw, 2 * lw + face, lw, p.ce_mid);
    cairo_restore(cr);
}

void paint_ce_time_field(cairo_t* cr, const GuiRect& f) {
    if (f.w <= 0 || f.h <= 0) return;
    const int lw = program_line_px();
    const GuiPalette& p = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cell(cr, f.x, f.y, f.w, f.h, p.ce_mid);
    cell(cr, f.x, f.y, f.w - lw, lw, p.ce_field_dark);
    cell(cr, f.x, f.y, lw, f.h - lw, p.ce_field_dark);
    cell(cr, f.x, f.y + f.h - lw, f.w, lw, p.ce_field_light);
    cell(cr, f.x + f.w - lw, f.y, lw, f.h, p.ce_field_light);
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
