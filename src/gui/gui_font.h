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
// TWO FACE SETS, ONE LIVE (architect 2026-10-06): a set is one chrome
// vocabulary's text — which file each of the three uses (GuiFace) is drawn
// from, its recorded vertical metrics, its tracking and whether its four math
// signs are lifted (GuiFaceSet below). All four files are installed on both
// devices; the live set's three uses pick among them.
//   - THE REACTOS SET, LIVE: WINE TAHOMA as ReactOS 0.4.16 ships it
//     (tahoma.ttf, tahomabd.ttf; TrueType outlines on a Bitstream Vera base),
//     the face the ReactOS captures are set in — its 11-ppem strike equals
//     tmp/reactos.png's menu and title pixels exactly (measured 2026-10-06).
//     The body is Tahoma, the bold (the caption's title alone) Tahoma Bold,
//     the small (the ruler's labels alone) Tahoma at the smaller em.
//   - THE WIN95 SET: NIMBUS SANS (URW's Helvetica of the base35 set, the
//     genre MS Sans Serif was drawn from; OpenType CFF outlines), Regular for
//     the body and the small, Bold for the title — the look of 2026-10-06's
//     morning, kept byte-identical for the `win95` vocabulary.
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
// at 96 dpi — Tahoma 8's 13-px cell with its 8-row cap, or MS Sans Serif 8's
// with its 9-row one — which is why they seat everything (CLAUDE.md's
// rounding doctrine: every chrome length is a Windows px).
//
// THE EM MATCHES THOSE METRICS VERTICALLY (architect 2026-10-05), derived at
// the install from the face's own measured ink (gui_face_em_px): the body's
// is the em at which the body file's "H" stands as tall as the recorded cap,
// the bold's the bold file's "H" against the same cap, the small's the body
// file's "0" ink against the recorded digit. Measured 2026-10-06: the ReactOS
// set 11.003, 10.996 and 7.943 Windows px (Tahoma's "H" 1489 of 2048, Tahoma
// Bold's 1490, Tahoma's "0" 1547 — 8 pt at GDI's 11 ppem, Tahoma's own 8-pt
// size); the win95 set 12.35, 12.35 and 9.38 (Nimbus's "H" 729 of 1000 in
// both weights, its "0" 746). The em is vertical alone: horizontally the
// face is its own, and a taller or deeper glyph ("$", a descender) may poke
// past a box sized off the recorded rows — accepted (architect 2026-10-05).
// A FACE'S OWN LINE BOX IS NEVER READ (Nimbus's hhea ascent, 729 of 1000, is
// its "H"'s own height, so a program that seats a line on it puts the caps
// at the box's top; Tahoma's is 2049 of 2048): the recorded metrics seat
// every run. HORIZONTALLY THE FACE IS ITS OWN: every width used for layout
// is the shaped run's (text_shape), the face's own advances at the live size
// less the tracking. VERTICALLY THE ONE EXCEPTION IS THE FOUR MATH SIGNS
// ("+", "=", "<", ">"), each set onto the hyphen's axis where the set says
// so (gui_sign_axis below, architect 2026-10-06).

#include <cairo/cairo.h>

#include <cstddef>
#include <cstdint>

enum class GuiFace { Body, Bold, Small };
inline constexpr std::size_t kGuiFaceCount = 3;

// THE FOUR FILES, THE INSTALL'S ONE ORDER: the Linux font step
// (CMakeLists.txt) and the APK's asset step (android/app/build_apk.sh) list
// the same names, the two backends hand the bytes in in this order, and a
// set names its files by their index here.
inline constexpr std::size_t kGuiFontFileCount = 4;
inline constexpr const char* kGuiFontFiles[kGuiFontFileCount] = {
    "NimbusSans-Regular.otf",     // 0: the win95 set's body and small
    "NimbusSans-Bold.otf",        // 1: the win95 set's bold
    "tahoma.ttf",                 // 2: the ReactOS set's body and small
    "tahomabd.ttf",               // 3: the ReactOS set's bold
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
// requires, and the live set's faces carry the glyph each em is measured on
// and, where the set lifts its signs, the five its sign axis is measured on
// (the hyphen and the four math signs, gui_sign_axis). Its producer is
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
// metrics; the tracking in Windows px per glyph; whether the four math signs
// are set onto the hyphen's axis.
struct GuiFaceSet {
    std::size_t    file[kGuiFaceCount]    = {};
    GuiFaceMetrics metrics[kGuiFaceCount] = {};
    double         tracking_px            = 0.0;
    bool           sign_lift              = false;
};

// THE REACTOS SET (architect 2026-10-06), live.
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
//   138 %, -0.14 at the tablet's 275 %, -0.15 at 300 %, -0.2 at 400 %.
//   SIGNS: lifted as derived, which comes to nearly nothing (gui_sign_axis).
inline constexpr GuiFaceSet kGuiFaceSetReactOS = {
    .file        = {2, 3, 2},
    .metrics     = {{11, 2, 8}, {11, 2, 8}, {6, 0, 6}},
    .tracking_px = -0.05,
    .sign_lift   = true,
};

// THE WIN95 SET (architect 2026-10-06), the `win95` vocabulary's.
//   METRICS: the body and the bold are MS Sans Serif 8's 13-px cell at
//   96 dpi — ascent 11 + descent 2, caps and digits 9 — as its pixel trace,
//   Cronyx Helvetica, carried them; the small is Small Fonts' 7-row digit, a
//   cell all above the baseline, as ACID and Vegas drew their rulers in it
//   (read off the architect's screenshots, 2026-10-05).
//   TRACKING: -3/16 Windows px per glyph — Nimbus keeps its 100 % width but
//   its letters are PULLED TOGETHER: -0.26 device px at the laptop's 138 %,
//   -0.52 at the tablet's 275 %, -0.75 at 400 %. Judged at 400 on the
//   tablet against MS Sans Serif's pixel fitting drawn x4: -0.75 device px
//   there was "almost an identical match", -1 brought his long line exactly
//   to its width and was a touch tight.
//   SIGNS: lifted onto the hyphen's axis (gui_sign_axis).
inline constexpr GuiFaceSet kGuiFaceSetWin95 = {
    .file        = {0, 1, 0},
    .metrics     = {{11, 2, 9}, {11, 2, 9}, {7, 0, 7}},
    .tracking_px = -0.1875,
    .sign_lift   = true,
};

// THE LIVE SET: the ReactOS set (architect 2026-10-06). One constexpr for
// now; the ChromeSpec step reads it off the `chrome=` vocabulary instead.
inline constexpr const GuiFaceSet& kGuiLiveFaceSet = kGuiFaceSetReactOS;

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

// THE FOUR MATH SIGNS SIT ON THE HYPHEN'S AXIS (architect 2026-10-06, "as
// MS Sans Serif had them"): where the live set lifts its signs, "+"
// (U+002B), "=" (U+003D), "<" (U+003C) and ">" (U+003E) each rise by the
// hyphen's ink centre less its own, in every face and every run; the hyphen
// and every other glyph stay where the face drew them. One rule: the
// period's faces put the five marks on ONE axis, and the lift seats a face's
// signs there.
//   THE WIN95 SET: MS Sans Serif centred all five at 0.389 of its 9-row cap,
//   while Nimbus keeps its hyphen near there (240..312 of 729, centre 0.379)
//   but draws the four signs on Helvetica's math axis, 0.318 ("+" -10..474,
//   "=" 111..353, "<" ">" -9..474), so its plus sat visibly low beside the
//   digits it signs; every letter and digit is within 0.35 Windows px of MS
//   Sans Serif's. The lifts: the regular file's 0.044 em ("+", "=") and
//   0.0435 ("<", ">"), the bold's 0.043 and 0.0425 — the plus rising, at
//   400 %, 2.17 device px in the body, 2.12 in the bold and 1.65 in the small.
//   THE REACTOS SET: Tahoma already draws the five on one axis. Its 11-ppem
//   strike centres "-" (row 3 above the baseline), "+", "<", ">" (rows 0..6)
//   and "=" (rows 2..4) all on row 3 of the 8-row cap, and Tahoma Bold's
//   strike the same but for its "=" (rows 2..5, half a row higher); the
//   outlines agree to a few units — the hyphen 566..730 of 2048 (centre
//   648), "+" and "=" centred at 652, "<" ">" at 654; the bold within one
//   unit. So the lift stays on, as derived, and comes to nearly nothing:
//   -0.0020 em ("+", "=") and -0.0029 ("<", ">") in the regular, within
//   0.0002 in the bold — the plus dropping 0.09 device px at 400 % in the
//   body (measured 2026-10-06).
// THE LIFTS ARE DERIVED at the install, like the em, from the face's own
// unscaled outline bounds (gui_font_bundled.cpp), in em units. THE MATCH IS
// BY GLYPH ID after substitution (text_shape compares a shaped glyph's id
// against `glyph`), never by codepoint: a cluster is not a glyph. The lift
// moves the ink alone — every advance, the run's width and the tracking are
// untouched. A set that does not lift leaves every lift 0.
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

// THE FACE'S CAIRO SCALED FONT at this scale (the use's file at its em times
// the scale, SLIGHT, outlines only): borrowed, owned by the face owner and
// cached on the scale; GUI thread only. text_shape shapes and paints every
// glyph on it.
cairo_scaled_font_t* gui_outline_scaled_font(const GuiFont& f);

// THE LIVE SET'S TRACKING, in WINDOWS PX per glyph (each set's figure and
// its measurements at the set above): every advance HarfBuzz returns is
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

// THE LINUX BINARY'S COPY OF THE FOUR FILES, in kGuiFontFiles' order,
// defined by gui_font_embedded.cpp, which only the Linux target compiles (the
// APK carries the same files as assets instead).
extern const GuiFontBytes gui_font_embedded_files[kGuiFontFileCount];
