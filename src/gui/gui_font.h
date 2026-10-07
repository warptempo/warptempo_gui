#pragma once

// THE ONE FACE OWNER: every text surface names its face here and nowhere
// else, so the painters never name a font. THE PRODUCT'S FACES ARE ITS OWN
// (architect 2026-10-02: "the app becomes its own thing; it has its own
// fonts"): four files under the repository's `fonts/` (fonts/README.md has
// their provenance and licences), carried by both binaries — compiled into
// the Linux executable (gui_font_embedded.cpp), shipped as the APK's assets
// on Android — and turned into faces by ONE implementation on both devices,
// gui_font_bundled.cpp, through FreeType. Neither device asks anything for a
// face: fontconfig is never consulted on the laptop and the tablet has
// nothing to consult.
//
// TWO FACE SETS, ONE LIVE (architect 2026-10-06, the Windows 2000 pivot;
// the second set 2026-10-07 with the clearlooks chrome): a set is one chrome
// vocabulary's text — which file each of the three uses (GuiFace) is drawn
// from, its recorded vertical metrics and its tracking (GuiFaceSet below) —
// and the chrome spec names its set (chrome_spec.h's ChromeSpec), the
// device config's `chrome` choosing the live spec once at launch. THE
// WIN2000 SET, kGuiFaceSetWin2000: WINE TAHOMA as ReactOS 0.4.16 ships it
// (tahoma.ttf, tahomabd.ttf; TrueType outlines on a Bitstream Vera base),
// the face the ReactOS captures are set in — its 11-ppem strike equals
// tmp/reactos.png's menu and title pixels exactly (measured 2026-10-06).
// THE GNOME2 SET, kGuiFaceSetGnome2: DEJAVU SANS as Debian 6 squeeze ships
// it (DejaVuSans.ttf, DejaVuSans-Bold.ttf), GNOME 2's Sans 10, the face his
// squeeze captures are set in. In each set the body is the regular file,
// the bold (the caption's title alone) the bold file, the small (the
// ruler's labels alone) the regular file at the smaller em.
// EVERY FACE DRAWS ITS ANTIALIASED OUTLINE AT EVERY SIZE, NEVER AN EMBEDDED
// BITMAP STRIKE (architect 2026-10-06; Tahoma carries strikes at 8–16 ppem,
// its bold at 9–13, and the sheets he judged were outline renders; DejaVu
// carries none): the install hides the strikes from FreeType and every glyph
// load passes FT_LOAD_NO_BITMAP (gui_font_bundled.cpp's head). Every run is
// shaped by HarfBuzz with the face's OWN ADVANCES LESS THE SET'S TRACKING
// (GuiFaceSet::tracking_px, gui_tracking_px below), under SLIGHT hinting
// (gui_font_bundled.cpp's head), at the DERIVED EM (gui_face_em_px), and
// painted through cairo_show_glyphs (text_shape.h).
//
// THE VERTICAL METRICS ARE RECORDED CONSTANTS, the ONLY SOURCE OF VERTICAL
// METRICS (the live set's, gui_face_metrics below; architect 2026-10-05, the
// constants 2026-10-06): ascent, descent, the line band, the cap band
// (redesign_baseline, line_baseline), the flag box, the lane heights and the
// ruler lane are the recorded integers in Windows px times the scale, as
// unrounded doubles (a font quantity is not a grid point), rounded at the
// element by the seat that reads them. They are the numbers the period drew
// at 96 dpi — Tahoma 8's 13-px cell with its 8-row cap, Sans 10's 17-px
// cell with its 10-row cap — which is why they seat everything (CLAUDE.md's rounding doctrine: every chrome length is a
// Windows px).
//
// THE EM MATCHES THOSE METRICS VERTICALLY (architect 2026-10-05), derived at
// the install from the face's own measured ink (gui_face_em_px): the body's
// is the em at which the body file's "H" stands as tall as the recorded cap,
// the bold's the bold file's "H" against the same cap, the small's the body
// file's "0" ink against the recorded digit. Measured 2026-10-06 for the
// win2000 set: 11.003, 10.996 and 7.943 Windows px (Tahoma's "H" 1489 of
// 2048, Tahoma Bold's 1490, Tahoma's "0" 1547 — 8 pt at GDI's 11 ppem,
// Tahoma's own 8-pt size); 2026-10-07 for the gnome2 set: 13.717, 13.717
// and 7.933 Windows px (DejaVu Sans's and DejaVu Sans Bold's "H" both 1493
// of 2048, DejaVu Sans's "0" 1549 — the body 3 % over Sans 10's 13.333).
// The em is vertical alone: horizontally the face is its own, and a taller
// or deeper glyph ("$", a descender) may poke past a box sized off the
// recorded rows — accepted (architect 2026-10-05). A FACE'S OWN LINE BOX IS
// NEVER READ (Tahoma's hhea ascent is 2049 of 2048, above its caps;
// DejaVu's 1901 / -483 happens to round to the recorded 13 / 4 at Sans 10,
// and is not read either): the recorded metrics seat every run. HORIZONTALLY THE FACE IS ITS OWN: every
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

// THE FOUR FILES, THE INSTALL'S ONE ORDER, EVERY SET'S (architect
// 2026-10-07, the second vocabulary's pair joining the first's): THIS LIST
// IS THE ONE PLACE THE NAMES ARE SPELLED — the Linux font step
// (CMakeLists.txt) and the APK's asset step (android/app/build_apk.sh) each
// read the quoted names out of this initializer, so a file added here is
// embedded and packed with no second list; the two backends hand the bytes
// in in this order, and a set names its files by their index here. Both
// sets' files are carried whichever chrome a device runs (the `chrome` key
// is read after the Android backend installs the faces, and the install
// measures every set's ems at once, gui_font_bundled.cpp).
inline constexpr std::size_t kGuiFontFileCount = 4;
inline constexpr const char* kGuiFontFiles[kGuiFontFileCount] = {
    "tahoma.ttf",                 // 0: win2000's body and small
    "tahomabd.ttf",               // 1: win2000's bold
    "DejaVuSans.ttf",             // 2: gnome2's body and small
    "DejaVuSans-Bold.ttf",        // 3: gnome2's bold
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
// four files selects as an FT-BACKED cairo face, which is what text_shape
// requires, and EVERY set's faces (every chrome spec's, kGuiChromeSpecs)
// carry the glyph each em is measured on — every set, because the install
// precedes the `chrome` key's read on Android.
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
//   TRACKING: none (architect 2026-10-07: "we're not matching font widths,
//   only the heights"): the recorded metrics match the period's heights and
//   the widths are the live face's own advances, as the gnome2 set's are.
inline constexpr GuiFaceSet kGuiFaceSetWin2000 = {
    .file        = {0, 1, 0},
    .metrics     = {{11, 2, 8}, {11, 2, 8}, {6, 0, 6}},
    .tracking_px = 0.0,
};

// THE GNOME2 SET (architect 2026-10-07, the planner's design under his free
// rein), the clearlooks chrome's: DEJAVU SANS AS DEBIAN 6 SQUEEZE SHIPS IT
// (ttf-dejavu 2.31-1, DejaVuSans.ttf and DejaVuSans-Bold.ttf byte for byte;
// fonts/README.md) — GNOME 2's Sans and metacity's Sans Bold, the faces his
// squeeze captures (tmp/squeeze/) are set in, drawn at the size below.
//   METRICS (architect 2026-10-07, ~02:20: "Windows is the base: a compact,
//   nicely proportioned layout; the fonts and the icons become
//   disproportionate in Clearlooks" — the Clearlooks vocabulary wears
//   Windows' PROPORTIONS under Clearlooks' own drawing): THE BASE'S CELL,
//   {11, 2, 8} for the body and the bold — ascent 11 + descent 2, the 13-row
//   cell, the cap at 8 rows — so DejaVu Sans is drawn to a cap of 8 W px, its
//   em derived at the install off its own "H" (0.729 em) as every face's is:
//   10.97 W px, 8.2 pt at 96 dpi, GNOME 2's own "Sans 8" (Appearance ->
//   Fonts' smallest common setting), not squeeze's default Sans 10 (the
//   17-row cell his captures show, which the 2026-10-07 base of the
//   vocabulary carried until this ruling); the small is the same SIX-row
//   digit as the win2000 set's, a cell all above the baseline (its em 7.93
//   off the "0"), so the ruler lane keeps its 17 (GNOME has no small face:
//   GTK's ruler drew the widget's own font). Every height GTK derives from
//   the cell is re-derived at 13 (chrome_spec.h's clearlooks instance).
//   TRACKING: none ("compromise and approximate with modern HarfBuzz and
//   DejaVu Sans": the bytecode look of 2010 is not reproduced, and Pango
//   tracked nothing; the heights are matched, never the widths).
inline constexpr GuiFaceSet kGuiFaceSetGnome2 = {
    .file        = {2, 3, 2},
    .metrics     = {{11, 2, 8}, {11, 2, 8}, {6, 0, 6}},
    .tracking_px = 0.0,
};

// THE LIVE SET IS THE CHROME SPEC'S (chrome_spec.h's live_chrome_spec(),
// chosen once at launch by the device config's `chrome`), which names its
// set by address. The include stands here, after the sets it names and
// before the first reader.
#include "chrome_spec.h"
inline const GuiFaceSet& gui_live_face_set() {
    return *live_chrome_spec().face_set;
}

// THE LIVE SET'S RECORDED VERTICAL METRICS, per GuiFace — the only vertical
// metric source (the head).
inline const GuiFaceMetrics& gui_face_metrics(GuiFace face) {
    return gui_live_face_set().metrics[static_cast<std::size_t>(face)];
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
// live set's recorded cap over the use's file's own ink height of the glyph
// named in the head ("H", or "0" for the small), per em, the ink read off
// the bundled file at the install (every file's, so the em follows whichever
// set the chrome names).
double gui_face_em_px(GuiFace face);

// THE FACE'S CAIRO SCALED FONT at this scale (the use's file at its em times
// the scale, SLIGHT, outlines only): borrowed, owned by the face owner and
// cached on the scale; GUI thread only. text_shape shapes and paints every
// glyph on it.
cairo_scaled_font_t* gui_outline_scaled_font(const GuiFont& f);

// THE LIVE SET'S TRACKING (GuiFaceSet::tracking_px), in WINDOWS PX per
// glyph (the figure and its reason at each set above; both sets 0 since
// 2026-10-07, the architect's heights-only rule — the mechanism stays, one
// number per set): every advance HarfBuzz returns is
// shortened by it, the last glyph of a run included, so a run's width stays
// the plain sum of its advances (text_shape.h) and a right-aligned run's
// right edge stays honest. It scales with the font like every chrome length
// and is NOT ROUNDED — it is an advance, not a grid point. ONE CONSTANT FOR
// THE THREE USES (the body, the caption's bold, the ruler's small). The font
// files themselves are unmodified (fonts/README.md).
// The tracking in DEVICE px at the font's scale, unrounded.
inline double gui_tracking_px(const GuiFont& f) {
    return gui_live_face_set().tracking_px * gui_font_scale(f);
}

// AN UPPER BOUND ON ONE GLYPH'S ADVANCE at the font's scale, in device px:
// the use's file's widest (its hhea maximum) at its em. A cull bound
// over-estimates by design; no layout reads this. The tracking is not
// subtracted: zero or negative, it never lengthens a real advance, so the
// untracked figure stays an upper bound.
double gui_font_advance_bound_px(const GuiFont& f);

// THE LINUX BINARY'S COPY OF THE FOUR FILES, in kGuiFontFiles' order,
// defined by gui_font_embedded.cpp, which only the Linux target compiles (the
// APK carries the same files as assets instead).
extern const GuiFontBytes gui_font_embedded_files[kGuiFontFileCount];
