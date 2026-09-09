#include "gui_font.h"

// THE BUNDLED FACE OWNER (gui_font.h): Android has no fontconfig, so the two
// families resolve to two font FILES the backend ships in the APK and hands in
// once through gui_font_install_bundled. The faces are built with FreeType and
// wrapped as cairo font faces, which is the same FT-backed shape the Linux toy
// path produces — text_shape's precondition (an FT-backed scaled font) holds
// unchanged, and no site below the seam learns which backend answered.
//
// WHAT THIS BACKEND REPRODUCES IS FONTCONFIG'S WHOLE ANSWER — THE FACE AND ITS
// HINT STYLE. The Linux answer for "sans" is Liberation Sans under
// `hintstyle: 1`, i.e. SLIGHT: FreeType's light autohinter, which grid-fits
// the vertical direction only; "monospace" is Liberation Mono under the same.
// The APK's two assets are those very files, byte for byte. But a face alone
// is not the answer, because a HINTER grid-fits the outline before anything
// measures it: the same bytes at the same size report different whole-pixel
// ink under a different hinter, and cairo's own default for a face built here
// is the font's NATIVE TrueType bytecode. One portable GUI wants ONE set of
// text rows on both backends, so the install builds a font-options object
// carrying SLIGHT and every select puts it on the context beside the face.
//
// WHY IT SHOWS: the chrome baseline centres the face's CAP BAND
// (redesign_baseline, paint_handler.cpp), so a seat reads an INK extent, and
// the two hinters disagree about the sans cap at three of the product's four
// sans sizes. Measured on the shipped bytes through this very road (an FT face
// wrapped by cairo_ft_font_face_create_for_ft_face) with hint metrics on:
//
//     face / size            cap unhinted   native   SLIGHT   ascent/descent
//     sans 16px    (12pt @100%)    11.01       12       12         15 / 4
//     sans 36px    (12pt @225%)    24.77       25       26         33 / 8
//     sans 13.33px (10pt @100%)     9.17       10        9         13 / 3
//     sans 30px    (10pt @225%)    20.64       21       20         28 / 7
//     mono 14.67px (11pt @100%)     9.66       10       10         13 / 5
//     mono 33px    (11pt @225%)    21.74       22       22        28 / 10
//
// Ascent and descent are the SAME under both hinters at every size, which is
// why only the cap-centred seats ever forked. NO WIDTH MOVES EITHER, twice
// over: the light autohinter grid-fits the VERTICAL direction alone, and
// text_shape's advances come off hb-ft, which loads its own glyphs unhinted. Under SLIGHT all six rows are
// the laptop's numbers, so every solver answer — box seats and line seats
// alike — is identical on both backends: "File" sits 21 rows above its cap
// band and 21 below in the tablet's 68 pill exactly as it sits 9/9 in the
// laptop's 30, where the native hinter's 25-row cap had seated it 21/22.
//
// NOT REPRODUCED, and deliberately: fontconfig's `rgba`. The laptop paints
// subpixel RGB antialiasing; a tablet ROTATES, so a subpixel order is not a
// face fact there and the bundled road leaves antialias and subpixel order at
// cairo's defaults — the tablet keeps GRAY antialiasing. That is a per-pixel
// coverage difference and not a metric one. HINT METRICS also stay at cairo's
// default, which is ON for the image surfaces both backends paint to, and that
// is what keeps every extent in the table above a whole pixel.
//
// THIS FILE IS NOT IN THE LINUX TARGET; it compiles only into the Android
// library, beside platform_android.cpp.
//
// LIFETIME: the library, the two FT faces, the two cairo faces and the font
// options are created once and never destroyed — the process's exit reclaims
// them, and there is no second install. The byte buffers are COPIES this file
// owns, because FT_New_Memory_Face does not copy and the face reads from them
// for as long as it lives, while the caller's asset mapping is its own to
// release.

#include <cairo/cairo-ft.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// One face's whole state: the bytes FreeType reads from, the FT face over them
// and the cairo face the painters get. Empty until the install, which is the
// only writer.
struct BundledFace {
    std::vector<uint8_t> bytes;
    FT_Face              ft   = nullptr;
    cairo_font_face_t*   face = nullptr;
};

FT_Library            g_library = nullptr;
cairo_font_options_t* g_options = nullptr;
BundledFace           g_sans;
BundledFace           g_mono;

// Build one face from a copy of `data`. A failure leaves `out` empty, which
// gui_select_font_face reads as "nothing installed" and answers by leaving the
// context's own face alone — cairo's default, which paints something rather
// than nothing. There is no error arm beyond the log: an APK that shipped a
// broken asset is a build fault, not a runtime state to recover from.
void build_face(BundledFace& out, const uint8_t* data, size_t len,
                const char* what) {
    if (data == nullptr || len == 0) return;
    out.bytes.assign(data, data + len);
    if (FT_New_Memory_Face(g_library, out.bytes.data(),
                           static_cast<FT_Long>(out.bytes.size()), 0,
                           &out.ft) != 0) {
        std::fprintf(stderr, "warptempo_gui: bundled %s face failed to load\n",
                     what);
        out.bytes.clear();
        out.ft = nullptr;
        return;
    }
    out.face = cairo_ft_font_face_create_for_ft_face(out.ft, 0);
    if (cairo_font_face_status(out.face) != CAIRO_STATUS_SUCCESS) {
        std::fprintf(stderr, "warptempo_gui: bundled %s face unusable\n", what);
        cairo_font_face_destroy(out.face);
        out.face = nullptr;
    }
}

} // namespace

void gui_font_install_bundled(const uint8_t* sans, size_t sans_len,
                              const uint8_t* mono, size_t mono_len) {
    if (g_library != nullptr) return;   // once per process; see LIFETIME above
    if (FT_Init_FreeType(&g_library) != 0) {
        std::fprintf(stderr, "warptempo_gui: FreeType failed to initialize\n");
        g_library = nullptr;
        return;
    }
    // FONTCONFIG'S HINT STYLE, and nothing else set: the head comment owns the
    // reasoning. The options are built beside the faces so that a face and the
    // hinter that measured this file's table can never be installed apart.
    g_options = cairo_font_options_create();
    cairo_font_options_set_hint_style(g_options, CAIRO_HINT_STYLE_SLIGHT);
    build_face(g_sans, sans, sans_len, "sans");
    build_face(g_mono, mono, mono_len, "monospace");
}

void gui_select_font_face(cairo_t* cr, GuiFontFamily family) {
    const BundledFace& f = (family == GuiFontFamily::Mono) ? g_mono : g_sans;
    if (f.face == nullptr) return;
    cairo_set_font_face(cr, f.face);
    // The hint style rides with the face at every select, which is why no site
    // above knows about it: the painters, the measures that borrow a scratch
    // context (mono_line_height_px, clock_cell_metrics) and the Android
    // install probe all arrive here before they size, shape, measure or paint.
    cairo_set_font_options(cr, g_options);
}
