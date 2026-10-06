#pragma once
// tools/palette/picker — the one face, Nimbus Sans (the product's fallback face, src/gui/gui_font.h), from the
// repository's fonts/ (the APK's asset, the laptop check's file), built as the product builds its fallback
// (src/gui/gui_font_bundled.cpp, the reference): a FreeType memory face under cairo-ft, hint style SLIGHT. The panel's
// words and numbers use it (its digits tabular, so a readout's digits stand still); no picture pixel is text drawn
// here.

#include <cairo.h>

#include <cstddef>
#include <cstdint>

// copies the bytes; false when FreeType or cairo refuses the face
bool fonts_install(const uint8_t* sans, size_t sans_len);

// select the face (and the product's font options) on cr at size_px
void fonts_select(cairo_t* cr, double size_px);
