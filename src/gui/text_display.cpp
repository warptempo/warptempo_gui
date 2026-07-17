#include "text_display.h"

namespace text_display {

double draw_line(cairo_t* cr,
                 double x,
                 double baseline_y,
                 const std::string& content,
                 GuiColor color,
                 double font_size) {
    if (content.empty()) return 0.0;
    cairo_save(cr);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    cairo_set_source_rgb(cr, color.r, color.g, color.b);
    cairo_move_to(cr, x, baseline_y);
    cairo_show_text(cr, content.c_str());
    cairo_text_extents_t ext;
    cairo_text_extents(cr, content.c_str(), &ext);
    cairo_restore(cr);
    return ext.x_advance;
}

} // namespace text_display
