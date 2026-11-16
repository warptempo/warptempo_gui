#include "materials.h"

// The rules are at the declarations (materials.h).

// THE KEY'S FACE TONE for a look, the enabled bit and the lit class — the
// one choice both paint_key roads fill with (the head's THE KEY,
// materials.h): the lit key's three faces — its class's color (the accent,
// or the purple for a radio key), darkened under a press, dimmed when the
// key is dead — and the unlit key's two; the enabled bit and the class are
// read for a lit key alone.
static GuiColor key_face_tone(KeyLook look, bool enabled, KeyLitClass cls) {
    const GuiPalette& p = palette();
    const bool pressed = key_look_pressed(look);
    const bool radio   = cls == KeyLitClass::Radio;
    return key_look_lit(look)
               ? (!enabled ? (radio ? p.radio_lit_dim_fill : p.lit_dim_fill)
                  : pressed ? (radio ? p.radio_lit_press_fill
                                     : p.lit_press_fill)
                            : (radio ? p.radio_lit_fill : p.cue))
               : (pressed ? p.key_press_face : p.key_face);
}

void paint_key(cairo_t* cr, const GuiRect& r, KeyLook look, bool enabled,
               KeyLitClass cls) {
    if (r.w <= 0 || r.h <= 0) return;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_ink_rect(cr, r, key_face_tone(look, enabled, cls));
    cairo_restore(cr);
}

void paint_key(cairo_t* cr, const GuiRectF& r, KeyLook look, bool enabled,
               KeyLitClass cls) {
    paint_ink_rect(cr, r, key_face_tone(look, enabled, cls));
}
