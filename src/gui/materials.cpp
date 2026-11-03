#include "materials.h"

// The rules are at the declarations (materials.h).

void paint_key(cairo_t* cr, const GuiRect& r, KeyLook look) {
    if (r.w <= 0 || r.h <= 0) return;
    const uint32_t face = look == KeyLook::LitPressed ? kLitPressFill
                        : look == KeyLook::Lit        ? kLitFill
                        : look == KeyLook::Pressed    ? kPressFace
                                                      : kKeyFace;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_ink_rect(cr, r, hex(face));
    cairo_restore(cr);
}
