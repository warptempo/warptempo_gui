#include "materials.h"

// The rules are at the declarations (materials.h).

void paint_key(cairo_t* cr, const GuiRect& r, KeyLook look, bool enabled) {
    if (r.w <= 0 || r.h <= 0) return;
    const GuiPalette& p = palette();
    // The lit key's three faces — the accent, darkened under a press,
    // dimmed when the key is dead — and the unlit key's two (the head's THE
    // KEY); the enabled bit is read for a lit key alone.
    const bool pressed = key_look_pressed(look);
    const GuiColor face =
        key_look_lit(look) ? (!enabled ? p.lit_dim_fill
                              : pressed ? p.lit_press_fill
                                        : p.cue)
                           : (pressed ? p.key_press_face : p.key_face);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_ink_rect(cr, r, face);
    cairo_restore(cr);
}
