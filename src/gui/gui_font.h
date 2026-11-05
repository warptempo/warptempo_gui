#pragma once

// THE ONE FACE OWNER: every text surface names its face here and nowhere
// else, so the painters never name a font. THE PRODUCT'S FACE IS ITS OWN
// (architect 2026-10-02: "the app becomes its own thing; it has its own
// fonts"): LIBERATION SANS 2.1.5, Regular and Bold (fonts/README.md has the
// provenance and the licence), shipped as the APK's assets (the one carrier
// since the laptop GUI's drop, architect 2026-10-30) and turned into faces by
// ONE implementation, gui_font_bundled.cpp, through FreeType. Nothing is
// asked for a face: the tablet has no fontconfig to consult. ONE FACE, HARD-CODED (architect 2026-10-12; the Tahoma
// and FreeSans sets and the `font` device key went that day,
// docs/engineering/closed_questions.md).
//
// THE SIZES ARE ONE PIXEL TABLE (architect 2026-10-23, the hi-fi skin:
// "whatever size the flags are, the timestamp and the tab text should take
// the same size … the caption the same size as the flag marker font"):
// kGuiFaceSizes below, each use's EM IN DEVICE PX AT THE TABLET'S 300 %,
// SCALED WITH gui_scale and rounded at the element (the amendment the same
// night: size_px = nearbyint(authored · gui_scale / 300), the scaled_px
// road). THE SIZES ARE WHOLE W (architect 2026-11-04, the unit; the W of
// 2 that night: every authored length a multiple of 2 device px at 300 %,
// the faces' ems included — the hi-fi's 22 / 16 of 2026-11-03's retrofit
// whole W again, the Legend 16 with the Small; pt was considered and not
// taken, gui_scale being the density knob). THE TEXT SIZES (architect
// 2026-11-03, the retrofit, reversing that morning's text round of 24; the
// unit of 2 since 2026-11-04 night): PROGRAM 22 Regular
// (architect 2026-10-26 ~13:05, the size's tuning key read off the tablet's
// config and settled): ROW 8'S CLOCK AND ITS STATE LINE (the tab's name, the
// GitHub word, the walk line, the progress line), THE RENDER PLAYER'S TWO
// TIME FIELDS (architect 2026-10-26 ~12:10, "yes" to symmetry with row 8's
// clock) and THE TOOLTIP'S NAME LINE ("the tooltips font size should be the
// same as the flags", 2026-10-23); LABEL 22 BOLD (architect 2026-11-01
// night, M5i: "THE MARKER LABEL IS BOLD"; Liberation Sans Bold's vertical
// metrics equal Regular's, only the run widens): THE CUES' LABELS AND THE
// LANE'S EDITORS (the flag editor, the iteration bound editor — the
// editor's text stands at the label's seat), the history lane's labels with
// them — the size the Program row's, a row of its own because its file is
// the other one; BODY 22 Regular, THE REST (the lists, the cards, the
// prompts, the dialog editors, the logo lane's battery + clock legend) —
// THE ROWS COINCIDE IN NUMBER AND STAY THEIR OWN ROWS: the roster is the
// distinction (the Label row tunes with the cues, the Body row with the
// rest); TITLE 24 Bold (the logo lane's brand "Warptempo" alone; architect
// 2026-10-24, "the caption size 24", off the logo mocks); RULER 16 BOLD (the
// ruler's digits alone; Bold since 2026-11-01 night, M5c; the retrofit's 16
// of mock g1, 2026-11-03, a whole W again since 2026-11-04 night); LEGEND
// 16 Regular (the keys' printed legends alone, uppercase, under every key;
// back 2026-11-03 with the legends at 14, 16 since 2026-11-04 night, the
// unit of 2, paint_handler.cpp's key_legend_seat); KEYS 24 Regular (the on-screen
// keyboard's caps alone — the letters, the symbols page's characters and
// the word caps, "a separate face"; architect 2026-10-24, "C24" off his type
// sheet) BY DEFAULT, THE ONE LIVE SIZE — the `osk_font_px` tuning key's
// (architect 2026-10-29 night, GuiFont below); HINT 18 Regular (the
// tooltip's hint line alone, under its name line — kdenlive's tooltip, a
// smaller hint under the name, architect 2026-10-31, paint_shift_tooltip).
// Every length authored round the text follows these sizes (each length's
// own comment records its class; the retrofit's set at modal_box.h's text
// seat, folder_overlay.h's row, notifications.h's card and the list glyphs).
// (The three recorded Windows metric sets — MS Sans Serif 8's {11, 2, x 6}
// cell, the ruler's six-row digit, Cool Edit's cue cap of 7 — and the ems
// derived to stand their ink those rows tall, 2026-10-05 to 2026-10-23, are
// git history.)
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
// digits (`Small`), row 8's clock and state line, the player's clocks and
// the tooltip's name line (`Program`), the keys' legends (`Legend`), the
// on-screen keyboard's caps (`Keys`), the tooltip's hint line (`Hint`) and
// the cues' labels with the lane's editors (`Label`).
enum class GuiFace { Body, Bold, Small, Program, Legend, Keys, Hint, Label };
inline constexpr std::size_t kGuiFaceCount = 8;

// THE TWO FILES, THE INSTALL'S ONE ORDER (architect 2026-10-12, Liberation
// Sans alone): THIS LIST IS THE ONE PLACE THE NAMES ARE SPELLED - the APK's
// asset step (apk/build_apk.sh) reads the quoted names out of this
// initializer, so a file added here is packed with no second list; the
// backend hands the bytes in in this order, and a use names its file by its
// index here. Both are TrueType (FT_New_Memory_Face).
inline constexpr std::size_t kGuiFontFileCount = 2;
inline constexpr const char* kGuiFontFiles[kGuiFontFileCount] = {
    "LiberationSans-Regular.ttf", // 0: the body, the clocks, the legends, the caps, the hint
    "LiberationSans-Bold.ttf",    // 1: the brand, the ruler, the cues
};

// THE PIXEL TABLE (the head; architect 2026-10-23 and the amendment): per
// use, in GuiFace's order, the file it is drawn from and its em in device
// px at gui_scale 300 %.
struct GuiFaceSize {
    std::size_t file      = 0;
    double      px_at_300 = 0.0;
};
inline constexpr GuiFaceSize kGuiFaceSizes[kGuiFaceCount] = {
    {0, 22.0},   // Body (the Program row's number too, a row of its own)
    {1, 24.0},   // Bold: the brand
    {1, 16.0},   // Small: the ruler's digits, Bold
    {0, 22.0},   // Program: row 8's clock and state line, the player's
                 // clocks, the tooltip's name line
    {0, 16.0},   // Legend: the keys' legends
    {0, 24.0},   // Keys: the on-screen keyboard's caps (the default of
                 // the LIVE size, the `osk_font_px` key's: GuiFont below)
    {0, 18.0},   // Hint: the tooltip's hint line
    {1, 22.0},   // Label: the cues' labels and the lane's editors, Bold
};

// THE USE'S TABLE SIZE, its em in device px at 300 % — the one read of the
// table's sizes, which GuiFont's default and render.h's gui_font(face) take.
inline constexpr double gui_font_table_px(GuiFace face) {
    return kGuiFaceSizes[static_cast<std::size_t>(face)].px_at_300;
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
// successor of a cairo scaled font. `percent` is gui_scale and `px_at_300`
// the use's em in device px at 300 %. THE SIZE TRAVELS IN THE VALUE because
// ONE USE'S SIZE IS LIVE: the Keys face's, the `osk_font_px` tuning key's
// (architect 2026-10-29 night, "font size osk config tmp key"; render.h's
// seat) — the table's 24 its default — so every cache keyed on a GuiFont
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
    GuiFace face      = GuiFace::Body;
    int     percent   = 100;
    double  px_at_300 = gui_font_table_px(GuiFace::Body);
};

// THE USE'S EM IN DEVICE PX at the font's scale: its size at 300 % times
// gui_scale over 300, rounded at the element, floored at one px.
inline double gui_font_em_px(const GuiFont& f) {
    const double v = std::nearbyint(
        f.px_at_300 * static_cast<double>(f.percent) / 300.0);
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
// lowercase ink, which the modal box's text seats on the list rows' text
// seat, 18 px under the pane's top row at 300 % (modal_box.h's
// modal_box_text_baseline0, 2026-10-23 / M8b 2026-11-01: the air the eye
// sees is the ink's, not the line box's; from the pane since 2026-11-04).
double gui_font_ascender_ink_px(const GuiFont& f);
// THE X-HEIGHT in device px at the font's scale, unrounded: the em times the
// use's file's own "x" ink per em (Liberation Sans 1082 / 2048 units, 11.6
// px at 300 %'s 22) — the band the eye centers in the marker lane (render.h's
// cue band, architect 2026-11-01 night, M5h).
double gui_font_x_ink_px(const GuiFont& f);

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
// row count. RASTERIZED, NOT READ OFF CAIRO'S GLYPH EXTENTS (2026-11-03):
// the hinted extents are grid-fitted boxes taken before the lifted pipe's
// fractional seat and cairo's subpixel placement, so they overstate it (the
// pipe in Regular 24 among the legend's digits: extents 20 + 3, the paint
// 19 + 3), and
// the design's unhinted bounds rounded, the first measure, miss the
// hinter's own rows (Bold 12 at 200 %: 8.38 rounds to 8, the paint 9). At
// the defaults (2026-11-04 night, the unit of 2's sizes): "0123456789:." in
// the ruler's Bold 16 at 300 % 12 + 0 (the ruler, render.h's ruler block),
// Bold 11 at 200 % 8 + 0; the brand "Warptempo" in Bold 24 17 + 5, Bold 16
// 12 + 3; the legend's alphabet (the pipe apart) in Regular 22 15 + 0,
// Regular 15 10 + 0, and the pipe alone 18 + 3, 13 + 2 (the logo lane,
// render.h's logo lane block). A reader takes the integers as they are.
// Measured once per file,
// em and set and kept (a set is a constant of its reader's), GUI thread
// only; a character the face lacks paints .notdef, so a set names only
// characters the face carries; a set with no ink is 0 + 0.
struct GuiInkBand {
    int above = 0;
    int below = 0;
};
GuiInkBand gui_font_ink_band(const GuiFont& f, std::string_view set);

// THE STRIKE'S BAND (architect 2026-10-26 ~15:10, "have coincident markers
// be shown as a strike through … They become one"; "it's the same thickness
// as the regular — for example, a T would have the same thickness"): the
// rows of the bar a struck cue label wears through its run (show_cue_label,
// render.cpp, the one painter), relative to the run's baseline row —
// [baseline + dy, baseline + dy + h). THE THICKNESS IS THE FACE'S STROKE AT
// THE SIZE: the use's file's "l" — one vertical stem — measured across its
// unscaled outline (xMax − xMin, per em, read at the install) times the em,
// std::nearbyint, at least one device px. THE HEIGHT IS THE FACE'S OWN
// STRIKEOUT POSITION (the file's OS/2 yStrikeoutPosition, per em, read at
// the install): the band centered on it above the baseline, its top edge
// rounded to a whole row at the element and the thickness below it. The cues'
// labels are the Label face, the Bold file, since 2026-11-01 night, which is
// measured alike (its own "l" stem and strikeout); for the record, for
// Liberation Sans Regular (the "l" 180 of 2048 units, the strikeout 530):
// at 300 %'s 21 px a stroke of 1.85 → 2 rows, centered 5.43 above the
// baseline → rows baseline − 6 .. − 5; at 200 %'s 14 px 1.23 → 1 row,
// 3.62 above → baseline − 4; at 138 %'s 10 px 0.88 → 1 row, 2.59 above →
// baseline − 3.
struct GuiStrikeBand {
    int dy = 0;   // the band's top row less the baseline row (negative: up)
    int h  = 1;   // the band's rows, the stroke
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

