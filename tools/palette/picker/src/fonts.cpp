#include "fonts.h"

#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <vector>

namespace {

struct Face {
    std::vector<uint8_t> bytes;
    FT_Face ft = nullptr;
    cairo_font_face_t* face = nullptr;
};

FT_Library g_lib = nullptr;
cairo_font_options_t* g_options = nullptr;
Face g_sans, g_mono;

bool build(Face& f, const uint8_t* data, size_t len) {
    if (!data || !len) return false;
    f.bytes.assign(data, data + len);
    if (FT_New_Memory_Face(g_lib, f.bytes.data(), FT_Long(f.bytes.size()), 0, &f.ft) != 0) return false;
    f.face = cairo_ft_font_face_create_for_ft_face(f.ft, 0);
    return cairo_font_face_status(f.face) == CAIRO_STATUS_SUCCESS;
}

} // namespace

bool fonts_install(const uint8_t* sans, size_t sans_len, const uint8_t* mono, size_t mono_len) {
    if (FT_Init_FreeType(&g_lib) != 0) return false;
    g_options = cairo_font_options_create();
    cairo_font_options_set_hint_style(g_options, CAIRO_HINT_STYLE_SLIGHT);
    return build(g_sans, sans, sans_len) && build(g_mono, mono, mono_len);
}

void fonts_select(cairo_t* cr, bool mono, double size_px) {
    cairo_set_font_face(cr, (mono ? g_mono : g_sans).face);
    cairo_set_font_options(cr, g_options);
    cairo_set_font_size(cr, size_px);
}
