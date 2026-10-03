#include "gui_font.h"

// THE FACE OWNER'S ONE IMPLEMENTATION (gui_font.h), on both devices: the two
// families resolve to the product's own two font FILES, Roboto and Roboto Mono
// from the repository's `fonts/`, handed in once through
// gui_font_install_bundled — the Linux executable's compiled-in copy
// (gui_font_embedded.cpp) or the APK's two assets. The faces are built with
// FreeType and wrapped as cairo font faces, which is the FT-backed shape
// text_shape requires (an FT-backed scaled font), and no site below the seam
// learns which device handed the bytes in.
//
// THE ANSWER IS THE PRODUCT'S OWN — THE FACE AND ITS HINT STYLE (architect
// 2026-10-02: "the app becomes its own thing"). Until that day the laptop's
// faces were fontconfig's "sans" and "monospace" (Liberation under
// `hintstyle: 1`) and this file reproduced that answer on the tablet; now
// nothing is asked of fontconfig and this file IS the answer. A face alone is
// not an answer, because a HINTER grid-fits the outline before anything
// measures it: the same bytes at the same size report different whole-pixel
// ink under a different hinter, and cairo's own default for a face built here
// is the font's NATIVE TrueType bytecode. THE PRODUCT'S HINTER IS SLIGHT —
// FreeType's light autohinter. It was first chosen to match fontconfig's
// answer and it STAYS ON ITS OWN MERITS: it grid-fits the VERTICAL direction
// alone, so the advances stay unhinted (text_shape's come off hb-ft, which
// loads its own glyphs unhinted), and both devices measure one set of rows.
// The install builds a font-options object carrying SLIGHT and every select
// puts it on the context beside the face.
//
// WHY IT SHOWS: the chrome baseline centres the face's CAP BAND
// (redesign_baseline, paint_handler.cpp), so a seat reads an INK extent, and
// the two hinters disagree about the sans cap at the tablet's two sans sizes.
// Measured on the shipped bytes through this very road (an FT face wrapped by
// cairo_ft_font_face_create_for_ft_face, on an image surface, hint metrics at
// its default) — Roboto, 2026-10-02, the tablet at 200 % since 2026-09-29:
//
//     face / size            cap unhinted   native   SLIGHT   ascent/descent
//     sans 16px    (12pt @100%)    11.38       12       12         15 / 4
//     sans 32px    (12pt @200%)    22.75       23       22         30 / 8
//     sans 13.33px (10pt @100%)     9.48        9        9         13 / 4
//     sans 26.67px (10pt @200%)    18.96       19       18         25 / 7
//     mono 14.67px (11pt @100%)    10.43       11       11         16 / 4
//     mono 29.33px (11pt @200%)    20.85       21       21         31 / 8
//
// THAT TABLE IS THE REFERENCE THE HINTER WAS CHOSEN ON, at the faces of its
// day (12 / 10 / 11 pt of the laptop-pixel unit). The product's faces are the
// Windows pixel's since 2026-10-02 (render.h: 13 / 10 / 12 Windows px, the
// tooltip hint 11) and measure, under SLIGHT through the same road, cap and
// ascent / descent:
//
//     sans  17.94px (13 @138%)   cap 13   17 / 5      35.75px (13 @275%)   cap 25   34 / 9
//     sans  13.8px  (10 @138%)   cap 11   13 / 4      27.5px  (10 @275%)   cap 20   26 / 7
//     mono  16.56px (12 @138%)   cap 12   18 / 5      33px    (12 @275%)   cap 24   35 / 9
//
// (the native hinter's caps at these sizes are unmeasured: the choice of
// SLIGHT rests on the reasons above, not on a size).
//
// Ascent and descent are the SAME under both hinters at every size, which is
// why only the cap-centred seats ever fork. NO WIDTH MOVES EITHER, for the
// two reasons above. Under SLIGHT the tablet's seats kept the laptop's
// symmetry at the reference sizes: "File" sat 19 rows above its cap band and
// 19 below in the tablet's 60-row menu lane, as it sat 9/9 in the laptop's
// 30; the native hinter's 23-row cap would have seated it 18/19 there.
//
// ANTIALIASING AND SUBPIXEL ORDER STAY AT CAIRO'S DEFAULTS, on both devices:
// GRAY antialiasing, no subpixel order. A tablet ROTATES, so a subpixel order
// is not a face fact there, and the laptop takes the same coverage now that
// it takes the same road (fontconfig's `rgba` had painted it subpixel RGB
// until 2026-10-02) — a per-pixel coverage difference and not a metric one.
// HINT METRICS also stay at cairo's default, which is ON for the image
// surfaces both backends paint to, and that is what keeps every extent in the
// table above a whole pixel.
//
// A RETUNE IS A FILE SWAP PLUS THIS TABLE RE-MEASURED (fonts/README.md): the
// seats and the comments that quote these rows (redesign_baseline's table,
// the ruler lane's arithmetic, the clock's) re-derive from it.
//
// LIFETIME: the library, the two FT faces, the two cairo faces and the font
// options are created once and never destroyed — the process's exit reclaims
// them, and there is no second install. The byte buffers are COPIES this file
// owns, because FT_New_Memory_Face does not copy and the face reads from them
// for as long as it lives, while the caller's bytes are its own to release.

#include <cairo/cairo-ft.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdio>
#include <cstring>
#include <initializer_list>
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
// context's own face alone — and the install's probe below then answers
// false, which each caller dies on. The log names which face failed; there is
// no recovery arm: the bytes are the repository's own files, so a face that
// fails to build is a build fault, not a runtime state.
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

// THE INSTALL OBSERVED (gui_font.h): does selecting each family actually put
// an FT-BACKED face on a context? Asked through gui_select_font_face on a
// scratch context — the very road every painter takes — rather than read off
// this file's own state, so the answer is the one text_shape will meet.
bool faces_select_ft_backed() {
    cairo_surface_t* probe_surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* probe = cairo_create(probe_surface);
    bool ft_backed = true;
    for (GuiFontFamily family : {GuiFontFamily::Sans, GuiFontFamily::Mono}) {
        gui_select_font_face(probe, family);
        if (cairo_font_face_get_type(cairo_get_font_face(probe)) !=
            CAIRO_FONT_TYPE_FT) {
            ft_backed = false;
        }
    }
    cairo_destroy(probe);
    cairo_surface_destroy(probe_surface);
    return ft_backed;
}

} // namespace

bool gui_font_install_bundled(const uint8_t* sans, size_t sans_len,
                              const uint8_t* mono, size_t mono_len) {
    // Once per process (see LIFETIME above): a second call builds nothing and
    // reports the standing install.
    if (g_library != nullptr) return faces_select_ft_backed();
    if (FT_Init_FreeType(&g_library) != 0) {
        std::fprintf(stderr, "warptempo_gui: FreeType failed to initialize\n");
        g_library = nullptr;
        return false;
    }
    // THE PRODUCT'S HINT STYLE, and nothing else set: the head comment owns
    // the reasoning. The options are built beside the faces so that a face
    // and the hinter that measured this file's table can never be installed
    // apart.
    g_options = cairo_font_options_create();
    cairo_font_options_set_hint_style(g_options, CAIRO_HINT_STYLE_SLIGHT);
    build_face(g_sans, sans, sans_len, "sans");
    build_face(g_mono, mono, mono_len, "monospace");
    return faces_select_ft_backed();
}

void gui_select_font_face(cairo_t* cr, GuiFontFamily family) {
    const BundledFace& f = (family == GuiFontFamily::Mono) ? g_mono : g_sans;
    if (f.face == nullptr) return;
    cairo_set_font_face(cr, f.face);
    // The hint style rides with the face at every select, which is why no site
    // above knows about it: the painters (clock_cell_metrics measures on the
    // painter's own font) and the install's own probe, which borrows a
    // scratch context, all arrive here before they size, shape, measure or
    // paint.
    cairo_set_font_options(cr, g_options);
}
