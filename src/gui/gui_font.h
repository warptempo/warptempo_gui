#pragma once

// THE ONE FACE OWNER: every text surface names its face here and nowhere
// else, so the painters never name a font. THE PRODUCT'S FACES ARE ITS OWN
// (architect 2026-10-02: "the app becomes its own thing; it has its own
// fonts"): eight files under the repository's `fonts/` (fonts/README.md has
// their provenance and licences), carried by both binaries — compiled into
// the Linux executable (gui_font_embedded.cpp), shipped as the APK's assets
// on Android — and turned into faces by ONE implementation on both devices,
// gui_font_bundled.cpp, through FreeType. Neither device asks anything for a
// face: fontconfig is never consulted on the laptop and the tablet has
// nothing to consult.
//
// FOUR FACE SETS, ONE LIVE (architect 2026-10-06, the Windows 2000 pivot;
// the second set 2026-10-07 with the clearlooks chrome; the third,
// kGuiFaceSetCde — Go Sans — 2026-10-08 with the cde chrome; the fourth,
// kGuiFaceSetMsSansSerif — FreeSans, the MS Sans Serif stand-in —
// 2026-10-09): a set is one desktop's text — which file each of the three
// uses (GuiFace) is drawn from, its recorded vertical metrics, its tracking
// and whether its math signs are lifted (GuiFaceSet below) — and the chrome
// spec names its own set (chrome_spec.h's ChromeSpec), the device config's
// `chrome` choosing the live spec once at launch; UNDER THE WINDOWS CHROME
// THE LIVE SCHEME'S FACE TAG may choose the MS Sans Serif set in its place
// (GuiSchemeFace and gui_live_face_set below, architect 2026-10-09: the
// face follows the scheme, as Windows' Appearance schemes carry their
// font). THE
// WIN2000 SET, kGuiFaceSetWin2000: WINE TAHOMA as ReactOS 0.4.16 ships it
// (tahoma.ttf, tahomabd.ttf; TrueType outlines on a Bitstream Vera base),
// the face the ReactOS captures are set in — its 11-ppem strike equals
// tmp/reactos.png's menu and title pixels exactly (measured 2026-10-06).
// THE GNOME2 SET, kGuiFaceSetGnome2: DEJAVU SANS as Debian 6 squeeze ships
// it (DejaVuSans.ttf, DejaVuSans-Bold.ttf), GNOME 2's Sans 10, the face his
// squeeze captures are set in. THE MS SANS SERIF SET,
// kGuiFaceSetMsSansSerif: GNU FREEFONT'S FREESANS (FreeSans.otf,
// FreeSansBold.otf; CFF outlines, URW's Nimbus Sans L under GNU FreeFont's
// extensions), standing in for the MS Sans Serif Windows set its chrome in
// before Windows 2000's Standard scheme took Tahoma. In each set the body is
// the regular file,
// the bold (the caption's title alone) the bold file, the small (the
// ruler's labels until 2026-10-09, when the ruler became Cool Edit's) the
// regular file at the smaller em, and THE PROGRAM'S (architect
// 2026-10-09, the program is Cool Edit: the ruler's digits and the cues'
// labels at Cool Edit's cap of 7 W, METRICS §4.2 and §4.4) the regular file
// at the em that stands its "H" seven rows tall.
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
// of 2048, DejaVu Sans's "0" 1549 — the body 3 % over Sans 10's 13.333);
// 2026-10-09 for the MS Sans Serif set at its 9-row cap: 12.346, 12.346 and 8.197
// Windows px (FreeSans's and FreeSans Bold's "H" both 729 of 1000, FreeSans's
// "0" 732 — Nimbus Sans's own figures, the outlines being Nimbus's).
// The em is vertical alone: horizontally the face is its own, and a taller
// or deeper glyph ("$", a descender) may poke past a box sized off the
// recorded rows — accepted (architect 2026-10-05). A FACE'S OWN LINE BOX IS
// NEVER READ (Tahoma's hhea ascent is 2049 of 2048, above its caps;
// DejaVu's 1901 / -483 happens to round to the recorded 13 / 4 at Sans 10,
// and is not read either; FreeSans's 900 / -200 of 1000 stands 171 units
// above its caps, so a program seating text on that box sets it low): the
// recorded metrics seat every run, every label's cap band centred by the
// recorded cap (redesign_baseline) or seated on the recorded ascent
// (line_baseline), so no face's line box can misplace a label. HORIZONTALLY THE FACE IS ITS OWN: every
// width used for layout is the shaped run's (text_shape), the face's own
// advances at the live size less the tracking.
//
// THE FOUR MATH SIGNS ARE WHERE THE FACE DRAWS THEM, EXCEPT IN A SET THAT
// LIFTS THEM (GuiFaceSet::sign_lift; gui_sign_axis below). Tahoma, DejaVu
// and Go do not lift: Tahoma already draws the five on one axis — its
// 11-ppem strike centres "-", "+", "<", ">" and "=" all on row 3 of the
// 8-row cap, and its outlines agree to a few units (the hyphen 566..730 of
// 2048, centre 648; "+" and "=" centred at 652, "<" ">" at 654; the bold
// within one unit) — so a derived lift would come to under 0.003 em, a
// tenth of a device px at 400 %, nothing for a mechanism to correct. THE
// MS SANS SERIF SET LIFTS (architect 2026-10-09: FreeSans is Nimbus's outlines, and
// the lift deleted with Nimbus on 2026-10-06 returns with them).

#include <cairo/cairo.h>

#include <cstddef>
#include <cstdint>

enum class GuiFace { Body, Bold, Small, Program };
inline constexpr std::size_t kGuiFaceCount = 4;

// THE EIGHT FILES, THE INSTALL'S ONE ORDER, EVERY SET'S (architect
// 2026-10-07, the second vocabulary's pair joining the first's; 2026-10-08
// the third's, Go; 2026-10-09 the fourth's, FreeSans): THIS LIST
// IS THE ONE PLACE THE NAMES ARE SPELLED — the Linux font step
// (CMakeLists.txt) and the APK's asset step (android/app/build_apk.sh) each
// read the quoted names out of this initializer, so a file added here is
// embedded and packed with no second list; the two backends hand the bytes
// in in this order, and a set names its files by their index here. Every
// set's files are carried whichever chrome a device runs (the `chrome` key
// is read after the Android backend installs the faces, and the install
// measures every set's ems at once, gui_font_bundled.cpp). FreeSans's two
// are OpenType CFF (".otf"), which the one loader takes as it takes the
// TrueType six (FT_New_Memory_Face reads either; gui_font_bundled.cpp's head
// on the hinter each format meets).
inline constexpr std::size_t kGuiFontFileCount = 8;
inline constexpr const char* kGuiFontFiles[kGuiFontFileCount] = {
    "tahoma.ttf",                 // 0: win2000's body and small
    "tahomabd.ttf",               // 1: win2000's bold
    "DejaVuSans.ttf",             // 2: gnome2's body and small
    "DejaVuSans-Bold.ttf",        // 3: gnome2's bold
    "Go-Regular.ttf",             // 4: cde's body and small
    "Go-Bold.ttf",                // 5: cde's bold
    "FreeSans.otf",               // 6: MS Sans Serif's body and small
    "FreeSansBold.otf",           // 7: MS Sans Serif's bold
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
// eight files selects as an FT-BACKED cairo face, which is what text_shape
// requires, and EVERY set's faces (kGuiFaceSets below) carry the glyph each
// em is measured on and, in a set that lifts its signs, the hyphen and the
// four signs its axis is measured on — every set, because the install
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

// THE PROGRAM FACE'S RECORDED METRICS, every set's (2026-10-09): Cool Edit's
// cap of 7 W for its ruler digits and its cue labels, read off his captures
// (tmp/research/cool_edit/METRICS.md §4.2, §4.4); the ascent and descent
// the cap's own proportion of the body's 11 / 2 over 8, rounded: {10, 2, 7}.
// No lane reads the cell: the ruler and the marker lane seat these runs on
// their own authored rows (program_spec.h), so the cell is a record and the
// cap the em's measure.
inline constexpr GuiFaceMetrics kGuiProgramFaceMetrics = {10, 2, 7};

// ONE CHROME VOCABULARY'S TEXT (architect 2026-10-06): per use (GuiFace, in
// its order), the kGuiFontFiles index the use is drawn from and its recorded
// metrics; the tracking in Windows px per glyph; whether the four math signs
// are set onto the hyphen's axis (gui_sign_axis below; true for the MS
// Sans Serif set alone, 2026-10-09).
struct GuiFaceSet {
    std::size_t    file[kGuiFaceCount]    = {};
    GuiFaceMetrics metrics[kGuiFaceCount] = {};
    double         tracking_px            = 0.0;
    bool           sign_lift              = false;
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
    .file        = {0, 1, 0, 0},
    .metrics     = {{11, 2, 8}, {11, 2, 8}, {6, 0, 6}, kGuiProgramFaceMetrics},
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
    .file        = {2, 3, 2, 2},
    .metrics     = {{11, 2, 8}, {11, 2, 8}, {6, 0, 6}, kGuiProgramFaceMetrics},
    .tracking_px = 0.0,
};

// THE CDE SET (architect 2026-10-08, ruling 4 of the CDE arc: "excellent
// choice"), the cde chrome's: GO SANS — Go Regular and Go Bold, Bigelow &
// Holmes' 2016 faces for the Go project (fonts/README.md; the Go project's
// BSD-3-style license) — THE LUCIDA STAND-IN. Solaris 9's CDE set its
// chrome in B&H Lucida Sans, a 14-px bitmap strike at 75 dpi (cap 10 in a
// 15-row cell; tmp/research/cde_solaris/report.md §4), which is proprietary
// and a strike besides (NO BITMAP FONTS, the head); Go is the only open face
// by Lucida's own designers, in its humanist proportions.
//   METRICS: THE BASE'S CELL, {11, 2, 8} for the body and the bold — the
//   settled rule for every later vocabulary, period authentic except for
//   the proportional fit to Windows' layout (CLAUDE.md's chrome row): Go
//   Regular drawn to a cap of 8 W px, its em derived at the install off its
//   own "H" (1480 of 2048, 0.723 em, read 2026-10-08 off the file through
//   FreeType) — about 11.07 W px — the bold file's "H" the same height; the
//   small face the same SIX-row digit as the other sets' (its em about 8.10
//   off the "0", 1517 of 2048), so the ruler's digits are the base's. CDE's
//   own cap 10 / cell 15 is NOT reproduced.
//   THE BOLD USE (the caption's title alone) names the bold file so the set
//   is whole, but UNDER cde THE TITLE IS SET IN THE BODY FACE: dtwm draws
//   the title in the interface font, the same medium face as the menus (the
//   notepad capture, title cap 10 at 1-px stems; paint_caption_row's cde
//   arm) — nothing reads the bold while cde is live.
//   TRACKING: none (the heights-only rule, as the other two sets).
inline constexpr GuiFaceSet kGuiFaceSetCde = {
    .file        = {4, 5, 4, 4},
    .metrics     = {{11, 2, 8}, {11, 2, 8}, {6, 0, 6}, kGuiProgramFaceMetrics},
    .tracking_px = 0.0,
};

// THE MS SANS SERIF SET (architect 2026-10-09: "let's use free sans"), NO
// CHROME'S OWN: the windows-2000 chrome wears it while the live scheme's
// face tag is MS Sans Serif (gui_live_face_set below) — GNU FREEFONT'S
// FREESANS, FreeSans and FreeSans Bold of GNU FreeFont 20120503, its last
// release (fonts/README.md; GPL-3.0-or-later with the font exception), THE
// MS SANS SERIF STAND-IN. Windows set its chrome in MS Sans Serif until
// Windows 2000 made Tahoma its Standard scheme's face — Windows 95's and
// 98's schemes name it, most of Windows 2000's other schemes name its
// TrueType twin Microsoft Sans Serif, and Windows Me's classic desktop is
// Windows 2000's chrome and Standard colors in MS Sans Serif (his
// guidebookgallery captures tmp/winme.png and tmp/win2000pro.png, WordPad
// at identical metrics; the catalog's windows-me-standard); MS Sans Serif has no open outline (Wine's
// is bitmap strikes — NO BITMAP FONTS, the head — and Microsoft Sans Serif
// is proprietary), and FreeSans is a Helvetica, the genre MS Sans Serif was
// drawn from, its Latin outlines URW's Nimbus Sans L (the face of the
// win95 vocabulary dropped 2026-10-06).
//   METRICS: MS SANS SERIF 8 AT 96 DPI, {11, 2, 9} for the body and the
//   bold — the base's 13-row cell (ascent 11 + descent 2), so every lane
//   and box keeps its 13, with CAPS AND DIGITS 9 ROWS, measured 2026-10-09
//   on tmp/winme.png (1:1): the menu bar's "F", "E", "V", "I", "H" rows
//   27..35, the status line's "F" 394..402, the caption's bold "S" and "W"
//   8..16 — nine rows each, where tmp/win2000pro.png's Tahoma stands eight
//   (the menu's "F" 28..35, the bold "D" 9..16); the win95 set recorded the
//   same {11, 2, 9}. The em derives at the install off FreeSans's own "H"
//   (729 of 1000 in both weights): 12.346 W px. THE SMALL IS THE BASE'S
//   SIX-ROW DIGIT, {6, 0, 6}, not the win95 set's {7, 0, 7}: that was Small
//   Fonts' digit as ACID and Vegas drew their rulers, the win95
//   vocabulary's own model; Windows Me's WordPad ruler is Windows 2000's
//   pixel for pixel (the two captures' digits, rows 110..118, identical),
//   so the ruler under MS Sans Serif differs from the base's in nothing and
//   keeps its digit — and with it the base's ruler lane, the stack and the
//   well, which a live swap of the two sets must not move (same_lanes
//   below). Its em 8.197 off FreeSans's "0" (732 of 1000).
//   TRACKING: none (the heights-only rule: the face's own advances and
//   kerning, "no manual kerning — let the spacing be set by the font",
//   architect 2026-10-09).
//   SIGNS: LIFTED onto the hyphen's axis (gui_sign_axis below; architect
//   2026-10-09, the lift returning with Nimbus's outlines).
inline constexpr GuiFaceSet kGuiFaceSetMsSansSerif = {
    .file        = {6, 7, 6, 6},
    .metrics     = {{11, 2, 9}, {11, 2, 9}, {6, 0, 6}, kGuiProgramFaceMetrics},
    .tracking_px = 0.0,
    .sign_lift   = true,
};

// EVERY SET THE OWNER DEFINES (2026-10-09), the install's probe walking it
// (gui_font_install_bundled: each set's files must carry the glyphs its ems
// and its sign axis are measured on) — every set, a chrome spec's own or
// one a scheme's face tag chooses, so each is proven installable whichever
// chrome and scheme the device later names.
inline constexpr const GuiFaceSet* kGuiFaceSets[] = {
    &kGuiFaceSetWin2000,
    &kGuiFaceSetGnome2,
    &kGuiFaceSetCde,
    &kGuiFaceSetMsSansSerif,
};

// A LIVE SWAP BETWEEN THE WINDOWS CHROME'S TWO SETS MOVES NO LANE (the
// face follows the scheme live, gui_live_face_set below): the two record
// the same cell, ascent 11 and descent 2 for the body and the bold, and
// the same six-row small digit and the same program face — only the body's
// cap differs (8 / 9), which no lane height reads — so the stack and the
// well keep their rows across a swap (since 2026-10-09 the ruler and the
// marker lane are the program's authored rows, program_spec.h, and read no
// face at all).
constexpr bool same_lanes(const GuiFaceSet& a, const GuiFaceSet& b) {
    for (std::size_t i = 0; i < kGuiFaceCount; ++i) {
        if (a.metrics[i].ascent != b.metrics[i].ascent ||
            a.metrics[i].descent != b.metrics[i].descent)
            return false;
    }
    const std::size_t small = static_cast<std::size_t>(GuiFace::Small);
    return a.metrics[small].cap == b.metrics[small].cap;
}
static_assert(same_lanes(kGuiFaceSetWin2000, kGuiFaceSetMsSansSerif));

// THE SCHEME'S FACE TAG (architect 2026-10-09: the face follows the
// scheme, as a Windows Appearance scheme carries its font): which of the
// Windows chrome's two faces a scheme names — every built-in's read from
// its own source (tools/theme_catalog/gen_theme_files.py's head: the
// menu font the source records, Tahoma where it names Tahoma, MS Sans
// Serif for every other face), a scheme file's from its optional `font`
// line (palette_file.h's head) — carried in the live pick
// (render.h's GuiChromePick::face). TAHOMA IS ALSO THE INERT DEFAULT: a
// non-Windows scheme names Tahoma, which under the windows chrome is the
// chrome's own face and under clearlooks and cde is never read.
enum class GuiSchemeFace { Tahoma, MsSansSerif };

// THE LIVE SET — THE ONE RESOLUTION (architect 2026-10-09): THE CHROME
// SPEC'S OWN (chrome_spec.h's live_chrome_spec(), chosen once at launch by
// the device config's `chrome`, which names its set by address), EXCEPT
// UNDER THE WINDOWS CHROME WHILE THE LIVE SCHEME'S FACE TAG IS MS SANS
// SERIF, when it is the MS Sans Serif set. Clearlooks and cde keep their
// own faces whatever scheme is live (their schemes are not Windows'). The
// live tag is the installed pick's (set_live_scheme_face, written by the
// install family's chrome half alone — render.cpp's fill_chrome_palette, at
// the launch's install_palette and at every live pick through
// install_chrome_pick), so the face moves LIVE with the scheme: the caches
// that hold the face key the set (gui_font_bundled.cpp's scaled fonts,
// paint_handler.cpp's time-field memo), the lanes cannot move (same_lanes
// above), and the picker's install damages the whole surface on any change
// of the scheme (GuiColorPicker::install_live_words). The include stands
// here, after the sets it names and before the first reader.
#include "chrome_spec.h"
namespace gui_font_detail {
inline GuiSchemeFace g_live_scheme_face = GuiSchemeFace::Tahoma;
} // namespace gui_font_detail
inline void set_live_scheme_face(GuiSchemeFace face) {
    gui_font_detail::g_live_scheme_face = face;
}
inline const GuiFaceSet& gui_live_face_set() {
    const ChromeSpec& spec = live_chrome_spec();
    if (spec.vocabulary == GuiChromeVocabulary::Win2000 &&
        gui_font_detail::g_live_scheme_face == GuiSchemeFace::MsSansSerif)
        return kGuiFaceSetMsSansSerif;
    return *spec.face_set;
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

// THE FOUR MATH SIGNS SIT ON THE HYPHEN'S AXIS IN A SET THAT LIFTS THEM
// (GuiFaceSet::sign_lift; architect 2026-10-06, "as MS Sans Serif had
// them", deleted with Nimbus that evening, RETURNED 2026-10-09 with
// FreeSans, whose outlines are Nimbus's): "+" (U+002B), "=" (U+003D), "<"
// (U+003C) and ">" (U+003E) each rise by the hyphen's ink centre less its
// own, in every use of the set and every run; the hyphen and every other
// glyph stay where the face drew them. One rule: the period's faces put the
// five marks on ONE axis, and the lift seats a face's signs there.
//   THE MS SANS SERIF SET: MS Sans Serif centred all five at 0.389 of its 9-row cap,
//   while FreeSans keeps its hyphen near there (240..312 of 1000, centre 276,
//   0.379 of its 729-unit cap) but draws the four signs on Helvetica's math
//   axis, 0.318 ("+" -10..474, "=" 111..353, "<" ">" -9..474), so its plus
//   sits visibly low beside the digits it signs. The lifts: the regular
//   file's 0.044 em ("+", "=") and 0.0435 ("<", ">"), the bold's 0.043 and
//   0.0425 (its hyphen 207..342) — read 2026-10-09 off FreeSans through
//   FreeType, EQUAL TO THE FIGURES RECORDED FOR NIMBUS SANS on 2026-10-06 to
//   the unit; at 300 % the body's plus rises 1.63 device px, the small's
//   1.08.
//   The sets that do not lift (Tahoma, DejaVu, Go) answer every lift 0: the
//   head records why Tahoma needs none.
// THE LIFTS ARE DERIVED at the install, like the em, from each file's own
// unscaled outline bounds (gui_font_bundled.cpp), in em units, for every
// file; a set's use reads its file's when the set lifts. THE MATCH IS BY
// GLYPH ID after substitution (text_shape compares a shaped glyph's id
// against `glyph`), never by codepoint: a cluster is not a glyph. The lift
// moves the ink alone — every advance, the run's width and the tracking are
// untouched.
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
// The live set's axis for `face`: its file's measured lifts where the set
// lifts its signs, else every lift 0 (and every glyph id 0, which a lift of
// 0 makes harmless however a run's glyph ids compare).
const GuiSignAxis& gui_sign_axis(GuiFace face);

// The lift of one shaped glyph in DEVICE px at the font's scale, HarfBuzz's
// sense (up-positive), unrounded: the sign's lift times the em times the
// scale, 0 for every glyph that is not one of the four signs and for every
// glyph of a set that does not lift. text_shape's one reader.
inline double gui_sign_lift_px(const GuiFont& f, unsigned glyph) {
    for (const GuiSignLift& s : gui_sign_axis(f.face).signs) {
        if (s.lift_em != 0.0 && s.glyph == glyph)
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

// THE LINUX BINARY'S COPY OF THE EIGHT FILES, in kGuiFontFiles' order,
// defined by gui_font_embedded.cpp, which only the Linux target compiles (the
// APK carries the same files as assets instead).
extern const GuiFontBytes gui_font_embedded_files[kGuiFontFileCount];
