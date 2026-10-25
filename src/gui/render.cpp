#include "render.h"
#include "app_state.h"
#include "audio.h"
#include "canvas_sink.h"    // the canvas painters' one road (GuiCanvasSink)
#include "gui_display_context.h"
#include "gui_font.h"
#include "text_shape.h"
#include "theme_file.h"
#include "palette_file.h"   // kGuiPaletteRoles
#include "cool_edit_derive.h"  // the Cool Edit block's tones off the face
#include "cool_edit_paint.h"   // the canvas column's painters and constants
#include "chrome_derive.h"  // live_chrome_words (the chrome off the ground's mean)
#include "materials.h"      // the surface chokepoint's materials
#include "value_format.h"
#include "warp_frame_map_view.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// kFlagBottomLiftPx now lives in render.h so the strip lane geometry in
// main.cpp and the stem blit in paint_handler.cpp reference the same value.

// The playhead's reach — its cull here and main.cpp's damage box — is the
// head's painted columns, cue_triangle_half_w_px and
// cue_triangle_reach_right_px (render.h), the one extent both read.

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

namespace {
// THE CANVAS'S LINES' RECTANGLES, collected for one sink fill (the grid:
// 2 channels x 3 lines x 2 sides; the center lines: 2).
struct CanvasLines {
    std::array<GuiRect, 16> rects{};
    int count = 0;
    std::span<const GuiRect> span() const {
        return std::span<const GuiRect>(rects.data(),
                                        static_cast<std::size_t>(count));
    }
};
// ONE CANVAS LINE ACROSS THE AREA on area-local row `row`, `t` device rows
// thick, as an aliased rect appended to the lines of one fill and cut to the
// area's rows: rows [row − t / 2, row − t / 2 + t). The grid's and the
// center lines' one row rule (render_canvas, render_center_lines).
void canvas_hline(CanvasLines& lines, const GuiRect& area, int row, int t) {
    const int top = std::max(area.y, area.y + row - t / 2);
    const int bot = std::min(area.y + area.h, area.y + row - t / 2 + t);
    if (bot > top) {
        assert(lines.count < static_cast<int>(lines.rects.size()));
        lines.rects[static_cast<std::size_t>(lines.count++)] =
            GuiRect{area.x, top, area.w, bot - top};
    }
}
} // namespace

// THROUGH THE CANVAS SINK (2026-10-13, canvas_sink.h): on the cairo road the
// one aliased scope, the ground's fill and the lines' one fill; on the GL
// canvas the same rectangles as list records. The stereo divider's rows are
// render_waveform_divider's, a pass of their own over the plate (below).
void render_canvas(GuiCanvasSink& sink, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    sink.begin(/*aliased=*/true);
    const GuiPalette& pal = palette();
    // The ground is the `waveform_canvas` role (the palette's row-6 block),
    // through the waveform's chokepoint (set_waveform_source, render.h — the
    // cairo sink's source).
    sink.rects(std::span<const GuiRect>(&area, 1), pal.waveform_canvas);
    CanvasLines lines;
    // THE LINES UNDER THE WAVEFORM, in render.h's row-6 canvas order: the
    // horizontal grid — t = waveform_line_px() thick, ONE DEVICE PX ("on
    // waveform → unscaled", the class's one inventory), each an aliased
    // integer rect inside its band. THE CENTER LINES STAND OVER THE WAVEFORM
    // (architect 2026-10-09 evening), render_center_lines below, a pass after
    // the plate's blit. NO VERTICAL GRID (architect 2026-10-09 ~18:40, the
    // rule and its reason at render.h's row-6 canvas paragraph).
    const int t = waveform_line_px();
    // THE HORIZONTAL GRID per channel, on the plate's own bands
    // (waveform_channel_band): a line on a row r covers rows
    // [r − t / 2, r − t / 2 + t) — the row r itself at one device px — the
    // zero row being the row the plate's bars straddle; the k-th grid line
    // stands t · nearbyint(k · (H / t) / 4) = nearbyint(k · H / 4) device rows
    // from the zero row, H the channel's half height in device rows — k = 1 ..
    // 3, the fourth on the band's edge not drawn. Each band is its well's
    // interior (no inset, architect 2026-10-09; the two wells 2026-10-16), so
    // H is half a band, every line inside it.
    const auto hline = [&](int row) {
        canvas_hline(lines, area, row, t);
    };
    const WaveformDivider div = waveform_divider(area.h);
    for (int ch = 0; ch < 2; ++ch) {
        const WaveformChannelBand band =
            waveform_channel_band(area.h, div.h(), ch);
        if (band.h <= 0) continue;
        const int zero = band.zero_row();
        const double half_q = static_cast<double>(band.h) / 2.0 / t;
        const int n = kProgramSpec.grid_divisions;
        for (int k = 1; k < n; ++k) {
            const int d = t * static_cast<int>(std::nearbyint(k * half_q / n));
            hline(zero - d);
            hline(zero + d);
        }
    }
    sink.rects(lines.span(), pal.grid);
    sink.end();
}

void render_center_lines(GuiCanvasSink& sink, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    // THE CENTER LINE per channel ON ITS ZERO ROW, OVER THE WAVEFORM'S INK
    // (architect 2026-10-09 evening: "it's supposed to be visible over the
    // waveform. Otherwise, in a project like this with tape hiss, there's
    // never enough zero that the red line would become visible ever"; the
    // order at render.h's row-6 canvas paragraph): one device px, the grid's
    // row rule (canvas_hline), in the palette's `center`, the channels'
    // bands the plate's own (waveform_channel_band), so it stands on the row
    // the plate's bars straddle. Through the canvas sink (2026-10-13), one
    // aliased scope and one fill on the cairo road as before.
    sink.begin(/*aliased=*/true);
    const int t = waveform_line_px();
    const int divider_px = waveform_divider_px();
    CanvasLines lines;
    for (int ch = 0; ch < 2; ++ch) {
        const WaveformChannelBand band =
            waveform_channel_band(area.h, divider_px, ch);
        if (band.h > 0) canvas_hline(lines, area, band.zero_row(), t);
    }
    sink.rects(lines.span(), palette().center);
    sink.end();
}

// THE STEREO DIVIDER (architect 2026-10-16, Cool Edit's two wells; the rule
// and its rows at render.h's waveform_divider, the one geometry owner):
// across the area's columns, the upper well's light bottom line in
// `ce_hilight`, the face row in `face` and the lower well's dark top line in
// `ce_dark`, each its scaled rows, cut to the area's rows — CHROME ON THE
// CANVAS, painted through the sink so the GL canvas draws it from its list
// exactly as the cairo road fills it (canvas_sink.h's head), the frame
// columns' share being paint_canvas_column_frame's. ITS OWN PASS, AFTER THE
// PLATE AND THE CENTER LINES (on_redraw's paint order, both roads): nothing
// legitimate paints on these rows — the plate's bands and the lines stand
// inside the wells, every vertical run is cut per well (waveform_well_rows)
// and every input leaves them alone (point_on_waveform_divider: chrome,
// inert) — so in the steady state the order changes no pixel; it is there
// for A PLATE FROM BEFORE A RESIZE, which stands at its old bands until the
// worker republishes (paint_waveform_plate blits whatever plate is
// published), and whose ink would otherwise cross the live divider's rows.
// Painted last of the two, the live divider is on top whatever plate stands.
void render_waveform_divider(GuiCanvasSink& sink, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    sink.begin(/*aliased=*/true);
    const GuiPalette& pal = palette();
    // THE RING TONES END TO END (render.h's waveform_divider): the light
    // line and the dark one the two ring tones, the face row the ground's
    // flat mean — the strokes the frame columns' share takes in the margins
    // (paint_canvas_column_frame), so no seam stands where they meet.
    const WaveformDivider div = waveform_divider(area.h);
    const auto divider_row = [&](int y, int h, GuiColor color) {
        const int top = std::max(area.y, area.y + y);
        const int bot = std::min(area.y + area.h, area.y + y + h);
        if (bot <= top) return;
        const GuiRect r{area.x, top, area.w, bot - top};
        sink.rects(std::span<const GuiRect>(&r, 1), color);
    };
    divider_row(div.y, div.line, pal.ce_hilight);
    divider_row(div.y + div.line, div.face, pal.face);
    divider_row(div.y + div.line + div.face, div.line, pal.ce_dark);
    sink.end();
}

// -- The relief helpers (the contract at the declaration, render.h) ---------

// THE DIRECT ROAD'S CONDITIONS (the contract is at the declaration).
bool direct_pixel_target(cairo_t* cr, DirectPixelTarget& out) {
    cairo_surface_t* target = cairo_get_target(cr);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) return false;
    if (cairo_get_group_target(cr) != target) return false;
    if (cairo_surface_get_type(target) != CAIRO_SURFACE_TYPE_IMAGE) return false;
    if (cairo_image_surface_get_format(target) != CAIRO_FORMAT_ARGB32)
        return false;
    double ox = 0.0, oy = 0.0;
    cairo_surface_get_device_offset(target, &ox, &oy);
    double sx = 1.0, sy = 1.0;
    cairo_surface_get_device_scale(target, &sx, &sy);
    if (ox != 0.0 || oy != 0.0 || sx != 1.0 || sy != 1.0) return false;
    if (cairo_get_operator(cr) != CAIRO_OPERATOR_OVER) return false;
    cairo_matrix_t m;
    cairo_get_matrix(cr, &m);
    if (m.xx != 1.0 || m.yy != 1.0 || m.xy != 0.0 || m.yx != 0.0) return false;
    if (m.x0 != std::floor(m.x0) || m.y0 != std::floor(m.y0)) return false;
    if (std::fabs(m.x0) > 1e6 || std::fabs(m.y0) > 1e6) return false;
    const int surf_w = cairo_image_surface_get_width(target);
    const int surf_h = cairo_image_surface_get_height(target);
    out.dx = static_cast<int>(m.x0);
    out.dy = static_cast<int>(m.y0);
    out.clip.clear();
    cairo_rectangle_list_t* list = cairo_copy_clip_rectangle_list(cr);
    bool ok = list->status == CAIRO_STATUS_SUCCESS;
    for (int i = 0; ok && i < list->num_rectangles; ++i) {
        const cairo_rectangle_t& r = list->rectangles[i];
        if (r.x != std::floor(r.x) || r.y != std::floor(r.y) ||
            r.width != std::floor(r.width) ||
            r.height != std::floor(r.height)) {
            ok = false;
            break;
        }
        const double x0 = std::max(r.x + out.dx, 0.0);
        const double y0 = std::max(r.y + out.dy, 0.0);
        const double x1 = std::min(r.x + r.width + out.dx,
                                   static_cast<double>(surf_w));
        const double y1 = std::min(r.y + r.height + out.dy,
                                   static_cast<double>(surf_h));
        if (x1 > x0 && y1 > y0)
            out.clip.push_back(GuiRect{static_cast<int>(x0),
                                       static_cast<int>(y0),
                                       static_cast<int>(x1 - x0),
                                       static_cast<int>(y1 - y0)});
    }
    cairo_rectangle_list_destroy(list);
    if (!ok) return false;
    cairo_surface_flush(target);
    out.data = cairo_image_surface_get_data(target);
    out.stride = cairo_image_surface_get_stride(target);
    out.surface = target;
    return out.data != nullptr;
}

void paint_cell_rect(cairo_t* cr, const GuiRect& r, GuiColor c) {
    if (r.w <= 0 || r.h <= 0) return;
    // The path first: the surface chokepoint reads it (render.h).
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    set_surface_source(cr, c);
    cairo_fill(cr);
}

void paint_ink_rect(cairo_t* cr, const GuiRect& r, GuiColor c) {
    if (r.w <= 0 || r.h <= 0) return;
    set_palette_source(cr, c);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_fill(cr);
}

// THE MITRE (architect 2026-10-06, overruling the planner's reading that
// Windows' DrawEdge joins its edges squarely, the dark side owning the corner,
// at every DPI: "Buttons don't have corners like that in real life. A perfect
// square button, like a keyboard key with a relief and bevels, would have a
// smooth straight corner, not a pixelated corner. ... Anything the original
// designers would have drawn with a diagonal if they could, we should." — THE
// CHROME IS SCALABLE). At the TOP-RIGHT and BOTTOM-LEFT corner blocks, where
// the top-left tone meets the bottom-right tone, each line's square (one
// device px, render.h's line rule, since 2026-10-23) is split along its
// diagonal from the ring's outer corner to its inner corner, the top-left
// tone on the half toward the top and left edges, the bottom-right tone on
// the half toward the bottom and right. The top-left and bottom-right corners
// are one tone already and stay square. THE FILLS: the top-left tone is ONE
// square L (the top row and the left column, both corner blocks whole),
// unantialiased; the bottom-right tone ONE mitred L over it (the bottom row
// and the right column, each end cut along the diagonal), antialiased by
// cairo — so every pixel on the diagonal is that tone at its coverage over
// the other, exactly the two tones' blend with no ground in it (two abutting
// antialiased fills would let the ground through their shared seam). The
// anti-aliasing is the renderer's, not a colour (the palette head).
namespace {
void paint_mitred_ring(cairo_t* cr, const GuiRect& r, GuiColor top_left,
                       GuiColor bottom_right, int lw);
} // namespace

// THE HAIRLINE RING (render.h's line rule, 2026-10-23): the same mitred
// ring, its lines ONE DEVICE PX.
void paint_hairline_frame(cairo_t* cr, const GuiRect& r, GuiColor top_left,
                          GuiColor bottom_right) {
    const int lw = hairline_px();
    if (r.w <= 0 || r.h <= 0) return;
    if (r.w < 2 * lw || r.h < 2 * lw) {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        paint_cell_rect(cr, r, top_left);
        cairo_restore(cr);
        return;
    }
    paint_mitred_ring(cr, r, top_left, bottom_right, lw);
}

namespace {
void paint_mitred_ring(cairo_t* cr, const GuiRect& r, GuiColor top_left,
                       GuiColor bottom_right, int lw) {
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
    set_surface_source(cr, top_left);
    cairo_fill(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_move_to(cr, x1, y0);
    cairo_line_to(cr, x1, y1);
    cairo_line_to(cr, x0, y1);
    cairo_line_to(cr, x0 + lw, y1 - lw);
    cairo_line_to(cr, x1 - lw, y1 - lw);
    cairo_line_to(cr, x1 - lw, y0 + lw);
    cairo_close_path(cr);
    set_surface_source(cr, bottom_right);
    cairo_fill(cr);
    cairo_restore(cr);
}
} // namespace

// (THE RELIEF FAMILY — paint_relief_frame, the two-line paint_relief_edge,
// paint_relief_plain_sunken and paint_relief_line_frame, every line one
// scaled Windows px (relief_line_px) — went 2026-10-23 with the hairline
// sweep: render.h's line rule; the view bar's span, their last wearer,
// takes paint_hairline_frame.)

// (THE WINDOWS PARTS' EDGES — paint_windows_soft_raised, _soft_sunken and
// _plain_raised in Windows 2000's Standard quartet, the caption buttons' and
// the sizing frame's, 2026-10-22 to 2026-10-23 — left with those parts; the
// hi-fi skin's caption and border that replaced them went 2026-10-24,
// render.h's caption note.)

// THE SCRUB'S THUMB (render.h's scrub block, 2026-10-23): a key at rest,
// no LED, on its box — its size's patch (materials.h's KEYS).
void paint_scrub_thumb(cairo_t* cr, const GuiRect& box) {
    if (box.w <= 0 || box.h <= 0) return;
    paint_key(cr, box, false, GuiKeyLed::None);
}

// THE COLUMN BOUNDS (2026-10-13; the contract is at the declaration, render.h):
// every step of the plate's render but the store, so the cairo road's writer
// below and the GL canvas's plate (waveform_cache.cpp's
// render_waveform_to_plate_columns) read one set of bounds.
bool render_waveform_columns(GuiRect area,
                             int col0,
                             const GuiAudio& audio,
                             int channel,
                             const WaveformBasis& basis,
                             const WaveformGainCurve* gain_or_null,
                             int outline_px,
                             int surf_w,
                             int surf_h,
                             const std::vector<WarpFrameMapSegment>* warp_frame_map,
                             WaveformBandColumns& out) {
    if (area.w <= 0 || area.h <= 2) return false;
    if (basis.full_width <= 0) return false;
    // The lattice step must be numeric and positive; a degenerate zoom refuses
    // here exactly as the old empty-viewport check did.
    if (!(basis.spp > 0.0)) return false;

    const int num_levels = audio.num_levels();
    if (num_levels <= 0) return false;
    if (surf_w <= 0 || surf_h <= 0) return false;

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
    // inner bar stands over the outer, both in the ink word, the inner's
    // outline (outline_px thick) in the outline word — the rule is at this
    // function's declaration; the write pass below).
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

    // Row bounds: this channel's band, intersected with the surface.
    int y_lo = area.y;
    int y_hi = area.y + area.h;          // exclusive
    if (y_lo < 0)      y_lo = 0;
    if (y_hi > surf_h) y_hi = surf_h;
    if (y_hi <= y_lo) return false;

    // COLUMNS THIS CALL OWNS. Every write is clipped to them, so a partial
    // render, if one is ever reintroduced, cannot bleed into a neighbour's
    // columns. (They also bound the write to the surface: both ends are clamped
    // into [0, surf_w) here, once, instead of at every store.)
    int col_lo = area.x;
    int col_hi = area.x + area.w;
    if (col_lo < 0)      col_lo = 0;
    if (col_hi > surf_w) col_hi = surf_w;
    if (col_hi <= col_lo) return false;

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

    // THE LIT INNER BAR'S INTERIOR: which of its rows wear the ink word and
    // which the outline word (the outline recolours pixels of the bar's own
    // shape and adds none). THE CONTOUR FROM THE 2t + 1 COLUMNS' EXTENTS, t =
    // outline_px (the line width, render.h's waveform_line_px, snapshotted on
    // the job), no 2D scan: a row of this bar is INTERIOR iff every row within
    // t above and below it is in this bar (rows [r0 + t, r1 - t]) and it lies
    // inside every column within t on each side (inside that column's
    // [r0, r1]; a column beyond the plate's side edge counts as inside) — an
    // erosion at distance t, the four-neighbour test at t = 1. That interior
    // is one interval [lo, hi] because each bar is one; the rows of the bar
    // above and below it are the border, the whole bar when the interior is
    // empty (lo > hi; when it is not, lo >= r0 and hi <= r1, each clause only
    // narrowing). No inner tip reaches full scale (kForegroundGain's
    // static_assert, waveform_gain.cpp), so no inner bar is clipped and each
    // of its ends is a true end of the shape, taking the border.
    assert(!gain_or_null || outline_px >= 1);
    const int t = outline_px;
    const auto inner_interior = [&](int i, const std::vector<BarRows>& rows) {
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
        return BarRows{lo, hi};
    };

    // THE BARS' ROWS, one per column of this call: the dark bar or the lit
    // outer in `outer_rows`, the lit inner in `inner_rows` — held so the
    // write pass can read the neighbours within t of every column and write
    // the surface row by row. Both callers are full-plate renders, so this
    // call's columns ARE the plate and its first and last columns' missing
    // neighbours are the plate's side edges.
    std::vector<BarRows> outer_rows(static_cast<size_t>(area.w));
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
        // themselves are untouched, here and everywhere.
        // DARK: the raw bar at scale 1.0, where the clamp is a no-op (raw
        // peaks already rest in range), so the dark plate is the plate this
        // writer always drew.
        if (gain_or_null) {
            const int64_t centre = (s0 + s1) / 2;
            const double e = static_cast<double>(
                waveform_expander_multiplier_over(*gain_or_null, s0, s1));
            const double outer = waveform_gain_at(*gain_or_null, centre) * e;
            const double inner = waveform_inner_scale_at(*gain_or_null, centre) * e;
            outer_rows[static_cast<size_t>(i)] =
                bar_rows(magnified_tip(mm.first, outer),
                         magnified_tip(mm.second, outer));
            inner_rows[static_cast<size_t>(i)] =
                bar_rows(magnified_tip(mm.first, inner),
                         magnified_tip(mm.second, inner));
        } else {
            outer_rows[static_cast<size_t>(i)] =
                bar_rows(magnified_tip(mm.first, 1.0),
                         magnified_tip(mm.second, 1.0));
        }

        g_prev = g1;
    }

    // EACH OWNED COLUMN'S ROWS (WaveformColumnRows, render.h): the outer
    // [o0, o1], the lit inner [i0, i1] and its interior [lo, hi], an empty
    // inner (the dark lamp's) never matching — the bounds the writer's word
    // and the GL canvas's select both read.
    const int ncols = col_hi - col_lo;
    out.y_lo   = y_lo;
    out.y_hi   = y_hi;
    out.col_lo = col_lo;
    out.col_hi = col_hi;
    out.whole  = y_lo == area.y && y_hi == area.y + area.h &&
                 col_lo == area.x && col_hi == area.x + area.w;
    out.cols.resize(static_cast<size_t>(ncols));
    for (int x = col_lo; x < col_hi; ++x) {
        const int i = x - area.x;
        const BarRows& o = outer_rows[static_cast<size_t>(i)];
        WaveformColumnRows& cw = out.cols[static_cast<size_t>(x - col_lo)];
        cw.o0 = o.r0;
        cw.o1 = o.r1;
        cw.i0 = y_hi;
        cw.i1 = y_lo - 1;
        cw.lo = y_hi;
        cw.hi = y_lo - 1;
        if (gain_or_null) {
            const BarRows& b = inner_rows[static_cast<size_t>(i)];
            const BarRows in = inner_interior(i, inner_rows);
            cw.i0 = b.r0;
            cw.i1 = b.r1;
            cw.lo = in.r0;
            cw.hi = in.r1;
        }
    }
    return true;
}

bool render_waveform(cairo_surface_t* dest,
                     GuiRect area,
                     int col0,
                     const GuiAudio& audio,
                     int channel,
                     const WaveformBasis& basis,
                     const WaveformGainCurve* gain_or_null,
                     int outline_px,
                     WaveformPlateInks inks,
                     const std::vector<WarpFrameMapSegment>* warp_frame_map) {
    if (!dest) return false;

    // ARGB32 ONLY: the writer stores 32-bit premultiplied words, so any other
    // format would be silently misinterpreted. Both plate surfaces are created
    // CAIRO_FORMAT_ARGB32 (waveform_cache.cpp); this is the guard that keeps
    // that true. Geometry comes from the surface itself — the stride accessor,
    // never width*4, since cairo is free to pad rows.
    if (cairo_image_surface_get_format(dest) != CAIRO_FORMAT_ARGB32)
        return false;
    // Flush BEFORE the first CPU access so any pending cairo drawing on the
    // surface has landed in the buffer (the one caller draws none before this
    // call since the band is written whole; the flush is the surface
    // contract, kept). Paired with the cairo_surface_mark_dirty after the
    // last write. (The column producer's refusals now come after it; a flush
    // with nothing pending changes no pixel.)
    cairo_surface_flush(dest);
    unsigned char* const surf_data = cairo_image_surface_get_data(dest);
    if (!surf_data) return false;
    const int surf_stride = cairo_image_surface_get_stride(dest);
    const int surf_w      = cairo_image_surface_get_width(dest);
    const int surf_h      = cairo_image_surface_get_height(dest);
    if (surf_w <= 0 || surf_h <= 0) return false;

    // THE BOUNDS, the one producer's (render_waveform_columns, render.h).
    WaveformBandColumns band;
    if (!render_waveform_columns(area, col0, audio, channel, basis,
                                 gain_or_null, outline_px, surf_w, surf_h,
                                 warp_frame_map, band))
        return false;
    const int y_lo   = band.y_lo;
    const int y_hi   = band.y_hi;
    const int col_lo = band.col_lo;
    const int col_hi = band.col_hi;
    const std::vector<WaveformColumnRows>& cols = band.cols;
    const int ncols = col_hi - col_lo;

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

    // THE WRITE PASS: DELTA ROWS, THE BAND WRITTEN WHOLE (2026-10-13, a cost
    // cut that writes the same words). Each owned column's rows are gathered
    // first — the outer [o0, o1], the lit inner [i0, i1] and its interior
    // [lo, hi] (an empty inner, the dark lamp's, never matches). A column's
    // word is then PIECEWISE CONSTANT down the band: it changes only where
    // one of its three intervals starts or ends, row a and row b + 1 of
    // [a, b]. So one scratch row holds every owned column's word at the
    // current row: it is computed whole at the band's first row, each later
    // row first re-decides only the columns with an interval boundary on it
    // (the boundaries bucketed by row in one counting pass), and the scratch
    // row is copied onto EVERY row of the band [y_lo, y_hi) across the owned
    // columns [col_lo, col_hi) — contiguous row copies, no per-pixel work.
    // THE SHAPE IS THE TABLET'S MEASUREMENT (2026-10-13, a standalone bench
    // of the write pass at the tablet's plate, 2256 x 960 in two 480-row
    // bands, median ms per whole plate on a Cortex-A720 core, the NDK's
    // clang at -O3, each variant's buffer asserted equal; lit plate with
    // outer bars over 70–95 % of the band / dark plate with short bars; a
    // variant that needs the whole-surface clear carries the clear's 0.39):
    // column-major runs (each bar's rows at the row stride) 3.33 / 0.65,
    // row-major branchy per pixel 3.32 / 1.19, row-major vectorized select
    // over the bars' rows 4.53 / 1.77 (slower than the branchy loop under
    // that compiler), the same select on unsigned ranges over the whole band
    // 1.38 / 1.38, NEON tiles of 16 columns with their bounds in registers
    // 1.84 / 0.70 (whole band 1.66 / 1.66), DELTA ROWS, this code, 0.47 /
    // 0.48 — about the clear's own cost, the store bandwidth.
    //
    // THE IDENTITY, the one statement of it: the column-major writer of
    // record REPLACE-wrote the outer bar's rows with the ink word, then (lit)
    // every row of the inner bar once — the ink word inside its interior, the
    // outline word elsewhere in it — over the outer, onto a surface cleared
    // to the zero word 0x00000000. The word it left at (x, y) is therefore
    // decided by priority alone: inside the inner, the interior test; else
    // inside the outer, the ink; else the zero word — `word_at` below,
    // evaluated on the bounds render_waveform_columns gathered from the same
    // BarRows and the same [lo, hi], for the same owned columns. Between two boundary rows no interval's membership changes, so
    // the scratch word re-decided at the last boundary IS word_at at every
    // row up to the next; no store reads a pixel, so the order cannot change
    // a word. THE BAND IS THIS CALL'S WHOLE: every pixel of [y_lo, y_hi) x
    // [col_lo, col_hi) is stored, the zero word where no bar covers, so the
    // pass reads nothing the surface held before it and needs no clear; the
    // two channels' bands are disjoint (waveform_channel_band), so neither
    // call stores over the other's. The return value says whether that
    // stored region is `area` whole; THE CALLER CLEARS whatever no call
    // stored (render_waveform_to_cache_surface, waveform_cache.cpp: the
    // spare row of an odd plate and a band whose call returned early).
    const auto word_at = [&](const WaveformColumnRows& cw, int y) -> uint32_t {
        if (y >= cw.i0 && y <= cw.i1)
            return (y >= cw.lo && y <= cw.hi) ? ink_word : outline_word;
        if (y >= cw.o0 && y <= cw.o1) return ink_word;
        return 0u;
    };
    // The boundary rows strictly inside the band (the first row is computed
    // whole), bucketed by row: `ends[r]` closes row y_lo + r's run of column
    // indices in `events`, which opens at ends[r - 1].
    const int nrows = y_hi - y_lo;
    std::vector<int> ends(static_cast<size_t>(nrows), 0);
    const auto for_each_boundary = [&](const WaveformColumnRows& cw, auto&& f) {
        const int iv[3][2] = {{cw.o0, cw.o1}, {cw.i0, cw.i1}, {cw.lo, cw.hi}};
        for (const auto& ab : iv) {
            if (ab[0] > ab[1]) continue;
            if (ab[0] > y_lo && ab[0] < y_hi) f(ab[0] - y_lo);
            if (ab[1] + 1 > y_lo && ab[1] + 1 < y_hi) f(ab[1] + 1 - y_lo);
        }
    };
    for (const WaveformColumnRows& cw : cols)
        for_each_boundary(cw, [&](int r) { ++ends[static_cast<size_t>(r)]; });
    int total = 0;
    for (int& e : ends) {
        const int k = e;
        e = total;   // for now, the row's first slot
        total += k;
    }
    std::vector<int> events(static_cast<size_t>(total));
    for (int j = 0; j < ncols; ++j)
        for_each_boundary(cols[static_cast<size_t>(j)], [&](int r) {
            events[static_cast<size_t>(ends[static_cast<size_t>(r)]++)] = j;
        });
    std::vector<uint32_t> scratch(static_cast<size_t>(ncols));
    for (int j = 0; j < ncols; ++j)
        scratch[static_cast<size_t>(j)] =
            word_at(cols[static_cast<size_t>(j)], y_lo);
    const size_t row_bytes = static_cast<size_t>(ncols) * sizeof(uint32_t);
    for (int r = 0; r < nrows; ++r) {
        const int y = y_lo + r;
        if (r > 0) {
            for (int e = ends[static_cast<size_t>(r - 1)];
                 e < ends[static_cast<size_t>(r)]; ++e) {
                const size_t j =
                    static_cast<size_t>(events[static_cast<size_t>(e)]);
                scratch[j] = word_at(cols[j], y);
            }
        }
        uint32_t* const row = reinterpret_cast<uint32_t*>(
            surf_data + static_cast<size_t>(y) * surf_stride) + col_lo;
        std::memcpy(row, scratch.data(), row_bytes);
    }

    // Last CPU write is done — hand the buffer back to cairo.
    cairo_surface_mark_dirty(dest);
    return band.whole;
}

namespace {
// ONE SOLID CANVAS LINE IN THE TWO WELLS: the waveform_line_px()-wide line
// at column `col` (its column gate and right-edge clip, waveform_line_width)
// as one rect per non-empty well (waveform_well_rows), so the stereo
// divider's rows carry none — the scanner's and the zoom anchor's one form,
// one sink fill.
void well_line_rects(GuiCanvasSink& sink, const GuiRect& area, int col,
                     GuiColor color) {
    const int width = waveform_line_width(area.w, col);
    if (width <= 0) return;
    std::array<GuiRect, 2> rs{};
    std::size_t n = 0;
    for (const WaveformWellRows& w : waveform_well_rows(area))
        if (w.y1 > w.y0) rs[n++] = GuiRect{area.x + col, w.y0, width, w.y1 - w.y0};
    if (n > 0) sink.rects(std::span<const GuiRect>(rs.data(), n), color);
}
} // namespace

void render_playhead(GuiCanvasSink& sink,
                     GuiRect area,
                     double  playhead_pixel_x,
                     GuiColor color,
                     PlayheadForm form) {
    if (area.w <= 0 || area.h <= 0) return;
    // Allow partial render at file start / end: a playhead whose column has
    // clipped just past the area edge still gets here, and the column gate
    // below decides whether any pixel lands. This keeps the playhead's visual
    // center aligned with its true frame position rather than snapping it
    // inward at the rightmost samples. The bound is the head's painted reach
    // (cue_triangle_half_w_px / cue_triangle_reach_right_px, render.h) — the
    // cull is stated in the same columns the damage box takes
    // (playhead_invalidate_rect, main.cpp).
    if (playhead_pixel_x <
        -static_cast<double>(cue_triangle_reach_right_px()))
        return;
    if (playhead_pixel_x >
        static_cast<double>(area.w - 1 + cue_triangle_half_w_px()))
        return;

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

    // The sink's scope with the antialias left as it stands (the integral
    // rect under the integral translation is what makes these cells exact,
    // canvas_sink.h's head).
    sink.begin(/*aliased=*/false);
    // The waveform_line_px()-wide line (render.h) paints whenever its column
    // is onscreen (gated on its own column and clipped at the right edge), so
    // it never leaks into an adjacent region, straight over whatever it
    // crosses — waveform ink included. THE CANVAS'S TWO WELLS (2026-10-09;
    // the stereo divider's rows left to it since 2026-10-16,
    // waveform_well_rows): the scanner solid, the resting cursor Cool Edit's
    // dotted column on its own phase (the declaration), one run per well.
    if (form == PlayheadForm::Dotted) {
        GuiCanvasDotRun runs[2];
        const int n = dotted_waveform_line_runs(
            area, col, kProgramSpec.dot_period,
            kProgramSpec.playhead_dot_phase, runs);
        if (n > 0)
            sink.dots(std::span<const GuiCanvasDotRun>(runs,
                                                       static_cast<std::size_t>(n)),
                      color);
    } else {
        well_line_rects(sink, area, col, color);
    }
    sink.end();
}

void render_strip_anchor_stem(GuiCanvasSink& sink, GuiRect area, int col) {
    if (area.w <= 0 || area.h <= 0) return;
    // The clamp is where the affordance lives: an anchor pushed to (or past) a
    // song edge pins to the edge column, so the stem draws exactly there.
    // The line is waveform_line_px() wide (render.h), its left edge on the
    // column, clipped at the right edge (fill_waveform_line), so an anchor
    // pinned to the last column paints that one column.
    if (col < 0)          col = 0;
    if (col >= area.w)    col = area.w - 1;

    sink.begin(/*aliased=*/false);
    // THE ANCHOR STEM IS THE SCANNER'S WHITE (architect 2026-10-09: "the zoom
    // stem and the scanner are both white, which unifies them: the
    // non-playhead stems that are related to the controls should be white,
    // and then the playhead is yellow"): a control's line during a gesture,
    // as the scanner is playback's, in the one `scanner` role — the playhead
    // alone wears `playhead_stem` (render.h's playhead paragraph). It is
    // deliberately as loud as a marker stem, a solid line where the cues'
    // and the cursor's are dotted.
    // The canvas's two wells (2026-10-09: the frame row above it is the
    // canvas's frame, which no line crosses; the stereo divider's rows
    // likewise since 2026-10-16, waveform_well_rows).
    well_line_rects(sink, area, col, palette().scanner);
    sink.end();
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

// (THE POPUP LISTS' SCROLL ARROW BUTTON — paint_trim_arrow_button, up and
// down, the trim caps' left and right gone with the view bar 2026-10-09 —
// went with the popup lists' scroll bar, 2026-10-14.)

void render_trim_flags(cairo_t* cr,
                       GuiRect top_strip_area,
                       GuiRect trim_bar,
                       GuiRect waveform_area,
                       long long viewport_start_sample,
                       long long viewport_end_sample,
                       const TrimRange& trim,
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

    // THE BAR'S COLUMNS ARE THE CANVAS'S (2026-10-09, the column's margins):
    // waveform_area's x and its effective width, the frame's side columns one
    // line outside them.
    const int lane_x   = waveform_area.x;
    const int lane_w   = waveform_area.w;   // the effective width
    const int lane_y   = trim_bar.y;
    const int lane_h   = trim_bar.h;

    // THE BAR (METRICS §3, §4.1; the hi-fi skin 2026-10-23): ITS FIELD BLACK
    // (kCeViewBarField) under THE PROGRAM RING in the two ring tones, every
    // line ONE DEVICE PX on its one-W slot's outer edge (render.h's line
    // rule, hairline_px), the slot's other rows the black field — this bar's
    // top line and left column the DARK tone, its right column the LIGHT,
    // the side columns running UNBROKEN down through the ruler's and the
    // marker lane's columns (paint_ruler_row) into the canvas ring's own
    // (paint_canvas_column_frame) — and ITS FOOT LINE, the ruler's light line
    // over, the light tone on its slot's LAST row, against the ruler. (The
    // wood's ring and the brass's foot line of 2026-10-22, three px a line at
    // 300 %, are git history.)
    const GuiPalette& pal = palette();
    const int lw = program_line_px();
    const int px = hairline_px();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    {
        const GuiMaterialScope flat(GuiMaterialRegion::Flat);
        paint_cell_rect(cr, GuiRect{lane_x - lw, lane_y, lane_w + 2 * lw, lane_h},
                        hex(kCeViewBarField));
        paint_cell_rect(cr, GuiRect{lane_x - lw, lane_y, lane_w + 2 * lw, px},
                        pal.ce_dark);
        paint_cell_rect(cr, GuiRect{lane_x - lw, lane_y, px, lane_h}, pal.ce_dark);
        paint_cell_rect(cr, GuiRect{lane_x + lane_w + lw - px, lane_y + px, px,
                                    lane_h - px},
                        pal.ce_hilight);
        paint_cell_rect(cr, GuiRect{lane_x - lw + px, lane_y + lane_h - px,
                                    lane_w + 2 * lw - 2 * px, px},
                        pal.ce_hilight);
    }
    cairo_rectangle(cr, lane_x, lane_y, lane_w, lane_h);
    cairo_clip(cr);

    const GuiRect lane{lane_x, lane_y, lane_w, lane_h};
    const int field_y = lane_y + lw;
    const int field_h = lane_h - 2 * lw;

    // THE SPAN — the trim window's columns, the begin column through the end
    // column inclusive, sliding off an edge like any content (the clip cuts
    // it; a side lying farther off than one quantum is held there, outside
    // the clip, so the rect stays in cairo's range at any zoom): the ink's
    // body, its top row and left column the light bevel, its bottom row and
    // right column the dark one, each ONE DEVICE PX (render.h's line rule,
    // the sweep of 2026-10-23; one scaled W until then), the two MITRED at
    // the top-right and bottom-left corner blocks (architect 2026-10-09
    // ~12:40, the diagonal rule reaching the program: cool_edit_paint.h's
    // head; paint_hairline_frame).
    const int span_lo = std::max(bc.col, -lw);
    const int span_hi = std::min(ec.col + 1, lane_w + lw);
    if (span_hi > span_lo && field_h > 0) {
        const int sx = lane_x + span_lo;
        const int sw = span_hi - span_lo;
        paint_cell_rect(cr, GuiRect{sx, field_y, sw, field_h},
                        pal.waveform_ink);
        paint_hairline_frame(cr, GuiRect{sx, field_y, sw, field_h},
                             pal.ce_span_hilight, pal.ce_span_shadow);
        // NO PLAYHEAD IN THE BAR (architect 2026-10-09 ~14:30, "I thought we
        // agreed no dots on the trim bar"; the mock of record draws none):
        // Cool Edit's period-2 cursor column over its span (METRICS §3) is
        // not drawn.
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
// grid point w for its triangle's left half alone — but only a marker whose
// own column is a real waveform column publishes a stem. The stem painter
// (paint_marker_stems) reads only real columns, so an off-surface entry
// paints no dot. `col` is the marker's column relative to x0. The one spelling for both lane
// producers (render_flag_boxes_impl, render_history_diff_flags).
bool stem_column_on_waveform(int col, int w) {
    return col >= 0 && col < w;
}

// Shared flag iteration used by render_flags, its phase-reset analogue and
// the `h` view's diff lane. Invokes `emit(i, left_x)` for EVERY visible
// marker IN STORE ORDER; the cue painter collects them and orders them by
// column itself (the overlap rule paints the labels left to right and the
// triangles left to right, render.h's marker-lane paragraph).
//
// THE PAINT/HIT INVARIANT. `left_x` — the marker's painted pixel column — is
// computed ONCE here: the triangle's apex column, the label's anchor and the
// dots' column. The painter draws from it, the hit rects are published from
// it, and the stem is published at it, so all three are one number by
// construction.
template <typename MarkerVec, typename Emit>
void iterate_visible_flags_impl(
    // The waveform's first column in the surface's x (FlagLaneRects'
    // columns_x, 2026-10-09).
    int columns_x,
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
            static_cast<double>(columns_x) + static_cast<double>(col);

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
//
// A CUE RUN IS A SHARED HANDLE on one memoized shaping (text_shape::
// memoized_run, the label runs' memo the ruler's timestamps share): the
// layout, every CueSegment and the producers' locals hold the same run, never
// a copy of its glyphs. A NULL HANDLE stands where no run is (no cells, an
// absent history half), and no reader dereferences one: every read is behind
// its segment's `present` or the layout's.
using CueRun = text_shape::SharedRun;
struct IterCellLayout {
    bool   present = false;
    CueRun lower_run;              // null unless `present`
    CueRun upper_run;              // null unless `present`
    int    lower_w = 0;   // the token's run, whole columns
    int    upper_w = 0;
};

static IterCellLayout measure_iter_cells(const GuiFont& font,
                                         const IterCellText& cells) {
    IterCellLayout l;
    if (!cells.present) return l;
    l.present   = true;
    l.lower_run = text_shape::memoized_run(font, cells.lower);
    l.upper_run = text_shape::memoized_run(font, cells.upper);
    l.lower_w   = static_cast<int>(std::nearbyint(l.lower_run->width_px));
    l.upper_w   = static_cast<int>(std::nearbyint(l.upper_run->width_px));
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

// THE FLAG'S KIND: WARP and PHASE RESET, co-equal, the authoring columns'
// cues by their column, and the `h` view's ADDED and REMOVED diff halves.
// Since 2026-10-09 ~11:50 the kind no longer colors a cue apart from the
// history's two halves: warp against phase reset reads from the label alone
// (his one-label rule). The palette block's marker-lane paragraph (render.h)
// owns the look.
enum class GuiFlagKind { Warp, PhaseReset, Added, Removed };

// The resolved paint of ONE cue (the palette block's marker-lane paragraph,
// render.h, owns the look): the triangle's color, the dots on each of the
// stem's two phases (none at all on a disabled cue), and whether its resting
// label wears the invalid color. The label's other looks are the segment's
// own (selected, inert or the bone, materials.h's kBoneInk).
struct FlagFace {
    GuiColor  triangle;
    GuiColor  shadow;
    GuiCueDot cue_phase_dots;
    GuiCueDot range_phase_dots;
    bool      invalid_label;
};

// THE ONE RESOLVER for every cue — both marker columns and the `h` view's
// diff flags (architect 2026-10-09 ~11:50–12:00, Cool Edit's two cue colors:
// "red+blue for warp and phase, red only for invalid (half the amount of
// dots)"; 2026-10-11 ~01:00–01:20, THE FLAG IS THE INVALID INDICATOR and the
// history's plus and minus). THE TABLE, the ladder read top to bottom, the
// first matching row wins:
//   flag                     triangle  dots (red's phase | blue's phase)   label
//                                                                      rest / selected
//   DISABLED (any kind)      faded     none                            faded / white
//   INVALID (any live kind)  invalid   invalid | —                     invalid / white
//   WARP, PHASE RESET        cue red   cue red | range blue            light / white
//   history ADDED (plus)     cue red   —       | cue red               light / white
//   history REMOVED (minus)  invalid   invalid | —                     light / white
// "invalid" is the palette's `invalid_label`, each built-in's center line
// (palette_file.h's role table); THE `range` BLUE LEAVES THE HISTORY VIEW
// (2026-10-11: the plus takes the marker's own face, the minus the invalid
// color), each half keeping its phase of 2026-10-09 so a changed pair's two
// stems on one column still alternate.
// THE DISABLED ROW'S FADE (architect 2026-10-10 ~12:20, "the arrow needs to
// be legible for me, but the text doesn't … since it's disabled, it's inert"):
// the triangle and its shadow are the live row's triangle color (the `cue`
// red, or the invalid color of an invalid cue or a REMOVED half, 2026-10-11)
// and `cue_shadow` BLENDED 45 % TOWARD THE FACE, the label's rest tone 65 %
// toward it — derived solid colors, "an opacity look … not actually opaque"
// (cool_edit_derive.h, the members `ce_off_*`); the selected label stays the
// chrome's `label` so a selection shows. Every INERT label — a disabled
// cue's segments and a tie follower's cells — wears the faded label, one
// look for inert cue text (the Windows emboss retired from cues 2026-10-10).
// THE DOTS' ROWS, counted from the canvas's first row, are the red's phase
// ≡ 7 and the blue's ≡ 3 (mod 8), the blue's first from the top — the
// capture's absolute red y ≡ 3 / blue y ≡ 7 re-counted from Cool Edit's
// canvas top at y = 108 ≡ 4 (mod 8) (program_spec.h's dot fields, the
// derivation's owner); "light" the bone (materials.h's kBoneInk, the words'
// and the flags' one ink since 2026-10-22; the near-white F0F0F0 on the
// brass, the same bytes, 2026-10-21; the panel's `ce_hilight` until then),
// "white" the
// chrome's `label` — Cool Edit's program text, EFEFEF on the default Face,
// dark past its light-face swap (2026-10-10: the chrome's `selected_text` it
// read until then became a text for the ink's fill, not for the face — the
// program text's other member since 2026-10-16; chrome_derive.h) — every
// label on the panel's face with no fill in either state. THE TRIANGLE AND THE DOTS NEVER CHANGE WITH
// SELECTION (Cool Edit's never do): the label carries the selection — ITS
// TEXT ALONE, the chrome's `label` on the panel's face, no fill (architect
// 2026-10-09 ~23:30: "clicking on a flag to select it makes the text turn
// white. That's it"), AN INVALID LABEL THE SAME (architect 2026-10-11: "No
// flags change color when you select them. It's the text that changes
// color … I'd rather the text just go white, and the flag should be the
// indicator that it's invalid"; the bright selected red of 2026-10-09
// retired with its role). A DISABLED INVALID CUE — the red set counts
// disabled rows (warp_red_flag_set_cached's (3)) — fades the invalid color.
// A `h`-view half is never the invalid class (HistoryDiffFlag's note), so
// `red` is false there.
FlagFace resolve_flag_face(GuiFlagKind kind, bool disabled, bool red) {
    const GuiPalette& p = palette();
    const bool invalid_triangle = red || kind == GuiFlagKind::Removed;
    if (disabled) {
        return FlagFace{invalid_triangle ? p.ce_off_invalid : p.ce_off_cue,
                        p.ce_off_cue_shadow, GuiCueDot::None, GuiCueDot::None,
                        false};
    }
    if (red)
        return FlagFace{p.invalid_label, p.ce_cue_shadow, GuiCueDot::Invalid,
                        GuiCueDot::None, true};
    switch (kind) {
        case GuiFlagKind::Warp:
        case GuiFlagKind::PhaseReset:
            break;
        case GuiFlagKind::Added:
            return FlagFace{p.cue, p.ce_cue_shadow, GuiCueDot::None,
                            GuiCueDot::Cue, false};
        case GuiFlagKind::Removed:
            return FlagFace{p.invalid_label, p.ce_cue_shadow,
                            GuiCueDot::Invalid, GuiCueDot::None, false};
    }
    return FlagFace{p.cue, p.ce_cue_shadow, GuiCueDot::Cue, GuiCueDot::Range,
                    false};
}

// ONE SEGMENT OF A CUE'S LABEL, as the pass paints it: its shaped run (the
// memo's shared handle, CueRun — null exactly when the segment is absent),
// its face box [x0, x1) in window x (the text at x0 plus the pad quantum),
// and its look — the chrome's selected text on the face, the faded inert
// label, or the bone.
struct CueSegment {
    bool   present  = false;
    CueRun run;
    int    x0       = 0;
    int    x1       = 0;
    bool   selected = false;
    // INERT: a disabled marker's segment or a tie follower's cell, the faded
    // label (ce_off_label) unless selected (2026-10-10).
    bool   inert    = false;
};
// THE CUE LABEL'S ONE TEXT PAINTER (architect 2026-10-11, "make the text stand
// out a little more because it's a little small, but that's on purpose"; Cool
// Edit draws no shadow under its cue labels, so this is the product's own
// departure): the run first in its cue's triangle shadow (`FlagFace::shadow`,
// the live `ce_cue_shadow` or the disabled cue's faded one) ONE SCALED
// QUANTUM right and down, then `ink` over it. Every label look on the panel's
// face goes through here — resting, invalid, inert, selected, and the flag
// editor's riding cells; the editor's FIELD text does not.
static void show_cue_label(cairo_t* cr, const text_shape::ShapedRun& run,
                           double x, double baseline, GuiColor ink,
                           GuiColor /*shadow*/) {
    // NO SHADOW in the hi-fi skin (architect 2026-10-23, the frame of record
    // pv_39b: the 22-px bone on the brushed lane alone; the triangle keeps
    // its own shadow, paint_ce_cue_triangle).
    set_palette_source(cr, ink);
    text_shape::show_shaped_run(cr, run, x, baseline);
}

// ONE CUE, laid out: its marker (or diff flag) index, its column in window
// x, the triangle's resolved face and whether its dots paint, and its label's
// up-to-three segments in their left-to-right order.
struct CueDraw {
    int                       index = -1;
    int                       col   = 0;
    FlagFace                  face{};
    std::array<CueSegment, 3> seg{};
    // Whether segments 1 and 2 are the marker's bound cells (the live lanes)
    // or plain halves of one item (the history's pair, whose label publishes
    // no cell boundary: no bracket lives in a commit).
    bool                      segments_are_cells = true;
    // THE HISTORY'S CHANGED PAIR (render_history_diff_flags, 2026-10-11):
    // the triangle SPLIT, its left half face.triangle and its right half
    // `split_right`, its axis `axis_offset` past col — a stem's centre 0.5,
    // or 1.0 where a nudged pair's midpoint falls on the boundary right of
    // col — and ITS OWN STEMS, the pair's two, published in place of the
    // face's one at col (each still gated to the waveform's columns).
    bool                      split = false;
    GuiColor                  split_right{};
    double                    axis_offset = 0.5;
    bool                      own_stems = false;
    std::array<MarkerStem, 2> stems{};
};

// THE CUE PASS'S SCRATCH (2026-10-13, a cost cut that paints the same
// pixels): the cues one producer lays out and paint_cues' three working
// vectors, kept across rebuilds so each pass clear()s and refills them
// instead of allocating and freeing them afresh. THE GUI THREAD'S ALONE, and
// used by one pass at a time: the three producers (render_flags,
// render_phase_reset_flags, render_history_diff_flags) each run whole on the
// GUI thread inside the flag cache's rebuild, one after another, and none is
// re-entered from inside itself or from paint_cues. Each producer clears
// `cues` before it fills it; the elements of the last pass stay until the
// next pass's clear, holding their runs' handles (CueRun) that long. NO
// HIGH-WATER BOUND: a project's worst case is a few hundred cues, so the
// kept capacity is a few hundred elements of each.
struct CuePassScratch {
    std::vector<CueDraw>             cues;
    std::vector<int>                 lo;
    std::vector<int>                 hi;
    std::vector<std::pair<int, int>> spans;
};
static CuePassScratch g_cue_pass_scratch;

// THE CUES' ONE PAINTER (render.h's marker-lane paragraph: the overlap rule,
// the states; the lengths program_spec.h's), shared by both marker columns
// and the `h` view's lane: the labels LEFT TO RIGHT, each on its opaque face
// box from its own triangle's left edge to its text's end, so a later cue
// masks an earlier label only across its own extent — a selected segment its
// text in the chrome's selected text on that face, no fill of its own
// (2026-10-09 ~23:30); then
// the triangles LEFT TO RIGHT; then the
// publication — one FlagHitRect per cue (its label box as painted and its
// triangle, both clipped to the waveform's columns), and one MarkerStem per
// cue whose dots paint and whose column is a waveform column (the history's
// changed pair its own two, CueDraw::own_stems). `lane` is the
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
    // Each label's OCCLUDING BOX [lo, hi): from its own triangle's left edge
    // (its column less the triangle's half width) to its last segment's end;
    // empty where no segment paints. The text stands where its segments put
    // it; the box only reaches back to the triangle. The three working
    // vectors are the pass's kept scratch (CuePassScratch), refilled here.
    std::vector<int>& lo = g_cue_pass_scratch.lo;
    std::vector<int>& hi = g_cue_pass_scratch.hi;
    lo.assign(static_cast<std::size_t>(n), 0);
    hi.assign(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        const CueDraw& c = cues[static_cast<std::size_t>(i)];
        int b = 0;
        bool any = false;
        for (const CueSegment& s : c.seg) {
            if (!s.present) continue;
            b = s.x1;
            any = true;
        }
        lo[static_cast<std::size_t>(i)] = c.col - half;
        hi[static_cast<std::size_t>(i)] = any ? std::max(c.col - half, b)
                                             : c.col - half;
    }

    // THE LABELS, LEFT TO RIGHT: a later cue's box masks an earlier cue's
    // text across its own extent alone.
    //
    // THE BOX AND THE TEXT PAINT ONLY WHERE NO LATER BOX WILL COVER THEM
    // (architect 2026-10-12 for the text, the marker slowdown at coarse zoom;
    // 2026-10-13 for the box, the flag cache's solid fills measuring 12.8 % of
    // the CPU with most boxes covered whole by later ones): a cost cut that
    // paints the same pixels — every later box is opaque, the same rows and
    // the same face color, and its left edge never lies left of this one's,
    // the cues being ordered by column, so a pixel of this box that a later
    // box covers ends as that box's face whether or not this one painted it.
    // The VISIBLE SPANS of [a, b) less every later non-empty box, found in
    // one walk over the later boxes that start inside it, are therefore the
    // whole of what this label paints: the face box over their union, then
    // the text clipped to it; the union of every label's spans equals the
    // union of every box. A long label before a short one keeps its span past
    // the short one's end; a label whose spans are empty paints nothing.
    std::vector<std::pair<int, int>>& spans = g_cue_pass_scratch.spans;
    for (int i = 0; i < n; ++i) {
        const CueDraw& c = cues[static_cast<std::size_t>(i)];
        const int a = lo[static_cast<std::size_t>(i)];
        const int b = hi[static_cast<std::size_t>(i)];
        if (b <= a) continue;

        spans.clear();
        int cursor = a;
        for (int j = i + 1; j < n && cursor < b; ++j) {
            const int ja = lo[static_cast<std::size_t>(j)];
            const int jb = hi[static_cast<std::size_t>(j)];
            if (ja >= b) break;
            if (jb <= ja) continue;
            if (ja > cursor) spans.emplace_back(cursor, ja);
            cursor = std::max(cursor, jb);
        }
        if (cursor < b) spans.emplace_back(cursor, b);
        if (spans.empty()) continue;

        cairo_save(cr);
        cairo_new_path(cr);
        for (const auto& sp : spans)
            cairo_rectangle(cr, sp.first, lane.y, sp.second - sp.first,
                            fill_h);
        cairo_clip(cr);
        // THE OPAQUE FACE BOX, the overlap rule's, under every segment
        // whatever its state: A SELECTED SEGMENT TAKES NO FILL (architect
        // 2026-10-09 ~23:30, render.h's SELECTED row), its text alone
        // changing color.
        // THE BOX IS THE LANE'S BRUSHED GROUND AS DRAWN (materials.h's
        // MARKER, 2026-10-23): the strip's own pixels at their place and
        // phase, so the box masks earlier text exactly.
        {
            const GuiMaterialScope marker(GuiMaterialRegion::Marker);
            for (const auto& sp : spans)
                paint_cell_rect(cr,
                                GuiRect{sp.first, lane.y, sp.second - sp.first,
                                        fill_h},
                                pal.face);
        }
        for (const CueSegment& s : c.seg) {
            if (!s.present) continue;
            const double tx = static_cast<double>(
                s.x0 + kProgramSpec.cue_fill_pad * u);
            // The label's ink (resolve_flag_face's table): a selected
            // segment the chrome's text on the face, an invalid cue's
            // included (2026-10-11), else the faded inert label, an invalid
            // cue's resting invalid color, or the bone — a selected
            // inert segment the chrome's text, so its selection shows.
            show_cue_label(cr, *s.run, tx, base,
                           s.selected ? pal.label
                           : s.inert  ? pal.ce_off_label
                           : c.face.invalid_label
                                      ? pal.invalid_label
                                      : hex(kBoneInk),
                           c.face.shadow);
        }
        cairo_restore(cr);
    }

    // THE TRIANGLES, LEFT TO RIGHT, over every label: ONE PASS on one
    // prepared target (CueTrianglePass, cool_edit_paint.h). Its contract
    // holds here: every label above closed its own save / restore, and the
    // pass's cairo roads keep theirs, so the clip, the matrix, the operator
    // and the group are the caller's throughout.
    CueTrianglePass triangles;
    triangles.begin(cr);
    for (const CueDraw& c : cues) {
        if (c.split)
            triangles.split(static_cast<double>(c.col) + c.axis_offset,
                            tri_top, c.face.triangle, c.split_right,
                            c.face.shadow);
        else
            triangles.triangle(c.col, tri_top, c.face.triangle,
                               c.face.shadow);
    }
    triangles.end();

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
            // The triangle's box: its top edge and the shadow's quantum,
            // its five rows.
            FlagHitRect tri;
            tri.x = static_cast<double>(c.col - half);
            tri.y = static_cast<double>(tri_top);
            tri.w = static_cast<double>(half + cue_triangle_reach_right_px());
            tri.h = static_cast<double>(tri_h);
            if (clip_hit_rect_to_waveform_columns(tri, x0, w)) {
                r.tri_x = tri.x;
                r.tri_y = tri.y;
                r.tri_w = tri.w;
                r.tri_h = tri.h;
            }
            if (label_on || r.tri_w > 0.0) out_hit_rects->push_back(r);
        }
        if (out_stems && c.own_stems) {
            for (const MarkerStem& s : c.stems)
                if (stem_column_on_waveform(
                        static_cast<int>(std::nearbyint(s.x)) - x0, w))
                    out_stems->push_back(s);
        } else if (out_stems &&
                   (c.face.cue_phase_dots != GuiCueDot::None ||
                    c.face.range_phase_dots != GuiCueDot::None) &&
                   stem_column_on_waveform(c.col - x0, w))
            out_stems->push_back(MarkerStem{c.index, static_cast<double>(c.col),
                                            c.face.cue_phase_dots,
                                            c.face.range_phase_dots});
    }
}

} // namespace

// The phase-reset lead-in ring's dots (declaration in render.h): the
// resolver above asked for a LIVE reset's STEM PAIR on the same class bit the
// flag pass hands it, so the ring can never wear a dot its stem would not.
// It stands outside the file's anonymous namespace so paint_handler.cpp
// reaches it; the resolver it calls stays file-local.
GuiStemDots phase_reset_stem_dots(bool red) {
    const FlagFace f =
        resolve_flag_face(GuiFlagKind::PhaseReset, /*disabled=*/false, red);
    return GuiStemDots{f.cue_phase_dots, f.range_phase_dots};
}
// The `h` view's live half's dots (declaration in render.h), off the same
// resolver, for the nudged pair's rectangle runs.
GuiStemDots history_half_dots(bool added) {
    const FlagFace f = resolve_flag_face(
        added ? GuiFlagKind::Added : GuiFlagKind::Removed,
        /*disabled=*/false, /*red=*/false);
    return GuiStemDots{f.cue_phase_dots, f.range_phase_dots};
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
    clip_to_waveform_columns(cr, lanes.columns_x, waveform_width,
                             top_strip_area.y, top_strip_area.h);
    // THE PROGRAM FACE for the whole pass (gui_font.h; the cues' cap 7),
    // every run carrying it (text_shape.h).
    const GuiFont font = gui_font(GuiFace::Program);
    // The pass's kept scratch (CuePassScratch), emptied for this pass.
    std::vector<CueDraw>& cues = g_cue_pass_scratch.cues;
    cues.clear();

    iterate_visible_flags_impl(lanes.columns_x, waveform_width, markers,
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
            // (cue_segment_boxes). A TIE FOLLOWER'S CELLS wear the faded inert
            // label (architect 2026-09-19: the leader's numbers, not this
            // marker's to author; the Windows emboss retired from cue text
            // 2026-10-10); a disabled marker's every segment does.
            const CueRun payload = text_shape::memoized_run(font, label_of(i));
            const IterCellLayout cl = measure_iter_cells(font, cells);
            const bool present[3] = {pass_paints(MarkerCell::Payload),
                                     cells.present &&
                                         pass_paints(MarkerCell::Lower),
                                     cells.present &&
                                         pass_paints(MarkerCell::Upper)};
            const int widths[3] = {
                static_cast<int>(std::nearbyint(payload->width_px)),
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
                seg.inert = dis || (s > 0 && cells.follower);
            }
            cues.push_back(std::move(c));
        });

    paint_cues(cr, lane, lanes.columns_x, waveform_width, cues, out_hit_rects,
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
        std::vector<MarkerStemRun>* out_runs,
        const std::vector<WarpFrameMapSegment>* warp_frame_map) {
    // The same clear-first contract the two marker painters carry: this pass is
    // the SOLE producer of both stashes while the history mode stands, so a
    // frame that paints nothing must leave nothing claimable behind — and of
    // the runs (MarkerStemRun) always.
    if (out_hit_rects) out_hit_rects->clear();
    if (out_stems)     out_stems->clear();
    if (out_runs)      out_runs->clear();
    if (top_strip_area.w <= 0 || top_strip_area.h <= 0) return;
    if (viewport_end_sample <= viewport_start_sample) return;

    const GuiRect lane = lanes.marker_lane;
    if (lane.h <= 0) return;

    cairo_save(cr);
    // The waveform's columns are this pass's clip too, the live lane's rule
    // (clip_to_waveform_columns, architect 2026-09-26).
    clip_to_waveform_columns(cr, lanes.columns_x, waveform_width,
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

    // THE NUDGED PAIR'S COLUMNS. The iterator maps a flag's time_frame — the
    // LEFT marker's (HistoryDiffFlag) — and the pair's RIGHT stem is mapped
    // here by the iterator's own two steps (frame_to_paint_sample, then
    // displayed_column_at over the same samples-per-pixel), so both stems
    // land where a live marker at either frame would. THE CULL WIDENS BY THE
    // WIDEST PAIR'S SPAN: the iterator culls on the left frame, and a pair
    // whose left stem is far off the left edge may still stand its right
    // stem, its rectangle and its centred flag in view.
    const double spp =
        static_cast<double>(viewport_end_sample - viewport_start_sample) /
        static_cast<double>(waveform_width);
    const auto paint_col = [&](int64_t frame) {
        return lanes.columns_x +
               displayed_column_at(
                   frame_to_paint_sample(static_cast<double>(frame),
                                         warp_frame_map),
                   static_cast<double>(viewport_start_sample), spp);
    };
    double widest_pair_px = 0.0;
    if (spp > 0.0) {
        for (const HistoryDiffFlag& f : flags) {
            if (!f.nudged()) continue;
            const double span =
                std::fabs(frame_to_paint_sample(
                              static_cast<double>(f.then_frame),
                              warp_frame_map) -
                          frame_to_paint_sample(
                              static_cast<double>(f.now_frame),
                              warp_frame_map)) / spp + 1.0;
            if (span > widest_pair_px) widest_pair_px = span;
        }
    }
    // The pass's kept scratch (CuePassScratch), emptied for this pass.
    std::vector<CueDraw>& cues = g_cue_pass_scratch.cues;
    cues.clear();

    iterate_visible_flags_impl(
        lanes.columns_x, waveform_width, flags,
        viewport_start_sample, viewport_end_sample,
        warp_frame_map,
        // NO DRAG OVERLAY: the mode consumes every authoring gesture, so no
        // marker drag can be in flight while this pass runs — and a diff flag is
        // not a marker in any store, so nothing could index it anyway.
        /*drag_overlay=*/nullptr,
        cull_width_px + widest_pair_px,
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
            const bool pair = f.removed && f.added;
            // THE LEFT MARKER'S STEM COLUMN, the iterator's; on a pair the
            // right one's off its own frame (paint_col above) — the same
            // column on a same-frame pair. THE FLAG STANDS CENTRED BETWEEN
            // THEM: col the midpoint floored, the triangle's axis the two
            // stems' centres' mean, so 0.5 past col on an even sum and on
            // the boundary right of col (1.0) on an odd one. THE LANDING
            // READS THIS BASIS (history_diff_flag_landing_domain_frame,
            // app_state.h, 2026-10-11): both halves mapped first, then
            // centred, so a click, Tab or Center seats the playhead under
            // this triangle across a warp breakpoint too.
            const int col_left = static_cast<int>(std::nearbyint(left_x));
            const int col_right =
                f.nudged()
                    ? paint_col(f.added_left() ? f.then_frame : f.now_frame)
                    : col_left;
            const int col_sum = col_left + col_right;
            c.col = static_cast<int>(
                std::floor(static_cast<double>(col_sum) / 2.0));
            c.axis_offset = (col_sum - 2 * c.col == 1) ? 1.0 : 0.5;
            // THE TWO HALVES AS TWO SEGMENTS, THE LEFT MARKER'S FIRST (the
            // removed then the added, the added first on a pair moved
            // earlier — HistoryDiffFlag::added_left), their box layout the
            // live lane's one (cue_segment_boxes), the segment slots 0 and
            // 1; a selected flag's two halves both turn the selected text,
            // no fill (render.h's SELECTED row).
            const bool added_first = f.added_left();
            // An absent half holds the null handle; its width reads 0, as
            // the empty run it stood for read, and no segment takes it.
            CueRun run_removed;
            CueRun run_added;
            if (f.removed)
                run_removed = text_shape::memoized_run(font, f.removed_text);
            if (f.added)
                run_added = text_shape::memoized_run(font, f.added_text);
            const CueRun& run0 = added_first ? run_added : run_removed;
            const CueRun& run1 = added_first ? run_removed : run_added;
            const bool present[3] = {
                added_first ? f.added : f.removed,
                added_first ? f.removed : f.added, false};
            const int widths[3] = {
                run0 ? static_cast<int>(std::nearbyint(run0->width_px)) : 0,
                run1 ? static_cast<int>(std::nearbyint(run1->width_px)) : 0,
                0};
            const CueSegmentBoxes boxes = cue_segment_boxes(widths, present);
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
            // disable TOGGLE paints one faded half beside one live one and
            // the direction of the toggle reads straight off the label. Each
            // bit is meaningful exactly when its half is painted, so a bit
            // resting at false on a half that does not exist is never
            // consulted.
            const bool removed_disabled = f.then_effective_disabled;
            const bool added_disabled   = f.now_effective_disabled;
            const bool inert0 = added_first ? added_disabled : removed_disabled;
            const bool inert1 = added_first ? removed_disabled : added_disabled;
            for (int s = 0; s < 2; ++s) {
                CueSegment& seg = c.seg[static_cast<std::size_t>(s)];
                if (!present[s]) continue;
                seg.present  = true;
                seg.run      = s == 0 ? run0 : run1;
                seg.x0       = c.col + boxes.x0[s];
                seg.x1       = c.col + boxes.x1[s];
                seg.selected = focused;
                seg.inert    = s == 0 ? inert0 : inert1;
            }

            // THE TRIANGLE AND THE DOTS through the live lane's one resolver
            // (resolve_flag_face) by the KIND of each half — a diff line is
            // never the invalid class (HistoryDiffFlag's note). A SINGLE
            // flag: the removed (the minus) the invalid color with its dots
            // alone, the added (the plus) the cue red with its dots alone
            // (2026-10-11); THE DOTS READ THE
            // DISABLED AXIS (architect 2026-08-22): a single half whose one
            // side is disabled publishes none and wears the faded triangle,
            // the live lane's rule.
            if (!pair) {
                const GuiFlagKind kind =
                    f.removed ? GuiFlagKind::Removed : GuiFlagKind::Added;
                const bool disabled =
                    f.removed ? removed_disabled : added_disabled;
                c.face = resolve_flag_face(kind, disabled, /*red=*/false);
                cues.push_back(std::move(c));
                return;
            }
            // A CHANGED PAIR (architect 2026-10-10 / 2026-10-11, the
            // declaration's paragraph): THE SPLIT TRIANGLE, each half the
            // live face of the marker on its side, and THE TWO STEMS, each
            // at its own column in its own half's dots. A CHANGED PAIR
            // ALWAYS KEEPS ITS DOTS AND ITS LIVE TRIANGLE whichever halves
            // are disabled (2026-08-22): the pair is a live EDIT being
            // displayed, not a line in a switched-off state; the label's
            // segments carry each half's disabled bit.
            const FlagFace removed_face = resolve_flag_face(
                GuiFlagKind::Removed, /*disabled=*/false, /*red=*/false);
            const FlagFace added_face = resolve_flag_face(
                GuiFlagKind::Added, /*disabled=*/false, /*red=*/false);
            const FlagFace& left_face  = added_first ? added_face : removed_face;
            const FlagFace& right_face = added_first ? removed_face : added_face;
            c.face        = left_face;
            c.split       = true;
            c.split_right = right_face.triangle;
            c.own_stems   = true;
            c.stems[0] = MarkerStem{i, static_cast<double>(col_left),
                                    left_face.cue_phase_dots,
                                    left_face.range_phase_dots};
            c.stems[1] = MarkerStem{i, static_cast<double>(col_right),
                                    right_face.cue_phase_dots,
                                    right_face.range_phase_dots};
            // THE RECTANGLE'S TOP AND BOTTOM between the two stems, on a
            // nudged pair whose stems stand on two columns (MarkerStemRun,
            // render.h): published whenever any of it reaches the waveform's
            // columns, the painter clipping it there.
            if (out_runs && col_right > col_left &&
                col_right >= lanes.columns_x &&
                col_left < lanes.columns_x + waveform_width)
                out_runs->push_back(MarkerStemRun{
                    i, static_cast<double>(col_left),
                    static_cast<double>(col_right)});
            cues.push_back(std::move(c));
        });

    paint_cues(cr, lane, lanes.columns_x, waveform_width, cues, out_hit_rects,
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
void set_surface_source(cairo_t* cr, GuiColor c) {
    if (!materials::set_source(cr, c)) set_palette_source(cr, c);
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
    // THE LIVE WORDS (render.h's program_palette_words): the eleven as
    // the install family last wrote them.
    GuiPaletteWords   g_program_words{};
} // namespace

const GuiPalette& palette() { return g_palette; }

GuiColor surface_text(GuiSurface surface) {
    // The table and its readers are at the declaration (render.h).
    const GuiPalette& pal = palette();
    switch (surface) {
    case GuiSurface::ListRow:         return pal.field_text;
    case GuiSurface::ListRowLit:      return pal.selected_text;
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

namespace {
// THE PROGRAM'S ELEVEN into the installed struct and the plate's two baked
// inks off the same words — the install family's first shared half (the
// chrome's members, derived from the same Face, are the other's; the
// generation is each member's own bump) — with THE PANEL'S FACE THE
// GROUND'S MEAN (materials.h's TONE RULE, 2026-10-23: the ring tones and
// the chrome's roles off it; its estimate before the first bake,
// construction state no frame paints).
uint32_t panel_face(const GuiPaletteWords&) {
    // THE GROUND'S MEAN (materials.h's TONE RULE, 2026-10-23), its estimate
    // before the first bake; the preset's own `face` is read by the
    // static_asserts alone.
    return materials::face(GuiMaterial::Ground);
}
void fill_program_palette(const GuiPaletteWords& w) {
    g_program_words = w;
    for (std::size_t i = 0; i < kGuiPaletteRoleCount; ++i)
        g_palette.*(kGuiPaletteRoles[i].member) = hex(w[i]);
    // THE COOL EDIT BLOCK, derived from the panel's face by Cool Edit's own
    // rule (cool_edit_derive.h). The view bar's span bevels derive from the
    // waveform's ink instead (ToneSource::Ink), so a new ink moves them live.
    const uint32_t face = panel_face(w);
    static constexpr std::size_t kInk = palette_role_index("waveform_ink");
    g_palette.face = hex(face);
    for (const cool_edit_derive::Tone& t : cool_edit_derive::kTones)
        if (t.member != nullptr)
            g_palette.*(t.member) =
                hex(cool_edit_derive::tone_of(t, face, w[kInk]));
    // THE HI-FI SKIN'S TONES OVER THE COOL EDIT BLOCK (materials.h's TONE
    // RULE, 2026-10-23): every ring, line and edge the two ring tones off
    // the ground's mean — the program ring, the clock's ring, the divider,
    // the fields' rings — and the ruler's ground the flat kRulerGround.
    g_palette.ce_hilight     = hex(ring_light_of(face));
    g_palette.ce_dark        = hex(ring_dark_of(face));
    g_palette.ce_field_light = hex(ring_light_of(face));
    g_palette.ce_field_dark  = hex(ring_dark_of(face));
    g_palette.ce_mid         = hex(kRulerGround);
    // THE ACCENT (materials.h's accent_of_ink, architect 2026-10-23): the
    // live preset's ink's hue under the LED's own lightness and saturation,
    // the lit lamps' one tone, their glow patches rebuilt with it.
    materials::set_accent(accent_of_ink(w[kInk]));
    // The panel label's pair, swapping past its own threshold (2026-10-10).
    g_palette.ce_label = hex(cool_edit_derive::label_ink(face));
    g_palette.ce_label_shadow = hex(cool_edit_derive::label_shadow(face));
    // The disabled cue's faded triangles, shadow and label (2026-10-10): the
    // label from the bone, ALL FOUR MIXED TOWARD THE MARKER LANE'S MEAN, the
    // ground the cues stand on (architect 2026-10-22, truthful to the
    // surface).
    {
        using namespace cool_edit_derive;
        const uint32_t face = materials::face(GuiMaterial::Marker);
        static constexpr std::size_t kCue = palette_role_index("cue");
        static constexpr std::size_t kInvalid =
            palette_role_index("invalid_label");
        g_palette.ce_off_cue = hex(disabled_triangle(w[kCue], face));
        g_palette.ce_off_invalid =
            hex(disabled_triangle(w[kInvalid], face));
        g_palette.ce_off_cue_shadow = hex(disabled_cue_shadow(face));
        g_palette.ce_off_label = hex(disabled_cue_label(kBoneInk, face));
    }
    static constexpr std::size_t kOutline =
        palette_role_index("waveform_outline");
    static_assert(kInk < kGuiPaletteRoleCount &&
                  kOutline < kGuiPaletteRoleCount);
    g_plate_inks = WaveformPlateInks{w[kInk], w[kOutline]};
}
// THE CHROME'S MEMBERS off the live words (live_chrome_words,
// chrome_derive.h: the hi-fi skin's constants and the ring tones off the
// ground's mean, 2026-10-23) — the install family's other shared half, run
// after the program's whenever either moves.
void fill_chrome_palette() {
    // THE CHROME OFF THE GROUND'S MEAN (materials.h's TONE RULE).
    GuiPaletteWords words = g_program_words;
    words[palette_role_index("face")] = panel_face(g_program_words);
    const GuiThemeWords w = live_chrome_words(words);
    for (std::size_t i = 0; i < kGuiThemeRoleCount; ++i)
        g_palette.*(kGuiThemeRoles[i].member) = hex(w[i]);
}
} // namespace

void install_palette(const GuiPaletteWords& program) {
    // The program's fields are filled off `program`, THE LIVE WORDS the
    // caller resolved (render.h: the launch's palette_record of the
    // `waveform` key), then the chrome's derived from them
    // (live_chrome_words, chrome_derive.h, which never misses), the two
    // tables covering the struct exactly (palette_file.cpp), so a role
    // cannot be valued and not painted.
    fill_program_palette(program);
    fill_chrome_palette();
    ++g_palette_generation;
}

void install_program_palette(const GuiPaletteWords& words) {
    // The contract and the apply shape the caller owes are at the
    // declaration (palette_file.h): the chrome re-derived from the new
    // words.
    fill_program_palette(words);
    fill_chrome_palette();
    ++g_palette_generation;
}

void install_true_colors(bool on) {
    // The contract, the caches it rebuilds and the apply shape the caller
    // owes are at the declaration (render.h).
    display_transform::set_true_colors(on);
    ++g_palette_generation;
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
    // fill (cue_fill_h_px, the whole lane since 2026-10-09 ~21:00), standing
    // exactly where the resting segment's box does.
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
    // The pad is one quantum (2026-10-09, the cue's label: the fill 1 W past
    // the text's end) and THE CARET ONE DEVICE PX (render.h's line rule, the
    // sweep of 2026-10-23; one quantum until then, the borrow then taking
    // the whole pad), so the borrow takes the caret's column out of the
    // right pad: a caret at end-of-text stands in the pad's first column,
    // inside the box — visible ink, not a lost one, so the travel
    // arithmetic below needs no floor of its own. The TEXT VIEWPORT is what
    // widens by the borrowed column; the box never does.
    const int caret_px = std::min(hairline_px(), pad_r);

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
    // exactly where the committed text stands. The caret one device px, the
    // borrow takes that one column of the pad and the viewport ends the
    // rest of the pad short of the box's right edge.
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
    // which keep their resting look: the faded inert label, a tie follower's
    // cells faded (2026-09-19), else the bone, or an invalid
    // marker's invalid color (below).
    const bool dis = phase ? pmv[static_cast<size_t>(idx)].disabled
                           : effective_disabled(mv, idx);
    // THE MARKER'S INVALID CLASS, off the same memoized red-flag set the flag
    // pass reads for this column (the flag cache's rebuild,
    // waveform_cache.cpp), so the field and the resting label agree on it;
    // DISABLED WINS, as in the flag pass (resolve_flag_face).
    const bool red =
        !dis &&
        (phase ? phase_reset_red_flag_set_cached(app).red.count(idx) > 0
               : warp_red_flag_set_cached(
                     app, audio.sample_rate(),
                     static_cast<long>(audio.total_frames()))
                         .red.count(idx) > 0);
    // THE FIELD IS WINDOWS' EDIT FIELD (architect 2026-10-09 ~12:15,
    // reversing 2026-10-07 ~09:45's "the flag in its selected face, no box
    // and no frame"): the chrome's FIELD PAIR — `field_ground` under
    // `field_text` (white under black under Windows 2000; every chrome's own
    // pair, read as roles) — on every state of the marker, disabled included
    // (the triangle carries the state). WHY: the field pair is the
    // legibility guarantee ("when you're looking very closely at a label it
    // has to be legible"), and THE SELECTED SUBSTRING takes the chrome's
    // SELECTED PAIR, `selected_text` on `selected_fill`, the ordinary
    // highlight — the pair that exists "precisely to avoid this problem" —
    // part of the chrome, as every selection in the product is: the program
    // constant pair kSelectionFill / kSelectionText since 2026-10-21
    // (chrome_derive.h's kChromeRoles). AN INVALID
    // MARKER'S FIELD IS THE SAME FIELD, its text and caret the field's own
    // (architect 2026-10-11: "No flags change color when you select them …
    // the flag should be the indicator that it's invalid"; the bright
    // selected red of 2026-10-09 retired with its role); `red` colors its
    // riding segments alone, their resting invalid color.
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

    // 1. THE FIELD IS A BOX OVER THE WHOLE LANE (cue_fill_h_px, rows 0 ..
    //    16, the selected label's box; architect 2026-10-09 ~21:00): the
    //    segment's box — its run and one quantum of pad either side, the
    //    text standing exactly where the resting label's does — with A
    //    ONE-QUANTUM OUTLINE in `field_text` round it, outside the pads on
    //    the left and right and on the box's first and last rows (the lane's
    //    top air row and the triangle's apex row), `field_ground` inside. IT
    //    PAINTS OVER THE LANE — the triangle's right edge and shadow and any
    //    neighbouring label it reaches (architect
    //    2026-10-09: "it may overlap with the flag and other elements, that's
    //    fine"). A refused Enter recolors nothing (text_editor::refuse selects
    //    the whole text; the owner's card says why). Since the editor opens on
    //    any store index (enter_top_flag_edit), disabled included, a disabled
    //    marker's field is this same box, never faded.
    const GuiRect field_box{bx - u, lane.y, box_w + 2 * u, fill_h};
    paint_cell_rect(cr, field_box, pal.field_text);
    paint_cell_rect(cr, GuiRect{field_box.x + u, field_box.y + u,
                                field_box.w - 2 * u, field_box.h - 2 * u},
                    field_fill);

    // TWO SETS OF ROWS. THE TEXT VIEWPORT IS THE FIELD'S WHOLE INNER HEIGHT,
    // inside the outline (2026-10-09, the field the whole lane since ~21:00)
    // — so a descender or a history bracket below the lane's baseline paints
    // whole (rows 12 .. 14 of the 17 at 100 %, 37 .. 43 of the 51 at 300 %,
    // render.h's descent assert). THE CARET AND THE SELECTION BAND ARE THE
    // TEXT'S LINE BOX (architect 2026-10-09 ~23:30, render.h's EDITING
    // paragraph): the program face's recorded ascent above the baseline and
    // its descent below it, the dialog field's own band rule
    // (paint_modal_dialog) — Windows' edit control fills its line with the
    // selection, and this field round a smaller line leaves rows of field
    // above and below the band. THE BOX IS CENTRED IN THE LANE and the
    // baseline is defined from it (architect 2026-10-09 evening, on the
    // glass at 300 %: the band stood three rows of field above and six
    // below, and he asked for it even): the band's rows are read off the one derivation the
    // baseline is (cue_line_box_top_px, cue_line_box_h_px, render.h), never
    // re-derived from the baseline — at 100 % rows 2 .. 13 of the inner
    // 1 .. 15, at 138 % rows 2 .. 18 of 1 .. 20, at 300 % 7 .. 42 of 3 .. 47,
    // at 360 % 10 .. 52 of 4 .. 58.
    const int view_y = lane.y + u;
    const int view_h = fill_h - 2 * u;
    const int band_y = lane.y + cue_line_box_top_px();
    const int band_h = cue_line_box_h_px();

    // Everything from here paints CLIPPED to the text viewport INSIDE THE
    // FIELD, so a scrolled run, its selection and its caret all stop at the
    // pads instead of bleeding over the box edge into the neighboring labels.
    cairo_save(cr);
    cairo_rectangle(cr, view_x0, static_cast<double>(view_y),
                    view_w, static_cast<double>(view_h));
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
    //    sits on. The band is the line box inside the taller viewport (the
    //    rows block above), so the complement is the columns left and right
    //    of it AND the rows above and below it — the dialog field's
    //    complement (paint_modal_dialog). With no selection the run paints
    //    unclipped inside the viewport.
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
        // viewport with the band cut out of it (even-odd), so the columns
        // left and right of the band and the rows above and below it — the
        // outer viewport clip still bounding a band that runs past the
        // viewport's side. A selection that fills the viewport's width
        // leaves the field-text run the rows above and below the band
        // alone, where a selected glyph's deeper ink takes the field text.
        cairo_rectangle(cr, view_x0, static_cast<double>(view_y), view_w,
                        static_cast<double>(view_h));
        cairo_rectangle(cr, static_cast<double>(ix0),
                        static_cast<double>(band_y),
                        static_cast<double>(band_w),
                        static_cast<double>(band_h));
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
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
    // Each segment wears its resting look — its box in the panel face (the
    // lane's brushed strip over the canvas's columns, the panel's ground past
    // them), the text
    // in the bone, an invalid marker's
    // dim red or the
    // faded inert label — and none takes
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
        const GuiRect canvas_cols = waveform_area(app);
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
        // THE RUN IS NOT CLIPPED (architect 2026-10-10 ~19:00): the riding
        // cells paint where the paint order puts them. The editor paints
        // after the lane pass, so the run stands over a neighbour's label and
        // triangle as the field itself does (render.h's EDITING paragraph,
        // "it may overlap with the flag and other elements, that's fine").
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
            // label's one box covers its gaps — the lane's brushed ground as
            // drawn (paint_cues' box, materials.h's MARKER) over the canvas's
            // columns, THE PANEL'S GROUND past them (the run is not clipped,
            // so a ride near the right edge reaches the margin, materials.h's
            // REGIONS).
            const int fx = std::max(cursor_x, face_x0);
            const int fx1 = x0 + w;
            const int brass1 = std::clamp(canvas_cols.x + canvas_cols.w, fx, std::max(fx, fx1));
            if (brass1 > fx) {
                const GuiMaterialScope marker(GuiMaterialRegion::Marker);
                paint_cell_rect(cr, GuiRect{fx, lane.y, brass1 - fx, fill_h},
                                pal.face);
            }
            if (fx1 > brass1) {
                const GuiMaterialScope ground(GuiMaterialRegion::Ground);
                paint_cell_rect(cr, GuiRect{brass1, lane.y, fx1 - brass1, fill_h},
                                pal.face);
            }
            const double tx = static_cast<double>(x0 + pad_l);
            // The resting label's ink: the faded inert label, else an invalid
            // marker's invalid color, else the bone
            // (resolve_flag_face's table; materials.h's INKS).
            show_cue_label(cr, seg_run, tx, baseline,
                           cell_dis ? pal.ce_off_label
                           : red    ? pal.invalid_label
                                    : hex(kBoneInk),
                           dis ? pal.ce_off_cue_shadow : pal.ce_cue_shadow);
            cursor_x = x0 + w;
        };
        // Both handles stand: either ride needs cl.present.
        if (ride_lower) ride(*cl.lower_run, cl.lower_w);
        const int upper_seam = cursor_x;
        if (ride_upper) ride(*cl.upper_run, cl.upper_w);

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
        // `box`. It ends where the pixels end.
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
