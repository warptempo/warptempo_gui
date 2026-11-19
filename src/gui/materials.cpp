#include "materials.h"

#include "cool_edit_paint.h"   // CoverageStampPass, the exact-coverage road

#include <cstddef>

// The rules are at the declarations (materials.h).

void paint_linked_group(cairo_t* cr, int x, int y,
                        std::span<const KeyState> states) {
    const int n = static_cast<int>(states.size());
    if (n <= 0) return;
    const GuiPalette& p = palette();
    const double b  = static_cast<double>(key_border_px());
    const double R  = static_cast<double>(key_corner_px());
    const double r  = static_cast<double>(key_inner_corner_px());
    const double w  = static_cast<double>(icon_case_w_px());
    const double h  = static_cast<double>(icon_case_h_px());
    const double x0 = static_cast<double>(x);
    const double y0 = static_cast<double>(y);
    const double y1 = y0 + h;
    const auto box_x = [&](int i) {
        return x0 + static_cast<double>(i * key_link_pitch_px());
    };
    const auto dim_ring = [&](int i) {
        return states[static_cast<std::size_t>(i)] == KeyState::Disabled;
    };
    CoverageStampPass pass;
    pass.begin(cr);
    // (1) THE RING, the group's rounded box, ONE STAMP A RUN of neighbors
    //     sharing a ring tone: a member's stretch from its box's left edge
    //     (one border in past a group's first, its left seam the left
    //     key's) to its box's right edge, a whole column; the outer corners
    //     on the runs that end the group.
    for (int i = 0; i < n;) {
        int j = i + 1;
        while (j < n && dim_ring(j) == dim_ring(i)) ++j;
        const double sx0 = i == 0 ? x0 : box_x(i) + b;
        const double sx1 = box_x(j - 1) + w;   // the group's right edge at j == n
        pass.rounded_rect(sx0, y0, sx1, y1, i == 0 ? R : 0.0,
                          j == n ? R : 0.0,
                          dim_ring(i) ? p.key_dim_border : p.well_border);
        i = j;
    }
    // (2) THE FACES: each member from one border inside its box to one
    //     border inside the next box (its right seam its own), the last to
    //     one border inside its own right edge, in its state's face — a lit
    //     member's the accent.
    for (int i = 0; i < n; ++i) {
        const KeyState s = states[static_cast<std::size_t>(i)];
        const GuiColor face = s == KeyState::Lit      ? p.cue
                            : s == KeyState::Disabled ? p.key_dim_face
                            : s == KeyState::LitInert ? p.lit_dim_fill
                                                      : p.key_face;
        const double fx0 = box_x(i) + b;
        const double fx1 = i == n - 1 ? box_x(i) + w - b : box_x(i + 1) + b;
        pass.rounded_rect(fx0, y0 + b, fx1, y1 - b, i == 0 ? r : 0.0,
                          i == n - 1 ? r : 0.0, face);
    }
    // (3) A LIT MEMBER'S LEFT SEAM in the accent, between the border's rows:
    //     whole cells over the left key's face.
    for (int i = 1; i < n; ++i) {
        if (states[static_cast<std::size_t>(i)] != KeyState::Lit) continue;
        pass.rect(box_x(i), y0 + b, box_x(i) + b, y1 - b, p.cue);
    }
    pass.end();
}

void paint_word_button(cairo_t* cr, const GuiRect& box, KeyState state) {
    if (box.w <= 0 || box.h <= 0) return;
    const GuiPalette& p = palette();
    const GuiColor face = state == KeyState::Lit      ? p.cue
                        : state == KeyState::Disabled ? p.key_dim_face
                        : state == KeyState::LitInert ? p.lit_dim_fill
                                                      : p.key_face;
    const GuiColor ring =
        state == KeyState::Disabled ? p.key_dim_border : p.well_border;
    const double b  = static_cast<double>(key_border_px());
    const double R  = static_cast<double>(key_corner_px());
    const double r  = static_cast<double>(key_inner_corner_px());
    const double x0 = static_cast<double>(box.x);
    const double y0 = static_cast<double>(box.y);
    const double x1 = x0 + static_cast<double>(box.w);
    const double y1 = y0 + static_cast<double>(box.h);
    CoverageStampPass pass;
    pass.begin(cr);
    pass.rounded_rect(x0 + b, y0 + b, x1 - b, y1 - b, r, r, face);
    pass.rounded_ring(x0, y0, x1, y1, R, b, ring);
    pass.end();
}

void paint_entry(cairo_t* cr, const GuiRect& box,
                 std::optional<double> progress, bool focused) {
    if (box.w <= 0 || box.h <= 0) return;
    const GuiPalette& p = palette();
    const double b  = static_cast<double>(key_border_px());
    const double R  = static_cast<double>(key_corner_px());
    const double r  = static_cast<double>(key_inner_corner_px());
    const double x0 = static_cast<double>(box.x);
    const double y0 = static_cast<double>(box.y);
    const double x1 = x0 + static_cast<double>(box.w);
    const double y1 = y0 + static_cast<double>(box.h);
    CoverageStampPass pass;
    pass.begin(cr);
    if (focused) {
        pass.rounded_rect(x0 + b, y0 + b, x1 - b, y1 - b, r, r, p.cue);
        const double r2 = std::max(0.0, r - b);
        pass.rounded_rect(x0 + 2 * b, y0 + 2 * b, x1 - 2 * b, y1 - 2 * b, r2,
                          r2, p.ruler_face);
    } else {
        pass.rounded_rect(x0 + b, y0 + b, x1 - b, y1 - b, r, r, p.ruler_face);
    }
    pass.rounded_ring(x0, y0, x1, y1, R, b, p.well_border);
    if (progress && *progress > 0.0) {
        const double t   = std::min(*progress, 1.0);
        const double ix0 = x0 + b, ix1 = x1 - b, iy1 = y1 - b;
        const double line = static_cast<double>(scaled_px(kEntryProgressW, 1));
        pass.rounded_rect_cut(ix0, y0 + b, ix1, iy1, r, r, ix0, iy1 - line,
                              ix0 + t * (ix1 - ix0), iy1, p.cue);
    }
    pass.end();
}
