#include "materials.h"

#include "cool_edit_paint.h"   // CoverageStampPass, the exact-coverage road

#include <cstddef>

// The rules are at the declarations (materials.h).

void paint_linked_group(cairo_t* cr, int x, int y,
                        std::span<const KeyState> states,
                        std::span<const double> hover,
                        std::span<const double> seam) {
    const int n = static_cast<int>(states.size());
    if (n <= 0) return;
    // A member's hover mix (the declaration's THE HOVER): a resting
    // member's alone, 0 with no span handed.
    const auto hover_of = [&](int i) {
        const std::size_t k = static_cast<std::size_t>(i);
        return k < hover.size() && states[k] == KeyState::Rest
                   ? std::clamp(hover[k], 0.0, 1.0)
                   : 0.0;
    };
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
    //     member's the accent, a resting member's the key face mixed toward
    //     the hover face by its hover.
    const auto rest_face = [&](KeyState s) {
        return s == KeyState::Lit      ? p.cue
             : s == KeyState::Disabled ? p.key_dim_face
             : s == KeyState::LitInert ? p.lit_dim_fill
                                       : p.key_face;
    };
    for (int i = 0; i < n; ++i) {
        const KeyState s = states[static_cast<std::size_t>(i)];
        const double fx0 = box_x(i) + b;
        const double fx1 = i == n - 1 ? box_x(i) + w - b : box_x(i + 1) + b;
        pass.rounded_rect(fx0, y0 + b, fx1, y1 - b, i == 0 ? r : 0.0,
                          i == n - 1 ? r : 0.0,
                          StampTone(rest_face(s), p.key_hover_face,
                                    hover_of(i)));
    }
    // (3) A LIT MEMBER'S LEFT SEAM in the accent, between the border's rows:
    //     whole cells over the left key's face.
    for (int i = 1; i < n; ++i) {
        if (states[static_cast<std::size_t>(i)] != KeyState::Lit) continue;
        pass.rect(box_x(i), y0 + b, box_x(i) + b, y1 - b, p.cue);
    }
    // (4) THE SEAMS in the border tone, each by ITS OWN stored mix as the
    //     caller read it for this run's states (the declaration): the
    //     seam between members i − 1 and i, whole cells between the border's
    //     rows, from the face it wears at rest (the left member's) toward
    //     `well_border`. NO LINE BESIDE A CHECKED MEMBER, lit or lit and
    //     inert, on either side (gtk3 CSS line 292: `:not(:checked)` of the
    //     hovering button and of its sibling; line 296: `button:checked +
    //     button:hover { box-shadow: none }`; `:checked` holds the checked
    //     and disabled key): that seam stays as (2) and (3) left it.
    const auto checked = [&](int i) {
        const KeyState s = states[static_cast<std::size_t>(i)];
        return s == KeyState::Lit || s == KeyState::LitInert;
    };
    for (int i = 1; i < n && static_cast<std::size_t>(i) < seam.size(); ++i) {
        const double m = std::clamp(seam[static_cast<std::size_t>(i)], 0.0, 1.0);
        if (!(m > 0.0) || checked(i - 1) || checked(i)) continue;
        pass.rect(box_x(i), y0 + b, box_x(i) + b, y1 - b,
                  StampTone(rest_face(states[static_cast<std::size_t>(i - 1)]),
                            p.well_border, m));
    }
    pass.end();
}

void paint_word_button(cairo_t* cr, const GuiRect& box, KeyState state,
                       double hover) {
    if (box.w <= 0 || box.h <= 0) return;
    const GuiPalette& p = palette();
    const GuiColor rest = state == KeyState::Lit      ? p.cue
                        : state == KeyState::Disabled ? p.key_dim_face
                        : state == KeyState::LitInert ? p.lit_dim_fill
                                                      : p.key_face;
    // A resting button's face toward the hover face by its hover (the
    // declaration's THE HOVER).
    const StampTone face(rest, p.key_hover_face,
                         state == KeyState::Rest ? std::clamp(hover, 0.0, 1.0)
                                                 : 0.0);
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
