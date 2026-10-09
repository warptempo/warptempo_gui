#include "gui_font.h"

// THE FACE OWNER'S ONE IMPLEMENTATION (gui_font.h), on both devices: the four
// files handed in once through gui_font_install_bundled — the Linux
// executable's compiled-in copy (gui_font_embedded.cpp) or the APK's assets —
// become four FT faces, the live set (gui_font.h's gui_live_face_set, the
// chrome spec's or the live scheme's) picks its four uses among them, and no
// site below the seam learns which device handed the bytes in.
//
// THE FACES (Tahoma and Tahoma Bold, TrueType; FreeSans and FreeSans Bold,
// OpenType CFF — one loader for both formats, FT_New_Memory_Face) are FT
// faces wrapped as
// cairo font faces — the FT-backed
// shape text_shape requires, since it shapes on the scaled font's own FT
// face through hb-ft.
//
// OUTLINES ONLY, NEVER A STRIKE (architect 2026-10-06; gui_font.h's head).
// Tahoma carries bitmap strikes (8–16 ppem, the bold 9–13) — FreeSans
// carries none (no fixed sizes, read 2026-10-09 through FreeType), so both
// roads below are no-ops on its two faces, run on all four alike —
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
// exactly. FREESANS'S CFF MEETS NO AUTOHINTER (measured
// 2026-10-09, FreeType 2.14): the CFF driver declares that it hints lightly,
// so FreeType hands the light target to the face's own PostScript hints
// through its CFF engine (Adobe's), which grid-fits the vertical direction
// alone as the light autohinter does — the load differs from a forced
// autohint's on 47 of the regular's A..z at 300 %, and every x stays
// within 1/64 px of the unhinted outline's at 100, 138 and 300 % (the Nimbus
// faces of 2026-10-06 met the same road; those figures were measured at the
// capture's 9-row cap, before the set took its x-height measure, gui_font.h).
// The face's blue zones snap the "H" and the "x" to whole rows — the
// hinter's rounding of the ink, which no seat reads (the recorded cap, or
// the x-height set's derived one, gui_font_cap_px, places every label).
// The advances stay
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

// THE GLYPHS A USE'S EM AND CAP ARE MEASURED ON (gui_font.h's
// GuiFaceMeasure): each file's outline ink height of each, per em, read once
// at the install (outline_ink_em), in GuiFaceMeasure's order — the "H", the
// "0", the "x". The em of a use is the live set's recorded measure over its
// file's entry for the measure's glyph (gui_face_em_px), and an x-height
// use's cap band the em times its file's "H" (gui_font_cap_px), so the
// install needs no knowledge of which set the chrome will name.
constexpr std::size_t kMeasureCount = 3;
double g_ink_em[kGuiFontFileCount][kMeasureCount] = {};

FT_Library            g_library = nullptr;
cairo_font_options_t* g_options = nullptr;
// The four files' faces, in kGuiFontFiles' order.
OutlineFace           g_outline[kGuiFontFileCount];
// Each file's five lifted marks' glyph ids and lifts — the math signs onto
// its hyphen's axis, the pipe onto its digits' band (gui_sign_axis) — and
// whether the file carries the seven glyphs they are measured on; read once
// at the install for every file.
GuiSignAxis           g_sign_axis[kGuiFontFileCount];
bool                  g_sign_axis_ok[kGuiFontFileCount] = {};

std::size_t face_index(GuiFace face) { return static_cast<std::size_t>(face); }
std::size_t measure_index(GuiFaceMeasure m) {
    return static_cast<std::size_t>(m);
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
    // NO-OP ON FREESANS, which has no strike to hide (the head).
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
// measured for every file whichever sets lift it; the set's flags choose at
// the reader. False when the face lacks a target or a mark.
bool measure_sign_axis(const OutlineFace& f, GuiSignAxis& out) {
    const InkCentre hyphen = outline_ink_centre(f, U'-');
    const InkCentre digit =
        outline_ink_centre(f, gui_measure_glyph(GuiFaceMeasure::Digit));
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
    // EVERY FILE BUILDS (gui_font.h's head), and each file's measure inks
    // and sign axis are read off its outline.
    constexpr GuiFaceMeasure kMeasures[kMeasureCount] = {
        GuiFaceMeasure::Cap, GuiFaceMeasure::Digit, GuiFaceMeasure::XHeight};
    for (std::size_t i = 0; i < kGuiFontFileCount; ++i) {
        build_outline(g_outline[i], files[i], kGuiFontFiles[i]);
        for (const GuiFaceMeasure m : kMeasures)
            g_ink_em[i][measure_index(m)] =
                outline_ink_em(g_outline[i], gui_measure_glyph(m));
        g_sign_axis_ok[i] = measure_sign_axis(g_outline[i], g_sign_axis[i]);
    }
    // THE EMS MATCH THE LIVE SET'S RECORDED METRICS VERTICALLY (gui_font.h),
    // derived per call from these inks (gui_face_em_px), so the probe asks
    // it of EVERY SET (kGuiFaceSets): each use's file must carry its
    // measure's glyph — and, for an x-height measure, the "H" its cap band
    // derives from (gui_font_cap_px) — and in a set that lifts its signs or
    // its pipe the seven its axis is measured on, whichever chrome and scheme
    // the device config later chooses.
    for (const GuiFaceSet* set : kGuiFaceSets)
        for (std::size_t i = 0; i < kGuiFaceCount; ++i) {
            const double* ink = g_ink_em[set->file[i]];
            const GuiFaceMeasure m = set->metrics[i].measure;
            if (ink[measure_index(m)] <= 0.0) ok = false;
            if (m == GuiFaceMeasure::XHeight &&
                ink[measure_index(GuiFaceMeasure::Cap)] <= 0.0)
                ok = false;
            if ((set->sign_lift || set->pipe_lift) &&
                !g_sign_axis_ok[set->file[i]])
                ok = false;
        }
    for (const OutlineFace& f : g_outline)
        if (!outline_ft_backed(f)) ok = false;
    return ok;
}

double gui_face_em_px(GuiFace face) {
    const GuiFaceSet& set = gui_live_face_set();
    const std::size_t i = face_index(face);
    const GuiFaceMetrics& m = set.metrics[i];
    return static_cast<double>(m.height) /
           g_ink_em[set.file[i]][measure_index(m.measure)];
}

double gui_font_cap_px(const GuiFont& f) {
    const GuiFaceMetrics& m = gui_face_metrics(f.face);
    if (m.measure != GuiFaceMeasure::XHeight)
        return m.height * gui_font_scale(f);
    const GuiFaceSet& set = gui_live_face_set();
    return gui_face_em_px(f.face) *
           g_ink_em[set.file[face_index(f.face)]]
                   [measure_index(GuiFaceMeasure::Cap)] *
           gui_font_scale(f);
}

const GuiSignAxis& gui_sign_axis(GuiFace face) {
    static const GuiSignAxis kLevel{};
    const GuiFaceSet& set = gui_live_face_set();
    return set.sign_lift || set.pipe_lift
               ? g_sign_axis[set.file[face_index(face)]]
               : kLevel;
}

cairo_scaled_font_t* gui_outline_scaled_font(const GuiFont& f) {
    // One per face, rebuilt when the scale moves (its two application
    // points, set_gui_scale_percent): a paint asks for it per run. The hb
    // font text_shape shapes with hangs on this scaled font as cairo user
    // data and dies with it (text_shape.cpp's hb_font_of), so it follows this
    // cache's key and rebuild without a second cache. THE LIVE SET IS A KEY
    // (2026-10-09): under the windows chrome it follows the live scheme's
    // face tag, which a pick moves live (gui_live_face_set, gui_font.h).
    struct Cached {
        int                  percent = -1;
        const GuiFaceSet*    set     = nullptr;
        cairo_scaled_font_t* font    = nullptr;
    };
    static Cached cache[kGuiFaceCount];
    Cached& c = cache[face_index(f.face)];
    const GuiFaceSet* set = &gui_live_face_set();
    if (c.percent == f.percent && c.set == set) return c.font;
    if (c.font != nullptr) cairo_scaled_font_destroy(c.font);
    const double em = gui_face_em_px(f.face) * gui_font_scale(f);
    cairo_matrix_t font_matrix;
    cairo_matrix_t ctm;
    cairo_matrix_init_scale(&font_matrix, em, em);
    cairo_matrix_init_identity(&ctm);
    c.font = cairo_scaled_font_create(outline_of(f.face).face, &font_matrix,
                                      &ctm, g_options);
    c.percent = f.percent;
    c.set     = set;
    return c.font;
}

double gui_font_advance_bound_px(const GuiFont& f) {
    return outline_of(f.face).max_advance_em * gui_face_em_px(f.face) *
           gui_font_scale(f);
}
