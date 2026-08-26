#include "gui_font.h"

// THE BUNDLED FACE OWNER (gui_font.h): Android has no fontconfig, so the two
// families resolve to two font FILES the backend ships in the APK and hands in
// once through gui_font_install_bundled. The faces are built with FreeType and
// wrapped as cairo font faces, which is the same FT-backed shape the Linux toy
// path produces — text_shape's precondition (an FT-backed scaled font) holds
// unchanged, and no site below the seam learns which backend answered.
//
// THIS FILE IS NOT IN THE LINUX TARGET; it compiles only into the Android
// library, beside platform_android.cpp.
//
// LIFETIME: the library, the two FT faces and the two cairo faces are created
// once and never destroyed — the process's exit reclaims them, and there is no
// second install. The byte buffers are COPIES this file owns, because
// FT_New_Memory_Face does not copy and the face reads from them for as long as
// it lives, while the caller's asset mapping is its own to release.

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

FT_Library  g_library = nullptr;
BundledFace g_sans;
BundledFace g_mono;

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
    build_face(g_sans, sans, sans_len, "sans");
    build_face(g_mono, mono, mono_len, "monospace");
}

void gui_select_font_face(cairo_t* cr, GuiFontFamily family) {
    const BundledFace& f = (family == GuiFontFamily::Mono) ? g_mono : g_sans;
    if (f.face == nullptr) return;
    cairo_set_font_face(cr, f.face);
}
