#include "materials.h"

// The rules are at the declarations (materials.h).

void paint_key(cairo_t* cr, const GuiRect& r, KeyLook look) {
    if (r.w <= 0 || r.h <= 0) return;
    const GuiPalette& p = palette();
    const GuiColor face = look == KeyLook::Lit      ? p.lit_fill
                        : look == KeyLook::LitPress ? p.lit_press_fill
                        : look == KeyLook::Press    ? p.key_press_face
                                                    : p.key_face;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_ink_rect(cr, r, face);
    cairo_restore(cr);
}
