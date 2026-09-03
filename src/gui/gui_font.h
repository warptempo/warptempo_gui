#pragma once

// THE ONE FACE OWNER: every text surface selects its face here and nowhere
// else; what "sans" and "monospace" RESOLVE TO is the backend's business —
// fontconfig's answer on Linux, the bundled Liberation files on Android — so
// the painters never name a font.
//
// The two families are the product's whole face inventory: the proportional
// sans every row shapes and paints on (12pt x gui_scale, the text_shape
// chokepoint's subject) and the monospace — ONE FACE, THREE CELLS since
// 2026-09-03 (it was two, the clock's pair, from 2026-08-28): the row-8 clock,
// the render player's `<position> / <length>` on the modal row, which takes
// the row-8 cell's size and metrics, and THE FOLDER OVERLAY'S BAND under all
// three of its contents (architect 2026-09-03, twice: the AV Sync Stats
// panel's rows that morning — a column of figures rebuilt every frame, which
// a proportional face would make walk under the eye — and the render player's
// and the Open project picker's listings that evening, his first try at their
// width asymmetry; the widening's own record is at paint_handler.cpp's
// bottom-row text block, which owns the rule). Slant and weight are not
// parameters: every site is normal/normal, so a face is named by its family
// and nothing else.
//
// This selects the FACE only. Size stays the caller's — each site sets its own
// cairo_set_font_size after selecting, and the scaled font it then borrows is
// what text_shape must be handed (shape with the font you paint with).
//
// TWO IMPLEMENTATIONS, ONE PER BACKEND, exactly one of them compiled into a
// given binary: gui_font_fontconfig.cpp (Linux — cairo's toy font API, so the
// resolution is fontconfig's) and gui_font_bundled.cpp (Android — two
// FreeType faces built from font files the backend hands in, there being no
// fontconfig on the platform).

#include <cairo/cairo.h>

#include <cstddef>
#include <cstdint>

enum class GuiFontFamily { Sans, Mono };

void gui_select_font_face(cairo_t* cr, GuiFontFamily family);

// THE BUNDLED BACKEND'S ONE SETUP CALL, defined by gui_font_bundled.cpp alone:
// the Android backend calls it once, before the first paint, with the two font
// files it read out of the APK's assets. The bytes are COPIED — the caller may
// free or unmap them the moment it returns — and the two faces built from them
// live for the process's life. On the Linux build there is nothing to install
// (fontconfig answers), so no caller exists and the symbol is not linked.
void gui_font_install_bundled(const uint8_t* sans, size_t sans_len,
                              const uint8_t* mono, size_t mono_len);
