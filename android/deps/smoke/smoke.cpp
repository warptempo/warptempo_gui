// Smoke TU for the android-arm64 dependency sysroot: include every header the
// product uses and force a real symbol reference into each library, so that a
// successful LINK proves the whole staging prefix is consistent (headers,
// static archives and pkg-config all agreeing).
//
// It is never run -- the device may not even be present. What it proves is that
// the five libraries resolve against each other and against bionic.

#include <cairo.h>
#include <cairo-ft.h>
#include <fftw3.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

#include <cstdio>

// Referenced, never called: these are the two entry points whose PRESENCE is
// the whole question (cairo-ft without fontconfig; hb-ft in a shaping-only
// harfbuzz). Taking the address makes the linker resolve them for real.
static void *const kFaceEntryPoints[] = {
    reinterpret_cast<void *>(&cairo_ft_font_face_create_for_ft_face),
    reinterpret_cast<void *>(&hb_ft_font_create_referenced),
};

extern "C" int warptempo_android_smoke(void) {
    // fftw3 + fftw3_threads: exactly the pair the frozen engine links, and
    // plan_with_nthreads(1) is the engine's determinism invariant.
    if (fftw_init_threads() == 0) return 1;
    fftw_plan_with_nthreads(1);
    double *buf = static_cast<double *>(fftw_malloc(sizeof(double) * 8));
    if (!buf) return 2;
    fftw_free(buf);
    fftw_cleanup_threads();

    // freetype
    FT_Library ft = nullptr;
    if (FT_Init_FreeType(&ft) != 0) return 3;
    FT_Done_FreeType(ft);

    // harfbuzz
    hb_buffer_t *hb = hb_buffer_create();
    if (!hb_buffer_allocation_successful(hb)) return 4;
    hb_buffer_destroy(hb);

    // cairo: the image surface is the product's one surface kind.
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 4, 4);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) return 5;
    cairo_t *cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_status_t st = cairo_status(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    if (st != CAIRO_STATUS_SUCCESS) return 6;

    std::printf("cairo %s / harfbuzz %s / freetype ok / fftw %p\n",
                cairo_version_string(), hb_version_string(),
                static_cast<const void *>(kFaceEntryPoints));
    return 0;
}
