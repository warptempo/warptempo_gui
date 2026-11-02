#include "materials.h"

#include <algorithm>

// The rules are at the declarations (materials.h).

void paint_key(cairo_t* cr, const GuiRect& r, KeyLook look) {
    if (r.w <= 0 || r.h <= 0) return;
    const uint32_t face = look == KeyLook::Lit       ? kLitFill
                        : look == KeyLook::Pressed   ? kPressFace
                                                     : kKeyFace;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_ink_rect(cr, r, hex(face));
    cairo_restore(cr);
}

GuiRect key_in_cell(const GuiRect& cell, bool joined_left, bool joined_right) {
    // Half the gap each side that abuts (kKeyGapPx, device px, unscaled).
    constexpr int half = kKeyGapPx / 2;
    static_assert(kKeyGapPx % 2 == 0, "the gap splits evenly between two keys");
    const int x0 = cell.x + (joined_left ? half : 0);
    const int x1 = cell.x + cell.w - (joined_right ? half : 0);
    return GuiRect{x0, cell.y, std::max(0, x1 - x0), cell.h};
}
