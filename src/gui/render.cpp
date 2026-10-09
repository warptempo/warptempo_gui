#include "render.h"
#include "app_state.h"
#include "audio.h"
#include "clearlooks_paint.h"
#include "cde_paint.h"
#include "gui_display_context.h"
#include "gui_font.h"
#include "text_shape.h"
#include "theme_file.h"
#include "palette_file.h"   // kGuiPaletteRoles, effective_palette_name
#include "cool_edit_derive.h"  // the Cool Edit block's tones off the face
#include "cool_edit_paint.h"   // kCeCaseInk, the program case's glyph ink
#include "chrome_derive.h"  // live_chrome_words (the chrome knob)
#include "value_format.h"
#include "warp_frame_map_view.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// kFlagBottomLiftPx now lives in render.h so the strip lane geometry in
// main.cpp and the stem blit in paint_handler.cpp reference the same value.

// playhead_half_px() is the half-width of the playhead column's reach; it lives
// in render.h as a single inline accessor shared by this TU's cull and
// main.cpp's invalidation, with its provenance and its authored value stated at
// the definition.

// THE NUMERIC RUN — the derived base and its whole deviation chain — is
// warp_tempo_run (warpmarkers.h), which both flag composers below and the
// SERIALIZER share. It stood here as a private twin of the serializer's own
// loop for one day, 2026-09-18/19: what the deviation chain promises is that
// the flag says what the file holds, and one body is what makes that true
// rather than a coincidence of two.

// Flag text mirrors the canonical line's PAYLOAD (post-pipe); metadata
// (b=/e=/#) never appears in it, and neither does the iteration bracket —
// the bounds are the two cells beside the flag, each with its own editor.
// This is the ONE composer for warp flag text and it CUTS NOTHING: the flag
// editor seeds from it (enter_top_flag_edit), so a commit cannot lose what
// the store holds. What the box paints is flag_display_text below. The
// contract is at the declaration (render.h).
//
// Variants:
//   label_ref              → "a.42"
//   inherit, no def        → "pass"
//   inherit, with def      → "pass:a.42"
//   owning, no scale       → "1.23"
//   owning, with a chain   → "1.23+0.01-0.02"
//   owning, with scale     → "1.23*1.2345"
//   def, no scale          → "1.23:a.03"
//   def, with scale        → "1.23*1.2345:a.03"
std::string flag_text(const std::vector<GuiWarpMarker>& markers, int idx) {
    const auto& m = markers[idx];

    if (!m.label_ref.empty()) {
        return m.label_ref;
    }

    std::string text;
    if (m.tempo_inherits) {
        text = "pass";
    } else {
        // Serializer forms (the tempo run straight from integer cents, scale
        // min-4 padded shortest round trip) — the flag paints the stored
        // value at full precision, exactly the serializer's bytes.
        text = warp_tempo_run(m);
        if (m.tempo_scale.has_value()) {
            text += "*";
            text += format_value_double(*m.tempo_scale, 4);
        }
    }
    if (!m.label_def.empty()) {
        text += ":";
        text += m.label_def;
    }
    return text;
}

// THE PAINTED FORM (architect 2026-09-19). The contract — what is cut, what
// never is, and why the seam measurement must read this and not its uncut
// sibling — is at the declaration (render.h). The scale is the only cut, and
// the truncation marker says so; with no scale there is nothing to cut and
// the whole payload paints, label definition included.
std::string flag_display_text(const std::vector<GuiWarpMarker>& markers,
                              int idx) {
    const auto& m = markers[idx];

    if (!m.label_ref.empty()) {
        return m.label_ref;
    }

    std::string text = m.tempo_inherits ? std::string("pass")
                                        : warp_tempo_run(m);
    if (!m.tempo_scale.has_value()) {
        if (!m.label_def.empty()) {
            text += ":";
            text += m.label_def;
        }
        return text;
    }
    // A scale is spelled min-4, so it is always longer than the cap and the
    // marker always follows — which is why the `||` below is not a
    // one-armed test in practice; it is there because the rule is "the marker
    // stands for whatever was cut", and a label definition is cut here too.
    const std::string scale = format_value_double(*m.tempo_scale, 4);
    text += "*";
    text += scale.substr(0, kMarkerFlagScaleGlyphs);
    if (scale.size() > kMarkerFlagScaleGlyphs || !m.label_def.empty())
        text += kMarkerLabelTruncationMarker;
    return text;
}

namespace {

// Forward-translate a per-marker effective position (a source-frame
// double) to the paint-sample position used by the stem, flag, and
// hit-rect loops. In target view (warp_frame_map
// non-null/non-empty) the source-frame is rounded with banker's
// nearbyint and looked up through map_source_to_target, and that lookup
// is itself rounded with nearbyint; in source view (null/empty
// warp_frame_map) the result is the frame double rounded with nearbyint.
// Both branches return the same integer displayed frame the playhead
// cursor stores (the active-domain translators apply the same
// nearbyint), so the stem, endcap, hit rect, and playhead share a column
// in every view. Painting from the fractional map_source_to_target value
// placed the stem one pixel off the playhead whenever rounding the
// target frame crossed a pixel-column boundary. Callers that need an
// integer sample-frame for trim or viewport arithmetic apply their own
// nearbyint to the returned double; rounding an already-integer-valued
// double is a no-op.
static inline double frame_to_paint_sample(
    double eff_frame,
    const std::vector<WarpFrameMapSegment>* warp_frame_map) {
    if (warp_frame_map && !warp_frame_map->empty()) {
        const size_t src_frame = static_cast<size_t>(
            std::nearbyint(eff_frame));
        return std::nearbyint(map_source_to_target(src_frame, *warp_frame_map));
    }
    return std::nearbyint(eff_frame);
}

} // namespace


void render_background(cairo_t* cr, int x, int y, int w, int h) {
    cairo_save(cr);
    set_palette_source(cr, palette().ground);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_restore(cr);
}

void render_canvas(cairo_t* cr, int x, int y, int w, int h) {
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // The ground is the `waveform_canvas` role (the palette's row-6 block),
    // through the waveform's chokepoint (set_waveform_source, render.h).
    set_waveform_source(cr, palette().waveform_canvas);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    // THE WELL (architect 2026-10-02; the colours at the row-6 palette block,
    // the thickness at waveform_border_px): taken FROM the area, painted in
    // the same pass as the ground so the two can never disagree about where
    // the canvas ends — the PLAIN SUNKEN edge's horizontals, full width: on
    // top a Shadow line then a DkShadow line, at the bottom a 3DLight line
    // then a Hilight line, each one relief line. waveform_content_rect reads
    // the same thickness; the stems cross the top lines (waveform_stem_band),
    // a marker's between its flanks (fill_stem_flanks, 2026-10-05),
    // and no vertical crosses the bottom ones. An area too short to carry both
    // borders draws neither rather than overlapping them.
    // UNDER CLEARLOOKS THE WELL IS GTK'S SCROLLED WINDOW (architect
    // 2026-10-07, the painters round's last part; paint_cl_well_frame,
    // clearlooks_paint.h): the outer line of each pair its one shade[5]
    // line, the inner the canvas laid above — the same thickness taken from
    // the area, so nothing that reads waveform_border_px moves.
    // UNDER CDE THE WELL IS A MOTIF TEXT WIDGET'S ONE-W SHADOW (2026-10-08;
    // dtpad's text area on the notepad capture, row 54 the field's bottom
    // shadow over the cream, the bevel one px at the ruling): the OUTER
    // line of each pair the sunken ring in the body's tones — the bottom
    // shadow on top, the top shadow at the bottom — and the inner line the
    // canvas, Clearlooks' shape in Motif's tones; the same thickness taken
    // from the area, so nothing that reads waveform_border_px moves.
    const int border = waveform_border_px();
    const int lw     = relief_line_px();
    if (h > 2 * border &&
        live_chrome_spec().vocabulary == GuiChromeVocabulary::Clearlooks) {
        paint_cl_well_frame(cr, GuiRect{x, y, w, h});
    } else if (h > 2 * border &&
               live_chrome_spec().vocabulary == GuiChromeVocabulary::Cde) {
        paint_cell_rect(cr, GuiRect{x, y, w, lw}, palette().shadow);
        paint_cell_rect(cr, GuiRect{x, y + h - lw, w, lw}, palette().hilight);
    } else if (h > 2 * border) {
        paint_cell_rect(cr, GuiRect{x, y, w, lw}, palette().shadow);
        paint_cell_rect(cr, GuiRect{x, y + lw, w, border - lw},
                        palette().dk_shadow);
        paint_cell_rect(cr, GuiRect{x, y + h - border, w, border - lw},
                        palette().light_3d);
        paint_cell_rect(cr, GuiRect{x, y + h - lw, w, lw}, palette().hilight);
    }
    cairo_restore(cr);
}

// -- The relief helpers (the contract at the declaration, render.h) ---------

void paint_cell_rect(cairo_t* cr, const GuiRect& r, GuiColor c) {
    if (r.w <= 0 || r.h <= 0) return;
    set_palette_source(cr, c);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_fill(cr);
}

namespace {
// A ring with SQUARE JOINS: the top and left in `top_left`, then the bottom
// and right in `bottom_right` over them, owning the top-right and bottom-left
// corner blocks — the one-colour frames' ring (no mitre to draw between one
// tone and itself) and the mitre's fallback on a ring narrower or shorter
// than two lines, whose two sides overlap (a thumb at its five-device-px
// floor).
void paint_square_ring(cairo_t* cr, const GuiRect& r, GuiColor top_left,
                       GuiColor bottom_right) {
    const int lw = relief_line_px();
    if (r.w <= 0 || r.h <= 0) return;
    const int lx = std::min(lw, r.w);
    const int ly = std::min(lw, r.h);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, GuiRect{r.x, r.y, r.w, ly}, top_left);
    paint_cell_rect(cr, GuiRect{r.x, r.y, lx, r.h}, top_left);
    paint_cell_rect(cr, GuiRect{r.x, r.y + r.h - ly, r.w, ly}, bottom_right);
    paint_cell_rect(cr, GuiRect{r.x + r.w - lx, r.y, lx, r.h}, bottom_right);
    cairo_restore(cr);
}
} // namespace

// THE MITRE (architect 2026-10-06, overruling the planner's reading that
// Windows' DrawEdge joins its edges squarely, the dark side owning the corner,
// at every DPI: "Buttons don't have corners like that in real life. A perfect
// square button, like a keyboard key with a relief and bevels, would have a
// smooth straight corner, not a pixelated corner. ... Anything the original
// designers would have drawn with a diagonal if they could, we should." — THE
// CHROME IS SCALABLE). At the TOP-RIGHT and BOTTOM-LEFT corner blocks, where
// the top-left tone meets the bottom-right tone, each relief_line_px() square
// is split along its diagonal from the ring's outer corner to its inner
// corner, the top-left tone on the half toward the top and left edges, the
// bottom-right tone on the half toward the bottom and right; the two-line
// edges' two rings nest, so their diagonals run on as one 45-degree line from
// the outer corner to the innermost. The top-left and bottom-right corners
// are one tone already and stay square. THE FILLS: the top-left tone is ONE
// square L (the top row and the left column, both corner blocks whole),
// unantialiased; the bottom-right tone ONE mitred L over it (the bottom row
// and the right column, each end cut along the diagonal), antialiased by
// cairo — so every pixel on the diagonal is that tone at its coverage over
// the other, exactly the two tones' blend with no ground in it (two abutting
// antialiased fills would let the ground through their shared seam). The
// anti-aliasing is the renderer's, not a colour (the palette head).
void paint_relief_frame(cairo_t* cr, const GuiRect& r, GuiColor top_left,
                        GuiColor bottom_right) {
    const int lw = relief_line_px();
    if (r.w <= 0 || r.h <= 0) return;
    if (r.w < 2 * lw || r.h < 2 * lw) {
        paint_square_ring(cr, r, top_left, bottom_right);
        return;
    }
    const double x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y0);
    cairo_line_to(cr, x1, y0 + lw);
    cairo_line_to(cr, x0 + lw, y0 + lw);
    cairo_line_to(cr, x0 + lw, y1);
    cairo_line_to(cr, x0, y1);
    cairo_close_path(cr);
    set_palette_source(cr, top_left);
    cairo_fill(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_move_to(cr, x1, y0);
    cairo_line_to(cr, x1, y1);
    cairo_line_to(cr, x0, y1);
    cairo_line_to(cr, x0 + lw, y1 - lw);
    cairo_line_to(cr, x1 - lw, y1 - lw);
    cairo_line_to(cr, x1 - lw, y0 + lw);
    cairo_close_path(cr);
    set_palette_source(cr, bottom_right);
    cairo_fill(cr);
    cairo_restore(cr);
}

namespace {
// THE TWO-LINE EDGE (DrawEdge): the outer ring on the rect, the inner ring on
// the rect inset one relief line.
void paint_relief_edge(cairo_t* cr, const GuiRect& r, GuiColor outer_tl,
                       GuiColor outer_br, GuiColor inner_tl,
                       GuiColor inner_br) {
    const int lw = relief_line_px();
    paint_relief_frame(cr, r, outer_tl, outer_br);
    paint_relief_frame(cr, GuiRect{r.x + lw, r.y + lw, r.w - 2 * lw,
                                   r.h - 2 * lw},
                       inner_tl, inner_br);
}
} // namespace

void paint_relief_soft_raised(cairo_t* cr, const GuiRect& r) {
    paint_relief_edge(cr, r, palette().hilight, palette().dk_shadow, palette().light_3d,
                      palette().shadow);
}

void paint_relief_soft_sunken(cairo_t* cr, const GuiRect& r) {
    paint_relief_edge(cr, r, palette().dk_shadow, palette().hilight, palette().shadow,
                      palette().light_3d);
}

void paint_relief_plain_raised(cairo_t* cr, const GuiRect& r) {
    paint_relief_edge(cr, r, palette().light_3d, palette().dk_shadow, palette().hilight,
                      palette().shadow);
}

void paint_relief_plain_sunken(cairo_t* cr, const GuiRect& r) {
    paint_relief_edge(cr, r, palette().shadow, palette().hilight, palette().dk_shadow,
                      palette().light_3d);
}

void paint_relief_sunken_outer(cairo_t* cr, const GuiRect& r) {
    paint_relief_frame(cr, r, palette().shadow, palette().hilight);
}

void paint_checker_rect(cairo_t* cr, const GuiRect& r, int phase_x,
                        int phase_y, GuiColor lit, GuiColor ground) {
    if (r.w <= 0 || r.h <= 0) return;
    // THE TILE, Windows' pattern brush: 2 x 2 device px, lit at (0, 0) and
    // (1, 1), the ground at the other two — both cells theme roles handed in,
    // written as words (argb32_opaque_word), so every pixel the fill lays is
    // one of the two and the fill is opaque; each cell's authored color
    // converted for the window (display_color, the painter's entry).
    cairo_surface_t* tile =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 2, 2);
    cairo_surface_flush(tile);
    unsigned char* data = cairo_image_surface_get_data(tile);
    const int stride = cairo_image_surface_get_stride(tile);
    const uint32_t lit_word    = argb32_opaque_word(display_color(lit));
    const uint32_t ground_word = argb32_opaque_word(display_color(ground));
    for (int y = 0; y < 2; ++y) {
        uint32_t* row = reinterpret_cast<uint32_t*>(data + y * stride);
        for (int x = 0; x < 2; ++x)
            row[x] = (x + y) % 2 == 0 ? lit_word : ground_word;
    }
    cairo_surface_mark_dirty(tile);
    // REPEATED, NEAREST, ITS ORIGIN AT THE PHASE: the pattern's (0, 0) lies on
    // (phase_x, phase_y), so that pixel is lit and the lattice runs on from
    // it either way; one fill of the rect, cairo cutting it to the clip.
    cairo_pattern_t* brush = cairo_pattern_create_for_surface(tile);
    cairo_pattern_set_extend(brush, CAIRO_EXTEND_REPEAT);
    cairo_pattern_set_filter(brush, CAIRO_FILTER_NEAREST);
    cairo_matrix_t to_tile;
    cairo_matrix_init_translate(&to_tile, -static_cast<double>(phase_x),
                                -static_cast<double>(phase_y));
    cairo_pattern_set_matrix(brush, &to_tile);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source(cr, brush);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_fill(cr);
    cairo_restore(cr);
    cairo_pattern_destroy(brush);
    cairo_surface_destroy(tile);
}

void paint_stipple_begin(cairo_t* cr, const GuiRect& bounds) {
    // The rule at the declaration.
    cairo_save(cr);
    cairo_rectangle(cr, bounds.x, bounds.y, std::max(0, bounds.w),
                    std::max(0, bounds.h));
    cairo_clip(cr);
    cairo_push_group(cr);
}

void paint_stipple_end(cairo_t* cr, int phase_x, int phase_y) {
    // THE MASK, Motif's 50 % stipple at one device px a cell: a 2 x 2 A8
    // tile, opaque at (0, 0) and (1, 1), repeated, nearest, its origin at the
    // phase — the paint_checker_rect brush's lattice, as an alpha mask.
    cairo_pop_group_to_source(cr);
    cairo_surface_t* tile = cairo_image_surface_create(CAIRO_FORMAT_A8, 2, 2);
    cairo_surface_flush(tile);
    unsigned char* data = cairo_image_surface_get_data(tile);
    const int stride = cairo_image_surface_get_stride(tile);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            data[y * stride + x] = (x + y) % 2 == 0 ? 0xFF : 0x00;
    cairo_surface_mark_dirty(tile);
    cairo_pattern_t* mask = cairo_pattern_create_for_surface(tile);
    cairo_pattern_set_extend(mask, CAIRO_EXTEND_REPEAT);
    cairo_pattern_set_filter(mask, CAIRO_FILTER_NEAREST);
    cairo_matrix_t to_tile;
    cairo_matrix_init_translate(&to_tile, -static_cast<double>(phase_x),
                                -static_cast<double>(phase_y));
    cairo_pattern_set_matrix(mask, &to_tile);
    cairo_mask(cr, mask);
    cairo_pattern_destroy(mask);
    cairo_surface_destroy(tile);
    cairo_restore(cr);
}

void paint_relief_line_frame(cairo_t* cr, const GuiRect& r, GuiColor c) {
    paint_square_ring(cr, r, c, c);
}

// THE SCRUB'S THUMB (the rule and its measurement at render.h's scrub
// block). In device px from the box's top-left: ring k (k = 0 the
// silhouette, 1 the soft edge's inner line, 2 the face) is the pentagon
//     (k lw, k lw) (w - k lw, k lw) (w - k lw, h - p) (w / 2, h - k lw)
//     (k lw, h - p)
// — its verticals ending where the point begins, h - p, its tip k lines up
// the axis, the playhead head's construction. Each line (ring k less ring
// k + 1) is two regions: the LIGHT one on the top, the left and the left
// diagonal and the DARK one on the right and the right diagonal, mitred
// along the 45-degree diagonal at the top-right corner and meeting on the
// axis at the tip. The five regions and the face are ONE PARTITION filled
// ADDED into a cleared group (so shared antialiased edges sum to whole
// pixels and no colour bleeds through a seam), then the group laid over the
// row once.
void paint_scrub_thumb(cairo_t* cr, const GuiRect& box) {
    if (box.w <= 0 || box.h <= 0) return;
    const GuiPalette& pal = palette();
    const double x  = box.x;
    const double y  = box.y;
    const double w  = box.w;
    const double h  = box.h;
    const double lw = relief_line_px();
    const double p  = std::min(static_cast<double>(scrub_thumb_point_px()),
                               h - 2.0 * lw);
    const double mid = w / 2.0;
    const auto poly = [&](std::initializer_list<std::pair<double, double>> pts) {
        cairo_new_path(cr);
        bool first = true;
        for (const auto& [px, py] : pts) {
            if (first) cairo_move_to(cr, x + px, y + py);
            else       cairo_line_to(cr, x + px, y + py);
            first = false;
        }
        cairo_close_path(cr);
    };
    // One line, ring k less ring k + 1, its light half then its dark half.
    const auto line = [&](double k, GuiColor light, GuiColor dark) {
        const double o = k * lw, i = (k + 1.0) * lw;
        poly({{o, o}, {w - o, o}, {w - i, i}, {i, i}, {i, h - p},
              {mid, h - i}, {mid, h - o}, {o, h - p}});
        set_palette_source(cr, light);
        cairo_fill(cr);
        poly({{w - o, o}, {w - o, h - p}, {mid, h - o}, {mid, h - i},
              {w - i, h - p}, {w - i, i}});
        set_palette_source(cr, dark);
        cairo_fill(cr);
    };
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_push_group(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
    line(0.0, pal.hilight, pal.dk_shadow);
    line(1.0, pal.light_3d, pal.shadow);
    const double f = 2.0 * lw;
    poly({{f, f}, {w - f, f}, {w - f, h - p}, {mid, h - f}, {f, h - p}});
    set_palette_source(cr, pal.ground);
    cairo_fill(cr);
    cairo_pop_group_to_source(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_paint(cr);
    cairo_restore(cr);
}

// -- THE CAPTION'S GRADIENT (architect 2026-10-05) ---------------------------
//
// THE ONE GRADIENT IN THE PRODUCT: a theme that records a gradient end
// (Windows 98's and 2000's GradientActiveTitle / GradientInactiveTitle) draws
// its caption from the start colour at the left to the end at the right,
// linear per channel, across the whole caption rect the painter hands in —
// under the icon and the buttons too, ReactOS's span (painting.c's
// GRADIENT_FILL_RECT_H over the full caption), where Windows 2000 holds the
// icon cell at the start colour and the buttons' stretch at the end (a
// recorded departure, docs/engineering/win2000_deviations.md).
//
// SMOOTH 24-BIT (architect 2026-10-06): each channel of each device column x
// is round(s + (e - s) * x / (w - 1)) — the start at the first column, the
// end at the last — every row alike, no dither and no quantisation:
// ReactOS's caption on the architect's capture, GreGradientFill at 32-bit
// colour (rounded linear ramps matched 589 of its 592 columns, the other
// three one level off), as Windows 2000 itself drew at 24 and 32 bits
// ("Dithering is performed in 16-, 8-, 4-, and 1-bpp mode", GradientFill's
// documentation). The cell is the device px, the scalable chrome's.
//
// A FLAT CAPTION (end equal to start — a theme that records no gradient
// end, the generator's flat-caption rule, tools/theme_catalog/
// gen_theme_files.py) IS ONE SOLID FILL OF THE EXACT COLOUR.
//
// OPAQUE AND NOTHING BLENDED AT PAINT TIME: each cell is one opaque rounded
// ramp value, written as words into an image (argb32_opaque_word's road, the
// plate's precedent) and laid on the caption whole. The image is kept
// between paints and rebuilt only when the caption's size, either colour or
// the conversion state moves (the picture reads nothing else; a gui_scale
// change moves the size; True Colors flips the conversion,
// install_true_colors), since the top strip's damage repaints the caption
// often and its ramp seldom changes.
namespace {
struct CaptionGradientImage {
    cairo_surface_t* surface = nullptr;
    int              w = 0, h = 0;
    uint32_t         start = 0, end = 0;
    bool             converted = false;   // display_transform::active()
};
CaptionGradientImage g_caption_gradient;
} // namespace

void paint_caption_gradient(cairo_t* cr, const GuiRect& r, GuiColor start,
                            GuiColor end) {
    if (r.w <= 0 || r.h <= 0) return;
    const uint32_t start_word = argb32_opaque_word(start);
    const uint32_t end_word   = argb32_opaque_word(end);
    if (start_word == end_word) {
        paint_cell_rect(cr, r, start);
        return;
    }
    CaptionGradientImage& img = g_caption_gradient;
    const bool converted = display_transform::active();
    if (!img.surface || img.w != r.w || img.h != r.h ||
        img.start != start_word || img.end != end_word ||
        img.converted != converted) {
        if (img.surface) cairo_surface_destroy(img.surface);
        img = CaptionGradientImage{
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r.w, r.h), r.w,
            r.h, start_word, end_word, converted};
        cairo_surface_flush(img.surface);
        unsigned char* data = cairo_image_surface_get_data(img.surface);
        const int stride = cairo_image_surface_get_stride(img.surface);
        const double s[3] = {start.r * 255.0, start.g * 255.0, start.b * 255.0};
        const double e[3] = {end.r * 255.0, end.g * 255.0, end.b * 255.0};
        // One row of rounded ramp words, then every row a copy of it. Each
        // level rounds by std::nearbyint, the tree's one rounding (a level
        // is not a grid point; this is symmetry with svg_icon's
        // saturated_copy, not a pixel). It parts from std::lround only on an
        // exact half, which takes the even level: no compiled theme's pair
        // lands on one at the two devices' full-width
        // captions (1920 and 2304 device px), so those ramps are unchanged;
        // other widths (the restored laptop's) can meet one, one level apart.
        // THE RAMP IS sRGB LEVELS, as ReactOS wrote them into its own
        // (sRGB) frame buffer: each column's rounded level is an authored
        // color like any other and is converted for the window at the
        // painter's entry (display_transform::display_rgb), never the two
        // ends converted and the ramp run between them.
        std::vector<uint32_t> row(static_cast<size_t>(r.w));
        for (int x = 0; x < r.w; ++x) {
            const double t = r.w > 1 ? static_cast<double>(x) / (r.w - 1)
                                     : 0.0;
            uint32_t rgb = 0;
            for (int c = 0; c < 3; ++c) {
                const long v = static_cast<long>(
                    std::nearbyint(s[c] + (e[c] - s[c]) * t));
                rgb |= static_cast<uint32_t>(std::clamp(v, 0L, 255L))
                       << (16 - 8 * c);
            }
            row[static_cast<size_t>(x)] =
                UINT32_C(0xFF000000) | display_transform::display_rgb(rgb);
        }
        for (int y = 0; y < r.h; ++y)
            std::memcpy(data + y * stride, row.data(),
                        static_cast<size_t>(r.w) * sizeof(uint32_t));
        cairo_surface_mark_dirty(img.surface);
    }
    cairo_save(cr);
    cairo_set_source_surface(cr, img.surface, r.x, r.y);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_fill(cr);
    cairo_restore(cr);
}

// -- THE SIZING FRAME (architect 2026-10-05; the rule at the declaration) ----

int window_frame_px() {
    // The spec's relief lines round Windows' two W of face (the
    // declaration): 2 + 2 under win2000 and clearlooks, dtwm's 3 + 2 under
    // cde.
    return live_chrome_spec().window_frame_lines * relief_line_px() +
           scaled_px(kWindowFramePx - 2 * kReliefLinePx, 0);
}

void paint_window_sizing_frame(cairo_t* cr, int surface_w, int surface_h,
                               int frame_px, bool focused) {
    if (frame_px <= 0 || surface_w <= 0 || surface_h <= 0) return;
    const int f = frame_px;
    // UNDER CLEARLOOKS metacity's `normal` frame (paint_cl_window_frame,
    // clearlooks_paint.h, where the agreement of the two geometries and the
    // one departure stand), painted on the band alone: the client area is
    // the app's, whose caption lane draws the title bar's lower rows with
    // the same painter (paint_caption_row).
    if (live_chrome_spec().vocabulary == GuiChromeVocabulary::Clearlooks) {
        cairo_save(cr);
        cairo_rectangle(cr, 0, 0, surface_w, surface_h);
        cairo_rectangle(cr, f, f, surface_w - 2 * f, surface_h - 2 * f);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_clip(cr);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
        paint_cl_window_frame(cr, 0, 0, surface_w, surface_h, f,
                              caption_row_h_px(), focused);
        cairo_restore(cr);
        return;
    }
    // UNDER CDE dtwm's frame (paint_cde_window_frame, cde_paint.h: the
    // frame's colour following the activation, its two rings), on the band
    // alone.
    if (live_chrome_spec().vocabulary == GuiChromeVocabulary::Cde) {
        paint_cde_window_frame(cr, surface_w, surface_h, f, focused);
        return;
    }
    // The ground across the band, then the window's raised edge on its outer
    // two lines.
    paint_cell_rect(cr, GuiRect{0, 0, surface_w, f}, palette().ground);
    paint_cell_rect(cr, GuiRect{0, surface_h - f, surface_w, f},
                    palette().ground);
    paint_cell_rect(cr, GuiRect{0, f, f, surface_h - 2 * f}, palette().ground);
    paint_cell_rect(cr, GuiRect{surface_w - f, f, f, surface_h - 2 * f},
                    palette().ground);
    paint_relief_plain_raised(cr, GuiRect{0, 0, surface_w, surface_h});
}

void paint_relief_etched_hline(cairo_t* cr, int x, int y, int w) {
    const int lw = relief_line_px();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, GuiRect{x, y, w, lw}, palette().shadow);
    paint_cell_rect(cr, GuiRect{x, y + lw, w, lw}, palette().hilight);
    cairo_restore(cr);
}

void render_waveform(cairo_surface_t* dest,
                     GuiRect area,
                     int col0,
                     const GuiAudio& audio,
                     int channel,
                     const WaveformBasis& basis,
                     const WaveformGainCurve* gain_or_null,
                     int outline_px,
                     WaveformPlateInks inks,
                     const std::vector<WarpFrameMapSegment>* warp_frame_map) {
    if (!dest) return;
    if (area.w <= 0 || area.h <= 2) return;
    if (basis.full_width <= 0) return;
    // The lattice step must be numeric and positive; a degenerate zoom refuses
    // here exactly as the old empty-viewport check did.
    if (!(basis.spp > 0.0)) return;

    const int num_levels = audio.num_levels();
    if (num_levels <= 0) return;

    // ARGB32 ONLY: the writer stores 32-bit premultiplied words, so any other
    // format would be silently misinterpreted. Both plate surfaces are created
    // CAIRO_FORMAT_ARGB32 (waveform_cache.cpp); this is the guard that keeps
    // that true. Geometry comes from the surface itself — the stride accessor,
    // never width*4, since cairo is free to pad rows.
    if (cairo_image_surface_get_format(dest) != CAIRO_FORMAT_ARGB32) return;
    // Flush BEFORE the first CPU access so any pending cairo drawing (the
    // caller's CLEAR of the columns this call regenerates) has landed in the
    // buffer. Paired with the cairo_surface_mark_dirty after the last write.
    cairo_surface_flush(dest);
    unsigned char* const surf_data = cairo_image_surface_get_data(dest);
    if (!surf_data) return;
    const int surf_stride = cairo_image_surface_get_stride(dest);
    const int surf_w      = cairo_image_surface_get_width(dest);
    const int surf_h      = cairo_image_surface_get_height(dest);
    if (surf_w <= 0 || surf_h <= 0) return;

    // THE AUTHORING LATTICE (see WaveformBasis). Recover the viewport's lattice
    // index with the SAME expression clamp_viewport_start uses to snap onto it,
    // so a resting viewport round-trips exactly; an off-lattice mid-gesture
    // viewport quantizes to its nearest rest. Columns are then indexed globally
    // from k0, which is what makes a pan a pure index shift — a column's frames
    // depend on k0+c and nothing else, never on which window drew it.
    const double samples_per_pixel = basis.spp;
    double k0d = std::nearbyint(static_cast<double>(basis.vp_start) /
                                samples_per_pixel);
    if (!(k0d >= 0.0)) k0d = 0.0;          // also rejects NaN
    const long long k0 = static_cast<long long>(k0d);

    // PYRAMID LEVEL IS CHOSEN PER COLUMN, from that column's own mapped SOURCE
    // width, through the one level-choosing owner (GuiAudio::level_for_span —
    // the stride ladder lives there and nothing here knows it).
    //
    // Source view: the mapped width is the basis spp for EVERY column — one
    // uniform value, so per-column selection provably yields the identical
    // level throughout and this is not a behavior change (only the denser
    // ladder is). It is passed as the exact spp rather than a per-column
    // rounded span precisely so that invariance holds by construction and
    // cannot wobble across a stride threshold on a rounding tie.
    //
    // Target view: the width is the column's TRUE local mapped span (g1 - g0),
    // which is what the read actually costs. The old single pick came from the
    // viewport-wide TARGET-domain spp while the reads are source-domain, and
    // the legal local slope reaches 16x (tempo 4 * marker scale 2 * settings
    // scale 2) — so a tempo-compressed column could read a far finer level than
    // its span warranted, up to hundreds of samples in one column. Selecting
    // from the column's own span restores the intended per-column bound (<=5
    // pairs or <=16 raw samples, unconditionally — the statement and its proof
    // live at GuiAudio::level_for_span) and is
    // strictly MORE accurate than the global estimate it replaces.
    //
    // Consequence, accepted: where the map slope crosses a stride threshold,
    // adjacent target-view columns may read different levels, a per-column
    // statistics discontinuity — now confined to the one column that reads it,
    // since no segment carries anything into a neighbour. Aesthetic only.
    const auto level_for_column = [&](double src_width) {
        return audio.level_for_span(warp_frame_map ? src_width
                                                   : samples_per_pixel);
    };

    const double y_center = area.y + area.h * 0.5;
    const double half_h   = area.h * 0.5;

    // THE VISUAL MAGNIFICATION, a function of source time: each column's
    // OUTER scale is the derived curve's gain at the column's centre source
    // frame, its INNER scale the compressor's scale times the foreground
    // gain's half at the same frame (folded together at the derivation), and
    // both take the expander's multiplier over the column's working columns.
    // The contract (the two bars' order and inks, the coarse-zoom centre rule
    // and the expander's smallest-reduction rule) is at this function's
    // declaration; the arithmetic is one multiply and ONE clamp per tip.
    // IT SCALES PIXELS ONLY — nothing this function touches is audio.
    const auto magnified_tip = [](double raw, double scale) {
        double v = raw * scale;
        if (v < -1.0) v = -1.0;
        if (v >  1.0) v =  1.0;
        return v;
    };

    // Each column is written straight into the plate's pixel words, and a
    // column is ONE HARD BAR: its own raw min/max interval, floored to rows and
    // filled inclusively with the opaque ink word (with the lamp lit, the
    // outer bar goes down first and the inner over it, both in the ink word,
    // the inner's outline (outline_px thick) in the outline word — the rule is at this
    // function's declaration).
    // There is no fractional coverage and no inter-column connectivity of
    // any kind — a spike stands alone, exactly as in a classic min/max
    // renderer; the inner's outline READS the neighbouring columns' rows to
    // decide which of the bar's own pixels are its border, and adds none.
    //
    // THE ANTIALIASED RENDERER IS DELETED (architect 2026-08-01, after the
    // side-by-side against a snapshotted AA binary: "subtle but noticeable — I
    // prefer without it"). What went is named here so its absence reads as a
    // decision rather than an omission: the Wu tip polylines and their
    // max-coverage compositing, the fractional boundary rows, the 256-entry
    // premultiplied coverage table, and BOTH EDGE HALOS. The technique is
    // recorded in docs/engineering/waveform_antialiasing_retired.md.
    //
    // THE >=1px NEVER-FADE FLOOR SURVIVES, as integer geometry rather than as a
    // unit deposit: floor(top) and floor(bot) coincide for any sub-pixel
    // interval, so the inclusive fill always writes at least one row and flat or
    // silent material draws a hairline instead of fading out.
    //
    // THE HALOS WENT BECAUSE THEIR REASON WENT. They existed so an EDGE column
    // would carry the same ink an interior column gets — under the segment
    // model a column's ink came from the segments on both its sides, so a
    // missing offscreen neighbour under-covered it and it popped during a pan.
    // A bar depends on nothing but its own interval, so every column is now
    // self-contained and there is nothing for an offscreen neighbour to
    // contribute: pan invariance strengthened rather than weakened here.
    //
    // THE PREMULTIPLIED WORDS, each built once per call through the one word
    // owner (argb32_opaque_word, render.h — its byte-order and rounding
    // contract lives there): the plate's ink (`inks`, the job's snapshot of
    // the `waveform_ink` role), worn by the dark lamp's raw bar and by both lit
    // bars' fills, and the inner bar's outline (the `waveform_outline` role;
    // built always, written only when lit).
    // Each is already the window's word (WaveformPlateInks, converted on the
    // GUI thread by waveform_plate_inks; display_transform.h's head), so the
    // worker reads no switch.
    const uint32_t ink_word     = argb32_opaque_word(hex(inks.ink_rgb));
    const uint32_t outline_word = argb32_opaque_word(hex(inks.outline_rgb));

    // Row bounds: this channel's band, intersected with the surface.
    int y_lo = area.y;
    int y_hi = area.y + area.h;          // exclusive
    if (y_lo < 0)      y_lo = 0;
    if (y_hi > surf_h) y_hi = surf_h;
    if (y_hi <= y_lo) return;

    // COLUMNS THIS CALL OWNS. Every write is clipped to them, so a partial
    // render, if one is ever reintroduced, cannot bleed into a neighbour's
    // columns. (They also bound the write to the surface: both ends are clamped
    // into [0, surf_w) here, once, instead of at every store.)
    int col_lo = area.x;
    int col_hi = area.x + area.w;
    if (col_lo < 0)      col_lo = 0;
    if (col_hi > surf_w) col_hi = surf_w;
    if (col_hi <= col_lo) return;

    // Write one pixel word, REPLACING what is there. Row/column bounds are
    // established by the bar writer below; this is its single store site.
    // Replace is unambiguously correct now: the caller cleared every column this
    // call regenerates, and each column is written once by its one bar with
    // the lamp dark, and lit by its outer bar and then its inner, which
    // overwrites the outer where they overlap, the order the magnification rule wants
    // (the max-compositing the tip segments needed went with them).
    const auto put = [&](int x, int y, uint32_t word) {
        auto* px = reinterpret_cast<uint32_t*>(
            surf_data + static_cast<size_t>(y) * surf_stride);
        px[x] = word;
    };

    // Global column c's display-domain edge, AS THE LATTICE POINT ITSELF:
    // g(k0+c) = nearbyint((k0+c)*spp), bit-for-bit the integer
    // clamp_viewport_start's grid() lambda produces. THE QUANTIZE LIVES HERE,
    // once, so every consumer — the loop and the carried-endpoint chain —
    // receives the same already-rounded lattice point and BOTH VIEWS
    // consume the identical integer. Rounding here rather than downstream is
    // what makes the target-view path honest: to_source used to truncate the
    // raw product through its size_t cast, so target view mapped
    // floor((k0+c)*spp) — a frame below the documented g(k0+c) whenever the
    // fraction would have rounded up, with ties following truncation instead of
    // banker's rounding. The pan invariant held either way (floor of a lattice
    // point is still a pure function of the global index), but the geometry sat
    // off the lattice this contract declares.
    const auto edge_at = [&](long long c) {
        return std::nearbyint(static_cast<double>(k0 + c) * samples_per_pixel);
    };
    // Display-domain lattice point -> source frame. `f` arrives INTEGRAL from
    // edge_at, so the size_t cast below is exact, not a second quantization.
    // Source view is the identity: the value is already g(k0+c), and the
    // caller's nearbyint on it is idempotent. Target view maps exactly that same
    // g(k0+c) through the warp_frame_map, so the pyramid read lands at the
    // matching authored audio. Negative display positions clamp at 0 (the map
    // takes an unsigned frame); callers treat a wholly-left-of-zero span as
    // empty rather than relying on this clamp.
    const auto to_source = [&](double f) {
        return warp_frame_map
                   ? map_target_to_source(
                         static_cast<size_t>(f < 0.0 ? 0.0 : f), *warp_frame_map)
                   : f;
    };

    // THE RUNNING LEFT EDGE in SOURCE frames, and the carried-endpoint chain it
    // serves: column i's left edge IS column i-1's right edge, so each edge is
    // translated through the map once rather than twice. It seeds at the FIRST
    // DRAWN column's own left edge — the halo column that used to seed it one
    // step earlier is gone with the segments (the deletion note is at the top of
    // this function).
    double g_prev = to_source(edge_at(static_cast<long long>(col0)));

    // THE BAR'S ROWS. The column's tips — its maximum -> top tip, its minimum
    // -> bottom tip, in float rows, never snapped — are clamped to this
    // channel's rows BEFORE any row index is derived, so a clipped interval
    // cannot address outside the band; then both ends are floored and both
    // row indices are clamped into the band's rows [y_lo, y_hi - 1], and the
    // bar is the rows r0 .. r1 inclusive. r0 == r1 for any sub-pixel
    // interval, which is the >=1px floor stated at the top of this function,
    // and it holds for both bars AT BOTH LANE EDGES: a bar clamped whole to
    // the top edge is the lane's top row, and one clamped whole to the bottom
    // edge (both tips at -1, flooring to the row past the lane) is the lane's
    // bottom row. The bar is never empty: the column's minimum is at most its
    // maximum and a tip's scale is positive, so yt <= yb, and the clamps keep
    // r0 <= r1. The regime split (thin vs tall) went with the tip segments:
    // there is one rendering for every column now, however small its
    // interval. One owner for both bars and both lamps, so the outer and the
    // inner cannot disagree about the geometry.
    struct BarRows {
        int r0;
        int r1;
    };
    const auto bar_rows = [&](double tip_min, double tip_max) {
        double yt = y_center - tip_max * half_h;
        double yb = y_center - tip_min * half_h;
        const double row_lo = static_cast<double>(y_lo);
        const double row_hi = static_cast<double>(y_hi);   // exclusive
        if (yt < row_lo) yt = row_lo;
        if (yb < row_lo) yb = row_lo;
        if (yt > row_hi) yt = row_hi;
        if (yb > row_hi) yb = row_hi;
        int r0 = static_cast<int>(std::floor(yt));
        int r1 = static_cast<int>(std::floor(yb));
        if (r0 < y_lo)     r0 = y_lo;
        if (r0 > y_hi - 1) r0 = y_hi - 1;
        if (r1 > y_hi - 1) r1 = y_hi - 1;
        return BarRows{r0, r1};
    };

    // THE DARK BAR AND THE LIT OUTER: its rows filled with one word, no
    // outline.
    const auto fill_bar = [&](int x, const BarRows& b, uint32_t word) {
        if (x < col_lo || x >= col_hi) return;
        for (int y = b.r0; y <= b.r1; ++y) put(x, y, word);
    };

    // THE LIT INNER BAR: its rows, each written ONCE with the ink word or the
    // outline word (the outline recolours pixels of the bar's own shape and
    // adds none). THE CONTOUR FROM THE 2t + 1 COLUMNS' EXTENTS, t =
    // outline_px (the line width, render.h's waveform_line_px, snapshotted on
    // the job), no 2D scan: a row of this bar is INTERIOR iff every row within
    // t above and below it is in this bar (rows [r0 + t, r1 - t]) and it lies
    // inside every column within t on each side (inside that column's
    // [r0, r1]; a column beyond the plate's side edge counts as inside) — an
    // erosion at distance t, the four-neighbour test at t = 1. That interior
    // is one interval [lo, hi] because each bar is one; the rows of the bar
    // above and below it are the border, the whole bar when the interior is
    // empty. No inner tip reaches full scale (kForegroundGain's
    // static_assert, waveform_gain.cpp), so no inner bar is clipped and each
    // of its ends is a true end of the shape, taking the border.
    assert(!gain_or_null || outline_px >= 1);
    const int t = outline_px;
    const auto outline_bar = [&](int i, const std::vector<BarRows>& rows) {
        const int x = area.x + i;
        if (x < col_lo || x >= col_hi) return;
        const BarRows& b = rows[static_cast<size_t>(i)];
        int lo = b.r0 + t;
        int hi = b.r1 - t;
        for (int d = 1; d <= t; ++d) {
            if (i - d >= 0) {
                const BarRows& n = rows[static_cast<size_t>(i - d)];
                lo = std::max(lo, n.r0);
                hi = std::min(hi, n.r1);
            }
            if (i + d < area.w) {
                const BarRows& n = rows[static_cast<size_t>(i + d)];
                lo = std::max(lo, n.r0);
                hi = std::min(hi, n.r1);
            }
        }
        if (lo > hi) {
            for (int y = b.r0; y <= b.r1; ++y) put(x, y, outline_word);
            return;
        }
        // lo >= r0 and hi <= r1 here: each clause only narrows the interval.
        for (int y = b.r0;   y < lo;     ++y) put(x, y, outline_word);
        for (int y = lo;     y <= hi;    ++y) put(x, y, ink_word);
        for (int y = hi + 1; y <= b.r1;  ++y) put(x, y, outline_word);
    };

    // THE LIT INNER BARS' ROWS, one per column of this call, held so the
    // write pass can read the neighbours within t of every column. Both
    // callers are full-plate renders, so this call's columns ARE the plate and
    // its first and last columns' missing neighbours are the plate's side
    // edges.
    std::vector<BarRows> inner_rows;
    if (gain_or_null) inner_rows.resize(static_cast<size_t>(area.w));

    for (int i = 0; i < area.w; i++) {
        const long long c  = static_cast<long long>(col0) + i;
        const double    f1 = edge_at(c + 1);
        const double    g0 = g_prev;
        const double    g1 = to_source(f1);

        const long long s0 = static_cast<long long>(std::nearbyint(g0));
        long long       s1 = static_cast<long long>(std::nearbyint(g1));
        if (s1 <= s0) s1 = s0 + 1;

        const int level = level_for_column(g1 - g0);
        const auto mm = audio.get_peak_range(channel, level, s0, s1);

        // LIT: THE OUTER, the column's raw extremes times the curve's gain at
        // the column's centre source frame and the expander's largest
        // multiplier over the working columns [s0, s1) spans; THE INNER, the
        // same extremes times the inner scale (the compressor's times the
        // foreground gain's half) at the same frame and the same multiplier. Each is clamped to the sample domain [-1, 1]
        // BEFORE it becomes rows: the clamp is what makes a magnified forte
        // clip flat against the lane's edges instead of running off into row
        // arithmetic (the inner's scale, c x 1/2, is at most 1/2, so its
        // clamp is a no-op kept for the one shape). A PICTURE gain: the samples
        // themselves are untouched, here and everywhere. The outer is
        // written here in the plate's ink; the inner's rows are held for the
        // write pass below, which paints it over the outer with its outline.
        // DARK: the raw bar at scale 1.0, where the clamp is a no-op (raw
        // peaks already rest in range), written here in the plate's ink, so
        // the dark plate is the plate this writer always drew.
        if (gain_or_null) {
            const int64_t centre = (s0 + s1) / 2;
            const double e = static_cast<double>(
                waveform_expander_multiplier_over(*gain_or_null, s0, s1));
            const double outer = waveform_gain_at(*gain_or_null, centre) * e;
            const double inner = waveform_inner_scale_at(*gain_or_null, centre) * e;
            fill_bar(area.x + i,
                     bar_rows(magnified_tip(mm.first, outer),
                              magnified_tip(mm.second, outer)),
                     ink_word);
            inner_rows[static_cast<size_t>(i)] =
                bar_rows(magnified_tip(mm.first, inner),
                         magnified_tip(mm.second, inner));
        } else {
            fill_bar(area.x + i,
                     bar_rows(magnified_tip(mm.first, 1.0),
                              magnified_tip(mm.second, 1.0)),
                     ink_word);
        }

        g_prev = g1;
    }

    // THE LIT WRITE PASS, per column: the inner's fill and outline over the
    // outer the loop above wrote — replace-writes, so where the two overlap
    // the inner wins, as it always has.
    if (gain_or_null) {
        for (int i = 0; i < area.w; i++) outline_bar(i, inner_rows);
    }

    // Last CPU write is done — hand the buffer back to cairo.
    cairo_surface_mark_dirty(dest);
}

void render_playhead(cairo_t* cr,
                     GuiRect area,
                     double  playhead_pixel_x,
                     GuiColor color,
                     PlayheadForm form) {
    if (area.w <= 0 || area.h <= 0) return;
    // Allow partial render at file start / end: a playhead whose column has
    // clipped just past the area edge still gets here, and the column gate
    // below decides whether any pixel lands. This keeps the playhead's visual
    // center aligned with its true frame position rather than snapping it
    // inward at the rightmost samples. The bound is the column's own reach
    // (playhead_half_px), unchanged by the triangle's retirement — the cull is
    // stated in the same half-width the invalidation uses.
    if (playhead_pixel_x < -static_cast<double>(playhead_half_px())) return;
    if (playhead_pixel_x > static_cast<double>(area.w - 1 + playhead_half_px())) return;

    // THE COLUMN IS THE NEAREST LATTICE POINT WHILE THE PLATE'S BAR IN IT IS A
    // CELL, AND THE HALF-COLUMN BIAS THAT LEAVES IS ACCEPTED (architect
    // 2026-09-02, a coarse-zoom cost).
    // The playhead — and the marker stems, endcaps and flags, which all reach
    // their column through the same nearbyint rule (frame_to_paint_sample at
    // the head of this file) — paints AT a lattice point, while the bar drawn
    // in that column covers the frames AFTER it, [g(c), g(c+1)) in the plate
    // loop above. So a point and the bar under it can disagree by up to half a
    // column: invisible at the working zoom, and only at coarse zooms does the
    // line fall on the wrong side of a transient (up to ~0.47 s at the full
    // zoom-out of a 30-minute movement, where one column is nearly a second
    // wide). The POINT model is what is load-bearing and it is not traded for
    // the cell rule here: every column→frame landing in the product rides it,
    // with the worst-case round-trip residue of the target-view landing's
    // derivation (half a source frame at the 1/16 slope floor plus three half
    // target frames, 9.5 / q px at the deepest zoom's q, half the working
    // column): 0.345 px on the laptop at 44.1 kHz (q = 27.5), 0.413 px on the
    // tablet (q = 23), and under half a pixel at every waveform width, the
    // deepest-zoom floor holding q >= 20 (0.475 px; kDeepestZoomMinFramesPerPx,
    // app_state.h). Reading the bar as [g(c) − spp/2, g(c) + spp/2) is the
    // alternative that was declined.
    const int col = static_cast<int>(std::nearbyint(playhead_pixel_x));

    cairo_save(cr);
    // The waveform_line_px()-wide line (render.h) paints whenever its column
    // is onscreen (gated on its own column and clipped at the right edge), so
    // it never leaks into an adjacent region, straight over whatever it
    // crosses — waveform ink included. THE CANVAS'S ROWS ALONE (2026-10-09):
    // the scanner solid, the resting cursor Cool Edit's dotted column on its
    // own phase (the declaration).
    const GuiRect rows = waveform_content_rect(area);
    set_palette_source(cr, color);
    if (form == PlayheadForm::Dotted)
        fill_dotted_waveform_line(cr, area.x, area.w, col, rows.y,
                                  rows.y + rows.h, kProgramSpec.dot_period,
                                  kProgramSpec.playhead_dot_phase);
    else
        fill_waveform_line(cr, area.x, area.w, col, rows.y, rows.y + rows.h);
    cairo_restore(cr);
}

void render_strip_anchor_stem(cairo_t* cr, GuiRect area, int col) {
    if (area.w <= 0 || area.h <= 0) return;
    // The clamp is where the affordance lives: an anchor pushed to (or past) a
    // song edge pins to the edge column, so the stem draws exactly there.
    // The line is waveform_line_px() wide (render.h), its left edge on the
    // column, clipped at the right edge (fill_waveform_line), so an anchor
    // pinned to the last column paints that one column.
    if (col < 0)          col = 0;
    if (col >= area.w)    col = area.w - 1;

    cairo_save(cr);
    // THE ANCHOR STEM IS THE PLAYHEAD'S STEM (architect 2026-08-01, at the
    // row-6 live look; the `playhead_stem` role since 2026-10-03), superseding
    // the dim tunable grey #686a6c this drew in. The affordance is
    // deliberately no longer "less loud than a marker stem": it is a position
    // line during a gesture, and the product's position lines are this one
    // colour.
    // The stems' band, as every stem (waveform_stem_band, architect
    // 2026-10-02): through the well's top lines to the canvas's foot.
    const GuiRect band = waveform_stem_band(area);
    set_palette_source(cr, palette().playhead_stem);
    fill_waveform_line(cr, area.x, area.w, col, band.y, band.y + band.h);
    cairo_restore(cr);
}

// -- Trim bound geometry owners -------------------------------------------
// One column formula, one mapping helper, one handle rect, one bridge-gap owner.
// See render.h for the full rationale (the UNIFIED displayed basis — the
// painter decides against the committed viewport and the hit sites read what
// it publishes, the event-sync ruling; the quantized-span denominator; the
// EOF-wall clamp; and the clip that slides an end off the edge).

TrimBoundColumn trim_bound_column(double displayed_ms,
                                  long long vp_start, long long vp_end,
                                  int wave_w) {
    TrimBoundColumn out;
    out.ms = displayed_ms;
    // The unrounded verdict: a bound in [vp_start, vp_end) is in view.
    out.in_viewport = displayed_ms >= static_cast<double>(vp_start) &&
                      displayed_ms <  static_cast<double>(vp_end);
    // The painters' quantized-span denominator: (vp_end - vp_start)/wave_w,
    // where vp_end itself was derived as vp_start + wave_w·q
    // (viewport_end_sample), so this is q exactly.
    const double span = static_cast<double>(vp_end - vp_start);
    const double samples_per_pixel = span / static_cast<double>(wave_w);
    // The one rounding, on the caller's UNIFIED displayed basis (this file's
    // header block above): displayed_column_at, warp_frame_map_view.h.
    out.col = displayed_column_at(displayed_ms,
                                  static_cast<double>(vp_start),
                                  samples_per_pixel);
    // THE EOF-WALL CLAMP, an IN-VIEW bound's alone (render.h): its rounding
    // can reach grid point wave_w, and the clamp keeps its button whole. An
    // offscreen bound keeps its own column, so its button stands where the
    // bound is and the lane's clip cuts it (architect 2026-10-05).
    if (out.in_viewport && wave_w > 0)
        out.col = std::clamp(out.col, 0, wave_w - 1);
    return out;
}

TrimBridgeGap trim_bridge_gap(const TrimBoundColumn& begin,
                              const TrimBoundColumn& end, int endcap_w) {
    // Contract at the declaration: the gap runs between the two buttons'
    // inner edges, wherever the buttons stand — on screen, part on it, or
    // past either edge (architect 2026-10-05).
    TrimBridgeGap g;
    g.lo = begin.col + endcap_w;
    g.hi = end.col - endcap_w + 1;
    return g;
}

double displayed_trim_ms(int64_t frame,
                         const std::vector<WarpFrameMapSegment>* map) {
    double ms = static_cast<double>(frame);
    if (map && !map->empty()) {
        const double q = static_cast<double>(frame < 0 ? 0 : frame);
        ms = std::nearbyint(map_source_to_target(q, *map));
    }
    return ms;
}

GuiRect trim_endcap_rect(bool is_begin, int strip_x,
                         const TrimBoundColumn& begin,
                         const TrimBoundColumn& end, GuiRect row) {
    const int btn_w = view_bar_grab_w_px();
    GuiRect r;
    // Begin left-edge-anchored (rect left ON the begin column); end
    // right-edge-anchored (rightmost pixel ON the end column), so each bound's
    // zone stands on the span's own end — UNLESS THE DRAWN WIDTH IS UNDER TWO
    // ZONES (architect 2026-10-03): then the begin keeps its column and the
    // end zone stands edge to edge right of it, overrunning its column by
    // 2 x btn_w − span. THE RULE READS THE COLUMNS WHEREVER THEY ARE, on
    // screen or off (architect 2026-10-05): a narrow window panned past an
    // edge keeps its two zones edge to edge and slides off whole. The span is
    // 64-bit: two far offscreen columns on opposite sides can pass an int's
    // range. The rect is the ZONE over the trim lane `row`'s whole height —
    // the hit band, before the lane's clip.
    const int64_t span = static_cast<int64_t>(end.col) - begin.col + 1;
    const bool narrow = span < 2 * static_cast<int64_t>(btn_w);
    if (is_begin)
        r.x = strip_x + begin.col;
    else
        r.x = narrow ? strip_x + begin.col + btn_w
                     : strip_x + end.col - btn_w + 1;
    r.y = row.y;
    r.w = btn_w;
    r.h = row.h;
    return r;
}

namespace {

// THE SCROLL ARROW'S DIRECTION: the popup lists' up and down
// (paint_popup_scroll_bar, below; the trim caps' left and right went with
// the view bar, 2026-10-09).
enum class ScrollArrowDir { Up, Down };

// ONE ARROW BUTTON (the glyph's rule at kTrimArrowGlyphWPx, render.h) on a
// popup list's scroll bar: the ground under the plain raised edge, then the
// scroll arrow as ONE FILLED TRIANGLE in the theme's LABEL (a chrome glyph
// on a chrome face, architect 2026-10-03; a triangle since 2026-10-06),
// antialiased, centred in device px (an odd difference flooring toward the
// top-left), pointing UP or DOWN (2026-10-08, the glyph's 4 x 7 turned: 7
// wide and 4 tall). PRESSED (a popup arrow's press until its lift): the
// ground under one Shadow ring instead of the raised edge, the glyph one
// Windows px right and down.
void paint_trim_arrow_button(cairo_t* cr, const GuiRect& b, ScrollArrowDir dir,
                             bool pressed) {
    paint_cell_rect(cr, b, palette().ground);
    if (pressed)
        paint_relief_line_frame(cr, b, palette().shadow);
    else
        paint_relief_plain_raised(cr, b);
    const int u = scaled_px(1, 1);
    const int glyph_w = kTrimArrowGlyphHPx * u;
    const int glyph_h = kTrimArrowGlyphWPx * u;
    const int push = pressed ? relief_line_px() : 0;
    const double gx = b.x + (b.w - glyph_w) / 2 + push;
    const double gy = b.y + (b.h - glyph_h) / 2 + push;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_new_path(cr);
    switch (dir) {
        case ScrollArrowDir::Up:
            cairo_move_to(cr, gx + glyph_w / 2.0, gy);
            cairo_line_to(cr, gx, gy + glyph_h);
            cairo_line_to(cr, gx + glyph_w, gy + glyph_h);
            break;
        case ScrollArrowDir::Down:
            cairo_move_to(cr, gx + glyph_w / 2.0, gy + glyph_h);
            cairo_line_to(cr, gx, gy);
            cairo_line_to(cr, gx + glyph_w, gy);
            break;
    }
    cairo_close_path(cr);
    set_palette_source(cr, palette().label);
    cairo_fill(cr);
    cairo_restore(cr);
}

} // namespace

void render_trim_flags(cairo_t* cr,
                       GuiRect top_strip_area,
                       GuiRect trim_bar,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim,
                       int playhead_col,
                       TrimBarHit* out_hit) {
    // COLD FIRST, so every early return below publishes "nothing grabbable"
    // over a lane that painted no bar (the contract at the declaration).
    if (out_hit) *out_hit = TrimBarHit{};
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (trim_bar.w <= 0 || trim_bar.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (waveform_area.w <= 0) return;

    // Both bounds resolve through the ONE shared column owner: .col is the
    // bound's column, an offscreen one past the lane's edge. The bar spans
    // between them wherever they stand, so the columns are computed
    // unconditionally.
    const TrimBoundColumn bc = trim_bound_column(
        static_cast<double>(trim.begin), viewport_start_sample,
        viewport_end_sample, waveform_area.w);
    const TrimBoundColumn ec = trim_bound_column(
        static_cast<double>(trim.end), viewport_start_sample,
        viewport_end_sample, waveform_area.w);

    const int lane_x   = trim_bar.x;
    const int lane_w   = waveform_area.w;   // the effective width
    const int lane_y   = trim_bar.y;
    const int lane_h   = trim_bar.h;

    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_rectangle(cr, lane_x, lane_y, lane_w, lane_h);
    cairo_clip(cr);

    // THE BAR (METRICS §3): the mid line along its top, the black field,
    // the hilight line along its foot — the ruler's top line, the lane below
    // standing straight on it. Horizontal lines only: the well under the
    // column has no side frame, and neither has the bar (the bar spans the
    // waveform's columns edge to edge, as the well does).
    const GuiPalette& pal = palette();
    const int lw = program_line_px();
    const GuiRect lane{lane_x, lane_y, lane_w, lane_h};
    const int field_y = lane_y + lw;
    const int field_h = lane_h - 2 * lw;
    paint_cell_rect(cr, GuiRect{lane_x, lane_y, lane_w, lw}, pal.ce_mid);
    paint_cell_rect(cr, GuiRect{lane_x, field_y, lane_w, field_h},
                    hex(kCeViewBarField));
    paint_cell_rect(cr, GuiRect{lane_x, lane_y + lane_h - lw, lane_w, lw},
                    pal.ce_hilight);

    // THE SPAN — the trim window's columns, the begin column through the end
    // column inclusive, sliding off an edge like any content (the clip cuts
    // it; a side lying farther off than one quantum is held there, outside
    // the clip, so the rect stays in cairo's range at any zoom): the ink's
    // body, its top row and left column the light bevel, its bottom row and
    // right column the dark one (the mock's order: the bottom row over the
    // left column's foot, the right column under the top row's end).
    const int span_lo = std::max(bc.col, -lw);
    const int span_hi = std::min(ec.col + 1, lane_w + lw);
    if (span_hi > span_lo && field_h > 0) {
        const int sx = lane_x + span_lo;
        const int sw = span_hi - span_lo;
        paint_cell_rect(cr, GuiRect{sx, field_y, sw, field_h},
                        pal.waveform_ink);
        paint_cell_rect(cr, GuiRect{sx, field_y, sw, lw}, pal.ce_span_hilight);
        paint_cell_rect(cr, GuiRect{sx, field_y, lw, field_h},
                        pal.ce_span_hilight);
        paint_cell_rect(cr, GuiRect{sx, field_y + field_h - lw, sw, lw},
                        pal.ce_span_shadow);
        paint_cell_rect(cr, GuiRect{sx + sw - lw, field_y + lw, lw,
                                    field_h - lw},
                        pal.ce_span_shadow);
        // THE PLAYHEAD OVER THE SPAN (METRICS §3): a one-quantum dotted
        // column on the playhead's column, FFFF00 and black alternating per
        // quantum row of the field, the yellow on the odd rows (Cool Edit's
        // 86, 88, … under its span's top row 85), and only where the column
        // stands on the span.
        if (playhead_col >= span_lo && playhead_col < span_hi &&
            playhead_col >= 0 && playhead_col < lane_w) {
            const int px = lane_x + playhead_col;
            const int dw = std::min(lw, lane_x + lane_w - px);
            for (int k = 0; field_y + k * lw < field_y + field_h; ++k) {
                const int y = field_y + k * lw;
                const int h = std::min(lw, field_y + field_h - y);
                paint_cell_rect(cr, GuiRect{px, y, dw, h},
                                hex(k % 2 == 1 ? kCePlayhead
                                               : kCeViewBarDot));
            }
        }
    }

    // THE GRABS, as painted: the span's two end zones and the body between
    // them (render.h's view_bar_grab_w_px, trim_endcap_rect,
    // trim_bridge_gap) — each zone cut to the lane's painted width, the clip
    // the pixels take, so a zone overrunning the lane claims its visible
    // columns alone; a window wholly off one side publishes no zone on that
    // side and an empty bridge.
    const int btn_w = view_bar_grab_w_px();
    const GuiRect begin_r = trim_endcap_rect(true, lane_x, bc, ec, trim_bar);
    const GuiRect end_r = trim_endcap_rect(false, lane_x, bc, ec, trim_bar);
    const TrimBridgeGap gap = trim_bridge_gap(bc, ec, btn_w);
    const auto lane_cut = [&](GuiRect r) {
        const int lo = std::max(r.x, lane_x);
        const int hi = std::min(r.x + r.w, lane_x + lane_w);
        r.x = lo;
        r.w = std::max(0, hi - lo);
        return r;
    };
    if (out_hit) {
        if (lane_cut(begin_r).w > 0) out_hit->begin = {true, lane_cut(begin_r)};
        if (lane_cut(end_r).w > 0)   out_hit->end   = {true, lane_cut(end_r)};
        out_hit->published = true;
        out_hit->lane      = lane;
        out_hit->bridge_lo = lane_x + std::max(gap.lo, 0);
        out_hit->bridge_hi = lane_x + std::min(gap.hi, lane_w);
    }

    cairo_restore(cr);
}

// THE POPUP LIST'S SCROLL BAR (the rule and the geometry at render.h's popup
// scroll block; the picture at the declaration): under win2000 the trim
// lane's vocabulary turned upright — the track's checker, the plain raised
// thumb and the two plain raised arrow buttons (paint_trim_arrow_button, up
// and down, pressed while `held` names one); under clearlooks squeeze's
// gummy GtkScrollbar (clearlooks_paint.h's vertical block, 2026-10-08): the
// classic trough 2 W short of each end (trough-under-steppers), the gummy
// slider — one W longer at an end it touches, the engine's junction, under
// the stepper drawn after it — then the two gummy steppers. Painted over the
// list box's ground, after its frame, beside its rows.
void paint_popup_scroll_bar(cairo_t* cr, const PopupScrollBar& b,
                            PopupScrollPart held) {
    if (!b.present || b.bar.w <= 0 || b.bar.h <= 0) return;
    const bool up_held   = held == PopupScrollPart::Up;
    const bool down_held = held == PopupScrollPart::Down;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    if (live_chrome_spec().vocabulary == GuiChromeVocabulary::Clearlooks) {
        const int u = relief_line_px();
        const int shrink = scaled_px(2);
        paint_cl_scroll_trough_v(cr, GuiRect{b.bar.x, b.bar.y + shrink, b.bar.w,
                                             b.bar.h - 2 * shrink});
        if (b.thumb.h > 0) {
            GuiRect slider = b.thumb;
            if (slider.y == b.track.y) { slider.y -= u; slider.h += u; }
            if (b.thumb.y + b.thumb.h == b.track.y + b.track.h) slider.h += u;
            paint_cl_scrollbar_slider(cr, slider);
        }
        paint_cl_scrollbar_stepper(cr, b.up, /*points_up=*/true, up_held);
        paint_cl_scrollbar_stepper(cr, b.down, /*points_up=*/false, down_held);
    } else if (live_chrome_spec().vocabulary == GuiChromeVocabulary::Cde) {
        // MOTIF'S VERTICAL XmScrollBar (2026-10-08; the Open dialog
        // capture's list bar, x 400-412): the trim lane's trough, slider and
        // arrows turned upright, on the bar's rects.
        paint_cde_trough(cr, b.bar);
        if (b.thumb.h > 0) paint_cde_slider(cr, b.thumb, /*horizontal=*/false);
        paint_cde_arrow(cr, b.up, CdeArrowDir::Up, up_held);
        paint_cde_arrow(cr, b.down, CdeArrowDir::Down, down_held);
    } else {
        paint_cell_rect(cr, b.track, palette().ground);
        paint_checker_rect(cr, b.track, b.bar.x, b.bar.y, palette().hilight,
                           palette().ground);
        if (b.thumb.h > 0) {
            paint_cell_rect(cr, b.thumb, palette().ground);
            paint_relief_plain_raised(cr, b.thumb);
        }
        paint_trim_arrow_button(cr, b.up, ScrollArrowDir::Up, up_held);
        paint_trim_arrow_button(cr, b.down, ScrollArrowDir::Down, down_held);
    }
    cairo_restore(cr);
}

namespace {

// THE WAVEFORM'S COLUMNS ARE THE MARKER LANE'S CLIP (architect 2026-09-26):
// every flag box, every flag-editor box and every hit rect either publishes
// is confined to window x in [x0, x0 + w) — the columns the waveform paints —
// so a non-multiple-of-16 window's leftover strip beside w carries nothing of
// a flag, and a flag running past the last column is cut off at it exactly as
// at a multiple-of-16 window's own edge. These two are the one spelling of
// that clip: the paint clip each painter sets before it draws (vertically the
// band it paints in, unchanged) and the hit rect's matching trim, so the claim
// is the painted rect clipped the same way — strictly as painted. A rect the
// clip empties publishes nothing (the callers test the answer).
void clip_to_waveform_columns(cairo_t* cr, int x0, int w, int band_y,
                              int band_h) {
    cairo_rectangle(cr, static_cast<double>(x0), static_cast<double>(band_y),
                    static_cast<double>(w > 0 ? w : 0),
                    static_cast<double>(band_h));
    cairo_clip(cr);
}
// Trims r's horizontal extent to [x0, x0 + w); true iff anything is left. The
// two cell boundaries are left where the painter put them: a boundary past
// the clip simply has no visible pixel beside it, and one left of it reads as
// the box the visible part belongs to, which is what the walk
// (hit_test_flag_cell) needs.
bool clip_hit_rect_to_waveform_columns(FlagHitRect& r, int x0, int w) {
    const double lo = std::max(r.x, static_cast<double>(x0));
    const double hi = std::min(r.x + r.w, static_cast<double>(x0 + w));
    if (hi <= lo) return false;
    r.x = lo;
    r.w = hi - lo;
    return true;
}
// THE STEM STASH IS GATED TO THE WAVEFORM'S COLUMNS [0, w), both edges: the
// flag iterator admits a marker whose flag reaches into [0, w) from either
// side — one left of column 0 whose right-running box hangs into view, one at
// grid point w for its border alone — but only a marker whose own column is a
// real waveform column publishes a stem. The stem painter and the playhead's
// suppression decider (playhead_stem_suppressed) read only real columns, so an
// off-surface entry can neither paint nor hide a coincident playhead's stem.
// `col` is the marker's column relative to x0. The one spelling for both lane
// producers (render_flag_boxes_impl, render_history_diff_flags).
bool stem_column_on_waveform(int col, int w) {
    return col >= 0 && col < w;
}

// Shared flag iteration used by render_flags, its phase-reset analogue and
// the `h` view's diff lane. Invokes `emit(i, left_x)` for EVERY visible
// marker IN STORE ORDER; the cue painter collects them and orders them by
// column itself (the overlap rule paints the labels right to left and the
// triangles left to right, render.h's marker-lane paragraph).
//
// THE PAINT/HIT INVARIANT. `left_x` — the marker's painted pixel column — is
// computed ONCE here: the triangle's apex column, the label's anchor and the
// dots' column. The painter draws from it, the hit rects are published from
// it, and the stem is published at it, so all three are one number by
// construction.
template <typename MarkerVec, typename Emit>
void iterate_visible_flags_impl(
    GuiRect top_strip_area,
    int waveform_width,
    const MarkerVec& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    const std::vector<WarpFrameMapSegment>* warp_frame_map,
    const DragOverlay* drag_overlay,
    // The LEFT cull's width bound in pixels — how far left of the viewport a box
    // may open and still reach into it. A caller-supplied number rather than a
    // derivation here because the callers do not share one width family: the
    // marker columns pass marker_flag_max_width_px(iteration_on), a constant
    // bound the display composers' own grammars guarantee, while the history mode's
    // diff lane does not truncate at all and derives its bound from the commit's
    // own longest label. A bound over-admits a handful of offscreen items per
    // frame and never drops a visible one, so the only requirement is that it
    // not UNDER-state.
    double cull_width_px,
    Emit&& emit) {
    const double span = static_cast<double>(viewport_end_sample -
                                            viewport_start_sample);
    // Map columns against the EFFECTIVE waveform width, not the strip's own
    // full width, so a flag shares the marker stem's samples-per-pixel and
    // stays column-aligned with it at every window width (they diverge only
    // when the two widths differ — a non-multiple-of-16 window; at
    // 1920/2560/3840 they are equal and this is a no-op).
    const double samples_per_pixel =
        span / static_cast<double>(waveform_width);
    if (samples_per_pixel <= 0.0) return;

    // THE CULL IS ASYMMETRIC BECAUSE THE CUE IS. A cue's label runs
    // RIGHTWARD from its column, so a marker to the LEFT of the viewport can
    // still reach into it (by up to a full label width) while a marker right
    // of the last column can reach in by its TRIANGLE'S LEFT HALF alone. The
    // left margin is a width BOUND rather than the real width, which is not
    // known until the label is shaped; the caller supplies it (see
    // cull_width_px above).
    //
    // THE RIGHT CULL IS THE PAINTED EXTENT (architect 2026-09-26): a cue is
    // admitted iff its triangle intersects the waveform's columns [0, w) —
    // col - reach < w, the triangle's widest row reaching four quanta left of
    // its column (cue_triangle_half_w_px, render.h). So a marker just past
    // the last column (the song's last half-column at the right wall, or a
    // marker just past the viewport's end mid-song) is admitted and paints
    // its triangle's left part ALONE on the last columns — every caller clips
    // its paint and its hit rect to [0, w), so nothing paints in the leftover
    // strip a non-multiple-of-16 window leaves beside w. Its DOTS are not
    // published (the callers gate the stem stash to [0, w) at both edges,
    // stem_column_on_waveform), and source_frame_off_right_edge
    // (warp_frame_map_view.h) still refuses AUTHORING there — a triangle's
    // half is not the marker's column. The column is displayed_column_at's —
    // the refusal's own rounding over the same q — so the painter and the
    // refusal cannot disagree on one basis. The sample compare below is only
    // a PREFILTER (it keeps the int cast bounded), set one column wider than
    // the triangle's reach so it never drops a marker the column test would
    // admit.
    const int reach = cue_triangle_half_w_px();
    const double cull_lo = static_cast<double>(viewport_start_sample) -
                           cull_width_px * samples_per_pixel;
    const double cull_hi =
        static_cast<double>(viewport_end_sample) +
        static_cast<double>(reach + 1) * samples_per_pixel;
    for (size_t i = 0; i < markers.size(); ++i) {
        const auto& m = markers[i];
        const double eff_time = drag_overlay
            ? drag_overlay->effective_time(
                  static_cast<int>(i), m.time_frame)
            : m.time_frame;
        const double ms =
            frame_to_paint_sample(eff_time, warp_frame_map);
        if (ms < cull_lo) continue;
        if (ms >= cull_hi) continue;   // prefilter — see the cull note above

        const int col = displayed_column_at(
            ms, static_cast<double>(viewport_start_sample), samples_per_pixel);
        // The cull — the note above: the triangle's first column is
        // col - reach.
        if (col - reach >= waveform_width) continue;
        const double left_x =
            static_cast<double>(top_strip_area.x) + static_cast<double>(col);

        emit(static_cast<int>(i), left_x);
    }
}

// (THE SHARED LABEL CAP IS GONE, 2026-09-19. cap_marker_label kept the first
// nine bytes of whatever a column composed and appended the truncation
// marker; the WARP payload was its only real subject — the phase-reset token
// is five bytes — and once a tempo could
// carry a chain, cutting by a byte count would have eaten the very terms the
// feature exists to show. The cut now lives inside the one composer that
// knows what it is cutting, flag_display_text above, and the phase-reset
// column's label reaches the pass whole.)

// The two bound cells an eligible flag paints while iteration mode is on, or
// nothing. The lambda form each column hands render_flag_boxes_impl answers
// this — the warp column through warp_iter_cells and the phase-reset column
// through phase_iter_cells (2026-09-09, when the mode grew its second
// column) — and the two differ in their COMPOSER alone, cents against hops.
struct IterCellText {
    bool        present = false;
    // IS THIS MARKER A TIE FOLLOWER (architect 2026-09-19) — one of several
    // markers the sweep treats as ONE AXIS, and not the tie's leader. Its two
    // cells show THE LEADER'S bracket (the strings below are composed off the
    // governing marker, so tied members cannot disagree) and paint GREYED,
    // wearing the palette's DISABLED blend because the cell is not this
    // marker's to author. It is an ordinary content input like the strings and
    // reaches the painter the same way; the flag cache needs no field for it,
    // the tie living in the store whose GENERATION the fingerprint already
    // carries.
    bool        follower = false;
    std::string lower;
    std::string upper;
};

// The cells of warp marker `i` under `iteration_on` — the ONE spelling of
// "which flags paint cells" (iter_popup_eligible_marker, the sweep's own
// eligibility) and of what they say (format_iter_bound_cell), shared by the
// flag pass and by the bound editor's anchor (committed_cell_seam_off), so the
// resting box and the field that opens past it cannot disagree about where
// the cells end.
//
// `iteration_on && iter_popup_eligible_marker` IS marker_paints_iter_cells
// (app_state.h) spelled across this painter's parameter boundary: the Tab
// walk asks that predicate whether a marker has bound cells to stop on, and
// the two readings compose the same two owners — the caller's one
// iteration_column_lit read (waveform_cache.cpp, over the column this pass
// paints) and the eligibility predicate below. This body cannot call it
// because the flag painter takes no AppState at all, every content input
// arriving as an explicit argument that the flag cache fingerprints.
static IterCellText warp_iter_cells(const std::vector<GuiWarpMarker>& markers,
                                    int i, bool iteration_on) {
    IterCellText c;
    if (!iteration_on || !iter_popup_eligible_marker(markers, i)) return c;
    // THE BRACKET IS THE GOVERNING MARKER'S (iter_bracket_governor,
    // warpmarkers.h): its own for an untied marker or a tie's leader, THE
    // LEADER'S for a follower — one owner for the walk, so the cells and
    // every act that reads a bound name the same bracket.
    const GuiWarpMarker& m = iter_bracket_governor(markers, i);
    c.present  = true;
    c.follower = marker_is_tie_follower(markers, i);
    c.lower    = format_iter_bound_cell(m, MarkerCell::Lower);
    c.upper    = format_iter_bound_cell(m, MarkerCell::Upper);
    return c;
}

// The cells of phase reset `i` under `iteration_on` — warp_iter_cells' twin
// (2026-09-09), off this column's own eligibility
// (phase_reset_iter_eligible_marker, phaseresetmarkers.h: every reset is a
// carrier, so the verdict is the disabled bit) and its own composer
// (format_phase_iter_bound_cell — the signed integer hop, `+0` for a blank
// bracket). Shared by the flag pass and the bound field's anchor
// (committed_cell_seam_off)
// for the same reason its twin is: the resting boxes and the field that opens
// past them cannot disagree about where the cells end. Its composition is
// marker_paints_iter_cells' on this column, and the note at its twin above
// covers why the painter spells it rather than calling it.
static IterCellText phase_iter_cells(
    const std::vector<GuiPhaseResetMarker>& phase_resets, int i,
    bool iteration_on) {
    IterCellText c;
    if (!iteration_on || !phase_reset_iter_eligible_marker(phase_resets, i))
        return c;
    // The governing reset's hops, the warp body's own rule on this column
    // (phase_iter_bracket_governor, phaseresetmarkers.h).
    const GuiPhaseResetMarker& m = phase_iter_bracket_governor(phase_resets, i);
    c.present  = true;
    c.follower = marker_is_tie_follower(phase_resets, i);
    c.lower    = format_phase_iter_bound_cell(m, MarkerCell::Lower);
    c.upper    = format_phase_iter_bound_cell(m, MarkerCell::Upper);
    return c;
}

// The two cells LAID OUT on `font`: each one's shaped run and its text's
// whole-column width, or nothing where no cells paint. THE ONE MEASURER. The
// flag pass, the bound field's anchor (committed_cell_seam_off) and the
// editor's RE-PAINT of whatever rides its unrolled right edge all lay them
// out through this one body, off the one composer and the one font, so no
// two of them can disagree about where a cell begins or how wide it is.
struct IterCellLayout {
    bool                  present = false;
    text_shape::ShapedRun lower_run;
    text_shape::ShapedRun upper_run;
    int                   lower_w = 0;   // the token's run, whole columns
    int                   upper_w = 0;
};

static IterCellLayout measure_iter_cells(const GuiFont& font,
                                         const IterCellText& cells) {
    IterCellLayout l;
    if (!cells.present) return l;
    l.present   = true;
    l.lower_run = text_shape::shape_text_run(font, cells.lower);
    l.upper_run = text_shape::shape_text_run(font, cells.upper);
    l.lower_w   = static_cast<int>(std::nearbyint(l.lower_run.width_px));
    l.upper_w   = static_cast<int>(std::nearbyint(l.upper_run.width_px));
    return l;
}

// THE LABEL'S SEGMENT BOXES (render.h's marker-lane paragraph;
// program_spec.h's cue lengths), as offsets from the marker's column: the
// first present segment's box opens the fill lead past the column, each box
// is its text's whole-column width with a pad quantum either side (the text
// at its box's start plus the pad — the label's lead), and each next box
// stands the segment gap past the one before. `w` the three texts' widths,
// `present` which stand. THE ONE LAYOUT the flag pass, the history lane and
// the editor read.
struct CueSegmentBoxes {
    int x0[3] = {0, 0, 0};
    int x1[3] = {0, 0, 0};
};
static CueSegmentBoxes cue_segment_boxes(const int (&w)[3],
                                         const bool (&present)[3]) {
    const int u = cue_unit_px();
    CueSegmentBoxes b;
    int x = kProgramSpec.cue_fill_lead * u;
    for (int s = 0; s < 3; ++s) {
        b.x0[s] = x;
        b.x1[s] = x;
        if (!present[s]) continue;
        b.x1[s] = x + 2 * kProgramSpec.cue_fill_pad * u + w[s];
        x = b.x1[s] + kProgramSpec.cue_segment_gap * u;
    }
    return b;
}

// THE FLAG'S KIND (architect 2026-10-04, reopening 2026-10-03's one flag
// colour for every kind): which of the theme's four flag pairs a box wears —
// WARP and PHASE RESET, co-equal, the authoring columns' flags by their
// column, and the `h` view's ADDED and REMOVED diff halves. The palette
// block's marker-lane paragraph (render.h) owns the look.
enum class GuiFlagKind { Warp, PhaseReset, Added, Removed };

// The resolved paint of ONE cue's triangle and dots (the palette block's
// marker-lane paragraph, render.h, owns the look): the face and whether the
// dots paint. The label's look is the segment's own (selected, embossed or
// the panel's light tone), never the triangle's.
struct FlagFace {
    GuiColor face;
    bool     has_stem;
};

// A kind's resting face, off the palette's roles (the selected faces are
// read by no cue since 2026-10-09, the triangle never changing when
// selected).
GuiColor flag_kind_face(GuiFlagKind kind) {
    const GuiPalette& p = palette();
    switch (kind) {
        case GuiFlagKind::Warp:       return p.warp_flag;
        case GuiFlagKind::PhaseReset: return p.phase_reset_flag;
        case GuiFlagKind::Added:      return p.added_flag;
        case GuiFlagKind::Removed:    return p.removed_flag;
    }
    return p.warp_flag;
}

// THE ONE LADDER for every cue — both marker columns and the `h` view's diff
// flags (architect 2026-10-03; the kinds 2026-10-04; Cool Edit's cue
// 2026-10-09): DISABLED wins (the theme's ground on the triangle, no dots),
// then INVALID, which WEARS THE REMOVED FACE (architect 2026-10-04, and again
// 2026-10-07: one red for both, the context telling them apart — invalid
// while authoring, removed in `h`), then the KIND's own face, the dots in
// it. Selected or not alike: Cool Edit's cue never changes when selected
// (architect 2026-10-09; the label carries the selection).
FlagFace resolve_flag_face(GuiFlagKind kind, bool disabled, bool red) {
    if (disabled) return FlagFace{palette().ground, false};
    return FlagFace{flag_kind_face(red ? GuiFlagKind::Removed : kind), true};
}

// ONE SEGMENT OF A CUE'S LABEL, as the pass paints it: its shaped run, its
// face box [x0, x1) in window x (the text at x0 plus the pad quantum), and
// its look — the chrome's selected pair, the disabled emboss, or the panel's
// light tone.
struct CueSegment {
    bool                  present  = false;
    text_shape::ShapedRun run;
    int                   x0       = 0;
    int                   x1       = 0;
    bool                  selected = false;
    bool                  embossed = false;
};
// ONE CUE, laid out: its marker (or diff flag) index, its column in window
// x, the triangle's resolved face and whether its dots paint, and its label's
// up-to-three segments in their left-to-right order. `join_selected` lights
// the selected segments as ONE box over the gap between them (the history's
// changed pair, one item).
struct CueDraw {
    int                       index = -1;
    int                       col   = 0;
    FlagFace                  face{};
    std::array<CueSegment, 3> seg{};
    bool                      join_selected = false;
    // Whether segments 1 and 2 are the marker's bound cells (the live lanes)
    // or plain halves of one item (the history's pair, whose label publishes
    // no cell boundary: no bracket lives in a commit).
    bool                      segments_are_cells = true;
};

// THE CUES' ONE PAINTER (render.h's marker-lane paragraph: the overlap rule,
// the states; the lengths program_spec.h's), shared by both marker columns
// and the `h` view's lane: the labels RIGHT TO LEFT, each on its opaque face
// box and cut at the next triangle's left edge where the next column stands
// past the overlap lead; then the triangles LEFT TO RIGHT; then the
// publication — one FlagHitRect per cue (its label box as painted and its
// triangle, both clipped to the waveform's columns), and one MarkerStem per
// cue whose dots paint and whose column is a waveform column. `lane` is the
// marker lane, `x0` / `w` the waveform's columns the pass is clipped to (the
// caller set the clip). Cues arrive in store order and are ordered here by
// column (stable).
static void paint_cues(cairo_t* cr, const GuiRect& lane, int x0, int w,
                       std::vector<CueDraw>& cues,
                       std::vector<FlagHitRect>* out_hit_rects,
                       std::vector<MarkerStem>* out_stems) {
    std::stable_sort(cues.begin(), cues.end(),
                     [](const CueDraw& a, const CueDraw& b) {
                         return a.col < b.col;
                     });
    const int u        = cue_unit_px();
    const int half     = cue_triangle_half_w_px();
    const int fill_h   = cue_fill_h_px();
    const int tri_top  = lane.y + cue_above_triangle_h_px();
    const int tri_h    = cue_triangle_h_px();
    const double base  = static_cast<double>(lane.y + cue_baseline_px());
    const GuiPalette& pal = palette();
    const int n = static_cast<int>(cues.size());
    // Each label's painted extent [lo, hi): its first segment's box start to
    // its last's end, cut at the next triangle's left edge where the next
    // column stands past the lead; empty where no segment paints.
    std::vector<int> lo(static_cast<std::size_t>(n), 0);
    std::vector<int> hi(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        const CueDraw& c = cues[static_cast<std::size_t>(i)];
        int a = 0, b = 0;
        bool any = false;
        for (const CueSegment& s : c.seg) {
            if (!s.present) continue;
            if (!any) a = s.x0;
            b = s.x1;
            any = true;
        }
        if (any && i + 1 < n) {
            const int next = cues[static_cast<std::size_t>(i + 1)].col;
            if (next - c.col > kProgramSpec.cue_overlap_lead * u)
                b = std::min(b, next - half);
        }
        lo[static_cast<std::size_t>(i)] = a;
        hi[static_cast<std::size_t>(i)] = any ? std::max(a, b) : a;
    }

    // THE LABELS, RIGHT TO LEFT.
    for (int i = n - 1; i >= 0; --i) {
        const CueDraw& c = cues[static_cast<std::size_t>(i)];
        const int a = lo[static_cast<std::size_t>(i)];
        const int b = hi[static_cast<std::size_t>(i)];
        if (b <= a) continue;
        cairo_save(cr);
        cairo_rectangle(cr, a, lane.y, b - a, fill_h);
        cairo_clip(cr);
        paint_cell_rect(cr, GuiRect{a, lane.y, b - a, fill_h}, pal.face);
        if (c.join_selected) {
            int sa = 0, sb = 0;
            bool any = false;
            for (const CueSegment& s : c.seg) {
                if (!s.present || !s.selected) continue;
                if (!any) sa = s.x0;
                sb = s.x1;
                any = true;
            }
            if (any)
                paint_cell_rect(cr, GuiRect{sa, lane.y, sb - sa, fill_h},
                                pal.selected_fill);
        }
        for (const CueSegment& s : c.seg) {
            if (!s.present) continue;
            if (s.selected && !c.join_selected)
                paint_cell_rect(cr, GuiRect{s.x0, lane.y, s.x1 - s.x0, fill_h},
                                pal.selected_fill);
            const double tx = static_cast<double>(
                s.x0 + kProgramSpec.cue_fill_pad * u);
            if (s.embossed && !s.selected) {
                show_embossed_run(cr, s.run, tx, base);
            } else {
                set_palette_source(cr, s.selected ? pal.selected_text
                                                  : pal.ce_hilight);
                text_shape::show_shaped_run(cr, s.run, tx, base);
            }
        }
        cairo_restore(cr);
    }

    // THE TRIANGLES, LEFT TO RIGHT, over every label.
    for (const CueDraw& c : cues)
        paint_ce_cue_triangle(cr, c.col, tri_top, c.face.face);

    // THE PUBLICATION, left to right.
    for (int i = 0; i < n; ++i) {
        const CueDraw& c = cues[static_cast<std::size_t>(i)];
        const int a = lo[static_cast<std::size_t>(i)];
        const int b = hi[static_cast<std::size_t>(i)];
        if (out_hit_rects) {
            FlagHitRect r;
            r.marker_index = c.index;
            // The label's box as painted; each boundary its segment's box
            // start, collapsing onto the box's right edge where the segment
            // did not paint.
            FlagHitRect label;
            label.x = static_cast<double>(a);
            label.y = static_cast<double>(lane.y);
            label.w = static_cast<double>(std::max(0, b - a));
            label.h = static_cast<double>(fill_h);
            const bool label_on =
                b > a && clip_hit_rect_to_waveform_columns(label, x0, w);
            r.x = label_on ? label.x : 0.0;
            r.y = label_on ? label.y : 0.0;
            r.w = label_on ? label.w : 0.0;
            r.h = label_on ? label.h : 0.0;
            const double right = static_cast<double>(b);
            const bool cells = c.segments_are_cells;
            r.iter_lower_boundary_x = cells && c.seg[1].present
                                          ? static_cast<double>(c.seg[1].x0)
                                          : right;
            r.iter_upper_boundary_x = cells && c.seg[2].present
                                          ? static_cast<double>(c.seg[2].x0)
                                          : right;
            // The triangle's box: its widest row and the shadow's quantum,
            // its five rows.
            FlagHitRect tri;
            tri.x = static_cast<double>(c.col - half);
            tri.y = static_cast<double>(tri_top);
            tri.w = static_cast<double>(2 * half + 2 * u);
            tri.h = static_cast<double>(tri_h);
            if (clip_hit_rect_to_waveform_columns(tri, x0, w)) {
                r.tri_x = tri.x;
                r.tri_y = tri.y;
                r.tri_w = tri.w;
                r.tri_h = tri.h;
            }
            if (label_on || r.tri_w > 0.0) out_hit_rects->push_back(r);
        }
        if (out_stems && c.face.has_stem &&
            stem_column_on_waveform(c.col - x0, w))
            out_stems->push_back(MarkerStem{c.index, static_cast<double>(c.col),
                                            c.face.face});
    }
}

} // namespace

// The phase-reset lead-in ring's color (declaration in render.h): the ladder
// above asked for a LIVE reset's face on the same class bit the flag pass
// hands it, so the ring can never pick a color its dots would not. It
// stands outside the file's anonymous namespace so paint_handler.cpp reaches
// it; the ladder it calls stays file-local.
GuiColor phase_reset_stem_color(bool red) {
    return resolve_flag_face(GuiFlagKind::PhaseReset, /*disabled=*/false, red)
        .face;
}

namespace {

// THE LABEL'S SEGMENTS IN PAINTED ORDER, RANKED: the payload, then the lower
// bound cell, the upper bound cell. That is the one
// left-to-right order this pass lays them in, the editor's riding run re-paints
// in, and FlagHitRect's two boundaries collapse along — and ranking it is
// what lets ONE COMPARISON express the suppression for both editor kinds
// (SuppressedBox, render.h): a segment belongs to this pass iff it stands
// LEFT of the edited one. Past the last rank sits kFlagBoxRankNone, the
// answer for every marker no editor stands on.
static int flag_box_rank(MarkerCell c) {
    switch (c) {
        case MarkerCell::Payload: return 0;
        case MarkerCell::Lower:   return 1;
        case MarkerCell::Upper:   return 2;
    }
    return 0;
}
static constexpr int kFlagBoxRankNone = 3;

// The one body both columns' painters call. `label_of(i)` composes the
// marker's display text and `disabled_of(i)` answers its column's disabled
// question (the warp side's label_ref cascade, the phase-reset side's bare
// bool); the lambdas survive because the
// columns hold different marker types.
template <typename MarkerVec, typename LabelFn, typename DisabledFn,
          typename CellsFn>
void render_flag_boxes_impl(
    cairo_t* cr,
    // The column's flag kind (Warp or PhaseReset), every box of the pass its
    // face pair's (resolve_flag_face).
    GuiFlagKind kind,
    GuiRect top_strip_area,
    FlagLaneRects lanes,
    int waveform_width,
    const MarkerVec& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    int sample_rate,
    const std::set<int>& selected_set,
    const std::set<int>& red_set,
    LabelFn&& label_of,
    DisabledFn&& disabled_of,
    // The two iteration bound cells marker i paints, or none (IterCellText):
    // the warp column answers through warp_iter_cells and the phase-reset
    // column through phase_iter_cells, each off its own eligibility and its
    // own composer.
    CellsFn&& cells_of,
    std::vector<FlagHitRect>* out_hit_rects,
    std::vector<MarkerStem>* out_stems,
    const std::vector<WarpFrameMapSegment>* warp_frame_map,
    const DragOverlay* drag_overlay,
    // THE ONE BOX THIS PASS DOES NOT PAINT (SuppressedBox, render.h): the
    // marker whose marker-lane editor stands, and WHICH of its boxes that
    // editor is standing in for. It replaced two separate indices the
    // day the bound field stopped being the odd one out: the rule is now ONE
    // COMPARISON against flag_box_rank, so the editor kinds are cases of one
    // model rather than separate arms. At most one box is ever
    // suppressed, the marker-lane editors being one text_editor::State.
    SuppressedBox suppressed,
    // Reaches the LEFT CULL only — it widens the width bound by the two bound
    // cells. Which flags paint cells is the cells lambda's business, so this
    // body never asks whether a given marker is eligible.
    bool iteration_on,
    // The focus and its addressed cell (render_flags' declaration): the one
    // selected marker whose bright cell is not the payload, or -1.
    int focus_marker,
    MarkerCell focus_cell) {
    if (out_hit_rects) out_hit_rects->clear();
    if (out_stems)     out_stems->clear();
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;
    if (sample_rate <= 0) return;
    const GuiRect lane = lanes.marker_lane;
    if (lane.h <= 0) return;

    cairo_save(cr);
    // THE PASS PAINTS INSIDE THE WAVEFORM'S COLUMNS (clip_to_waveform_columns
    // above, architect 2026-09-26): a cue running past the last column is
    // cut off there. Released by the pass's closing restore.
    clip_to_waveform_columns(cr, top_strip_area.x, waveform_width,
                             top_strip_area.y, top_strip_area.h);
    // THE PROGRAM FACE for the whole pass (gui_font.h; the cues' cap 7),
    // every run carrying it (text_shape.h).
    const GuiFont font = gui_font(GuiFace::Program);
    std::vector<CueDraw> cues;

    iterate_visible_flags_impl(top_strip_area, waveform_width, markers,
                               viewport_start_sample, viewport_end_sample,
                               warp_frame_map, drag_overlay,
                               // `iteration_on` widens the bound by the two
                               // cells; the reasoning is at the bound.
                               marker_flag_max_width_px(iteration_on),
        [&](int i, double left_x) {
            CueDraw c;
            c.index = i;
            c.col   = static_cast<int>(std::nearbyint(left_x));
            // WHICH OF THIS MARKER'S SEGMENTS THIS PASS PAINTS: everything
            // LEFT of the edited segment stands at rest, and nothing from it
            // rightward — those ride the field's right edge under
            // render_flag_editor_box (the one model, SuppressedBox). A
            // marker no editor stands on paints its whole label.
            const int suppressed_rank =
                i == suppressed.marker_index ? flag_box_rank(suppressed.cell)
                                             : kFlagBoxRankNone;
            const auto pass_paints = [&](MarkerCell cell) {
                return flag_box_rank(cell) < suppressed_rank;
            };
            const IterCellText cells = cells_of(i);
            const bool dis = disabled_of(i);
            const bool red = red_set.count(i) > 0;
            const bool sel = selected_set.count(i) > 0;
            // THE SELECTION IS ONE SEGMENT'S (architect 2026-09-05): the
            // payload for every selected marker but the focus, whose
            // addressed cell is the axis — and where the axis names a cell
            // shown NOWHERE (by this pass or by the open field standing in
            // for it: a bound cell on an owner disabled after its press),
            // the payload takes it, so a selected marker always shows its
            // selection, once.
            MarkerCell bright = i == focus_marker ? focus_cell
                                                  : MarkerCell::Payload;
            const bool bright_cell_shown =
                bright == MarkerCell::Payload ||
                (i == suppressed.marker_index && bright == suppressed.cell) ||
                cells.present;
            if (!bright_cell_shown) bright = MarkerCell::Payload;
            c.face = resolve_flag_face(kind, dis, red);

            // THE SEGMENTS: the payload label (the column's composer, the cut
            // inside it), then the two bound cells where they paint — each
            // shaped on the program face and boxed by the one layout
            // (cue_segment_boxes). A TIE FOLLOWER'S CELLS wear the disabled
            // emboss (architect 2026-09-19: the leader's numbers, not this
            // marker's to author); a disabled marker's every segment does.
            const text_shape::ShapedRun payload =
                text_shape::shape_text_run(font, label_of(i));
            const IterCellLayout cl = measure_iter_cells(font, cells);
            const bool present[3] = {pass_paints(MarkerCell::Payload),
                                     cells.present &&
                                         pass_paints(MarkerCell::Lower),
                                     cells.present &&
                                         pass_paints(MarkerCell::Upper)};
            const int widths[3] = {
                static_cast<int>(std::nearbyint(payload.width_px)),
                cl.lower_w, cl.upper_w};
            const CueSegmentBoxes boxes = cue_segment_boxes(widths, present);
            const MarkerCell order[3] = {MarkerCell::Payload, MarkerCell::Lower,
                                         MarkerCell::Upper};
            for (int s = 0; s < 3; ++s) {
                CueSegment& seg = c.seg[static_cast<std::size_t>(s)];
                if (!present[s]) continue;
                seg.present  = true;
                seg.run      = s == 0 ? payload
                             : s == 1 ? cl.lower_run : cl.upper_run;
                seg.x0       = c.col + boxes.x0[s];
                seg.x1       = c.col + boxes.x1[s];
                seg.selected = sel && bright == order[s];
                seg.embossed = dis || (s > 0 && cells.follower);
            }
            cues.push_back(std::move(c));
        });

    paint_cues(cr, lane, top_strip_area.x, waveform_width, cues, out_hit_rects,
               out_stems);
    cairo_restore(cr);
}

} // namespace

// THE ONE DERIVATION of the standing suppression (the contract is at the
// declaration in render.h). It is deliberately not three call sites asking the
// editor state three ways: the flag pass that SKIPS a box, the flag cache that
// KEYS the frame on it and the editor painter that DRAWS in its place all read
// this one body, so none of them can believe a different box is being edited.
SuppressedBox suppressed_flag_box(const AppState& app) {
    const text_editor::State& ed = app.top_flag_editor;
    if (!text_editor::is_active(ed)) return SuppressedBox{};
    SuppressedBox s;
    switch (ed.kind) {
        case text_editor::Kind::FlagPayload:
            s.cell = MarkerCell::Payload; break;
        case text_editor::Kind::IterBound:
            // The session's own side bit, given its cell name at the one place
            // that names it (iter_bound_editor_side, app_state.h).
            s.cell = iter_bound_editor_side(ed); break;
        default:
            // Every other kind edits somewhere else entirely (the BpmBracket
            // and the dialog fields paint in the bottom row's modal), so the
            // marker lane suppresses nothing for them.
            return SuppressedBox{};
    }
    s.marker_index = ed.target;
    return s;
}

void render_flags(cairo_t* cr,
                  GuiRect top_strip_area,
                  FlagLaneRects lanes,
                  int waveform_width,
                  const std::vector<GuiWarpMarker>& markers,
                  long long viewport_start_sample,
                  long long viewport_end_sample,
                  int sample_rate,
                  const std::set<int>& selected_set,
                  const std::set<int>& red_set,
                  bool iteration_on,
                  int focus_marker,
                  MarkerCell focus_cell,
                  std::vector<FlagHitRect>* out_hit_rects,
                  std::vector<MarkerStem>* out_stems,
                  const std::vector<WarpFrameMapSegment>* warp_frame_map,
                  const DragOverlay* drag_overlay,
                  SuppressedBox suppressed) {
    render_flag_boxes_impl(
        cr, GuiFlagKind::Warp, top_strip_area, lanes, waveform_width, markers,
        viewport_start_sample, viewport_end_sample, sample_rate,
        selected_set, red_set,
        // THE PAINTED COMPOSER (flag_display_text, render.h): the tempo's
        // base and its whole chain, the scale capped to `*N.NN`, and the
        // bounds are the two cells beside it. The editor seeds from the UNCUT
        // sibling, flag_text, so nothing a commit reads passes through here.
        [&](int i) { return flag_display_text(markers, i); },
        // The warp column's disabled verdict follows the label_ref cascade.
        [&](int i) { return effective_disabled(markers, i); },
        // The two bound cells, on exactly the markers the sweep reads
        // (warp_iter_cells above), and none outside the mode.
        [&](int i) { return warp_iter_cells(markers, i, iteration_on); },
        out_hit_rects, out_stems, warp_frame_map, drag_overlay,
        suppressed, iteration_on,
        focus_marker, focus_cell);
}

void render_phase_reset_flags(cairo_t* cr,
                            GuiRect top_strip_area,
                            FlagLaneRects lanes,
                            int waveform_width,
                            const std::vector<GuiPhaseResetMarker>& phase_resets,
                            long long viewport_start_sample,
                            long long viewport_end_sample,
                            int sample_rate,
                            const std::set<int>& selected_set,
                            const std::set<int>& red_set,
                            bool iteration_on,
                            int focus_marker,
                            MarkerCell focus_cell,
                            std::vector<FlagHitRect>* out_hit_rects,
                            std::vector<MarkerStem>* out_stems,
                            const std::vector<WarpFrameMapSegment>* warp_frame_map,
                            const DragOverlay* drag_overlay,
                            SuppressedBox suppressed) {
    render_flag_boxes_impl(
        cr, GuiFlagKind::PhaseReset, top_strip_area, lanes, waveform_width,
        phase_resets,
        viewport_start_sample, viewport_end_sample, sample_rate,
        selected_set, red_set,
        // A phase reset authors no payload, so its flag carries the display-only
        // token (render.h owns it and what it reads). It reaches the pass
        // whole: the shared byte cap is gone (2026-09-19) and this column has
        // nothing to cut — the token is five bytes by ruling.
        [&](int) { return std::string(kPhaseResetLaneToken); },
        // No label_ref cascade on this column — the bool is the whole verdict.
        [&](int i) { return phase_resets[i].disabled; },
        // THE TWO BOUND CELLS, on exactly the resets the sweep reads
        // (phase_iter_cells above), and none outside the mode. The bracket is
        // a HOP bracket here — the reset's position walked along the analysis
        // lattice — so the cells carry the signed integer and no decimals,
        // which is what tells the two columns' cells apart at a glance.
        [&](int i) { return phase_iter_cells(phase_resets, i, iteration_on); },
        out_hit_rects, out_stems, warp_frame_map, drag_overlay,
        // THE BOUND CELLS ARE THE ONLY BOXES THIS COLUMN SUPPRESSES, and the
        // asymmetry is real rather than an oversight (the warp/phase-reset
        // symmetry rule, warpmarkers.h): the BOUND editor is both columns'
        // since 2026-09-09, while the PAYLOAD editor is a
        // WARP-column surface by its own open gates — a phase reset authors
        // no payload line, its flag carrying a display-only token — so no
        // phase-reset flag can ever be the edited one for that kind. THE FORK
        // IS HERE rather than at the caller because
        // this painter owns its column's asymmetry: a suppression naming the
        // payload is dropped, so a warp target index can never be applied to
        // this store.
        suppressed.cell == MarkerCell::Payload ? SuppressedBox{} : suppressed,
        iteration_on,
        focus_marker, focus_cell);
}

void render_history_diff_flags(
        cairo_t* cr,
        GuiRect top_strip_area,
        FlagLaneRects lanes,
        int waveform_width,
        const std::vector<HistoryDiffFlag>& flags,
        long long viewport_start_sample,
        long long viewport_end_sample,
        int focus_index,
        const std::set<int>& selected,
        std::vector<FlagHitRect>* out_hit_rects,
        std::vector<MarkerStem>* out_stems,
        const std::vector<WarpFrameMapSegment>* warp_frame_map) {
    // The same clear-first contract the two marker painters carry: this pass is
    // the SOLE producer of both stashes while the history mode stands, so a
    // frame that paints nothing must leave nothing claimable behind.
    if (out_hit_rects) out_hit_rects->clear();
    if (out_stems)     out_stems->clear();
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;

    const GuiRect lane = lanes.marker_lane;
    if (lane.h <= 0) return;

    cairo_save(cr);
    // The waveform's columns are this pass's clip too, the live lane's rule
    // (clip_to_waveform_columns, architect 2026-09-26).
    clip_to_waveform_columns(cr, top_strip_area.x, waveform_width,
                             top_strip_area.y, top_strip_area.h);
    // The program face for the pass, exactly as render_flag_boxes_impl names
    // it (gui_font.h).
    const GuiFont font = gui_font(GuiFace::Program);
    const int u = cue_unit_px();

    // THE LEFT CULL'S BOUND, DERIVED FROM THIS COMMIT'S OWN TEXT rather than
    // from the marker lane's own worst case, because THESE LABELS ARE NOT CUT.
    // The live lane truncates because a marker label is free text the user types
    // and a runaway one would swamp its neighbours; a diff flag's label is the
    // SIDECAR'S OWN TOKEN with a three-byte sign prefix, and cutting it would
    // throw away the one thing the flag exists to show — a `[-]chorus=1.05`
    // cut at nine bytes would read `[-]choru...`, which names neither
    // the label nor the value. So the text prints whole and the bound follows
    // it: one byte per em is the same over-estimate marker_flag_max_width_px
    // makes (no ASCII glyph on this face advances a full em at these sizes), and
    // an over-estimate is exactly what a cull bound must be.
    double widest_bytes = 0.0;
    for (const HistoryDiffFlag& f : flags) {
        const double n = static_cast<double>(f.removed_text.size() +
                                             f.added_text.size());
        if (n > widest_bytes) widest_bytes = n;
    }
    const double cull_width_px =
        widest_bytes * gui_font_advance_bound_px(font) +
        // The label's lead, both segments' pads and the gap between a
        // changed pair's halves. A bound must never under-state, and the
        // widest flag in a commit may be a pair.
        static_cast<double>((kProgramSpec.cue_fill_lead +
                             4 * kProgramSpec.cue_fill_pad +
                             kProgramSpec.cue_segment_gap) * u);
    std::vector<CueDraw> cues;

    iterate_visible_flags_impl(
        top_strip_area, waveform_width, flags,
        viewport_start_sample, viewport_end_sample,
        warp_frame_map,
        // NO DRAG OVERLAY: the mode consumes every authoring gesture, so no
        // marker drag can be in flight while this pass runs — and a diff flag is
        // not a marker in any store, so nothing could index it anyway.
        /*drag_overlay=*/nullptr,
        cull_width_px,
        [&](int i, double left_x) {
            const HistoryDiffFlag& f = flags[static_cast<std::size_t>(i)];
            // THE MODE'S OWN FOCUS AND ITS OWN SELECTION, never the live one:
            // either gives the flag THE SELECTED FACE, and BOTH HALVES of a
            // changed pair take it together, the stem with them — a double
            // flag is one item, so it shows as one. The two are ONE
            // face by ruling (the declaration says why), so this is an OR
            // rather than a ladder.
            const bool focused =
                (i == focus_index) || (selected.count(i) != 0);
            // A flag with neither half is not constructible by the resolver
            // above; the guard keeps a degenerate one from publishing a
            // zero-width claim.
            if (!f.removed && !f.added) return;
            CueDraw c;
            c.index = i;
            c.col   = static_cast<int>(std::nearbyint(left_x));
            // THE TWO HALVES AS TWO SEGMENTS (the removed then the added),
            // their box layout the live lane's one (cue_segment_boxes), the
            // segment slots 0 and 1; a selected flag lights both halves and
            // the gap between as ONE box (join_selected).
            text_shape::ShapedRun run_removed;
            text_shape::ShapedRun run_added;
            if (f.removed)
                run_removed = text_shape::shape_text_run(font, f.removed_text);
            if (f.added)
                run_added = text_shape::shape_text_run(font, f.added_text);
            const bool present[3] = {f.removed, f.added, false};
            const int widths[3] = {
                static_cast<int>(std::nearbyint(run_removed.width_px)),
                static_cast<int>(std::nearbyint(run_added.width_px)), 0};
            const CueSegmentBoxes boxes = cue_segment_boxes(widths, present);
            const bool pair = f.removed && f.added;
            c.join_selected      = true;
            c.segments_are_cells = false;

            // THE DISABLED AXIS, ONE EFFECTIVE BIT PER COMMIT SIDE (architect
            // 2026-08-22, the cascade joining the same day the axis landed).
            // Each half asks its OWN side's EFFECTIVE verdict — the removed
            // half `then_effective_disabled`, the added half
            // `now_effective_disabled`, each resolved within its own side's
            // full warp set at the delta (phase resets have no cascade, so
            // their local bit filled these verbatim) — so the DIM is the live
            // lane's truth per commit: a label ref whose same-side definition
            // is disabled dims here exactly as its live marker does, while the
            // '#' in the LABEL text stays the line's verbatim local byte (the
            // text/face split at HistoryDiffFlag). A
            // disable TOGGLE paints one embossed half beside one live one and
            // the direction of the toggle reads straight off the label. Each
            // bit is meaningful exactly when its half is painted, so a bit
            // resting at false on a half that does not exist is never
            // consulted.
            const bool removed_disabled = f.then_effective_disabled;
            const bool added_disabled   = f.now_effective_disabled;
            for (int s = 0; s < 2; ++s) {
                CueSegment& seg = c.seg[static_cast<std::size_t>(s)];
                if (!present[s]) continue;
                seg.present  = true;
                seg.run      = s == 0 ? run_removed : run_added;
                seg.x0       = c.col + boxes.x0[s];
                seg.x1       = c.col + boxes.x1[s];
                seg.selected = focused;
                seg.embossed = s == 0 ? removed_disabled : added_disabled;
            }

            // THE TRIANGLE AND THE DOTS through the live lane's one ladder
            // (resolve_flag_face), AS THE KIND OF THE HALF THE DOTS LEAVE
            // FROM (architect 2026-10-04): the removed half's on a changed
            // pair or a removed-only flag, the added half's on an added-only
            // one — a diff line is never the invalid class (HistoryDiffFlag's
            // note). THE DOTS READ THE DISABLED AXIS (architect 2026-08-22):
            // a SINGLE half whose one side is disabled publishes none, the
            // live lane's rule; A CHANGED PAIR ALWAYS KEEPS ITS DOTS, its
            // triangle the removed face: the pair is a live EDIT being
            // displayed, not a line in a switched-off state.
            const GuiFlagKind kind =
                f.removed ? GuiFlagKind::Removed : GuiFlagKind::Added;
            const bool single_disabled =
                !pair && (f.removed ? removed_disabled : added_disabled);
            c.face = resolve_flag_face(kind, single_disabled, /*red=*/false);
            cues.push_back(std::move(c));
        });

    paint_cues(cr, lane, top_strip_area.x, waveform_width, cues, out_hit_rects,
               out_stems);
    cairo_restore(cr);
}

namespace {
    // Current GUI scale, in PERCENT. Set by set_gui_scale_percent from the two
    // application points (gui_main's startup read of the device config, and the
    // settings editor's gui_scale commit). EVERY painted pixel quantity in the product reads it
    // through gui_scale_factor().
    int    g_gui_scale_percent = 100;
} // namespace

void   set_gui_scale_percent(int percent) { g_gui_scale_percent = percent; }

int    gui_scale_percent() { return g_gui_scale_percent; }
double gui_scale_factor()  {
    return static_cast<double>(g_gui_scale_percent) / 100.0;
}

// -- The palette's chokepoints (the contract is at their declaration,
// render.h) ---------------------------------------------------------------

void set_palette_source(cairo_t* cr, GuiColor c) {
    const GuiColor d = display_color(c);
    cairo_set_source_rgb(cr, d.r, d.g, d.b);
}
void set_waveform_source(cairo_t* cr, GuiColor c) {
    const GuiColor d = display_color(c);
    cairo_set_source_rgb(cr, d.r, d.g, d.b);
}

// -- The active palette (the contract is at its declaration, render.h) -----

namespace {
    // Construction state, never painted: gui_main installs the palette
    // before the window exists (the gui_scale shape).
    GuiPalette        g_palette{};
    uint64_t          g_palette_generation = 0;
    WaveformPlateInks g_plate_inks{};
    // THE LIVE WORDS (render.h's program_palette_words): the sixteen as
    // the install family last wrote them.
    GuiPaletteWords   g_program_words{};
    // THE LIVE CHROME PICK (render.h's live_chrome_pick): the knob as the
    // install family last wrote it.
    std::optional<GuiChromePick> g_chrome_pick;
} // namespace

const GuiPalette& palette() { return g_palette; }

GuiColor surface_text(GuiSurface surface) {
    // The table and its readers are at the declaration (render.h).
    const GuiPalette& pal = palette();
    const bool clearlooks =
        live_chrome_spec().vocabulary == GuiChromeVocabulary::Clearlooks;
    switch (surface) {
    case GuiSurface::Face:            return pal.label;
    case GuiSurface::ProgramCase:     return hex(kCeCaseInk);
    case GuiSurface::CaptionActive:
        return clearlooks ? pal.cl_title_text : pal.caption_active_text;
    case GuiSurface::CaptionInactive:
        return clearlooks ? pal.cl_title_unfocused : pal.caption_inactive_text;
    case GuiSurface::Card:            return pal.card_text;
    case GuiSurface::ListRow:
        return clearlooks ? pal.cl_text : pal.field_text;
    case GuiSurface::ListRowLit:
        return clearlooks ? pal.cl_text_selected : pal.selected_text;
    }
    return pal.label;
}
uint64_t palette_generation() { return g_palette_generation; }
WaveformPlateInks waveform_plate_inks() {
    // The window's words (the struct's declaration, render.h).
    return WaveformPlateInks{display_transform::display_rgb(g_plate_inks.ink_rgb),
                             display_transform::display_rgb(
                                 g_plate_inks.outline_rgb)};
}
const GuiPaletteWords& program_palette_words() { return g_program_words; }
const std::optional<GuiChromePick>& live_chrome_pick() { return g_chrome_pick; }

namespace {
// THE PROGRAM'S SIXTEEN into the installed struct and the plate's two baked
// inks off the same words — the install family's shared half (the
// generation is each member's own bump).
void fill_program_palette(const GuiPaletteWords& w) {
    g_program_words = w;
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        g_palette.*(kGuiPaletteRoles[i].member) = hex(w[i]);
    // THE COOL EDIT BLOCK, derived from the panel's face by Cool Edit's own
    // rule (cool_edit_derive.h), so a pick of Face moves every tone live.
    // The view bar's span bevels derive from the waveform's ink instead
    // (ToneSource::Ink), so a pick of the ink moves them live too.
    static constexpr std::size_t kFace = palette_role_index("face");
    static constexpr std::size_t kInk = palette_role_index("waveform_ink");
    static_assert(kFace < kGuiPaletteRoleCount);
    for (const cool_edit_derive::Tone& t : cool_edit_derive::kTones)
        if (t.member != nullptr)
            g_palette.*(t.member) = hex(cool_edit_derive::tone(
                t.source == cool_edit_derive::ToneSource::Ink ? w[kInk]
                                                              : w[kFace],
                t.a, t.b));
    static constexpr std::size_t kOutline =
        palette_role_index("waveform_outline");
    static_assert(kInk < kGuiPaletteRoleCount &&
                  kOutline < kGuiPaletteRoleCount);
    g_plate_inks = WaveformPlateInks{w[kInk], w[kOutline]};
}
// THE CHROME'S MEMBERS off the live chrome's words with the knob
// (live_chrome_words, chrome_derive.h: the compiled theme, the knob derived
// over it by the live chrome's derivation) — the install family's other
// shared half — AND THE LIVE SCHEME'S FACE TAG with them (2026-10-09; the
// chrome's own scheme, no pick, is Tahoma): the one writer of
// set_live_scheme_face, so the face the windows chrome wears is always the
// installed scheme's (gui_live_face_set, gui_font.h).
void fill_chrome_palette(const std::optional<GuiChromePick>& chrome) {
    g_chrome_pick = chrome;
    set_live_scheme_face(chrome ? chrome->face : GuiSchemeFace::Tahoma);
    const GuiThemeWords w = live_chrome_words(live_chrome_spec(), chrome);
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i)
        g_palette.*(kGuiThemeRoles[i].member) = hex(w[i]);
}
} // namespace

void install_palette(const GuiPaletteWords& program,
                     const std::optional<GuiChromePick>& chrome) {
    // The chrome's fields are filled off the live chrome's compiled theme
    // with the scheme's keys (live_chrome_words, chrome_derive.h, which
    // never misses) and the program's off `program`, THE LIVE WORDS the
    // caller resolved (render.h: the launch's palette_record of the
    // `palette` key, beside the scheme_record of the `scheme` key), the two
    // tables covering the struct exactly
    // (palette_file.cpp), so a role cannot be valued and not painted.
    fill_chrome_palette(chrome);
    fill_program_palette(program);
    ++g_palette_generation;
}

void install_chrome_pick(const std::optional<GuiChromePick>& chrome) {
    // The contract is at the declaration (render.h); the apply shape is the
    // caller's (GuiColorPicker::install_live_words).
    fill_chrome_palette(chrome);
    ++g_palette_generation;
}

void install_program_palette(const GuiPaletteWords& words) {
    // The contract and the apply shape the caller owes are at the
    // declaration (palette_file.h).
    fill_program_palette(words);
    ++g_palette_generation;
}

void install_true_colors(bool on) {
    // The contract, the caches it rebuilds and the apply shape the caller
    // owes are at the declaration (render.h).
    display_transform::set_true_colors(on);
    ++g_palette_generation;
}

void show_embossed_run(cairo_t* cr, const text_shape::ShapedRun& run,
                       double x, double baseline) {
    // UNDER CDE Motif's stipple of the label (render.h's declaration): the
    // run's cell — its ascent above the baseline — anchors the phase, and a
    // margin of an ascent round the run bounds the group (a glyph poking
    // past its cell is kept whole).
    if (live_chrome_spec().vocabulary == GuiChromeVocabulary::Cde) {
        const int asc = std::max(
            1, static_cast<int>(std::nearbyint(gui_font_ascent_px(run.font))));
        const int px = static_cast<int>(std::nearbyint(x));
        const int top = static_cast<int>(std::nearbyint(baseline)) - asc;
        paint_stipple_begin(
            cr, GuiRect{px - asc, top - asc,
                        static_cast<int>(std::ceil(run.width_px)) + 2 * asc,
                        3 * asc});
        set_palette_source(cr, palette().label);
        text_shape::show_shaped_run(cr, run, x, baseline);
        paint_stipple_end(cr, px, top);
        return;
    }
    // Under clearlooks GTK's own insensitive text, the same two copies in
    // the engine's two tones (render.h's declaration).
    const bool clearlooks =
        live_chrome_spec().vocabulary == GuiChromeVocabulary::Clearlooks;
    const double off = static_cast<double>(relief_line_px());
    set_palette_source(cr, clearlooks ? palette().cl_text_insensitive_etch
                                      : palette().hilight);
    text_shape::show_shaped_run(cr, run, x + off, baseline + off);
    set_palette_source(cr, clearlooks ? palette().cl_text_insensitive
                                      : palette().shadow);
    text_shape::show_shaped_run(cr, run, x, baseline);
}

// -- The flag editor's unrolled box ---------------------------------------

// WHERE ONE SEGMENT'S BOX STANDS on the committed label, as an offset from
// the marker's column — the bound editor's anchor, and the whole of what
// that anchor needs. Off the same eligibility, the same tokens and the same
// font the flag pass lays the segments out with (warp_iter_cells or
// phase_iter_cells, `phase` deciding which — the bound editor is both
// columns' since 2026-09-09) and the one layout (cue_segment_boxes), so the
// field opens on exactly the box the resting segment stands in.
//
// IT ANSWERS WHERE, NOT HOW WIDE (architect 2026-09-05: the bound field takes
// the payload field's one width rule, its box being its own pads and its own
// run). Where no cells paint at all the answer is the payload's box, unreachable
// because the open asked (enter_iter_bound_edit) and a keyboard-modal editor
// freezes the mode bit.
static int committed_cell_seam_off(const AppState& app,
                                   const GuiFont& font, bool phase,
                                   int idx, MarkerCell side,
                                   bool iteration_on) {
    const std::vector<GuiWarpMarker>&       mv  = app.warpmarkers.markers();
    const std::vector<GuiPhaseResetMarker>& pmv =
        app.phaseresetmarkers.markers();
    // THE PAINTED COMPOSER ON EACH COLUMN, never the uncut one: this shapes
    // exactly what the flag pass shaped, or the field would open at a column
    // no segment stands on (the declaration of flag_display_text says why
    // this is the one place a wrong composer hides).
    const std::string label = phase ? std::string(kPhaseResetLaneToken)
                                    : flag_display_text(mv, idx);
    const text_shape::ShapedRun run = text_shape::shape_text_run(font, label);
    const IterCellLayout cl = measure_iter_cells(
        font, phase ? phase_iter_cells(pmv, idx, iteration_on)
                    : warp_iter_cells(mv, idx, iteration_on));
    const bool present[3] = {true, cl.present, cl.present};
    const int widths[3] = {static_cast<int>(std::nearbyint(run.width_px)),
                           cl.lower_w, cl.upper_w};
    const CueSegmentBoxes boxes = cue_segment_boxes(widths, present);
    const int s = !cl.present ? 0 : flag_box_rank(side);
    return boxes.x0[s];
}

// The contract (the face, the unclamped position, the non-const AppState) is
// at the declaration in render.h. What follows is the mechanics.
void render_flag_editor_box(cairo_t* cr, AppState& app, const GuiAudio& audio) {
    // The publication is unconditional: every run that finds no open editor
    // writes the invalid state, so a closed session can never leave the pointer
    // path a stale box to grab.
    FlagEditorBox& out = app.flag_editor_box;
    out = FlagEditorBox{};

    text_editor::State& ed = app.top_flag_editor;
    if (!text_editor::is_active(ed)) return;
    // TWO KINDS PAINT IN THE MARKER LANE AND THEY ARE ONE MODEL. FlagPayload
    // unrolls the flag ITSELF to hold the payload; IterBound opens ONE BOUND
    // CELL as the field. In every case the edited box yields in the cached pass, the field is the
    // width of its own content, the boxes LEFT of it stand exactly where they
    // rest and the boxes RIGHT of it ride the field's edge (SuppressedBox,
    // render.h — the whole ruling). The BpmBracket kind paints in the bottom
    // row's modal instead and returns here.
    const bool bound_kind   = (ed.kind == text_editor::Kind::IterBound);
    const bool payload_kind = (ed.kind == text_editor::Kind::FlagPayload);
    if (!payload_kind && !bound_kind) return;

    // The PAYLOAD editor is a WARP-COLUMN surface by its own open gates, in
    // EITHER audio view since 2026-08-24 (the home-view binding's fifth ruled
    // exception, active_column_authoring_allowed, app_state.h) — which costs
    // this painter nothing, the column below being resolved on the DISPLAYED
    // basis and the live map like every other lane item. The BOUND editor is
    // both columns' since 2026-09-09 (the phase-reset
    // column carries an iteration bracket of its own), so its store is the
    // ACTIVE column's — the column the open route resolved the index against.
    // A target index the store has since shrunk past is the only failure
    // shape, and it simply paints nothing.
    const bool phase = bound_kind && app.active_markers_view == 'P';
    const std::vector<GuiWarpMarker>&       mv  = app.warpmarkers.markers();
    const std::vector<GuiPhaseResetMarker>& pmv = app.phaseresetmarkers.markers();
    const int idx = ed.target;
    const int store_n = phase ? static_cast<int>(pmv.size())
                              : static_cast<int>(mv.size());
    if (idx < 0 || idx >= store_n) return;
    const int64_t marker_frame =
        phase ? pmv[static_cast<size_t>(idx)].time_frame
              : mv[static_cast<size_t>(idx)].time_frame;

    // THE MARKER LANE, the flag pass's own: the field is the label's selected
    // fill (rows 0 to cue_fill_h_px), standing exactly where the resting
    // segment's box does.
    const GuiRect lane = top_marker_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;
    const int u      = cue_unit_px();
    const int fill_h = cue_fill_h_px();

    cairo_save(cr);
    // The program face, the cues' own (gui_font.h).
    const GuiFont font = gui_font(GuiFace::Program);

    // THE FULL, UNTRUNCATED pending — the unroll's whole point. The scale cap
    // is a PAINTED-FLAG rule; an editor shows what it is editing.
    const text_shape::ShapedRun run =
        text_shape::shape_text_run(font, ed.pending);
    std::vector<double> byte_x =
        text_shape::byte_offsets_px(run, ed.pending.size());

    // The segment box's pad quantum either side of the run (cue_segment_boxes).
    const int pad_l = kProgramSpec.cue_fill_pad * u;
    const int pad_r = pad_l;
    // THE CARET'S COLUMN, AND WHERE EVERY FIELD FINDS IT. The caret at
    // end-of-text stands one column past the last glyph, so a field must own a
    // column its run does not — and IT BORROWS THAT COLUMN FROM ITS OWN RIGHT
    // PAD rather than buying one, on both kinds alike (architect
    // 2026-09-05: "all flag editors should work under the same principle
    // graphically ... graphically to the user it should be transparent
    // switching between the comments, the bounds and the main payload; the
    // main difference should be the colour of the comments and the syntax").
    // So the box below is exactly two pads plus its run, which is exactly what
    // the resting box it stands in for is: at the open — where the run is the
    // committed text on the same font — the payload field IS the flag and
    // the bound field IS its cell, and nothing riding past the field steps sideways when it opens. What moves
    // afterwards is what is TYPED, ON EVERY KIND ALIKE (architect 2026-09-05,
    // retiring the bound field's pin to its cell — "the two editors on the
    // opposite ends behaving one way and the bounds one in the middle behaving
    // in a different way makes the whole thing seem hacked together"): the box
    // grows and shrinks with the pending run, an emptied field is two pads —
    // the smallest field there is, on every kind — and the marker's boxes to
    // the right of this one ride the edge. There was no fixed-width grammar to
    // pin a bound field to in any case: this face is PROPORTIONAL, so `-3.75`
    // and `+1.00` are not the same width.
    // The pad and the caret are both one quantum (2026-10-09, the cue's
    // label: the fill 1 W past the text's end), so the borrow takes the whole
    // pad: the viewport ends on the box's own right edge and the caret's
    // column is the box's last fill column — visible ink, not a lost one, so
    // the travel arithmetic below needs no floor of its own. The TEXT
    // VIEWPORT is what widens by the borrowed column; the box never does.
    const int caret_px = u;

    const int run_w = static_cast<int>(std::nearbyint(run.width_px));
    // THE BOX IS ITS TWO PADS AND ITS RUN, AND NO WIDTH RULE BOUNDS IT — not
    // the lane, not the window. The LANE-WIDTH CAP that stood here went with
    // the position clamp below (architect 2026-09-06): it was a WIDTH rule
    // kept for a POSITION rule's sake — a box no wider than the lane can
    // always be slid fully on-window — and with the field standing wherever
    // its own box stands, a field wider than the waveform simply runs off its
    // last column and is cut off there (the clip below), which is the same
    // truthful answer the cap existed to avoid giving. Nothing else read it:
    // the text viewport, the view offset and the riding run all derive from
    // `box_w` rather than from the lane.
    const int box_w = pad_l + run_w + pad_r;

    const std::vector<WarpFrameMapSegment>& map =
        displayed_or_live_target_map(app, audio);
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    const int col = painted_column_of_source_frame_on_basis(
        app, audio, static_cast<double>(marker_frame),
        map, basis.vp_start, basis.spp);
    const GuiRect area = waveform_area(app);
    // THE WAVEFORM'S COLUMNS ARE THIS BOX'S CLIP (architect 2026-09-26, the
    // flag pass's own rule — clip_to_waveform_columns): the width is the one
    // the column above was mapped against (the item basis, which is the
    // cached flag pass's plate width), so the open editor is cut off at
    // exactly the column its resting label is.
    // It holds for everything this painter draws — the field, its text, the
    // caret and the riding segments — until the closing restore.
    const int clip_w = basis.area_w > 0 ? basis.area_w : area.w;
    // (The cue's triangle and its dots stay the flag pass's and
    // paint_marker_stems', unchanged by the open: the triangle never changes
    // when selected. A refusal recolors neither, architect 2026-10-03.)
    clip_to_waveform_columns(cr, area.x, clip_w, lane.y, lane.h);

    // ITERATION MODE ADDS THE TWO BOUND CELLS TO THE COMMITTED FLAG, so the
    // anchors below must ask under the same verdict the flag pass paints
    // under: THE COLUMN'S, not the mode's bare bit (iteration_column_lit,
    // app_state.h — architect 2026-09-10, the lamp is lit for the column it
    // was pressed in and the cells live there alone; the live column is
    // stable for the whole life of the lamp, the W/P switch being one of the
    // acts the iteration lock refuses). `phase` above is this editor's
    // column, the payload editor being the warp column's by its own open
    // gates, so the two anchors measure exactly the boxes the cached pass
    // painted.
    const bool iteration_on = iteration_column_lit(app, phase ? 'P' : 'W');
    // EVERY FIELD OPENS WHERE ITS SEGMENT SITS (committed_cell_seam_off, off
    // the one layout cue_segment_boxes): the payload field on the label's
    // first box, the fill lead past the marker's column; a BOUND field on its
    // own cell's box, the marker's segments to its LEFT standing where they
    // are. In every case the segments to the RIGHT of the field yield to it
    // and RIDE ITS EDGE below, which at the open is exactly where they rest:
    // the field buys no column of its own (the borrow above), so
    // `bx + box_w` is the committed box's own right edge until something is
    // typed.
    //
    // THE FIELD'S CELL — which of the marker's boxes this field stands in for —
    // is read off the ONE derivation the flag pass's suppression is read off
    // (suppressed_flag_box), so the box that yields and the box that opens have
    // exactly one answer between them in the product. It is the payload's own
    // Payload for the flag editor and the session's side for a bound editor.
    const MarkerCell field_cell = suppressed_flag_box(app).cell;
    const int anchor_off = committed_cell_seam_off(app, font, phase, idx,
                                                   field_cell, iteration_on);

    // NO FIELD IS CLAMPED, ON ANY KIND, AT EITHER EDGE (architect 2026-09-06,
    // on the clamp this line used to carry: "let's get rid of that, that's a
    // good catch … leave its position truthful, don't clamp it, don't do
    // anything"). The box opened at the seam above and then slid left or right
    // to keep itself whole on-window — a rule written for the payload editor
    // when the flag was the only box on the row, and one the ONE GRAPHIC MODEL
    // cannot survive: at a lane edge the slide walked the field left OVER the
    // lower cell or the flag box that the cached pass is still painting at
    // rest, so the boxes to its left no longer stood where they rest and the
    // field no longer stood in its own box's slot — the two things the model
    // promises. So the field opens where its box IS and stays there.
    //
    // THE WAVEFORM'S COLUMNS ARE THE CLIP (architect 2026-09-26, set where
    // `area` is read above, superseding "the window is the clip"): a box past
    // the left edge falls off at column 0 and a box past the last column is
    // cut off at it, exactly as the cached flag pass clips its resting boxes,
    // so nothing of the field paints in the leftover strip a non-multiple-of-16
    // window leaves beside the waveform. A field cut off at an edge is READ BY
    // PANNING THE VIEWPORT: the marker-lane editors are pointer- and
    // wheel-transparent, so the wheel and the grab-pan work while one stands
    // and the field travels with its marker, which is the same answer the row
    // gives for a label that is half off the edge at rest.
    const int bx = area.x + col + anchor_off;

    // The text VIEWPORT inside the box: the band the run is clipped to, and the
    // width the view offset is measured against. The caret column belongs to it
    // (a caret at the end must be inside the clip to be seen), and THIS IS
    // WHERE THE BORROW IS SPENT, on every kind: the viewport reaches one column
    // INTO the right pad — the column the box above deliberately does not buy —
    // so a caret at end-of-text is inside the clip with the run standing
    // exactly where the committed text stands. The pad being the caret's own
    // quantum, the borrow takes the whole pad and the viewport ends on the
    // box's right edge, the caret's column being the box's last fill column.
    const double view_x0 = static_cast<double>(bx + pad_l);
    const int view_pad_r = std::max(pad_r - caret_px, 0);
    const double view_x1 = static_cast<double>(bx + box_w - view_pad_r);
    const double view_w  = view_x1 - view_x0;

    // THE MINIMAL-TRAVEL VIEW OFFSET (the field's contract is at
    // State::view_offset_px). Scroll only as far as the caret demands, in
    // whichever direction it left the VIEWPORT, then clamp to the run's own
    // travel — so a caret walking right pushes the view right one glyph at a
    // time and walking back left pulls it back the same way, never jumping.
    // The caret's own column is reserved at the right edge, so the comparison
    // is against (view_w - caret) rather than view_w: a caret at end-of-text
    // stops with its column inside the clip instead of half past it.
    //
    // WHAT IT STILL HAS TO DO HERE IS SUB-PIXEL, AND IT IS NOT NOTHING. The
    // box holds this field's WHOLE run by construction (two pads and the run,
    // no cap), so no long buffer travels on this surface any more — but
    // `box_w` takes the run's width to the nearest whole column and can round
    // DOWN by as much as half of one, which leaves the caret's reserved column
    // sitting exactly on the clip's right edge with no ink of its own. The
    // travel below pulls the origin back by that remainder and the caret is
    // seen. (The DIALOG field, whose box is a fixed width a long buffer really
    // does not fit, is where the glyph-by-glyph travel lives — the same rule
    // applied at paint_modal_dialog to a surface that needs all of it.)
    //
    // IT KEEPS THE CARET INSIDE THE FIELD, NEVER INSIDE THE WINDOW. With the
    // field standing at its own box wherever that box is, a field at a lane
    // edge is cut off and its caret can be cut off with it; the answer to that
    // is the VIEWPORT PAN (the ruling at `bx` above), not a scroll of the text
    // inside a box that is already showing all of it.
    const int cursor_pos =
        std::clamp(ed.cursor_pos, 0, static_cast<int>(ed.pending.size()));
    const double caret_off = byte_x[static_cast<size_t>(cursor_pos)];
    const double travel_w  = view_w - static_cast<double>(caret_px);
    double vo = ed.view_offset_px;
    if (caret_off - vo < 0.0)        vo = caret_off;
    if (caret_off - vo > travel_w)   vo = caret_off - travel_w;
    const double max_vo = run.width_px + static_cast<double>(caret_px) - view_w;
    if (vo > max_vo) vo = max_vo;
    if (vo < 0.0)    vo = 0.0;
    ed.view_offset_px = vo;

    const double text_origin_x = view_x0 - vo;
    const double baseline = static_cast<double>(lane.y + cue_baseline_px());

    // THE MARKER'S OWN STATE for the segments riding the field's right edge,
    // which keep their resting look: the disabled emboss, a tie follower's
    // cells embossed (2026-09-19), else the panel's light tone.
    const bool dis = phase ? pmv[static_cast<size_t>(idx)].disabled
                           : effective_disabled(mv, idx);
    // THE FIELD IS WINDOWS' EDIT FIELD (architect 2026-10-09 ~12:15,
    // reversing 2026-10-07 ~09:45's "the flag in its selected face, no box
    // and no frame"): the chrome's FIELD PAIR — `field_ground` under
    // `field_text` (white under black under Windows 2000; every chrome's own
    // pair, read as roles) — on every state of the marker, disabled and
    // invalid included (the triangle carries the state). WHY: the field pair
    // is the legibility guarantee ("when you're looking very closely at a
    // label it has to be legible"), and THE SELECTED SUBSTRING takes the
    // chrome's SELECTED PAIR, `selected_text` on `selected_fill`, the
    // ordinary highlight — the pair that exists "precisely to avoid this
    // problem" — part of the chrome, as every selection in the product is.
    const GuiPalette& pal = palette();
    const GuiColor field_fill = pal.field_ground;
    const GuiColor field_ink  = pal.field_text;
    const GuiColor sel_fill   = pal.selected_fill;
    const GuiColor sel_text   = pal.selected_text;
    const int  field_rank = flag_box_rank(field_cell);
    const IterCellText ride_text =
        field_rank >= flag_box_rank(MarkerCell::Upper) ? IterCellText{}
        : phase ? phase_iter_cells(pmv, idx, iteration_on)
                : warp_iter_cells(mv, idx, iteration_on);
    const bool ride_cells = ride_text.present;

    // 1. THE FIELD IS A BOX ON THE LABEL'S ROWS (cue_fill_h_px, rows 0 .. 8):
    //    the segment's box — its run and one quantum of pad either side, the
    //    text standing exactly where the resting label's does — with A
    //    ONE-QUANTUM OUTLINE in `field_text` round it, outside the pads on
    //    the left and right and on the box's first and last rows (the label's
    //    air rows), `field_ground` inside. IT PAINTS OVER THE LANE — the
    //    triangle's shadow and any neighbouring label it reaches (architect
    //    2026-10-09: "it may overlap with the flag and other elements, that's
    //    fine"). A refused Enter recolors nothing (text_editor::refuse selects
    //    the whole text; the owner's card says why). Since the editor opens on
    //    any store index (enter_top_flag_edit), disabled included, a disabled
    //    marker's field is this same box, never embossed.
    const GuiRect field_box{bx - u, lane.y, box_w + 2 * u, fill_h};
    paint_cell_rect(cr, field_box, field_ink);
    paint_cell_rect(cr, GuiRect{field_box.x + u, field_box.y + u,
                                field_box.w - 2 * u, field_box.h - 2 * u},
                    field_fill);

    // The caret / selection band: inside the outline, the field's whole
    // inner height. A text field's caret spans its whole field, so this needs
    // no font-extent solve.
    const int band_y = lane.y + u;
    const int band_h = fill_h - 2 * u;

    // Everything from here paints CLIPPED to the text viewport INSIDE THE
    // FIELD, so a scrolled run, its selection and its caret all stop at the
    // pads instead of bleeding over the box edge into the neighboring labels.
    cairo_save(cr);
    cairo_rectangle(cr, view_x0, static_cast<double>(band_y),
                    view_w, static_cast<double>(band_h));
    cairo_clip(cr);

    // 2. The selection highlight, then 3. the text — THE CHROME'S SELECTED
    //    PAIR on the band over the field pair (`field_ink` on `field_fill`,
    //    set at the state block above).
    //
    //    THE SELECTED SUBSTRING IS THE WHOLE RUN RE-SHOWN UNDER A CLIP, never
    //    a run shaped from the substring alone: shaping the selected bytes on
    //    their own could kern the first glyph differently and shift the ink
    //    sideways under a band whose edges came from byte_x. THE CLIP TAKES THE
    //    BAND'S OWN ROUNDED COLUMNS, not the fractional byte_x pair the band
    //    rounded FROM, so the selected ink starts and stops exactly where the
    //    band does.
    //
    //    ONE INK PER PIXEL (2026-08-28, off the architect's own screenshot of
    //    a selected word wearing a grey halo): with a selection standing, THE
    //    FIELD-TEXT RUN IS CLIPPED TO THE COMPLEMENT OF THE BAND inside the
    //    text viewport and the selected run to the band, two disjoint regions
    //    whose union is the whole viewport, so no pixel is painted by both
    //    inks and every edge pixel antialiases against exactly the ground it
    //    sits on. The band spans the viewport's whole height, so the
    //    complement is the columns left and right of it. With no selection
    //    the run paints unclipped inside the viewport.
    //
    //    Both edges come from byte_x, so the highlight cannot drift off the
    //    glyphs it marks however proportional they are.
    const bool has_sel = text_editor::has_selection(ed);
    const size_t s0 = static_cast<size_t>(text_editor::selection_start(ed));
    const size_t s1 = static_cast<size_t>(text_editor::selection_end(ed));
    const int ix0 =
        static_cast<int>(std::nearbyint(text_origin_x + byte_x[s0]));
    const int ix1 =
        static_cast<int>(std::nearbyint(text_origin_x + byte_x[s1]));
    // THE BAND'S WIDTH IN COLUMNS, resolved once: the fill below, the selected
    // run's clip and the other run's complement all read this one expression,
    // so the three cannot disagree by a pixel. A selection whose glyphs carry
    // no advance still marks one column.
    const int band_w = (ix1 > ix0) ? (ix1 - ix0) : 1;
    if (has_sel) {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        set_palette_source(cr, sel_fill);
        cairo_rectangle(cr, ix0, band_y, band_w, band_h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // THE RUN, SHOWN ONCE PER REGION (the ruling in the block above): the
    // whole run in the flag label off the band, the whole run again in
    // the selected text on it, neither reaching a pixel the other painted.
    set_palette_source(cr, field_ink);
    if (!has_sel) {
        text_shape::show_shaped_run(cr, run, text_origin_x, baseline);
    } else {
        cairo_save(cr);
        // The band's complement inside the viewport, as ONE clip path: the
        // columns left of the band and the columns right of it. A part with
        // nothing in it is left out rather than added empty — an empty
        // rectangle is a no-op in a fill but not obviously so in a clip path,
        // and a selection that fills the viewport is meant to leave the
        // field-text run nothing at all.
        const double band_x0 = static_cast<double>(ix0);
        const double band_x1 = static_cast<double>(ix0 + band_w);
        if (band_x0 > view_x0) {
            cairo_rectangle(cr, view_x0, static_cast<double>(band_y),
                            band_x0 - view_x0, static_cast<double>(band_h));
        }
        if (view_x0 + view_w > band_x1) {
            cairo_rectangle(cr, band_x1, static_cast<double>(band_y),
                            (view_x0 + view_w) - band_x1,
                            static_cast<double>(band_h));
        }
        cairo_clip(cr);
        text_shape::show_shaped_run(cr, run, text_origin_x, baseline);
        cairo_restore(cr);

        cairo_save(cr);
        cairo_rectangle(cr, ix0, band_y, band_w, band_h);
        cairo_clip(cr);
        set_palette_source(cr, sel_text);
        text_shape::show_shaped_run(cr, run, text_origin_x, baseline);
        cairo_restore(cr);
    }

    // 4. The caret: a blink-gated filled integer column at the cursor's own
    //    byte boundary, AA off. IT IS INK, NOT FIELD, so it stays the box's
    //    TEXT color, `field_ink`, wherever it lands (architect
    //    2026-10-03) — a caret that changed color on crossing a selection
    //    edge would be stating something about the selection rather than
    //    about the cursor. (It stands at the band's edge, never inside it.)
    if (text_editor::cursor_visible_now(ed)) {
        const int cx =
            static_cast<int>(std::nearbyint(text_origin_x + caret_off));
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        set_palette_source(cr, field_ink);
        cairo_rectangle(cr, cx, band_y, caret_px, band_h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    cairo_restore(cr);   // the text-viewport clip

    // THE MARKER'S SEGMENTS TO THE RIGHT OF THE FIELD RIDE ITS EDGE — in the
    // flag pass's own left-to-right order, each wearing its resting look. WHAT
    // RIDES FOLLOWS FROM WHICH BOX THE FIELD STANDS IN FOR (the one graphic
    // model, SuppressedBox in render.h): the payload field carries the two
    // bound cells, the LOWER-bound field carries the upper cell, and the
    // UPPER-bound field carries nothing, the upper cell being the marker's
    // rightmost box.
    //
    // THEY MUST NOT SIMPLY VANISH for the length of an edit: the cells are the
    // marker's range, which he is often typing AGAINST (architect 2026-09-05,
    // "the cells should stand"). So the cached pass drops
    // them and they paint HERE instead, from the field's right edge, sliding
    // with it as it grows and shrinks so THE ROW READS AS IT READS AT REST. At
    // the open the run does not move at all — the field buys no caret column,
    // so its right edge is the committed box's own — and it moves afterwards
    // only by what is typed.
    //
    // Each segment wears its resting look — its box in the panel face, the
    // text in the panel's light tone or the disabled emboss — and none takes
    // the selected look while the field stands, each open having seated the
    // selection axis on the cell it edits.
    //
    // IT IS PUBLISHED AS A SECOND RECT, NEVER FOLDED INTO `box`, and that rect
    // is a FLAG HIT RECT — the same shape and the same two boundaries the
    // flag pass publishes for a resting run, because THE RIDING CELLS ARE THE
    // MARKER'S OWN CELLS FOR THE POINTER TOO (architect 2026-09-05). A press
    // on one closes this editor as any outside press does and then acts on the
    // cell it landed on, so the run has to answer "which marker, which cell"
    // exactly as the resting boxes answer it — one walk, one idiom, no second
    // derivation (the contract is at FlagEditorBox::riding_cells). Folding it
    // into `box` would be the wrong union all the same: the caret / text-drag
    // claim seats a caret for ANY press inside `box` and the cursor map shows
    // the I-beam over exactly that rect, so a run folded in would map presses
    // on painted cell ink to payload bytes and promise text
    // editing where none is.
    // WHAT RIDES, BY RANK: every box standing right of the field's own. The
    // UPPER-bound field's rank is the last, so nothing rides under it. THE
    // STORE BELOW IS THE FIELD'S OWN COLUMN since 2026-09-09, when the bound
    // editor became both columns': the payload editor is warp-only by its
    // open gates, and `phase` already answered that question for the box
    // above.
    if (ride_cells) {
        // The cells, off the ONE composer and the ONE measurer the flag pass
        // reads (`ride_text`, composed above), so the re-paint cannot show a
        // different token or a different width from the cell it stands in
        // for. Nothing is shaped where no cell rides (the UPPER field, or a
        // marker with no cells), this branch not running — and where the
        // LOWER field stands it lays out one token that will not paint, the
        // pair being one measurement, which is the same one-run cost the flag
        // pass pays on that marker.
        const IterCellText& cells = ride_text;
        const IterCellLayout cl = measure_iter_cells(font, cells);
        // A TIE FOLLOWER'S RIDING CELLS GREY exactly as its resting ones do
        // (2026-09-19): they are the same boxes at a different x, off the same
        // composer, so the face composes the same term — and it comes from the
        // composer's own answer rather than a second walk of the tie.
        const bool cell_dis = dis || cells.follower;
        const bool ride_lower =
            cl.present && field_rank < flag_box_rank(MarkerCell::Lower);
        const bool ride_upper =
            cl.present && field_rank < flag_box_rank(MarkerCell::Upper);

        // THE RUN'S TWO BOUNDARIES, accumulated left to right from the
        // field's own right edge — the layout's segment gap, then each
        // riding box (cue_segment_boxes' own walk), and the same collapse
        // rule: a box that is not in the run leaves its boundary standing
        // where the next one begins, so no point can answer a box with no
        // ink. The gap after the field belongs to the first riding box.
        const int gap    = kProgramSpec.cue_segment_gap * u;
        const int run_x0 = bx + box_w;
        int cursor_x = run_x0;
        const int lower_seam = cursor_x;
        // The field's outline column stands in the first gap's first quantum
        // (field_box reaches one quantum past the pads), so the riding face
        // starts after it: the run stays where the resting label's is and the
        // outline is never painted over.
        const int face_x0 = run_x0 + u;
        const auto ride = [&](const text_shape::ShapedRun& seg_run, int seg_w) {
            const int x0 = cursor_x + gap;
            const int w  = pad_l + seg_w + pad_r;
            // The face box over the gap and the segment, as the resting
            // label's one box covers its gaps.
            const int fx = std::max(cursor_x, face_x0);
            paint_cell_rect(cr, GuiRect{fx, lane.y, x0 + w - fx, fill_h},
                            pal.face);
            const double tx = static_cast<double>(x0 + pad_l);
            if (cell_dis) {
                show_embossed_run(cr, seg_run, tx, baseline);
            } else {
                set_palette_source(cr, pal.ce_hilight);
                text_shape::show_shaped_run(cr, seg_run, tx, baseline);
            }
            cursor_x = x0 + w;
        };
        if (ride_lower) ride(cl.lower_run, cl.lower_w);
        const int upper_seam = cursor_x;
        if (ride_upper) ride(cl.upper_run, cl.upper_w);

        // THE RUN IS PUBLISHED AS A FLAG HIT RECT, keyed to the marker being
        // edited: its rect is the WHOLE re-painted run's extent, the gaps
        // included — the same paint-equals-claim rule the label boxes take —
        // and its two boundaries are the ones accumulated above, so
        // hit_test_flag_cell's walk answers Upper or Lower over exactly the
        // boxes that show one, and nothing at all for a box that stayed
        // behind in the lane pass. No point in the rect can answer Payload:
        // the run begins ON the lower boundary, and the payload's own box is
        // the FIELD, published as `box` and claimed by the caret. Nothing is
        // published where nothing rode — the cold marker_index -1 and the
        // zero rect, which contains no point. It carries no triangle: the
        // triangle stays the flag pass's. It begins after the field's outline
        // column, as its face does: that column is the field's, claimed by
        // `box`.
        const int run_w = cursor_x - face_x0;
        if (cursor_x > run_x0 && run_w > 0) {
            FlagHitRect& r = out.riding_cells;
            r.marker_index          = idx;
            r.x                     = static_cast<double>(face_x0);
            r.y                     = static_cast<double>(lane.y);
            r.w                     = static_cast<double>(run_w);
            r.h                     = static_cast<double>(fill_h);
            r.iter_lower_boundary_x = static_cast<double>(lower_seam);
            r.iter_upper_boundary_x = static_cast<double>(upper_seam);
            // Clipped as the pixels are; a run wholly past the last column
            // publishes the cold rect, which contains no point.
            if (!clip_hit_rect_to_waveform_columns(r, area.x, clip_w))
                r = FlagHitRect{};
            // EVERY CASE IS ONE EXPRESSION, the accumulator having already
            // done the collapsing: under a bound field the boundaries of the
            // boxes that stayed behind sit at the run's own left edge, so no
            // point answers them.
        }
    }

    cairo_restore(cr);   // the font state

    out.valid         = true;
    // THE PUBLISHED BOX IS THE PAINTED FIELD — the same rule the labels' hit
    // rects take: a press on a pad is a press on this editor, and it maps to
    // the nearest byte boundary (the box-is-the-claim clause at
    // FlagEditorBox), so the I-beam covers every column the field painted,
    // its outline included.
    //
    // AND IT IS CLIPPED TO THE WAVEFORM'S COLUMNS as its pixels are (the clip
    // above): a field cut off at the last column claims its visible part, and
    // a field wholly past the edge publishes an empty box, which contains no
    // point.
    {
        const int x_lo = std::max(field_box.x, area.x);
        const int x_hi = std::min(field_box.x + field_box.w, area.x + clip_w);
        out.box = GuiRect{x_lo, lane.y, std::max(x_hi - x_lo, 0), fill_h};
    }
    out.text_origin_x = text_origin_x;
    out.byte_x        = std::move(byte_x);
}
