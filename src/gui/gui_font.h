#pragma once

// THE ONE FACE OWNER: every text surface selects its face here and nowhere
// else, so the painters never name a font. THE PRODUCT'S FACES ARE ITS OWN
// (architect 2026-10-02: "the app becomes its own thing; it has its own fonts,
// and those are Roboto and Roboto Mono"): the three files under the
// repository's `fonts/` directory, carried by both binaries — compiled into the
// Linux executable (gui_font_embedded.cpp), shipped as the APK's three assets
// on Android — and turned into faces by ONE implementation on both devices,
// gui_font_bundled.cpp, through FreeType. Neither device asks anything for a
// face: fontconfig is never consulted on the laptop (its answer for a missing
// family is a silent substitute, a backstop by another name), and the tablet
// has nothing to consult (Samsung's font setting is a framework substitution a
// native app never sees). THE ANSWER INCLUDES THE HINT STYLE, not the face
// alone: a hinter grid-fits the outline, so the same bytes report different
// whole-pixel ink under different hinters, and the product's own choice is
// SLIGHT on both devices (gui_font_bundled.cpp's head, with its table).
//
// The three families are the product's whole face inventory: the proportional
// sans every row shapes and paints on (13 Windows px x gui_scale since
// 2026-10-02, redesign_font_size_px; the text_shape
// chokepoint's subject); THE SANS IN BOLD, ONE SURFACE — the window caption's
// title (architect 2026-10-05: Windows 95's caption font was the body face in
// bold, so Roboto Bold at the body's 13; paint_caption_row, paint_handler.cpp);
// and the monospace — ONE FACE, TWO CELLS (since
// 2026-08-28): the row-8 clock and the render player's `<position> /
// <length>` on the modal row, which takes the row-8 cell's size and metrics.
// The AV SYNC STATS PANEL's rows were a third from 2026-09-03 and went with
// the panel on 2026-09-30; the rule's own record is at paint_handler.cpp's
// bottom-row text block. Slant and weight are not parameters: the weight is
// the family's (SansBold is its own file, never a synthesized emboldening),
// nothing is slanted, and each family is exactly one file.
//
// This selects the FACE only. Size stays the caller's — each site sets its own
// cairo_set_font_size after selecting, and the scaled font it then borrows is
// what text_shape must be handed (shape with the font you paint with).

#include <cairo/cairo.h>

#include <cstddef>
#include <cstdint>

enum class GuiFontFamily { Sans, SansBold, Mono };

void gui_select_font_face(cairo_t* cr, GuiFontFamily family);

// THE ONE SETUP CALL, with TWO CALLERS, one per platform, each once before the
// first paint: the Wayland backend (GuiPlatform::init) hands in the bytes
// compiled into the executable, the Android backend (install_fonts_or_die)
// the three files it read out of the APK's assets. The bytes are COPIED — the
// caller may free or unmap them the moment it returns — and the three faces
// built from them live for the process's life.
//
// THE RETURN IS THE INSTALL OBSERVED, not assumed: true when selecting each
// family actually puts an FT-BACKED face on a context, which is both what the
// install produces and what text_shape requires; a context whose face is
// still cairo's toy default answers false. Its producer is breach-only (the
// bytes are the repository's own, so a face that fails to build is a build
// defect), and each caller dies on false rather than painting in cairo's
// default face.
bool gui_font_install_bundled(const uint8_t* sans, size_t sans_len,
                              const uint8_t* sans_bold, size_t sans_bold_len,
                              const uint8_t* mono, size_t mono_len);

// THE LINUX BINARY'S COPY OF THE THREE FILES, defined by gui_font_embedded.cpp,
// which only the Linux target compiles (the APK carries the same files as
// assets instead, so the Android library defines none of these and its one
// caller above never names them). Byte for byte `fonts/Roboto-Regular.ttf`,
// `fonts/Roboto-Bold.ttf` and `fonts/RobotoMono-Regular.ttf`.
extern const uint8_t gui_font_embedded_sans[];
extern const size_t  gui_font_embedded_sans_len;
extern const uint8_t gui_font_embedded_sans_bold[];
extern const size_t  gui_font_embedded_sans_bold_len;
extern const uint8_t gui_font_embedded_mono[];
extern const size_t  gui_font_embedded_mono_len;
