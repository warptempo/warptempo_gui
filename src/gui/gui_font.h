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
// nothing to consult.
//
// ONE FACE, HARD-CODED (architect 2026-10-12: "making Liberation Sans and
// GNOME 2.28 the only options, hard-coded, and drop their config keys"; the
// Tahoma and FreeSans sets and the `font` device key with its Settings row
// were removed that day, docs/engineering/closed_questions.md). Liberation
// Sans is Arial's metric twin, a stand-in for Arial (architect 2026-10-09:
// "my go-to font because of the kerning ... the most well-behaved"); the
// faces are named by their files. A set (GuiFaceSet below) is one face's
// text - which file each of the four uses (GuiFace) is drawn from, its
// recorded vertical metrics, its tracking and whether its math signs and its
// pipe are lifted - and kGuiFaceSetLiberation is the one set. The body is
// the regular file, the bold (the caption's title alone) the bold file, the
// small (THE RULER'S DIGITS - Cool Edit's flipped ruler at the base's six-row
// digit, program_spec.h's ruler fields) the regular file at the smaller em,
// and THE PROGRAM'S (the cues' labels at Cool Edit's cap of 7 W, METRICS
// 4.2) the regular file at the em that stands its "H" seven rows tall.
// EVERY FACE DRAWS ITS ANTIALIASED OUTLINE AT EVERY SIZE, NEVER AN EMBEDDED
// BITMAP STRIKE (architect 2026-10-06): the install hides any strike from
// FreeType, in every file, and every glyph load passes FT_LOAD_NO_BITMAP
// (gui_font_bundled.cpp's head). Every run is shaped by HarfBuzz with the
// face's OWN ADVANCES LESS THE SET'S TRACKING (GuiFaceSet::tracking_px,
// gui_tracking_px below), under SLIGHT hinting (gui_font_bundled.cpp's
// head), at the DERIVED EM (gui_face_em_px), and painted through
// cairo_show_glyphs (text_shape.h).
//
// THE VERTICAL METRICS ARE RECORDED CONSTANTS, the ONLY SOURCE OF VERTICAL
// METRICS (the set's, gui_face_metrics below; architect 2026-10-05, the
// constants 2026-10-06): ascent, descent, the line band, the cap band
// (redesign_baseline, line_baseline), the flag box, the lane heights and the
// ruler lane are the recorded integers in Windows px times the scale, as
// unrounded doubles (a font quantity is not a grid point), rounded at the
// element by the seat that reads them - save one derived figure, the cap
// band of a use measured by its x-height (gui_font_cap_px). They are the
// numbers the period drew at 96 dpi - a 13-px cell, MS Sans Serif 8's 6-row
// x-height - which is why they seat everything (CLAUDE.md's rounding
// doctrine: every chrome length is a Windows px).
//
// THE EM MATCHES THOSE METRICS VERTICALLY (architect 2026-10-05), derived at
// the install from the face's own measured ink (gui_face_em_px): each use's
// recorded measure names its glyph (GuiFaceMeasure) and the em is the one at
// which that glyph's ink stands the recorded rows tall - the body's and the
// bold's THE X-HEIGHT off each file's "x" (architect 2026-10-09 ~11:40 and
// ~23:30), their cap band derived (gui_font_cap_px), the small's the body
// file's "0" ink against the recorded digit, the program face's the "H"
// against its 7. Measured 2026-10-09: the body 11.357 W px (Liberation
// Sans's "x" 1082 of 2048), its derived cap 7.813 (the "H" 1409 of 2048),
// the small 8.474 (the "0" 1450 of 2048). The em is vertical alone:
// horizontally the face is its own, and a taller or deeper glyph ("$", a
// descender) may poke past a box sized off the recorded rows - accepted
// (architect 2026-10-05). A FACE'S OWN LINE BOX IS NEVER READ: the recorded
// metrics seat every run, every label's cap band centred by the recorded
// cap, or the derived one (redesign_baseline), or seated on the recorded
// ascent (line_baseline), so no face's line box can misplace a label.
// HORIZONTALLY THE FACE IS ITS OWN: every width used for layout is the
// shaped run's (text_shape), the face's own advances at the live size less
// the tracking.
//
// THE FOUR MATH SIGNS AND THE PIPE ARE LIFTED (GuiFaceSet::sign_lift and
// pipe_lift; gui_sign_axis below): the signs onto the hyphen's axis,
// downward (Arial's metrics draw them above it), the pipe onto the digits'
// band.


#include <cairo/cairo.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

enum class GuiFace { Body, Bold, Small, Program };
inline constexpr std::size_t kGuiFaceCount = 4;

// THE TWO FILES, THE INSTALL'S ONE ORDER (architect 2026-10-12, Liberation
// Sans alone): THIS LIST IS THE ONE PLACE THE NAMES ARE SPELLED - the Linux
// font step (CMakeLists.txt) and the APK's asset step
// (android/app/build_apk.sh) each read the quoted names out of this
// initializer, so a file added here is embedded and packed with no second
// list; the two backends hand the bytes in in this order, and a set names its
// files by their index here. Both are TrueType (FT_New_Memory_Face).
inline constexpr std::size_t kGuiFontFileCount = 2;
inline constexpr const char* kGuiFontFiles[kGuiFontFileCount] = {
    "LiberationSans-Regular.ttf", // 0: the body, the small and the program
    "LiberationSans-Bold.ttf",    // 1: the bold
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
// requires, and the set's faces (kGuiFaceSets below) carry the glyph each
// em is measured on and, in a set that lifts its signs or its pipe, the
// hyphen, the "0" and the five marks its axis is measured on.
// Its producer is breach-only (the bytes are the repository's own, so a face
// that fails to build is a build defect), and each caller dies on false.
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

// WHICH INK HEIGHT A USE'S RECORDED NUMBER IS (2026-10-09; architect
// 2026-10-09 ~11:40, "authentic to 1 windows px per measure for 1:1
// proportion … I'm talking about height"): each recorded measure names its
// glyph, and the em is the one at which that glyph's outline ink stands the
// recorded rows tall (gui_face_em_px) — THE CAP off "H" (the body, the bold
// and the program face), THE DIGIT off "0" (the small face: WordPad's
// six-row ruler digit), or THE X-HEIGHT off "x" (the body and bold: the
// face does not share MS Sans Serif's cap-to-x proportion, so one of the two
// heights must be the exact one, and the lowercase is most of what is read
// — the set's block below).
// Every measure is a HEIGHT: the face keeps its own advances, never a width
// match (the head).
enum class GuiFaceMeasure { Cap, Digit, XHeight };
constexpr char32_t gui_measure_glyph(GuiFaceMeasure m) {
    switch (m) {
    case GuiFaceMeasure::Cap:     return U'H';
    case GuiFaceMeasure::Digit:   return U'0';
    case GuiFaceMeasure::XHeight: return U'x';
    }
    return U'H';
}

// THE RECORDED VERTICAL METRICS of one use, in Windows px: the ascent and
// the descent (the cell is their sum) and THE MEASURE, the rows the named
// glyph's ink stands above the baseline (GuiFaceMeasure above). Each set
// records its own (below). THE CAP BAND the chrome's labels center
// (gui_font_cap_px) is the measure itself for a cap or digit measure and is
// DERIVED for an x-height measure.
struct GuiFaceMetrics {
    int            ascent  = 0;
    int            descent = 0;
    int            height  = 0;
    GuiFaceMeasure measure = GuiFaceMeasure::Cap;
};

// THE PROGRAM FACE'S RECORDED METRICS, (2026-10-09): Cool Edit's
// cap of 7 W for its cue labels, read off his captures
// (tmp/research/cool_edit/METRICS.md §4.2; its ruler digits too until
// 2026-10-09 ~21:00, when the ruler took the small face's six-row digit);
// the ascent and descent the cap's own proportion of the body's 11 / 2 over
// 8, rounded: {10, 2, 7}, measured by the cap ("H"). No lane's HEIGHT reads the cell (the marker lane's
// rows are program_spec.h's), but the cell SEATS THE CUES' TEXT: it is the
// line box render.h's cue_baseline_px centres in the lane, the label's
// baseline its top plus the ascent and the flag editor's selection band the
// box itself (architect 2026-10-09 evening), the descent held clear of the
// cue's triangle by render.h's assert.
inline constexpr GuiFaceMetrics kGuiProgramFaceMetrics = {
    10, 2, 7, GuiFaceMeasure::Cap};

// ONE FACE'S TEXT (architect 2026-10-06): per use (GuiFace, in
// its order), the kGuiFontFiles index the use is drawn from and its recorded
// metrics; the tracking in Windows px per glyph; whether the four math signs
// are set onto the hyphen's axis (gui_sign_axis below); whether the pipe is
// set onto the digits' band.
struct GuiFaceSet {
    std::size_t    file[kGuiFaceCount]    = {};
    GuiFaceMetrics metrics[kGuiFaceCount] = {};
    double         tracking_px            = 0.0;
    bool           sign_lift              = false;
    bool           pipe_lift              = false;
};

// THE LIBERATION SET, THE ONE FACE (architect 2026-10-09 ~23:30, the
// product's only face since 2026-10-12): LIBERATION SANS 2.1.5, Regular and
// Bold (fonts/README.md; SIL Open Font License 1.1, the files unmodified
// under their own names) - a stand-in for Arial, no period chrome's face.
// "My go-to font because of the kerning. It looks the best at 1080p ... the
// most well-behaved of all the fonts" (his words, 2026-10-09). TrueType
// under the light autohinter (gui_font_bundled.cpp's head), it keeps every
// glyph of "Settings" on the baseline row at 34.07, 40.88 and 45.80 px, its
// overshoot a half-tone row at 68.7 (the planner's probe, 2026-10-09).
//   METRICS: MS SANS SERIF 8 AT 96 DPI BY ITS X-HEIGHT, {11, 2, x 6} for
//   the body and the bold (architect 2026-10-09 ~11:40: "authentic to 1
//   windows px per measure for 1:1 proportion ... I'm talking about
//   height") - the base's 13-row cell (ascent 11 + descent 2), the
//   lowercase standing 6 rows: the em derives at the install off each
//   file's own "x" (the regular's 1082 of 2048, 11.357 W px), the cap
//   derived off the "H" (1409 of 2048, 7.813 W for the body) - the band the
//   chrome's labels center (gui_font_cap_px). THE SMALL IS THE BASE'S
//   SIX-ROW DIGIT, {6, 0, 6}, its em 8.474 off the "0" (1450 of 2048).
//   TRACKING: none (the heights-only rule: the face's own advances and
//   kerning, "no manual kerning - let the spacing be set by the font",
//   architect 2026-10-09).
//   SIGNS: SEATED ON THE HYPHEN'S AXIS, DOWNWARD (the sign paragraph
//   below). THE PIPE: LIFTED onto the digits' band.
inline constexpr GuiFaceSet kGuiFaceSetLiberation = {
    .file        = {0, 1, 0, 0},
    .metrics     = {{11, 2, 6, GuiFaceMeasure::XHeight},
                    {11, 2, 6, GuiFaceMeasure::XHeight},
                    {6, 0, 6, GuiFaceMeasure::Digit},
                    kGuiProgramFaceMetrics},
    .tracking_px = 0.0,
    .sign_lift   = true,
    .pipe_lift   = true,
};

// EVERY SET THE OWNER DEFINES, the install's probe walking it
// (gui_font_install_bundled: each set's files must carry the glyphs its ems
// and its sign axis are measured on).
inline constexpr const GuiFaceSet* kGuiFaceSets[] = {
    &kGuiFaceSetLiberation,
};

// THE SET - the one face, hard-coded (architect 2026-10-12). A function,
// not a constant, so the call sites read as they did when a device key chose.
inline const GuiFaceSet& gui_live_face_set() { return kGuiFaceSetLiberation; }

// THE SET'S RECORDED VERTICAL METRICS, per GuiFace — the only vertical
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
// THE CAP BAND in device px at the font's scale, unrounded — what
// redesign_baseline centers in a chrome box (paint_handler.cpp): for a use
// measured by its cap or its digit the recorded measure itself, exactly as
// recorded; for a use measured by its X-HEIGHT (the body and bold,
// 2026-10-09) DERIVED, the em times the use's file's own "H" ink per em
// (Liberation Sans's 7.813 W for the body), so a label centers
// the cap band its face truly draws, never a recorded cap the face does not
// stand. Out of line: the "H" ink is the install's (gui_font_bundled.cpp).
double gui_font_cap_px(const GuiFont& f);

// THE FACE'S EM, in Windows px, DERIVED FROM THE RECORDED METRICS: the
// set's recorded measure over the use's file's own ink height of the
// glyph the measure names (GuiFaceMeasure: "H", "0" or "x"), per em, the
// ink read off the bundled file at the install.
double gui_face_em_px(GuiFace face);

// FIVE MARKS ON ONE RULE, EACH SEATED ON ITS TARGET IN A SET THAT LIFTS IT
// (kGuiLiftedSigns, one table, one reader): a mark rises by its target's
// ink centre less its own, in every use of the set and every run; the
// targets and every other glyph stay where the face drew them.
//   THE FOUR MATH SIGNS SIT ON THE HYPHEN'S AXIS (GuiFaceSet::sign_lift):
//   "+" (U+002B), "=" (U+003D), "<" (U+003C) and ">" (U+003E) take the
//   hyphen's ink centre. Liberation Sans keeps its hyphen at 0.386 of its
//   cap (464..624 of 2048, centre 544, against the "H" 1409 - MS Sans
//   Serif's own 0.389) but draws the four on Arial's math axis, higher ("+"
//   180..1182, centre 681; "=" 344..1004, 674; "<" ">" 154..1194, 674), so
//   the lift is NEGATIVE, downward: the regular file's -0.067 em ("+") and
//   -0.063 ("=", "<", ">"); the bold's (its hyphen 409..653, centre 531;
//   "+" 161..1201, "=" 291..1065, "<" ">" 125..1229) -0.073, -0.072 and
//   -0.071 - the planner's measurement in font units, 2026-10-09; at 300 %
//   the body's plus falls 2.28 device px at its x-height em. NOTHING
//   CLAMPS A LIFT AT 0: the install stores the signed difference
//   (measure_sign_axis, gui_font_bundled.cpp) and the one reader adds it
//   whatever its sign (gui_sign_lift_px, text_shape's offset).
//   THE PIPE SITS ON THE DIGITS' BAND (GuiFaceSet::pipe_lift; architect
//   2026-10-09 ~21:20, "the pipe should be even", row 8's clock setting it
//   between the timestamp and the tab letter): "|" (U+007C) takes the "0"'s
//   ink centre, so its overhang above and below the digits beside it is the
//   same - a bar on the hyphen's axis would hang low. Liberation Sans draws
//   it -434..1484 of 2048 (525) against its "0"'s -20..1430 (705) - 0.088
//   em, the bold's -455..1484 (514.5) against the same "0" 0.093 em (the
//   planner's measurement, 2026-10-09).
// THE LIFTS ARE DERIVED at the install, like the em, from each file's own
// unscaled outline bounds (gui_font_bundled.cpp), in em units, for every
// file and every mark; a set's use reads its file's for the marks the set
// lifts. THE MATCH IS BY GLYPH ID after substitution (text_shape compares a
// shaped glyph's id against `glyph`), never by codepoint: a cluster is not
// a glyph. The lift moves the ink alone — every advance, the run's width
// and the tracking are untouched.
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
// Whether `set` lifts the marks seated on `target`.
constexpr bool gui_face_set_lifts(const GuiFaceSet& set, GuiSignTarget target) {
    return target == GuiSignTarget::HyphenAxis ? set.sign_lift : set.pipe_lift;
}
struct GuiSignLift {
    unsigned glyph   = 0;    // the face's glyph id for the mark
    double   lift_em = 0.0;  // up-positive, in em
};
struct GuiSignAxis {
    GuiSignLift signs[kGuiSignCount] = {};  // in kGuiLiftedSigns' order
};
// The live set's axis for `face`: its file's measured lifts where the set
// lifts any mark, else every lift 0 (and every glyph id 0, which a lift of
// 0 makes harmless however a run's glyph ids compare). The per-mark choice
// is the reader's (gui_sign_lift_px).
const GuiSignAxis& gui_sign_axis(GuiFace face);

// The lift of one shaped glyph in DEVICE px at the font's scale, HarfBuzz's
// sense (up-positive), unrounded: the mark's lift times the em times the
// scale, 0 for every glyph that is not one of the five marks and for every
// mark the live set does not lift. text_shape's one reader.
inline double gui_sign_lift_px(const GuiFont& f, unsigned glyph) {
    const GuiFaceSet&  set  = gui_live_face_set();
    const GuiSignAxis& axis = gui_sign_axis(f.face);
    for (std::size_t i = 0; i < kGuiSignCount; ++i) {
        const GuiSignLift& s = axis.signs[i];
        if (s.lift_em != 0.0 && s.glyph == glyph &&
            gui_face_set_lifts(set, kGuiLiftedSigns[i].target))
            return s.lift_em * gui_face_em_px(f.face) * gui_font_scale(f);
    }
    return 0.0;
}

// THE FACE'S CAIRO SCALED FONT at this scale (the use's file at its em times
// the scale, SLIGHT, outlines only): borrowed, owned by the face owner and
// cached on the scale; GUI thread only. text_shape shapes and paints every
// glyph on it.
cairo_scaled_font_t* gui_outline_scaled_font(const GuiFont& f);

// THE LIVE SET'S TRACKING (GuiFaceSet::tracking_px), in WINDOWS PX per
// glyph (the figure and its reason at the set above; 0 since
// 2026-10-07, the architect's heights-only rule — the mechanism stays, one
// number per set): every advance HarfBuzz returns is
// shortened by it, the last glyph of a run included, so a run's width stays
// the plain sum of its advances (text_shape.h) and a right-aligned run's
// right edge stays honest. It scales with the font like every chrome length
// and is NOT ROUNDED — it is an advance, not a grid point. ONE CONSTANT FOR
// THE FOUR USES (the body, the caption's bold, the ruler's small, the
// program's). The font
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

// THE LINUX BINARY'S COPY OF THE TWO FILES, in kGuiFontFiles' order,
// defined by gui_font_embedded.cpp, which only the Linux target compiles (the
// APK carries the same files as assets instead).
extern const GuiFontBytes gui_font_embedded_files[kGuiFontFileCount];
