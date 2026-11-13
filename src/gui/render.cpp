#include "render.h"
#include "app_state.h"
#include "audio.h"
#include "canvas_sink.h"    // the canvas painters' one road (GuiCanvasSink)
#include "gui_display_context.h"
#include "gui_font.h"
#include "text_shape.h"
#include "theme_file.h"
#include "palette_file.h"   // kGuiPaletteRoles (the program's roles)
#include "cool_edit_paint.h"   // the canvas column's painters and constants
#include "theme_file.h"     // kGuiThemeRoles (the chrome's roles)
#include "materials.h"      // paint_key, KeyLook, KeyLitClass
#include "value_format.h"
#include "warp_frame_map_view.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
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
// cue_triangle_reach_right_px (render.h, the chip's), the one extent both
// read.

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
// area's rows: rows [row − (t − 1) / 2, row − (t − 1) / 2 + t) — an even
// line has no center row, so its TOP row is the row it stands on (at t = 2
// the row and the one under it); an odd line is centered. The grid's and
// the center lines' one row rule (render_canvas, render_center_lines).
// THE DELIBERATE ASYMMETRY (architect 2026-11-06, session 66 / 67, the
// straddle ruled for the verticals alone): A VERTICAL LINE STRADDLES ITS
// FRAME (render.h's waveform_line_first_col) because a frame is a pixel
// BOUNDARY, the left edge of its column; THESE ROWS DO NOT, because the
// row they stand on is a pixel CENTER. The plate's zero is the band's
// y + h · 0.5 (render_waveform_columns' y_center), the zero row y + h / 2
// its floor (WaveformChannelBand::zero_row), and the band's h is ODD on the
// tablet — the canvas 1085 rows (243 .. 1327 at gui_scale 200, key_height
// 17), the divider 2, each band (1085 − 2) / 2 = 541 rows, odd at every
// key_height (one W of key_height moves the canvas by 4 device rows) — so
// the zero lies on the middle of row y + 270 and no 2-row line centers on
// it: these rows [r, r + 2) stand half a row low, a straddle's
// [r − 1, r + 1) half a row high — no gain, so the rows keep their top-row
// frame.
void canvas_hline(CanvasLines& lines, const GuiRect& area, int row, int t) {
    const int first = area.y + canvas_hline_first_row(row, t);
    const int top = std::max(area.y, first);
    const int bot = std::min(area.y + area.h, first + t);
    if (bot > top) {
        assert(lines.count < static_cast<int>(lines.rects.size()));
        lines.rects[static_cast<std::size_t>(lines.count++)] =
            GuiRect{area.x, top, area.w, bot - top};
    }
}
} // namespace

namespace {
// THE GRID'S RECTS across `area` (its columns; its rows the canvas's): the
// one row rule render_canvas reads.
void canvas_grid_lines(CanvasLines& lines, const GuiRect& area) {
    // THE LINES UNDER THE WAVEFORM, in render.h's row-6 canvas order: the
    // horizontal grid — t = waveform_line_px() thick, ONE W (2 device px on
    // the tablet at 200 %, the unit, architect 2026-11-04; the class's one inventory
    // at render.h's waveform_line_px), each an aliased integer rect inside
    // its band. THE CENTER LINES STAND OVER THE WAVEFORM
    // (architect 2026-10-09 evening), render_center_lines below, a pass after
    // the plate. NO VERTICAL GRID (architect 2026-10-09 ~18:40, the
    // rule and its reason at render.h's row-6 canvas paragraph).
    const int t = waveform_line_px();
    // THE HORIZONTAL GRID per channel, on the plate's own bands
    // (waveform_channel_band): a line on a row r covers rows
    // [r − (t − 1) / 2, r − (t − 1) / 2 + t) — the row r and the one under it
    // on the tablet (t = 2, the rows' top-row frame, canvas_hline's
    // asymmetry) — the zero row being
    // the row the plate's bars straddle; the k-th grid line stands t ·
    // nearbyint(k · (H / t) / 4) device rows from the zero row (at t = 2, 2 ·
    // nearbyint(k · H / 8), an even number of rows), H the channel's half
    // height in device rows — k = 1 .. 3, the fourth on the band's edge not
    // drawn. Each band is its well's
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
}
// THE CENTER LINES' RECTS across `area`, the same way.
void canvas_center_lines(CanvasLines& lines, const GuiRect& area) {
    // THE CENTER LINE per channel ON ITS ZERO ROW, OVER THE WAVEFORM'S INK
    // (architect 2026-10-09 evening: "it's supposed to be visible over the
    // waveform. Otherwise, in a project like this with tape hiss, there's
    // never enough zero that the red line would become visible ever"; the
    // order at render.h's row-6 canvas paragraph): one W, the grid's row
    // rule (canvas_hline), in the palette's `center`, the channels'
    // bands the plate's own (waveform_channel_band), so it stands on the row
    // the plate's bars straddle. Through the canvas sink (2026-10-13).
    const int t = waveform_line_px();
    const int divider_px = waveform_divider_px();
    for (int ch = 0; ch < 2; ++ch) {
        const WaveformChannelBand band =
            waveform_channel_band(area.h, divider_px, ch);
        if (band.h > 0) canvas_hline(lines, area, band.zero_row(), t);
    }
}
// THE DIVIDER'S ROW across `area`, cut to the area's rows (an empty rect
// where it falls outside them).
GuiRect canvas_divider_rect(const GuiRect& area) {
    const WaveformDivider div = waveform_divider(area.h);
    const int top = std::max(area.y, area.y + div.y);
    const int bot = std::min(area.y + area.h, area.y + div.y + div.h());
    if (bot <= top) return GuiRect{0, 0, 0, 0};
    return GuiRect{area.x, top, area.w, bot - top};
}
} // namespace

// THROUGH THE CANVAS SINK (2026-10-13, canvas_sink.h): the ground's rect and
// the lines' rects as list records. The stereo divider's row is
// render_waveform_divider's, a pass of its own over the plate (below).
void render_canvas(GuiCanvasSink& sink, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    const GuiPalette& pal = palette();
    // The ground is the `waveform_canvas` role (the palette's row-6 block),
    // display_color applied by the sink.
    sink.rects(std::span<const GuiRect>(&area, 1), pal.waveform_canvas);
    CanvasLines lines;
    canvas_grid_lines(lines, area);
    sink.rects(lines.span(), pal.grid);
}

void render_center_lines(GuiCanvasSink& sink, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    CanvasLines lines;
    canvas_center_lines(lines, area);
    sink.rects(lines.span(), palette().center);
}

// THE STEREO DIVIDER (architect 2026-10-16, Cool Edit's two wells; flat
// since 2026-11-01 night; the rule and its row at render.h's
// waveform_divider, the one geometry owner): across the area's columns, ONE
// ROW OF THE GROUND (the palette's `face`, `ground`) cut to the area's rows —
// CHROME ON THE CANVAS, painted through the sink so the GL canvas draws it
// from its list (canvas_sink.h's head). ITS OWN PASS, AFTER THE PLATE AND THE CENTER
// LINES and BEFORE EVERY VERTICAL LINE (build_canvas_list's order): the
// plate's bands and the horizontal lines stand inside the wells and every
// input leaves the row alone (point_on_waveform_divider: chrome, inert), so
// over the plate the order changes no pixel in the steady state; it is there
// for A PLATE FROM BEFORE A RESIZE, which stands at its old bands until the
// worker republishes (paint_waveform_plate draws whatever plate is
// published), and whose ink would otherwise cross the live divider's row.
// Painted after the plate, the live divider is on top whatever plate stands;
// painted before the lead-in ring, the stems, the cursor, the scanner and
// the anchor, it lies UNDER them, each crossing it as any grid line
// (architect 2026-11-02: "the divider is just another grid line now").
void render_waveform_divider(GuiCanvasSink& sink, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    const GuiRect r = canvas_divider_rect(area);
    if (r.w <= 0 || r.h <= 0) return;
    sink.rects(std::span<const GuiRect>(&r, 1), palette().face);
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
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    set_palette_source(cr, c);
    cairo_fill(cr);
}

void paint_ink_rect(cairo_t* cr, const GuiRect& r, GuiColor c) {
    if (r.w <= 0 || r.h <= 0) return;
    set_palette_source(cr, c);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_fill(cr);
}

// (THE RELIEF FAMILY — paint_relief_frame, paint_relief_edge,
// paint_relief_plain_sunken, paint_relief_line_frame, every line one scaled
// Windows px — went 2026-10-23, and its one-device-px successor, the mitred
// two-tone ring, 2026-11-02 with the overlays' flat pass: render.h's NO
// RELIEF AND NO RING.)

// (THE WINDOWS PARTS' EDGES — paint_windows_soft_raised, _soft_sunken and
// _plain_raised in Windows 2000's Standard quartet, the caption buttons' and
// the sizing frame's, 2026-10-22 to 2026-10-23 — left with those parts; the
// hi-fi skin's caption and border that replaced them went 2026-10-24,
// render.h's caption note.)

// THE SCRUB'S THUMB (render.h's scrub block, 2026-10-23): the flat key on
// its box (materials.h's paint_key) — never lit (it is dragged, not turned
// on or off), so the enabled bit and the lit class, which part a lit key's
// faces alone, have nothing to say here (Lamp, every non-roster surface's
// class) — the press face while `pressed`, the owner's grab on the thumb
// (the press arc, architect 2026-11-05; the rule at the declaration).
void paint_scrub_thumb(cairo_t* cr, const GuiRect& box, bool pressed) {
    if (box.w <= 0 || box.h <= 0) return;
    paint_key(cr, box, key_look(/*checked=*/false, pressed),
              /*enabled=*/true, KeyLitClass::Lamp);
}

// THE COLUMN BOUNDS (2026-10-13; the contract is at the declaration, render.h):
// the plate's render, its product the quarter-row tips the GL canvas's plate
// rasterizes (waveform_cache.cpp's render_waveform_to_plate_columns;
// gl_canvas.cpp's kListFragmentShader).
bool render_waveform_columns(GuiRect area,
                             int col0,
                             const GuiAudio& audio,
                             int channel,
                             const WaveformBasis& basis,
                             const WaveformGainCurve* gain_or_null,
                             int plate_w,
                             int plate_h,
                             const std::vector<WarpFrameMapSegment>* warp_frame_map,
                             WaveformBandColumns& out) {
    if (area.w <= 0 || area.h <= 2) return false;
    if (basis.full_width <= 0) return false;
    // The lattice step must be numeric and positive; a degenerate zoom refuses
    // here exactly as the old empty-viewport check did.
    if (!(basis.spp > 0.0)) return false;

    const int num_levels = audio.num_levels();
    if (num_levels <= 0) return false;
    if (plate_w <= 0 || plate_h <= 0) return false;

    // THE AUTHORING LATTICE (see WaveformBasis). Recover the viewport's lattice
    // index on ITS OWN LATTICE, of origin basis.origin, with the SAME
    // expression clamp_viewport_start uses to snap onto it
    // (nearest_viewport_grid_point), so a resting viewport round-trips
    // exactly. Columns are then indexed globally from k0, which is what makes
    // a pan a pure index shift — a column's frames depend on the origin and
    // k0+c and nothing else, never on which window drew it.
    const double samples_per_pixel = basis.spp;
    double k0d = std::nearbyint(
        static_cast<double>(basis.vp_start - basis.origin) /
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
    // from the column's own span restores the intended per-column bound (at
    // most 4 + 6(L-1) pairs and 30 raw samples at ladder level L, or at most
    // 16 raw samples below the finest stride, unconditionally — the statement
    // and its proof live at GuiAudio::level_for_span).
    //
    // The level is a COST choice only (architect 2026-10-30): get_peak_range
    // reads the exact extrema of [s0, s1) at every level, so where the map
    // slope crosses a stride threshold and adjacent target-view columns read
    // different levels, the picture shows no seam, and a zoom across a rung
    // changes nothing but the read's cost.
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

    // A column is ITS TWO TIPS IN QUARTER ROWS (architect 2026-10-30; the
    // rule is at this function's declaration): its own raw min/max interval
    // quantized to the 4x grid, the bar the subrows [T, B − 1], which the
    // GPU fills in full ink as the shape's core, joins to its neighbors by
    // the contour through the column centers, grows by the `waveform_grow`
    // key and rasterizes at exact coverage (with the lamp lit, the inner bar
    // stands over the outer's ink, the grown inner filled solid in the core,
    // on the GPU). This
    // function reads no neighbor and holds no interior: every column is its
    // own tips, so a column's tips are a pure function of its own span and
    // the picture a pure function of the plate's tips.
    //
    // THE ANTIALIASING RETURNED 2026-10-30 IN ANOTHER FORM — exact coverage
    // on the GPU from these tips; what the 2026-08-01 deletion took (the Wu
    // tip polylines, their max-coverage compositing, the fractional boundary
    // rows, the coverage table, the edge halos) stays in git and in
    // docs/engineering/waveform_antialiasing_retired.md, none of it here.
    //
    // THE NEVER-FADE FLOOR IS A QUARTER ROW (bar_tips, below;
    // kWaveformFloorSubcells, render.h): an interval under a quarter row
    // takes one subrow, which the GPU's grow (the disc dilation by the
    // `waveform_grow` key, two subcells by default) makes a line of — at
    // the default nonzero coverage over two device rows, about one and a
    // quarter device px of summed coverage (20 of 16 subcells) whichever
    // quarter-row phase the tip has — so flat or silent material draws a
    // soft hairline instead of fading out.

    // Row bounds: this channel's band, intersected with the plate.
    int y_lo = area.y;
    int y_hi = area.y + area.h;          // exclusive
    if (y_lo < 0)       y_lo = 0;
    if (y_hi > plate_h) y_hi = plate_h;
    if (y_hi <= y_lo) return false;

    // COLUMNS THIS CALL OWNS, both ends clamped into [0, plate_w) here, once:
    // the bounds name no column outside them, so a partial render, if one is
    // ever reintroduced, cannot bleed into a neighbour's columns.
    int col_lo = area.x;
    int col_hi = area.x + area.w;
    if (col_lo < 0)       col_lo = 0;
    if (col_hi > plate_w) col_hi = plate_w;
    if (col_hi <= col_lo) return false;

    // Global column c's display-domain edge, AS THE LATTICE POINT ITSELF:
    // g(k0+c) = origin + nearbyint((k0+c)*spp), bit-for-bit the integer
    // the chokepoint's snap produces (origin + viewport_grid_point(k0+c, q);
    // the origin a whole frame, added after the rounding, so the edge stays
    // integral). THE QUANTIZE LIVES HERE,
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
        return static_cast<double>(basis.origin) +
               std::nearbyint(static_cast<double>(k0 + c) * samples_per_pixel);
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

    // THE BAR'S TIPS IN QUARTER ROWS (architect 2026-10-30). The column's
    // tips — its maximum -> top tip, its minimum -> bottom tip, in float rows
    // — are clamped to this channel's rows BEFORE any subrow is derived, so a
    // clipped interval cannot address outside the band; then each is
    // quantized to the nearest quarter row, T = nearbyint(4 · top) and B =
    // nearbyint(4 · bottom), and the bar is the subrows [T, B − 1] (the
    // center rule: subrow k's center k + 0.5 lies in [T, B] iff T <= k <=
    // B − 1). The band's subrows are [4 · y_lo, 4 · y_hi), and both tips land
    // in [4 · y_lo, 4 · y_hi] exactly (the clamps' ends are whole rows). THE
    // NEVER-FADE FLOOR IS A QUARTER ROW (kWaveformFloorSubcells = 1,
    // render.h; the reason there): an interval under a quarter row has
    // B == T and an empty bar — digital silence at the zero row is the
    // archetype — and TAKES ONE SUBROW, B = T + 1, T pulled down one where it
    // sat on the band's last edge (T == 4 · y_hi) so the subrow lies inside
    // the band; the GPU's grow then makes a line of it (at the default grow
    // five subrows, nonzero coverage over two device rows and about one and
    // a quarter device px of summed coverage at any tip phase, Ableton's
    // measured silence line).
    // Nothing can fade out or vanish, and it holds for both bars AT BOTH LANE
    // EDGES: a bar clamped whole to the top edge is the band's first subrow,
    // one clamped whole to the bottom edge (both tips at -1) its last. The
    // floor is needed because an interval under a quarter row holds no
    // subrow center and would otherwise rasterize to nothing; the contour
    // needs none, since it only ever joins two bars that exist. The bar
    // is never empty: the column's minimum is at most its maximum and a
    // tip's scale is positive, so yt <= yb, nearbyint is monotone, and the
    // floor keeps B >= T + kWaveformFloorSubcells. One owner for both bars
    // and both lamps, so the
    // outer and the inner cannot disagree about the geometry; a present
    // inner is told from the absent one by B > T alone (WaveformColumnRows).
    struct BarTips {
        int top;
        int bot;   // exclusive: the bar's subrows are [top, bot − 1]
    };
    const int sub_lo = kWaveformSubcells * y_lo;
    const int sub_hi = kWaveformSubcells * y_hi;   // exclusive
    const auto bar_tips = [&](double tip_min, double tip_max) {
        double yt = y_center - tip_max * half_h;
        double yb = y_center - tip_min * half_h;
        const double row_lo = static_cast<double>(y_lo);
        const double row_hi = static_cast<double>(y_hi);   // exclusive
        if (yt < row_lo) yt = row_lo;
        if (yb < row_lo) yb = row_lo;
        if (yt > row_hi) yt = row_hi;
        if (yb > row_hi) yb = row_hi;
        int top = static_cast<int>(std::nearbyint(yt * kWaveformSubcells));
        int bot = static_cast<int>(std::nearbyint(yb * kWaveformSubcells));
        if (bot < top + kWaveformFloorSubcells) {
            bot = top + kWaveformFloorSubcells;
            if (bot > sub_hi) {
                top = sub_hi - kWaveformFloorSubcells;
                bot = sub_hi;
            }
        }
        assert(sub_lo <= top && top + kWaveformFloorSubcells <= bot &&
               bot <= sub_hi);
        return BarTips{top, bot};
    };

    // THE BARS' TIPS, one per column of this call: the dark bar or the lit
    // outer in `outer_tips`, the lit inner in `inner_tips`. The one caller
    // renders the full plate, so this call's columns ARE the plate; the
    // neighbors a column's picture reads are the GPU's business.
    std::vector<BarTips> outer_tips(static_cast<size_t>(area.w));
    std::vector<BarTips> inner_tips;
    if (gain_or_null) inner_tips.resize(static_cast<size_t>(area.w));

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
        // peaks already rest in range).
        if (gain_or_null) {
            const int64_t centre = (s0 + s1) / 2;
            const double e = static_cast<double>(
                waveform_expander_multiplier_over(*gain_or_null, s0, s1));
            const double outer = waveform_gain_at(*gain_or_null, centre) * e;
            const double inner = waveform_inner_scale_at(*gain_or_null, centre) * e;
            outer_tips[static_cast<size_t>(i)] =
                bar_tips(magnified_tip(mm.first, outer),
                         magnified_tip(mm.second, outer));
            inner_tips[static_cast<size_t>(i)] =
                bar_tips(magnified_tip(mm.first, inner),
                         magnified_tip(mm.second, inner));
        } else {
            outer_tips[static_cast<size_t>(i)] =
                bar_tips(magnified_tip(mm.first, 1.0),
                         magnified_tip(mm.second, 1.0));
        }

        g_prev = g1;
    }

    // EACH OWNED COLUMN'S TIPS (WaveformColumnRows, render.h): the outer's
    // [o_top, o_bot) and the lit inner's [i_top, i_bot) in quarter rows, the
    // absent inner (the dark lamp's) 0, 0 — the bounds the GL canvas's plate
    // rasterizes.
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
        const BarTips& o = outer_tips[static_cast<size_t>(i)];
        WaveformColumnRows& cw = out.cols[static_cast<size_t>(x - col_lo)];
        cw.o_top = o.top;
        cw.o_bot = o.bot;
        cw.i_top = 0;
        cw.i_bot = 0;
        if (gain_or_null) {
            const BarTips& b = inner_tips[static_cast<size_t>(i)];
            cw.i_top = b.top;
            cw.i_bot = b.bot;
        }
    }
    return true;
}

namespace {
// ONE SOLID CANVAS LINE DOWN THE AREA: the waveform_line_px()-wide line on
// column `col`, straddling its frame (its column gate and its clip at both
// edges, waveform_line_cols) as one rect over the area's whole height, the stereo
// divider's row crossed (architect 2026-11-02, render.h's waveform_divider)
// — the scanner's and the zoom anchor's one form, one sink fill.
void canvas_line_rect(GuiCanvasSink& sink, const GuiRect& area, int col,
                      GuiColor color) {
    const WaveformLineCols c = waveform_line_cols(area.w, col);
    if (c.w <= 0 || area.h <= 0) return;
    const GuiRect r{area.x + c.x0, area.y, c.w, area.h};
    sink.rects(std::span<const GuiRect>(&r, 1), color);
}
} // namespace

void render_playhead(GuiCanvasSink& sink,
                     GuiRect area,
                     int     col,
                     GuiColor color) {
    if (area.w <= 0 || area.h <= 0) return;
    // Allow partial render at file start / end: a playhead whose column has
    // clipped just past the area edge still gets here, and the column gate
    // below decides whether any pixel lands. This keeps the playhead's visual
    // center aligned with its true frame position rather than snapping it
    // inward at the rightmost samples. The bound is the head's painted reach
    // (cue_triangle_half_w_px / cue_triangle_reach_right_px, render.h) — the
    // cull is stated in the same columns the damage box takes
    // (playhead_invalidate_rect, main.cpp).
    if (col < -cue_triangle_reach_right_px()) return;
    if (col > area.w - 1 + cue_triangle_half_w_px()) return;

    // THE COLUMN IS THE NEAREST LATTICE POINT WHILE THE PLATE'S BAR IN IT IS A
    // CELL, AND THE HALF-COLUMN BIAS THAT LEAVES IS ACCEPTED (architect
    // 2026-09-02, a coarse-zoom cost). THE NEAREST LATTICE POINT IS CHOSEN ON
    // THE LATTICE'S OWN INDEX (architect 2026-11-06, session 68): `col` is
    // the caller's playhead_pixel_x / scanner_pixel_x, displayed_column_at's
    // K − m on the plate basis — the same column the ruler's head stands on
    // and the damage box is built on (playhead_invalidate_rect) — so a tie
    // breaks in the lattice's coordinates, TO THE RIGHT (architect
    // 2026-11-06, session 69: the lattice point starts its pixel, so index K
    // owns [K − 0.5, K + 0.5), the reason at displayed_column_at), and the
    // line translates rigidly with the plate's global cells under a pan.
    // The playhead — and the marker stems, endcaps and flags, which all reach
    // their column through the same rule (frame_to_paint_sample's frame
    // rounding at the head of this file, then displayed_column_at) — paints
    // AT a lattice point, while the bar drawn
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
    // column): 0.413 px on the tablet (q = 23), and under half a pixel at
    // every waveform width, the
    // deepest-zoom floor holding q >= 20 (0.475 px; kDeepestZoomMinFramesPerPx,
    // app_state.h). Reading the bar as [g(c) − spp/2, g(c) + spp/2) is the
    // alternative that was declined.

    // The waveform_line_px()-wide line (render.h), on its column straddling
    // the frame (waveform_line_cols), paints whenever its column is onscreen (gated on its own column and
    // clipped at both edges), so
    // it never leaks into an adjacent region, straight over whatever it
    // crosses — waveform ink included. THE CANVAS'S WHOLE HEIGHT, the stereo
    // divider's row crossed (architect 2026-11-02, render.h's
    // waveform_divider): solid, the scanner and the resting cursor alike
    // (2026-11-04), one rect.
    canvas_line_rect(sink, area, col, color);
}

void render_strip_anchor_stem(GuiCanvasSink& sink, GuiRect area, int col) {
    if (area.w <= 0 || area.h <= 0) return;
    // The clamp is where the affordance lives: an anchor pushed to (or past) a
    // song edge pins to the edge column, so the stem draws exactly there.
    // The line is waveform_line_px() wide (render.h), on the column
    // straddling its frame, clipped at both edges (waveform_line_cols), so
    // an anchor
    // pinned to the first or the last column paints its columns inside.
    if (col < 0)          col = 0;
    if (col >= area.w)    col = area.w - 1;

    // THE ANCHOR STEM IS THE SCANNER'S WHITE (architect 2026-10-09: "the zoom
    // stem and the scanner are both white, which unifies them: the
    // non-playhead stems that are related to the controls should be white,
    // and then the playhead is yellow"): a control's line during a gesture,
    // as the scanner is playback's, in the one `scanner` role — the playhead
    // wears its own two roles (render.h's playhead paragraph).
    // The canvas's whole height (2026-10-09: no line crosses the row of
    // ground above the canvas; the stereo divider's row it crosses, architect
    // 2026-11-02).
    canvas_line_rect(sink, area, col, palette().scanner);
}

// -- Trim bound geometry owners -------------------------------------------
// One column formula, one mapping helper, one handle rect, one bridge-gap owner.
// See render.h for the full rationale (the UNIFIED displayed basis — the
// painter decides against the committed viewport and the hit sites read what
// it publishes, the event-sync ruling; the quantized-span denominator; the
// EOF-wall clamp; and the clip that slides an end off the edge).

TrimBoundColumn trim_bound_column(double displayed_ms,
                                  long long vp_start, long long vp_end,
                                  long long lattice_origin, int wave_w) {
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
                                  samples_per_pixel, lattice_origin);
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

TrimSpanColumns trim_span_columns(const TrimBoundColumn& begin,
                                  const TrimBoundColumn& end, int wave_w) {
    // Contract at the declaration. The end's +1 in 64 bits: an offscreen
    // end keeps its raw column (trim_bound_column), which the cut to
    // wave_w then holds.
    TrimSpanColumns s;
    s.lo = std::max(begin.col, 0);
    s.hi = static_cast<int>(std::min<int64_t>(
        static_cast<int64_t>(end.col) + 1, static_cast<int64_t>(wave_w)));
    return s;
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
                       long long viewport_lattice_origin,
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
        viewport_end_sample, viewport_lattice_origin, waveform_area.w);
    const TrimBoundColumn ec = trim_bound_column(
        static_cast<double>(trim.end), viewport_start_sample,
        viewport_end_sample, viewport_lattice_origin, waveform_area.w);

    // THE BAR'S COLUMNS ARE THE WELL'S (THE EDGE RULE, 2026-11-01 night):
    // waveform_area's x and its effective width, which are the well's
    // columns (THE WELL IS THE WAVEFORM AREA, architect 2026-11-03).
    const int lane_x   = waveform_area.x;
    const int lane_w   = waveform_area.w;   // the effective width
    const int lane_y   = trim_bar.y;
    const int lane_h   = trim_bar.h;

    // THE BAR (render.h's well paragraph; architect 2026-11-03, mock z2):
    // under the lane's first row of ground (the caller's), THE FILL'S ROWS
    // (trim_bar_fill_h_px) a FLAT `trim_bar_face` across the well's columns,
    // NO FRAME; the bar's last row the caller's
    // ground too (paint_trim erases the whole lane first), the one-W line
    // between the bar and the ruler, nothing painted over it here.
    const int px = hairline_px();
    const int bar_y = lane_y + px;
    const int fill_h = std::min(trim_bar_fill_h_px(), lane_h - px);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    if (fill_h > 0)
        paint_cell_rect(cr, GuiRect{lane_x, bar_y, lane_w, fill_h},
                        palette().trim_bar_face);
    cairo_rectangle(cr, lane_x, lane_y, lane_w, lane_h);
    cairo_clip(cr);

    const GuiRect lane{lane_x, lane_y, lane_w, lane_h};

    // THE SPAN — the trim window's columns, the begin column through the end
    // column inclusive, sliding off an edge like any content, cut to the
    // waveform's columns (trim_span_columns, the one owner the sweep
    // highlight reads too: THE TRIM REFLECTS THE WAVEFORM'S COLUMNS, even
    // where the waveform is wrong (architect 2026-11-03) — the bar's columns
    // are the waveform area's, nothing more, so a trim spanning the view
    // fills the bar end to end, and the rect stays in cairo's range at any
    // zoom): A
    // FLAT `trim_span_face` FILL on the fill's rows, no outline of its own
    // (architect 2026-11-03, mock z2). NO PLAYHEAD IN THE BAR (architect
    // 2026-10-09 ~14:30, "I thought we agreed no dots on the trim bar").
    const TrimSpanColumns span = trim_span_columns(bc, ec, lane_w);
    if (span.hi > span.lo && fill_h > 0)
        paint_cell_rect(cr, GuiRect{lane_x + span.lo, bar_y,
                                    span.hi - span.lo, fill_h},
                        palette().trim_span_face);

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

// THE STRIKE'S BAR OVER ONE STRUCK RUN — the contract and its two readers
// are at the declaration (render.h): the face's strike band under the run's
// baseline row, across the run's rounded advance.
GuiRect struck_run_bar(const GuiFont& font, double x, double width_px,
                       double baseline) {
    const GuiStrikeBand band = gui_font_strike_band(font);
    const int bx0 = static_cast<int>(std::nearbyint(x));
    const int bx1 = static_cast<int>(std::nearbyint(x + width_px));
    return GuiRect{bx0, static_cast<int>(std::nearbyint(baseline)) + band.dy,
                   bx1 - bx0, band.h};
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
// paints no stem. `col` is the marker's column relative to x0. The one spelling for both lane
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
// computed ONCE here: the chip's apex column, the label's anchor and the
// stem's column. The painter draws from it, the hit rects are published from
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
    long long viewport_lattice_origin,
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
    // admitted iff its chip intersects the waveform's columns [0, w) —
    // col - reach < w, the chip's widest row reaching its rows less one
    // device px left of its column (cue_triangle_half_w_px, render.h). So a marker just past
    // the last column (the song's last half-column at the right wall, or a
    // marker just past the viewport's end mid-song) is admitted and paints
    // its triangle's left part ALONE on the last columns — every caller clips
    // its paint and its hit rect to [0, w), so nothing paints in the leftover
    // strip a non-multiple-of-16 window leaves beside w. Its STEM is not
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
            ms, static_cast<double>(viewport_start_sample), samples_per_pixel,
            viewport_lattice_origin);
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
// (app_state.h) spelled across this painter's parameter boundary: the value
// drag asks that predicate whether a marker has bound cells to step, and
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
    // LEADER'S for a follower — one owner, so the cells and
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

// THE LABEL'S SEGMENT BOXES (render.h's marker-lane paragraph and the
// label's horizontal lengths), as offsets from the marker's column: the
// first present segment's box opens the fill lead past the column (the
// label's lead less the fill's pad, render.h's cue_fill_lead_px — the lead
// the fill's reach right, the chip's outline W and the gap, so the
// boxes follow the triangle's rows, architect 2026-10-26 ~12:40; the W
// 2026-11-04), each box
// is its text's whole-column width with the fill's pad either side (the text
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
    const int pad = cue_fill_pad_px();
    CueSegmentBoxes b;
    int x = cue_fill_lead_px();
    for (int s = 0; s < 3; ++s) {
        b.x0[s] = x;
        b.x1[s] = x;
        if (!present[s]) continue;
        b.x1[s] = x + 2 * pad + w[s];
        x = b.x1[s] + cue_segment_gap_px();
    }
    return b;
}

// THE FLAG'S SIDE: a live lane's cue, or one of the `h` view's three kinds of
// item — REMOVED, ADDED, CHANGED (HistoryDiffFlag, render.h; architect
// 2026-11-11, session 106). The history's sides pick the chip's style and
// the strike's meaning of their own (resolve_flag_face's history rows). The
// axis colors nothing: WARP and PHASE RESET, co-equal, wear one cue, the axis
// reading from the view the lane lists (the lane paints the active
// column's markers alone, the Warp and Phase Reset views, and the `h` view
// that column's half of the delta) and from the label (his one-label
// rule), so the resolver takes no kind.
enum class GuiFlagSide { Live, Added, Removed, Changed };

// The resolved paint of ONE cue (the palette block's marker-lane paragraph,
// render.h, owns the look): THE CHIP'S fill and outline (CueTrianglePass,
// the outline's staircase under the fill's), whether the cue PUBLISHES ITS
// STEM ENTRY (every selected cue, enabled or disabled, MarkerStem: the stem
// is the selection's), and whether its label is STRUCK (show_cue_label's bar). The
// label's other look is the segment's own (inert, or the text tone,
// materials.h's `text`); the selection never reaches it.
struct FlagFace {
    GuiColor fill;
    GuiColor outline;
    bool     stem;
    bool     struck;
};

// THE ONE RESOLVER for every cue — both marker columns and the `h` view's
// diff flags (architect 2026-11-05, his pick a2 of tmp/s42 on real captures:
// THE STEM IS THE SELECTION'S, reversing 2026-11-04's coincidence stem —
// "dropped, along with the rest of cool edit stuff except a few details like
// the triangle playhead"; 2026-10-26 ~15:10–15:30, THE STRIKE: "It's very
// odd having anything but red mean invalid. So I think what we should do is
// have coincident markers be shown as a strike through, which is actually
// effective because that's what happens to them. They become one").
// THE TABLE ("stem" the publication of the cue's stem entry, MarkerStem,
// painted wherever it is published; "label" the label's ink, the segment's
// `inert` on the history rows):
//   side      flag        fill          outline               stem      label      strike
//   LIVE      DISABLED    inactive_cue  inactive_cue_outline  selected  inactive   `strike`
//   LIVE      ENABLED     cue_fill      cue_outline           selected  text tone  `strike`
//   REMOVED   any         inactive_cue  inactive_cue_outline  selected  inactive   always
//   ADDED     any         cue_fill      cue_outline           selected  text tone  never
//   CHANGED   any         cue_fill      cue_outline           selected  text tone  per segment
// — warp or phase reset alike. THE LIVE ROWS: EVERY ENABLED CHIP IS
// THE `cue_fill`, THE TEXT TONE (Default's B5B5B5, the keys' glyphs' and the
// label's own ink, so a chip and its label are one color; architect
// 2026-11-12, session 114: "for all themes, the marker color should simply
// be the icon glyph color. This will unify"), in THE OUTLINE, Live's dark
// line 111111, selected or not; the accent `cue` paints no marker. EVERY
// SELECTED CUE, ENABLED OR DISABLED, ADDS THE STEM (architect
// 2026-11-11, session 106: "disabled markers also get the dark stem, same as
// enabled ones") — a solid one-W `cue_outline` line from the chip's apex
// row down the canvas's whole height on the cue's column, over the
// playhead's line (paint_marker_stems), the playhead on it or not; no
// other stem exists (Live paints none at rest). THE DISABLED AXIS IS THE
// CHIP'S AND THE LABEL'S ALONE, THE SELECTION THE STEM'S ALONE: the two
// never cross, so the stem stands through a disable toggle of a selected
// marker while its chip flips. A DISABLED CHIP IS SOLID GREY (architect
// 2026-11-05, "disabled marker gets dim label"): the `inactive_cue` 5D5D5D
// fill in THE DISABLED OUTLINE, `inactive_cue_outline` (architect
// 2026-11-12, session 114: the `cue_outline` moved 75 % of the way toward
// the lane's Desktop, Default's 242424 — theme_roles.h's DERIVED block), the
// playhead on it or not, selected or not; THE STEM STAYS `cue_outline` on
// every selected cue, disabled too — the stem is the one mark of the
// selection. THE CHIP CARRIES THE SELECTION, THE PLAYHEAD THE FOCUS (architect
// 2026-11-05: "only 1 playhead onscreen ever"): the focused member has no
// paint of its own — every click and toggle lands the playhead on it
// (land_playhead_on_marker) — and THE RESOLVER NEVER SEES THE PLAYHEAD.
// A LIVE CUE'S STRIKE: `strike` is its column's red set
// (warp_red_flag_set_cached / phase_reset_red_flag_set_cached — the
// coincident run, and on the warp column a label ref the render falls back
// from and the definitions of a label two live definitions hold, 2026-10-29).
// THE HISTORY ROWS (architect 2026-11-11, session 106: "disabled style and
// strike take on a different meaning in history — disabled means a marker
// was removed, and strike means a text was changed. a removed flag always
// has its text struck by definition"; "the diff should show as enabled"):
// `disabled` and `strike` are unread there — a REMOVED item wears the
// disabled style struck, an ADDED item the enabled style plain, a CHANGED
// item the enabled style with TWO SEGMENTS, THE STRIKE PER SEGMENT (architect
// 2026-11-11, session 107: "the label reference with a line through it, and
// then pass next to that"): its older text's segment takes the REMOVED row's
// strike (struck) and its newer text's the ADDED row's (plain), so the
// CHANGED face's own `struck` is false and no painter reads it
// (render_history_diff_flags); the stem the selection's as on the live
// rows. An inert label's text and its bar are the inactive label
// (CueSegment); the bar is part of the text (show_cue_label).
// THE INERT LABEL (architect 2026-11-02, mock C3; the inert look's reason
// 2026-10-10 ~12:20, "the arrow needs to be legible for me, but the text
// doesn't … since it's disabled, it's inert"): the one constant
// `inactive_cue` 5D5D5D (palette_file.h), the disabled chip's fill too.
// Every INERT label — a disabled cue's segments, a tie follower's cells and
// a removed history item's label — wears it, one look for inert cue text,
// selected or not. A
// DISABLED STRUCK CUE — the red set counts disabled rows
// (warp_red_flag_set_cached's (3)) — is the grey chip with its inactive
// label struck. "Text tone" is materials.h's `text` (Default's B5B5B5), the words'
// and the flags' one ink — every label on the lane's face with no fill,
// selected or not.
FlagFace resolve_flag_face(GuiFlagSide side, bool disabled, bool strike,
                           bool selected) {
    const GuiPalette& p = palette();
    switch (side) {
        case GuiFlagSide::Live:
            return disabled
                       ? FlagFace{p.inactive_cue, p.inactive_cue_outline,
                                  selected, strike}
                       : FlagFace{p.cue_fill, p.cue_outline, selected, strike};
        case GuiFlagSide::Removed:
            return FlagFace{p.inactive_cue, p.inactive_cue_outline, selected,
                            true};
        case GuiFlagSide::Added:
            return FlagFace{p.cue_fill, p.cue_outline, selected, false};
        case GuiFlagSide::Changed:
            return FlagFace{p.cue_fill, p.cue_outline, selected, false};
    }
    return FlagFace{p.cue_fill, p.cue_outline, selected, strike};
}

// ONE SEGMENT OF A CUE'S LABEL, as the pass paints it: its shaped run (the
// memo's shared handle, CueRun — null exactly when the segment is absent),
// its face box [x0, x1) in window x (the text at x0 plus the fill's pad),
// and its look — the inactive label or the text tone, the selection never
// reaching it (2026-11-02) — and whether it is STRUCK.
struct CueSegment {
    bool   present  = false;
    CueRun run;
    int    x0       = 0;
    int    x1       = 0;
    // INERT: a disabled marker's segment or a tie follower's cell, the
    // inactive label (`inactive_cue`, 2026-11-02).
    bool   inert    = false;
    // STRUCK (architect 2026-10-26): its side's `struck` (resolve_flag_face)
    // — every segment of a coincident live marker; in the `h` view a removed
    // item's one segment and a changed item's older one (2026-11-11,
    // session 107).
    bool   struck   = false;
};
// THE CUE LABEL'S ONE TEXT PAINTER: the run in `ink`, NO SHADOW (architect
// 2026-11-02, the well's part of the flat look, "no shadow anywhere"; its
// shadow one label quantum right and down, 2026-10-25 to 2026-11-02, is git
// history). Every label look on the lane's face goes through here —
// resting, struck, inert, and the flag editor's riding cells; the
// editor's FIELD text does not, and never strikes (the text there is being
// edited).
// THE STRIKE IS PART OF THE TEXT (architect 2026-10-26 ~15:10: "just the
// strike through on the text"): where `struck`, the run's strike bar
// (struck_run_bar, render.h — the one geometry, the `h` view's cue tooltip
// its other reader) in the text's ink. One cell rect a segment; nothing
// memoized.
static void show_cue_label(cairo_t* cr, const text_shape::ShapedRun& run,
                           double x, double baseline, GuiColor ink,
                           bool struck) {
    set_palette_source(cr, ink);
    text_shape::show_shaped_run(cr, run, x, baseline);
    if (struck)
        paint_ink_rect(cr, struck_run_bar(run.font, x, run.width_px, baseline),
                       ink);
}

// ONE CUE, laid out: its marker (or diff flag) index, its column in window
// x, its resolved face (the chip's colors and whether it publishes a stem
// entry), and its label's up-to-three segments in their left-to-right order.
struct CueDraw {
    int                       index = -1;
    int                       col   = 0;
    FlagFace                  face{};
    std::array<CueSegment, 3> seg{};
    // Whether segments 1 and 2 are the marker's bound cells (the live lanes)
    // or plain halves of one item (the `h` view's changed item, its older
    // and newer texts, architect 2026-11-11, session 107), whose label
    // publishes no cell boundary: no bracket lives in a commit, so the
    // publication collapses both boundaries onto the label's right edge
    // (FlagHitRect's contract) and hit_test_flag_cell answers Payload.
    bool                      segments_are_cells = true;
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
// box from its own chip's left edge to its text's end, so a later cue
// masks an earlier label only across its own extent, a selected label as at
// rest (2026-11-02); then
// the chips LEFT TO RIGHT, each in its face's fill and outline
// (resolve_flag_face, 2026-11-04); then the
// publication — one FlagHitRect per cue (its label box as painted and its
// chip, both clipped to the waveform's columns),
// and one MarkerStem per cue whose face says `stem` — every SELECTED cue,
// enabled or disabled, an `h` item one cue — whose column is a waveform
// column (paint_marker_stems paints every entry, architect 2026-11-05).
// `lane` is the
// marker lane, `x0` / `w` the waveform's columns the pass is clipped to (the
// caller set the clip). Cues arrive in store order and are ordered here by
// column (stable).
// A label segment's TEXT ORIGIN, the pen x its run is shown at: its box
// start plus the fill pad. One expression for the label pass and the
// publication (FlagHitRect::text_x), so the cue tooltip's text lines up on
// the pen the label was really drawn at.
static double cue_segment_text_x(const CueSegment& s, int pad) {
    return static_cast<double>(s.x0 + pad);
}

static void paint_cues(cairo_t* cr, const GuiRect& lane, int x0, int w,
                       std::vector<CueDraw>& cues,
                       std::vector<FlagHitRect>* out_hit_rects,
                       std::vector<MarkerStem>* out_stems) {
    std::stable_sort(cues.begin(), cues.end(),
                     [](const CueDraw& a, const CueDraw& b) {
                         return a.col < b.col;
                     });
    const int pad      = cue_fill_pad_px();   // the label's fill pad
    const int half     = cue_triangle_half_w_px();   // the chip's
    const int fill_h   = cue_fill_h_px();
    // The chip's rows (render.h's cue_chip_rows): the outline's staircase
    // from the lane's rows less the chip's height, its apex on the lane's
    // last row, and the fill's top t rows under the chip's.
    const int chip_top = lane.y + cue_above_chip_h_px();
    const int chip_h   = cue_chip_h_px();
    const int fill_top = chip_top + cue_chip_top_rows() * cue_triangle_unit_px();
    const double base  = static_cast<double>(lane.y + cue_baseline_px());
    const GuiPalette& pal = palette();
    const int n = static_cast<int>(cues.size());
    // Each label's OCCLUDING BOX [lo, hi): from its own chip's left edge
    // (its column less the chip's half width) to its last segment's end;
    // empty where no segment paints. The text stands where its segments put
    // it; the box only reaches back to the chip. The three working
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
        // whatever its state: A SELECTED SEGMENT TAKES NO FILL and no ink of
        // its own (architect 2026-11-02, render.h's THE STATES).
        // THE BOX IS THE LANE'S FLAT FACE (2026-10-29; Live's 2A2A2A since
        // 2026-11-03, materials.h's `lane_face`), the one color of every
        // pixel of the lane, so the box masks earlier text exactly.
        {
            for (const auto& sp : spans)
                paint_cell_rect(cr,
                                GuiRect{sp.first, lane.y, sp.second - sp.first,
                                        fill_h},
                                pal.lane_face);
        }
        for (const CueSegment& s : c.seg) {
            if (!s.present) continue;
            const double tx = cue_segment_text_x(s, pad);
            // The label's ink (resolve_flag_face's table): the inactive
            // label or the text tone, selected or not; a struck segment's
            // bar in the same ink (show_cue_label).
            show_cue_label(cr, *s.run, tx, base,
                           s.inert ? pal.inactive_cue : pal.text,
                           s.struck);
        }
        cairo_restore(cr);
    }

    // THE CHIPS, LEFT TO RIGHT, over every label: ONE PASS on one
    // prepared target (CueTrianglePass, cool_edit_paint.h), UNDER A CLIP OF
    // THE LANE'S ROWS over the waveform's columns (the caller's columns, so
    // the clip only gains its rows; whole pixels, so the stamp's identity
    // holds): the chip stands on the lane's last rows, its outline's
    // staircase under its fill's, so a right chip's outline cuts its left
    // neighbour's chip as its fill does. Its contract
    // holds here: every label above closed its own save / restore, the clip
    // is set before begin() and released after end(), and the pass's cairo
    // roads keep theirs, so the clip, the matrix, the operator and the
    // group are constant throughout the pass.
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(x0), static_cast<double>(lane.y),
                    static_cast<double>(w > 0 ? w : 0),
                    static_cast<double>(lane.h));
    cairo_clip(cr);
    CueTrianglePass triangles;
    triangles.begin(cr);
    for (const CueDraw& c : cues)
        triangles.triangle(c.col, fill_top, c.face.fill, c.face.outline);
    triangles.end();
    cairo_restore(cr);

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
            // The label's text origin: the first painted segment's pen x,
            // the very `tx` the label pass above hands show_cue_label
            // (FlagHitRect::text_x, the cue tooltip's seat).
            if (label_on) {
                for (const CueSegment& s : c.seg) {
                    if (!s.present) continue;
                    r.has_text_x = true;
                    r.text_x     = cue_segment_text_x(s, pad);
                    break;
                }
            }
            // The chip's box: its painted columns
            // (cue_triangle_half_w_px, cue_triangle_reach_right_px) over its
            // rows.
            FlagHitRect tri;
            tri.x = static_cast<double>(c.col - half);
            tri.y = static_cast<double>(chip_top);
            tri.w = static_cast<double>(half + cue_triangle_reach_right_px());
            tri.h = static_cast<double>(chip_h);
            if (clip_hit_rect_to_waveform_columns(tri, x0, w)) {
                r.tri_x = tri.x;
                r.tri_y = tri.y;
                r.tri_w = tri.w;
                r.tri_h = tri.h;
            }
            if (label_on || r.tri_w > 0.0) out_hit_rects->push_back(r);
        }
        if (out_stems && c.face.stem &&
            stem_column_on_waveform(c.col - x0, w))
            out_stems->push_back(
                MarkerStem{c.index, static_cast<double>(c.col)});
    }
}

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
// question (the warp side's effective_disabled, the phase-reset side's bare
// bool); the lambdas survive because the
// columns hold different marker types.
template <typename MarkerVec, typename LabelFn, typename DisabledFn,
          typename CellsFn>
void render_flag_boxes_impl(
    cairo_t* cr,
    GuiRect top_strip_area,
    FlagLaneRects lanes,
    int waveform_width,
    const MarkerVec& markers,
    long long viewport_start_sample,
    long long viewport_end_sample,
    long long viewport_lattice_origin,
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
    bool iteration_on) {
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
    // THE LABEL FACE for the whole pass (gui_font.h: Bold Label 11 W, 22
    // device px on the tablet),
    // every run carrying it (text_shape.h).
    const GuiFont font = gui_font(GuiFace::Label);
    // The pass's kept scratch (CuePassScratch), emptied for this pass.
    std::vector<CueDraw>& cues = g_cue_pass_scratch.cues;
    cues.clear();

    iterate_visible_flags_impl(lanes.columns_x, waveform_width, markers,
                               viewport_start_sample, viewport_end_sample,
                               viewport_lattice_origin,
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
            // THE SELECTION IS THE CHIP'S AND THE STEM'S (architect
            // 2026-11-05, resolve_flag_face's table): every selected marker,
            // enabled or disabled (2026-11-11), publishes its stem entry
            // (MarkerStem); no segment of the label carries the selection.
            c.face = resolve_flag_face(GuiFlagSide::Live, dis, red, sel);

            // THE SEGMENTS: the payload label (the column's composer, the cut
            // inside it), then the two bound cells where they paint — each
            // shaped on the label face and boxed by the one layout
            // (cue_segment_boxes). A TIE FOLLOWER'S CELLS wear the inactive
            // label (architect 2026-09-19: the leader's numbers, not this
            // marker's to author); a disabled marker's every segment does.
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
            for (int s = 0; s < 3; ++s) {
                CueSegment& seg = c.seg[static_cast<std::size_t>(s)];
                if (!present[s]) continue;
                seg.present  = true;
                seg.run      = s == 0 ? payload
                             : s == 1 ? cl.lower_run : cl.upper_run;
                seg.x0       = c.col + boxes.x0[s];
                seg.x1       = c.col + boxes.x1[s];
                seg.inert = dis || (s > 0 && cells.follower);
                // A COINCIDENT MARKER'S EVERY SEGMENT IS STRUCK (its face,
                // resolve_flag_face's table), the bound cells with the payload.
                seg.struck = c.face.struck;
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
            // Every other kind edits somewhere else entirely (the dialog
            // kinds ride the settings editor's buffer and paint in the modal
            // box), so the marker lane suppresses nothing for them.
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
                  long long viewport_lattice_origin,
                  int sample_rate,
                  const std::set<int>& selected_set,
                  const std::set<int>& red_set,
                  bool iteration_on,
                  std::vector<FlagHitRect>* out_hit_rects,
                  std::vector<MarkerStem>* out_stems,
                  const std::vector<WarpFrameMapSegment>* warp_frame_map,
                  const DragOverlay* drag_overlay,
                  SuppressedBox suppressed) {
    render_flag_boxes_impl(
        cr, top_strip_area, lanes, waveform_width, markers,
        viewport_start_sample, viewport_end_sample,
        viewport_lattice_origin, sample_rate,
        selected_set, red_set,
        // THE PAINTED COMPOSER (flag_display_text, render.h): the tempo's
        // base and its whole chain, the scale capped to `*N.NN`, and the
        // bounds are the two cells beside it. The editor seeds from the UNCUT
        // sibling, flag_text, so nothing a commit reads passes through here.
        [&](int i) { return flag_display_text(markers, i); },
        // The warp column's disabled verdict is the one participation owner's
        // (the marker's own flag since 2026-10-29).
        [&](int i) { return effective_disabled(markers, i); },
        // The two bound cells, on exactly the markers the sweep reads
        // (warp_iter_cells above), and none outside the mode.
        [&](int i) { return warp_iter_cells(markers, i, iteration_on); },
        out_hit_rects, out_stems, warp_frame_map, drag_overlay,
        suppressed, iteration_on);
}

void render_phase_reset_flags(cairo_t* cr,
                            GuiRect top_strip_area,
                            FlagLaneRects lanes,
                            int waveform_width,
                            const std::vector<GuiPhaseResetMarker>& phase_resets,
                            long long viewport_start_sample,
                            long long viewport_end_sample,
                            long long viewport_lattice_origin,
                            int sample_rate,
                            const std::set<int>& selected_set,
                            const std::set<int>& red_set,
                            bool iteration_on,
                            std::vector<FlagHitRect>* out_hit_rects,
                            std::vector<MarkerStem>* out_stems,
                            const std::vector<WarpFrameMapSegment>* warp_frame_map,
                            const DragOverlay* drag_overlay,
                            SuppressedBox suppressed) {
    render_flag_boxes_impl(
        cr, top_strip_area, lanes, waveform_width,
        phase_resets,
        viewport_start_sample, viewport_end_sample,
        viewport_lattice_origin, sample_rate,
        selected_set, red_set,
        // A phase reset authors no payload, so its flag carries the display-only
        // token (render.h owns it and what it reads). It reaches the pass
        // whole: the shared byte cap is gone (2026-09-19) and this column has
        // nothing to cut — the token is five bytes by ruling.
        [&](int) { return std::string(kPhaseResetLaneToken); },
        // The bool is the whole verdict on this column.
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
        iteration_on);
}

void render_history_diff_flags(
        cairo_t* cr,
        GuiRect top_strip_area,
        FlagLaneRects lanes,
        int waveform_width,
        const std::vector<HistoryDiffFlag>& flags,
        long long viewport_start_sample,
        long long viewport_end_sample,
        long long viewport_lattice_origin,
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
    clip_to_waveform_columns(cr, lanes.columns_x, waveform_width,
                             top_strip_area.y, top_strip_area.h);
    // The label face for the pass, exactly as render_flag_boxes_impl names
    // it (gui_font.h).
    const GuiFont font = gui_font(GuiFace::Label);

    // THE LEFT CULL'S BOUND, DERIVED FROM THIS COMMIT'S OWN TEXT rather than
    // from the marker lane's own worst case, because THESE LABELS ARE NOT CUT.
    // The live lane truncates because a marker label is free text the user types
    // and a runaway one would swamp its neighbours; a diff flag's label is the
    // SIDECAR'S OWN TOKEN (history_diff_label), and cutting it would throw
    // away the one thing the flag exists to show — a `chorus=1.05` cut at six
    // bytes would read `chorus...`, which names the label and not the value.
    // So the text prints whole and the bound follows
    // it: one byte per em is the same over-estimate marker_flag_max_width_px
    // makes (no ASCII glyph on this face advances a full em at these sizes), and
    // an over-estimate is exactly what a cull bound must be. An item's label
    // is both its sides' texts (an absent side's is empty).
    double widest_bytes = 0.0;
    for (const HistoryDiffFlag& f : flags) {
        const double n = static_cast<double>(f.then_label.size() +
                                             f.now_label.size());
        if (n > widest_bytes) widest_bytes = n;
    }
    const double cull_width_px =
        widest_bytes * gui_font_advance_bound_px(font) +
        // The label's lead, both segments' pads and the gap between a
        // changed item's two. A bound must never under-state, and the
        // widest item in a commit may be a changed one.
        static_cast<double>(cue_fill_lead_px() + 4 * cue_fill_pad_px() +
                            cue_segment_gap_px());

    // The pass's kept scratch (CuePassScratch), emptied for this pass.
    std::vector<CueDraw>& cues = g_cue_pass_scratch.cues;
    cues.clear();

    iterate_visible_flags_impl(
        lanes.columns_x, waveform_width, flags,
        viewport_start_sample, viewport_end_sample,
        viewport_lattice_origin,
        warp_frame_map,
        // NO DRAG OVERLAY: the mode consumes every authoring gesture, so no
        // marker drag can be in flight while this pass runs — and a diff flag is
        // not a marker in any store, so nothing could index it anyway.
        /*drag_overlay=*/nullptr,
        cull_width_px,
        [&](int i, double left_x) {
            const HistoryDiffFlag& f = flags[static_cast<std::size_t>(i)];
            // A flag with neither half is not constructible by the builder
            // (rebuild_history_diff_flags); the guard keeps a degenerate one
            // from publishing a zero-width claim.
            if (!f.removed && !f.added) return;
            // THE MODE'S OWN FOCUS AND ITS OWN SELECTION, never the live one:
            // either SELECTS the item (the declaration says why), and a
            // selected item publishes its stem entry at its one column
            // (resolve_flag_face's table, the live lane's rule).
            const bool selected_item =
                (i == focus_index) || (selected.count(i) != 0);
            // THE ITEM'S STYLE (architect 2026-11-11, session 106;
            // resolve_flag_face's history rows): removed, added or changed —
            // never its lines' disable bits, which the labels' `#` shows.
            const GuiFlagSide side = !f.added   ? GuiFlagSide::Removed
                                     : !f.removed ? GuiFlagSide::Added
                                                  : GuiFlagSide::Changed;
            // ONE CUE, ITS SIDES' LABELS AS SEGMENTS (architect 2026-11-11,
            // session 107, restoring the same-frame label of 2026-10-26: "the
            // label reference with a line through it, and then pass next to
            // that"): the older side's text first, then the newer side's, in
            // the live lane's box layout (cue_segment_boxes) — the segment
            // gap between a changed item's two. Each segment STRUCK on its
            // own side's row (resolve_flag_face: the older text's the
            // REMOVED row's, always; the newer text's the ADDED row's,
            // never); INERT on a removed item alone (the disabled style's dim
            // label), a changed item's two in the text tone (the enabled
            // style). No segment is a bound cell (segments_are_cells).
            CueDraw c;
            c.index = i;
            c.col   = static_cast<int>(std::nearbyint(left_x));
            c.face  = resolve_flag_face(side, /*disabled=*/false,
                                        /*strike=*/false, selected_item);
            c.segments_are_cells = false;
            struct SideText {
                const std::string* text;
                GuiFlagSide        side;
            };
            SideText texts[2] = {};
            int count = 0;
            if (f.removed)
                texts[count++] = SideText{&f.then_label, GuiFlagSide::Removed};
            if (f.added)
                texts[count++] = SideText{&f.now_label, GuiFlagSide::Added};
            CueRun runs[2];
            bool present[3] = {false, false, false};
            int  widths[3]  = {0, 0, 0};
            for (int s = 0; s < count; ++s) {
                runs[s]    = text_shape::memoized_run(font, *texts[s].text);
                present[s] = true;
                widths[s]  = static_cast<int>(std::nearbyint(runs[s]->width_px));
            }
            const CueSegmentBoxes boxes = cue_segment_boxes(widths, present);
            for (int s = 0; s < count; ++s) {
                CueSegment& seg = c.seg[static_cast<std::size_t>(s)];
                seg.present = true;
                seg.run     = runs[s];
                seg.x0      = c.col + boxes.x0[s];
                seg.x1      = c.col + boxes.x1[s];
                seg.inert   = (side == GuiFlagSide::Removed);
                seg.struck  = resolve_flag_face(texts[s].side,
                                                /*disabled=*/false,
                                                /*strike=*/false,
                                                selected_item)
                                  .struck;
            }
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
    // THE SEATS BELOW START AT CONSTRUCTION STATE (the k…Default constants,
    // device_config.h), which gui_main's startup seeding overwrites from the
    // read config before the first layout; none is a first-run value — the
    // shipped first-run value is the defaults file's (device_config.h's THE
    // FIRST-RUN VALUES, 2026-11-09).
    // THE KEY'S HEIGHT in W (render.h's `key_height`
    // seat), set by set_key_height_w from the same two application
    // points.
    int    g_key_height_w = kKeyHeightDefaultW;
    // THE KEY'S FACE WIDTH in W (render.h's `key_width` seat), set by
    // set_key_width_w from the same two application points.
    int    g_key_width_w = kKeyWidthDefaultW;
    // THE RUN GAP in W (render.h's `run_gap` seat), set by set_run_gap_w
    // from the same two application points.
    int    g_run_gap_w = kRunGapDefaultW;
    // THE ON-SCREEN KEYBOARD'S KEY BOX in W (render.h's
    // `osk_key_w` / `osk_key_h` seat), set by set_osk_key_size_w from the
    // same two application points.
    int    g_osk_key_width_w = kOskKeyWDefaultW;
    int    g_osk_key_height_w = kOskKeyHDefaultW;
    // THE ON-SCREEN KEYBOARD'S CAP SIZE, its em in W (render.h's
    // `osk_font_px` seat), set by set_osk_font_em_w from the same two
    // application points.
    int    g_osk_font_em_w = kOskFontPxDefaultW;
    // THE WAVEFORM PLATE'S GROW in quarters of a device px (render.h's
    // `waveform_grow` seat, a permanent key), set by
    // set_waveform_grow_quarters from the same two application points.
    int    g_waveform_grow_quarters = kWaveformGrowDefaultQuarters;
} // namespace
// A QUARTER OF A DEVICE PX IS ONE SUBCELL: the key's lattice is the grid's,
// so the seat's number is the plate shader's GROW with no conversion.
static_assert(kWaveformGrowQuartersPerPx == kWaveformSubcells,
              "the waveform_grow key counts the plate's subcells");

void   set_gui_scale_percent(int percent) { g_gui_scale_percent = percent; }
void   set_key_height_w(int w) {
    g_key_height_w = w;
}
int    key_height_w() { return g_key_height_w; }
void   set_key_width_w(int w) {
    g_key_width_w = w;
}
int    key_width_w() { return g_key_width_w; }
void   set_run_gap_w(int w) {
    g_run_gap_w = w;
}
int    run_gap_w() { return g_run_gap_w; }
void   set_osk_key_size_w(int w_w, int h_w) {
    g_osk_key_width_w = w_w;
    g_osk_key_height_w = h_w;
}
int    osk_key_width_w() { return g_osk_key_width_w; }
int    osk_key_height_w() { return g_osk_key_height_w; }
void   set_osk_font_em_w(int em_w) { g_osk_font_em_w = em_w; }
int    osk_font_em_w() { return g_osk_font_em_w; }
void   set_waveform_grow_quarters(int quarters) {
    g_waveform_grow_quarters = quarters;
}
int    waveform_grow_quarters() { return g_waveform_grow_quarters; }

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
    // The installed theme's row, a copy (installed_theme, render.h): the
    // compiled reference row until the first install.
    GuiTheme          g_theme{std::string(kGuiReferenceThemeSlug),
                              std::string(kGuiThemeGroupSystem),
                              kGuiReferenceThemeTones};
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
const GuiTheme& installed_theme() { return g_theme; }
WaveformPlateInks waveform_plate_inks() {
    // The window's words (the struct's declaration, render.h), the installed
    // theme's two tones.
    const GuiThemeTones& t = installed_theme().tones;
    return WaveformPlateInks{display_transform::display_rgb(t.waveform_ink),
                             display_transform::display_rgb(t.waveform_core)};
}

void install_palette(const GuiTheme& theme) {
    // The contract and the apply shape the live caller owes are at the
    // declaration (render.h). THE PROGRAM'S THIRTEEN off the palette's role
    // table (palette_file.h), THE CHROME'S THIRTY off the chrome's
    // (theme_file.h), each member its row's tone, and THE PANEL'S FACE the
    // theme's ground (the flat look, materials.h's head) — the tables
    // covering the struct exactly (palette_file.cpp), so a role cannot be
    // valued and not painted; the row copied for the word readers
    // (installed_theme).
    g_theme = theme;
    for (const GuiPaletteRole& r : kGuiPaletteRoles)
        g_palette.*(r.member) = hex(theme.tones.*(r.tone));
    for (const GuiThemeRole& r : kGuiThemeRoles)
        g_palette.*(r.member) = hex(theme.tones.*(r.tone));
    g_palette.face = hex(theme.tones.ground);
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
    // render.h — the whole ruling).
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
    const int u      = cue_editor_outline_px();   // the field's outline
    const int fill_h = cue_fill_h_px();

    cairo_save(cr);
    // The label face, the cues' own (gui_font.h).
    const GuiFont font = gui_font(GuiFace::Label);

    // THE FULL, UNTRUNCATED pending — the unroll's whole point. The scale cap
    // is a PAINTED-FLAG rule; an editor shows what it is editing.
    const text_shape::ShapedRun run =
        text_shape::shape_text_run(font, ed.pending);
    std::vector<double> byte_x =
        text_shape::byte_offsets_px(run, ed.pending.size());

    // The segment box's fill pad either side of the run (cue_segment_boxes).
    const int pad_l = cue_fill_pad_px();
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
    // The pad is the fill's (render.h's cue_fill_pad_px, one W, 2 device px
    // on the tablet) and THE CARET ONE W (render.h's line rule, hairline_px, 2026-11-04;
    // held to the pad, so at the defaults the whole pad), so the borrow takes
    // the caret's columns out of the right pad: a caret at end-of-text stands
    // in the pad, inside the box — visible ink, not a lost one, so the travel
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
        map, basis.vp_start, basis.spp, basis.lattice_origin);
    const GuiRect area = waveform_area(app);
    // THE WAVEFORM'S COLUMNS ARE THIS BOX'S CLIP (architect 2026-09-26, the
    // flag pass's own rule — clip_to_waveform_columns): the width is the one
    // the column above was mapped against (the item basis, which is the
    // cached flag pass's plate width), so the open editor is cut off at
    // exactly the column its resting label is.
    // It holds for everything this painter draws — the field, its text, the
    // caret and the riding segments — until the closing restore.
    const int clip_w = basis.area_w > 0 ? basis.area_w : area.w;
    // (The cue's chip and its stem stay the flag pass's and
    // paint_marker_stems', unchanged by the open. A refusal recolors
    // neither, architect 2026-10-03.)
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
    // Esc, A PAN AND A REOPEN (architect 2026-10-25): the marker-lane editors
    // are modals since that day, so nothing pans while one stands, and the
    // open does not pan to it either — the field travels with its marker,
    // the same answer the row gives for a label half off the edge at rest.
    const int bx = area.x + col + anchor_off;

    // The text VIEWPORT inside the box: the band the run is clipped to, and the
    // width the view offset is measured against. The caret column belongs to it
    // (a caret at the end must be inside the clip to be seen), and THIS IS
    // WHERE THE BORROW IS SPENT, on every kind: the viewport reaches one column
    // INTO the right pad — the column the box above deliberately does not buy —
    // so a caret at end-of-text is inside the clip with the run standing
    // exactly where the committed text stands. The caret one W (held to the
    // pad), the borrow takes those columns of the pad and the viewport ends
    // the rest of the pad short of the box's right edge.
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
    // which keep their resting look: the inactive label on a disabled
    // marker and a tie follower's cells (2026-09-19), else the text tone — struck on a coincident
    // marker (below).
    const bool dis = phase ? pmv[static_cast<size_t>(idx)].disabled
                           : effective_disabled(mv, idx);
    // THE MARKER'S STRIKE (resolve_flag_face's table, 2026-10-26), off the
    // same memoized red-flag set the flag pass reads for this column (the
    // flag cache's rebuild, waveform_cache.cpp), so the riding segments and
    // the resting label agree on it — disabled or not, as there; the FIELD
    // itself never strikes (show_cue_label's head).
    const bool struck =
        phase ? phase_reset_red_flag_set_cached(
                    app, audio.sample_rate(),
                    static_cast<long>(audio.total_frames()))
                        .red.count(idx) > 0
              : warp_red_flag_set_cached(
                    app, audio.sample_rate(),
                    static_cast<long>(audio.total_frames()))
                        .red.count(idx) > 0;
    // THE FIELD IS AN EDIT FIELD (architect 2026-10-09 ~12:15, reversing
    // 2026-10-07 ~09:45's "the flag in its selected face, no box and no
    // frame"; its look 2026-11-01 night, M5g: "the box's tones as mocked"):
    // THE GROUND `ground` (Default's 242424) inside a `text` (Default's B5B5B5) outline, the text
    // and the caret the chrome's `field_text` (the text tone) — on every
    // state of the marker, disabled included (the chip carries the
    // state). WHY: the field is the legibility guarantee ("when you're
    // looking very closely at a label it has to be legible"), and THE
    // SELECTED SUBSTRING takes the chrome's SELECTED PAIR, `selected_text`
    // 111111 on `selected_fill` 637E86 (Live's browser selection,
    // materials.h's `selection_fill` / `selection_text`), the ordinary highlight
    // — the pair that exists "precisely to avoid this problem" — part of the
    // chrome, as every selection in the product is. A COINCIDENT
    // MARKER'S FIELD IS THE SAME FIELD, its text and caret the field's own,
    // unstruck; `struck` marks its riding segments alone.
    const GuiPalette& pal = palette();
    const GuiColor field_fill    = pal.ground;
    const GuiColor field_outline = pal.text;
    const GuiColor field_ink     = pal.field_text;
    const GuiColor sel_fill   = pal.selected_fill;
    const GuiColor sel_text   = pal.selected_text;
    const int  field_rank = flag_box_rank(field_cell);
    const IterCellText ride_text =
        field_rank >= flag_box_rank(MarkerCell::Upper) ? IterCellText{}
        : phase ? phase_iter_cells(pmv, idx, iteration_on)
                : warp_iter_cells(mv, idx, iteration_on);
    const bool ride_cells = ride_text.present;

    // 1. THE FIELD IS A BOX OVER THE WHOLE LANE (cue_fill_h_px, the selected
    //    label's box; architect 2026-10-09 ~21:00; the lane's rows ARE the
    //    box's since 2026-11-01 night, render.h's marker_lane_h_px): the
    //    segment's box — its run and the fill's pad either side, the text
    //    standing exactly where the resting label's does — with THE
    //    EDITOR'S OUTLINE (render.h's cue_editor_outline_px, one W, 2 device
    //    px on the tablet) in `text` round it, outside the pads on the left and
    //    right and on the lane's first and last outline rows, the ground
    //    inside. IT PAINTS OVER THE LANE — the
    //    triangle's right edge and any neighbouring label it reaches
    //    (architect 2026-10-09: "it may overlap with the flag and other
    //    elements, that's fine"). A refused Enter recolors nothing
    //    (text_editor::refuse selects the whole text; the owner's card says
    //    why). Since the editor opens on any store index
    //    (enter_top_flag_edit), disabled included, a disabled marker's field
    //    is this same box, never the inactive grey.
    const GuiRect field_box{bx - u, lane.y, box_w + 2 * u, fill_h};
    paint_cell_rect(cr, field_box, field_outline);
    paint_cell_rect(cr, GuiRect{field_box.x + u, field_box.y + u,
                                field_box.w - 2 * u, field_box.h - 2 * u},
                    field_fill);

    // TWO SETS OF ROWS. THE TEXT VIEWPORT IS THE FIELD'S WHOLE INNER HEIGHT,
    // inside the outline — so a descender or a history bracket below the
    // lane's baseline paints whole. THE CARET AND THE SELECTION BAND ARE THE
    // LEGAL SET'S BAND (architect 2026-11-01 night, M5g / M5h: the warp
    // line's grammar's ink, the x-height band centered in it; render.h's
    // EDITING paragraph): the band's rows are read off the one derivation
    // (cue_band_top_px, cue_band_h_px, render.h), never re-derived here — on
    // the tablet rows 8 .. 29 of the inner 2 .. 36, six rows of field above
    // and seven below.
    const int view_y = lane.y + u;
    const int view_h = fill_h - 2 * u;
    const int band_y = lane.y + cue_band_top_px();
    const int band_h = cue_band_h_px();

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
    // Each segment wears its resting look — its box in the lane's flat face
    // through the well's columns, the ground past them, the text
    // in the text tone or the inactive label, struck on a coincident marker —
    // and none takes
    // the selected look while the field stands, each open having seated the
    // selection axis on the cell it edits.
    //
    // IT IS PUBLISHED AS A SECOND RECT, NEVER FOLDED INTO `box`, and that rect
    // is a FLAG HIT RECT — the same shape and the same two boundaries the
    // flag pass publishes for a resting run, because THE RIDING CELLS ARE THE
    // MARKER'S OWN CELLS FOR THE POINTER TOO (architect 2026-09-05), so the
    // run answers "which marker, which cell" exactly as the resting boxes
    // answer it — one walk, one idiom, no second derivation (the contract is
    // at FlagEditorBox::riding_cells). A PRESS on one is veiled since
    // 2026-10-25 — a press outside the field, which the editor's modal veil
    // consumes, the editor standing (on_button_press). Folding it
    // into `box` would be the wrong union all the same: the caret / text-drag
    // claim seats a caret for ANY press inside `box`, so a run folded in would map presses
    // on painted cell ink to payload bytes and promise text
    // editing where none is.
    // WHAT RIDES, BY RANK: every box standing right of the field's own. The
    // UPPER-bound field's rank is the last, so nothing rides under it. THE
    // STORE BELOW IS THE FIELD'S OWN COLUMN since 2026-09-09, when the bound
    // editor became both columns': the payload editor is warp-only by its
    // open gates, and `phase` already answered that question for the box
    // above.
    if (ride_cells) {
        const int well_end = well_columns(app).end;
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
        const int gap    = cue_segment_gap_px();
        const int run_x0 = bx + box_w;
        // THE RUN IS NOT CLIPPED (architect 2026-10-10 ~19:00): the riding
        // cells paint where the paint order puts them. The editor paints
        // after the lane pass, so the run stands over a neighbour's label and
        // triangle as the field itself does (render.h's EDITING paragraph,
        // "it may overlap with the flag and other elements, that's fine").
        int cursor_x = run_x0;
        const int lower_seam = cursor_x;
        // The field's outline columns stand in the first gap's first columns
        // (field_box reaches one outline past the pads), so the riding face
        // starts after it: the run stays where the resting label's is and the
        // outline is never painted over.
        const int face_x0 = run_x0 + u;
        const auto ride = [&](const text_shape::ShapedRun& seg_run, int seg_w) {
            const int x0 = cursor_x + gap;
            const int w  = pad_l + seg_w + pad_r;
            // The face box over the gap and the segment, as the resting
            // label's one box covers its gaps — the lane's flat `lane_face`
            // (paint_cues' box) through the well's columns, the ground past
            // them (the run is not clipped, so a ride near the right edge
            // reaches the margin).
            const int fx = std::max(cursor_x, face_x0);
            const int fx1 = x0 + w;
            const int lane1 = std::clamp(well_end, fx, std::max(fx, fx1));
            if (lane1 > fx)
                paint_cell_rect(cr, GuiRect{fx, lane.y, lane1 - fx, fill_h},
                                pal.lane_face);
            if (fx1 > lane1)
                paint_cell_rect(cr, GuiRect{lane1, lane.y, fx1 - lane1, fill_h},
                                pal.face);
            const double tx = static_cast<double>(x0 + pad_l);
            // The resting label's ink: the inactive label, else the text
            // tone (resolve_flag_face's table; materials.h's text tones),
            // struck on a coincident marker.
            show_cue_label(cr, seg_run, tx, baseline,
                           cell_dis ? pal.inactive_cue : pal.text,
                           struck);
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
