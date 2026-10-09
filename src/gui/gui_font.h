#pragma once

// THE ONE FACE OWNER: every text surface names its face here and nowhere
// else, so the painters never name a font. THE PRODUCT'S FACES ARE ITS OWN
// (architect 2026-10-02: "the app becomes its own thing; it has its own
// fonts"): six files under the repository's `fonts/` (fonts/README.md has
// their provenance and licences), carried by both binaries — compiled into
// the Linux executable (gui_font_embedded.cpp), shipped as the APK's assets
// on Android — and turned into faces by ONE implementation on both devices,
// gui_font_bundled.cpp, through FreeType. Neither device asks anything for a
// face: fontconfig is never consulted on the laptop and the tablet has
// nothing to consult.
//
// THREE FACE SETS, ONE LIVE, CHOSEN BY THE `font` DEVICE KEY ALONE
// (architect 2026-10-06, the Windows 2000 pivot; the second,
// kGuiFaceSetFreeSans — FreeSans, the MS Sans Serif stand-in —
// 2026-10-09; WINDOWS' FACES ALONE, architect 2026-10-09 ~21:30: "Go Sans
// can be removed because it's not a Windows font"; the third,
// kGuiFaceSetLiberation — Liberation Sans, THE FALLBACK FACE — and the key,
// architect 2026-10-09 ~21:20 / ~23:30: "the font is its own drop-down … it
// doesn't really make sense picking a font via palette or even in the
// palette picker"). EVERY SET IS NAMED BY ITS FILES, NEVER BY THE WINDOWS
// FACE IT STANDS IN FOR (architect 2026-10-09 evening: "I just want to make
// sure that the fonts are named honestly … Liberation Sans is a stand-in for
// Arial, and FreeSans is what we're actually providing"): the key's words,
// the Settings row's names and the identifiers say Tahoma, FreeSans and
// Liberation Sans; the stand-in is each block's story. A set is one face's text — which file each of the four
// uses (GuiFace) is drawn from, its recorded vertical metrics, its tracking
// and whether its math signs and its pipe are lifted (GuiFaceSet below) —
// and the device config's `font` names the live one (kGuiFontKeys and
// gui_live_face_set below), whatever chrome and scheme are live; a scheme
// carries no face. THE WIN2000 SET, kGuiFaceSetWin2000 (the key `tahoma`,
// THE ABSENT KEY'S): WINE TAHOMA as ReactOS 0.4.16 ships it (tahoma.ttf,
// tahomabd.ttf; TrueType outlines on a Bitstream Vera base), the face the
// ReactOS captures are set in — its 11-ppem strike equals tmp/reactos.png's
// menu and title pixels exactly (measured 2026-10-06). THE FREESANS SET,
// kGuiFaceSetFreeSans (`free-sans`): GNU FREEFONT'S FREESANS (FreeSans.otf,
// FreeSansBold.otf; CFF outlines, URW's Nimbus Sans L under GNU FreeFont's
// extensions), standing in for the MS Sans Serif Windows set its chrome in
// before Windows 2000's Standard scheme took Tahoma. THE LIBERATION SET,
// kGuiFaceSetLiberation (`liberation-sans`): LIBERATION SANS 2.1.5
// (LiberationSans-Regular.ttf, LiberationSans-Bold.ttf; TrueType outlines,
// Arial's metrics — a stand-in for Arial, architect 2026-10-09), the face a
// user whom FreeSans's hinting troubles falls back to. In each set the body is
// the regular file,
// the bold (the caption's title alone) the bold file, the small (THE
// RULER'S DIGITS — Cool Edit's flipped ruler at the base's six-row digit
// since architect 2026-10-09 ~16:50 / ~21:00, program_spec.h's ruler
// fields) the regular file at the smaller em, and THE PROGRAM'S (architect
// 2026-10-09, the program is Cool Edit: the cues' labels at Cool Edit's
// cap of 7 W, METRICS §4.2) the regular file at the em that stands its "H"
// seven rows tall.
// EVERY FACE DRAWS ITS ANTIALIASED OUTLINE AT EVERY SIZE, NEVER AN EMBEDDED
// BITMAP STRIKE (architect 2026-10-06; Tahoma carries strikes at 8–16 ppem,
// its bold at 9–13, and the sheets he judged were outline renders; FreeSans
// carries none): the install hides any strike from FreeType, in every file,
// and every glyph
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
// element by the seat that reads them — save one derived figure, the cap
// band of a use measured by its x-height (gui_font_cap_px, 2026-10-09). They
// are the numbers the period drew at 96 dpi — Tahoma 8's 13-px cell with its
// 8-row cap, MS Sans Serif 8's 6-row x-height (which the Liberation set takes
// too) — which is why they seat everything (CLAUDE.md's rounding
// doctrine: every chrome length is a Windows px).
//
// THE EM MATCHES THOSE METRICS VERTICALLY (architect 2026-10-05), derived at
// the install from the face's own measured ink (gui_face_em_px): each use's
// recorded measure names its glyph (GuiFaceMeasure, 2026-10-09) and the em
// is the one at which that glyph's ink stands the recorded rows tall — the
// body's the body file's "H" against the recorded cap, the bold's the bold
// file's "H" against the same cap, the small's the body file's "0" ink
// against the recorded digit, the program face's the "H" against its 7 —
// EXCEPT THE FREESANS AND LIBERATION SETS' BODY AND BOLD, measured by
// THE X-HEIGHT off each file's "x" (architect 2026-10-09 ~11:40 and ~23:30;
// those sets' blocks below), their cap band derived (gui_font_cap_px).
// Measured 2026-10-06 for the
// win2000 set: 11.003, 10.996 and 7.943 Windows px (Tahoma's "H" 1489 of
// 2048, Tahoma Bold's 1490, Tahoma's "0" 1547 — 8 pt at GDI's 11 ppem,
// Tahoma's own 8-pt size); 2026-10-09 for the FreeSans set at its 6-row x-height: 11.450, 11.111
// and 8.197 Windows px (FreeSans's "x" 524 of 1000, FreeSans Bold's 540,
// FreeSans's "0" 732 — Nimbus Sans's own figures, the outlines being
// Nimbus's), the derived caps 8.347 and 8.100 off the "H", 729 of 1000 in
// both weights; 2026-10-09 for the Liberation set at the same x-height: the
// body 11.357 W px (Liberation Sans's "x" 1082 of 2048), its derived cap
// 7.813 (the "H" 1409 of 2048), the small 8.474 (the "0" 1450 of 2048).
// The em is vertical alone: horizontally the face is its own, and a taller
// or deeper glyph ("$", a descender) may poke past a box sized off the
// recorded rows — accepted (architect 2026-10-05). A FACE'S OWN LINE BOX IS
// NEVER READ (Tahoma's hhea ascent is 2049 of 2048, above its caps;
// FreeSans's 900 / -200 of 1000 stands 171 units
// above its caps, so a program seating text on that box sets it low): the
// recorded metrics seat every run, every label's cap band centred by the
// recorded cap, or the derived one (redesign_baseline), or seated on the
// recorded ascent
// (line_baseline), so no face's line box can misplace a label. HORIZONTALLY THE FACE IS ITS OWN: every
// width used for layout is the shaped run's (text_shape), the face's own
// advances at the live size less the tracking.
//
// THE FOUR MATH SIGNS AND THE PIPE ARE WHERE THE FACE DRAWS THEM, EXCEPT
// IN A SET THAT LIFTS THEM (GuiFaceSet::sign_lift and pipe_lift;
// gui_sign_axis below). THE SIGNS: Tahoma does not lift them —
// Tahoma already draws the five on one axis: its 11-ppem strike centres
// "-", "+", "<", ">" and "=" all on row 3 of the 8-row cap, and its
// outlines agree to a few units (the hyphen 566..730 of 2048, centre 648;
// "+" and "=" centred at 652, "<" ">" at 654; the bold within one unit) —
// so a derived lift would come to under 0.003 em, a tenth of a device px at
// 400 %, nothing for a mechanism to correct. THE FREESANS SET LIFTS
// THEM (architect 2026-10-09: FreeSans is Nimbus's outlines, and the lift
// deleted with Nimbus on 2026-10-06 returns with them), and THE LIBERATION
// SET SEATS THEM TOO, DOWNWARD (2026-10-09: Arial's metrics put the four
// above the hyphen's axis, so its lift is negative). THE PIPE (architect
// 2026-10-09 ~21:20, "the pipe should be even"): every set lifts it onto
// the digits' band, Tahoma's hanging a whole W low of it (the numbers at
// the sign paragraph below).

#include <cairo/cairo.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

enum class GuiFace { Body, Bold, Small, Program };
inline constexpr std::size_t kGuiFaceCount = 4;

// THE SIX FILES, THE INSTALL'S ONE ORDER, EVERY SET'S (architect
// 2026-10-06, Tahoma's pair; 2026-10-09 FreeSans's and Liberation Sans's
// beside it): THIS LIST
// IS THE ONE PLACE THE NAMES ARE SPELLED — the Linux font step
// (CMakeLists.txt) and the APK's asset step (android/app/build_apk.sh) each
// read the quoted names out of this initializer, so a file added here is
// embedded and packed with no second list; the two backends hand the bytes
// in in this order, and a set names its files by their index here. Every
// set's files are carried whichever face a device names (the `font` key is
// read after the Android backend installs the faces, and the install
// measures every set's ems at once, gui_font_bundled.cpp).
// FreeSans's two are OpenType CFF (".otf"), which the one loader takes as it
// takes Tahoma's and Liberation Sans's TrueType pairs (FT_New_Memory_Face
// reads either; gui_font_bundled.cpp's head on the hinter each format meets).
inline constexpr std::size_t kGuiFontFileCount = 6;
inline constexpr const char* kGuiFontFiles[kGuiFontFileCount] = {
    "tahoma.ttf",                 // 0: win2000's body and small
    "tahomabd.ttf",               // 1: win2000's bold
    "FreeSans.otf",               // 2: MS Sans Serif's body and small
    "FreeSansBold.otf",           // 3: MS Sans Serif's bold
    "LiberationSans-Regular.ttf", // 4: Liberation's body and small
    "LiberationSans-Bold.ttf",    // 5: Liberation's bold
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
// six files selects as an FT-BACKED cairo face, which is what text_shape
// requires, and EVERY set's faces (kGuiFaceSets below) carry the glyph each
// em is measured on and, in a set that lifts its signs or its pipe, the
// hyphen, the "0" and the five marks its axis is measured on — every set,
// because the install precedes the `font` key's read on Android, and the
// Settings dialog's OK on a new Font choice moves the face live.
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

// WHICH INK HEIGHT A USE'S RECORDED NUMBER IS (2026-10-09; architect
// 2026-10-09 ~11:40, "authentic to 1 windows px per measure for 1:1
// proportion … I'm talking about height"): each recorded measure names its
// glyph, and the em is the one at which that glyph's outline ink stands the
// recorded rows tall (gui_face_em_px) — THE CAP off "H" (the body, the bold
// and the program face of every set, the body and the bold of Tahoma's),
// THE DIGIT off "0" (the small face of every set: WordPad's six-row ruler
// digit), or THE X-HEIGHT off "x" (the FreeSans and Liberation sets'
// body and bold: neither face shares MS Sans Serif's cap-to-x proportion,
// so one of the two heights must be the exact one, and the lowercase is
// most of what is read — the FreeSans set's block below).
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

// THE PROGRAM FACE'S RECORDED METRICS, every set's (2026-10-09): Cool Edit's
// cap of 7 W for its cue labels, read off his captures
// (tmp/research/cool_edit/METRICS.md §4.2; its ruler digits too until
// 2026-10-09 ~21:00, when the ruler took the small face's six-row digit);
// the ascent and descent the cap's own proportion of the body's 11 / 2 over
// 8, rounded: {10, 2, 7}, measured by the cap ("H") under every set,
// FreeSans's included. No lane's HEIGHT reads the cell (the marker lane's
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
// are set onto the hyphen's axis (gui_sign_axis below; true for the
// FreeSans and Liberation sets, 2026-10-09); whether the pipe is set onto
// the digits' band (true for every set, 2026-10-09 ~21:20).
struct GuiFaceSet {
    std::size_t    file[kGuiFaceCount]    = {};
    GuiFaceMetrics metrics[kGuiFaceCount] = {};
    double         tracking_px            = 0.0;
    bool           sign_lift              = false;
    bool           pipe_lift              = false;
};

// THE WIN2000 SET (architect 2026-10-06), the `font` key's `tahoma`, the
// face an absent key means: Wine Tahoma.
//   METRICS: the body and the bold are Tahoma 8 at 96 dpi (GDI's LOGFONT
//   -11, 11 ppem), its 13-px cell — ascent 11 + descent 2 — with caps and
//   digits 8 rows, the cell Windows 95's MS Sans Serif 8 also drew, so every
//   lane and box keeps its 13; the small is a SIX-row digit, a cell all
//   above the baseline: WordPad's ruler digits, which stand 6 rows beside
//   the 22-px icons at 100 % (architect 2026-10-06).
//   TRACKING: none (architect 2026-10-07: "we're not matching font widths,
//   only the heights"): the recorded metrics match the period's heights and
//   the widths are the live face's own advances.
//   SIGNS: where Tahoma draws them (the head). THE PIPE: LIFTED onto the
//   digits' band (gui_sign_axis below; architect 2026-10-09 ~21:20) — its
//   bar hangs 1.08 W low of the "0" at the body's em, 3.25 device px at
//   300 %, past the quarter W the ruling allowed as even.
inline constexpr GuiFaceSet kGuiFaceSetWin2000 = {
    .file        = {0, 1, 0, 0},
    .metrics     = {{11, 2, 8, GuiFaceMeasure::Cap},
                    {11, 2, 8, GuiFaceMeasure::Cap},
                    {6, 0, 6, GuiFaceMeasure::Digit},
                    kGuiProgramFaceMetrics},
    .tracking_px = 0.0,
    .pipe_lift   = true,
};

// THE FREESANS SET (architect 2026-10-09: "let's use free sans"), the
// `font` key's `free-sans`, "FreeSans" in the Settings row — named by its
// files, never by the face it stands in for (architect 2026-10-09 evening,
// the head; gui_live_face_set below) — GNU FREEFONT'S FREESANS, FreeSans
// and FreeSans Bold of GNU FreeFont 20120503, its last release
// (fonts/README.md; GPL-3.0-or-later with the font exception), THE MS SANS
// SERIF STAND-IN, at that face's x-height measure. Windows set its chrome in MS Sans Serif until
// Windows 2000 made Tahoma its Standard scheme's face — Windows 95's and
// 98's schemes name it, most of Windows 2000's other schemes name its
// TrueType twin Microsoft Sans Serif, and Windows Me's classic desktop is
// Windows 2000's chrome and Standard colors in MS Sans Serif (his
// guidebookgallery captures tmp/winme.png and tmp/win2000pro.png, WordPad
// at identical metrics); MS Sans Serif has no open outline (Wine's
// is bitmap strikes — NO BITMAP FONTS, the head — and Microsoft Sans Serif
// is proprietary), and FreeSans is a Helvetica, the genre MS Sans Serif was
// drawn from, its Latin outlines URW's Nimbus Sans L (the face of the
// win95 vocabulary dropped 2026-10-06).
//   METRICS: MS SANS SERIF 8 AT 96 DPI BY ITS X-HEIGHT, {11, 2, x 6} for
//   the body and the bold (architect 2026-10-09 ~11:40: "authentic to 1
//   windows px per measure for 1:1 proportion … I'm talking about height")
//   — the base's 13-row cell (ascent 11 + descent 2), so every lane and box
//   keeps its 13, the lowercase standing 6 ROWS, measured 2026-10-09 on
//   tmp/winme.png (1:1), the menu titles' x-height 6 where
//   tmp/win2000pro.png's Tahoma stands 6 too. THE CAPTURE'S CAPS AND DIGITS
//   STAND 9 ROWS (the menu bar's "F", "E", "V", "I", "H" rows 27..35, the
//   status line's "F" 394..402, the caption's bold "S" and "W" 8..16 —
//   where Tahoma's stand eight, the menu's "F" 28..35, the bold "D" 9..16):
//   that 9 is the bitmap's own tall-cap proportion, cap over x 1.5, which a
//   Helvetica does not share — FreeSans's "H" 729 and "x" 524 of 1000 stand
//   1.39 — so no one em honors both heights, and the ruling makes THE
//   X-HEIGHT THE EXACT MEASURE, the lowercase being most of what is read (at
//   the capture's cap 9 FreeSans's x stood 6.47 W, half a row over: the
//   face read larger than Tahoma's). The em derives at the install off
//   each file's own "x" (GuiFaceMeasure::XHeight): FreeSans's 524 of 1000,
//   11.450 W px against Tahoma's 11.003; FreeSans Bold's 540 of 1000 (the
//   x-height URW's metrics record for Nimbus Sans L Bold, whose outlines
//   the file carries — the install reads the file's own ink), 11.111 W px.
//   THE CAP IS DERIVED, the em times the file's own
//   "H" (729 of 1000 in both weights): 8.347 W for the body, 8.100 for the
//   bold — the band the chrome's labels center (gui_font_cap_px). The
//   heights-only rule stands: the x-height is a height. THE SMALL IS THE BASE'S
//   SIX-ROW DIGIT, {6, 0, 6}, not the win95 set's {7, 0, 7}: that was Small
//   Fonts' digit as ACID and Vegas drew their rulers, the win95
//   vocabulary's own model; Windows Me's WordPad ruler is Windows 2000's
//   pixel for pixel (the two captures' digits, rows 110..118, identical),
//   so the ruler under FreeSans differs from the base's in nothing and
//   keeps its digit — and with it the base's ruler lane, the stack and the
//   well, which a live swap of the sets must not move (same_lanes
//   below). Its em 8.197 off FreeSans's "0" (732 of 1000).
//   TRACKING: none (the heights-only rule: the face's own advances and
//   kerning, "no manual kerning — let the spacing be set by the font",
//   architect 2026-10-09).
//   SIGNS: LIFTED onto the hyphen's axis (gui_sign_axis below; architect
//   2026-10-09, the lift returning with Nimbus's outlines). THE PIPE:
//   LIFTED onto the digits' band (architect 2026-10-09 ~21:20, "we have
//   sign lift for the plus sign in FreeSans. We should do that for the pipe
//   as well").
inline constexpr GuiFaceSet kGuiFaceSetFreeSans = {
    .file        = {2, 3, 2, 2},
    .metrics     = {{11, 2, 6, GuiFaceMeasure::XHeight},
                    {11, 2, 6, GuiFaceMeasure::XHeight},
                    {6, 0, 6, GuiFaceMeasure::Digit},
                    kGuiProgramFaceMetrics},
    .tracking_px = 0.0,
    .sign_lift   = true,
    .pipe_lift   = true,
};

// THE LIBERATION SET (architect 2026-10-09 ~23:30: "we can drop in
// Liberation as a fallback if the user has problems. For me FreeSans is
// working out right now … we should install Liberation Sans as a
// fallback"), the `font` key's `liberation-sans`, "Liberation Sans" in the
// Settings row, named by its files — a stand-in for Arial (architect
// 2026-10-09 evening), no period chrome's face — LIBERATION SANS 2.1.5,
// Regular and Bold (fonts/README.md;
// SIL Open Font License 1.1, the files unmodified under their own names):
// "my go-to font because of the kerning. It looks the best at 1080p … the
// most well-behaved of all the fonts" (his words, 2026-10-09). THE FALLBACK
// FOR FREESANS'S HINTING: FreeSans's CFF hints snap the round letters'
// baseline overshoot to a whole row above a 41.7-px em (past ~360 %),
// while Liberation Sans, TrueType under the light autohinter like Tahoma
// (gui_font_bundled.cpp's head), keeps every glyph of "Settings" on the
// baseline row at 34.07, 40.88 and 45.80 px, its overshoot a half-tone row
// at 68.7 (the planner's probe, 2026-10-09). It was the product's fallback
// face once before, 2026-10-05 to 2026-10-06 (git history).
//   METRICS: THE FREESANS SET'S, BY THE X-HEIGHT, {11, 2, x 6} for the
//   body and the bold — its sibling as a stand-in at Windows' metrics, the
//   base's 13-row cell, the lowercase six rows: the em derives at the
//   install off each file's own "x" (the regular's 1082 of 2048, 11.357 W
//   px), the cap derived off the "H" (1409 of 2048, 7.813 W for the body)
//   — the band the chrome's labels center (gui_font_cap_px). THE SMALL IS
//   THE BASE'S SIX-ROW DIGIT, {6, 0, 6}, its em 8.474 off the "0" (1450 of
//   2048), so the ruler lane and the well keep their rows (same_lanes
//   below).
//   TRACKING: none (the heights-only rule; the face's own kerning, the
//   reason he keeps it).
//   SIGNS: SEATED ON THE HYPHEN'S AXIS, DOWNWARD (2026-10-09): Arial's
//   metrics draw the four above the hyphen's centre (the numbers at the
//   sign paragraph below), so the lift is negative — the reader moves a
//   mark by its lift whatever its sign (gui_sign_lift_px). THE PIPE: LIFTED
//   onto the digits' band, as in every set.
inline constexpr GuiFaceSet kGuiFaceSetLiberation = {
    .file        = {4, 5, 4, 4},
    .metrics     = {{11, 2, 6, GuiFaceMeasure::XHeight},
                    {11, 2, 6, GuiFaceMeasure::XHeight},
                    {6, 0, 6, GuiFaceMeasure::Digit},
                    kGuiProgramFaceMetrics},
    .tracking_px = 0.0,
    .sign_lift   = true,
    .pipe_lift   = true,
};

// EVERY SET THE OWNER DEFINES (2026-10-09), the install's probe walking it
// (gui_font_install_bundled: each set's files must carry the glyphs its ems
// and its sign axis are measured on) — every set the `font` key can name,
// so each is proven installable whichever face the device later names or
// a Settings commit moves to.
inline constexpr const GuiFaceSet* kGuiFaceSets[] = {
    &kGuiFaceSetWin2000,
    &kGuiFaceSetFreeSans,
    &kGuiFaceSetLiberation,
};

// A LIVE SWAP BETWEEN THE SETS MOVES NO LANE (the Settings dialog's OK on a
// new Font choice moves the face live — a pick in the row's list only moves
// its highlight, 2026-10-09 evening; gui_live_face_set below): the three
// record
// the same cell, ascent 11 and descent 2 for the body and the bold, and
// the same six-row small digit and the same program face — only the body's
// and the bold's measure differ (Tahoma's cap 8 / FreeSans's and Liberation
// Sans's x-height 6, the cap derived), which no lane height reads — so the
// stack and the well keep their rows across a swap (since 2026-10-09 the
// ruler and the marker lane are the program's authored rows,
// program_spec.h, and read no face's cell for their heights — the ruler's
// six-row digit the small face's in every set, render.h's assert).
constexpr bool same_lanes(const GuiFaceSet& a, const GuiFaceSet& b) {
    for (std::size_t i = 0; i < kGuiFaceCount; ++i) {
        if (a.metrics[i].ascent != b.metrics[i].ascent ||
            a.metrics[i].descent != b.metrics[i].descent)
            return false;
    }
    const std::size_t small = static_cast<std::size_t>(GuiFace::Small);
    return a.metrics[small].height == b.metrics[small].height &&
           a.metrics[small].measure == b.metrics[small].measure;
}
static_assert(same_lanes(kGuiFaceSetWin2000, kGuiFaceSetFreeSans));
static_assert(same_lanes(kGuiFaceSetWin2000, kGuiFaceSetLiberation));

// THE `font` DEVICE KEY — THE FACE COMES FROM THE KEY ALONE (architect
// 2026-10-09 ~21:20: "the font is its own drop-down"; "the scheme's default
// font should stop being honored — it should only be honored from the font
// picker. No default setting because it's a drop-down"): the key's whole
// vocabulary, a closed compiled list in the Settings menu's Font row's order
// (kFontChoiceSource, app_state.h), each word naming its set, its shown
// name beside it, Title Case (the row's combo and list) — icons.h's
// kIconSetKeys' shape. THE WORDS AND THE NAMES ARE THE FILES' (architect
// 2026-10-09 evening: "I just want to make sure that the fonts are named
// honestly … FreeSans is what we're actually providing. So the drop-down
// should use FreeSans in the name") — never the Windows face a set stands
// in for, which is its block's story. THE ORDER IS ALPHABETICAL BY THE
// SHOWN NAME, FreeSans, Liberation Sans, Tahoma (architect 2026-10-09: the
// order of the evening before was "neither historical nor alphabetical;
// let's just make it alphabetical"), and the three tables below run in it
// in parallel; nothing reads a position in them — the default, the
// first-run stamp and the launch's resolution all go by the key's word
// (kDefaultFontKey, gui_face_set_for_font_key).
// THE ABSENT KEY IS `tahoma` (kDefaultFontKey, Windows 2000's own face): the
// row always shows the live value, and a pick always writes the line
// (device_config.h). No chrome and no scheme names a face.
inline constexpr const char* kGuiFontKeys[] = {
    "free-sans", "liberation-sans", "tahoma"};
inline constexpr const char* kGuiFontDisplayNames[] = {
    "FreeSans", "Liberation Sans", "Tahoma"};
inline constexpr const GuiFaceSet* kGuiFontKeySets[] = {
    &kGuiFaceSetFreeSans, &kGuiFaceSetLiberation, &kGuiFaceSetWin2000};
static_assert(std::size(kGuiFontKeys) == std::size(kGuiFontDisplayNames));
static_assert(std::size(kGuiFontKeys) == std::size(kGuiFontKeySets));

// THE `font` KEY'S GRAMMAR — the ONE owner, asked by the device config's
// reader (any other word the launch's first-error hard fail: a hand edit is
// the only producer) and by the settings editor's Font row (its refused
// commit): a set's key, byte for byte — chrome_spec.h's is_chrome_key's
// shape. The set it names, or none.
constexpr const GuiFaceSet* gui_face_set_for_font_key(std::string_view v) {
    for (std::size_t i = 0; i < std::size(kGuiFontKeys); ++i)
        if (v == kGuiFontKeys[i]) return kGuiFontKeySets[i];
    return nullptr;
}
constexpr bool is_font_key(std::string_view v) {
    return gui_face_set_for_font_key(v) != nullptr;
}
inline constexpr const char* kFontGrammarReason =
    "must be free-sans, liberation-sans or tahoma";
// THE DEFAULT, a config with no `font` line (DeviceConfig::font's
// initializer spells it).
inline constexpr const char* kDefaultFontKey = "tahoma";
static_assert(is_font_key(kDefaultFontKey));

// THE LIVE SET — THE ONE RESOLUTION (architect 2026-10-09): the set the
// device config's `font` names, installed at launch (set_live_font, gui_main,
// beside set_live_chrome_spec, before the first paint) and MOVED LIVE when
// the Settings dialog's OK commits a new Font choice (commit_device_setting's
// font arm, settings_editor.cpp; the pick in the row's list only moves its
// highlight, the choice rows committing at OK alone since 2026-10-09
// evening): the caches that hold the face key the set
// (gui_font_bundled.cpp's scaled fonts and with them text_shape's hb fonts,
// paint_handler.cpp's time-field memo, the flag cache's fp_face_set —
// paint_handler.h; nothing else holds a shaped run or a width across
// frames — the inventory re-grepped 2026-10-09 when the face first moved
// live), the lanes cannot move (same_lanes above),
// and the commit damages the whole window. Construction state is
// the default's, until gui_main installs the config's.
namespace gui_font_detail {
inline const GuiFaceSet* g_live_set =
    gui_face_set_for_font_key(kDefaultFontKey);
} // namespace gui_font_detail
// Precondition: is_font_key(key), the config's reader or the settings
// editor's grammar having judged it.
inline void set_live_font(std::string_view key) {
    const GuiFaceSet* set = gui_face_set_for_font_key(key);
    assert(set != nullptr);
    gui_font_detail::g_live_set = set;
}
inline const GuiFaceSet& gui_live_face_set() {
    return *gui_font_detail::g_live_set;
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
// THE CAP BAND in device px at the font's scale, unrounded — what
// redesign_baseline centers in a chrome box (paint_handler.cpp): for a use
// measured by its cap or its digit the recorded measure itself, exactly as
// recorded; for a use measured by its X-HEIGHT (the FreeSans and
// Liberation sets' body and bold, 2026-10-09) DERIVED, the em times the
// use's file's own "H" ink per em (FreeSans's 8.347 W, FreeSans Bold's
// 8.100, Liberation Sans's 7.813), so a label centers
// the cap band its face truly draws, never a recorded cap the face does not
// stand. Out of line: the "H" ink is the install's (gui_font_bundled.cpp).
double gui_font_cap_px(const GuiFont& f);

// THE FACE'S EM, in Windows px, DERIVED FROM THE RECORDED METRICS: the
// live set's recorded measure over the use's file's own ink height of the
// glyph the measure names (GuiFaceMeasure: "H", "0" or "x"), per em, the
// ink read off the bundled file at the install (every file's, so the em
// follows whichever set the `font` key names).
double gui_face_em_px(GuiFace face);

// FIVE MARKS ON ONE RULE, EACH SEATED ON ITS TARGET IN A SET THAT LIFTS IT
// (kGuiLiftedSigns, one table, one reader): a mark rises by its target's
// ink centre less its own, in every use of the set and every run; the
// targets and every other glyph stay where the face drew them.
//   THE FOUR MATH SIGNS SIT ON THE HYPHEN'S AXIS IN A SET THAT LIFTS THEM
//   (GuiFaceSet::sign_lift; architect 2026-10-06, "as MS Sans Serif had
//   them", deleted with Nimbus that evening, RETURNED 2026-10-09 with
//   FreeSans, whose outlines are Nimbus's): "+" (U+002B), "=" (U+003D), "<"
//   (U+003C) and ">" (U+003E) take the hyphen's ink centre. The period's
//   faces put the hyphen and the signs on ONE axis, and the lift seats a
//   face's signs there.
//   THE PIPE SITS ON THE DIGITS' BAND IN A SET THAT LIFTS IT
//   (GuiFaceSet::pipe_lift; architect 2026-10-09 ~21:20, "the pipe should
//   be even", row 8's clock setting it between the timestamp and the tab
//   letter): "|" (U+007C) takes the "0"'s ink centre, so its overhang above
//   and below the digits beside it is the same — a bar on the hyphen's axis
//   would hang low. Both faces draw it as a descender-to-ascender bar
//   centred well under the digits: TAHOMA'S -483..1565 of 2048 (centre
//   541) against its "0"'s -31..1516 (742.5) and its hyphen's 648 — a lift
//   of 0.0984 em, 1.08 W at the body's em (11.003 W), 3.25 device px at
//   300 % and 3.90 at 360, the small's 0.78 W (2.34 / 2.81 px), the bold's
//   -392..1555 (581.5) against 745 a lift of 0.0798 em (2.63 / 3.16 px);
//   FREESANS'S -212..729 of 1000 (258.5) against its "0"'s -23..709 (343)
//   and its hyphen's 276 — 0.0845 em, 0.97 W at the body's x-height em
//   (11.450 W), 2.90 device px at 300 % and 3.48 at 360, the small's 0.69 W
//   (2.08 / 2.49 px), the bold's -200..729 (264.5) against 350.5 0.086 em
//   (2.87 / 3.44 px) — read 2026-10-09 off the files through FreeType;
//   LIBERATION SANS'S -434..1484 of 2048 (525) against its "0"'s -20..1430
//   (705) — 0.088 em, the bold's -455..1484 (514.5) against the same "0"
//   0.093 em (the planner's measurement, 2026-10-09). Every set lifts it,
//   Tahoma's being far past the quarter W the ruling allowed as already
//   even.
//   THE FREESANS SET'S SIGNS: MS Sans Serif, the face it stands in for,
//   centred all five at 0.389 of its 9-row cap,
//   while FreeSans keeps its hyphen near there (240..312 of 1000, centre 276,
//   0.379 of its 729-unit cap) but draws the four signs on Helvetica's math
//   axis, 0.318 ("+" -10..474, "=" 111..353, "<" ">" -9..474), so its plus
//   sits visibly low beside the digits it signs. The lifts: the regular
//   file's 0.044 em ("+", "=") and 0.0435 ("<", ">"), the bold's 0.043 and
//   0.0425 (its hyphen 207..342) — read 2026-10-09 off FreeSans through
//   FreeType, EQUAL TO THE FIGURES RECORDED FOR NIMBUS SANS on 2026-10-06 to
//   the unit; at 300 % the body's plus rises 1.51 device px at its
//   x-height em (2026-10-09), the small's 1.08.
//   THE LIBERATION SET'S SIGNS SIT ABOVE THE AXIS, SO THEY MOVE DOWN
//   (2026-10-09): Liberation Sans keeps its hyphen at 0.386 of its cap
//   (464..624 of 2048, centre 544, against the "H" 1409 — MS Sans Serif's
//   own 0.389) but draws the four on Arial's math axis, higher ("+"
//   180..1182, centre 681; "=" 344..1004, 674; "<" ">" 154..1194, 674), the
//   opposite of FreeSans. The lifts: the regular file's -0.067 em ("+") and
//   -0.063 ("=", "<", ">"); the bold's (its hyphen 409..653, centre 531;
//   "+" 161..1201, "=" 291..1065, "<" ">" 125..1229) -0.073, -0.072 and
//   -0.071 — the planner's measurement in font units, 2026-10-09; at 300 %
//   the body's plus falls 2.28 device px at its x-height em. NOTHING
//   CLAMPS A LIFT AT 0: the install stores the signed difference
//   (measure_sign_axis, gui_font_bundled.cpp) and the one reader adds it
//   whatever its sign (gui_sign_lift_px, text_shape's offset).
//   A set that does not lift a mark answers its lift 0: the head records
//   why Tahoma's signs need none, and which sets lift the pipe.
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
// glyph (the figure and its reason at each set above; every set 0 since
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

// THE LINUX BINARY'S COPY OF THE SIX FILES, in kGuiFontFiles' order,
// defined by gui_font_embedded.cpp, which only the Linux target compiles (the
// APK carries the same files as assets instead).
extern const GuiFontBytes gui_font_embedded_files[kGuiFontFileCount];
