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
// ONE FACE AT EVERY SCALE: NIMBUS SANS (architect 2026-10-06), URW's
// Helvetica of the base35 set — the genre MS Sans Serif was drawn from — an
// outline face (OpenType CFF), in three uses (GuiFace):
//   - BODY: Nimbus Sans Regular, every row of the chrome, the time fields
//     included (their digits are tabular);
//   - BOLD: Nimbus Sans Bold, the window caption's title alone (Windows 95's
//     caption font was the body face in bold);
//   - SMALL: Nimbus Sans Regular at the smaller em, the ruler's labels alone.
// Every run is shaped by HarfBuzz with Nimbus's OWN ADVANCES LESS THE
// TRACKING (kGuiTrackingPx below: its 100 % width, its letters
// pulled together, architect 2026-10-06), under SLIGHT hinting
// (gui_font_bundled.cpp's head), at the DERIVED EM (gui_face_em_px),
// and painted through cairo_show_glyphs (text_shape.h).
//
// THE VERTICAL METRICS ARE RECORDED CONSTANTS, the ONLY SOURCE OF VERTICAL
// METRICS (kGuiFaceMetrics below; architect 2026-10-05, the constants
// 2026-10-06): ascent, descent, the line band, the cap band
// (redesign_baseline, line_baseline), the flag box, the lane heights and the
// ruler lane are the recorded integers in Windows px times the scale, as
// unrounded doubles (a font quantity is not a grid point), rounded at the
// element by the seat that reads them. They are the numbers Windows 95 drew
// at 96 dpi — MS Sans Serif 8's 13-px cell for the body and the bold, Small
// Fonts' 7-row digit for the small — which is why they seat everything
// (CLAUDE.md's rounding doctrine: every chrome length is a Windows px).
//
// THE EM MATCHES THOSE METRICS VERTICALLY (architect 2026-10-05), derived at
// the install from Nimbus's own measured ink (gui_face_em_px): the
// body's is the em at which Nimbus's "H" stands as tall as the recorded
// 9-row cap, the bold's Nimbus Bold's "H" against the same 9, the small's
// Nimbus's "0" ink against the recorded 7-row digit (12.35, 12.35 and 9.38
// Windows px, measured 2026-10-06). The em is vertical alone: horizontally
// the face is its own, and a taller or deeper glyph ("$", a descender) may
// poke past a box sized off the recorded rows — accepted (architect
// 2026-10-05). NIMBUS SITS HIGH IN AN APP THAT TRUSTS A FONT'S LINE BOX (its
// hhea ascent, 729 of 1000, is its "H"'s own height, so a line seated on it
// puts the caps at the box's top); the fault is moot here because no line
// box is ever read off the outline: the recorded metrics seat every run.
// HORIZONTALLY THE FACE IS ITS OWN: every width used for layout is the
// shaped run's (text_shape), Nimbus's own advances at the live size less the
// tracking. VERTICALLY THE ONE EXCEPTION IS THE FOUR MATH SIGNS ("+", "=",
// "<", ">"), each lifted onto the hyphen's axis as MS Sans Serif drew them
// (gui_sign_axis below, architect 2026-10-06).

#include <cairo/cairo.h>

#include <cstddef>
#include <cstdint>

enum class GuiFace { Body, Bold, Small };
inline constexpr std::size_t kGuiFaceCount = 3;

// THE TWO FILES, THE INSTALL'S ONE ORDER: the Linux font step
// (CMakeLists.txt) and the APK's asset step (android/app/build_apk.sh) list
// the same names, the two backends hand the bytes in in this order.
inline constexpr std::size_t kGuiFontFileCount = 2;
inline constexpr const char* kGuiFontFiles[kGuiFontFileCount] = {
    "NimbusSans-Regular.otf",     // the body and the small
    "NimbusSans-Bold.otf",        // the bold
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
// THE RETURN IS THE INSTALL OBSERVED, not assumed: true when each face
// selects as an FT-BACKED cairo face, which is what text_shape requires, and
// carries the glyph its em is measured on and the five its sign axis is
// measured on (the hyphen and the four math signs, gui_sign_axis). Its producer is breach-only (the
// bytes are the repository's own, so a face that fails to build is a build
// defect), and each caller dies on false.
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

// THE RECORDED VERTICAL METRICS, in Windows px (architect 2026-10-06): the
// ascent and the descent (the cell is their sum) and the CAP band, the
// digits' and capitals' rows above the baseline. PROVENANCE: the body and
// the bold are MS Sans Serif 8's 13-px cell at 96 dpi — ascent 11 +
// descent 2, caps and digits 9 — as its pixel trace, Cronyx Helvetica,
// carried them; the small is Small Fonts' 7-row digit, a cell all above the
// baseline, as ACID and Vegas drew their rulers in it (read off the
// architect's screenshots, 2026-10-05). Recorded 2026-10-06.
struct GuiFaceMetrics {
    int ascent  = 0;
    int descent = 0;
    int cap     = 0;
};
inline constexpr GuiFaceMetrics kGuiFaceMetrics[kGuiFaceCount] = {
    {11, 2, 9},   // Body
    {11, 2, 9},   // Bold
    {7, 0, 7},    // Small
};
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
// recorded cap over Nimbus's own ink height of the glyph named above ("H", or
// "0" for the small), per em, read off the bundled file at the install.
double gui_face_em_px(GuiFace face);

// THE FOUR MATH SIGNS SIT ON THE HYPHEN'S AXIS (architect 2026-10-06, "as
// MS Sans Serif had them"): "+" (U+002B), "=" (U+003D), "<" (U+003C) and ">"
// (U+003E) each rise by the hyphen's ink centre less its own, in every face
// and every run; the hyphen and every other glyph stay where Nimbus drew
// them. WHY: MS Sans Serif put the five marks on ONE axis — "-", "+", "=",
// "<" and ">" all centred at 0.389 of its 9-row cap — while Nimbus keeps its
// hyphen near there (240..312 of 729, centre 0.379) but draws the four signs
// on Helvetica's math axis, 0.318 ("+" -10..474, "=" 111..353, "<" ">"
// -9..474), so its plus sat visibly low beside the digits it signs; every
// letter and digit is within 0.35 Windows px of MS Sans Serif's. One rule:
// Helvetica's math axis is seated on its hyphen's, the period's one axis for
// the five. THE LIFTS ARE DERIVED at the install, like the em, from the
// face's own unscaled outline bounds (gui_font_bundled.cpp), in em units: the
// regular file's 0.044 ("+", "=") and 0.0435 ("<", ">"), the bold's 0.043
// and 0.0425 — the plus rising, at 400 %, 2.17 device px in the body, 2.12 in
// the bold and 1.65 in the small. THE MATCH IS BY GLYPH ID after substitution (text_shape compares
// a shaped glyph's id against `glyph`), never by codepoint: a cluster is not
// a glyph. The lift moves the ink alone — every advance, the run's width and
// the tracking are untouched.
inline constexpr std::size_t kGuiSignCount = 4;
inline constexpr char32_t kGuiMathSigns[kGuiSignCount] = {U'+', U'=', U'<',
                                                          U'>'};
struct GuiSignLift {
    unsigned glyph   = 0;    // the face's glyph id for the sign
    double   lift_em = 0.0;  // up-positive, in em
};
struct GuiSignAxis {
    GuiSignLift signs[kGuiSignCount] = {};
};
const GuiSignAxis& gui_sign_axis(GuiFace face);

// The lift of one shaped glyph in DEVICE px at the font's scale, HarfBuzz's
// sense (up-positive), unrounded: the sign's lift times the em times the
// scale, 0 for every glyph that is not one of the four signs.
inline double gui_sign_lift_px(const GuiFont& f, unsigned glyph) {
    for (const GuiSignLift& s : gui_sign_axis(f.face).signs) {
        if (s.glyph == glyph)
            return s.lift_em * gui_face_em_px(f.face) * gui_font_scale(f);
    }
    return 0.0;
}

// THE FACE'S CAIRO SCALED FONT at this scale (Nimbus at its em times the
// scale, SLIGHT): borrowed, owned by the face owner and cached on the scale;
// GUI thread only. text_shape shapes and paints every glyph on it.
cairo_scaled_font_t* gui_outline_scaled_font(const GuiFont& f);

// THE TRACKING, in WINDOWS PX per glyph (architect 2026-10-06): Nimbus keeps
// its 100 % width but its letters are PULLED TOGETHER — every advance
// HarfBuzz returns is shortened by 3/16 of a Windows px, the last glyph of a
// run included, so a run's width stays the plain sum of its advances
// (text_shape.h) and a right-aligned run's right edge stays honest. It
// scales with the font like every chrome length and is NOT ROUNDED — it is
// an advance, not a grid point: -0.26 device px at the laptop's 138 %, -0.52
// at the tablet's 275 %, -0.75 at 400 %. Judged at 400 on the tablet against
// MS Sans Serif's pixel fitting drawn x4: -0.75 device px there was "almost
// an identical match", -1 brought his long line exactly to its width and was
// a touch tight. ONE CONSTANT FOR THE THREE FACES (the body, the caption's
// bold, the ruler's small). The font files themselves are unmodified
// (fonts/README.md).
inline constexpr double kGuiTrackingPx = -0.1875;
// The tracking in DEVICE px at the font's scale, unrounded.
inline double gui_tracking_px(const GuiFont& f) {
    return kGuiTrackingPx * gui_font_scale(f);
}

// AN UPPER BOUND ON ONE GLYPH'S ADVANCE at the font's scale, in device px:
// Nimbus's widest (its hhea maximum) at its em. A cull bound over-estimates
// by design; no layout reads this. The tracking is not subtracted: being
// negative, it only shortens a real advance, so the untracked figure stays
// an upper bound.
double gui_font_advance_bound_px(const GuiFont& f);

// THE LINUX BINARY'S COPY OF THE TWO FILES, in kGuiFontFiles' order,
// defined by gui_font_embedded.cpp, which only the Linux target compiles (the
// APK carries the same files as assets instead).
extern const GuiFontBytes gui_font_embedded_files[kGuiFontFileCount];
