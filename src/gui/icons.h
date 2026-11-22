#pragma once

// THE ICONS (architect 2026-10-06), ONE SET, PAPIRUS (his set, architect
// 2026-11-17, session 140; ONE LOOK, Arc-Dark and Papirus, no device key,
// architect 2026-11-21, send GG). An Icon is a drawing the GUI paints, ONE
// FILE PER ENUMERATOR in ONE FLAT FOLDER, assets/icons/, each file NAMED BY
// ITS ENUMERATOR (`<Name>.svg`; assets/icons/README.md the drawings, their
// provenance and their licences): papirus-icon-theme 33d971eb, its 16-px symbolic set and three
// drawings of its 16-px actions tree, written by
// tools/icons/import_papirus.py, the roster the parked picks of session 123
// with five re-picked by the architect 2026-11-17 (the importer's table).
// RECOLORED to `currentColor` by the one RECOLOR RULE (the README's; the
// importer's one table the enumerator -> source list) and PAINTED IN
// THE TEXT TONES — a lit key's in the print of its accent face —
// (materials.h) at ONE ROAD, THE INK BOX (below), each seat naming its own
// ink length (the battery's the title bar's cap band, render.h's title bar
// block):
//   THE KEYS (architect 2026-11-01, M4 / M5: one visual size; no printed
//   legend under the key since 2026-11-18, send Z): every key carries one —
//   and since 2026-11-08 the
//   on-screen keyboard's glyph keys, at the same one size
//   (onscreen_keyboard::cap_icon: Tab FormatIndentMore, Enter KeyEnter, the
//   rest shared drawings) —
//   its 16 W canvas centered on its box (paint_key_glyph, paint_handler.cpp;
//   send Z, 2026-11-19), `text` (Default's B5B5B5) at rest,
//   `key_dim_ink` (Default's 5D5D5D) on a disabled key, `text_on_lit`
//   (Default's 070707) on a lit or pressed one's accent face, and
//   `text_on_lit_dim` (Default's 2A221A) on a lit disabled one's dimmed face
//   (materials.h's key_ink; architect 2026-11-11, session 106; 2026-11-17,
//   session 140; 2026-11-19, send Z).
//   THE SEATS OFF THE KEYS (2026-11-02, M7): the title bar's note
//   (MusicNote16th, BPM's glyph too) and its battery + clock legend's glyph
//   (Battery000Charging / Battery000) (paint_title_bar), the list rows'
//   folder and double note (Folder / ViewMediaTrack, SOURCE's glyph too,
//   paint_folder_overlay), the cards' class glyphs (DialogInformation /
//   DialogError, paint_notifications).
// THE ROSTER IS AS RULED (2026-11-17, session 140); a re-pick is a row of
// the importer's table and a rerun. EVERY FILE IS A DISTINCT DRAWING (by
// realpath and path data; the icon theme's names are often symlinks, so one
// drawing is one enumerator) — the score viewer's page keys,
// GoPreviousViewPage / GoNextViewPage, are chevrons of their own. THE TWO
// CARDS, DialogError and DialogInformation, are imported, their discs
// cutting the mark out so one color shows it (assets/icons/README.md's
// provenance).
// THE LAUNCHER ICON is assets/icons/AppIcon.svg, a regular file holding
// breeze-icons' note (the launcher's picture alone), which
// apk/build_apk.sh renders at every build; no enumerator names it (no
// painter draws it, and the load reads only the enumerators' files).
// THE SET PARSED AT LAUNCH through resvg (svg_icon.h: the wrapper and the
// error rule), straight from the bundle — never copied into the config
// folder (load_set, below) — and rasterised lazily, per (drawing, ink px,
// color, x phase, y phase) — the place's fraction in 64ths on each axis —
// into an ARGB32 surface that a draw copies at the place's whole device px,
// no resample (draw_glyph, below).
//
// THE BOUND DRAWING (architect 2026-11-02; the road of 2026-10-10 restored):
// a file that paints in `currentColor` — every file here — WEARS THE COLOR
// ITS CALLER PASSES, a 0xRRGGBB value (one of the compiled row's words — the
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

#include "render.h"        // scaled_px

#include <cairo/cairo.h>

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>

namespace icons {

// THE DRAWINGS (tools/icons/import_papirus.py's ROSTER, which writes
// `<Name>.svg` for each): ONE LIST, spelled once, from which the enumerators
// and their file names are both made (Icon and kIconNames below), so the two
// cannot part. Which seat wears which is its owner's: the keys' act vocabularies
// (redesign_button_icon, app_state.h; render_player_button_icon,
// score_viewer_button_icon, paint_handler.cpp /
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

// The drawings, one per file of the folder (each file named by its
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
// file in the folder is `<name>.svg`. A misspelling is a missing file at the
// next launch, which fails it. (The folder's AppIcon.svg is the launcher's
// and no enumerator's: the load never looks it up.)
inline constexpr const char* kIconNames[] = {
#define WARPTEMPO_ICON_NAME(name) #name,
    WARPTEMPO_ICONS(WARPTEMPO_ICON_NAME)
#undef WARPTEMPO_ICON_NAME
};
static_assert(std::size(kIconNames) == kIconCount);

// THE LOAD, once, in gui_main before the window: the set's files fetched
// from the bundle (GuiPlatform::bundled_icon_files — the APK's icons/
// assets; READ IN PLACE, never copied into the config folder: nobody
// authors an icon on the device), then per enumerator in enum order
// `<name>.svg` parsed (svg_icon::parse), all before any draw; files of other
// names (AppIcon.svg, the launcher's) never looked up. THE ERROR ARM'S
// PRODUCERS: the bundle unreadable (IO), a file not there, or a file not
// well-formed SVG (a build defect) — "icons: Folder.svg: missing", the first
// only, which gui_main prints and exits on (the validation doctrine's class
// (1)). A construct resvg does not draw passes the load (svg_icon.h's error
// rule).
std::optional<std::string> load_set();

// THE RASTERS DROPPED, on a scale change (GuiInputHandler::apply_gui_scale)
// and on a flip of True Colors (GuiInputHandler::apply_true_colors — the
// rasters are converted for the window as they are made, svg_icon.h): the
// cache refills lazily at the new sizes and in the new conversion, the
// fits with them; the parsed drawings stay (a bound parse keys its sRGB
// color, which neither change moves). A key's lit edge drops nothing:
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
// keys' drawing as its whole canvas in a 16 W box (kKeyDrawingBoxW,
// paint_key_glyph, 32 device px on the tablet: Papirus's 16-px drawings at
// their native size; send Z, 2026-11-19, ink_in_box below), the title bar's
// note (the drawing's ink in a 12 W box,
// ink_in_box below) and battery (the cap band's height, render.h's title
// bar block) — the list rows' drawing as its whole canvas in a 16 W box
// (kListDrawingBoxW, paint_handler.cpp's THE LIST VIEW; send AA's stage D2,
// 2026-11-19), the cards' drawing as
// its whole canvas in a 16 W box (kNotificationGlyphBoxW, notifications.h,
// ink_in_box; send AA's stage D1, 2026-11-19). The fit
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

// A DRAWING IN A BOX (architect 2026-11-19, send Z: the title bar's note "in
// a 12 W drawing box", "the logo should take the same font size as the
// body"): what the ink-box road needs to draw the drawing as if its whole
// canvas were scaled into a `box_px` square — `ink_px` the ink's larger side
// at that scale (the drawing's ink over its canvas's larger side, both read
// off the fit's zoom-8 probe, times the box, rounded to the nearest px: the
// road's sizes are whole), and `left` / `top` the ink box's offset inside
// the box at that scale, real px. Zeros for a drawing with no ink.
struct InkInBox {
    int    ink_px = 0;
    double left   = 0.0;
    double top    = 0.0;
};
InkInBox ink_in_box(Icon icon, int box_px);

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

// THE GLYPH IN A MIXED INK (architect 2026-11-20: a key's drawing
// fades with its face, Arc's `transition: all` carrying `color`, the ink a
// symbolic drawing is drawn in): THE SAME PLACE AND THE SAME CROPPED RASTER
// AS draw_glyph's for `raster_color` (one of the mix's own inks, so the
// cache holds no raster it would not hold at rest and grows by nothing), ITS
// ALPHA THE MASK of a source in `window_word` — the mix's word, already the
// window's (cool_edit_paint.h's stamp_tone_word). A bound drawing's
// coverage does not depend on its color (one `currentColor` paint), so the
// mask is the drawing's own coverage at that phase, and the mixed tone
// costs one mask composite and no render; a drawing with its own inks
// ignores the tone (the head's THE BOUND DRAWING) and draws its one raster.
// Used only while a fade runs: a plain ink takes draw_glyph, byte for byte.
void draw_glyph_tinted(cairo_t* cr, Icon icon, double x, double y,
                       int ink_px, uint32_t raster_color,
                       uint32_t window_word);

} // namespace icons
