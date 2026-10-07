#include "render.h"
#include "app_state.h"
#include "audio.h"
#include "device_config.h"
#include "gui_display_context.h"
#include "gui_font.h"
#include "text_shape.h"
#include "theme_file.h"
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
    const int border = waveform_border_px();
    const int lw     = relief_line_px();
    if (h > 2 * border) {
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

void paint_relief_raised_inner(cairo_t* cr, const GuiRect& r) {
    paint_relief_frame(cr, r, palette().hilight, palette().shadow);
}

void paint_checker_rect(cairo_t* cr, const GuiRect& r, int phase_x,
                        int phase_y, GuiColor lit, GuiColor ground) {
    if (r.w <= 0 || r.h <= 0) return;
    // THE TILE, Windows' pattern brush: 2 x 2 device px, lit at (0, 0) and
    // (1, 1), the ground at the other two — both cells theme roles handed in,
    // written as words (argb32_opaque_word), so every pixel the fill lays is
    // one of the two and the fill is opaque.
    cairo_surface_t* tile =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 2, 2);
    cairo_surface_flush(tile);
    unsigned char* data = cairo_image_surface_get_data(tile);
    const int stride = cairo_image_surface_get_stride(tile);
    const uint32_t lit_word    = argb32_opaque_word(lit);
    const uint32_t ground_word = argb32_opaque_word(ground);
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
// A FLAT CAPTION (end equal to start — every theme that records no gradient
// end, theme_file.h's flat-caption rule) IS ONE SOLID FILL OF THE EXACT
// COLOUR.
//
// OPAQUE AND NOTHING BLENDED AT PAINT TIME: each cell is one opaque rounded
// ramp value, written as words into an image (argb32_opaque_word's road, the
// plate's precedent) and laid on the caption whole. The image is kept
// between paints and rebuilt only when the caption's size or either colour
// moves (the picture reads nothing else; a gui_scale change moves the size),
// since the top strip's damage repaints the caption often and its ramp
// seldom changes.
namespace {
struct CaptionGradientImage {
    cairo_surface_t* surface = nullptr;
    int              w = 0, h = 0;
    uint32_t         start = 0, end = 0;
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
    if (!img.surface || img.w != r.w || img.h != r.h ||
        img.start != start_word || img.end != end_word) {
        if (img.surface) cairo_surface_destroy(img.surface);
        img = CaptionGradientImage{
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r.w, r.h), r.w,
            r.h, start_word, end_word};
        cairo_surface_flush(img.surface);
        unsigned char* data = cairo_image_surface_get_data(img.surface);
        const int stride = cairo_image_surface_get_stride(img.surface);
        const double s[3] = {start.r * 255.0, start.g * 255.0, start.b * 255.0};
        const double e[3] = {end.r * 255.0, end.g * 255.0, end.b * 255.0};
        // One row of rounded ramp words, then every row a copy of it. Each
        // level rounds by std::nearbyint, the tree's one rounding (a level
        // is not a grid point; this is symmetry with svg_icon's
        // saturated_copy, not a pixel). It parts from std::lround only on an
        // exact half, which takes the even level: no bundled theme's pair,
        // nor the built-in's, lands on one at the two devices' full-width
        // captions (1920 and 2304 device px), so those ramps are unchanged;
        // other widths (the restored laptop's) can meet one, one level apart.
        std::vector<uint32_t> row(static_cast<size_t>(r.w));
        for (int x = 0; x < r.w; ++x) {
            const double t = r.w > 1 ? static_cast<double>(x) / (r.w - 1)
                                     : 0.0;
            uint32_t word = UINT32_C(0xFF000000);
            for (int c = 0; c < 3; ++c) {
                const long v = static_cast<long>(
                    std::nearbyint(s[c] + (e[c] - s[c]) * t));
                word |= static_cast<uint32_t>(std::clamp(v, 0L, 255L))
                        << (16 - 8 * c);
            }
            row[static_cast<size_t>(x)] = word;
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
    return 2 * relief_line_px() +
           scaled_px(kWindowFramePx - 2 * kReliefLinePx, 0);
}

void paint_window_sizing_frame(cairo_t* cr, int surface_w, int surface_h,
                               int frame_px) {
    if (frame_px <= 0 || surface_w <= 0 || surface_h <= 0) return;
    const int f = frame_px;
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

void paint_relief_etched_vline(cairo_t* cr, int x, int y, int h) {
    const int lw = relief_line_px();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, GuiRect{x, y, lw, h}, palette().shadow);
    paint_cell_rect(cr, GuiRect{x + lw, y, lw, h}, palette().hilight);
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
                     PlayheadRows band) {
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
    // is onscreen (gated on its own column and clipped at the right edge by
    // fill_waveform_line, so it never leaks into an adjacent region).
    // ONE SOLID LINE, straight over whatever it crosses — waveform ink included.
    // A saturated stem over the dark ink reads without any cut, so there is no
    // two-tone overdraw here (see the declaration for the retirement).
    // THE ROWS ARE THE CALLER'S (architect 2026-10-02): a stem crosses the
    // well's top lines, the scanner keeps to the canvas (the declaration).
    const GuiRect rows = band == PlayheadRows::Stem
                             ? waveform_stem_band(area)
                             : waveform_content_rect(area);
    set_palette_source(cr, color);
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
    const int btn_w = trim_arrow_button_w_px();
    GuiRect r;
    // Begin left-edge-anchored (rect left ON the begin column); end
    // right-edge-anchored (rightmost pixel ON the end column), so each bound's
    // button stands on the column the bound occupies — UNLESS THE DRAWN WIDTH
    // IS UNDER TWO BUTTONS (architect 2026-10-03): then the begin keeps its
    // column and the end button stands edge to edge right of it, the right
    // arrow alone overrunning its column by 2 x btn_w − span. THE RULE READS
    // THE COLUMNS WHEREVER THEY ARE, on screen or off (architect 2026-10-05):
    // a narrow window panned past an edge keeps its two buttons edge to edge
    // and slides off whole, never re-seating the end button as the begin
    // leaves the view. The span is 64-bit: two far offscreen columns on
    // opposite sides can pass an int's range. The rect is the BUTTON over
    // the trim lane `row`'s whole height — the painted face and the hit band
    // at once, before the lane's clip.
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

// ONE ARROW BUTTON (the rule at kTrimArrowButtonPx and kTrimArrowGlyphWPx,
// render.h): the ground under the plain raised edge, then the scroll arrow
// as ONE FILLED TRIANGLE in the theme's LABEL (a chrome glyph on a chrome
// face, architect 2026-10-03; a triangle since 2026-10-06), antialiased,
// centred in device px (an odd difference flooring toward the top-left), its
// tip LEFT on the begin button and RIGHT on the end button. PRESSED (a
// single-bound grab holds it, render_trim_flags' declaration): the ground
// under one Shadow ring instead of the raised edge, the glyph one Windows px
// right and down. The caller's clip (the lane's) cuts a button that overruns
// the lane.
void paint_trim_arrow_button(cairo_t* cr, const GuiRect& b, bool points_left,
                             bool pressed) {
    paint_cell_rect(cr, b, palette().ground);
    if (pressed)
        paint_relief_line_frame(cr, b, palette().shadow);
    else
        paint_relief_plain_raised(cr, b);
    const int u = scaled_px(1, 1);
    const int glyph_w = kTrimArrowGlyphWPx * u;
    const int glyph_h = kTrimArrowGlyphHPx * u;
    const int push = pressed ? relief_line_px() : 0;
    const double gx = b.x + (b.w - glyph_w) / 2 + push;
    const double gy = b.y + (b.h - glyph_h) / 2 + push;
    const double tip_x  = points_left ? gx : gx + glyph_w;
    const double base_x = points_left ? gx + glyph_w : gx;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_new_path(cr);
    cairo_move_to(cr, tip_x, gy + glyph_h / 2.0);
    cairo_line_to(cr, base_x, gy);
    cairo_line_to(cr, base_x, gy + glyph_h);
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
                       TrimPressedCap pressed,
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

    // THE TRACK, the whole lane: the ground, then the checked dither, its
    // phase at the lane's top-left so every lane height dithers alike
    // (architect 2026-10-02, the AC set).
    const GuiRect lane{lane_x, lane_y, lane_w, lane_h};
    paint_cell_rect(cr, lane, palette().ground);
    paint_checker_rect(cr, lane, lane_x, lane_y, palette().hilight,
                       palette().ground);

    // THE BODY — the thumb between its two arrow buttons, the ground under a
    // PLAIN RAISED edge, the lane's full height (Windows' scroll-bar thumb;
    // no grip). Each side stops at its button's inner edge, the bridge's own
    // interval (trim_bridge_gap, the one owner, so the painted body and the
    // published bridge are one number) — empty in the narrow case (architect
    // 2026-10-03) — WHEREVER THE BUTTON STANDS (architect 2026-10-05): a
    // button sliding off an edge takes the body's side edge with it, the clip
    // above cutting both, so the bar never grows into a button's room. A side
    // whose edge lies farther off than the edge's own thickness (two relief
    // lines) is held there, outside the clip, which keeps the rect's
    // coordinates in cairo's range at any zoom; a window wholly off one side
    // paints no body.
    const int btn_w = trim_arrow_button_w_px();
    const TrimBridgeGap gap = trim_bridge_gap(bc, ec, btn_w);
    const int run = 2 * relief_line_px();
    const int body_lo = std::max(gap.lo, -run);
    const int body_hi = std::min(gap.hi, lane_w + run);
    if (body_hi > body_lo) {
        const GuiRect body{lane_x + body_lo, lane_y, body_hi - body_lo,
                           lane_h};
        paint_cell_rect(cr, body, palette().ground);
        paint_relief_plain_raised(cr, body);
    }

    // THE TWO ARROW BUTTONS, from the ONE rect owner (trim_endcap_rect: the
    // edge anchoring and the narrow rule), painted over the body and
    // published for the hit as painted — each cut to the lane's painted
    // width, the clip the pixels take, so a button overrunning the lane
    // claims its visible columns alone and the inert gutter claims nothing.
    // A BUTTON SLIDES OFF THE EDGE (architect 2026-10-05): it paints while
    // any of its columns is on the lane, clipped there, whether or not its
    // bound is in view, and one the clip empties paints and publishes
    // nothing. A PARTLY VISIBLE BUTTON IS HITTABLE IN EXACTLY ITS VISIBLE
    // COLUMNS (architect 2026-10-06): the rect published is lane_cut's, the
    // same columns the clip lets paint, over the lane's whole height, and
    // hit_test_trim_endcap tests that rect as it is — so no painted pixel of
    // a button is ever unclickable, and no unpainted one answers.
    // A TRIM WINDOW WHOLLY OFF ONE SIDE PAINTS NOTHING AND GETS NO CUE
    // (architect 2026-10-06): no button, no body, an empty bridge. The
    // dithered lane already says "everything in view is trimmed off", no
    // period idiom shows which side the window lies on, and the framing
    // double-click brings it back.
    const auto lane_cut = [&](GuiRect r) {
        const int lo = std::max(r.x, lane_x);
        const int hi = std::min(r.x + r.w, lane_x + lane_w);
        r.x = lo;
        r.w = std::max(0, hi - lo);
        return r;
    };
    const GuiRect begin_r = trim_endcap_rect(true, lane_x, bc, ec, trim_bar);
    if (lane_cut(begin_r).w > 0) {
        paint_trim_arrow_button(cr, begin_r, /*points_left=*/true,
                                pressed == TrimPressedCap::Begin);
        if (out_hit) out_hit->begin = {true, lane_cut(begin_r)};
    }
    const GuiRect end_r = trim_endcap_rect(false, lane_x, bc, ec, trim_bar);
    if (lane_cut(end_r).w > 0) {
        paint_trim_arrow_button(cr, end_r, /*points_left=*/false,
                                pressed == TrimPressedCap::End);
        if (out_hit) out_hit->end = {true, lane_cut(end_r)};
    }

    // THE BRIDGE'S PUBLICATION is the body between the two buttons' inner
    // edges, clipped to the lane's painted width (trim_bridge_gap, the
    // shared owner), so the pair drag's band and the buttons never overlap.
    if (out_hit) {
        out_hit->published = true;
        out_hit->lane      = lane;
        out_hit->bridge_lo = lane_x + std::max(gap.lo, 0);
        out_hit->bridge_hi = lane_x + std::min(gap.hi, lane_w);
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

// Shared flag iteration used by render_flags and its phase-reset analogue.
// Invokes `emit(i, left_x)` for EVERY visible marker IN STORE ORDER — which is
// also the PAINT order, and therefore the occlusion order: LATER OVER EARLIER,
// with no other occlusion management of any kind (row 5, 2026-08-01). The
// ascending-x stable sort that used to run here is GONE with the z-order it
// served: the old flags lifted selected shapes above unselected and tie-broke by
// column, and both of those rules retired when selection became a colour swap
// and the marker-text lane's arbitration was deleted.
//
// THE PAINT/HIT INVARIANT. `left_x` — the marker's painted pixel column — is
// computed ONCE here and is the box's LEFT EDGE (the composite shows the stem
// standing on that same column). The painter fills from it, the hit rect is
// published from it, and the stem is published at it, so all three are one
// number by construction.
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

    // THE CULL IS ASYMMETRIC BECAUSE THE BOX IS. A flag opens at its column and
    // runs RIGHTWARD, so a marker to the LEFT of the viewport can still reach
    // into it (by up to a full box width) while a marker right of the last
    // column can reach in by its LEFT BORDER alone. The left margin is a width
    // BOUND rather than the real width, which is not known until the label is
    // shaped; the caller supplies it (see cull_width_px above).
    //
    // THE RIGHT CULL IS THE PAINTED EXTENT (architect 2026-09-26): a flag is
    // admitted iff its box, LEFT BORDER INCLUDED, intersects the waveform's
    // columns [0, w) — col - border_w < w, the border standing outside the
    // fill on the column's left (marker_flag_border_px, render.h). So a marker
    // at grid point w (one past the last column w - 1: the song's last
    // half-column at the right wall, or a marker just past the viewport's end
    // mid-song) is admitted and paints its left border ALONE, on the last
    // column(s) — every caller clips its paint and its hit rect to [0, w), so
    // the fill, the text and the cells fall outside and nothing paints in the
    // leftover strip a non-multiple-of-16 window leaves beside w. Its STEM is
    // not published (the callers gate the stem stash to [0, w) at both edges,
    // stem_column_on_waveform: a column past the last, or left of the first,
    // has no pixel of its own), and source_frame_off_right_edge
    // (warp_frame_map_view.h) still refuses AUTHORING there — a border is not
    // the marker's column. A marker whose border would stand at or past w is
    // culled (at gui_scale 100, one border column, that is w + 1 and beyond;
    // a wider border admits as many columns more as it reaches back). The
    // column is displayed_column_at's — the refusal's own rounding over the
    // same q (the displayed span over the plate width recovers q exactly,
    // viewport_end_sample) — so the painter and the refusal cannot disagree
    // on one basis. The sample compare below is only a PREFILTER (it keeps
    // the int cast bounded), set one column wider than the border's reach so
    // it never drops a marker the column test would admit.
    const int border_w = marker_flag_border_px();
    const double cull_lo = static_cast<double>(viewport_start_sample) -
                           cull_width_px * samples_per_pixel;
    const double cull_hi =
        static_cast<double>(viewport_end_sample) +
        static_cast<double>(border_w + 1) * samples_per_pixel;
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
        // The cull — the note above: the border's first column is col - border_w.
        if (col - border_w >= waveform_width) continue;
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

// The two cells LAID OUT on `font`: each one's shaped run, each one's FILL
// width (the flag's own two pads plus the token) and the whole run's painted
// extent — both seam columns and both fills — or all zeroes where no cells
// paint. THE ONE MEASURER. The flag pass, the bound field's anchor
// (committed_cell_seam_off) and the editor's RE-PAINT of whatever rides its
// unrolled right edge all lay them out through this one body, off the one
// composer and the one font, so no two of them can disagree about where a
// cell begins or how wide it is.
struct IterCellLayout {
    bool                  present = false;
    text_shape::ShapedRun lower_run;
    text_shape::ShapedRun upper_run;
    int                   lower_w = 0;   // fill width: two pads + the token
    int                   upper_w = 0;
    int                   span_w  = 0;   // both seams + both fills, 0 with none
};

static IterCellLayout measure_iter_cells(const GuiFont& font,
                                         const IterCellText& cells) {
    IterCellLayout l;
    if (!cells.present) return l;
    const int pads = marker_flag_pad_left_px() + marker_flag_pad_right_px();
    l.present   = true;
    l.lower_run = text_shape::shape_text_run(font, cells.lower);
    l.upper_run = text_shape::shape_text_run(font, cells.upper);
    l.lower_w   = pads + static_cast<int>(std::nearbyint(l.lower_run.width_px));
    l.upper_w   = pads + static_cast<int>(std::nearbyint(l.upper_run.width_px));
    l.span_w    = 2 * marker_flag_border_px() + l.lower_w + l.upper_w;
    return l;
}

// THE FLAG'S KIND (architect 2026-10-04, reopening 2026-10-03's one flag
// colour for every kind): which of the theme's four flag pairs a box wears —
// WARP and PHASE RESET, co-equal, the authoring columns' flags by their
// column, and the `h` view's ADDED and REMOVED diff halves. The palette
// block's marker-lane paragraph (render.h) owns the look.
enum class GuiFlagKind { Warp, PhaseReset, Added, Removed };

// The resolved paint of ONE marker flag box (the palette block's marker-lane
// paragraph, render.h, owns the look): its face, its label's ink — or the
// disabled emboss — and the stem. The OUTLINE is no part of it: every flag's
// is the theme's DkShadow, selected or not (palette().dk_shadow at each box
// painter).
struct FlagFace {
    GuiColor face;
    GuiColor label;
    bool     embossed;    // DISABLED and unselected: THE DISABLED EMBOSS
    GuiColor stem;
    bool     has_stem;
};

// A kind's face and its selected face, off the theme's roles.
struct FlagPair {
    GuiColor face;
    GuiColor selected;
};
FlagPair flag_pair(GuiFlagKind kind) {
    const GuiPalette& p = palette();
    switch (kind) {
        case GuiFlagKind::Warp:
            return {p.warp_flag, p.warp_flag_selected};
        case GuiFlagKind::PhaseReset:
            return {p.phase_reset_flag, p.phase_reset_flag_selected};
        case GuiFlagKind::Added:
            return {p.added_flag, p.added_flag_selected};
        case GuiFlagKind::Removed:
            return {p.removed_flag, p.removed_flag_selected};
    }
    return {p.warp_flag, p.warp_flag_selected};
}

// THE ONE LADDER for every flag box — both marker columns, their bound cells,
// the `h` view's diff flags, the editor's riding cells and the editor's own
// box (architect 2026-10-03, the flat flag; the kinds 2026-10-04): DISABLED
// wins (the theme's ground, the label embossed, no stem), then INVALID, which
// WEARS THE REMOVED PAIR (one red for both, the context telling them apart:
// invalid while authoring, removed in `h`), then the KIND's own pair, the
// stem in the face. ONE LABEL PAIR FOR EVERY KIND: `flag_label` on a face,
// `flag_label_selected` on a selected one. SELECTION IS A BRIGHTER FACE
// (architect 2026-10-03, retiring the white outline): each arm answers
// `selected` with its pair's selected face; and THE SELECTED DISABLED ARM,
// PROVISIONAL (the palette block's THE STATES), is Windows 95's highlighted
// disabled menu item: the KIND's selected face under a FLAT label in the
// theme's Shadow, no emboss, and still no stem.
FlagFace resolve_flag_face(GuiFlagKind kind, bool disabled, bool red,
                           bool selected) {
    const GuiPalette& p = palette();
    FlagFace f;
    if (disabled) {
        f.face     = selected ? flag_pair(kind).selected : p.ground;
        f.label    = p.shadow;   // the emboss's word ink, or the flat GrayText
        f.embossed = !selected;  // show_embossed_run on the ground only
        f.stem     = f.face;
        f.has_stem = false;      // NO STEM EVER for a disabled marker
        return f;
    }
    const FlagPair pair = flag_pair(red ? GuiFlagKind::Removed : kind);
    f.face     = selected ? pair.selected : pair.face;
    f.label    = selected ? p.flag_label_selected : p.flag_label;
    f.embossed = false;
    f.stem     = f.face;
    f.has_stem = true;
    return f;
}

// THE FLAT BOX'S OUTLINE AND FACE (architect 2026-10-03; the geometry at
// marker_flag_edge_h_px and marker_flag_border_px, render.h): in `outline`
// the left border column OUTSIDE the face at [x - border_w, x), the top and
// bottom rows INSIDE the band across the face's columns and the run's closing
// column at [x + w, x + w + border_w) when `closes` — and the face between
// them. The left column is a SEAM a box may share with the box to its left;
// it takes the one outline colour like the rest, the theme's DkShadow on a
// flag box and the editor's black frame on the field. The box keeps the
// band's height and its width. Aliased, integer rects, like everything in
// this lane.
static void paint_flat_flag_box(cairo_t* cr, const GuiRect& lane, int x, int w,
                                int border_w, int edge_h, bool closes,
                                GuiColor outline, GuiColor face) {
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    set_palette_source(cr, outline);
    cairo_rectangle(cr, x - border_w, lane.y, border_w, lane.h);
    cairo_rectangle(cr, x, lane.y, w, edge_h);
    cairo_rectangle(cr, x, lane.y + lane.h - edge_h, w, edge_h);
    if (closes) cairo_rectangle(cr, x + w, lane.y, border_w, lane.h);
    cairo_fill(cr);
    set_palette_source(cr, face);
    cairo_rectangle(cr, x, lane.y + edge_h, w, lane.h - 2 * edge_h);
    cairo_fill(cr);
    cairo_restore(cr);
}

// THE STEM CROSSES THE BOX'S BOTTOM OUTLINE (architect 2026-10-03): the
// marker's stem leaves the box's LEFTMOST FACE COLUMN `x` and runs down over
// the outline's bottom rows into the well and the canvas, where
// paint_marker_stems continues it (waveform_stem_band) — one same-colour
// column, waveform_line_px() wide like the stem itself. Painted by the box's
// own painter because those rows are the lane's; a box with no stem (a
// disabled one) leaves its outline whole. THE FLANKS IN THESE ROWS
// (architect 2026-10-05) are the box's own outline — its left border column
// and the bottom row beside the stem — so this paints only the stem; the
// well's rows below are paint_marker_stem_flanks' (fill_stem_flanks).
static void paint_flag_stem_crossing(cairo_t* cr, const GuiRect& lane, int x,
                                     int edge_h, GuiColor stem) {
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    set_palette_source(cr, stem);
    cairo_rectangle(cr, x, lane.y + lane.h - edge_h, waveform_line_px(),
                    edge_h);
    cairo_fill(cr);
    cairo_restore(cr);
}

// THE FLAG LABEL — the one body every flag box's text goes through: `run` at
// (x, baseline) in the face's label ink (the selected label on a selected
// face), or embossed (show_embossed_run) when the face is disabled and
// unselected. The run carries its own font.
static void paint_flag_label(cairo_t* cr, const text_shape::ShapedRun& run,
                             double x, double baseline, const FlagFace& face) {
    if (face.embossed)
        show_embossed_run(cr, run, x, baseline);
    else {
        set_palette_source(cr, face.label);
        text_shape::show_shaped_run(cr, run, x, baseline);
    }
}

} // namespace

// The phase-reset lead-in ring's colour (declaration in render.h): the ladder
// above asked for a LIVE reset's stem on the same class and selection bits the
// flag pass hands it, so the ring can never pick a colour its stem would not.
// It stands outside the file's anonymous namespace so paint_handler.cpp
// reaches it; the ladder it calls stays file-local.
GuiColor phase_reset_stem_color(bool red, bool selected) {
    return resolve_flag_face(GuiFlagKind::PhaseReset, /*disabled=*/false, red,
                             selected).stem;
}

namespace {

// THE MARKER'S BOXES IN PAINTED ORDER, RANKED: the flag box, then the lower
// bound cell, the upper bound cell. That is the one
// left-to-right order this pass paints in, the editor's riding run re-paints
// in, and FlagHitRect's two boundaries collapse along — and ranking it is
// what lets ONE COMPARISON express the suppression for both editor kinds
// (SuppressedBox, render.h): a box belongs to this pass iff it stands LEFT of
// the edited one. Past the last rank sits kFlagBoxRankNone, the answer for
// every marker no editor stands on.
static int flag_box_rank(MarkerCell c) {
    switch (c) {
        case MarkerCell::Payload: return 0;
        case MarkerCell::Lower:   return 1;
        case MarkerCell::Upper:   return 2;
    }
    return 0;
}
static constexpr int kFlagBoxRankNone = 3;

// ONE BOUND CELL PAINTED — the flag CONTINUED rightward: the flat box
// (paint_flat_flag_box) whose left border is the cell's SEAM COLUMN — the
// outline column the cell shares with the box to its left — then the token on
// the flag's own left pad. Aliased throughout, like every box in this lane.
//
// `closes` ADDS THE RUN'S CLOSING COLUMN (architect 2026-09-25): one outline
// column just past the face, painted iff this cell is the LAST box of its
// marker's run, so a run ends on an outline column as it begins on one and
// every interior seam stays the next box's own single left column.
//
// IT HAS TWO CALLERS AND THAT IS THE POINT: the cached flag pass paints the
// resting cells with it, and the payload editor's own painter re-paints them
// with it at the unrolled field's right edge (render_flag_editor_box), so a
// cell cannot read one way at rest and another under the editor. The FACE is
// the caller's — each cell resolves its own through the one ladder, the
// selected face belonging to the addressed cell alone; the seam column is the
// flag outline, DkShadow whichever box it divides. A cell carries no stem.
static void paint_iter_bound_cell(cairo_t* cr, const GuiRect& lane, int seam_x,
                                  int fill_w, int border_w, int edge_h,
                                  int pad_l, double baseline,
                                  const text_shape::ShapedRun& run,
                                  const FlagFace& face, bool closes) {
    paint_flat_flag_box(cr, lane, seam_x + border_w, fill_w, border_w, edge_h,
                        closes, palette().dk_shadow, face.face);
    paint_flag_label(cr, run, static_cast<double>(seam_x + border_w + pad_l),
                     baseline, face);
}

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

    // THE BOX'S BAND, NOT THE LANE (architect 2026-10-02): every box, label
    // and hit rect below stands on the flag box's rows, one Windows px of
    // ground above it and its bottom the lane's bottom (marker_flag_box_band,
    // architect 2026-10-03), so the box stands on the well's top line and
    // the stem paint_marker_stems draws from the waveform area's first row
    // (waveform_stem_band) continues it with no row between.
    const GuiRect lane = marker_flag_box_band(lanes.marker_lane);
    if (lane.h <= 0) return;

    cairo_save(cr);
    // THE PASS PAINTS INSIDE THE WAVEFORM'S COLUMNS (clip_to_waveform_columns
    // above, architect 2026-09-26): a box running past the last column is cut
    // off there, and a marker at grid point w shows its left border alone on
    // the last column(s). Released by the pass's closing restore.
    clip_to_waveform_columns(cr, top_strip_area.x, waveform_width,
                             top_strip_area.y, top_strip_area.h);
    // THE BODY FACE for the whole pass, named once through the one face
    // owner (gui_font.h); every label is laid out and painted on it, each run
    // carrying it (text_shape.h).
    const GuiFont font = gui_font(GuiFace::Body);

    const int    pad_l    = marker_flag_pad_left_px();
    const int    pad_r    = marker_flag_pad_right_px();
    const int    edge_h   = marker_flag_edge_h_px();
    const int    border_w = marker_flag_border_px();
    const double baseline = static_cast<double>(lane.y) +
                            static_cast<double>(marker_flag_baseline_px());

    iterate_visible_flags_impl(top_strip_area, waveform_width, markers,
                               viewport_start_sample, viewport_end_sample,
                               warp_frame_map, drag_overlay,
                               // `iteration_on` widens the bound by the iter
                               // bracket's own glyphs, which the payload's
                               // own worst case does not cover; the reasoning
                               // is at the bound.
                               marker_flag_max_width_px(iteration_on),
        [&](int i, double left_x) {
            // THE LABEL LAMBDA COMPOSES THE PAINTED FORM ITSELF — each
            // column's own, and the cut (where there is one) is inside it.
            const std::string text = label_of(i);
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, text);

            const int bx = static_cast<int>(std::nearbyint(left_x));
            const int bw = pad_l + pad_r +
                static_cast<int>(std::nearbyint(run.width_px));

            // WHICH OF THIS MARKER'S BOXES THIS PASS PAINTS. Everything LEFT
            // of the edited box stands at rest — nothing moves on that side of
            // an open field — and nothing from the edited box rightward is
            // drawn here, because those boxes RIDE THE FIELD'S RIGHT EDGE and
            // are painted, and published, by render_flag_editor_box instead
            // (the one model, SuppressedBox). A marker no editor stands on
            // ranks past every box and paints its whole run.
            const int suppressed_rank =
                i == suppressed.marker_index ? flag_box_rank(suppressed.cell)
                                             : kFlagBoxRankNone;
            const auto pass_paints = [&](MarkerCell c) {
                return flag_box_rank(c) < suppressed_rank;
            };

            // THE TWO BOUND CELLS (architect 2026-09-04), when the marker
            // paints them and no field is standing in for them. Each cell is a
            // seam column plus a fill of two pads and the shaped token, and
            // `cells_span_w` — the extent of what is actually PAINTED here —
            // is what the hit rect sits past, 0 with no
            // cells so it needs no arm of its own. The cells go one at a
            // time now rather than as a pair: under the UPPER bound's field the
            // lower cell stands and the upper yields, which is the whole point
            // of ranking the boxes.
            const IterCellText cells = cells_of(i);
            const bool paint_lower =
                cells.present && pass_paints(MarkerCell::Lower);
            const bool paint_upper =
                cells.present && pass_paints(MarkerCell::Upper);
            // Nothing is shaped for a marker whose cells do not paint: the one
            // measurer takes empty cells and answers all zeroes. Under the
            // UPPER field it shapes one token it will not paint — the measurer
            // lays the pair out together, which is exactly what makes it ONE
            // measurer, and the cost is a single five-byte run on a single
            // marker for as long as that field stands.
            const IterCellLayout cl =
                measure_iter_cells(font, paint_lower ? cells : IterCellText{});
            const int lower_w = paint_lower ? cl.lower_w : 0;
            const int upper_w = paint_upper ? cl.upper_w : 0;
            const int cells_span_w = (paint_lower ? border_w + lower_w : 0) +
                                     (paint_upper ? border_w + upper_w : 0);
            // THE RUN'S CLOSING COLUMN (architect 2026-09-25): one border
            // column past the run's LAST box, so a short later flag's tail
            // never blends into a long earlier one's. This pass paints it iff
            // it paints the run's last box — no field stands on this marker —
            // because under an open field the run's end is the editor's to
            // paint (the field itself or its riding boxes), and a closing
            // column here would double the field's own left seam into two.
            const bool pass_closes = suppressed_rank == kFlagBoxRankNone;
            const int  close_w     = pass_closes ? border_w : 0;
            // The lower cell's seam column and the upper cell's seam column —
            // where each cell paints, and the two boundaries the hit rect
            // publishes. An ABSENT box's boundary collapses onto the RECT'S
            // RIGHT EDGE (`run_end`, the closing column included), whether it
            // is absent at rest or standing in an open field: absent boxes are
            // always the run's tail, so this is "onto the next" spelled once,
            // and the closing column reads as the last box it closes.
            const int lower_x   = bx + bw;
            const int upper_x   = paint_lower ? lower_x + border_w + lower_w
                                              : lower_x;
            const int run_end   = bx + bw + cells_span_w + close_w;

            // The three bits the one ladder reads (resolve_flag_face):
            // disabled WINS over invalid, and selection is the brighter face
            // of either.
            const bool dis = disabled_of(i);
            const bool red = red_set.count(i) > 0;
            const bool sel = selected_set.count(i) > 0;
            // THE SELECTION IS ONE CELL'S (architect 2026-09-05, "light the
            // colour of only the flag that's clicked"; the brighter face since
            // 2026-10-03): a selected marker shows the selected face on its
            // ADDRESSED cell and no other box. The addressed cell is the
            // payload for every selected marker but the focus, whose addressed
            // cell is the axis — and where the axis names a cell this marker
            // does not paint (a bound cell on an owner disabled after its
            // press), the payload takes it, so a selected marker always shows
            // its selection somewhere. Disabled and invalid resolve cell by
            // cell through the same ladder. THE STEM IS THE FLAG BOX'S
            // (architect 2026-10-04, "otherwise it looks disconnected"; the
            // palette block's THE STATES): it leaves from the payload box and
            // wears that box's face, so it takes the selected face only when
            // the payload is the bright cell, and keeps the marker's resting
            // face while a bound cell is the addressed one.
            MarkerCell bright = i == focus_marker ? focus_cell
                                                  : MarkerCell::Payload;
            // THE FALLBACK ASKS WHETHER THE BRIGHT CELL IS SHOWN AT ALL, by
            // this pass OR by the open field standing in for it — never merely
            // whether THIS pass paints it. A field paints the box it edits and
            // asks this very question of its own cell
            // (render_flag_editor_box), so the marker's selection shows there;
            // brightening the flag box as well would mark two boxes at once,
            // and would mark the flag under every bound field — exactly the
            // odd-one-out the one graphic model retired. What is left for the
            // fallback is a cell that does not exist anywhere: a bound cell on
            // an owner disabled after its press. Then the payload takes the
            // selected face, so a selected marker always shows its selection,
            // and shows it once.
            const bool bright_cell_shown =
                bright == MarkerCell::Payload ||
                (i == suppressed.marker_index && bright == suppressed.cell) ||
                cells.present;
            if (!bright_cell_shown) bright = MarkerCell::Payload;
            const auto cell_selected = [&](MarkerCell c) {
                return sel && c == bright;
            };
            // The payload box's face, and with it the marker's stem
            // (face.stem): the one resolution both read.
            const FlagFace face = resolve_flag_face(
                kind, dis, red, cell_selected(MarkerCell::Payload));

            // THE EDITED MARKER'S BOX IS NOT PAINTED HERE — the open editor
            // owns every pixel of it (render_flag_editor_box, which paints the
            // same face at the same lane y: the flag unrolled).
            //
            // THIS IS A COVERAGE FIX, NOT AN OPTIMIZATION (bug, architect
            // 2026-08-02: "typing leaves the old text painted"). The overlay
            // used to be drawn straight over this box on the assumption that it
            // always covered it, which held only while the editor's text was at
            // least as wide as the committed label — true at open (the editor
            // shows the FULL payload where the label is capped at nine glyphs,
            // and exactly the committed run, to the column, where it is not)
            // and false the moment the user replaces that auto-selected text
            // with something shorter. The overlay then
            // shrank while THIS box kept its committed width, and the tail of
            // the cached label stayed on screen to the right of the editor —
            // read as a stale-pixel/invalidation fault, but the damage was
            // always correct (the whole strip repaints on every keystroke) and
            // the stale ink was this pass's, one z-layer down.
            //
            // WHAT IS SKIPPED IS THE WHOLE BOX, ITS LEFT BORDER INCLUDED: the
            // editor paints the flag entire (border, fill, top edge), so
            // leaving this pass's border standing would put a dark column
            // beside — or, at the editor's left clamp, inside — a box that
            // already draws its own.
            //
            // Suppressing the box takes ITS HIT RECT WITH IT (the argument is at
            // the publish below). THE STEM IS WHAT SURVIVES: it still paints and
            // still publishes, anchored at the flag's left column, and the editor
            // unrolls from that same column — so the marker keeps its stem for
            // the whole session exactly as it does when idle, and keeps the stem
            // click that goes with it.
            //
            // ONLY THE PAYLOAD FIELD REACHES THIS: the flag box is the marker's
            // LEFTMOST box, so it is suppressed by the payload editor alone,
            // and under a bound field it paints exactly as it does
            // at rest.
            if (pass_paints(MarkerCell::Payload)) {
                // THE FLAT BOX (paint_flat_flag_box): the outline the theme's
                // DkShadow, selected or not, and the face the ladder's.
                //
                // THE LEFT BORDER IS OUTSIDE THE FACE, one column LEFT of the
                // frame column (the geometry clause and the clip-at-the-left-
                // edge answer are at marker_flag_border_px, render.h).
                //
                // OVERLAP READS ONE COLUMN EARLIER. A later marker's box
                // covers an earlier one's tail from its BORDER, so two flags a
                // box-width apart butt up as outline-against-face instead of
                // face-against-face — which is the whole point of an outline
                // in this lane and is why later-over-earlier stays the entire
                // occlusion model. AND THE RUN CLOSES ON ONE TOO (architect
                // 2026-09-25): a short later flag standing over a long earlier
                // one ends on its closing column, so the earlier tail emerging
                // past it is ruled off rather than running face into face.
                //
                // THE CLOSING COLUMN ON A CELL-LESS RUN: the flag box is the
                // run's last box, so it closes on its own outline. With cells
                // the upper cell closes instead (paint_iter_bound_cell's
                // `closes`), and the flag's right side is the lower cell's
                // seam column.
                // THE STEM IS PAINTED AFTER THE BOX, so it breaks the bottom
                // outline row at its own column, selected or not.
                paint_flat_flag_box(cr, lane, bx, bw, border_w, edge_h,
                                    pass_closes && !paint_lower,
                                    palette().dk_shadow, face.face);
                if (face.has_stem)
                    paint_flag_stem_crossing(cr, lane, bx, edge_h, face.stem);
                // The label, on the run just measured — same font, same glyphs,
                // so the box width and the painted text cannot disagree.
                paint_flag_label(cr, run, static_cast<double>(bx + pad_l),
                                 baseline, face);
            }

            // THE BOUND CELLS: the flag CONTINUED rightward, twice, in the
            // flag's OWN state — face, outline and ink all off the same
            // ladder, so a cell reads as another payload of the same flag and
            // not as a second surface. Each cell resolves its own face,
            // because the selected face is the addressed cell's alone
            // (above). The seam is the flag's own left-border column laid on
            // each cell's left edge, the DkShadow outline whichever box it
            // divides: the box to its left is the
            // flag box for the lower cell — painted here whenever the lower
            // is, the payload field suppressing both — and the lower cell for
            // the upper. No budget and no truncation: the token is
            // fixed-width by grammar (kIterCellGlyphs). The lower cell first,
            // then the upper — the bracket's own order. The ANATOMY is the one
            // cell painter's (paint_iter_bound_cell), which the payload
            // editor's re-paint calls with the same faces.
            if (paint_lower) {
                const auto cell_face = [&](MarkerCell which) {
                    // A TIE FOLLOWER'S CELLS TAKE THE DISABLED FACE
                    // (architect 2026-09-19): they show the LEADER's numbers
                    // and are not this marker's to author, which is exactly
                    // what the disabled face already says — the ground and
                    // the embossed label, no new colour and no second rule.
                    // The marker's own `dis` is false wherever `follower` is
                    // true (a disabled marker is no tie member and paints no
                    // cells at all); the FLAG BOX above is untouched and
                    // keeps its live state, the disabled face being about the
                    // cells alone.
                    return resolve_flag_face(kind, dis || cells.follower,
                                             red, cell_selected(which));
                };
                const FlagFace lower_face = cell_face(MarkerCell::Lower);
                // The lower cell never closes a run this pass paints: with no
                // field standing the upper cell follows it, and under the
                // UPPER field the field abuts it on its own left seam (the
                // field's frame, painted over that column).
                paint_iter_bound_cell(cr, lane, lower_x, lower_w, border_w,
                                      edge_h, pad_l, baseline, cl.lower_run,
                                      lower_face, /*closes=*/false);
                // The upper cell is the lower's own right-hand neighbour, so it
                // paints iff nothing has taken it: the UPPER bound's field is
                // the one thing that does, and then the run simply ends here.
                // Painted, it is the run's last box and carries the closing
                // column (pass_closes is true exactly then).
                if (paint_upper) {
                    const FlagFace upper_face = cell_face(MarkerCell::Upper);
                    paint_iter_bound_cell(
                        cr, lane, upper_x, upper_w, border_w, edge_h, pad_l,
                        baseline, cl.upper_run, upper_face, pass_closes);
                }
            }

            // THE SUPPRESSED BOX PUBLISHES NO HIT RECT EITHER (2026-08-02,
            // correcting this pass's first suppression): the rect must match the
            // pixels, which is this stash's whole doctrine, and a box that is not
            // painted has no extent to claim. The earlier reasoning — that the
            // entry was unreachable because the press path resolves
            // app.flag_editor_box first — held only while the editor was at
            // least as WIDE as the committed flag, which is precisely the width
            // assumption this pass removed from the painting. A narrowed editor
            // left a visually BLANK tail between its right edge and the old
            // committed width, and a press there closed the editor as an outside
            // press and then fell through to hit_test_flag IN THE SAME PRESS
            // (input_pointer.cpp), selecting, landing, seeding a double-click or
            // arming a drag from pixels where nothing was drawn.
            //
            // THE FLAG IS CLICKABLE AGAIN ON THE FRAME IT REAPPEARS, and the
            // ordering is what makes that safe rather than a lost click: the
            // press that closes the editor is already consumed, and closing
            // changes the flag cache's suppression fields, which miss its
            // fingerprint and rebuild it — one render_flags pass that paints the
            // box and republishes its rect together, since they are the same
            // pass. So the NEXT press sees a rect that exists exactly where a box
            // now does. There is no frame with one and not the other in either
            // direction.
            //
            // THE STEM IS THE DELIBERATE EXCEPTION and still publishes below: it
            // is still PAINTED for the whole editing session, so it is
            // legitimately hit-testable by the same paint-equals-claim rule that
            // takes the box's rect away.
            //
            // AND THE RIDING BOXES ARE THE SECOND EXCEPTION SINCE 2026-09-05, on
            // that same rule rather than against it: whatever stands right of an
            // open field is painted for the whole session — at that field's right
            // edge, by the editor's own painter — so it is legitimately
            // clickable, and it is THAT painter which publishes its rect, at the
            // pixels it put it on (FlagEditorBox::riding_cells, render.h). What
            // this pass suppresses is the geometry it does not draw; what it must
            // never do is claim it, and neither must anyone else.
            //
            // A MARKER UNDER A BOUND FIELD PUBLISHES HERE ALL THE
            // SAME, because its flag box is still painted: the rect below covers
            // what this pass drew — the flag, plus the lower cell under the upper
            // field — and stops at the edited box's own seam, where the field
            // begins. The two publications are disjoint by construction, so a
            // point falls in exactly one of them.
            if (out_hit_rects && pass_paints(MarkerCell::Payload)) {
                // THE RECT IS THE WHOLE BOX, BORDER INCLUDED — a click on the
                // border is a click on the flag. It is the painted extent, as
                // this stash always was; the border merely made the extent one
                // column wider than the fill.
                //
                // AND THE BOUND CELLS ARE PART OF THAT
                // EXTENT: the rect widens over them, SEAM COLUMNS INCLUDED, so
                // every span is ORDINARY FLAG SURFACE for press, drag and
                // select — one marker, one clickable box. What forks on the
                // span is the press's ADDRESSED CELL and the DOUBLE-CLICK'S
                // editor (hit_test_flag_cell, app_state.cpp), and both fork on
                // the boundaries published beside the rect rather than on a
                // re-derivation, so paint and hit cannot drift. A box standing
                // in an open FIELD contributes nothing and its boundary falls
                // onto the next painted box's: an UPPER cell in its field
                // puts its boundary at the lower cell's end, a
                // LOWER cell in its field puts both at the flag's own
                // right edge — so the claim shrinks to exactly the boxes this
                // pass drew, and the rect's right edge is the last of them.
                //
                // EACH BOUNDARY IS ITS BOX'S SEAM COLUMN: a press ON a seam
                // reads as the box the seam introduces. On a cell-less flag
                // every boundary equals the rect's own right edge, so no point
                // can fall past it.
                //
                // THE CLOSING COLUMN IS INSIDE THE RECT (2026-09-25), its last
                // column, and reads as the box it closes: past the upper
                // boundary on a run with cells (Upper), and under a cell-less
                // flag's two boundaries, which sit at `run_end` past it
                // (Payload). Under an open field the pass paints no closing
                // column and `run_end` is the edited box's own seam, as before.
                FlagHitRect r;
                r.marker_index = i;
                r.x = static_cast<double>(bx - border_w);
                r.y = static_cast<double>(lane.y);
                r.w = static_cast<double>(run_end - (bx - border_w));
                r.h = static_cast<double>(lane.h);
                r.iter_lower_boundary_x =
                    static_cast<double>(paint_lower ? lower_x : run_end);
                r.iter_upper_boundary_x =
                    static_cast<double>(paint_upper ? upper_x : run_end);
                // CLIPPED AS THE PIXELS ARE (the pass's clip above): a box
                // cut off at the last column claims its visible part, and a
                // marker at grid point w claims its border strip alone.
                if (clip_hit_rect_to_waveform_columns(r, top_strip_area.x,
                                                      waveform_width))
                    out_hit_rects->push_back(r);
            }
            // The stem stash is gated to [0, w), both edges
            // (stem_column_on_waveform).
            if (face.has_stem &&
                stem_column_on_waveform(bx - top_strip_area.x,
                                        waveform_width)) {
                // THE STEM STAYS ON THE FILL'S LEFTMOST COLUMN — bx, the
                // marker's own frame column, unchanged by the border standing
                // to its left (the architect's explicit clause, spelled at
                // marker_flag_border_px).
                if (out_stems)
                    out_stems->push_back(
                        MarkerStem{i, static_cast<double>(bx), face.stem});
            }
        });

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

    // The box's band, the live lane's rule: the box stands on the well and
    // the stem continues from the waveform area's first row.
    const GuiRect lane = marker_flag_box_band(lanes.marker_lane);
    if (lane.h <= 0) return;

    cairo_save(cr);
    // The waveform's columns are this pass's clip too, the live lane's rule
    // (clip_to_waveform_columns, architect 2026-09-26).
    clip_to_waveform_columns(cr, top_strip_area.x, waveform_width,
                             top_strip_area.y, top_strip_area.h);
    // The body face for the pass, exactly as render_flag_boxes_impl names it
    // (gui_font.h).
    const GuiFont font = gui_font(GuiFace::Body);

    const int    pad_l    = marker_flag_pad_left_px();
    const int    pad_r    = marker_flag_pad_right_px();
    const int    edge_h   = marker_flag_edge_h_px();
    const int    border_w = marker_flag_border_px();
    const double baseline = static_cast<double>(lane.y) +
                            static_cast<double>(marker_flag_baseline_px());

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
        2.0 * static_cast<double>(pad_l + pad_r) +
        // THREE border columns: the box's own at its left, the SEAM DIVIDER a
        // changed pair carries between its halves (2026-08-20), and the
        // flag's CLOSING column at its right (2026-09-25). A bound must never
        // under-state (the left one merely over-admits by a column), and the
        // widest flag in a commit may be a pair.
        3.0 * static_cast<double>(border_w);

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

            text_shape::ShapedRun run_removed;
            text_shape::ShapedRun run_added;
            int w_removed = 0;
            int w_added   = 0;
            if (f.removed) {
                run_removed = text_shape::shape_text_run(font, f.removed_text);
                w_removed = pad_l + pad_r +
                    static_cast<int>(std::nearbyint(run_removed.width_px));
            }
            if (f.added) {
                run_added = text_shape::shape_text_run(font, f.added_text);
                w_added = pad_l + pad_r +
                    static_cast<int>(std::nearbyint(run_added.width_px));
            }
            // THE SEAM DIVIDER between the two halves, and ONLY when there
            // ARE two: a purely removed or purely added flag is one field with
            // no seam to rule (2026-08-20's experiment; the rationale is at the
            // paint below).
            const int seam_w = (w_removed > 0 && w_added > 0) ? border_w : 0;
            const int bw = w_removed + seam_w + w_added;
            // A flag with neither half is not constructible by the resolver
            // above; the guard keeps a degenerate one from publishing a
            // zero-width claim.
            if (bw <= 0) return;

            const int bx = static_cast<int>(std::nearbyint(left_x));

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
            // disable TOGGLE paints one dimmed half beside one full-strength one
            // and the direction of the toggle reads straight off the flag. Each
            // bit is meaningful exactly when its half is painted; the guards
            // below are the half's own `w_* > 0`, so a bit resting at false on a
            // half that does not exist is never consulted.
            const bool removed_disabled = f.then_effective_disabled;
            const bool added_disabled   = f.now_effective_disabled;

            // EACH HALF THROUGH THE LIVE LANE'S ONE LADDER (resolve_flag_face,
            // architect 2026-10-03), AS ITS OWN KIND (architect 2026-10-04): a
            // removed half the Removed pair, an added half the Added pair — a
            // diff line is never the invalid class — the disabled face (the
            // ground, the label embossed) for a half whose own side disables
            // it, and the selected face on both when the flag is focused or
            // selected. THE LABEL CARRIES THE SIGN too (history_diff_label's
            // bracket, paint_handler.h), saying in words what the face says
            // at a glance.
            const FlagFace removed_face = resolve_flag_face(
                GuiFlagKind::Removed, removed_disabled, /*red=*/false,
                focused);
            const FlagFace added_face = resolve_flag_face(
                GuiFlagKind::Added, added_disabled, /*red=*/false, focused);

            // THE FLAT BOX, the live lane's anatomy (paint_flat_flag_box): ONE
            // outline column at the box's left, outside the face, the top and
            // bottom outline rows across each half, and the flag's CLOSING
            // column at its right (2026-09-25). A changed pair is one flag —
            // one rect, one focus, one revert — whose halves meet on a SECOND
            // outline column, THE SEAM (2026-08-20, standing since
            // 2026-09-02, when the halves were two saturated hues; it stays
            // the anatomy's one seam rule): the added half's own left border,
            // the DkShadow outline like every column of the box.
            if (w_removed > 0)
                paint_flat_flag_box(cr, lane, bx, w_removed, border_w, edge_h,
                                    /*closes=*/w_added == 0,
                                    palette().dk_shadow, removed_face.face);
            if (w_added > 0)
                paint_flat_flag_box(
                    cr, lane, bx + w_removed + seam_w, w_added, border_w,
                    edge_h, /*closes=*/true, palette().dk_shadow,
                    added_face.face);

            // THE TWO LABELS, each on its own half through the one label body
            // (paint_flag_label): its own ink or emboss.
            if (w_removed > 0)
                paint_flag_label(cr, run_removed,
                                 static_cast<double>(bx + pad_l), baseline,
                                 removed_face);
            if (w_added > 0)
                paint_flag_label(
                    cr, run_added,
                    static_cast<double>(bx + w_removed + seam_w + pad_l),
                    baseline, added_face);

            if (out_hit_rects) {
                // THE WHOLE BOX, BOTH BORDERS INCLUDED (the left one and the
                // closing one, 2026-09-25), and a changed pair claims as
                // ONE rect — which is what makes the mode's focus click land on
                // one item however wide it is painted.
                FlagHitRect r;
                r.marker_index = i;
                r.x = static_cast<double>(bx - border_w);
                r.y = static_cast<double>(lane.y);
                r.w = static_cast<double>(bw + 2 * border_w);
                r.h = static_cast<double>(lane.h);
                // NO BOUND CELLS IN THIS MODE, so every
                // boundary is the rect's own right edge and no point can fall
                // past it: the view paints the delta's own two-tone flag and
                // nothing else. The live lane's cells have no twin here:
                // an iteration bracket is session-only and never in a commit,
                // so a diff flag has no bounds to show and the mode's
                // `h`-refused arrows nothing to step.
                r.iter_lower_boundary_x = r.x + r.w;
                r.iter_upper_boundary_x = r.x + r.w;
                // Clipped as the pixels are, the live lane's rule.
                if (clip_hit_rect_to_waveform_columns(r, top_strip_area.x,
                                                      waveform_width))
                    out_hit_rects->push_back(r);
            }
            // The stem stash stays gated to [0, w), both edges, the live
            // lane's rule (stem_column_on_waveform).
            if (stem_column_on_waveform(bx - top_strip_area.x,
                                        waveform_width)) {
                // THE STEM IS THE FACE OF THE HALF IT LEAVES FROM (architect
                // 2026-10-04, the stem belonging to its box): the box's
                // leftmost face column is the removed half's on a changed
                // pair or a removed-only flag, the added half's on an
                // added-only one — its selected face when the flag is focused
                // or selected (the live lane's stem, through the one ladder),
                // and a diff flag is never the invalid class.
                //
                // AND IT READS THE DISABLED AXIS (architect 2026-08-22), on the
                // SINGLE-half flags alone. A removed-only or added-only flag
                // whose one side is disabled publishes NO ENTRY AT ALL — the live
                // lane's rule verbatim, expressed the live lane's way, as an
                // absent entry rather than a bit the consumer re-decides
                // (MarkerStem's contract). A CHANGED PAIR ALWAYS KEEPS ITS STEM,
                // whichever of its halves are disabled: the pair is not a line in
                // a switched-off state, it is a live EDIT being displayed, and a
                // disable toggle is precisely the edit whose stem must not
                // vanish. That is why the test below is on the SINGLE halves
                // and never on both effective bits at once. The stem crosses
                // the box's bottom outline at its leftmost face column, the
                // live lane's rule (paint_flag_stem_crossing).
                const bool pair = (w_removed > 0 && w_added > 0);
                const bool single_disabled =
                    !pair && (w_removed > 0 ? removed_disabled
                                            : added_disabled);
                if (!single_disabled) {
                    const GuiColor stem_c =
                        resolve_flag_face(w_removed > 0 ? GuiFlagKind::Removed
                                                        : GuiFlagKind::Added,
                                          /*disabled=*/false, /*red=*/false,
                                          focused).stem;
                    paint_flag_stem_crossing(cr, lane, bx, edge_h, stem_c);
                    if (out_stems)
                        out_stems->push_back(
                            MarkerStem{i, static_cast<double>(bx), stem_c});
                }
            }
        });

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

namespace {
    // The waveform's configured maximum height in AUTHORED px — the device
    // config's `max_waveform_height`, 0 meaning no maximum. Installed by
    // set_max_waveform_height_px at the scale's two application points (the
    // contract is at the declaration, render.h). 500 is construction state,
    // the templates' value; startup installs the config's before any read.
    int    g_max_waveform_height_px = 500;
} // namespace

void set_max_waveform_height_px(int authored_px) {
    g_max_waveform_height_px = authored_px;
}
int waveform_max_h_px() {
    if (g_max_waveform_height_px <= 0) return std::numeric_limits<int>::max();
    return scaled_px(g_max_waveform_height_px, 1);
}

// -- The palette's chokepoints (the contract is at their declaration,
// render.h) ---------------------------------------------------------------

void set_palette_source(cairo_t* cr, GuiColor c) {
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
}
void set_waveform_source(cairo_t* cr, GuiColor c) {
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
}

// -- The active palette (the contract is at its declaration, render.h) -----

namespace {
    // Construction state, never painted: gui_main installs the device
    // config's palette before the window exists (the gui_scale shape).
    GuiPalette        g_palette{};
    uint64_t          g_palette_generation = 0;
    WaveformPlateInks g_plate_inks{};
} // namespace

const GuiPalette& palette() { return g_palette; }
uint64_t palette_generation() { return g_palette_generation; }
WaveformPlateInks waveform_plate_inks() { return g_plate_inks; }

void install_palette(const DeviceConfig& cfg) {
    // The key arrived through its one grammar (the config's reader or the
    // settings editor's commit, is_theme_key), so the lookup has no producer
    // of a miss (theme_words asserts it). Every field is filled off the role
    // table, the one enumeration (theme_file.h), so a role cannot be read and
    // not painted.
    const GuiThemeWords& w = theme_words(cfg.theme);
    GuiPalette p{};
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i)
        p.*(kGuiThemeRoles[i].member) = hex(w[i]);
    g_palette = p;
    static constexpr std::size_t kInk     = theme_role_index("waveform_ink");
    static constexpr std::size_t kOutline = theme_role_index("waveform_outline");
    static_assert(kInk < kGuiThemeRoleCount && kOutline < kGuiThemeRoleCount);
    g_plate_inks = WaveformPlateInks{w[kInk], w[kOutline]};
    ++g_palette_generation;
}

void show_embossed_run(cairo_t* cr, const text_shape::ShapedRun& run,
                       double x, double baseline) {
    const double off = static_cast<double>(relief_line_px());
    set_palette_source(cr, palette().hilight);
    text_shape::show_shaped_run(cr, run, x + off, baseline + off);
    set_palette_source(cr, palette().shadow);
    text_shape::show_shaped_run(cr, run, x, baseline);
}

// (THE TIP-DOWN TRIANGLE MASK IS GONE — 2026-08-02. build_triangle_mask,
// playhead_triangle_mask and their two file-scope cache globals built an
// antialiased A8 silhouette (2H-1 by H) that render_playhead's draw_triangle
// branch stamped; row 5 retired the cursor triangle for the MARKER lane's
// aliased head and left every caller passing false, so the whole cluster was
// unreachable. The geometry it anchored survives at its exact values in
// render.h — waveform_inset_px() and playhead_half_px(), each spelling its own
// derivation now.)

// -- The flag editor's unrolled box ---------------------------------------

// WHERE ONE BOUND CELL'S SEAM COLUMN STANDS on the committed flag, as an
// offset from the flag fill's left edge — the bound editor's anchor, and the
// whole of what that anchor needs. Off the same eligibility, the same tokens
// and the same font the flag pass lays the cells out with (warp_iter_cells or
// phase_iter_cells, `phase` deciding which — the bound editor is both columns'
// since 2026-09-09), so the field opens on exactly the column the resting
// cell's seam stands on.
//
// IT ANSWERS WHERE, NOT HOW WIDE. It used to publish the cell's fill width too,
// which the field was PINNED to; that pin is retired (architect 2026-09-05 —
// the bound field takes the payload field's one width rule, its box
// being its own two pads and its own run like it), so the width has no
// reader left and the struct went with it. The lower cell's anchor is the flag's
// own right edge and the upper's is past the lower cell, which is the only
// thing the side decides here; where no cells paint at all the answer is the
// flag's right edge, unreachable because the open asked (enter_iter_bound_edit)
// and a keyboard-modal editor freezes the mode bit.
static int committed_cell_seam_off(const AppState& app,
                                   const GuiFont& font, bool phase,
                                   int idx, MarkerCell side,
                                   bool iteration_on) {
    const std::vector<GuiWarpMarker>&       mv  = app.warpmarkers.markers();
    const std::vector<GuiPhaseResetMarker>& pmv =
        app.phaseresetmarkers.markers();
    // THE PAINTED COMPOSER ON EACH COLUMN, never the uncut one: this shapes
    // exactly what the flag pass shaped, or the field would open at a column
    // no cell stands on (the declaration of flag_display_text says why this
    // is the one place a wrong composer hides).
    const std::string label = phase ? std::string(kPhaseResetLaneToken)
                                    : flag_display_text(mv, idx);
    const text_shape::ShapedRun run = text_shape::shape_text_run(font, label);
    const int pads   = marker_flag_pad_left_px() + marker_flag_pad_right_px();
    const int flag_w = pads + static_cast<int>(std::nearbyint(run.width_px));
    const IterCellLayout cl = measure_iter_cells(
        font, phase ? phase_iter_cells(pmv, idx, iteration_on)
                    : warp_iter_cells(mv, idx, iteration_on));
    if (cl.present && side == MarkerCell::Upper)
        return flag_w + marker_flag_border_px() + cl.lower_w;
    return flag_w;
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

    // THE BOX'S BAND, the flag pass's own (marker_flag_box_band): the field
    // stands exactly where the resting box does.
    const GuiRect lane = marker_flag_box_band(top_marker_row_area(app));
    if (lane.w <= 0 || lane.h <= 0) return;

    cairo_save(cr);
    // The body face, named once through the one face owner (gui_font.h).
    const GuiFont font = gui_font(GuiFace::Body);

    // THE FULL, UNTRUNCATED pending — the unroll's whole point. The scale cap
    // is a PAINTED-FLAG rule; an editor shows what it is editing.
    const text_shape::ShapedRun run =
        text_shape::shape_text_run(font, ed.pending);
    std::vector<double> byte_x =
        text_shape::byte_offsets_px(run, ed.pending.size());

    const int pad_l    = marker_flag_pad_left_px();
    const int pad_r    = marker_flag_pad_right_px();
    const int edge_h   = marker_flag_edge_h_px();
    const int border_w = marker_flag_border_px();
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
    // The pad is two authored pixels and the caret one, so the borrow leaves
    // the fill a pad on that side at every scale but the smallest, where both
    // floor to one column: the viewport then ends on the box's own right edge
    // and the caret's column is the box's last fill column — visible ink, not
    // a lost one, so the travel arithmetic below needs no floor of its own.
    // The TEXT VIEWPORT is what widens by the borrowed column; the box never
    // does.
    // One authored pixel, scaled like every other row-5 length, with the
    // tree's own per-metric floor: it rounds to 0 at gui_scale 50, which would
    // leave the caret no column to stand in.
    const int caret_px = scaled_px(1.0, 1);

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
    // exactly the column its resting flag is, and a marker at grid point w
    // shows the same left border alone whether it rests or is being edited.
    // It holds for everything this painter draws — the box, its text, the
    // caret and the riding cells — until the closing restore.
    const int clip_w = basis.area_w > 0 ? basis.area_w : area.w;
    // (The stem below the box is paint_marker_stems', in the marker's own
    // selected face; this painter draws only its crossing of the frame's
    // bottom rows, below. A refusal recolours neither, architect 2026-10-03.)
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
    // EVERY FIELD OPENS WHERE ITS CELL SITS. The BOUND field opens past its
    // own cell's seam divider (committed_cell_seam_off), so the field's fill
    // begins on exactly the column the resting cell's fill begins on (the
    // divider itself is painted below, outside this fill on its left, which
    // is that border's own geometry), the marker's boxes to its LEFT
    // standing where they are. The PAYLOAD field opens on the marker's own
    // column, the flag unrolling from itself. In every case the boxes to the
    // RIGHT of the field yield to it and RIDE ITS EDGE below, which at the open
    // is exactly where they rest: the field buys no column of its own (the
    // borrow above), so `bx + box_w` is the committed box's own right edge
    // until something is typed.
    //
    // THE FIELD'S CELL — which of the marker's boxes this field stands in for —
    // is read off the ONE derivation the flag pass's suppression is read off
    // (suppressed_flag_box), so the box that yields and the box that opens have
    // exactly one answer between them in the product. It is the payload's own
    // Payload for the flag editor and the session's side for a bound editor.
    const MarkerCell field_cell = suppressed_flag_box(app).cell;
    const int anchor_off =
        bound_kind ? committed_cell_seam_off(app, font, phase, idx,
                                             field_cell, iteration_on) +
                         border_w
                   : 0;

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
    // gives for a flag box that is half off the edge at rest. THE LEFT BORDER
    // SITS WHERE THE RESTING FLAG'S DOES — `bx - border_w`, outside the fill
    // on the column's left, on the same column mapping — so opening the
    // editor on a marker at grid point w changes no pixel of the border it
    // shows there (at the laptop's 138 % the one column w - 1; at the
    // tablet's 275 % the three columns w - 3 .. w - 1).
    const int bx = area.x + col + anchor_off;

    // The text VIEWPORT inside the box: the band the run is clipped to, and the
    // width the view offset is measured against. The caret column belongs to it
    // (a caret at the end must be inside the clip to be seen), and THIS IS
    // WHERE THE BORROW IS SPENT, on every kind: the viewport reaches one column
    // INTO the right pad — the column the box above deliberately does not buy —
    // so a caret at end-of-text is inside the clip with the run standing
    // exactly where the committed text stands. Where the pad has floored to the
    // caret's own width (gui_scale 50) the borrow takes the whole pad and the
    // viewport ends on the box's right edge, the caret's column being the box's
    // last fill column.
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
    const double baseline = static_cast<double>(lane.y) +
                            static_cast<double>(marker_flag_baseline_px());

    // THE MARKER'S OWN STATE, through the one ladder (resolve_flag_face) —
    // for the box, which is the marker's SELECTED face (below), for the STEM
    // under the payload field (the payload box's own face), and for the cells
    // riding the field's right edge, which keep their resting anatomy.
    const bool dis = phase ? pmv[static_cast<size_t>(idx)].disabled
                           : effective_disabled(mv, idx);
    // The column's kind, the flag pass's own (render_flags /
    // render_phase_reset_flags), so the field and the boxes riding it wear
    // the pair their resting twins wear.
    const GuiFlagKind kind = phase ? GuiFlagKind::PhaseReset
                                   : GuiFlagKind::Warp;
    // The class's red is the COLUMN'S OWN paint cue, the set the resting flag
    // pass for this column reads, so the boxes riding the field wear the
    // removed face (the invalid one) their resting twins wear on every
    // column.
    const bool red_class =
        phase
            ? phase_reset_red_flag_set_cached(app).red.count(idx) > 0
            : warp_red_flag_set_cached(
                  app, audio.sample_rate(),
                  static_cast<long>(audio.total_frames())).red.count(idx) > 0;
    // THE SELECTION IS THE ADDRESSED CELL'S ALONE (the flag pass's own rule,
    // render_flags). Every box in the riding run below asks the question of
    // its own cell — which answers no on every one of them, each open having
    // seated the axis on the cell it edits — so the riding cells never take
    // the selected face while the field stands, the field being where the
    // marker's selection shows.
    const bool sel = app.selected_markers.count(idx) > 0;
    const MarkerCell bright = idx == app.last_selected_marker
                                  ? app.addressed_cell : MarkerCell::Payload;
    const auto cell_selected = [&](MarkerCell c) { return sel && c == bright; };
    // THE FIELD IS THE SELECTED FLAG OPENED FOR EDIT (architect 2026-10-03,
    // set BX): the ladder's SELECTED answer for the edited marker — its
    // kind's selected face, or `removed_flag_selected` over an invalid marker
    // (a disabled marker's the provisional selected-disabled arm's face, an
    // edit field never being embossed); under the payload field its stem is
    // in that face (below).
    const FlagFace face =
        resolve_flag_face(kind, dis, red_class, /*selected=*/true);
    // DOES THE FIELD CLOSE THE RUN (architect 2026-09-25: every marker's run
    // ends on ONE outline column on its rightmost box)? Iff nothing rides past
    // it — the UPPER field, the marker's last box by rank, or a payload field
    // on a marker with no cells. Otherwise the last riding box closes (below)
    // and the field's right side is the first riding cell's own seam column,
    // never a closing column plus a seam. The answer is the riding run's own
    // composer output (`ride_text`, read again below), so the two cannot
    // disagree about which box ends the run.
    const int  field_rank = flag_box_rank(field_cell);
    const IterCellText ride_text =
        field_rank >= flag_box_rank(MarkerCell::Upper) ? IterCellText{}
        : phase ? phase_iter_cells(pmv, idx, iteration_on)
                : warp_iter_cells(mv, idx, iteration_on);
    const bool ride_cells = ride_text.present;

    // 1. THE BOX IS WINDOWS 95's IN-PLACE LABEL EDIT (architect 2026-10-03,
    //    Explorer's F2 rename, Acid Pro's track-name editor; the palette
    //    block's editing paragraph): a FLAT box on the flag's own outline
    //    geometry (paint_flat_flag_box — the left border column outside the
    //    face, the top and bottom rows inside the band, the closing column
    //    when the field ends the run) framed in BLACK (kFlagEditorFrame,
    //    Windows' WindowFrame), its face the marker's SELECTED face (`face`,
    //    above; the field pair plays no part). A refused Enter recolours
    //    nothing (text_editor::refuse selects the whole text; the owner's
    //    card says why). The pads either side of the text are Windows' field
    //    margin strips. The left border column is the flag's own for the
    //    payload editor and the SEAM column for the bound field — "the
    //    outline outside the face on its left" either way. Since the editor
    //    opens on any store index (enter_top_flag_edit), disabled included, a
    //    disabled marker's field is this same box: an edit field is never
    //    embossed.
    paint_flat_flag_box(cr, lane, bx, box_w, border_w, edge_h,
                        /*closes=*/!ride_cells, kFlagEditorFrame, face.face);
    // THE STEM FOLLOWS THE PAYLOAD BOX (architect 2026-10-04, "otherwise it
    // looks disconnected"). Under the PAYLOAD field the field IS the payload
    // box opened, and the open seats the axis there (set_single_selection
    // resets AppState::addressed_cell to the payload), so the payload is the
    // bright cell and the stem wears the box's selected face, crossing the
    // frame's bottom rows from the box's leftmost face column
    // (paint_flag_stem_crossing) — the colour the flag pass publishes for the
    // waveform's run off the same bit. Under a BOUND-CELL field nothing is
    // painted here: the field is a cell and a cell carries no stem, and the
    // flag box stands in the flag pass at rest, the open having seated the
    // axis on the edited cell (enter_iter_bound_edit writes
    // app.addressed_cell = side), so that box — and the stem leaving it —
    // keeps the marker's resting face.
    if (field_cell == MarkerCell::Payload && face.has_stem)
        paint_flag_stem_crossing(cr, lane, bx, edge_h, face.stem);

    // The caret / selection band: the box interior between the frame's top
    // and bottom rows. A text field's caret spans its whole field, and here
    // the field IS the box, so this needs no font-extent solve.
    const int band_y = lane.y + edge_h;
    const int band_h = lane.h - 2 * edge_h;

    // Everything from here paints CLIPPED to the text viewport INSIDE THE
    // FRAME, so a scrolled run, its selection and its caret all stop at the
    // pads and the frame rows instead of bleeding over the box edge into the
    // neighbouring flags or over the frame.
    cairo_save(cr);
    cairo_rectangle(cr, view_x0, static_cast<double>(band_y),
                    view_w, static_cast<double>(band_h));
    cairo_clip(cr);

    // 2. The selection highlight, then 3. the text — THE THEME'S SELECTED PAIR
    //    over the selected face's own label, `flag_label_selected` (architect
    //    2026-10-03, set BX).
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
        set_palette_source(cr, palette().selected_fill);
        cairo_rectangle(cr, ix0, band_y, band_w, band_h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // THE RUN, SHOWN ONCE PER REGION (the ruling in the block above): the
    // whole run in the selected label off the band, the whole run again in
    // the selected text on it, neither reaching a pixel the other painted.
    set_palette_source(cr, palette().flag_label_selected);
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
        set_palette_source(cr, palette().selected_text);
        text_shape::show_shaped_run(cr, run, text_origin_x, baseline);
        cairo_restore(cr);
    }

    // 4. The caret: a blink-gated filled integer column at the cursor's own
    //    byte boundary, AA off. IT IS INK, NOT FIELD, so it stays the box's
    //    TEXT colour, `flag_label_selected`, wherever it lands (architect
    //    2026-10-03), over the selection band included — a caret that changed
    //    colour on crossing a selection edge would be stating something about
    //    the selection rather than about the cursor.
    if (text_editor::cursor_visible_now(ed)) {
        const int cx =
            static_cast<int>(std::nearbyint(text_origin_x + caret_off));
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        set_palette_source(cr, palette().flag_label_selected);
        cairo_rectangle(cr, cx, band_y, caret_px, band_h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    cairo_restore(cr);   // the text-viewport clip

    // THE MARKER'S BOXES TO THE RIGHT OF THE FIELD RIDE ITS EDGE — in the flag
    // pass's own left-to-right order, each wearing its resting anatomy. WHAT
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
    // Each box wears its resting anatomy through the resting painter
    // (paint_iter_bound_cell), and each asks the selected-cell question its
    // resting twin asks — which answers no on every one of them under every
    // kind, each open having seated the axis on the cell it edits, so none
    // takes the selected face while the field stands.
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
        // THE RIDING BOXES KEEP THEIR RESTING ANATOMY, the flag outline round
        // each. The one column the field shares with the first riding box is
        // the field's frame (repainted after the run, below).

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

        // THE RUN'S TWO SEAM COLUMNS, accumulated left to right from the
        // field's own right edge — the same walk the flag pass makes from the
        // flag's, and the same collapse rule: a box that is not in the run
        // leaves its boundary standing where the next one begins, so no point
        // can answer a box with no ink. With everything riding this is exactly
        // the flag pass's `lower_x` / `upper_x` with the field's
        // width in the flag's place.
        const int run_x0 = bx + box_w;
        int cursor_x = run_x0;
        // Each riding cell's SEAM is the flag outline, as the flag pass paints
        // it, where its left neighbour is a riding cell; the first riding
        // box's seam is the field's own right column, repainted in the frame's
        // colour below.
        const int lower_seam = cursor_x;
        const FlagFace lower_face = resolve_flag_face(
            kind, cell_dis, red_class, cell_selected(MarkerCell::Lower));
        if (ride_lower) {
            paint_iter_bound_cell(
                cr, lane, cursor_x, cl.lower_w, border_w, edge_h, pad_l,
                baseline, cl.lower_run, lower_face,
                // Never the run's last box: the upper cell rides after it on
                // every kind that carries the lower.
                /*closes=*/false);
            cursor_x += border_w + cl.lower_w;
        }
        const int upper_seam = cursor_x;
        if (ride_upper) {
            const FlagFace upper_face = resolve_flag_face(
                kind, cell_dis, red_class, cell_selected(MarkerCell::Upper));
            paint_iter_bound_cell(
                cr, lane, cursor_x, cl.upper_w, border_w, edge_h, pad_l,
                baseline, cl.upper_run, upper_face,
                // THE RUN'S LAST BOX, so it closes the run (2026-09-25) —
                // the resting run's own ending, at the field's edge.
                /*closes=*/true);
            cursor_x += border_w + cl.upper_w + border_w;
        }

        // THE RUN IS PUBLISHED AS A FLAG HIT RECT, keyed to the marker being
        // edited: its rect is the WHOLE re-painted run's painted extent, every
        // seam divider and the closing column included (the latter past the
        // upper boundary, so it answers Upper, the box it closes) — the same paint-equals-claim rule the flag
        // rects take — and its two boundaries are the seam columns
        // accumulated above, so hit_test_flag_cell's walk answers
        // Upper or Lower over exactly the pixels that show one, and
        // answers
        // nothing at all for a box that stayed behind in the lane pass. No
        // point in the rect can answer Payload: the run begins ON the lower
        // boundary (which, where the lower cell does not ride, is where the
        // first riding box begins), and the payload's own box is the FIELD,
        // published as `box` and claimed by the caret. Nothing is published
        // where nothing rode — the cold marker_index -1 and the zero rect,
        // which contains no point.
        const int run_w = cursor_x - run_x0;
        if (run_w > 0) {
            FlagHitRect& r = out.riding_cells;
            r.marker_index          = idx;
            r.x                     = static_cast<double>(run_x0);
            r.y                     = static_cast<double>(lane.y);
            r.w                     = static_cast<double>(run_w);
            r.h                     = static_cast<double>(lane.h);
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
        // THE FIELD'S FRAME CLOSES ON ITS OWN RIGHT COLUMN, which is also the
        // first riding cell's seam (the one shared column, its left border):
        // repainted in the frame's colour over the seam the cell painter just
        // laid, so the black frame is whole on all four sides and the field
        // reads as one box.
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        set_palette_source(cr, kFlagEditorFrame);
        cairo_rectangle(cr, run_x0, lane.y, border_w, lane.h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    cairo_restore(cr);   // the font state

    out.valid         = true;
    // THE PUBLISHED BOX IS THE PAINTED BOX, BORDER INCLUDED — the same rule the
    // flags' hit rects take: a press on the border is a press on this editor,
    // and it maps to byte 0 through the nearest-boundary search exactly as a
    // press on the left pad does (the box-is-the-claim clause at FlagEditorBox).
    // A bound field's border is the SEAM DIVIDER and is published with it,
    // so a press on that column seats the caret at byte 0 — which agrees with
    // the resting cell, where the same column reads as that cell rather than
    // as the box left of it.
    //
    // A FIELD THAT CLOSES THE RUN PUBLISHES ITS CLOSING COLUMN TOO
    // (2026-09-25), the box's last column: a press there seats the caret at
    // the end through the same nearest-boundary search a press on the right
    // pad takes, and the I-beam covers every column the field painted.
    //
    // AND IT IS CLIPPED TO THE WAVEFORM'S COLUMNS as its pixels are (the clip
    // above): a field cut off at the last column claims its visible part, a
    // marker at grid point w claims its border strip alone, and a field wholly
    // past the edge publishes an empty box, which contains no point.
    {
        const int x_lo = std::max(bx - border_w, area.x);
        const int x_hi = std::min(bx + box_w + (ride_cells ? 0 : border_w),
                                  area.x + clip_w);
        out.box = GuiRect{x_lo, lane.y, std::max(x_hi - x_lo, 0), lane.h};
    }
    out.text_origin_x = text_origin_x;
    out.byte_x        = std::move(byte_x);
}
