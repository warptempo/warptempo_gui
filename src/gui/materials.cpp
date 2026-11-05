#include "materials.h"

// The rules are at the declarations (materials.h).

void paint_key(cairo_t* cr, const GuiRect& r, KeyLook look, KeyLitClass cls) {
    if (r.w <= 0 || r.h <= 0) return;
    const GuiPalette& p = palette();
    // The two lit looks take the class's pair (the head's THE DOORS WEAR
    // LAVENDER); Rest and Press are one face for every class.
    const bool door = cls == KeyLitClass::Door;
    const GuiColor face =
        look == KeyLook::Lit      ? (door ? p.door_lit_fill : p.lit_fill)
      : look == KeyLook::LitPress ? (door ? p.door_lit_press_fill
                                          : p.lit_press_fill)
      : look == KeyLook::Press    ? p.key_press_face
                                  : p.key_face;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_ink_rect(cr, r, face);
    cairo_restore(cr);
}
