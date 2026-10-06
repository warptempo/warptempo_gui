#include "icons.h"

#include <cstdio>
#include <cstring>

// THE LINUX DECODE (architect 2026-10-05, after the APK build caught the
// gap: Android's cairo is built -Dpng=disabled, android/deps/50_cairo.sh's
// own choice, so cairo_image_surface_create_from_png_stream cannot appear
// in any TU the Android target compiles). icons.cpp, shared by both
// backends, no longer calls it at all; THIS FILE IS THE ONE PLACE IN THE
// PRODUCT THAT DOES, and it is NOT IN THE ANDROID TARGET (CMakeLists.txt
// compiles it into warptempo_gui's Linux executable alone, the font and
// Chicago95-byte embed files' own precedent) nor in warptempo_cli, which
// paints nothing. The Android backend decodes the same PNGs its own road
// (platform_android.cpp's install_chicago95_bitmaps_or_die, AImageDecoder)
// and hands icons::install_chicago95_bitmaps (icons.cpp) the same pixel
// shape this file produces.

namespace icons {
namespace {

// cairo_read_func_t over an in-memory buffer — the fonts' own tiny reader,
// re-stated here rather than shared because it is three lines and its only
// other use (icons.cpp, before this split) is gone.
struct Reader {
    const uint8_t* p;
    size_t         left;
};
cairo_status_t read_from_buffer(void* closure, unsigned char* data,
                                unsigned int length) {
    Reader* r = static_cast<Reader*>(closure);
    if (length > r->left) return CAIRO_STATUS_READ_ERROR;
    std::memcpy(data, r->p, length);
    r->p += length;
    r->left -= length;
    return CAIRO_STATUS_SUCCESS;
}

} // namespace

bool install_chicago95_bitmaps_from_png_linux(
    const Chicago95Bytes (&files)[kChicago95FileCount]) {
    cairo_surface_t* decoded[kChicago95FileCount]    = {};
    cairo_surface_t* normalized[kChicago95FileCount] = {};
    Chicago95Pixels  pixels[kChicago95FileCount];
    bool ok = true;

    for (std::size_t i = 0; i < kChicago95FileCount; ++i) {
        Reader reader{files[i].data, files[i].len};
        decoded[i] =
            cairo_image_surface_create_from_png_stream(read_from_buffer,
                                                        &reader);
        const int w = cairo_image_surface_get_width(decoded[i]);
        const int h = cairo_image_surface_get_height(decoded[i]);
        if (cairo_surface_status(decoded[i]) != CAIRO_STATUS_SUCCESS ||
            w != 16 || h != 16) {
            std::fprintf(stderr,
                         "icons: Chicago95 icon \"%s\" did not decode as a "
                         "16x16 PNG\n",
                         kChicago95Files[i]);
            ok = false;
            continue;
        }
        // NORMALIZE TO ARGB32: cairo's loader picks the format the SOURCE
        // actually carries — ARGB32 for a file with an alpha channel,
        // RGB24 (fully opaque, no alpha channel at all) for one without,
        // music-player.png's case. Painting either onto a fresh transparent
        // ARGB32 canvas composites it OVER at alpha 255 throughout (RGB24
        // has none to blend against), so install_chicago95_bitmaps
        // (icons.cpp, the one shared owner) is handed exactly one pixel
        // shape no matter which format the source PNG triggered.
        normalized[i] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
        cairo_t* cr = cairo_create(normalized[i]);
        cairo_set_source_surface(cr, decoded[i], 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_flush(normalized[i]);
        pixels[i].argb32 = cairo_image_surface_get_data(normalized[i]);
    }

    const bool installed = install_chicago95_bitmaps(pixels);
    for (cairo_surface_t* s : decoded) if (s) cairo_surface_destroy(s);
    for (cairo_surface_t* s : normalized) if (s) cairo_surface_destroy(s);
    return ok && installed;
}

} // namespace icons
