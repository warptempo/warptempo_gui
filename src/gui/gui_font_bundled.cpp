#include "gui_font.h"

// THE FACE OWNER'S ONE IMPLEMENTATION (gui_font.h), on both devices: two
// files handed in once through gui_font_install_bundled — the Linux
// executable's compiled-in copy (gui_font_embedded.cpp) or the APK's assets —
// become two FT faces, the pixel table (gui_font.h's kGuiFaceSizes) picks
// its five uses among them, and no site below the seam learns
// which device handed the bytes in.
//
// THE FACES (Liberation Sans and its Bold, TrueType, FT_New_Memory_Face) are
// FT faces wrapped as
// cairo font faces — the FT-backed
// shape text_shape requires, since it shapes on the scaled font's own FT
// face through hb-ft.
//
// OUTLINES ONLY, NEVER A STRIKE (architect 2026-10-06; gui_font.h's head).
// The roads below run on every file, so a strike in any file (Tahoma, the
// product's first face, carried them at 8–16 ppem), Liberation Sans's
// included, is closed off — and FreeType reads a strike on two roads, both closed here: (1) SIZE SELECTION — a
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
// direction alone. For Liberation Sans's TrueType that is the AUTOHINTER in
// its light mode (measured 2026-10-06, FreeType 2.14) — FreeType's TrueType
// driver does not hint lightly, so it hands the light target to the
// autohinter rather than to the bytecode interpreter (the load equals a
// forced autohint's; the face's instructions are never run on this road),
// and every x equals the unhinted outline's exactly. The face's blue zones
// snap the "H" and the "x" to whole rows — the hinter's rounding of the ink,
// which no seat reads (the unhinted "H" per em, gui_font_cap_px, places
// every label). The advances stay
// unhinted (text_shape's come off hb-ft, which loads its own glyphs
// unhinted) and both devices measure one set of widths. The vertical
// metrics are the file's own hhea per em, read at the install and rounded
// at the size (gui_font.h, 2026-10-23), so the hinter only decides how the
// ink sits on the pixel rows. ANTIALIASING AND SUBPIXEL
// ORDER STAY AT CAIRO'S DEFAULTS on both devices: GRAY, no subpixel order (a
// tablet ROTATES, so a subpixel order is not a face fact there). HINT
// METRICS stay at cairo's default too, ON for the image surfaces both
// backends paint to.
//
// LIFETIME: the library, the faces and the font options are created once
// and never destroyed — the process's exit reclaims them, and there is no
// second install. On Android the process ends with the activity
// (MainActivity.onDestroy; the rule at platform_android.cpp's android_main
// tail, 2026-10-07), so this once-per-process rule never meets a second
// android_main. The byte buffers are COPIES this file owns, because
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

// EACH FILE'S OWN METRICS (gui_font.h's head, 2026-10-23), per em, read
// once at the install: the "H" ink height the cap band is measured on, the
// "l" ink height the ascender line is measured on (gui_font_ascender_ink_px),
// and the face's ascender, descender and line height (FreeType's, the file's
// hhea) — the vertical metrics every seat reads.
struct FileMetrics {
    double cap_em     = 0.0;
    double l_ink_em   = 0.0;
    double ascent_em  = 0.0;
    double descent_em = 0.0;
    double line_em    = 0.0;
};
FileMetrics g_metrics[kGuiFontFileCount];

FT_Library            g_library = nullptr;
cairo_font_options_t* g_options = nullptr;
// The two files' faces, in kGuiFontFiles' order.
OutlineFace           g_outline[kGuiFontFileCount];
// Each file's five lifted marks' glyph ids and lifts — the math signs onto
// its hyphen's axis, the pipe onto its digits' band (gui_sign_axis) — and
// whether the file carries the seven glyphs they are measured on; read once
// at the install for every file.
GuiSignAxis           g_sign_axis[kGuiFontFileCount];
bool                  g_sign_axis_ok[kGuiFontFileCount] = {};

std::size_t face_index(GuiFace face) { return static_cast<std::size_t>(face); }
std::size_t file_of(GuiFace face) { return kGuiFaceSizes[face_index(face)].file; }

// The face a use is drawn from: the table's file for it.
const OutlineFace& outline_of(GuiFace face) {
    return g_outline[file_of(face)];
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
    // NO-OP ON A FILE WITH NO STRIKE, as Liberation Sans's (the head).
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

// THE SIGN AXIS OF ONE FILE (gui_font.h, kGuiLiftedSigns, gui_sign_axis):
// each mark's lift is its target's ink centre less its own, per em — the
// hyphen's for the four math signs, the "0"'s (the digit measure's glyph,
// the digits' band) for the pipe (2026-10-09 ~21:20). Every mark is
// measured for every file; the set's flags choose at the reader. False when the face lacks a target or a mark.
bool measure_sign_axis(const OutlineFace& f, GuiSignAxis& out) {
    const InkCentre hyphen = outline_ink_centre(f, U'-');
    const InkCentre digit =
        outline_ink_centre(f, U'0');
    if (hyphen.gid == 0 || digit.gid == 0) return false;
    for (std::size_t i = 0; i < kGuiSignCount; ++i) {
        const GuiLiftedSign& mark = kGuiLiftedSigns[i];
        const InkCentre sign = outline_ink_centre(f, mark.codepoint);
        if (sign.gid == 0) return false;
        const double target = mark.target == GuiSignTarget::HyphenAxis
                                  ? hyphen.centre
                                  : digit.centre;
        out.signs[i].glyph   = sign.gid;
        out.signs[i].lift_em = (target - sign.centre) /
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
    // EVERY FILE BUILDS (gui_font.h's head), and each file's "H" ink, its
    // own vertical metrics (FreeType's ascender, descender and height, the
    // file's hhea, per em) and its sign axis are read off it; every file
    // must carry the "H" and the seven glyphs its axis is measured on.
    for (std::size_t i = 0; i < kGuiFontFileCount; ++i) {
        build_outline(g_outline[i], files[i], kGuiFontFiles[i]);
        FileMetrics& m = g_metrics[i];
        m.cap_em   = outline_ink_em(g_outline[i], U'H');
        m.l_ink_em = outline_ink_em(g_outline[i], U'l');
        if (const FT_Face ft = g_outline[i].ft) {
            const double upem = static_cast<double>(ft->units_per_EM);
            m.ascent_em  = static_cast<double>(ft->ascender) / upem;
            m.descent_em = -static_cast<double>(ft->descender) / upem;
            m.line_em    = static_cast<double>(ft->height) / upem;
        }
        if (m.cap_em <= 0.0 || m.l_ink_em <= 0.0 || m.ascent_em <= 0.0)
            ok = false;
        g_sign_axis_ok[i] = measure_sign_axis(g_outline[i], g_sign_axis[i]);
        if (!g_sign_axis_ok[i]) ok = false;
    }
    for (const OutlineFace& f : g_outline)
        if (!outline_ft_backed(f)) ok = false;
    return ok;
}

double gui_font_ascent_px(const GuiFont& f) {
    return std::nearbyint(g_metrics[file_of(f.face)].ascent_em * gui_font_em_px(f));
}
double gui_font_descent_px(const GuiFont& f) {
    return std::nearbyint(g_metrics[file_of(f.face)].descent_em * gui_font_em_px(f));
}
double gui_font_line_px(const GuiFont& f) {
    return std::nearbyint(g_metrics[file_of(f.face)].line_em * gui_font_em_px(f));
}

double gui_font_cap_px(const GuiFont& f) {
    return g_metrics[file_of(f.face)].cap_em * gui_font_em_px(f);
}

double gui_font_ascender_ink_px(const GuiFont& f) {
    return g_metrics[file_of(f.face)].l_ink_em * gui_font_em_px(f);
}

const GuiSignAxis& gui_sign_axis(GuiFace face) {
    return g_sign_axis[file_of(face)];
}

cairo_scaled_font_t* gui_outline_scaled_font(const GuiFont& f) {
    // One per face, rebuilt when its EM moves — the scale (its two
    // application points, set_gui_scale_percent) or the Program face's live
    // size (the `cue_text` key, gui_font.h's GuiFont): a paint asks for it
    // per run. The hb font text_shape shapes with hangs on this scaled font
    // as cairo user data and dies with it (text_shape.cpp's hb_font_of), so
    // it follows this cache's key and rebuild without a second cache.
    struct Cached {
        double               em   = -1.0;
        cairo_scaled_font_t* font = nullptr;
    };
    static Cached cache[kGuiFaceCount];
    Cached& c = cache[face_index(f.face)];
    const double em = gui_font_em_px(f);
    if (c.em == em) return c.font;
    if (c.font != nullptr) cairo_scaled_font_destroy(c.font);
    cairo_matrix_t font_matrix;
    cairo_matrix_t ctm;
    cairo_matrix_init_scale(&font_matrix, em, em);
    cairo_matrix_init_identity(&ctm);
    c.font = cairo_scaled_font_create(outline_of(f.face).face, &font_matrix,
                                      &ctm, g_options);
    c.em = em;
    return c.font;
}

double gui_font_advance_bound_px(const GuiFont& f) {
    return outline_of(f.face).max_advance_em * gui_font_em_px(f);
}