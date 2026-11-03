#pragma once

// THE ICONS (architect 2026-10-06; BREEZE ONLY, EVERYWHERE 2026-11-01, M7:
// "we should use breeze glyphs only. ok to modify eg to make them
// monochrome"). An Icon is a drawing the GUI paints, ONE FILE PER ENUMERATOR
// in ONE FLAT FOLDER, assets/icons/, each file NAMED BY ITS ENUMERATOR
// (`<Name>.svg`; the folder's README the drawings, their provenance and
// their licence). EVERY DRAWING IS KDE's BREEZE (breeze-icons 6.31.0's dark
// theme, the 22-px folders), RECOLORED to `currentColor` by
// tools/icons/import_breeze.py (the README's RECOLOR RULE; its one table is
// the enumerator -> source list) and PAINTED IN THE TEXT TONES (materials.h)
// at ONE ROAD, THE INK BOX (below), each seat naming its own ink length:
//   THE KEYS (architect 2026-11-01, M4 / M5: "Breeze is committed", one
//   visual size, the legend kept under the key): every key carries one,
//   centered on its face (paint_key_glyph, paint_handler.cpp), kTextInk
//   B5B5B5 at rest and pressed, kKeyDimInk 5A5A5A on a disabled key,
//   kTextOnLit 111111 on a lit one (materials.h's key_ink, 2026-11-02).
//   THE SEATS OFF THE KEYS (2026-11-02, M7): the logo lane's note
//   (MusicNote16th, BPM's glyph too) and its battery legend's glyph
//   (Battery000Charging / Battery000) (paint_logo_lane), the list rows'
//   folder and double note (Folder / ViewMediaTrack, SOURCE's glyph too,
//   paint_folder_overlay), the cards' class glyphs (DialogInformation /
//   DialogError, paint_notifications).
// THE ROSTER IS AS RULED (architect 2026-11-02, R12 / R14 / R15); a re-pick
// is a row of the importer's table and a rerun. EVERY FILE IS A DISTINCT
// DRAWING (by realpath and path data; Breeze's names are often symlinks, so
// one drawing is one enumerator — WindowClose is dialog-close too) SAVE THE
// ONE DELIBERATE SHARE R15 records: GoPreviousViewPage / GoNextViewPage, the
// score viewer's page keys, are GoPrevious's / GoNext's drawings. THE TWO
// CARDS ARE THE ONE PAIR NOT IMPORTED: DialogError and DialogInformation are
// the 10-10 hand transcriptions, the mark CUT OUT of the filled square
// (even-odd) so one color shows it (R14 (3)); the importer checks them and
// never writes them (its HAND list; assets/icons/README.md's provenance).
// THE LAUNCHER ICON is assets/icons/AppIcon.svg, a relative symlink to
// MusicNote16th.svg, which apk/build_apk.sh renders at every build; no
// enumerator names it (no painter draws it, and the load reads only the
// enumerators' files).
// PARSED AT LAUNCH through resvg (svg_icon.h: the wrapper and the error
// rule), straight from the bundle — never copied into the config folder
// (load_set, below) — and rasterised lazily, per (drawing, ink px, color),
// into an ARGB32 surface that a draw copies at an integer device px, no
// resample.
//
// THE BOUND DRAWING (architect 2026-11-02; the road of 2026-10-10 restored):
// a file that paints in `currentColor` — every file here — WEARS THE COLOR
// ITS CALLER PASSES, a 0xRRGGBB value (one of the text tones; no role
// table): its bytes are kept and parsed per color with resvg's user
// stylesheet `svg { color: #RRGGBB; }` (svg_icon::parse), which
// `currentColor` resolves against — the file never edited at run time. The
// parses are held per (drawing, color) and the rasters keyed on the color.
// (A file with no `currentColor` would keep its one parse and its own inks;
// none stands today.) No drawing wears a shadow (the flat look,
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

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>

namespace icons {

// THE DRAWINGS, Breeze's, in the importer's table order
// (tools/icons/import_breeze.py's ROSTER, which writes `<Name>.svg` for each;
// the two hand-transcribed cards, its HAND list, after Folder):
// ONE LIST, spelled once, from which the enumerators and their file names
// are both made (Icon and kIconNames below), so the two cannot part. Which
// seat wears which is its owner's: the keys' act vocabularies
// (redesign_button_icon, app_state.h; render_player_button_icon,
// score_viewer_button_icon, modal_word_icon, paint_handler.cpp /
// score_viewer.h) and the off-key seats' painters (the head).
#define WARPTEMPO_BREEZE_ICONS(X)                                        \
    X(DocumentOpen) X(DocumentSave) X(DocumentSaveAs) X(VcsPull)         \
    X(NetworkConnect) X(VcsCommit) X(EditUndo) X(EditRedo)               \
    X(MediaRecord) X(DialogCancel) X(EditCopy) X(ZoomIn) X(ZoomOut)      \
    X(ZoomFitBest) X(ZoomPrevious) X(ZoomInY) X(GoJump)                  \
    X(TimelineUseZoneOn) X(EditCut) X(MusicNote16th) X(Mathmode)         \
    X(Merge) X(AudioHeadphones) X(Lock) X(SettingsConfigure)             \
    X(HelpWhatsthis) X(ViewMediaLyrics) X(Raindrop) X(ShallowHistory)    \
    X(DeepHistory) X(BlackSum) X(DocumentRevert) X(KeyframePrevious)     \
    X(KeyframeNext) X(DialogOkApply) X(ViewMediaTrack)                   \
    X(ViewMediaVisualization) X(Speedometer) X(ChronometerStart)         \
    X(MediaPlaybackStart) X(MediaPlaybackStop) X(MediaPlaybackPause)     \
    X(MediaSkipBackward) X(MediaSkipForward) X(MediaSeekBackward)        \
    X(MediaSeekForward) X(MediaRepeatSingle) X(ListAdd) X(ListRemove)    \
    X(EditDelete) X(ViewHidden) X(InsertLink) X(Link)                    \
    X(GoJumpDeclaration) X(DocumentEdit) X(EditSelect)                   \
    X(TransformMoveVertical) X(GoDown) X(GoUp) X(BboxPrev) X(BboxNext)   \
    X(TabDetach) X(ZoomOriginal) X(GoPrevious) X(GoNext)                 \
    X(GoParentFolder) X(GoPreviousViewPage) X(GoNextViewPage)            \
    X(EditReset) X(ZoneIn) X(ZoneOut) X(WindowClose) X(TaskComplete)     \
    X(EditClear) X(ViewRefresh) X(Folder) X(DialogInformation)           \
    X(DialogError) X(Battery000) X(Battery000Charging)

// The drawings, one per file of assets/icons/ (each file named by its
// enumerator — kIconNames below).
enum class Icon {
#define WARPTEMPO_ICON_ENUM(name) name,
    WARPTEMPO_BREEZE_ICONS(WARPTEMPO_ICON_ENUM)
#undef WARPTEMPO_ICON_ENUM
};

// Roster size, the names' and the load's count (load_set parses one
// document per enumerator).
#define WARPTEMPO_ICON_ONE(name) +1
inline constexpr int kIconCount = 0 WARPTEMPO_BREEZE_ICONS(WARPTEMPO_ICON_ONE);
#undef WARPTEMPO_ICON_ONE

// THE FILE NAMES, the enumerators spelled in enum order: each enumerator's
// file in assets/icons/ is `<name>.svg`. A misspelling is a missing file at
// the next launch, which fails it. (The one repeat — GoPreviousViewPage
// is GoPrevious's drawing, GoNextViewPage GoNext's, the head's deliberate
// share — is two files, each written by the importer from its own source
// name. The folder's one symlink,
// AppIcon.svg -> MusicNote16th.svg, is the launcher's and no enumerator's;
// the bundle's road dereferences it, build_apk.sh's cp -L.)
inline constexpr const char* kIconNames[] = {
#define WARPTEMPO_ICON_NAME(name) #name,
    WARPTEMPO_BREEZE_ICONS(WARPTEMPO_ICON_NAME)
#undef WARPTEMPO_ICON_NAME
};
static_assert(std::size(kIconNames) == kIconCount);

// THE LOAD, once, in gui_main before the window: the folder's files fetched
// from the bundle (GuiPlatform::bundled_icon_files — the APK's icons/
// assets; READ IN PLACE, never copied into the config folder: nobody authors
// an icon on the device), then per enumerator in enum order `<name>.svg` parsed
// (svg_icon::parse), all before any draw; files of other names (the README,
// the licence text, AppIcon.svg) ignored. THE ERROR ARM'S PRODUCERS: the
// bundle unreadable (IO), the file not there, or a file not well-formed SVG
// (a build defect) — "icons: Folder.svg: missing", the first only, which
// gui_main prints and exits on (the validation doctrine's class (1)). A
// construct resvg does not draw passes the load (svg_icon.h's error rule).
std::optional<std::string> load_set();

// THE RASTERS DROPPED, on a scale change (GuiInputHandler::apply_gui_scale)
// and on a flip of True Colors (GuiInputHandler::apply_true_colors — the
// rasters are converted for the window as they are made, svg_icon.h): the
// cache refills lazily at the new sizes and in the new conversion, the
// fits with them; the parsed drawings stay (a bound parse keys its sRGB
// color, which neither change moves). (No color pick drops anything: the
// glyphs' colors are the constant text tones.)
void drop_rasters();

// -- THE INK BOX: one road for every seat (architect 2026-11-01, M4 / M5:
// "the glyphs smaller … the problem with M3 was the uneven size, e.g. DRAG
// was tiny" -> ONE VISUAL SIZE for every key glyph; 2026-11-02, M7: the
// seats off the keys on the same road at their own lengths) ---------------
//
// A GLYPH IS SCALED BY ITS INK, never by its viewBox: the drawing is
// rendered at a uniform zoom from its origin (svg_icon::rasterise_zoom) and
// its INK BOX — the bounds of every pixel whose alpha is > 0, the
// antialiased rim included — is cropped out; THE FIT is the zoom at which
// the ink box's LARGER SIDE is `ink_px`, found as the approved M5 mock found
// it: a probe at zoom 8 measures the ink in user units, the zoom is `ink_px`
// over its larger side, and the zoom steps down by 1 % while the rim's
// extra row makes it overrun. THE SEATS' LENGTHS, each its owner's: the
// keys' skin_px of 20 / 33 of the `key_height` tuning key's side
// (kKeyGlyphInkPerKeySide, paint_key_glyph, 40 at the default 66,
// 2026-11-02), the logo lane's
// note skin_px(21) (kLogoNoteInkAt300) and battery glyph skin_px(19)
// (kLogoGlyphInkAt300, render.h), the list rows' skin_px(27)
// (kListGlyphInkAt300, paint_handler.cpp), the cards' .525 of the first
// line's inside height (kNotificationGlyphRatio, notifications.h). The fit
// is the drawing's alpha alone, so it is held per (drawing, ink px) and
// shared by the colors; the cropped raster per (drawing, ink px, color),
// converted for the window as every raster (svg_icon::convert_to_display).

// The ink box's size, device px, at `ink_px` (its larger side `ink_px` or
// less; zeros for a drawing with no ink or `ink_px` <= 0).
struct InkSize {
    int w = 0, h = 0;
};
InkSize glyph_ink_size(Icon icon, int ink_px);

// THE GLYPH, its INK BOX's top-left at (x, y), in `color` (0xRRGGBB, sRGB —
// the caller's text tone; the head's THE BOUND DRAWING): one copy of the
// cropped raster at that integer device px, no resample, a straight OVER.
// Cairo state is saved and restored; the caller's clip stands. Cannot fail:
// the launch's load parsed every drawing.
void draw_glyph(cairo_t* cr, Icon icon, int x, int y, int ink_px,
                uint32_t color);

} // namespace icons
