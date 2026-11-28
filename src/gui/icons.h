#pragma once

// THE ICONS (architect 2026-10-06), ONE SET, PAPIRUS (his set, architect
// 2026-11-17; ONE LOOK, Arc-Dark and Papirus, no device key,
// architect 2026-11-21). An Icon is a drawing the GUI paints, ONE
// FILE PER ENUMERATOR in ONE FLAT FOLDER, assets/icons/, each file NAMED BY
// ITS ENUMERATOR (`<Name>.svg`; assets/icons/README.md the drawings, their
// provenance and their licences): papirus-icon-theme 20260801, its 16-px
// symbolic set and drawings of its 16-px actions, devices and panel trees,
// written by tools/icons/import_papirus.py, the roster the importer's table
// as the architect picked it (2026-11-17; re-picks 2026-11-22 and
// 2026-11-23; additions and re-picks 2026-11-24; RELEASE's BluetoothPaired
// added 2026-11-26, Papirus's panel/bluetooth-paired) — FOURTEEN enumerators OUR
// OWN, from twelve sources in tools/icons/own/ (the README's THE OWN
// DRAWINGS): BboxPrev / BboxNext the
// arrow to a line from Papirus's go-first, one source, Next's mirrored by the
// importer's transform; TransformMoveVertical the double arrow from the same
// go-first; and, architect 2026-11-23, BlackSum a sigma (ours outright, no
// Papirus source), GoPreviousViewPage / GoNextViewPage go-previous's head
// without its stem (one source, Next's mirrored; the score viewer's page
// keys and, architect 2026-11-27, the history view's Older / Newer),
// TimelineUseZoneOn go-first's dot and the marker pair's wall,
// GoParentFolder folder-symbolic's folder with an up arrow cut out,
// VcsCommit / VcsPull go-up's / go-down's arrow on a line, NetworkConnect
// the two arrows side by side; and, architect 2026-11-24 ("L1"), the tab
// pair's TabA / TabB, Liberation Sans Bold's own outlines of the two
// capitals, 10 tall in the 16 box on Papirus's grid, rows 3 to 13 and
// the left edge on a whole unit (resized the same day, "PG_1_P1"; the
// face's, under its OFL); and,
// architect 2026-11-24 ("V1"), ZoomPrevious, view-restore's four corners
// with the left and right pairs 1 closer, both gaps 2. The score's
// Start / End (ZoneIn / ZoneOut) are Papirus's bookmark-new /
// bookmark-remove, untouched (architect 2026-11-24, "G"). The own
// drawings' straight arrows wear
// Papirus's head and stem (go-previous / go-up / go-down / go-first) rather
// than a solid triangle head, the shape he keeps for the keys' straight
// arrows (architect 2026-11-23); Papirus's own drawings keep the heads
// Papirus drew.
// RECOLORED to `currentColor` by the one RECOLOR RULE (the README's; the
// importer's one table the enumerator -> source list) and PAINTED IN
// THE TEXT TONES — a lit key's in the print of its accent face —
// (materials.h) at ONE ROAD, THE BOX (below), each seat naming its own box
// (the battery its ink's height, the title bar's cap band, render.h's title
// bar block):
//   THE KEYS (architect 2026-11-01: one visual size): every key carries
//   one — the
//   on-screen keyboard's glyph keys too, at the same one size
//   (onscreen_keyboard::cap_icon: Tab FormatIndentMore, Enter KeyEnter, the
//   rest shared drawings) —
//   its 16 U canvas in a 16 U box centered on the key's (paint_key_glyph,
//   paint_handler.cpp; 2026-11-19), `text` (D3DAE3) at rest,
//   `key_dim_ink` (7F8591) on a disabled key, `text_on_lit`
//   (FFFFFF) on a lit or pressed one's accent face, and
//   `text_on_lit_dim` (9AA1AF) on a lit disabled one's dimmed face
//   (materials.h's key_ink; their lines of kArcDarkTones, gui_theme.h;
//   architect 2026-11-11, 2026-11-17, 2026-11-19).
//   THE SEATS OFF THE KEYS (2026-11-02): the title bar's note
//   (MusicNote16th) and its battery's glyph in the Bold run
//   (Battery000Charging / Battery000) (paint_title_bar), the list rows'
//   folder and headphones (Folder / AudioHeadphones; the wav rows wear
//   PLAYER's drawing, architect 2026-11-22) and, on every other list's
//   rows, the opener key's drawing — SETUP's gear (SettingsConfigure) on the
//   settings page and its value lists, PRESETS' bookmark (Bookmarks) on the
//   trim presets, BPM's note on the BPM form (architect 2026-11-22;
//   paint_list_view's list_row_drawing), the
//   cards' class glyphs (DialogInformation / DialogError,
//   paint_notifications). THE NOTE IS SHARED THREE WAYS (architect
//   2026-11-22): the title bar, the BPM key and the BPM
//   form's rows (the launcher's art too, below); THE HEADPHONES TWO
//   (architect 2026-11-22): the PLAYER key and the render player's
//   wav rows; SOURCE's disc, ViewMediaTrack, is the key's alone.
// THE ROSTER IS AS RULED (2026-11-17; 2026-11-22; 2026-11-23; 2026-11-24;
// 2026-11-26; 2026-11-27);
// a re-pick is
// a row of the importer's table and a rerun. EVERY FILE IS A
// DISTINCT DRAWING (by the written shapes and transforms; the icon
// theme's names are often symlinks, so one drawing is one enumerator) — the
// score viewer's page keys and the history view's Older / Newer
// (architect 2026-11-27), GoPreviousViewPage / GoNextViewPage, wear our
// own head without a stem, not GoPrevious's / GoNext's arrows; BboxPrev and
// BboxNext share one source file, and so do the two page keys, two drawings
// each by the transform. EVERY DRAWING IS PAPIRUS'S BUT THE FOURTEEN OF OUR
// OWN above, the sigma ours outright, the two letters the text face's
// outlines and the rest made from Papirus's (architect 2026-11-22;
// 2026-11-23; 2026-11-24; the importer's own sources, tools/icons/own/).
// THE TWO
// CARDS, DialogError and DialogInformation, are imported, their discs
// cutting the mark out so one color shows it (assets/icons/README.md's
// provenance).
// THE LAUNCHER ICON is the title bar's note, MusicNote16th.svg, which
// apk/build_apk.sh renders at every build from this folder (architect
// 2026-11-21: one drawing, one file).
// THE SET PARSED AT LAUNCH through resvg (svg_icon.h: the wrapper and the
// error rule), straight from the bundle — never copied into the config
// folder (load_set, below) — and rasterised lazily, per (drawing, zoom,
// color, x phase, y phase) — the place's fraction in 64ths on each axis —
// into an ARGB32 surface that a draw copies at the place's whole device px,
// no resample (draw_glyph, below).
//
// THE BOUND DRAWING (architect 2026-11-02):
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
// one pointer is the pen or a finger, input_core.h), so it ships no cursor
// art and draws none.
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
    X(DocumentOpen) X(DocumentSave) X(VcsPull)                           \
    X(NetworkConnect) X(VcsCommit) X(EditUndo) X(EditRedo)               \
    X(MediaRecord) X(DialogCancel) X(EditCopy) X(EditPaste) X(ZoomIn)    \
    X(ZoomOut) X(ZoomFitBest) X(ZoomPrevious) X(ZoomInY) X(GoJump)                  \
    X(TimelineUseZoneOn) X(EditCut) X(Bookmarks) X(MusicNote16th)       \
    X(Mathmode)                                                          \
    X(Merge) X(AudioHeadphones) X(Lock) X(SettingsConfigure)             \
    X(HelpWhatsthis) X(BluetoothPaired) X(ViewMediaLyrics)               \
    X(ShallowHistory)                                                    \
    X(DeepHistory) X(BlackSum) X(DocumentRevert)                         \
    X(DialogOkApply) X(ViewMediaTrack)                                   \
    X(ViewMediaVisualization) X(Speedometer) X(ChronometerStart)         \
    X(MediaPlaybackStart) X(MediaPlaybackStop) X(MediaPlaybackPause)     \
    X(MediaSkipBackward) X(MediaSkipForward) X(MediaSeekBackward)        \
    X(MediaSeekForward) X(MediaRepeatSingle) X(ListAdd) X(ListRemove)    \
    X(EditDelete) X(ViewHidden) X(InsertLink) X(Link)                    \
    X(GoJumpDeclaration) X(DocumentEdit) X(EditSelect)                   \
    X(TransformMoveVertical) X(GoDown) X(GoUp) X(BboxPrev) X(BboxNext)   \
    X(TabA) X(TabB) X(ZoomOriginal) X(GoPrevious) X(GoNext)              \
    X(GoParentFolder) X(GoPreviousViewPage) X(GoNextViewPage)            \
    X(EditReset) X(ZoneIn) X(ZoneOut) X(EditClear)                       \
    X(Folder) X(DialogInformation)                                       \
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
// next launch, which fails it.
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
// names never looked up. THE ERROR ARM'S
// PRODUCERS: the bundle unreadable (IO), a file not there, or a file not
// well-formed SVG (a build defect) — "icons: Folder.svg: missing", the first
// only, which gui_main prints and exits on (the validation doctrine's class
// (1)). A construct resvg does not draw passes the load (svg_icon.h's error
// rule).
std::optional<std::string> load_set();

// THE RASTERS DROPPED, on a scale change (GuiInputHandler::apply_gui_scale)
// and on a flip of True Colors (GuiInputHandler::apply_true_colors — the
// rasters are converted for the window as they are made, svg_icon.h): the
// cache refills lazily at the new sizes and in the new conversion; the
// parsed drawings and their measures stay (a bound parse keys its sRGB
// color, which neither change moves; a canvas and an ink are in the
// drawing's own units). A key's lit edge drops nothing: the print on a lit
// face is one more color key beside the text tones.
void drop_rasters();

// -- THE BOX: one road for every seat (architect 2026-11-01:
// "the glyphs smaller … the problem with M3 was the uneven size, e.g. DRAG
// was tiny" -> ONE VISUAL SIZE for every key glyph; 2026-11-02: the
// seats off the keys on the same road at their own lengths) ---------------
//
// EVERY DRAWING STANDS AT THE EXACT ZOOM ITS SEAT ASKS, ITS CANVAS'S ORIGIN
// ON THE SEAT'S EXACT PLACE (architect 2026-11-26: "I haven't noticed any
// lack of sharpness with the stem lines or anything else that has gone off
// the grid. So I don't see why the icons should be the one exception";
// "Let's just go ahead and do it"). A seat names A BOX, a square `box_px`
// real device px on a side with its top-left at a real place, and the
// drawing is its whole canvas scaled into it: the zoom the box over the
// canvas's larger side (svg_icon::canvas_side), the canvas's origin on the
// box's top-left. Nothing stands between the drawing's own geometry and the
// paint but that zoom and that place (the no-grid rule, render.h's hull):
// no whole-px fit of the ink, no rounding of the zoom or of the seat.
// THE SEATS' BOXES, each its owner's: the keys' 16 U box (kKeyDrawingBoxU,
// paint_key_glyph, 32 device px on the tablet, zoom 2: Papirus's 16-px
// drawings at their native size; 2026-11-19), the list rows' 16 U box
// (kListDrawingBoxU, paint_handler.cpp's THE LIST VIEW; 2026-11-19), the
// cards' 16 U box (kNotificationGlyphBoxU, notifications.h; 2026-11-19),
// the title bar's note's 12 U box (render.h's title bar block), the note
// seated by its ink (glyph_ink_in_box). A SEAT SIZED BY ITS INK names the
// ink's length and takes the box that gives it (glyph_box_for_ink_height):
// the title bar's battery, its ink's height the cap band's (render.h's THE
// BATTERY). THE INK is the drawing's exact extent in its own units
// (svg_icon::ink_bounds) times the box's zoom, real px: a seat that places
// or sizes a drawing by its ink reads it there, never off a raster.

// THE DRAWING'S INK IN A BOX: the ink's real rect, device px, offset from the
// box's top-left, when the drawing's canvas is scaled into a `box_px`
// square. Nullopt for a drawing with no ink or a box of no size.
std::optional<GuiRectF> glyph_ink_in_box(Icon icon, double box_px);

// THE BOX FOR AN INK'S HEIGHT: the box, device px, in which the drawing's ink
// stands exactly `ink_h_px` tall — the zoom `ink_h_px` over the ink's height
// in its units, unrounded, times the canvas. 0 for a drawing with no ink.
double glyph_box_for_ink_height(Icon icon, double ink_h_px);

// THE GLYPH IN ITS BOX: the drawing's canvas scaled into the `box_px` square
// whose top-left is (x, y), real device px (THE BOX above; the no-grid arc,
// architect 2026-11-15, render.h's hull), in `color` (0xRRGGBB, sRGB — the
// caller's tone; the head's THE BOUND DRAWING). THE RASTER IS RENDERED AT
// THE BOX'S ZOOM AND AT THE PLACE'S FRACTION, to a 64th of a px on each axis
// (the text's own resolution, text_raster.h): resvg renders the drawing at
// that zoom with the fraction as its translation (svg_icon::rasterise_zoom's
// phase), the hull of the render's ink — the bounds of its pixels whose
// alpha is non-zero — is cropped out and copied at the place's whole part
// plus the crop's offset, no resample, a straight OVER (the pixels the crop
// leaves out are wholly transparent, which OVER leaves as they were, so the
// crop changes no byte). AT A WHOLE PLACE the raster is the phase-less one,
// byte for byte. EVERYTHING IT INKS lies inside the hull of the box at that
// place (resvg renders the canvas alone, and the phase's one column or row),
// so a caller's damage over the box's hull covers it. THE CACHE: one raster
// per (drawing, zoom, color, x phase, y phase), the zoom the exact double
// the box gives (a seat's box is one length at a scale, computed the same
// way on every frame, so its zoom compares exactly), BOUNDED — kept until the
// set holds kGlyphRasterBound of them, then DROPPED WHOLE (the shaping memo's
// rule); the bound's reason at the constant (icons.cpp). Cairo state is
// saved and restored; the caller's clip stands. Cannot fail: the launch's
// load parsed every drawing.
void draw_glyph(cairo_t* cr, Icon icon, double x, double y, double box_px,
                uint32_t color);

// THE GLYPH IN A MIXED INK (architect 2026-11-20: a key's drawing
// fades with its face, Arc's `transition: all` carrying `color`, the ink a
// symbolic drawing is drawn in): THE SAME BOX AND THE SAME CROPPED RASTER
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
                       double box_px, uint32_t raster_color,
                       uint32_t window_word);

} // namespace icons
