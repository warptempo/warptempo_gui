#pragma once

// THE ONE FACE OWNER: every text surface names its face here and nowhere
// else, so the painters never name a font. THE PRODUCT'S FACE IS ITS OWN
// (architect 2026-10-02: "the app becomes its own thing; it has its own
// fonts"): LIBERATION SANS 2.1.5, Regular and Bold (fonts/README.md has the
// provenance and the licence), carried by both binaries - compiled into the
// Linux executable (gui_font_embedded.cpp), shipped as the APK's assets on
// Android - and turned into faces by ONE implementation on both devices,
// gui_font_bundled.cpp, through FreeType. Neither device asks anything for a
// face: fontconfig is never consulted on the laptop and the tablet has
// nothing to consult. ONE FACE, HARD-CODED (architect 2026-10-12; the Tahoma
// and FreeSans sets and the `font` device key went that day,
// docs/engineering/closed_questions.md).
//
// THE SIZES ARE ONE PIXEL TABLE (architect 2026-10-23, the hi-fi skin:
// "whatever size the flags are, the timestamp and the tab text should take
// the same size … the caption the same size as the flag marker font"; "the
// timestamps are good"; "ok to use the same square icons with 14px font"):
// kGuiFaceSizes below, each use's EM IN DEVICE PX AT THE TABLET'S 300 %,
// SCALED WITH gui_scale and rounded at the element (the amendment the same
// night: size_px = nearbyint(authored · gui_scale / 300), the scaled_px
// road) — PROGRAM 26 Regular (architect 2026-10-26 ~11:30, the cue
// label's size tuning settled: "text 26 … the bottom left timestamp and tab/github/etc
// text should take 26 as well"): THE CUES' LABELS, THE LANE'S EDITORS (the
// flag editor, the iteration bound editor), ROW 8'S CLOCK AND ITS STATE
// LINE (the tab's name, the GitHub word, the walk line, the progress line),
// THE RENDER PLAYER'S TWO TIME FIELDS (architect 2026-10-26 ~12:10, "yes"
// to symmetry with row 8's clock; the body's until then) and THE TOOLTIP
// ("the tooltips font size should be the same as the flags", 2026-10-23);
// BODY 22 Regular, THE REST (the lists, the cards, the prompts, the dialog
// editors, the logo lane's battery + clock legend); TITLE 24 Bold (the logo lane's brand "Warptempo" alone since
// 2026-10-24, the laptop's caption title beside it that morning; architect 2026-10-24, "the caption size
// 24", off the logo mocks — 22 from 2026-10-23
// until then), RULER 16 Regular (the ruler's digits alone; architect
// 2026-10-24, "R16" off 14 / 15 / 16 / 17 on his type sheet — "we took
// them both too far": the night's R17, off 15 / 17 / 19 / 21, stood
// 2026-10-23 to 2026-10-24, the skin's first night 15), LEGEND 14
// Regular (the keys' printed legends alone; the tooltip wore it the first
// night of the skin), KEYS 24 Regular (the on-screen keyboard's caps alone —
// the letters, the symbol pages' characters and the word caps, "a separate
// face"; architect 2026-10-24, "C24" off 22 / 24 / 26 / 28 on
// his type sheet — the night's C34, off 22 / 28 / 34 / 40, stood
// 2026-10-23 to 2026-10-24, the caps wearing the body's 22 before it). (The
// three recorded Windows metric sets — MS Sans Serif 8's {11, 2, x 6} cell,
// the ruler's six-row digit, Cool Edit's cue cap of 7 — and the ems derived
// to stand their ink those rows tall, 2026-10-05 to 2026-10-23, are git
// history.)
//
// THE VERTICAL METRICS ARE THE FACE'S OWN AT THAT PIXEL SIZE (architect
// 2026-10-23, REVERSING the rule of 2026-10-06 that a face's line box is
// never read: with no Windows cell left to match, the face's own box is the
// truthful one): FreeType's ascender, descender and line height (the
// file's hhea, read once per file at the install, in em) times the em,
// each rounded to whole device px (gui_font_ascent_px, gui_font_descent_px,
// gui_font_line_px). A text's cap band is still centered by
// redesign_baseline's rule on its seat's rows, the cap the em times the
// file's own "H" ink per em (gui_font_cap_px). Horizontally the face keeps
// its own true advances, TRACKING 0 (the heights-only rule's "no manual
// kerning — let the spacing be set by the font", architect 2026-10-09), the
// one shaping chokepoint unchanged (text_shape.h).
//
// EVERY FACE DRAWS ITS ANTIALIASED OUTLINE AT EVERY SIZE, NEVER AN EMBEDDED
// BITMAP STRIKE (architect 2026-10-06): the install hides any strike from
// FreeType, in every file, and every glyph load passes FT_LOAD_NO_BITMAP
// (gui_font_bundled.cpp's head). Every run is shaped by HarfBuzz under
// SLIGHT hinting and painted through cairo_show_glyphs (text_shape.h).
//
// THE FOUR MATH SIGNS AND THE PIPE ARE LIFTED (gui_sign_axis below): the
// signs onto the hyphen's axis, downward (Arial's metrics draw them above
// it), the pipe onto the digits' band.


#include <cairo/cairo.h>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

// THE USES (the head's table): the body, the brand's bold, the ruler's
// digits (`Small`), the cues' labels and their kin (`Program`: the lane's
// editors, row 8's clock and state line, the player's clocks, the
// tooltip), the keys' legends
// and the on-screen keyboard's caps (`Keys`).
enum class GuiFace { Body, Bold, Small, Program, Legend, Keys };
inline constexpr std::size_t kGuiFaceCount = 6;

// THE TWO FILES, THE INSTALL'S ONE ORDER (architect 2026-10-12, Liberation
// Sans alone): THIS LIST IS THE ONE PLACE THE NAMES ARE SPELLED - the Linux
// font step (CMakeLists.txt) and the APK's asset step
// (android/app/build_apk.sh) each read the quoted names out of this
// initializer, so a file added here is embedded and packed with no second
// list; the two backends hand the bytes in in this order, and a use names
// its file by its index here. Both are TrueType (FT_New_Memory_Face).
inline constexpr std::size_t kGuiFontFileCount = 2;
inline constexpr const char* kGuiFontFiles[kGuiFontFileCount] = {
    "LiberationSans-Regular.ttf", // 0: the body, the ruler, the cues, the legends, the caps
    "LiberationSans-Bold.ttf",    // 1: the brand
};

// THE PIXEL TABLE (the head; architect 2026-10-23 and the amendment): per
// use, in GuiFace's order, the file it is drawn from and its em in device
// px at gui_scale 300 %.
struct GuiFaceSize {
    std::size_t file      = 0;
    double      px_at_300 = 0.0;
};
inline constexpr GuiFaceSize kGuiFaceSizes[kGuiFaceCount] = {
    {0, 22.0},   // Body
    {1, 24.0},   // Bold: the brand
    {0, 16.0},   // Small: the ruler's digits
    {0, 26.0},   // Program: the cues' labels, the lane's editors, row 8's
                 // clock and state line, the player's clocks, the tooltip
    {0, 14.0},   // Legend: the keys' legends
    {0, 24.0},   // Keys: the on-screen keyboard's caps
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
// requires, and carries the "H" the cap band is measured on and the hyphen,
// the "0" and the five marks the sign axis is measured on. Its producer is
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

// THE USE'S EM IN DEVICE PX at the font's scale: its table size times
// gui_scale over 300, rounded at the element, floored at one px.
inline double gui_font_em_px(const GuiFont& f) {
    const double v = std::nearbyint(
        kGuiFaceSizes[static_cast<std::size_t>(f.face)].px_at_300 *
        static_cast<double>(f.percent) / 300.0);
    return v < 1.0 ? 1.0 : v;
}

// THE FACE'S OWN VERTICAL METRICS (the head) in DEVICE px at the font's
// scale, each rounded to whole px: the ascent above the baseline, the
// descent below it, and the line height (the file's ascender, descender
// and line gap). Out of line: the file's metrics are the install's.
double gui_font_ascent_px(const GuiFont& f);
double gui_font_descent_px(const GuiFont& f);
double gui_font_line_px(const GuiFont& f);
// THE CAP BAND in device px at the font's scale, unrounded — what
// redesign_baseline centers in a chrome box (paint_handler.cpp): the em
// times the use's file's own "H" ink per em.
double gui_font_cap_px(const GuiFont& f);
// THE ASCENDER LINE in device px above the baseline at the font's scale,
// unrounded: the em times the use's file's own "l" ink per em (Liberation
// Sans 1484 / 2048 units, 15.9 px at 300 %'s 22) — the top of the
// lowercase ink, which the modal box's text seats under 12 px of plain
// field inside the ring (modal_box.h's modal_box_text_baseline0, 2026-10-23: the air the eye
// sees is the ink's, not the line box's).
double gui_font_ascender_ink_px(const GuiFont& f);

// FIVE MARKS ON ONE RULE, EACH SEATED ON ITS TARGET (kGuiLiftedSigns, one
// table, one reader): a mark rises by its target's ink centre less its own,
// in every use and every run; the targets and every other glyph stay where
// the face drew them.
//   THE FOUR MATH SIGNS SIT ON THE HYPHEN'S AXIS: "+" (U+002B), "="
//   (U+003D), "<" (U+003C) and ">" (U+003E) take the hyphen's ink centre.
//   Liberation Sans keeps its hyphen at 0.386 of its cap but draws the four
//   on Arial's math axis, higher, so the lift is NEGATIVE, downward (the
//   planner's measurement in font units, 2026-10-09). NOTHING CLAMPS A LIFT
//   AT 0: the install stores the signed difference and the one reader adds
//   it whatever its sign.
//   THE PIPE SITS ON THE DIGITS' BAND (architect 2026-10-09 ~21:20, "the
//   pipe should be even"): "|" (U+007C) takes the "0"'s ink centre, so its
//   overhang above and below the digits beside it is the same.
// THE LIFTS ARE DERIVED at the install from each file's own unscaled
// outline bounds (gui_font_bundled.cpp), in em units. THE MATCH IS BY GLYPH
// ID after substitution (text_shape compares a shaped glyph's id against
// `glyph`), never by codepoint. The lift moves the ink alone — every
// advance and the run's width are untouched.
enum class GuiSignTarget { HyphenAxis, DigitBand };
struct GuiLiftedSign {
    char32_t      codepoint;
    GuiSignTarget target;   // whose ink centre the mark is seated on
};
inline constexpr std::size_t kGuiSignCount = 5;
inline constexpr GuiLiftedSign kGuiLiftedSigns[kGuiSignCount] = {
    {U'+', GuiSignTarget::HyphenAxis},
    {U'=', GuiSignTarget::HyphenAxis},
    {U'<', GuiSignTarget::HyphenAxis},
    {U'>', GuiSignTarget::HyphenAxis},
    {U'|', GuiSignTarget::DigitBand},
};
struct GuiSignLift {
    unsigned glyph   = 0;    // the face's glyph id for the mark
    double   lift_em = 0.0;  // up-positive, in em
};
struct GuiSignAxis {
    GuiSignLift signs[kGuiSignCount] = {};  // in kGuiLiftedSigns' order
};
// The axis of `face`'s file: its measured lifts.
const GuiSignAxis& gui_sign_axis(GuiFace face);

// The lift of one shaped glyph in DEVICE px at the font's scale, HarfBuzz's
// sense (up-positive), unrounded: the mark's lift times the em, 0 for every
// glyph that is not one of the five marks. text_shape's one reader.
inline double gui_sign_lift_px(const GuiFont& f, unsigned glyph) {
    const GuiSignAxis& axis = gui_sign_axis(f.face);
    for (std::size_t i = 0; i < kGuiSignCount; ++i) {
        const GuiSignLift& s = axis.signs[i];
        if (s.lift_em != 0.0 && s.glyph == glyph)
            return s.lift_em * gui_font_em_px(f);
    }
    return 0.0;
}

// THE FACE'S CAIRO SCALED FONT at this scale (the use's file at its em,
// SLIGHT, outlines only): borrowed, owned by the face owner and cached on
// the scale; GUI thread only. text_shape shapes and paints every glyph on
// it.
cairo_scaled_font_t* gui_outline_scaled_font(const GuiFont& f);

// THE TRACKING, in DEVICE px per glyph: 0 (the head; the mechanism stays,
// one number): every advance HarfBuzz returns is shortened by it, so a
// run's width stays the plain sum of its advances (text_shape.h).
inline double gui_tracking_px(const GuiFont&) {
    return 0.0;
}

// AN UPPER BOUND ON ONE GLYPH'S ADVANCE at the font's scale, in device px:
// the use's file's widest (its hhea maximum) at its em. A cull bound
// over-estimates by design; no layout reads this.
double gui_font_advance_bound_px(const GuiFont& f);

// THE LINUX BINARY'S COPY OF THE TWO FILES, in kGuiFontFiles' order,
// defined by gui_font_embedded.cpp, which only the Linux target compiles (the
// APK carries the same files as assets instead).
extern const GuiFontBytes gui_font_embedded_files[kGuiFontFileCount];
