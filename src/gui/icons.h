#pragma once

// THE ICONS (architect 2026-10-06; the set cut to the drawings on screen
// 2026-10-25; BREEZE'S GLYPHS ON EVERY KEY 2026-11-02). An Icon is a drawing
// the GUI paints, ONE FILE PER ENUMERATOR in ONE FLAT FOLDER, assets/icons/,
// each file NAMED BY ITS ENUMERATOR (`<Name>.svg`; the folder's README the
// drawings, their provenance and their licence). TWO KINDS STAND THERE:
//   THE KEY GLYPHS — KDE's BREEZE drawings (breeze-icons 6.31.0's dark
//   theme, the 22-px folders), RECOLORED to `currentColor` by
//   tools/icons/import_breeze.py (the README's RECOLOR RULE; its one table
//   is the enumerator -> source list) and PAINTED IN THE TEXT TONES
//   (architect 2026-11-01, M4 / M5: "Breeze is committed", one visual size,
//   the legend kept under the key; M7: Breeze only, monochrome re-coloring
//   allowed): every key carries one, centered on its face (paint_key_glyph,
//   paint_handler.cpp), kTextInk B5B5B5 at rest, kTextDimInk 757575 on a
//   disabled key, kTextOnLit 111111 on a lit one (materials.h's text
//   tones), placed by THE INK BOX (below). The roster is a draft for the
//   architect's final picking (2026-11-02: "tmp icons fine"); a re-pick is
//   a row of the importer's table and a rerun.
//   THE PROJECT'S OWN DRAWINGS, the boxed glyphs (below) — the quaver (the
//   logo lane, the wav rows, the launcher), the folder (the folder rows)
//   and the cards' i and X — in their own bone ink, until Breeze replaces
//   them too (M7's ruling; the next step of the arc).
// PARSED AT LAUNCH through resvg (svg_icon.h: the wrapper and the error
// rule), straight from the bundle — never copied into the config folder
// (load_set, below) — and rasterised lazily into an ARGB32 surface that a
// draw copies at an integer device px, no resample: a boxed glyph per
// (drawing, device px), a key glyph per (drawing, ink px, color).
//
// THE BOUND DRAWING (architect 2026-11-02; the road of 2026-10-10 restored):
// a file that paints in `currentColor` — every Breeze file, and none of the
// project's own — WEARS THE COLOR ITS CALLER PASSES, a 0xRRGGBB value (one
// of the three text tones; no role table): its bytes are kept and parsed
// per color with resvg's user stylesheet `svg { color: #RRGGBB; }`
// (svg_icon::parse), which `currentColor` resolves against — the file never
// edited at run time. The parses are held per (drawing, color) and the
// rasters keyed on the color. A file with no `currentColor` keeps its one
// parse and its own inks. No drawing wears a shadow (the flat look,
// 2026-11-01).
//
// NO CURSOR IS AN ICON: the product shows no pointer cursor at all (the
// mouse is closed, architect 2026-10-30), so it ships no cursor art and
// draws none.
//
// GUI-ONLY, like text_shape: icons exist only where pixels do,
// and warptempo_cli must never carry this TU (nor svg_icon's, nor resvg).

#include "render.h"        // skin_px

#include <cairo/cairo.h>

#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>

namespace icons {

// THE KEY GLYPHS, the Breeze drawings in the importer's table order
// (tools/icons/import_breeze.py's ROSTER, which writes `<Name>.svg` for each):
// ONE LIST, spelled once, from which the enumerators and their file names
// are both made (Icon and kIconNames below), so the two cannot part. Which
// key wears which is the act vocabularies' own owners (redesign_button_icon,
// app_state.h; render_player_button_icon, score_viewer_button_icon,
// modal_word_icon, paint_handler.cpp / score_viewer.h). Folder, HelpAbout
// and the two batteries are imported for the off-key seats the next step
// moves onto Breeze (the quaver's seats taking MusicNote16th, BPM's glyph);
// no painter reads those four yet.
#define WARPTEMPO_BREEZE_ICONS(X)                                           \
    X(DocumentOpen) X(DocumentSave) X(DocumentSaveAs)                       \
    X(VcsPull) X(NetworkConnect) X(VcsCommit)                               \
    X(EditUndo) X(EditRedo) X(PreviewRenderOn)                              \
    X(DialogCancel) X(EditCopy) X(ZoomInY)                                  \
    X(ZoomOutY) X(ZoomFitBest) X(ZoomPrevious)                              \
    X(Zoom) X(ChronometerStart) X(TimelineUseZoneOn)                        \
    X(TransformCrop) X(MusicNote16th) X(Mathmode)                           \
    X(Merge) X(MediaPlaylistPlay) X(Lock)                                   \
    X(SettingsConfigure) X(HelpWhatsthis) X(ViewMediaLyrics)                \
    X(Raindrop) X(ShallowHistory) X(DeepHistory)                            \
    X(GnumericAutosum) X(DocumentRevert) X(ArrowLeft)                       \
    X(ArrowRight) X(DocumentImport) X(ViewMediaTrack)                       \
    X(ViewMediaVisualization) X(LabplotXyCurve) X(ViewRefresh)              \
    X(MediaPlaybackStart) X(MediaPlaybackStop) X(MediaPlaybackPause)        \
    X(MediaSkipBackward) X(MediaSkipForward) X(MediaSeekBackward)           \
    X(MediaSeekForward) X(MediaRepeatSingle) X(ListAdd)                     \
    X(EditDelete) X(ViewHidden) X(InsertLink)                               \
    X(Link) X(GoJump) X(DocumentEdit)                                       \
    X(EditSelect) X(TimelineLift) X(GoDown)                                 \
    X(GoUp) X(KeyframePrevious) X(KeyframeNext)                             \
    X(TabDetach) X(ZoomOriginal) X(GoPrevious)                              \
    X(GoNext) X(GoParentFolder) X(GoPreviousViewPage)                       \
    X(GoNextViewPage) X(EditReset) X(ZoneIn)                                \
    X(ZoneOut) X(DialogClose) X(DialogOkApply)                              \
    X(EditClear) X(Folder) X(HelpAbout)                                     \
    X(Battery000) X(Battery000Charging)

// The drawings, one per file of assets/icons/ (each file named by its
// enumerator — kIconNames below): the Breeze key glyphs, then the project's
// own.
enum class Icon {
#define WARPTEMPO_ICON_ENUM(name) name,
    WARPTEMPO_BREEZE_ICONS(WARPTEMPO_ICON_ENUM)
#undef WARPTEMPO_ICON_ENUM
    // THE LIST ROWS' TWO GLYPHS (the folder overlay, the project picker),
    // BOXED DRAWINGS since 2026-10-23 (THE BOXED GLYPHS, below).
    FolderRow,           // a folder row — the planner's folder, a square
                         // with a tab at the top (architect 2026-10-23: "just
                         // a simple square with a tab at the top")
    WavRow,              // a wav row — the quaver, a symlink to Quaver.svg
                         // (architect 2026-10-23: "use the logo in the
                         // render player")
    // THE NOTIFICATION CARDS' CLASS GLYPHS (notifications.h), BOXED
    // DRAWINGS since 2026-10-24 (THE BOXED GLYPHS, below).
    CardNormal,          // a NORMAL card's glyph — the planner's i, a small
                         // rectangle over a larger one (architect
                         // 2026-10-24: "Spartan and minimalist")
    CardCritical,        // a CRITICAL card's glyph — the planner's X, the
                         // old caption's close at its stroke ratio
                         // (architect 2026-10-24)
    AppIcon,             // the program icon outside the window, a symlink
                         // to Quaver.svg (architect 2026-10-23: "the new note
                         // glyph on the caption's color"): the launcher
                         // icon a build product of this file
                         // (apk/build_apk.sh's launcher step). No
                         // painter draws it; the launch's load vouches that
                         // the file parses
    Quaver,              // the logo lane's quaver (architect 2026-10-23):
                         // Gemini Argon's one-path conversion of the
                         // planner's bitmap at his prompt, a BOXED DRAWING
                         // (below); the wav rows wear it too (WavRow's link)
};

// Roster size, the names' and the load's count (load_set parses one
// document per enumerator): the Breeze list's and the project's six.
#define WARPTEMPO_ICON_ONE(name) +1
inline constexpr int kBreezeIconCount =
    0 WARPTEMPO_BREEZE_ICONS(WARPTEMPO_ICON_ONE);
#undef WARPTEMPO_ICON_ONE
inline constexpr int kIconCount = kBreezeIconCount + 6;
static_assert(static_cast<int>(Icon::Quaver) + 1 == kIconCount);

// THE FILE NAMES, the enumerators spelled in enum order: each enumerator's
// file in assets/icons/ is `<name>.svg`. A misspelling is a missing file at
// the next launch, which fails it. THE REPEATS ARE SYMLINKS, relative, inside
// the folder: AppIcon.svg -> Quaver.svg and WavRow.svg -> Quaver.svg (the
// folder's README lists them). The bundle's road dereferences a link
// (build_apk.sh's copy, cp -L), so the load reads a link as the file it
// names. (Breeze's own repeats — ArrowLeft and GoPreviousViewPage are
// GoPrevious's drawing, ArrowRight and GoNextViewPage GoNext's — are
// separate files, each written by the importer from its own source name.)
inline constexpr const char* kIconNames[] = {
#define WARPTEMPO_ICON_NAME(name) #name,
    WARPTEMPO_BREEZE_ICONS(WARPTEMPO_ICON_NAME)
#undef WARPTEMPO_ICON_NAME
    "FolderRow", "WavRow", "CardNormal", "CardCritical", "AppIcon", "Quaver",
};
static_assert(std::size(kIconNames) == kIconCount);

// THE BOXED GLYPHS (architect 2026-10-23: the quaver, "use the logo in the
// render player, but then we need a folder icon as well … the same
// proportion as the caption"; the cards' i and X 2026-10-24, "sit nicely
// with the other icons, the note and the folder"): EVERY DRAWING IS DRAWN TO
// A BOX — a 34 x 40-unit viewBox (kGlyphBoxW x kGlyphBoxH) whose ink stands
// on rows 7..34, kGlyphBoxInkRows = 27 tall — and PLACED BY THE BOX, its
// rows centered on the seat, its left edge the seat's x, never centered on
// its ink. ONE RULE FOR EVERY BOXED SEAT: the box is the ink's 40 / 27,
// ROUNDED AT THE BOX. A seat whose ink is authored in device px at 300 %
// takes icon_box_px — the logo lane's 21-px ink (skin_px(21 · 40 / 27), the
// box 31 tall and 26 wide at 300 %) and the list rows' 27-px ink, .525 of the
// 51-px row (skin_px(40), the box 40 tall and 34 wide); a seat whose ink is
// already live device px, derived from live lengths, takes icon_box_live_px
// — the cards' ink, .525 of the first line's inside height (notifications.h's
// kNotificationGlyphRatio: the box 40 tall and 34 wide at 300 %).
inline constexpr double kGlyphBoxW       = 34.0;
inline constexpr double kGlyphBoxH       = 40.0;
inline constexpr double kGlyphBoxInkRows = 27.0;
// The box's height, device px, for a seat whose ink is `ink_px_at300`
// device px tall at 300 % (skin_px, rounded at the element, floored at 1).
inline int icon_box_px(double ink_px_at300) {
    return skin_px(ink_px_at300 * kGlyphBoxH / kGlyphBoxInkRows, 1);
}
// The box's height, device px, for a seat whose ink is `ink_px` LIVE device
// px tall (the same 40 / 27, rounded at the box with std::nearbyint,
// floored at 1).
inline int icon_box_live_px(double ink_px) {
    const int h = static_cast<int>(
        std::nearbyint(ink_px * kGlyphBoxH / kGlyphBoxInkRows));
    return h < 1 ? 1 : h;
}
// The box's width at height `box_h`, the viewBox's proportion, rounded.
inline int icon_box_w_px(int box_h) {
    return static_cast<int>(std::nearbyint(box_h * kGlyphBoxW / kGlyphBoxH));
}

// THE LOAD, once, in gui_main before the window: the folder's files fetched
// from the bundle (GuiPlatform::bundled_icon_files — the APK's icons/
// assets; READ IN PLACE, never copied into the config folder: nobody authors
// an icon on the device), then per enumerator in enum order `<name>.svg` parsed
// (svg_icon::parse), all before any draw; files of other names (the README,
// the licence text) ignored. THE ERROR ARM'S PRODUCERS: the bundle
// unreadable (IO), the file not there, or a file not well-formed SVG (a
// build defect) — "icons: Quaver.svg: missing", the first only, which
// gui_main prints and exits on (the validation doctrine's class (1)). A
// construct resvg does not draw passes the load (svg_icon.h's error rule).
std::optional<std::string> load_set();

// THE RASTERS DROPPED, on a scale change (GuiInputHandler::apply_gui_scale)
// and on a flip of True Colors (GuiInputHandler::apply_true_colors — the
// rasters are converted for the window as they are made, svg_icon.h): the
// cache refills lazily at the new sizes and in the new conversion, the key
// glyphs' fits with them; the parsed drawings stay (a bound parse keys its
// sRGB color, which neither change moves). (No color pick drops anything:
// the key glyphs' colors are the three constant text tones.)
void drop_rasters();

// A BOXED GLYPH (THE BOXED GLYPHS, above) in its box: `box_h` device px
// tall (icon_box_px), its top-left (box_x, box_y). The raster is a square
// cell box_h a side, the drawing fitted to its height and centered across it
// (svg_icon::rasterise), so the cell stands half the cell's spare width left
// of the box's edge. Cairo state is saved and restored. Cannot fail: the
// launch's load parsed every drawing.
void draw_boxed(cairo_t* cr, Icon icon, int box_x, int box_y, int box_h);

// A BOXED GLYPH'S INK at `box_h` (architect 2026-10-24, the logo lane's
// quaver: "the glyph flush with the icons"): its lit columns and rows
// MEASURED OFF THE LIVE RASTER draw_boxed copies — a pixel is ink at alpha
// >= 128 — in device px from the box's top-left as draw_boxed places it,
// `left` / `top` the first ink column and row, `right` / `bottom` one past
// the last; never estimated from the file's box (the drawing's antialiasing
// and its fit to the raster decide where its ink lands). Measured once per
// (glyph, size), beside the raster in its cache (drop_rasters empties
// both). A raster with no ink answers four zeros. Its one reader is the
// logo lane's painter (paint_handler.cpp's paint_logo_lane).
struct BoxedInk {
    int left = 0, top = 0, right = 0, bottom = 0;
};
BoxedInk boxed_ink(Icon icon, int box_h);

// -- THE INK BOX: the key glyphs' one visual size (architect 2026-11-01, M4 /
// M5: "the glyphs smaller … the problem with M3 was the uneven size, e.g.
// DRAG was tiny" -> ONE VISUAL SIZE for every glyph) ---------------------
//
// A KEY GLYPH IS SCALED BY ITS INK, never by its viewBox: the drawing is
// rendered at a uniform zoom from its origin (svg_icon::rasterise_zoom) and
// its INK BOX — the bounds of every pixel whose alpha is > 0, the
// antialiased rim included — is cropped out; THE FIT is the zoom at which
// the ink box's LARGER SIDE is `ink_px`, found as the mock found it
// (the approved M5 mock's own fit): a probe at zoom 8 measures
// the ink in user units, the zoom is `ink_px` over its larger side, and the
// zoom steps down by 1 % while the rim's extra row makes it overrun.
// Callers pass skin_px(20) (kKeyGlyphInkAt300, paint_handler.cpp: 20 device
// px at 300 %, scaled as every chrome length). The fit is the drawing's
// alpha alone, so it is held per (drawing, ink px) and shared by the
// colors; the cropped raster per (drawing, ink px, color), converted for the
// window as every raster (svg_icon::convert_to_display). Any drawing may be
// asked; the project's own paint in their own inks whatever `color` says.

// The ink box's size, device px, at `ink_px` (its larger side `ink_px` or
// less; zeros for a drawing with no ink or `ink_px` <= 0).
struct InkSize {
    int w = 0, h = 0;
};
InkSize glyph_ink_size(Icon icon, int ink_px);

// THE KEY GLYPH, its INK BOX's top-left at (x, y), in `color` (0xRRGGBB,
// sRGB — the caller's text tone; the head's THE BOUND DRAWING): one copy of
// the cropped raster at that integer device px, no resample, a straight
// OVER. Cairo state is saved and restored; the caller's clip stands. Cannot
// fail: the launch's load parsed every drawing.
void draw_glyph(cairo_t* cr, Icon icon, int x, int y, int ink_px,
                uint32_t color);

} // namespace icons
