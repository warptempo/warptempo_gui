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
//       (each glyph composited on its own) writes. Proven on the host for
//       both files, on ARGB32 and A8, at every size kGuiFaceSizes held when
//       the road landed (2026-11-16: 7, 8, 9, 11 and 12 W, every em a
//       whole count of px). An em outside those — the table's 10 W and
//       18 W today, a fractional em at a scale whose product is one
//       (gui_font.h's head, architect 2026-11-26) — is unmeasured: the
//       argument (the same load, the same box, the same rasterizer, the em
//       reaching FreeType as cairo's own 26.6 size) does not depend on the
//       em, but no host run has held it there.
//   (b) AT A FRACTION THE INK MOVES WITH THE PLACE, LINEARLY (its centroid
//       by the fraction, to 1/64 px), and nothing is cut.
// The one place the two roads part: a glyph INSIDE a run, whose pen is a
// sum of unhinted advances and so a fraction, stands on its own 1/64, up to
// an eighth of a px from cairo's nearest quarter.
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
// text_shape.cpp is its one product caller: show_shaped_run and
// show_shaped_run_bytes (the placement is that file's, its fixed
// arithmetic) and the ink measure (ink_left_px / ink_right_px).
//
// THE THREAD CONTRACT — GUI THREAD ONLY: the two tables, the run's mask and
// its stamp list are plain statics with NO LOCK, and the FT face a miss
// loads from is the one the scaled font's other users share
// (gui_outline_scaled_font, itself the GUI thread's). Every road into this
// unit runs on that thread: the chrome's painters and the flag cache's
// rebuild (paint_handler.cpp, render.cpp, waveform_cache.cpp's
// maybe_rebuild_flag_cache — on_redraw, the pre-paint hook and the tick),
// the lane table's ink bands (gui_font_ink_band, main.cpp's layout). No
// worker reaches it — the waveform worker, the async renderer, the history
// workers, the score page worker (which decodes pages, never text), the
// audio and noise threads include neither text_shape.h nor a painter.

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

// THE SAME PAINT, CUT TO WHERE THE TEXT IS VISIBLE — AN EXACT CUT WITH NO
// CLIP (architect 2026-11-15, the no-grid arc; the marker lane's labels,
// render.cpp's paint_cues, where a later label's box hides an earlier
// label's text from a real x on): `visible` is `nvisible` intervals of user
// x, [x0, x1) each, ASCENDING AND DISJOINT, and the run's coverage mask is
// MULTIPLIED, COLUMN BY COLUMN, BY THE LENGTH OF THE COLUMN THAT LIES INSIDE
// THEM before it is composited — a column wholly inside keeps its coverage,
// one wholly outside is emptied, and the column a cut's edge crosses keeps
// the fraction of its coverage that the edge leaves visible. That is the
// ink clipped at the edge at exact coverage (an opaque box laid over the
// ink from the edge on leaves the same pixel), made on the text's own mask:
// the context's clip is never touched, so no fractional or multi-rect clip
// stands under any later fill or stamp, and a hidden part costs nothing to
// composite. No interval paints nothing.
struct XSpan {
    double x0 = 0.0;
    double x1 = 0.0;
};
void show_glyphs_within(cairo_t* cr, cairo_scaled_font_t* font,
                        const cairo_glyph_t* glyphs, int count,
                        const XSpan* visible, int nvisible);

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
