#pragma once

// THE TEXT'S RASTER ROAD — EVERY GLYPH AT ITS EXACT PLACE, TO FREETYPE'S OWN
// 1/64 PX ON BOTH AXES (architect 2026-11-15, the no-grid arc: "no grid
// whatsoever. That just one rule for everything"; render.h's hull holds the
// rule and what stays whole under it).
//
// WHY IT IS NOT cairo_show_glyphs (measured on cairo 1.18.4 / FreeType
// 2.14.3 / pixman 0.46.4, the APK's own versions): cairo's image backend
// places a glyph on ONE OF FOUR QUARTER PHASES an axis (its composite_glyphs'
// PHASE / POSITION, the nearest quarter, an odd eighth rounding up), and it
// clips the run to a box it rounds ANOTHER way (the glyph positions rounded
// to whole px, its round_glyph_positions, which no public option turns
// off), so at a fractional baseline it cuts the run's top or bottom row of
// ink — Bold 22 at a half row loses the cap tops' whole top row — and at a
// fractional pen a corner of the first column. So a pen or a baseline handed
// to it exactly is neither placed exactly nor painted whole.
//
// THE ROAD: each glyph is loaded through FreeType with the flags cairo's own
// font backend loads it with under the product's options (the face's
// FT_LOAD_NO_BITMAP and, for hint style SLIGHT, FT_LOAD_TARGET_LIGHT —
// gui_font_bundled.cpp's options: the light autohinter fits the outline to
// whole rows AT THE ORIGIN and leaves every x as designed), its outline is
// then translated by the EXACT 26.6 FRACTION of its place on both axes
// (FT_Outline_Translate: the fitted outline slides off the rows by the
// baseline's fraction, exactly as cairo's own quarter phases slide it) and
// rendered gray (FT_Render_Glyph, normal: exact area coverage, on the
// outline's box rounded outward, so no row or column of ink is ever cut);
// the run's coverages are summed, saturating, into ONE A8 MASK and the mask
// is composited in the context's CURRENT SOURCE through cairo
// (cairo_mask_surface) at the whole part of the place, so the context's
// clip, its operator and its source hold as they do for any paint.
//
// WHAT IS PROMISED:
//   (a) A GLYPH AT A WHOLE PLACE IS cairo_show_glyphs' GLYPH, BYTE FOR BYTE
//       — the same outline, hinted the same way, rendered by the same
//       rasterizer on the same box — and a run of them is composited as
//       cairo composites it: the saturating sum is pixman's own glyph mask
//       (the arm cairo takes when a glyph's box meets the boxes before it,
//       _cairo_traps_compositor_glyphs' use_mask), and where no two boxes
//       meet the sum of one is the glyph, which is what cairo's other arm
//       (each glyph composited on its own) writes. Proven on the host at
//       every size of kGuiFaceSizes for both files, on ARGB32 and A8.
//   (b) AT A FRACTION THE INK MOVES WITH THE PLACE, LINEARLY (its centroid
//       by the fraction, to 1/64 px), and nothing is cut.
// What cairo's road did that this one does not: a glyph INSIDE a run, whose
// pen is a sum of unhinted advances and so a fraction, stood on cairo's
// nearest quarter; it stands on its own 1/64 now, up to an eighth of a px
// from where cairo put it.
//
// THE CONTEXT: its matrix a pure translation (every product context's — the
// backbuffer's identity, the flag cache's whole-row translation); the place
// is taken in DEVICE px, the translation folded in, so a mask is always
// composited on whole device px and never resampled.
//
// THE COST AND THE CACHE: a glyph rasterized costs FreeType's load and
// render, 2.2 us a glyph at Bold 22 on the host against cairo's 0.28 us
// from its own glyph cache — so the rasters are KEPT, in one bounded table
// keyed {face, em, glyph, x phase, y phase in 64ths}, their bytes in one
// arena, CLEARED ALL AT ONCE AT ITS BOUND (the shaping memo's own rule,
// text_shape.h): text at rest, and a lane of labels whose baseline does not
// move (64 x phases of a few dozen glyphs), is served from it, and a hit
// costs a table probe and the sum into the mask. Nothing is allocated per
// glyph: the table and the arena are reserved once, the run's mask is one
// reused buffer, and one small cairo surface wraps it for the composite
// (cairo's own glyph mask is the same one allocation a run).
//
// A SELF-CONTAINED UNIT: cairo, cairo-ft and FreeType alone — no HarfBuzz,
// no GuiFont, nothing of the product's — so a host driver links it against
// the same libraries and holds (a) and (b) against cairo itself.
// text_shape.cpp's show_shaped_run is its one product caller (the placement
// is that file's, its fixed arithmetic), and its ink measure the other.
// GUI THREAD ONLY, as the scaled font it reads is (gui_outline_scaled_font).

#include <cairo/cairo.h>

namespace text_raster {

// PAINT `count` GLYPHS OF `font` AT THEIR EXACT PLACES in `cr`'s current
// source — cairo_show_glyphs' argument shape, so a caller (and a check)
// swaps one call for the other: `font` the cairo-ft scaled font the glyphs
// were laid out on (its font face names the file, its font matrix the em in
// device px; the product's options are assumed — hint style SLIGHT, gray,
// an identity font CTM), each glyph's (x, y) its origin on the baseline in
// user space, y down. The context's source, clip and operator are honored;
// its state and its current path are not modified.
void show_glyphs(cairo_t* cr, cairo_scaled_font_t* font,
                 const cairo_glyph_t* glyphs, int count);

// THE OUTLINE'S OWN BOX OF `count` PLACED GLYPHS, unrounded: the union of
// each glyph's hinted outline's control box (26.6, the box the rasterizer
// rounds outward for its bitmap, so the ink lies inside it) at the glyph's
// place — x0 / x1 the first and last ink's real edges, y0 / y1 the top and
// bottom's (y down). `empty` when no glyph has an outline (a run of
// spaces). The measure of ink a seat aligns to exactly; cairo's own glyph
// extents answer whole-px bearings under its hint metrics.
struct InkBox {
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    bool   empty = true;
};
InkBox ink_box(cairo_scaled_font_t* font, const cairo_glyph_t* glyphs,
               int count);

} // namespace text_raster
