#include "text_shape.h"

#include <cairo/cairo-ft.h>
#include <hb-ft.h>
#include <hb.h>

namespace text_shape {

namespace {

// HarfBuzz positions arrive in 26.6 fixed point here: hb-ft scales the font
// from the FT face's own size metrics, so every offset and advance is a
// pixel value times 64.
constexpr double k26Dot6 = 64.0;

// RAII over the three handles the shaping pass borrows. Each release is
// unconditional and ordered by declaration: the buffer and the hb font go
// before the face unlock, which is what cairo requires. The body allocates
// (the glyph vector), so no plain call at the end could be trusted to run.
class ScaledFontFace {
public:
    explicit ScaledFontFace(cairo_scaled_font_t* font)
        : font_(font), face_(cairo_ft_scaled_font_lock_face(font)) {}
    ~ScaledFontFace() { cairo_ft_scaled_font_unlock_face(font_); }
    ScaledFontFace(const ScaledFontFace&) = delete;
    ScaledFontFace& operator=(const ScaledFontFace&) = delete;
    FT_Face face() const { return face_; }

private:
    cairo_scaled_font_t* font_;
    FT_Face              face_;
};

class HbFont {
public:
    explicit HbFont(FT_Face face) : font_(hb_ft_font_create(face, nullptr)) {}
    ~HbFont() { hb_font_destroy(font_); }
    HbFont(const HbFont&) = delete;
    HbFont& operator=(const HbFont&) = delete;
    hb_font_t* get() const { return font_; }

private:
    hb_font_t* font_;
};

class HbBuffer {
public:
    HbBuffer() : buffer_(hb_buffer_create()) {}
    ~HbBuffer() { hb_buffer_destroy(buffer_); }
    HbBuffer(const HbBuffer&) = delete;
    HbBuffer& operator=(const HbBuffer&) = delete;
    hb_buffer_t* get() const { return buffer_; }

private:
    hb_buffer_t* buffer_;
};

} // namespace

ShapedRun shape_text_run(cairo_scaled_font_t* font, std::string_view utf8) {
    ShapedRun run;
    if (utf8.empty()) return run;

    // The hb font is built per call, and stays that way deliberately: the
    // build is a face wrap plus a scale read, and a cache would have to be
    // keyed on the scaled font's identity and invalidated with it. If a
    // profile ever shows the build on a hot paint path, that is the moment to
    // decide the cache — not before.
    ScaledFontFace locked(font);
    HbFont         hb_font(locked.face());
    HbBuffer       buffer;

    hb_buffer_add_utf8(buffer.get(), utf8.data(),
                       static_cast<int>(utf8.size()), 0,
                       static_cast<int>(utf8.size()));
    // Direction is ours (LTR horizontal runs only); script and language are
    // guessed from the text, which leaves the already-set direction alone.
    hb_buffer_set_direction(buffer.get(), HB_DIRECTION_LTR);
    hb_buffer_guess_segment_properties(buffer.get());

    hb_shape(hb_font.get(), buffer.get(), nullptr, 0);

    unsigned            count = 0;
    const hb_glyph_info_t*     infos =
        hb_buffer_get_glyph_infos(buffer.get(), &count);
    const hb_glyph_position_t* positions =
        hb_buffer_get_glyph_positions(buffer.get(), &count);

    run.glyphs.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        ShapedGlyph glyph;
        // After shaping, `codepoint` holds the substituted GLYPH ID.
        glyph.glyph_index  = infos[i].codepoint;
        glyph.x_offset_px  = positions[i].x_offset / k26Dot6;
        glyph.y_offset_px  = positions[i].y_offset / k26Dot6;
        glyph.x_advance_px = positions[i].x_advance / k26Dot6;
        run.width_px += glyph.x_advance_px;
        run.glyphs.push_back(glyph);
    }
    return run;
}

void show_shaped_run(cairo_t* cr, const ShapedRun& run, double x, double y) {
    if (run.glyphs.empty()) return;

    std::vector<cairo_glyph_t> glyphs;
    glyphs.reserve(run.glyphs.size());
    double pen_x = x;
    for (const ShapedGlyph& glyph : run.glyphs) {
        cairo_glyph_t placed;
        placed.index = glyph.glyph_index;
        placed.x     = pen_x + glyph.x_offset_px;
        // HarfBuzz y is up-positive, cairo's is down-positive.
        placed.y     = y - glyph.y_offset_px;
        glyphs.push_back(placed);
        pen_x += glyph.x_advance_px;
    }
    cairo_show_glyphs(cr, glyphs.data(), static_cast<int>(glyphs.size()));
}

} // namespace text_shape
