// Text for the spike: freetype straight onto an APK asset's bytes, harfbuzz for
// shaping, cairo-ft for painting. No fontconfig exists in this sysroot and none
// is wanted -- the product's one shaping chokepoint already owns the same three
// pieces, and this is the Android-shaped rehearsal of that seam.
//
// TWO FT_Face OBJECTS PER FONT, from one buffer. cairo sets the face's size when
// it renders and harfbuzz reads the face's size when it shapes; sharing one face
// makes the two fight over FT_Set_Pixel_Sizes. Two faces over the same immutable
// memory costs nothing and removes the whole class of bug.
//
// CAIRO_ANTIALIAS_GRAY IS PINNED, deliberately and non-negotiably: subpixel AA is
// channel-ASYMMETRIC (it encodes the panel's physical RGB stripe order), so it
// would render fringed through the R/B swizzle this spike performs on every blit.
#pragma once

#include <cairo/cairo-ft.h>
#include <cairo/cairo.h>
#include <ft2build.h>
#include <hb-ft.h>
#include <hb.h>

#include <string>

#include FT_FREETYPE_H

class SpikeFont {
public:
    ~SpikeFont();

    // `bytes` must outlive the font: FT_New_Memory_Face does not copy, and the
    // AAsset backing it is held open for the process's life.
    bool init(FT_Library lib, const void* bytes, size_t size, double px, std::string& err);

    // Shape with harfbuzz, paint with cairo. (x, y) is the left end of the baseline.
    void draw(cairo_t* cr, double x, double y, const char* utf8) const;

    // Same shaping, advance only.
    double advance(const char* utf8) const;

    double px() const { return px_; }
    double line_height() const { return px_ * 1.35; }
    bool valid() const { return cairo_face_ != nullptr; }

private:
    double px_ = 0.0;
    FT_Face shape_face_ = nullptr;   // harfbuzz's
    FT_Face paint_face_ = nullptr;   // cairo's
    hb_font_t* hb_font_ = nullptr;
    cairo_font_face_t* cairo_face_ = nullptr;
    cairo_scaled_font_t* scaled_ = nullptr;
};
