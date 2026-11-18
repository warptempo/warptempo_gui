#pragma once

// THE ICONS (architect 2026-10-06), TWO SETS, THE `icons` DEVICE KEY
// CHOOSING (architect 2026-11-14, session 123: "the icons get their own
// device key", NOT tied to the theme — the theme files stay palette-only and
// either set goes with any theme; 2026-11-17, session 140: "No breeze glyphs
// at all", HIS SET PAPIRUS, the key's first-run value). An Icon is a drawing
// the GUI paints, ONE FILE PER ENUMERATOR PER SET, each set ONE FLAT FOLDER,
// each file NAMED BY ITS ENUMERATOR (`<Name>.svg`; assets/icons/README.md
// the drawings, their provenance and their licences):
//   BREEZE, assets/icons/ (BREEZE ONLY, EVERYWHERE from 2026-11-01, M7, to
//   2026-11-17: "we should use breeze glyphs only. ok to modify eg to make
//   them monochrome") — KDE's breeze-icons 6.31.0 dark theme, the 22-px
//   folders, written by tools/icons/import_breeze.py;
//   PAPIRUS, assets/icons/papirus/ — papirus-icon-theme 33d971eb, its 16-px
//   symbolic set and three drawings of its 16-px actions tree, written by
//   tools/icons/import_papirus.py, the roster the parked picks of session
//   123 with five re-picked by the architect 2026-11-17 (the importer's
//   table).
// Both are RECOLORED to `currentColor` by the one RECOLOR RULE (the README's;
// each importer's one table the enumerator -> source list) and PAINTED IN
// THE TEXT TONES — a lit key's in the print of its accent or purple face —
// (materials.h) at ONE ROAD, THE INK BOX (below), each seat naming its own
// ink length (the battery's the set's, render.h's LOGO LANE block):
//   THE KEYS (architect 2026-11-01, M4 / M5: "Breeze is committed", one
//   visual size; its printed legend under the key again since 2026-11-03,
//   paint_key_legend): every key carries one — and since 2026-11-08 the
//   on-screen keyboard's glyph keys, at the same one size
//   (onscreen_keyboard::cap_icon: Tab FormatIndentMore, Enter KeyEnter, the
//   rest shared drawings) —
//   centered on its face (paint_key_glyph, paint_handler.cpp), `text`
//   B5B5B5 at rest and pressed, `key_dim_ink` (Default's 5D5D5D) on a disabled key,
//   `text_on_lit` (Default's 070707) on a lit one's accent face (a radio
//   key's purple, 2026-11-11, session 107), pressed or
//   not, and `text_on_lit_dim` (Default's 2E2114; a radio key's
//   `radio_text_on_lit_dim`, 231E2D) on a lit disabled one's dimmed face
//   (materials.h's key_ink; architect 2026-11-11, session 106; 2026-11-17,
//   session 140).
//   THE SEATS OFF THE KEYS (2026-11-02, M7): the logo lane's note
//   (MusicNote16th, BPM's glyph too) and its battery + clock legend's glyph
//   (Battery000Charging / Battery000) (paint_logo_lane), the list rows'
//   folder and double note (Folder / ViewMediaTrack, SOURCE's glyph too,
//   paint_folder_overlay), the cards' class glyphs (DialogInformation /
//   DialogError, paint_notifications).
// THE ROSTERS ARE AS RULED (Breeze's: architect 2026-11-02, R12 / R14 /
// R15; Papirus's: 2026-11-17, session 140); a re-pick is a row of its
// importer's table and a rerun. EVERY FILE OF A SET IS A DISTINCT DRAWING
// (by realpath and path data; the icon themes' names are often symlinks,
// so one drawing is one enumerator — Breeze's WindowClose is dialog-close
// too)
// SAVE BREEZE'S ONE DELIBERATE SHARE R15 records: GoPreviousViewPage /
// GoNextViewPage, the score viewer's page keys, are GoPrevious's / GoNext's
// drawings (Papirus's are chevrons of their own, no share). THE TWO CARDS:
// Breeze's DialogError and DialogInformation are the 10-10 hand
// transcriptions, the mark CUT OUT of the filled square (even-odd) so one
// color shows it (R14 (3)), which its importer checks and never writes (its
// HAND list); Papirus's are imported, its discs already cutting the mark out
// (assets/icons/README.md's provenance).
// THE LAUNCHER ICON is assets/icons/AppIcon.svg, a relative symlink to
// Breeze's MusicNote16th.svg, whatever set the key chooses, which
// apk/build_apk.sh renders at every build; no enumerator names it (no
// painter draws it, and the load reads only the enumerators' files).
// BOTH SETS PARSED AT LAUNCH through resvg (svg_icon.h: the wrapper and the
// error rule), straight from the bundle — never copied into the config
// folder (load_set, below) — the key's set chosen (select_set), and
// rasterised lazily, per (drawing, ink px, color), into an ARGB32 surface
// that a draw copies at an integer device px, no resample.
//
// THE BOUND DRAWING (architect 2026-11-02; the road of 2026-10-10 restored):
// a file that paints in `currentColor` — every file here — WEARS THE COLOR
// ITS CALLER PASSES, a 0xRRGGBB value (one of the theme row's words — the
// text tones, a lit key's print and its dimmed tone; no role
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

#include "device_config.h" // GuiIconSet, the `icons` key's two sets
#include "render.h"        // scaled_px

#include <cairo/cairo.h>

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>

namespace icons {

// THE DRAWINGS, in the importers' table order
// (tools/icons/import_breeze.py's ROSTER, which writes `<Name>.svg` for each,
// Breeze's two hand-transcribed cards, its HAND list, after Folder;
// tools/icons/import_papirus.py's ROSTER, every row):
// ONE LIST FOR BOTH SETS, spelled once, from which the enumerators and their
// file names are both made (Icon and kIconNames below), so the two cannot
// part and the sets cannot hold different rosters. Which
// seat wears which is its owner's: the keys' act vocabularies
// (redesign_button_icon, app_state.h; render_player_button_icon,
// score_viewer_button_icon, modal_word_icon, paint_handler.cpp /
// score_viewer.h) and the off-key seats' painters (the head).
#define WARPTEMPO_ICONS(X)                                        \
    X(DocumentOpen) X(DocumentSave) X(DocumentSaveAs) X(VcsPull)         \
    X(NetworkConnect) X(VcsCommit) X(EditUndo) X(EditRedo)               \
    X(MediaRecord) X(DialogCancel) X(EditCopy) X(EditPaste) X(ZoomIn)    \
    X(ZoomOut) X(ZoomFitBest) X(ZoomPrevious) X(ZoomInY) X(GoJump)                  \
    X(TimelineUseZoneOn) X(EditCut) X(Bookmarks) X(MusicNote16th)       \
    X(Mathmode)                                                          \
    X(Merge) X(AudioHeadphones) X(Lock) X(SettingsConfigure)             \
    X(HelpWhatsthis) X(ViewMediaLyrics) X(ShallowHistory)                \
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
    X(EditReset) X(ZoneIn) X(ZoneOut) X(WindowClose)                     \
    X(EditClear) X(ViewRefresh) X(Folder) X(DialogInformation)           \
    X(DialogError) X(Battery000) X(Battery000Charging)                   \
    X(FormatIndentMore) X(KeyEnter)

// The drawings, one per file of each set's folder (each file named by its
// enumerator — kIconNames below).
enum class Icon {
#define WARPTEMPO_ICON_ENUM(name) name,
    WARPTEMPO_ICONS(WARPTEMPO_ICON_ENUM)
#undef WARPTEMPO_ICON_ENUM
};

// Roster size, the names' and the load's count (load_set parses one
// document per enumerator).
#define WARPTEMPO_ICON_ONE(name) +1
inline constexpr int kIconCount = 0 WARPTEMPO_ICONS(WARPTEMPO_ICON_ONE);
#undef WARPTEMPO_ICON_ONE

// THE FILE NAMES, the enumerators spelled in enum order: each enumerator's
// file in each set's folder is `<name>.svg`. A misspelling is a missing file
// at the next launch, which fails it. (Breeze's one repeat —
// GoPreviousViewPage is GoPrevious's drawing, GoNextViewPage GoNext's, the
// head's deliberate share — is two files, each written by the importer from
// its own source name. The Breeze folder's one symlink,
// AppIcon.svg -> MusicNote16th.svg, is the launcher's and no enumerator's;
// the bundle's road dereferences it, build_apk.sh's cp -L.)
inline constexpr const char* kIconNames[] = {
#define WARPTEMPO_ICON_NAME(name) #name,
    WARPTEMPO_ICONS(WARPTEMPO_ICON_NAME)
#undef WARPTEMPO_ICON_NAME
};
static_assert(std::size(kIconNames) == kIconCount);

// THE LOAD, once, in gui_main before the window: BOTH SETS' files fetched
// from the bundle (GuiPlatform::bundled_icon_files — the APK's icons/ and
// icons/papirus/ assets; READ IN PLACE, never copied into the config folder:
// nobody authors an icon on the device), then per set, Breeze first, per
// enumerator in enum order `<name>.svg` parsed (svg_icon::parse), all before
// any draw, so a live switch of the key meets a parsed set; files of other
// names (the README, the licence text, AppIcon.svg) ignored. THE ERROR ARM'S
// PRODUCERS: the bundle unreadable (IO), a file not there in EITHER set, or a
// file not well-formed SVG (a build defect) — "icons: Folder.svg: missing"
// (Breeze's) or "icons: papirus/Folder.svg: missing", the first only, which
// gui_main prints and exits on (the validation doctrine's class (1)). A
// construct resvg does not draw passes the load (svg_icon.h's error rule).
// The set drawn is Papirus until select_set names one.
std::optional<std::string> load_set();

// THE SET EVERY PAINTER DRAWS (the `icons` device key, device_config.h):
// chosen at launch from the config after load_set and before the window
// (gui_main), and at the key's live commit (GuiInputHandler::apply_icons,
// which drops the rasters and repaints whole). Every draw and every fit
// reads the chosen set's drawings; the other set's parses stay, idle.
void select_set(GuiIconSet set);
GuiIconSet selected_set();

// THE RASTERS DROPPED, on a scale change (GuiInputHandler::apply_gui_scale)
// and on a flip of True Colors (GuiInputHandler::apply_true_colors — the
// rasters are converted for the window as they are made, svg_icon.h): the
// cache refills lazily at the new sizes and in the new conversion, the
// fits with them; the parsed drawings stay (a bound parse keys its sRGB
// color, which neither change moves), AND ON A THEME COMMIT
// (GuiInputHandler::apply_theme: a raster keys on its sRGB color, which the
// theme moves, render.h's install_palette), AND ON AN ICON SET COMMIT
// (GuiInputHandler::apply_icons: both sets' rasters and fits, the chosen
// set's refilling as it is drawn). A key's lit edge drops nothing:
// the print on a lit face is one more color key beside the text tones.
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
// keys' scaled_px of 9 / 17 of the `key_height` tuning key's W
// (kKeyGlyphInkPerKeySide, paint_key_glyph, 9 W at the default 17, 18
// device px on the tablet, 2026-11-04 night, his pick d1, one W over the 8
// W before it), the logo lane's note scaled_px(9) (kLogoNoteInkW) and
// battery glyph — Breeze's scaled_px(11) (kLogoGlyphInkW), Papirus's the
// legend digits' ink rows (render.h's LOGO LANE block) — the list rows'
// scaled_px(13) (kListGlyphInkW, paint_handler.cpp), the cards' .525 of the first
// line's inside height (kNotificationGlyphRatio, notifications.h). The fit
// is the drawing's alpha alone, so it is held per (drawing, ink px) and
// shared by the colors; the cropped raster per (drawing, ink px, color,
// phase — draw_glyph below),
// converted for the window as every raster (svg_icon::convert_to_display).

// The ink box's size, device px, at `ink_px` (its larger side `ink_px` or
// less; zeros for a drawing with no ink or `ink_px` <= 0).
struct InkSize {
    int w = 0, h = 0;
};
InkSize glyph_ink_size(Icon icon, int ink_px);

// THE GLYPH, its INK BOX's top-left AT EXACTLY (x, y) — real device px
// (architect 2026-11-15, the no-grid arc; render.h's hull holds the rule) —
// in `color` (0xRRGGBB, sRGB — the caller's tone; the head's THE BOUND
// DRAWING). THE INK BOX IS A REAL RECT: the fit's box (the bounds of the
// drawing's ink at the fitted zoom, `glyph_ink_size`, whole px by its
// measure) standing with its top-left corner on (x, y). THE RASTER IS
// RENDERED AT THE PLACE'S FRACTION, to a 64th of a px on each axis (the
// text's own resolution, text_raster.h): the drawing rasterized by resvg
// with the fraction as its translation (svg_icon::rasterise_zoom's phase),
// the fit's box cropped out of it with one column or row more for the ink
// the fraction pushes right or down — the hull of the box at that phase —
// and that crop copied at the whole part of the place, no resample, a
// straight OVER. AT A WHOLE PLACE the raster is the phase-less one, byte
// for byte (the same render call, the same crop). THE CACHE: one raster per
// (drawing, ink px, color, x phase, y phase), BOUNDED — kept until the
// set holds kGlyphRasterBound of them, then DROPPED WHOLE (the shaping
// memo's rule); the bound's reason at the constant (icons.cpp).
// Cairo state is saved and restored; the caller's clip stands. Cannot fail:
// the launch's load parsed every drawing.
void draw_glyph(cairo_t* cr, Icon icon, double x, double y, int ink_px,
                uint32_t color);

} // namespace icons
