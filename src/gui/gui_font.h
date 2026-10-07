#pragma once

// THE ONE FACE OWNER: every text surface names its face here and nowhere
// else, so the painters never name a font. THE PRODUCT'S FACES ARE ITS OWN
// (architect 2026-10-02: "the app becomes its own thing; it has its own
// fonts"): two files under the repository's `fonts/` (fonts/README.md has
// their provenance and licence), carried by both binaries — compiled into
// the Linux executable (gui_font_embedded.cpp), shipped as the APK's assets
// on Android — and turned into faces by ONE implementation on both devices,
// gui_font_bundled.cpp, through FreeType. Neither device asks anything for a
// face: fontconfig is never consulted on the laptop and the tablet has
// nothing to consult.
//
// ONE FACE SET (architect 2026-10-06, the Windows 2000 pivot: "We can drop
// Nimbus and the Windows 95 theme as a whole"): a set is one chrome
// vocabulary's text — which file each of the three uses (GuiFace) is drawn
// from, its recorded vertical metrics and its tracking (GuiFaceSet below) —
// and the chrome spec names its set (chrome_spec.h's ChromeSpec). The table
// stays a table so a later vocabulary's face joins it as a second instance;
// today the one set, kGuiFaceSetWin2000, is live: WINE TAHOMA as ReactOS
// 0.4.16 ships it (tahoma.ttf, tahomabd.ttf; TrueType outlines on a
// Bitstream Vera base), the face the ReactOS captures are set in — its
// 11-ppem strike equals tmp/reactos.png's menu and title pixels exactly
// (measured 2026-10-06). The body is Tahoma, the bold (the caption's title
// alone) Tahoma Bold, the small (the ruler's labels alone) Tahoma at the
// smaller em.
// EVERY FACE DRAWS ITS ANTIALIASED OUTLINE AT EVERY SIZE, NEVER AN EMBEDDED
// BITMAP STRIKE (architect 2026-10-06; Tahoma carries strikes at 8–16 ppem,
// its bold at 9–13, and the sheets he judged were outline renders): the
// install hides the strikes from FreeType and every glyph load passes
// FT_LOAD_NO_BITMAP (gui_font_bundled.cpp's head). Every run is shaped by
// HarfBuzz with the face's OWN ADVANCES LESS THE SET'S TRACKING
// (kGuiTrackingPx below), under SLIGHT hinting (gui_font_bundled.cpp's
// head), at the DERIVED EM (gui_face_em_px), and painted through
// cairo_show_glyphs (text_shape.h).
//
// THE VERTICAL METRICS ARE RECORDED CONSTANTS, the ONLY SOURCE OF VERTICAL
// METRICS (the live set's kGuiFaceMetrics below; architect 2026-10-05, the
// constants 2026-10-06): ascent, descent, the line band, the cap band
// (redesign_baseline, line_baseline), the flag box, the lane heights and the
// ruler lane are the recorded integers in Windows px times the scale, as
// unrounded doubles (a font quantity is not a grid point), rounded at the
// element by the seat that reads them. They are the numbers the period drew
// at 96 dpi — Tahoma 8's 13-px cell with its 8-row cap — which is why they
// seat everything (CLAUDE.md's rounding doctrine: every chrome length is a
// Windows px).
//
// THE EM MATCHES THOSE METRICS VERTICALLY (architect 2026-10-05), derived at
// the install from the face's own measured ink (gui_face_em_px): the body's
// is the em at which the body file's "H" stands as tall as the recorded cap,
// the bold's the bold file's "H" against the same cap, the small's the body
// file's "0" ink against the recorded digit. Measured 2026-10-06: 11.003,
// 10.996 and 7.943 Windows px (Tahoma's "H" 1489 of 2048, Tahoma Bold's
// 1490, Tahoma's "0" 1547 — 8 pt at GDI's 11 ppem, Tahoma's own 8-pt size).
// The em is vertical alone: horizontally the face is its own, and a taller
// or deeper glyph ("$", a descender) may poke past a box sized off the
// recorded rows — accepted (architect 2026-10-05). A FACE'S OWN LINE BOX IS
// NEVER READ (Tahoma's hhea ascent is 2049 of 2048, above its caps): the
// recorded metrics seat every run. HORIZONTALLY THE FACE IS ITS OWN: every
// width used for layout is the shaped run's (text_shape), the face's own
// advances at the live size less the tracking.
//
// THE FOUR MATH SIGNS ARE WHERE THE FACE DRAWS THEM (2026-10-06): the lift
// that set "+", "=", "<" and ">" onto the hyphen's axis is deleted with
// Nimbus, whose signs sat on Helvetica's lower math axis. Tahoma already
// draws the five on one axis — its 11-ppem strike centres "-", "+", "<",
// ">" and "=" all on row 3 of the 8-row cap, and its outlines agree to a
// few units (the hyphen 566..730 of 2048, centre 648; "+" and "=" centred at
// 652, "<" ">" at 654; the bold within one unit) — so the derived lift came
// to under 0.003 em, a tenth of a device px at 400 %, and nothing remains
// for a mechanism to correct.

#include <cairo/cairo.h>

#include <cstddef>
#include <cstdint>

enum class GuiFace { Body, Bold, Small };
inline constexpr std::size_t kGuiFaceCount = 3;

// THE TWO FILES, THE INSTALL'S ONE ORDER: the Linux font step
// (CMakeLists.txt) and the APK's asset step (android/app/build_apk.sh) list
// the same names, the two backends hand the bytes in in this order, and a
// set names its files by their index here.
inline constexpr std::size_t kGuiFontFileCount = 2;
inline constexpr const char* kGuiFontFiles[kGuiFontFileCount] = {
    "tahoma.ttf",                 // 0: the body and the small
    "tahomabd.ttf",               // 1: the bold
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
// THE RETURN IS THE INSTALL OBSERVED, not assumed: true when each of the
// two files selects as an FT-BACKED cairo face, which is what text_shape
// requires, and the live set's faces carry the glyph each em is measured on.
// Its producer is breach-only (the bytes are the repository's own, so a face that fails to
// build is a build defect), and each caller dies on false.
bool gui_font_install_bundled(const GuiFontBytes (&files)[kGuiFontFileCount]);

// A FACE AT A SCALE: what every text consumer holds, the successor of a cairo
// scaled font. `percent` is gui_scale (render.h's gui_font(face) builds one
// at the live scale).
struct GuiFont {
    GuiFace face    = GuiFace::Body;
    int     percent = 100;
};
inline double gui_font_scale(const GuiFont& f) {
    return static_cast<double>(f.percent) / 100.0;
}

// THE RECORDED VERTICAL METRICS of one use, in Windows px: the ascent and
// the descent (the cell is their sum) and the CAP band, the digits' and
// capitals' rows above the baseline. Each set records its own (below).
struct GuiFaceMetrics {
    int ascent  = 0;
    int descent = 0;
    int cap     = 0;
};

// ONE CHROME VOCABULARY'S TEXT (architect 2026-10-06): per use (GuiFace, in
// its order), the kGuiFontFiles index the use is drawn from and its recorded
// metrics; the tracking in Windows px per glyph.
struct GuiFaceSet {
    std::size_t    file[kGuiFaceCount]    = {};
    GuiFaceMetrics metrics[kGuiFaceCount] = {};
    double         tracking_px            = 0.0;
};

// THE WIN2000 SET (architect 2026-10-06), the chrome spec's, live: Wine Tahoma.
//   METRICS: the body and the bold are Tahoma 8 at 96 dpi (GDI's LOGFONT
//   -11, 11 ppem), its 13-px cell — ascent 11 + descent 2 — with caps and
//   digits 8 rows, the cell Windows 95's MS Sans Serif 8 also drew, so every
//   lane and box keeps its 13; the small is a SIX-row digit, a cell all
//   above the baseline: WordPad's ruler digits, which stand 6 rows beside
//   the 22-px icons at 100 % (architect 2026-10-06).
//   TRACKING: -1/20 Windows px per glyph (architect 2026-10-06): Wine
//   Tahoma's outline letters, measured against Tahoma 8's strike drawn x3,
//   run 0.146 device px per glyph wider at 300 % (0.049 W), so its letters
//   are pulled together by that much: -0.07 device px at the laptop's
//   138 %, -0.14 at the tablet's 275 %, -0.15 at 300 %.
inline constexpr GuiFaceSet kGuiFaceSetWin2000 = {
    .file        = {0, 1, 0},
    .metrics     = {{11, 2, 8}, {11, 2, 8}, {6, 0, 6}},
    .tracking_px = -0.05,
};

// THE LIVE SET IS THE CHROME SPEC'S (chrome_spec.h's kLiveChromeSpec), which
// names its set by address. The include stands here, after the set it names
// and before the first reader.
#include "chrome_spec.h"
inline constexpr const GuiFaceSet& kGuiLiveFaceSet = *kLiveChromeSpec.face_set;

// THE LIVE SET'S RECORDED VERTICAL METRICS, per GuiFace — the only vertical
// metric source (the head).
inline constexpr const GuiFaceMetrics (&kGuiFaceMetrics)[kGuiFaceCount] =
    kGuiLiveFaceSet.metrics;
inline constexpr const GuiFaceMetrics& gui_face_metrics(GuiFace face) {
    return kGuiFaceMetrics[static_cast<std::size_t>(face)];
}

// The vertical metrics in DEVICE px at the font's scale: the recorded
// Windows px times the scale, unrounded.
inline double gui_font_ascent_px(const GuiFont& f) {
    return gui_face_metrics(f.face).ascent * gui_font_scale(f);
}
inline double gui_font_descent_px(const GuiFont& f) {
    return gui_face_metrics(f.face).descent * gui_font_scale(f);
}
inline double gui_font_line_px(const GuiFont& f) {
    return gui_font_ascent_px(f) + gui_font_descent_px(f);
}
inline double gui_font_cap_px(const GuiFont& f) {
    return gui_face_metrics(f.face).cap * gui_font_scale(f);
}

// THE FACE'S EM, in Windows px, DERIVED FROM THE RECORDED METRICS: the
// recorded cap over the use's file's own ink height of the glyph named in
// the head ("H", or "0" for the small), per em, read off the bundled file at
// the install.
double gui_face_em_px(GuiFace face);

// THE FACE'S CAIRO SCALED FONT at this scale (the use's file at its em times
// the scale, SLIGHT, outlines only): borrowed, owned by the face owner and
// cached on the scale; GUI thread only. text_shape shapes and paints every
// glyph on it.
cairo_scaled_font_t* gui_outline_scaled_font(const GuiFont& f);

// THE LIVE SET'S TRACKING, in WINDOWS PX per glyph (the figure and its
// measurement at the set above): every advance HarfBuzz returns is
// shortened by it, the last glyph of a run included, so a run's width stays
// the plain sum of its advances (text_shape.h) and a right-aligned run's
// right edge stays honest. It scales with the font like every chrome length
// and is NOT ROUNDED — it is an advance, not a grid point. ONE CONSTANT FOR
// THE THREE USES (the body, the caption's bold, the ruler's small). The font
// files themselves are unmodified (fonts/README.md).
inline constexpr double kGuiTrackingPx = kGuiLiveFaceSet.tracking_px;
// The tracking in DEVICE px at the font's scale, unrounded.
inline double gui_tracking_px(const GuiFont& f) {
    return kGuiTrackingPx * gui_font_scale(f);
}

// AN UPPER BOUND ON ONE GLYPH'S ADVANCE at the font's scale, in device px:
// the use's file's widest (its hhea maximum) at its em. A cull bound
// over-estimates by design; no layout reads this. The tracking is not
// subtracted: being negative, it only shortens a real advance, so the
// untracked figure stays an upper bound.
double gui_font_advance_bound_px(const GuiFont& f);

// THE LINUX BINARY'S COPY OF THE TWO FILES, in kGuiFontFiles' order,
// defined by gui_font_embedded.cpp, which only the Linux target compiles (the
// APK carries the same files as assets instead).
extern const GuiFontBytes gui_font_embedded_files[kGuiFontFileCount];
