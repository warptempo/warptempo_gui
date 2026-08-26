#include "gui_font.h"

// THE LINUX FACE OWNER (gui_font.h): the family names go straight into cairo's
// toy font API, whose resolution on this platform is fontconfig's — "sans" and
// "monospace" are the generic aliases the system configures, and the product
// pins neither a family file nor a fontconfig rule of its own. This is the
// product's ONE cairo_select_font_face call.
//
// gui_font_install_bundled has no definition here: there is nothing to install
// when fontconfig answers, and no Linux caller exists.

void gui_select_font_face(cairo_t* cr, GuiFontFamily family) {
    cairo_select_font_face(cr,
                           family == GuiFontFamily::Mono ? "monospace" : "sans",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
}
