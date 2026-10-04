#pragma once
// tools/palette/picker — the two faces, Roboto and Roboto Mono, from the repository's fonts/ (the APK's assets, the
// laptop check's files), built as the product builds them (src/gui/gui_font_bundled.cpp, the reference): FreeType
// memory faces under cairo-ft, hint style SLIGHT. Only the panel's words and numbers use them; no picture pixel is
// text drawn here.

#include <cairo.h>

#include <cstddef>
#include <cstdint>

// copies the bytes; false when FreeType or cairo refuses either face
bool fonts_install(const uint8_t* sans, size_t sans_len, const uint8_t* mono, size_t mono_len);

// select the face (and the product's font options) on cr at size_px
void fonts_select(cairo_t* cr, bool mono, double size_px);
