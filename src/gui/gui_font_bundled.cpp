#include "gui_font.h"

// THE FACE OWNER'S ONE IMPLEMENTATION (gui_font.h), on both devices: the four
// files handed in once through gui_font_install_bundled — the Linux
// executable's compiled-in copy (gui_font_embedded.cpp) or the APK's assets —
// become four FT faces, the live set (gui_font.h's kGuiLiveFaceSet) picks
// its three uses among them, and no site below the seam learns which device
// handed the bytes in.
//
// THE FACES (Nimbus Sans Regular and Bold, OpenType CFF; Tahoma and Tahoma
// Bold, TrueType) are FT faces wrapped as cairo font faces — the FT-backed
// shape text_shape requires, since it shapes on the scaled font's own FT
// face through hb-ft.
//
// OUTLINES ONLY, NEVER A STRIKE (architect 2026-10-06; gui_font.h's head).
// Tahoma carries bitmap strikes (8–16 ppem, the bold 9–13), and FreeType
// reads a strike on two roads, both closed here: (1) SIZE SELECTION — a
// TrueType face with strikes answers a size request whose ppem ROUNDS to a
// strike's by SELECTING the strike, scaling even the outline at the strike's
// whole ppem (the laptop's 138 % asked Tahoma for 15.19 ppem and got 15.00,
// its small face 10.95 and got 11, measured 2026-10-06 on FreeType 2.14), so
// build_outline hides the strikes from FreeType (the face's fixed-sizes flag
// cleared and its count zeroed, the file's bytes untouched) and every size
// is the outline's own scale; (2) GLYPH LOADING — every load road passes
// FT_LOAD_NO_BITMAP: cairo's, as the cairo face's load flags; hb-ft's, set
// on each hb font (text_shape.cpp); the install's measurement loads take
// FT_LOAD_NO_SCALE, which implies it. Nimbus carries no strikes, so neither
// changes a pixel the win95 set paints.
//
// THE PRODUCT'S HINTER IS SLIGHT (architect 2026-10-02, kept 2026-10-06):
// cairo's SLIGHT is FreeType's light target, which grid-fits the VERTICAL
// direction alone. What runs depends on the outline format (measured
// 2026-10-06, FreeType 2.14): for Nimbus's CFF, the face's own hints through
// FreeType's CFF engine, a light hinter by declaration, every x within 1/64
// px of the unhinted outline; for Tahoma's TrueType, the AUTOHINTER in its
// light mode — FreeType's TrueType driver does not hint lightly, so it hands
// the light target to the autohinter rather than to the bytecode
// interpreter (the load equals a forced autohint's; Wine Tahoma's glyphs
// carry no instructions anyway, maxp's maxSizeOfInstructions 0), and every x
// equals the unhinted outline's exactly. Either way the advances stay
// unhinted (text_shape's come off hb-ft, which loads its own glyphs
// unhinted) and both devices measure one set of widths. The vertical
// metrics are the recorded constants anyway (gui_font.h), so the hinter
// only decides how the ink sits on the pixel rows. ANTIALIASING AND SUBPIXEL
// ORDER STAY AT CAIRO'S DEFAULTS on both devices: GRAY, no subpixel order (a
// tablet ROTATES, so a subpixel order is not a face fact there). HINT
// METRICS stay at cairo's default too, ON for the image surfaces both
// backends paint to.
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

// The four math signs' glyph ids and lifts per face (gui_sign_axis).
GuiSignAxis g_sign_axis[kGuiFaceCount] = {};

FT_Library            g_library = nullptr;
cairo_font_options_t* g_options = nullptr;
// The four files' faces, in kGuiFontFiles' order.
OutlineFace           g_outline[kGuiFontFileCount];

std::size_t face_index(GuiFace face) { return static_cast<std::size_t>(face); }

// The face a use is drawn from: the live set's file for it.
const OutlineFace& outline_of(GuiFace face) {
    return g_outline[kGuiLiveFaceSet.file[face_index(face)]];
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
    // THE STRIKES HIDDEN FROM SIZE SELECTION (the head's road 1): with the
    // flag clear, a size request never matches a strike, so the size is the
    // outline's own scale and no strike is ever selected for a load.
    out.ft->face_flags &= ~static_cast<FT_Long>(FT_FACE_FLAG_FIXED_SIZES);
    out.ft->num_fixed_sizes = 0;
    out.max_advance_em = static_cast<double>(out.ft->max_advance_width) /
                         static_cast<double>(out.ft->units_per_EM);
    // Cairo's loads (the head's road 2).
    out.face = cairo_ft_font_face_create_for_ft_face(out.ft, FT_LOAD_NO_BITMAP);
    if (cairo_font_face_status(out.face) != CAIRO_STATUS_SUCCESS) {
        std::fprintf(stderr, "warptempo_gui: bundled %s unusable\n", what);
        cairo_font_face_destroy(out.face);
        out.face = nullptr;
    }
}

// THE OUTLINE'S INK HEIGHT OF ONE GLYPH, per em: the unscaled, unhinted
// outline's bounding box (FT_LOAD_NO_SCALE, which implies FT_LOAD_NO_HINTING
// and FT_LOAD_NO_BITMAP), so the answer is the design's and no hinter's or
// strike's. 0 when the face lacks the glyph.
double outline_ink_em(const OutlineFace& f, char32_t cp) {
    if (f.ft == nullptr) return 0.0;
    const FT_UInt gid = FT_Get_Char_Index(f.ft, cp);
    if (gid == 0 || FT_Load_Glyph(f.ft, gid, FT_LOAD_NO_SCALE) != 0) return 0.0;
    return static_cast<double>(f.ft->glyph->metrics.height) /
           static_cast<double>(f.ft->units_per_EM);
}

// THE OUTLINE'S INK CENTRE OF ONE GLYPH, in font units up from the baseline,
// off the same unscaled, unhinted bounding box as outline_ink_em; `gid` 0
// when the face lacks the glyph.
struct InkCentre {
    FT_UInt gid    = 0;
    double  centre = 0.0;
};
InkCentre outline_ink_centre(const OutlineFace& f, char32_t cp) {
    if (f.ft == nullptr) return {};
    const FT_UInt gid = FT_Get_Char_Index(f.ft, cp);
    if (gid == 0 || FT_Load_Glyph(f.ft, gid, FT_LOAD_NO_SCALE) != 0) return {};
    const FT_Glyph_Metrics& m = f.ft->glyph->metrics;
    return {gid, static_cast<double>(m.horiBearingY) -
                     static_cast<double>(m.height) / 2.0};
}

// THE SIGN AXIS OF ONE FACE (gui_font.h, gui_sign_axis): each math sign's
// lift is the hyphen's ink centre less its own, per em. False when the face
// lacks the hyphen or a sign.
bool measure_sign_axis(const OutlineFace& f, GuiSignAxis& out) {
    const InkCentre hyphen = outline_ink_centre(f, U'-');
    if (hyphen.gid == 0) return false;
    for (std::size_t i = 0; i < kGuiSignCount; ++i) {
        const InkCentre sign = outline_ink_centre(f, kGuiMathSigns[i]);
        if (sign.gid == 0) return false;
        out.signs[i].glyph   = sign.gid;
        out.signs[i].lift_em = (hyphen.centre - sign.centre) /
                               static_cast<double>(f.ft->units_per_EM);
    }
    return true;
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
    // ALL FOUR FILES BUILD, whichever set is live (gui_font.h's head).
    for (std::size_t i = 0; i < kGuiFontFileCount; ++i)
        build_outline(g_outline[i], files[i], kGuiFontFiles[i]);
    // THE EMS MATCH THE LIVE SET'S RECORDED METRICS VERTICALLY (gui_font.h):
    // the recorded cap over the outline's ink height of the same band — the
    // "H" for the two text faces, the "0" for the digits.
    const char32_t band_glyph[kGuiFaceCount] = {U'H', U'H', U'0'};
    for (std::size_t i = 0; i < kGuiFaceCount; ++i) {
        const double ink = outline_ink_em(outline_of(static_cast<GuiFace>(i)),
                                          band_glyph[i]);
        if (ink <= 0.0) { ok = false; continue; }
        g_face_em[i] =
            static_cast<double>(kGuiFaceMetrics[i].cap) / ink;
        // THE FOUR MATH SIGNS' LIFTS ONTO THE HYPHEN'S AXIS (gui_font.h),
        // where the set lifts them; otherwise every lift stays 0.
        if (kGuiLiveFaceSet.sign_lift &&
            !measure_sign_axis(outline_of(static_cast<GuiFace>(i)),
                               g_sign_axis[i]))
            ok = false;
    }
    for (const OutlineFace& f : g_outline)
        if (!outline_ft_backed(f)) ok = false;
    return ok;
}

double gui_face_em_px(GuiFace face) {
    return g_face_em[face_index(face)];
}

const GuiSignAxis& gui_sign_axis(GuiFace face) {
    return g_sign_axis[face_index(face)];
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
