#pragma once

// THE ONE FACE OWNER: every text surface names its face here and nowhere
// else, so the painters never name a font. THE PRODUCT'S FACE IS ITS OWN
// (architect 2026-10-02: "the app becomes its own thing; it has its own
// fonts"): LIBERATION SANS 2.1.5, Regular and Bold (fonts/README.md has the
// provenance and the licence), shipped as the APK's assets (the one carrier,
// architect 2026-10-30) and turned into faces by ONE implementation,
// gui_font_bundled.cpp, through FreeType. Nothing is asked for a face: the
// tablet has no fontconfig to consult. ONE FACE, HARD-CODED, no device key
// (architect 2026-10-12; no other family is proposed,
// docs/engineering/closed_questions.md).
//
// THE SIZES ARE ONE PIXEL TABLE: kGuiFaceSizes below, each use's EM IN W,
// SCALED WITH gui_scale and rounded at the element (size_px =
// nearbyint(em_w · gui_scale / 100), the scaled_px road; one W = 2 device
// px on the tablet at its 200 %, render.h's scaled_px). THE SIZES ARE WHOLE
// W. EVERY TEXT IS 12 W, ARC'S 9 pt (architect 2026-11-18: "set it
// so that the font is actually at 9 point"; one Arc px is one W, Arc being
// drawn for 9 pt = 12 px) — 24 device px on the tablet — BUT THE RULER'S
// DIGITS, 10 W (20 device px; Arc's `font-size: smaller`, 12 / 1.2: "the
// ruler, we should make the ruler font small still"). No other size on the
// main page. THE PROGRESS PAGE'S COUNT TAKES GTK'S "small" (2026-11-19):
// CAPTION 10 W (Pango's "small", 12 / 1.2). A question's
// and the progress page's sentence are plain Body text, no larger face
// (architect 2026-11-20: GTK's "large" is a popup's — "the oversized text
// is the clue").
// THE USES: BODY 12 (the lists, the cards, a question's and the progress
// page's sentence, the value sub-page's entry, the word buttons,
// the NOISE number, the status bar), PROGRAM 12 (the
// clocks — the bottom icon row's timestamp and the render player's two
// time labels — and the tooltip's name line), LABEL 12 (the cues'
// labels and the lane's editors, the history lane's labels with them),
// HINT 12 (the tooltip's hint line), BOLD 12 (THE TITLE BAR: the brand and
// its right group, render.h's title bar block), SMALL 10 (the ruler's
// digits), KEYS 12 (the on-screen keyboard's caps — the default of the
// one LIVE size, the `osk_font_px` tuning key's, GuiFont below) and
// CAPTION 10 (the progress page's count, above). The rows
// coinciding in number stay rows of their own: the roster is the
// distinction.
// TWO WEIGHTS (architect 2026-11-19: "yes, regular is good"): THE
// REGULAR FOR BODY TEXT — Body, Program, Label, Hint, Caption, and the
// on-screen keyboard's caps (Keys, 2026-11-19) — and THE
// BOLD FOR THE TITLE BAR (Bold) AND
// THE RULER'S DIGITS (Small), as GTK Arc sets them (xfwm's title bold,
// every other text regular).
// Every length authored round the text follows these sizes (each length's
// own comment records its class).
//
// THE LINE BOX (architect 2026-11-19; GTK's seat): what Pango lays a
// line in — the face's ascent and descent AT THE SIZE IN W, each rounded UP
// to a whole W (FreeType's FT_Request_Size: the ascender ceiled, the
// descender floored), gui_font_line_box_w below. Liberation Sans Regular and
// Bold carry one hhea (ascender 1854, descender -434 of 2048 units): 10.86 +
// 2.54 at 12 W, so 11 + 3 = 14 W; 9.05 + 2.12 at 10 W, so 10 + 3 = 13 W.
// render.h's line_box_baseline seats a text by it in a box of whole W.
//
// THE VERTICAL METRICS ARE THE FACE'S OWN AT THAT PIXEL SIZE (architect
// 2026-10-23: the face's own box is the truthful one): FreeType's
// ascender, descender and line height (the
// file's hhea, read once per file at the install, in em) times the em,
// each rounded to whole device px (gui_font_ascent_px, gui_font_descent_px,
// gui_font_line_px). A text stands on the line-box rule's baseline
// (line_box_baseline) but the on-screen keyboard's caps, whose cap band
// redesign_baseline centers on the key, the cap the em times the file's own
// "H" ink per em (gui_font_cap_px). Horizontally the face keeps
// its own true advances, TRACKING 0 (the heights-only rule's "no manual
// kerning — let the spacing be set by the font", architect 2026-10-09), the
// one shaping chokepoint unchanged (text_shape.h) — the face's own kerning
// on every surface but the time fields, whose runs shape with `kern` off
// (text_shape::Kerning, architect 2026-11-05: the pair "11" kerned the
// digits into a walk; paint_handler.cpp's kTimeShape block).
//
// EVERY FACE DRAWS ITS ANTIALIASED OUTLINE AT EVERY SIZE, NEVER AN EMBEDDED
// BITMAP STRIKE (architect 2026-10-06): the install hides any strike from
// FreeType, in every file, and every glyph load passes FT_LOAD_NO_BITMAP
// (gui_font_bundled.cpp's head). Every run is shaped by HarfBuzz and
// painted under SLIGHT hinting through the raster road, each glyph at its
// exact place (text_shape.h, text_raster.h).
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

// THE USES (the head's table): the body (`Body`), the title bar (`Bold`), the
// ruler's digits (`Small`), the clocks and the
// tooltip's name line (`Program`), the on-screen keyboard's caps (`Keys`),
// the tooltip's hint line (`Hint`), the cues' labels with the lane's
// editors (`Label`) and the progress page's count (`Caption`).
enum class GuiFace { Body, Bold, Small, Program, Keys, Hint, Label, Caption };
inline constexpr std::size_t kGuiFaceCount = 8;

// THE TWO FILES, THE INSTALL'S ONE ORDER (architect 2026-10-12, Liberation
// Sans alone): THIS LIST IS THE ONE PLACE THE NAMES ARE SPELLED - the APK's
// asset step (apk/build_apk.sh) reads the quoted names out of this
// initializer, so a file added here is packed with no second list; the
// backend hands the bytes in in this order, and a use names its file by its
// index here — the Regular read by the body text and the keyboard's caps,
// the Bold by the title bar and the ruler's digits (the head). Both
// are TrueType
// (FT_New_Memory_Face).
inline constexpr std::size_t kGuiFontFileCount = 2;
inline constexpr const char* kGuiFontFiles[kGuiFontFileCount] = {
    "LiberationSans-Regular.ttf", // 0: the body text
    "LiberationSans-Bold.ttf",    // 1: the title bar, the ruler
};

// THE PIXEL TABLE (the head): per use, in GuiFace's order, the file it is
// drawn from and its em in W (one W = one px at 100 %, 2 device px on the
// tablet at 200 %; render.h's scaled_px): 12 W everywhere but the ruler's
// 10 and the progress page's count, the Regular for body text and the
// keyboard's caps, the Bold for the title bar and the
// ruler (architect 2026-11-18 / 2026-11-19).
struct GuiFaceSize {
    std::size_t file = 0;
    double      em_w = 0.0;
};
inline constexpr GuiFaceSize kGuiFaceSizes[kGuiFaceCount] = {
    {0, 12.0},   // Body: the lists, the cards, the pages, the status bar
    {1, 12.0},   // Bold: the title bar (the brand and its right group)
    {1, 10.0},   // Small: the ruler's digits
    {0, 12.0},   // Program: the clocks, the tooltip's name
    {0, 12.0},   // Keys: the on-screen keyboard's caps (the default of
                 // the LIVE size, the `osk_font_px` key's: GuiFont below)
    {0, 12.0},   // Hint: the tooltip's hint line
    {0, 12.0},   // Label: the cues' labels and the lane's editors
    {0, 10.0},   // Caption: the progress page's count (GTK's "small",
                 // 12 / 1.2)
};

// THE USE'S TABLE SIZE, its em in W — the one read of the table's sizes,
// which GuiFont's default and render.h's gui_font(face) take.
inline constexpr double gui_font_table_px(GuiFace face) {
    return kGuiFaceSizes[static_cast<std::size_t>(face)].em_w;
}

struct GuiFontBytes {
    const uint8_t* data = nullptr;
    size_t         len  = 0;
};

// THE ONE SETUP CALL, with ONE CALLER, once before the first paint: the
// backend (install_fonts_or_die, platform_android.cpp) hands in the files it
// read out of the APK's assets. The bytes are COPIED — the caller may free
// or unmap them the moment it returns.
//
// THE RETURN IS THE INSTALL OBSERVED, not assumed: true when each of the
// two files selects as an FT-BACKED cairo face, which is what text_shape
// requires, and carries the "H" the cap band is measured on, the "x" the
// marker lane's band centers, the "l" and
// the OS/2 strikeout position the strike is measured on
// (gui_font_strike_band), and the hyphen, the "0" and the five marks the
// sign axis is measured on. Its producer is
// breach-only (the bytes are the repository's own, so a face that fails to
// build is a build defect), and each caller dies on false.
bool gui_font_install_bundled(const GuiFontBytes (&files)[kGuiFontFileCount]);

// A FACE AT A SCALE AND A SIZE: what every text consumer holds, the
// successor of a cairo scaled font. `percent` is gui_scale and `em_w` the
// use's em in W. THE SIZE TRAVELS IN THE VALUE because
// ONE USE'S SIZE IS LIVE: the Keys face's, the `osk_font_px` tuning key's
// (architect 2026-10-29, "font size osk config tmp key"; render.h's
// seat) — the table's 12 its default — so every cache keyed on a GuiFont
// (the scaled font, gui_font_bundled.cpp's gui_outline_scaled_font, keyed on
// the em; text_shape's run memo, keyed on the size's bytes) keys on the size
// too, and a committed size re-shapes with no cache of its own to drop.
// Every other use's size is the table's. THE ONE CONSTRUCTOR ROAD is
// render.h's gui_font(face), which fills the size from the live seat for
// Keys and from gui_font_table_px for every other face; nothing else builds
// a GuiFont. The member defaults are the default face's own table size, so
// even a default-constructed value is a truthful Body at 100 %, never a
// wrong size. When he rules the number the size leaves the value again.
struct GuiFont {
    GuiFace face    = GuiFace::Body;
    int     percent = 100;
    double  em_w    = gui_font_table_px(GuiFace::Body);
};

// THE USE'S EM IN DEVICE PX at the font's scale: its em in W times gui_scale
// over 100, rounded at the element, floored at one px (the scaled_px
// conversion in the double domain: 22 on the tablet at 200 % for an 11-W
// face).
inline double gui_font_em_px(const GuiFont& f) {
    const double v = std::nearbyint(
        f.em_w * static_cast<double>(f.percent) / 100.0);
    return v < 1.0 ? 1.0 : v;
}

// THE FACE'S OWN VERTICAL METRICS (the head) in DEVICE px at the font's
// scale: the ascent above the baseline, the descent below it, and the line
// height (the file's ascender, descender and line gap). Out of line: the
// file's metrics are the install's. TWO FORMS OF EACH (architect
// 2026-11-15, the no-grid arc; render.h's hull holds the rule):
//   THE WHOLE FORM, rounded to whole px — A SIZE FOR LAYOUT ALONE: a row's
//     pitch, a band's or a field's height, a box other things are laid out
//     against. Never a placement.
//   THE EXACT FORM (`_exact_px`), the em times the file's metric,
//     unrounded — what every PLACEMENT reads: a baseline, a band's top
//     under a baseline, a line's step, the hull of a line box.
double gui_font_ascent_px(const GuiFont& f);
double gui_font_descent_px(const GuiFont& f);
double gui_font_line_px(const GuiFont& f);
double gui_font_ascent_exact_px(const GuiFont& f);
double gui_font_descent_exact_px(const GuiFont& f);
double gui_font_line_exact_px(const GuiFont& f);
// THE LINE BOX IN W (the head's THE LINE BOX; architect 2026-11-19):
// the face's ascent and descent at the use's em IN W — its size at 100 % —
// each rounded up to a whole W, as FreeType's size request rounds them for
// Pango; the line box their sum. 11 + 3 at 12 W, 10 + 3 at 10 W (Liberation
// Sans, both files). Out of line: the file's metrics are the install's.
struct GuiLineBoxW {
    int ascent  = 0;
    int descent = 0;
    int line() const { return ascent + descent; }
};
GuiLineBoxW gui_font_line_box_w(const GuiFont& f);
// THE CAP BAND in device px at the font's scale, unrounded — what
// redesign_baseline centers on an on-screen keyboard key (paint_handler.cpp):
// the em times the use's file's own "H" ink per em.
double gui_font_cap_px(const GuiFont& f);

// THE INK BAND OF A GLYPH SET (architect 2026-11-03, the text round: "the
// digits' ink centered on the ruler"; "logo should be centered in logo
// lane"): THE ROWS THE PAINT INKS — how many whole rows of the set's painted
// ink stand above the baseline row's top edge (`above`) and below it
// (`below`, downward), the set painted as one run through the shaping
// chokepoint on the painter's own road (text_shape's shape_text_run and
// show_shaped_run: the use's SLIGHT-hinted scaled font, gui_outline_scaled_font,
// each lifted mark, kGuiLiftedSigns below, at the fractional seat the
// shaper gives it) at an INTEGER baseline onto a scratch A8 surface, a row
// counted when any pixel of it holds coverage; `above + below` is the set's
// row count. RASTERIZED, NOT READ OFF GLYPH EXTENTS (2026-11-03):
// grid-fitted extents are boxes taken before the lifted pipe's
// fractional seat and each glyph's own fractional place, so they overstate
// it (the
// pipe in Regular 24 among digits: extents 20 + 3, the paint
// 19 + 3), and
// the design's unhinted bounds rounded miss the
// hinter's own rows (Bold 12: 8.38 rounds to 8, the paint 9). Its readers:
// the title bar's right group (the alphabet of its runs in the Bold 12 W,
// the cap band the battery and the lamps stand on — render.h's
// title bar block). A reader takes the integers as they are.
// Measured once per file,
// em and set and kept (a set is a constant of its reader's), GUI thread
// only; a character the face lacks paints .notdef, so a set names only
// characters the face carries; a set with no ink is 0 + 0.
struct GuiInkBand {
    int above = 0;
    int below = 0;
};
GuiInkBand gui_font_ink_band(const GuiFont& f, std::string_view set);

// THE STRIKE'S BAND (architect 2026-10-26: "have coincident markers
// be shown as a strike through … They become one"; "it's the same thickness
// as the regular — for example, a T would have the same thickness"): the
// rows of the bar a struck cue label wears through its run (show_cue_label,
// render.cpp, the one painter), relative to the run's baseline —
// [baseline + dy, baseline + dy + h), REAL EDGES (architect 2026-11-15, the
// no-grid arc: nothing rounded between the face's metrics and the bar; the
// painters lay it at exact coverage). THE THICKNESS IS THE FACE'S STROKE AT
// THE SIZE: the use's file's "l" — one vertical stem — measured across its
// unscaled outline (xMax − xMin, per em, read at the install) times the em.
// THE HEIGHT IS THE FACE'S OWN
// STRIKEOUT POSITION (the file's OS/2 yStrikeoutPosition, per em, read at
// the install): the band centered on it above the baseline. The cues'
// labels are the Label face, the Regular file (2026-11-19);
// for the record, for Liberation Sans Regular (the "l" 180 of 2048 units, the strikeout 530):
// at a 21-px em a stroke of 1.85 px, centered 5.43 above the baseline; at
// 14 px 1.23, 3.62 above; at 10 px 0.88, 2.59 above.
struct GuiStrikeBand {
    double dy = 0.0;   // the band's top edge less the baseline (negative: up)
    double h  = 0.0;   // the band's height, the stroke
};
GuiStrikeBand gui_font_strike_band(const GuiFont& f);

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
//   THE PIPE SITS ON THE DIGITS' BAND (architect 2026-10-09: "the
//   pipe should be even"): "|" (U+007C) takes the "0"'s ink centre, so its
//   overhang above and below the digits beside it is the same — wherever
//   the face shows one (the labels' grammar, a typed line, the status bar's
//   field separator, and the title bar's right group's text pipe —
//   render.h's title bar block).
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
// the em (the scale and the size, GuiFont above); GUI thread only.
// text_shape shapes and paints every glyph on it.
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

