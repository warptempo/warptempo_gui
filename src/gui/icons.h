#pragma once

// THE ICONS (architect 2026-10-06; the set cut to the drawings on screen
// 2026-10-25). An Icon is a drawing the GUI paints, ONE FILE PER ENUMERATOR
// in ONE FLAT FOLDER, assets/icons/, each file NAMED BY ITS ENUMERATOR
// (`<Name>.svg`; the folder's README the drawings, their authors and their
// licence). The drawings are the project's own: the quaver (the logo lane,
// the wav rows), the folder (the folder rows) and the cards' i and X — the
// keys print legends and draw no glyph (the hi-fi skin, 2026-10-23).
// PARSED AT LAUNCH through resvg (svg_icon.h: the wrapper and the error
// rule), straight from the bundle — never copied into the config folder
// (load_set, below) — and rasterised lazily per (drawing, device px) into an
// ARGB32 surface that a draw copies at an integer device px, no resample.
// THE INKS ARE THE DRAWINGS' OWN, never a theme role, and no drawing is ever
// shown disabled; a drawing's SHADOW (draw_boxed_shadow, the logo lane's
// quaver, 2026-10-28) is the palette's cue shadow through the raster's
// alpha, never a recolored drawing.
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
#include <iterator>
#include <optional>
#include <string>

namespace icons {

// The drawings, one per file of assets/icons/ (each file named by its
// enumerator — kIconNames below).
enum class Icon {
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
// document per enumerator). Keep it equal to the enumerator count above.
inline constexpr int kIconCount = 6;

// THE FILE NAMES, the enumerators spelled in enum order: each enumerator's
// file in assets/icons/ is `<name>.svg`. A misspelling is a missing file at
// the next launch, which fails it. THE REPEATS ARE SYMLINKS, relative, inside
// the folder: AppIcon.svg -> Quaver.svg and WavRow.svg -> Quaver.svg (the
// folder's README lists them). The bundle's road dereferences a link
// (build_apk.sh's copy, cp -L), so the load reads a link as the file it
// names.
inline constexpr const char* kIconNames[] = {
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
// cache refills lazily at the new sizes and in the new conversion; the
// parsed drawings stay. (No color pick drops anything: no glyph wears a
// role, the head's rule, and a shadow's color is its paint's source, never
// the raster's.)
void drop_rasters();

// A BOXED GLYPH (THE BOXED GLYPHS, above) in its box: `box_h` device px
// tall (icon_box_px), its top-left (box_x, box_y). The raster is a square
// cell box_h a side, the drawing fitted to its height and centered across it
// (svg_icon::rasterise), so the cell stands half the cell's spare width left
// of the box's edge. Cairo state is saved and restored. Cannot fail: the
// launch's load parsed every drawing.
void draw_boxed(cairo_t* cr, Icon icon, int box_x, int box_y, int box_h);

// A BOXED GLYPH'S SHADOW (architect 2026-10-28: the logo lane's quaver
// wears the cue label's shadow, materials.h's INKS), painted BEFORE
// draw_boxed at the same box: the live raster draw_boxed copies, ITS ALPHA
// ALONE as a mask (cairo_mask_surface) under the flat `shadow`, one label
// quantum right and one down (render.h's cue_label_shadow_offset_px). The
// raster's colors are the window's already (svg_icon::convert_to_display,
// which keeps alpha), so the mask reads nothing the conversion changed, and
// `shadow` goes through the window's display transform as every chrome ink
// does (set_palette_source) — sRGB in, the window's bytes out, so a True
// Colors flip or a palette pick reaches it at the next paint with no raster
// dropped. The shadow is ink, never extent: the box's placement and
// boxed_ink never read it. Cairo state is saved and restored. Its one
// reader is the logo lane's painter (paint_handler.cpp's paint_logo_lane).
void draw_boxed_shadow(cairo_t* cr, Icon icon, int box_x, int box_y,
                       int box_h, GuiColor shadow);

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

} // namespace icons
