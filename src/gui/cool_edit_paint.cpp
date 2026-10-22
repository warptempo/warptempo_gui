#include "cool_edit_paint.h"

#include "materials.h"   // the case's ebony, the tabs' brass, the inks
#include "text_shape.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// The rules are at the declarations (cool_edit_paint.h); the construction is
// the approved mock's (tmp/mocks/cool_edit/mock_ce.py), each W of it a
// rounded part here.

namespace {

// One cell of a surface tone (paint_cell_rect: the scope's material).
void cell(cairo_t* cr, int x, int y, int w, int h, GuiColor c) {
    paint_cell_rect(cr, GuiRect{x, y, w, h}, c);
}

// Each channel's byte between a and b at t, rounded to its byte.
GuiColor lerp_color(const GuiColor& a, const GuiColor& b, double t) {
    const auto ch = [t](double ca, double cb) {
        return std::nearbyint((ca + (cb - ca) * t) * 255.0) / 255.0;
    };
    return GuiColor{ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b)};
}

} // namespace

int paint_ce_case(cairo_t* cr, const GuiRect& r, bool down) {
    if (r.w <= 0 || r.h <= 0) return 0;
    const int lw = program_line_px();
    // THE GLYPH SEAT: the case less its lines on each axis — square on the
    // band's, row 8's and the render player's cases (r.w == r.h), gw x gh on
    // a word button or a key (the declaration).
    const int gw = r.w - 3 * lw;
    const int gh = r.h - 3 * lw;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // THE CASE'S OWN TONES ARE FLAT, never a scope's (the ebony's lines and
    // Cool Edit's constants, materials.h's CASE).
    const GuiMaterialScope flat(GuiMaterialRegion::Flat);
    if (down) {
        // THE PRESSED AND CHECKED FACE ON THE EBONY (architect 2026-10-21,
        // "the top and left should be darker than the case"; Cool Edit's
        // constant F8F8F8 / 808080 rings, measured on a light grey case,
        // read as a raised white frame on the ebony): THE OUTER RING black on
        // the top row and left column and THE CASE'S HIGHLIGHT on the bottom
        // row and right column, MITRED at its top-right and bottom-left
        // corner blocks (the head's diagonal rule); THE INNER RING the case's
        // LAST SHADOW TONE on the top row and left column, square (one tone,
        // its partner the face), and nothing on the bottom and right, the
        // ramp running on to the outer ring there; the ramp on the seat one
        // line further in, the ebony's pressed patch
        // (materials::paint_case_ramp). Every tone a flat stroke off the
        // ebony's Face (materials::case_lines).
        const materials::CaseLines lines = materials::case_lines();
        paint_relief_frame(cr, r, hex(kCeRingOuter), lines.highlight);
        paint_ink_rect(cr, GuiRect{r.x + lw, r.y + lw, lw + gw, lw},
                       lines.shadow_last);
        paint_ink_rect(cr, GuiRect{r.x + lw, r.y + lw, lw, lw + gh},
                       lines.shadow_last);
        materials::paint_case_ramp(cr, r.x + 2 * lw, r.y + 2 * lw, gw, gh,
                                   true);
        cairo_restore(cr);
        return lw;
    }
    materials::paint_case_ramp(cr, r.x + lw, r.y + lw, gw, gh, false);
    // THE HIGHLIGHT / SHADOW RING inside the black line, MITRED at its
    // top-right and bottom-left corner blocks (the head's diagonal rule):
    // the highlight's square L, the shadow's first tone over it as the
    // mitred L — Cool Edit's measured ADAEAF junction pixel is exactly the
    // two tones' half-and-half, the diagonal at one capture px — then the
    // shadow's ramp over the ring's straight runs, each step one device row
    // down the right column (gh steps) or one device column along the bottom
    // row (gw steps), the corner blocks left as the mitre drew them: the
    // ebony's flat strokes (materials::case_lines). The two runs' cells are
    // disjoint (the column's rows end where the bottom row's begins), so the
    // order between them paints nothing differently; within a run, STEPS OF
    // ONE BYTE TRIPLE ARE ONE FILL (2026-10-22, the same opaque aliased
    // pixels: the ramp's two ends lie a few bytes apart, so a key's 200-odd
    // columns are a handful of fills, not one each).
    const materials::CaseLines lines = materials::case_lines();
    paint_relief_frame(cr, GuiRect{r.x, r.y, r.w - lw, r.h - lw},
                       lines.highlight, lines.shadow_first);
    const auto shadow_ramp = [&](int n, const auto& cells) {
        const auto tone = [&](int i) {
            const double t = n > 1 ? static_cast<double>(i) / (n - 1) : 0.0;
            return lerp_color(lines.shadow_first, lines.shadow_last, t);
        };
        int      i0 = 0;
        GuiColor c0 = tone(0);
        for (int i = 1; i <= n; ++i) {
            const GuiColor c = i < n ? tone(i) : c0;
            if (i < n && c.r == c0.r && c.g == c0.g && c.b == c0.b) continue;
            paint_ink_rect(cr, cells(i0, i - i0), c0);
            i0 = i;
            c0 = c;
        }
    };
    if (gh > 0)
        shadow_ramp(gh, [&](int i, int n) {
            return GuiRect{r.x + lw + gw, r.y + lw + i, lw, n};
        });
    if (gw > 0)
        shadow_ramp(gw, [&](int i, int n) {
            return GuiRect{r.x + lw + i, r.y + lw + gh, n, lw};
        });
    // The shadow's own corner (one tone, square), then the black line on
    // the right and bottom (one tone, square).
    paint_ink_rect(cr, GuiRect{r.x + lw + gw, r.y + lw + gh, lw, lw},
                   lines.corner);
    paint_ink_rect(cr, GuiRect{r.x + 2 * lw + gw, r.y, lw, r.h},
                   hex(kCeCaseOuter));
    paint_ink_rect(cr, GuiRect{r.x, r.y + 2 * lw + gh, r.w, lw},
                   hex(kCeCaseOuter));
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

void paint_ce_time_field(cairo_t* cr, const GuiRect& f) {
    if (f.w <= 0 || f.h <= 0) return;
    const GuiPalette& p = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // THE GROUND BLACK (materials.h's kTimeFieldGround, 2026-10-21; Cool
    // Edit's `mid` tone until then), ink: a constant, flat.
    paint_ink_rect(cr, f, hex(kTimeFieldGround));
    // The field-dark / field-light ring, MITRED (the head's rule), the
    // scope's tones.
    paint_relief_frame(cr, f, p.ce_field_dark, p.ce_field_light);
    cairo_restore(cr);
}

// -- THE TAB STRIP (architect 2026-10-18; the rules at the declaration) ------

namespace {

// A ONE-LINE TOP-RIGHT CORNER BLOCK at (bx, by), `lw` square, mitred (this
// file's head): `top` above the 45-degree diagonal from the block's outer
// corner (its top right) to its inner one (its bottom left), `right` below
// it, antialiased — paint_relief_frame's corner, one block.
void mitre_top_right(cairo_t* cr, int bx, int by, int lw, GuiColor top,
                     GuiColor right) {
    cell(cr, bx, by, lw, lw, top);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_new_path(cr);
    cairo_move_to(cr, bx + lw, by);
    cairo_line_to(cr, bx + lw, by + lw);
    cairo_line_to(cr, bx, by + lw);
    cairo_close_path(cr);
    set_surface_source(cr, right);
    cairo_fill(cr);
    cairo_restore(cr);
}

// THE LIT TAB'S LEFT FOOT at (bx, by), `lw` square: a ring's BOTTOM-RIGHT
// corner, mitred — the diagonal from its top left (inner) to its bottom
// right (outer) — `column` above it, `below` under it, antialiased (the
// declaration).
void foot_bottom_right(cairo_t* cr, int bx, int by, int lw, GuiColor column,
                     GuiColor below) {
    cell(cr, bx, by, lw, lw, below);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_new_path(cr);
    cairo_move_to(cr, bx, by);
    cairo_line_to(cr, bx + lw, by);
    cairo_line_to(cr, bx + lw, by + lw);
    cairo_close_path(cr);
    set_surface_source(cr, column);
    cairo_fill(cr);
    cairo_restore(cr);
}

// THE LIT TAB'S RIGHT FOOT at (bx, by), `lw` square: the left foot's mirror,
// a ring's BOTTOM-LEFT corner, mitred — the diagonal from its bottom left
// (outer) to its top right (inner) — `column` above-left of it, `line`
// below-right, antialiased (the declaration).
void foot_bottom_left(cairo_t* cr, int bx, int by, int lw, GuiColor column,
                      GuiColor line) {
    cell(cr, bx, by, lw, lw, line);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_new_path(cr);
    cairo_move_to(cr, bx, by);
    cairo_line_to(cr, bx + lw, by);
    cairo_line_to(cr, bx, by + lw);
    cairo_close_path(cr);
    set_surface_source(cr, column);
    cairo_fill(cr);
    cairo_restore(cr);
}

} // namespace

CeTabStripLayout ce_tab_strip_layout(
    const GuiRect& lane, const GuiFont& font,
    const std::array<const text_shape::ShapedRun*, kCeTabCount>& labels,
    int active) {
    const ProgramSpec& p = kProgramSpec;
    const int lw     = program_line_px();
    const int page_y = lane.y + tab_strip_recess_h_px();
    const int top_i  = lane.y + scaled_px(p.tab_inactive_top_px);
    const int top_a  = lane.y + scaled_px(p.tab_active_top_px);
    const int pad    = scaled_px(p.tab_pad_px);
    const int gap    = scaled_px(p.tab_gap_px);
    const int grow   = ce_tab_growth_px();
    CeTabStripLayout out;
    // The first footprint one W in from the page edge, its own rounded part
    // (the lit tab's growth the same W, so the first tab lit stands on the
    // edge).
    int x = lane.x + scaled_px(p.tab_lead_px);
    for (int i = 0; i < kCeTabCount; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        // THE CELL, NOT THE INK (architect 2026-10-18 ~20:20): the label's
        // shaped advance, the chokepoint's measure as every label's, its own
        // rounded part, the pad either side of it between the edge columns —
        // so the cell is centered in the footprint.
        const text_shape::ShapedRun& run = *labels[k];
        const int cell_w = static_cast<int>(std::nearbyint(run.width_px));
        const int wid = lw + pad + cell_w + pad + lw;
        const bool lit = i == active;
        out.tab[k] = lit ? GuiRect{x - grow, top_a, wid + 2 * grow,
                                   page_y + lw - top_a}
                         : GuiRect{x, top_i, wid, page_y - top_i};
        // The run's origin at the cell's left edge, the same lit or unlit —
        // the lit frame grows round the fixed label — and never shifted by
        // the first glyph's bearing: the ink lands where the face puts it.
        out.label_x[k] = static_cast<double>(x + lw + pad);
        // The body under the top light row: 15 W unlit, 17 W lit.
        const int body_y = (lit ? top_a : top_i) + lw;
        out.baseline[k] =
            redesign_baseline(font, static_cast<double>(body_y),
                              static_cast<double>(page_y - body_y));
        x += wid + gap;
    }
    return out;
}

void paint_ce_tab_strip(
    cairo_t* cr, const GuiRect& lane, const CeTabStripLayout& layout,
    const std::array<const text_shape::ShapedRun*, kCeTabCount>& labels,
    int active, const std::array<bool, kCeTabCount>& enabled) {
    if (lane.w <= 0 || lane.h <= 0) return;
    const int lw = program_line_px();
    const GuiPalette& p = palette();
    const int recess_h = tab_strip_recess_h_px();
    const int page_y = lane.y + recess_h;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // THE GROUND, THE WOOD (materials.h's WOOD, 2026-10-21): the strip's
    // recess the wood as drawn (the Face's cover — the frame's plank, not the
    // band's void), then the page's top light line, the wood's flat stroke.
    {
        const GuiMaterialScope wood(GuiMaterialRegion::Wood);
        cell(cr, lane.x, lane.y, lane.w, recess_h, p.face);
        cell(cr, lane.x, page_y, lane.w, lane.h - recess_h, p.ce_hilight);
    }
    // THE TABS, THE BRASS (materials.h's METAL): every body and edge below.
    const GuiMaterialScope metal(GuiMaterialRegion::Metal);
    // THE TAB LINE, one run under the inactive tabs, one W past each end
    // (its own rounded part).
    const int reach = scaled_px(kProgramSpec.tab_line_reach_px);
    int run0 = lane.x + lane.w;
    int run1 = lane.x;
    for (int i = 0; i < kCeTabCount; ++i) {
        if (i == active) continue;
        const GuiRect& t = layout.tab[static_cast<std::size_t>(i)];
        run0 = std::min(run0, t.x - reach);
        run1 = std::max(run1, t.x + t.w + reach);
    }
    if (run1 > run0) cell(cr, run0, page_y, run1 - run0, lw, p.ce_tab_line);
    const auto run_covers = [&](int col) { return col >= run0 && col < run1; };
    for (int i = 0; i < kCeTabCount; ++i) {
        const GuiRect& t = layout.tab[static_cast<std::size_t>(i)];
        if (i != active) {
            // THE BODY: the brass under Cool Edit's measured 15-row gradient
            // as a luma ratio, one stop per W row of its 15, the rows tiling
            // it exactly (kCaseRamp's rule) — its baked rows
            // (materials::paint_tab_body; materials.h's METAL).
            const int body_y = t.y + lw;
            materials::paint_tab_body(
                cr, GuiRect{t.x + lw, body_y, t.w - 2 * lw, page_y - body_y});
            // THE EDGES: the light top row and left column, the dark right
            // column, the top-right block mitred.
            cell(cr, t.x, t.y, lw, t.h, p.ce_tab_light);
            cell(cr, t.x, t.y, t.w - lw, lw, p.ce_tab_light);
            cell(cr, t.x + t.w - lw, t.y + lw, lw, t.h - lw, p.ce_tab_dark);
            mitre_top_right(cr, t.x + t.w - lw, t.y, lw, p.ce_tab_light,
                            p.ce_tab_dark);
            continue;
        }
        // The right growth is the drop shadow's column, the mid right column
        // one line inside it.
        const int grow = ce_tab_growth_px();
        const int end = t.x + t.w;          // past the shadow column
        const int shadow_x = end - grow;    // the drop shadow
        const int mid_x = shadow_x - lw;    // the mid right column
        // THE FACE through the page line (the tab merges into the page) — the
        // brass as drawn — the hilight left column through it too, the
        // hilight top row, the mid right column under its mitred top-right
        // block.
        cell(cr, t.x, t.y, shadow_x - t.x, t.h, p.face);
        cell(cr, t.x, t.y, lw, t.h, p.ce_hilight);
        cell(cr, t.x, t.y, mid_x - t.x, lw, p.ce_hilight);
        cell(cr, mid_x, t.y + lw, lw, page_y - t.y - lw, p.ce_mid);
        mitre_top_right(cr, mid_x, t.y, lw, p.ce_hilight, p.ce_mid);
        // THE SHADOW, square: the strip's ground in its top cell (the wood as
        // drawn), the page-dark tone to the recess's last row, the page line
        // under it.
        const GuiColor under_shadow =
            run_covers(shadow_x) ? p.ce_tab_line : p.ce_hilight;
        {
            const GuiMaterialScope wood(GuiMaterialRegion::Wood);
            cell(cr, shadow_x, t.y, grow, lw, p.face);
        }
        cell(cr, shadow_x, t.y + lw, grow, page_y - t.y - lw, p.ce_page_dark);
        cell(cr, shadow_x, page_y, grow, lw, under_shadow);
        // THE TWO FEET, one ring's two bottom corners (the declaration): the
        // right foot the mid column against the page line running on under
        // the shadow, mitred as a bottom-left corner; the left foot the
        // hilight column against the tab line arriving from the left, mitred
        // as a bottom-right corner, where one arrives.
        foot_bottom_left(cr, mid_x, page_y, lw, p.ce_mid, under_shadow);
        if (run_covers(t.x - 1))
            foot_bottom_right(cr, t.x, page_y, lw, p.ce_hilight, p.ce_tab_line);
    }
    // THE LABELS IN THE CHROME'S ONE INK over the words' drop shadow at
    // (+d, +d) device px, d the `text_shadow` key (materials.h's INKS,
    // architect 2026-10-21: kWordInk, the near-white earlier that day, the
    // program text and Cool Edit's measured ink before), lit or not; a dead
    // tab's embossed in the brass's tones.
    const double d = static_cast<double>(materials::text_shadow_px());
    for (int i = 0; i < kCeTabCount; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        const text_shape::ShapedRun& run = *labels[k];
        if (!enabled[k]) {
            show_embossed_run(cr, run, layout.label_x[k], layout.baseline[k]);
            continue;
        }
        set_palette_source(cr, hex(kWordShadow));
        text_shape::show_shaped_run(cr, run, layout.label_x[k] + d,
                                    layout.baseline[k] + d);
        set_palette_source(cr, hex(kWordInk));
        text_shape::show_shaped_run(cr, run, layout.label_x[k],
                                    layout.baseline[k]);
    }
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
    const double lw = static_cast<double>(program_line_px());
    set_palette_source(cr, palette().ce_label_shadow);
    text_shape::show_shaped_run(cr, run, x + lw, baseline + lw);
    set_palette_source(cr, hex(kWordInk));
    text_shape::show_shaped_run(cr, run, x, baseline);
}
