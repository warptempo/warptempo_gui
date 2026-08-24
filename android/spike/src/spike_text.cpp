#include "spike_text.h"

#include <vector>

#include "spike_log.h"

SpikeFont::~SpikeFont() {
    if (scaled_) cairo_scaled_font_destroy(scaled_);
    if (cairo_face_) cairo_font_face_destroy(cairo_face_);
    if (hb_font_) hb_font_destroy(hb_font_);
    if (paint_face_) FT_Done_Face(paint_face_);
    if (shape_face_) FT_Done_Face(shape_face_);
}

bool SpikeFont::init(FT_Library lib, const void* bytes, size_t size, double px, std::string& err) {
    px_ = px;
    const FT_Byte* data = static_cast<const FT_Byte*>(bytes);

    if (FT_New_Memory_Face(lib, data, static_cast<FT_Long>(size), 0, &shape_face_) != 0) {
        err = "FT_New_Memory_Face (shaping) failed";
        return false;
    }
    if (FT_New_Memory_Face(lib, data, static_cast<FT_Long>(size), 0, &paint_face_) != 0) {
        err = "FT_New_Memory_Face (painting) failed";
        return false;
    }
    if (FT_Set_Pixel_Sizes(shape_face_, 0, static_cast<FT_UInt>(px)) != 0) {
        err = "FT_Set_Pixel_Sizes failed";
        return false;
    }

    hb_font_ = hb_ft_font_create_referenced(shape_face_);
    if (!hb_font_) { err = "hb_ft_font_create_referenced failed"; return false; }

    cairo_face_ = cairo_ft_font_face_create_for_ft_face(paint_face_, 0);
    if (cairo_font_face_status(cairo_face_) != CAIRO_STATUS_SUCCESS) {
        err = "cairo_ft_font_face_create_for_ft_face failed";
        return false;
    }

    cairo_matrix_t m;
    cairo_matrix_init_scale(&m, px, px);
    cairo_matrix_t ctm;
    cairo_matrix_init_identity(&ctm);

    cairo_font_options_t* opts = cairo_font_options_create();
    cairo_font_options_set_antialias(opts, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(opts, CAIRO_HINT_STYLE_SLIGHT);
    cairo_font_options_set_hint_metrics(opts, CAIRO_HINT_METRICS_OFF);
    scaled_ = cairo_scaled_font_create(cairo_face_, &m, &ctm, opts);
    cairo_font_options_destroy(opts);

    if (cairo_scaled_font_status(scaled_) != CAIRO_STATUS_SUCCESS) {
        err = "cairo_scaled_font_create failed";
        return false;
    }

    SPIKE_LOGI("font ready: %s %s, %.1f px, %ld glyphs", shape_face_->family_name,
               shape_face_->style_name, px, static_cast<long>(shape_face_->num_glyphs));
    return true;
}

namespace {

// One shaping pass. Positions come back in 26.6 fixed point because the FT_Face
// was sized in pixels.
hb_buffer_t* shape(hb_font_t* font, const char* utf8) {
    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, utf8, -1, 0, -1);
    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
    hb_buffer_set_language(buf, hb_language_from_string("en", -1));
    hb_shape(font, buf, nullptr, 0);
    return buf;
}

}  // namespace

void SpikeFont::draw(cairo_t* cr, double x, double y, const char* utf8) const {
    if (!valid() || !utf8 || !*utf8) return;

    hb_buffer_t* buf = shape(hb_font_, utf8);
    unsigned int n = 0;
    const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
    const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);

    std::vector<cairo_glyph_t> glyphs;
    glyphs.reserve(n);
    double cx = x;
    double cy = y;
    for (unsigned int i = 0; i < n; ++i) {
        cairo_glyph_t g;
        g.index = info[i].codepoint;  // after shaping this is a GLYPH id, not a char
        g.x = cx + pos[i].x_offset / 64.0;
        g.y = cy - pos[i].y_offset / 64.0;
        glyphs.push_back(g);
        cx += pos[i].x_advance / 64.0;
        cy -= pos[i].y_advance / 64.0;
    }
    hb_buffer_destroy(buf);

    cairo_set_scaled_font(cr, scaled_);
    cairo_show_glyphs(cr, glyphs.data(), static_cast<int>(glyphs.size()));
}

double SpikeFont::advance(const char* utf8) const {
    if (!valid() || !utf8 || !*utf8) return 0.0;
    hb_buffer_t* buf = shape(hb_font_, utf8);
    unsigned int n = 0;
    const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
    double w = 0.0;
    for (unsigned int i = 0; i < n; ++i) w += pos[i].x_advance / 64.0;
    hb_buffer_destroy(buf);
    return w;
}
