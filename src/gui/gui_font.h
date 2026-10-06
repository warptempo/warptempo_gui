#pragma once

// THE ONE FACE OWNER: every text surface names its face here and nowhere
// else, so the painters never name a font. THE PRODUCT'S FACES ARE ITS OWN
// (architect 2026-10-02: "the app becomes its own thing; it has its own
// fonts"): five files under the repository's `fonts/` (fonts/README.md has
// each one's provenance and licence), carried by both binaries — compiled
// into the Linux executable (gui_font_embedded.cpp), shipped as the APK's
// assets on Android — and turned into faces by ONE implementation on both
// devices, gui_font_bundled.cpp, through FreeType. Neither device asks
// anything for a face: fontconfig is never consulted on the laptop and the
// tablet has nothing to consult.
//
// THE PERIOD BITMAP FACES (architect 2026-10-05). Three faces, each ONE
// STRIKE of 1-bit pixels authored in Windows px:
//   - BODY: Cronyx Helvetica (crox1h.otb), the pixel trace of MS Sans Serif
//     8 pt — a 13-px cell, ascent 11 + descent 2, caps and digits 9, every
//     row of the chrome, the time fields included (their digits are tabular);
//   - BOLD: its bold (crox1hb.otb), the window caption's title alone
//     (Windows 95's caption font was the body face in bold);
//   - SMALL: the reconstructed Small Fonts digits (small_fonts_digits.otb,
//     tools/small_fonts/: 0-9 . : only, a 7-row cell all above the
//     baseline), the ruler's labels alone.
// THE BITMAP MODE: at a gui_scale that is a whole multiple of 100, k =
// gui_scale / 100, each glyph is drawn from its strike, each strike pixel a
// k x k block of device px in the text colour — nearest neighbour, no
// antialias, no hinting, no filtering, the origin on a whole device px
// (text_shape's show_shaped_run blits the strike itself: cairo-ft would
// scale a strike through a filtered pattern).
//
// THE FALLBACK, AT EVERY OTHER SCALE: NIMBUS SANS (architect 2026-10-06;
// Regular for the body and the small face, Bold for the caption), URW's
// Helvetica of the base35 set — the genre MS Sans Serif was drawn from — an
// outline face (OpenType CFF) shaped by HarfBuzz with ITS OWN ADVANCES,
// under SLIGHT hinting (gui_font_bundled.cpp's head), seated on the bitmap
// face's baseline. ITS EM MATCHES THE BITMAP FACE VERTICALLY (architect
// 2026-10-05), derived at the install from the two faces' measured values
// (gui_fallback_em_px): the body's is the em at which Nimbus's "H" stands as
// tall as Cronyx's 9-row cap, the bold's Nimbus Bold's "H" against Cronyx
// Bold's, the small's Nimbus's "0" ink against Small Fonts' 7-row digit
// (12.35, 12.35 and 9.38 Windows px, measured 2026-10-06). Each face keeps
// its own true advances, so Nimbus text runs wider than Cronyx would at the
// same scale, and a taller or deeper Nimbus glyph ("$", a descender) may poke
// past a box sized off the bitmap face's rows — both accepted (architect
// 2026-10-05: the strikes are primary and the fallback may spill over here
// and there). NIMBUS SITS HIGH IN AN APP THAT TRUSTS A FONT'S LINE BOX (its
// hhea ascent, 729 of 1000, is its "H"'s own height, so a line seated on it
// puts the caps at the box's top); the fault is moot here because no line
// box is ever read off the outline: the strikes' metrics below seat every
// run. IN BITMAP MODE a codepoint the strike lacks (free text is UTF-8;
// Cronyx carries ASCII and KOI8 Cyrillic) is drawn from Nimbus at its own
// advance — the one place the bitmap mode meets an outline.
//
// THE BITMAP FACES ARE THE ONLY SOURCE OF VERTICAL METRICS, AT EVERY SCALE
// (architect 2026-10-05): ascent, descent, the line band, the cap band
// (redesign_baseline, line_baseline), the flag box, the lane heights and the
// ruler lane are the strike's integers in Windows px times the scale, as
// unrounded doubles (a font quantity is not a grid point), rounded at the
// element by the seat that reads them. HORIZONTALLY THE LIVE FACE IS ITS
// OWN: every width used for layout is the shaped run's (text_shape), the
// strike's advances times k in bitmap mode, Nimbus's own at the live size
// otherwise.

#include <cairo/cairo.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

enum class GuiFace { Body, Bold, Small };
inline constexpr std::size_t kGuiFaceCount = 3;

// THE FIVE FILES, THE INSTALL'S ONE ORDER: the Linux font step
// (CMakeLists.txt) and the APK's asset step (android/app/build_apk.sh) list
// the same names, the two backends hand the bytes in in this order.
inline constexpr std::size_t kGuiFontFileCount = 5;
inline constexpr const char* kGuiFontFiles[kGuiFontFileCount] = {
    "crox1h.otb",                 // the body strike
    "crox1hb.otb",                // the bold strike
    "small_fonts_digits.otb",     // the small strike
    "NimbusSans-Regular.otf",     // the fallback for the body and the small
    "NimbusSans-Bold.otf",        // the fallback for the bold
};

struct GuiFontBytes {
    const uint8_t* data = nullptr;
    size_t         len  = 0;
};

// THE ONE SETUP CALL, with TWO CALLERS, one per platform, each once before the
// first paint: the Wayland backend (GuiPlatform::init) hands in the bytes
// compiled into the executable, the Android backend (install_fonts_or_die)
// the files it read out of the APK's assets. The bytes are COPIED — the
// caller may free or unmap them the moment it returns.
//
// THE RETURN IS THE INSTALL OBSERVED, not assumed: true when every strike
// read out with its "0" and a positive cell and each fallback face selects as
// an FT-BACKED cairo face, which is what text_shape requires. Its producer is
// breach-only (the bytes are the repository's own, so a face that fails to
// build is a build defect), and each caller dies on false.
bool gui_font_install_bundled(const GuiFontBytes (&files)[kGuiFontFileCount]);

// A FACE AT A SCALE: what every text consumer holds, the successor of a cairo
// scaled font. `percent` is gui_scale (render.h's gui_font(face) builds one
// at the live scale).
struct GuiFont {
    GuiFace face    = GuiFace::Body;
    int     percent = 100;
};
// THE BITMAP SWITCH, ON THE SCALE ALONE (architect 2026-10-05): a whole
// multiple of 100 is a bitmap scale for every bitmap-faced thing the product
// draws, not the fonts alone — the icon pass (icons.h's draw_cased) reads
// this same predicate on gui_scale_percent() so a scale that turns the text
// bitmap turns the Chicago95 glyphs bitmap with it, with no second switch to
// drift out of step.
inline bool gui_scale_is_bitmap(int percent) { return percent % 100 == 0; }
inline bool gui_font_is_bitmap(const GuiFont& f) {
    return gui_scale_is_bitmap(f.percent);
}
inline double gui_font_scale(const GuiFont& f) {
    return static_cast<double>(f.percent) / 100.0;
}

// ONE STRIKE GLYPH, in strike pixels: `top` rows above the baseline to the
// bitmap's first row, `left` columns from the pen; `lit` is width x height,
// row-major, 1 for a lit pixel.
struct GuiStrikeGlyph {
    int                  advance = 0;
    int                  left    = 0;
    int                  top     = 0;
    int                  width   = 0;
    int                  height  = 0;
    std::vector<uint8_t> lit;
};
// The strike's glyph for `cp`, or nullptr when the strike lacks it.
const GuiStrikeGlyph* gui_strike_glyph(GuiFace face, char32_t cp);

// THE STRIKE'S VERTICAL METRICS, in Windows px: its ascent and descent (the
// cell is their sum) and its CAP band, the "0"'s rows above the baseline —
// the one glyph all three strikes carry, standing exactly at Cronyx's 9-row
// caps and at Small Fonts' 7-row cell.
struct GuiStrikeMetrics {
    int ascent  = 0;
    int descent = 0;
    int cap     = 0;
};
const GuiStrikeMetrics& gui_strike_metrics(GuiFace face);

// The vertical metrics in DEVICE px at the font's scale: the strike's
// Windows px times the scale, unrounded.
inline double gui_font_ascent_px(const GuiFont& f) {
    return gui_strike_metrics(f.face).ascent * gui_font_scale(f);
}
inline double gui_font_descent_px(const GuiFont& f) {
    return gui_strike_metrics(f.face).descent * gui_font_scale(f);
}
inline double gui_font_line_px(const GuiFont& f) {
    return gui_font_ascent_px(f) + gui_font_descent_px(f);
}
inline double gui_font_cap_px(const GuiFont& f) {
    return gui_strike_metrics(f.face).cap * gui_font_scale(f);
}

// THE FALLBACK'S EM for this face, in Windows px: the strike's cap (its
// "0"'s rows) over Nimbus's own ink height of the glyph named above, per
// em, both read off the bundled files at the install.
double gui_fallback_em_px(GuiFace face);

// THE FALLBACK'S CAIRO SCALED FONT for this face at this scale (Nimbus at
// its em times the scale, SLIGHT): borrowed, owned by the face owner and
// cached on the scale; GUI thread only. text_shape shapes and paints every
// outline glyph on it.
cairo_scaled_font_t* gui_outline_scaled_font(const GuiFont& f);

// AN UPPER BOUND ON ONE GLYPH'S ADVANCE at the font's scale, in device px:
// the larger of the strike's widest advance and Nimbus's widest (its
// hhea maximum) at its em. A cull bound over-estimates by design; no layout
// reads this.
double gui_font_advance_bound_px(const GuiFont& f);

// THE LINUX BINARY'S COPY OF THE FIVE FILES, in kGuiFontFiles' order,
// defined by gui_font_embedded.cpp, which only the Linux target compiles (the
// APK carries the same files as assets instead).
extern const GuiFontBytes gui_font_embedded_files[kGuiFontFileCount];
