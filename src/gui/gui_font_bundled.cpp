#include "gui_font.h"

// THE FACE OWNER'S ONE IMPLEMENTATION (gui_font.h), on both devices: the two
// files handed in once through gui_font_install_bundled — the Linux
// executable's compiled-in copy (gui_font_embedded.cpp) or the APK's assets —
// become the two Nimbus Sans faces, and no site below the seam learns which
// device handed the bytes in.
//
// THE FACES (Nimbus Sans Regular and Bold — OpenType CFF, which FreeType
// opens and cairo-ft wraps exactly as it does a TrueType face) are FT faces
// wrapped as cairo font faces — the FT-backed shape text_shape requires,
// since it shapes on the scaled font's own FT face through hb-ft. THE
// PRODUCT'S HINTER IS SLIGHT (architect 2026-10-02, kept with Nimbus
// 2026-10-06): FreeType's LIGHT mode, which for a CFF face runs the face's
// own hints through FreeType's CFF engine — a light hinter by declaration,
// so the face is not handed to the autohinter as a TrueType face would be —
// and grid-fits the VERTICAL direction alone, so the advances stay unhinted
// (text_shape's come off hb-ft, which loads its own glyphs unhinted) and
// both devices measure one set of widths. The vertical metrics are the
// recorded constants anyway (gui_font.h), so the hinter only decides how the
// ink sits on the baseline. ANTIALIASING AND SUBPIXEL ORDER STAY AT CAIRO'S
// DEFAULTS on both devices: GRAY, no subpixel order (a tablet ROTATES, so a
// subpixel order is not a face fact there). HINT METRICS stay at cairo's
// default too, ON for the image surfaces both backends paint to.
//
// LIFETIME: the library, the faces and the font options are created once
// and never destroyed — the process's exit reclaims them, and there is no
// second install. The byte buffers are COPIES this file owns, because
// FT_New_Memory_Face does not copy and the face reads from them for as long
// as it lives.

#include <cairo/cairo-ft.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdio>
#include <vector>

namespace {

// One face's whole state: the bytes FreeType reads from, the FT face
// over them, the cairo face the painters get, and its widest advance in em.
struct OutlineFace {
    std::vector<uint8_t> bytes;
    FT_Face              ft   = nullptr;
    cairo_font_face_t*   face = nullptr;
    double               max_advance_em = 0.0;
};

// The em per face, in Windows px (gui_face_em_px).
double g_face_em[kGuiFaceCount] = {};

FT_Library            g_library = nullptr;
cairo_font_options_t* g_options = nullptr;
OutlineFace           g_outline_regular;
OutlineFace           g_outline_bold;

std::size_t face_index(GuiFace face) { return static_cast<std::size_t>(face); }

const OutlineFace& outline_of(GuiFace face) {
    return face == GuiFace::Bold ? g_outline_bold : g_outline_regular;
}

// Build one face from a copy of `bytes`. A failure leaves `out`
// empty, which the install's probe reads as not installed.
void build_outline(OutlineFace& out, const GuiFontBytes& bytes,
                   const char* what) {
    if (bytes.data == nullptr || bytes.len == 0) return;
    out.bytes.assign(bytes.data, bytes.data + bytes.len);
    if (FT_New_Memory_Face(g_library, out.bytes.data(),
                           static_cast<FT_Long>(out.bytes.size()), 0,
                           &out.ft) != 0) {
        std::fprintf(stderr, "warptempo_gui: bundled %s failed to load\n",
                     what);
        out.bytes.clear();
        out.ft = nullptr;
        return;
    }
    out.max_advance_em = static_cast<double>(out.ft->max_advance_width) /
                         static_cast<double>(out.ft->units_per_EM);
    out.face = cairo_ft_font_face_create_for_ft_face(out.ft, 0);
    if (cairo_font_face_status(out.face) != CAIRO_STATUS_SUCCESS) {
        std::fprintf(stderr, "warptempo_gui: bundled %s unusable\n", what);
        cairo_font_face_destroy(out.face);
        out.face = nullptr;
    }
}

// THE OUTLINE'S INK HEIGHT OF ONE GLYPH, per em: the unscaled, unhinted
// outline's bounding box (FT_LOAD_NO_SCALE), so the answer is the design's
// and no hinter's. 0 when the face lacks the glyph.
double outline_ink_em(const OutlineFace& f, char32_t cp) {
    if (f.ft == nullptr) return 0.0;
    const FT_UInt gid = FT_Get_Char_Index(f.ft, cp);
    if (gid == 0 || FT_Load_Glyph(f.ft, gid, FT_LOAD_NO_SCALE) != 0) return 0.0;
    return static_cast<double>(f.ft->glyph->metrics.height) /
           static_cast<double>(f.ft->units_per_EM);
}

bool outline_ft_backed(const OutlineFace& f) {
    return f.face != nullptr &&
           cairo_font_face_get_type(f.face) == CAIRO_FONT_TYPE_FT;
}

} // namespace

bool gui_font_install_bundled(const GuiFontBytes (&files)[kGuiFontFileCount]) {
    // Once per process (see LIFETIME above).
    if (g_library != nullptr) return false;
    if (FT_Init_FreeType(&g_library) != 0) {
        std::fprintf(stderr, "warptempo_gui: FreeType failed to initialize\n");
        g_library = nullptr;
        return false;
    }
    g_options = cairo_font_options_create();
    cairo_font_options_set_hint_style(g_options, CAIRO_HINT_STYLE_SLIGHT);
    bool ok = true;
    build_outline(g_outline_regular, files[0], kGuiFontFiles[0]);
    build_outline(g_outline_bold, files[1], kGuiFontFiles[1]);
    // THE EMS MATCH THE RECORDED METRICS VERTICALLY (gui_font.h): the
    // recorded cap over the outline's ink height of the same band — the "H"
    // for the two text faces, the "0" for the digits.
    const char32_t band_glyph[kGuiFaceCount] = {U'H', U'H', U'0'};
    for (std::size_t i = 0; i < kGuiFaceCount; ++i) {
        const double ink = outline_ink_em(outline_of(static_cast<GuiFace>(i)),
                                          band_glyph[i]);
        if (ink <= 0.0) { ok = false; continue; }
        g_face_em[i] =
            static_cast<double>(kGuiFaceMetrics[i].cap) / ink;
    }
    return ok && outline_ft_backed(g_outline_regular) &&
           outline_ft_backed(g_outline_bold);
}

double gui_face_em_px(GuiFace face) {
    return g_face_em[face_index(face)];
}

cairo_scaled_font_t* gui_outline_scaled_font(const GuiFont& f) {
    // One per face, rebuilt when the scale moves (its two application
    // points, set_gui_scale_percent): a paint asks for it per run.
    struct Cached {
        int                  percent = -1;
        cairo_scaled_font_t* font    = nullptr;
    };
    static Cached cache[kGuiFaceCount];
    Cached& c = cache[face_index(f.face)];
    if (c.percent == f.percent) return c.font;
    if (c.font != nullptr) cairo_scaled_font_destroy(c.font);
    const double em = g_face_em[face_index(f.face)] * gui_font_scale(f);
    cairo_matrix_t font_matrix;
    cairo_matrix_t ctm;
    cairo_matrix_init_scale(&font_matrix, em, em);
    cairo_matrix_init_identity(&ctm);
    c.font = cairo_scaled_font_create(outline_of(f.face).face, &font_matrix,
                                      &ctm, g_options);
    c.percent = f.percent;
    return c.font;
}

double gui_font_advance_bound_px(const GuiFont& f) {
    return outline_of(f.face).max_advance_em *
           g_face_em[face_index(f.face)] * gui_font_scale(f);
}
