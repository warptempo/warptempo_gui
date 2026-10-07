#include "gui_font.h"

// THE FACE OWNER'S ONE IMPLEMENTATION (gui_font.h), on both devices: the four
// files handed in once through gui_font_install_bundled — the Linux
// executable's compiled-in copy (gui_font_embedded.cpp) or the APK's assets —
// become four FT faces, the live set (gui_font.h's gui_live_face_set, the
// chrome spec's) picks its three uses among them, and no site below the seam
// learns which device handed the bytes in.
//
// THE FACES (Tahoma, Tahoma Bold, DejaVu Sans and DejaVu Sans Bold, all
// TrueType) are FT faces wrapped as cairo font faces — the FT-backed
// shape text_shape requires, since it shapes on the scaled font's own FT
// face through hb-ft.
//
// OUTLINES ONLY, NEVER A STRIKE (architect 2026-10-06; gui_font.h's head).
// Tahoma carries bitmap strikes (8–16 ppem, the bold 9–13) — DejaVu Sans
// 2.31 carries none (no EBDT / EBLC table in either file, read 2026-10-07),
// so both roads below are no-ops on its two faces, run on all four alike —
// and FreeType
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
// FT_LOAD_NO_SCALE, which implies it.
//
// THE PRODUCT'S HINTER IS SLIGHT (architect 2026-10-02, kept 2026-10-06):
// cairo's SLIGHT is FreeType's light target, which grid-fits the VERTICAL
// direction alone. For Tahoma's TrueType that is the AUTOHINTER in its light
// mode (measured 2026-10-06, FreeType 2.14) — FreeType's TrueType driver
// does not hint lightly, so it hands the light target to the autohinter
// rather than to the bytecode interpreter (the load equals a forced
// autohint's; Wine Tahoma's glyphs carry no instructions anyway, maxp's
// maxSizeOfInstructions 0), and every x equals the unhinted outline's
// exactly. DejaVu's files DO carry bytecode (fpgm, prep, the glyphs'
// programs — squeeze's hinter of 2010 ran it), and the light target leaves
// it unrun the same way: the gnome2 set's ink is the light autohinter's,
// "compromise and approximate with modern HarfBuzz and DejaVu Sans"
// (architect 2026-10-07). The advances stay
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

// THE GLYPH EACH USE'S EM IS MEASURED ON (gui_font.h's head): the "H" for
// the two text faces, the "0" for the digits.
constexpr char32_t kBandGlyph[kGuiFaceCount] = {U'H', U'H', U'0'};
// Each file's outline ink height of each band glyph, per em, read once at the
// install (outline_ink_em): index 0 the "H", 1 the "0". The em of a use is
// the live set's recorded cap over its file's entry (gui_face_em_px), so the
// install needs no knowledge of which set the chrome will name.
double g_ink_em[kGuiFontFileCount][2] = {};

FT_Library            g_library = nullptr;
cairo_font_options_t* g_options = nullptr;
// The four files' faces, in kGuiFontFiles' order.
OutlineFace           g_outline[kGuiFontFileCount];

std::size_t face_index(GuiFace face) { return static_cast<std::size_t>(face); }
std::size_t band_index(GuiFace face) {
    return kBandGlyph[face_index(face)] == U'0' ? 1 : 0;
}

// The face a use is drawn from: the live set's file for it.
const OutlineFace& outline_of(GuiFace face) {
    return g_outline[gui_live_face_set().file[face_index(face)]];
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
    // outline's own scale and no strike is ever selected for a load. A
    // NO-OP ON DEJAVU, which has no strike to hide (the head).
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
    // EVERY FILE BUILDS (gui_font.h's head), and each file's band inks are
    // read off its outline.
    for (std::size_t i = 0; i < kGuiFontFileCount; ++i) {
        build_outline(g_outline[i], files[i], kGuiFontFiles[i]);
        g_ink_em[i][0] = outline_ink_em(g_outline[i], U'H');
        g_ink_em[i][1] = outline_ink_em(g_outline[i], U'0');
    }
    // THE EMS MATCH THE LIVE SET'S RECORDED METRICS VERTICALLY (gui_font.h),
    // derived per call from these inks (gui_face_em_px), so the probe asks
    // it of EVERY SET a chrome spec names: each use's file must carry its
    // band glyph, whichever chrome the device config later chooses.
    for (const ChromeSpec* spec : kGuiChromeSpecs)
        for (std::size_t i = 0; i < kGuiFaceCount; ++i)
            if (g_ink_em[spec->face_set->file[i]]
                        [band_index(static_cast<GuiFace>(i))] <= 0.0)
                ok = false;
    for (const OutlineFace& f : g_outline)
        if (!outline_ft_backed(f)) ok = false;
    return ok;
}

double gui_face_em_px(GuiFace face) {
    const GuiFaceSet& set = gui_live_face_set();
    const std::size_t i = face_index(face);
    return static_cast<double>(set.metrics[i].cap) /
           g_ink_em[set.file[i]][band_index(face)];
}

cairo_scaled_font_t* gui_outline_scaled_font(const GuiFont& f) {
    // One per face, rebuilt when the scale moves (its two application
    // points, set_gui_scale_percent): a paint asks for it per run. The hb
    // font text_shape shapes with hangs on this scaled font as cairo user
    // data and dies with it (text_shape.cpp's hb_font_of), so it follows this
    // cache's key and rebuild without a second cache. THE SET IS NOT A KEY:
    // the chrome, and with it the live set, is chosen once before the first
    // paint (set_live_chrome_spec, gui_main) and never moves.
    struct Cached {
        int                  percent = -1;
        cairo_scaled_font_t* font    = nullptr;
    };
    static Cached cache[kGuiFaceCount];
    Cached& c = cache[face_index(f.face)];
    if (c.percent == f.percent) return c.font;
    if (c.font != nullptr) cairo_scaled_font_destroy(c.font);
    const double em = gui_face_em_px(f.face) * gui_font_scale(f);
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
    return outline_of(f.face).max_advance_em * gui_face_em_px(f.face) *
           gui_font_scale(f);
}
